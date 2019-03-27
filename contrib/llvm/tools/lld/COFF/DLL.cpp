//===- DLL.cpp ------------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines various types of chunks for the DLL import or export
// descriptor tables. They are inherently Windows-specific.
// You need to read Microsoft PE/COFF spec to understand details
// about the data structures.
//
// If you are not particularly interested in linking against Windows
// DLL, you can skip this file, and you should still be able to
// understand the rest of the linker.
//
//===----------------------------------------------------------------------===//

#include "DLL.h"
#include "Chunks.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::COFF;

namespace lld {
namespace coff {
namespace {

// Import table

// A chunk for the import descriptor table.
class HintNameChunk : public Chunk {
public:
  HintNameChunk(StringRef N, uint16_t H) : Name(N), Hint(H) {}

  size_t getSize() const override {
    // Starts with 2 byte Hint field, followed by a null-terminated string,
    // ends with 0 or 1 byte padding.
    return alignTo(Name.size() + 3, 2);
  }

  void writeTo(uint8_t *Buf) const override {
    memset(Buf + OutputSectionOff, 0, getSize());
    write16le(Buf + OutputSectionOff, Hint);
    memcpy(Buf + OutputSectionOff + 2, Name.data(), Name.size());
  }

private:
  StringRef Name;
  uint16_t Hint;
};

// A chunk for the import descriptor table.
class LookupChunk : public Chunk {
public:
  explicit LookupChunk(Chunk *C) : HintName(C) { Alignment = Config->Wordsize; }
  size_t getSize() const override { return Config->Wordsize; }

  void writeTo(uint8_t *Buf) const override {
    if (Config->is64())
      write64le(Buf + OutputSectionOff, HintName->getRVA());
    else
      write32le(Buf + OutputSectionOff, HintName->getRVA());
  }

  Chunk *HintName;
};

// A chunk for the import descriptor table.
// This chunk represent import-by-ordinal symbols.
// See Microsoft PE/COFF spec 7.1. Import Header for details.
class OrdinalOnlyChunk : public Chunk {
public:
  explicit OrdinalOnlyChunk(uint16_t V) : Ordinal(V) {
    Alignment = Config->Wordsize;
  }
  size_t getSize() const override { return Config->Wordsize; }

  void writeTo(uint8_t *Buf) const override {
    // An import-by-ordinal slot has MSB 1 to indicate that
    // this is import-by-ordinal (and not import-by-name).
    if (Config->is64()) {
      write64le(Buf + OutputSectionOff, (1ULL << 63) | Ordinal);
    } else {
      write32le(Buf + OutputSectionOff, (1ULL << 31) | Ordinal);
    }
  }

  uint16_t Ordinal;
};

// A chunk for the import descriptor table.
class ImportDirectoryChunk : public Chunk {
public:
  explicit ImportDirectoryChunk(Chunk *N) : DLLName(N) {}
  size_t getSize() const override { return sizeof(ImportDirectoryTableEntry); }

  void writeTo(uint8_t *Buf) const override {
    memset(Buf + OutputSectionOff, 0, getSize());

    auto *E = (coff_import_directory_table_entry *)(Buf + OutputSectionOff);
    E->ImportLookupTableRVA = LookupTab->getRVA();
    E->NameRVA = DLLName->getRVA();
    E->ImportAddressTableRVA = AddressTab->getRVA();
  }

  Chunk *DLLName;
  Chunk *LookupTab;
  Chunk *AddressTab;
};

// A chunk representing null terminator in the import table.
// Contents of this chunk is always null bytes.
class NullChunk : public Chunk {
public:
  explicit NullChunk(size_t N) : Size(N) {}
  bool hasData() const override { return false; }
  size_t getSize() const override { return Size; }

  void writeTo(uint8_t *Buf) const override {
    memset(Buf + OutputSectionOff, 0, Size);
  }

private:
  size_t Size;
};

static std::vector<std::vector<DefinedImportData *>>
binImports(const std::vector<DefinedImportData *> &Imports) {
  // Group DLL-imported symbols by DLL name because that's how
  // symbols are layed out in the import descriptor table.
  auto Less = [](const std::string &A, const std::string &B) {
    return Config->DLLOrder[A] < Config->DLLOrder[B];
  };
  std::map<std::string, std::vector<DefinedImportData *>,
           bool(*)(const std::string &, const std::string &)> M(Less);
  for (DefinedImportData *Sym : Imports)
    M[Sym->getDLLName().lower()].push_back(Sym);

  std::vector<std::vector<DefinedImportData *>> V;
  for (auto &KV : M) {
    // Sort symbols by name for each group.
    std::vector<DefinedImportData *> &Syms = KV.second;
    std::sort(Syms.begin(), Syms.end(),
              [](DefinedImportData *A, DefinedImportData *B) {
                return A->getName() < B->getName();
              });
    V.push_back(std::move(Syms));
  }
  return V;
}

// Export table
// See Microsoft PE/COFF spec 4.3 for details.

// A chunk for the delay import descriptor table etnry.
class DelayDirectoryChunk : public Chunk {
public:
  explicit DelayDirectoryChunk(Chunk *N) : DLLName(N) {}

  size_t getSize() const override {
    return sizeof(delay_import_directory_table_entry);
  }

  void writeTo(uint8_t *Buf) const override {
    memset(Buf + OutputSectionOff, 0, getSize());

    auto *E = (delay_import_directory_table_entry *)(Buf + OutputSectionOff);
    E->Attributes = 1;
    E->Name = DLLName->getRVA();
    E->ModuleHandle = ModuleHandle->getRVA();
    E->DelayImportAddressTable = AddressTab->getRVA();
    E->DelayImportNameTable = NameTab->getRVA();
  }

  Chunk *DLLName;
  Chunk *ModuleHandle;
  Chunk *AddressTab;
  Chunk *NameTab;
};

// Initial contents for delay-loaded functions.
// This code calls __delayLoadHelper2 function to resolve a symbol
// and then overwrites its jump table slot with the result
// for subsequent function calls.
static const uint8_t ThunkX64[] = {
    0x51,                               // push    rcx
    0x52,                               // push    rdx
    0x41, 0x50,                         // push    r8
    0x41, 0x51,                         // push    r9
    0x48, 0x83, 0xEC, 0x48,             // sub     rsp, 48h
    0x66, 0x0F, 0x7F, 0x04, 0x24,       // movdqa  xmmword ptr [rsp], xmm0
    0x66, 0x0F, 0x7F, 0x4C, 0x24, 0x10, // movdqa  xmmword ptr [rsp+10h], xmm1
    0x66, 0x0F, 0x7F, 0x54, 0x24, 0x20, // movdqa  xmmword ptr [rsp+20h], xmm2
    0x66, 0x0F, 0x7F, 0x5C, 0x24, 0x30, // movdqa  xmmword ptr [rsp+30h], xmm3
    0x48, 0x8D, 0x15, 0, 0, 0, 0,       // lea     rdx, [__imp_<FUNCNAME>]
    0x48, 0x8D, 0x0D, 0, 0, 0, 0,       // lea     rcx, [___DELAY_IMPORT_...]
    0xE8, 0, 0, 0, 0,                   // call    __delayLoadHelper2
    0x66, 0x0F, 0x6F, 0x04, 0x24,       // movdqa  xmm0, xmmword ptr [rsp]
    0x66, 0x0F, 0x6F, 0x4C, 0x24, 0x10, // movdqa  xmm1, xmmword ptr [rsp+10h]
    0x66, 0x0F, 0x6F, 0x54, 0x24, 0x20, // movdqa  xmm2, xmmword ptr [rsp+20h]
    0x66, 0x0F, 0x6F, 0x5C, 0x24, 0x30, // movdqa  xmm3, xmmword ptr [rsp+30h]
    0x48, 0x83, 0xC4, 0x48,             // add     rsp, 48h
    0x41, 0x59,                         // pop     r9
    0x41, 0x58,                         // pop     r8
    0x5A,                               // pop     rdx
    0x59,                               // pop     rcx
    0xFF, 0xE0,                         // jmp     rax
};

static const uint8_t ThunkX86[] = {
    0x51,              // push  ecx
    0x52,              // push  edx
    0x68, 0, 0, 0, 0,  // push  offset ___imp__<FUNCNAME>
    0x68, 0, 0, 0, 0,  // push  offset ___DELAY_IMPORT_DESCRIPTOR_<DLLNAME>_dll
    0xE8, 0, 0, 0, 0,  // call  ___delayLoadHelper2@8
    0x5A,              // pop   edx
    0x59,              // pop   ecx
    0xFF, 0xE0,        // jmp   eax
};

static const uint8_t ThunkARM[] = {
    0x40, 0xf2, 0x00, 0x0c, // mov.w   ip, #0 __imp_<FUNCNAME>
    0xc0, 0xf2, 0x00, 0x0c, // mov.t   ip, #0 __imp_<FUNCNAME>
    0x2d, 0xe9, 0x0f, 0x48, // push.w  {r0, r1, r2, r3, r11, lr}
    0x0d, 0xf2, 0x10, 0x0b, // addw    r11, sp, #16
    0x2d, 0xed, 0x10, 0x0b, // vpush   {d0, d1, d2, d3, d4, d5, d6, d7}
    0x61, 0x46,             // mov     r1, ip
    0x40, 0xf2, 0x00, 0x00, // mov.w   r0, #0 DELAY_IMPORT_DESCRIPTOR
    0xc0, 0xf2, 0x00, 0x00, // mov.t   r0, #0 DELAY_IMPORT_DESCRIPTOR
    0x00, 0xf0, 0x00, 0xd0, // bl      #0 __delayLoadHelper2
    0x84, 0x46,             // mov     ip, r0
    0xbd, 0xec, 0x10, 0x0b, // vpop    {d0, d1, d2, d3, d4, d5, d6, d7}
    0xbd, 0xe8, 0x0f, 0x48, // pop.w   {r0, r1, r2, r3, r11, lr}
    0x60, 0x47,             // bx      ip
};

static const uint8_t ThunkARM64[] = {
    0x11, 0x00, 0x00, 0x90, // adrp    x17, #0      __imp_<FUNCNAME>
    0x31, 0x02, 0x00, 0x91, // add     x17, x17, #0 :lo12:__imp_<FUNCNAME>
    0xfd, 0x7b, 0xb3, 0xa9, // stp     x29, x30, [sp, #-208]!
    0xfd, 0x03, 0x00, 0x91, // mov     x29, sp
    0xe0, 0x07, 0x01, 0xa9, // stp     x0, x1, [sp, #16]
    0xe2, 0x0f, 0x02, 0xa9, // stp     x2, x3, [sp, #32]
    0xe4, 0x17, 0x03, 0xa9, // stp     x4, x5, [sp, #48]
    0xe6, 0x1f, 0x04, 0xa9, // stp     x6, x7, [sp, #64]
    0xe0, 0x87, 0x02, 0xad, // stp     q0, q1, [sp, #80]
    0xe2, 0x8f, 0x03, 0xad, // stp     q2, q3, [sp, #112]
    0xe4, 0x97, 0x04, 0xad, // stp     q4, q5, [sp, #144]
    0xe6, 0x9f, 0x05, 0xad, // stp     q6, q7, [sp, #176]
    0xe1, 0x03, 0x11, 0xaa, // mov     x1, x17
    0x00, 0x00, 0x00, 0x90, // adrp    x0, #0     DELAY_IMPORT_DESCRIPTOR
    0x00, 0x00, 0x00, 0x91, // add     x0, x0, #0 :lo12:DELAY_IMPORT_DESCRIPTOR
    0x00, 0x00, 0x00, 0x94, // bl      #0 __delayLoadHelper2
    0xf0, 0x03, 0x00, 0xaa, // mov     x16, x0
    0xe6, 0x9f, 0x45, 0xad, // ldp     q6, q7, [sp, #176]
    0xe4, 0x97, 0x44, 0xad, // ldp     q4, q5, [sp, #144]
    0xe2, 0x8f, 0x43, 0xad, // ldp     q2, q3, [sp, #112]
    0xe0, 0x87, 0x42, 0xad, // ldp     q0, q1, [sp, #80]
    0xe6, 0x1f, 0x44, 0xa9, // ldp     x6, x7, [sp, #64]
    0xe4, 0x17, 0x43, 0xa9, // ldp     x4, x5, [sp, #48]
    0xe2, 0x0f, 0x42, 0xa9, // ldp     x2, x3, [sp, #32]
    0xe0, 0x07, 0x41, 0xa9, // ldp     x0, x1, [sp, #16]
    0xfd, 0x7b, 0xcd, 0xa8, // ldp     x29, x30, [sp], #208
    0x00, 0x02, 0x1f, 0xd6, // br      x16
};

// A chunk for the delay import thunk.
class ThunkChunkX64 : public Chunk {
public:
  ThunkChunkX64(Defined *I, Chunk *D, Defined *H)
      : Imp(I), Desc(D), Helper(H) {}

  size_t getSize() const override { return sizeof(ThunkX64); }

  void writeTo(uint8_t *Buf) const override {
    memcpy(Buf + OutputSectionOff, ThunkX64, sizeof(ThunkX64));
    write32le(Buf + OutputSectionOff + 36, Imp->getRVA() - RVA - 40);
    write32le(Buf + OutputSectionOff + 43, Desc->getRVA() - RVA - 47);
    write32le(Buf + OutputSectionOff + 48, Helper->getRVA() - RVA - 52);
  }

  Defined *Imp = nullptr;
  Chunk *Desc = nullptr;
  Defined *Helper = nullptr;
};

class ThunkChunkX86 : public Chunk {
public:
  ThunkChunkX86(Defined *I, Chunk *D, Defined *H)
      : Imp(I), Desc(D), Helper(H) {}

  size_t getSize() const override { return sizeof(ThunkX86); }

  void writeTo(uint8_t *Buf) const override {
    memcpy(Buf + OutputSectionOff, ThunkX86, sizeof(ThunkX86));
    write32le(Buf + OutputSectionOff + 3, Imp->getRVA() + Config->ImageBase);
    write32le(Buf + OutputSectionOff + 8, Desc->getRVA() + Config->ImageBase);
    write32le(Buf + OutputSectionOff + 13, Helper->getRVA() - RVA - 17);
  }

  void getBaserels(std::vector<Baserel> *Res) override {
    Res->emplace_back(RVA + 3);
    Res->emplace_back(RVA + 8);
  }

  Defined *Imp = nullptr;
  Chunk *Desc = nullptr;
  Defined *Helper = nullptr;
};

class ThunkChunkARM : public Chunk {
public:
  ThunkChunkARM(Defined *I, Chunk *D, Defined *H)
      : Imp(I), Desc(D), Helper(H) {}

  size_t getSize() const override { return sizeof(ThunkARM); }

  void writeTo(uint8_t *Buf) const override {
    memcpy(Buf + OutputSectionOff, ThunkARM, sizeof(ThunkARM));
    applyMOV32T(Buf + OutputSectionOff + 0, Imp->getRVA() + Config->ImageBase);
    applyMOV32T(Buf + OutputSectionOff + 22, Desc->getRVA() + Config->ImageBase);
    applyBranch24T(Buf + OutputSectionOff + 30, Helper->getRVA() - RVA - 34);
  }

  void getBaserels(std::vector<Baserel> *Res) override {
    Res->emplace_back(RVA + 0, IMAGE_REL_BASED_ARM_MOV32T);
    Res->emplace_back(RVA + 22, IMAGE_REL_BASED_ARM_MOV32T);
  }

  Defined *Imp = nullptr;
  Chunk *Desc = nullptr;
  Defined *Helper = nullptr;
};

class ThunkChunkARM64 : public Chunk {
public:
  ThunkChunkARM64(Defined *I, Chunk *D, Defined *H)
      : Imp(I), Desc(D), Helper(H) {}

  size_t getSize() const override { return sizeof(ThunkARM64); }

  void writeTo(uint8_t *Buf) const override {
    memcpy(Buf + OutputSectionOff, ThunkARM64, sizeof(ThunkARM64));
    applyArm64Addr(Buf + OutputSectionOff + 0, Imp->getRVA(), RVA + 0, 12);
    applyArm64Imm(Buf + OutputSectionOff + 4, Imp->getRVA() & 0xfff, 0);
    applyArm64Addr(Buf + OutputSectionOff + 52, Desc->getRVA(), RVA + 52, 12);
    applyArm64Imm(Buf + OutputSectionOff + 56, Desc->getRVA() & 0xfff, 0);
    applyArm64Branch26(Buf + OutputSectionOff + 60,
                       Helper->getRVA() - RVA - 60);
  }

  Defined *Imp = nullptr;
  Chunk *Desc = nullptr;
  Defined *Helper = nullptr;
};

// A chunk for the import descriptor table.
class DelayAddressChunk : public Chunk {
public:
  explicit DelayAddressChunk(Chunk *C) : Thunk(C) {
    Alignment = Config->Wordsize;
  }
  size_t getSize() const override { return Config->Wordsize; }

  void writeTo(uint8_t *Buf) const override {
    if (Config->is64()) {
      write64le(Buf + OutputSectionOff, Thunk->getRVA() + Config->ImageBase);
    } else {
      uint32_t Bit = 0;
      // Pointer to thumb code must have the LSB set, so adjust it.
      if (Config->Machine == ARMNT)
        Bit = 1;
      write32le(Buf + OutputSectionOff, (Thunk->getRVA() + Config->ImageBase) | Bit);
    }
  }

  void getBaserels(std::vector<Baserel> *Res) override {
    Res->emplace_back(RVA);
  }

  Chunk *Thunk;
};

// Export table
// Read Microsoft PE/COFF spec 5.3 for details.

// A chunk for the export descriptor table.
class ExportDirectoryChunk : public Chunk {
public:
  ExportDirectoryChunk(int I, int J, Chunk *D, Chunk *A, Chunk *N, Chunk *O)
      : MaxOrdinal(I), NameTabSize(J), DLLName(D), AddressTab(A), NameTab(N),
        OrdinalTab(O) {}

  size_t getSize() const override {
    return sizeof(export_directory_table_entry);
  }

  void writeTo(uint8_t *Buf) const override {
    memset(Buf + OutputSectionOff, 0, getSize());

    auto *E = (export_directory_table_entry *)(Buf + OutputSectionOff);
    E->NameRVA = DLLName->getRVA();
    E->OrdinalBase = 0;
    E->AddressTableEntries = MaxOrdinal + 1;
    E->NumberOfNamePointers = NameTabSize;
    E->ExportAddressTableRVA = AddressTab->getRVA();
    E->NamePointerRVA = NameTab->getRVA();
    E->OrdinalTableRVA = OrdinalTab->getRVA();
  }

  uint16_t MaxOrdinal;
  uint16_t NameTabSize;
  Chunk *DLLName;
  Chunk *AddressTab;
  Chunk *NameTab;
  Chunk *OrdinalTab;
};

class AddressTableChunk : public Chunk {
public:
  explicit AddressTableChunk(size_t MaxOrdinal) : Size(MaxOrdinal + 1) {}
  size_t getSize() const override { return Size * 4; }

  void writeTo(uint8_t *Buf) const override {
    memset(Buf + OutputSectionOff, 0, getSize());

    for (const Export &E : Config->Exports) {
      uint8_t *P = Buf + OutputSectionOff + E.Ordinal * 4;
      uint32_t Bit = 0;
      // Pointer to thumb code must have the LSB set, so adjust it.
      if (Config->Machine == ARMNT && !E.Data)
        Bit = 1;
      if (E.ForwardChunk) {
        write32le(P, E.ForwardChunk->getRVA() | Bit);
      } else {
        write32le(P, cast<Defined>(E.Sym)->getRVA() | Bit);
      }
    }
  }

private:
  size_t Size;
};

class NamePointersChunk : public Chunk {
public:
  explicit NamePointersChunk(std::vector<Chunk *> &V) : Chunks(V) {}
  size_t getSize() const override { return Chunks.size() * 4; }

  void writeTo(uint8_t *Buf) const override {
    uint8_t *P = Buf + OutputSectionOff;
    for (Chunk *C : Chunks) {
      write32le(P, C->getRVA());
      P += 4;
    }
  }

private:
  std::vector<Chunk *> Chunks;
};

class ExportOrdinalChunk : public Chunk {
public:
  explicit ExportOrdinalChunk(size_t I) : Size(I) {}
  size_t getSize() const override { return Size * 2; }

  void writeTo(uint8_t *Buf) const override {
    uint8_t *P = Buf + OutputSectionOff;
    for (Export &E : Config->Exports) {
      if (E.Noname)
        continue;
      write16le(P, E.Ordinal);
      P += 2;
    }
  }

private:
  size_t Size;
};

} // anonymous namespace

void IdataContents::create() {
  std::vector<std::vector<DefinedImportData *>> V = binImports(Imports);

  // Create .idata contents for each DLL.
  for (std::vector<DefinedImportData *> &Syms : V) {
    // Create lookup and address tables. If they have external names,
    // we need to create HintName chunks to store the names.
    // If they don't (if they are import-by-ordinals), we store only
    // ordinal values to the table.
    size_t Base = Lookups.size();
    for (DefinedImportData *S : Syms) {
      uint16_t Ord = S->getOrdinal();
      if (S->getExternalName().empty()) {
        Lookups.push_back(make<OrdinalOnlyChunk>(Ord));
        Addresses.push_back(make<OrdinalOnlyChunk>(Ord));
        continue;
      }
      auto *C = make<HintNameChunk>(S->getExternalName(), Ord);
      Lookups.push_back(make<LookupChunk>(C));
      Addresses.push_back(make<LookupChunk>(C));
      Hints.push_back(C);
    }
    // Terminate with null values.
    Lookups.push_back(make<NullChunk>(Config->Wordsize));
    Addresses.push_back(make<NullChunk>(Config->Wordsize));

    for (int I = 0, E = Syms.size(); I < E; ++I)
      Syms[I]->setLocation(Addresses[Base + I]);

    // Create the import table header.
    DLLNames.push_back(make<StringChunk>(Syms[0]->getDLLName()));
    auto *Dir = make<ImportDirectoryChunk>(DLLNames.back());
    Dir->LookupTab = Lookups[Base];
    Dir->AddressTab = Addresses[Base];
    Dirs.push_back(Dir);
  }
  // Add null terminator.
  Dirs.push_back(make<NullChunk>(sizeof(ImportDirectoryTableEntry)));
}

std::vector<Chunk *> DelayLoadContents::getChunks() {
  std::vector<Chunk *> V;
  V.insert(V.end(), Dirs.begin(), Dirs.end());
  V.insert(V.end(), Names.begin(), Names.end());
  V.insert(V.end(), HintNames.begin(), HintNames.end());
  V.insert(V.end(), DLLNames.begin(), DLLNames.end());
  return V;
}

std::vector<Chunk *> DelayLoadContents::getDataChunks() {
  std::vector<Chunk *> V;
  V.insert(V.end(), ModuleHandles.begin(), ModuleHandles.end());
  V.insert(V.end(), Addresses.begin(), Addresses.end());
  return V;
}

uint64_t DelayLoadContents::getDirSize() {
  return Dirs.size() * sizeof(delay_import_directory_table_entry);
}

void DelayLoadContents::create(Defined *H) {
  Helper = H;
  std::vector<std::vector<DefinedImportData *>> V = binImports(Imports);

  // Create .didat contents for each DLL.
  for (std::vector<DefinedImportData *> &Syms : V) {
    // Create the delay import table header.
    DLLNames.push_back(make<StringChunk>(Syms[0]->getDLLName()));
    auto *Dir = make<DelayDirectoryChunk>(DLLNames.back());

    size_t Base = Addresses.size();
    for (DefinedImportData *S : Syms) {
      Chunk *T = newThunkChunk(S, Dir);
      auto *A = make<DelayAddressChunk>(T);
      Addresses.push_back(A);
      Thunks.push_back(T);
      StringRef ExtName = S->getExternalName();
      if (ExtName.empty()) {
        Names.push_back(make<OrdinalOnlyChunk>(S->getOrdinal()));
      } else {
        auto *C = make<HintNameChunk>(ExtName, 0);
        Names.push_back(make<LookupChunk>(C));
        HintNames.push_back(C);
      }
    }
    // Terminate with null values.
    Addresses.push_back(make<NullChunk>(8));
    Names.push_back(make<NullChunk>(8));

    for (int I = 0, E = Syms.size(); I < E; ++I)
      Syms[I]->setLocation(Addresses[Base + I]);
    auto *MH = make<NullChunk>(8);
    MH->Alignment = 8;
    ModuleHandles.push_back(MH);

    // Fill the delay import table header fields.
    Dir->ModuleHandle = MH;
    Dir->AddressTab = Addresses[Base];
    Dir->NameTab = Names[Base];
    Dirs.push_back(Dir);
  }
  // Add null terminator.
  Dirs.push_back(make<NullChunk>(sizeof(delay_import_directory_table_entry)));
}

Chunk *DelayLoadContents::newThunkChunk(DefinedImportData *S, Chunk *Dir) {
  switch (Config->Machine) {
  case AMD64:
    return make<ThunkChunkX64>(S, Dir, Helper);
  case I386:
    return make<ThunkChunkX86>(S, Dir, Helper);
  case ARMNT:
    return make<ThunkChunkARM>(S, Dir, Helper);
  case ARM64:
    return make<ThunkChunkARM64>(S, Dir, Helper);
  default:
    llvm_unreachable("unsupported machine type");
  }
}

EdataContents::EdataContents() {
  uint16_t MaxOrdinal = 0;
  for (Export &E : Config->Exports)
    MaxOrdinal = std::max(MaxOrdinal, E.Ordinal);

  auto *DLLName = make<StringChunk>(sys::path::filename(Config->OutputFile));
  auto *AddressTab = make<AddressTableChunk>(MaxOrdinal);
  std::vector<Chunk *> Names;
  for (Export &E : Config->Exports)
    if (!E.Noname)
      Names.push_back(make<StringChunk>(E.ExportName));

  std::vector<Chunk *> Forwards;
  for (Export &E : Config->Exports) {
    if (E.ForwardTo.empty())
      continue;
    E.ForwardChunk = make<StringChunk>(E.ForwardTo);
    Forwards.push_back(E.ForwardChunk);
  }

  auto *NameTab = make<NamePointersChunk>(Names);
  auto *OrdinalTab = make<ExportOrdinalChunk>(Names.size());
  auto *Dir = make<ExportDirectoryChunk>(MaxOrdinal, Names.size(), DLLName,
                                         AddressTab, NameTab, OrdinalTab);
  Chunks.push_back(Dir);
  Chunks.push_back(DLLName);
  Chunks.push_back(AddressTab);
  Chunks.push_back(NameTab);
  Chunks.push_back(OrdinalTab);
  Chunks.insert(Chunks.end(), Names.begin(), Names.end());
  Chunks.insert(Chunks.end(), Forwards.begin(), Forwards.end());
}

} // namespace coff
} // namespace lld
