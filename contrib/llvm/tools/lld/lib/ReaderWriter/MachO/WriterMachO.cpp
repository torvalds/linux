//===- lib/ReaderWriter/MachO/WriterMachO.cpp -----------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ExecutableAtoms.h"
#include "MachONormalizedFile.h"
#include "lld/Core/File.h"
#include "lld/Core/Writer.h"
#include "lld/ReaderWriter/MachOLinkingContext.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <system_error>

using lld::mach_o::normalized::NormalizedFile;

namespace lld {
namespace mach_o {

class MachOWriter : public Writer {
public:
  MachOWriter(const MachOLinkingContext &ctxt) : _ctx(ctxt) {}

  llvm::Error writeFile(const lld::File &file, StringRef path) override {
    // Construct empty normalized file from atoms.
    llvm::Expected<std::unique_ptr<NormalizedFile>> nFile =
        normalized::normalizedFromAtoms(file, _ctx);
    if (auto ec = nFile.takeError())
      return ec;

    // For testing, write out yaml form of normalized file.
    if (_ctx.printAtoms()) {
      std::unique_ptr<Writer> yamlWriter = createWriterYAML(_ctx);
      if (auto ec = yamlWriter->writeFile(file, "-"))
        return ec;
    }

    // Write normalized file as mach-o binary.
    return writeBinary(*nFile->get(), path);
  }

  void createImplicitFiles(std::vector<std::unique_ptr<File>> &r) override {
    // When building main executables, add _main as required entry point.
    if (_ctx.outputTypeHasEntry())
      r.emplace_back(new CEntryFile(_ctx));
    // If this can link with dylibs, need helper function (dyld_stub_binder).
    if (_ctx.needsStubsPass())
      r.emplace_back(new StubHelperFile(_ctx));
    // Final linked images can access a symbol for their mach_header.
    if (_ctx.outputMachOType() != llvm::MachO::MH_OBJECT)
      r.emplace_back(new MachHeaderAliasFile(_ctx));
  }
private:
  const MachOLinkingContext &_ctx;
 };


} // namespace mach_o

std::unique_ptr<Writer> createWriterMachO(const MachOLinkingContext &context) {
  return std::unique_ptr<Writer>(new lld::mach_o::MachOWriter(context));
}

} // namespace lld
