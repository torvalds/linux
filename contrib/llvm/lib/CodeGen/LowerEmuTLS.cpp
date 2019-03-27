//===- LowerEmuTLS.cpp - Add __emutls_[vt].* variables --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transformation is required for targets depending on libgcc style
// emulated thread local storage variables. For every defined TLS variable xyz,
// an __emutls_v.xyz is generated. If there is non-zero initialized value
// an __emutls_t.xyz is also generated.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "loweremutls"

namespace {

class LowerEmuTLS : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  LowerEmuTLS() : ModulePass(ID) {
    initializeLowerEmuTLSPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override;
private:
  bool addEmuTlsVar(Module &M, const GlobalVariable *GV);
  static void copyLinkageVisibility(Module &M,
                                    const GlobalVariable *from,
                                    GlobalVariable *to) {
    to->setLinkage(from->getLinkage());
    to->setVisibility(from->getVisibility());
    if (from->hasComdat()) {
      to->setComdat(M.getOrInsertComdat(to->getName()));
      to->getComdat()->setSelectionKind(from->getComdat()->getSelectionKind());
    }
  }
};
}

char LowerEmuTLS::ID = 0;

INITIALIZE_PASS(LowerEmuTLS, DEBUG_TYPE,
                "Add __emutls_[vt]. variables for emultated TLS model", false,
                false)

ModulePass *llvm::createLowerEmuTLSPass() { return new LowerEmuTLS(); }

bool LowerEmuTLS::runOnModule(Module &M) {
  if (skipModule(M))
    return false;

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  auto &TM = TPC->getTM<TargetMachine>();
  if (!TM.useEmulatedTLS())
    return false;

  bool Changed = false;
  SmallVector<const GlobalVariable*, 8> TlsVars;
  for (const auto &G : M.globals()) {
    if (G.isThreadLocal())
      TlsVars.append({&G});
  }
  for (const auto G : TlsVars)
    Changed |= addEmuTlsVar(M, G);
  return Changed;
}

bool LowerEmuTLS::addEmuTlsVar(Module &M, const GlobalVariable *GV) {
  LLVMContext &C = M.getContext();
  PointerType *VoidPtrType = Type::getInt8PtrTy(C);

  std::string EmuTlsVarName = ("__emutls_v." + GV->getName()).str();
  GlobalVariable *EmuTlsVar = M.getNamedGlobal(EmuTlsVarName);
  if (EmuTlsVar)
    return false;  // It has been added before.

  const DataLayout &DL = M.getDataLayout();
  Constant *NullPtr = ConstantPointerNull::get(VoidPtrType);

  // Get non-zero initializer from GV's initializer.
  const Constant *InitValue = nullptr;
  if (GV->hasInitializer()) {
    InitValue = GV->getInitializer();
    const ConstantInt *InitIntValue = dyn_cast<ConstantInt>(InitValue);
    // When GV's init value is all 0, omit the EmuTlsTmplVar and let
    // the emutls library function to reset newly allocated TLS variables.
    if (isa<ConstantAggregateZero>(InitValue) ||
        (InitIntValue && InitIntValue->isZero()))
      InitValue = nullptr;
  }

  // Create the __emutls_v. symbol, whose type has 4 fields:
  //     word size;   // size of GV in bytes
  //     word align;  // alignment of GV
  //     void *ptr;   // initialized to 0; set at run time per thread.
  //     void *templ; // 0 or point to __emutls_t.*
  // sizeof(word) should be the same as sizeof(void*) on target.
  IntegerType *WordType = DL.getIntPtrType(C);
  PointerType *InitPtrType = InitValue ?
      PointerType::getUnqual(InitValue->getType()) : VoidPtrType;
  Type *ElementTypes[4] = {WordType, WordType, VoidPtrType, InitPtrType};
  ArrayRef<Type*> ElementTypeArray(ElementTypes, 4);
  StructType *EmuTlsVarType = StructType::create(ElementTypeArray);
  EmuTlsVar = cast<GlobalVariable>(
      M.getOrInsertGlobal(EmuTlsVarName, EmuTlsVarType));
  copyLinkageVisibility(M, GV, EmuTlsVar);

  // Define "__emutls_t.*" and "__emutls_v.*" only if GV is defined.
  if (!GV->hasInitializer())
    return true;

  Type *GVType = GV->getValueType();
  unsigned GVAlignment = GV->getAlignment();
  if (!GVAlignment) {
    // When LLVM IL declares a variable without alignment, use
    // the ABI default alignment for the type.
    GVAlignment = DL.getABITypeAlignment(GVType);
  }

  // Define "__emutls_t.*" if there is InitValue
  GlobalVariable *EmuTlsTmplVar = nullptr;
  if (InitValue) {
    std::string EmuTlsTmplName = ("__emutls_t." + GV->getName()).str();
    EmuTlsTmplVar = dyn_cast_or_null<GlobalVariable>(
        M.getOrInsertGlobal(EmuTlsTmplName, GVType));
    assert(EmuTlsTmplVar && "Failed to create emualted TLS initializer");
    EmuTlsTmplVar->setConstant(true);
    EmuTlsTmplVar->setInitializer(const_cast<Constant*>(InitValue));
    EmuTlsTmplVar->setAlignment(GVAlignment);
    copyLinkageVisibility(M, GV, EmuTlsTmplVar);
  }

  // Define "__emutls_v.*" with initializer and alignment.
  Constant *ElementValues[4] = {
      ConstantInt::get(WordType, DL.getTypeStoreSize(GVType)),
      ConstantInt::get(WordType, GVAlignment),
      NullPtr, EmuTlsTmplVar ? EmuTlsTmplVar : NullPtr
  };
  ArrayRef<Constant*> ElementValueArray(ElementValues, 4);
  EmuTlsVar->setInitializer(
      ConstantStruct::get(EmuTlsVarType, ElementValueArray));
  unsigned MaxAlignment = std::max(
      DL.getABITypeAlignment(WordType),
      DL.getABITypeAlignment(VoidPtrType));
  EmuTlsVar->setAlignment(MaxAlignment);
  return true;
}
