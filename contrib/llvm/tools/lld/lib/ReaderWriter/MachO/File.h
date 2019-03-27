//===- lib/ReaderWriter/MachO/File.h ----------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_MACHO_FILE_H
#define LLD_READER_WRITER_MACHO_FILE_H

#include "Atoms.h"
#include "DebugInfo.h"
#include "MachONormalizedFile.h"
#include "lld/Core/SharedLibraryFile.h"
#include "lld/Core/Simple.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Format.h"
#include <unordered_map>

namespace lld {
namespace mach_o {

using lld::mach_o::normalized::Section;

class MachOFile : public SimpleFile {
public:

  /// Real file constructor - for on-disk files.
  MachOFile(std::unique_ptr<MemoryBuffer> mb, MachOLinkingContext *ctx)
    : SimpleFile(mb->getBufferIdentifier(), File::kindMachObject),
      _mb(std::move(mb)), _ctx(ctx) {}

  /// Dummy file constructor - for virtual files.
  MachOFile(StringRef path)
    : SimpleFile(path, File::kindMachObject) {}

  void addDefinedAtom(StringRef name, Atom::Scope scope,
                      DefinedAtom::ContentType type, DefinedAtom::Merge merge,
                      uint64_t sectionOffset, uint64_t contentSize, bool thumb,
                      bool noDeadStrip, bool copyRefs,
                      const Section *inSection) {
    assert(sectionOffset+contentSize <= inSection->content.size());
    ArrayRef<uint8_t> content = inSection->content.slice(sectionOffset,
                                                        contentSize);
    if (copyRefs) {
      // Make a copy of the atom's name and content that is owned by this file.
      name = name.copy(allocator());
      content = content.copy(allocator());
    }
    DefinedAtom::Alignment align(
        inSection->alignment,
        sectionOffset % inSection->alignment);
    auto *atom =
        new (allocator()) MachODefinedAtom(*this, name, scope, type, merge,
                                           thumb, noDeadStrip, content, align);
    addAtomForSection(inSection, atom, sectionOffset);
  }

  void addDefinedAtomInCustomSection(StringRef name, Atom::Scope scope,
                      DefinedAtom::ContentType type, DefinedAtom::Merge merge,
                      bool thumb, bool noDeadStrip, uint64_t sectionOffset,
                      uint64_t contentSize, StringRef sectionName,
                      bool copyRefs, const Section *inSection) {
    assert(sectionOffset+contentSize <= inSection->content.size());
    ArrayRef<uint8_t> content = inSection->content.slice(sectionOffset,
                                                        contentSize);
   if (copyRefs) {
      // Make a copy of the atom's name and content that is owned by this file.
      name = name.copy(allocator());
      content = content.copy(allocator());
      sectionName = sectionName.copy(allocator());
    }
    DefinedAtom::Alignment align(
        inSection->alignment,
        sectionOffset % inSection->alignment);
    auto *atom =
        new (allocator()) MachODefinedCustomSectionAtom(*this, name, scope, type,
                                                        merge, thumb,
                                                        noDeadStrip, content,
                                                        sectionName, align);
    addAtomForSection(inSection, atom, sectionOffset);
  }

  void addZeroFillDefinedAtom(StringRef name, Atom::Scope scope,
                              uint64_t sectionOffset, uint64_t size,
                              bool noDeadStrip, bool copyRefs,
                              const Section *inSection) {
    if (copyRefs) {
      // Make a copy of the atom's name and content that is owned by this file.
      name = name.copy(allocator());
    }
    DefinedAtom::Alignment align(
        inSection->alignment,
        sectionOffset % inSection->alignment);

    DefinedAtom::ContentType type = DefinedAtom::typeUnknown;
    switch (inSection->type) {
    case llvm::MachO::S_ZEROFILL:
      type = DefinedAtom::typeZeroFill;
      break;
    case llvm::MachO::S_THREAD_LOCAL_ZEROFILL:
      type = DefinedAtom::typeTLVInitialZeroFill;
      break;
    default:
      llvm_unreachable("Unrecognized zero-fill section");
    }

    auto *atom =
        new (allocator()) MachODefinedAtom(*this, name, scope, type, size,
                                           noDeadStrip, align);
    addAtomForSection(inSection, atom, sectionOffset);
  }

  void addUndefinedAtom(StringRef name, bool copyRefs) {
    if (copyRefs) {
      // Make a copy of the atom's name that is owned by this file.
      name = name.copy(allocator());
    }
    auto *atom = new (allocator()) SimpleUndefinedAtom(*this, name);
    addAtom(*atom);
    _undefAtoms[name] = atom;
  }

  void addTentativeDefAtom(StringRef name, Atom::Scope scope, uint64_t size,
                           DefinedAtom::Alignment align, bool copyRefs) {
    if (copyRefs) {
      // Make a copy of the atom's name that is owned by this file.
      name = name.copy(allocator());
    }
    auto *atom =
        new (allocator()) MachOTentativeDefAtom(*this, name, scope, size, align);
    addAtom(*atom);
    _undefAtoms[name] = atom;
  }

  /// Search this file for an the atom from 'section' that covers
  /// 'offsetInSect'.  Returns nullptr is no atom found.
  MachODefinedAtom *findAtomCoveringAddress(const Section &section,
                                            uint64_t offsetInSect,
                                            uint32_t *foundOffsetAtom=nullptr) {
    const auto &pos = _sectionAtoms.find(&section);
    if (pos == _sectionAtoms.end())
      return nullptr;
    const auto &vec = pos->second;
    assert(offsetInSect < section.content.size());
    // Vector of atoms for section are already sorted, so do binary search.
    const auto &atomPos = std::lower_bound(vec.begin(), vec.end(), offsetInSect,
        [offsetInSect](const SectionOffsetAndAtom &ao,
                       uint64_t targetAddr) -> bool {
          // Each atom has a start offset of its slice of the
          // section's content. This compare function must return true
          // iff the atom's range is before the offset being searched for.
          uint64_t atomsEndOffset = ao.offset+ao.atom->rawContent().size();
          return (atomsEndOffset <= offsetInSect);
        });
    if (atomPos == vec.end())
      return nullptr;
    if (foundOffsetAtom)
      *foundOffsetAtom = offsetInSect - atomPos->offset;
    return atomPos->atom;
  }

  /// Searches this file for an UndefinedAtom named 'name'. Returns
  /// nullptr is no such atom found.
  const lld::Atom *findUndefAtom(StringRef name) {
    auto pos = _undefAtoms.find(name);
    if (pos == _undefAtoms.end())
      return nullptr;
    return pos->second;
  }

  typedef std::function<void (MachODefinedAtom* atom)> DefinedAtomVisitor;

  void eachDefinedAtom(DefinedAtomVisitor vistor) {
    for (auto &sectAndAtoms : _sectionAtoms) {
      for (auto &offAndAtom : sectAndAtoms.second) {
        vistor(offAndAtom.atom);
      }
    }
  }

  typedef std::function<void(MachODefinedAtom *atom, uint64_t offset)>
      SectionAtomVisitor;

  void eachAtomInSection(const Section &section, SectionAtomVisitor visitor) {
    auto pos = _sectionAtoms.find(&section);
    if (pos == _sectionAtoms.end())
      return;
    auto vec = pos->second;

    for (auto &offAndAtom : vec)
      visitor(offAndAtom.atom, offAndAtom.offset);
  }

  MachOLinkingContext::Arch arch() const { return _arch; }
  void setArch(MachOLinkingContext::Arch arch) { _arch = arch; }

  MachOLinkingContext::OS OS() const { return _os; }
  void setOS(MachOLinkingContext::OS os) { _os = os; }

  MachOLinkingContext::ObjCConstraint objcConstraint() const {
    return _objcConstraint;
  }
  void setObjcConstraint(MachOLinkingContext::ObjCConstraint v) {
    _objcConstraint = v;
  }

  uint32_t minVersion() const { return _minVersion; }
  void setMinVersion(uint32_t v) { _minVersion = v; }

  LoadCommandType minVersionLoadCommandKind() const {
    return _minVersionLoadCommandKind;
  }
  void setMinVersionLoadCommandKind(LoadCommandType v) {
    _minVersionLoadCommandKind = v;
  }

  uint32_t swiftVersion() const { return _swiftVersion; }
  void setSwiftVersion(uint32_t v) { _swiftVersion = v; }

  bool subsectionsViaSymbols() const {
    return _flags & llvm::MachO::MH_SUBSECTIONS_VIA_SYMBOLS;
  }
  void setFlags(normalized::FileFlags v) { _flags = v; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const File *F) {
    return F->kind() == File::kindMachObject;
  }

  void setDebugInfo(std::unique_ptr<DebugInfo> debugInfo) {
    _debugInfo = std::move(debugInfo);
  }

  DebugInfo* debugInfo() const { return _debugInfo.get(); }
  std::unique_ptr<DebugInfo> takeDebugInfo() { return std::move(_debugInfo); }

protected:
  std::error_code doParse() override {
    // Convert binary file to normalized mach-o.
    auto normFile = normalized::readBinary(_mb, _ctx->arch());
    if (auto ec = normFile.takeError())
      return llvm::errorToErrorCode(std::move(ec));
    // Convert normalized mach-o to atoms.
    if (auto ec = normalized::normalizedObjectToAtoms(this, **normFile, false))
      return llvm::errorToErrorCode(std::move(ec));
    return std::error_code();
  }

private:
  struct SectionOffsetAndAtom { uint64_t offset;  MachODefinedAtom *atom; };

  void addAtomForSection(const Section *inSection, MachODefinedAtom* atom,
                         uint64_t sectionOffset) {
    SectionOffsetAndAtom offAndAtom;
    offAndAtom.offset = sectionOffset;
    offAndAtom.atom   = atom;
     _sectionAtoms[inSection].push_back(offAndAtom);
    addAtom(*atom);
  }

  typedef llvm::DenseMap<const normalized::Section *,
                         std::vector<SectionOffsetAndAtom>>  SectionToAtoms;
  typedef llvm::StringMap<const lld::Atom *> NameToAtom;

  std::unique_ptr<MemoryBuffer> _mb;
  MachOLinkingContext          *_ctx;
  SectionToAtoms                _sectionAtoms;
  NameToAtom                     _undefAtoms;
  MachOLinkingContext::Arch      _arch = MachOLinkingContext::arch_unknown;
  MachOLinkingContext::OS        _os = MachOLinkingContext::OS::unknown;
  uint32_t                       _minVersion = 0;
  LoadCommandType               _minVersionLoadCommandKind = (LoadCommandType)0;
  MachOLinkingContext::ObjCConstraint _objcConstraint =
      MachOLinkingContext::objc_unknown;
  uint32_t                       _swiftVersion = 0;
  normalized::FileFlags        _flags = llvm::MachO::MH_SUBSECTIONS_VIA_SYMBOLS;
  std::unique_ptr<DebugInfo>   _debugInfo;
};

class MachODylibFile : public SharedLibraryFile {
public:
  MachODylibFile(std::unique_ptr<MemoryBuffer> mb, MachOLinkingContext *ctx)
      : SharedLibraryFile(mb->getBufferIdentifier()),
        _mb(std::move(mb)), _ctx(ctx) {}

  MachODylibFile(StringRef path) : SharedLibraryFile(path) {}

  OwningAtomPtr<SharedLibraryAtom> exports(StringRef name) const override {
    // Pass down _installName so that if this requested symbol
    // is re-exported through this dylib, the SharedLibraryAtom's loadName()
    // is this dylib installName and not the implementation dylib's.
    // NOTE: isData is not needed for dylibs (it matters for static libs).
    return exports(name, _installName);
  }

  /// Adds symbol name that this dylib exports. The corresponding
  /// SharedLibraryAtom is created lazily (since most symbols are not used).
  void addExportedSymbol(StringRef name, bool weakDef, bool copyRefs) {
    if (copyRefs) {
      name = name.copy(allocator());
    }
    AtomAndFlags info(weakDef);
    _nameToAtom[name] = info;
  }

  void addReExportedDylib(StringRef dylibPath) {
    _reExportedDylibs.emplace_back(dylibPath);
  }

  StringRef installName() const { return _installName; }
  uint32_t currentVersion() { return _currentVersion; }
  uint32_t compatVersion() { return _compatVersion; }

  void setInstallName(StringRef name) { _installName = name; }
  void setCompatVersion(uint32_t version) { _compatVersion = version; }
  void setCurrentVersion(uint32_t version) { _currentVersion = version; }

  typedef std::function<MachODylibFile *(StringRef)> FindDylib;

  void loadReExportedDylibs(FindDylib find) {
    for (ReExportedDylib &entry : _reExportedDylibs) {
      entry.file = find(entry.path);
    }
  }

  StringRef getDSOName() const override { return _installName; }

  std::error_code doParse() override {
    // Convert binary file to normalized mach-o.
    auto normFile = normalized::readBinary(_mb, _ctx->arch());
    if (auto ec = normFile.takeError())
      return llvm::errorToErrorCode(std::move(ec));
    // Convert normalized mach-o to atoms.
    if (auto ec = normalized::normalizedDylibToAtoms(this, **normFile, false))
      return llvm::errorToErrorCode(std::move(ec));
    return std::error_code();
  }

private:
  OwningAtomPtr<SharedLibraryAtom> exports(StringRef name,
                                   StringRef installName) const {
    // First, check if requested symbol is directly implemented by this dylib.
    auto entry = _nameToAtom.find(name);
    if (entry != _nameToAtom.end()) {
      // FIXME: Make this map a set and only used in assert builds.
      // Note, its safe to assert here as the resolver is the only client of
      // this API and it only requests exports for undefined symbols.
      // If we return from here we are no longer undefined so we should never
      // get here again.
      assert(!entry->second.atom && "Duplicate shared library export");
      bool weakDef = entry->second.weakDef;
      auto *atom = new (allocator()) MachOSharedLibraryAtom(*this, name,
                                                            installName,
                                                            weakDef);
      entry->second.atom = atom;
      return atom;
    }

    // Next, check if symbol is implemented in some re-exported dylib.
    for (const ReExportedDylib &dylib : _reExportedDylibs) {
      assert(dylib.file);
      auto atom = dylib.file->exports(name, installName);
      if (atom.get())
        return atom;
    }

    // Symbol not exported or re-exported by this dylib.
    return nullptr;
  }

  struct ReExportedDylib {
    ReExportedDylib(StringRef p) : path(p), file(nullptr) { }
    StringRef       path;
    MachODylibFile *file;
  };

  struct AtomAndFlags {
    AtomAndFlags() : atom(nullptr), weakDef(false) { }
    AtomAndFlags(bool weak) : atom(nullptr), weakDef(weak) { }
    const SharedLibraryAtom  *atom;
    bool                      weakDef;
  };

  std::unique_ptr<MemoryBuffer>              _mb;
  MachOLinkingContext                       *_ctx;
  StringRef                                  _installName;
  uint32_t                                   _currentVersion;
  uint32_t                                   _compatVersion;
  std::vector<ReExportedDylib>               _reExportedDylibs;
  mutable std::unordered_map<StringRef, AtomAndFlags> _nameToAtom;
};

} // end namespace mach_o
} // end namespace lld

#endif // LLD_READER_WRITER_MACHO_FILE_H
