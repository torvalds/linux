//===- lib/ReaderWriter/MachO/File.h ----------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_MACHO_DEBUGINFO_H
#define LLD_READER_WRITER_MACHO_DEBUGINFO_H

#include "lld/Core/Atom.h"
#include <vector>

#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"


namespace lld {
namespace mach_o {

class DebugInfo {
public:
  enum class Kind {
    Dwarf,
    Stabs
  };

  Kind kind() const { return _kind; }

  void setAllocator(std::unique_ptr<llvm::BumpPtrAllocator> allocator) {
    _allocator = std::move(allocator);
  }

protected:
  DebugInfo(Kind kind) : _kind(kind) {}

private:
  std::unique_ptr<llvm::BumpPtrAllocator> _allocator;
  Kind _kind;
};

struct TranslationUnitSource {
  StringRef name;
  StringRef path;
};

class DwarfDebugInfo : public DebugInfo {
public:
  DwarfDebugInfo(TranslationUnitSource tu)
    : DebugInfo(Kind::Dwarf), _tu(std::move(tu)) {}

  static inline bool classof(const DebugInfo *di) {
    return di->kind() == Kind::Dwarf;
  }

  const TranslationUnitSource &translationUnitSource() const { return _tu; }

private:
  TranslationUnitSource _tu;
};

struct Stab {
  Stab(const Atom* atom, uint8_t type, uint8_t other, uint16_t desc,
       uint32_t value, StringRef str)
    : atom(atom), type(type), other(other), desc(desc), value(value),
      str(str) {}

  const class Atom*   atom;
  uint8_t             type;
  uint8_t             other;
  uint16_t            desc;
  uint32_t            value;
  StringRef           str;
};

inline raw_ostream& operator<<(raw_ostream &os, Stab &s) {
  os << "Stab -- atom: " << llvm::format("%p", s.atom) << ", type: " << (uint32_t)s.type
     << ", other: " << (uint32_t)s.other << ", desc: " << s.desc << ", value: " << s.value
     << ", str: '" << s.str << "'";
  return os;
}

class StabsDebugInfo : public DebugInfo {
public:

  typedef std::vector<Stab> StabsList;

  StabsDebugInfo(StabsList stabs)
    : DebugInfo(Kind::Stabs), _stabs(std::move(stabs)) {}

  static inline bool classof(const DebugInfo *di) {
    return di->kind() == Kind::Stabs;
  }

  const StabsList& stabs() const { return _stabs; }

public:
  StabsList _stabs;
};

} // end namespace mach_o
} // end namespace lld

#endif // LLD_READER_WRITER_MACHO_DEBUGINFO_H
