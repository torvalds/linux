//===- SyntheticSections.cpp ----------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains linker-synthesized sections. Currently,
// synthetic sections are created either output sections or input sections,
// but we are rewriting code so that all synthetic sections are created as
// input sections.
//
//===----------------------------------------------------------------------===//

#include "SyntheticSections.h"
#include "Bits.h"
#include "Config.h"
#include "InputFiles.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Target.h"
#include "Writer.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "lld/Common/Threads.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/xxhash.h"
#include <cstdlib>
#include <thread>

using namespace llvm;
using namespace llvm::dwarf;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support;

using namespace lld;
using namespace lld::elf;

using llvm::support::endian::read32le;
using llvm::support::endian::write32le;
using llvm::support::endian::write64le;

constexpr size_t MergeNoTailSection::NumShards;

// Returns an LLD version string.
static ArrayRef<uint8_t> getVersion() {
  // Check LLD_VERSION first for ease of testing.
  // You can get consistent output by using the environment variable.
  // This is only for testing.
  StringRef S = getenv("LLD_VERSION");
  if (S.empty())
    S = Saver.save(Twine("Linker: ") + getLLDVersion());

  // +1 to include the terminating '\0'.
  return {(const uint8_t *)S.data(), S.size() + 1};
}

// Creates a .comment section containing LLD version info.
// With this feature, you can identify LLD-generated binaries easily
// by "readelf --string-dump .comment <file>".
// The returned object is a mergeable string section.
MergeInputSection *elf::createCommentSection() {
  return make<MergeInputSection>(SHF_MERGE | SHF_STRINGS, SHT_PROGBITS, 1,
                                 getVersion(), ".comment");
}

// .MIPS.abiflags section.
template <class ELFT>
MipsAbiFlagsSection<ELFT>::MipsAbiFlagsSection(Elf_Mips_ABIFlags Flags)
    : SyntheticSection(SHF_ALLOC, SHT_MIPS_ABIFLAGS, 8, ".MIPS.abiflags"),
      Flags(Flags) {
  this->Entsize = sizeof(Elf_Mips_ABIFlags);
}

template <class ELFT> void MipsAbiFlagsSection<ELFT>::writeTo(uint8_t *Buf) {
  memcpy(Buf, &Flags, sizeof(Flags));
}

template <class ELFT>
MipsAbiFlagsSection<ELFT> *MipsAbiFlagsSection<ELFT>::create() {
  Elf_Mips_ABIFlags Flags = {};
  bool Create = false;

  for (InputSectionBase *Sec : InputSections) {
    if (Sec->Type != SHT_MIPS_ABIFLAGS)
      continue;
    Sec->Live = false;
    Create = true;

    std::string Filename = toString(Sec->File);
    const size_t Size = Sec->data().size();
    // Older version of BFD (such as the default FreeBSD linker) concatenate
    // .MIPS.abiflags instead of merging. To allow for this case (or potential
    // zero padding) we ignore everything after the first Elf_Mips_ABIFlags
    if (Size < sizeof(Elf_Mips_ABIFlags)) {
      error(Filename + ": invalid size of .MIPS.abiflags section: got " +
            Twine(Size) + " instead of " + Twine(sizeof(Elf_Mips_ABIFlags)));
      return nullptr;
    }
    auto *S = reinterpret_cast<const Elf_Mips_ABIFlags *>(Sec->data().data());
    if (S->version != 0) {
      error(Filename + ": unexpected .MIPS.abiflags version " +
            Twine(S->version));
      return nullptr;
    }

    // LLD checks ISA compatibility in calcMipsEFlags(). Here we just
    // select the highest number of ISA/Rev/Ext.
    Flags.isa_level = std::max(Flags.isa_level, S->isa_level);
    Flags.isa_rev = std::max(Flags.isa_rev, S->isa_rev);
    Flags.isa_ext = std::max(Flags.isa_ext, S->isa_ext);
    Flags.gpr_size = std::max(Flags.gpr_size, S->gpr_size);
    Flags.cpr1_size = std::max(Flags.cpr1_size, S->cpr1_size);
    Flags.cpr2_size = std::max(Flags.cpr2_size, S->cpr2_size);
    Flags.ases |= S->ases;
    Flags.flags1 |= S->flags1;
    Flags.flags2 |= S->flags2;
    Flags.fp_abi = elf::getMipsFpAbiFlag(Flags.fp_abi, S->fp_abi, Filename);
  };

  if (Create)
    return make<MipsAbiFlagsSection<ELFT>>(Flags);
  return nullptr;
}

// .MIPS.options section.
template <class ELFT>
MipsOptionsSection<ELFT>::MipsOptionsSection(Elf_Mips_RegInfo Reginfo)
    : SyntheticSection(SHF_ALLOC, SHT_MIPS_OPTIONS, 8, ".MIPS.options"),
      Reginfo(Reginfo) {
  this->Entsize = sizeof(Elf_Mips_Options) + sizeof(Elf_Mips_RegInfo);
}

template <class ELFT> void MipsOptionsSection<ELFT>::writeTo(uint8_t *Buf) {
  auto *Options = reinterpret_cast<Elf_Mips_Options *>(Buf);
  Options->kind = ODK_REGINFO;
  Options->size = getSize();

  if (!Config->Relocatable)
    Reginfo.ri_gp_value = In.MipsGot->getGp();
  memcpy(Buf + sizeof(Elf_Mips_Options), &Reginfo, sizeof(Reginfo));
}

template <class ELFT>
MipsOptionsSection<ELFT> *MipsOptionsSection<ELFT>::create() {
  // N64 ABI only.
  if (!ELFT::Is64Bits)
    return nullptr;

  std::vector<InputSectionBase *> Sections;
  for (InputSectionBase *Sec : InputSections)
    if (Sec->Type == SHT_MIPS_OPTIONS)
      Sections.push_back(Sec);

  if (Sections.empty())
    return nullptr;

  Elf_Mips_RegInfo Reginfo = {};
  for (InputSectionBase *Sec : Sections) {
    Sec->Live = false;

    std::string Filename = toString(Sec->File);
    ArrayRef<uint8_t> D = Sec->data();

    while (!D.empty()) {
      if (D.size() < sizeof(Elf_Mips_Options)) {
        error(Filename + ": invalid size of .MIPS.options section");
        break;
      }

      auto *Opt = reinterpret_cast<const Elf_Mips_Options *>(D.data());
      if (Opt->kind == ODK_REGINFO) {
        Reginfo.ri_gprmask |= Opt->getRegInfo().ri_gprmask;
        Sec->getFile<ELFT>()->MipsGp0 = Opt->getRegInfo().ri_gp_value;
        break;
      }

      if (!Opt->size)
        fatal(Filename + ": zero option descriptor size");
      D = D.slice(Opt->size);
    }
  };

  return make<MipsOptionsSection<ELFT>>(Reginfo);
}

// MIPS .reginfo section.
template <class ELFT>
MipsReginfoSection<ELFT>::MipsReginfoSection(Elf_Mips_RegInfo Reginfo)
    : SyntheticSection(SHF_ALLOC, SHT_MIPS_REGINFO, 4, ".reginfo"),
      Reginfo(Reginfo) {
  this->Entsize = sizeof(Elf_Mips_RegInfo);
}

template <class ELFT> void MipsReginfoSection<ELFT>::writeTo(uint8_t *Buf) {
  if (!Config->Relocatable)
    Reginfo.ri_gp_value = In.MipsGot->getGp();
  memcpy(Buf, &Reginfo, sizeof(Reginfo));
}

template <class ELFT>
MipsReginfoSection<ELFT> *MipsReginfoSection<ELFT>::create() {
  // Section should be alive for O32 and N32 ABIs only.
  if (ELFT::Is64Bits)
    return nullptr;

  std::vector<InputSectionBase *> Sections;
  for (InputSectionBase *Sec : InputSections)
    if (Sec->Type == SHT_MIPS_REGINFO)
      Sections.push_back(Sec);

  if (Sections.empty())
    return nullptr;

  Elf_Mips_RegInfo Reginfo = {};
  for (InputSectionBase *Sec : Sections) {
    Sec->Live = false;

    if (Sec->data().size() != sizeof(Elf_Mips_RegInfo)) {
      error(toString(Sec->File) + ": invalid size of .reginfo section");
      return nullptr;
    }

    auto *R = reinterpret_cast<const Elf_Mips_RegInfo *>(Sec->data().data());
    Reginfo.ri_gprmask |= R->ri_gprmask;
    Sec->getFile<ELFT>()->MipsGp0 = R->ri_gp_value;
  };

  return make<MipsReginfoSection<ELFT>>(Reginfo);
}

InputSection *elf::createInterpSection() {
  // StringSaver guarantees that the returned string ends with '\0'.
  StringRef S = Saver.save(Config->DynamicLinker);
  ArrayRef<uint8_t> Contents = {(const uint8_t *)S.data(), S.size() + 1};

  auto *Sec = make<InputSection>(nullptr, SHF_ALLOC, SHT_PROGBITS, 1, Contents,
                                 ".interp");
  Sec->Live = true;
  return Sec;
}

Defined *elf::addSyntheticLocal(StringRef Name, uint8_t Type, uint64_t Value,
                                uint64_t Size, InputSectionBase &Section) {
  auto *S = make<Defined>(Section.File, Name, STB_LOCAL, STV_DEFAULT, Type,
                          Value, Size, &Section);
  if (In.SymTab)
    In.SymTab->addSymbol(S);
  return S;
}

static size_t getHashSize() {
  switch (Config->BuildId) {
  case BuildIdKind::Fast:
    return 8;
  case BuildIdKind::Md5:
  case BuildIdKind::Uuid:
    return 16;
  case BuildIdKind::Sha1:
    return 20;
  case BuildIdKind::Hexstring:
    return Config->BuildIdVector.size();
  default:
    llvm_unreachable("unknown BuildIdKind");
  }
}

BuildIdSection::BuildIdSection()
    : SyntheticSection(SHF_ALLOC, SHT_NOTE, 4, ".note.gnu.build-id"),
      HashSize(getHashSize()) {}

void BuildIdSection::writeTo(uint8_t *Buf) {
  write32(Buf, 4);                      // Name size
  write32(Buf + 4, HashSize);           // Content size
  write32(Buf + 8, NT_GNU_BUILD_ID);    // Type
  memcpy(Buf + 12, "GNU", 4);           // Name string
  HashBuf = Buf + 16;
}

// Split one uint8 array into small pieces of uint8 arrays.
static std::vector<ArrayRef<uint8_t>> split(ArrayRef<uint8_t> Arr,
                                            size_t ChunkSize) {
  std::vector<ArrayRef<uint8_t>> Ret;
  while (Arr.size() > ChunkSize) {
    Ret.push_back(Arr.take_front(ChunkSize));
    Arr = Arr.drop_front(ChunkSize);
  }
  if (!Arr.empty())
    Ret.push_back(Arr);
  return Ret;
}

// Computes a hash value of Data using a given hash function.
// In order to utilize multiple cores, we first split data into 1MB
// chunks, compute a hash for each chunk, and then compute a hash value
// of the hash values.
void BuildIdSection::computeHash(
    llvm::ArrayRef<uint8_t> Data,
    std::function<void(uint8_t *Dest, ArrayRef<uint8_t> Arr)> HashFn) {
  std::vector<ArrayRef<uint8_t>> Chunks = split(Data, 1024 * 1024);
  std::vector<uint8_t> Hashes(Chunks.size() * HashSize);

  // Compute hash values.
  parallelForEachN(0, Chunks.size(), [&](size_t I) {
    HashFn(Hashes.data() + I * HashSize, Chunks[I]);
  });

  // Write to the final output buffer.
  HashFn(HashBuf, Hashes);
}

BssSection::BssSection(StringRef Name, uint64_t Size, uint32_t Alignment)
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_NOBITS, Alignment, Name) {
  this->Bss = true;
  this->Size = Size;
}

void BuildIdSection::writeBuildId(ArrayRef<uint8_t> Buf) {
  switch (Config->BuildId) {
  case BuildIdKind::Fast:
    computeHash(Buf, [](uint8_t *Dest, ArrayRef<uint8_t> Arr) {
      write64le(Dest, xxHash64(Arr));
    });
    break;
  case BuildIdKind::Md5:
    computeHash(Buf, [](uint8_t *Dest, ArrayRef<uint8_t> Arr) {
      memcpy(Dest, MD5::hash(Arr).data(), 16);
    });
    break;
  case BuildIdKind::Sha1:
    computeHash(Buf, [](uint8_t *Dest, ArrayRef<uint8_t> Arr) {
      memcpy(Dest, SHA1::hash(Arr).data(), 20);
    });
    break;
  case BuildIdKind::Uuid:
    if (auto EC = getRandomBytes(HashBuf, HashSize))
      error("entropy source failure: " + EC.message());
    break;
  case BuildIdKind::Hexstring:
    memcpy(HashBuf, Config->BuildIdVector.data(), Config->BuildIdVector.size());
    break;
  default:
    llvm_unreachable("unknown BuildIdKind");
  }
}

EhFrameSection::EhFrameSection()
    : SyntheticSection(SHF_ALLOC, SHT_PROGBITS, 1, ".eh_frame") {}

// Search for an existing CIE record or create a new one.
// CIE records from input object files are uniquified by their contents
// and where their relocations point to.
template <class ELFT, class RelTy>
CieRecord *EhFrameSection::addCie(EhSectionPiece &Cie, ArrayRef<RelTy> Rels) {
  Symbol *Personality = nullptr;
  unsigned FirstRelI = Cie.FirstRelocation;
  if (FirstRelI != (unsigned)-1)
    Personality =
        &Cie.Sec->template getFile<ELFT>()->getRelocTargetSym(Rels[FirstRelI]);

  // Search for an existing CIE by CIE contents/relocation target pair.
  CieRecord *&Rec = CieMap[{Cie.data(), Personality}];

  // If not found, create a new one.
  if (!Rec) {
    Rec = make<CieRecord>();
    Rec->Cie = &Cie;
    CieRecords.push_back(Rec);
  }
  return Rec;
}

// There is one FDE per function. Returns true if a given FDE
// points to a live function.
template <class ELFT, class RelTy>
bool EhFrameSection::isFdeLive(EhSectionPiece &Fde, ArrayRef<RelTy> Rels) {
  auto *Sec = cast<EhInputSection>(Fde.Sec);
  unsigned FirstRelI = Fde.FirstRelocation;

  // An FDE should point to some function because FDEs are to describe
  // functions. That's however not always the case due to an issue of
  // ld.gold with -r. ld.gold may discard only functions and leave their
  // corresponding FDEs, which results in creating bad .eh_frame sections.
  // To deal with that, we ignore such FDEs.
  if (FirstRelI == (unsigned)-1)
    return false;

  const RelTy &Rel = Rels[FirstRelI];
  Symbol &B = Sec->template getFile<ELFT>()->getRelocTargetSym(Rel);

  // FDEs for garbage-collected or merged-by-ICF sections are dead.
  if (auto *D = dyn_cast<Defined>(&B))
    if (SectionBase *Sec = D->Section)
      return Sec->Live;
  return false;
}

// .eh_frame is a sequence of CIE or FDE records. In general, there
// is one CIE record per input object file which is followed by
// a list of FDEs. This function searches an existing CIE or create a new
// one and associates FDEs to the CIE.
template <class ELFT, class RelTy>
void EhFrameSection::addSectionAux(EhInputSection *Sec, ArrayRef<RelTy> Rels) {
  OffsetToCie.clear();
  for (EhSectionPiece &Piece : Sec->Pieces) {
    // The empty record is the end marker.
    if (Piece.Size == 4)
      return;

    size_t Offset = Piece.InputOff;
    uint32_t ID = read32(Piece.data().data() + 4);
    if (ID == 0) {
      OffsetToCie[Offset] = addCie<ELFT>(Piece, Rels);
      continue;
    }

    uint32_t CieOffset = Offset + 4 - ID;
    CieRecord *Rec = OffsetToCie[CieOffset];
    if (!Rec)
      fatal(toString(Sec) + ": invalid CIE reference");

    if (!isFdeLive<ELFT>(Piece, Rels))
      continue;
    Rec->Fdes.push_back(&Piece);
    NumFdes++;
  }
}

template <class ELFT> void EhFrameSection::addSection(InputSectionBase *C) {
  auto *Sec = cast<EhInputSection>(C);
  Sec->Parent = this;

  Alignment = std::max(Alignment, Sec->Alignment);
  Sections.push_back(Sec);

  for (auto *DS : Sec->DependentSections)
    DependentSections.push_back(DS);

  if (Sec->Pieces.empty())
    return;

  if (Sec->AreRelocsRela)
    addSectionAux<ELFT>(Sec, Sec->template relas<ELFT>());
  else
    addSectionAux<ELFT>(Sec, Sec->template rels<ELFT>());
}

static void writeCieFde(uint8_t *Buf, ArrayRef<uint8_t> D) {
  memcpy(Buf, D.data(), D.size());

  size_t Aligned = alignTo(D.size(), Config->Wordsize);

  // Zero-clear trailing padding if it exists.
  memset(Buf + D.size(), 0, Aligned - D.size());

  // Fix the size field. -4 since size does not include the size field itself.
  write32(Buf, Aligned - 4);
}

void EhFrameSection::finalizeContents() {
  assert(!this->Size); // Not finalized.
  size_t Off = 0;
  for (CieRecord *Rec : CieRecords) {
    Rec->Cie->OutputOff = Off;
    Off += alignTo(Rec->Cie->Size, Config->Wordsize);

    for (EhSectionPiece *Fde : Rec->Fdes) {
      Fde->OutputOff = Off;
      Off += alignTo(Fde->Size, Config->Wordsize);
    }
  }

  // The LSB standard does not allow a .eh_frame section with zero
  // Call Frame Information records. glibc unwind-dw2-fde.c
  // classify_object_over_fdes expects there is a CIE record length 0 as a
  // terminator. Thus we add one unconditionally.
  Off += 4;

  this->Size = Off;
}

// Returns data for .eh_frame_hdr. .eh_frame_hdr is a binary search table
// to get an FDE from an address to which FDE is applied. This function
// returns a list of such pairs.
std::vector<EhFrameSection::FdeData> EhFrameSection::getFdeData() const {
  uint8_t *Buf = getParent()->Loc + OutSecOff;
  std::vector<FdeData> Ret;

  uint64_t VA = In.EhFrameHdr->getVA();
  for (CieRecord *Rec : CieRecords) {
    uint8_t Enc = getFdeEncoding(Rec->Cie);
    for (EhSectionPiece *Fde : Rec->Fdes) {
      uint64_t Pc = getFdePc(Buf, Fde->OutputOff, Enc);
      uint64_t FdeVA = getParent()->Addr + Fde->OutputOff;
      if (!isInt<32>(Pc - VA))
        fatal(toString(Fde->Sec) + ": PC offset is too large: 0x" +
              Twine::utohexstr(Pc - VA));
      Ret.push_back({uint32_t(Pc - VA), uint32_t(FdeVA - VA)});
    }
  }

  // Sort the FDE list by their PC and uniqueify. Usually there is only
  // one FDE for a PC (i.e. function), but if ICF merges two functions
  // into one, there can be more than one FDEs pointing to the address.
  auto Less = [](const FdeData &A, const FdeData &B) {
    return A.PcRel < B.PcRel;
  };
  std::stable_sort(Ret.begin(), Ret.end(), Less);
  auto Eq = [](const FdeData &A, const FdeData &B) {
    return A.PcRel == B.PcRel;
  };
  Ret.erase(std::unique(Ret.begin(), Ret.end(), Eq), Ret.end());

  return Ret;
}

static uint64_t readFdeAddr(uint8_t *Buf, int Size) {
  switch (Size) {
  case DW_EH_PE_udata2:
    return read16(Buf);
  case DW_EH_PE_sdata2:
    return (int16_t)read16(Buf);
  case DW_EH_PE_udata4:
    return read32(Buf);
  case DW_EH_PE_sdata4:
    return (int32_t)read32(Buf);
  case DW_EH_PE_udata8:
  case DW_EH_PE_sdata8:
    return read64(Buf);
  case DW_EH_PE_absptr:
    return readUint(Buf);
  }
  fatal("unknown FDE size encoding");
}

// Returns the VA to which a given FDE (on a mmap'ed buffer) is applied to.
// We need it to create .eh_frame_hdr section.
uint64_t EhFrameSection::getFdePc(uint8_t *Buf, size_t FdeOff,
                                  uint8_t Enc) const {
  // The starting address to which this FDE applies is
  // stored at FDE + 8 byte.
  size_t Off = FdeOff + 8;
  uint64_t Addr = readFdeAddr(Buf + Off, Enc & 0xf);
  if ((Enc & 0x70) == DW_EH_PE_absptr)
    return Addr;
  if ((Enc & 0x70) == DW_EH_PE_pcrel)
    return Addr + getParent()->Addr + Off;
  fatal("unknown FDE size relative encoding");
}

void EhFrameSection::writeTo(uint8_t *Buf) {
  // Write CIE and FDE records.
  for (CieRecord *Rec : CieRecords) {
    size_t CieOffset = Rec->Cie->OutputOff;
    writeCieFde(Buf + CieOffset, Rec->Cie->data());

    for (EhSectionPiece *Fde : Rec->Fdes) {
      size_t Off = Fde->OutputOff;
      writeCieFde(Buf + Off, Fde->data());

      // FDE's second word should have the offset to an associated CIE.
      // Write it.
      write32(Buf + Off + 4, Off + 4 - CieOffset);
    }
  }

  // Apply relocations. .eh_frame section contents are not contiguous
  // in the output buffer, but relocateAlloc() still works because
  // getOffset() takes care of discontiguous section pieces.
  for (EhInputSection *S : Sections)
    S->relocateAlloc(Buf, nullptr);
}

GotSection::GotSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS,
                       Target->GotEntrySize, ".got") {
  // PPC64 saves the ElfSym::GlobalOffsetTable .TOC. as the first entry in the
  // .got. If there are no references to .TOC. in the symbol table,
  // ElfSym::GlobalOffsetTable will not be defined and we won't need to save
  // .TOC. in the .got. When it is defined, we increase NumEntries by the number
  // of entries used to emit ElfSym::GlobalOffsetTable.
  if (ElfSym::GlobalOffsetTable && !Target->GotBaseSymInGotPlt)
    NumEntries += Target->GotHeaderEntriesNum;
}

void GotSection::addEntry(Symbol &Sym) {
  Sym.GotIndex = NumEntries;
  ++NumEntries;
}

bool GotSection::addDynTlsEntry(Symbol &Sym) {
  if (Sym.GlobalDynIndex != -1U)
    return false;
  Sym.GlobalDynIndex = NumEntries;
  // Global Dynamic TLS entries take two GOT slots.
  NumEntries += 2;
  return true;
}

// Reserves TLS entries for a TLS module ID and a TLS block offset.
// In total it takes two GOT slots.
bool GotSection::addTlsIndex() {
  if (TlsIndexOff != uint32_t(-1))
    return false;
  TlsIndexOff = NumEntries * Config->Wordsize;
  NumEntries += 2;
  return true;
}

uint64_t GotSection::getGlobalDynAddr(const Symbol &B) const {
  return this->getVA() + B.GlobalDynIndex * Config->Wordsize;
}

uint64_t GotSection::getGlobalDynOffset(const Symbol &B) const {
  return B.GlobalDynIndex * Config->Wordsize;
}

void GotSection::finalizeContents() {
  Size = NumEntries * Config->Wordsize;
}

bool GotSection::empty() const {
  // We need to emit a GOT even if it's empty if there's a relocation that is
  // relative to GOT(such as GOTOFFREL) or there's a symbol that points to a GOT
  // (i.e. _GLOBAL_OFFSET_TABLE_) that the target defines relative to the .got.
  return NumEntries == 0 && !HasGotOffRel &&
         !(ElfSym::GlobalOffsetTable && !Target->GotBaseSymInGotPlt);
}

void GotSection::writeTo(uint8_t *Buf) {
  // Buf points to the start of this section's buffer,
  // whereas InputSectionBase::relocateAlloc() expects its argument
  // to point to the start of the output section.
  Target->writeGotHeader(Buf);
  relocateAlloc(Buf - OutSecOff, Buf - OutSecOff + Size);
}

static uint64_t getMipsPageAddr(uint64_t Addr) {
  return (Addr + 0x8000) & ~0xffff;
}

static uint64_t getMipsPageCount(uint64_t Size) {
  return (Size + 0xfffe) / 0xffff + 1;
}

MipsGotSection::MipsGotSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE | SHF_MIPS_GPREL, SHT_PROGBITS, 16,
                       ".got") {}

void MipsGotSection::addEntry(InputFile &File, Symbol &Sym, int64_t Addend,
                              RelExpr Expr) {
  FileGot &G = getGot(File);
  if (Expr == R_MIPS_GOT_LOCAL_PAGE) {
    if (const OutputSection *OS = Sym.getOutputSection())
      G.PagesMap.insert({OS, {}});
    else
      G.Local16.insert({{nullptr, getMipsPageAddr(Sym.getVA(Addend))}, 0});
  } else if (Sym.isTls())
    G.Tls.insert({&Sym, 0});
  else if (Sym.IsPreemptible && Expr == R_ABS)
    G.Relocs.insert({&Sym, 0});
  else if (Sym.IsPreemptible)
    G.Global.insert({&Sym, 0});
  else if (Expr == R_MIPS_GOT_OFF32)
    G.Local32.insert({{&Sym, Addend}, 0});
  else
    G.Local16.insert({{&Sym, Addend}, 0});
}

void MipsGotSection::addDynTlsEntry(InputFile &File, Symbol &Sym) {
  getGot(File).DynTlsSymbols.insert({&Sym, 0});
}

void MipsGotSection::addTlsIndex(InputFile &File) {
  getGot(File).DynTlsSymbols.insert({nullptr, 0});
}

size_t MipsGotSection::FileGot::getEntriesNum() const {
  return getPageEntriesNum() + Local16.size() + Global.size() + Relocs.size() +
         Tls.size() + DynTlsSymbols.size() * 2;
}

size_t MipsGotSection::FileGot::getPageEntriesNum() const {
  size_t Num = 0;
  for (const std::pair<const OutputSection *, FileGot::PageBlock> &P : PagesMap)
    Num += P.second.Count;
  return Num;
}

size_t MipsGotSection::FileGot::getIndexedEntriesNum() const {
  size_t Count = getPageEntriesNum() + Local16.size() + Global.size();
  // If there are relocation-only entries in the GOT, TLS entries
  // are allocated after them. TLS entries should be addressable
  // by 16-bit index so count both reloc-only and TLS entries.
  if (!Tls.empty() || !DynTlsSymbols.empty())
    Count += Relocs.size() + Tls.size() + DynTlsSymbols.size() * 2;
  return Count;
}

MipsGotSection::FileGot &MipsGotSection::getGot(InputFile &F) {
  if (!F.MipsGotIndex.hasValue()) {
    Gots.emplace_back();
    Gots.back().File = &F;
    F.MipsGotIndex = Gots.size() - 1;
  }
  return Gots[*F.MipsGotIndex];
}

uint64_t MipsGotSection::getPageEntryOffset(const InputFile *F,
                                            const Symbol &Sym,
                                            int64_t Addend) const {
  const FileGot &G = Gots[*F->MipsGotIndex];
  uint64_t Index = 0;
  if (const OutputSection *OutSec = Sym.getOutputSection()) {
    uint64_t SecAddr = getMipsPageAddr(OutSec->Addr);
    uint64_t SymAddr = getMipsPageAddr(Sym.getVA(Addend));
    Index = G.PagesMap.lookup(OutSec).FirstIndex + (SymAddr - SecAddr) / 0xffff;
  } else {
    Index = G.Local16.lookup({nullptr, getMipsPageAddr(Sym.getVA(Addend))});
  }
  return Index * Config->Wordsize;
}

uint64_t MipsGotSection::getSymEntryOffset(const InputFile *F, const Symbol &S,
                                           int64_t Addend) const {
  const FileGot &G = Gots[*F->MipsGotIndex];
  Symbol *Sym = const_cast<Symbol *>(&S);
  if (Sym->isTls())
    return G.Tls.lookup(Sym) * Config->Wordsize;
  if (Sym->IsPreemptible)
    return G.Global.lookup(Sym) * Config->Wordsize;
  return G.Local16.lookup({Sym, Addend}) * Config->Wordsize;
}

uint64_t MipsGotSection::getTlsIndexOffset(const InputFile *F) const {
  const FileGot &G = Gots[*F->MipsGotIndex];
  return G.DynTlsSymbols.lookup(nullptr) * Config->Wordsize;
}

uint64_t MipsGotSection::getGlobalDynOffset(const InputFile *F,
                                            const Symbol &S) const {
  const FileGot &G = Gots[*F->MipsGotIndex];
  Symbol *Sym = const_cast<Symbol *>(&S);
  return G.DynTlsSymbols.lookup(Sym) * Config->Wordsize;
}

const Symbol *MipsGotSection::getFirstGlobalEntry() const {
  if (Gots.empty())
    return nullptr;
  const FileGot &PrimGot = Gots.front();
  if (!PrimGot.Global.empty())
    return PrimGot.Global.front().first;
  if (!PrimGot.Relocs.empty())
    return PrimGot.Relocs.front().first;
  return nullptr;
}

unsigned MipsGotSection::getLocalEntriesNum() const {
  if (Gots.empty())
    return HeaderEntriesNum;
  return HeaderEntriesNum + Gots.front().getPageEntriesNum() +
         Gots.front().Local16.size();
}

bool MipsGotSection::tryMergeGots(FileGot &Dst, FileGot &Src, bool IsPrimary) {
  FileGot Tmp = Dst;
  set_union(Tmp.PagesMap, Src.PagesMap);
  set_union(Tmp.Local16, Src.Local16);
  set_union(Tmp.Global, Src.Global);
  set_union(Tmp.Relocs, Src.Relocs);
  set_union(Tmp.Tls, Src.Tls);
  set_union(Tmp.DynTlsSymbols, Src.DynTlsSymbols);

  size_t Count = IsPrimary ? HeaderEntriesNum : 0;
  Count += Tmp.getIndexedEntriesNum();

  if (Count * Config->Wordsize > Config->MipsGotSize)
    return false;

  std::swap(Tmp, Dst);
  return true;
}

void MipsGotSection::finalizeContents() { updateAllocSize(); }

bool MipsGotSection::updateAllocSize() {
  Size = HeaderEntriesNum * Config->Wordsize;
  for (const FileGot &G : Gots)
    Size += G.getEntriesNum() * Config->Wordsize;
  return false;
}

template <class ELFT> void MipsGotSection::build() {
  if (Gots.empty())
    return;

  std::vector<FileGot> MergedGots(1);

  // For each GOT move non-preemptible symbols from the `Global`
  // to `Local16` list. Preemptible symbol might become non-preemptible
  // one if, for example, it gets a related copy relocation.
  for (FileGot &Got : Gots) {
    for (auto &P: Got.Global)
      if (!P.first->IsPreemptible)
        Got.Local16.insert({{P.first, 0}, 0});
    Got.Global.remove_if([&](const std::pair<Symbol *, size_t> &P) {
      return !P.first->IsPreemptible;
    });
  }

  // For each GOT remove "reloc-only" entry if there is "global"
  // entry for the same symbol. And add local entries which indexed
  // using 32-bit value at the end of 16-bit entries.
  for (FileGot &Got : Gots) {
    Got.Relocs.remove_if([&](const std::pair<Symbol *, size_t> &P) {
      return Got.Global.count(P.first);
    });
    set_union(Got.Local16, Got.Local32);
    Got.Local32.clear();
  }

  // Evaluate number of "reloc-only" entries in the resulting GOT.
  // To do that put all unique "reloc-only" and "global" entries
  // from all GOTs to the future primary GOT.
  FileGot *PrimGot = &MergedGots.front();
  for (FileGot &Got : Gots) {
    set_union(PrimGot->Relocs, Got.Global);
    set_union(PrimGot->Relocs, Got.Relocs);
    Got.Relocs.clear();
  }

  // Evaluate number of "page" entries in each GOT.
  for (FileGot &Got : Gots) {
    for (std::pair<const OutputSection *, FileGot::PageBlock> &P :
         Got.PagesMap) {
      const OutputSection *OS = P.first;
      uint64_t SecSize = 0;
      for (BaseCommand *Cmd : OS->SectionCommands) {
        if (auto *ISD = dyn_cast<InputSectionDescription>(Cmd))
          for (InputSection *IS : ISD->Sections) {
            uint64_t Off = alignTo(SecSize, IS->Alignment);
            SecSize = Off + IS->getSize();
          }
      }
      P.second.Count = getMipsPageCount(SecSize);
    }
  }

  // Merge GOTs. Try to join as much as possible GOTs but do not exceed
  // maximum GOT size. At first, try to fill the primary GOT because
  // the primary GOT can be accessed in the most effective way. If it
  // is not possible, try to fill the last GOT in the list, and finally
  // create a new GOT if both attempts failed.
  for (FileGot &SrcGot : Gots) {
    InputFile *File = SrcGot.File;
    if (tryMergeGots(MergedGots.front(), SrcGot, true)) {
      File->MipsGotIndex = 0;
    } else {
      // If this is the first time we failed to merge with the primary GOT,
      // MergedGots.back() will also be the primary GOT. We must make sure not
      // to try to merge again with IsPrimary=false, as otherwise, if the
      // inputs are just right, we could allow the primary GOT to become 1 or 2
      // words too big due to ignoring the header size.
      if (MergedGots.size() == 1 ||
          !tryMergeGots(MergedGots.back(), SrcGot, false)) {
        MergedGots.emplace_back();
        std::swap(MergedGots.back(), SrcGot);
      }
      File->MipsGotIndex = MergedGots.size() - 1;
    }
  }
  std::swap(Gots, MergedGots);

  // Reduce number of "reloc-only" entries in the primary GOT
  // by substracting "global" entries exist in the primary GOT.
  PrimGot = &Gots.front();
  PrimGot->Relocs.remove_if([&](const std::pair<Symbol *, size_t> &P) {
    return PrimGot->Global.count(P.first);
  });

  // Calculate indexes for each GOT entry.
  size_t Index = HeaderEntriesNum;
  for (FileGot &Got : Gots) {
    Got.StartIndex = &Got == PrimGot ? 0 : Index;
    for (std::pair<const OutputSection *, FileGot::PageBlock> &P :
         Got.PagesMap) {
      // For each output section referenced by GOT page relocations calculate
      // and save into PagesMap an upper bound of MIPS GOT entries required
      // to store page addresses of local symbols. We assume the worst case -
      // each 64kb page of the output section has at least one GOT relocation
      // against it. And take in account the case when the section intersects
      // page boundaries.
      P.second.FirstIndex = Index;
      Index += P.second.Count;
    }
    for (auto &P: Got.Local16)
      P.second = Index++;
    for (auto &P: Got.Global)
      P.second = Index++;
    for (auto &P: Got.Relocs)
      P.second = Index++;
    for (auto &P: Got.Tls)
      P.second = Index++;
    for (auto &P: Got.DynTlsSymbols) {
      P.second = Index;
      Index += 2;
    }
  }

  // Update Symbol::GotIndex field to use this
  // value later in the `sortMipsSymbols` function.
  for (auto &P : PrimGot->Global)
    P.first->GotIndex = P.second;
  for (auto &P : PrimGot->Relocs)
    P.first->GotIndex = P.second;

  // Create dynamic relocations.
  for (FileGot &Got : Gots) {
    // Create dynamic relocations for TLS entries.
    for (std::pair<Symbol *, size_t> &P : Got.Tls) {
      Symbol *S = P.first;
      uint64_t Offset = P.second * Config->Wordsize;
      if (S->IsPreemptible)
        In.RelaDyn->addReloc(Target->TlsGotRel, this, Offset, S);
    }
    for (std::pair<Symbol *, size_t> &P : Got.DynTlsSymbols) {
      Symbol *S = P.first;
      uint64_t Offset = P.second * Config->Wordsize;
      if (S == nullptr) {
        if (!Config->Pic)
          continue;
        In.RelaDyn->addReloc(Target->TlsModuleIndexRel, this, Offset, S);
      } else {
        // When building a shared library we still need a dynamic relocation
        // for the module index. Therefore only checking for
        // S->IsPreemptible is not sufficient (this happens e.g. for
        // thread-locals that have been marked as local through a linker script)
        if (!S->IsPreemptible && !Config->Pic)
          continue;
        In.RelaDyn->addReloc(Target->TlsModuleIndexRel, this, Offset, S);
        // However, we can skip writing the TLS offset reloc for non-preemptible
        // symbols since it is known even in shared libraries
        if (!S->IsPreemptible)
          continue;
        Offset += Config->Wordsize;
        In.RelaDyn->addReloc(Target->TlsOffsetRel, this, Offset, S);
      }
    }

    // Do not create dynamic relocations for non-TLS
    // entries in the primary GOT.
    if (&Got == PrimGot)
      continue;

    // Dynamic relocations for "global" entries.
    for (const std::pair<Symbol *, size_t> &P : Got.Global) {
      uint64_t Offset = P.second * Config->Wordsize;
      In.RelaDyn->addReloc(Target->RelativeRel, this, Offset, P.first);
    }
    if (!Config->Pic)
      continue;
    // Dynamic relocations for "local" entries in case of PIC.
    for (const std::pair<const OutputSection *, FileGot::PageBlock> &L :
         Got.PagesMap) {
      size_t PageCount = L.second.Count;
      for (size_t PI = 0; PI < PageCount; ++PI) {
        uint64_t Offset = (L.second.FirstIndex + PI) * Config->Wordsize;
        In.RelaDyn->addReloc({Target->RelativeRel, this, Offset, L.first,
                              int64_t(PI * 0x10000)});
      }
    }
    for (const std::pair<GotEntry, size_t> &P : Got.Local16) {
      uint64_t Offset = P.second * Config->Wordsize;
      In.RelaDyn->addReloc({Target->RelativeRel, this, Offset, true,
                            P.first.first, P.first.second});
    }
  }
}

bool MipsGotSection::empty() const {
  // We add the .got section to the result for dynamic MIPS target because
  // its address and properties are mentioned in the .dynamic section.
  return Config->Relocatable;
}

uint64_t MipsGotSection::getGp(const InputFile *F) const {
  // For files without related GOT or files refer a primary GOT
  // returns "common" _gp value. For secondary GOTs calculate
  // individual _gp values.
  if (!F || !F->MipsGotIndex.hasValue() || *F->MipsGotIndex == 0)
    return ElfSym::MipsGp->getVA(0);
  return getVA() + Gots[*F->MipsGotIndex].StartIndex * Config->Wordsize +
         0x7ff0;
}

void MipsGotSection::writeTo(uint8_t *Buf) {
  // Set the MSB of the second GOT slot. This is not required by any
  // MIPS ABI documentation, though.
  //
  // There is a comment in glibc saying that "The MSB of got[1] of a
  // gnu object is set to identify gnu objects," and in GNU gold it
  // says "the second entry will be used by some runtime loaders".
  // But how this field is being used is unclear.
  //
  // We are not really willing to mimic other linkers behaviors
  // without understanding why they do that, but because all files
  // generated by GNU tools have this special GOT value, and because
  // we've been doing this for years, it is probably a safe bet to
  // keep doing this for now. We really need to revisit this to see
  // if we had to do this.
  writeUint(Buf + Config->Wordsize, (uint64_t)1 << (Config->Wordsize * 8 - 1));
  for (const FileGot &G : Gots) {
    auto Write = [&](size_t I, const Symbol *S, int64_t A) {
      uint64_t VA = A;
      if (S) {
        VA = S->getVA(A);
        if (S->StOther & STO_MIPS_MICROMIPS)
          VA |= 1;
      }
      writeUint(Buf + I * Config->Wordsize, VA);
    };
    // Write 'page address' entries to the local part of the GOT.
    for (const std::pair<const OutputSection *, FileGot::PageBlock> &L :
         G.PagesMap) {
      size_t PageCount = L.second.Count;
      uint64_t FirstPageAddr = getMipsPageAddr(L.first->Addr);
      for (size_t PI = 0; PI < PageCount; ++PI)
        Write(L.second.FirstIndex + PI, nullptr, FirstPageAddr + PI * 0x10000);
    }
    // Local, global, TLS, reloc-only  entries.
    // If TLS entry has a corresponding dynamic relocations, leave it
    // initialized by zero. Write down adjusted TLS symbol's values otherwise.
    // To calculate the adjustments use offsets for thread-local storage.
    // https://www.linux-mips.org/wiki/NPTL
    for (const std::pair<GotEntry, size_t> &P : G.Local16)
      Write(P.second, P.first.first, P.first.second);
    // Write VA to the primary GOT only. For secondary GOTs that
    // will be done by REL32 dynamic relocations.
    if (&G == &Gots.front())
      for (const std::pair<const Symbol *, size_t> &P : G.Global)
        Write(P.second, P.first, 0);
    for (const std::pair<Symbol *, size_t> &P : G.Relocs)
      Write(P.second, P.first, 0);
    for (const std::pair<Symbol *, size_t> &P : G.Tls)
      Write(P.second, P.first, P.first->IsPreemptible ? 0 : -0x7000);
    for (const std::pair<Symbol *, size_t> &P : G.DynTlsSymbols) {
      if (P.first == nullptr && !Config->Pic)
        Write(P.second, nullptr, 1);
      else if (P.first && !P.first->IsPreemptible) {
        // If we are emitting PIC code with relocations we mustn't write
        // anything to the GOT here. When using Elf_Rel relocations the value
        // one will be treated as an addend and will cause crashes at runtime
        if (!Config->Pic)
          Write(P.second, nullptr, 1);
        Write(P.second + 1, P.first, -0x8000);
      }
    }
  }
}

// On PowerPC the .plt section is used to hold the table of function addresses
// instead of the .got.plt, and the type is SHT_NOBITS similar to a .bss
// section. I don't know why we have a BSS style type for the section but it is
// consitent across both 64-bit PowerPC ABIs as well as the 32-bit PowerPC ABI.
GotPltSection::GotPltSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE,
                       Config->EMachine == EM_PPC64 ? SHT_NOBITS : SHT_PROGBITS,
                       Target->GotPltEntrySize,
                       Config->EMachine == EM_PPC64 ? ".plt" : ".got.plt") {}

void GotPltSection::addEntry(Symbol &Sym) {
  assert(Sym.PltIndex == Entries.size());
  Entries.push_back(&Sym);
}

size_t GotPltSection::getSize() const {
  return (Target->GotPltHeaderEntriesNum + Entries.size()) *
         Target->GotPltEntrySize;
}

void GotPltSection::writeTo(uint8_t *Buf) {
  Target->writeGotPltHeader(Buf);
  Buf += Target->GotPltHeaderEntriesNum * Target->GotPltEntrySize;
  for (const Symbol *B : Entries) {
    Target->writeGotPlt(Buf, *B);
    Buf += Config->Wordsize;
  }
}

bool GotPltSection::empty() const {
  // We need to emit a GOT.PLT even if it's empty if there's a symbol that
  // references the _GLOBAL_OFFSET_TABLE_ and the Target defines the symbol
  // relative to the .got.plt section.
  return Entries.empty() &&
         !(ElfSym::GlobalOffsetTable && Target->GotBaseSymInGotPlt);
}

static StringRef getIgotPltName() {
  // On ARM the IgotPltSection is part of the GotSection.
  if (Config->EMachine == EM_ARM)
    return ".got";

  // On PowerPC64 the GotPltSection is renamed to '.plt' so the IgotPltSection
  // needs to be named the same.
  if (Config->EMachine == EM_PPC64)
    return ".plt";

  return ".got.plt";
}

// On PowerPC64 the GotPltSection type is SHT_NOBITS so we have to follow suit
// with the IgotPltSection.
IgotPltSection::IgotPltSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE,
                       Config->EMachine == EM_PPC64 ? SHT_NOBITS : SHT_PROGBITS,
                       Target->GotPltEntrySize, getIgotPltName()) {}

void IgotPltSection::addEntry(Symbol &Sym) {
  Sym.IsInIgot = true;
  assert(Sym.PltIndex == Entries.size());
  Entries.push_back(&Sym);
}

size_t IgotPltSection::getSize() const {
  return Entries.size() * Target->GotPltEntrySize;
}

void IgotPltSection::writeTo(uint8_t *Buf) {
  for (const Symbol *B : Entries) {
    Target->writeIgotPlt(Buf, *B);
    Buf += Config->Wordsize;
  }
}

StringTableSection::StringTableSection(StringRef Name, bool Dynamic)
    : SyntheticSection(Dynamic ? (uint64_t)SHF_ALLOC : 0, SHT_STRTAB, 1, Name),
      Dynamic(Dynamic) {
  // ELF string tables start with a NUL byte.
  addString("");
}

// Adds a string to the string table. If HashIt is true we hash and check for
// duplicates. It is optional because the name of global symbols are already
// uniqued and hashing them again has a big cost for a small value: uniquing
// them with some other string that happens to be the same.
unsigned StringTableSection::addString(StringRef S, bool HashIt) {
  if (HashIt) {
    auto R = StringMap.insert(std::make_pair(S, this->Size));
    if (!R.second)
      return R.first->second;
  }
  unsigned Ret = this->Size;
  this->Size = this->Size + S.size() + 1;
  Strings.push_back(S);
  return Ret;
}

void StringTableSection::writeTo(uint8_t *Buf) {
  for (StringRef S : Strings) {
    memcpy(Buf, S.data(), S.size());
    Buf[S.size()] = '\0';
    Buf += S.size() + 1;
  }
}

// Returns the number of version definition entries. Because the first entry
// is for the version definition itself, it is the number of versioned symbols
// plus one. Note that we don't support multiple versions yet.
static unsigned getVerDefNum() { return Config->VersionDefinitions.size() + 1; }

template <class ELFT>
DynamicSection<ELFT>::DynamicSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_DYNAMIC, Config->Wordsize,
                       ".dynamic") {
  this->Entsize = ELFT::Is64Bits ? 16 : 8;

  // .dynamic section is not writable on MIPS and on Fuchsia OS
  // which passes -z rodynamic.
  // See "Special Section" in Chapter 4 in the following document:
  // ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
  if (Config->EMachine == EM_MIPS || Config->ZRodynamic)
    this->Flags = SHF_ALLOC;

  // Add strings to .dynstr early so that .dynstr's size will be
  // fixed early.
  for (StringRef S : Config->FilterList)
    addInt(DT_FILTER, In.DynStrTab->addString(S));
  for (StringRef S : Config->AuxiliaryList)
    addInt(DT_AUXILIARY, In.DynStrTab->addString(S));

  if (!Config->Rpath.empty())
    addInt(Config->EnableNewDtags ? DT_RUNPATH : DT_RPATH,
           In.DynStrTab->addString(Config->Rpath));

  for (InputFile *File : SharedFiles) {
    SharedFile<ELFT> *F = cast<SharedFile<ELFT>>(File);
    if (F->IsNeeded)
      addInt(DT_NEEDED, In.DynStrTab->addString(F->SoName));
  }
  if (!Config->SoName.empty())
    addInt(DT_SONAME, In.DynStrTab->addString(Config->SoName));
}

template <class ELFT>
void DynamicSection<ELFT>::add(int32_t Tag, std::function<uint64_t()> Fn) {
  Entries.push_back({Tag, Fn});
}

template <class ELFT>
void DynamicSection<ELFT>::addInt(int32_t Tag, uint64_t Val) {
  Entries.push_back({Tag, [=] { return Val; }});
}

template <class ELFT>
void DynamicSection<ELFT>::addInSec(int32_t Tag, InputSection *Sec) {
  Entries.push_back({Tag, [=] { return Sec->getVA(0); }});
}

template <class ELFT>
void DynamicSection<ELFT>::addInSecRelative(int32_t Tag, InputSection *Sec) {
  size_t TagOffset = Entries.size() * Entsize;
  Entries.push_back(
      {Tag, [=] { return Sec->getVA(0) - (getVA() + TagOffset); }});
}

template <class ELFT>
void DynamicSection<ELFT>::addOutSec(int32_t Tag, OutputSection *Sec) {
  Entries.push_back({Tag, [=] { return Sec->Addr; }});
}

template <class ELFT>
void DynamicSection<ELFT>::addSize(int32_t Tag, OutputSection *Sec) {
  Entries.push_back({Tag, [=] { return Sec->Size; }});
}

template <class ELFT>
void DynamicSection<ELFT>::addSym(int32_t Tag, Symbol *Sym) {
  Entries.push_back({Tag, [=] { return Sym->getVA(); }});
}

// A Linker script may assign the RELA relocation sections to the same
// output section. When this occurs we cannot just use the OutputSection
// Size. Moreover the [DT_JMPREL, DT_JMPREL + DT_PLTRELSZ) is permitted to
// overlap with the [DT_RELA, DT_RELA + DT_RELASZ).
static uint64_t addPltRelSz() {
  size_t Size = In.RelaPlt->getSize();
  if (In.RelaIplt->getParent() == In.RelaPlt->getParent() &&
      In.RelaIplt->Name == In.RelaPlt->Name)
    Size += In.RelaIplt->getSize();
  return Size;
}

// Add remaining entries to complete .dynamic contents.
template <class ELFT> void DynamicSection<ELFT>::finalizeContents() {
  // Set DT_FLAGS and DT_FLAGS_1.
  uint32_t DtFlags = 0;
  uint32_t DtFlags1 = 0;
  if (Config->Bsymbolic)
    DtFlags |= DF_SYMBOLIC;
  if (Config->ZGlobal)
    DtFlags1 |= DF_1_GLOBAL;
  if (Config->ZInitfirst)
    DtFlags1 |= DF_1_INITFIRST;
  if (Config->ZInterpose)
    DtFlags1 |= DF_1_INTERPOSE;
  if (Config->ZNodefaultlib)
    DtFlags1 |= DF_1_NODEFLIB;
  if (Config->ZNodelete)
    DtFlags1 |= DF_1_NODELETE;
  if (Config->ZNodlopen)
    DtFlags1 |= DF_1_NOOPEN;
  if (Config->ZNow) {
    DtFlags |= DF_BIND_NOW;
    DtFlags1 |= DF_1_NOW;
  }
  if (Config->ZOrigin) {
    DtFlags |= DF_ORIGIN;
    DtFlags1 |= DF_1_ORIGIN;
  }
  if (!Config->ZText)
    DtFlags |= DF_TEXTREL;
  if (Config->HasStaticTlsModel)
    DtFlags |= DF_STATIC_TLS;

  if (DtFlags)
    addInt(DT_FLAGS, DtFlags);
  if (DtFlags1)
    addInt(DT_FLAGS_1, DtFlags1);

  // DT_DEBUG is a pointer to debug informaion used by debuggers at runtime. We
  // need it for each process, so we don't write it for DSOs. The loader writes
  // the pointer into this entry.
  //
  // DT_DEBUG is the only .dynamic entry that needs to be written to. Some
  // systems (currently only Fuchsia OS) provide other means to give the
  // debugger this information. Such systems may choose make .dynamic read-only.
  // If the target is such a system (used -z rodynamic) don't write DT_DEBUG.
  if (!Config->Shared && !Config->Relocatable && !Config->ZRodynamic)
    addInt(DT_DEBUG, 0);

  if (OutputSection *Sec = In.DynStrTab->getParent())
    this->Link = Sec->SectionIndex;

  if (!In.RelaDyn->empty()) {
    addInSec(In.RelaDyn->DynamicTag, In.RelaDyn);
    addSize(In.RelaDyn->SizeDynamicTag, In.RelaDyn->getParent());

    bool IsRela = Config->IsRela;
    addInt(IsRela ? DT_RELAENT : DT_RELENT,
           IsRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel));

    // MIPS dynamic loader does not support RELCOUNT tag.
    // The problem is in the tight relation between dynamic
    // relocations and GOT. So do not emit this tag on MIPS.
    if (Config->EMachine != EM_MIPS) {
      size_t NumRelativeRels = In.RelaDyn->getRelativeRelocCount();
      if (Config->ZCombreloc && NumRelativeRels)
        addInt(IsRela ? DT_RELACOUNT : DT_RELCOUNT, NumRelativeRels);
    }
  }
  if (In.RelrDyn && !In.RelrDyn->Relocs.empty()) {
    addInSec(Config->UseAndroidRelrTags ? DT_ANDROID_RELR : DT_RELR,
             In.RelrDyn);
    addSize(Config->UseAndroidRelrTags ? DT_ANDROID_RELRSZ : DT_RELRSZ,
            In.RelrDyn->getParent());
    addInt(Config->UseAndroidRelrTags ? DT_ANDROID_RELRENT : DT_RELRENT,
           sizeof(Elf_Relr));
  }
  // .rel[a].plt section usually consists of two parts, containing plt and
  // iplt relocations. It is possible to have only iplt relocations in the
  // output. In that case RelaPlt is empty and have zero offset, the same offset
  // as RelaIplt have. And we still want to emit proper dynamic tags for that
  // case, so here we always use RelaPlt as marker for the begining of
  // .rel[a].plt section.
  if (In.RelaPlt->getParent()->Live) {
    addInSec(DT_JMPREL, In.RelaPlt);
    Entries.push_back({DT_PLTRELSZ, addPltRelSz});
    switch (Config->EMachine) {
    case EM_MIPS:
      addInSec(DT_MIPS_PLTGOT, In.GotPlt);
      break;
    case EM_SPARCV9:
      addInSec(DT_PLTGOT, In.Plt);
      break;
    default:
      addInSec(DT_PLTGOT, In.GotPlt);
      break;
    }
    addInt(DT_PLTREL, Config->IsRela ? DT_RELA : DT_REL);
  }

  addInSec(DT_SYMTAB, In.DynSymTab);
  addInt(DT_SYMENT, sizeof(Elf_Sym));
  addInSec(DT_STRTAB, In.DynStrTab);
  addInt(DT_STRSZ, In.DynStrTab->getSize());
  if (!Config->ZText)
    addInt(DT_TEXTREL, 0);
  if (In.GnuHashTab)
    addInSec(DT_GNU_HASH, In.GnuHashTab);
  if (In.HashTab)
    addInSec(DT_HASH, In.HashTab);

  if (Out::PreinitArray) {
    addOutSec(DT_PREINIT_ARRAY, Out::PreinitArray);
    addSize(DT_PREINIT_ARRAYSZ, Out::PreinitArray);
  }
  if (Out::InitArray) {
    addOutSec(DT_INIT_ARRAY, Out::InitArray);
    addSize(DT_INIT_ARRAYSZ, Out::InitArray);
  }
  if (Out::FiniArray) {
    addOutSec(DT_FINI_ARRAY, Out::FiniArray);
    addSize(DT_FINI_ARRAYSZ, Out::FiniArray);
  }

  if (Symbol *B = Symtab->find(Config->Init))
    if (B->isDefined())
      addSym(DT_INIT, B);
  if (Symbol *B = Symtab->find(Config->Fini))
    if (B->isDefined())
      addSym(DT_FINI, B);

  bool HasVerNeed = InX<ELFT>::VerNeed->getNeedNum() != 0;
  if (HasVerNeed || In.VerDef)
    addInSec(DT_VERSYM, InX<ELFT>::VerSym);
  if (In.VerDef) {
    addInSec(DT_VERDEF, In.VerDef);
    addInt(DT_VERDEFNUM, getVerDefNum());
  }
  if (HasVerNeed) {
    addInSec(DT_VERNEED, InX<ELFT>::VerNeed);
    addInt(DT_VERNEEDNUM, InX<ELFT>::VerNeed->getNeedNum());
  }

  if (Config->EMachine == EM_MIPS) {
    addInt(DT_MIPS_RLD_VERSION, 1);
    addInt(DT_MIPS_FLAGS, RHF_NOTPOT);
    addInt(DT_MIPS_BASE_ADDRESS, Target->getImageBase());
    addInt(DT_MIPS_SYMTABNO, In.DynSymTab->getNumSymbols());

    add(DT_MIPS_LOCAL_GOTNO, [] { return In.MipsGot->getLocalEntriesNum(); });

    if (const Symbol *B = In.MipsGot->getFirstGlobalEntry())
      addInt(DT_MIPS_GOTSYM, B->DynsymIndex);
    else
      addInt(DT_MIPS_GOTSYM, In.DynSymTab->getNumSymbols());
    addInSec(DT_PLTGOT, In.MipsGot);
    if (In.MipsRldMap) {
      if (!Config->Pie)
        addInSec(DT_MIPS_RLD_MAP, In.MipsRldMap);
      // Store the offset to the .rld_map section
      // relative to the address of the tag.
      addInSecRelative(DT_MIPS_RLD_MAP_REL, In.MipsRldMap);
    }
  }

  // Glink dynamic tag is required by the V2 abi if the plt section isn't empty.
  if (Config->EMachine == EM_PPC64 && !In.Plt->empty()) {
    // The Glink tag points to 32 bytes before the first lazy symbol resolution
    // stub, which starts directly after the header.
    Entries.push_back({DT_PPC64_GLINK, [=] {
                         unsigned Offset = Target->PltHeaderSize - 32;
                         return In.Plt->getVA(0) + Offset;
                       }});
  }

  addInt(DT_NULL, 0);

  getParent()->Link = this->Link;
  this->Size = Entries.size() * this->Entsize;
}

template <class ELFT> void DynamicSection<ELFT>::writeTo(uint8_t *Buf) {
  auto *P = reinterpret_cast<Elf_Dyn *>(Buf);

  for (std::pair<int32_t, std::function<uint64_t()>> &KV : Entries) {
    P->d_tag = KV.first;
    P->d_un.d_val = KV.second();
    ++P;
  }
}

uint64_t DynamicReloc::getOffset() const {
  return InputSec->getVA(OffsetInSec);
}

int64_t DynamicReloc::computeAddend() const {
  if (UseSymVA)
    return Sym->getVA(Addend);
  if (!OutputSec)
    return Addend;
  // See the comment in the DynamicReloc ctor.
  return getMipsPageAddr(OutputSec->Addr) + Addend;
}

uint32_t DynamicReloc::getSymIndex() const {
  if (Sym && !UseSymVA)
    return Sym->DynsymIndex;
  return 0;
}

RelocationBaseSection::RelocationBaseSection(StringRef Name, uint32_t Type,
                                             int32_t DynamicTag,
                                             int32_t SizeDynamicTag)
    : SyntheticSection(SHF_ALLOC, Type, Config->Wordsize, Name),
      DynamicTag(DynamicTag), SizeDynamicTag(SizeDynamicTag) {}

void RelocationBaseSection::addReloc(RelType DynType, InputSectionBase *IS,
                                     uint64_t OffsetInSec, Symbol *Sym) {
  addReloc({DynType, IS, OffsetInSec, false, Sym, 0});
}

void RelocationBaseSection::addReloc(RelType DynType,
                                     InputSectionBase *InputSec,
                                     uint64_t OffsetInSec, Symbol *Sym,
                                     int64_t Addend, RelExpr Expr,
                                     RelType Type) {
  // Write the addends to the relocated address if required. We skip
  // it if the written value would be zero.
  if (Config->WriteAddends && (Expr != R_ADDEND || Addend != 0))
    InputSec->Relocations.push_back({Expr, Type, OffsetInSec, Addend, Sym});
  addReloc({DynType, InputSec, OffsetInSec, Expr != R_ADDEND, Sym, Addend});
}

void RelocationBaseSection::addReloc(const DynamicReloc &Reloc) {
  if (Reloc.Type == Target->RelativeRel)
    ++NumRelativeRelocs;
  Relocs.push_back(Reloc);
}

void RelocationBaseSection::finalizeContents() {
  // When linking glibc statically, .rel{,a}.plt contains R_*_IRELATIVE
  // relocations due to IFUNC (e.g. strcpy). sh_link will be set to 0 in that
  // case.
  InputSection *SymTab = Config->Relocatable ? In.SymTab : In.DynSymTab;
  if (SymTab && SymTab->getParent())
    getParent()->Link = SymTab->getParent()->SectionIndex;
  else
    getParent()->Link = 0;

  if (In.RelaPlt == this)
    getParent()->Info = In.GotPlt->getParent()->SectionIndex;
  if (In.RelaIplt == this)
    getParent()->Info = In.IgotPlt->getParent()->SectionIndex;
}

RelrBaseSection::RelrBaseSection()
    : SyntheticSection(SHF_ALLOC,
                       Config->UseAndroidRelrTags ? SHT_ANDROID_RELR : SHT_RELR,
                       Config->Wordsize, ".relr.dyn") {}

template <class ELFT>
static void encodeDynamicReloc(typename ELFT::Rela *P,
                               const DynamicReloc &Rel) {
  if (Config->IsRela)
    P->r_addend = Rel.computeAddend();
  P->r_offset = Rel.getOffset();
  P->setSymbolAndType(Rel.getSymIndex(), Rel.Type, Config->IsMips64EL);
}

template <class ELFT>
RelocationSection<ELFT>::RelocationSection(StringRef Name, bool Sort)
    : RelocationBaseSection(Name, Config->IsRela ? SHT_RELA : SHT_REL,
                            Config->IsRela ? DT_RELA : DT_REL,
                            Config->IsRela ? DT_RELASZ : DT_RELSZ),
      Sort(Sort) {
  this->Entsize = Config->IsRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel);
}

static bool compRelocations(const DynamicReloc &A, const DynamicReloc &B) {
  bool AIsRel = A.Type == Target->RelativeRel;
  bool BIsRel = B.Type == Target->RelativeRel;
  if (AIsRel != BIsRel)
    return AIsRel;
  return A.getSymIndex() < B.getSymIndex();
}

template <class ELFT> void RelocationSection<ELFT>::writeTo(uint8_t *Buf) {
  if (Sort)
    std::stable_sort(Relocs.begin(), Relocs.end(), compRelocations);

  for (const DynamicReloc &Rel : Relocs) {
    encodeDynamicReloc<ELFT>(reinterpret_cast<Elf_Rela *>(Buf), Rel);
    Buf += Config->IsRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel);
  }
}

template <class ELFT> unsigned RelocationSection<ELFT>::getRelocOffset() {
  return this->Entsize * Relocs.size();
}

template <class ELFT>
AndroidPackedRelocationSection<ELFT>::AndroidPackedRelocationSection(
    StringRef Name)
    : RelocationBaseSection(
          Name, Config->IsRela ? SHT_ANDROID_RELA : SHT_ANDROID_REL,
          Config->IsRela ? DT_ANDROID_RELA : DT_ANDROID_REL,
          Config->IsRela ? DT_ANDROID_RELASZ : DT_ANDROID_RELSZ) {
  this->Entsize = 1;
}

template <class ELFT>
bool AndroidPackedRelocationSection<ELFT>::updateAllocSize() {
  // This function computes the contents of an Android-format packed relocation
  // section.
  //
  // This format compresses relocations by using relocation groups to factor out
  // fields that are common between relocations and storing deltas from previous
  // relocations in SLEB128 format (which has a short representation for small
  // numbers). A good example of a relocation type with common fields is
  // R_*_RELATIVE, which is normally used to represent function pointers in
  // vtables. In the REL format, each relative relocation has the same r_info
  // field, and is only different from other relative relocations in terms of
  // the r_offset field. By sorting relocations by offset, grouping them by
  // r_info and representing each relocation with only the delta from the
  // previous offset, each 8-byte relocation can be compressed to as little as 1
  // byte (or less with run-length encoding). This relocation packer was able to
  // reduce the size of the relocation section in an Android Chromium DSO from
  // 2,911,184 bytes to 174,693 bytes, or 6% of the original size.
  //
  // A relocation section consists of a header containing the literal bytes
  // 'APS2' followed by a sequence of SLEB128-encoded integers. The first two
  // elements are the total number of relocations in the section and an initial
  // r_offset value. The remaining elements define a sequence of relocation
  // groups. Each relocation group starts with a header consisting of the
  // following elements:
  //
  // - the number of relocations in the relocation group
  // - flags for the relocation group
  // - (if RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG is set) the r_offset delta
  //   for each relocation in the group.
  // - (if RELOCATION_GROUPED_BY_INFO_FLAG is set) the value of the r_info
  //   field for each relocation in the group.
  // - (if RELOCATION_GROUP_HAS_ADDEND_FLAG and
  //   RELOCATION_GROUPED_BY_ADDEND_FLAG are set) the r_addend delta for
  //   each relocation in the group.
  //
  // Following the relocation group header are descriptions of each of the
  // relocations in the group. They consist of the following elements:
  //
  // - (if RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG is not set) the r_offset
  //   delta for this relocation.
  // - (if RELOCATION_GROUPED_BY_INFO_FLAG is not set) the value of the r_info
  //   field for this relocation.
  // - (if RELOCATION_GROUP_HAS_ADDEND_FLAG is set and
  //   RELOCATION_GROUPED_BY_ADDEND_FLAG is not set) the r_addend delta for
  //   this relocation.

  size_t OldSize = RelocData.size();

  RelocData = {'A', 'P', 'S', '2'};
  raw_svector_ostream OS(RelocData);
  auto Add = [&](int64_t V) { encodeSLEB128(V, OS); };

  // The format header includes the number of relocations and the initial
  // offset (we set this to zero because the first relocation group will
  // perform the initial adjustment).
  Add(Relocs.size());
  Add(0);

  std::vector<Elf_Rela> Relatives, NonRelatives;

  for (const DynamicReloc &Rel : Relocs) {
    Elf_Rela R;
    encodeDynamicReloc<ELFT>(&R, Rel);

    if (R.getType(Config->IsMips64EL) == Target->RelativeRel)
      Relatives.push_back(R);
    else
      NonRelatives.push_back(R);
  }

  llvm::sort(Relatives, [](const Elf_Rel &A, const Elf_Rel &B) {
    return A.r_offset < B.r_offset;
  });

  // Try to find groups of relative relocations which are spaced one word
  // apart from one another. These generally correspond to vtable entries. The
  // format allows these groups to be encoded using a sort of run-length
  // encoding, but each group will cost 7 bytes in addition to the offset from
  // the previous group, so it is only profitable to do this for groups of
  // size 8 or larger.
  std::vector<Elf_Rela> UngroupedRelatives;
  std::vector<std::vector<Elf_Rela>> RelativeGroups;
  for (auto I = Relatives.begin(), E = Relatives.end(); I != E;) {
    std::vector<Elf_Rela> Group;
    do {
      Group.push_back(*I++);
    } while (I != E && (I - 1)->r_offset + Config->Wordsize == I->r_offset);

    if (Group.size() < 8)
      UngroupedRelatives.insert(UngroupedRelatives.end(), Group.begin(),
                                Group.end());
    else
      RelativeGroups.emplace_back(std::move(Group));
  }

  unsigned HasAddendIfRela =
      Config->IsRela ? RELOCATION_GROUP_HAS_ADDEND_FLAG : 0;

  uint64_t Offset = 0;
  uint64_t Addend = 0;

  // Emit the run-length encoding for the groups of adjacent relative
  // relocations. Each group is represented using two groups in the packed
  // format. The first is used to set the current offset to the start of the
  // group (and also encodes the first relocation), and the second encodes the
  // remaining relocations.
  for (std::vector<Elf_Rela> &G : RelativeGroups) {
    // The first relocation in the group.
    Add(1);
    Add(RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG |
        RELOCATION_GROUPED_BY_INFO_FLAG | HasAddendIfRela);
    Add(G[0].r_offset - Offset);
    Add(Target->RelativeRel);
    if (Config->IsRela) {
      Add(G[0].r_addend - Addend);
      Addend = G[0].r_addend;
    }

    // The remaining relocations.
    Add(G.size() - 1);
    Add(RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG |
        RELOCATION_GROUPED_BY_INFO_FLAG | HasAddendIfRela);
    Add(Config->Wordsize);
    Add(Target->RelativeRel);
    if (Config->IsRela) {
      for (auto I = G.begin() + 1, E = G.end(); I != E; ++I) {
        Add(I->r_addend - Addend);
        Addend = I->r_addend;
      }
    }

    Offset = G.back().r_offset;
  }

  // Now the ungrouped relatives.
  if (!UngroupedRelatives.empty()) {
    Add(UngroupedRelatives.size());
    Add(RELOCATION_GROUPED_BY_INFO_FLAG | HasAddendIfRela);
    Add(Target->RelativeRel);
    for (Elf_Rela &R : UngroupedRelatives) {
      Add(R.r_offset - Offset);
      Offset = R.r_offset;
      if (Config->IsRela) {
        Add(R.r_addend - Addend);
        Addend = R.r_addend;
      }
    }
  }

  // Finally the non-relative relocations.
  llvm::sort(NonRelatives, [](const Elf_Rela &A, const Elf_Rela &B) {
    return A.r_offset < B.r_offset;
  });
  if (!NonRelatives.empty()) {
    Add(NonRelatives.size());
    Add(HasAddendIfRela);
    for (Elf_Rela &R : NonRelatives) {
      Add(R.r_offset - Offset);
      Offset = R.r_offset;
      Add(R.r_info);
      if (Config->IsRela) {
        Add(R.r_addend - Addend);
        Addend = R.r_addend;
      }
    }
  }

  // Don't allow the section to shrink; otherwise the size of the section can
  // oscillate infinitely.
  if (RelocData.size() < OldSize)
    RelocData.append(OldSize - RelocData.size(), 0);

  // Returns whether the section size changed. We need to keep recomputing both
  // section layout and the contents of this section until the size converges
  // because changing this section's size can affect section layout, which in
  // turn can affect the sizes of the LEB-encoded integers stored in this
  // section.
  return RelocData.size() != OldSize;
}

template <class ELFT> RelrSection<ELFT>::RelrSection() {
  this->Entsize = Config->Wordsize;
}

template <class ELFT> bool RelrSection<ELFT>::updateAllocSize() {
  // This function computes the contents of an SHT_RELR packed relocation
  // section.
  //
  // Proposal for adding SHT_RELR sections to generic-abi is here:
  //   https://groups.google.com/forum/#!topic/generic-abi/bX460iggiKg
  //
  // The encoded sequence of Elf64_Relr entries in a SHT_RELR section looks
  // like [ AAAAAAAA BBBBBBB1 BBBBBBB1 ... AAAAAAAA BBBBBB1 ... ]
  //
  // i.e. start with an address, followed by any number of bitmaps. The address
  // entry encodes 1 relocation. The subsequent bitmap entries encode up to 63
  // relocations each, at subsequent offsets following the last address entry.
  //
  // The bitmap entries must have 1 in the least significant bit. The assumption
  // here is that an address cannot have 1 in lsb. Odd addresses are not
  // supported.
  //
  // Excluding the least significant bit in the bitmap, each non-zero bit in
  // the bitmap represents a relocation to be applied to a corresponding machine
  // word that follows the base address word. The second least significant bit
  // represents the machine word immediately following the initial address, and
  // each bit that follows represents the next word, in linear order. As such,
  // a single bitmap can encode up to 31 relocations in a 32-bit object, and
  // 63 relocations in a 64-bit object.
  //
  // This encoding has a couple of interesting properties:
  // 1. Looking at any entry, it is clear whether it's an address or a bitmap:
  //    even means address, odd means bitmap.
  // 2. Just a simple list of addresses is a valid encoding.

  size_t OldSize = RelrRelocs.size();
  RelrRelocs.clear();

  // Same as Config->Wordsize but faster because this is a compile-time
  // constant.
  const size_t Wordsize = sizeof(typename ELFT::uint);

  // Number of bits to use for the relocation offsets bitmap.
  // Must be either 63 or 31.
  const size_t NBits = Wordsize * 8 - 1;

  // Get offsets for all relative relocations and sort them.
  std::vector<uint64_t> Offsets;
  for (const RelativeReloc &Rel : Relocs)
    Offsets.push_back(Rel.getOffset());
  llvm::sort(Offsets.begin(), Offsets.end());

  // For each leading relocation, find following ones that can be folded
  // as a bitmap and fold them.
  for (size_t I = 0, E = Offsets.size(); I < E;) {
    // Add a leading relocation.
    RelrRelocs.push_back(Elf_Relr(Offsets[I]));
    uint64_t Base = Offsets[I] + Wordsize;
    ++I;

    // Find foldable relocations to construct bitmaps.
    while (I < E) {
      uint64_t Bitmap = 0;

      while (I < E) {
        uint64_t Delta = Offsets[I] - Base;

        // If it is too far, it cannot be folded.
        if (Delta >= NBits * Wordsize)
          break;

        // If it is not a multiple of wordsize away, it cannot be folded.
        if (Delta % Wordsize)
          break;

        // Fold it.
        Bitmap |= 1ULL << (Delta / Wordsize);
        ++I;
      }

      if (!Bitmap)
        break;

      RelrRelocs.push_back(Elf_Relr((Bitmap << 1) | 1));
      Base += NBits * Wordsize;
    }
  }

  return RelrRelocs.size() != OldSize;
}

SymbolTableBaseSection::SymbolTableBaseSection(StringTableSection &StrTabSec)
    : SyntheticSection(StrTabSec.isDynamic() ? (uint64_t)SHF_ALLOC : 0,
                       StrTabSec.isDynamic() ? SHT_DYNSYM : SHT_SYMTAB,
                       Config->Wordsize,
                       StrTabSec.isDynamic() ? ".dynsym" : ".symtab"),
      StrTabSec(StrTabSec) {}

// Orders symbols according to their positions in the GOT,
// in compliance with MIPS ABI rules.
// See "Global Offset Table" in Chapter 5 in the following document
// for detailed description:
// ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
static bool sortMipsSymbols(const SymbolTableEntry &L,
                            const SymbolTableEntry &R) {
  // Sort entries related to non-local preemptible symbols by GOT indexes.
  // All other entries go to the beginning of a dynsym in arbitrary order.
  if (L.Sym->isInGot() && R.Sym->isInGot())
    return L.Sym->GotIndex < R.Sym->GotIndex;
  if (!L.Sym->isInGot() && !R.Sym->isInGot())
    return false;
  return !L.Sym->isInGot();
}

void SymbolTableBaseSection::finalizeContents() {
  if (OutputSection *Sec = StrTabSec.getParent())
    getParent()->Link = Sec->SectionIndex;

  if (this->Type != SHT_DYNSYM) {
    sortSymTabSymbols();
    return;
  }

  // If it is a .dynsym, there should be no local symbols, but we need
  // to do a few things for the dynamic linker.

  // Section's Info field has the index of the first non-local symbol.
  // Because the first symbol entry is a null entry, 1 is the first.
  getParent()->Info = 1;

  if (In.GnuHashTab) {
    // NB: It also sorts Symbols to meet the GNU hash table requirements.
    In.GnuHashTab->addSymbols(Symbols);
  } else if (Config->EMachine == EM_MIPS) {
    std::stable_sort(Symbols.begin(), Symbols.end(), sortMipsSymbols);
  }

  size_t I = 0;
  for (const SymbolTableEntry &S : Symbols)
    S.Sym->DynsymIndex = ++I;
}

// The ELF spec requires that all local symbols precede global symbols, so we
// sort symbol entries in this function. (For .dynsym, we don't do that because
// symbols for dynamic linking are inherently all globals.)
//
// Aside from above, we put local symbols in groups starting with the STT_FILE
// symbol. That is convenient for purpose of identifying where are local symbols
// coming from.
void SymbolTableBaseSection::sortSymTabSymbols() {
  // Move all local symbols before global symbols.
  auto E = std::stable_partition(
      Symbols.begin(), Symbols.end(), [](const SymbolTableEntry &S) {
        return S.Sym->isLocal() || S.Sym->computeBinding() == STB_LOCAL;
      });
  size_t NumLocals = E - Symbols.begin();
  getParent()->Info = NumLocals + 1;

  // We want to group the local symbols by file. For that we rebuild the local
  // part of the symbols vector. We do not need to care about the STT_FILE
  // symbols, they are already naturally placed first in each group. That
  // happens because STT_FILE is always the first symbol in the object and hence
  // precede all other local symbols we add for a file.
  MapVector<InputFile *, std::vector<SymbolTableEntry>> Arr;
  for (const SymbolTableEntry &S : llvm::make_range(Symbols.begin(), E))
    Arr[S.Sym->File].push_back(S);

  auto I = Symbols.begin();
  for (std::pair<InputFile *, std::vector<SymbolTableEntry>> &P : Arr)
    for (SymbolTableEntry &Entry : P.second)
      *I++ = Entry;
}

void SymbolTableBaseSection::addSymbol(Symbol *B) {
  // Adding a local symbol to a .dynsym is a bug.
  assert(this->Type != SHT_DYNSYM || !B->isLocal());

  bool HashIt = B->isLocal();
  Symbols.push_back({B, StrTabSec.addString(B->getName(), HashIt)});
}

size_t SymbolTableBaseSection::getSymbolIndex(Symbol *Sym) {
  // Initializes symbol lookup tables lazily. This is used only
  // for -r or -emit-relocs.
  llvm::call_once(OnceFlag, [&] {
    SymbolIndexMap.reserve(Symbols.size());
    size_t I = 0;
    for (const SymbolTableEntry &E : Symbols) {
      if (E.Sym->Type == STT_SECTION)
        SectionIndexMap[E.Sym->getOutputSection()] = ++I;
      else
        SymbolIndexMap[E.Sym] = ++I;
    }
  });

  // Section symbols are mapped based on their output sections
  // to maintain their semantics.
  if (Sym->Type == STT_SECTION)
    return SectionIndexMap.lookup(Sym->getOutputSection());
  return SymbolIndexMap.lookup(Sym);
}

template <class ELFT>
SymbolTableSection<ELFT>::SymbolTableSection(StringTableSection &StrTabSec)
    : SymbolTableBaseSection(StrTabSec) {
  this->Entsize = sizeof(Elf_Sym);
}

static BssSection *getCommonSec(Symbol *Sym) {
  if (!Config->DefineCommon)
    if (auto *D = dyn_cast<Defined>(Sym))
      return dyn_cast_or_null<BssSection>(D->Section);
  return nullptr;
}

static uint32_t getSymSectionIndex(Symbol *Sym) {
  if (getCommonSec(Sym))
    return SHN_COMMON;
  if (!isa<Defined>(Sym) || Sym->NeedsPltAddr)
    return SHN_UNDEF;
  if (const OutputSection *OS = Sym->getOutputSection())
    return OS->SectionIndex >= SHN_LORESERVE ? (uint32_t)SHN_XINDEX
                                             : OS->SectionIndex;
  return SHN_ABS;
}

// Write the internal symbol table contents to the output symbol table.
template <class ELFT> void SymbolTableSection<ELFT>::writeTo(uint8_t *Buf) {
  // The first entry is a null entry as per the ELF spec.
  memset(Buf, 0, sizeof(Elf_Sym));
  Buf += sizeof(Elf_Sym);

  auto *ESym = reinterpret_cast<Elf_Sym *>(Buf);

  for (SymbolTableEntry &Ent : Symbols) {
    Symbol *Sym = Ent.Sym;

    // Set st_info and st_other.
    ESym->st_other = 0;
    if (Sym->isLocal()) {
      ESym->setBindingAndType(STB_LOCAL, Sym->Type);
    } else {
      ESym->setBindingAndType(Sym->computeBinding(), Sym->Type);
      ESym->setVisibility(Sym->Visibility);
    }

    ESym->st_name = Ent.StrTabOffset;
    ESym->st_shndx = getSymSectionIndex(Ent.Sym);

    // Copy symbol size if it is a defined symbol. st_size is not significant
    // for undefined symbols, so whether copying it or not is up to us if that's
    // the case. We'll leave it as zero because by not setting a value, we can
    // get the exact same outputs for two sets of input files that differ only
    // in undefined symbol size in DSOs.
    if (ESym->st_shndx == SHN_UNDEF)
      ESym->st_size = 0;
    else
      ESym->st_size = Sym->getSize();

    // st_value is usually an address of a symbol, but that has a
    // special meaining for uninstantiated common symbols (this can
    // occur if -r is given).
    if (BssSection *CommonSec = getCommonSec(Ent.Sym))
      ESym->st_value = CommonSec->Alignment;
    else
      ESym->st_value = Sym->getVA();

    ++ESym;
  }

  // On MIPS we need to mark symbol which has a PLT entry and requires
  // pointer equality by STO_MIPS_PLT flag. That is necessary to help
  // dynamic linker distinguish such symbols and MIPS lazy-binding stubs.
  // https://sourceware.org/ml/binutils/2008-07/txt00000.txt
  if (Config->EMachine == EM_MIPS) {
    auto *ESym = reinterpret_cast<Elf_Sym *>(Buf);

    for (SymbolTableEntry &Ent : Symbols) {
      Symbol *Sym = Ent.Sym;
      if (Sym->isInPlt() && Sym->NeedsPltAddr)
        ESym->st_other |= STO_MIPS_PLT;
      if (isMicroMips()) {
        // Set STO_MIPS_MICROMIPS flag and less-significant bit for
        // a defined microMIPS symbol and symbol should point to its
        // PLT entry (in case of microMIPS, PLT entries always contain
        // microMIPS code).
        if (Sym->isDefined() &&
            ((Sym->StOther & STO_MIPS_MICROMIPS) || Sym->NeedsPltAddr)) {
          if (StrTabSec.isDynamic())
            ESym->st_value |= 1;
          ESym->st_other |= STO_MIPS_MICROMIPS;
        }
      }
      if (Config->Relocatable)
        if (auto *D = dyn_cast<Defined>(Sym))
          if (isMipsPIC<ELFT>(D))
            ESym->st_other |= STO_MIPS_PIC;
      ++ESym;
    }
  }
}

SymtabShndxSection::SymtabShndxSection()
    : SyntheticSection(0, SHT_SYMTAB_SHNDX, 4, ".symtab_shndxr") {
  this->Entsize = 4;
}

void SymtabShndxSection::writeTo(uint8_t *Buf) {
  // We write an array of 32 bit values, where each value has 1:1 association
  // with an entry in .symtab. If the corresponding entry contains SHN_XINDEX,
  // we need to write actual index, otherwise, we must write SHN_UNDEF(0).
  Buf += 4; // Ignore .symtab[0] entry.
  for (const SymbolTableEntry &Entry : In.SymTab->getSymbols()) {
    if (getSymSectionIndex(Entry.Sym) == SHN_XINDEX)
      write32(Buf, Entry.Sym->getOutputSection()->SectionIndex);
    Buf += 4;
  }
}

bool SymtabShndxSection::empty() const {
  // SHT_SYMTAB can hold symbols with section indices values up to
  // SHN_LORESERVE. If we need more, we want to use extension SHT_SYMTAB_SHNDX
  // section. Problem is that we reveal the final section indices a bit too
  // late, and we do not know them here. For simplicity, we just always create
  // a .symtab_shndxr section when the amount of output sections is huge.
  size_t Size = 0;
  for (BaseCommand *Base : Script->SectionCommands)
    if (isa<OutputSection>(Base))
      ++Size;
  return Size < SHN_LORESERVE;
}

void SymtabShndxSection::finalizeContents() {
  getParent()->Link = In.SymTab->getParent()->SectionIndex;
}

size_t SymtabShndxSection::getSize() const {
  return In.SymTab->getNumSymbols() * 4;
}

// .hash and .gnu.hash sections contain on-disk hash tables that map
// symbol names to their dynamic symbol table indices. Their purpose
// is to help the dynamic linker resolve symbols quickly. If ELF files
// don't have them, the dynamic linker has to do linear search on all
// dynamic symbols, which makes programs slower. Therefore, a .hash
// section is added to a DSO by default. A .gnu.hash is added if you
// give the -hash-style=gnu or -hash-style=both option.
//
// The Unix semantics of resolving dynamic symbols is somewhat expensive.
// Each ELF file has a list of DSOs that the ELF file depends on and a
// list of dynamic symbols that need to be resolved from any of the
// DSOs. That means resolving all dynamic symbols takes O(m)*O(n)
// where m is the number of DSOs and n is the number of dynamic
// symbols. For modern large programs, both m and n are large.  So
// making each step faster by using hash tables substiantially
// improves time to load programs.
//
// (Note that this is not the only way to design the shared library.
// For instance, the Windows DLL takes a different approach. On
// Windows, each dynamic symbol has a name of DLL from which the symbol
// has to be resolved. That makes the cost of symbol resolution O(n).
// This disables some hacky techniques you can use on Unix such as
// LD_PRELOAD, but this is arguably better semantics than the Unix ones.)
//
// Due to historical reasons, we have two different hash tables, .hash
// and .gnu.hash. They are for the same purpose, and .gnu.hash is a new
// and better version of .hash. .hash is just an on-disk hash table, but
// .gnu.hash has a bloom filter in addition to a hash table to skip
// DSOs very quickly. If you are sure that your dynamic linker knows
// about .gnu.hash, you want to specify -hash-style=gnu. Otherwise, a
// safe bet is to specify -hash-style=both for backward compatibilty.
GnuHashTableSection::GnuHashTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_HASH, Config->Wordsize, ".gnu.hash") {
}

void GnuHashTableSection::finalizeContents() {
  if (OutputSection *Sec = In.DynSymTab->getParent())
    getParent()->Link = Sec->SectionIndex;

  // Computes bloom filter size in word size. We want to allocate 12
  // bits for each symbol. It must be a power of two.
  if (Symbols.empty()) {
    MaskWords = 1;
  } else {
    uint64_t NumBits = Symbols.size() * 12;
    MaskWords = NextPowerOf2(NumBits / (Config->Wordsize * 8));
  }

  Size = 16;                            // Header
  Size += Config->Wordsize * MaskWords; // Bloom filter
  Size += NBuckets * 4;                 // Hash buckets
  Size += Symbols.size() * 4;           // Hash values
}

void GnuHashTableSection::writeTo(uint8_t *Buf) {
  // The output buffer is not guaranteed to be zero-cleared because we pre-
  // fill executable sections with trap instructions. This is a precaution
  // for that case, which happens only when -no-rosegment is given.
  memset(Buf, 0, Size);

  // Write a header.
  write32(Buf, NBuckets);
  write32(Buf + 4, In.DynSymTab->getNumSymbols() - Symbols.size());
  write32(Buf + 8, MaskWords);
  write32(Buf + 12, Shift2);
  Buf += 16;

  // Write a bloom filter and a hash table.
  writeBloomFilter(Buf);
  Buf += Config->Wordsize * MaskWords;
  writeHashTable(Buf);
}

// This function writes a 2-bit bloom filter. This bloom filter alone
// usually filters out 80% or more of all symbol lookups [1].
// The dynamic linker uses the hash table only when a symbol is not
// filtered out by a bloom filter.
//
// [1] Ulrich Drepper (2011), "How To Write Shared Libraries" (Ver. 4.1.2),
//     p.9, https://www.akkadia.org/drepper/dsohowto.pdf
void GnuHashTableSection::writeBloomFilter(uint8_t *Buf) {
  unsigned C = Config->Is64 ? 64 : 32;
  for (const Entry &Sym : Symbols) {
    // When C = 64, we choose a word with bits [6:...] and set 1 to two bits in
    // the word using bits [0:5] and [26:31].
    size_t I = (Sym.Hash / C) & (MaskWords - 1);
    uint64_t Val = readUint(Buf + I * Config->Wordsize);
    Val |= uint64_t(1) << (Sym.Hash % C);
    Val |= uint64_t(1) << ((Sym.Hash >> Shift2) % C);
    writeUint(Buf + I * Config->Wordsize, Val);
  }
}

void GnuHashTableSection::writeHashTable(uint8_t *Buf) {
  uint32_t *Buckets = reinterpret_cast<uint32_t *>(Buf);
  uint32_t OldBucket = -1;
  uint32_t *Values = Buckets + NBuckets;
  for (auto I = Symbols.begin(), E = Symbols.end(); I != E; ++I) {
    // Write a hash value. It represents a sequence of chains that share the
    // same hash modulo value. The last element of each chain is terminated by
    // LSB 1.
    uint32_t Hash = I->Hash;
    bool IsLastInChain = (I + 1) == E || I->BucketIdx != (I + 1)->BucketIdx;
    Hash = IsLastInChain ? Hash | 1 : Hash & ~1;
    write32(Values++, Hash);

    if (I->BucketIdx == OldBucket)
      continue;
    // Write a hash bucket. Hash buckets contain indices in the following hash
    // value table.
    write32(Buckets + I->BucketIdx, I->Sym->DynsymIndex);
    OldBucket = I->BucketIdx;
  }
}

static uint32_t hashGnu(StringRef Name) {
  uint32_t H = 5381;
  for (uint8_t C : Name)
    H = (H << 5) + H + C;
  return H;
}

// Add symbols to this symbol hash table. Note that this function
// destructively sort a given vector -- which is needed because
// GNU-style hash table places some sorting requirements.
void GnuHashTableSection::addSymbols(std::vector<SymbolTableEntry> &V) {
  // We cannot use 'auto' for Mid because GCC 6.1 cannot deduce
  // its type correctly.
  std::vector<SymbolTableEntry>::iterator Mid =
      std::stable_partition(V.begin(), V.end(), [](const SymbolTableEntry &S) {
        return !S.Sym->isDefined();
      });

  // We chose load factor 4 for the on-disk hash table. For each hash
  // collision, the dynamic linker will compare a uint32_t hash value.
  // Since the integer comparison is quite fast, we believe we can
  // make the load factor even larger. 4 is just a conservative choice.
  //
  // Note that we don't want to create a zero-sized hash table because
  // Android loader as of 2018 doesn't like a .gnu.hash containing such
  // table. If that's the case, we create a hash table with one unused
  // dummy slot.
  NBuckets = std::max<size_t>((V.end() - Mid) / 4, 1);

  if (Mid == V.end())
    return;

  for (SymbolTableEntry &Ent : llvm::make_range(Mid, V.end())) {
    Symbol *B = Ent.Sym;
    uint32_t Hash = hashGnu(B->getName());
    uint32_t BucketIdx = Hash % NBuckets;
    Symbols.push_back({B, Ent.StrTabOffset, Hash, BucketIdx});
  }

  std::stable_sort(
      Symbols.begin(), Symbols.end(),
      [](const Entry &L, const Entry &R) { return L.BucketIdx < R.BucketIdx; });

  V.erase(Mid, V.end());
  for (const Entry &Ent : Symbols)
    V.push_back({Ent.Sym, Ent.StrTabOffset});
}

HashTableSection::HashTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_HASH, 4, ".hash") {
  this->Entsize = 4;
}

void HashTableSection::finalizeContents() {
  if (OutputSection *Sec = In.DynSymTab->getParent())
    getParent()->Link = Sec->SectionIndex;

  unsigned NumEntries = 2;                       // nbucket and nchain.
  NumEntries += In.DynSymTab->getNumSymbols();   // The chain entries.

  // Create as many buckets as there are symbols.
  NumEntries += In.DynSymTab->getNumSymbols();
  this->Size = NumEntries * 4;
}

void HashTableSection::writeTo(uint8_t *Buf) {
  // See comment in GnuHashTableSection::writeTo.
  memset(Buf, 0, Size);

  unsigned NumSymbols = In.DynSymTab->getNumSymbols();

  uint32_t *P = reinterpret_cast<uint32_t *>(Buf);
  write32(P++, NumSymbols); // nbucket
  write32(P++, NumSymbols); // nchain

  uint32_t *Buckets = P;
  uint32_t *Chains = P + NumSymbols;

  for (const SymbolTableEntry &S : In.DynSymTab->getSymbols()) {
    Symbol *Sym = S.Sym;
    StringRef Name = Sym->getName();
    unsigned I = Sym->DynsymIndex;
    uint32_t Hash = hashSysV(Name) % NumSymbols;
    Chains[I] = Buckets[Hash];
    write32(Buckets + Hash, I);
  }
}

// On PowerPC64 the lazy symbol resolvers go into the `global linkage table`
// in the .glink section, rather then the typical .plt section.
PltSection::PltSection(bool IsIplt)
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 16,
                       Config->EMachine == EM_PPC64 ? ".glink" : ".plt"),
      HeaderSize(!IsIplt || Config->ZRetpolineplt ? Target->PltHeaderSize : 0),
      IsIplt(IsIplt) {
  // The PLT needs to be writable on SPARC as the dynamic linker will
  // modify the instructions in the PLT entries.
  if (Config->EMachine == EM_SPARCV9)
    this->Flags |= SHF_WRITE;
}

void PltSection::writeTo(uint8_t *Buf) {
  // At beginning of PLT or retpoline IPLT, we have code to call the dynamic
  // linker to resolve dynsyms at runtime. Write such code.
  if (HeaderSize > 0)
    Target->writePltHeader(Buf);
  size_t Off = HeaderSize;
  // The IPlt is immediately after the Plt, account for this in RelOff
  unsigned PltOff = getPltRelocOff();

  for (auto &I : Entries) {
    const Symbol *B = I.first;
    unsigned RelOff = I.second + PltOff;
    uint64_t Got = B->getGotPltVA();
    uint64_t Plt = this->getVA() + Off;
    Target->writePlt(Buf + Off, Got, Plt, B->PltIndex, RelOff);
    Off += Target->PltEntrySize;
  }
}

template <class ELFT> void PltSection::addEntry(Symbol &Sym) {
  Sym.PltIndex = Entries.size();
  RelocationBaseSection *PltRelocSection = In.RelaPlt;
  if (IsIplt) {
    PltRelocSection = In.RelaIplt;
    Sym.IsInIplt = true;
  }
  unsigned RelOff =
      static_cast<RelocationSection<ELFT> *>(PltRelocSection)->getRelocOffset();
  Entries.push_back(std::make_pair(&Sym, RelOff));
}

size_t PltSection::getSize() const {
  return HeaderSize + Entries.size() * Target->PltEntrySize;
}

// Some architectures such as additional symbols in the PLT section. For
// example ARM uses mapping symbols to aid disassembly
void PltSection::addSymbols() {
  // The PLT may have symbols defined for the Header, the IPLT has no header
  if (!IsIplt)
    Target->addPltHeaderSymbols(*this);
  size_t Off = HeaderSize;
  for (size_t I = 0; I < Entries.size(); ++I) {
    Target->addPltSymbols(*this, Off);
    Off += Target->PltEntrySize;
  }
}

unsigned PltSection::getPltRelocOff() const {
  return IsIplt ? In.Plt->getSize() : 0;
}

// The string hash function for .gdb_index.
static uint32_t computeGdbHash(StringRef S) {
  uint32_t H = 0;
  for (uint8_t C : S)
    H = H * 67 + toLower(C) - 113;
  return H;
}

GdbIndexSection::GdbIndexSection()
    : SyntheticSection(0, SHT_PROGBITS, 1, ".gdb_index") {}

// Returns the desired size of an on-disk hash table for a .gdb_index section.
// There's a tradeoff between size and collision rate. We aim 75% utilization.
size_t GdbIndexSection::computeSymtabSize() const {
  return std::max<size_t>(NextPowerOf2(Symbols.size() * 4 / 3), 1024);
}

// Compute the output section size.
void GdbIndexSection::initOutputSize() {
  Size = sizeof(GdbIndexHeader) + computeSymtabSize() * 8;

  for (GdbChunk &Chunk : Chunks)
    Size += Chunk.CompilationUnits.size() * 16 + Chunk.AddressAreas.size() * 20;

  // Add the constant pool size if exists.
  if (!Symbols.empty()) {
    GdbSymbol &Sym = Symbols.back();
    Size += Sym.NameOff + Sym.Name.size() + 1;
  }
}

static std::vector<InputSection *> getDebugInfoSections() {
  std::vector<InputSection *> Ret;
  for (InputSectionBase *S : InputSections)
    if (InputSection *IS = dyn_cast<InputSection>(S))
      if (IS->Name == ".debug_info")
        Ret.push_back(IS);
  return Ret;
}

static std::vector<GdbIndexSection::CuEntry> readCuList(DWARFContext &Dwarf) {
  std::vector<GdbIndexSection::CuEntry> Ret;
  for (std::unique_ptr<DWARFUnit> &Cu : Dwarf.compile_units())
    Ret.push_back({Cu->getOffset(), Cu->getLength() + 4});
  return Ret;
}

static std::vector<GdbIndexSection::AddressEntry>
readAddressAreas(DWARFContext &Dwarf, InputSection *Sec) {
  std::vector<GdbIndexSection::AddressEntry> Ret;

  uint32_t CuIdx = 0;
  for (std::unique_ptr<DWARFUnit> &Cu : Dwarf.compile_units()) {
    Expected<DWARFAddressRangesVector> Ranges = Cu->collectAddressRanges();
    if (!Ranges) {
      error(toString(Sec) + ": " + toString(Ranges.takeError()));
      return {};
    }

    ArrayRef<InputSectionBase *> Sections = Sec->File->getSections();
    for (DWARFAddressRange &R : *Ranges) {
      InputSectionBase *S = Sections[R.SectionIndex];
      if (!S || S == &InputSection::Discarded || !S->Live)
        continue;
      // Range list with zero size has no effect.
      if (R.LowPC == R.HighPC)
        continue;
      auto *IS = cast<InputSection>(S);
      uint64_t Offset = IS->getOffsetInFile();
      Ret.push_back({IS, R.LowPC - Offset, R.HighPC - Offset, CuIdx});
    }
    ++CuIdx;
  }

  return Ret;
}

template <class ELFT>
static std::vector<GdbIndexSection::NameAttrEntry>
readPubNamesAndTypes(const LLDDwarfObj<ELFT> &Obj,
                     const std::vector<GdbIndexSection::CuEntry> &CUs) {
  const DWARFSection &PubNames = Obj.getGnuPubNamesSection();
  const DWARFSection &PubTypes = Obj.getGnuPubTypesSection();

  std::vector<GdbIndexSection::NameAttrEntry> Ret;
  for (const DWARFSection *Pub : {&PubNames, &PubTypes}) {
    DWARFDebugPubTable Table(Obj, *Pub, Config->IsLE, true);
    for (const DWARFDebugPubTable::Set &Set : Table.getData()) {
      // The value written into the constant pool is Kind << 24 | CuIndex. As we
      // don't know how many compilation units precede this object to compute
      // CuIndex, we compute (Kind << 24 | CuIndexInThisObject) instead, and add
      // the number of preceding compilation units later.
      uint32_t I =
          lower_bound(CUs, Set.Offset,
                      [](GdbIndexSection::CuEntry CU, uint32_t Offset) {
                        return CU.CuOffset < Offset;
                      }) -
          CUs.begin();
      for (const DWARFDebugPubTable::Entry &Ent : Set.Entries)
        Ret.push_back({{Ent.Name, computeGdbHash(Ent.Name)},
                       (Ent.Descriptor.toBits() << 24) | I});
    }
  }
  return Ret;
}

// Create a list of symbols from a given list of symbol names and types
// by uniquifying them by name.
static std::vector<GdbIndexSection::GdbSymbol>
createSymbols(ArrayRef<std::vector<GdbIndexSection::NameAttrEntry>> NameAttrs,
              const std::vector<GdbIndexSection::GdbChunk> &Chunks) {
  typedef GdbIndexSection::GdbSymbol GdbSymbol;
  typedef GdbIndexSection::NameAttrEntry NameAttrEntry;

  // For each chunk, compute the number of compilation units preceding it.
  uint32_t CuIdx = 0;
  std::vector<uint32_t> CuIdxs(Chunks.size());
  for (uint32_t I = 0, E = Chunks.size(); I != E; ++I) {
    CuIdxs[I] = CuIdx;
    CuIdx += Chunks[I].CompilationUnits.size();
  }

  // The number of symbols we will handle in this function is of the order
  // of millions for very large executables, so we use multi-threading to
  // speed it up.
  size_t NumShards = 32;
  size_t Concurrency = 1;
  if (ThreadsEnabled)
    Concurrency =
        std::min<size_t>(PowerOf2Floor(hardware_concurrency()), NumShards);

  // A sharded map to uniquify symbols by name.
  std::vector<DenseMap<CachedHashStringRef, size_t>> Map(NumShards);
  size_t Shift = 32 - countTrailingZeros(NumShards);

  // Instantiate GdbSymbols while uniqufying them by name.
  std::vector<std::vector<GdbSymbol>> Symbols(NumShards);
  parallelForEachN(0, Concurrency, [&](size_t ThreadId) {
    uint32_t I = 0;
    for (ArrayRef<NameAttrEntry> Entries : NameAttrs) {
      for (const NameAttrEntry &Ent : Entries) {
        size_t ShardId = Ent.Name.hash() >> Shift;
        if ((ShardId & (Concurrency - 1)) != ThreadId)
          continue;

        uint32_t V = Ent.CuIndexAndAttrs + CuIdxs[I];
        size_t &Idx = Map[ShardId][Ent.Name];
        if (Idx) {
          Symbols[ShardId][Idx - 1].CuVector.push_back(V);
          continue;
        }

        Idx = Symbols[ShardId].size() + 1;
        Symbols[ShardId].push_back({Ent.Name, {V}, 0, 0});
      }
      ++I;
    }
  });

  size_t NumSymbols = 0;
  for (ArrayRef<GdbSymbol> V : Symbols)
    NumSymbols += V.size();

  // The return type is a flattened vector, so we'll copy each vector
  // contents to Ret.
  std::vector<GdbSymbol> Ret;
  Ret.reserve(NumSymbols);
  for (std::vector<GdbSymbol> &Vec : Symbols)
    for (GdbSymbol &Sym : Vec)
      Ret.push_back(std::move(Sym));

  // CU vectors and symbol names are adjacent in the output file.
  // We can compute their offsets in the output file now.
  size_t Off = 0;
  for (GdbSymbol &Sym : Ret) {
    Sym.CuVectorOff = Off;
    Off += (Sym.CuVector.size() + 1) * 4;
  }
  for (GdbSymbol &Sym : Ret) {
    Sym.NameOff = Off;
    Off += Sym.Name.size() + 1;
  }

  return Ret;
}

// Returns a newly-created .gdb_index section.
template <class ELFT> GdbIndexSection *GdbIndexSection::create() {
  std::vector<InputSection *> Sections = getDebugInfoSections();

  // .debug_gnu_pub{names,types} are useless in executables.
  // They are present in input object files solely for creating
  // a .gdb_index. So we can remove them from the output.
  for (InputSectionBase *S : InputSections)
    if (S->Name == ".debug_gnu_pubnames" || S->Name == ".debug_gnu_pubtypes")
      S->Live = false;

  std::vector<GdbChunk> Chunks(Sections.size());
  std::vector<std::vector<NameAttrEntry>> NameAttrs(Sections.size());

  parallelForEachN(0, Sections.size(), [&](size_t I) {
    ObjFile<ELFT> *File = Sections[I]->getFile<ELFT>();
    DWARFContext Dwarf(make_unique<LLDDwarfObj<ELFT>>(File));

    Chunks[I].Sec = Sections[I];
    Chunks[I].CompilationUnits = readCuList(Dwarf);
    Chunks[I].AddressAreas = readAddressAreas(Dwarf, Sections[I]);
    NameAttrs[I] = readPubNamesAndTypes<ELFT>(
        static_cast<const LLDDwarfObj<ELFT> &>(Dwarf.getDWARFObj()),
        Chunks[I].CompilationUnits);
  });

  auto *Ret = make<GdbIndexSection>();
  Ret->Chunks = std::move(Chunks);
  Ret->Symbols = createSymbols(NameAttrs, Ret->Chunks);
  Ret->initOutputSize();
  return Ret;
}

void GdbIndexSection::writeTo(uint8_t *Buf) {
  // Write the header.
  auto *Hdr = reinterpret_cast<GdbIndexHeader *>(Buf);
  uint8_t *Start = Buf;
  Hdr->Version = 7;
  Buf += sizeof(*Hdr);

  // Write the CU list.
  Hdr->CuListOff = Buf - Start;
  for (GdbChunk &Chunk : Chunks) {
    for (CuEntry &Cu : Chunk.CompilationUnits) {
      write64le(Buf, Chunk.Sec->OutSecOff + Cu.CuOffset);
      write64le(Buf + 8, Cu.CuLength);
      Buf += 16;
    }
  }

  // Write the address area.
  Hdr->CuTypesOff = Buf - Start;
  Hdr->AddressAreaOff = Buf - Start;
  uint32_t CuOff = 0;
  for (GdbChunk &Chunk : Chunks) {
    for (AddressEntry &E : Chunk.AddressAreas) {
      uint64_t BaseAddr = E.Section->getVA(0);
      write64le(Buf, BaseAddr + E.LowAddress);
      write64le(Buf + 8, BaseAddr + E.HighAddress);
      write32le(Buf + 16, E.CuIndex + CuOff);
      Buf += 20;
    }
    CuOff += Chunk.CompilationUnits.size();
  }

  // Write the on-disk open-addressing hash table containing symbols.
  Hdr->SymtabOff = Buf - Start;
  size_t SymtabSize = computeSymtabSize();
  uint32_t Mask = SymtabSize - 1;

  for (GdbSymbol &Sym : Symbols) {
    uint32_t H = Sym.Name.hash();
    uint32_t I = H & Mask;
    uint32_t Step = ((H * 17) & Mask) | 1;

    while (read32le(Buf + I * 8))
      I = (I + Step) & Mask;

    write32le(Buf + I * 8, Sym.NameOff);
    write32le(Buf + I * 8 + 4, Sym.CuVectorOff);
  }

  Buf += SymtabSize * 8;

  // Write the string pool.
  Hdr->ConstantPoolOff = Buf - Start;
  parallelForEach(Symbols, [&](GdbSymbol &Sym) {
    memcpy(Buf + Sym.NameOff, Sym.Name.data(), Sym.Name.size());
  });

  // Write the CU vectors.
  for (GdbSymbol &Sym : Symbols) {
    write32le(Buf, Sym.CuVector.size());
    Buf += 4;
    for (uint32_t Val : Sym.CuVector) {
      write32le(Buf, Val);
      Buf += 4;
    }
  }
}

bool GdbIndexSection::empty() const { return Chunks.empty(); }

EhFrameHeader::EhFrameHeader()
    : SyntheticSection(SHF_ALLOC, SHT_PROGBITS, 4, ".eh_frame_hdr") {}

// .eh_frame_hdr contains a binary search table of pointers to FDEs.
// Each entry of the search table consists of two values,
// the starting PC from where FDEs covers, and the FDE's address.
// It is sorted by PC.
void EhFrameHeader::writeTo(uint8_t *Buf) {
  typedef EhFrameSection::FdeData FdeData;

  std::vector<FdeData> Fdes = In.EhFrame->getFdeData();

  Buf[0] = 1;
  Buf[1] = DW_EH_PE_pcrel | DW_EH_PE_sdata4;
  Buf[2] = DW_EH_PE_udata4;
  Buf[3] = DW_EH_PE_datarel | DW_EH_PE_sdata4;
  write32(Buf + 4, In.EhFrame->getParent()->Addr - this->getVA() - 4);
  write32(Buf + 8, Fdes.size());
  Buf += 12;

  for (FdeData &Fde : Fdes) {
    write32(Buf, Fde.PcRel);
    write32(Buf + 4, Fde.FdeVARel);
    Buf += 8;
  }
}

size_t EhFrameHeader::getSize() const {
  // .eh_frame_hdr has a 12 bytes header followed by an array of FDEs.
  return 12 + In.EhFrame->NumFdes * 8;
}

bool EhFrameHeader::empty() const { return In.EhFrame->empty(); }

VersionDefinitionSection::VersionDefinitionSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_verdef, sizeof(uint32_t),
                       ".gnu.version_d") {}

static StringRef getFileDefName() {
  if (!Config->SoName.empty())
    return Config->SoName;
  return Config->OutputFile;
}

void VersionDefinitionSection::finalizeContents() {
  FileDefNameOff = In.DynStrTab->addString(getFileDefName());
  for (VersionDefinition &V : Config->VersionDefinitions)
    V.NameOff = In.DynStrTab->addString(V.Name);

  if (OutputSection *Sec = In.DynStrTab->getParent())
    getParent()->Link = Sec->SectionIndex;

  // sh_info should be set to the number of definitions. This fact is missed in
  // documentation, but confirmed by binutils community:
  // https://sourceware.org/ml/binutils/2014-11/msg00355.html
  getParent()->Info = getVerDefNum();
}

void VersionDefinitionSection::writeOne(uint8_t *Buf, uint32_t Index,
                                        StringRef Name, size_t NameOff) {
  uint16_t Flags = Index == 1 ? VER_FLG_BASE : 0;

  // Write a verdef.
  write16(Buf, 1);                  // vd_version
  write16(Buf + 2, Flags);          // vd_flags
  write16(Buf + 4, Index);          // vd_ndx
  write16(Buf + 6, 1);              // vd_cnt
  write32(Buf + 8, hashSysV(Name)); // vd_hash
  write32(Buf + 12, 20);            // vd_aux
  write32(Buf + 16, 28);            // vd_next

  // Write a veraux.
  write32(Buf + 20, NameOff); // vda_name
  write32(Buf + 24, 0);       // vda_next
}

void VersionDefinitionSection::writeTo(uint8_t *Buf) {
  writeOne(Buf, 1, getFileDefName(), FileDefNameOff);

  for (VersionDefinition &V : Config->VersionDefinitions) {
    Buf += EntrySize;
    writeOne(Buf, V.Id, V.Name, V.NameOff);
  }

  // Need to terminate the last version definition.
  write32(Buf + 16, 0); // vd_next
}

size_t VersionDefinitionSection::getSize() const {
  return EntrySize * getVerDefNum();
}

// .gnu.version is a table where each entry is 2 byte long.
template <class ELFT>
VersionTableSection<ELFT>::VersionTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_versym, sizeof(uint16_t),
                       ".gnu.version") {
  this->Entsize = 2;
}

template <class ELFT> void VersionTableSection<ELFT>::finalizeContents() {
  // At the moment of june 2016 GNU docs does not mention that sh_link field
  // should be set, but Sun docs do. Also readelf relies on this field.
  getParent()->Link = In.DynSymTab->getParent()->SectionIndex;
}

template <class ELFT> size_t VersionTableSection<ELFT>::getSize() const {
  return (In.DynSymTab->getSymbols().size() + 1) * 2;
}

template <class ELFT> void VersionTableSection<ELFT>::writeTo(uint8_t *Buf) {
  Buf += 2;
  for (const SymbolTableEntry &S : In.DynSymTab->getSymbols()) {
    write16(Buf, S.Sym->VersionId);
    Buf += 2;
  }
}

template <class ELFT> bool VersionTableSection<ELFT>::empty() const {
  return !In.VerDef && InX<ELFT>::VerNeed->empty();
}

template <class ELFT>
VersionNeedSection<ELFT>::VersionNeedSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_verneed, sizeof(uint32_t),
                       ".gnu.version_r") {
  // Identifiers in verneed section start at 2 because 0 and 1 are reserved
  // for VER_NDX_LOCAL and VER_NDX_GLOBAL.
  // First identifiers are reserved by verdef section if it exist.
  NextIndex = getVerDefNum() + 1;
}

template <class ELFT> void VersionNeedSection<ELFT>::addSymbol(Symbol *SS) {
  auto &File = cast<SharedFile<ELFT>>(*SS->File);
  if (SS->VerdefIndex == VER_NDX_GLOBAL) {
    SS->VersionId = VER_NDX_GLOBAL;
    return;
  }

  // If we don't already know that we need an Elf_Verneed for this DSO, prepare
  // to create one by adding it to our needed list and creating a dynstr entry
  // for the soname.
  if (File.VerdefMap.empty())
    Needed.push_back({&File, In.DynStrTab->addString(File.SoName)});
  const typename ELFT::Verdef *Ver = File.Verdefs[SS->VerdefIndex];
  typename SharedFile<ELFT>::NeededVer &NV = File.VerdefMap[Ver];

  // If we don't already know that we need an Elf_Vernaux for this Elf_Verdef,
  // prepare to create one by allocating a version identifier and creating a
  // dynstr entry for the version name.
  if (NV.Index == 0) {
    NV.StrTab = In.DynStrTab->addString(File.getStringTable().data() +
                                        Ver->getAux()->vda_name);
    NV.Index = NextIndex++;
  }
  SS->VersionId = NV.Index;
}

template <class ELFT> void VersionNeedSection<ELFT>::writeTo(uint8_t *Buf) {
  // The Elf_Verneeds need to appear first, followed by the Elf_Vernauxs.
  auto *Verneed = reinterpret_cast<Elf_Verneed *>(Buf);
  auto *Vernaux = reinterpret_cast<Elf_Vernaux *>(Verneed + Needed.size());

  for (std::pair<SharedFile<ELFT> *, size_t> &P : Needed) {
    // Create an Elf_Verneed for this DSO.
    Verneed->vn_version = 1;
    Verneed->vn_cnt = P.first->VerdefMap.size();
    Verneed->vn_file = P.second;
    Verneed->vn_aux =
        reinterpret_cast<char *>(Vernaux) - reinterpret_cast<char *>(Verneed);
    Verneed->vn_next = sizeof(Elf_Verneed);
    ++Verneed;

    // Create the Elf_Vernauxs for this Elf_Verneed. The loop iterates over
    // VerdefMap, which will only contain references to needed version
    // definitions. Each Elf_Vernaux is based on the information contained in
    // the Elf_Verdef in the source DSO. This loop iterates over a std::map of
    // pointers, but is deterministic because the pointers refer to Elf_Verdef
    // data structures within a single input file.
    for (auto &NV : P.first->VerdefMap) {
      Vernaux->vna_hash = NV.first->vd_hash;
      Vernaux->vna_flags = 0;
      Vernaux->vna_other = NV.second.Index;
      Vernaux->vna_name = NV.second.StrTab;
      Vernaux->vna_next = sizeof(Elf_Vernaux);
      ++Vernaux;
    }

    Vernaux[-1].vna_next = 0;
  }
  Verneed[-1].vn_next = 0;
}

template <class ELFT> void VersionNeedSection<ELFT>::finalizeContents() {
  if (OutputSection *Sec = In.DynStrTab->getParent())
    getParent()->Link = Sec->SectionIndex;
  getParent()->Info = Needed.size();
}

template <class ELFT> size_t VersionNeedSection<ELFT>::getSize() const {
  unsigned Size = Needed.size() * sizeof(Elf_Verneed);
  for (const std::pair<SharedFile<ELFT> *, size_t> &P : Needed)
    Size += P.first->VerdefMap.size() * sizeof(Elf_Vernaux);
  return Size;
}

template <class ELFT> bool VersionNeedSection<ELFT>::empty() const {
  return getNeedNum() == 0;
}

void MergeSyntheticSection::addSection(MergeInputSection *MS) {
  MS->Parent = this;
  Sections.push_back(MS);
}

MergeTailSection::MergeTailSection(StringRef Name, uint32_t Type,
                                   uint64_t Flags, uint32_t Alignment)
    : MergeSyntheticSection(Name, Type, Flags, Alignment),
      Builder(StringTableBuilder::RAW, Alignment) {}

size_t MergeTailSection::getSize() const { return Builder.getSize(); }

void MergeTailSection::writeTo(uint8_t *Buf) { Builder.write(Buf); }

void MergeTailSection::finalizeContents() {
  // Add all string pieces to the string table builder to create section
  // contents.
  for (MergeInputSection *Sec : Sections)
    for (size_t I = 0, E = Sec->Pieces.size(); I != E; ++I)
      if (Sec->Pieces[I].Live)
        Builder.add(Sec->getData(I));

  // Fix the string table content. After this, the contents will never change.
  Builder.finalize();

  // finalize() fixed tail-optimized strings, so we can now get
  // offsets of strings. Get an offset for each string and save it
  // to a corresponding StringPiece for easy access.
  for (MergeInputSection *Sec : Sections)
    for (size_t I = 0, E = Sec->Pieces.size(); I != E; ++I)
      if (Sec->Pieces[I].Live)
        Sec->Pieces[I].OutputOff = Builder.getOffset(Sec->getData(I));
}

void MergeNoTailSection::writeTo(uint8_t *Buf) {
  for (size_t I = 0; I < NumShards; ++I)
    Shards[I].write(Buf + ShardOffsets[I]);
}

// This function is very hot (i.e. it can take several seconds to finish)
// because sometimes the number of inputs is in an order of magnitude of
// millions. So, we use multi-threading.
//
// For any strings S and T, we know S is not mergeable with T if S's hash
// value is different from T's. If that's the case, we can safely put S and
// T into different string builders without worrying about merge misses.
// We do it in parallel.
void MergeNoTailSection::finalizeContents() {
  // Initializes string table builders.
  for (size_t I = 0; I < NumShards; ++I)
    Shards.emplace_back(StringTableBuilder::RAW, Alignment);

  // Concurrency level. Must be a power of 2 to avoid expensive modulo
  // operations in the following tight loop.
  size_t Concurrency = 1;
  if (ThreadsEnabled)
    Concurrency =
        std::min<size_t>(PowerOf2Floor(hardware_concurrency()), NumShards);

  // Add section pieces to the builders.
  parallelForEachN(0, Concurrency, [&](size_t ThreadId) {
    for (MergeInputSection *Sec : Sections) {
      for (size_t I = 0, E = Sec->Pieces.size(); I != E; ++I) {
        size_t ShardId = getShardId(Sec->Pieces[I].Hash);
        if ((ShardId & (Concurrency - 1)) == ThreadId && Sec->Pieces[I].Live)
          Sec->Pieces[I].OutputOff = Shards[ShardId].add(Sec->getData(I));
      }
    }
  });

  // Compute an in-section offset for each shard.
  size_t Off = 0;
  for (size_t I = 0; I < NumShards; ++I) {
    Shards[I].finalizeInOrder();
    if (Shards[I].getSize() > 0)
      Off = alignTo(Off, Alignment);
    ShardOffsets[I] = Off;
    Off += Shards[I].getSize();
  }
  Size = Off;

  // So far, section pieces have offsets from beginning of shards, but
  // we want offsets from beginning of the whole section. Fix them.
  parallelForEach(Sections, [&](MergeInputSection *Sec) {
    for (size_t I = 0, E = Sec->Pieces.size(); I != E; ++I)
      if (Sec->Pieces[I].Live)
        Sec->Pieces[I].OutputOff +=
            ShardOffsets[getShardId(Sec->Pieces[I].Hash)];
  });
}

static MergeSyntheticSection *createMergeSynthetic(StringRef Name,
                                                   uint32_t Type,
                                                   uint64_t Flags,
                                                   uint32_t Alignment) {
  bool ShouldTailMerge = (Flags & SHF_STRINGS) && Config->Optimize >= 2;
  if (ShouldTailMerge)
    return make<MergeTailSection>(Name, Type, Flags, Alignment);
  return make<MergeNoTailSection>(Name, Type, Flags, Alignment);
}

template <class ELFT> void elf::splitSections() {
  // splitIntoPieces needs to be called on each MergeInputSection
  // before calling finalizeContents().
  parallelForEach(InputSections, [](InputSectionBase *Sec) {
    if (auto *S = dyn_cast<MergeInputSection>(Sec))
      S->splitIntoPieces();
    else if (auto *Eh = dyn_cast<EhInputSection>(Sec))
      Eh->split<ELFT>();
  });
}

// This function scans over the inputsections to create mergeable
// synthetic sections.
//
// It removes MergeInputSections from the input section array and adds
// new synthetic sections at the location of the first input section
// that it replaces. It then finalizes each synthetic section in order
// to compute an output offset for each piece of each input section.
void elf::mergeSections() {
  std::vector<MergeSyntheticSection *> MergeSections;
  for (InputSectionBase *&S : InputSections) {
    MergeInputSection *MS = dyn_cast<MergeInputSection>(S);
    if (!MS)
      continue;

    // We do not want to handle sections that are not alive, so just remove
    // them instead of trying to merge.
    if (!MS->Live) {
      S = nullptr;
      continue;
    }

    StringRef OutsecName = getOutputSectionName(MS);
    uint32_t Alignment = std::max<uint32_t>(MS->Alignment, MS->Entsize);

    auto I = llvm::find_if(MergeSections, [=](MergeSyntheticSection *Sec) {
      // While we could create a single synthetic section for two different
      // values of Entsize, it is better to take Entsize into consideration.
      //
      // With a single synthetic section no two pieces with different Entsize
      // could be equal, so we may as well have two sections.
      //
      // Using Entsize in here also allows us to propagate it to the synthetic
      // section.
      return Sec->Name == OutsecName && Sec->Flags == MS->Flags &&
             Sec->Entsize == MS->Entsize && Sec->Alignment == Alignment;
    });
    if (I == MergeSections.end()) {
      MergeSyntheticSection *Syn =
          createMergeSynthetic(OutsecName, MS->Type, MS->Flags, Alignment);
      MergeSections.push_back(Syn);
      I = std::prev(MergeSections.end());
      S = Syn;
      Syn->Entsize = MS->Entsize;
    } else {
      S = nullptr;
    }
    (*I)->addSection(MS);
  }
  for (auto *MS : MergeSections)
    MS->finalizeContents();

  std::vector<InputSectionBase *> &V = InputSections;
  V.erase(std::remove(V.begin(), V.end(), nullptr), V.end());
}

MipsRldMapSection::MipsRldMapSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS, Config->Wordsize,
                       ".rld_map") {}

ARMExidxSentinelSection::ARMExidxSentinelSection()
    : SyntheticSection(SHF_ALLOC | SHF_LINK_ORDER, SHT_ARM_EXIDX,
                       Config->Wordsize, ".ARM.exidx") {}

// Write a terminating sentinel entry to the end of the .ARM.exidx table.
// This section will have been sorted last in the .ARM.exidx table.
// This table entry will have the form:
// | PREL31 upper bound of code that has exception tables | EXIDX_CANTUNWIND |
// The sentinel must have the PREL31 value of an address higher than any
// address described by any other table entry.
void ARMExidxSentinelSection::writeTo(uint8_t *Buf) {
  assert(Highest);
  uint64_t S = Highest->getVA(Highest->getSize());
  uint64_t P = getVA();
  Target->relocateOne(Buf, R_ARM_PREL31, S - P);
  write32le(Buf + 4, 1);
}

// The sentinel has to be removed if there are no other .ARM.exidx entries.
bool ARMExidxSentinelSection::empty() const {
  for (InputSection *IS : getInputSections(getParent()))
    if (!isa<ARMExidxSentinelSection>(IS))
      return false;
  return true;
}

bool ARMExidxSentinelSection::classof(const SectionBase *D) {
  return D->kind() == InputSectionBase::Synthetic && D->Type == SHT_ARM_EXIDX;
}

ThunkSection::ThunkSection(OutputSection *OS, uint64_t Off)
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS,
                       Config->Wordsize, ".text.thunk") {
  this->Parent = OS;
  this->OutSecOff = Off;
}

void ThunkSection::addThunk(Thunk *T) {
  Thunks.push_back(T);
  T->addSymbols(*this);
}

void ThunkSection::writeTo(uint8_t *Buf) {
  for (Thunk *T : Thunks)
    T->writeTo(Buf + T->Offset);
}

InputSection *ThunkSection::getTargetInputSection() const {
  if (Thunks.empty())
    return nullptr;
  const Thunk *T = Thunks.front();
  return T->getTargetInputSection();
}

bool ThunkSection::assignOffsets() {
  uint64_t Off = 0;
  for (Thunk *T : Thunks) {
    Off = alignTo(Off, T->Alignment);
    T->setOffset(Off);
    uint32_t Size = T->size();
    T->getThunkTargetSym()->Size = Size;
    Off += Size;
  }
  bool Changed = Off != Size;
  Size = Off;
  return Changed;
}

// If linking position-dependent code then the table will store the addresses
// directly in the binary so the section has type SHT_PROGBITS. If linking
// position-independent code the section has type SHT_NOBITS since it will be
// allocated and filled in by the dynamic linker.
PPC64LongBranchTargetSection::PPC64LongBranchTargetSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE,
                       Config->Pic ? SHT_NOBITS : SHT_PROGBITS, 8,
                       ".branch_lt") {}

void PPC64LongBranchTargetSection::addEntry(Symbol &Sym) {
  assert(Sym.PPC64BranchltIndex == 0xffff);
  Sym.PPC64BranchltIndex = Entries.size();
  Entries.push_back(&Sym);
}

size_t PPC64LongBranchTargetSection::getSize() const {
  return Entries.size() * 8;
}

void PPC64LongBranchTargetSection::writeTo(uint8_t *Buf) {
  assert(Target->GotPltEntrySize == 8);
  // If linking non-pic we have the final addresses of the targets and they get
  // written to the table directly. For pic the dynamic linker will allocate
  // the section and fill it it.
  if (Config->Pic)
    return;

  for (const Symbol *Sym : Entries) {
    assert(Sym->getVA());
    // Need calls to branch to the local entry-point since a long-branch
    // must be a local-call.
    write64(Buf,
            Sym->getVA() + getPPC64GlobalEntryToLocalEntryOffset(Sym->StOther));
    Buf += Target->GotPltEntrySize;
  }
}

bool PPC64LongBranchTargetSection::empty() const {
  // `removeUnusedSyntheticSections()` is called before thunk allocation which
  // is too early to determine if this section will be empty or not. We need
  // Finalized to keep the section alive until after thunk creation. Finalized
  // only gets set to true once `finalizeSections()` is called after thunk
  // creation. Becuase of this, if we don't create any long-branch thunks we end
  // up with an empty .branch_lt section in the binary.
  return Finalized && Entries.empty();
}

InStruct elf::In;

template GdbIndexSection *GdbIndexSection::create<ELF32LE>();
template GdbIndexSection *GdbIndexSection::create<ELF32BE>();
template GdbIndexSection *GdbIndexSection::create<ELF64LE>();
template GdbIndexSection *GdbIndexSection::create<ELF64BE>();

template void elf::splitSections<ELF32LE>();
template void elf::splitSections<ELF32BE>();
template void elf::splitSections<ELF64LE>();
template void elf::splitSections<ELF64BE>();

template void EhFrameSection::addSection<ELF32LE>(InputSectionBase *);
template void EhFrameSection::addSection<ELF32BE>(InputSectionBase *);
template void EhFrameSection::addSection<ELF64LE>(InputSectionBase *);
template void EhFrameSection::addSection<ELF64BE>(InputSectionBase *);

template void PltSection::addEntry<ELF32LE>(Symbol &Sym);
template void PltSection::addEntry<ELF32BE>(Symbol &Sym);
template void PltSection::addEntry<ELF64LE>(Symbol &Sym);
template void PltSection::addEntry<ELF64BE>(Symbol &Sym);

template void MipsGotSection::build<ELF32LE>();
template void MipsGotSection::build<ELF32BE>();
template void MipsGotSection::build<ELF64LE>();
template void MipsGotSection::build<ELF64BE>();

template class elf::MipsAbiFlagsSection<ELF32LE>;
template class elf::MipsAbiFlagsSection<ELF32BE>;
template class elf::MipsAbiFlagsSection<ELF64LE>;
template class elf::MipsAbiFlagsSection<ELF64BE>;

template class elf::MipsOptionsSection<ELF32LE>;
template class elf::MipsOptionsSection<ELF32BE>;
template class elf::MipsOptionsSection<ELF64LE>;
template class elf::MipsOptionsSection<ELF64BE>;

template class elf::MipsReginfoSection<ELF32LE>;
template class elf::MipsReginfoSection<ELF32BE>;
template class elf::MipsReginfoSection<ELF64LE>;
template class elf::MipsReginfoSection<ELF64BE>;

template class elf::DynamicSection<ELF32LE>;
template class elf::DynamicSection<ELF32BE>;
template class elf::DynamicSection<ELF64LE>;
template class elf::DynamicSection<ELF64BE>;

template class elf::RelocationSection<ELF32LE>;
template class elf::RelocationSection<ELF32BE>;
template class elf::RelocationSection<ELF64LE>;
template class elf::RelocationSection<ELF64BE>;

template class elf::AndroidPackedRelocationSection<ELF32LE>;
template class elf::AndroidPackedRelocationSection<ELF32BE>;
template class elf::AndroidPackedRelocationSection<ELF64LE>;
template class elf::AndroidPackedRelocationSection<ELF64BE>;

template class elf::RelrSection<ELF32LE>;
template class elf::RelrSection<ELF32BE>;
template class elf::RelrSection<ELF64LE>;
template class elf::RelrSection<ELF64BE>;

template class elf::SymbolTableSection<ELF32LE>;
template class elf::SymbolTableSection<ELF32BE>;
template class elf::SymbolTableSection<ELF64LE>;
template class elf::SymbolTableSection<ELF64BE>;

template class elf::VersionTableSection<ELF32LE>;
template class elf::VersionTableSection<ELF32BE>;
template class elf::VersionTableSection<ELF64LE>;
template class elf::VersionTableSection<ELF64BE>;

template class elf::VersionNeedSection<ELF32LE>;
template class elf::VersionNeedSection<ELF32BE>;
template class elf::VersionNeedSection<ELF64LE>;
template class elf::VersionNeedSection<ELF64BE>;
