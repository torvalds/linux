//===--- Mips.cpp - Mips ToolChain Implementations --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MipsLinux.h"
#include "Arch/Mips.h"
#include "CommonArgs.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

/// Mips Toolchain
MipsLLVMToolChain::MipsLLVMToolChain(const Driver &D,
                                     const llvm::Triple &Triple,
                                     const ArgList &Args)
    : Linux(D, Triple, Args) {
  // Select the correct multilib according to the given arguments.
  DetectedMultilibs Result;
  findMIPSMultilibs(D, Triple, "", Args, Result);
  Multilibs = Result.Multilibs;
  SelectedMultilib = Result.SelectedMultilib;

  // Find out the library suffix based on the ABI.
  LibSuffix = tools::mips::getMipsABILibSuffix(Args, Triple);
  getFilePaths().clear();
  getFilePaths().push_back(computeSysRoot() + "/usr/lib" + LibSuffix);
}

void MipsLLVMToolChain::AddClangSystemIncludeArgs(
    const ArgList &DriverArgs, ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(clang::driver::options::OPT_nostdinc))
    return;

  const Driver &D = getDriver();

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P);
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  const auto &Callback = Multilibs.includeDirsCallback();
  if (Callback) {
    for (const auto &Path : Callback(SelectedMultilib))
      addExternCSystemIncludeIfExists(DriverArgs, CC1Args,
                                      D.getInstalledDir() + Path);
  }
}

Tool *MipsLLVMToolChain::buildLinker() const {
  return new tools::gnutools::Linker(*this);
}

std::string MipsLLVMToolChain::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot + SelectedMultilib.osSuffix();

  const std::string InstalledDir(getDriver().getInstalledDir());
  std::string SysRootPath =
      InstalledDir + "/../sysroot" + SelectedMultilib.osSuffix();
  if (llvm::sys::fs::exists(SysRootPath))
    return SysRootPath;

  return std::string();
}

ToolChain::CXXStdlibType
MipsLLVMToolChain::GetCXXStdlibType(const ArgList &Args) const {
  Arg *A = Args.getLastArg(options::OPT_stdlib_EQ);
  if (A) {
    StringRef Value = A->getValue();
    if (Value != "libc++")
      getDriver().Diag(clang::diag::err_drv_invalid_stdlib_name)
          << A->getAsString(Args);
  }

  return ToolChain::CST_Libcxx;
}

void MipsLLVMToolChain::addLibCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  if (const auto &Callback = Multilibs.includeDirsCallback()) {
    for (std::string Path : Callback(SelectedMultilib)) {
      Path = getDriver().getInstalledDir() + Path + "/c++/v1";
      if (llvm::sys::fs::exists(Path)) {
        addSystemInclude(DriverArgs, CC1Args, Path);
        return;
      }
    }
  }
}

void MipsLLVMToolChain::AddCXXStdlibLibArgs(const ArgList &Args,
                                            ArgStringList &CmdArgs) const {
  assert((GetCXXStdlibType(Args) == ToolChain::CST_Libcxx) &&
         "Only -lc++ (aka libxx) is supported in this toolchain.");

  CmdArgs.push_back("-lc++");
  CmdArgs.push_back("-lc++abi");
  CmdArgs.push_back("-lunwind");
}

std::string MipsLLVMToolChain::getCompilerRT(const ArgList &Args,
                                             StringRef Component,
                                             bool Shared) const {
  SmallString<128> Path(getDriver().ResourceDir);
  llvm::sys::path::append(Path, SelectedMultilib.osSuffix(), "lib" + LibSuffix,
                          getOS());
  llvm::sys::path::append(Path, Twine("libclang_rt." + Component + "-" +
                                      "mips" + (Shared ? ".so" : ".a")));
  return Path.str();
}
