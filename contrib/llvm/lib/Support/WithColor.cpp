//===- WithColor.cpp ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

cl::OptionCategory llvm::ColorCategory("Color Options");

static cl::opt<cl::boolOrDefault>
    UseColor("color", cl::cat(ColorCategory),
             cl::desc("Use colors in output (default=autodetect)"),
             cl::init(cl::BOU_UNSET));

WithColor::WithColor(raw_ostream &OS, HighlightColor Color, bool DisableColors)
    : OS(OS), DisableColors(DisableColors) {
  // Detect color from terminal type unless the user passed the --color option.
  if (colorsEnabled()) {
    switch (Color) {
    case HighlightColor::Address:
      OS.changeColor(raw_ostream::YELLOW);
      break;
    case HighlightColor::String:
      OS.changeColor(raw_ostream::GREEN);
      break;
    case HighlightColor::Tag:
      OS.changeColor(raw_ostream::BLUE);
      break;
    case HighlightColor::Attribute:
      OS.changeColor(raw_ostream::CYAN);
      break;
    case HighlightColor::Enumerator:
      OS.changeColor(raw_ostream::MAGENTA);
      break;
    case HighlightColor::Macro:
      OS.changeColor(raw_ostream::RED);
      break;
    case HighlightColor::Error:
      OS.changeColor(raw_ostream::RED, true);
      break;
    case HighlightColor::Warning:
      OS.changeColor(raw_ostream::MAGENTA, true);
      break;
    case HighlightColor::Note:
      OS.changeColor(raw_ostream::BLACK, true);
      break;
    case HighlightColor::Remark:
      OS.changeColor(raw_ostream::BLUE, true);
      break;
    }
  }
}

raw_ostream &WithColor::error() { return error(errs()); }

raw_ostream &WithColor::warning() { return warning(errs()); }

raw_ostream &WithColor::note() { return note(errs()); }

raw_ostream &WithColor::remark() { return remark(errs()); }

raw_ostream &WithColor::error(raw_ostream &OS, StringRef Prefix,
                              bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Error, DisableColors).get()
         << "error: ";
}

raw_ostream &WithColor::warning(raw_ostream &OS, StringRef Prefix,
                                bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Warning, DisableColors).get()
         << "warning: ";
}

raw_ostream &WithColor::note(raw_ostream &OS, StringRef Prefix,
                             bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Note, DisableColors).get() << "note: ";
}

raw_ostream &WithColor::remark(raw_ostream &OS, StringRef Prefix,
                               bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Remark, DisableColors).get()
         << "remark: ";
}

bool WithColor::colorsEnabled() {
  if (DisableColors)
    return false;
  if (UseColor == cl::BOU_UNSET)
    return OS.has_colors();
  return UseColor == cl::BOU_TRUE;
}

WithColor &WithColor::changeColor(raw_ostream::Colors Color, bool Bold,
                                  bool BG) {
  if (colorsEnabled())
    OS.changeColor(Color, Bold, BG);
  return *this;
}

WithColor &WithColor::resetColor() {
  if (colorsEnabled())
    OS.resetColor();
  return *this;
}

WithColor::~WithColor() { resetColor(); }
