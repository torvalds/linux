//===- SymbolizableObjectFile.cpp -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of SymbolizableObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "SymbolizableObjectFile.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;
using namespace object;
using namespace symbolize;

static DILineInfoSpecifier
getDILineInfoSpecifier(FunctionNameKind FNKind) {
  return DILineInfoSpecifier(
      DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, FNKind);
}

ErrorOr<std::unique_ptr<SymbolizableObjectFile>>
SymbolizableObjectFile::create(object::ObjectFile *Obj,
                               std::unique_ptr<DIContext> DICtx) {
  std::unique_ptr<SymbolizableObjectFile> res(
      new SymbolizableObjectFile(Obj, std::move(DICtx)));
  std::unique_ptr<DataExtractor> OpdExtractor;
  uint64_t OpdAddress = 0;
  // Find the .opd (function descriptor) section if any, for big-endian
  // PowerPC64 ELF.
  if (Obj->getArch() == Triple::ppc64) {
    for (section_iterator Section : Obj->sections()) {
      StringRef Name;
      StringRef Data;
      if (auto EC = Section->getName(Name))
        return EC;
      if (Name == ".opd") {
        if (auto EC = Section->getContents(Data))
          return EC;
        OpdExtractor.reset(new DataExtractor(Data, Obj->isLittleEndian(),
                                             Obj->getBytesInAddress()));
        OpdAddress = Section->getAddress();
        break;
      }
    }
  }
  std::vector<std::pair<SymbolRef, uint64_t>> Symbols =
      computeSymbolSizes(*Obj);
  for (auto &P : Symbols)
    res->addSymbol(P.first, P.second, OpdExtractor.get(), OpdAddress);

  // If this is a COFF object and we didn't find any symbols, try the export
  // table.
  if (Symbols.empty()) {
    if (auto *CoffObj = dyn_cast<COFFObjectFile>(Obj))
      if (auto EC = res->addCoffExportSymbols(CoffObj))
        return EC;
  }
  return std::move(res);
}

SymbolizableObjectFile::SymbolizableObjectFile(ObjectFile *Obj,
                                               std::unique_ptr<DIContext> DICtx)
    : Module(Obj), DebugInfoContext(std::move(DICtx)) {}

namespace {

struct OffsetNamePair {
  uint32_t Offset;
  StringRef Name;

  bool operator<(const OffsetNamePair &R) const {
    return Offset < R.Offset;
  }
};

} // end anonymous namespace

std::error_code SymbolizableObjectFile::addCoffExportSymbols(
    const COFFObjectFile *CoffObj) {
  // Get all export names and offsets.
  std::vector<OffsetNamePair> ExportSyms;
  for (const ExportDirectoryEntryRef &Ref : CoffObj->export_directories()) {
    StringRef Name;
    uint32_t Offset;
    if (auto EC = Ref.getSymbolName(Name))
      return EC;
    if (auto EC = Ref.getExportRVA(Offset))
      return EC;
    ExportSyms.push_back(OffsetNamePair{Offset, Name});
  }
  if (ExportSyms.empty())
    return std::error_code();

  // Sort by ascending offset.
  array_pod_sort(ExportSyms.begin(), ExportSyms.end());

  // Approximate the symbol sizes by assuming they run to the next symbol.
  // FIXME: This assumes all exports are functions.
  uint64_t ImageBase = CoffObj->getImageBase();
  for (auto I = ExportSyms.begin(), E = ExportSyms.end(); I != E; ++I) {
    OffsetNamePair &Export = *I;
    // FIXME: The last export has a one byte size now.
    uint32_t NextOffset = I != E ? I->Offset : Export.Offset + 1;
    uint64_t SymbolStart = ImageBase + Export.Offset;
    uint64_t SymbolSize = NextOffset - Export.Offset;
    SymbolDesc SD = {SymbolStart, SymbolSize};
    Functions.insert(std::make_pair(SD, Export.Name));
  }
  return std::error_code();
}

std::error_code SymbolizableObjectFile::addSymbol(const SymbolRef &Symbol,
                                                  uint64_t SymbolSize,
                                                  DataExtractor *OpdExtractor,
                                                  uint64_t OpdAddress) {
  Expected<SymbolRef::Type> SymbolTypeOrErr = Symbol.getType();
  if (!SymbolTypeOrErr)
    return errorToErrorCode(SymbolTypeOrErr.takeError());
  SymbolRef::Type SymbolType = *SymbolTypeOrErr;
  if (SymbolType != SymbolRef::ST_Function && SymbolType != SymbolRef::ST_Data)
    return std::error_code();
  Expected<uint64_t> SymbolAddressOrErr = Symbol.getAddress();
  if (!SymbolAddressOrErr)
    return errorToErrorCode(SymbolAddressOrErr.takeError());
  uint64_t SymbolAddress = *SymbolAddressOrErr;
  if (OpdExtractor) {
    // For big-endian PowerPC64 ELF, symbols in the .opd section refer to
    // function descriptors. The first word of the descriptor is a pointer to
    // the function's code.
    // For the purposes of symbolization, pretend the symbol's address is that
    // of the function's code, not the descriptor.
    uint64_t OpdOffset = SymbolAddress - OpdAddress;
    uint32_t OpdOffset32 = OpdOffset;
    if (OpdOffset == OpdOffset32 &&
        OpdExtractor->isValidOffsetForAddress(OpdOffset32))
      SymbolAddress = OpdExtractor->getAddress(&OpdOffset32);
  }
  Expected<StringRef> SymbolNameOrErr = Symbol.getName();
  if (!SymbolNameOrErr)
    return errorToErrorCode(SymbolNameOrErr.takeError());
  StringRef SymbolName = *SymbolNameOrErr;
  // Mach-O symbol table names have leading underscore, skip it.
  if (Module->isMachO() && !SymbolName.empty() && SymbolName[0] == '_')
    SymbolName = SymbolName.drop_front();
  // FIXME: If a function has alias, there are two entries in symbol table
  // with same address size. Make sure we choose the correct one.
  auto &M = SymbolType == SymbolRef::ST_Function ? Functions : Objects;
  SymbolDesc SD = { SymbolAddress, SymbolSize };
  M.insert(std::make_pair(SD, SymbolName));
  return std::error_code();
}

// Return true if this is a 32-bit x86 PE COFF module.
bool SymbolizableObjectFile::isWin32Module() const {
  auto *CoffObject = dyn_cast<COFFObjectFile>(Module);
  return CoffObject && CoffObject->getMachine() == COFF::IMAGE_FILE_MACHINE_I386;
}

uint64_t SymbolizableObjectFile::getModulePreferredBase() const {
  if (auto *CoffObject = dyn_cast<COFFObjectFile>(Module))
    return CoffObject->getImageBase();
  return 0;
}

bool SymbolizableObjectFile::getNameFromSymbolTable(SymbolRef::Type Type,
                                                    uint64_t Address,
                                                    std::string &Name,
                                                    uint64_t &Addr,
                                                    uint64_t &Size) const {
  const auto &SymbolMap = Type == SymbolRef::ST_Function ? Functions : Objects;
  if (SymbolMap.empty())
    return false;
  SymbolDesc SD = { Address, Address };
  auto SymbolIterator = SymbolMap.upper_bound(SD);
  if (SymbolIterator == SymbolMap.begin())
    return false;
  --SymbolIterator;
  if (SymbolIterator->first.Size != 0 &&
      SymbolIterator->first.Addr + SymbolIterator->first.Size <= Address)
    return false;
  Name = SymbolIterator->second.str();
  Addr = SymbolIterator->first.Addr;
  Size = SymbolIterator->first.Size;
  return true;
}

bool SymbolizableObjectFile::shouldOverrideWithSymbolTable(
    FunctionNameKind FNKind, bool UseSymbolTable) const {
  // When DWARF is used with -gline-tables-only / -gmlt, the symbol table gives
  // better answers for linkage names than the DIContext. Otherwise, we are
  // probably using PEs and PDBs, and we shouldn't do the override. PE files
  // generally only contain the names of exported symbols.
  return FNKind == FunctionNameKind::LinkageName && UseSymbolTable &&
         isa<DWARFContext>(DebugInfoContext.get());
}

DILineInfo SymbolizableObjectFile::symbolizeCode(uint64_t ModuleOffset,
                                                 FunctionNameKind FNKind,
                                                 bool UseSymbolTable) const {
  DILineInfo LineInfo;
  if (DebugInfoContext) {
    LineInfo = DebugInfoContext->getLineInfoForAddress(
        ModuleOffset, getDILineInfoSpecifier(FNKind));
  }
  // Override function name from symbol table if necessary.
  if (shouldOverrideWithSymbolTable(FNKind, UseSymbolTable)) {
    std::string FunctionName;
    uint64_t Start, Size;
    if (getNameFromSymbolTable(SymbolRef::ST_Function, ModuleOffset,
                               FunctionName, Start, Size)) {
      LineInfo.FunctionName = FunctionName;
    }
  }
  return LineInfo;
}

DIInliningInfo SymbolizableObjectFile::symbolizeInlinedCode(
    uint64_t ModuleOffset, FunctionNameKind FNKind, bool UseSymbolTable) const {
  DIInliningInfo InlinedContext;

  if (DebugInfoContext)
    InlinedContext = DebugInfoContext->getInliningInfoForAddress(
        ModuleOffset, getDILineInfoSpecifier(FNKind));
  // Make sure there is at least one frame in context.
  if (InlinedContext.getNumberOfFrames() == 0)
    InlinedContext.addFrame(DILineInfo());

  // Override the function name in lower frame with name from symbol table.
  if (shouldOverrideWithSymbolTable(FNKind, UseSymbolTable)) {
    std::string FunctionName;
    uint64_t Start, Size;
    if (getNameFromSymbolTable(SymbolRef::ST_Function, ModuleOffset,
                               FunctionName, Start, Size)) {
      InlinedContext.getMutableFrame(InlinedContext.getNumberOfFrames() - 1)
          ->FunctionName = FunctionName;
    }
  }

  return InlinedContext;
}

DIGlobal SymbolizableObjectFile::symbolizeData(uint64_t ModuleOffset) const {
  DIGlobal Res;
  getNameFromSymbolTable(SymbolRef::ST_Data, ModuleOffset, Res.Name, Res.Start,
                         Res.Size);
  return Res;
}
