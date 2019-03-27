//===-- NVPTXISelDAGToDAG.cpp - A dag to dag inst selector for NVPTX ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the NVPTX target.
//
//===----------------------------------------------------------------------===//

#include "NVPTXISelDAGToDAG.h"
#include "NVPTXUtilities.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"

using namespace llvm;

#define DEBUG_TYPE "nvptx-isel"

/// createNVPTXISelDag - This pass converts a legalized DAG into a
/// NVPTX-specific DAG, ready for instruction scheduling.
FunctionPass *llvm::createNVPTXISelDag(NVPTXTargetMachine &TM,
                                       llvm::CodeGenOpt::Level OptLevel) {
  return new NVPTXDAGToDAGISel(TM, OptLevel);
}

NVPTXDAGToDAGISel::NVPTXDAGToDAGISel(NVPTXTargetMachine &tm,
                                     CodeGenOpt::Level OptLevel)
    : SelectionDAGISel(tm, OptLevel), TM(tm) {
  doMulWide = (OptLevel > 0);
}

bool NVPTXDAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &static_cast<const NVPTXSubtarget &>(MF.getSubtarget());
  return SelectionDAGISel::runOnMachineFunction(MF);
}

int NVPTXDAGToDAGISel::getDivF32Level() const {
  return Subtarget->getTargetLowering()->getDivF32Level();
}

bool NVPTXDAGToDAGISel::usePrecSqrtF32() const {
  return Subtarget->getTargetLowering()->usePrecSqrtF32();
}

bool NVPTXDAGToDAGISel::useF32FTZ() const {
  return Subtarget->getTargetLowering()->useF32FTZ(*MF);
}

bool NVPTXDAGToDAGISel::allowFMA() const {
  const NVPTXTargetLowering *TL = Subtarget->getTargetLowering();
  return TL->allowFMA(*MF, OptLevel);
}

bool NVPTXDAGToDAGISel::allowUnsafeFPMath() const {
  const NVPTXTargetLowering *TL = Subtarget->getTargetLowering();
  return TL->allowUnsafeFPMath(*MF);
}

bool NVPTXDAGToDAGISel::useShortPointers() const {
  return TM.useShortPointers();
}

/// Select - Select instructions not customized! Used for
/// expanded, promoted and normal instructions.
void NVPTXDAGToDAGISel::Select(SDNode *N) {

  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return; // Already selected.
  }

  switch (N->getOpcode()) {
  case ISD::LOAD:
  case ISD::ATOMIC_LOAD:
    if (tryLoad(N))
      return;
    break;
  case ISD::STORE:
  case ISD::ATOMIC_STORE:
    if (tryStore(N))
      return;
    break;
  case ISD::EXTRACT_VECTOR_ELT:
    if (tryEXTRACT_VECTOR_ELEMENT(N))
      return;
    break;
  case NVPTXISD::SETP_F16X2:
    SelectSETP_F16X2(N);
    return;

  case NVPTXISD::LoadV2:
  case NVPTXISD::LoadV4:
    if (tryLoadVector(N))
      return;
    break;
  case NVPTXISD::LDGV2:
  case NVPTXISD::LDGV4:
  case NVPTXISD::LDUV2:
  case NVPTXISD::LDUV4:
    if (tryLDGLDU(N))
      return;
    break;
  case NVPTXISD::StoreV2:
  case NVPTXISD::StoreV4:
    if (tryStoreVector(N))
      return;
    break;
  case NVPTXISD::LoadParam:
  case NVPTXISD::LoadParamV2:
  case NVPTXISD::LoadParamV4:
    if (tryLoadParam(N))
      return;
    break;
  case NVPTXISD::StoreRetval:
  case NVPTXISD::StoreRetvalV2:
  case NVPTXISD::StoreRetvalV4:
    if (tryStoreRetval(N))
      return;
    break;
  case NVPTXISD::StoreParam:
  case NVPTXISD::StoreParamV2:
  case NVPTXISD::StoreParamV4:
  case NVPTXISD::StoreParamS32:
  case NVPTXISD::StoreParamU32:
    if (tryStoreParam(N))
      return;
    break;
  case ISD::INTRINSIC_WO_CHAIN:
    if (tryIntrinsicNoChain(N))
      return;
    break;
  case ISD::INTRINSIC_W_CHAIN:
    if (tryIntrinsicChain(N))
      return;
    break;
  case NVPTXISD::Tex1DFloatS32:
  case NVPTXISD::Tex1DFloatFloat:
  case NVPTXISD::Tex1DFloatFloatLevel:
  case NVPTXISD::Tex1DFloatFloatGrad:
  case NVPTXISD::Tex1DS32S32:
  case NVPTXISD::Tex1DS32Float:
  case NVPTXISD::Tex1DS32FloatLevel:
  case NVPTXISD::Tex1DS32FloatGrad:
  case NVPTXISD::Tex1DU32S32:
  case NVPTXISD::Tex1DU32Float:
  case NVPTXISD::Tex1DU32FloatLevel:
  case NVPTXISD::Tex1DU32FloatGrad:
  case NVPTXISD::Tex1DArrayFloatS32:
  case NVPTXISD::Tex1DArrayFloatFloat:
  case NVPTXISD::Tex1DArrayFloatFloatLevel:
  case NVPTXISD::Tex1DArrayFloatFloatGrad:
  case NVPTXISD::Tex1DArrayS32S32:
  case NVPTXISD::Tex1DArrayS32Float:
  case NVPTXISD::Tex1DArrayS32FloatLevel:
  case NVPTXISD::Tex1DArrayS32FloatGrad:
  case NVPTXISD::Tex1DArrayU32S32:
  case NVPTXISD::Tex1DArrayU32Float:
  case NVPTXISD::Tex1DArrayU32FloatLevel:
  case NVPTXISD::Tex1DArrayU32FloatGrad:
  case NVPTXISD::Tex2DFloatS32:
  case NVPTXISD::Tex2DFloatFloat:
  case NVPTXISD::Tex2DFloatFloatLevel:
  case NVPTXISD::Tex2DFloatFloatGrad:
  case NVPTXISD::Tex2DS32S32:
  case NVPTXISD::Tex2DS32Float:
  case NVPTXISD::Tex2DS32FloatLevel:
  case NVPTXISD::Tex2DS32FloatGrad:
  case NVPTXISD::Tex2DU32S32:
  case NVPTXISD::Tex2DU32Float:
  case NVPTXISD::Tex2DU32FloatLevel:
  case NVPTXISD::Tex2DU32FloatGrad:
  case NVPTXISD::Tex2DArrayFloatS32:
  case NVPTXISD::Tex2DArrayFloatFloat:
  case NVPTXISD::Tex2DArrayFloatFloatLevel:
  case NVPTXISD::Tex2DArrayFloatFloatGrad:
  case NVPTXISD::Tex2DArrayS32S32:
  case NVPTXISD::Tex2DArrayS32Float:
  case NVPTXISD::Tex2DArrayS32FloatLevel:
  case NVPTXISD::Tex2DArrayS32FloatGrad:
  case NVPTXISD::Tex2DArrayU32S32:
  case NVPTXISD::Tex2DArrayU32Float:
  case NVPTXISD::Tex2DArrayU32FloatLevel:
  case NVPTXISD::Tex2DArrayU32FloatGrad:
  case NVPTXISD::Tex3DFloatS32:
  case NVPTXISD::Tex3DFloatFloat:
  case NVPTXISD::Tex3DFloatFloatLevel:
  case NVPTXISD::Tex3DFloatFloatGrad:
  case NVPTXISD::Tex3DS32S32:
  case NVPTXISD::Tex3DS32Float:
  case NVPTXISD::Tex3DS32FloatLevel:
  case NVPTXISD::Tex3DS32FloatGrad:
  case NVPTXISD::Tex3DU32S32:
  case NVPTXISD::Tex3DU32Float:
  case NVPTXISD::Tex3DU32FloatLevel:
  case NVPTXISD::Tex3DU32FloatGrad:
  case NVPTXISD::TexCubeFloatFloat:
  case NVPTXISD::TexCubeFloatFloatLevel:
  case NVPTXISD::TexCubeS32Float:
  case NVPTXISD::TexCubeS32FloatLevel:
  case NVPTXISD::TexCubeU32Float:
  case NVPTXISD::TexCubeU32FloatLevel:
  case NVPTXISD::TexCubeArrayFloatFloat:
  case NVPTXISD::TexCubeArrayFloatFloatLevel:
  case NVPTXISD::TexCubeArrayS32Float:
  case NVPTXISD::TexCubeArrayS32FloatLevel:
  case NVPTXISD::TexCubeArrayU32Float:
  case NVPTXISD::TexCubeArrayU32FloatLevel:
  case NVPTXISD::Tld4R2DFloatFloat:
  case NVPTXISD::Tld4G2DFloatFloat:
  case NVPTXISD::Tld4B2DFloatFloat:
  case NVPTXISD::Tld4A2DFloatFloat:
  case NVPTXISD::Tld4R2DS64Float:
  case NVPTXISD::Tld4G2DS64Float:
  case NVPTXISD::Tld4B2DS64Float:
  case NVPTXISD::Tld4A2DS64Float:
  case NVPTXISD::Tld4R2DU64Float:
  case NVPTXISD::Tld4G2DU64Float:
  case NVPTXISD::Tld4B2DU64Float:
  case NVPTXISD::Tld4A2DU64Float:
  case NVPTXISD::TexUnified1DFloatS32:
  case NVPTXISD::TexUnified1DFloatFloat:
  case NVPTXISD::TexUnified1DFloatFloatLevel:
  case NVPTXISD::TexUnified1DFloatFloatGrad:
  case NVPTXISD::TexUnified1DS32S32:
  case NVPTXISD::TexUnified1DS32Float:
  case NVPTXISD::TexUnified1DS32FloatLevel:
  case NVPTXISD::TexUnified1DS32FloatGrad:
  case NVPTXISD::TexUnified1DU32S32:
  case NVPTXISD::TexUnified1DU32Float:
  case NVPTXISD::TexUnified1DU32FloatLevel:
  case NVPTXISD::TexUnified1DU32FloatGrad:
  case NVPTXISD::TexUnified1DArrayFloatS32:
  case NVPTXISD::TexUnified1DArrayFloatFloat:
  case NVPTXISD::TexUnified1DArrayFloatFloatLevel:
  case NVPTXISD::TexUnified1DArrayFloatFloatGrad:
  case NVPTXISD::TexUnified1DArrayS32S32:
  case NVPTXISD::TexUnified1DArrayS32Float:
  case NVPTXISD::TexUnified1DArrayS32FloatLevel:
  case NVPTXISD::TexUnified1DArrayS32FloatGrad:
  case NVPTXISD::TexUnified1DArrayU32S32:
  case NVPTXISD::TexUnified1DArrayU32Float:
  case NVPTXISD::TexUnified1DArrayU32FloatLevel:
  case NVPTXISD::TexUnified1DArrayU32FloatGrad:
  case NVPTXISD::TexUnified2DFloatS32:
  case NVPTXISD::TexUnified2DFloatFloat:
  case NVPTXISD::TexUnified2DFloatFloatLevel:
  case NVPTXISD::TexUnified2DFloatFloatGrad:
  case NVPTXISD::TexUnified2DS32S32:
  case NVPTXISD::TexUnified2DS32Float:
  case NVPTXISD::TexUnified2DS32FloatLevel:
  case NVPTXISD::TexUnified2DS32FloatGrad:
  case NVPTXISD::TexUnified2DU32S32:
  case NVPTXISD::TexUnified2DU32Float:
  case NVPTXISD::TexUnified2DU32FloatLevel:
  case NVPTXISD::TexUnified2DU32FloatGrad:
  case NVPTXISD::TexUnified2DArrayFloatS32:
  case NVPTXISD::TexUnified2DArrayFloatFloat:
  case NVPTXISD::TexUnified2DArrayFloatFloatLevel:
  case NVPTXISD::TexUnified2DArrayFloatFloatGrad:
  case NVPTXISD::TexUnified2DArrayS32S32:
  case NVPTXISD::TexUnified2DArrayS32Float:
  case NVPTXISD::TexUnified2DArrayS32FloatLevel:
  case NVPTXISD::TexUnified2DArrayS32FloatGrad:
  case NVPTXISD::TexUnified2DArrayU32S32:
  case NVPTXISD::TexUnified2DArrayU32Float:
  case NVPTXISD::TexUnified2DArrayU32FloatLevel:
  case NVPTXISD::TexUnified2DArrayU32FloatGrad:
  case NVPTXISD::TexUnified3DFloatS32:
  case NVPTXISD::TexUnified3DFloatFloat:
  case NVPTXISD::TexUnified3DFloatFloatLevel:
  case NVPTXISD::TexUnified3DFloatFloatGrad:
  case NVPTXISD::TexUnified3DS32S32:
  case NVPTXISD::TexUnified3DS32Float:
  case NVPTXISD::TexUnified3DS32FloatLevel:
  case NVPTXISD::TexUnified3DS32FloatGrad:
  case NVPTXISD::TexUnified3DU32S32:
  case NVPTXISD::TexUnified3DU32Float:
  case NVPTXISD::TexUnified3DU32FloatLevel:
  case NVPTXISD::TexUnified3DU32FloatGrad:
  case NVPTXISD::TexUnifiedCubeFloatFloat:
  case NVPTXISD::TexUnifiedCubeFloatFloatLevel:
  case NVPTXISD::TexUnifiedCubeS32Float:
  case NVPTXISD::TexUnifiedCubeS32FloatLevel:
  case NVPTXISD::TexUnifiedCubeU32Float:
  case NVPTXISD::TexUnifiedCubeU32FloatLevel:
  case NVPTXISD::TexUnifiedCubeArrayFloatFloat:
  case NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel:
  case NVPTXISD::TexUnifiedCubeArrayS32Float:
  case NVPTXISD::TexUnifiedCubeArrayS32FloatLevel:
  case NVPTXISD::TexUnifiedCubeArrayU32Float:
  case NVPTXISD::TexUnifiedCubeArrayU32FloatLevel:
  case NVPTXISD::Tld4UnifiedR2DFloatFloat:
  case NVPTXISD::Tld4UnifiedG2DFloatFloat:
  case NVPTXISD::Tld4UnifiedB2DFloatFloat:
  case NVPTXISD::Tld4UnifiedA2DFloatFloat:
  case NVPTXISD::Tld4UnifiedR2DS64Float:
  case NVPTXISD::Tld4UnifiedG2DS64Float:
  case NVPTXISD::Tld4UnifiedB2DS64Float:
  case NVPTXISD::Tld4UnifiedA2DS64Float:
  case NVPTXISD::Tld4UnifiedR2DU64Float:
  case NVPTXISD::Tld4UnifiedG2DU64Float:
  case NVPTXISD::Tld4UnifiedB2DU64Float:
  case NVPTXISD::Tld4UnifiedA2DU64Float:
    if (tryTextureIntrinsic(N))
      return;
    break;
  case NVPTXISD::Suld1DI8Clamp:
  case NVPTXISD::Suld1DI16Clamp:
  case NVPTXISD::Suld1DI32Clamp:
  case NVPTXISD::Suld1DI64Clamp:
  case NVPTXISD::Suld1DV2I8Clamp:
  case NVPTXISD::Suld1DV2I16Clamp:
  case NVPTXISD::Suld1DV2I32Clamp:
  case NVPTXISD::Suld1DV2I64Clamp:
  case NVPTXISD::Suld1DV4I8Clamp:
  case NVPTXISD::Suld1DV4I16Clamp:
  case NVPTXISD::Suld1DV4I32Clamp:
  case NVPTXISD::Suld1DArrayI8Clamp:
  case NVPTXISD::Suld1DArrayI16Clamp:
  case NVPTXISD::Suld1DArrayI32Clamp:
  case NVPTXISD::Suld1DArrayI64Clamp:
  case NVPTXISD::Suld1DArrayV2I8Clamp:
  case NVPTXISD::Suld1DArrayV2I16Clamp:
  case NVPTXISD::Suld1DArrayV2I32Clamp:
  case NVPTXISD::Suld1DArrayV2I64Clamp:
  case NVPTXISD::Suld1DArrayV4I8Clamp:
  case NVPTXISD::Suld1DArrayV4I16Clamp:
  case NVPTXISD::Suld1DArrayV4I32Clamp:
  case NVPTXISD::Suld2DI8Clamp:
  case NVPTXISD::Suld2DI16Clamp:
  case NVPTXISD::Suld2DI32Clamp:
  case NVPTXISD::Suld2DI64Clamp:
  case NVPTXISD::Suld2DV2I8Clamp:
  case NVPTXISD::Suld2DV2I16Clamp:
  case NVPTXISD::Suld2DV2I32Clamp:
  case NVPTXISD::Suld2DV2I64Clamp:
  case NVPTXISD::Suld2DV4I8Clamp:
  case NVPTXISD::Suld2DV4I16Clamp:
  case NVPTXISD::Suld2DV4I32Clamp:
  case NVPTXISD::Suld2DArrayI8Clamp:
  case NVPTXISD::Suld2DArrayI16Clamp:
  case NVPTXISD::Suld2DArrayI32Clamp:
  case NVPTXISD::Suld2DArrayI64Clamp:
  case NVPTXISD::Suld2DArrayV2I8Clamp:
  case NVPTXISD::Suld2DArrayV2I16Clamp:
  case NVPTXISD::Suld2DArrayV2I32Clamp:
  case NVPTXISD::Suld2DArrayV2I64Clamp:
  case NVPTXISD::Suld2DArrayV4I8Clamp:
  case NVPTXISD::Suld2DArrayV4I16Clamp:
  case NVPTXISD::Suld2DArrayV4I32Clamp:
  case NVPTXISD::Suld3DI8Clamp:
  case NVPTXISD::Suld3DI16Clamp:
  case NVPTXISD::Suld3DI32Clamp:
  case NVPTXISD::Suld3DI64Clamp:
  case NVPTXISD::Suld3DV2I8Clamp:
  case NVPTXISD::Suld3DV2I16Clamp:
  case NVPTXISD::Suld3DV2I32Clamp:
  case NVPTXISD::Suld3DV2I64Clamp:
  case NVPTXISD::Suld3DV4I8Clamp:
  case NVPTXISD::Suld3DV4I16Clamp:
  case NVPTXISD::Suld3DV4I32Clamp:
  case NVPTXISD::Suld1DI8Trap:
  case NVPTXISD::Suld1DI16Trap:
  case NVPTXISD::Suld1DI32Trap:
  case NVPTXISD::Suld1DI64Trap:
  case NVPTXISD::Suld1DV2I8Trap:
  case NVPTXISD::Suld1DV2I16Trap:
  case NVPTXISD::Suld1DV2I32Trap:
  case NVPTXISD::Suld1DV2I64Trap:
  case NVPTXISD::Suld1DV4I8Trap:
  case NVPTXISD::Suld1DV4I16Trap:
  case NVPTXISD::Suld1DV4I32Trap:
  case NVPTXISD::Suld1DArrayI8Trap:
  case NVPTXISD::Suld1DArrayI16Trap:
  case NVPTXISD::Suld1DArrayI32Trap:
  case NVPTXISD::Suld1DArrayI64Trap:
  case NVPTXISD::Suld1DArrayV2I8Trap:
  case NVPTXISD::Suld1DArrayV2I16Trap:
  case NVPTXISD::Suld1DArrayV2I32Trap:
  case NVPTXISD::Suld1DArrayV2I64Trap:
  case NVPTXISD::Suld1DArrayV4I8Trap:
  case NVPTXISD::Suld1DArrayV4I16Trap:
  case NVPTXISD::Suld1DArrayV4I32Trap:
  case NVPTXISD::Suld2DI8Trap:
  case NVPTXISD::Suld2DI16Trap:
  case NVPTXISD::Suld2DI32Trap:
  case NVPTXISD::Suld2DI64Trap:
  case NVPTXISD::Suld2DV2I8Trap:
  case NVPTXISD::Suld2DV2I16Trap:
  case NVPTXISD::Suld2DV2I32Trap:
  case NVPTXISD::Suld2DV2I64Trap:
  case NVPTXISD::Suld2DV4I8Trap:
  case NVPTXISD::Suld2DV4I16Trap:
  case NVPTXISD::Suld2DV4I32Trap:
  case NVPTXISD::Suld2DArrayI8Trap:
  case NVPTXISD::Suld2DArrayI16Trap:
  case NVPTXISD::Suld2DArrayI32Trap:
  case NVPTXISD::Suld2DArrayI64Trap:
  case NVPTXISD::Suld2DArrayV2I8Trap:
  case NVPTXISD::Suld2DArrayV2I16Trap:
  case NVPTXISD::Suld2DArrayV2I32Trap:
  case NVPTXISD::Suld2DArrayV2I64Trap:
  case NVPTXISD::Suld2DArrayV4I8Trap:
  case NVPTXISD::Suld2DArrayV4I16Trap:
  case NVPTXISD::Suld2DArrayV4I32Trap:
  case NVPTXISD::Suld3DI8Trap:
  case NVPTXISD::Suld3DI16Trap:
  case NVPTXISD::Suld3DI32Trap:
  case NVPTXISD::Suld3DI64Trap:
  case NVPTXISD::Suld3DV2I8Trap:
  case NVPTXISD::Suld3DV2I16Trap:
  case NVPTXISD::Suld3DV2I32Trap:
  case NVPTXISD::Suld3DV2I64Trap:
  case NVPTXISD::Suld3DV4I8Trap:
  case NVPTXISD::Suld3DV4I16Trap:
  case NVPTXISD::Suld3DV4I32Trap:
  case NVPTXISD::Suld1DI8Zero:
  case NVPTXISD::Suld1DI16Zero:
  case NVPTXISD::Suld1DI32Zero:
  case NVPTXISD::Suld1DI64Zero:
  case NVPTXISD::Suld1DV2I8Zero:
  case NVPTXISD::Suld1DV2I16Zero:
  case NVPTXISD::Suld1DV2I32Zero:
  case NVPTXISD::Suld1DV2I64Zero:
  case NVPTXISD::Suld1DV4I8Zero:
  case NVPTXISD::Suld1DV4I16Zero:
  case NVPTXISD::Suld1DV4I32Zero:
  case NVPTXISD::Suld1DArrayI8Zero:
  case NVPTXISD::Suld1DArrayI16Zero:
  case NVPTXISD::Suld1DArrayI32Zero:
  case NVPTXISD::Suld1DArrayI64Zero:
  case NVPTXISD::Suld1DArrayV2I8Zero:
  case NVPTXISD::Suld1DArrayV2I16Zero:
  case NVPTXISD::Suld1DArrayV2I32Zero:
  case NVPTXISD::Suld1DArrayV2I64Zero:
  case NVPTXISD::Suld1DArrayV4I8Zero:
  case NVPTXISD::Suld1DArrayV4I16Zero:
  case NVPTXISD::Suld1DArrayV4I32Zero:
  case NVPTXISD::Suld2DI8Zero:
  case NVPTXISD::Suld2DI16Zero:
  case NVPTXISD::Suld2DI32Zero:
  case NVPTXISD::Suld2DI64Zero:
  case NVPTXISD::Suld2DV2I8Zero:
  case NVPTXISD::Suld2DV2I16Zero:
  case NVPTXISD::Suld2DV2I32Zero:
  case NVPTXISD::Suld2DV2I64Zero:
  case NVPTXISD::Suld2DV4I8Zero:
  case NVPTXISD::Suld2DV4I16Zero:
  case NVPTXISD::Suld2DV4I32Zero:
  case NVPTXISD::Suld2DArrayI8Zero:
  case NVPTXISD::Suld2DArrayI16Zero:
  case NVPTXISD::Suld2DArrayI32Zero:
  case NVPTXISD::Suld2DArrayI64Zero:
  case NVPTXISD::Suld2DArrayV2I8Zero:
  case NVPTXISD::Suld2DArrayV2I16Zero:
  case NVPTXISD::Suld2DArrayV2I32Zero:
  case NVPTXISD::Suld2DArrayV2I64Zero:
  case NVPTXISD::Suld2DArrayV4I8Zero:
  case NVPTXISD::Suld2DArrayV4I16Zero:
  case NVPTXISD::Suld2DArrayV4I32Zero:
  case NVPTXISD::Suld3DI8Zero:
  case NVPTXISD::Suld3DI16Zero:
  case NVPTXISD::Suld3DI32Zero:
  case NVPTXISD::Suld3DI64Zero:
  case NVPTXISD::Suld3DV2I8Zero:
  case NVPTXISD::Suld3DV2I16Zero:
  case NVPTXISD::Suld3DV2I32Zero:
  case NVPTXISD::Suld3DV2I64Zero:
  case NVPTXISD::Suld3DV4I8Zero:
  case NVPTXISD::Suld3DV4I16Zero:
  case NVPTXISD::Suld3DV4I32Zero:
    if (trySurfaceIntrinsic(N))
      return;
    break;
  case ISD::AND:
  case ISD::SRA:
  case ISD::SRL:
    // Try to select BFE
    if (tryBFE(N))
      return;
    break;
  case ISD::ADDRSPACECAST:
    SelectAddrSpaceCast(N);
    return;
  case ISD::ConstantFP:
    if (tryConstantFP16(N))
      return;
    break;
  default:
    break;
  }
  SelectCode(N);
}

bool NVPTXDAGToDAGISel::tryIntrinsicChain(SDNode *N) {
  unsigned IID = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  switch (IID) {
  default:
    return false;
  case Intrinsic::nvvm_ldg_global_f:
  case Intrinsic::nvvm_ldg_global_i:
  case Intrinsic::nvvm_ldg_global_p:
  case Intrinsic::nvvm_ldu_global_f:
  case Intrinsic::nvvm_ldu_global_i:
  case Intrinsic::nvvm_ldu_global_p:
    return tryLDGLDU(N);
  }
}

// There's no way to specify FP16 immediates in .f16 ops, so we have to
// load them into an .f16 register first.
bool NVPTXDAGToDAGISel::tryConstantFP16(SDNode *N) {
  if (N->getValueType(0) != MVT::f16)
    return false;
  SDValue Val = CurDAG->getTargetConstantFP(
      cast<ConstantFPSDNode>(N)->getValueAPF(), SDLoc(N), MVT::f16);
  SDNode *LoadConstF16 =
      CurDAG->getMachineNode(NVPTX::LOAD_CONST_F16, SDLoc(N), MVT::f16, Val);
  ReplaceNode(N, LoadConstF16);
  return true;
}

// Map ISD:CONDCODE value to appropriate CmpMode expected by
// NVPTXInstPrinter::printCmpMode()
static unsigned getPTXCmpMode(const CondCodeSDNode &CondCode, bool FTZ) {
  using NVPTX::PTXCmpMode::CmpMode;
  unsigned PTXCmpMode = [](ISD::CondCode CC) {
    switch (CC) {
    default:
      llvm_unreachable("Unexpected condition code.");
    case ISD::SETOEQ:
      return CmpMode::EQ;
    case ISD::SETOGT:
      return CmpMode::GT;
    case ISD::SETOGE:
      return CmpMode::GE;
    case ISD::SETOLT:
      return CmpMode::LT;
    case ISD::SETOLE:
      return CmpMode::LE;
    case ISD::SETONE:
      return CmpMode::NE;
    case ISD::SETO:
      return CmpMode::NUM;
    case ISD::SETUO:
      return CmpMode::NotANumber;
    case ISD::SETUEQ:
      return CmpMode::EQU;
    case ISD::SETUGT:
      return CmpMode::GTU;
    case ISD::SETUGE:
      return CmpMode::GEU;
    case ISD::SETULT:
      return CmpMode::LTU;
    case ISD::SETULE:
      return CmpMode::LEU;
    case ISD::SETUNE:
      return CmpMode::NEU;
    case ISD::SETEQ:
      return CmpMode::EQ;
    case ISD::SETGT:
      return CmpMode::GT;
    case ISD::SETGE:
      return CmpMode::GE;
    case ISD::SETLT:
      return CmpMode::LT;
    case ISD::SETLE:
      return CmpMode::LE;
    case ISD::SETNE:
      return CmpMode::NE;
    }
  }(CondCode.get());

  if (FTZ)
    PTXCmpMode |= NVPTX::PTXCmpMode::FTZ_FLAG;

  return PTXCmpMode;
}

bool NVPTXDAGToDAGISel::SelectSETP_F16X2(SDNode *N) {
  unsigned PTXCmpMode =
      getPTXCmpMode(*cast<CondCodeSDNode>(N->getOperand(2)), useF32FTZ());
  SDLoc DL(N);
  SDNode *SetP = CurDAG->getMachineNode(
      NVPTX::SETP_f16x2rr, DL, MVT::i1, MVT::i1, N->getOperand(0),
      N->getOperand(1), CurDAG->getTargetConstant(PTXCmpMode, DL, MVT::i32));
  ReplaceNode(N, SetP);
  return true;
}

// Find all instances of extract_vector_elt that use this v2f16 vector
// and coalesce them into a scattering move instruction.
bool NVPTXDAGToDAGISel::tryEXTRACT_VECTOR_ELEMENT(SDNode *N) {
  SDValue Vector = N->getOperand(0);

  // We only care about f16x2 as it's the only real vector type we
  // need to deal with.
  if (Vector.getSimpleValueType() != MVT::v2f16)
    return false;

  // Find and record all uses of this vector that extract element 0 or 1.
  SmallVector<SDNode *, 4> E0, E1;
  for (const auto &U : Vector.getNode()->uses()) {
    if (U->getOpcode() != ISD::EXTRACT_VECTOR_ELT)
      continue;
    if (U->getOperand(0) != Vector)
      continue;
    if (const ConstantSDNode *IdxConst =
            dyn_cast<ConstantSDNode>(U->getOperand(1))) {
      if (IdxConst->getZExtValue() == 0)
        E0.push_back(U);
      else if (IdxConst->getZExtValue() == 1)
        E1.push_back(U);
      else
        llvm_unreachable("Invalid vector index.");
    }
  }

  // There's no point scattering f16x2 if we only ever access one
  // element of it.
  if (E0.empty() || E1.empty())
    return false;

  unsigned Op = NVPTX::SplitF16x2;
  // If the vector has been BITCAST'ed from i32, we can use original
  // value directly and avoid register-to-register move.
  SDValue Source = Vector;
  if (Vector->getOpcode() == ISD::BITCAST) {
    Op = NVPTX::SplitI32toF16x2;
    Source = Vector->getOperand(0);
  }
  // Merge (f16 extractelt(V, 0), f16 extractelt(V,1))
  // into f16,f16 SplitF16x2(V)
  SDNode *ScatterOp =
      CurDAG->getMachineNode(Op, SDLoc(N), MVT::f16, MVT::f16, Source);
  for (auto *Node : E0)
    ReplaceUses(SDValue(Node, 0), SDValue(ScatterOp, 0));
  for (auto *Node : E1)
    ReplaceUses(SDValue(Node, 0), SDValue(ScatterOp, 1));

  return true;
}

static unsigned int getCodeAddrSpace(MemSDNode *N) {
  const Value *Src = N->getMemOperand()->getValue();

  if (!Src)
    return NVPTX::PTXLdStInstCode::GENERIC;

  if (auto *PT = dyn_cast<PointerType>(Src->getType())) {
    switch (PT->getAddressSpace()) {
    case llvm::ADDRESS_SPACE_LOCAL: return NVPTX::PTXLdStInstCode::LOCAL;
    case llvm::ADDRESS_SPACE_GLOBAL: return NVPTX::PTXLdStInstCode::GLOBAL;
    case llvm::ADDRESS_SPACE_SHARED: return NVPTX::PTXLdStInstCode::SHARED;
    case llvm::ADDRESS_SPACE_GENERIC: return NVPTX::PTXLdStInstCode::GENERIC;
    case llvm::ADDRESS_SPACE_PARAM: return NVPTX::PTXLdStInstCode::PARAM;
    case llvm::ADDRESS_SPACE_CONST: return NVPTX::PTXLdStInstCode::CONSTANT;
    default: break;
    }
  }
  return NVPTX::PTXLdStInstCode::GENERIC;
}

static bool canLowerToLDG(MemSDNode *N, const NVPTXSubtarget &Subtarget,
                          unsigned CodeAddrSpace, MachineFunction *F) {
  // We use ldg (i.e. ld.global.nc) for invariant loads from the global address
  // space.
  //
  // We have two ways of identifying invariant loads: Loads may be explicitly
  // marked as invariant, or we may infer them to be invariant.
  //
  // We currently infer invariance for loads from
  //  - constant global variables, and
  //  - kernel function pointer params that are noalias (i.e. __restrict) and
  //    never written to.
  //
  // TODO: Perform a more powerful invariance analysis (ideally IPO, and ideally
  // not during the SelectionDAG phase).
  //
  // TODO: Infer invariance only at -O2.  We still want to use ldg at -O0 for
  // explicitly invariant loads because these are how clang tells us to use ldg
  // when the user uses a builtin.
  if (!Subtarget.hasLDG() || CodeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL)
    return false;

  if (N->isInvariant())
    return true;

  bool IsKernelFn = isKernelFunction(F->getFunction());

  // We use GetUnderlyingObjects() here instead of GetUnderlyingObject() mainly
  // because the former looks through phi nodes while the latter does not. We
  // need to look through phi nodes to handle pointer induction variables.
  SmallVector<Value *, 8> Objs;
  GetUnderlyingObjects(const_cast<Value *>(N->getMemOperand()->getValue()),
                       Objs, F->getDataLayout());

  return all_of(Objs, [&](Value *V) {
    if (auto *A = dyn_cast<const Argument>(V))
      return IsKernelFn && A->onlyReadsMemory() && A->hasNoAliasAttr();
    if (auto *GV = dyn_cast<const GlobalVariable>(V))
      return GV->isConstant();
    return false;
  });
}

bool NVPTXDAGToDAGISel::tryIntrinsicNoChain(SDNode *N) {
  unsigned IID = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
  switch (IID) {
  default:
    return false;
  case Intrinsic::nvvm_texsurf_handle_internal:
    SelectTexSurfHandle(N);
    return true;
  }
}

void NVPTXDAGToDAGISel::SelectTexSurfHandle(SDNode *N) {
  // Op 0 is the intrinsic ID
  SDValue Wrapper = N->getOperand(1);
  SDValue GlobalVal = Wrapper.getOperand(0);
  ReplaceNode(N, CurDAG->getMachineNode(NVPTX::texsurf_handles, SDLoc(N),
                                        MVT::i64, GlobalVal));
}

void NVPTXDAGToDAGISel::SelectAddrSpaceCast(SDNode *N) {
  SDValue Src = N->getOperand(0);
  AddrSpaceCastSDNode *CastN = cast<AddrSpaceCastSDNode>(N);
  unsigned SrcAddrSpace = CastN->getSrcAddressSpace();
  unsigned DstAddrSpace = CastN->getDestAddressSpace();
  assert(SrcAddrSpace != DstAddrSpace &&
         "addrspacecast must be between different address spaces");

  if (DstAddrSpace == ADDRESS_SPACE_GENERIC) {
    // Specific to generic
    unsigned Opc;
    switch (SrcAddrSpace) {
    default: report_fatal_error("Bad address space in addrspacecast");
    case ADDRESS_SPACE_GLOBAL:
      Opc = TM.is64Bit() ? NVPTX::cvta_global_yes_64 : NVPTX::cvta_global_yes;
      break;
    case ADDRESS_SPACE_SHARED:
      Opc = TM.is64Bit() ? (useShortPointers() ? NVPTX::cvta_shared_yes_6432
                                               : NVPTX::cvta_shared_yes_64)
                         : NVPTX::cvta_shared_yes;
      break;
    case ADDRESS_SPACE_CONST:
      Opc = TM.is64Bit() ? (useShortPointers() ? NVPTX::cvta_const_yes_6432
                                               : NVPTX::cvta_const_yes_64)
                         : NVPTX::cvta_const_yes;
      break;
    case ADDRESS_SPACE_LOCAL:
      Opc = TM.is64Bit() ? (useShortPointers() ? NVPTX::cvta_local_yes_6432
                                               : NVPTX::cvta_local_yes_64)
                         : NVPTX::cvta_local_yes;
      break;
    }
    ReplaceNode(N, CurDAG->getMachineNode(Opc, SDLoc(N), N->getValueType(0),
                                          Src));
    return;
  } else {
    // Generic to specific
    if (SrcAddrSpace != 0)
      report_fatal_error("Cannot cast between two non-generic address spaces");
    unsigned Opc;
    switch (DstAddrSpace) {
    default: report_fatal_error("Bad address space in addrspacecast");
    case ADDRESS_SPACE_GLOBAL:
      Opc = TM.is64Bit() ? NVPTX::cvta_to_global_yes_64
                         : NVPTX::cvta_to_global_yes;
      break;
    case ADDRESS_SPACE_SHARED:
      Opc = TM.is64Bit() ? (useShortPointers() ? NVPTX::cvta_to_shared_yes_3264
                                                : NVPTX::cvta_to_shared_yes_64)
                         : NVPTX::cvta_to_shared_yes;
      break;
    case ADDRESS_SPACE_CONST:
      Opc = TM.is64Bit() ? (useShortPointers() ? NVPTX::cvta_to_const_yes_3264
                                             : NVPTX::cvta_to_const_yes_64)
                         : NVPTX::cvta_to_const_yes;
      break;
    case ADDRESS_SPACE_LOCAL:
      Opc = TM.is64Bit() ? (useShortPointers() ? NVPTX::cvta_to_local_yes_3264
                                               : NVPTX::cvta_to_local_yes_64)
                         : NVPTX::cvta_to_local_yes;
      break;
    case ADDRESS_SPACE_PARAM:
      Opc = TM.is64Bit() ? NVPTX::nvvm_ptr_gen_to_param_64
                         : NVPTX::nvvm_ptr_gen_to_param;
      break;
    }
    ReplaceNode(N, CurDAG->getMachineNode(Opc, SDLoc(N), N->getValueType(0),
                                          Src));
    return;
  }
}

// Helper function template to reduce amount of boilerplate code for
// opcode selection.
static Optional<unsigned> pickOpcodeForVT(
    MVT::SimpleValueType VT, unsigned Opcode_i8, unsigned Opcode_i16,
    unsigned Opcode_i32, Optional<unsigned> Opcode_i64, unsigned Opcode_f16,
    unsigned Opcode_f16x2, unsigned Opcode_f32, Optional<unsigned> Opcode_f64) {
  switch (VT) {
  case MVT::i1:
  case MVT::i8:
    return Opcode_i8;
  case MVT::i16:
    return Opcode_i16;
  case MVT::i32:
    return Opcode_i32;
  case MVT::i64:
    return Opcode_i64;
  case MVT::f16:
    return Opcode_f16;
  case MVT::v2f16:
    return Opcode_f16x2;
  case MVT::f32:
    return Opcode_f32;
  case MVT::f64:
    return Opcode_f64;
  default:
    return None;
  }
}

bool NVPTXDAGToDAGISel::tryLoad(SDNode *N) {
  SDLoc dl(N);
  MemSDNode *LD = cast<MemSDNode>(N);
  assert(LD->readMem() && "Expected load");
  LoadSDNode *PlainLoad = dyn_cast<LoadSDNode>(N);
  EVT LoadedVT = LD->getMemoryVT();
  SDNode *NVPTXLD = nullptr;

  // do not support pre/post inc/dec
  if (PlainLoad && PlainLoad->isIndexed())
    return false;

  if (!LoadedVT.isSimple())
    return false;

  AtomicOrdering Ordering = LD->getOrdering();
  // In order to lower atomic loads with stronger guarantees we would need to
  // use load.acquire or insert fences. However these features were only added
  // with PTX ISA 6.0 / sm_70.
  // TODO: Check if we can actually use the new instructions and implement them.
  if (isStrongerThanMonotonic(Ordering))
    return false;

  // Address Space Setting
  unsigned int CodeAddrSpace = getCodeAddrSpace(LD);
  if (canLowerToLDG(LD, *Subtarget, CodeAddrSpace, MF)) {
    return tryLDGLDU(N);
  }

  unsigned int PointerSize =
      CurDAG->getDataLayout().getPointerSizeInBits(LD->getAddressSpace());

  // Volatile Setting
  // - .volatile is only available for .global and .shared
  // - .volatile has the same memory synchronization semantics as .relaxed.sys
  bool isVolatile = LD->isVolatile() || Ordering == AtomicOrdering::Monotonic;
  if (CodeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::SHARED &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::GENERIC)
    isVolatile = false;

  // Type Setting: fromType + fromTypeWidth
  //
  // Sign   : ISD::SEXTLOAD
  // Unsign : ISD::ZEXTLOAD, ISD::NON_EXTLOAD or ISD::EXTLOAD and the
  //          type is integer
  // Float  : ISD::NON_EXTLOAD or ISD::EXTLOAD and the type is float
  MVT SimpleVT = LoadedVT.getSimpleVT();
  MVT ScalarVT = SimpleVT.getScalarType();
  // Read at least 8 bits (predicates are stored as 8-bit values)
  unsigned fromTypeWidth = std::max(8U, ScalarVT.getSizeInBits());
  unsigned int fromType;

  // Vector Setting
  unsigned vecType = NVPTX::PTXLdStInstCode::Scalar;
  if (SimpleVT.isVector()) {
    assert(LoadedVT == MVT::v2f16 && "Unexpected vector type");
    // v2f16 is loaded using ld.b32
    fromTypeWidth = 32;
  }

  if (PlainLoad && (PlainLoad->getExtensionType() == ISD::SEXTLOAD))
    fromType = NVPTX::PTXLdStInstCode::Signed;
  else if (ScalarVT.isFloatingPoint())
    // f16 uses .b16 as its storage type.
    fromType = ScalarVT.SimpleTy == MVT::f16 ? NVPTX::PTXLdStInstCode::Untyped
                                             : NVPTX::PTXLdStInstCode::Float;
  else
    fromType = NVPTX::PTXLdStInstCode::Unsigned;

  // Create the machine instruction DAG
  SDValue Chain = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue Addr;
  SDValue Offset, Base;
  Optional<unsigned> Opcode;
  MVT::SimpleValueType TargetVT = LD->getSimpleValueType(0).SimpleTy;

  if (SelectDirectAddr(N1, Addr)) {
    Opcode = pickOpcodeForVT(
        TargetVT, NVPTX::LD_i8_avar, NVPTX::LD_i16_avar, NVPTX::LD_i32_avar,
        NVPTX::LD_i64_avar, NVPTX::LD_f16_avar, NVPTX::LD_f16x2_avar,
        NVPTX::LD_f32_avar, NVPTX::LD_f64_avar);
    if (!Opcode)
      return false;
    SDValue Ops[] = { getI32Imm(isVolatile, dl), getI32Imm(CodeAddrSpace, dl),
                      getI32Imm(vecType, dl), getI32Imm(fromType, dl),
                      getI32Imm(fromTypeWidth, dl), Addr, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode.getValue(), dl, TargetVT,
                                     MVT::Other, Ops);
  } else if (PointerSize == 64 ? SelectADDRsi64(N1.getNode(), N1, Base, Offset)
                               : SelectADDRsi(N1.getNode(), N1, Base, Offset)) {
    Opcode = pickOpcodeForVT(TargetVT, NVPTX::LD_i8_asi, NVPTX::LD_i16_asi,
                                 NVPTX::LD_i32_asi, NVPTX::LD_i64_asi,
                                 NVPTX::LD_f16_asi, NVPTX::LD_f16x2_asi,
                                 NVPTX::LD_f32_asi, NVPTX::LD_f64_asi);
    if (!Opcode)
      return false;
    SDValue Ops[] = { getI32Imm(isVolatile, dl), getI32Imm(CodeAddrSpace, dl),
                      getI32Imm(vecType, dl), getI32Imm(fromType, dl),
                      getI32Imm(fromTypeWidth, dl), Base, Offset, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode.getValue(), dl, TargetVT,
                                     MVT::Other, Ops);
  } else if (PointerSize == 64 ? SelectADDRri64(N1.getNode(), N1, Base, Offset)
                               : SelectADDRri(N1.getNode(), N1, Base, Offset)) {
    if (PointerSize == 64)
      Opcode = pickOpcodeForVT(
          TargetVT, NVPTX::LD_i8_ari_64, NVPTX::LD_i16_ari_64,
          NVPTX::LD_i32_ari_64, NVPTX::LD_i64_ari_64, NVPTX::LD_f16_ari_64,
          NVPTX::LD_f16x2_ari_64, NVPTX::LD_f32_ari_64, NVPTX::LD_f64_ari_64);
    else
      Opcode = pickOpcodeForVT(
          TargetVT, NVPTX::LD_i8_ari, NVPTX::LD_i16_ari, NVPTX::LD_i32_ari,
          NVPTX::LD_i64_ari, NVPTX::LD_f16_ari, NVPTX::LD_f16x2_ari,
          NVPTX::LD_f32_ari, NVPTX::LD_f64_ari);
    if (!Opcode)
      return false;
    SDValue Ops[] = { getI32Imm(isVolatile, dl), getI32Imm(CodeAddrSpace, dl),
                      getI32Imm(vecType, dl), getI32Imm(fromType, dl),
                      getI32Imm(fromTypeWidth, dl), Base, Offset, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode.getValue(), dl, TargetVT,
                                     MVT::Other, Ops);
  } else {
    if (PointerSize == 64)
      Opcode = pickOpcodeForVT(
          TargetVT, NVPTX::LD_i8_areg_64, NVPTX::LD_i16_areg_64,
          NVPTX::LD_i32_areg_64, NVPTX::LD_i64_areg_64, NVPTX::LD_f16_areg_64,
          NVPTX::LD_f16x2_areg_64, NVPTX::LD_f32_areg_64,
          NVPTX::LD_f64_areg_64);
    else
      Opcode = pickOpcodeForVT(
          TargetVT, NVPTX::LD_i8_areg, NVPTX::LD_i16_areg, NVPTX::LD_i32_areg,
          NVPTX::LD_i64_areg, NVPTX::LD_f16_areg, NVPTX::LD_f16x2_areg,
          NVPTX::LD_f32_areg, NVPTX::LD_f64_areg);
    if (!Opcode)
      return false;
    SDValue Ops[] = { getI32Imm(isVolatile, dl), getI32Imm(CodeAddrSpace, dl),
                      getI32Imm(vecType, dl), getI32Imm(fromType, dl),
                      getI32Imm(fromTypeWidth, dl), N1, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode.getValue(), dl, TargetVT,
                                     MVT::Other, Ops);
  }

  if (!NVPTXLD)
    return false;

  MachineMemOperand *MemRef = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(NVPTXLD), {MemRef});

  ReplaceNode(N, NVPTXLD);
  return true;
}

bool NVPTXDAGToDAGISel::tryLoadVector(SDNode *N) {

  SDValue Chain = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  SDValue Addr, Offset, Base;
  Optional<unsigned> Opcode;
  SDLoc DL(N);
  SDNode *LD;
  MemSDNode *MemSD = cast<MemSDNode>(N);
  EVT LoadedVT = MemSD->getMemoryVT();

  if (!LoadedVT.isSimple())
    return false;

  // Address Space Setting
  unsigned int CodeAddrSpace = getCodeAddrSpace(MemSD);
  if (canLowerToLDG(MemSD, *Subtarget, CodeAddrSpace, MF)) {
    return tryLDGLDU(N);
  }

  unsigned int PointerSize =
      CurDAG->getDataLayout().getPointerSizeInBits(MemSD->getAddressSpace());

  // Volatile Setting
  // - .volatile is only availalble for .global and .shared
  bool IsVolatile = MemSD->isVolatile();
  if (CodeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::SHARED &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::GENERIC)
    IsVolatile = false;

  // Vector Setting
  MVT SimpleVT = LoadedVT.getSimpleVT();

  // Type Setting: fromType + fromTypeWidth
  //
  // Sign   : ISD::SEXTLOAD
  // Unsign : ISD::ZEXTLOAD, ISD::NON_EXTLOAD or ISD::EXTLOAD and the
  //          type is integer
  // Float  : ISD::NON_EXTLOAD or ISD::EXTLOAD and the type is float
  MVT ScalarVT = SimpleVT.getScalarType();
  // Read at least 8 bits (predicates are stored as 8-bit values)
  unsigned FromTypeWidth = std::max(8U, ScalarVT.getSizeInBits());
  unsigned int FromType;
  // The last operand holds the original LoadSDNode::getExtensionType() value
  unsigned ExtensionType = cast<ConstantSDNode>(
      N->getOperand(N->getNumOperands() - 1))->getZExtValue();
  if (ExtensionType == ISD::SEXTLOAD)
    FromType = NVPTX::PTXLdStInstCode::Signed;
  else if (ScalarVT.isFloatingPoint())
    FromType = ScalarVT.SimpleTy == MVT::f16 ? NVPTX::PTXLdStInstCode::Untyped
                                             : NVPTX::PTXLdStInstCode::Float;
  else
    FromType = NVPTX::PTXLdStInstCode::Unsigned;

  unsigned VecType;

  switch (N->getOpcode()) {
  case NVPTXISD::LoadV2:
    VecType = NVPTX::PTXLdStInstCode::V2;
    break;
  case NVPTXISD::LoadV4:
    VecType = NVPTX::PTXLdStInstCode::V4;
    break;
  default:
    return false;
  }

  EVT EltVT = N->getValueType(0);

  // v8f16 is a special case. PTX doesn't have ld.v8.f16
  // instruction. Instead, we split the vector into v2f16 chunks and
  // load them with ld.v4.b32.
  if (EltVT == MVT::v2f16) {
    assert(N->getOpcode() == NVPTXISD::LoadV4 && "Unexpected load opcode.");
    EltVT = MVT::i32;
    FromType = NVPTX::PTXLdStInstCode::Untyped;
    FromTypeWidth = 32;
  }

  if (SelectDirectAddr(Op1, Addr)) {
    switch (N->getOpcode()) {
    default:
      return false;
    case NVPTXISD::LoadV2:
      Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                               NVPTX::LDV_i8_v2_avar, NVPTX::LDV_i16_v2_avar,
                               NVPTX::LDV_i32_v2_avar, NVPTX::LDV_i64_v2_avar,
                               NVPTX::LDV_f16_v2_avar, NVPTX::LDV_f16x2_v2_avar,
                               NVPTX::LDV_f32_v2_avar, NVPTX::LDV_f64_v2_avar);
      break;
    case NVPTXISD::LoadV4:
      Opcode =
          pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v4_avar,
                          NVPTX::LDV_i16_v4_avar, NVPTX::LDV_i32_v4_avar, None,
                          NVPTX::LDV_f16_v4_avar, NVPTX::LDV_f16x2_v4_avar,
                          NVPTX::LDV_f32_v4_avar, None);
      break;
    }
    if (!Opcode)
      return false;
    SDValue Ops[] = { getI32Imm(IsVolatile, DL), getI32Imm(CodeAddrSpace, DL),
                      getI32Imm(VecType, DL), getI32Imm(FromType, DL),
                      getI32Imm(FromTypeWidth, DL), Addr, Chain };
    LD = CurDAG->getMachineNode(Opcode.getValue(), DL, N->getVTList(), Ops);
  } else if (PointerSize == 64
                 ? SelectADDRsi64(Op1.getNode(), Op1, Base, Offset)
                 : SelectADDRsi(Op1.getNode(), Op1, Base, Offset)) {
    switch (N->getOpcode()) {
    default:
      return false;
    case NVPTXISD::LoadV2:
      Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                               NVPTX::LDV_i8_v2_asi, NVPTX::LDV_i16_v2_asi,
                               NVPTX::LDV_i32_v2_asi, NVPTX::LDV_i64_v2_asi,
                               NVPTX::LDV_f16_v2_asi, NVPTX::LDV_f16x2_v2_asi,
                               NVPTX::LDV_f32_v2_asi, NVPTX::LDV_f64_v2_asi);
      break;
    case NVPTXISD::LoadV4:
      Opcode =
          pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v4_asi,
                          NVPTX::LDV_i16_v4_asi, NVPTX::LDV_i32_v4_asi, None,
                          NVPTX::LDV_f16_v4_asi, NVPTX::LDV_f16x2_v4_asi,
                          NVPTX::LDV_f32_v4_asi, None);
      break;
    }
    if (!Opcode)
      return false;
    SDValue Ops[] = { getI32Imm(IsVolatile, DL), getI32Imm(CodeAddrSpace, DL),
                      getI32Imm(VecType, DL), getI32Imm(FromType, DL),
                      getI32Imm(FromTypeWidth, DL), Base, Offset, Chain };
    LD = CurDAG->getMachineNode(Opcode.getValue(), DL, N->getVTList(), Ops);
  } else if (PointerSize == 64
                 ? SelectADDRri64(Op1.getNode(), Op1, Base, Offset)
                 : SelectADDRri(Op1.getNode(), Op1, Base, Offset)) {
    if (PointerSize == 64) {
      switch (N->getOpcode()) {
      default:
        return false;
      case NVPTXISD::LoadV2:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v2_ari_64,
            NVPTX::LDV_i16_v2_ari_64, NVPTX::LDV_i32_v2_ari_64,
            NVPTX::LDV_i64_v2_ari_64, NVPTX::LDV_f16_v2_ari_64,
            NVPTX::LDV_f16x2_v2_ari_64, NVPTX::LDV_f32_v2_ari_64,
            NVPTX::LDV_f64_v2_ari_64);
        break;
      case NVPTXISD::LoadV4:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v4_ari_64,
            NVPTX::LDV_i16_v4_ari_64, NVPTX::LDV_i32_v4_ari_64, None,
            NVPTX::LDV_f16_v4_ari_64, NVPTX::LDV_f16x2_v4_ari_64,
            NVPTX::LDV_f32_v4_ari_64, None);
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return false;
      case NVPTXISD::LoadV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::LDV_i8_v2_ari, NVPTX::LDV_i16_v2_ari,
                                 NVPTX::LDV_i32_v2_ari, NVPTX::LDV_i64_v2_ari,
                                 NVPTX::LDV_f16_v2_ari, NVPTX::LDV_f16x2_v2_ari,
                                 NVPTX::LDV_f32_v2_ari, NVPTX::LDV_f64_v2_ari);
        break;
      case NVPTXISD::LoadV4:
        Opcode =
            pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v4_ari,
                            NVPTX::LDV_i16_v4_ari, NVPTX::LDV_i32_v4_ari, None,
                            NVPTX::LDV_f16_v4_ari, NVPTX::LDV_f16x2_v4_ari,
                            NVPTX::LDV_f32_v4_ari, None);
        break;
      }
    }
    if (!Opcode)
      return false;
    SDValue Ops[] = { getI32Imm(IsVolatile, DL), getI32Imm(CodeAddrSpace, DL),
                      getI32Imm(VecType, DL), getI32Imm(FromType, DL),
                      getI32Imm(FromTypeWidth, DL), Base, Offset, Chain };

    LD = CurDAG->getMachineNode(Opcode.getValue(), DL, N->getVTList(), Ops);
  } else {
    if (PointerSize == 64) {
      switch (N->getOpcode()) {
      default:
        return false;
      case NVPTXISD::LoadV2:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v2_areg_64,
            NVPTX::LDV_i16_v2_areg_64, NVPTX::LDV_i32_v2_areg_64,
            NVPTX::LDV_i64_v2_areg_64, NVPTX::LDV_f16_v2_areg_64,
            NVPTX::LDV_f16x2_v2_areg_64, NVPTX::LDV_f32_v2_areg_64,
            NVPTX::LDV_f64_v2_areg_64);
        break;
      case NVPTXISD::LoadV4:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v4_areg_64,
            NVPTX::LDV_i16_v4_areg_64, NVPTX::LDV_i32_v4_areg_64, None,
            NVPTX::LDV_f16_v4_areg_64, NVPTX::LDV_f16x2_v4_areg_64,
            NVPTX::LDV_f32_v4_areg_64, None);
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return false;
      case NVPTXISD::LoadV2:
        Opcode =
            pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v2_areg,
                            NVPTX::LDV_i16_v2_areg, NVPTX::LDV_i32_v2_areg,
                            NVPTX::LDV_i64_v2_areg, NVPTX::LDV_f16_v2_areg,
                            NVPTX::LDV_f16x2_v2_areg, NVPTX::LDV_f32_v2_areg,
                            NVPTX::LDV_f64_v2_areg);
        break;
      case NVPTXISD::LoadV4:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::LDV_i8_v4_areg,
            NVPTX::LDV_i16_v4_areg, NVPTX::LDV_i32_v4_areg, None,
            NVPTX::LDV_f16_v4_areg, NVPTX::LDV_f16x2_v4_areg,
            NVPTX::LDV_f32_v4_areg, None);
        break;
      }
    }
    if (!Opcode)
      return false;
    SDValue Ops[] = { getI32Imm(IsVolatile, DL), getI32Imm(CodeAddrSpace, DL),
                      getI32Imm(VecType, DL), getI32Imm(FromType, DL),
                      getI32Imm(FromTypeWidth, DL), Op1, Chain };
    LD = CurDAG->getMachineNode(Opcode.getValue(), DL, N->getVTList(), Ops);
  }

  MachineMemOperand *MemRef = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(LD), {MemRef});

  ReplaceNode(N, LD);
  return true;
}

bool NVPTXDAGToDAGISel::tryLDGLDU(SDNode *N) {

  SDValue Chain = N->getOperand(0);
  SDValue Op1;
  MemSDNode *Mem;
  bool IsLDG = true;

  // If this is an LDG intrinsic, the address is the third operand. If its an
  // LDG/LDU SD node (from custom vector handling), then its the second operand
  if (N->getOpcode() == ISD::INTRINSIC_W_CHAIN) {
    Op1 = N->getOperand(2);
    Mem = cast<MemIntrinsicSDNode>(N);
    unsigned IID = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
    switch (IID) {
    default:
      return false;
    case Intrinsic::nvvm_ldg_global_f:
    case Intrinsic::nvvm_ldg_global_i:
    case Intrinsic::nvvm_ldg_global_p:
      IsLDG = true;
      break;
    case Intrinsic::nvvm_ldu_global_f:
    case Intrinsic::nvvm_ldu_global_i:
    case Intrinsic::nvvm_ldu_global_p:
      IsLDG = false;
      break;
    }
  } else {
    Op1 = N->getOperand(1);
    Mem = cast<MemSDNode>(N);
  }

  Optional<unsigned> Opcode;
  SDLoc DL(N);
  SDNode *LD;
  SDValue Base, Offset, Addr;

  EVT EltVT = Mem->getMemoryVT();
  unsigned NumElts = 1;
  if (EltVT.isVector()) {
    NumElts = EltVT.getVectorNumElements();
    EltVT = EltVT.getVectorElementType();
    // vectors of f16 are loaded/stored as multiples of v2f16 elements.
    if (EltVT == MVT::f16 && N->getValueType(0) == MVT::v2f16) {
      assert(NumElts % 2 == 0 && "Vector must have even number of elements");
      EltVT = MVT::v2f16;
      NumElts /= 2;
    }
  }

  // Build the "promoted" result VTList for the load. If we are really loading
  // i8s, then the return type will be promoted to i16 since we do not expose
  // 8-bit registers in NVPTX.
  EVT NodeVT = (EltVT == MVT::i8) ? MVT::i16 : EltVT;
  SmallVector<EVT, 5> InstVTs;
  for (unsigned i = 0; i != NumElts; ++i) {
    InstVTs.push_back(NodeVT);
  }
  InstVTs.push_back(MVT::Other);
  SDVTList InstVTList = CurDAG->getVTList(InstVTs);

  if (SelectDirectAddr(Op1, Addr)) {
    switch (N->getOpcode()) {
    default:
      return false;
    case ISD::LOAD:
    case ISD::INTRINSIC_W_CHAIN:
      if (IsLDG)
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                     NVPTX::INT_PTX_LDG_GLOBAL_i8avar,
                                     NVPTX::INT_PTX_LDG_GLOBAL_i16avar,
                                     NVPTX::INT_PTX_LDG_GLOBAL_i32avar,
                                     NVPTX::INT_PTX_LDG_GLOBAL_i64avar,
                                     NVPTX::INT_PTX_LDG_GLOBAL_f16avar,
                                     NVPTX::INT_PTX_LDG_GLOBAL_f16x2avar,
                                     NVPTX::INT_PTX_LDG_GLOBAL_f32avar,
                                     NVPTX::INT_PTX_LDG_GLOBAL_f64avar);
      else
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                     NVPTX::INT_PTX_LDU_GLOBAL_i8avar,
                                     NVPTX::INT_PTX_LDU_GLOBAL_i16avar,
                                     NVPTX::INT_PTX_LDU_GLOBAL_i32avar,
                                     NVPTX::INT_PTX_LDU_GLOBAL_i64avar,
                                     NVPTX::INT_PTX_LDU_GLOBAL_f16avar,
                                     NVPTX::INT_PTX_LDU_GLOBAL_f16x2avar,
                                     NVPTX::INT_PTX_LDU_GLOBAL_f32avar,
                                     NVPTX::INT_PTX_LDU_GLOBAL_f64avar);
      break;
    case NVPTXISD::LoadV2:
    case NVPTXISD::LDGV2:
      Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                   NVPTX::INT_PTX_LDG_G_v2i8_ELE_avar,
                                   NVPTX::INT_PTX_LDG_G_v2i16_ELE_avar,
                                   NVPTX::INT_PTX_LDG_G_v2i32_ELE_avar,
                                   NVPTX::INT_PTX_LDG_G_v2i64_ELE_avar,
                                   NVPTX::INT_PTX_LDG_G_v2f16_ELE_avar,
                                   NVPTX::INT_PTX_LDG_G_v2f16x2_ELE_avar,
                                   NVPTX::INT_PTX_LDG_G_v2f32_ELE_avar,
                                   NVPTX::INT_PTX_LDG_G_v2f64_ELE_avar);
      break;
    case NVPTXISD::LDUV2:
      Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                   NVPTX::INT_PTX_LDU_G_v2i8_ELE_avar,
                                   NVPTX::INT_PTX_LDU_G_v2i16_ELE_avar,
                                   NVPTX::INT_PTX_LDU_G_v2i32_ELE_avar,
                                   NVPTX::INT_PTX_LDU_G_v2i64_ELE_avar,
                                   NVPTX::INT_PTX_LDU_G_v2f16_ELE_avar,
                                   NVPTX::INT_PTX_LDU_G_v2f16x2_ELE_avar,
                                   NVPTX::INT_PTX_LDU_G_v2f32_ELE_avar,
                                   NVPTX::INT_PTX_LDU_G_v2f64_ELE_avar);
      break;
    case NVPTXISD::LoadV4:
    case NVPTXISD::LDGV4:
      Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                               NVPTX::INT_PTX_LDG_G_v4i8_ELE_avar,
                               NVPTX::INT_PTX_LDG_G_v4i16_ELE_avar,
                               NVPTX::INT_PTX_LDG_G_v4i32_ELE_avar, None,
                               NVPTX::INT_PTX_LDG_G_v4f16_ELE_avar,
                               NVPTX::INT_PTX_LDG_G_v4f16x2_ELE_avar,
                               NVPTX::INT_PTX_LDG_G_v4f32_ELE_avar, None);
      break;
    case NVPTXISD::LDUV4:
      Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                               NVPTX::INT_PTX_LDU_G_v4i8_ELE_avar,
                               NVPTX::INT_PTX_LDU_G_v4i16_ELE_avar,
                               NVPTX::INT_PTX_LDU_G_v4i32_ELE_avar, None,
                               NVPTX::INT_PTX_LDU_G_v4f16_ELE_avar,
                               NVPTX::INT_PTX_LDU_G_v4f16x2_ELE_avar,
                               NVPTX::INT_PTX_LDU_G_v4f32_ELE_avar, None);
      break;
    }
    if (!Opcode)
      return false;
    SDValue Ops[] = { Addr, Chain };
    LD = CurDAG->getMachineNode(Opcode.getValue(), DL, InstVTList, Ops);
  } else if (TM.is64Bit() ? SelectADDRri64(Op1.getNode(), Op1, Base, Offset)
                          : SelectADDRri(Op1.getNode(), Op1, Base, Offset)) {
    if (TM.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return false;
      case ISD::LOAD:
      case ISD::INTRINSIC_W_CHAIN:
        if (IsLDG)
          Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i8ari64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i16ari64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i32ari64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i64ari64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f16ari64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f16x2ari64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f32ari64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f64ari64);
        else
          Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i8ari64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i16ari64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i32ari64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i64ari64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f16ari64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f16x2ari64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f32ari64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f64ari64);
        break;
      case NVPTXISD::LoadV2:
      case NVPTXISD::LDGV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                     NVPTX::INT_PTX_LDG_G_v2i8_ELE_ari64,
                                     NVPTX::INT_PTX_LDG_G_v2i16_ELE_ari64,
                                     NVPTX::INT_PTX_LDG_G_v2i32_ELE_ari64,
                                     NVPTX::INT_PTX_LDG_G_v2i64_ELE_ari64,
                                     NVPTX::INT_PTX_LDG_G_v2f16_ELE_ari64,
                                     NVPTX::INT_PTX_LDG_G_v2f16x2_ELE_ari64,
                                     NVPTX::INT_PTX_LDG_G_v2f32_ELE_ari64,
                                     NVPTX::INT_PTX_LDG_G_v2f64_ELE_ari64);
        break;
      case NVPTXISD::LDUV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                     NVPTX::INT_PTX_LDU_G_v2i8_ELE_ari64,
                                     NVPTX::INT_PTX_LDU_G_v2i16_ELE_ari64,
                                     NVPTX::INT_PTX_LDU_G_v2i32_ELE_ari64,
                                     NVPTX::INT_PTX_LDU_G_v2i64_ELE_ari64,
                                     NVPTX::INT_PTX_LDU_G_v2f16_ELE_ari64,
                                     NVPTX::INT_PTX_LDU_G_v2f16x2_ELE_ari64,
                                     NVPTX::INT_PTX_LDU_G_v2f32_ELE_ari64,
                                     NVPTX::INT_PTX_LDU_G_v2f64_ELE_ari64);
        break;
      case NVPTXISD::LoadV4:
      case NVPTXISD::LDGV4:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDG_G_v4i8_ELE_ari64,
                                 NVPTX::INT_PTX_LDG_G_v4i16_ELE_ari64,
                                 NVPTX::INT_PTX_LDG_G_v4i32_ELE_ari64, None,
                                 NVPTX::INT_PTX_LDG_G_v4f16_ELE_ari64,
                                 NVPTX::INT_PTX_LDG_G_v4f16x2_ELE_ari64,
                                 NVPTX::INT_PTX_LDG_G_v4f32_ELE_ari64, None);
        break;
      case NVPTXISD::LDUV4:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDU_G_v4i8_ELE_ari64,
                                 NVPTX::INT_PTX_LDU_G_v4i16_ELE_ari64,
                                 NVPTX::INT_PTX_LDU_G_v4i32_ELE_ari64, None,
                                 NVPTX::INT_PTX_LDU_G_v4f16_ELE_ari64,
                                 NVPTX::INT_PTX_LDU_G_v4f16x2_ELE_ari64,
                                 NVPTX::INT_PTX_LDU_G_v4f32_ELE_ari64, None);
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return false;
      case ISD::LOAD:
      case ISD::INTRINSIC_W_CHAIN:
        if (IsLDG)
          Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i8ari,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i16ari,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i32ari,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i64ari,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f16ari,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f16x2ari,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f32ari,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f64ari);
        else
          Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i8ari,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i16ari,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i32ari,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i64ari,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f16ari,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f16x2ari,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f32ari,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f64ari);
        break;
      case NVPTXISD::LoadV2:
      case NVPTXISD::LDGV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                     NVPTX::INT_PTX_LDG_G_v2i8_ELE_ari32,
                                     NVPTX::INT_PTX_LDG_G_v2i16_ELE_ari32,
                                     NVPTX::INT_PTX_LDG_G_v2i32_ELE_ari32,
                                     NVPTX::INT_PTX_LDG_G_v2i64_ELE_ari32,
                                     NVPTX::INT_PTX_LDG_G_v2f16_ELE_ari32,
                                     NVPTX::INT_PTX_LDG_G_v2f16x2_ELE_ari32,
                                     NVPTX::INT_PTX_LDG_G_v2f32_ELE_ari32,
                                     NVPTX::INT_PTX_LDG_G_v2f64_ELE_ari32);
        break;
      case NVPTXISD::LDUV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                     NVPTX::INT_PTX_LDU_G_v2i8_ELE_ari32,
                                     NVPTX::INT_PTX_LDU_G_v2i16_ELE_ari32,
                                     NVPTX::INT_PTX_LDU_G_v2i32_ELE_ari32,
                                     NVPTX::INT_PTX_LDU_G_v2i64_ELE_ari32,
                                     NVPTX::INT_PTX_LDU_G_v2f16_ELE_ari32,
                                     NVPTX::INT_PTX_LDU_G_v2f16x2_ELE_ari32,
                                     NVPTX::INT_PTX_LDU_G_v2f32_ELE_ari32,
                                     NVPTX::INT_PTX_LDU_G_v2f64_ELE_ari32);
        break;
      case NVPTXISD::LoadV4:
      case NVPTXISD::LDGV4:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDG_G_v4i8_ELE_ari32,
                                 NVPTX::INT_PTX_LDG_G_v4i16_ELE_ari32,
                                 NVPTX::INT_PTX_LDG_G_v4i32_ELE_ari32, None,
                                 NVPTX::INT_PTX_LDG_G_v4f16_ELE_ari32,
                                 NVPTX::INT_PTX_LDG_G_v4f16x2_ELE_ari32,
                                 NVPTX::INT_PTX_LDG_G_v4f32_ELE_ari32, None);
        break;
      case NVPTXISD::LDUV4:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDU_G_v4i8_ELE_ari32,
                                 NVPTX::INT_PTX_LDU_G_v4i16_ELE_ari32,
                                 NVPTX::INT_PTX_LDU_G_v4i32_ELE_ari32, None,
                                 NVPTX::INT_PTX_LDU_G_v4f16_ELE_ari32,
                                 NVPTX::INT_PTX_LDU_G_v4f16x2_ELE_ari32,
                                 NVPTX::INT_PTX_LDU_G_v4f32_ELE_ari32, None);
        break;
      }
    }
    if (!Opcode)
      return false;
    SDValue Ops[] = {Base, Offset, Chain};
    LD = CurDAG->getMachineNode(Opcode.getValue(), DL, InstVTList, Ops);
  } else {
    if (TM.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return false;
      case ISD::LOAD:
      case ISD::INTRINSIC_W_CHAIN:
        if (IsLDG)
          Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i8areg64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i16areg64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i32areg64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_i64areg64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f16areg64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f16x2areg64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f32areg64,
                                       NVPTX::INT_PTX_LDG_GLOBAL_f64areg64);
        else
          Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i8areg64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i16areg64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i32areg64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_i64areg64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f16areg64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f16x2areg64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f32areg64,
                                       NVPTX::INT_PTX_LDU_GLOBAL_f64areg64);
        break;
      case NVPTXISD::LoadV2:
      case NVPTXISD::LDGV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                     NVPTX::INT_PTX_LDG_G_v2i8_ELE_areg64,
                                     NVPTX::INT_PTX_LDG_G_v2i16_ELE_areg64,
                                     NVPTX::INT_PTX_LDG_G_v2i32_ELE_areg64,
                                     NVPTX::INT_PTX_LDG_G_v2i64_ELE_areg64,
                                     NVPTX::INT_PTX_LDG_G_v2f16_ELE_areg64,
                                     NVPTX::INT_PTX_LDG_G_v2f16x2_ELE_areg64,
                                     NVPTX::INT_PTX_LDG_G_v2f32_ELE_areg64,
                                     NVPTX::INT_PTX_LDG_G_v2f64_ELE_areg64);
        break;
      case NVPTXISD::LDUV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                     NVPTX::INT_PTX_LDU_G_v2i8_ELE_areg64,
                                     NVPTX::INT_PTX_LDU_G_v2i16_ELE_areg64,
                                     NVPTX::INT_PTX_LDU_G_v2i32_ELE_areg64,
                                     NVPTX::INT_PTX_LDU_G_v2i64_ELE_areg64,
                                     NVPTX::INT_PTX_LDU_G_v2f16_ELE_areg64,
                                     NVPTX::INT_PTX_LDU_G_v2f16x2_ELE_areg64,
                                     NVPTX::INT_PTX_LDU_G_v2f32_ELE_areg64,
                                     NVPTX::INT_PTX_LDU_G_v2f64_ELE_areg64);
        break;
      case NVPTXISD::LoadV4:
      case NVPTXISD::LDGV4:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDG_G_v4i8_ELE_areg64,
                                 NVPTX::INT_PTX_LDG_G_v4i16_ELE_areg64,
                                 NVPTX::INT_PTX_LDG_G_v4i32_ELE_areg64, None,
                                 NVPTX::INT_PTX_LDG_G_v4f16_ELE_areg64,
                                 NVPTX::INT_PTX_LDG_G_v4f16x2_ELE_areg64,
                                 NVPTX::INT_PTX_LDG_G_v4f32_ELE_areg64, None);
        break;
      case NVPTXISD::LDUV4:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDU_G_v4i8_ELE_areg64,
                                 NVPTX::INT_PTX_LDU_G_v4i16_ELE_areg64,
                                 NVPTX::INT_PTX_LDU_G_v4i32_ELE_areg64, None,
                                 NVPTX::INT_PTX_LDU_G_v4f16_ELE_areg64,
                                 NVPTX::INT_PTX_LDU_G_v4f16x2_ELE_areg64,
                                 NVPTX::INT_PTX_LDU_G_v4f32_ELE_areg64, None);
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return false;
      case ISD::LOAD:
      case ISD::INTRINSIC_W_CHAIN:
        if (IsLDG)
          Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                   NVPTX::INT_PTX_LDG_GLOBAL_i8areg,
                                   NVPTX::INT_PTX_LDG_GLOBAL_i16areg,
                                   NVPTX::INT_PTX_LDG_GLOBAL_i32areg,
                                   NVPTX::INT_PTX_LDG_GLOBAL_i64areg,
                                   NVPTX::INT_PTX_LDG_GLOBAL_f16areg,
                                   NVPTX::INT_PTX_LDG_GLOBAL_f16x2areg,
                                   NVPTX::INT_PTX_LDG_GLOBAL_f32areg,
                                   NVPTX::INT_PTX_LDG_GLOBAL_f64areg);
        else
          Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                   NVPTX::INT_PTX_LDU_GLOBAL_i8areg,
                                   NVPTX::INT_PTX_LDU_GLOBAL_i16areg,
                                   NVPTX::INT_PTX_LDU_GLOBAL_i32areg,
                                   NVPTX::INT_PTX_LDU_GLOBAL_i64areg,
                                   NVPTX::INT_PTX_LDU_GLOBAL_f16areg,
                                   NVPTX::INT_PTX_LDU_GLOBAL_f16x2areg,
                                   NVPTX::INT_PTX_LDU_GLOBAL_f32areg,
                                   NVPTX::INT_PTX_LDU_GLOBAL_f64areg);
        break;
      case NVPTXISD::LoadV2:
      case NVPTXISD::LDGV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDG_G_v2i8_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v2i16_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v2i32_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v2i64_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v2f16_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v2f16x2_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v2f32_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v2f64_ELE_areg32);
        break;
      case NVPTXISD::LDUV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDU_G_v2i8_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v2i16_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v2i32_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v2i64_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v2f16_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v2f16x2_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v2f32_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v2f64_ELE_areg32);
        break;
      case NVPTXISD::LoadV4:
      case NVPTXISD::LDGV4:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDG_G_v4i8_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v4i16_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v4i32_ELE_areg32, None,
                                 NVPTX::INT_PTX_LDG_G_v4f16_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v4f16x2_ELE_areg32,
                                 NVPTX::INT_PTX_LDG_G_v4f32_ELE_areg32, None);
        break;
      case NVPTXISD::LDUV4:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::INT_PTX_LDU_G_v4i8_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v4i16_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v4i32_ELE_areg32, None,
                                 NVPTX::INT_PTX_LDU_G_v4f16_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v4f16x2_ELE_areg32,
                                 NVPTX::INT_PTX_LDU_G_v4f32_ELE_areg32, None);
        break;
      }
    }
    if (!Opcode)
      return false;
    SDValue Ops[] = { Op1, Chain };
    LD = CurDAG->getMachineNode(Opcode.getValue(), DL, InstVTList, Ops);
  }

  MachineMemOperand *MemRef = Mem->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(LD), {MemRef});

  // For automatic generation of LDG (through SelectLoad[Vector], not the
  // intrinsics), we may have an extending load like:
  //
  //   i32,ch = load<LD1[%data1(addrspace=1)], zext from i8> t0, t7, undef:i64
  //
  // In this case, the matching logic above will select a load for the original
  // memory type (in this case, i8) and our types will not match (the node needs
  // to return an i32 in this case). Our LDG/LDU nodes do not support the
  // concept of sign-/zero-extension, so emulate it here by adding an explicit
  // CVT instruction. Ptxas should clean up any redundancies here.

  EVT OrigType = N->getValueType(0);
  LoadSDNode *LdNode = dyn_cast<LoadSDNode>(N);

  if (OrigType != EltVT && LdNode) {
    // We have an extending-load. The instruction we selected operates on the
    // smaller type, but the SDNode we are replacing has the larger type. We
    // need to emit a CVT to make the types match.
    bool IsSigned = LdNode->getExtensionType() == ISD::SEXTLOAD;
    unsigned CvtOpc = GetConvertOpcode(OrigType.getSimpleVT(),
                                       EltVT.getSimpleVT(), IsSigned);

    // For each output value, apply the manual sign/zero-extension and make sure
    // all users of the load go through that CVT.
    for (unsigned i = 0; i != NumElts; ++i) {
      SDValue Res(LD, i);
      SDValue OrigVal(N, i);

      SDNode *CvtNode =
        CurDAG->getMachineNode(CvtOpc, DL, OrigType, Res,
                               CurDAG->getTargetConstant(NVPTX::PTXCvtMode::NONE,
                                                         DL, MVT::i32));
      ReplaceUses(OrigVal, SDValue(CvtNode, 0));
    }
  }

  ReplaceNode(N, LD);
  return true;
}

bool NVPTXDAGToDAGISel::tryStore(SDNode *N) {
  SDLoc dl(N);
  MemSDNode *ST = cast<MemSDNode>(N);
  assert(ST->writeMem() && "Expected store");
  StoreSDNode *PlainStore = dyn_cast<StoreSDNode>(N);
  AtomicSDNode *AtomicStore = dyn_cast<AtomicSDNode>(N);
  assert((PlainStore || AtomicStore) && "Expected store");
  EVT StoreVT = ST->getMemoryVT();
  SDNode *NVPTXST = nullptr;

  // do not support pre/post inc/dec
  if (PlainStore && PlainStore->isIndexed())
    return false;

  if (!StoreVT.isSimple())
    return false;

  AtomicOrdering Ordering = ST->getOrdering();
  // In order to lower atomic loads with stronger guarantees we would need to
  // use store.release or insert fences. However these features were only added
  // with PTX ISA 6.0 / sm_70.
  // TODO: Check if we can actually use the new instructions and implement them.
  if (isStrongerThanMonotonic(Ordering))
    return false;

  // Address Space Setting
  unsigned int CodeAddrSpace = getCodeAddrSpace(ST);
  unsigned int PointerSize =
      CurDAG->getDataLayout().getPointerSizeInBits(ST->getAddressSpace());

  // Volatile Setting
  // - .volatile is only available for .global and .shared
  // - .volatile has the same memory synchronization semantics as .relaxed.sys
  bool isVolatile = ST->isVolatile() || Ordering == AtomicOrdering::Monotonic;
  if (CodeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::SHARED &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::GENERIC)
    isVolatile = false;

  // Vector Setting
  MVT SimpleVT = StoreVT.getSimpleVT();
  unsigned vecType = NVPTX::PTXLdStInstCode::Scalar;

  // Type Setting: toType + toTypeWidth
  // - for integer type, always use 'u'
  //
  MVT ScalarVT = SimpleVT.getScalarType();
  unsigned toTypeWidth = ScalarVT.getSizeInBits();
  if (SimpleVT.isVector()) {
    assert(StoreVT == MVT::v2f16 && "Unexpected vector type");
    // v2f16 is stored using st.b32
    toTypeWidth = 32;
  }

  unsigned int toType;
  if (ScalarVT.isFloatingPoint())
    // f16 uses .b16 as its storage type.
    toType = ScalarVT.SimpleTy == MVT::f16 ? NVPTX::PTXLdStInstCode::Untyped
                                           : NVPTX::PTXLdStInstCode::Float;
  else
    toType = NVPTX::PTXLdStInstCode::Unsigned;

  // Create the machine instruction DAG
  SDValue Chain = ST->getChain();
  SDValue Value = PlainStore ? PlainStore->getValue() : AtomicStore->getVal();
  SDValue BasePtr = ST->getBasePtr();
  SDValue Addr;
  SDValue Offset, Base;
  Optional<unsigned> Opcode;
  MVT::SimpleValueType SourceVT =
      Value.getNode()->getSimpleValueType(0).SimpleTy;

  if (SelectDirectAddr(BasePtr, Addr)) {
    Opcode = pickOpcodeForVT(SourceVT, NVPTX::ST_i8_avar, NVPTX::ST_i16_avar,
                             NVPTX::ST_i32_avar, NVPTX::ST_i64_avar,
                             NVPTX::ST_f16_avar, NVPTX::ST_f16x2_avar,
                             NVPTX::ST_f32_avar, NVPTX::ST_f64_avar);
    if (!Opcode)
      return false;
    SDValue Ops[] = {Value,
                     getI32Imm(isVolatile, dl),
                     getI32Imm(CodeAddrSpace, dl),
                     getI32Imm(vecType, dl),
                     getI32Imm(toType, dl),
                     getI32Imm(toTypeWidth, dl),
                     Addr,
                     Chain};
    NVPTXST = CurDAG->getMachineNode(Opcode.getValue(), dl, MVT::Other, Ops);
  } else if (PointerSize == 64
                 ? SelectADDRsi64(BasePtr.getNode(), BasePtr, Base, Offset)
                 : SelectADDRsi(BasePtr.getNode(), BasePtr, Base, Offset)) {
    Opcode = pickOpcodeForVT(SourceVT, NVPTX::ST_i8_asi, NVPTX::ST_i16_asi,
                             NVPTX::ST_i32_asi, NVPTX::ST_i64_asi,
                             NVPTX::ST_f16_asi, NVPTX::ST_f16x2_asi,
                             NVPTX::ST_f32_asi, NVPTX::ST_f64_asi);
    if (!Opcode)
      return false;
    SDValue Ops[] = {Value,
                     getI32Imm(isVolatile, dl),
                     getI32Imm(CodeAddrSpace, dl),
                     getI32Imm(vecType, dl),
                     getI32Imm(toType, dl),
                     getI32Imm(toTypeWidth, dl),
                     Base,
                     Offset,
                     Chain};
    NVPTXST = CurDAG->getMachineNode(Opcode.getValue(), dl, MVT::Other, Ops);
  } else if (PointerSize == 64
                 ? SelectADDRri64(BasePtr.getNode(), BasePtr, Base, Offset)
                 : SelectADDRri(BasePtr.getNode(), BasePtr, Base, Offset)) {
    if (PointerSize == 64)
      Opcode = pickOpcodeForVT(
          SourceVT, NVPTX::ST_i8_ari_64, NVPTX::ST_i16_ari_64,
          NVPTX::ST_i32_ari_64, NVPTX::ST_i64_ari_64, NVPTX::ST_f16_ari_64,
          NVPTX::ST_f16x2_ari_64, NVPTX::ST_f32_ari_64, NVPTX::ST_f64_ari_64);
    else
      Opcode = pickOpcodeForVT(SourceVT, NVPTX::ST_i8_ari, NVPTX::ST_i16_ari,
                               NVPTX::ST_i32_ari, NVPTX::ST_i64_ari,
                               NVPTX::ST_f16_ari, NVPTX::ST_f16x2_ari,
                               NVPTX::ST_f32_ari, NVPTX::ST_f64_ari);
    if (!Opcode)
      return false;

    SDValue Ops[] = {Value,
                     getI32Imm(isVolatile, dl),
                     getI32Imm(CodeAddrSpace, dl),
                     getI32Imm(vecType, dl),
                     getI32Imm(toType, dl),
                     getI32Imm(toTypeWidth, dl),
                     Base,
                     Offset,
                     Chain};
    NVPTXST = CurDAG->getMachineNode(Opcode.getValue(), dl, MVT::Other, Ops);
  } else {
    if (PointerSize == 64)
      Opcode =
          pickOpcodeForVT(SourceVT, NVPTX::ST_i8_areg_64, NVPTX::ST_i16_areg_64,
                          NVPTX::ST_i32_areg_64, NVPTX::ST_i64_areg_64,
                          NVPTX::ST_f16_areg_64, NVPTX::ST_f16x2_areg_64,
                          NVPTX::ST_f32_areg_64, NVPTX::ST_f64_areg_64);
    else
      Opcode = pickOpcodeForVT(SourceVT, NVPTX::ST_i8_areg, NVPTX::ST_i16_areg,
                               NVPTX::ST_i32_areg, NVPTX::ST_i64_areg,
                               NVPTX::ST_f16_areg, NVPTX::ST_f16x2_areg,
                               NVPTX::ST_f32_areg, NVPTX::ST_f64_areg);
    if (!Opcode)
      return false;
    SDValue Ops[] = {Value,
                     getI32Imm(isVolatile, dl),
                     getI32Imm(CodeAddrSpace, dl),
                     getI32Imm(vecType, dl),
                     getI32Imm(toType, dl),
                     getI32Imm(toTypeWidth, dl),
                     BasePtr,
                     Chain};
    NVPTXST = CurDAG->getMachineNode(Opcode.getValue(), dl, MVT::Other, Ops);
  }

  if (!NVPTXST)
    return false;

  MachineMemOperand *MemRef = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(NVPTXST), {MemRef});
  ReplaceNode(N, NVPTXST);
  return true;
}

bool NVPTXDAGToDAGISel::tryStoreVector(SDNode *N) {
  SDValue Chain = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  SDValue Addr, Offset, Base;
  Optional<unsigned> Opcode;
  SDLoc DL(N);
  SDNode *ST;
  EVT EltVT = Op1.getValueType();
  MemSDNode *MemSD = cast<MemSDNode>(N);
  EVT StoreVT = MemSD->getMemoryVT();

  // Address Space Setting
  unsigned CodeAddrSpace = getCodeAddrSpace(MemSD);
  if (CodeAddrSpace == NVPTX::PTXLdStInstCode::CONSTANT) {
    report_fatal_error("Cannot store to pointer that points to constant "
                       "memory space");
  }
  unsigned int PointerSize =
      CurDAG->getDataLayout().getPointerSizeInBits(MemSD->getAddressSpace());

  // Volatile Setting
  // - .volatile is only availalble for .global and .shared
  bool IsVolatile = MemSD->isVolatile();
  if (CodeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::SHARED &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::GENERIC)
    IsVolatile = false;

  // Type Setting: toType + toTypeWidth
  // - for integer type, always use 'u'
  assert(StoreVT.isSimple() && "Store value is not simple");
  MVT ScalarVT = StoreVT.getSimpleVT().getScalarType();
  unsigned ToTypeWidth = ScalarVT.getSizeInBits();
  unsigned ToType;
  if (ScalarVT.isFloatingPoint())
    ToType = ScalarVT.SimpleTy == MVT::f16 ? NVPTX::PTXLdStInstCode::Untyped
                                           : NVPTX::PTXLdStInstCode::Float;
  else
    ToType = NVPTX::PTXLdStInstCode::Unsigned;

  SmallVector<SDValue, 12> StOps;
  SDValue N2;
  unsigned VecType;

  switch (N->getOpcode()) {
  case NVPTXISD::StoreV2:
    VecType = NVPTX::PTXLdStInstCode::V2;
    StOps.push_back(N->getOperand(1));
    StOps.push_back(N->getOperand(2));
    N2 = N->getOperand(3);
    break;
  case NVPTXISD::StoreV4:
    VecType = NVPTX::PTXLdStInstCode::V4;
    StOps.push_back(N->getOperand(1));
    StOps.push_back(N->getOperand(2));
    StOps.push_back(N->getOperand(3));
    StOps.push_back(N->getOperand(4));
    N2 = N->getOperand(5);
    break;
  default:
    return false;
  }

  // v8f16 is a special case. PTX doesn't have st.v8.f16
  // instruction. Instead, we split the vector into v2f16 chunks and
  // store them with st.v4.b32.
  if (EltVT == MVT::v2f16) {
    assert(N->getOpcode() == NVPTXISD::StoreV4 && "Unexpected load opcode.");
    EltVT = MVT::i32;
    ToType = NVPTX::PTXLdStInstCode::Untyped;
    ToTypeWidth = 32;
  }

  StOps.push_back(getI32Imm(IsVolatile, DL));
  StOps.push_back(getI32Imm(CodeAddrSpace, DL));
  StOps.push_back(getI32Imm(VecType, DL));
  StOps.push_back(getI32Imm(ToType, DL));
  StOps.push_back(getI32Imm(ToTypeWidth, DL));

  if (SelectDirectAddr(N2, Addr)) {
    switch (N->getOpcode()) {
    default:
      return false;
    case NVPTXISD::StoreV2:
      Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                               NVPTX::STV_i8_v2_avar, NVPTX::STV_i16_v2_avar,
                               NVPTX::STV_i32_v2_avar, NVPTX::STV_i64_v2_avar,
                               NVPTX::STV_f16_v2_avar, NVPTX::STV_f16x2_v2_avar,
                               NVPTX::STV_f32_v2_avar, NVPTX::STV_f64_v2_avar);
      break;
    case NVPTXISD::StoreV4:
      Opcode =
          pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v4_avar,
                          NVPTX::STV_i16_v4_avar, NVPTX::STV_i32_v4_avar, None,
                          NVPTX::STV_f16_v4_avar, NVPTX::STV_f16x2_v4_avar,
                          NVPTX::STV_f32_v4_avar, None);
      break;
    }
    StOps.push_back(Addr);
  } else if (PointerSize == 64 ? SelectADDRsi64(N2.getNode(), N2, Base, Offset)
                               : SelectADDRsi(N2.getNode(), N2, Base, Offset)) {
    switch (N->getOpcode()) {
    default:
      return false;
    case NVPTXISD::StoreV2:
      Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                               NVPTX::STV_i8_v2_asi, NVPTX::STV_i16_v2_asi,
                               NVPTX::STV_i32_v2_asi, NVPTX::STV_i64_v2_asi,
                               NVPTX::STV_f16_v2_asi, NVPTX::STV_f16x2_v2_asi,
                               NVPTX::STV_f32_v2_asi, NVPTX::STV_f64_v2_asi);
      break;
    case NVPTXISD::StoreV4:
      Opcode =
          pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v4_asi,
                          NVPTX::STV_i16_v4_asi, NVPTX::STV_i32_v4_asi, None,
                          NVPTX::STV_f16_v4_asi, NVPTX::STV_f16x2_v4_asi,
                          NVPTX::STV_f32_v4_asi, None);
      break;
    }
    StOps.push_back(Base);
    StOps.push_back(Offset);
  } else if (PointerSize == 64 ? SelectADDRri64(N2.getNode(), N2, Base, Offset)
                               : SelectADDRri(N2.getNode(), N2, Base, Offset)) {
    if (PointerSize == 64) {
      switch (N->getOpcode()) {
      default:
        return false;
      case NVPTXISD::StoreV2:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v2_ari_64,
            NVPTX::STV_i16_v2_ari_64, NVPTX::STV_i32_v2_ari_64,
            NVPTX::STV_i64_v2_ari_64, NVPTX::STV_f16_v2_ari_64,
            NVPTX::STV_f16x2_v2_ari_64, NVPTX::STV_f32_v2_ari_64,
            NVPTX::STV_f64_v2_ari_64);
        break;
      case NVPTXISD::StoreV4:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v4_ari_64,
            NVPTX::STV_i16_v4_ari_64, NVPTX::STV_i32_v4_ari_64, None,
            NVPTX::STV_f16_v4_ari_64, NVPTX::STV_f16x2_v4_ari_64,
            NVPTX::STV_f32_v4_ari_64, None);
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return false;
      case NVPTXISD::StoreV2:
        Opcode = pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy,
                                 NVPTX::STV_i8_v2_ari, NVPTX::STV_i16_v2_ari,
                                 NVPTX::STV_i32_v2_ari, NVPTX::STV_i64_v2_ari,
                                 NVPTX::STV_f16_v2_ari, NVPTX::STV_f16x2_v2_ari,
                                 NVPTX::STV_f32_v2_ari, NVPTX::STV_f64_v2_ari);
        break;
      case NVPTXISD::StoreV4:
        Opcode =
            pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v4_ari,
                            NVPTX::STV_i16_v4_ari, NVPTX::STV_i32_v4_ari, None,
                            NVPTX::STV_f16_v4_ari, NVPTX::STV_f16x2_v4_ari,
                            NVPTX::STV_f32_v4_ari, None);
        break;
      }
    }
    StOps.push_back(Base);
    StOps.push_back(Offset);
  } else {
    if (PointerSize == 64) {
      switch (N->getOpcode()) {
      default:
        return false;
      case NVPTXISD::StoreV2:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v2_areg_64,
            NVPTX::STV_i16_v2_areg_64, NVPTX::STV_i32_v2_areg_64,
            NVPTX::STV_i64_v2_areg_64, NVPTX::STV_f16_v2_areg_64,
            NVPTX::STV_f16x2_v2_areg_64, NVPTX::STV_f32_v2_areg_64,
            NVPTX::STV_f64_v2_areg_64);
        break;
      case NVPTXISD::StoreV4:
        Opcode = pickOpcodeForVT(
            EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v4_areg_64,
            NVPTX::STV_i16_v4_areg_64, NVPTX::STV_i32_v4_areg_64, None,
            NVPTX::STV_f16_v4_areg_64, NVPTX::STV_f16x2_v4_areg_64,
            NVPTX::STV_f32_v4_areg_64, None);
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return false;
      case NVPTXISD::StoreV2:
        Opcode =
            pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v2_areg,
                            NVPTX::STV_i16_v2_areg, NVPTX::STV_i32_v2_areg,
                            NVPTX::STV_i64_v2_areg, NVPTX::STV_f16_v2_areg,
                            NVPTX::STV_f16x2_v2_areg, NVPTX::STV_f32_v2_areg,
                            NVPTX::STV_f64_v2_areg);
        break;
      case NVPTXISD::StoreV4:
        Opcode =
            pickOpcodeForVT(EltVT.getSimpleVT().SimpleTy, NVPTX::STV_i8_v4_areg,
                            NVPTX::STV_i16_v4_areg, NVPTX::STV_i32_v4_areg, None,
                            NVPTX::STV_f16_v4_areg, NVPTX::STV_f16x2_v4_areg,
                            NVPTX::STV_f32_v4_areg, None);
        break;
      }
    }
    StOps.push_back(N2);
  }

  if (!Opcode)
    return false;

  StOps.push_back(Chain);

  ST = CurDAG->getMachineNode(Opcode.getValue(), DL, MVT::Other, StOps);

  MachineMemOperand *MemRef = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(ST), {MemRef});

  ReplaceNode(N, ST);
  return true;
}

bool NVPTXDAGToDAGISel::tryLoadParam(SDNode *Node) {
  SDValue Chain = Node->getOperand(0);
  SDValue Offset = Node->getOperand(2);
  SDValue Flag = Node->getOperand(3);
  SDLoc DL(Node);
  MemSDNode *Mem = cast<MemSDNode>(Node);

  unsigned VecSize;
  switch (Node->getOpcode()) {
  default:
    return false;
  case NVPTXISD::LoadParam:
    VecSize = 1;
    break;
  case NVPTXISD::LoadParamV2:
    VecSize = 2;
    break;
  case NVPTXISD::LoadParamV4:
    VecSize = 4;
    break;
  }

  EVT EltVT = Node->getValueType(0);
  EVT MemVT = Mem->getMemoryVT();

  Optional<unsigned> Opcode;

  switch (VecSize) {
  default:
    return false;
  case 1:
    Opcode = pickOpcodeForVT(MemVT.getSimpleVT().SimpleTy,
                             NVPTX::LoadParamMemI8, NVPTX::LoadParamMemI16,
                             NVPTX::LoadParamMemI32, NVPTX::LoadParamMemI64,
                             NVPTX::LoadParamMemF16, NVPTX::LoadParamMemF16x2,
                             NVPTX::LoadParamMemF32, NVPTX::LoadParamMemF64);
    break;
  case 2:
    Opcode =
        pickOpcodeForVT(MemVT.getSimpleVT().SimpleTy, NVPTX::LoadParamMemV2I8,
                        NVPTX::LoadParamMemV2I16, NVPTX::LoadParamMemV2I32,
                        NVPTX::LoadParamMemV2I64, NVPTX::LoadParamMemV2F16,
                        NVPTX::LoadParamMemV2F16x2, NVPTX::LoadParamMemV2F32,
                        NVPTX::LoadParamMemV2F64);
    break;
  case 4:
    Opcode = pickOpcodeForVT(
        MemVT.getSimpleVT().SimpleTy, NVPTX::LoadParamMemV4I8,
        NVPTX::LoadParamMemV4I16, NVPTX::LoadParamMemV4I32, None,
        NVPTX::LoadParamMemV4F16, NVPTX::LoadParamMemV4F16x2,
        NVPTX::LoadParamMemV4F32, None);
    break;
  }
  if (!Opcode)
    return false;

  SDVTList VTs;
  if (VecSize == 1) {
    VTs = CurDAG->getVTList(EltVT, MVT::Other, MVT::Glue);
  } else if (VecSize == 2) {
    VTs = CurDAG->getVTList(EltVT, EltVT, MVT::Other, MVT::Glue);
  } else {
    EVT EVTs[] = { EltVT, EltVT, EltVT, EltVT, MVT::Other, MVT::Glue };
    VTs = CurDAG->getVTList(EVTs);
  }

  unsigned OffsetVal = cast<ConstantSDNode>(Offset)->getZExtValue();

  SmallVector<SDValue, 2> Ops;
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, DL, MVT::i32));
  Ops.push_back(Chain);
  Ops.push_back(Flag);

  ReplaceNode(Node, CurDAG->getMachineNode(Opcode.getValue(), DL, VTs, Ops));
  return true;
}

bool NVPTXDAGToDAGISel::tryStoreRetval(SDNode *N) {
  SDLoc DL(N);
  SDValue Chain = N->getOperand(0);
  SDValue Offset = N->getOperand(1);
  unsigned OffsetVal = cast<ConstantSDNode>(Offset)->getZExtValue();
  MemSDNode *Mem = cast<MemSDNode>(N);

  // How many elements do we have?
  unsigned NumElts = 1;
  switch (N->getOpcode()) {
  default:
    return false;
  case NVPTXISD::StoreRetval:
    NumElts = 1;
    break;
  case NVPTXISD::StoreRetvalV2:
    NumElts = 2;
    break;
  case NVPTXISD::StoreRetvalV4:
    NumElts = 4;
    break;
  }

  // Build vector of operands
  SmallVector<SDValue, 6> Ops;
  for (unsigned i = 0; i < NumElts; ++i)
    Ops.push_back(N->getOperand(i + 2));
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, DL, MVT::i32));
  Ops.push_back(Chain);

  // Determine target opcode
  // If we have an i1, use an 8-bit store. The lowering code in
  // NVPTXISelLowering will have already emitted an upcast.
  Optional<unsigned> Opcode = 0;
  switch (NumElts) {
  default:
    return false;
  case 1:
    Opcode = pickOpcodeForVT(Mem->getMemoryVT().getSimpleVT().SimpleTy,
                             NVPTX::StoreRetvalI8, NVPTX::StoreRetvalI16,
                             NVPTX::StoreRetvalI32, NVPTX::StoreRetvalI64,
                             NVPTX::StoreRetvalF16, NVPTX::StoreRetvalF16x2,
                             NVPTX::StoreRetvalF32, NVPTX::StoreRetvalF64);
    break;
  case 2:
    Opcode = pickOpcodeForVT(Mem->getMemoryVT().getSimpleVT().SimpleTy,
                             NVPTX::StoreRetvalV2I8, NVPTX::StoreRetvalV2I16,
                             NVPTX::StoreRetvalV2I32, NVPTX::StoreRetvalV2I64,
                             NVPTX::StoreRetvalV2F16, NVPTX::StoreRetvalV2F16x2,
                             NVPTX::StoreRetvalV2F32, NVPTX::StoreRetvalV2F64);
    break;
  case 4:
    Opcode = pickOpcodeForVT(Mem->getMemoryVT().getSimpleVT().SimpleTy,
                             NVPTX::StoreRetvalV4I8, NVPTX::StoreRetvalV4I16,
                             NVPTX::StoreRetvalV4I32, None,
                             NVPTX::StoreRetvalV4F16, NVPTX::StoreRetvalV4F16x2,
                             NVPTX::StoreRetvalV4F32, None);
    break;
  }
  if (!Opcode)
    return false;

  SDNode *Ret = CurDAG->getMachineNode(Opcode.getValue(), DL, MVT::Other, Ops);
  MachineMemOperand *MemRef = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Ret), {MemRef});

  ReplaceNode(N, Ret);
  return true;
}

bool NVPTXDAGToDAGISel::tryStoreParam(SDNode *N) {
  SDLoc DL(N);
  SDValue Chain = N->getOperand(0);
  SDValue Param = N->getOperand(1);
  unsigned ParamVal = cast<ConstantSDNode>(Param)->getZExtValue();
  SDValue Offset = N->getOperand(2);
  unsigned OffsetVal = cast<ConstantSDNode>(Offset)->getZExtValue();
  MemSDNode *Mem = cast<MemSDNode>(N);
  SDValue Flag = N->getOperand(N->getNumOperands() - 1);

  // How many elements do we have?
  unsigned NumElts = 1;
  switch (N->getOpcode()) {
  default:
    return false;
  case NVPTXISD::StoreParamU32:
  case NVPTXISD::StoreParamS32:
  case NVPTXISD::StoreParam:
    NumElts = 1;
    break;
  case NVPTXISD::StoreParamV2:
    NumElts = 2;
    break;
  case NVPTXISD::StoreParamV4:
    NumElts = 4;
    break;
  }

  // Build vector of operands
  SmallVector<SDValue, 8> Ops;
  for (unsigned i = 0; i < NumElts; ++i)
    Ops.push_back(N->getOperand(i + 3));
  Ops.push_back(CurDAG->getTargetConstant(ParamVal, DL, MVT::i32));
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, DL, MVT::i32));
  Ops.push_back(Chain);
  Ops.push_back(Flag);

  // Determine target opcode
  // If we have an i1, use an 8-bit store. The lowering code in
  // NVPTXISelLowering will have already emitted an upcast.
  Optional<unsigned> Opcode = 0;
  switch (N->getOpcode()) {
  default:
    switch (NumElts) {
    default:
      return false;
    case 1:
      Opcode = pickOpcodeForVT(Mem->getMemoryVT().getSimpleVT().SimpleTy,
                               NVPTX::StoreParamI8, NVPTX::StoreParamI16,
                               NVPTX::StoreParamI32, NVPTX::StoreParamI64,
                               NVPTX::StoreParamF16, NVPTX::StoreParamF16x2,
                               NVPTX::StoreParamF32, NVPTX::StoreParamF64);
      break;
    case 2:
      Opcode = pickOpcodeForVT(Mem->getMemoryVT().getSimpleVT().SimpleTy,
                               NVPTX::StoreParamV2I8, NVPTX::StoreParamV2I16,
                               NVPTX::StoreParamV2I32, NVPTX::StoreParamV2I64,
                               NVPTX::StoreParamV2F16, NVPTX::StoreParamV2F16x2,
                               NVPTX::StoreParamV2F32, NVPTX::StoreParamV2F64);
      break;
    case 4:
      Opcode = pickOpcodeForVT(Mem->getMemoryVT().getSimpleVT().SimpleTy,
                               NVPTX::StoreParamV4I8, NVPTX::StoreParamV4I16,
                               NVPTX::StoreParamV4I32, None,
                               NVPTX::StoreParamV4F16, NVPTX::StoreParamV4F16x2,
                               NVPTX::StoreParamV4F32, None);
      break;
    }
    if (!Opcode)
      return false;
    break;
  // Special case: if we have a sign-extend/zero-extend node, insert the
  // conversion instruction first, and use that as the value operand to
  // the selected StoreParam node.
  case NVPTXISD::StoreParamU32: {
    Opcode = NVPTX::StoreParamI32;
    SDValue CvtNone = CurDAG->getTargetConstant(NVPTX::PTXCvtMode::NONE, DL,
                                                MVT::i32);
    SDNode *Cvt = CurDAG->getMachineNode(NVPTX::CVT_u32_u16, DL,
                                         MVT::i32, Ops[0], CvtNone);
    Ops[0] = SDValue(Cvt, 0);
    break;
  }
  case NVPTXISD::StoreParamS32: {
    Opcode = NVPTX::StoreParamI32;
    SDValue CvtNone = CurDAG->getTargetConstant(NVPTX::PTXCvtMode::NONE, DL,
                                                MVT::i32);
    SDNode *Cvt = CurDAG->getMachineNode(NVPTX::CVT_s32_s16, DL,
                                         MVT::i32, Ops[0], CvtNone);
    Ops[0] = SDValue(Cvt, 0);
    break;
  }
  }

  SDVTList RetVTs = CurDAG->getVTList(MVT::Other, MVT::Glue);
  SDNode *Ret =
      CurDAG->getMachineNode(Opcode.getValue(), DL, RetVTs, Ops);
  MachineMemOperand *MemRef = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Ret), {MemRef});

  ReplaceNode(N, Ret);
  return true;
}

bool NVPTXDAGToDAGISel::tryTextureIntrinsic(SDNode *N) {
  unsigned Opc = 0;

  switch (N->getOpcode()) {
  default: return false;
  case NVPTXISD::Tex1DFloatS32:
    Opc = NVPTX::TEX_1D_F32_S32;
    break;
  case NVPTXISD::Tex1DFloatFloat:
    Opc = NVPTX::TEX_1D_F32_F32;
    break;
  case NVPTXISD::Tex1DFloatFloatLevel:
    Opc = NVPTX::TEX_1D_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DFloatFloatGrad:
    Opc = NVPTX::TEX_1D_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DS32S32:
    Opc = NVPTX::TEX_1D_S32_S32;
    break;
  case NVPTXISD::Tex1DS32Float:
    Opc = NVPTX::TEX_1D_S32_F32;
    break;
  case NVPTXISD::Tex1DS32FloatLevel:
    Opc = NVPTX::TEX_1D_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DS32FloatGrad:
    Opc = NVPTX::TEX_1D_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DU32S32:
    Opc = NVPTX::TEX_1D_U32_S32;
    break;
  case NVPTXISD::Tex1DU32Float:
    Opc = NVPTX::TEX_1D_U32_F32;
    break;
  case NVPTXISD::Tex1DU32FloatLevel:
    Opc = NVPTX::TEX_1D_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DU32FloatGrad:
    Opc = NVPTX::TEX_1D_U32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DArrayFloatS32:
    Opc = NVPTX::TEX_1D_ARRAY_F32_S32;
    break;
  case NVPTXISD::Tex1DArrayFloatFloat:
    Opc = NVPTX::TEX_1D_ARRAY_F32_F32;
    break;
  case NVPTXISD::Tex1DArrayFloatFloatLevel:
    Opc = NVPTX::TEX_1D_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DArrayFloatFloatGrad:
    Opc = NVPTX::TEX_1D_ARRAY_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DArrayS32S32:
    Opc = NVPTX::TEX_1D_ARRAY_S32_S32;
    break;
  case NVPTXISD::Tex1DArrayS32Float:
    Opc = NVPTX::TEX_1D_ARRAY_S32_F32;
    break;
  case NVPTXISD::Tex1DArrayS32FloatLevel:
    Opc = NVPTX::TEX_1D_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DArrayS32FloatGrad:
    Opc = NVPTX::TEX_1D_ARRAY_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DArrayU32S32:
    Opc = NVPTX::TEX_1D_ARRAY_U32_S32;
    break;
  case NVPTXISD::Tex1DArrayU32Float:
    Opc = NVPTX::TEX_1D_ARRAY_U32_F32;
    break;
  case NVPTXISD::Tex1DArrayU32FloatLevel:
    Opc = NVPTX::TEX_1D_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DArrayU32FloatGrad:
    Opc = NVPTX::TEX_1D_ARRAY_U32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DFloatS32:
    Opc = NVPTX::TEX_2D_F32_S32;
    break;
  case NVPTXISD::Tex2DFloatFloat:
    Opc = NVPTX::TEX_2D_F32_F32;
    break;
  case NVPTXISD::Tex2DFloatFloatLevel:
    Opc = NVPTX::TEX_2D_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DFloatFloatGrad:
    Opc = NVPTX::TEX_2D_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DS32S32:
    Opc = NVPTX::TEX_2D_S32_S32;
    break;
  case NVPTXISD::Tex2DS32Float:
    Opc = NVPTX::TEX_2D_S32_F32;
    break;
  case NVPTXISD::Tex2DS32FloatLevel:
    Opc = NVPTX::TEX_2D_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DS32FloatGrad:
    Opc = NVPTX::TEX_2D_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DU32S32:
    Opc = NVPTX::TEX_2D_U32_S32;
    break;
  case NVPTXISD::Tex2DU32Float:
    Opc = NVPTX::TEX_2D_U32_F32;
    break;
  case NVPTXISD::Tex2DU32FloatLevel:
    Opc = NVPTX::TEX_2D_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DU32FloatGrad:
    Opc = NVPTX::TEX_2D_U32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DArrayFloatS32:
    Opc = NVPTX::TEX_2D_ARRAY_F32_S32;
    break;
  case NVPTXISD::Tex2DArrayFloatFloat:
    Opc = NVPTX::TEX_2D_ARRAY_F32_F32;
    break;
  case NVPTXISD::Tex2DArrayFloatFloatLevel:
    Opc = NVPTX::TEX_2D_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DArrayFloatFloatGrad:
    Opc = NVPTX::TEX_2D_ARRAY_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DArrayS32S32:
    Opc = NVPTX::TEX_2D_ARRAY_S32_S32;
    break;
  case NVPTXISD::Tex2DArrayS32Float:
    Opc = NVPTX::TEX_2D_ARRAY_S32_F32;
    break;
  case NVPTXISD::Tex2DArrayS32FloatLevel:
    Opc = NVPTX::TEX_2D_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DArrayS32FloatGrad:
    Opc = NVPTX::TEX_2D_ARRAY_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DArrayU32S32:
    Opc = NVPTX::TEX_2D_ARRAY_U32_S32;
    break;
  case NVPTXISD::Tex2DArrayU32Float:
    Opc = NVPTX::TEX_2D_ARRAY_U32_F32;
    break;
  case NVPTXISD::Tex2DArrayU32FloatLevel:
    Opc = NVPTX::TEX_2D_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DArrayU32FloatGrad:
    Opc = NVPTX::TEX_2D_ARRAY_U32_F32_GRAD;
    break;
  case NVPTXISD::Tex3DFloatS32:
    Opc = NVPTX::TEX_3D_F32_S32;
    break;
  case NVPTXISD::Tex3DFloatFloat:
    Opc = NVPTX::TEX_3D_F32_F32;
    break;
  case NVPTXISD::Tex3DFloatFloatLevel:
    Opc = NVPTX::TEX_3D_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex3DFloatFloatGrad:
    Opc = NVPTX::TEX_3D_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex3DS32S32:
    Opc = NVPTX::TEX_3D_S32_S32;
    break;
  case NVPTXISD::Tex3DS32Float:
    Opc = NVPTX::TEX_3D_S32_F32;
    break;
  case NVPTXISD::Tex3DS32FloatLevel:
    Opc = NVPTX::TEX_3D_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex3DS32FloatGrad:
    Opc = NVPTX::TEX_3D_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex3DU32S32:
    Opc = NVPTX::TEX_3D_U32_S32;
    break;
  case NVPTXISD::Tex3DU32Float:
    Opc = NVPTX::TEX_3D_U32_F32;
    break;
  case NVPTXISD::Tex3DU32FloatLevel:
    Opc = NVPTX::TEX_3D_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex3DU32FloatGrad:
    Opc = NVPTX::TEX_3D_U32_F32_GRAD;
    break;
  case NVPTXISD::TexCubeFloatFloat:
    Opc = NVPTX::TEX_CUBE_F32_F32;
    break;
  case NVPTXISD::TexCubeFloatFloatLevel:
    Opc = NVPTX::TEX_CUBE_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeS32Float:
    Opc = NVPTX::TEX_CUBE_S32_F32;
    break;
  case NVPTXISD::TexCubeS32FloatLevel:
    Opc = NVPTX::TEX_CUBE_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeU32Float:
    Opc = NVPTX::TEX_CUBE_U32_F32;
    break;
  case NVPTXISD::TexCubeU32FloatLevel:
    Opc = NVPTX::TEX_CUBE_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeArrayFloatFloat:
    Opc = NVPTX::TEX_CUBE_ARRAY_F32_F32;
    break;
  case NVPTXISD::TexCubeArrayFloatFloatLevel:
    Opc = NVPTX::TEX_CUBE_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeArrayS32Float:
    Opc = NVPTX::TEX_CUBE_ARRAY_S32_F32;
    break;
  case NVPTXISD::TexCubeArrayS32FloatLevel:
    Opc = NVPTX::TEX_CUBE_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeArrayU32Float:
    Opc = NVPTX::TEX_CUBE_ARRAY_U32_F32;
    break;
  case NVPTXISD::TexCubeArrayU32FloatLevel:
    Opc = NVPTX::TEX_CUBE_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tld4R2DFloatFloat:
    Opc = NVPTX::TLD4_R_2D_F32_F32;
    break;
  case NVPTXISD::Tld4G2DFloatFloat:
    Opc = NVPTX::TLD4_G_2D_F32_F32;
    break;
  case NVPTXISD::Tld4B2DFloatFloat:
    Opc = NVPTX::TLD4_B_2D_F32_F32;
    break;
  case NVPTXISD::Tld4A2DFloatFloat:
    Opc = NVPTX::TLD4_A_2D_F32_F32;
    break;
  case NVPTXISD::Tld4R2DS64Float:
    Opc = NVPTX::TLD4_R_2D_S32_F32;
    break;
  case NVPTXISD::Tld4G2DS64Float:
    Opc = NVPTX::TLD4_G_2D_S32_F32;
    break;
  case NVPTXISD::Tld4B2DS64Float:
    Opc = NVPTX::TLD4_B_2D_S32_F32;
    break;
  case NVPTXISD::Tld4A2DS64Float:
    Opc = NVPTX::TLD4_A_2D_S32_F32;
    break;
  case NVPTXISD::Tld4R2DU64Float:
    Opc = NVPTX::TLD4_R_2D_U32_F32;
    break;
  case NVPTXISD::Tld4G2DU64Float:
    Opc = NVPTX::TLD4_G_2D_U32_F32;
    break;
  case NVPTXISD::Tld4B2DU64Float:
    Opc = NVPTX::TLD4_B_2D_U32_F32;
    break;
  case NVPTXISD::Tld4A2DU64Float:
    Opc = NVPTX::TLD4_A_2D_U32_F32;
    break;
  case NVPTXISD::TexUnified1DFloatS32:
    Opc = NVPTX::TEX_UNIFIED_1D_F32_S32;
    break;
  case NVPTXISD::TexUnified1DFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_1D_F32_F32;
    break;
  case NVPTXISD::TexUnified1DFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DS32S32:
    Opc = NVPTX::TEX_UNIFIED_1D_S32_S32;
    break;
  case NVPTXISD::TexUnified1DS32Float:
    Opc = NVPTX::TEX_UNIFIED_1D_S32_F32;
    break;
  case NVPTXISD::TexUnified1DS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DU32S32:
    Opc = NVPTX::TEX_UNIFIED_1D_U32_S32;
    break;
  case NVPTXISD::TexUnified1DU32Float:
    Opc = NVPTX::TEX_UNIFIED_1D_U32_F32;
    break;
  case NVPTXISD::TexUnified1DU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DArrayFloatS32:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_F32_S32;
    break;
  case NVPTXISD::TexUnified1DArrayFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_F32_F32;
    break;
  case NVPTXISD::TexUnified1DArrayFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DArrayFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DArrayS32S32:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_S32_S32;
    break;
  case NVPTXISD::TexUnified1DArrayS32Float:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_S32_F32;
    break;
  case NVPTXISD::TexUnified1DArrayS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DArrayS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DArrayU32S32:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_U32_S32;
    break;
  case NVPTXISD::TexUnified1DArrayU32Float:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_U32_F32;
    break;
  case NVPTXISD::TexUnified1DArrayU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DArrayU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DFloatS32:
    Opc = NVPTX::TEX_UNIFIED_2D_F32_S32;
    break;
  case NVPTXISD::TexUnified2DFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_2D_F32_F32;
    break;
  case NVPTXISD::TexUnified2DFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DS32S32:
    Opc = NVPTX::TEX_UNIFIED_2D_S32_S32;
    break;
  case NVPTXISD::TexUnified2DS32Float:
    Opc = NVPTX::TEX_UNIFIED_2D_S32_F32;
    break;
  case NVPTXISD::TexUnified2DS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DU32S32:
    Opc = NVPTX::TEX_UNIFIED_2D_U32_S32;
    break;
  case NVPTXISD::TexUnified2DU32Float:
    Opc = NVPTX::TEX_UNIFIED_2D_U32_F32;
    break;
  case NVPTXISD::TexUnified2DU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DArrayFloatS32:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_F32_S32;
    break;
  case NVPTXISD::TexUnified2DArrayFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_F32_F32;
    break;
  case NVPTXISD::TexUnified2DArrayFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DArrayFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DArrayS32S32:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_S32_S32;
    break;
  case NVPTXISD::TexUnified2DArrayS32Float:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_S32_F32;
    break;
  case NVPTXISD::TexUnified2DArrayS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DArrayS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DArrayU32S32:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_U32_S32;
    break;
  case NVPTXISD::TexUnified2DArrayU32Float:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_U32_F32;
    break;
  case NVPTXISD::TexUnified2DArrayU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DArrayU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified3DFloatS32:
    Opc = NVPTX::TEX_UNIFIED_3D_F32_S32;
    break;
  case NVPTXISD::TexUnified3DFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_3D_F32_F32;
    break;
  case NVPTXISD::TexUnified3DFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_3D_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified3DFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_3D_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified3DS32S32:
    Opc = NVPTX::TEX_UNIFIED_3D_S32_S32;
    break;
  case NVPTXISD::TexUnified3DS32Float:
    Opc = NVPTX::TEX_UNIFIED_3D_S32_F32;
    break;
  case NVPTXISD::TexUnified3DS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_3D_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified3DS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_3D_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified3DU32S32:
    Opc = NVPTX::TEX_UNIFIED_3D_U32_S32;
    break;
  case NVPTXISD::TexUnified3DU32Float:
    Opc = NVPTX::TEX_UNIFIED_3D_U32_F32;
    break;
  case NVPTXISD::TexUnified3DU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_3D_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified3DU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_3D_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnifiedCubeFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_CUBE_F32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeS32Float:
    Opc = NVPTX::TEX_UNIFIED_CUBE_S32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeU32Float:
    Opc = NVPTX::TEX_UNIFIED_CUBE_U32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeArrayFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_F32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeArrayS32Float:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_S32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeArrayS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeArrayU32Float:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_U32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeArrayU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tld4UnifiedR2DFloatFloat:
    Opc = NVPTX::TLD4_UNIFIED_R_2D_F32_F32;
    break;
  case NVPTXISD::Tld4UnifiedG2DFloatFloat:
    Opc = NVPTX::TLD4_UNIFIED_G_2D_F32_F32;
    break;
  case NVPTXISD::Tld4UnifiedB2DFloatFloat:
    Opc = NVPTX::TLD4_UNIFIED_B_2D_F32_F32;
    break;
  case NVPTXISD::Tld4UnifiedA2DFloatFloat:
    Opc = NVPTX::TLD4_UNIFIED_A_2D_F32_F32;
    break;
  case NVPTXISD::Tld4UnifiedR2DS64Float:
    Opc = NVPTX::TLD4_UNIFIED_R_2D_S32_F32;
    break;
  case NVPTXISD::Tld4UnifiedG2DS64Float:
    Opc = NVPTX::TLD4_UNIFIED_G_2D_S32_F32;
    break;
  case NVPTXISD::Tld4UnifiedB2DS64Float:
    Opc = NVPTX::TLD4_UNIFIED_B_2D_S32_F32;
    break;
  case NVPTXISD::Tld4UnifiedA2DS64Float:
    Opc = NVPTX::TLD4_UNIFIED_A_2D_S32_F32;
    break;
  case NVPTXISD::Tld4UnifiedR2DU64Float:
    Opc = NVPTX::TLD4_UNIFIED_R_2D_U32_F32;
    break;
  case NVPTXISD::Tld4UnifiedG2DU64Float:
    Opc = NVPTX::TLD4_UNIFIED_G_2D_U32_F32;
    break;
  case NVPTXISD::Tld4UnifiedB2DU64Float:
    Opc = NVPTX::TLD4_UNIFIED_B_2D_U32_F32;
    break;
  case NVPTXISD::Tld4UnifiedA2DU64Float:
    Opc = NVPTX::TLD4_UNIFIED_A_2D_U32_F32;
    break;
  }

  // Copy over operands
  SmallVector<SDValue, 8> Ops(N->op_begin() + 1, N->op_end());
  Ops.push_back(N->getOperand(0)); // Move chain to the back.

  ReplaceNode(N, CurDAG->getMachineNode(Opc, SDLoc(N), N->getVTList(), Ops));
  return true;
}

bool NVPTXDAGToDAGISel::trySurfaceIntrinsic(SDNode *N) {
  unsigned Opc = 0;
  switch (N->getOpcode()) {
  default: return false;
  case NVPTXISD::Suld1DI8Clamp:
    Opc = NVPTX::SULD_1D_I8_CLAMP;
    break;
  case NVPTXISD::Suld1DI16Clamp:
    Opc = NVPTX::SULD_1D_I16_CLAMP;
    break;
  case NVPTXISD::Suld1DI32Clamp:
    Opc = NVPTX::SULD_1D_I32_CLAMP;
    break;
  case NVPTXISD::Suld1DI64Clamp:
    Opc = NVPTX::SULD_1D_I64_CLAMP;
    break;
  case NVPTXISD::Suld1DV2I8Clamp:
    Opc = NVPTX::SULD_1D_V2I8_CLAMP;
    break;
  case NVPTXISD::Suld1DV2I16Clamp:
    Opc = NVPTX::SULD_1D_V2I16_CLAMP;
    break;
  case NVPTXISD::Suld1DV2I32Clamp:
    Opc = NVPTX::SULD_1D_V2I32_CLAMP;
    break;
  case NVPTXISD::Suld1DV2I64Clamp:
    Opc = NVPTX::SULD_1D_V2I64_CLAMP;
    break;
  case NVPTXISD::Suld1DV4I8Clamp:
    Opc = NVPTX::SULD_1D_V4I8_CLAMP;
    break;
  case NVPTXISD::Suld1DV4I16Clamp:
    Opc = NVPTX::SULD_1D_V4I16_CLAMP;
    break;
  case NVPTXISD::Suld1DV4I32Clamp:
    Opc = NVPTX::SULD_1D_V4I32_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayI8Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_I8_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayI16Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_I16_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayI32Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_I32_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayI64Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_I64_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayV2I8Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V2I8_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayV2I16Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V2I16_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayV2I32Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V2I32_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayV2I64Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V2I64_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayV4I8Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V4I8_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayV4I16Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V4I16_CLAMP;
    break;
  case NVPTXISD::Suld1DArrayV4I32Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V4I32_CLAMP;
    break;
  case NVPTXISD::Suld2DI8Clamp:
    Opc = NVPTX::SULD_2D_I8_CLAMP;
    break;
  case NVPTXISD::Suld2DI16Clamp:
    Opc = NVPTX::SULD_2D_I16_CLAMP;
    break;
  case NVPTXISD::Suld2DI32Clamp:
    Opc = NVPTX::SULD_2D_I32_CLAMP;
    break;
  case NVPTXISD::Suld2DI64Clamp:
    Opc = NVPTX::SULD_2D_I64_CLAMP;
    break;
  case NVPTXISD::Suld2DV2I8Clamp:
    Opc = NVPTX::SULD_2D_V2I8_CLAMP;
    break;
  case NVPTXISD::Suld2DV2I16Clamp:
    Opc = NVPTX::SULD_2D_V2I16_CLAMP;
    break;
  case NVPTXISD::Suld2DV2I32Clamp:
    Opc = NVPTX::SULD_2D_V2I32_CLAMP;
    break;
  case NVPTXISD::Suld2DV2I64Clamp:
    Opc = NVPTX::SULD_2D_V2I64_CLAMP;
    break;
  case NVPTXISD::Suld2DV4I8Clamp:
    Opc = NVPTX::SULD_2D_V4I8_CLAMP;
    break;
  case NVPTXISD::Suld2DV4I16Clamp:
    Opc = NVPTX::SULD_2D_V4I16_CLAMP;
    break;
  case NVPTXISD::Suld2DV4I32Clamp:
    Opc = NVPTX::SULD_2D_V4I32_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayI8Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_I8_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayI16Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_I16_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayI32Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_I32_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayI64Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_I64_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayV2I8Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V2I8_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayV2I16Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V2I16_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayV2I32Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V2I32_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayV2I64Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V2I64_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayV4I8Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V4I8_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayV4I16Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V4I16_CLAMP;
    break;
  case NVPTXISD::Suld2DArrayV4I32Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V4I32_CLAMP;
    break;
  case NVPTXISD::Suld3DI8Clamp:
    Opc = NVPTX::SULD_3D_I8_CLAMP;
    break;
  case NVPTXISD::Suld3DI16Clamp:
    Opc = NVPTX::SULD_3D_I16_CLAMP;
    break;
  case NVPTXISD::Suld3DI32Clamp:
    Opc = NVPTX::SULD_3D_I32_CLAMP;
    break;
  case NVPTXISD::Suld3DI64Clamp:
    Opc = NVPTX::SULD_3D_I64_CLAMP;
    break;
  case NVPTXISD::Suld3DV2I8Clamp:
    Opc = NVPTX::SULD_3D_V2I8_CLAMP;
    break;
  case NVPTXISD::Suld3DV2I16Clamp:
    Opc = NVPTX::SULD_3D_V2I16_CLAMP;
    break;
  case NVPTXISD::Suld3DV2I32Clamp:
    Opc = NVPTX::SULD_3D_V2I32_CLAMP;
    break;
  case NVPTXISD::Suld3DV2I64Clamp:
    Opc = NVPTX::SULD_3D_V2I64_CLAMP;
    break;
  case NVPTXISD::Suld3DV4I8Clamp:
    Opc = NVPTX::SULD_3D_V4I8_CLAMP;
    break;
  case NVPTXISD::Suld3DV4I16Clamp:
    Opc = NVPTX::SULD_3D_V4I16_CLAMP;
    break;
  case NVPTXISD::Suld3DV4I32Clamp:
    Opc = NVPTX::SULD_3D_V4I32_CLAMP;
    break;
  case NVPTXISD::Suld1DI8Trap:
    Opc = NVPTX::SULD_1D_I8_TRAP;
    break;
  case NVPTXISD::Suld1DI16Trap:
    Opc = NVPTX::SULD_1D_I16_TRAP;
    break;
  case NVPTXISD::Suld1DI32Trap:
    Opc = NVPTX::SULD_1D_I32_TRAP;
    break;
  case NVPTXISD::Suld1DI64Trap:
    Opc = NVPTX::SULD_1D_I64_TRAP;
    break;
  case NVPTXISD::Suld1DV2I8Trap:
    Opc = NVPTX::SULD_1D_V2I8_TRAP;
    break;
  case NVPTXISD::Suld1DV2I16Trap:
    Opc = NVPTX::SULD_1D_V2I16_TRAP;
    break;
  case NVPTXISD::Suld1DV2I32Trap:
    Opc = NVPTX::SULD_1D_V2I32_TRAP;
    break;
  case NVPTXISD::Suld1DV2I64Trap:
    Opc = NVPTX::SULD_1D_V2I64_TRAP;
    break;
  case NVPTXISD::Suld1DV4I8Trap:
    Opc = NVPTX::SULD_1D_V4I8_TRAP;
    break;
  case NVPTXISD::Suld1DV4I16Trap:
    Opc = NVPTX::SULD_1D_V4I16_TRAP;
    break;
  case NVPTXISD::Suld1DV4I32Trap:
    Opc = NVPTX::SULD_1D_V4I32_TRAP;
    break;
  case NVPTXISD::Suld1DArrayI8Trap:
    Opc = NVPTX::SULD_1D_ARRAY_I8_TRAP;
    break;
  case NVPTXISD::Suld1DArrayI16Trap:
    Opc = NVPTX::SULD_1D_ARRAY_I16_TRAP;
    break;
  case NVPTXISD::Suld1DArrayI32Trap:
    Opc = NVPTX::SULD_1D_ARRAY_I32_TRAP;
    break;
  case NVPTXISD::Suld1DArrayI64Trap:
    Opc = NVPTX::SULD_1D_ARRAY_I64_TRAP;
    break;
  case NVPTXISD::Suld1DArrayV2I8Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V2I8_TRAP;
    break;
  case NVPTXISD::Suld1DArrayV2I16Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V2I16_TRAP;
    break;
  case NVPTXISD::Suld1DArrayV2I32Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V2I32_TRAP;
    break;
  case NVPTXISD::Suld1DArrayV2I64Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V2I64_TRAP;
    break;
  case NVPTXISD::Suld1DArrayV4I8Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V4I8_TRAP;
    break;
  case NVPTXISD::Suld1DArrayV4I16Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V4I16_TRAP;
    break;
  case NVPTXISD::Suld1DArrayV4I32Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V4I32_TRAP;
    break;
  case NVPTXISD::Suld2DI8Trap:
    Opc = NVPTX::SULD_2D_I8_TRAP;
    break;
  case NVPTXISD::Suld2DI16Trap:
    Opc = NVPTX::SULD_2D_I16_TRAP;
    break;
  case NVPTXISD::Suld2DI32Trap:
    Opc = NVPTX::SULD_2D_I32_TRAP;
    break;
  case NVPTXISD::Suld2DI64Trap:
    Opc = NVPTX::SULD_2D_I64_TRAP;
    break;
  case NVPTXISD::Suld2DV2I8Trap:
    Opc = NVPTX::SULD_2D_V2I8_TRAP;
    break;
  case NVPTXISD::Suld2DV2I16Trap:
    Opc = NVPTX::SULD_2D_V2I16_TRAP;
    break;
  case NVPTXISD::Suld2DV2I32Trap:
    Opc = NVPTX::SULD_2D_V2I32_TRAP;
    break;
  case NVPTXISD::Suld2DV2I64Trap:
    Opc = NVPTX::SULD_2D_V2I64_TRAP;
    break;
  case NVPTXISD::Suld2DV4I8Trap:
    Opc = NVPTX::SULD_2D_V4I8_TRAP;
    break;
  case NVPTXISD::Suld2DV4I16Trap:
    Opc = NVPTX::SULD_2D_V4I16_TRAP;
    break;
  case NVPTXISD::Suld2DV4I32Trap:
    Opc = NVPTX::SULD_2D_V4I32_TRAP;
    break;
  case NVPTXISD::Suld2DArrayI8Trap:
    Opc = NVPTX::SULD_2D_ARRAY_I8_TRAP;
    break;
  case NVPTXISD::Suld2DArrayI16Trap:
    Opc = NVPTX::SULD_2D_ARRAY_I16_TRAP;
    break;
  case NVPTXISD::Suld2DArrayI32Trap:
    Opc = NVPTX::SULD_2D_ARRAY_I32_TRAP;
    break;
  case NVPTXISD::Suld2DArrayI64Trap:
    Opc = NVPTX::SULD_2D_ARRAY_I64_TRAP;
    break;
  case NVPTXISD::Suld2DArrayV2I8Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V2I8_TRAP;
    break;
  case NVPTXISD::Suld2DArrayV2I16Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V2I16_TRAP;
    break;
  case NVPTXISD::Suld2DArrayV2I32Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V2I32_TRAP;
    break;
  case NVPTXISD::Suld2DArrayV2I64Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V2I64_TRAP;
    break;
  case NVPTXISD::Suld2DArrayV4I8Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V4I8_TRAP;
    break;
  case NVPTXISD::Suld2DArrayV4I16Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V4I16_TRAP;
    break;
  case NVPTXISD::Suld2DArrayV4I32Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V4I32_TRAP;
    break;
  case NVPTXISD::Suld3DI8Trap:
    Opc = NVPTX::SULD_3D_I8_TRAP;
    break;
  case NVPTXISD::Suld3DI16Trap:
    Opc = NVPTX::SULD_3D_I16_TRAP;
    break;
  case NVPTXISD::Suld3DI32Trap:
    Opc = NVPTX::SULD_3D_I32_TRAP;
    break;
  case NVPTXISD::Suld3DI64Trap:
    Opc = NVPTX::SULD_3D_I64_TRAP;
    break;
  case NVPTXISD::Suld3DV2I8Trap:
    Opc = NVPTX::SULD_3D_V2I8_TRAP;
    break;
  case NVPTXISD::Suld3DV2I16Trap:
    Opc = NVPTX::SULD_3D_V2I16_TRAP;
    break;
  case NVPTXISD::Suld3DV2I32Trap:
    Opc = NVPTX::SULD_3D_V2I32_TRAP;
    break;
  case NVPTXISD::Suld3DV2I64Trap:
    Opc = NVPTX::SULD_3D_V2I64_TRAP;
    break;
  case NVPTXISD::Suld3DV4I8Trap:
    Opc = NVPTX::SULD_3D_V4I8_TRAP;
    break;
  case NVPTXISD::Suld3DV4I16Trap:
    Opc = NVPTX::SULD_3D_V4I16_TRAP;
    break;
  case NVPTXISD::Suld3DV4I32Trap:
    Opc = NVPTX::SULD_3D_V4I32_TRAP;
    break;
  case NVPTXISD::Suld1DI8Zero:
    Opc = NVPTX::SULD_1D_I8_ZERO;
    break;
  case NVPTXISD::Suld1DI16Zero:
    Opc = NVPTX::SULD_1D_I16_ZERO;
    break;
  case NVPTXISD::Suld1DI32Zero:
    Opc = NVPTX::SULD_1D_I32_ZERO;
    break;
  case NVPTXISD::Suld1DI64Zero:
    Opc = NVPTX::SULD_1D_I64_ZERO;
    break;
  case NVPTXISD::Suld1DV2I8Zero:
    Opc = NVPTX::SULD_1D_V2I8_ZERO;
    break;
  case NVPTXISD::Suld1DV2I16Zero:
    Opc = NVPTX::SULD_1D_V2I16_ZERO;
    break;
  case NVPTXISD::Suld1DV2I32Zero:
    Opc = NVPTX::SULD_1D_V2I32_ZERO;
    break;
  case NVPTXISD::Suld1DV2I64Zero:
    Opc = NVPTX::SULD_1D_V2I64_ZERO;
    break;
  case NVPTXISD::Suld1DV4I8Zero:
    Opc = NVPTX::SULD_1D_V4I8_ZERO;
    break;
  case NVPTXISD::Suld1DV4I16Zero:
    Opc = NVPTX::SULD_1D_V4I16_ZERO;
    break;
  case NVPTXISD::Suld1DV4I32Zero:
    Opc = NVPTX::SULD_1D_V4I32_ZERO;
    break;
  case NVPTXISD::Suld1DArrayI8Zero:
    Opc = NVPTX::SULD_1D_ARRAY_I8_ZERO;
    break;
  case NVPTXISD::Suld1DArrayI16Zero:
    Opc = NVPTX::SULD_1D_ARRAY_I16_ZERO;
    break;
  case NVPTXISD::Suld1DArrayI32Zero:
    Opc = NVPTX::SULD_1D_ARRAY_I32_ZERO;
    break;
  case NVPTXISD::Suld1DArrayI64Zero:
    Opc = NVPTX::SULD_1D_ARRAY_I64_ZERO;
    break;
  case NVPTXISD::Suld1DArrayV2I8Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V2I8_ZERO;
    break;
  case NVPTXISD::Suld1DArrayV2I16Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V2I16_ZERO;
    break;
  case NVPTXISD::Suld1DArrayV2I32Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V2I32_ZERO;
    break;
  case NVPTXISD::Suld1DArrayV2I64Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V2I64_ZERO;
    break;
  case NVPTXISD::Suld1DArrayV4I8Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V4I8_ZERO;
    break;
  case NVPTXISD::Suld1DArrayV4I16Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V4I16_ZERO;
    break;
  case NVPTXISD::Suld1DArrayV4I32Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V4I32_ZERO;
    break;
  case NVPTXISD::Suld2DI8Zero:
    Opc = NVPTX::SULD_2D_I8_ZERO;
    break;
  case NVPTXISD::Suld2DI16Zero:
    Opc = NVPTX::SULD_2D_I16_ZERO;
    break;
  case NVPTXISD::Suld2DI32Zero:
    Opc = NVPTX::SULD_2D_I32_ZERO;
    break;
  case NVPTXISD::Suld2DI64Zero:
    Opc = NVPTX::SULD_2D_I64_ZERO;
    break;
  case NVPTXISD::Suld2DV2I8Zero:
    Opc = NVPTX::SULD_2D_V2I8_ZERO;
    break;
  case NVPTXISD::Suld2DV2I16Zero:
    Opc = NVPTX::SULD_2D_V2I16_ZERO;
    break;
  case NVPTXISD::Suld2DV2I32Zero:
    Opc = NVPTX::SULD_2D_V2I32_ZERO;
    break;
  case NVPTXISD::Suld2DV2I64Zero:
    Opc = NVPTX::SULD_2D_V2I64_ZERO;
    break;
  case NVPTXISD::Suld2DV4I8Zero:
    Opc = NVPTX::SULD_2D_V4I8_ZERO;
    break;
  case NVPTXISD::Suld2DV4I16Zero:
    Opc = NVPTX::SULD_2D_V4I16_ZERO;
    break;
  case NVPTXISD::Suld2DV4I32Zero:
    Opc = NVPTX::SULD_2D_V4I32_ZERO;
    break;
  case NVPTXISD::Suld2DArrayI8Zero:
    Opc = NVPTX::SULD_2D_ARRAY_I8_ZERO;
    break;
  case NVPTXISD::Suld2DArrayI16Zero:
    Opc = NVPTX::SULD_2D_ARRAY_I16_ZERO;
    break;
  case NVPTXISD::Suld2DArrayI32Zero:
    Opc = NVPTX::SULD_2D_ARRAY_I32_ZERO;
    break;
  case NVPTXISD::Suld2DArrayI64Zero:
    Opc = NVPTX::SULD_2D_ARRAY_I64_ZERO;
    break;
  case NVPTXISD::Suld2DArrayV2I8Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V2I8_ZERO;
    break;
  case NVPTXISD::Suld2DArrayV2I16Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V2I16_ZERO;
    break;
  case NVPTXISD::Suld2DArrayV2I32Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V2I32_ZERO;
    break;
  case NVPTXISD::Suld2DArrayV2I64Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V2I64_ZERO;
    break;
  case NVPTXISD::Suld2DArrayV4I8Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V4I8_ZERO;
    break;
  case NVPTXISD::Suld2DArrayV4I16Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V4I16_ZERO;
    break;
  case NVPTXISD::Suld2DArrayV4I32Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V4I32_ZERO;
    break;
  case NVPTXISD::Suld3DI8Zero:
    Opc = NVPTX::SULD_3D_I8_ZERO;
    break;
  case NVPTXISD::Suld3DI16Zero:
    Opc = NVPTX::SULD_3D_I16_ZERO;
    break;
  case NVPTXISD::Suld3DI32Zero:
    Opc = NVPTX::SULD_3D_I32_ZERO;
    break;
  case NVPTXISD::Suld3DI64Zero:
    Opc = NVPTX::SULD_3D_I64_ZERO;
    break;
  case NVPTXISD::Suld3DV2I8Zero:
    Opc = NVPTX::SULD_3D_V2I8_ZERO;
    break;
  case NVPTXISD::Suld3DV2I16Zero:
    Opc = NVPTX::SULD_3D_V2I16_ZERO;
    break;
  case NVPTXISD::Suld3DV2I32Zero:
    Opc = NVPTX::SULD_3D_V2I32_ZERO;
    break;
  case NVPTXISD::Suld3DV2I64Zero:
    Opc = NVPTX::SULD_3D_V2I64_ZERO;
    break;
  case NVPTXISD::Suld3DV4I8Zero:
    Opc = NVPTX::SULD_3D_V4I8_ZERO;
    break;
  case NVPTXISD::Suld3DV4I16Zero:
    Opc = NVPTX::SULD_3D_V4I16_ZERO;
    break;
  case NVPTXISD::Suld3DV4I32Zero:
    Opc = NVPTX::SULD_3D_V4I32_ZERO;
    break;
  }

  // Copy over operands
  SmallVector<SDValue, 8> Ops(N->op_begin() + 1, N->op_end());
  Ops.push_back(N->getOperand(0)); // Move chain to the back.

  ReplaceNode(N, CurDAG->getMachineNode(Opc, SDLoc(N), N->getVTList(), Ops));
  return true;
}


/// SelectBFE - Look for instruction sequences that can be made more efficient
/// by using the 'bfe' (bit-field extract) PTX instruction
bool NVPTXDAGToDAGISel::tryBFE(SDNode *N) {
  SDLoc DL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue Len;
  SDValue Start;
  SDValue Val;
  bool IsSigned = false;

  if (N->getOpcode() == ISD::AND) {
    // Canonicalize the operands
    // We want 'and %val, %mask'
    if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
      std::swap(LHS, RHS);
    }

    ConstantSDNode *Mask = dyn_cast<ConstantSDNode>(RHS);
    if (!Mask) {
      // We need a constant mask on the RHS of the AND
      return false;
    }

    // Extract the mask bits
    uint64_t MaskVal = Mask->getZExtValue();
    if (!isMask_64(MaskVal)) {
      // We *could* handle shifted masks here, but doing so would require an
      // 'and' operation to fix up the low-order bits so we would trade
      // shr+and for bfe+and, which has the same throughput
      return false;
    }

    // How many bits are in our mask?
    uint64_t NumBits = countTrailingOnes(MaskVal);
    Len = CurDAG->getTargetConstant(NumBits, DL, MVT::i32);

    if (LHS.getOpcode() == ISD::SRL || LHS.getOpcode() == ISD::SRA) {
      // We have a 'srl/and' pair, extract the effective start bit and length
      Val = LHS.getNode()->getOperand(0);
      Start = LHS.getNode()->getOperand(1);
      ConstantSDNode *StartConst = dyn_cast<ConstantSDNode>(Start);
      if (StartConst) {
        uint64_t StartVal = StartConst->getZExtValue();
        // How many "good" bits do we have left?  "good" is defined here as bits
        // that exist in the original value, not shifted in.
        uint64_t GoodBits = Start.getValueSizeInBits() - StartVal;
        if (NumBits > GoodBits) {
          // Do not handle the case where bits have been shifted in. In theory
          // we could handle this, but the cost is likely higher than just
          // emitting the srl/and pair.
          return false;
        }
        Start = CurDAG->getTargetConstant(StartVal, DL, MVT::i32);
      } else {
        // Do not handle the case where the shift amount (can be zero if no srl
        // was found) is not constant. We could handle this case, but it would
        // require run-time logic that would be more expensive than just
        // emitting the srl/and pair.
        return false;
      }
    } else {
      // Do not handle the case where the LHS of the and is not a shift. While
      // it would be trivial to handle this case, it would just transform
      // 'and' -> 'bfe', but 'and' has higher-throughput.
      return false;
    }
  } else if (N->getOpcode() == ISD::SRL || N->getOpcode() == ISD::SRA) {
    if (LHS->getOpcode() == ISD::AND) {
      ConstantSDNode *ShiftCnst = dyn_cast<ConstantSDNode>(RHS);
      if (!ShiftCnst) {
        // Shift amount must be constant
        return false;
      }

      uint64_t ShiftAmt = ShiftCnst->getZExtValue();

      SDValue AndLHS = LHS->getOperand(0);
      SDValue AndRHS = LHS->getOperand(1);

      // Canonicalize the AND to have the mask on the RHS
      if (isa<ConstantSDNode>(AndLHS)) {
        std::swap(AndLHS, AndRHS);
      }

      ConstantSDNode *MaskCnst = dyn_cast<ConstantSDNode>(AndRHS);
      if (!MaskCnst) {
        // Mask must be constant
        return false;
      }

      uint64_t MaskVal = MaskCnst->getZExtValue();
      uint64_t NumZeros;
      uint64_t NumBits;
      if (isMask_64(MaskVal)) {
        NumZeros = 0;
        // The number of bits in the result bitfield will be the number of
        // trailing ones (the AND) minus the number of bits we shift off
        NumBits = countTrailingOnes(MaskVal) - ShiftAmt;
      } else if (isShiftedMask_64(MaskVal)) {
        NumZeros = countTrailingZeros(MaskVal);
        unsigned NumOnes = countTrailingOnes(MaskVal >> NumZeros);
        // The number of bits in the result bitfield will be the number of
        // trailing zeros plus the number of set bits in the mask minus the
        // number of bits we shift off
        NumBits = NumZeros + NumOnes - ShiftAmt;
      } else {
        // This is not a mask we can handle
        return false;
      }

      if (ShiftAmt < NumZeros) {
        // Handling this case would require extra logic that would make this
        // transformation non-profitable
        return false;
      }

      Val = AndLHS;
      Start = CurDAG->getTargetConstant(ShiftAmt, DL, MVT::i32);
      Len = CurDAG->getTargetConstant(NumBits, DL, MVT::i32);
    } else if (LHS->getOpcode() == ISD::SHL) {
      // Here, we have a pattern like:
      //
      // (sra (shl val, NN), MM)
      // or
      // (srl (shl val, NN), MM)
      //
      // If MM >= NN, we can efficiently optimize this with bfe
      Val = LHS->getOperand(0);

      SDValue ShlRHS = LHS->getOperand(1);
      ConstantSDNode *ShlCnst = dyn_cast<ConstantSDNode>(ShlRHS);
      if (!ShlCnst) {
        // Shift amount must be constant
        return false;
      }
      uint64_t InnerShiftAmt = ShlCnst->getZExtValue();

      SDValue ShrRHS = RHS;
      ConstantSDNode *ShrCnst = dyn_cast<ConstantSDNode>(ShrRHS);
      if (!ShrCnst) {
        // Shift amount must be constant
        return false;
      }
      uint64_t OuterShiftAmt = ShrCnst->getZExtValue();

      // To avoid extra codegen and be profitable, we need Outer >= Inner
      if (OuterShiftAmt < InnerShiftAmt) {
        return false;
      }

      // If the outer shift is more than the type size, we have no bitfield to
      // extract (since we also check that the inner shift is <= the outer shift
      // then this also implies that the inner shift is < the type size)
      if (OuterShiftAmt >= Val.getValueSizeInBits()) {
        return false;
      }

      Start = CurDAG->getTargetConstant(OuterShiftAmt - InnerShiftAmt, DL,
                                        MVT::i32);
      Len = CurDAG->getTargetConstant(Val.getValueSizeInBits() - OuterShiftAmt,
                                      DL, MVT::i32);

      if (N->getOpcode() == ISD::SRA) {
        // If we have a arithmetic right shift, we need to use the signed bfe
        // variant
        IsSigned = true;
      }
    } else {
      // No can do...
      return false;
    }
  } else {
    // No can do...
    return false;
  }


  unsigned Opc;
  // For the BFE operations we form here from "and" and "srl", always use the
  // unsigned variants.
  if (Val.getValueType() == MVT::i32) {
    if (IsSigned) {
      Opc = NVPTX::BFE_S32rii;
    } else {
      Opc = NVPTX::BFE_U32rii;
    }
  } else if (Val.getValueType() == MVT::i64) {
    if (IsSigned) {
      Opc = NVPTX::BFE_S64rii;
    } else {
      Opc = NVPTX::BFE_U64rii;
    }
  } else {
    // We cannot handle this type
    return false;
  }

  SDValue Ops[] = {
    Val, Start, Len
  };

  ReplaceNode(N, CurDAG->getMachineNode(Opc, DL, N->getVTList(), Ops));
  return true;
}

// SelectDirectAddr - Match a direct address for DAG.
// A direct address could be a globaladdress or externalsymbol.
bool NVPTXDAGToDAGISel::SelectDirectAddr(SDValue N, SDValue &Address) {
  // Return true if TGA or ES.
  if (N.getOpcode() == ISD::TargetGlobalAddress ||
      N.getOpcode() == ISD::TargetExternalSymbol) {
    Address = N;
    return true;
  }
  if (N.getOpcode() == NVPTXISD::Wrapper) {
    Address = N.getOperand(0);
    return true;
  }
  // addrspacecast(MoveParam(arg_symbol) to addrspace(PARAM)) -> arg_symbol
  if (AddrSpaceCastSDNode *CastN = dyn_cast<AddrSpaceCastSDNode>(N)) {
    if (CastN->getSrcAddressSpace() == ADDRESS_SPACE_GENERIC &&
        CastN->getDestAddressSpace() == ADDRESS_SPACE_PARAM &&
        CastN->getOperand(0).getOpcode() == NVPTXISD::MoveParam)
      return SelectDirectAddr(CastN->getOperand(0).getOperand(0), Address);
  }
  return false;
}

// symbol+offset
bool NVPTXDAGToDAGISel::SelectADDRsi_imp(
    SDNode *OpNode, SDValue Addr, SDValue &Base, SDValue &Offset, MVT mvt) {
  if (Addr.getOpcode() == ISD::ADD) {
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
      SDValue base = Addr.getOperand(0);
      if (SelectDirectAddr(base, Base)) {
        Offset = CurDAG->getTargetConstant(CN->getZExtValue(), SDLoc(OpNode),
                                           mvt);
        return true;
      }
    }
  }
  return false;
}

// symbol+offset
bool NVPTXDAGToDAGISel::SelectADDRsi(SDNode *OpNode, SDValue Addr,
                                     SDValue &Base, SDValue &Offset) {
  return SelectADDRsi_imp(OpNode, Addr, Base, Offset, MVT::i32);
}

// symbol+offset
bool NVPTXDAGToDAGISel::SelectADDRsi64(SDNode *OpNode, SDValue Addr,
                                       SDValue &Base, SDValue &Offset) {
  return SelectADDRsi_imp(OpNode, Addr, Base, Offset, MVT::i64);
}

// register+offset
bool NVPTXDAGToDAGISel::SelectADDRri_imp(
    SDNode *OpNode, SDValue Addr, SDValue &Base, SDValue &Offset, MVT mvt) {
  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), mvt);
    Offset = CurDAG->getTargetConstant(0, SDLoc(OpNode), mvt);
    return true;
  }
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false; // direct calls.

  if (Addr.getOpcode() == ISD::ADD) {
    if (SelectDirectAddr(Addr.getOperand(0), Addr)) {
      return false;
    }
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
      if (FrameIndexSDNode *FIN =
              dyn_cast<FrameIndexSDNode>(Addr.getOperand(0)))
        // Constant offset from frame ref.
        Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), mvt);
      else
        Base = Addr.getOperand(0);
      Offset = CurDAG->getTargetConstant(CN->getZExtValue(), SDLoc(OpNode),
                                         mvt);
      return true;
    }
  }
  return false;
}

// register+offset
bool NVPTXDAGToDAGISel::SelectADDRri(SDNode *OpNode, SDValue Addr,
                                     SDValue &Base, SDValue &Offset) {
  return SelectADDRri_imp(OpNode, Addr, Base, Offset, MVT::i32);
}

// register+offset
bool NVPTXDAGToDAGISel::SelectADDRri64(SDNode *OpNode, SDValue Addr,
                                       SDValue &Base, SDValue &Offset) {
  return SelectADDRri_imp(OpNode, Addr, Base, Offset, MVT::i64);
}

bool NVPTXDAGToDAGISel::ChkMemSDNodeAddressSpace(SDNode *N,
                                                 unsigned int spN) const {
  const Value *Src = nullptr;
  if (MemSDNode *mN = dyn_cast<MemSDNode>(N)) {
    if (spN == 0 && mN->getMemOperand()->getPseudoValue())
      return true;
    Src = mN->getMemOperand()->getValue();
  }
  if (!Src)
    return false;
  if (auto *PT = dyn_cast<PointerType>(Src->getType()))
    return (PT->getAddressSpace() == spN);
  return false;
}

/// SelectInlineAsmMemoryOperand - Implement addressing mode selection for
/// inline asm expressions.
bool NVPTXDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, unsigned ConstraintID, std::vector<SDValue> &OutOps) {
  SDValue Op0, Op1;
  switch (ConstraintID) {
  default:
    return true;
  case InlineAsm::Constraint_m: // memory
    if (SelectDirectAddr(Op, Op0)) {
      OutOps.push_back(Op0);
      OutOps.push_back(CurDAG->getTargetConstant(0, SDLoc(Op), MVT::i32));
      return false;
    }
    if (SelectADDRri(Op.getNode(), Op, Op0, Op1)) {
      OutOps.push_back(Op0);
      OutOps.push_back(Op1);
      return false;
    }
    break;
  }
  return true;
}

/// GetConvertOpcode - Returns the CVT_ instruction opcode that implements a
/// conversion from \p SrcTy to \p DestTy.
unsigned NVPTXDAGToDAGISel::GetConvertOpcode(MVT DestTy, MVT SrcTy,
                                             bool IsSigned) {
  switch (SrcTy.SimpleTy) {
  default:
    llvm_unreachable("Unhandled source type");
  case MVT::i8:
    switch (DestTy.SimpleTy) {
    default:
      llvm_unreachable("Unhandled dest type");
    case MVT::i16:
      return IsSigned ? NVPTX::CVT_s16_s8 : NVPTX::CVT_u16_u8;
    case MVT::i32:
      return IsSigned ? NVPTX::CVT_s32_s8 : NVPTX::CVT_u32_u8;
    case MVT::i64:
      return IsSigned ? NVPTX::CVT_s64_s8 : NVPTX::CVT_u64_u8;
    }
  case MVT::i16:
    switch (DestTy.SimpleTy) {
    default:
      llvm_unreachable("Unhandled dest type");
    case MVT::i8:
      return IsSigned ? NVPTX::CVT_s8_s16 : NVPTX::CVT_u8_u16;
    case MVT::i32:
      return IsSigned ? NVPTX::CVT_s32_s16 : NVPTX::CVT_u32_u16;
    case MVT::i64:
      return IsSigned ? NVPTX::CVT_s64_s16 : NVPTX::CVT_u64_u16;
    }
  case MVT::i32:
    switch (DestTy.SimpleTy) {
    default:
      llvm_unreachable("Unhandled dest type");
    case MVT::i8:
      return IsSigned ? NVPTX::CVT_s8_s32 : NVPTX::CVT_u8_u32;
    case MVT::i16:
      return IsSigned ? NVPTX::CVT_s16_s32 : NVPTX::CVT_u16_u32;
    case MVT::i64:
      return IsSigned ? NVPTX::CVT_s64_s32 : NVPTX::CVT_u64_u32;
    }
  case MVT::i64:
    switch (DestTy.SimpleTy) {
    default:
      llvm_unreachable("Unhandled dest type");
    case MVT::i8:
      return IsSigned ? NVPTX::CVT_s8_s64 : NVPTX::CVT_u8_u64;
    case MVT::i16:
      return IsSigned ? NVPTX::CVT_s16_s64 : NVPTX::CVT_u16_u64;
    case MVT::i32:
      return IsSigned ? NVPTX::CVT_s32_s64 : NVPTX::CVT_u32_u64;
    }
  }
}
