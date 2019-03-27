//===- MSFError.h - Error extensions for MSF Files --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MSFERROR_H
#define LLVM_DEBUGINFO_MSF_MSFERROR_H

#include "llvm/Support/Error.h"

#include <string>

namespace llvm {
namespace msf {
enum class msf_error_code {
  unspecified = 1,
  insufficient_buffer,
  not_writable,
  no_stream,
  invalid_format,
  block_in_use
};
} // namespace msf
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::msf::msf_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace msf {
const std::error_category &MSFErrCategory();

inline std::error_code make_error_code(msf_error_code E) {
  return std::error_code(static_cast<int>(E), MSFErrCategory());
}

/// Base class for errors originating when parsing raw PDB files
class MSFError : public ErrorInfo<MSFError, StringError> {
public:
  using ErrorInfo<MSFError, StringError>::ErrorInfo; // inherit constructors
  MSFError(const Twine &S) : ErrorInfo(S, msf_error_code::unspecified) {}
  static char ID;
};
} // namespace msf
} // namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MSFERROR_H
