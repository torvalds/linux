//===- PreprocessorOptions.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_
#define LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class MemoryBuffer;

} // namespace llvm

namespace clang {

/// Enumerate the kinds of standard library that
enum ObjCXXARCStandardLibraryKind {
  ARCXX_nolib,

  /// libc++
  ARCXX_libcxx,

  /// libstdc++
  ARCXX_libstdcxx
};

/// PreprocessorOptions - This class is used for passing the various options
/// used in preprocessor initialization to InitializePreprocessor().
class PreprocessorOptions {
public:
  std::vector<std::pair<std::string, bool/*isUndef*/>> Macros;
  std::vector<std::string> Includes;
  std::vector<std::string> MacroIncludes;

  /// Initialize the preprocessor with the compiler and target specific
  /// predefines.
  bool UsePredefines = true;

  /// Whether we should maintain a detailed record of all macro
  /// definitions and expansions.
  bool DetailedRecord = false;

  /// When true, we are creating or using a PCH where a #pragma hdrstop is
  /// expected to indicate the beginning or end of the PCH.
  bool PCHWithHdrStop = false;

  /// When true, we are creating a PCH or creating the PCH object while
  /// expecting a #pragma hdrstop to separate the two.  Allow for a
  /// missing #pragma hdrstop, which generates a PCH for the whole file,
  /// and creates an empty PCH object.
  bool PCHWithHdrStopCreate = false;

  /// If non-empty, the filename used in an #include directive in the primary
  /// source file (or command-line preinclude) that is used to implement
  /// MSVC-style precompiled headers. When creating a PCH, after the #include
  /// of this header, the PCH generation stops. When using a PCH, tokens are
  /// skipped until after an #include of this header is seen.
  std::string PCHThroughHeader;

  /// The implicit PCH included at the start of the translation unit, or empty.
  std::string ImplicitPCHInclude;

  /// Headers that will be converted to chained PCHs in memory.
  std::vector<std::string> ChainedIncludes;

  /// When true, disables most of the normal validation performed on
  /// precompiled headers.
  bool DisablePCHValidation = false;

  /// When true, a PCH with compiler errors will not be rejected.
  bool AllowPCHWithCompilerErrors = false;

  /// Dump declarations that are deserialized from PCH, for testing.
  bool DumpDeserializedPCHDecls = false;

  /// This is a set of names for decls that we do not want to be
  /// deserialized, and we emit an error if they are; for testing purposes.
  std::set<std::string> DeserializedPCHDeclsToErrorOn;

  /// If non-zero, the implicit PCH include is actually a precompiled
  /// preamble that covers this number of bytes in the main source file.
  ///
  /// The boolean indicates whether the preamble ends at the start of a new
  /// line.
  std::pair<unsigned, bool> PrecompiledPreambleBytes;

  /// True indicates that a preamble is being generated.
  ///
  /// When the lexer is done, one of the things that need to be preserved is the
  /// conditional #if stack, so the ASTWriter/ASTReader can save/restore it when
  /// processing the rest of the file.
  bool GeneratePreamble = false;

  /// Whether to write comment locations into the PCH when building it.
  /// Reading the comments from the PCH can be a performance hit even if the
  /// clients don't use them.
  bool WriteCommentListToPCH = true;

  /// When enabled, preprocessor is in a mode for parsing a single file only.
  ///
  /// Disables #includes of other files and if there are unresolved identifiers
  /// in preprocessor directive conditions it causes all blocks to be parsed so
  /// that the client can get the maximum amount of information from the parser.
  bool SingleFileParseMode = false;

  /// When enabled, the preprocessor will construct editor placeholder tokens.
  bool LexEditorPlaceholders = true;

  /// True if the SourceManager should report the original file name for
  /// contents of files that were remapped to other files. Defaults to true.
  bool RemappedFilesKeepOriginalName = true;

  /// The set of file remappings, which take existing files on
  /// the system (the first part of each pair) and gives them the
  /// contents of other files on the system (the second part of each
  /// pair).
  std::vector<std::pair<std::string, std::string>> RemappedFiles;

  /// The set of file-to-buffer remappings, which take existing files
  /// on the system (the first part of each pair) and gives them the contents
  /// of the specified memory buffer (the second part of each pair).
  std::vector<std::pair<std::string, llvm::MemoryBuffer *>> RemappedFileBuffers;

  /// Whether the compiler instance should retain (i.e., not free)
  /// the buffers associated with remapped files.
  ///
  /// This flag defaults to false; it can be set true only through direct
  /// manipulation of the compiler invocation object, in cases where the
  /// compiler invocation and its buffers will be reused.
  bool RetainRemappedFileBuffers = false;

  /// The Objective-C++ ARC standard library that we should support,
  /// by providing appropriate definitions to retrofit the standard library
  /// with support for lifetime-qualified pointers.
  ObjCXXARCStandardLibraryKind ObjCXXARCStandardLibrary = ARCXX_nolib;

  /// Records the set of modules
  class FailedModulesSet {
    llvm::StringSet<> Failed;

  public:
    bool hasAlreadyFailed(StringRef module) {
      return Failed.count(module) > 0;
    }

    void addFailed(StringRef module) {
      Failed.insert(module);
    }
  };

  /// The set of modules that failed to build.
  ///
  /// This pointer will be shared among all of the compiler instances created
  /// to (re)build modules, so that once a module fails to build anywhere,
  /// other instances will see that the module has failed and won't try to
  /// build it again.
  std::shared_ptr<FailedModulesSet> FailedModules;

public:
  PreprocessorOptions() : PrecompiledPreambleBytes(0, false) {}

  void addMacroDef(StringRef Name) { Macros.emplace_back(Name, false); }
  void addMacroUndef(StringRef Name) { Macros.emplace_back(Name, true); }

  void addRemappedFile(StringRef From, StringRef To) {
    RemappedFiles.emplace_back(From, To);
  }

  void addRemappedFile(StringRef From, llvm::MemoryBuffer *To) {
    RemappedFileBuffers.emplace_back(From, To);
  }

  void clearRemappedFiles() {
    RemappedFiles.clear();
    RemappedFileBuffers.clear();
  }

  /// Reset any options that are not considered when building a
  /// module.
  void resetNonModularOptions() {
    Includes.clear();
    MacroIncludes.clear();
    ChainedIncludes.clear();
    DumpDeserializedPCHDecls = false;
    ImplicitPCHInclude.clear();
    SingleFileParseMode = false;
    LexEditorPlaceholders = true;
    RetainRemappedFileBuffers = true;
    PrecompiledPreambleBytes.first = 0;
    PrecompiledPreambleBytes.second = false;
  }
};

} // namespace clang

#endif // LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_
