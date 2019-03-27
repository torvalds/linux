//===- lib/ReaderWriter/MachO/StubsPass.cpp ---------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This linker pass updates call-sites which have references to shared library
// atoms to instead have a reference to a stub (PLT entry) for the specified
// symbol.  Each file format defines a subclass of StubsPass which implements
// the abstract methods for creating the file format specific StubAtoms.
//
//===----------------------------------------------------------------------===//

#include "ArchHandler.h"
#include "File.h"
#include "MachOPasses.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/DefinedAtom.h"
#include "lld/Core/File.h"
#include "lld/Core/Reference.h"
#include "lld/Core/Simple.h"
#include "lld/ReaderWriter/MachOLinkingContext.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace lld {
namespace mach_o {

//
//  Lazy Pointer Atom created by the stubs pass.
//
class LazyPointerAtom : public SimpleDefinedAtom {
public:
  LazyPointerAtom(const File &file, bool is64)
    : SimpleDefinedAtom(file), _is64(is64) { }

  ~LazyPointerAtom() override = default;

  ContentType contentType() const override {
    return DefinedAtom::typeLazyPointer;
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

private:
  const bool _is64;
};

//
//  NonLazyPointer (GOT) Atom created by the stubs pass.
//
class NonLazyPointerAtom : public SimpleDefinedAtom {
public:
  NonLazyPointerAtom(const File &file, bool is64, ContentType contentType)
    : SimpleDefinedAtom(file), _is64(is64), _contentType(contentType) { }

  ~NonLazyPointerAtom() override = default;

  ContentType contentType() const override {
    return _contentType;
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

private:
  const bool _is64;
  const ContentType _contentType;
};

//
// Stub Atom created by the stubs pass.
//
class StubAtom : public SimpleDefinedAtom {
public:
  StubAtom(const File &file, const ArchHandler::StubInfo &stubInfo)
      : SimpleDefinedAtom(file), _stubInfo(stubInfo){ }

  ~StubAtom() override = default;

  ContentType contentType() const override {
    return DefinedAtom::typeStub;
  }

  Alignment alignment() const override {
    return 1 << _stubInfo.codeAlignment;
  }

  uint64_t size() const override {
    return _stubInfo.stubSize;
  }

  ContentPermissions permissions() const override {
    return DefinedAtom::permR_X;
  }

  ArrayRef<uint8_t> rawContent() const override {
    return llvm::makeArrayRef(_stubInfo.stubBytes, _stubInfo.stubSize);
  }

private:
  const ArchHandler::StubInfo   &_stubInfo;
};

//
// Stub Helper Atom created by the stubs pass.
//
class StubHelperAtom : public SimpleDefinedAtom {
public:
  StubHelperAtom(const File &file, const ArchHandler::StubInfo &stubInfo)
      : SimpleDefinedAtom(file), _stubInfo(stubInfo) { }

  ~StubHelperAtom() override = default;

  ContentType contentType() const override {
    return DefinedAtom::typeStubHelper;
  }

  Alignment alignment() const override {
    return 1 << _stubInfo.codeAlignment;
  }

  uint64_t size() const override {
    return _stubInfo.stubHelperSize;
  }

  ContentPermissions permissions() const override {
    return DefinedAtom::permR_X;
  }

  ArrayRef<uint8_t> rawContent() const override {
    return llvm::makeArrayRef(_stubInfo.stubHelperBytes,
                              _stubInfo.stubHelperSize);
  }

private:
  const ArchHandler::StubInfo   &_stubInfo;
};

//
// Stub Helper Common Atom created by the stubs pass.
//
class StubHelperCommonAtom : public SimpleDefinedAtom {
public:
  StubHelperCommonAtom(const File &file, const ArchHandler::StubInfo &stubInfo)
      : SimpleDefinedAtom(file), _stubInfo(stubInfo) { }

  ~StubHelperCommonAtom() override = default;

  ContentType contentType() const override {
    return DefinedAtom::typeStubHelper;
  }

  Alignment alignment() const override {
    return 1 << _stubInfo.stubHelperCommonAlignment;
  }

  uint64_t size() const override {
    return _stubInfo.stubHelperCommonSize;
  }

  ContentPermissions permissions() const override {
    return DefinedAtom::permR_X;
  }

  ArrayRef<uint8_t> rawContent() const override {
    return llvm::makeArrayRef(_stubInfo.stubHelperCommonBytes,
                        _stubInfo.stubHelperCommonSize);
  }

private:
  const ArchHandler::StubInfo   &_stubInfo;
};

class StubsPass : public Pass {
public:
  StubsPass(const MachOLinkingContext &context)
      : _ctx(context), _archHandler(_ctx.archHandler()),
        _stubInfo(_archHandler.stubInfo()),
        _file(*_ctx.make_file<MachOFile>("<mach-o Stubs pass>")) {
    _file.setOrdinal(_ctx.getNextOrdinalAndIncrement());
  }

  llvm::Error perform(SimpleFile &mergedFile) override {
    // Skip this pass if output format uses text relocations instead of stubs.
    if (!this->noTextRelocs())
      return llvm::Error::success();

    // Scan all references in all atoms.
    for (const DefinedAtom *atom : mergedFile.defined()) {
      for (const Reference *ref : *atom) {
        // Look at call-sites.
        if (!this->isCallSite(*ref))
          continue;
        const Atom *target = ref->target();
        assert(target != nullptr);
        if (isa<SharedLibraryAtom>(target)) {
          // Calls to shared libraries go through stubs.
          _targetToUses[target].push_back(ref);
          continue;
        }
        const DefinedAtom *defTarget = dyn_cast<DefinedAtom>(target);
        if (defTarget && defTarget->interposable() != DefinedAtom::interposeNo){
          // Calls to interposable functions in same linkage unit must also go
          // through a stub.
          assert(defTarget->scope() != DefinedAtom::scopeTranslationUnit);
          _targetToUses[target].push_back(ref);
        }
      }
    }

    // Exit early if no stubs needed.
    if (_targetToUses.empty())
      return llvm::Error::success();

    // First add help-common and GOT slots used by lazy binding.
    SimpleDefinedAtom *helperCommonAtom =
        new (_file.allocator()) StubHelperCommonAtom(_file, _stubInfo);
    SimpleDefinedAtom *helperCacheNLPAtom =
        new (_file.allocator()) NonLazyPointerAtom(_file, _ctx.is64Bit(),
                                    _stubInfo.stubHelperImageCacheContentType);
    SimpleDefinedAtom *helperBinderNLPAtom =
        new (_file.allocator()) NonLazyPointerAtom(_file, _ctx.is64Bit(),
                                    _stubInfo.stubHelperImageCacheContentType);
    addReference(helperCommonAtom, _stubInfo.stubHelperCommonReferenceToCache,
                 helperCacheNLPAtom);
    addOptReference(
        helperCommonAtom, _stubInfo.stubHelperCommonReferenceToCache,
        _stubInfo.optStubHelperCommonReferenceToCache, helperCacheNLPAtom);
    addReference(helperCommonAtom, _stubInfo.stubHelperCommonReferenceToBinder,
                 helperBinderNLPAtom);
    addOptReference(
        helperCommonAtom, _stubInfo.stubHelperCommonReferenceToBinder,
        _stubInfo.optStubHelperCommonReferenceToBinder, helperBinderNLPAtom);
    mergedFile.addAtom(*helperCommonAtom);
    mergedFile.addAtom(*helperBinderNLPAtom);
    mergedFile.addAtom(*helperCacheNLPAtom);

    // Add reference to dyld_stub_binder in libSystem.dylib
    auto I = std::find_if(
        mergedFile.sharedLibrary().begin(), mergedFile.sharedLibrary().end(),
        [&](const SharedLibraryAtom *atom) {
          return atom->name().equals(_stubInfo.binderSymbolName);
        });
    assert(I != mergedFile.sharedLibrary().end() &&
           "dyld_stub_binder not found");
    addReference(helperBinderNLPAtom, _stubInfo.nonLazyPointerReferenceToBinder, *I);

    // Sort targets by name, so stubs and lazy pointers are consistent
    std::vector<const Atom *> targetsNeedingStubs;
    for (auto it : _targetToUses)
      targetsNeedingStubs.push_back(it.first);
    std::sort(targetsNeedingStubs.begin(), targetsNeedingStubs.end(),
              [](const Atom * left, const Atom * right) {
      return (left->name().compare(right->name()) < 0);
    });

    // Make and append stubs, lazy pointers, and helpers in alphabetical order.
    unsigned lazyOffset = 0;
    for (const Atom *target : targetsNeedingStubs) {
      auto *stub = new (_file.allocator()) StubAtom(_file, _stubInfo);
      auto *lp =
          new (_file.allocator()) LazyPointerAtom(_file, _ctx.is64Bit());
      auto *helper = new (_file.allocator()) StubHelperAtom(_file, _stubInfo);

      addReference(stub, _stubInfo.stubReferenceToLP, lp);
      addOptReference(stub, _stubInfo.stubReferenceToLP,
                      _stubInfo.optStubReferenceToLP, lp);
      addReference(lp, _stubInfo.lazyPointerReferenceToHelper, helper);
      addReference(lp, _stubInfo.lazyPointerReferenceToFinal, target);
      addReference(helper, _stubInfo.stubHelperReferenceToImm, helper);
      addReferenceAddend(helper, _stubInfo.stubHelperReferenceToImm, helper,
                         lazyOffset);
      addReference(helper, _stubInfo.stubHelperReferenceToHelperCommon,
                   helperCommonAtom);

      mergedFile.addAtom(*stub);
      mergedFile.addAtom(*lp);
      mergedFile.addAtom(*helper);

      // Update each reference to use stub.
      for (const Reference *ref : _targetToUses[target]) {
        assert(ref->target() == target);
        // Switch call site to reference stub atom instead.
        const_cast<Reference *>(ref)->setTarget(stub);
      }

      // Calculate new offset
      lazyOffset += target->name().size() + 12;
    }

    return llvm::Error::success();
  }

private:
  bool noTextRelocs() {
    return true;
  }

  bool isCallSite(const Reference &ref) {
    return _archHandler.isCallSite(ref);
  }

  void addReference(SimpleDefinedAtom* atom,
                    const ArchHandler::ReferenceInfo &refInfo,
                    const lld::Atom* target) {
    atom->addReference(Reference::KindNamespace::mach_o,
                      refInfo.arch, refInfo.kind, refInfo.offset,
                      target, refInfo.addend);
  }

  void addReferenceAddend(SimpleDefinedAtom *atom,
                          const ArchHandler::ReferenceInfo &refInfo,
                          const lld::Atom *target, uint64_t addend) {
    atom->addReference(Reference::KindNamespace::mach_o, refInfo.arch,
                       refInfo.kind, refInfo.offset, target, addend);
  }

   void addOptReference(SimpleDefinedAtom* atom,
                    const ArchHandler::ReferenceInfo &refInfo,
                    const ArchHandler::OptionalRefInfo &optRef,
                    const lld::Atom* target) {
      if (!optRef.used)
        return;
    atom->addReference(Reference::KindNamespace::mach_o,
                      refInfo.arch, optRef.kind, optRef.offset,
                      target, optRef.addend);
  }

  typedef llvm::DenseMap<const Atom*,
                         llvm::SmallVector<const Reference *, 8>> TargetToUses;

  const MachOLinkingContext &_ctx;
  mach_o::ArchHandler                            &_archHandler;
  const ArchHandler::StubInfo                    &_stubInfo;
  MachOFile                                      &_file;
  TargetToUses                                    _targetToUses;
};

void addStubsPass(PassManager &pm, const MachOLinkingContext &ctx) {
  pm.add(std::unique_ptr<Pass>(new StubsPass(ctx)));
}

} // end namespace mach_o
} // end namespace lld
