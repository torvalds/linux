//===- MipsArchTree.cpp --------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// This file contains a helper function for the Writer.
//
//===---------------------------------------------------------------------===//

#include "InputFiles.h"
#include "SymbolTable.h"
#include "Writer.h"

#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/MipsABIFlags.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;

using namespace lld;
using namespace lld::elf;

namespace {
struct ArchTreeEdge {
  uint32_t Child;
  uint32_t Parent;
};

struct FileFlags {
  InputFile *File;
  uint32_t Flags;
};
} // namespace

static StringRef getAbiName(uint32_t Flags) {
  switch (Flags) {
  case 0:
    return "n64";
  case EF_MIPS_ABI2:
    return "n32";
  case EF_MIPS_ABI_O32:
    return "o32";
  case EF_MIPS_ABI_O64:
    return "o64";
  case EF_MIPS_ABI_EABI32:
    return "eabi32";
  case EF_MIPS_ABI_EABI64:
    return "eabi64";
  default:
    return "unknown";
  }
}

static StringRef getNanName(bool IsNan2008) {
  return IsNan2008 ? "2008" : "legacy";
}

static StringRef getFpName(bool IsFp64) { return IsFp64 ? "64" : "32"; }

static void checkFlags(ArrayRef<FileFlags> Files) {
  assert(!Files.empty() && "expected non-empty file list");

  uint32_t ABI = Files[0].Flags & (EF_MIPS_ABI | EF_MIPS_ABI2);
  bool Nan = Files[0].Flags & EF_MIPS_NAN2008;
  bool Fp = Files[0].Flags & EF_MIPS_FP64;

  for (const FileFlags &F : Files) {
    if (Config->Is64 && F.Flags & EF_MIPS_MICROMIPS)
      error(toString(F.File) + ": microMIPS 64-bit is not supported");

    uint32_t ABI2 = F.Flags & (EF_MIPS_ABI | EF_MIPS_ABI2);
    if (ABI != ABI2)
      error(toString(F.File) + ": ABI '" + getAbiName(ABI2) +
            "' is incompatible with target ABI '" + getAbiName(ABI) + "'");

    bool Nan2 = F.Flags & EF_MIPS_NAN2008;
    if (Nan != Nan2)
      error(toString(F.File) + ": -mnan=" + getNanName(Nan2) +
            " is incompatible with target -mnan=" + getNanName(Nan));

    bool Fp2 = F.Flags & EF_MIPS_FP64;
    if (Fp != Fp2)
      error(toString(F.File) + ": -mfp" + getFpName(Fp2) +
            " is incompatible with target -mfp" + getFpName(Fp));
  }
}

static uint32_t getMiscFlags(ArrayRef<FileFlags> Files) {
  uint32_t Ret = 0;
  for (const FileFlags &F : Files)
    Ret |= F.Flags &
           (EF_MIPS_ABI | EF_MIPS_ABI2 | EF_MIPS_ARCH_ASE | EF_MIPS_NOREORDER |
            EF_MIPS_MICROMIPS | EF_MIPS_NAN2008 | EF_MIPS_32BITMODE);
  return Ret;
}

static uint32_t getPicFlags(ArrayRef<FileFlags> Files) {
  // Check PIC/non-PIC compatibility.
  bool IsPic = Files[0].Flags & (EF_MIPS_PIC | EF_MIPS_CPIC);
  for (const FileFlags &F : Files.slice(1)) {
    bool IsPic2 = F.Flags & (EF_MIPS_PIC | EF_MIPS_CPIC);
    if (IsPic && !IsPic2)
      warn(toString(F.File) +
           ": linking non-abicalls code with abicalls code " +
           toString(Files[0].File));
    if (!IsPic && IsPic2)
      warn(toString(F.File) +
           ": linking abicalls code with non-abicalls code " +
           toString(Files[0].File));
  }

  // Compute the result PIC/non-PIC flag.
  uint32_t Ret = Files[0].Flags & (EF_MIPS_PIC | EF_MIPS_CPIC);
  for (const FileFlags &F : Files.slice(1))
    Ret &= F.Flags & (EF_MIPS_PIC | EF_MIPS_CPIC);

  // PIC code is inherently CPIC and may not set CPIC flag explicitly.
  if (Ret & EF_MIPS_PIC)
    Ret |= EF_MIPS_CPIC;
  return Ret;
}

static ArchTreeEdge ArchTree[] = {
    // MIPS32R6 and MIPS64R6 are not compatible with other extensions
    // MIPS64R2 extensions.
    {EF_MIPS_ARCH_64R2 | EF_MIPS_MACH_OCTEON3, EF_MIPS_ARCH_64R2},
    {EF_MIPS_ARCH_64R2 | EF_MIPS_MACH_OCTEON2, EF_MIPS_ARCH_64R2},
    {EF_MIPS_ARCH_64R2 | EF_MIPS_MACH_OCTEON, EF_MIPS_ARCH_64R2},
    {EF_MIPS_ARCH_64R2 | EF_MIPS_MACH_LS3A, EF_MIPS_ARCH_64R2},
    // MIPS64 extensions.
    {EF_MIPS_ARCH_64 | EF_MIPS_MACH_SB1, EF_MIPS_ARCH_64},
    {EF_MIPS_ARCH_64 | EF_MIPS_MACH_XLR, EF_MIPS_ARCH_64},
    {EF_MIPS_ARCH_64R2, EF_MIPS_ARCH_64},
    // MIPS V extensions.
    {EF_MIPS_ARCH_64, EF_MIPS_ARCH_5},
    // R5000 extensions.
    {EF_MIPS_ARCH_4 | EF_MIPS_MACH_5500, EF_MIPS_ARCH_4 | EF_MIPS_MACH_5400},
    // MIPS IV extensions.
    {EF_MIPS_ARCH_4 | EF_MIPS_MACH_5400, EF_MIPS_ARCH_4},
    {EF_MIPS_ARCH_4 | EF_MIPS_MACH_9000, EF_MIPS_ARCH_4},
    {EF_MIPS_ARCH_5, EF_MIPS_ARCH_4},
    // VR4100 extensions.
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4111, EF_MIPS_ARCH_3 | EF_MIPS_MACH_4100},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4120, EF_MIPS_ARCH_3 | EF_MIPS_MACH_4100},
    // MIPS III extensions.
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4010, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4100, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4650, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_5900, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_LS2E, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_LS2F, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_4, EF_MIPS_ARCH_3},
    // MIPS32 extensions.
    {EF_MIPS_ARCH_32R2, EF_MIPS_ARCH_32},
    // MIPS II extensions.
    {EF_MIPS_ARCH_3, EF_MIPS_ARCH_2},
    {EF_MIPS_ARCH_32, EF_MIPS_ARCH_2},
    // MIPS I extensions.
    {EF_MIPS_ARCH_1 | EF_MIPS_MACH_3900, EF_MIPS_ARCH_1},
    {EF_MIPS_ARCH_2, EF_MIPS_ARCH_1},
};

static bool isArchMatched(uint32_t New, uint32_t Res) {
  if (New == Res)
    return true;
  if (New == EF_MIPS_ARCH_32 && isArchMatched(EF_MIPS_ARCH_64, Res))
    return true;
  if (New == EF_MIPS_ARCH_32R2 && isArchMatched(EF_MIPS_ARCH_64R2, Res))
    return true;
  for (const auto &Edge : ArchTree) {
    if (Res == Edge.Child) {
      Res = Edge.Parent;
      if (Res == New)
        return true;
    }
  }
  return false;
}

static StringRef getMachName(uint32_t Flags) {
  switch (Flags & EF_MIPS_MACH) {
  case EF_MIPS_MACH_NONE:
    return "";
  case EF_MIPS_MACH_3900:
    return "r3900";
  case EF_MIPS_MACH_4010:
    return "r4010";
  case EF_MIPS_MACH_4100:
    return "r4100";
  case EF_MIPS_MACH_4650:
    return "r4650";
  case EF_MIPS_MACH_4120:
    return "r4120";
  case EF_MIPS_MACH_4111:
    return "r4111";
  case EF_MIPS_MACH_5400:
    return "vr5400";
  case EF_MIPS_MACH_5900:
    return "vr5900";
  case EF_MIPS_MACH_5500:
    return "vr5500";
  case EF_MIPS_MACH_9000:
    return "rm9000";
  case EF_MIPS_MACH_LS2E:
    return "loongson2e";
  case EF_MIPS_MACH_LS2F:
    return "loongson2f";
  case EF_MIPS_MACH_LS3A:
    return "loongson3a";
  case EF_MIPS_MACH_OCTEON:
    return "octeon";
  case EF_MIPS_MACH_OCTEON2:
    return "octeon2";
  case EF_MIPS_MACH_OCTEON3:
    return "octeon3";
  case EF_MIPS_MACH_SB1:
    return "sb1";
  case EF_MIPS_MACH_XLR:
    return "xlr";
  default:
    return "unknown machine";
  }
}

static StringRef getArchName(uint32_t Flags) {
  switch (Flags & EF_MIPS_ARCH) {
  case EF_MIPS_ARCH_1:
    return "mips1";
  case EF_MIPS_ARCH_2:
    return "mips2";
  case EF_MIPS_ARCH_3:
    return "mips3";
  case EF_MIPS_ARCH_4:
    return "mips4";
  case EF_MIPS_ARCH_5:
    return "mips5";
  case EF_MIPS_ARCH_32:
    return "mips32";
  case EF_MIPS_ARCH_64:
    return "mips64";
  case EF_MIPS_ARCH_32R2:
    return "mips32r2";
  case EF_MIPS_ARCH_64R2:
    return "mips64r2";
  case EF_MIPS_ARCH_32R6:
    return "mips32r6";
  case EF_MIPS_ARCH_64R6:
    return "mips64r6";
  default:
    return "unknown arch";
  }
}

static std::string getFullArchName(uint32_t Flags) {
  StringRef Arch = getArchName(Flags);
  StringRef Mach = getMachName(Flags);
  if (Mach.empty())
    return Arch.str();
  return (Arch + " (" + Mach + ")").str();
}

// There are (arguably too) many MIPS ISAs out there. Their relationships
// can be represented as a forest. If all input files have ISAs which
// reachable by repeated proceeding from the single child to the parent,
// these input files are compatible. In that case we need to return "highest"
// ISA. If there are incompatible input files, we show an error.
// For example, mips1 is a "parent" of mips2 and such files are compatible.
// Output file gets EF_MIPS_ARCH_2 flag. From the other side mips3 and mips32
// are incompatible because nor mips3 is a parent for misp32, nor mips32
// is a parent for mips3.
static uint32_t getArchFlags(ArrayRef<FileFlags> Files) {
  uint32_t Ret = Files[0].Flags & (EF_MIPS_ARCH | EF_MIPS_MACH);

  for (const FileFlags &F : Files.slice(1)) {
    uint32_t New = F.Flags & (EF_MIPS_ARCH | EF_MIPS_MACH);

    // Check ISA compatibility.
    if (isArchMatched(New, Ret))
      continue;
    if (!isArchMatched(Ret, New)) {
      error("incompatible target ISA:\n>>> " + toString(Files[0].File) + ": " +
            getFullArchName(Ret) + "\n>>> " + toString(F.File) + ": " +
            getFullArchName(New));
      return 0;
    }
    Ret = New;
  }
  return Ret;
}

template <class ELFT> uint32_t elf::calcMipsEFlags() {
  std::vector<FileFlags> V;
  for (InputFile *F : ObjectFiles)
    V.push_back({F, cast<ObjFile<ELFT>>(F)->getObj().getHeader()->e_flags});
  if (V.empty())
    return 0;
  checkFlags(V);
  return getMiscFlags(V) | getPicFlags(V) | getArchFlags(V);
}

static int compareMipsFpAbi(uint8_t FpA, uint8_t FpB) {
  if (FpA == FpB)
    return 0;
  if (FpB == Mips::Val_GNU_MIPS_ABI_FP_ANY)
    return 1;
  if (FpB == Mips::Val_GNU_MIPS_ABI_FP_64A &&
      FpA == Mips::Val_GNU_MIPS_ABI_FP_64)
    return 1;
  if (FpB != Mips::Val_GNU_MIPS_ABI_FP_XX)
    return -1;
  if (FpA == Mips::Val_GNU_MIPS_ABI_FP_DOUBLE ||
      FpA == Mips::Val_GNU_MIPS_ABI_FP_64 ||
      FpA == Mips::Val_GNU_MIPS_ABI_FP_64A)
    return 1;
  return -1;
}

static StringRef getMipsFpAbiName(uint8_t FpAbi) {
  switch (FpAbi) {
  case Mips::Val_GNU_MIPS_ABI_FP_ANY:
    return "any";
  case Mips::Val_GNU_MIPS_ABI_FP_DOUBLE:
    return "-mdouble-float";
  case Mips::Val_GNU_MIPS_ABI_FP_SINGLE:
    return "-msingle-float";
  case Mips::Val_GNU_MIPS_ABI_FP_SOFT:
    return "-msoft-float";
  case Mips::Val_GNU_MIPS_ABI_FP_OLD_64:
    return "-mgp32 -mfp64 (old)";
  case Mips::Val_GNU_MIPS_ABI_FP_XX:
    return "-mfpxx";
  case Mips::Val_GNU_MIPS_ABI_FP_64:
    return "-mgp32 -mfp64";
  case Mips::Val_GNU_MIPS_ABI_FP_64A:
    return "-mgp32 -mfp64 -mno-odd-spreg";
  default:
    return "unknown";
  }
}

uint8_t elf::getMipsFpAbiFlag(uint8_t OldFlag, uint8_t NewFlag,
                              StringRef FileName) {
  if (compareMipsFpAbi(NewFlag, OldFlag) >= 0)
    return NewFlag;
  if (compareMipsFpAbi(OldFlag, NewFlag) < 0)
    error(FileName + ": floating point ABI '" + getMipsFpAbiName(NewFlag) +
          "' is incompatible with target floating point ABI '" +
          getMipsFpAbiName(OldFlag) + "'");
  return OldFlag;
}

template <class ELFT> static bool isN32Abi(const InputFile *F) {
  if (auto *EF = dyn_cast<ELFFileBase<ELFT>>(F))
    return EF->getObj().getHeader()->e_flags & EF_MIPS_ABI2;
  return false;
}

bool elf::isMipsN32Abi(const InputFile *F) {
  switch (Config->EKind) {
  case ELF32LEKind:
    return isN32Abi<ELF32LE>(F);
  case ELF32BEKind:
    return isN32Abi<ELF32BE>(F);
  case ELF64LEKind:
    return isN32Abi<ELF64LE>(F);
  case ELF64BEKind:
    return isN32Abi<ELF64BE>(F);
  default:
    llvm_unreachable("unknown Config->EKind");
  }
}

bool elf::isMicroMips() { return Config->EFlags & EF_MIPS_MICROMIPS; }

bool elf::isMipsR6() {
  uint32_t Arch = Config->EFlags & EF_MIPS_ARCH;
  return Arch == EF_MIPS_ARCH_32R6 || Arch == EF_MIPS_ARCH_64R6;
}

template uint32_t elf::calcMipsEFlags<ELF32LE>();
template uint32_t elf::calcMipsEFlags<ELF32BE>();
template uint32_t elf::calcMipsEFlags<ELF64LE>();
template uint32_t elf::calcMipsEFlags<ELF64BE>();
