//===- Testing/Support/SupportHelpers.h -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TESTING_SUPPORT_SUPPORTHELPERS_H
#define LLVM_TESTING_SUPPORT_SUPPORTHELPERS_H

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_os_ostream.h"
#include "gtest/gtest-printers.h"

#include <string>

namespace llvm {
namespace detail {
struct ErrorHolder {
  std::vector<std::shared_ptr<ErrorInfoBase>> Infos;

  bool Success() const { return Infos.empty(); }
};

template <typename T> struct ExpectedHolder : public ErrorHolder {
  ExpectedHolder(ErrorHolder Err, Expected<T> &Exp)
      : ErrorHolder(std::move(Err)), Exp(Exp) {}

  Expected<T> &Exp;
};

inline void PrintTo(const ErrorHolder &Err, std::ostream *Out) {
  raw_os_ostream OS(*Out);
  OS << (Err.Success() ? "succeeded" : "failed");
  if (!Err.Success()) {
    const char *Delim = "  (";
    for (const auto &Info : Err.Infos) {
      OS << Delim;
      Delim = "; ";
      Info->log(OS);
    }
    OS << ")";
  }
}

template <typename T>
void PrintTo(const ExpectedHolder<T> &Item, std::ostream *Out) {
  if (Item.Success()) {
    *Out << "succeeded with value " << ::testing::PrintToString(*Item.Exp);
  } else {
    PrintTo(static_cast<const ErrorHolder &>(Item), Out);
  }
}
} // namespace detail

namespace unittest {
SmallString<128> getInputFileDirectory(const char *Argv0);
}
} // namespace llvm

#endif
