//===- Error.h - system_error extensions for llvm-cxxdump -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This declares a new error_category for the llvm-cxxdump tool.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_CXXDUMP_ERROR_H
#define LLVM_TOOLS_LLVM_CXXDUMP_ERROR_H

#include <system_error>

namespace llvm {
const std::error_category &cxxdump_category();

enum class cxxdump_error {
  success = 0,
  file_not_found,
  unrecognized_file_format,
};

inline std::error_code make_error_code(cxxdump_error e) {
  return std::error_code(static_cast<int>(e), cxxdump_category());
}

} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::cxxdump_error> : std::true_type {};
}

#endif
