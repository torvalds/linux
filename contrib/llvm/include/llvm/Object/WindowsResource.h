//===-- WindowsResource.h ---------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// This file declares the .res file class.  .res files are intermediate
// products of the typical resource-compilation process on Windows.  This
// process is as follows:
//
// .rc file(s) ---(rc.exe)---> .res file(s) ---(cvtres.exe)---> COFF file
//
// .rc files are human-readable scripts that list all resources a program uses.
//
// They are compiled into .res files, which are a list of the resources in
// binary form.
//
// Finally the data stored in the .res is compiled into a COFF file, where it
// is organized in a directory tree structure for optimized access by the
// program during runtime.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648007(v=vs.85).aspx
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_INCLUDE_LLVM_OBJECT_RESFILE_H
#define LLVM_INCLUDE_LLVM_OBJECT_RESFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ScopedPrinter.h"

#include <map>

namespace llvm {
namespace object {

class WindowsResource;

const size_t WIN_RES_MAGIC_SIZE = 16;
const size_t WIN_RES_NULL_ENTRY_SIZE = 16;
const uint32_t WIN_RES_HEADER_ALIGNMENT = 4;
const uint32_t WIN_RES_DATA_ALIGNMENT = 4;
const uint16_t WIN_RES_PURE_MOVEABLE = 0x0030;

struct WinResHeaderPrefix {
  support::ulittle32_t DataSize;
  support::ulittle32_t HeaderSize;
};

// Type and Name may each either be an integer ID or a string.  This struct is
// only used in the case where they are both IDs.
struct WinResIDs {
  uint16_t TypeFlag;
  support::ulittle16_t TypeID;
  uint16_t NameFlag;
  support::ulittle16_t NameID;

  void setType(uint16_t ID) {
    TypeFlag = 0xffff;
    TypeID = ID;
  }

  void setName(uint16_t ID) {
    NameFlag = 0xffff;
    NameID = ID;
  }
};

struct WinResHeaderSuffix {
  support::ulittle32_t DataVersion;
  support::ulittle16_t MemoryFlags;
  support::ulittle16_t Language;
  support::ulittle32_t Version;
  support::ulittle32_t Characteristics;
};

class EmptyResError : public GenericBinaryError {
public:
  EmptyResError(Twine Msg, object_error ECOverride)
      : GenericBinaryError(Msg, ECOverride) {}
};

class ResourceEntryRef {
public:
  Error moveNext(bool &End);
  bool checkTypeString() const { return IsStringType; }
  ArrayRef<UTF16> getTypeString() const { return Type; }
  uint16_t getTypeID() const { return TypeID; }
  bool checkNameString() const { return IsStringName; }
  ArrayRef<UTF16> getNameString() const { return Name; }
  uint16_t getNameID() const { return NameID; }
  uint16_t getDataVersion() const { return Suffix->DataVersion; }
  uint16_t getLanguage() const { return Suffix->Language; }
  uint16_t getMemoryFlags() const { return Suffix->MemoryFlags; }
  uint16_t getMajorVersion() const { return Suffix->Version >> 16; }
  uint16_t getMinorVersion() const { return Suffix->Version; }
  uint32_t getCharacteristics() const { return Suffix->Characteristics; }
  ArrayRef<uint8_t> getData() const { return Data; }

private:
  friend class WindowsResource;

  ResourceEntryRef(BinaryStreamRef Ref, const WindowsResource *Owner);
  Error loadNext();

  static Expected<ResourceEntryRef> create(BinaryStreamRef Ref,
                                           const WindowsResource *Owner);

  BinaryStreamReader Reader;
  bool IsStringType;
  ArrayRef<UTF16> Type;
  uint16_t TypeID;
  bool IsStringName;
  ArrayRef<UTF16> Name;
  uint16_t NameID;
  const WinResHeaderSuffix *Suffix = nullptr;
  ArrayRef<uint8_t> Data;
};

class WindowsResource : public Binary {
public:
  Expected<ResourceEntryRef> getHeadEntry();

  static bool classof(const Binary *V) { return V->isWinRes(); }

  static Expected<std::unique_ptr<WindowsResource>>
  createWindowsResource(MemoryBufferRef Source);

private:
  friend class ResourceEntryRef;

  WindowsResource(MemoryBufferRef Source);

  BinaryByteStream BBS;
};

class WindowsResourceParser {
public:
  class TreeNode;
  WindowsResourceParser();
  Error parse(WindowsResource *WR);
  void printTree(raw_ostream &OS) const;
  const TreeNode &getTree() const { return Root; }
  const ArrayRef<std::vector<uint8_t>> getData() const { return Data; }
  const ArrayRef<std::vector<UTF16>> getStringTable() const {
    return StringTable;
  }

  class TreeNode {
  public:
    template <typename T>
    using Children = std::map<T, std::unique_ptr<TreeNode>>;

    void print(ScopedPrinter &Writer, StringRef Name) const;
    uint32_t getTreeSize() const;
    uint32_t getStringIndex() const { return StringIndex; }
    uint32_t getDataIndex() const { return DataIndex; }
    uint16_t getMajorVersion() const { return MajorVersion; }
    uint16_t getMinorVersion() const { return MinorVersion; }
    uint32_t getCharacteristics() const { return Characteristics; }
    bool checkIsDataNode() const { return IsDataNode; }
    const Children<uint32_t> &getIDChildren() const { return IDChildren; }
    const Children<std::string> &getStringChildren() const {
      return StringChildren;
    }

  private:
    friend class WindowsResourceParser;

    static uint32_t StringCount;
    static uint32_t DataCount;

    static std::unique_ptr<TreeNode> createStringNode();
    static std::unique_ptr<TreeNode> createIDNode();
    static std::unique_ptr<TreeNode> createDataNode(uint16_t MajorVersion,
                                                    uint16_t MinorVersion,
                                                    uint32_t Characteristics);

    explicit TreeNode(bool IsStringNode);
    TreeNode(uint16_t MajorVersion, uint16_t MinorVersion,
             uint32_t Characteristics);

    void addEntry(const ResourceEntryRef &Entry, bool &IsNewTypeString,
                  bool &IsNewNameString);
    TreeNode &addTypeNode(const ResourceEntryRef &Entry, bool &IsNewTypeString);
    TreeNode &addNameNode(const ResourceEntryRef &Entry, bool &IsNewNameString);
    TreeNode &addLanguageNode(const ResourceEntryRef &Entry);
    TreeNode &addChild(uint32_t ID, bool IsDataNode = false,
                       uint16_t MajorVersion = 0, uint16_t MinorVersion = 0,
                       uint32_t Characteristics = 0);
    TreeNode &addChild(ArrayRef<UTF16> NameRef, bool &IsNewString);

    bool IsDataNode = false;
    uint32_t StringIndex;
    uint32_t DataIndex;
    Children<uint32_t> IDChildren;
    Children<std::string> StringChildren;
    uint16_t MajorVersion = 0;
    uint16_t MinorVersion = 0;
    uint32_t Characteristics = 0;
  };

private:
  TreeNode Root;
  std::vector<std::vector<uint8_t>> Data;
  std::vector<std::vector<UTF16>> StringTable;
};

Expected<std::unique_ptr<MemoryBuffer>>
writeWindowsResourceCOFF(llvm::COFF::MachineTypes MachineType,
                         const WindowsResourceParser &Parser);

} // namespace object
} // namespace llvm

#endif
