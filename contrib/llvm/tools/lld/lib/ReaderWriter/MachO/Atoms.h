//===- lib/ReaderWriter/MachO/Atoms.h ---------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_MACHO_ATOMS_H
#define LLD_READER_WRITER_MACHO_ATOMS_H

#include "lld/Core/Atom.h"
#include "lld/Core/DefinedAtom.h"
#include "lld/Core/SharedLibraryAtom.h"
#include "lld/Core/Simple.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <string>

namespace lld {

class File;

namespace mach_o {

class MachODefinedAtom : public SimpleDefinedAtom {
public:
  MachODefinedAtom(const File &f, const StringRef name, Scope scope,
                   ContentType type, Merge merge, bool thumb, bool noDeadStrip,
                   const ArrayRef<uint8_t> content, Alignment align)
      : SimpleDefinedAtom(f), _name(name), _content(content),
        _align(align), _contentType(type), _scope(scope), _merge(merge),
        _thumb(thumb), _noDeadStrip(noDeadStrip) {}

  // Constructor for zero-fill content
  MachODefinedAtom(const File &f, const StringRef name, Scope scope,
                   ContentType type, uint64_t size, bool noDeadStrip,
                   Alignment align)
      : SimpleDefinedAtom(f), _name(name),
        _content(ArrayRef<uint8_t>(nullptr, size)), _align(align),
        _contentType(type), _scope(scope), _merge(mergeNo), _thumb(false),
        _noDeadStrip(noDeadStrip) {}

  ~MachODefinedAtom() override = default;

  uint64_t size() const override { return _content.size(); }

  ContentType contentType() const override { return _contentType; }

  Alignment alignment() const override { return _align; }

  StringRef name() const override { return _name; }

  Scope scope() const override { return _scope; }

  Merge merge() const override { return _merge; }

  DeadStripKind deadStrip() const override {
    if (_contentType == DefinedAtom::typeInitializerPtr)
      return deadStripNever;
    if (_contentType == DefinedAtom::typeTerminatorPtr)
      return deadStripNever;
    if (_noDeadStrip)
      return deadStripNever;
    return deadStripNormal;
  }

  ArrayRef<uint8_t> rawContent() const override {
    // Note: Zerofill atoms have a content pointer which is null.
    return _content;
  }

  bool isThumb() const { return _thumb; }

private:
  const StringRef _name;
  const ArrayRef<uint8_t> _content;
  const DefinedAtom::Alignment _align;
  const ContentType _contentType;
  const Scope _scope;
  const Merge _merge;
  const bool _thumb;
  const bool _noDeadStrip;
};

class MachODefinedCustomSectionAtom : public MachODefinedAtom {
public:
  MachODefinedCustomSectionAtom(const File &f, const StringRef name,
                                Scope scope, ContentType type, Merge merge,
                                bool thumb, bool noDeadStrip,
                                const ArrayRef<uint8_t> content,
                                StringRef sectionName, Alignment align)
      : MachODefinedAtom(f, name, scope, type, merge, thumb, noDeadStrip,
                         content, align),
        _sectionName(sectionName) {}

  ~MachODefinedCustomSectionAtom() override = default;

  SectionChoice sectionChoice() const override {
    return DefinedAtom::sectionCustomRequired;
  }

  StringRef customSectionName() const override {
    return _sectionName;
  }
private:
  StringRef _sectionName;
};

class MachOTentativeDefAtom : public SimpleDefinedAtom {
public:
  MachOTentativeDefAtom(const File &f, const StringRef name, Scope scope,
                        uint64_t size, DefinedAtom::Alignment align)
      : SimpleDefinedAtom(f), _name(name), _scope(scope), _size(size),
        _align(align) {}

  ~MachOTentativeDefAtom() override = default;

  uint64_t size() const override { return _size; }

  Merge merge() const override { return DefinedAtom::mergeAsTentative; }

  ContentType contentType() const override { return DefinedAtom::typeZeroFill; }

  Alignment alignment() const override { return _align; }

  StringRef name() const override { return _name; }

  Scope scope() const override { return _scope; }

  ArrayRef<uint8_t> rawContent() const override { return ArrayRef<uint8_t>(); }

private:
  const std::string _name;
  const Scope _scope;
  const uint64_t _size;
  const DefinedAtom::Alignment _align;
};

class MachOSharedLibraryAtom : public SharedLibraryAtom {
public:
  MachOSharedLibraryAtom(const File &file, StringRef name,
                         StringRef dylibInstallName, bool weakDef)
      : SharedLibraryAtom(), _file(file), _name(name),
        _dylibInstallName(dylibInstallName) {}
  ~MachOSharedLibraryAtom() override = default;

  StringRef loadName() const override { return _dylibInstallName; }

  bool canBeNullAtRuntime() const override {
    // FIXME: this may actually be changeable. For now, all symbols are strongly
    // defined though.
    return false;
  }

  const File &file() const override { return _file; }

  StringRef name() const override { return _name; }

  Type type() const override {
    // Unused in MachO (I think).
    return Type::Unknown;
  }

  uint64_t size() const override {
    // Unused in MachO (I think)
    return 0;
  }

private:
  const File &_file;
  StringRef _name;
  StringRef _dylibInstallName;
};

} // end namespace mach_o
} // end namespace lld

#endif // LLD_READER_WRITER_MACHO_ATOMS_H
