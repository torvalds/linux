//===--- PPC.cpp - PPC Helpers for Tools ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// getPPCTargetCPU - Get the (LLVM) name of the PowerPC cpu we are targeting.
std::string ppc::getPPCTargetCPU(const ArgList &Args) {
  if (Arg *A = Args.getLastArg(clang::driver::options::OPT_mcpu_EQ)) {
    StringRef CPUName = A->getValue();

    if (CPUName == "native") {
      std::string CPU = llvm::sys::getHostCPUName();
      if (!CPU.empty() && CPU != "generic")
        return CPU;
      else
        return "";
    }

    return llvm::StringSwitch<const char *>(CPUName)
        .Case("common", "generic")
        .Case("440", "440")
        .Case("440fp", "440")
        .Case("450", "450")
        .Case("601", "601")
        .Case("602", "602")
        .Case("603", "603")
        .Case("603e", "603e")
        .Case("603ev", "603ev")
        .Case("604", "604")
        .Case("604e", "604e")
        .Case("620", "620")
        .Case("630", "pwr3")
        .Case("G3", "g3")
        .Case("7400", "7400")
        .Case("G4", "g4")
        .Case("7450", "7450")
        .Case("G4+", "g4+")
        .Case("750", "750")
        .Case("970", "970")
        .Case("G5", "g5")
        .Case("a2", "a2")
        .Case("a2q", "a2q")
        .Case("e500mc", "e500mc")
        .Case("e5500", "e5500")
        .Case("power3", "pwr3")
        .Case("power4", "pwr4")
        .Case("power5", "pwr5")
        .Case("power5x", "pwr5x")
        .Case("power6", "pwr6")
        .Case("power6x", "pwr6x")
        .Case("power7", "pwr7")
        .Case("power8", "pwr8")
        .Case("power9", "pwr9")
        .Case("pwr3", "pwr3")
        .Case("pwr4", "pwr4")
        .Case("pwr5", "pwr5")
        .Case("pwr5x", "pwr5x")
        .Case("pwr6", "pwr6")
        .Case("pwr6x", "pwr6x")
        .Case("pwr7", "pwr7")
        .Case("pwr8", "pwr8")
        .Case("pwr9", "pwr9")
        .Case("powerpc", "ppc")
        .Case("powerpc64", "ppc64")
        .Case("powerpc64le", "ppc64le")
        .Default("");
  }

  return "";
}

const char *ppc::getPPCAsmModeForCPU(StringRef Name) {
  return llvm::StringSwitch<const char *>(Name)
        .Case("pwr7", "-mpower7")
        .Case("power7", "-mpower7")
        .Case("pwr8", "-mpower8")
        .Case("power8", "-mpower8")
        .Case("ppc64le", "-mpower8")
        .Case("pwr9", "-mpower9")
        .Case("power9", "-mpower9")
        .Default("-many");
}

void ppc::getPPCTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                               const ArgList &Args,
                               std::vector<StringRef> &Features) {
  handleTargetFeaturesGroup(Args, Features, options::OPT_m_ppc_Features_Group);

  ppc::FloatABI FloatABI = ppc::getPPCFloatABI(D, Args);
  if (FloatABI == ppc::FloatABI::Soft)
    Features.push_back("-hard-float");

  ppc::ReadGOTPtrMode ReadGOT = ppc::getPPCReadGOTPtrMode(D, Triple, Args);
  if (ReadGOT == ppc::ReadGOTPtrMode::SecurePlt)
    Features.push_back("+secure-plt");
}

ppc::ReadGOTPtrMode ppc::getPPCReadGOTPtrMode(const Driver &D, const llvm::Triple &Triple,
                                              const ArgList &Args) {
  if (Args.getLastArg(options::OPT_msecure_plt))
    return ppc::ReadGOTPtrMode::SecurePlt;
  if (Triple.isOSOpenBSD())
    return ppc::ReadGOTPtrMode::SecurePlt;
  else
    return ppc::ReadGOTPtrMode::Bss;
}

ppc::FloatABI ppc::getPPCFloatABI(const Driver &D, const ArgList &Args) {
  ppc::FloatABI ABI = ppc::FloatABI::Invalid;
  if (Arg *A =
          Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                          options::OPT_mfloat_abi_EQ)) {
    if (A->getOption().matches(options::OPT_msoft_float))
      ABI = ppc::FloatABI::Soft;
    else if (A->getOption().matches(options::OPT_mhard_float))
      ABI = ppc::FloatABI::Hard;
    else {
      ABI = llvm::StringSwitch<ppc::FloatABI>(A->getValue())
                .Case("soft", ppc::FloatABI::Soft)
                .Case("hard", ppc::FloatABI::Hard)
                .Default(ppc::FloatABI::Invalid);
      if (ABI == ppc::FloatABI::Invalid && !StringRef(A->getValue()).empty()) {
        D.Diag(clang::diag::err_drv_invalid_mfloat_abi) << A->getAsString(Args);
        ABI = ppc::FloatABI::Hard;
      }
    }
  }

  // If unspecified, choose the default based on the platform.
  if (ABI == ppc::FloatABI::Invalid) {
    ABI = ppc::FloatABI::Hard;
  }

  return ABI;
}

bool ppc::hasPPCAbiArg(const ArgList &Args, const char *Value) {
  Arg *A = Args.getLastArg(options::OPT_mabi_EQ);
  return A && (A->getValue() == StringRef(Value));
}
