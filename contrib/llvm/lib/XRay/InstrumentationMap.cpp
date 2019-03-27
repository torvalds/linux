//===- InstrumentationMap.cpp - XRay Instrumentation Map ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the InstrumentationMap type for XRay sleds.
//
//===----------------------------------------------------------------------===//

#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/YAMLTraits.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace xray;

Optional<int32_t> InstrumentationMap::getFunctionId(uint64_t Addr) const {
  auto I = FunctionIds.find(Addr);
  if (I != FunctionIds.end())
    return I->second;
  return None;
}

Optional<uint64_t> InstrumentationMap::getFunctionAddr(int32_t FuncId) const {
  auto I = FunctionAddresses.find(FuncId);
  if (I != FunctionAddresses.end())
    return I->second;
  return None;
}

using RelocMap = DenseMap<uint64_t, uint64_t>;

static Error
loadObj(StringRef Filename, object::OwningBinary<object::ObjectFile> &ObjFile,
          InstrumentationMap::SledContainer &Sleds,
          InstrumentationMap::FunctionAddressMap &FunctionAddresses,
          InstrumentationMap::FunctionAddressReverseMap &FunctionIds) {
  InstrumentationMap Map;

  // Find the section named "xray_instr_map".
  if ((!ObjFile.getBinary()->isELF() && !ObjFile.getBinary()->isMachO()) ||
      !(ObjFile.getBinary()->getArch() == Triple::x86_64 ||
        ObjFile.getBinary()->getArch() == Triple::ppc64le))
    return make_error<StringError>(
        "File format not supported (only does ELF and Mach-O little endian 64-bit).",
        std::make_error_code(std::errc::not_supported));

  StringRef Contents = "";
  const auto &Sections = ObjFile.getBinary()->sections();
  auto I = llvm::find_if(Sections, [&](object::SectionRef Section) {
    StringRef Name = "";
    if (Section.getName(Name))
      return false;
    return Name == "xray_instr_map";
  });

  if (I == Sections.end())
    return make_error<StringError>(
        "Failed to find XRay instrumentation map.",
        std::make_error_code(std::errc::executable_format_error));

  if (I->getContents(Contents))
    return errorCodeToError(
        std::make_error_code(std::errc::executable_format_error));

  RelocMap Relocs;
  if (ObjFile.getBinary()->isELF()) {
    uint32_t RelativeRelocation = [](object::ObjectFile *ObjFile) {
      if (const auto *ELFObj = dyn_cast<object::ELF32LEObjectFile>(ObjFile))
        return ELFObj->getELFFile()->getRelativeRelocationType();
      else if (const auto *ELFObj = dyn_cast<object::ELF32BEObjectFile>(ObjFile))
        return ELFObj->getELFFile()->getRelativeRelocationType();
      else if (const auto *ELFObj = dyn_cast<object::ELF64LEObjectFile>(ObjFile))
        return ELFObj->getELFFile()->getRelativeRelocationType();
      else if (const auto *ELFObj = dyn_cast<object::ELF64BEObjectFile>(ObjFile))
        return ELFObj->getELFFile()->getRelativeRelocationType();
      else
        return static_cast<uint32_t>(0);
    }(ObjFile.getBinary());

    for (const object::SectionRef &Section : Sections) {
      for (const object::RelocationRef &Reloc : Section.relocations()) {
        if (Reloc.getType() != RelativeRelocation)
          continue;
        if (auto AddendOrErr = object::ELFRelocationRef(Reloc).getAddend())
          Relocs.insert({Reloc.getOffset(), *AddendOrErr});
      }
    }
  }

  // Copy the instrumentation map data into the Sleds data structure.
  auto C = Contents.bytes_begin();
  static constexpr size_t ELF64SledEntrySize = 32;

  if ((C - Contents.bytes_end()) % ELF64SledEntrySize != 0)
    return make_error<StringError>(
        Twine("Instrumentation map entries not evenly divisible by size of "
              "an XRay sled entry in ELF64."),
        std::make_error_code(std::errc::executable_format_error));

  auto RelocateOrElse = [&](uint32_t Offset, uint64_t Address) {
    if (!Address) {
      uint64_t A = I->getAddress() + C - Contents.bytes_begin() + Offset;
      RelocMap::const_iterator R = Relocs.find(A);
      if (R != Relocs.end())
        return R->second;
    }
    return Address;
  };

  int32_t FuncId = 1;
  uint64_t CurFn = 0;
  for (; C != Contents.bytes_end(); C += ELF64SledEntrySize) {
    DataExtractor Extractor(
        StringRef(reinterpret_cast<const char *>(C), ELF64SledEntrySize), true,
        8);
    Sleds.push_back({});
    auto &Entry = Sleds.back();
    uint32_t OffsetPtr = 0;
    uint32_t AddrOff = OffsetPtr;
    Entry.Address = RelocateOrElse(AddrOff, Extractor.getU64(&OffsetPtr));
    uint32_t FuncOff = OffsetPtr;
    Entry.Function = RelocateOrElse(FuncOff, Extractor.getU64(&OffsetPtr));
    auto Kind = Extractor.getU8(&OffsetPtr);
    static constexpr SledEntry::FunctionKinds Kinds[] = {
        SledEntry::FunctionKinds::ENTRY, SledEntry::FunctionKinds::EXIT,
        SledEntry::FunctionKinds::TAIL,
        SledEntry::FunctionKinds::LOG_ARGS_ENTER,
        SledEntry::FunctionKinds::CUSTOM_EVENT};
    if (Kind >= sizeof(Kinds))
      return errorCodeToError(
          std::make_error_code(std::errc::executable_format_error));
    Entry.Kind = Kinds[Kind];
    Entry.AlwaysInstrument = Extractor.getU8(&OffsetPtr) != 0;

    // We do replicate the function id generation scheme implemented in the
    // XRay runtime.
    // FIXME: Figure out how to keep this consistent with the XRay runtime.
    if (CurFn == 0) {
      CurFn = Entry.Function;
      FunctionAddresses[FuncId] = Entry.Function;
      FunctionIds[Entry.Function] = FuncId;
    }
    if (Entry.Function != CurFn) {
      ++FuncId;
      CurFn = Entry.Function;
      FunctionAddresses[FuncId] = Entry.Function;
      FunctionIds[Entry.Function] = FuncId;
    }
  }
  return Error::success();
}

static Error
loadYAML(int Fd, size_t FileSize, StringRef Filename,
         InstrumentationMap::SledContainer &Sleds,
         InstrumentationMap::FunctionAddressMap &FunctionAddresses,
         InstrumentationMap::FunctionAddressReverseMap &FunctionIds) {
  std::error_code EC;
  sys::fs::mapped_file_region MappedFile(
      Fd, sys::fs::mapped_file_region::mapmode::readonly, FileSize, 0, EC);
  if (EC)
    return make_error<StringError>(
        Twine("Failed memory-mapping file '") + Filename + "'.", EC);

  std::vector<YAMLXRaySledEntry> YAMLSleds;
  yaml::Input In(StringRef(MappedFile.data(), MappedFile.size()));
  In >> YAMLSleds;
  if (In.error())
    return make_error<StringError>(
        Twine("Failed loading YAML document from '") + Filename + "'.",
        In.error());

  Sleds.reserve(YAMLSleds.size());
  for (const auto &Y : YAMLSleds) {
    FunctionAddresses[Y.FuncId] = Y.Function;
    FunctionIds[Y.Function] = Y.FuncId;
    Sleds.push_back(
        SledEntry{Y.Address, Y.Function, Y.Kind, Y.AlwaysInstrument});
  }
  return Error::success();
}

// FIXME: Create error types that encapsulate a bit more information than what
// StringError instances contain.
Expected<InstrumentationMap>
llvm::xray::loadInstrumentationMap(StringRef Filename) {
  // At this point we assume the file is an object file -- and if that doesn't
  // work, we treat it as YAML.
  // FIXME: Extend to support non-ELF and non-x86_64 binaries.

  InstrumentationMap Map;
  auto ObjectFileOrError = object::ObjectFile::createObjectFile(Filename);
  if (!ObjectFileOrError) {
    auto E = ObjectFileOrError.takeError();
    // We try to load it as YAML if the ELF load didn't work.
    int Fd;
    if (sys::fs::openFileForRead(Filename, Fd))
      return std::move(E);

    uint64_t FileSize;
    if (sys::fs::file_size(Filename, FileSize))
      return std::move(E);

    // If the file is empty, we return the original error.
    if (FileSize == 0)
      return std::move(E);

    // From this point on the errors will be only for the YAML parts, so we
    // consume the errors at this point.
    consumeError(std::move(E));
    if (auto E = loadYAML(Fd, FileSize, Filename, Map.Sleds,
                          Map.FunctionAddresses, Map.FunctionIds))
      return std::move(E);
  } else if (auto E = loadObj(Filename, *ObjectFileOrError, Map.Sleds,
                                Map.FunctionAddresses, Map.FunctionIds)) {
    return std::move(E);
  }
  return Map;
}
