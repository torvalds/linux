//===- SymbolRemappingReader.h - Read symbol remapping file -----*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for reading and applying symbol
// remapping files.
//
// Support is provided only for the Itanium C++ name mangling scheme for now.
//
// NOTE: If you are making changes to this file format, please remember
//       to document them in the Clang documentation at
//       tools/clang/docs/UsersManual.rst.
//
// File format
// -----------
//
// The symbol remappings are written as an ASCII text file. Blank lines and
// lines starting with a # are ignored. All other lines specify a kind of
// mangled name fragment, along with two fragments of that kind that should
// be treated as equivalent, separated by spaces.
//
// See http://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling for a
// description of the Itanium name mangling scheme.
//
// The accepted fragment kinds are:
//
//  * name  A <name>, such as 6foobar or St3__1
//  * type  A <type>, such as Ss or N4llvm9StringRefE
//  * encoding  An <encoding> (a complete mangling without the leading _Z)
//
// For example:
//
// # Ignore int / long differences to treat symbols from 32-bit and 64-bit
// # builds with differing size_t / ptrdiff_t / intptr_t as equivalent.
// type i l
// type j m
//
// # Ignore differences between libc++ and libstdc++, and between libstdc++'s
// # C++98 and C++11 ABIs.
// name 3std St3__1
// name 3std St7__cxx11
//
// # Remap a function overload to a specialization of a template (including
// # any local symbols declared within it).
// encoding N2NS1fEi N2NS1fIiEEvT_
//
// # Substitutions must be remapped separately from namespace 'std' for now.
// name Sa NSt3__19allocatorE
// name Sb NSt3__112basic_stringE
// type Ss NSt3__112basic_stringIcSt11char_traitsIcESaE
// # ...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SYMBOLREMAPPINGREADER_H
#define LLVM_SUPPORT_SYMBOLREMAPPINGREADER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ItaniumManglingCanonicalizer.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {

class SymbolRemappingParseError : public ErrorInfo<SymbolRemappingParseError> {
public:
  SymbolRemappingParseError(StringRef File, int64_t Line, Twine Message)
      : File(File), Line(Line), Message(Message.str()) {}

  void log(llvm::raw_ostream &OS) const override {
    OS << File << ':' << Line << ": " << Message;
  }
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }

  StringRef getFileName() const { return File; }
  int64_t getLineNum() const { return Line; }
  StringRef getMessage() const { return Message; }

  static char ID;

private:
  std::string File;
  int64_t Line;
  std::string Message;
};

/// Reader for symbol remapping files.
///
/// Remaps the symbol names in profile data to match those in the program
/// according to a set of rules specified in a given file.
class SymbolRemappingReader {
public:
  /// Read remappings from the given buffer, which must live as long as
  /// the remapper.
  Error read(MemoryBuffer &B);

  /// A Key represents an equivalence class of symbol names.
  using Key = uintptr_t;

  /// Construct a key for the given symbol, or return an existing one if an
  /// equivalent name has already been inserted. The symbol name must live
  /// as long as the remapper.
  ///
  /// The result will be Key() if the name cannot be remapped (typically
  /// because it is not a valid mangled name).
  Key insert(StringRef FunctionName) {
    return Canonicalizer.canonicalize(FunctionName);
  }

  /// Map the given symbol name into the key for the corresponding equivalence
  /// class.
  ///
  /// The result will typically be Key() if no equivalent symbol has been
  /// inserted, but this is not guaranteed: a Key different from all keys ever
  /// returned by \c insert may be returned instead.
  Key lookup(StringRef FunctionName) {
    return Canonicalizer.lookup(FunctionName);
  }

private:
  ItaniumManglingCanonicalizer Canonicalizer;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_SYMBOLREMAPPINGREADER_H
