//===- MachOUniversal.cpp - Mach-O universal binary -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachOUniversalBinary class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;
using namespace object;

static Error
malformedError(Twine Msg) {
  std::string StringMsg = "truncated or malformed fat file (" + Msg.str() + ")";
  return make_error<GenericBinaryError>(std::move(StringMsg),
                                        object_error::parse_failed);
}

template<typename T>
static T getUniversalBinaryStruct(const char *Ptr) {
  T Res;
  memcpy(&Res, Ptr, sizeof(T));
  // Universal binary headers have big-endian byte order.
  if (sys::IsLittleEndianHost)
    swapStruct(Res);
  return Res;
}

MachOUniversalBinary::ObjectForArch::ObjectForArch(
    const MachOUniversalBinary *Parent, uint32_t Index)
    : Parent(Parent), Index(Index) {
  // The iterators use Parent as a nullptr and an Index+1 == NumberOfObjects.
  if (!Parent || Index >= Parent->getNumberOfObjects()) {
    clear();
  } else {
    // Parse object header.
    StringRef ParentData = Parent->getData();
    if (Parent->getMagic() == MachO::FAT_MAGIC) {
      const char *HeaderPos = ParentData.begin() + sizeof(MachO::fat_header) +
                              Index * sizeof(MachO::fat_arch);
      Header = getUniversalBinaryStruct<MachO::fat_arch>(HeaderPos);
    } else { // Parent->getMagic() == MachO::FAT_MAGIC_64
      const char *HeaderPos = ParentData.begin() + sizeof(MachO::fat_header) +
                              Index * sizeof(MachO::fat_arch_64);
      Header64 = getUniversalBinaryStruct<MachO::fat_arch_64>(HeaderPos);
    }
  }
}

Expected<std::unique_ptr<MachOObjectFile>>
MachOUniversalBinary::ObjectForArch::getAsObjectFile() const {
  if (!Parent)
    report_fatal_error("MachOUniversalBinary::ObjectForArch::getAsObjectFile() "
                       "called when Parent is a nullptr");

  StringRef ParentData = Parent->getData();
  StringRef ObjectData;
  uint32_t cputype;
  if (Parent->getMagic() == MachO::FAT_MAGIC) {
    ObjectData = ParentData.substr(Header.offset, Header.size);
    cputype = Header.cputype;
  } else { // Parent->getMagic() == MachO::FAT_MAGIC_64
    ObjectData = ParentData.substr(Header64.offset, Header64.size);
    cputype = Header64.cputype;
  }
  StringRef ObjectName = Parent->getFileName();
  MemoryBufferRef ObjBuffer(ObjectData, ObjectName);
  return ObjectFile::createMachOObjectFile(ObjBuffer, cputype, Index);
}

Expected<std::unique_ptr<Archive>>
MachOUniversalBinary::ObjectForArch::getAsArchive() const {
  if (!Parent)
    report_fatal_error("MachOUniversalBinary::ObjectForArch::getAsArchive() "
                       "called when Parent is a nullptr");

  StringRef ParentData = Parent->getData();
  StringRef ObjectData;
  if (Parent->getMagic() == MachO::FAT_MAGIC)
    ObjectData = ParentData.substr(Header.offset, Header.size);
  else // Parent->getMagic() == MachO::FAT_MAGIC_64
    ObjectData = ParentData.substr(Header64.offset, Header64.size);
  StringRef ObjectName = Parent->getFileName();
  MemoryBufferRef ObjBuffer(ObjectData, ObjectName);
  return Archive::create(ObjBuffer);
}

void MachOUniversalBinary::anchor() { }

Expected<std::unique_ptr<MachOUniversalBinary>>
MachOUniversalBinary::create(MemoryBufferRef Source) {
  Error Err = Error::success();
  std::unique_ptr<MachOUniversalBinary> Ret(
      new MachOUniversalBinary(Source, Err));
  if (Err)
    return std::move(Err);
  return std::move(Ret);
}

MachOUniversalBinary::MachOUniversalBinary(MemoryBufferRef Source, Error &Err)
    : Binary(Binary::ID_MachOUniversalBinary, Source), Magic(0),
      NumberOfObjects(0) {
  ErrorAsOutParameter ErrAsOutParam(&Err);
  if (Data.getBufferSize() < sizeof(MachO::fat_header)) {
    Err = make_error<GenericBinaryError>("File too small to be a Mach-O "
                                         "universal file",
                                         object_error::invalid_file_type);
    return;
  }
  // Check for magic value and sufficient header size.
  StringRef Buf = getData();
  MachO::fat_header H =
      getUniversalBinaryStruct<MachO::fat_header>(Buf.begin());
  Magic = H.magic;
  NumberOfObjects = H.nfat_arch;
  if (NumberOfObjects == 0) {
    Err = malformedError("contains zero architecture types");
    return;
  }
  uint32_t MinSize = sizeof(MachO::fat_header);
  if (Magic == MachO::FAT_MAGIC)
    MinSize += sizeof(MachO::fat_arch) * NumberOfObjects;
  else if (Magic == MachO::FAT_MAGIC_64)
    MinSize += sizeof(MachO::fat_arch_64) * NumberOfObjects;
  else {
    Err = malformedError("bad magic number");
    return;
  }
  if (Buf.size() < MinSize) {
    Err = malformedError("fat_arch" +
                         Twine(Magic == MachO::FAT_MAGIC ? "" : "_64") +
                         " structs would extend past the end of the file");
    return;
  }
  for (uint32_t i = 0; i < NumberOfObjects; i++) {
    ObjectForArch A(this, i);
    uint64_t bigSize = A.getOffset();
    bigSize += A.getSize();
    if (bigSize > Buf.size()) {
      Err = malformedError("offset plus size of cputype (" +
        Twine(A.getCPUType()) + ") cpusubtype (" +
        Twine(A.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK) +
        ") extends past the end of the file");
      return;
    }
#define MAXSECTALIGN 15 /* 2**15 or 0x8000 */
    if (A.getAlign() > MAXSECTALIGN) {
      Err = malformedError("align (2^" + Twine(A.getAlign()) + ") too large "
        "for cputype (" + Twine(A.getCPUType()) + ") cpusubtype (" +
        Twine(A.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK) +
        ") (maximum 2^" + Twine(MAXSECTALIGN) + ")");
      return;
    }
    if(A.getOffset() % (1 << A.getAlign()) != 0){
      Err = malformedError("offset: " + Twine(A.getOffset()) +
        " for cputype (" + Twine(A.getCPUType()) + ") cpusubtype (" +
        Twine(A.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK) +
        ") not aligned on it's alignment (2^" + Twine(A.getAlign()) + ")");
      return;
    }
    if (A.getOffset() < MinSize) {
      Err =  malformedError("cputype (" + Twine(A.getCPUType()) + ") "
        "cpusubtype (" + Twine(A.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK) +
        ") offset " + Twine(A.getOffset()) + " overlaps universal headers");
      return;
    }
  }
  for (uint32_t i = 0; i < NumberOfObjects; i++) {
    ObjectForArch A(this, i);
    for (uint32_t j = i + 1; j < NumberOfObjects; j++) {
      ObjectForArch B(this, j);
      if (A.getCPUType() == B.getCPUType() &&
          (A.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK) ==
          (B.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK)) {
        Err = malformedError("contains two of the same architecture (cputype "
          "(" + Twine(A.getCPUType()) + ") cpusubtype (" +
          Twine(A.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK) + "))");
        return;
      }
      if ((A.getOffset() >= B.getOffset() &&
           A.getOffset() < B.getOffset() + B.getSize()) ||
          (A.getOffset() + A.getSize() > B.getOffset() &&
           A.getOffset() + A.getSize() < B.getOffset() + B.getSize()) ||
          (A.getOffset() <= B.getOffset() &&
           A.getOffset() + A.getSize() >= B.getOffset() + B.getSize())) {
        Err =  malformedError("cputype (" + Twine(A.getCPUType()) + ") "
          "cpusubtype (" + Twine(A.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK) +
          ") at offset " + Twine(A.getOffset()) + " with a size of " +
          Twine(A.getSize()) + ", overlaps cputype (" + Twine(B.getCPUType()) +
          ") cpusubtype (" + Twine(B.getCPUSubType() & ~MachO::CPU_SUBTYPE_MASK)
          + ") at offset " + Twine(B.getOffset()) + " with a size of "
          + Twine(B.getSize()));
        return;
      }
    }
  }
  Err = Error::success();
}

Expected<std::unique_ptr<MachOObjectFile>>
MachOUniversalBinary::getObjectForArch(StringRef ArchName) const {
  if (Triple(ArchName).getArch() == Triple::ArchType::UnknownArch)
    return make_error<GenericBinaryError>("Unknown architecture "
                                          "named: " +
                                              ArchName,
                                          object_error::arch_not_found);

  for (auto &Obj : objects())
    if (Obj.getArchFlagName() == ArchName)
      return Obj.getAsObjectFile();
  return make_error<GenericBinaryError>("fat file does not "
                                        "contain " +
                                            ArchName,
                                        object_error::arch_not_found);
}
