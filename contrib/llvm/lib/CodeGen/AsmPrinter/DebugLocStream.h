//===--- lib/CodeGen/DebugLocStream.h - DWARF debug_loc stream --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DEBUGLOCSTREAM_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DEBUGLOCSTREAM_H

#include "ByteStreamer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

class AsmPrinter;
class DbgVariable;
class DwarfCompileUnit;
class MachineInstr;
class MCSymbol;

/// Byte stream of .debug_loc entries.
///
/// Stores a unified stream of .debug_loc entries.  There's \a List for each
/// variable/inlined-at pair, and an \a Entry for each \a DebugLocEntry.
///
/// FIXME: Do we need all these temp symbols?
/// FIXME: Why not output directly to the output stream?
class DebugLocStream {
public:
  struct List {
    DwarfCompileUnit *CU;
    MCSymbol *Label = nullptr;
    size_t EntryOffset;
    List(DwarfCompileUnit *CU, size_t EntryOffset)
        : CU(CU), EntryOffset(EntryOffset) {}
  };
  struct Entry {
    const MCSymbol *BeginSym;
    const MCSymbol *EndSym;
    size_t ByteOffset;
    size_t CommentOffset;
    Entry(const MCSymbol *BeginSym, const MCSymbol *EndSym, size_t ByteOffset,
          size_t CommentOffset)
        : BeginSym(BeginSym), EndSym(EndSym), ByteOffset(ByteOffset),
          CommentOffset(CommentOffset) {}
  };

private:
  SmallVector<List, 4> Lists;
  SmallVector<Entry, 32> Entries;
  SmallString<256> DWARFBytes;
  SmallVector<std::string, 32> Comments;

  /// Only verbose textual output needs comments.  This will be set to
  /// true for that case, and false otherwise.
  bool GenerateComments;

public:
  DebugLocStream(bool GenerateComments) : GenerateComments(GenerateComments) { }
  size_t getNumLists() const { return Lists.size(); }
  const List &getList(size_t LI) const { return Lists[LI]; }
  ArrayRef<List> getLists() const { return Lists; }

  class ListBuilder;
  class EntryBuilder;

private:
  /// Start a new .debug_loc entry list.
  ///
  /// Start a new .debug_loc entry list.  Return the new list's index so it can
  /// be retrieved later via \a getList().
  ///
  /// Until the next call, \a startEntry() will add entries to this list.
  size_t startList(DwarfCompileUnit *CU) {
    size_t LI = Lists.size();
    Lists.emplace_back(CU, Entries.size());
    return LI;
  }

  /// Finalize a .debug_loc entry list.
  ///
  /// If there are no entries in this list, delete it outright.  Otherwise,
  /// create a label with \a Asm.
  ///
  /// \return false iff the list is deleted.
  bool finalizeList(AsmPrinter &Asm);

  /// Start a new .debug_loc entry.
  ///
  /// Until the next call, bytes added to the stream will be added to this
  /// entry.
  void startEntry(const MCSymbol *BeginSym, const MCSymbol *EndSym) {
    Entries.emplace_back(BeginSym, EndSym, DWARFBytes.size(), Comments.size());
  }

  /// Finalize a .debug_loc entry, deleting if it's empty.
  void finalizeEntry();

public:
  BufferByteStreamer getStreamer() {
    return BufferByteStreamer(DWARFBytes, Comments, GenerateComments);
  }

  ArrayRef<Entry> getEntries(const List &L) const {
    size_t LI = getIndex(L);
    return makeArrayRef(Entries)
        .slice(Lists[LI].EntryOffset, getNumEntries(LI));
  }

  ArrayRef<char> getBytes(const Entry &E) const {
    size_t EI = getIndex(E);
    return makeArrayRef(DWARFBytes.begin(), DWARFBytes.end())
        .slice(Entries[EI].ByteOffset, getNumBytes(EI));
  }
  ArrayRef<std::string> getComments(const Entry &E) const {
    size_t EI = getIndex(E);
    return makeArrayRef(Comments)
        .slice(Entries[EI].CommentOffset, getNumComments(EI));
  }

private:
  size_t getIndex(const List &L) const {
    assert(&Lists.front() <= &L && &L <= &Lists.back() &&
           "Expected valid list");
    return &L - &Lists.front();
  }
  size_t getIndex(const Entry &E) const {
    assert(&Entries.front() <= &E && &E <= &Entries.back() &&
           "Expected valid entry");
    return &E - &Entries.front();
  }
  size_t getNumEntries(size_t LI) const {
    if (LI + 1 == Lists.size())
      return Entries.size() - Lists[LI].EntryOffset;
    return Lists[LI + 1].EntryOffset - Lists[LI].EntryOffset;
  }
  size_t getNumBytes(size_t EI) const {
    if (EI + 1 == Entries.size())
      return DWARFBytes.size() - Entries[EI].ByteOffset;
    return Entries[EI + 1].ByteOffset - Entries[EI].ByteOffset;
  }
  size_t getNumComments(size_t EI) const {
    if (EI + 1 == Entries.size())
      return Comments.size() - Entries[EI].CommentOffset;
    return Entries[EI + 1].CommentOffset - Entries[EI].CommentOffset;
  }
};

/// Builder for DebugLocStream lists.
class DebugLocStream::ListBuilder {
  DebugLocStream &Locs;
  AsmPrinter &Asm;
  DbgVariable &V;
  const MachineInstr &MI;
  size_t ListIndex;

public:
  ListBuilder(DebugLocStream &Locs, DwarfCompileUnit &CU, AsmPrinter &Asm,
              DbgVariable &V, const MachineInstr &MI)
      : Locs(Locs), Asm(Asm), V(V), MI(MI), ListIndex(Locs.startList(&CU)) {}

  /// Finalize the list.
  ///
  /// If the list is empty, delete it.  Otherwise, finalize it by creating a
  /// temp symbol in \a Asm and setting up the \a DbgVariable.
  ~ListBuilder();

  DebugLocStream &getLocs() { return Locs; }
};

/// Builder for DebugLocStream entries.
class DebugLocStream::EntryBuilder {
  DebugLocStream &Locs;

public:
  EntryBuilder(ListBuilder &List, const MCSymbol *Begin, const MCSymbol *End)
      : Locs(List.getLocs()) {
    Locs.startEntry(Begin, End);
  }

  /// Finalize the entry, deleting it if it's empty.
  ~EntryBuilder() { Locs.finalizeEntry(); }

  BufferByteStreamer getStreamer() { return Locs.getStreamer(); }
};

} // namespace llvm

#endif
