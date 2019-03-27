//===- lib/ReaderWriter/MachO/TLVPass.cpp -----------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This linker pass transforms all TLV references to real references.
///
//===----------------------------------------------------------------------===//

#include "ArchHandler.h"
#include "File.h"
#include "MachOPasses.h"
#include "lld/Core/Simple.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"

namespace lld {
namespace mach_o {

//
// TLVP Entry Atom created by the TLV pass.
//
class TLVPEntryAtom : public SimpleDefinedAtom {
public:
  TLVPEntryAtom(const File &file, bool is64, StringRef name)
      : SimpleDefinedAtom(file), _is64(is64), _name(name) {}

  ~TLVPEntryAtom() override = default;

  ContentType contentType() const override {
    return DefinedAtom::typeTLVInitializerPtr;
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

class TLVPass : public Pass {
public:
  TLVPass(const MachOLinkingContext &context)
      : _ctx(context), _archHandler(_ctx.archHandler()),
        _file(*_ctx.make_file<MachOFile>("<mach-o TLV pass>")) {
    _file.setOrdinal(_ctx.getNextOrdinalAndIncrement());
  }

private:
  llvm::Error perform(SimpleFile &mergedFile) override {
    bool allowTLV = _ctx.minOS("10.7", "1.0");

    for (const DefinedAtom *atom : mergedFile.defined()) {
      for (const Reference *ref : *atom) {
        if (!_archHandler.isTLVAccess(*ref))
          continue;

        if (!allowTLV)
          return llvm::make_error<GenericError>(
            "targeted OS version does not support use of thread local "
            "variables in " + atom->name() + " for architecture " +
            _ctx.archName());

        const Atom *target = ref->target();
        assert(target != nullptr);

        const DefinedAtom *tlvpEntry = makeTLVPEntry(target);
        const_cast<Reference*>(ref)->setTarget(tlvpEntry);
        _archHandler.updateReferenceToTLV(ref);
      }
    }

    std::vector<const TLVPEntryAtom*> entries;
    entries.reserve(_targetToTLVP.size());
    for (auto &it : _targetToTLVP)
      entries.push_back(it.second);
    std::sort(entries.begin(), entries.end(),
              [](const TLVPEntryAtom *lhs, const TLVPEntryAtom *rhs) {
                return (lhs->slotName().compare(rhs->slotName()) < 0);
              });

    for (const TLVPEntryAtom *slot : entries)
      mergedFile.addAtom(*slot);

    return llvm::Error::success();
  }

  const DefinedAtom *makeTLVPEntry(const Atom *target) {
    auto pos = _targetToTLVP.find(target);

    if (pos != _targetToTLVP.end())
      return pos->second;

    auto *tlvpEntry = new (_file.allocator())
      TLVPEntryAtom(_file, _ctx.is64Bit(), target->name());
    _targetToTLVP[target] = tlvpEntry;
    const ArchHandler::ReferenceInfo &nlInfo =
      _archHandler.stubInfo().nonLazyPointerReferenceToBinder;
    tlvpEntry->addReference(Reference::KindNamespace::mach_o, nlInfo.arch,
                            nlInfo.kind, 0, target, 0);
    return tlvpEntry;
  }

  const MachOLinkingContext &_ctx;
  mach_o::ArchHandler &_archHandler;
  MachOFile           &_file;
  llvm::DenseMap<const Atom*, const TLVPEntryAtom*> _targetToTLVP;
};

void addTLVPass(PassManager &pm, const MachOLinkingContext &ctx) {
  assert(ctx.needsTLVPass());
  pm.add(llvm::make_unique<TLVPass>(ctx));
}

} // end namesapce mach_o
} // end namesapce lld
