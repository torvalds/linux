//===-- ThreadSafeModule.cpp - Thread safe Module, Context, and Utilities
//h-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Transforms/Utils/Cloning.h"

namespace llvm {
namespace orc {

ThreadSafeModule cloneToNewContext(ThreadSafeModule &TSM,
                                   GVPredicate ShouldCloneDef,
                                   GVModifier UpdateClonedDefSource) {
  assert(TSM && "Can not clone null module");

  if (!ShouldCloneDef)
    ShouldCloneDef = [](const GlobalValue &) { return true; };

  auto Lock = TSM.getContextLock();

  SmallVector<char, 1> ClonedModuleBuffer;

  {
    std::set<GlobalValue *> ClonedDefsInSrc;
    ValueToValueMapTy VMap;
    auto Tmp = CloneModule(*TSM.getModule(), VMap, [&](const GlobalValue *GV) {
      if (ShouldCloneDef(*GV)) {
        ClonedDefsInSrc.insert(const_cast<GlobalValue *>(GV));
        return true;
      }
      return false;
    });

    if (UpdateClonedDefSource)
      for (auto *GV : ClonedDefsInSrc)
        UpdateClonedDefSource(*GV);

    BitcodeWriter BCWriter(ClonedModuleBuffer);

    BCWriter.writeModule(*Tmp);
    BCWriter.writeSymtab();
    BCWriter.writeStrtab();
  }

  MemoryBufferRef ClonedModuleBufferRef(
      StringRef(ClonedModuleBuffer.data(), ClonedModuleBuffer.size()),
      "cloned module buffer");
  ThreadSafeContext NewTSCtx(llvm::make_unique<LLVMContext>());

  auto ClonedModule =
      cantFail(parseBitcodeFile(ClonedModuleBufferRef, *NewTSCtx.getContext()));
  ClonedModule->setModuleIdentifier(TSM.getModule()->getName());
  return ThreadSafeModule(std::move(ClonedModule), std::move(NewTSCtx));
}

} // end namespace orc
} // end namespace llvm
