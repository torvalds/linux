//===- SystemUtils.h - Utilities to do low-level system stuff ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains functions used to do a variety of low-level, often
// system-specific, tasks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SYSTEMUTILS_H
#define LLVM_SUPPORT_SYSTEMUTILS_H

namespace llvm {
  class raw_ostream;

/// Determine if the raw_ostream provided is connected to a terminal. If so,
/// generate a warning message to errs() advising against display of bitcode
/// and return true. Otherwise just return false.
/// Check for output written to a console
bool CheckBitcodeOutputToConsole(
  raw_ostream &stream_to_check, ///< The stream to be checked
  bool print_warning = true     ///< Control whether warnings are printed
);

} // End llvm namespace

#endif
