//===--- Sparc.cpp - Tools Implementations ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Sparc.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

const char *sparc::getSparcAsmModeForCPU(StringRef Name,
                                         const llvm::Triple &Triple) {
  if (Triple.getArch() == llvm::Triple::sparcv9) {
    return llvm::StringSwitch<const char *>(Name)
        .Case("niagara", "-Av9b")
        .Case("niagara2", "-Av9b")
        .Case("niagara3", "-Av9d")
        .Case("niagara4", "-Av9d")
        .Default("-Av9");
  } else {
    return llvm::StringSwitch<const char *>(Name)
        .Case("v8", "-Av8")
        .Case("supersparc", "-Av8")
        .Case("sparclite", "-Asparclite")
        .Case("f934", "-Asparclite")
        .Case("hypersparc", "-Av8")
        .Case("sparclite86x", "-Asparclite")
        .Case("sparclet", "-Asparclet")
        .Case("tsc701", "-Asparclet")
        .Case("v9", "-Av8plus")
        .Case("ultrasparc", "-Av8plus")
        .Case("ultrasparc3", "-Av8plus")
        .Case("niagara", "-Av8plusb")
        .Case("niagara2", "-Av8plusb")
        .Case("niagara3", "-Av8plusd")
        .Case("niagara4", "-Av8plusd")
        .Case("ma2100", "-Aleon")
        .Case("ma2150", "-Aleon")
        .Case("ma2155", "-Aleon")
        .Case("ma2450", "-Aleon")
        .Case("ma2455", "-Aleon")
        .Case("ma2x5x", "-Aleon")
        .Case("ma2080", "-Aleon")
        .Case("ma2085", "-Aleon")
        .Case("ma2480", "-Aleon")
        .Case("ma2485", "-Aleon")
        .Case("ma2x8x", "-Aleon")
        .Case("myriad2", "-Aleon")
        .Case("myriad2.1", "-Aleon")
        .Case("myriad2.2", "-Aleon")
        .Case("myriad2.3", "-Aleon")
        .Case("leon2", "-Av8")
        .Case("at697e", "-Av8")
        .Case("at697f", "-Av8")
        .Case("leon3", "-Aleon")
        .Case("ut699", "-Av8")
        .Case("gr712rc", "-Aleon")
        .Case("leon4", "-Aleon")
        .Case("gr740", "-Aleon")
        .Default("-Av8");
  }
}

sparc::FloatABI sparc::getSparcFloatABI(const Driver &D,
                                        const ArgList &Args) {
  sparc::FloatABI ABI = sparc::FloatABI::Invalid;
  if (Arg *A = Args.getLastArg(clang::driver::options::OPT_msoft_float,
                               options::OPT_mhard_float,
                               options::OPT_mfloat_abi_EQ)) {
    if (A->getOption().matches(clang::driver::options::OPT_msoft_float))
      ABI = sparc::FloatABI::Soft;
    else if (A->getOption().matches(options::OPT_mhard_float))
      ABI = sparc::FloatABI::Hard;
    else {
      ABI = llvm::StringSwitch<sparc::FloatABI>(A->getValue())
                .Case("soft", sparc::FloatABI::Soft)
                .Case("hard", sparc::FloatABI::Hard)
                .Default(sparc::FloatABI::Invalid);
      if (ABI == sparc::FloatABI::Invalid &&
          !StringRef(A->getValue()).empty()) {
        D.Diag(clang::diag::err_drv_invalid_mfloat_abi) << A->getAsString(Args);
        ABI = sparc::FloatABI::Hard;
      }
    }
  }

  // If unspecified, choose the default based on the platform.
  // Only the hard-float ABI on Sparc is standardized, and it is the
  // default. GCC also supports a nonstandard soft-float ABI mode, also
  // implemented in LLVM. However as this is not standard we set the default
  // to be hard-float.
  if (ABI == sparc::FloatABI::Invalid) {
    ABI = sparc::FloatABI::Hard;
  }

  return ABI;
}

void sparc::getSparcTargetFeatures(const Driver &D, const ArgList &Args,
                                   std::vector<StringRef> &Features) {
  sparc::FloatABI FloatABI = sparc::getSparcFloatABI(D, Args);
  if (FloatABI == sparc::FloatABI::Soft)
    Features.push_back("+soft-float");
}
