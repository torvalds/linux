//===- Symbols.cpp --------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "InputFiles.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;

// Returns a symbol name for an error message.
std::string lld::toString(coff::Symbol &B) {
  if (Optional<std::string> S = lld::demangleMSVC(B.getName()))
    return ("\"" + *S + "\" (" + B.getName() + ")").str();
  return B.getName();
}

namespace lld {
namespace coff {

StringRef Symbol::getName() {
  // COFF symbol names are read lazily for a performance reason.
  // Non-external symbol names are never used by the linker except for logging
  // or debugging. Their internal references are resolved not by name but by
  // symbol index. And because they are not external, no one can refer them by
  // name. Object files contain lots of non-external symbols, and creating
  // StringRefs for them (which involves lots of strlen() on the string table)
  // is a waste of time.
  if (Name.empty()) {
    auto *D = cast<DefinedCOFF>(this);
    cast<ObjFile>(D->File)->getCOFFObj()->getSymbolName(D->Sym, Name);
  }
  return Name;
}

InputFile *Symbol::getFile() {
  if (auto *Sym = dyn_cast<DefinedCOFF>(this))
    return Sym->File;
  if (auto *Sym = dyn_cast<Lazy>(this))
    return Sym->File;
  return nullptr;
}

bool Symbol::isLive() const {
  if (auto *R = dyn_cast<DefinedRegular>(this))
    return R->getChunk()->Live;
  if (auto *Imp = dyn_cast<DefinedImportData>(this))
    return Imp->File->Live;
  if (auto *Imp = dyn_cast<DefinedImportThunk>(this))
    return Imp->WrappedSym->File->ThunkLive;
  // Assume any other kind of symbol is live.
  return true;
}

// MinGW specific.
void Symbol::replaceKeepingName(Symbol *Other, size_t Size) {
  StringRef OrigName = Name;
  memcpy(this, Other, Size);
  Name = OrigName;
}

COFFSymbolRef DefinedCOFF::getCOFFSymbol() {
  size_t SymSize = cast<ObjFile>(File)->getCOFFObj()->getSymbolTableEntrySize();
  if (SymSize == sizeof(coff_symbol16))
    return COFFSymbolRef(reinterpret_cast<const coff_symbol16 *>(Sym));
  assert(SymSize == sizeof(coff_symbol32));
  return COFFSymbolRef(reinterpret_cast<const coff_symbol32 *>(Sym));
}

uint16_t DefinedAbsolute::NumOutputSections;

static Chunk *makeImportThunk(DefinedImportData *S, uint16_t Machine) {
  if (Machine == AMD64)
    return make<ImportThunkChunkX64>(S);
  if (Machine == I386)
    return make<ImportThunkChunkX86>(S);
  if (Machine == ARM64)
    return make<ImportThunkChunkARM64>(S);
  assert(Machine == ARMNT);
  return make<ImportThunkChunkARM>(S);
}

DefinedImportThunk::DefinedImportThunk(StringRef Name, DefinedImportData *S,
                                       uint16_t Machine)
    : Defined(DefinedImportThunkKind, Name), WrappedSym(S),
      Data(makeImportThunk(S, Machine)) {}

Defined *Undefined::getWeakAlias() {
  // A weak alias may be a weak alias to another symbol, so check recursively.
  for (Symbol *A = WeakAlias; A; A = cast<Undefined>(A)->WeakAlias)
    if (auto *D = dyn_cast<Defined>(A))
      return D;
  return nullptr;
}
} // namespace coff
} // namespace lld
