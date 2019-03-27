//===--- RISCV.cpp - RISCV Helpers for Tools --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/raw_ostream.h"
#include "ToolChains/CommonArgs.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

static StringRef getExtensionTypeDesc(StringRef Ext) {
  if (Ext.startswith("sx"))
    return "non-standard supervisor-level extension";
  if (Ext.startswith("s"))
    return "standard supervisor-level extension";
  if (Ext.startswith("x"))
    return "non-standard user-level extension";
  return StringRef();
}

static StringRef getExtensionType(StringRef Ext) {
  if (Ext.startswith("sx"))
    return "sx";
  if (Ext.startswith("s"))
    return "s";
  if (Ext.startswith("x"))
    return "x";
  return StringRef();
}

static bool isSupportedExtension(StringRef Ext) {
  // LLVM does not support "sx", "s" nor "x" extensions.
  return false;
}

// Extensions may have a version number, and may be separated by
// an underscore '_' e.g.: rv32i2_m2.
// Version number is divided into major and minor version numbers,
// separated by a 'p'. If the minor version is 0 then 'p0' can be
// omitted from the version string. E.g., rv32i2p0, rv32i2, rv32i2p1.
static bool getExtensionVersion(const Driver &D, StringRef MArch,
                                StringRef Ext, StringRef In,
                                std::string &Major, std::string &Minor) {
  auto I = In.begin();
  auto E = In.end();

  while (I != E && isDigit(*I))
    Major.append(1, *I++);

  if (Major.empty())
    return true;

  if (I != E && *I == 'p') {
    ++I;

    while (I != E && isDigit(*I))
      Minor.append(1, *I++);

    // Expected 'p' to be followed by minor version number.
    if (Minor.empty()) {
      std::string Error =
        "minor version number missing after 'p' for extension";
      D.Diag(diag::err_drv_invalid_riscv_ext_arch_name)
        << MArch << Error << Ext;
      return false;
    }
  }

  // TODO: Handle extensions with version number.
  std::string Error = "unsupported version number " + Major;
  if (!Minor.empty())
    Error += "." + Minor;
  Error += " for extension";
  D.Diag(diag::err_drv_invalid_riscv_ext_arch_name) << MArch << Error << Ext;

  return false;
}

// Handle other types of extensions other than the standard
// general purpose and standard user-level extensions.
// Parse the ISA string containing non-standard user-level
// extensions, standard supervisor-level extensions and
// non-standard supervisor-level extensions.
// These extensions start with 'x', 's', 'sx' prefixes, follow a
// canonical order, might have a version number (major, minor)
// and are separated by a single underscore '_'.
// Set the hardware features for the extensions that are supported.
static void getExtensionFeatures(const Driver &D,
                                 const ArgList &Args,
                                 std::vector<StringRef> &Features,
                                 StringRef &MArch, StringRef &Exts) {
  if (Exts.empty())
    return;

  // Multi-letter extensions are seperated by a single underscore
  // as described in RISC-V User-Level ISA V2.2.
  SmallVector<StringRef, 8> Split;
  Exts.split(Split, StringRef("_"));

  SmallVector<StringRef, 3> Prefix;
  Prefix.push_back("x");
  Prefix.push_back("s");
  Prefix.push_back("sx");
  auto I = Prefix.begin();
  auto E = Prefix.end();

  SmallVector<StringRef, 8> AllExts;

  for (StringRef Ext : Split) {

    if (Ext.empty()) {
      D.Diag(diag::err_drv_invalid_riscv_arch_name) << MArch
        << "extension name missing after separator '_'";
      return;
    }

    StringRef Type = getExtensionType(Ext);
    StringRef Name(Ext.substr(Type.size()));
    StringRef Desc = getExtensionTypeDesc(Ext);

    if (Type.empty()) {
      D.Diag(diag::err_drv_invalid_riscv_ext_arch_name)
        << MArch << "invalid extension prefix" << Ext;
      return;
    }

    // Check ISA extensions are specified in the canonical order.
    while (I != E && *I != Type)
      ++I;

    if (I == E) {
      std::string Error = Desc;
      Error += " not given in canonical order";
      D.Diag(diag::err_drv_invalid_riscv_ext_arch_name)
        << MArch <<  Error << Ext;
      return;
    }

    // The order is OK, do not advance I to the next prefix
    // to allow repeated extension type, e.g.: rv32ixabc_xdef.

    if (Name.empty()) {
      std::string Error = Desc;
      Error += " name missing after";
      D.Diag(diag::err_drv_invalid_riscv_ext_arch_name)
        << MArch << Error << Ext;
      return;
    }

    std::string Major, Minor;
    auto Pos = Name.find_if(isDigit);
    if (Pos != StringRef::npos) {
      auto Next =  Name.substr(Pos);
      Name = Name.substr(0, Pos);
      if (!getExtensionVersion(D, MArch, Ext, Next, Major, Minor))
        return;
    }

    // Check if duplicated extension.
    if (std::find(AllExts.begin(), AllExts.end(), Ext) != AllExts.end()) {
      std::string Error = "duplicated ";
      Error += Desc;
      D.Diag(diag::err_drv_invalid_riscv_ext_arch_name)
        << MArch << Error << Ext;
      return;
    }

    // Extension format is correct, keep parsing the extensions.
    // TODO: Save Type, Name, Major, Minor to avoid parsing them later.
    AllExts.push_back(Ext);
  }

  // Set target features.
  // TODO: Hardware features to be handled in Support/TargetParser.cpp.
  // TODO: Use version number when setting target features.
  for (auto Ext : AllExts) {
    if (!isSupportedExtension(Ext)) {
      StringRef Desc = getExtensionTypeDesc(getExtensionType(Ext));
      std::string Error = "unsupported ";
      Error += Desc;
      D.Diag(diag::err_drv_invalid_riscv_ext_arch_name)
        << MArch << Error << Ext;
      return;
    }
    Features.push_back(Args.MakeArgString("+" + Ext));
  }
}

void riscv::getRISCVTargetFeatures(const Driver &D, const ArgList &Args,
                                   std::vector<StringRef> &Features) {
  if (const Arg *A = Args.getLastArg(options::OPT_march_EQ)) {
    StringRef MArch = A->getValue();

    // RISC-V ISA strings must be lowercase.
    if (std::any_of(std::begin(MArch), std::end(MArch),
                    [](char c) { return isupper(c); })) {

      D.Diag(diag::err_drv_invalid_riscv_arch_name) << MArch
        << "string must be lowercase";
      return;
    }

    // ISA string must begin with rv32 or rv64.
    if (!(MArch.startswith("rv32") || MArch.startswith("rv64")) ||
        (MArch.size() < 5)) {
      D.Diag(diag::err_drv_invalid_riscv_arch_name) << MArch
        << "string must begin with rv32{i,e,g} or rv64{i,g}";
      return;
    }

    bool HasRV64 = MArch.startswith("rv64") ? true : false;

    // The canonical order specified in ISA manual.
    // Ref: Table 22.1 in RISC-V User-Level ISA V2.2
    StringRef StdExts = "mafdqlcbjtpvn";
    bool HasF = false, HasD = false;
    char Baseline = MArch[4];

    // First letter should be 'e', 'i' or 'g'.
    switch (Baseline) {
    default:
      D.Diag(diag::err_drv_invalid_riscv_arch_name) << MArch
        << "first letter should be 'e', 'i' or 'g'";
      return;
    case 'e': {
      StringRef Error;
      // Currently LLVM does not support 'e'.
      // Extension 'e' is not allowed in rv64.
      if (HasRV64)
        Error = "standard user-level extension 'e' requires 'rv32'";
      else
        Error = "unsupported standard user-level extension 'e'";
      D.Diag(diag::err_drv_invalid_riscv_arch_name)
        << MArch << Error;
      return;
    }
    case 'i':
      break;
    case 'g':
      // g = imafd
      StdExts = StdExts.drop_front(4);
      Features.push_back("+m");
      Features.push_back("+a");
      Features.push_back("+f");
      Features.push_back("+d");
      HasF = true;
      HasD = true;
      break;
    }

    // Skip rvxxx
    StringRef Exts = MArch.substr(5);

    // Remove non-standard extensions and supervisor-level extensions.
    // They have 'x', 's', 'sx' prefixes. Parse them at the end.
    // Find the very first occurrence of 's' or 'x'.
    StringRef OtherExts;
    size_t Pos = Exts.find_first_of("sx");
    if (Pos != StringRef::npos) {
      OtherExts = Exts.substr(Pos);
      Exts = Exts.substr(0, Pos);
    }

    std::string Major, Minor;
    if (!getExtensionVersion(D, MArch, std::string(1, Baseline),
                             Exts, Major, Minor))
      return;

    // TODO: Use version number when setting target features
    // and consume the underscore '_' that might follow.

    auto StdExtsItr = StdExts.begin();
    auto StdExtsEnd = StdExts.end();

    for (auto I = Exts.begin(), E = Exts.end(); I != E; ++I)  {
      char c = *I;

      // Check ISA extensions are specified in the canonical order.
      while (StdExtsItr != StdExtsEnd && *StdExtsItr != c)
        ++StdExtsItr;

      if (StdExtsItr == StdExtsEnd) {
        // Either c contains a valid extension but it was not given in
        // canonical order or it is an invalid extension.
        StringRef Error;
        if (StdExts.contains(c))
          Error = "standard user-level extension not given in canonical order";
        else
          Error = "invalid standard user-level extension";
        D.Diag(diag::err_drv_invalid_riscv_ext_arch_name)
          << MArch <<  Error << std::string(1, c);
        return;
      }

      // Move to next char to prevent repeated letter.
      ++StdExtsItr;

      if (std::next(I) != E) {
        // Skip c.
        std::string Next = std::string(std::next(I), E);
        std::string Major, Minor;
        if (!getExtensionVersion(D, MArch, std::string(1, c),
                                 Next, Major, Minor))
          return;

        // TODO: Use version number when setting target features
        // and consume the underscore '_' that might follow.
      }

      // The order is OK, then push it into features.
      switch (c) {
      default:
        // Currently LLVM supports only "mafdc".
        D.Diag(diag::err_drv_invalid_riscv_ext_arch_name)
          << MArch << "unsupported standard user-level extension"
          << std::string(1, c);
        return;
      case 'm':
        Features.push_back("+m");
        break;
      case 'a':
        Features.push_back("+a");
        break;
      case 'f':
        Features.push_back("+f");
        HasF = true;
        break;
      case 'd':
        Features.push_back("+d");
        HasD = true;
        break;
      case 'c':
        Features.push_back("+c");
        break;
      }
    }

    // Dependency check.
    // It's illegal to specify the 'd' (double-precision floating point)
    // extension without also specifying the 'f' (single precision
    // floating-point) extension.
    if (HasD && !HasF)
      D.Diag(diag::err_drv_invalid_riscv_arch_name) << MArch
        << "d requires f extension to also be specified";

    // Additional dependency checks.
    // TODO: The 'q' extension requires rv64.
    // TODO: It is illegal to specify 'e' extensions with 'f' and 'd'.

    // Handle all other types of extensions.
    getExtensionFeatures(D, Args, Features, MArch, OtherExts);
  }

  // Now add any that the user explicitly requested on the command line,
  // which may override the defaults.
  handleTargetFeaturesGroup(Args, Features, options::OPT_m_riscv_Features_Group);
}

StringRef riscv::getRISCVABI(const ArgList &Args, const llvm::Triple &Triple) {
  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ))
    return A->getValue();

  return Triple.getArch() == llvm::Triple::riscv32 ? "ilp32" : "lp64";
}
