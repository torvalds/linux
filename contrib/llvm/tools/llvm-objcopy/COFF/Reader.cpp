//===- Reader.cpp ---------------------------------------------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Reader.h"
#include "Object.h"
#include "llvm-objcopy.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstddef>
#include <cstdint>

namespace llvm {
namespace objcopy {
namespace coff {

using namespace object;

Error COFFReader::readExecutableHeaders(Object &Obj) const {
  const dos_header *DH = COFFObj.getDOSHeader();
  Obj.Is64 = COFFObj.is64();
  if (!DH)
    return Error::success();

  Obj.IsPE = true;
  Obj.DosHeader = *DH;
  if (DH->AddressOfNewExeHeader > sizeof(*DH))
    Obj.DosStub = ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(&DH[1]),
                                    DH->AddressOfNewExeHeader - sizeof(*DH));

  if (COFFObj.is64()) {
    const pe32plus_header *PE32Plus = nullptr;
    if (auto EC = COFFObj.getPE32PlusHeader(PE32Plus))
      return errorCodeToError(EC);
    Obj.PeHeader = *PE32Plus;
  } else {
    const pe32_header *PE32 = nullptr;
    if (auto EC = COFFObj.getPE32Header(PE32))
      return errorCodeToError(EC);
    copyPeHeader(Obj.PeHeader, *PE32);
    // The pe32plus_header (stored in Object) lacks the BaseOfData field.
    Obj.BaseOfData = PE32->BaseOfData;
  }

  for (size_t I = 0; I < Obj.PeHeader.NumberOfRvaAndSize; I++) {
    const data_directory *Dir;
    if (auto EC = COFFObj.getDataDirectory(I, Dir))
      return errorCodeToError(EC);
    Obj.DataDirectories.emplace_back(*Dir);
  }
  return Error::success();
}

Error COFFReader::readSections(Object &Obj) const {
  // Section indexing starts from 1.
  for (size_t I = 1, E = COFFObj.getNumberOfSections(); I <= E; I++) {
    const coff_section *Sec;
    if (auto EC = COFFObj.getSection(I, Sec))
      return errorCodeToError(EC);
    Obj.Sections.push_back(Section());
    Section &S = Obj.Sections.back();
    S.Header = *Sec;
    if (auto EC = COFFObj.getSectionContents(Sec, S.Contents))
      return errorCodeToError(EC);
    ArrayRef<coff_relocation> Relocs = COFFObj.getRelocations(Sec);
    for (const coff_relocation &R : Relocs)
      S.Relocs.push_back(R);
    if (auto EC = COFFObj.getSectionName(Sec, S.Name))
      return errorCodeToError(EC);
    if (Sec->hasExtendedRelocations())
      return make_error<StringError>("Extended relocations not supported yet",
                                     object_error::parse_failed);
  }
  return Error::success();
}

Error COFFReader::readSymbols(Object &Obj, bool IsBigObj) const {
  std::vector<Symbol> Symbols;
  Symbols.reserve(COFFObj.getRawNumberOfSymbols());
  for (uint32_t I = 0, E = COFFObj.getRawNumberOfSymbols(); I < E;) {
    Expected<COFFSymbolRef> SymOrErr = COFFObj.getSymbol(I);
    if (!SymOrErr)
      return SymOrErr.takeError();
    COFFSymbolRef SymRef = *SymOrErr;

    Symbols.push_back(Symbol());
    Symbol &Sym = Symbols.back();
    // Copy symbols from the original form into an intermediate coff_symbol32.
    if (IsBigObj)
      copySymbol(Sym.Sym,
                 *reinterpret_cast<const coff_symbol32 *>(SymRef.getRawPtr()));
    else
      copySymbol(Sym.Sym,
                 *reinterpret_cast<const coff_symbol16 *>(SymRef.getRawPtr()));
    if (auto EC = COFFObj.getSymbolName(SymRef, Sym.Name))
      return errorCodeToError(EC);
    Sym.AuxData = COFFObj.getSymbolAuxData(SymRef);
    assert((Sym.AuxData.size() %
            (IsBigObj ? sizeof(coff_symbol32) : sizeof(coff_symbol16))) == 0);
    I += 1 + SymRef.getNumberOfAuxSymbols();
  }
  Obj.addSymbols(Symbols);
  return Error::success();
}

Error COFFReader::setRelocTargets(Object &Obj) const {
  std::vector<const Symbol *> RawSymbolTable;
  for (const Symbol &Sym : Obj.getSymbols()) {
    RawSymbolTable.push_back(&Sym);
    for (size_t I = 0; I < Sym.Sym.NumberOfAuxSymbols; I++)
      RawSymbolTable.push_back(nullptr);
  }
  for (Section &Sec : Obj.Sections) {
    for (Relocation &R : Sec.Relocs) {
      if (R.Reloc.SymbolTableIndex >= RawSymbolTable.size())
        return make_error<StringError>("SymbolTableIndex out of range",
                                       object_error::parse_failed);
      const Symbol *Sym = RawSymbolTable[R.Reloc.SymbolTableIndex];
      if (Sym == nullptr)
        return make_error<StringError>("Invalid SymbolTableIndex",
                                       object_error::parse_failed);
      R.Target = Sym->UniqueId;
      R.TargetName = Sym->Name;
    }
  }
  return Error::success();
}

Expected<std::unique_ptr<Object>> COFFReader::create() const {
  auto Obj = llvm::make_unique<Object>();

  const coff_file_header *CFH = nullptr;
  const coff_bigobj_file_header *CBFH = nullptr;
  COFFObj.getCOFFHeader(CFH);
  COFFObj.getCOFFBigObjHeader(CBFH);
  bool IsBigObj = false;
  if (CFH) {
    Obj->CoffFileHeader = *CFH;
  } else {
    if (!CBFH)
      return make_error<StringError>("No COFF file header returned",
                                     object_error::parse_failed);
    // Only copying the few fields from the bigobj header that we need
    // and won't recreate in the end.
    Obj->CoffFileHeader.Machine = CBFH->Machine;
    Obj->CoffFileHeader.TimeDateStamp = CBFH->TimeDateStamp;
    IsBigObj = true;
  }

  if (Error E = readExecutableHeaders(*Obj))
    return std::move(E);
  if (Error E = readSections(*Obj))
    return std::move(E);
  if (Error E = readSymbols(*Obj, IsBigObj))
    return std::move(E);
  if (Error E = setRelocTargets(*Obj))
    return std::move(E);

  return std::move(Obj);
}

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm
