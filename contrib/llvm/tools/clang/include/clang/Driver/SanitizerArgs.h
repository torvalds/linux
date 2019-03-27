//===--- SanitizerArgs.h - Arguments for sanitizer tools  -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_DRIVER_SANITIZERARGS_H
#define LLVM_CLANG_DRIVER_SANITIZERARGS_H

#include "clang/Basic/Sanitizers.h"
#include "clang/Driver/Types.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include <string>
#include <vector>

namespace clang {
namespace driver {

class ToolChain;

class SanitizerArgs {
  SanitizerSet Sanitizers;
  SanitizerSet RecoverableSanitizers;
  SanitizerSet TrapSanitizers;

  std::vector<std::string> BlacklistFiles;
  std::vector<std::string> ExtraDeps;
  int CoverageFeatures = 0;
  int MsanTrackOrigins = 0;
  bool MsanUseAfterDtor = true;
  bool CfiCrossDso = false;
  bool CfiICallGeneralizePointers = false;
  int AsanFieldPadding = 0;
  bool SharedRuntime = false;
  bool AsanUseAfterScope = true;
  bool AsanPoisonCustomArrayCookie = false;
  bool AsanGlobalsDeadStripping = false;
  bool AsanUseOdrIndicator = false;
  std::string HwasanAbi;
  bool LinkCXXRuntimes = false;
  bool NeedPIE = false;
  bool SafeStackRuntime = false;
  bool Stats = false;
  bool TsanMemoryAccess = true;
  bool TsanFuncEntryExit = true;
  bool TsanAtomics = true;
  bool MinimalRuntime = false;
  // True if cross-dso CFI support if provided by the system (i.e. Android).
  bool ImplicitCfiRuntime = false;

 public:
  /// Parses the sanitizer arguments from an argument list.
  SanitizerArgs(const ToolChain &TC, const llvm::opt::ArgList &Args);

  bool needsSharedRt() const { return SharedRuntime; }

  bool needsAsanRt() const { return Sanitizers.has(SanitizerKind::Address); }
  bool needsHwasanRt() const { return Sanitizers.has(SanitizerKind::HWAddress); }
  bool needsTsanRt() const { return Sanitizers.has(SanitizerKind::Thread); }
  bool needsMsanRt() const { return Sanitizers.has(SanitizerKind::Memory); }
  bool needsFuzzer() const { return Sanitizers.has(SanitizerKind::Fuzzer); }
  bool needsLsanRt() const {
    return Sanitizers.has(SanitizerKind::Leak) &&
           !Sanitizers.has(SanitizerKind::Address) &&
           !Sanitizers.has(SanitizerKind::HWAddress);
  }
  bool needsUbsanRt() const;
  bool requiresMinimalRuntime() const { return MinimalRuntime; }
  bool needsDfsanRt() const { return Sanitizers.has(SanitizerKind::DataFlow); }
  bool needsSafeStackRt() const { return SafeStackRuntime; }
  bool needsCfiRt() const;
  bool needsCfiDiagRt() const;
  bool needsStatsRt() const { return Stats; }
  bool needsEsanRt() const {
    return Sanitizers.hasOneOf(SanitizerKind::Efficiency);
  }
  bool needsScudoRt() const { return Sanitizers.has(SanitizerKind::Scudo); }

  bool requiresPIE() const;
  bool needsUnwindTables() const;
  bool needsLTO() const;
  bool linkCXXRuntimes() const { return LinkCXXRuntimes; }
  bool hasCrossDsoCfi() const { return CfiCrossDso; }
  bool hasAnySanitizer() const { return !Sanitizers.empty(); }
  void addArgs(const ToolChain &TC, const llvm::opt::ArgList &Args,
               llvm::opt::ArgStringList &CmdArgs, types::ID InputType) const;
};

}  // namespace driver
}  // namespace clang

#endif
