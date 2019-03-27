// MmapWriteExecChecker.cpp - Check for the prot argument -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This checker tests the 3rd argument of mmap's calls to check if
// it is writable and executable in the same time. It's somehow
// an optional checker since for example in JIT libraries it is pretty common.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"

#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;
using llvm::APSInt;

namespace {
class MmapWriteExecChecker : public Checker<check::PreCall> {
  CallDescription MmapFn;
  CallDescription MprotectFn;
  static int ProtWrite;
  static int ProtExec;
  static int ProtRead;
  mutable std::unique_ptr<BugType> BT;
public:
  MmapWriteExecChecker() : MmapFn("mmap", 6), MprotectFn("mprotect", 3) {}
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  int ProtExecOv;
  int ProtReadOv;
};
}

int MmapWriteExecChecker::ProtWrite = 0x02;
int MmapWriteExecChecker::ProtExec  = 0x04;
int MmapWriteExecChecker::ProtRead  = 0x01;

void MmapWriteExecChecker::checkPreCall(const CallEvent &Call,
                                         CheckerContext &C) const {
  if (Call.isCalled(MmapFn) || Call.isCalled(MprotectFn)) {
    SVal ProtVal = Call.getArgSVal(2);
    Optional<nonloc::ConcreteInt> ProtLoc = ProtVal.getAs<nonloc::ConcreteInt>();
    int64_t Prot = ProtLoc->getValue().getSExtValue();
    if (ProtExecOv != ProtExec)
      ProtExec = ProtExecOv;
    if (ProtReadOv != ProtRead)
      ProtRead = ProtReadOv;

    // Wrong settings
    if (ProtRead == ProtExec)
      return;

    if ((Prot & (ProtWrite | ProtExec)) == (ProtWrite | ProtExec)) {
      if (!BT)
        BT.reset(new BugType(this, "W^X check fails, Write Exec prot flags set", "Security"));

      ExplodedNode *N = C.generateNonFatalErrorNode();
      if (!N)
        return;

      auto Report = llvm::make_unique<BugReport>(
          *BT, "Both PROT_WRITE and PROT_EXEC flags are set. This can "
               "lead to exploitable memory regions, which could be overwritten "
               "with malicious code", N);
      Report->addRange(Call.getArgSourceRange(2));
      C.emitReport(std::move(Report));
    }
  }
}

void ento::registerMmapWriteExecChecker(CheckerManager &mgr) {
  MmapWriteExecChecker *Mwec =
      mgr.registerChecker<MmapWriteExecChecker>();
  Mwec->ProtExecOv =
    mgr.getAnalyzerOptions()
      .getCheckerIntegerOption("MmapProtExec", 0x04, Mwec);
  Mwec->ProtReadOv =
    mgr.getAnalyzerOptions()
      .getCheckerIntegerOption("MmapProtRead", 0x01, Mwec);
}
