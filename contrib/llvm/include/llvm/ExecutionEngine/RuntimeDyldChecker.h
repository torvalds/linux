//===---- RuntimeDyldChecker.h - RuntimeDyld tester framework -----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_RUNTIMEDYLDCHECKER_H
#define LLVM_EXECUTIONENGINE_RUNTIMEDYLDCHECKER_H

#include "llvm/ADT/Optional.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace llvm {

class StringRef;
class MCDisassembler;
class MemoryBuffer;
class MCInstPrinter;
class RuntimeDyld;
class RuntimeDyldCheckerImpl;
class raw_ostream;

/// RuntimeDyld invariant checker for verifying that RuntimeDyld has
///        correctly applied relocations.
///
/// The RuntimeDyldChecker class evaluates expressions against an attached
/// RuntimeDyld instance to verify that relocations have been applied
/// correctly.
///
/// The expression language supports basic pointer arithmetic and bit-masking,
/// and has limited disassembler integration for accessing instruction
/// operands and the next PC (program counter) address for each instruction.
///
/// The language syntax is:
///
/// check = expr '=' expr
///
/// expr = binary_expr
///      | sliceable_expr
///
/// sliceable_expr = '*{' number '}' load_addr_expr [slice]
///                | '(' expr ')' [slice]
///                | ident_expr [slice]
///                | number [slice]
///
/// slice = '[' high-bit-index ':' low-bit-index ']'
///
/// load_addr_expr = symbol
///                | '(' symbol '+' number ')'
///                | '(' symbol '-' number ')'
///
/// ident_expr = 'decode_operand' '(' symbol ',' operand-index ')'
///            | 'next_pc'        '(' symbol ')'
///            | 'stub_addr' '(' file-name ',' section-name ',' symbol ')'
///            | symbol
///
/// binary_expr = expr '+' expr
///             | expr '-' expr
///             | expr '&' expr
///             | expr '|' expr
///             | expr '<<' expr
///             | expr '>>' expr
///
class RuntimeDyldChecker {
public:
  RuntimeDyldChecker(RuntimeDyld &RTDyld, MCDisassembler *Disassembler,
                     MCInstPrinter *InstPrinter, raw_ostream &ErrStream);
  ~RuntimeDyldChecker();

  // Get the associated RTDyld instance.
  RuntimeDyld& getRTDyld();

  // Get the associated RTDyld instance.
  const RuntimeDyld& getRTDyld() const;

  /// Check a single expression against the attached RuntimeDyld
  ///        instance.
  bool check(StringRef CheckExpr) const;

  /// Scan the given memory buffer for lines beginning with the string
  ///        in RulePrefix. The remainder of the line is passed to the check
  ///        method to be evaluated as an expression.
  bool checkAllRulesInBuffer(StringRef RulePrefix, MemoryBuffer *MemBuf) const;

  /// Returns the address of the requested section (or an error message
  ///        in the second element of the pair if the address cannot be found).
  ///
  /// if 'LocalAddress' is true, this returns the address of the section
  /// within the linker's memory. If 'LocalAddress' is false it returns the
  /// address within the target process (i.e. the load address).
  std::pair<uint64_t, std::string> getSectionAddr(StringRef FileName,
                                                  StringRef SectionName,
                                                  bool LocalAddress);

  /// If there is a section at the given local address, return its load
  ///        address, otherwise return none.
  Optional<uint64_t> getSectionLoadAddress(void *LocalAddress) const;

private:
  std::unique_ptr<RuntimeDyldCheckerImpl> Impl;
};

} // end namespace llvm

#endif
