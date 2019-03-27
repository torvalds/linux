//===- ExternalPreprocessorSource.h - Abstract Macro Interface --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ExternalPreprocessorSource interface, which enables
//  construction of macro definitions from some external source.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_LEX_EXTERNALPREPROCESSORSOURCE_H
#define LLVM_CLANG_LEX_EXTERNALPREPROCESSORSOURCE_H

namespace clang {

class IdentifierInfo;
class Module;

/// Abstract interface for external sources of preprocessor
/// information.
///
/// This abstract class allows an external sources (such as the \c ASTReader)
/// to provide additional preprocessing information.
class ExternalPreprocessorSource {
public:
  virtual ~ExternalPreprocessorSource();

  /// Read the set of macros defined by this external macro source.
  virtual void ReadDefinedMacros() = 0;

  /// Update an out-of-date identifier.
  virtual void updateOutOfDateIdentifier(IdentifierInfo &II) = 0;

  /// Return the identifier associated with the given ID number.
  ///
  /// The ID 0 is associated with the NULL identifier.
  virtual IdentifierInfo *GetIdentifier(unsigned ID) = 0;

  /// Map a module ID to a module.
  virtual Module *getModule(unsigned ModuleID) = 0;
};

}

#endif
