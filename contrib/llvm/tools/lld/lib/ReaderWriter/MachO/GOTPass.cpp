//===- lib/ReaderWriter/MachO/GOTPass.cpp -----------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This linker pass transforms all GOT kind references to real references.
/// That is, in assembly you can write something like:
///     movq foo@GOTPCREL(%rip), %rax
/// which means you want to load a pointer to "foo" out of the GOT (global
/// Offsets Table). In the object file, the Atom containing this instruction
/// has a Reference whose target is an Atom named "foo" and the Reference
/// kind is a GOT load.  The linker needs to instantiate a pointer sized
/// GOT entry.  This is done be creating a GOT Atom to represent that pointer
/// sized data in this pass, and altering the Atom graph so the Reference now
/// points to the GOT Atom entry (corresponding to "foo") and changing the
/// Reference Kind to reflect it is now pointing to a GOT entry (rather
/// then needing a GOT entry).
///
/// There is one optimization the linker can do here.  If the target of the GOT
/// is in the same linkage unit and does not need to be interposable, and
/// the GOT use is just a load (not some other operation), this pass can
/// transform that load into an LEA (add).  This optimizes away one memory load
/// which at runtime that could stall the pipeline.  This optimization only
/// works for architectures in which a (GOT) load instruction can be change to
/// an LEA instruction that is the same size.  The method isGOTAccess() should
/// only return true for "canBypassGOT" if this optimization is supported.
///
//===----------------------------------------------------------------------===//

#include "ArchHandler.h"
#include "File.h"
#include "MachOPasses.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/DefinedAtom.h"
#include "lld/Core/File.h"
#include "lld/Core/Reference.h"
#include "lld/Core/Simple.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

namespace lld {
namespace mach_o {

//
//  GOT Entry Atom created by the GOT pass.
//
class GOTEntryAtom : public SimpleDefinedAtom {
public:
  GOTEntryAtom(const File &file, bool is64, StringRef name)
    : SimpleDefinedAtom(file), _is64(is64), _name(name) { }

  ~GOTEntryAtom() override = default;

  ContentType contentType() const override {
    return DefinedAtom::typeGOT;
  }

  Alignment alignment() const override {
    return _is64 ? 8 : 4;
  }

  uint64_t size() const override {
    return _is64 ? 8 : 4;
  }

  ContentPermissions permissions() const override {
    return DefinedAtom::permRW_;
  }

  ArrayRef<uint8_t> rawContent() const override {
    static const uint8_t zeros[] =
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    return llvm::makeArrayRef(zeros, size());
  }

  StringRef slotName() const {
    return _name;
  }

private:
  const bool _is64;
  StringRef _name;
};

/// Pass for instantiating and optimizing GOT slots.
///
class GOTPass : public Pass {
public:
  GOTPass(const MachOLinkingContext &context)
      : _ctx(context), _archHandler(_ctx.archHandler()),
        _file(*_ctx.make_file<MachOFile>("<mach-o GOT Pass>")) {
    _file.setOrdinal(_ctx.getNextOrdinalAndIncrement());
  }

private:
  llvm::Error perform(SimpleFile &mergedFile) override {
    // Scan all references in all atoms.
    for (const DefinedAtom *atom : mergedFile.defined()) {
      for (const Reference *ref : *atom) {
        // Look at instructions accessing the GOT.
        bool canBypassGOT;
        if (!_archHandler.isGOTAccess(*ref, canBypassGOT))
          continue;
        const Atom *target = ref->target();
        assert(target != nullptr);

        if (!shouldReplaceTargetWithGOTAtom(target, canBypassGOT)) {
          // Update reference kind to reflect that target is a direct accesss.
          _archHandler.updateReferenceToGOT(ref, false);
        } else {
          // Replace the target with a reference to a GOT entry.
          const DefinedAtom *gotEntry = makeGOTEntry(target);
          const_cast<Reference *>(ref)->setTarget(gotEntry);
          // Update reference kind to reflect that target is now a GOT entry.
          _archHandler.updateReferenceToGOT(ref, true);
        }
      }
    }

    // Sort and add all created GOT Atoms to master file
    std::vector<const GOTEntryAtom *> entries;
    entries.reserve(_targetToGOT.size());
    for (auto &it : _targetToGOT)
      entries.push_back(it.second);
    std::sort(entries.begin(), entries.end(),
              [](const GOTEntryAtom *left, const GOTEntryAtom *right) {
      return (left->slotName().compare(right->slotName()) < 0);
    });
    for (const GOTEntryAtom *slot : entries)
      mergedFile.addAtom(*slot);

    return llvm::Error::success();
  }

  bool shouldReplaceTargetWithGOTAtom(const Atom *target, bool canBypassGOT) {
    // Accesses to shared library symbols must go through GOT.
    if (isa<SharedLibraryAtom>(target))
      return true;
    // Accesses to interposable symbols in same linkage unit must also go
    // through GOT.
    const DefinedAtom *defTarget = dyn_cast<DefinedAtom>(target);
    if (defTarget != nullptr &&
        defTarget->interposable() != DefinedAtom::interposeNo) {
      assert(defTarget->scope() != DefinedAtom::scopeTranslationUnit);
      return true;
    }
    // Target does not require indirection.  So, if instruction allows GOT to be
    // by-passed, do that optimization and don't create GOT entry.
    return !canBypassGOT;
  }

  const DefinedAtom *makeGOTEntry(const Atom *target) {
    auto pos = _targetToGOT.find(target);
    if (pos == _targetToGOT.end()) {
      auto *gotEntry = new (_file.allocator())
          GOTEntryAtom(_file, _ctx.is64Bit(), target->name());
      _targetToGOT[target] = gotEntry;
      const ArchHandler::ReferenceInfo &nlInfo = _archHandler.stubInfo().
                                                nonLazyPointerReferenceToBinder;
      gotEntry->addReference(Reference::KindNamespace::mach_o, nlInfo.arch,
                             nlInfo.kind, 0, target, 0);
      return gotEntry;
    }
    return pos->second;
  }

  const MachOLinkingContext &_ctx;
  mach_o::ArchHandler                             &_archHandler;
  MachOFile                                       &_file;
  llvm::DenseMap<const Atom*, const GOTEntryAtom*> _targetToGOT;
};

void addGOTPass(PassManager &pm, const MachOLinkingContext &ctx) {
  assert(ctx.needsGOTPass());
  pm.add(llvm::make_unique<GOTPass>(ctx));
}

} // end namesapce mach_o
} // end namesapce lld
