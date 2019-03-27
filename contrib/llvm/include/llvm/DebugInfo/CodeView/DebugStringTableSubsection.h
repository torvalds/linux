//===- DebugStringTableSubsection.h - CodeView String Table -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSTRINGTABLESUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSTRINGTABLESUBSECTION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

class BinaryStreamReader;

namespace codeview {

/// Represents a read-only view of a CodeView string table.  This is a very
/// simple flat buffer consisting of null-terminated strings, where strings
/// are retrieved by their offset in the buffer.  DebugStringTableSubsectionRef
/// does not own the underlying storage for the buffer.
class DebugStringTableSubsectionRef : public DebugSubsectionRef {
public:
  DebugStringTableSubsectionRef();

  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::StringTable;
  }

  Error initialize(BinaryStreamRef Contents);
  Error initialize(BinaryStreamReader &Reader);

  Expected<StringRef> getString(uint32_t Offset) const;

  bool valid() const { return Stream.valid(); }

  BinaryStreamRef getBuffer() const { return Stream; }

private:
  BinaryStreamRef Stream;
};

/// Represents a read-write view of a CodeView string table.
/// DebugStringTableSubsection owns the underlying storage for the table, and is
/// capable of serializing the string table into a format understood by
/// DebugStringTableSubsectionRef.
class DebugStringTableSubsection : public DebugSubsection {
public:
  DebugStringTableSubsection();

  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::StringTable;
  }

  // If string S does not exist in the string table, insert it.
  // Returns the ID for S.
  uint32_t insert(StringRef S);

  // Return the ID for string S.  Assumes S exists in the table.
  uint32_t getIdForString(StringRef S) const;

  StringRef getStringForId(uint32_t Id) const;

  uint32_t calculateSerializedSize() const override;
  Error commit(BinaryStreamWriter &Writer) const override;

  uint32_t size() const;

  StringMap<uint32_t>::const_iterator begin() const {
    return StringToId.begin();
  }

  StringMap<uint32_t>::const_iterator end() const { return StringToId.end(); }

  std::vector<uint32_t> sortedIds() const;

private:
  DenseMap<uint32_t, StringRef> IdToString;
  StringMap<uint32_t> StringToId;
  uint32_t StringSize = 1;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGSTRINGTABLESUBSECTION_H
