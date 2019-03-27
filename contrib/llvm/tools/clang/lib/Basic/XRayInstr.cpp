//===--- XRayInstr.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is part of XRay, a function call instrumentation system.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/XRayInstr.h"
#include "llvm/ADT/StringSwitch.h"

namespace clang {

XRayInstrMask parseXRayInstrValue(StringRef Value) {
  XRayInstrMask ParsedKind = llvm::StringSwitch<XRayInstrMask>(Value)
                                 .Case("all", XRayInstrKind::All)
                                 .Case("custom", XRayInstrKind::Custom)
                                 .Case("function", XRayInstrKind::Function)
                                 .Case("typed", XRayInstrKind::Typed)
                                 .Case("none", XRayInstrKind::None)
                                 .Default(XRayInstrKind::None);
  return ParsedKind;
}

} // namespace clang
