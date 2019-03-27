//===---- CoverageMappingGen.h - Coverage mapping generation ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Instrumentation-based code coverage mapping generator
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_COVERAGEMAPPINGGEN_H
#define LLVM_CLANG_LIB_CODEGEN_COVERAGEMAPPINGGEN_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {

class LangOptions;
class SourceManager;
class FileEntry;
class Preprocessor;
class Decl;
class Stmt;

/// Stores additional source code information like skipped ranges which
/// is required by the coverage mapping generator and is obtained from
/// the preprocessor.
class CoverageSourceInfo : public PPCallbacks {
  std::vector<SourceRange> SkippedRanges;
public:
  ArrayRef<SourceRange> getSkippedRanges() const { return SkippedRanges; }

  void SourceRangeSkipped(SourceRange Range, SourceLocation EndifLoc) override;
};

namespace CodeGen {

class CodeGenModule;

/// Organizes the cross-function state that is used while generating
/// code coverage mapping data.
class CoverageMappingModuleGen {
  CodeGenModule &CGM;
  CoverageSourceInfo &SourceInfo;
  llvm::SmallDenseMap<const FileEntry *, unsigned, 8> FileEntries;
  std::vector<llvm::Constant *> FunctionRecords;
  std::vector<llvm::Constant *> FunctionNames;
  llvm::StructType *FunctionRecordTy;
  std::vector<std::string> CoverageMappings;

public:
  CoverageMappingModuleGen(CodeGenModule &CGM, CoverageSourceInfo &SourceInfo)
      : CGM(CGM), SourceInfo(SourceInfo), FunctionRecordTy(nullptr) {}

  CoverageSourceInfo &getSourceInfo() const {
    return SourceInfo;
  }

  /// Add a function's coverage mapping record to the collection of the
  /// function mapping records.
  void addFunctionMappingRecord(llvm::GlobalVariable *FunctionName,
                                StringRef FunctionNameValue,
                                uint64_t FunctionHash,
                                const std::string &CoverageMapping,
                                bool IsUsed = true);

  /// Emit the coverage mapping data for a translation unit.
  void emit();

  /// Return the coverage mapping translation unit file id
  /// for the given file.
  unsigned getFileID(const FileEntry *File);
};

/// Organizes the per-function state that is used while generating
/// code coverage mapping data.
class CoverageMappingGen {
  CoverageMappingModuleGen &CVM;
  SourceManager &SM;
  const LangOptions &LangOpts;
  llvm::DenseMap<const Stmt *, unsigned> *CounterMap;

public:
  CoverageMappingGen(CoverageMappingModuleGen &CVM, SourceManager &SM,
                     const LangOptions &LangOpts)
      : CVM(CVM), SM(SM), LangOpts(LangOpts), CounterMap(nullptr) {}

  CoverageMappingGen(CoverageMappingModuleGen &CVM, SourceManager &SM,
                     const LangOptions &LangOpts,
                     llvm::DenseMap<const Stmt *, unsigned> *CounterMap)
      : CVM(CVM), SM(SM), LangOpts(LangOpts), CounterMap(CounterMap) {}

  /// Emit the coverage mapping data which maps the regions of
  /// code to counters that will be used to find the execution
  /// counts for those regions.
  void emitCounterMapping(const Decl *D, llvm::raw_ostream &OS);

  /// Emit the coverage mapping data for an unused function.
  /// It creates mapping regions with the counter of zero.
  void emitEmptyMapping(const Decl *D, llvm::raw_ostream &OS);
};

} // end namespace CodeGen
} // end namespace clang

#endif
