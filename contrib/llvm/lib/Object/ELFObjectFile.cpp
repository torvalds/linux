//===- ELFObjectFile.cpp - ELF object file implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Part of the ELFObjectFile class implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ELFObjectFile.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/ARMAttributeParser.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

using namespace llvm;
using namespace object;

ELFObjectFileBase::ELFObjectFileBase(unsigned int Type, MemoryBufferRef Source)
    : ObjectFile(Type, Source) {}

template <class ELFT>
static Expected<std::unique_ptr<ELFObjectFile<ELFT>>>
createPtr(MemoryBufferRef Object) {
  auto Ret = ELFObjectFile<ELFT>::create(Object);
  if (Error E = Ret.takeError())
    return std::move(E);
  return make_unique<ELFObjectFile<ELFT>>(std::move(*Ret));
}

Expected<std::unique_ptr<ObjectFile>>
ObjectFile::createELFObjectFile(MemoryBufferRef Obj) {
  std::pair<unsigned char, unsigned char> Ident =
      getElfArchType(Obj.getBuffer());
  std::size_t MaxAlignment =
      1ULL << countTrailingZeros(uintptr_t(Obj.getBufferStart()));

  if (MaxAlignment < 2)
    return createError("Insufficient alignment");

  if (Ident.first == ELF::ELFCLASS32) {
    if (Ident.second == ELF::ELFDATA2LSB)
      return createPtr<ELF32LE>(Obj);
    else if (Ident.second == ELF::ELFDATA2MSB)
      return createPtr<ELF32BE>(Obj);
    else
      return createError("Invalid ELF data");
  } else if (Ident.first == ELF::ELFCLASS64) {
    if (Ident.second == ELF::ELFDATA2LSB)
      return createPtr<ELF64LE>(Obj);
    else if (Ident.second == ELF::ELFDATA2MSB)
      return createPtr<ELF64BE>(Obj);
    else
      return createError("Invalid ELF data");
  }
  return createError("Invalid ELF class");
}

SubtargetFeatures ELFObjectFileBase::getMIPSFeatures() const {
  SubtargetFeatures Features;
  unsigned PlatformFlags = getPlatformFlags();

  switch (PlatformFlags & ELF::EF_MIPS_ARCH) {
  case ELF::EF_MIPS_ARCH_1:
    break;
  case ELF::EF_MIPS_ARCH_2:
    Features.AddFeature("mips2");
    break;
  case ELF::EF_MIPS_ARCH_3:
    Features.AddFeature("mips3");
    break;
  case ELF::EF_MIPS_ARCH_4:
    Features.AddFeature("mips4");
    break;
  case ELF::EF_MIPS_ARCH_5:
    Features.AddFeature("mips5");
    break;
  case ELF::EF_MIPS_ARCH_32:
    Features.AddFeature("mips32");
    break;
  case ELF::EF_MIPS_ARCH_64:
    Features.AddFeature("mips64");
    break;
  case ELF::EF_MIPS_ARCH_32R2:
    Features.AddFeature("mips32r2");
    break;
  case ELF::EF_MIPS_ARCH_64R2:
    Features.AddFeature("mips64r2");
    break;
  case ELF::EF_MIPS_ARCH_32R6:
    Features.AddFeature("mips32r6");
    break;
  case ELF::EF_MIPS_ARCH_64R6:
    Features.AddFeature("mips64r6");
    break;
  default:
    llvm_unreachable("Unknown EF_MIPS_ARCH value");
  }

  switch (PlatformFlags & ELF::EF_MIPS_MACH) {
  case ELF::EF_MIPS_MACH_NONE:
    // No feature associated with this value.
    break;
  case ELF::EF_MIPS_MACH_OCTEON:
    Features.AddFeature("cnmips");
    break;
  default:
    llvm_unreachable("Unknown EF_MIPS_ARCH value");
  }

  if (PlatformFlags & ELF::EF_MIPS_ARCH_ASE_M16)
    Features.AddFeature("mips16");
  if (PlatformFlags & ELF::EF_MIPS_MICROMIPS)
    Features.AddFeature("micromips");

  return Features;
}

SubtargetFeatures ELFObjectFileBase::getARMFeatures() const {
  SubtargetFeatures Features;
  ARMAttributeParser Attributes;
  std::error_code EC = getBuildAttributes(Attributes);
  if (EC)
    return SubtargetFeatures();

  // both ARMv7-M and R have to support thumb hardware div
  bool isV7 = false;
  if (Attributes.hasAttribute(ARMBuildAttrs::CPU_arch))
    isV7 = Attributes.getAttributeValue(ARMBuildAttrs::CPU_arch)
      == ARMBuildAttrs::v7;

  if (Attributes.hasAttribute(ARMBuildAttrs::CPU_arch_profile)) {
    switch(Attributes.getAttributeValue(ARMBuildAttrs::CPU_arch_profile)) {
    case ARMBuildAttrs::ApplicationProfile:
      Features.AddFeature("aclass");
      break;
    case ARMBuildAttrs::RealTimeProfile:
      Features.AddFeature("rclass");
      if (isV7)
        Features.AddFeature("hwdiv");
      break;
    case ARMBuildAttrs::MicroControllerProfile:
      Features.AddFeature("mclass");
      if (isV7)
        Features.AddFeature("hwdiv");
      break;
    }
  }

  if (Attributes.hasAttribute(ARMBuildAttrs::THUMB_ISA_use)) {
    switch(Attributes.getAttributeValue(ARMBuildAttrs::THUMB_ISA_use)) {
    default:
      break;
    case ARMBuildAttrs::Not_Allowed:
      Features.AddFeature("thumb", false);
      Features.AddFeature("thumb2", false);
      break;
    case ARMBuildAttrs::AllowThumb32:
      Features.AddFeature("thumb2");
      break;
    }
  }

  if (Attributes.hasAttribute(ARMBuildAttrs::FP_arch)) {
    switch(Attributes.getAttributeValue(ARMBuildAttrs::FP_arch)) {
    default:
      break;
    case ARMBuildAttrs::Not_Allowed:
      Features.AddFeature("vfp2", false);
      Features.AddFeature("vfp3", false);
      Features.AddFeature("vfp4", false);
      break;
    case ARMBuildAttrs::AllowFPv2:
      Features.AddFeature("vfp2");
      break;
    case ARMBuildAttrs::AllowFPv3A:
    case ARMBuildAttrs::AllowFPv3B:
      Features.AddFeature("vfp3");
      break;
    case ARMBuildAttrs::AllowFPv4A:
    case ARMBuildAttrs::AllowFPv4B:
      Features.AddFeature("vfp4");
      break;
    }
  }

  if (Attributes.hasAttribute(ARMBuildAttrs::Advanced_SIMD_arch)) {
    switch(Attributes.getAttributeValue(ARMBuildAttrs::Advanced_SIMD_arch)) {
    default:
      break;
    case ARMBuildAttrs::Not_Allowed:
      Features.AddFeature("neon", false);
      Features.AddFeature("fp16", false);
      break;
    case ARMBuildAttrs::AllowNeon:
      Features.AddFeature("neon");
      break;
    case ARMBuildAttrs::AllowNeon2:
      Features.AddFeature("neon");
      Features.AddFeature("fp16");
      break;
    }
  }

  if (Attributes.hasAttribute(ARMBuildAttrs::DIV_use)) {
    switch(Attributes.getAttributeValue(ARMBuildAttrs::DIV_use)) {
    default:
      break;
    case ARMBuildAttrs::DisallowDIV:
      Features.AddFeature("hwdiv", false);
      Features.AddFeature("hwdiv-arm", false);
      break;
    case ARMBuildAttrs::AllowDIVExt:
      Features.AddFeature("hwdiv");
      Features.AddFeature("hwdiv-arm");
      break;
    }
  }

  return Features;
}

SubtargetFeatures ELFObjectFileBase::getRISCVFeatures() const {
  SubtargetFeatures Features;
  unsigned PlatformFlags = getPlatformFlags();

  if (PlatformFlags & ELF::EF_RISCV_RVC) {
    Features.AddFeature("c");
  }

  return Features;
}

SubtargetFeatures ELFObjectFileBase::getFeatures() const {
  switch (getEMachine()) {
  case ELF::EM_MIPS:
    return getMIPSFeatures();
  case ELF::EM_ARM:
    return getARMFeatures();
  case ELF::EM_RISCV:
    return getRISCVFeatures();
  default:
    return SubtargetFeatures();
  }
}

// FIXME Encode from a tablegen description or target parser.
void ELFObjectFileBase::setARMSubArch(Triple &TheTriple) const {
  if (TheTriple.getSubArch() != Triple::NoSubArch)
    return;

  ARMAttributeParser Attributes;
  std::error_code EC = getBuildAttributes(Attributes);
  if (EC)
    return;

  std::string Triple;
  // Default to ARM, but use the triple if it's been set.
  if (TheTriple.isThumb())
    Triple = "thumb";
  else
    Triple = "arm";

  if (Attributes.hasAttribute(ARMBuildAttrs::CPU_arch)) {
    switch(Attributes.getAttributeValue(ARMBuildAttrs::CPU_arch)) {
    case ARMBuildAttrs::v4:
      Triple += "v4";
      break;
    case ARMBuildAttrs::v4T:
      Triple += "v4t";
      break;
    case ARMBuildAttrs::v5T:
      Triple += "v5t";
      break;
    case ARMBuildAttrs::v5TE:
      Triple += "v5te";
      break;
    case ARMBuildAttrs::v5TEJ:
      Triple += "v5tej";
      break;
    case ARMBuildAttrs::v6:
      Triple += "v6";
      break;
    case ARMBuildAttrs::v6KZ:
      Triple += "v6kz";
      break;
    case ARMBuildAttrs::v6T2:
      Triple += "v6t2";
      break;
    case ARMBuildAttrs::v6K:
      Triple += "v6k";
      break;
    case ARMBuildAttrs::v7:
      Triple += "v7";
      break;
    case ARMBuildAttrs::v6_M:
      Triple += "v6m";
      break;
    case ARMBuildAttrs::v6S_M:
      Triple += "v6sm";
      break;
    case ARMBuildAttrs::v7E_M:
      Triple += "v7em";
      break;
    }
  }
  if (!isLittleEndian())
    Triple += "eb";

  TheTriple.setArchName(Triple);
}

std::vector<std::pair<DataRefImpl, uint64_t>>
ELFObjectFileBase::getPltAddresses() const {
  std::string Err;
  const auto Triple = makeTriple();
  const auto *T = TargetRegistry::lookupTarget(Triple.str(), Err);
  if (!T)
    return {};
  uint64_t JumpSlotReloc = 0;
  switch (Triple.getArch()) {
    case Triple::x86:
      JumpSlotReloc = ELF::R_386_JUMP_SLOT;
      break;
    case Triple::x86_64:
      JumpSlotReloc = ELF::R_X86_64_JUMP_SLOT;
      break;
    case Triple::aarch64:
      JumpSlotReloc = ELF::R_AARCH64_JUMP_SLOT;
      break;
    default:
      return {};
  }
  std::unique_ptr<const MCInstrInfo> MII(T->createMCInstrInfo());
  std::unique_ptr<const MCInstrAnalysis> MIA(
      T->createMCInstrAnalysis(MII.get()));
  if (!MIA)
    return {};
  Optional<SectionRef> Plt = None, RelaPlt = None, GotPlt = None;
  for (const SectionRef &Section : sections()) {
    StringRef Name;
    if (Section.getName(Name))
      continue;
    if (Name == ".plt")
      Plt = Section;
    else if (Name == ".rela.plt" || Name == ".rel.plt")
      RelaPlt = Section;
    else if (Name == ".got.plt")
      GotPlt = Section;
  }
  if (!Plt || !RelaPlt || !GotPlt)
    return {};
  StringRef PltContents;
  if (Plt->getContents(PltContents))
    return {};
  ArrayRef<uint8_t> PltBytes((const uint8_t *)PltContents.data(),
                             Plt->getSize());
  auto PltEntries = MIA->findPltEntries(Plt->getAddress(), PltBytes,
                                        GotPlt->getAddress(), Triple);
  // Build a map from GOT entry virtual address to PLT entry virtual address.
  DenseMap<uint64_t, uint64_t> GotToPlt;
  for (const auto &Entry : PltEntries)
    GotToPlt.insert(std::make_pair(Entry.second, Entry.first));
  // Find the relocations in the dynamic relocation table that point to
  // locations in the GOT for which we know the corresponding PLT entry.
  std::vector<std::pair<DataRefImpl, uint64_t>> Result;
  for (const auto &Relocation : RelaPlt->relocations()) {
    if (Relocation.getType() != JumpSlotReloc)
      continue;
    auto PltEntryIter = GotToPlt.find(Relocation.getOffset());
    if (PltEntryIter != GotToPlt.end())
      Result.push_back(std::make_pair(
          Relocation.getSymbol()->getRawDataRefImpl(), PltEntryIter->second));
  }
  return Result;
}
