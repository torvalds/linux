//===-- SymbolContextScope.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SymbolContextScope_h_
#define liblldb_SymbolContextScope_h_

#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class SymbolContextScope SymbolContextScope.h
/// "lldb/Symbol/SymbolContextScope.h" Inherit from this if your object is
/// part of a symbol context
///        and can reconstruct its symbol context.
///
/// Many objects that are part of a symbol context that have pointers back to
/// parent objects that own them. Any members of a symbol context that, once
/// they are built, will not go away, can inherit from this pure virtual class
/// and can then reconstruct their symbol context without having to keep a
/// complete SymbolContext object in the object.
///
/// Examples of these objects include:
///     @li Module
///     @li CompileUnit
///     @li Function
///     @li Block
///     @li Symbol
///
/// Other objects can store a "SymbolContextScope *" using any pointers to one
/// of the above objects. This allows clients to hold onto a pointer that
/// uniquely will identify a symbol context. Those clients can then always
/// reconstruct the symbol context using the pointer, or use it to uniquely
/// identify a symbol context for an object.
///
/// Example objects include that currently use "SymbolContextScope *" objects
/// include:
///     @li Variable objects that can reconstruct where they are scoped
///         by making sure the SymbolContextScope * comes from the scope
///         in which the variable was declared. If a variable is a global,
///         the appropriate CompileUnit * will be used when creating the
///         variable. A static function variables, can the Block scope
///         in which the variable is defined. Function arguments can use
///         the Function object as their scope. The SymbolFile parsers
///         will set these correctly as the variables are parsed.
///     @li Type objects that know exactly in which scope they
///         originated much like the variables above.
///     @li StackID objects that are able to know that if the CFA
///         (stack pointer at the beginning of a function) and the
///         start PC for the function/symbol and the SymbolContextScope
///         pointer (a unique pointer that identifies a symbol context
///         location) match within the same thread, that the stack
///         frame is the same as the previous stack frame.
///
/// Objects that adhere to this protocol can reconstruct enough of a symbol
/// context to allow functions that take a symbol context to be called. Lists
/// can also be created using a SymbolContextScope* and and object pairs that
/// allow large collections of objects to be passed around with minimal
/// overhead.
//----------------------------------------------------------------------
class SymbolContextScope {
public:
  virtual ~SymbolContextScope() = default;

  //------------------------------------------------------------------
  /// Reconstruct the object's symbol context into \a sc.
  ///
  /// The object should fill in as much of the SymbolContext as it can so
  /// function calls that require a symbol context can be made for the given
  /// object.
  ///
  /// @param[out] sc
  ///     A symbol context object pointer that gets filled in.
  //------------------------------------------------------------------
  virtual void CalculateSymbolContext(SymbolContext *sc) = 0;

  virtual lldb::ModuleSP CalculateSymbolContextModule() {
    return lldb::ModuleSP();
  }

  virtual CompileUnit *CalculateSymbolContextCompileUnit() { return nullptr; }

  virtual Function *CalculateSymbolContextFunction() { return nullptr; }

  virtual Block *CalculateSymbolContextBlock() { return nullptr; }

  virtual Symbol *CalculateSymbolContextSymbol() { return nullptr; }

  //------------------------------------------------------------------
  /// Dump the object's symbol context to the stream \a s.
  ///
  /// The object should dump its symbol context to the stream \a s. This
  /// function is widely used in the DumpDebug and verbose output for lldb
  /// objects.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object's symbol context.
  //------------------------------------------------------------------
  virtual void DumpSymbolContext(Stream *s) = 0;
};

} // namespace lldb_private

#endif // liblldb_SymbolContextScope_h_
