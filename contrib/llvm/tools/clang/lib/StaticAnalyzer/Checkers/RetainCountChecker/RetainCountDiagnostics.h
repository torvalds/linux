//== RetainCountDiagnostics.h - Checks for leaks and other issues -*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines diagnostics for RetainCountChecker, which implements
//  a reference count checker for Core Foundation and Cocoa on (Mac OS X).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_RETAINCOUNTCHECKER_DIAGNOSTICS_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_RETAINCOUNTCHECKER_DIAGNOSTICS_H

#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h"
#include "clang/StaticAnalyzer/Core/RetainSummaryManager.h"

namespace clang {
namespace ento {
namespace retaincountchecker {

class RefCountBug : public BugType {
protected:
  RefCountBug(const CheckerBase *checker, StringRef name)
      : BugType(checker, name, categories::MemoryRefCount) {}

public:
  virtual const char *getDescription() const = 0;

  virtual bool isLeak() const { return false; }
};

class RefCountReport : public BugReport {
protected:
  SymbolRef Sym;

public:
  RefCountReport(RefCountBug &D, const LangOptions &LOpts,
              ExplodedNode *n, SymbolRef sym,
              bool registerVisitor = true);

  RefCountReport(RefCountBug &D, const LangOptions &LOpts,
              ExplodedNode *n, SymbolRef sym,
              StringRef endText);

  llvm::iterator_range<ranges_iterator> getRanges() override {
    const RefCountBug& BugTy = static_cast<RefCountBug&>(getBugType());
    if (!BugTy.isLeak())
      return BugReport::getRanges();
    return llvm::make_range(ranges_iterator(), ranges_iterator());
  }
};

class RefLeakReport : public RefCountReport {
  const MemRegion* AllocBinding;
  const Stmt *AllocStmt;

  // Finds the function declaration where a leak warning for the parameter
  // 'sym' should be raised.
  void deriveParamLocation(CheckerContext &Ctx, SymbolRef sym);
  // Finds the location where a leak warning for 'sym' should be raised.
  void deriveAllocLocation(CheckerContext &Ctx, SymbolRef sym);
  // Produces description of a leak warning which is printed on the console.
  void createDescription(CheckerContext &Ctx);

public:
  RefLeakReport(RefCountBug &D, const LangOptions &LOpts, ExplodedNode *n,
                SymbolRef sym, CheckerContext &Ctx);

  PathDiagnosticLocation getLocation(const SourceManager &SM) const override {
    assert(Location.isValid());
    return Location;
  }
};

} // end namespace retaincountchecker
} // end namespace ento
} // end namespace clang

#endif
