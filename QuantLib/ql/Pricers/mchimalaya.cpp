
/*
 Copyright (C) 2000, 2001, 2002, 2003 RiskMap srl

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it under the
 terms of the QuantLib license.  You should have received a copy of the
 license along with this program; if not, please email quantlib-dev@lists.sf.net
 The license is also available online at http://quantlib.org/html/license.html

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/MonteCarlo/mctypedefs.hpp>
#include <ql/Pricers/mchimalaya.hpp>

namespace QuantLib {

    namespace {

        class HimalayaPathPricer : public PathPricer<MultiPath> {
          public:
            HimalayaPathPricer(const std::vector<double>& underlying,
                               double strike,
                               const RelinkableHandle<TermStructure>& 
                                                                discountTS)
            : PathPricer<MultiPath>(discountTS),
              underlying_(underlying), strike_(strike) {
                for (Size j=0; j<underlying_.size(); j++) {
                    QL_REQUIRE(underlying_[j]>0.0,
                               "HimalayaPathPricer: "
                               "underlying less/equal zero not allowed");
                    QL_REQUIRE(strike>=0.0,
                               "HimalayaPathPricer: "
                               "strike less than zero not allowed");
                }
            }

            double operator()(const MultiPath& multiPath) const {
                Size numAssets = multiPath.assetNumber();
                Size numSteps = multiPath.pathSize();
                QL_REQUIRE(underlying_.size() == numAssets,
                           "HimalayaPathPricer: "
                           "the multi-path must contain "
                           + SizeFormatter::toString(underlying_.size()) + 
                           " assets");
                QL_REQUIRE(numAssets>0,
                           "HimalayaPathPricer: no asset given");

                std::vector<double> prices(underlying_);
                double averagePrice = 0;
                std::vector<bool> remainingAssets(numAssets, true);
                double bestPrice;
                Size removeAsset, i, j;
                Size fixings = numSteps;
                if (multiPath[0].timeGrid().mandatoryTimes()[0] == 0.0) {
                    bestPrice = 0.0;
                    // dummy assignement to avoid compiler warning
                    removeAsset=0;
                    for (j = 0; j < numAssets; j++) {
                        if (prices[j] >= bestPrice) {
                            bestPrice = prices[j];
                            removeAsset = j;
                        }
                    }
                    remainingAssets[removeAsset] = false;
                    averagePrice += bestPrice;
                    fixings = numSteps+1;
                }
                for (i = 0; i < numSteps; i++) {
                    bestPrice = 0.0;
                    // dummy assignement to avoid compiler warning
                    removeAsset=0;
                    for (j = 0; j < numAssets; j++) {
                        if (remainingAssets[j]) {
                            prices[j] *= QL_EXP(multiPath[j][i]);
                            if (prices[j] >= bestPrice) {
                                bestPrice = prices[j];
                                removeAsset = j;
                            }
                        }
                    }
                    remainingAssets[removeAsset] = false;
                    averagePrice += bestPrice;
                }
                averagePrice /= QL_MIN(fixings, numAssets);
                double optPrice = QL_MAX(averagePrice - strike_, 0.0);

                return discountTS_->discount(multiPath[0].timeGrid().back())
                    * optPrice;
            }

          private:
            Option::Type type_;
            std::vector<double> underlying_;
            double strike_;
        };

    }

    McHimalaya::McHimalaya(
               const std::vector<double>& underlying,
               const std::vector<RelinkableHandle<TermStructure> >& 
                                                             dividendYield,
               const RelinkableHandle<TermStructure>& riskFreeRate,
               const std::vector<RelinkableHandle<BlackVolTermStructure> >& 
                                                             volatilities,
               const Matrix& correlation,
               double strike,
               const std::vector<Time>& times,
               long seed) {

        Size  n = correlation.rows();
        QL_REQUIRE(correlation.columns() == n,
                   "McHimalaya: correlation matrix not square");
        QL_REQUIRE(underlying.size() == n,
                   "McHimalaya: underlying size does not match that of"
                   " correlation matrix");
        QL_REQUIRE(dividendYield.size() == n,
                   "McHimalaya: dividendYield size does not match"
                   " that of correlation matrix");
        QL_REQUIRE(times.size() >= 1,
                   "McHimalaya: you must have at least one time-step");

        // initialize the path generator
        std::vector<boost::shared_ptr<DiffusionProcess> > processes(n);
        for (Size i=0; i<n; i++)
            processes[i] = Handle<DiffusionProcess>(
                    new BlackScholesProcess(riskFreeRate, dividendYield[i],
                                            volatilities[i], underlying[i]));

        TimeGrid grid(times.begin(), times.end());
        PseudoRandom::rsg_type rsg = 
            PseudoRandom::make_sequence_generator(n*(grid.size()-1),seed);

        typedef MultiAsset<PseudoRandom>::path_generator_type generator;
        boost::shared_ptr<generator> pathGenerator(
                                  new generator(processes, correlation, grid, 
                                                rsg, false));

        // initialize the path pricer
        boost::shared_ptr<PathPricer<MultiPath> > pathPricer(
            new HimalayaPathPricer(underlying, strike, riskFreeRate));

        // initialize the multi-factor Monte Carlo
        mcModel_ = boost::shared_ptr<MonteCarloModel<MultiAsset<
                                                      PseudoRandom> > > (
            new MonteCarloModel<MultiAsset<PseudoRandom> > (
                             pathGenerator, pathPricer, Statistics(), false));

    }

}
