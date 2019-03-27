//===-- xray-graph.h - XRay Function Call Graph Renderer --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A class to get a color from a specified gradient.
//
//===----------------------------------------------------------------------===//

#ifndef XRAY_COLOR_HELPER_H
#define XRAY_COLOR_HELPER_H

#include <tuple>

#include "llvm/ADT/ArrayRef.h"

namespace llvm {
namespace xray {

/// The color helper class it a healper class which allows you to easily get a
/// color in a gradient. This is used to color-code edges in XRay-Graph tools.
///
/// There are two types of color schemes in this class:
///   - Sequential schemes, which are used to represent information from some
///     minimum to some maximum. These take an input in the range [0,1]
///   - Diverging schemes, which are used to represent information representing
///     differenes, or a range that goes from negative to positive. These take
///     an input in the range [-1,1].
/// Usage;
/// ColorHelper S(ColorHelper::SequentialScheme::OrRd); //Chose a color scheme.
/// for (double p = 0.0; p <= 1; p += 0.1){
///   cout() << S.getColor(p) << " \n"; // Sample the gradient at 0.1 intervals
/// }
///
/// ColorHelper D(ColorHelper::DivergingScheme::Spectral); // Choose a color
///                                                        // scheme.
/// for (double p= -1; p <= 1 ; p += 0.1){
///   cout() << D.getColor(p) << " \n"; // sample the gradient at 0.1 intervals
/// }
class ColorHelper {
  double MinIn;
  double MaxIn;

  ArrayRef<std::tuple<uint8_t, uint8_t, uint8_t>> ColorMap;
  ArrayRef<std::tuple<uint8_t, uint8_t, uint8_t>> BoundMap;

public:
  /// Enum of the availible Sequential Color Schemes
  enum class SequentialScheme {
    // Schemes based on the ColorBrewer Color schemes of the same name from
    // http://www.colorbrewer.org/ by Cynthis A Brewer Penn State University.
    Greys,
    OrRd,
    PuBu
  };

  ColorHelper(SequentialScheme S);

  /// Enum of the availible Diverging Color Schemes
  enum class DivergingScheme {
    // Schemes based on the ColorBrewer Color schemes of the same name from
    // http://www.colorbrewer.org/ by Cynthis A Brewer Penn State University.
    PiYG
  };

  ColorHelper(DivergingScheme S);

  // Sample the gradient at the input point.
  std::tuple<uint8_t, uint8_t, uint8_t> getColorTuple(double Point) const;

  std::string getColorString(double Point) const;

  // Get the Default color, at the moment allways black.
  std::tuple<uint8_t, uint8_t, uint8_t> getDefaultColorTuple() const {
    return std::make_tuple(0, 0, 0);
  }

  std::string getDefaultColorString() const { return "black"; }

  // Convert a tuple to a string
  static std::string getColorString(std::tuple<uint8_t, uint8_t, uint8_t> t);
};
} // namespace xray
} // namespace llvm
#endif
