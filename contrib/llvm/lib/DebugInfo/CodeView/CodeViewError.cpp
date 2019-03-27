//===- CodeViewError.cpp - Error extensions for CodeView --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"

using namespace llvm;
using namespace llvm::codeview;

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class CodeViewErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "llvm.codeview"; }
  std::string message(int Condition) const override {
    switch (static_cast<cv_error_code>(Condition)) {
    case cv_error_code::unspecified:
      return "An unknown CodeView error has occurred.";
    case cv_error_code::insufficient_buffer:
      return "The buffer is not large enough to read the requested number of "
             "bytes.";
    case cv_error_code::corrupt_record:
      return "The CodeView record is corrupted.";
    case cv_error_code::no_records:
      return "There are no records.";
    case cv_error_code::operation_unsupported:
      return "The requested operation is not supported.";
    case cv_error_code::unknown_member_record:
      return "The member record is of an unknown type.";
    }
    llvm_unreachable("Unrecognized cv_error_code");
  }
};

static llvm::ManagedStatic<CodeViewErrorCategory> CodeViewErrCategory;
const std::error_category &llvm::codeview::CVErrorCategory() {
  return *CodeViewErrCategory;
}

char CodeViewError::ID;
