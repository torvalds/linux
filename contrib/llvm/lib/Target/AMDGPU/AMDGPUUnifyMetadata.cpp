//===- AMDGPUUnifyMetadata.cpp - Unify OpenCL metadata --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// \file
// This pass that unifies multiple OpenCL metadata due to linking.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include <algorithm>
#include <cassert>

using namespace llvm;

namespace {

  namespace kOCLMD {

    const char SpirVer[]            = "opencl.spir.version";
    const char OCLVer[]             = "opencl.ocl.version";
    const char UsedExt[]            = "opencl.used.extensions";
    const char UsedOptCoreFeat[]    = "opencl.used.optional.core.features";
    const char CompilerOptions[]    = "opencl.compiler.options";
    const char LLVMIdent[]          = "llvm.ident";

  } // end namespace kOCLMD

  /// Unify multiple OpenCL metadata due to linking.
  class AMDGPUUnifyMetadata : public ModulePass {
  public:
    static char ID;

    explicit AMDGPUUnifyMetadata() : ModulePass(ID) {}

  private:
    bool runOnModule(Module &M) override;

    /// Unify version metadata.
    /// \return true if changes are made.
    /// Assume the named metadata has operands each of which is a pair of
    /// integer constant, e.g.
    /// !Name = {!n1, !n2}
    /// !n1 = {i32 1, i32 2}
    /// !n2 = {i32 2, i32 0}
    /// Keep the largest version as the sole operand if PickFirst is false.
    /// Otherwise pick it from the first value, representing kernel module.
    bool unifyVersionMD(Module &M, StringRef Name, bool PickFirst) {
      auto NamedMD = M.getNamedMetadata(Name);
      if (!NamedMD || NamedMD->getNumOperands() <= 1)
        return false;
      MDNode *MaxMD = nullptr;
      auto MaxVer = 0U;
      for (const auto &VersionMD : NamedMD->operands()) {
        assert(VersionMD->getNumOperands() == 2);
        auto CMajor = mdconst::extract<ConstantInt>(VersionMD->getOperand(0));
        auto VersionMajor = CMajor->getZExtValue();
        auto CMinor = mdconst::extract<ConstantInt>(VersionMD->getOperand(1));
        auto VersionMinor = CMinor->getZExtValue();
        auto Ver = (VersionMajor * 100) + (VersionMinor * 10);
        if (Ver > MaxVer) {
          MaxVer = Ver;
          MaxMD = VersionMD;
        }
        if (PickFirst)
          break;
      }
      NamedMD->eraseFromParent();
      NamedMD = M.getOrInsertNamedMetadata(Name);
      NamedMD->addOperand(MaxMD);
      return true;
    }

  /// Unify version metadata.
  /// \return true if changes are made.
  /// Assume the named metadata has operands each of which is a list e.g.
  /// !Name = {!n1, !n2}
  /// !n1 = !{!"cl_khr_fp16", {!"cl_khr_fp64"}}
  /// !n2 = !{!"cl_khr_image"}
  /// Combine it into a single list with unique operands.
  bool unifyExtensionMD(Module &M, StringRef Name) {
    auto NamedMD = M.getNamedMetadata(Name);
    if (!NamedMD || NamedMD->getNumOperands() == 1)
      return false;

    SmallVector<Metadata *, 4> All;
    for (const auto &MD : NamedMD->operands())
      for (const auto &Op : MD->operands())
        if (std::find(All.begin(), All.end(), Op.get()) == All.end())
          All.push_back(Op.get());

    NamedMD->eraseFromParent();
    NamedMD = M.getOrInsertNamedMetadata(Name);
    for (const auto &MD : All)
      NamedMD->addOperand(MDNode::get(M.getContext(), MD));

    return true;
  }
};

} // end anonymous namespace

char AMDGPUUnifyMetadata::ID = 0;

char &llvm::AMDGPUUnifyMetadataID = AMDGPUUnifyMetadata::ID;

INITIALIZE_PASS(AMDGPUUnifyMetadata, "amdgpu-unify-metadata",
                "Unify multiple OpenCL metadata due to linking",
                false, false)

ModulePass* llvm::createAMDGPUUnifyMetadataPass() {
  return new AMDGPUUnifyMetadata();
}

bool AMDGPUUnifyMetadata::runOnModule(Module &M) {
  const char* Vers[] = {
      kOCLMD::SpirVer,
      kOCLMD::OCLVer
  };
  const char* Exts[] = {
      kOCLMD::UsedExt,
      kOCLMD::UsedOptCoreFeat,
      kOCLMD::CompilerOptions,
      kOCLMD::LLVMIdent
  };

  bool Changed = false;

  for (auto &I : Vers)
    Changed |= unifyVersionMD(M, I, true);

  for (auto &I : Exts)
    Changed |= unifyExtensionMD(M, I);

  return Changed;
}
