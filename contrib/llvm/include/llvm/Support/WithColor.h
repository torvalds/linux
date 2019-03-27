//===- WithColor.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WITHCOLOR_H
#define LLVM_SUPPORT_WITHCOLOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

extern cl::OptionCategory ColorCategory;

class raw_ostream;

// Symbolic names for various syntax elements.
enum class HighlightColor {
  Address,
  String,
  Tag,
  Attribute,
  Enumerator,
  Macro,
  Error,
  Warning,
  Note,
  Remark
};

/// An RAII object that temporarily switches an output stream to a specific
/// color.
class WithColor {
  raw_ostream &OS;
  bool DisableColors;

public:
  /// To be used like this: WithColor(OS, HighlightColor::String) << "text";
  /// @param OS The output stream
  /// @param S Symbolic name for syntax element to color
  /// @param DisableColors Whether to ignore color changes regardless of -color
  /// and support in OS
  WithColor(raw_ostream &OS, HighlightColor S, bool DisableColors = false);
  /// To be used like this: WithColor(OS, raw_ostream::Black) << "text";
  /// @param OS The output stream
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold Bold/brighter text, default false
  /// @param BG If true, change the background, default: change foreground
  /// @param DisableColors Whether to ignore color changes regardless of -color
  /// and support in OS
  WithColor(raw_ostream &OS,
            raw_ostream::Colors Color = raw_ostream::SAVEDCOLOR,
            bool Bold = false, bool BG = false, bool DisableColors = false)
      : OS(OS), DisableColors(DisableColors) {
    changeColor(Color, Bold, BG);
  }
  ~WithColor();

  raw_ostream &get() { return OS; }
  operator raw_ostream &() { return OS; }
  template <typename T> WithColor &operator<<(T &O) {
    OS << O;
    return *this;
  }
  template <typename T> WithColor &operator<<(const T &O) {
    OS << O;
    return *this;
  }

  /// Convenience method for printing "error: " to stderr.
  static raw_ostream &error();
  /// Convenience method for printing "warning: " to stderr.
  static raw_ostream &warning();
  /// Convenience method for printing "note: " to stderr.
  static raw_ostream &note();
  /// Convenience method for printing "remark: " to stderr.
  static raw_ostream &remark();

  /// Convenience method for printing "error: " to the given stream.
  static raw_ostream &error(raw_ostream &OS, StringRef Prefix = "",
                            bool DisableColors = false);
  /// Convenience method for printing "warning: " to the given stream.
  static raw_ostream &warning(raw_ostream &OS, StringRef Prefix = "",
                              bool DisableColors = false);
  /// Convenience method for printing "note: " to the given stream.
  static raw_ostream &note(raw_ostream &OS, StringRef Prefix = "",
                           bool DisableColors = false);
  /// Convenience method for printing "remark: " to the given stream.
  static raw_ostream &remark(raw_ostream &OS, StringRef Prefix = "",
                             bool DisableColors = false);

  /// Determine whether colors are displayed.
  bool colorsEnabled();

  /// Change the color of text that will be output from this point forward.
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold Bold/brighter text, default false
  /// @param BG If true, change the background, default: change foreground
  WithColor &changeColor(raw_ostream::Colors Color, bool Bold = false,
                         bool BG = false);

  /// Reset the colors to terminal defaults. Call this when you are done
  /// outputting colored text, or before program exit.
  WithColor &resetColor();
};

} // end namespace llvm

#endif // LLVM_LIB_DEBUGINFO_WITHCOLOR_H
