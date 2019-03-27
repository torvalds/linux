//===--- XRayInstr.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::XRayInstrKind enum.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_XRAYINSTR_H
#define LLVM_CLANG_BASIC_XRAYINSTR_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>

namespace clang {

using XRayInstrMask = uint32_t;

namespace XRayInstrKind {

// TODO: Auto-generate these as we add more instrumentation kinds.
enum XRayInstrOrdinal : XRayInstrMask {
  XRIO_Function,
  XRIO_Custom,
  XRIO_Typed,
  XRIO_Count
};

constexpr XRayInstrMask None = 0;
constexpr XRayInstrMask Function = 1U << XRIO_Function;
constexpr XRayInstrMask Custom = 1U << XRIO_Custom;
constexpr XRayInstrMask Typed = 1U << XRIO_Typed;
constexpr XRayInstrMask All = Function | Custom | Typed;

} // namespace XRayInstrKind

struct XRayInstrSet {
  bool has(XRayInstrMask K) const {
    assert(llvm::isPowerOf2_32(K));
    return Mask & K;
  }

  bool hasOneOf(XRayInstrMask K) const { return Mask & K; }

  void set(XRayInstrMask K, bool Value) {
    assert(llvm::isPowerOf2_32(K));
    Mask = Value ? (Mask | K) : (Mask & ~K);
  }

  void clear(XRayInstrMask K = XRayInstrKind::All) { Mask &= ~K; }

  bool empty() const { return Mask == 0; }

  bool full() const { return Mask == XRayInstrKind::All; }

  XRayInstrMask Mask = 0;
};

XRayInstrMask parseXRayInstrValue(StringRef Value);

} // namespace clang

#endif // LLVM_CLANG_BASIC_XRAYINSTR_H
