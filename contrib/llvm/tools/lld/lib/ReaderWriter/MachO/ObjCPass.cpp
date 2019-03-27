//===- lib/ReaderWriter/MachO/ObjCPass.cpp -------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "ArchHandler.h"
#include "File.h"
#include "MachONormalizedFileBinaryUtils.h"
#include "MachOPasses.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/DefinedAtom.h"
#include "lld/Core/File.h"
#include "lld/Core/Reference.h"
#include "lld/Core/Simple.h"
#include "lld/ReaderWriter/MachOLinkingContext.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

namespace lld {
namespace mach_o {

///
/// ObjC Image Info Atom created by the ObjC pass.
///
class ObjCImageInfoAtom : public SimpleDefinedAtom {
public:
  ObjCImageInfoAtom(const File &file, bool isBig,
                    MachOLinkingContext::ObjCConstraint objCConstraint,
                    uint32_t swiftVersion)
      : SimpleDefinedAtom(file) {

    Data.info.version = 0;

    switch (objCConstraint) {
    case MachOLinkingContext::objc_unknown:
      llvm_unreachable("Shouldn't run the objc pass without a constraint");
    case MachOLinkingContext::objc_supports_gc:
    case MachOLinkingContext::objc_gc_only:
      llvm_unreachable("GC is not supported");
    case MachOLinkingContext::objc_retainReleaseForSimulator:
      // The retain/release for simulator flag is already the correct
      // encoded value for the data so just set it here.
      Data.info.flags = (uint32_t)objCConstraint;
      break;
    case MachOLinkingContext::objc_retainRelease:
      // We don't need to encode this flag, so just leave the flags as 0.
      Data.info.flags = 0;
      break;
    }

    Data.info.flags |= (swiftVersion << 8);

    normalized::write32(Data.bytes + 4, Data.info.flags, isBig);
  }

  ~ObjCImageInfoAtom() override = default;

  ContentType contentType() const override {
    return DefinedAtom::typeObjCImageInfo;
  }

  Alignment alignment() const override {
    return 4;
  }

  uint64_t size() const override {
    return 8;
  }

  ContentPermissions permissions() const override {
    return DefinedAtom::permR__;
  }

  ArrayRef<uint8_t> rawContent() const override {
    return llvm::makeArrayRef(Data.bytes, size());
  }

private:

  struct objc_image_info  {
    uint32_t	version;
    uint32_t	flags;
  };

  union {
    objc_image_info info;
    uint8_t bytes[8];
  } Data;
};

class ObjCPass : public Pass {
public:
  ObjCPass(const MachOLinkingContext &context)
      : _ctx(context),
        _file(*_ctx.make_file<MachOFile>("<mach-o objc pass>")) {
    _file.setOrdinal(_ctx.getNextOrdinalAndIncrement());
  }

  llvm::Error perform(SimpleFile &mergedFile) override {
    // Add the image info.
    mergedFile.addAtom(*getImageInfo());

    return llvm::Error::success();
  }

private:

  const DefinedAtom* getImageInfo() {
    bool IsBig = MachOLinkingContext::isBigEndian(_ctx.arch());
    return new (_file.allocator()) ObjCImageInfoAtom(_file, IsBig,
                                                     _ctx.objcConstraint(),
                                                     _ctx.swiftVersion());
  }

  const MachOLinkingContext   &_ctx;
  MachOFile                   &_file;
};



void addObjCPass(PassManager &pm, const MachOLinkingContext &ctx) {
  pm.add(llvm::make_unique<ObjCPass>(ctx));
}

} // end namespace mach_o
} // end namespace lld
