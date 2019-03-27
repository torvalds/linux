//===- R600OpenCLImageTypeLoweringPass.cpp ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass resolves calls to OpenCL image attribute, image resource ID and
/// sampler resource ID getter functions.
///
/// Image attributes (size and format) are expected to be passed to the kernel
/// as kernel arguments immediately following the image argument itself,
/// therefore this pass adds image size and format arguments to the kernel
/// functions in the module. The kernel functions with image arguments are
/// re-created using the new signature. The new arguments are added to the
/// kernel metadata with kernel_arg_type set to "image_size" or "image_format".
/// Note: this pass may invalidate pointers to functions.
///
/// Resource IDs of read-only images, write-only images and samplers are
/// defined to be their index among the kernel arguments of the same
/// type and access qualifier.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <tuple>

using namespace llvm;

static StringRef GetImageSizeFunc =         "llvm.OpenCL.image.get.size";
static StringRef GetImageFormatFunc =       "llvm.OpenCL.image.get.format";
static StringRef GetImageResourceIDFunc =   "llvm.OpenCL.image.get.resource.id";
static StringRef GetSamplerResourceIDFunc =
    "llvm.OpenCL.sampler.get.resource.id";

static StringRef ImageSizeArgMDType =   "__llvm_image_size";
static StringRef ImageFormatArgMDType = "__llvm_image_format";

static StringRef KernelsMDNodeName = "opencl.kernels";
static StringRef KernelArgMDNodeNames[] = {
  "kernel_arg_addr_space",
  "kernel_arg_access_qual",
  "kernel_arg_type",
  "kernel_arg_base_type",
  "kernel_arg_type_qual"};
static const unsigned NumKernelArgMDNodes = 5;

namespace {

using MDVector = SmallVector<Metadata *, 8>;
struct KernelArgMD {
  MDVector ArgVector[NumKernelArgMDNodes];
};

} // end anonymous namespace

static inline bool
IsImageType(StringRef TypeString) {
  return TypeString == "image2d_t" || TypeString == "image3d_t";
}

static inline bool
IsSamplerType(StringRef TypeString) {
  return TypeString == "sampler_t";
}

static Function *
GetFunctionFromMDNode(MDNode *Node) {
  if (!Node)
    return nullptr;

  size_t NumOps = Node->getNumOperands();
  if (NumOps != NumKernelArgMDNodes + 1)
    return nullptr;

  auto F = mdconst::dyn_extract<Function>(Node->getOperand(0));
  if (!F)
    return nullptr;

  // Sanity checks.
  size_t ExpectNumArgNodeOps = F->arg_size() + 1;
  for (size_t i = 0; i < NumKernelArgMDNodes; ++i) {
    MDNode *ArgNode = dyn_cast_or_null<MDNode>(Node->getOperand(i + 1));
    if (ArgNode->getNumOperands() != ExpectNumArgNodeOps)
      return nullptr;
    if (!ArgNode->getOperand(0))
      return nullptr;

    // FIXME: It should be possible to do image lowering when some metadata
    // args missing or not in the expected order.
    MDString *StringNode = dyn_cast<MDString>(ArgNode->getOperand(0));
    if (!StringNode || StringNode->getString() != KernelArgMDNodeNames[i])
      return nullptr;
  }

  return F;
}

static StringRef
AccessQualFromMD(MDNode *KernelMDNode, unsigned ArgIdx) {
  MDNode *ArgAQNode = cast<MDNode>(KernelMDNode->getOperand(2));
  return cast<MDString>(ArgAQNode->getOperand(ArgIdx + 1))->getString();
}

static StringRef
ArgTypeFromMD(MDNode *KernelMDNode, unsigned ArgIdx) {
  MDNode *ArgTypeNode = cast<MDNode>(KernelMDNode->getOperand(3));
  return cast<MDString>(ArgTypeNode->getOperand(ArgIdx + 1))->getString();
}

static MDVector
GetArgMD(MDNode *KernelMDNode, unsigned OpIdx) {
  MDVector Res;
  for (unsigned i = 0; i < NumKernelArgMDNodes; ++i) {
    MDNode *Node = cast<MDNode>(KernelMDNode->getOperand(i + 1));
    Res.push_back(Node->getOperand(OpIdx));
  }
  return Res;
}

static void
PushArgMD(KernelArgMD &MD, const MDVector &V) {
  assert(V.size() == NumKernelArgMDNodes);
  for (unsigned i = 0; i < NumKernelArgMDNodes; ++i) {
    MD.ArgVector[i].push_back(V[i]);
  }
}

namespace {

class R600OpenCLImageTypeLoweringPass : public ModulePass {
  static char ID;

  LLVMContext *Context;
  Type *Int32Type;
  Type *ImageSizeType;
  Type *ImageFormatType;
  SmallVector<Instruction *, 4> InstsToErase;

  bool replaceImageUses(Argument &ImageArg, uint32_t ResourceID,
                        Argument &ImageSizeArg,
                        Argument &ImageFormatArg) {
    bool Modified = false;

    for (auto &Use : ImageArg.uses()) {
      auto Inst = dyn_cast<CallInst>(Use.getUser());
      if (!Inst) {
        continue;
      }

      Function *F = Inst->getCalledFunction();
      if (!F)
        continue;

      Value *Replacement = nullptr;
      StringRef Name = F->getName();
      if (Name.startswith(GetImageResourceIDFunc)) {
        Replacement = ConstantInt::get(Int32Type, ResourceID);
      } else if (Name.startswith(GetImageSizeFunc)) {
        Replacement = &ImageSizeArg;
      } else if (Name.startswith(GetImageFormatFunc)) {
        Replacement = &ImageFormatArg;
      } else {
        continue;
      }

      Inst->replaceAllUsesWith(Replacement);
      InstsToErase.push_back(Inst);
      Modified = true;
    }

    return Modified;
  }

  bool replaceSamplerUses(Argument &SamplerArg, uint32_t ResourceID) {
    bool Modified = false;

    for (const auto &Use : SamplerArg.uses()) {
      auto Inst = dyn_cast<CallInst>(Use.getUser());
      if (!Inst) {
        continue;
      }

      Function *F = Inst->getCalledFunction();
      if (!F)
        continue;

      Value *Replacement = nullptr;
      StringRef Name = F->getName();
      if (Name == GetSamplerResourceIDFunc) {
        Replacement = ConstantInt::get(Int32Type, ResourceID);
      } else {
        continue;
      }

      Inst->replaceAllUsesWith(Replacement);
      InstsToErase.push_back(Inst);
      Modified = true;
    }

    return Modified;
  }

  bool replaceImageAndSamplerUses(Function *F, MDNode *KernelMDNode) {
    uint32_t NumReadOnlyImageArgs = 0;
    uint32_t NumWriteOnlyImageArgs = 0;
    uint32_t NumSamplerArgs = 0;

    bool Modified = false;
    InstsToErase.clear();
    for (auto ArgI = F->arg_begin(); ArgI != F->arg_end(); ++ArgI) {
      Argument &Arg = *ArgI;
      StringRef Type = ArgTypeFromMD(KernelMDNode, Arg.getArgNo());

      // Handle image types.
      if (IsImageType(Type)) {
        StringRef AccessQual = AccessQualFromMD(KernelMDNode, Arg.getArgNo());
        uint32_t ResourceID;
        if (AccessQual == "read_only") {
          ResourceID = NumReadOnlyImageArgs++;
        } else if (AccessQual == "write_only") {
          ResourceID = NumWriteOnlyImageArgs++;
        } else {
          llvm_unreachable("Wrong image access qualifier.");
        }

        Argument &SizeArg = *(++ArgI);
        Argument &FormatArg = *(++ArgI);
        Modified |= replaceImageUses(Arg, ResourceID, SizeArg, FormatArg);

      // Handle sampler type.
      } else if (IsSamplerType(Type)) {
        uint32_t ResourceID = NumSamplerArgs++;
        Modified |= replaceSamplerUses(Arg, ResourceID);
      }
    }
    for (unsigned i = 0; i < InstsToErase.size(); ++i) {
      InstsToErase[i]->eraseFromParent();
    }

    return Modified;
  }

  std::tuple<Function *, MDNode *>
  addImplicitArgs(Function *F, MDNode *KernelMDNode) {
    bool Modified = false;

    FunctionType *FT = F->getFunctionType();
    SmallVector<Type *, 8> ArgTypes;

    // Metadata operands for new MDNode.
    KernelArgMD NewArgMDs;
    PushArgMD(NewArgMDs, GetArgMD(KernelMDNode, 0));

    // Add implicit arguments to the signature.
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
      ArgTypes.push_back(FT->getParamType(i));
      MDVector ArgMD = GetArgMD(KernelMDNode, i + 1);
      PushArgMD(NewArgMDs, ArgMD);

      if (!IsImageType(ArgTypeFromMD(KernelMDNode, i)))
        continue;

      // Add size implicit argument.
      ArgTypes.push_back(ImageSizeType);
      ArgMD[2] = ArgMD[3] = MDString::get(*Context, ImageSizeArgMDType);
      PushArgMD(NewArgMDs, ArgMD);

      // Add format implicit argument.
      ArgTypes.push_back(ImageFormatType);
      ArgMD[2] = ArgMD[3] = MDString::get(*Context, ImageFormatArgMDType);
      PushArgMD(NewArgMDs, ArgMD);

      Modified = true;
    }
    if (!Modified) {
      return std::make_tuple(nullptr, nullptr);
    }

    // Create function with new signature and clone the old body into it.
    auto NewFT = FunctionType::get(FT->getReturnType(), ArgTypes, false);
    auto NewF = Function::Create(NewFT, F->getLinkage(), F->getName());
    ValueToValueMapTy VMap;
    auto NewFArgIt = NewF->arg_begin();
    for (auto &Arg: F->args()) {
      auto ArgName = Arg.getName();
      NewFArgIt->setName(ArgName);
      VMap[&Arg] = &(*NewFArgIt++);
      if (IsImageType(ArgTypeFromMD(KernelMDNode, Arg.getArgNo()))) {
        (NewFArgIt++)->setName(Twine("__size_") + ArgName);
        (NewFArgIt++)->setName(Twine("__format_") + ArgName);
      }
    }
    SmallVector<ReturnInst*, 8> Returns;
    CloneFunctionInto(NewF, F, VMap, /*ModuleLevelChanges=*/false, Returns);

    // Build new MDNode.
    SmallVector<Metadata *, 6> KernelMDArgs;
    KernelMDArgs.push_back(ConstantAsMetadata::get(NewF));
    for (unsigned i = 0; i < NumKernelArgMDNodes; ++i)
      KernelMDArgs.push_back(MDNode::get(*Context, NewArgMDs.ArgVector[i]));
    MDNode *NewMDNode = MDNode::get(*Context, KernelMDArgs);

    return std::make_tuple(NewF, NewMDNode);
  }

  bool transformKernels(Module &M) {
    NamedMDNode *KernelsMDNode = M.getNamedMetadata(KernelsMDNodeName);
    if (!KernelsMDNode)
      return false;

    bool Modified = false;
    for (unsigned i = 0; i < KernelsMDNode->getNumOperands(); ++i) {
      MDNode *KernelMDNode = KernelsMDNode->getOperand(i);
      Function *F = GetFunctionFromMDNode(KernelMDNode);
      if (!F)
        continue;

      Function *NewF;
      MDNode *NewMDNode;
      std::tie(NewF, NewMDNode) = addImplicitArgs(F, KernelMDNode);
      if (NewF) {
        // Replace old function and metadata with new ones.
        F->eraseFromParent();
        M.getFunctionList().push_back(NewF);
        M.getOrInsertFunction(NewF->getName(), NewF->getFunctionType(),
                              NewF->getAttributes());
        KernelsMDNode->setOperand(i, NewMDNode);

        F = NewF;
        KernelMDNode = NewMDNode;
        Modified = true;
      }

      Modified |= replaceImageAndSamplerUses(F, KernelMDNode);
    }

    return Modified;
  }

public:
  R600OpenCLImageTypeLoweringPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override {
    Context = &M.getContext();
    Int32Type = Type::getInt32Ty(M.getContext());
    ImageSizeType = ArrayType::get(Int32Type, 3);
    ImageFormatType = ArrayType::get(Int32Type, 2);

    return transformKernels(M);
  }

  StringRef getPassName() const override {
    return "R600 OpenCL Image Type Pass";
  }
};

} // end anonymous namespace

char R600OpenCLImageTypeLoweringPass::ID = 0;

ModulePass *llvm::createR600OpenCLImageTypeLoweringPass() {
  return new R600OpenCLImageTypeLoweringPass();
}
