//===- Error.cpp - system_error extensions for llvm-cxxdump -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines a new error_category for the llvm-cxxdump tool.
//
//===----------------------------------------------------------------------===//

#include "Error.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class cxxdump_error_category : public std::error_category {
public:
  const char *name() const noexcept override { return "llvm.cxxdump"; }
  std::string message(int ev) const override {
    switch (static_cast<cxxdump_error>(ev)) {
    case cxxdump_error::success:
      return "Success";
    case cxxdump_error::file_not_found:
      return "No such file.";
    case cxxdump_error::unrecognized_file_format:
      return "Unrecognized file type.";
    }
    llvm_unreachable(
        "An enumerator of cxxdump_error does not have a message defined.");
  }
};
} // namespace

namespace llvm {
const std::error_category &cxxdump_category() {
  static cxxdump_error_category o;
  return o;
}
} // namespace llvm
