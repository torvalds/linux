//===- lib/MC/MCContext.cpp - Machine Code Context ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCContext.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCLabel.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCSymbolMachO.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdlib>
#include <tuple>
#include <utility>

using namespace llvm;

static cl::opt<char*>
AsSecureLogFileName("as-secure-log-file-name",
        cl::desc("As secure log file name (initialized from "
                 "AS_SECURE_LOG_FILE env variable)"),
        cl::init(getenv("AS_SECURE_LOG_FILE")), cl::Hidden);

MCContext::MCContext(const MCAsmInfo *mai, const MCRegisterInfo *mri,
                     const MCObjectFileInfo *mofi, const SourceMgr *mgr,
                     bool DoAutoReset)
    : SrcMgr(mgr), InlineSrcMgr(nullptr), MAI(mai), MRI(mri), MOFI(mofi),
      Symbols(Allocator), UsedNames(Allocator),
      CurrentDwarfLoc(0, 0, 0, DWARF2_FLAG_IS_STMT, 0, 0),
      AutoReset(DoAutoReset) {
  SecureLogFile = AsSecureLogFileName;

  if (SrcMgr && SrcMgr->getNumBuffers())
    MainFileName =
        SrcMgr->getMemoryBuffer(SrcMgr->getMainFileID())->getBufferIdentifier();
}

MCContext::~MCContext() {
  if (AutoReset)
    reset();

  // NOTE: The symbols are all allocated out of a bump pointer allocator,
  // we don't need to free them here.
}

//===----------------------------------------------------------------------===//
// Module Lifetime Management
//===----------------------------------------------------------------------===//

void MCContext::reset() {
  // Call the destructors so the fragments are freed
  COFFAllocator.DestroyAll();
  ELFAllocator.DestroyAll();
  MachOAllocator.DestroyAll();

  MCSubtargetAllocator.DestroyAll();
  UsedNames.clear();
  Symbols.clear();
  Allocator.Reset();
  Instances.clear();
  CompilationDir.clear();
  MainFileName.clear();
  MCDwarfLineTablesCUMap.clear();
  SectionsForRanges.clear();
  MCGenDwarfLabelEntries.clear();
  DwarfDebugFlags = StringRef();
  DwarfCompileUnitID = 0;
  CurrentDwarfLoc = MCDwarfLoc(0, 0, 0, DWARF2_FLAG_IS_STMT, 0, 0);

  CVContext.reset();

  MachOUniquingMap.clear();
  ELFUniquingMap.clear();
  COFFUniquingMap.clear();
  WasmUniquingMap.clear();

  NextID.clear();
  AllowTemporaryLabels = true;
  DwarfLocSeen = false;
  GenDwarfForAssembly = false;
  GenDwarfFileNumber = 0;

  HadError = false;
}

//===----------------------------------------------------------------------===//
// Symbol Manipulation
//===----------------------------------------------------------------------===//

MCSymbol *MCContext::getOrCreateSymbol(const Twine &Name) {
  SmallString<128> NameSV;
  StringRef NameRef = Name.toStringRef(NameSV);

  assert(!NameRef.empty() && "Normal symbols cannot be unnamed!");

  MCSymbol *&Sym = Symbols[NameRef];
  if (!Sym)
    Sym = createSymbol(NameRef, false, false);

  return Sym;
}

MCSymbol *MCContext::getOrCreateFrameAllocSymbol(StringRef FuncName,
                                                 unsigned Idx) {
  return getOrCreateSymbol(Twine(MAI->getPrivateGlobalPrefix()) + FuncName +
                           "$frame_escape_" + Twine(Idx));
}

MCSymbol *MCContext::getOrCreateParentFrameOffsetSymbol(StringRef FuncName) {
  return getOrCreateSymbol(Twine(MAI->getPrivateGlobalPrefix()) + FuncName +
                           "$parent_frame_offset");
}

MCSymbol *MCContext::getOrCreateLSDASymbol(StringRef FuncName) {
  return getOrCreateSymbol(Twine(MAI->getPrivateGlobalPrefix()) + "__ehtable$" +
                           FuncName);
}

MCSymbol *MCContext::createSymbolImpl(const StringMapEntry<bool> *Name,
                                      bool IsTemporary) {
  if (MOFI) {
    switch (MOFI->getObjectFileType()) {
    case MCObjectFileInfo::IsCOFF:
      return new (Name, *this) MCSymbolCOFF(Name, IsTemporary);
    case MCObjectFileInfo::IsELF:
      return new (Name, *this) MCSymbolELF(Name, IsTemporary);
    case MCObjectFileInfo::IsMachO:
      return new (Name, *this) MCSymbolMachO(Name, IsTemporary);
    case MCObjectFileInfo::IsWasm:
      return new (Name, *this) MCSymbolWasm(Name, IsTemporary);
    }
  }
  return new (Name, *this) MCSymbol(MCSymbol::SymbolKindUnset, Name,
                                    IsTemporary);
}

MCSymbol *MCContext::createSymbol(StringRef Name, bool AlwaysAddSuffix,
                                  bool CanBeUnnamed) {
  if (CanBeUnnamed && !UseNamesOnTempLabels)
    return createSymbolImpl(nullptr, true);

  // Determine whether this is a user written assembler temporary or normal
  // label, if used.
  bool IsTemporary = CanBeUnnamed;
  if (AllowTemporaryLabels && !IsTemporary)
    IsTemporary = Name.startswith(MAI->getPrivateGlobalPrefix());

  SmallString<128> NewName = Name;
  bool AddSuffix = AlwaysAddSuffix;
  unsigned &NextUniqueID = NextID[Name];
  while (true) {
    if (AddSuffix) {
      NewName.resize(Name.size());
      raw_svector_ostream(NewName) << NextUniqueID++;
    }
    auto NameEntry = UsedNames.insert(std::make_pair(NewName, true));
    if (NameEntry.second || !NameEntry.first->second) {
      // Ok, we found a name.
      // Mark it as used for a non-section symbol.
      NameEntry.first->second = true;
      // Have the MCSymbol object itself refer to the copy of the string that is
      // embedded in the UsedNames entry.
      return createSymbolImpl(&*NameEntry.first, IsTemporary);
    }
    assert(IsTemporary && "Cannot rename non-temporary symbols");
    AddSuffix = true;
  }
  llvm_unreachable("Infinite loop");
}

MCSymbol *MCContext::createTempSymbol(const Twine &Name, bool AlwaysAddSuffix,
                                      bool CanBeUnnamed) {
  SmallString<128> NameSV;
  raw_svector_ostream(NameSV) << MAI->getPrivateGlobalPrefix() << Name;
  return createSymbol(NameSV, AlwaysAddSuffix, CanBeUnnamed);
}

MCSymbol *MCContext::createLinkerPrivateTempSymbol() {
  SmallString<128> NameSV;
  raw_svector_ostream(NameSV) << MAI->getLinkerPrivateGlobalPrefix() << "tmp";
  return createSymbol(NameSV, true, false);
}

MCSymbol *MCContext::createTempSymbol(bool CanBeUnnamed) {
  return createTempSymbol("tmp", true, CanBeUnnamed);
}

unsigned MCContext::NextInstance(unsigned LocalLabelVal) {
  MCLabel *&Label = Instances[LocalLabelVal];
  if (!Label)
    Label = new (*this) MCLabel(0);
  return Label->incInstance();
}

unsigned MCContext::GetInstance(unsigned LocalLabelVal) {
  MCLabel *&Label = Instances[LocalLabelVal];
  if (!Label)
    Label = new (*this) MCLabel(0);
  return Label->getInstance();
}

MCSymbol *MCContext::getOrCreateDirectionalLocalSymbol(unsigned LocalLabelVal,
                                                       unsigned Instance) {
  MCSymbol *&Sym = LocalSymbols[std::make_pair(LocalLabelVal, Instance)];
  if (!Sym)
    Sym = createTempSymbol(false);
  return Sym;
}

MCSymbol *MCContext::createDirectionalLocalSymbol(unsigned LocalLabelVal) {
  unsigned Instance = NextInstance(LocalLabelVal);
  return getOrCreateDirectionalLocalSymbol(LocalLabelVal, Instance);
}

MCSymbol *MCContext::getDirectionalLocalSymbol(unsigned LocalLabelVal,
                                               bool Before) {
  unsigned Instance = GetInstance(LocalLabelVal);
  if (!Before)
    ++Instance;
  return getOrCreateDirectionalLocalSymbol(LocalLabelVal, Instance);
}

MCSymbol *MCContext::lookupSymbol(const Twine &Name) const {
  SmallString<128> NameSV;
  StringRef NameRef = Name.toStringRef(NameSV);
  return Symbols.lookup(NameRef);
}

void MCContext::setSymbolValue(MCStreamer &Streamer,
                              StringRef Sym,
                              uint64_t Val) {
  auto Symbol = getOrCreateSymbol(Sym);
  Streamer.EmitAssignment(Symbol, MCConstantExpr::create(Val, *this));
}

//===----------------------------------------------------------------------===//
// Section Management
//===----------------------------------------------------------------------===//

MCSectionMachO *MCContext::getMachOSection(StringRef Segment, StringRef Section,
                                           unsigned TypeAndAttributes,
                                           unsigned Reserved2, SectionKind Kind,
                                           const char *BeginSymName) {
  // We unique sections by their segment/section pair.  The returned section
  // may not have the same flags as the requested section, if so this should be
  // diagnosed by the client as an error.

  // Form the name to look up.
  SmallString<64> Name;
  Name += Segment;
  Name.push_back(',');
  Name += Section;

  // Do the lookup, if we have a hit, return it.
  MCSectionMachO *&Entry = MachOUniquingMap[Name];
  if (Entry)
    return Entry;

  MCSymbol *Begin = nullptr;
  if (BeginSymName)
    Begin = createTempSymbol(BeginSymName, false);

  // Otherwise, return a new section.
  return Entry = new (MachOAllocator.Allocate()) MCSectionMachO(
             Segment, Section, TypeAndAttributes, Reserved2, Kind, Begin);
}

void MCContext::renameELFSection(MCSectionELF *Section, StringRef Name) {
  StringRef GroupName;
  if (const MCSymbol *Group = Section->getGroup())
    GroupName = Group->getName();

  unsigned UniqueID = Section->getUniqueID();
  ELFUniquingMap.erase(
      ELFSectionKey{Section->getSectionName(), GroupName, UniqueID});
  auto I = ELFUniquingMap.insert(std::make_pair(
                                     ELFSectionKey{Name, GroupName, UniqueID},
                                     Section))
               .first;
  StringRef CachedName = I->first.SectionName;
  const_cast<MCSectionELF *>(Section)->setSectionName(CachedName);
}

MCSectionELF *MCContext::createELFSectionImpl(StringRef Section, unsigned Type,
                                              unsigned Flags, SectionKind K,
                                              unsigned EntrySize,
                                              const MCSymbolELF *Group,
                                              unsigned UniqueID,
                                              const MCSymbolELF *Associated) {
  MCSymbolELF *R;
  MCSymbol *&Sym = Symbols[Section];
  // A section symbol can not redefine regular symbols. There may be multiple
  // sections with the same name, in which case the first such section wins.
  if (Sym && Sym->isDefined() &&
      (!Sym->isInSection() || Sym->getSection().getBeginSymbol() != Sym))
    reportError(SMLoc(), "invalid symbol redefinition");
  if (Sym && Sym->isUndefined()) {
    R = cast<MCSymbolELF>(Sym);
  } else {
    auto NameIter = UsedNames.insert(std::make_pair(Section, false)).first;
    R = new (&*NameIter, *this) MCSymbolELF(&*NameIter, /*isTemporary*/ false);
    if (!Sym)
      Sym = R;
  }
  R->setBinding(ELF::STB_LOCAL);
  R->setType(ELF::STT_SECTION);

  auto *Ret = new (ELFAllocator.Allocate()) MCSectionELF(
      Section, Type, Flags, K, EntrySize, Group, UniqueID, R, Associated);

  auto *F = new MCDataFragment();
  Ret->getFragmentList().insert(Ret->begin(), F);
  F->setParent(Ret);
  R->setFragment(F);

  return Ret;
}

MCSectionELF *MCContext::createELFRelSection(const Twine &Name, unsigned Type,
                                             unsigned Flags, unsigned EntrySize,
                                             const MCSymbolELF *Group,
                                             const MCSectionELF *RelInfoSection) {
  StringMap<bool>::iterator I;
  bool Inserted;
  std::tie(I, Inserted) =
      RelSecNames.insert(std::make_pair(Name.str(), true));

  return createELFSectionImpl(
      I->getKey(), Type, Flags, SectionKind::getReadOnly(), EntrySize, Group,
      true, cast<MCSymbolELF>(RelInfoSection->getBeginSymbol()));
}

MCSectionELF *MCContext::getELFNamedSection(const Twine &Prefix,
                                            const Twine &Suffix, unsigned Type,
                                            unsigned Flags,
                                            unsigned EntrySize) {
  return getELFSection(Prefix + "." + Suffix, Type, Flags, EntrySize, Suffix);
}

MCSectionELF *MCContext::getELFSection(const Twine &Section, unsigned Type,
                                       unsigned Flags, unsigned EntrySize,
                                       const Twine &Group, unsigned UniqueID,
                                       const MCSymbolELF *Associated) {
  MCSymbolELF *GroupSym = nullptr;
  if (!Group.isTriviallyEmpty() && !Group.str().empty())
    GroupSym = cast<MCSymbolELF>(getOrCreateSymbol(Group));

  return getELFSection(Section, Type, Flags, EntrySize, GroupSym, UniqueID,
                       Associated);
}

MCSectionELF *MCContext::getELFSection(const Twine &Section, unsigned Type,
                                       unsigned Flags, unsigned EntrySize,
                                       const MCSymbolELF *GroupSym,
                                       unsigned UniqueID,
                                       const MCSymbolELF *Associated) {
  StringRef Group = "";
  if (GroupSym)
    Group = GroupSym->getName();
  // Do the lookup, if we have a hit, return it.
  auto IterBool = ELFUniquingMap.insert(
      std::make_pair(ELFSectionKey{Section.str(), Group, UniqueID}, nullptr));
  auto &Entry = *IterBool.first;
  if (!IterBool.second)
    return Entry.second;

  StringRef CachedName = Entry.first.SectionName;

  SectionKind Kind;
  if (Flags & ELF::SHF_ARM_PURECODE)
    Kind = SectionKind::getExecuteOnly();
  else if (Flags & ELF::SHF_EXECINSTR)
    Kind = SectionKind::getText();
  else
    Kind = SectionKind::getReadOnly();

  MCSectionELF *Result = createELFSectionImpl(
      CachedName, Type, Flags, Kind, EntrySize, GroupSym, UniqueID, Associated);
  Entry.second = Result;
  return Result;
}

MCSectionELF *MCContext::createELFGroupSection(const MCSymbolELF *Group) {
  return createELFSectionImpl(".group", ELF::SHT_GROUP, 0,
                              SectionKind::getReadOnly(), 4, Group, ~0,
                              nullptr);
}

MCSectionCOFF *MCContext::getCOFFSection(StringRef Section,
                                         unsigned Characteristics,
                                         SectionKind Kind,
                                         StringRef COMDATSymName, int Selection,
                                         unsigned UniqueID,
                                         const char *BeginSymName) {
  MCSymbol *COMDATSymbol = nullptr;
  if (!COMDATSymName.empty()) {
    COMDATSymbol = getOrCreateSymbol(COMDATSymName);
    COMDATSymName = COMDATSymbol->getName();
  }


  // Do the lookup, if we have a hit, return it.
  COFFSectionKey T{Section, COMDATSymName, Selection, UniqueID};
  auto IterBool = COFFUniquingMap.insert(std::make_pair(T, nullptr));
  auto Iter = IterBool.first;
  if (!IterBool.second)
    return Iter->second;

  MCSymbol *Begin = nullptr;
  if (BeginSymName)
    Begin = createTempSymbol(BeginSymName, false);

  StringRef CachedName = Iter->first.SectionName;
  MCSectionCOFF *Result = new (COFFAllocator.Allocate()) MCSectionCOFF(
      CachedName, Characteristics, COMDATSymbol, Selection, Kind, Begin);

  Iter->second = Result;
  return Result;
}

MCSectionCOFF *MCContext::getCOFFSection(StringRef Section,
                                         unsigned Characteristics,
                                         SectionKind Kind,
                                         const char *BeginSymName) {
  return getCOFFSection(Section, Characteristics, Kind, "", 0, GenericSectionID,
                        BeginSymName);
}

MCSectionCOFF *MCContext::getCOFFSection(StringRef Section) {
  COFFSectionKey T{Section, "", 0, GenericSectionID};
  auto Iter = COFFUniquingMap.find(T);
  if (Iter == COFFUniquingMap.end())
    return nullptr;
  return Iter->second;
}

MCSectionCOFF *MCContext::getAssociativeCOFFSection(MCSectionCOFF *Sec,
                                                    const MCSymbol *KeySym,
                                                    unsigned UniqueID) {
  // Return the normal section if we don't have to be associative or unique.
  if (!KeySym && UniqueID == GenericSectionID)
    return Sec;

  // If we have a key symbol, make an associative section with the same name and
  // kind as the normal section.
  unsigned Characteristics = Sec->getCharacteristics();
  if (KeySym) {
    Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
    return getCOFFSection(Sec->getSectionName(), Characteristics,
                          Sec->getKind(), KeySym->getName(),
                          COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE, UniqueID);
  }

  return getCOFFSection(Sec->getSectionName(), Characteristics, Sec->getKind(),
                        "", 0, UniqueID);
}

MCSectionWasm *MCContext::getWasmSection(const Twine &Section, SectionKind K,
                                         const Twine &Group, unsigned UniqueID,
                                         const char *BeginSymName) {
  MCSymbolWasm *GroupSym = nullptr;
  if (!Group.isTriviallyEmpty() && !Group.str().empty()) {
    GroupSym = cast<MCSymbolWasm>(getOrCreateSymbol(Group));
    GroupSym->setComdat(true);
  }

  return getWasmSection(Section, K, GroupSym, UniqueID, BeginSymName);
}

MCSectionWasm *MCContext::getWasmSection(const Twine &Section, SectionKind Kind,
                                         const MCSymbolWasm *GroupSym,
                                         unsigned UniqueID,
                                         const char *BeginSymName) {
  StringRef Group = "";
  if (GroupSym)
    Group = GroupSym->getName();
  // Do the lookup, if we have a hit, return it.
  auto IterBool = WasmUniquingMap.insert(
      std::make_pair(WasmSectionKey{Section.str(), Group, UniqueID}, nullptr));
  auto &Entry = *IterBool.first;
  if (!IterBool.second)
    return Entry.second;

  StringRef CachedName = Entry.first.SectionName;

  MCSymbol *Begin = createSymbol(CachedName, false, false);
  cast<MCSymbolWasm>(Begin)->setType(wasm::WASM_SYMBOL_TYPE_SECTION);

  MCSectionWasm *Result = new (WasmAllocator.Allocate())
      MCSectionWasm(CachedName, Kind, GroupSym, UniqueID, Begin);
  Entry.second = Result;

  auto *F = new MCDataFragment();
  Result->getFragmentList().insert(Result->begin(), F);
  F->setParent(Result);
  Begin->setFragment(F);

  return Result;
}

MCSubtargetInfo &MCContext::getSubtargetCopy(const MCSubtargetInfo &STI) {
  return *new (MCSubtargetAllocator.Allocate()) MCSubtargetInfo(STI);
}

void MCContext::addDebugPrefixMapEntry(const std::string &From,
                                       const std::string &To) {
  DebugPrefixMap.insert(std::make_pair(From, To));
}

void MCContext::RemapDebugPaths() {
  const auto &DebugPrefixMap = this->DebugPrefixMap;
  const auto RemapDebugPath = [&DebugPrefixMap](std::string &Path) {
    for (const auto &Entry : DebugPrefixMap)
      if (StringRef(Path).startswith(Entry.first)) {
        std::string RemappedPath =
            (Twine(Entry.second) + Path.substr(Entry.first.size())).str();
        Path.swap(RemappedPath);
      }
  };

  // Remap compilation directory.
  std::string CompDir = CompilationDir.str();
  RemapDebugPath(CompDir);
  CompilationDir = CompDir;

  // Remap MCDwarfDirs in all compilation units.
  for (auto &CUIDTablePair : MCDwarfLineTablesCUMap)
    for (auto &Dir : CUIDTablePair.second.getMCDwarfDirs())
      RemapDebugPath(Dir);
}

//===----------------------------------------------------------------------===//
// Dwarf Management
//===----------------------------------------------------------------------===//

/// getDwarfFile - takes a file name and number to place in the dwarf file and
/// directory tables.  If the file number has already been allocated it is an
/// error and zero is returned and the client reports the error, else the
/// allocated file number is returned.  The file numbers may be in any order.
Expected<unsigned> MCContext::getDwarfFile(StringRef Directory,
                                           StringRef FileName,
                                           unsigned FileNumber,
                                           MD5::MD5Result *Checksum,
                                           Optional<StringRef> Source,
                                           unsigned CUID) {
  MCDwarfLineTable &Table = MCDwarfLineTablesCUMap[CUID];
  return Table.tryGetFile(Directory, FileName, Checksum, Source, FileNumber);
}

/// isValidDwarfFileNumber - takes a dwarf file number and returns true if it
/// currently is assigned and false otherwise.
bool MCContext::isValidDwarfFileNumber(unsigned FileNumber, unsigned CUID) {
  const MCDwarfLineTable &LineTable = getMCDwarfLineTable(CUID);
  if (FileNumber == 0)
    return getDwarfVersion() >= 5 && LineTable.hasRootFile();
  if (FileNumber >= LineTable.getMCDwarfFiles().size())
    return false;

  return !LineTable.getMCDwarfFiles()[FileNumber].Name.empty();
}

/// Remove empty sections from SectionsForRanges, to avoid generating
/// useless debug info for them.
void MCContext::finalizeDwarfSections(MCStreamer &MCOS) {
  SectionsForRanges.remove_if(
      [&](MCSection *Sec) { return !MCOS.mayHaveInstructions(*Sec); });
}

CodeViewContext &MCContext::getCVContext() {
  if (!CVContext.get())
    CVContext.reset(new CodeViewContext);
  return *CVContext.get();
}

//===----------------------------------------------------------------------===//
// Error Reporting
//===----------------------------------------------------------------------===//

void MCContext::reportError(SMLoc Loc, const Twine &Msg) {
  HadError = true;

  // If we have a source manager use it. Otherwise, try using the inline source
  // manager.
  // If that fails, use the generic report_fatal_error().
  if (SrcMgr)
    SrcMgr->PrintMessage(Loc, SourceMgr::DK_Error, Msg);
  else if (InlineSrcMgr)
    InlineSrcMgr->PrintMessage(Loc, SourceMgr::DK_Error, Msg);
  else
    report_fatal_error(Msg, false);
}

void MCContext::reportFatalError(SMLoc Loc, const Twine &Msg) {
  reportError(Loc, Msg);

  // If we reached here, we are failing ungracefully. Run the interrupt handlers
  // to make sure any special cleanups get done, in particular that we remove
  // files registered with RemoveFileOnSignal.
  sys::RunInterruptHandlers();
  exit(1);
}
