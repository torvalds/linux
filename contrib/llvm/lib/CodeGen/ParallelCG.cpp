//===-- ParallelCG.cpp ----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines functions that can be used for parallel code generation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ParallelCG.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/SplitModule.h"

using namespace llvm;

static void codegen(Module *M, llvm::raw_pwrite_stream &OS,
                    function_ref<std::unique_ptr<TargetMachine>()> TMFactory,
                    TargetMachine::CodeGenFileType FileType) {
  std::unique_ptr<TargetMachine> TM = TMFactory();
  legacy::PassManager CodeGenPasses;
  if (TM->addPassesToEmitFile(CodeGenPasses, OS, nullptr, FileType))
    report_fatal_error("Failed to setup codegen");
  CodeGenPasses.run(*M);
}

std::unique_ptr<Module> llvm::splitCodeGen(
    std::unique_ptr<Module> M, ArrayRef<llvm::raw_pwrite_stream *> OSs,
    ArrayRef<llvm::raw_pwrite_stream *> BCOSs,
    const std::function<std::unique_ptr<TargetMachine>()> &TMFactory,
    TargetMachine::CodeGenFileType FileType, bool PreserveLocals) {
  assert(BCOSs.empty() || BCOSs.size() == OSs.size());

  if (OSs.size() == 1) {
    if (!BCOSs.empty())
      WriteBitcodeToFile(*M, *BCOSs[0]);
    codegen(M.get(), *OSs[0], TMFactory, FileType);
    return M;
  }

  // Create ThreadPool in nested scope so that threads will be joined
  // on destruction.
  {
    ThreadPool CodegenThreadPool(OSs.size());
    int ThreadCount = 0;

    SplitModule(
        std::move(M), OSs.size(),
        [&](std::unique_ptr<Module> MPart) {
          // We want to clone the module in a new context to multi-thread the
          // codegen. We do it by serializing partition modules to bitcode
          // (while still on the main thread, in order to avoid data races) and
          // spinning up new threads which deserialize the partitions into
          // separate contexts.
          // FIXME: Provide a more direct way to do this in LLVM.
          SmallString<0> BC;
          raw_svector_ostream BCOS(BC);
          WriteBitcodeToFile(*MPart, BCOS);

          if (!BCOSs.empty()) {
            BCOSs[ThreadCount]->write(BC.begin(), BC.size());
            BCOSs[ThreadCount]->flush();
          }

          llvm::raw_pwrite_stream *ThreadOS = OSs[ThreadCount++];
          // Enqueue the task
          CodegenThreadPool.async(
              [TMFactory, FileType, ThreadOS](const SmallString<0> &BC) {
                LLVMContext Ctx;
                Expected<std::unique_ptr<Module>> MOrErr = parseBitcodeFile(
                    MemoryBufferRef(StringRef(BC.data(), BC.size()),
                                    "<split-module>"),
                    Ctx);
                if (!MOrErr)
                  report_fatal_error("Failed to read bitcode");
                std::unique_ptr<Module> MPartInCtx = std::move(MOrErr.get());

                codegen(MPartInCtx.get(), *ThreadOS, TMFactory, FileType);
              },
              // Pass BC using std::move to ensure that it get moved rather than
              // copied into the thread's context.
              std::move(BC));
        },
        PreserveLocals);
  }

  return {};
}
