//===-- WindowsResource.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the .res file class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/WindowsResource.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include <ctime>
#include <queue>
#include <system_error>

using namespace llvm;
using namespace object;

namespace llvm {
namespace object {

#define RETURN_IF_ERROR(X)                                                     \
  if (auto EC = X)                                                             \
    return EC;

const uint32_t MIN_HEADER_SIZE = 7 * sizeof(uint32_t) + 2 * sizeof(uint16_t);

// COFF files seem to be inconsistent with alignment between sections, just use
// 8-byte because it makes everyone happy.
const uint32_t SECTION_ALIGNMENT = sizeof(uint64_t);

uint32_t WindowsResourceParser::TreeNode::StringCount = 0;
uint32_t WindowsResourceParser::TreeNode::DataCount = 0;

WindowsResource::WindowsResource(MemoryBufferRef Source)
    : Binary(Binary::ID_WinRes, Source) {
  size_t LeadingSize = WIN_RES_MAGIC_SIZE + WIN_RES_NULL_ENTRY_SIZE;
  BBS = BinaryByteStream(Data.getBuffer().drop_front(LeadingSize),
                         support::little);
}

Expected<std::unique_ptr<WindowsResource>>
WindowsResource::createWindowsResource(MemoryBufferRef Source) {
  if (Source.getBufferSize() < WIN_RES_MAGIC_SIZE + WIN_RES_NULL_ENTRY_SIZE)
    return make_error<GenericBinaryError>(
        "File too small to be a resource file",
        object_error::invalid_file_type);
  std::unique_ptr<WindowsResource> Ret(new WindowsResource(Source));
  return std::move(Ret);
}

Expected<ResourceEntryRef> WindowsResource::getHeadEntry() {
  if (BBS.getLength() < sizeof(WinResHeaderPrefix) + sizeof(WinResHeaderSuffix))
    return make_error<EmptyResError>(".res contains no entries",
                                     object_error::unexpected_eof);
  return ResourceEntryRef::create(BinaryStreamRef(BBS), this);
}

ResourceEntryRef::ResourceEntryRef(BinaryStreamRef Ref,
                                   const WindowsResource *Owner)
    : Reader(Ref) {}

Expected<ResourceEntryRef>
ResourceEntryRef::create(BinaryStreamRef BSR, const WindowsResource *Owner) {
  auto Ref = ResourceEntryRef(BSR, Owner);
  if (auto E = Ref.loadNext())
    return std::move(E);
  return Ref;
}

Error ResourceEntryRef::moveNext(bool &End) {
  // Reached end of all the entries.
  if (Reader.bytesRemaining() == 0) {
    End = true;
    return Error::success();
  }
  RETURN_IF_ERROR(loadNext());

  return Error::success();
}

static Error readStringOrId(BinaryStreamReader &Reader, uint16_t &ID,
                            ArrayRef<UTF16> &Str, bool &IsString) {
  uint16_t IDFlag;
  RETURN_IF_ERROR(Reader.readInteger(IDFlag));
  IsString = IDFlag != 0xffff;

  if (IsString) {
    Reader.setOffset(
        Reader.getOffset() -
        sizeof(uint16_t)); // Re-read the bytes which we used to check the flag.
    RETURN_IF_ERROR(Reader.readWideString(Str));
  } else
    RETURN_IF_ERROR(Reader.readInteger(ID));

  return Error::success();
}

Error ResourceEntryRef::loadNext() {
  const WinResHeaderPrefix *Prefix;
  RETURN_IF_ERROR(Reader.readObject(Prefix));

  if (Prefix->HeaderSize < MIN_HEADER_SIZE)
    return make_error<GenericBinaryError>("Header size is too small.",
                                          object_error::parse_failed);

  RETURN_IF_ERROR(readStringOrId(Reader, TypeID, Type, IsStringType));

  RETURN_IF_ERROR(readStringOrId(Reader, NameID, Name, IsStringName));

  RETURN_IF_ERROR(Reader.padToAlignment(WIN_RES_HEADER_ALIGNMENT));

  RETURN_IF_ERROR(Reader.readObject(Suffix));

  RETURN_IF_ERROR(Reader.readArray(Data, Prefix->DataSize));

  RETURN_IF_ERROR(Reader.padToAlignment(WIN_RES_DATA_ALIGNMENT));

  return Error::success();
}

WindowsResourceParser::WindowsResourceParser() : Root(false) {}

Error WindowsResourceParser::parse(WindowsResource *WR) {
  auto EntryOrErr = WR->getHeadEntry();
  if (!EntryOrErr) {
    auto E = EntryOrErr.takeError();
    if (E.isA<EmptyResError>()) {
      // Check if the .res file contains no entries.  In this case we don't have
      // to throw an error but can rather just return without parsing anything.
      // This applies for files which have a valid PE header magic and the
      // mandatory empty null resource entry.  Files which do not fit this
      // criteria would have already been filtered out by
      // WindowsResource::createWindowsResource().
      consumeError(std::move(E));
      return Error::success();
    }
    return E;
  }

  ResourceEntryRef Entry = EntryOrErr.get();
  bool End = false;
  while (!End) {
    Data.push_back(Entry.getData());

    bool IsNewTypeString = false;
    bool IsNewNameString = false;

    Root.addEntry(Entry, IsNewTypeString, IsNewNameString);

    if (IsNewTypeString)
      StringTable.push_back(Entry.getTypeString());

    if (IsNewNameString)
      StringTable.push_back(Entry.getNameString());

    RETURN_IF_ERROR(Entry.moveNext(End));
  }

  return Error::success();
}

void WindowsResourceParser::printTree(raw_ostream &OS) const {
  ScopedPrinter Writer(OS);
  Root.print(Writer, "Resource Tree");
}

void WindowsResourceParser::TreeNode::addEntry(const ResourceEntryRef &Entry,
                                               bool &IsNewTypeString,
                                               bool &IsNewNameString) {
  TreeNode &TypeNode = addTypeNode(Entry, IsNewTypeString);
  TreeNode &NameNode = TypeNode.addNameNode(Entry, IsNewNameString);
  NameNode.addLanguageNode(Entry);
}

WindowsResourceParser::TreeNode::TreeNode(bool IsStringNode) {
  if (IsStringNode)
    StringIndex = StringCount++;
}

WindowsResourceParser::TreeNode::TreeNode(uint16_t MajorVersion,
                                          uint16_t MinorVersion,
                                          uint32_t Characteristics)
    : IsDataNode(true), MajorVersion(MajorVersion), MinorVersion(MinorVersion),
      Characteristics(Characteristics) {
    DataIndex = DataCount++;
}

std::unique_ptr<WindowsResourceParser::TreeNode>
WindowsResourceParser::TreeNode::createStringNode() {
  return std::unique_ptr<TreeNode>(new TreeNode(true));
}

std::unique_ptr<WindowsResourceParser::TreeNode>
WindowsResourceParser::TreeNode::createIDNode() {
  return std::unique_ptr<TreeNode>(new TreeNode(false));
}

std::unique_ptr<WindowsResourceParser::TreeNode>
WindowsResourceParser::TreeNode::createDataNode(uint16_t MajorVersion,
                                                uint16_t MinorVersion,
                                                uint32_t Characteristics) {
  return std::unique_ptr<TreeNode>(
      new TreeNode(MajorVersion, MinorVersion, Characteristics));
}

WindowsResourceParser::TreeNode &
WindowsResourceParser::TreeNode::addTypeNode(const ResourceEntryRef &Entry,
                                             bool &IsNewTypeString) {
  if (Entry.checkTypeString())
    return addChild(Entry.getTypeString(), IsNewTypeString);
  else
    return addChild(Entry.getTypeID());
}

WindowsResourceParser::TreeNode &
WindowsResourceParser::TreeNode::addNameNode(const ResourceEntryRef &Entry,
                                             bool &IsNewNameString) {
  if (Entry.checkNameString())
    return addChild(Entry.getNameString(), IsNewNameString);
  else
    return addChild(Entry.getNameID());
}

WindowsResourceParser::TreeNode &
WindowsResourceParser::TreeNode::addLanguageNode(
    const ResourceEntryRef &Entry) {
  return addChild(Entry.getLanguage(), true, Entry.getMajorVersion(),
                  Entry.getMinorVersion(), Entry.getCharacteristics());
}

WindowsResourceParser::TreeNode &WindowsResourceParser::TreeNode::addChild(
    uint32_t ID, bool IsDataNode, uint16_t MajorVersion, uint16_t MinorVersion,
    uint32_t Characteristics) {
  auto Child = IDChildren.find(ID);
  if (Child == IDChildren.end()) {
    auto NewChild =
        IsDataNode ? createDataNode(MajorVersion, MinorVersion, Characteristics)
                   : createIDNode();
    WindowsResourceParser::TreeNode &Node = *NewChild;
    IDChildren.emplace(ID, std::move(NewChild));
    return Node;
  } else
    return *(Child->second);
}

WindowsResourceParser::TreeNode &
WindowsResourceParser::TreeNode::addChild(ArrayRef<UTF16> NameRef,
                                          bool &IsNewString) {
  std::string NameString;
  ArrayRef<UTF16> CorrectedName;
  std::vector<UTF16> EndianCorrectedName;
  if (sys::IsBigEndianHost) {
    EndianCorrectedName.resize(NameRef.size() + 1);
    llvm::copy(NameRef, EndianCorrectedName.begin() + 1);
    EndianCorrectedName[0] = UNI_UTF16_BYTE_ORDER_MARK_SWAPPED;
    CorrectedName = makeArrayRef(EndianCorrectedName);
  } else
    CorrectedName = NameRef;
  convertUTF16ToUTF8String(CorrectedName, NameString);

  auto Child = StringChildren.find(NameString);
  if (Child == StringChildren.end()) {
    auto NewChild = createStringNode();
    IsNewString = true;
    WindowsResourceParser::TreeNode &Node = *NewChild;
    StringChildren.emplace(NameString, std::move(NewChild));
    return Node;
  } else
    return *(Child->second);
}

void WindowsResourceParser::TreeNode::print(ScopedPrinter &Writer,
                                            StringRef Name) const {
  ListScope NodeScope(Writer, Name);
  for (auto const &Child : StringChildren) {
    Child.second->print(Writer, Child.first);
  }
  for (auto const &Child : IDChildren) {
    Child.second->print(Writer, to_string(Child.first));
  }
}

// This function returns the size of the entire resource tree, including
// directory tables, directory entries, and data entries.  It does not include
// the directory strings or the relocations of the .rsrc section.
uint32_t WindowsResourceParser::TreeNode::getTreeSize() const {
  uint32_t Size = (IDChildren.size() + StringChildren.size()) *
                  sizeof(coff_resource_dir_entry);

  // Reached a node pointing to a data entry.
  if (IsDataNode) {
    Size += sizeof(coff_resource_data_entry);
    return Size;
  }

  // If the node does not point to data, it must have a directory table pointing
  // to other nodes.
  Size += sizeof(coff_resource_dir_table);

  for (auto const &Child : StringChildren) {
    Size += Child.second->getTreeSize();
  }
  for (auto const &Child : IDChildren) {
    Size += Child.second->getTreeSize();
  }
  return Size;
}

class WindowsResourceCOFFWriter {
public:
  WindowsResourceCOFFWriter(COFF::MachineTypes MachineType,
                            const WindowsResourceParser &Parser, Error &E);
  std::unique_ptr<MemoryBuffer> write();

private:
  void performFileLayout();
  void performSectionOneLayout();
  void performSectionTwoLayout();
  void writeCOFFHeader();
  void writeFirstSectionHeader();
  void writeSecondSectionHeader();
  void writeFirstSection();
  void writeSecondSection();
  void writeSymbolTable();
  void writeStringTable();
  void writeDirectoryTree();
  void writeDirectoryStringTable();
  void writeFirstSectionRelocations();
  std::unique_ptr<WritableMemoryBuffer> OutputBuffer;
  char *BufferStart;
  uint64_t CurrentOffset = 0;
  COFF::MachineTypes MachineType;
  const WindowsResourceParser::TreeNode &Resources;
  const ArrayRef<std::vector<uint8_t>> Data;
  uint64_t FileSize;
  uint32_t SymbolTableOffset;
  uint32_t SectionOneSize;
  uint32_t SectionOneOffset;
  uint32_t SectionOneRelocations;
  uint32_t SectionTwoSize;
  uint32_t SectionTwoOffset;
  const ArrayRef<std::vector<UTF16>> StringTable;
  std::vector<uint32_t> StringTableOffsets;
  std::vector<uint32_t> DataOffsets;
  std::vector<uint32_t> RelocationAddresses;
};

WindowsResourceCOFFWriter::WindowsResourceCOFFWriter(
    COFF::MachineTypes MachineType, const WindowsResourceParser &Parser,
    Error &E)
    : MachineType(MachineType), Resources(Parser.getTree()),
      Data(Parser.getData()), StringTable(Parser.getStringTable()) {
  performFileLayout();

  OutputBuffer = WritableMemoryBuffer::getNewMemBuffer(FileSize);
}

void WindowsResourceCOFFWriter::performFileLayout() {
  // Add size of COFF header.
  FileSize = COFF::Header16Size;

  // one .rsrc section header for directory tree, another for resource data.
  FileSize += 2 * COFF::SectionSize;

  performSectionOneLayout();
  performSectionTwoLayout();

  // We have reached the address of the symbol table.
  SymbolTableOffset = FileSize;

  FileSize += COFF::Symbol16Size;     // size of the @feat.00 symbol.
  FileSize += 4 * COFF::Symbol16Size; // symbol + aux for each section.
  FileSize += Data.size() * COFF::Symbol16Size; // 1 symbol per resource.
  FileSize += 4; // four null bytes for the string table.
}

void WindowsResourceCOFFWriter::performSectionOneLayout() {
  SectionOneOffset = FileSize;

  SectionOneSize = Resources.getTreeSize();
  uint32_t CurrentStringOffset = SectionOneSize;
  uint32_t TotalStringTableSize = 0;
  for (auto const &String : StringTable) {
    StringTableOffsets.push_back(CurrentStringOffset);
    uint32_t StringSize = String.size() * sizeof(UTF16) + sizeof(uint16_t);
    CurrentStringOffset += StringSize;
    TotalStringTableSize += StringSize;
  }
  SectionOneSize += alignTo(TotalStringTableSize, sizeof(uint32_t));

  // account for the relocations of section one.
  SectionOneRelocations = FileSize + SectionOneSize;
  FileSize += SectionOneSize;
  FileSize +=
      Data.size() * COFF::RelocationSize; // one relocation for each resource.
  FileSize = alignTo(FileSize, SECTION_ALIGNMENT);
}

void WindowsResourceCOFFWriter::performSectionTwoLayout() {
  // add size of .rsrc$2 section, which contains all resource data on 8-byte
  // alignment.
  SectionTwoOffset = FileSize;
  SectionTwoSize = 0;
  for (auto const &Entry : Data) {
    DataOffsets.push_back(SectionTwoSize);
    SectionTwoSize += alignTo(Entry.size(), sizeof(uint64_t));
  }
  FileSize += SectionTwoSize;
  FileSize = alignTo(FileSize, SECTION_ALIGNMENT);
}

static std::time_t getTime() {
  std::time_t Now = time(nullptr);
  if (Now < 0 || !isUInt<32>(Now))
    return UINT32_MAX;
  return Now;
}

std::unique_ptr<MemoryBuffer> WindowsResourceCOFFWriter::write() {
  BufferStart = OutputBuffer->getBufferStart();

  writeCOFFHeader();
  writeFirstSectionHeader();
  writeSecondSectionHeader();
  writeFirstSection();
  writeSecondSection();
  writeSymbolTable();
  writeStringTable();

  return std::move(OutputBuffer);
}

void WindowsResourceCOFFWriter::writeCOFFHeader() {
  // Write the COFF header.
  auto *Header = reinterpret_cast<coff_file_header *>(BufferStart);
  Header->Machine = MachineType;
  Header->NumberOfSections = 2;
  Header->TimeDateStamp = getTime();
  Header->PointerToSymbolTable = SymbolTableOffset;
  // One symbol for every resource plus 2 for each section and @feat.00
  Header->NumberOfSymbols = Data.size() + 5;
  Header->SizeOfOptionalHeader = 0;
  Header->Characteristics = COFF::IMAGE_FILE_32BIT_MACHINE;
}

void WindowsResourceCOFFWriter::writeFirstSectionHeader() {
  // Write the first section header.
  CurrentOffset += sizeof(coff_file_header);
  auto *SectionOneHeader =
      reinterpret_cast<coff_section *>(BufferStart + CurrentOffset);
  strncpy(SectionOneHeader->Name, ".rsrc$01", (size_t)COFF::NameSize);
  SectionOneHeader->VirtualSize = 0;
  SectionOneHeader->VirtualAddress = 0;
  SectionOneHeader->SizeOfRawData = SectionOneSize;
  SectionOneHeader->PointerToRawData = SectionOneOffset;
  SectionOneHeader->PointerToRelocations = SectionOneRelocations;
  SectionOneHeader->PointerToLinenumbers = 0;
  SectionOneHeader->NumberOfRelocations = Data.size();
  SectionOneHeader->NumberOfLinenumbers = 0;
  SectionOneHeader->Characteristics += COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
  SectionOneHeader->Characteristics += COFF::IMAGE_SCN_MEM_READ;
}

void WindowsResourceCOFFWriter::writeSecondSectionHeader() {
  // Write the second section header.
  CurrentOffset += sizeof(coff_section);
  auto *SectionTwoHeader =
      reinterpret_cast<coff_section *>(BufferStart + CurrentOffset);
  strncpy(SectionTwoHeader->Name, ".rsrc$02", (size_t)COFF::NameSize);
  SectionTwoHeader->VirtualSize = 0;
  SectionTwoHeader->VirtualAddress = 0;
  SectionTwoHeader->SizeOfRawData = SectionTwoSize;
  SectionTwoHeader->PointerToRawData = SectionTwoOffset;
  SectionTwoHeader->PointerToRelocations = 0;
  SectionTwoHeader->PointerToLinenumbers = 0;
  SectionTwoHeader->NumberOfRelocations = 0;
  SectionTwoHeader->NumberOfLinenumbers = 0;
  SectionTwoHeader->Characteristics = COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
  SectionTwoHeader->Characteristics += COFF::IMAGE_SCN_MEM_READ;
}

void WindowsResourceCOFFWriter::writeFirstSection() {
  // Write section one.
  CurrentOffset += sizeof(coff_section);

  writeDirectoryTree();
  writeDirectoryStringTable();
  writeFirstSectionRelocations();

  CurrentOffset = alignTo(CurrentOffset, SECTION_ALIGNMENT);
}

void WindowsResourceCOFFWriter::writeSecondSection() {
  // Now write the .rsrc$02 section.
  for (auto const &RawDataEntry : Data) {
    llvm::copy(RawDataEntry, BufferStart + CurrentOffset);
    CurrentOffset += alignTo(RawDataEntry.size(), sizeof(uint64_t));
  }

  CurrentOffset = alignTo(CurrentOffset, SECTION_ALIGNMENT);
}

void WindowsResourceCOFFWriter::writeSymbolTable() {
  // Now write the symbol table.
  // First, the feat symbol.
  auto *Symbol = reinterpret_cast<coff_symbol16 *>(BufferStart + CurrentOffset);
  strncpy(Symbol->Name.ShortName, "@feat.00", (size_t)COFF::NameSize);
  Symbol->Value = 0x11;
  Symbol->SectionNumber = 0xffff;
  Symbol->Type = COFF::IMAGE_SYM_DTYPE_NULL;
  Symbol->StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
  Symbol->NumberOfAuxSymbols = 0;
  CurrentOffset += sizeof(coff_symbol16);

  // Now write the .rsrc1 symbol + aux.
  Symbol = reinterpret_cast<coff_symbol16 *>(BufferStart + CurrentOffset);
  strncpy(Symbol->Name.ShortName, ".rsrc$01", (size_t)COFF::NameSize);
  Symbol->Value = 0;
  Symbol->SectionNumber = 1;
  Symbol->Type = COFF::IMAGE_SYM_DTYPE_NULL;
  Symbol->StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
  Symbol->NumberOfAuxSymbols = 1;
  CurrentOffset += sizeof(coff_symbol16);
  auto *Aux = reinterpret_cast<coff_aux_section_definition *>(BufferStart +
                                                              CurrentOffset);
  Aux->Length = SectionOneSize;
  Aux->NumberOfRelocations = Data.size();
  Aux->NumberOfLinenumbers = 0;
  Aux->CheckSum = 0;
  Aux->NumberLowPart = 0;
  Aux->Selection = 0;
  CurrentOffset += sizeof(coff_aux_section_definition);

  // Now write the .rsrc2 symbol + aux.
  Symbol = reinterpret_cast<coff_symbol16 *>(BufferStart + CurrentOffset);
  strncpy(Symbol->Name.ShortName, ".rsrc$02", (size_t)COFF::NameSize);
  Symbol->Value = 0;
  Symbol->SectionNumber = 2;
  Symbol->Type = COFF::IMAGE_SYM_DTYPE_NULL;
  Symbol->StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
  Symbol->NumberOfAuxSymbols = 1;
  CurrentOffset += sizeof(coff_symbol16);
  Aux = reinterpret_cast<coff_aux_section_definition *>(BufferStart +
                                                        CurrentOffset);
  Aux->Length = SectionTwoSize;
  Aux->NumberOfRelocations = 0;
  Aux->NumberOfLinenumbers = 0;
  Aux->CheckSum = 0;
  Aux->NumberLowPart = 0;
  Aux->Selection = 0;
  CurrentOffset += sizeof(coff_aux_section_definition);

  // Now write a symbol for each relocation.
  for (unsigned i = 0; i < Data.size(); i++) {
    auto RelocationName = formatv("$R{0:X-6}", i & 0xffffff).sstr<COFF::NameSize>();
    Symbol = reinterpret_cast<coff_symbol16 *>(BufferStart + CurrentOffset);
    memcpy(Symbol->Name.ShortName, RelocationName.data(), (size_t) COFF::NameSize);
    Symbol->Value = DataOffsets[i];
    Symbol->SectionNumber = 2;
    Symbol->Type = COFF::IMAGE_SYM_DTYPE_NULL;
    Symbol->StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
    Symbol->NumberOfAuxSymbols = 0;
    CurrentOffset += sizeof(coff_symbol16);
  }
}

void WindowsResourceCOFFWriter::writeStringTable() {
  // Just 4 null bytes for the string table.
  auto COFFStringTable = reinterpret_cast<void *>(BufferStart + CurrentOffset);
  memset(COFFStringTable, 0, 4);
}

void WindowsResourceCOFFWriter::writeDirectoryTree() {
  // Traverse parsed resource tree breadth-first and write the corresponding
  // COFF objects.
  std::queue<const WindowsResourceParser::TreeNode *> Queue;
  Queue.push(&Resources);
  uint32_t NextLevelOffset =
      sizeof(coff_resource_dir_table) + (Resources.getStringChildren().size() +
                                         Resources.getIDChildren().size()) *
                                            sizeof(coff_resource_dir_entry);
  std::vector<const WindowsResourceParser::TreeNode *> DataEntriesTreeOrder;
  uint32_t CurrentRelativeOffset = 0;

  while (!Queue.empty()) {
    auto CurrentNode = Queue.front();
    Queue.pop();
    auto *Table = reinterpret_cast<coff_resource_dir_table *>(BufferStart +
                                                              CurrentOffset);
    Table->Characteristics = CurrentNode->getCharacteristics();
    Table->TimeDateStamp = 0;
    Table->MajorVersion = CurrentNode->getMajorVersion();
    Table->MinorVersion = CurrentNode->getMinorVersion();
    auto &IDChildren = CurrentNode->getIDChildren();
    auto &StringChildren = CurrentNode->getStringChildren();
    Table->NumberOfNameEntries = StringChildren.size();
    Table->NumberOfIDEntries = IDChildren.size();
    CurrentOffset += sizeof(coff_resource_dir_table);
    CurrentRelativeOffset += sizeof(coff_resource_dir_table);

    // Write the directory entries immediately following each directory table.
    for (auto const &Child : StringChildren) {
      auto *Entry = reinterpret_cast<coff_resource_dir_entry *>(BufferStart +
                                                                CurrentOffset);
      Entry->Identifier.setNameOffset(
          StringTableOffsets[Child.second->getStringIndex()]);
      if (Child.second->checkIsDataNode()) {
        Entry->Offset.DataEntryOffset = NextLevelOffset;
        NextLevelOffset += sizeof(coff_resource_data_entry);
        DataEntriesTreeOrder.push_back(Child.second.get());
      } else {
        Entry->Offset.SubdirOffset = NextLevelOffset + (1 << 31);
        NextLevelOffset += sizeof(coff_resource_dir_table) +
                           (Child.second->getStringChildren().size() +
                            Child.second->getIDChildren().size()) *
                               sizeof(coff_resource_dir_entry);
        Queue.push(Child.second.get());
      }
      CurrentOffset += sizeof(coff_resource_dir_entry);
      CurrentRelativeOffset += sizeof(coff_resource_dir_entry);
    }
    for (auto const &Child : IDChildren) {
      auto *Entry = reinterpret_cast<coff_resource_dir_entry *>(BufferStart +
                                                                CurrentOffset);
      Entry->Identifier.ID = Child.first;
      if (Child.second->checkIsDataNode()) {
        Entry->Offset.DataEntryOffset = NextLevelOffset;
        NextLevelOffset += sizeof(coff_resource_data_entry);
        DataEntriesTreeOrder.push_back(Child.second.get());
      } else {
        Entry->Offset.SubdirOffset = NextLevelOffset + (1 << 31);
        NextLevelOffset += sizeof(coff_resource_dir_table) +
                           (Child.second->getStringChildren().size() +
                            Child.second->getIDChildren().size()) *
                               sizeof(coff_resource_dir_entry);
        Queue.push(Child.second.get());
      }
      CurrentOffset += sizeof(coff_resource_dir_entry);
      CurrentRelativeOffset += sizeof(coff_resource_dir_entry);
    }
  }

  RelocationAddresses.resize(Data.size());
  // Now write all the resource data entries.
  for (auto DataNodes : DataEntriesTreeOrder) {
    auto *Entry = reinterpret_cast<coff_resource_data_entry *>(BufferStart +
                                                               CurrentOffset);
    RelocationAddresses[DataNodes->getDataIndex()] = CurrentRelativeOffset;
    Entry->DataRVA = 0; // Set to zero because it is a relocation.
    Entry->DataSize = Data[DataNodes->getDataIndex()].size();
    Entry->Codepage = 0;
    Entry->Reserved = 0;
    CurrentOffset += sizeof(coff_resource_data_entry);
    CurrentRelativeOffset += sizeof(coff_resource_data_entry);
  }
}

void WindowsResourceCOFFWriter::writeDirectoryStringTable() {
  // Now write the directory string table for .rsrc$01
  uint32_t TotalStringTableSize = 0;
  for (auto &String : StringTable) {
    uint16_t Length = String.size();
    support::endian::write16le(BufferStart + CurrentOffset, Length);
    CurrentOffset += sizeof(uint16_t);
    auto *Start = reinterpret_cast<UTF16 *>(BufferStart + CurrentOffset);
    llvm::copy(String, Start);
    CurrentOffset += Length * sizeof(UTF16);
    TotalStringTableSize += Length * sizeof(UTF16) + sizeof(uint16_t);
  }
  CurrentOffset +=
      alignTo(TotalStringTableSize, sizeof(uint32_t)) - TotalStringTableSize;
}

void WindowsResourceCOFFWriter::writeFirstSectionRelocations() {

  // Now write the relocations for .rsrc$01
  // Five symbols already in table before we start, @feat.00 and 2 for each
  // .rsrc section.
  uint32_t NextSymbolIndex = 5;
  for (unsigned i = 0; i < Data.size(); i++) {
    auto *Reloc =
        reinterpret_cast<coff_relocation *>(BufferStart + CurrentOffset);
    Reloc->VirtualAddress = RelocationAddresses[i];
    Reloc->SymbolTableIndex = NextSymbolIndex++;
    switch (MachineType) {
    case COFF::IMAGE_FILE_MACHINE_ARMNT:
      Reloc->Type = COFF::IMAGE_REL_ARM_ADDR32NB;
      break;
    case COFF::IMAGE_FILE_MACHINE_AMD64:
      Reloc->Type = COFF::IMAGE_REL_AMD64_ADDR32NB;
      break;
    case COFF::IMAGE_FILE_MACHINE_I386:
      Reloc->Type = COFF::IMAGE_REL_I386_DIR32NB;
      break;
    case COFF::IMAGE_FILE_MACHINE_ARM64:
      Reloc->Type = COFF::IMAGE_REL_ARM64_ADDR32NB;
      break;
    default:
      llvm_unreachable("unknown machine type");
    }
    CurrentOffset += sizeof(coff_relocation);
  }
}

Expected<std::unique_ptr<MemoryBuffer>>
writeWindowsResourceCOFF(COFF::MachineTypes MachineType,
                         const WindowsResourceParser &Parser) {
  Error E = Error::success();
  WindowsResourceCOFFWriter Writer(MachineType, Parser, E);
  if (E)
    return std::move(E);
  return Writer.write();
}

} // namespace object
} // namespace llvm
