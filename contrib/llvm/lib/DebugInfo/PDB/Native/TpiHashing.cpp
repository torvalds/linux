//===- TpiHashing.cpp -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/TpiHashing.h"

#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/PDB/Native/Hash.h"
#include "llvm/Support/JamCRC.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

// Corresponds to `fUDTAnon`.
static bool isAnonymous(StringRef Name) {
  return Name == "<unnamed-tag>" || Name == "__unnamed" ||
         Name.endswith("::<unnamed-tag>") || Name.endswith("::__unnamed");
}

// Computes the hash for a user-defined type record. This could be a struct,
// class, union, or enum.
static uint32_t getHashForUdt(const TagRecord &Rec,
                              ArrayRef<uint8_t> FullRecord) {
  ClassOptions Opts = Rec.getOptions();
  bool ForwardRef = bool(Opts & ClassOptions::ForwardReference);
  bool Scoped = bool(Opts & ClassOptions::Scoped);
  bool HasUniqueName = bool(Opts & ClassOptions::HasUniqueName);
  bool IsAnon = HasUniqueName && isAnonymous(Rec.getName());

  if (!ForwardRef && !Scoped && !IsAnon)
    return hashStringV1(Rec.getName());
  if (!ForwardRef && HasUniqueName && !IsAnon)
    return hashStringV1(Rec.getUniqueName());
  return hashBufferV8(FullRecord);
}

template <typename T>
static Expected<uint32_t> getHashForUdt(const CVType &Rec) {
  T Deserialized;
  if (auto E = TypeDeserializer::deserializeAs(const_cast<CVType &>(Rec),
                                               Deserialized))
    return std::move(E);
  return getHashForUdt(Deserialized, Rec.data());
}

template <typename T>
static Expected<TagRecordHash> getTagRecordHashForUdt(const CVType &Rec) {
  T Deserialized;
  if (auto E = TypeDeserializer::deserializeAs(const_cast<CVType &>(Rec),
                                               Deserialized))
    return std::move(E);

  ClassOptions Opts = Deserialized.getOptions();

  bool ForwardRef = bool(Opts & ClassOptions::ForwardReference);

  uint32_t ThisRecordHash = getHashForUdt(Deserialized, Rec.data());

  // If we don't have a forward ref we can't compute the hash of it from the
  // full record because it requires hashing the entire buffer.
  if (!ForwardRef)
    return TagRecordHash{std::move(Deserialized), ThisRecordHash, 0};

  bool Scoped = bool(Opts & ClassOptions::Scoped);

  StringRef NameToHash =
      Scoped ? Deserialized.getUniqueName() : Deserialized.getName();
  uint32_t FullHash = hashStringV1(NameToHash);
  return TagRecordHash{std::move(Deserialized), FullHash, ThisRecordHash};
}

template <typename T>
static Expected<uint32_t> getSourceLineHash(const CVType &Rec) {
  T Deserialized;
  if (auto E = TypeDeserializer::deserializeAs(const_cast<CVType &>(Rec),
                                               Deserialized))
    return std::move(E);
  char Buf[4];
  support::endian::write32le(Buf, Deserialized.getUDT().getIndex());
  return hashStringV1(StringRef(Buf, 4));
}

Expected<TagRecordHash> llvm::pdb::hashTagRecord(const codeview::CVType &Type) {
  switch (Type.kind()) {
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_INTERFACE:
    return getTagRecordHashForUdt<ClassRecord>(Type);
  case LF_UNION:
    return getTagRecordHashForUdt<UnionRecord>(Type);
  case LF_ENUM:
    return getTagRecordHashForUdt<EnumRecord>(Type);
  default:
    assert(false && "Type is not a tag record!");
  }
  return make_error<StringError>("Invalid record type",
                                 inconvertibleErrorCode());
}

Expected<uint32_t> llvm::pdb::hashTypeRecord(const CVType &Rec) {
  switch (Rec.kind()) {
  case LF_CLASS:
  case LF_STRUCTURE:
  case LF_INTERFACE:
    return getHashForUdt<ClassRecord>(Rec);
  case LF_UNION:
    return getHashForUdt<UnionRecord>(Rec);
  case LF_ENUM:
    return getHashForUdt<EnumRecord>(Rec);

  case LF_UDT_SRC_LINE:
    return getSourceLineHash<UdtSourceLineRecord>(Rec);
  case LF_UDT_MOD_SRC_LINE:
    return getSourceLineHash<UdtModSourceLineRecord>(Rec);

  default:
    break;
  }

  // Run CRC32 over the bytes. This corresponds to `hashBufv8`.
  JamCRC JC(/*Init=*/0U);
  ArrayRef<char> Bytes(reinterpret_cast<const char *>(Rec.data().data()),
                       Rec.data().size());
  JC.update(Bytes);
  return JC.getCRC();
}
