//===- Archive.h - ar archive file format -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ar archive file format class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ARCHIVE_H
#define LLVM_OBJECT_ARCHIVE_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace object {

class Archive;

class ArchiveMemberHeader {
public:
  friend class Archive;

  ArchiveMemberHeader(Archive const *Parent, const char *RawHeaderPtr,
                      uint64_t Size, Error *Err);
  // ArchiveMemberHeader() = default;

  /// Get the name without looking up long names.
  Expected<StringRef> getRawName() const;

  /// Get the name looking up long names.
  Expected<StringRef> getName(uint64_t Size) const;

  /// Members are not larger than 4GB.
  Expected<uint32_t> getSize() const;

  Expected<sys::fs::perms> getAccessMode() const;
  Expected<sys::TimePoint<std::chrono::seconds>> getLastModified() const;

  StringRef getRawLastModified() const {
    return StringRef(ArMemHdr->LastModified,
                     sizeof(ArMemHdr->LastModified)).rtrim(' ');
  }

  Expected<unsigned> getUID() const;
  Expected<unsigned> getGID() const;

  // This returns the size of the private struct ArMemHdrType
  uint64_t getSizeOf() const {
    return sizeof(ArMemHdrType);
  }

private:
  struct ArMemHdrType {
    char Name[16];
    char LastModified[12];
    char UID[6];
    char GID[6];
    char AccessMode[8];
    char Size[10]; ///< Size of data, not including header or padding.
    char Terminator[2];
  };
  Archive const *Parent;
  ArMemHdrType const *ArMemHdr;
};

class Archive : public Binary {
  virtual void anchor();

public:
  class Child {
    friend Archive;
    friend ArchiveMemberHeader;

    const Archive *Parent;
    ArchiveMemberHeader Header;
    /// Includes header but not padding byte.
    StringRef Data;
    /// Offset from Data to the start of the file.
    uint16_t StartOfFile;

    Expected<bool> isThinMember() const;

  public:
    Child(const Archive *Parent, const char *Start, Error *Err);
    Child(const Archive *Parent, StringRef Data, uint16_t StartOfFile);

    bool operator ==(const Child &other) const {
      assert(!Parent || !other.Parent || Parent == other.Parent);
      return Data.begin() == other.Data.begin();
    }

    const Archive *getParent() const { return Parent; }
    Expected<Child> getNext() const;

    Expected<StringRef> getName() const;
    Expected<std::string> getFullName() const;
    Expected<StringRef> getRawName() const { return Header.getRawName(); }

    Expected<sys::TimePoint<std::chrono::seconds>> getLastModified() const {
      return Header.getLastModified();
    }

    StringRef getRawLastModified() const {
      return Header.getRawLastModified();
    }

    Expected<unsigned> getUID() const { return Header.getUID(); }
    Expected<unsigned> getGID() const { return Header.getGID(); }

    Expected<sys::fs::perms> getAccessMode() const {
      return Header.getAccessMode();
    }

    /// \return the size of the archive member without the header or padding.
    Expected<uint64_t> getSize() const;
    /// \return the size in the archive header for this member.
    Expected<uint64_t> getRawSize() const;

    Expected<StringRef> getBuffer() const;
    uint64_t getChildOffset() const;

    Expected<MemoryBufferRef> getMemoryBufferRef() const;

    Expected<std::unique_ptr<Binary>>
    getAsBinary(LLVMContext *Context = nullptr) const;
  };

  class child_iterator {
    Child C;
    Error *E = nullptr;

  public:
    child_iterator() : C(Child(nullptr, nullptr, nullptr)) {}
    child_iterator(const Child &C, Error *E) : C(C), E(E) {}

    const Child *operator->() const { return &C; }
    const Child &operator*() const { return C; }

    bool operator==(const child_iterator &other) const {
      // Ignore errors here: If an error occurred during increment then getNext
      // will have been set to child_end(), and the following comparison should
      // do the right thing.
      return C == other.C;
    }

    bool operator!=(const child_iterator &other) const {
      return !(*this == other);
    }

    // Code in loops with child_iterators must check for errors on each loop
    // iteration.  And if there is an error break out of the loop.
    child_iterator &operator++() { // Preincrement
      assert(E && "Can't increment iterator with no Error attached");
      ErrorAsOutParameter ErrAsOutParam(E);
      if (auto ChildOrErr = C.getNext())
        C = *ChildOrErr;
      else {
        C = C.getParent()->child_end().C;
        *E = ChildOrErr.takeError();
        E = nullptr;
      }
      return *this;
    }
  };

  class Symbol {
    const Archive *Parent;
    uint32_t SymbolIndex;
    uint32_t StringIndex; // Extra index to the string.

  public:
    Symbol(const Archive *p, uint32_t symi, uint32_t stri)
      : Parent(p)
      , SymbolIndex(symi)
      , StringIndex(stri) {}

    bool operator ==(const Symbol &other) const {
      return (Parent == other.Parent) && (SymbolIndex == other.SymbolIndex);
    }

    StringRef getName() const;
    Expected<Child> getMember() const;
    Symbol getNext() const;
  };

  class symbol_iterator {
    Symbol symbol;

  public:
    symbol_iterator(const Symbol &s) : symbol(s) {}

    const Symbol *operator->() const { return &symbol; }
    const Symbol &operator*() const { return symbol; }

    bool operator==(const symbol_iterator &other) const {
      return symbol == other.symbol;
    }

    bool operator!=(const symbol_iterator &other) const {
      return !(*this == other);
    }

    symbol_iterator& operator++() {  // Preincrement
      symbol = symbol.getNext();
      return *this;
    }
  };

  Archive(MemoryBufferRef Source, Error &Err);
  static Expected<std::unique_ptr<Archive>> create(MemoryBufferRef Source);

  enum Kind {
    K_GNU,
    K_GNU64,
    K_BSD,
    K_DARWIN,
    K_DARWIN64,
    K_COFF
  };

  Kind kind() const { return (Kind)Format; }
  bool isThin() const { return IsThin; }

  child_iterator child_begin(Error &Err, bool SkipInternal = true) const;
  child_iterator child_end() const;
  iterator_range<child_iterator> children(Error &Err,
                                          bool SkipInternal = true) const {
    return make_range(child_begin(Err, SkipInternal), child_end());
  }

  symbol_iterator symbol_begin() const;
  symbol_iterator symbol_end() const;
  iterator_range<symbol_iterator> symbols() const {
    return make_range(symbol_begin(), symbol_end());
  }

  // Cast methods.
  static bool classof(Binary const *v) {
    return v->isArchive();
  }

  // check if a symbol is in the archive
  Expected<Optional<Child>> findSym(StringRef name) const;

  bool isEmpty() const;
  bool hasSymbolTable() const;
  StringRef getSymbolTable() const { return SymbolTable; }
  StringRef getStringTable() const { return StringTable; }
  uint32_t getNumberOfSymbols() const;

  std::vector<std::unique_ptr<MemoryBuffer>> takeThinBuffers() {
    return std::move(ThinBuffers);
  }

private:
  StringRef SymbolTable;
  StringRef StringTable;

  StringRef FirstRegularData;
  uint16_t FirstRegularStartOfFile = -1;
  void setFirstRegular(const Child &C);

  unsigned Format : 3;
  unsigned IsThin : 1;
  mutable std::vector<std::unique_ptr<MemoryBuffer>> ThinBuffers;
};

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_ARCHIVE_H
