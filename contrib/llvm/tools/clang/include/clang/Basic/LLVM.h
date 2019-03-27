//===--- LLVM.h - Import various common LLVM datatypes ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Forward-declares and imports various common LLVM datatypes that
/// clang wants to use unqualified.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_LLVM_H
#define LLVM_CLANG_BASIC_LLVM_H

// Do not proliferate #includes here, require clients to #include their
// dependencies.
// Casting.h has complex templates that cannot be easily forward declared.
#include "llvm/Support/Casting.h"
// None.h includes an enumerator that is desired & cannot be forward declared
// without a definition of NoneType.
#include "llvm/ADT/None.h"

namespace llvm {
  // ADT's.
  class StringRef;
  class Twine;
  class VersionTuple;
  template<typename T> class ArrayRef;
  template<typename T> class MutableArrayRef;
  template<typename T> class OwningArrayRef;
  template<unsigned InternalLen> class SmallString;
  template<typename T, unsigned N> class SmallVector;
  template<typename T> class SmallVectorImpl;
  template<typename T> class Optional;
  template <class T> class Expected;

  template<typename T>
  struct SaveAndRestore;

  // Reference counting.
  template <typename T> class IntrusiveRefCntPtr;
  template <typename T> struct IntrusiveRefCntPtrInfo;
  template <class Derived> class RefCountedBase;

  class raw_ostream;
  class raw_pwrite_stream;
  // TODO: DenseMap, ...
}


namespace clang {
  // Casting operators.
  using llvm::isa;
  using llvm::cast;
  using llvm::dyn_cast;
  using llvm::dyn_cast_or_null;
  using llvm::cast_or_null;

  // ADT's.
  using llvm::ArrayRef;
  using llvm::MutableArrayRef;
  using llvm::None;
  using llvm::Optional;
  using llvm::OwningArrayRef;
  using llvm::SaveAndRestore;
  using llvm::SmallString;
  using llvm::SmallVector;
  using llvm::SmallVectorImpl;
  using llvm::StringRef;
  using llvm::Twine;
  using llvm::VersionTuple;

  // Error handling.
  using llvm::Expected;

  // Reference counting.
  using llvm::IntrusiveRefCntPtr;
  using llvm::IntrusiveRefCntPtrInfo;
  using llvm::RefCountedBase;

  using llvm::raw_ostream;
  using llvm::raw_pwrite_stream;
} // end namespace clang.

#endif
