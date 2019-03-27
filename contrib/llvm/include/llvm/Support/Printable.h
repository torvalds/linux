//===--- Printable.h - Print function helpers -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Printable struct.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PRINTABLE_H
#define LLVM_SUPPORT_PRINTABLE_H

#include <functional>

namespace llvm {

class raw_ostream;

/// Simple wrapper around std::function<void(raw_ostream&)>.
/// This class is useful to construct print helpers for raw_ostream.
///
/// Example:
///     Printable PrintRegister(unsigned Register) {
///       return Printable([Register](raw_ostream &OS) {
///         OS << getRegisterName(Register);
///       }
///     }
///     ... OS << PrintRegister(Register); ...
///
/// Implementation note: Ideally this would just be a typedef, but doing so
/// leads to operator << being ambiguous as function has matching constructors
/// in some STL versions. I have seen the problem on gcc 4.6 libstdc++ and
/// microsoft STL.
class Printable {
public:
  std::function<void(raw_ostream &OS)> Print;
  Printable(std::function<void(raw_ostream &OS)> Print)
      : Print(std::move(Print)) {}
};

inline raw_ostream &operator<<(raw_ostream &OS, const Printable &P) {
  P.Print(OS);
  return OS;
}

}

#endif
