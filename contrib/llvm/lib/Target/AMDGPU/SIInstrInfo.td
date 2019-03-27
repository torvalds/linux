//===-- SIInstrInfo.td - SI Instruction Infos -------------*- tablegen -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
def isCI : Predicate<"Subtarget->getGeneration() "
                      ">= AMDGPUSubtarget::SEA_ISLANDS">;
def isCIOnly : Predicate<"Subtarget->getGeneration() =="
                         "AMDGPUSubtarget::SEA_ISLANDS">,
  AssemblerPredicate <"FeatureSeaIslands">;
def isVIOnly : Predicate<"Subtarget->getGeneration() =="
                         "AMDGPUSubtarget::VOLCANIC_ISLANDS">,
  AssemblerPredicate <"FeatureVolcanicIslands">;

def DisableInst : Predicate <"false">, AssemblerPredicate<"FeatureDisable">;

class GCNPredicateControl : PredicateControl {
  Predicate SIAssemblerPredicate = isSICI;
  Predicate VIAssemblerPredicate = isVI;
}

// Execpt for the NONE field, this must be kept in sync with the
// SIEncodingFamily enum in AMDGPUInstrInfo.cpp
def SIEncodingFamily {
  int NONE = -1;
  int SI = 0;
  int VI = 1;
  int SDWA = 2;
  int SDWA9 = 3;
  int GFX80 = 4;
  int GFX9 = 5;
}

//===----------------------------------------------------------------------===//
// SI DAG Nodes
//===----------------------------------------------------------------------===//

def AMDGPUclamp : SDNode<"AMDGPUISD::CLAMP", SDTFPUnaryOp>;

def SIsbuffer_load : SDNode<"AMDGPUISD::SBUFFER_LOAD",
  SDTypeProfile<1, 3, [SDTCisVT<1, v4i32>, SDTCisVT<2, i32>, SDTCisVT<3, i1>]>,
  [SDNPMayLoad, SDNPMemOperand]
>;

def SIds_ordered_count : SDNode<"AMDGPUISD::DS_ORDERED_COUNT",
  SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisVT<2, i16>]>,
  [SDNPMayLoad, SDNPMayStore, SDNPMemOperand, SDNPHasChain, SDNPInGlue]
>;

def SIatomic_inc : SDNode<"AMDGPUISD::ATOMIC_INC", SDTAtomic2,
  [SDNPMayLoad, SDNPMayStore, SDNPMemOperand, SDNPHasChain]
>;

def SIatomic_dec : SDNode<"AMDGPUISD::ATOMIC_DEC", SDTAtomic2,
  [SDNPMayLoad, SDNPMayStore, SDNPMemOperand, SDNPHasChain]
>;

def SDTAtomic2_f32 : SDTypeProfile<1, 2, [
  SDTCisSameAs<0,2>, SDTCisFP<0>, SDTCisPtrTy<1>
]>;

def SIatomic_fadd : SDNode<"AMDGPUISD::ATOMIC_LOAD_FADD", SDTAtomic2_f32,
  [SDNPMayLoad, SDNPMayStore, SDNPMemOperand, SDNPHasChain]
>;

def SIatomic_fmin : SDNode<"AMDGPUISD::ATOMIC_LOAD_FMIN", SDTAtomic2_f32,
  [SDNPMayLoad, SDNPMayStore, SDNPMemOperand, SDNPHasChain]
>;

def SIatomic_fmax : SDNode<"AMDGPUISD::ATOMIC_LOAD_FMAX", SDTAtomic2_f32,
  [SDNPMayLoad, SDNPMayStore, SDNPMemOperand, SDNPHasChain]
>;

def SDTtbuffer_load : SDTypeProfile<1, 8,
  [                     // vdata
   SDTCisVT<1, v4i32>,  // rsrc
   SDTCisVT<2, i32>,    // vindex(VGPR)
   SDTCisVT<3, i32>,    // voffset(VGPR)
   SDTCisVT<4, i32>,    // soffset(SGPR)
   SDTCisVT<5, i32>,    // offset(imm)
   SDTCisVT<6, i32>,    // format(imm)
   SDTCisVT<7, i32>,    // cachecontrol(imm)
   SDTCisVT<8, i1>      // idxen(imm)
  ]>;

def SItbuffer_load :   SDNode<"AMDGPUISD::TBUFFER_LOAD_FORMAT", SDTtbuffer_load,
                              [SDNPMayLoad, SDNPMemOperand, SDNPHasChain]>;
def SItbuffer_load_d16 : SDNode<"AMDGPUISD::TBUFFER_LOAD_FORMAT_D16",
                                SDTtbuffer_load,
                                [SDNPMayLoad, SDNPMemOperand, SDNPHasChain]>;

def SDTtbuffer_store : SDTypeProfile<0, 9,
    [                     // vdata
     SDTCisVT<1, v4i32>,  // rsrc
     SDTCisVT<2, i32>,    // vindex(VGPR)
     SDTCisVT<3, i32>,    // voffset(VGPR)
     SDTCisVT<4, i32>,    // soffset(SGPR)
     SDTCisVT<5, i32>,    // offset(imm)
     SDTCisVT<6, i32>,    // format(imm)
     SDTCisVT<7, i32>,    // cachecontrol(imm)
     SDTCisVT<8, i1>      // idxen(imm)
    ]>;

def SItbuffer_store : SDNode<"AMDGPUISD::TBUFFER_STORE_FORMAT", SDTtbuffer_store,
                             [SDNPMayStore, SDNPMemOperand, SDNPHasChain]>;
def SItbuffer_store_x3 : SDNode<"AMDGPUISD::TBUFFER_STORE_FORMAT_X3",
                                SDTtbuffer_store,
                                [SDNPMayStore, SDNPMemOperand, SDNPHasChain]>;
def SItbuffer_store_d16 : SDNode<"AMDGPUISD::TBUFFER_STORE_FORMAT_D16",
                                SDTtbuffer_store,
                                [SDNPMayStore, SDNPMemOperand, SDNPHasChain]>;

def SDTBufferLoad : SDTypeProfile<1, 7,
    [                    // vdata
     SDTCisVT<1, v4i32>, // rsrc
     SDTCisVT<2, i32>,   // vindex(VGPR)
     SDTCisVT<3, i32>,   // voffset(VGPR)
     SDTCisVT<4, i32>,   // soffset(SGPR)
     SDTCisVT<5, i32>,   // offset(imm)
     SDTCisVT<6, i32>,   // cachepolicy(imm)
     SDTCisVT<7, i1>]>;  // idxen(imm)

def SIbuffer_load : SDNode <"AMDGPUISD::BUFFER_LOAD", SDTBufferLoad,
                            [SDNPMemOperand, SDNPHasChain, SDNPMayLoad]>;
def SIbuffer_load_format : SDNode <"AMDGPUISD::BUFFER_LOAD_FORMAT", SDTBufferLoad,
                            [SDNPMemOperand, SDNPHasChain, SDNPMayLoad]>;
def SIbuffer_load_format_d16 : SDNode <"AMDGPUISD::BUFFER_LOAD_FORMAT_D16",
                                SDTBufferLoad,
                                [SDNPMemOperand, SDNPHasChain, SDNPMayLoad]>;

def SDTBufferStore : SDTypeProfile<0, 8,
    [                    // vdata
     SDTCisVT<1, v4i32>, // rsrc
     SDTCisVT<2, i32>,   // vindex(VGPR)
     SDTCisVT<3, i32>,   // voffset(VGPR)
     SDTCisVT<4, i32>,   // soffset(SGPR)
     SDTCisVT<5, i32>,   // offset(imm)
     SDTCisVT<6, i32>,   // cachepolicy(imm)
     SDTCisVT<7, i1>]>;  // idxen(imm)

def SIbuffer_store : SDNode <"AMDGPUISD::BUFFER_STORE", SDTBufferStore,
                             [SDNPMayStore, SDNPMemOperand, SDNPHasChain]>;
def SIbuffer_store_format : SDNode <"AMDGPUISD::BUFFER_STORE_FORMAT",
                            SDTBufferStore,
                            [SDNPMayStore, SDNPMemOperand, SDNPHasChain]>;
def SIbuffer_store_format_d16 : SDNode <"AMDGPUISD::BUFFER_STORE_FORMAT_D16",
                            SDTBufferStore,
                            [SDNPMayStore, SDNPMemOperand, SDNPHasChain]>;

class SDBufferAtomic<string opcode> : SDNode <opcode,
  SDTypeProfile<1, 8,
      [SDTCisVT<0, i32>,   // dst
       SDTCisVT<1, i32>,   // vdata
       SDTCisVT<2, v4i32>, // rsrc
       SDTCisVT<3, i32>,   // vindex(VGPR)
       SDTCisVT<4, i32>,   // voffset(VGPR)
       SDTCisVT<5, i32>,   // soffset(SGPR)
       SDTCisVT<6, i32>,   // offset(imm)
       SDTCisVT<7, i32>,   // cachepolicy(imm)
       SDTCisVT<8, i1>]>,  // idxen(imm)
  [SDNPMemOperand, SDNPHasChain, SDNPMayLoad, SDNPMayStore]
>;

def SIbuffer_atomic_swap : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_SWAP">;
def SIbuffer_atomic_add : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_ADD">;
def SIbuffer_atomic_sub : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_SUB">;
def SIbuffer_atomic_smin : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_SMIN">;
def SIbuffer_atomic_umin : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_UMIN">;
def SIbuffer_atomic_smax : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_SMAX">;
def SIbuffer_atomic_umax : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_UMAX">;
def SIbuffer_atomic_and : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_AND">;
def SIbuffer_atomic_or : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_OR">;
def SIbuffer_atomic_xor : SDBufferAtomic <"AMDGPUISD::BUFFER_ATOMIC_XOR">;

def SIbuffer_atomic_cmpswap : SDNode <"AMDGPUISD::BUFFER_ATOMIC_CMPSWAP",
  SDTypeProfile<1, 9,
    [SDTCisVT<0, i32>,   // dst
     SDTCisVT<1, i32>,   // src
     SDTCisVT<2, i32>,   // cmp
     SDTCisVT<3, v4i32>, // rsrc
     SDTCisVT<4, i32>,   // vindex(VGPR)
     SDTCisVT<5, i32>,   // voffset(VGPR)
     SDTCisVT<6, i32>,   // soffset(SGPR)
     SDTCisVT<7, i32>,   // offset(imm)
     SDTCisVT<8, i32>,   // cachepolicy(imm)
     SDTCisVT<9, i1>]>,  // idxen(imm)
  [SDNPMemOperand, SDNPHasChain, SDNPMayLoad, SDNPMayStore]
>;

def SIpc_add_rel_offset : SDNode<"AMDGPUISD::PC_ADD_REL_OFFSET",
  SDTypeProfile<1, 2, [SDTCisVT<0, iPTR>, SDTCisSameAs<0,1>, SDTCisSameAs<0,2>]>
>;

//===----------------------------------------------------------------------===//
// ValueType helpers
//===----------------------------------------------------------------------===//

// Returns 1 if the source arguments have modifiers, 0 if they do not.
// XXX - do f16 instructions?
class isFloatType<ValueType SrcVT> {
  bit ret =
    !if(!eq(SrcVT.Value, f16.Value), 1,
    !if(!eq(SrcVT.Value, f32.Value), 1,
    !if(!eq(SrcVT.Value, f64.Value), 1,
    !if(!eq(SrcVT.Value, v2f16.Value), 1,
    0))));
}

class isIntType<ValueType SrcVT> {
  bit ret =
    !if(!eq(SrcVT.Value, i16.Value), 1,
    !if(!eq(SrcVT.Value, i32.Value), 1,
    !if(!eq(SrcVT.Value, i64.Value), 1,
    0)));
}

class isPackedType<ValueType SrcVT> {
  bit ret =
    !if(!eq(SrcVT.Value, v2i16.Value), 1,
      !if(!eq(SrcVT.Value, v2f16.Value), 1, 0)
    );
}

//===----------------------------------------------------------------------===//
// PatFrags for global memory operations
//===----------------------------------------------------------------------===//

defm atomic_inc_global : global_binary_atomic_op<SIatomic_inc>;
defm atomic_dec_global : global_binary_atomic_op<SIatomic_dec>;

def atomic_inc_local : local_binary_atomic_op<SIatomic_inc>;
def atomic_dec_local : local_binary_atomic_op<SIatomic_dec>;
def atomic_load_fadd_local : local_binary_atomic_op<SIatomic_fadd>;
def atomic_load_fmin_local : local_binary_atomic_op<SIatomic_fmin>;
def atomic_load_fmax_local : local_binary_atomic_op<SIatomic_fmax>;

//===----------------------------------------------------------------------===//
// SDNodes PatFrags for loads/stores with a glue input.
// This is for SDNodes and PatFrag for local loads and stores to
// enable s_mov_b32 m0, -1 to be glued to the memory instructions.
//
// These mirror the regular load/store PatFrags and rely on special
// processing during Select() to add the glued copy.
//
//===----------------------------------------------------------------------===//

def AMDGPUld_glue : SDNode <"ISD::LOAD", SDTLoad,
  [SDNPHasChain, SDNPMayLoad, SDNPMemOperand, SDNPInGlue]
>;

def AMDGPUatomic_ld_glue : SDNode <"ISD::ATOMIC_LOAD", SDTAtomicLoad,
  [SDNPHasChain, SDNPMayLoad, SDNPMemOperand, SDNPInGlue]
>;

def unindexedload_glue : PatFrag <(ops node:$ptr), (AMDGPUld_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getAddressingMode() == ISD::UNINDEXED;
}]>;

def load_glue : PatFrag <(ops node:$ptr), (unindexedload_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getExtensionType() == ISD::NON_EXTLOAD;
}]>;

def atomic_load_32_glue : PatFrag<(ops node:$ptr),
  (AMDGPUatomic_ld_glue node:$ptr)> {
  let IsAtomic = 1;
  let MemoryVT = i32;
}

def atomic_load_64_glue : PatFrag<(ops node:$ptr),
  (AMDGPUatomic_ld_glue node:$ptr)> {
  let IsAtomic = 1;
  let MemoryVT = i64;
}

def extload_glue : PatFrag<(ops node:$ptr), (load_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getExtensionType() == ISD::EXTLOAD;
}]>;

def sextload_glue : PatFrag<(ops node:$ptr), (unindexedload_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getExtensionType() == ISD::SEXTLOAD;
}]>;

def zextload_glue : PatFrag<(ops node:$ptr), (unindexedload_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getExtensionType() == ISD::ZEXTLOAD;
}]>;

def az_extload_glue : AZExtLoadBase <unindexedload_glue>;

def az_extloadi8_glue : PatFrag<(ops node:$ptr), (az_extload_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def az_extloadi16_glue : PatFrag<(ops node:$ptr), (az_extload_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def sextloadi8_glue : PatFrag<(ops node:$ptr), (sextload_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def sextloadi16_glue : PatFrag<(ops node:$ptr), (sextload_glue node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def load_glue_align8 : Aligned8Bytes <
  (ops node:$ptr), (load_glue node:$ptr)
>;
def load_glue_align16 : Aligned16Bytes <
  (ops node:$ptr), (load_glue node:$ptr)
>;


def load_local_m0 : LoadFrag<load_glue>, LocalAddress;
def sextloadi8_local_m0 : LoadFrag<sextloadi8_glue>, LocalAddress;
def sextloadi16_local_m0 : LoadFrag<sextloadi16_glue>, LocalAddress;
def az_extloadi8_local_m0 : LoadFrag<az_extloadi8_glue>, LocalAddress;
def az_extloadi16_local_m0 : LoadFrag<az_extloadi16_glue>, LocalAddress;
def load_align8_local_m0 : LoadFrag <load_glue_align8>, LocalAddress;
def load_align16_local_m0 : LoadFrag <load_glue_align16>, LocalAddress;
def atomic_load_32_local_m0 : LoadFrag<atomic_load_32_glue>, LocalAddress;
def atomic_load_64_local_m0 : LoadFrag<atomic_load_64_glue>, LocalAddress;


def AMDGPUst_glue : SDNode <"ISD::STORE", SDTStore,
  [SDNPHasChain, SDNPMayStore, SDNPMemOperand, SDNPInGlue]
>;

def AMDGPUatomic_st_glue : SDNode <"ISD::ATOMIC_STORE", SDTAtomicStore,
  [SDNPHasChain, SDNPMayStore, SDNPMemOperand, SDNPInGlue]
>;

def atomic_store_glue : PatFrag<(ops node:$ptr, node:$val),
  (AMDGPUatomic_st_glue node:$ptr, node:$val)> {
}

def unindexedstore_glue : PatFrag<(ops node:$val, node:$ptr),
                                   (AMDGPUst_glue node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->getAddressingMode() == ISD::UNINDEXED;
}]>;

def store_glue : PatFrag<(ops node:$val, node:$ptr),
                      (unindexedstore_glue node:$val, node:$ptr), [{
  return !cast<StoreSDNode>(N)->isTruncatingStore();
}]>;

def truncstore_glue : PatFrag<(ops node:$val, node:$ptr),
  (unindexedstore_glue node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->isTruncatingStore();
}]>;

def truncstorei8_glue : PatFrag<(ops node:$val, node:$ptr),
                           (truncstore_glue node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def truncstorei16_glue : PatFrag<(ops node:$val, node:$ptr),
                           (truncstore_glue node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def store_glue_align8 : Aligned8Bytes <
  (ops node:$value, node:$ptr), (store_glue node:$value, node:$ptr)
>;

def store_glue_align16 : Aligned16Bytes <
  (ops node:$value, node:$ptr), (store_glue node:$value, node:$ptr)
>;

def store_local_m0 : StoreFrag<store_glue>, LocalAddress;
def truncstorei8_local_m0 : StoreFrag<truncstorei8_glue>, LocalAddress;
def truncstorei16_local_m0 : StoreFrag<truncstorei16_glue>, LocalAddress;
def atomic_store_local_m0 : StoreFrag<AMDGPUatomic_st_glue>, LocalAddress;

def store_align8_local_m0 : StoreFrag<store_glue_align8>, LocalAddress;
def store_align16_local_m0 : StoreFrag<store_glue_align16>, LocalAddress;

def si_setcc_uniform : PatFrag <
  (ops node:$lhs, node:$rhs, node:$cond),
  (setcc node:$lhs, node:$rhs, node:$cond), [{
  for (SDNode *Use : N->uses()) {
    if (Use->isMachineOpcode() || Use->getOpcode() != ISD::CopyToReg)
      return false;

    unsigned Reg = cast<RegisterSDNode>(Use->getOperand(1))->getReg();
    if (Reg != AMDGPU::SCC)
      return false;
  }
  return true;
}]>;

def lshr_rev : PatFrag <
  (ops node:$src1, node:$src0),
  (srl $src0, $src1)
>;

def ashr_rev : PatFrag <
  (ops node:$src1, node:$src0),
  (sra $src0, $src1)
>;

def lshl_rev : PatFrag <
  (ops node:$src1, node:$src0),
  (shl $src0, $src1)
>;

multiclass SIAtomicM0Glue2 <string op_name, bit is_amdgpu = 0,
                            SDTypeProfile tc = SDTAtomic2> {

  def _glue : SDNode <
    !if(is_amdgpu, "AMDGPUISD", "ISD")#"::ATOMIC_"#op_name, tc,
    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand, SDNPInGlue]
  >;

  def _local_m0 : local_binary_atomic_op <!cast<SDNode>(NAME#"_glue")>;
}

defm atomic_load_add : SIAtomicM0Glue2 <"LOAD_ADD">;
defm atomic_load_sub : SIAtomicM0Glue2 <"LOAD_SUB">;
defm atomic_inc : SIAtomicM0Glue2 <"INC", 1>;
defm atomic_dec : SIAtomicM0Glue2 <"DEC", 1>;
defm atomic_load_and : SIAtomicM0Glue2 <"LOAD_AND">;
defm atomic_load_min : SIAtomicM0Glue2 <"LOAD_MIN">;
defm atomic_load_max : SIAtomicM0Glue2 <"LOAD_MAX">;
defm atomic_load_or : SIAtomicM0Glue2 <"LOAD_OR">;
defm atomic_load_xor : SIAtomicM0Glue2 <"LOAD_XOR">;
defm atomic_load_umin : SIAtomicM0Glue2 <"LOAD_UMIN">;
defm atomic_load_umax : SIAtomicM0Glue2 <"LOAD_UMAX">;
defm atomic_swap : SIAtomicM0Glue2 <"SWAP">;
defm atomic_load_fadd : SIAtomicM0Glue2 <"LOAD_FADD", 1, SDTAtomic2_f32>;
defm atomic_load_fmin : SIAtomicM0Glue2 <"LOAD_FMIN", 1, SDTAtomic2_f32>;
defm atomic_load_fmax : SIAtomicM0Glue2 <"LOAD_FMAX", 1, SDTAtomic2_f32>;

def atomic_cmp_swap_glue : SDNode <"ISD::ATOMIC_CMP_SWAP", SDTAtomic3,
  [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand, SDNPInGlue]
>;

def atomic_cmp_swap_local_m0 : AtomicCmpSwapLocal<atomic_cmp_swap_glue>;


def as_i1imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue(), SDLoc(N), MVT::i1);
}]>;

def as_i8imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue(), SDLoc(N), MVT::i8);
}]>;

def as_i16imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getSExtValue(), SDLoc(N), MVT::i16);
}]>;

def as_i32imm: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getSExtValue(), SDLoc(N), MVT::i32);
}]>;

def as_i64imm: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getSExtValue(), SDLoc(N), MVT::i64);
}]>;

def cond_as_i32imm: SDNodeXForm<cond, [{
  return CurDAG->getTargetConstant(N->get(), SDLoc(N), MVT::i32);
}]>;

// Copied from the AArch64 backend:
def bitcast_fpimm_to_i32 : SDNodeXForm<fpimm, [{
return CurDAG->getTargetConstant(
  N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def frameindex_to_targetframeindex : SDNodeXForm<frameindex, [{
  auto FI = cast<FrameIndexSDNode>(N);
  return CurDAG->getTargetFrameIndex(FI->getIndex(), MVT::i32);
}]>;

// Copied from the AArch64 backend:
def bitcast_fpimm_to_i64 : SDNodeXForm<fpimm, [{
return CurDAG->getTargetConstant(
  N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i64);
}]>;

class bitextract_imm<int bitnum> : SDNodeXForm<imm, [{
  uint64_t Imm = N->getZExtValue();
  unsigned Bit = (Imm >> }] # bitnum # [{ ) & 1;
  return CurDAG->getTargetConstant(Bit, SDLoc(N), MVT::i1);
}]>;

def SIMM16bit : PatLeaf <(imm),
  [{return isInt<16>(N->getSExtValue());}]
>;

class InlineImm <ValueType vt> : PatLeaf <(vt imm), [{
  return isInlineImmediate(N);
}]>;

class InlineFPImm <ValueType vt> : PatLeaf <(vt fpimm), [{
  return isInlineImmediate(N);
}]>;

class VGPRImm <dag frag> : PatLeaf<frag, [{
  return isVGPRImm(N);
}]>;

def NegateImm : SDNodeXForm<imm, [{
  return CurDAG->getConstant(-N->getSExtValue(), SDLoc(N), MVT::i32);
}]>;

// TODO: When FP inline imm values work?
def NegSubInlineConst32 : ImmLeaf<i32, [{
  return Imm < -16 && Imm >= -64;
}], NegateImm>;

def NegSubInlineConst16 : ImmLeaf<i16, [{
  return Imm < -16 && Imm >= -64;
}], NegateImm>;

def ShiftAmt32Imm : PatLeaf <(imm), [{
  return N->getZExtValue() < 32;
}]>;

//===----------------------------------------------------------------------===//
// Custom Operands
//===----------------------------------------------------------------------===//

def SoppBrTarget : AsmOperandClass {
  let Name = "SoppBrTarget";
  let ParserMethod = "parseSOppBrTarget";
}

def sopp_brtarget : Operand<OtherVT> {
  let EncoderMethod = "getSOPPBrEncoding";
  let DecoderMethod = "decodeSoppBrTarget";
  let OperandType = "OPERAND_PCREL";
  let ParserMatchClass = SoppBrTarget;
}

def si_ga : Operand<iPTR>;

def InterpSlotMatchClass : AsmOperandClass {
  let Name = "InterpSlot";
  let PredicateMethod = "isInterpSlot";
  let ParserMethod = "parseInterpSlot";
  let RenderMethod = "addImmOperands";
}

def InterpSlot : Operand<i32> {
  let PrintMethod = "printInterpSlot";
  let ParserMatchClass = InterpSlotMatchClass;
  let OperandType = "OPERAND_IMMEDIATE";
}

def AttrMatchClass : AsmOperandClass {
  let Name = "Attr";
  let PredicateMethod = "isInterpAttr";
  let ParserMethod = "parseInterpAttr";
  let RenderMethod = "addImmOperands";
}

// It appears to be necessary to create a separate operand for this to
// be able to parse attr<num> with no space.
def Attr : Operand<i32> {
  let PrintMethod = "printInterpAttr";
  let ParserMatchClass = AttrMatchClass;
  let OperandType = "OPERAND_IMMEDIATE";
}

def AttrChanMatchClass : AsmOperandClass {
  let Name = "AttrChan";
  let PredicateMethod = "isAttrChan";
  let RenderMethod = "addImmOperands";
}

def AttrChan : Operand<i32> {
  let PrintMethod = "printInterpAttrChan";
  let ParserMatchClass = AttrChanMatchClass;
  let OperandType = "OPERAND_IMMEDIATE";
}

def SendMsgMatchClass : AsmOperandClass {
  let Name = "SendMsg";
  let PredicateMethod = "isSendMsg";
  let ParserMethod = "parseSendMsgOp";
  let RenderMethod = "addImmOperands";
}

def SwizzleMatchClass : AsmOperandClass {
  let Name = "Swizzle";
  let PredicateMethod = "isSwizzle";
  let ParserMethod = "parseSwizzleOp";
  let RenderMethod = "addImmOperands";
  let IsOptional = 1;
}

def ExpTgtMatchClass : AsmOperandClass {
  let Name = "ExpTgt";
  let PredicateMethod = "isExpTgt";
  let ParserMethod = "parseExpTgt";
  let RenderMethod = "printExpTgt";
}

def SendMsgImm : Operand<i32> {
  let PrintMethod = "printSendMsg";
  let ParserMatchClass = SendMsgMatchClass;
}

def SwizzleImm : Operand<i16> {
  let PrintMethod = "printSwizzle";
  let ParserMatchClass = SwizzleMatchClass;
}

def SWaitMatchClass : AsmOperandClass {
  let Name = "SWaitCnt";
  let RenderMethod = "addImmOperands";
  let ParserMethod = "parseSWaitCntOps";
}

def VReg32OrOffClass : AsmOperandClass {
  let Name = "VReg32OrOff";
  let ParserMethod = "parseVReg32OrOff";
}

def WAIT_FLAG : Operand <i32> {
  let ParserMatchClass = SWaitMatchClass;
  let PrintMethod = "printWaitFlag";
}

include "SIInstrFormats.td"
include "VIInstrFormats.td"

// ===----------------------------------------------------------------------===//
// ExpSrc* Special cases for exp src operands which are printed as
// "off" depending on en operand.
// ===----------------------------------------------------------------------===//

def ExpSrc0 : RegisterOperand<VGPR_32> {
  let PrintMethod = "printExpSrc0";
  let ParserMatchClass = VReg32OrOffClass;
}

def ExpSrc1 : RegisterOperand<VGPR_32> {
  let PrintMethod = "printExpSrc1";
  let ParserMatchClass = VReg32OrOffClass;
}

def ExpSrc2 : RegisterOperand<VGPR_32> {
  let PrintMethod = "printExpSrc2";
  let ParserMatchClass = VReg32OrOffClass;
}

def ExpSrc3 : RegisterOperand<VGPR_32> {
  let PrintMethod = "printExpSrc3";
  let ParserMatchClass = VReg32OrOffClass;
}

class SDWASrc<ValueType vt> : RegisterOperand<VS_32> {
  let OperandNamespace = "AMDGPU";
  string Type = !if(isFloatType<vt>.ret, "FP", "INT");
  let OperandType = "OPERAND_REG_INLINE_C_"#Type#vt.Size;
  let DecoderMethod = "decodeSDWASrc"#vt.Size;
  let EncoderMethod = "getSDWASrcEncoding";
}

def SDWASrc_i32 : SDWASrc<i32>;
def SDWASrc_i16 : SDWASrc<i16>;
def SDWASrc_f32 : SDWASrc<f32>;
def SDWASrc_f16 : SDWASrc<f16>;

def SDWAVopcDst : VOPDstOperand<SReg_64> {
  let OperandNamespace = "AMDGPU";
  let OperandType = "OPERAND_SDWA_VOPC_DST";
  let EncoderMethod = "getSDWAVopcDstEncoding";
  let DecoderMethod = "decodeSDWAVopcDst";
}

class NamedMatchClass<string CName, bit Optional = 1> : AsmOperandClass {
  let Name = "Imm"#CName;
  let PredicateMethod = "is"#CName;
  let ParserMethod = !if(Optional, "parseOptionalOperand", "parse"#CName);
  let RenderMethod = "addImmOperands";
  let IsOptional = Optional;
  let DefaultMethod = !if(Optional, "default"#CName, ?);
}

class NamedOperandBit<string Name, AsmOperandClass MatchClass> : Operand<i1> {
  let PrintMethod = "print"#Name;
  let ParserMatchClass = MatchClass;
}

class NamedOperandU8<string Name, AsmOperandClass MatchClass> : Operand<i8> {
  let PrintMethod = "print"#Name;
  let ParserMatchClass = MatchClass;
}

class NamedOperandU12<string Name, AsmOperandClass MatchClass> : Operand<i16> {
  let PrintMethod = "print"#Name;
  let ParserMatchClass = MatchClass;
}

class NamedOperandU16<string Name, AsmOperandClass MatchClass> : Operand<i16> {
  let PrintMethod = "print"#Name;
  let ParserMatchClass = MatchClass;
}

class NamedOperandS13<string Name, AsmOperandClass MatchClass> : Operand<i16> {
  let PrintMethod = "print"#Name;
  let ParserMatchClass = MatchClass;
}

class NamedOperandU32<string Name, AsmOperandClass MatchClass> : Operand<i32> {
  let PrintMethod = "print"#Name;
  let ParserMatchClass = MatchClass;
}

class NamedOperandU32Default0<string Name, AsmOperandClass MatchClass> :
  OperandWithDefaultOps<i32, (ops (i32 0))> {
  let PrintMethod = "print"#Name;
  let ParserMatchClass = MatchClass;
}

let OperandType = "OPERAND_IMMEDIATE" in {

def offen : NamedOperandBit<"Offen", NamedMatchClass<"Offen">>;
def idxen : NamedOperandBit<"Idxen", NamedMatchClass<"Idxen">>;
def addr64 : NamedOperandBit<"Addr64", NamedMatchClass<"Addr64">>;

def offset_u12 : NamedOperandU12<"Offset", NamedMatchClass<"OffsetU12">>;
def offset_s13 : NamedOperandS13<"OffsetS13", NamedMatchClass<"OffsetS13">>;
def offset : NamedOperandU16<"Offset", NamedMatchClass<"Offset">>;
def offset0 : NamedOperandU8<"Offset0", NamedMatchClass<"Offset0">>;
def offset1 : NamedOperandU8<"Offset1", NamedMatchClass<"Offset1">>;

def gds : NamedOperandBit<"GDS", NamedMatchClass<"GDS">>;

def omod : NamedOperandU32<"OModSI", NamedMatchClass<"OModSI">>;
def clampmod : NamedOperandBit<"ClampSI", NamedMatchClass<"ClampSI">>;
def highmod : NamedOperandBit<"High", NamedMatchClass<"High">>;

def GLC : NamedOperandBit<"GLC", NamedMatchClass<"GLC">>;
def SLC : NamedOperandBit<"SLC", NamedMatchClass<"SLC">>;
def TFE : NamedOperandBit<"TFE", NamedMatchClass<"TFE">>;
def UNorm : NamedOperandBit<"UNorm", NamedMatchClass<"UNorm">>;
def DA : NamedOperandBit<"DA", NamedMatchClass<"DA">>;
def R128A16 : NamedOperandBit<"R128A16", NamedMatchClass<"R128A16">>;
def D16 : NamedOperandBit<"D16", NamedMatchClass<"D16">>;
def LWE : NamedOperandBit<"LWE", NamedMatchClass<"LWE">>;
def exp_compr : NamedOperandBit<"ExpCompr", NamedMatchClass<"ExpCompr">>;
def exp_vm : NamedOperandBit<"ExpVM", NamedMatchClass<"ExpVM">>;

def FORMAT : NamedOperandU8<"FORMAT", NamedMatchClass<"FORMAT">>;

def DMask : NamedOperandU16<"DMask", NamedMatchClass<"DMask">>;

def dpp_ctrl : NamedOperandU32<"DPPCtrl", NamedMatchClass<"DPPCtrl", 0>>;
def row_mask : NamedOperandU32<"RowMask", NamedMatchClass<"RowMask">>;
def bank_mask : NamedOperandU32<"BankMask", NamedMatchClass<"BankMask">>;
def bound_ctrl : NamedOperandBit<"BoundCtrl", NamedMatchClass<"BoundCtrl">>;

def dst_sel : NamedOperandU32<"SDWADstSel", NamedMatchClass<"SDWADstSel">>;
def src0_sel : NamedOperandU32<"SDWASrc0Sel", NamedMatchClass<"SDWASrc0Sel">>;
def src1_sel : NamedOperandU32<"SDWASrc1Sel", NamedMatchClass<"SDWASrc1Sel">>;
def dst_unused : NamedOperandU32<"SDWADstUnused", NamedMatchClass<"SDWADstUnused">>;

def op_sel : NamedOperandU32Default0<"OpSel", NamedMatchClass<"OpSel">>;
def op_sel_hi : NamedOperandU32Default0<"OpSelHi", NamedMatchClass<"OpSelHi">>;
def neg_lo : NamedOperandU32Default0<"NegLo", NamedMatchClass<"NegLo">>;
def neg_hi : NamedOperandU32Default0<"NegHi", NamedMatchClass<"NegHi">>;

def hwreg : NamedOperandU16<"Hwreg", NamedMatchClass<"Hwreg", 0>>;

def exp_tgt : NamedOperandU8<"ExpTgt", NamedMatchClass<"ExpTgt", 0>> {

}

} // End OperandType = "OPERAND_IMMEDIATE"

class KImmMatchClass<int size> : AsmOperandClass {
  let Name = "KImmFP"#size;
  let PredicateMethod = "isKImmFP"#size;
  let ParserMethod = "parseImm";
  let RenderMethod = "addKImmFP"#size#"Operands";
}

class kimmOperand<ValueType vt> : Operand<vt> {
  let OperandNamespace = "AMDGPU";
  let OperandType = "OPERAND_KIMM"#vt.Size;
  let PrintMethod = "printU"#vt.Size#"ImmOperand";
  let ParserMatchClass = !cast<AsmOperandClass>("KImmFP"#vt.Size#"MatchClass");
}

// 32-bit VALU immediate operand that uses the constant bus.
def KImmFP32MatchClass : KImmMatchClass<32>;
def f32kimm : kimmOperand<i32>;

// 32-bit VALU immediate operand with a 16-bit value that uses the
// constant bus.
def KImmFP16MatchClass : KImmMatchClass<16>;
def f16kimm : kimmOperand<i16>;


def VOPDstS64 : VOPDstOperand <SReg_64>;

class FPInputModsMatchClass <int opSize> : AsmOperandClass {
  let Name = "RegOrImmWithFP"#opSize#"InputMods";
  let ParserMethod = "parseRegOrImmWithFPInputMods";
  let PredicateMethod = "isRegOrImmWithFP"#opSize#"InputMods";
}

def FP16InputModsMatchClass : FPInputModsMatchClass<16>;
def FP32InputModsMatchClass : FPInputModsMatchClass<32>;
def FP64InputModsMatchClass : FPInputModsMatchClass<64>;

class InputMods <AsmOperandClass matchClass> : Operand <i32> {
  let OperandNamespace = "AMDGPU";
  let OperandType = "OPERAND_INPUT_MODS";
  let ParserMatchClass = matchClass;
}

class FPInputMods <FPInputModsMatchClass matchClass> : InputMods <matchClass> {
  let PrintMethod = "printOperandAndFPInputMods";
}

def FP16InputMods : FPInputMods<FP16InputModsMatchClass>;
def FP32InputMods : FPInputMods<FP32InputModsMatchClass>;
def FP64InputMods : FPInputMods<FP64InputModsMatchClass>;

class IntInputModsMatchClass <int opSize> : AsmOperandClass {
  let Name = "RegOrImmWithInt"#opSize#"InputMods";
  let ParserMethod = "parseRegOrImmWithIntInputMods";
  let PredicateMethod = "isRegOrImmWithInt"#opSize#"InputMods";
}
def Int32InputModsMatchClass : IntInputModsMatchClass<32>;
def Int64InputModsMatchClass : IntInputModsMatchClass<64>;

class IntInputMods <IntInputModsMatchClass matchClass> : InputMods <matchClass> {
  let PrintMethod = "printOperandAndIntInputMods";
}
def Int32InputMods : IntInputMods<Int32InputModsMatchClass>;
def Int64InputMods : IntInputMods<Int64InputModsMatchClass>;

class OpSelModsMatchClass : AsmOperandClass {
  let Name = "OpSelMods";
  let ParserMethod = "parseRegOrImm";
  let PredicateMethod = "isRegOrImm";
}

def IntOpSelModsMatchClass : OpSelModsMatchClass;
def IntOpSelMods : InputMods<IntOpSelModsMatchClass>;

class FPSDWAInputModsMatchClass <int opSize> : AsmOperandClass {
  let Name = "SDWAWithFP"#opSize#"InputMods";
  let ParserMethod = "parseRegOrImmWithFPInputMods";
  let PredicateMethod = "isSDWAFP"#opSize#"Operand";
}

def FP16SDWAInputModsMatchClass : FPSDWAInputModsMatchClass<16>;
def FP32SDWAInputModsMatchClass : FPSDWAInputModsMatchClass<32>;

class FPSDWAInputMods <FPSDWAInputModsMatchClass matchClass> :
  InputMods <matchClass> {
  let PrintMethod = "printOperandAndFPInputMods";
}

def FP16SDWAInputMods : FPSDWAInputMods<FP16SDWAInputModsMatchClass>;
def FP32SDWAInputMods : FPSDWAInputMods<FP32SDWAInputModsMatchClass>;

def FPVRegInputModsMatchClass : AsmOperandClass {
  let Name = "VRegWithFPInputMods";
  let ParserMethod = "parseRegWithFPInputMods";
  let PredicateMethod = "isVReg";
}

def FPVRegInputMods : InputMods <FPVRegInputModsMatchClass> {
  let PrintMethod = "printOperandAndFPInputMods";
}

class IntSDWAInputModsMatchClass <int opSize> : AsmOperandClass {
  let Name = "SDWAWithInt"#opSize#"InputMods";
  let ParserMethod = "parseRegOrImmWithIntInputMods";
  let PredicateMethod = "isSDWAInt"#opSize#"Operand";
}

def Int16SDWAInputModsMatchClass : IntSDWAInputModsMatchClass<16>;
def Int32SDWAInputModsMatchClass : IntSDWAInputModsMatchClass<32>;

class IntSDWAInputMods <IntSDWAInputModsMatchClass matchClass> :
  InputMods <matchClass> {
  let PrintMethod = "printOperandAndIntInputMods";
}

def Int16SDWAInputMods : IntSDWAInputMods<Int16SDWAInputModsMatchClass>;
def Int32SDWAInputMods : IntSDWAInputMods<Int32SDWAInputModsMatchClass>;

def IntVRegInputModsMatchClass : AsmOperandClass {
  let Name = "VRegWithIntInputMods";
  let ParserMethod = "parseRegWithIntInputMods";
  let PredicateMethod = "isVReg";
}

def IntVRegInputMods : InputMods <IntVRegInputModsMatchClass> {
  let PrintMethod = "printOperandAndIntInputMods";
}

class PackedFPInputModsMatchClass <int opSize> : AsmOperandClass {
  let Name = "PackedFP"#opSize#"InputMods";
  let ParserMethod = "parseRegOrImm";
  let PredicateMethod = "isRegOrImm";
//  let PredicateMethod = "isPackedFP"#opSize#"InputMods";
}

class PackedIntInputModsMatchClass <int opSize> : AsmOperandClass {
  let Name = "PackedInt"#opSize#"InputMods";
  let ParserMethod = "parseRegOrImm";
  let PredicateMethod = "isRegOrImm";
//  let PredicateMethod = "isPackedInt"#opSize#"InputMods";
}

def PackedF16InputModsMatchClass : PackedFPInputModsMatchClass<16>;
def PackedI16InputModsMatchClass : PackedIntInputModsMatchClass<16>;

class PackedFPInputMods <PackedFPInputModsMatchClass matchClass> : InputMods <matchClass> {
//  let PrintMethod = "printPackedFPInputMods";
}

class PackedIntInputMods <PackedIntInputModsMatchClass matchClass> : InputMods <matchClass> {
  //let PrintMethod = "printPackedIntInputMods";
}

def PackedF16InputMods : PackedFPInputMods<PackedF16InputModsMatchClass>;
def PackedI16InputMods : PackedIntInputMods<PackedI16InputModsMatchClass>;

//===----------------------------------------------------------------------===//
// Complex patterns
//===----------------------------------------------------------------------===//

def DS1Addr1Offset : ComplexPattern<i32, 2, "SelectDS1Addr1Offset">;
def DS64Bit4ByteAligned : ComplexPattern<i32, 3, "SelectDS64Bit4ByteAligned">;

def MOVRELOffset : ComplexPattern<i32, 2, "SelectMOVRELOffset">;

def VOP3Mods0 : ComplexPattern<untyped, 4, "SelectVOP3Mods0">;
def VOP3Mods0Clamp : ComplexPattern<untyped, 3, "SelectVOP3Mods0Clamp">;
def VOP3Mods0Clamp0OMod : ComplexPattern<untyped, 4, "SelectVOP3Mods0Clamp0OMod">;
def VOP3Mods  : ComplexPattern<untyped, 2, "SelectVOP3Mods">;
def VOP3NoMods : ComplexPattern<untyped, 1, "SelectVOP3NoMods">;
// VOP3Mods, but the input source is known to never be NaN.
def VOP3Mods_nnan : ComplexPattern<fAny, 2, "SelectVOP3Mods_NNaN">;

def VOP3OMods : ComplexPattern<untyped, 3, "SelectVOP3OMods">;

def VOP3PMods  : ComplexPattern<untyped, 2, "SelectVOP3PMods">;
def VOP3PMods0 : ComplexPattern<untyped, 3, "SelectVOP3PMods0">;

def VOP3OpSel  : ComplexPattern<untyped, 2, "SelectVOP3OpSel">;
def VOP3OpSel0 : ComplexPattern<untyped, 3, "SelectVOP3OpSel0">;

def VOP3OpSelMods  : ComplexPattern<untyped, 2, "SelectVOP3OpSelMods">;
def VOP3OpSelMods0 : ComplexPattern<untyped, 3, "SelectVOP3OpSelMods0">;

def VOP3PMadMixMods  : ComplexPattern<untyped, 2, "SelectVOP3PMadMixMods">;


def Hi16Elt  : ComplexPattern<untyped, 1, "SelectHi16Elt">;

//===----------------------------------------------------------------------===//
// SI assembler operands
//===----------------------------------------------------------------------===//

def SIOperand {
  int ZERO = 0x80;
  int VCC = 0x6A;
  int FLAT_SCR = 0x68;
}

// This should be kept in sync with SISrcMods enum
def SRCMODS {
  int NONE = 0;
  int NEG = 1;
  int ABS = 2;
  int NEG_ABS = 3;

  int NEG_HI = ABS;
  int OP_SEL_0 = 4;
  int OP_SEL_1 = 8;
  int DST_OP_SEL = 8;
}

def DSTCLAMP {
  int NONE = 0;
  int ENABLE = 1;
}

def DSTOMOD {
  int NONE = 0;
}

def TRAPID{
  int LLVM_TRAP = 2;
  int LLVM_DEBUG_TRAP = 3;
}

//===----------------------------------------------------------------------===//
//
// SI Instruction multiclass helpers.
//
// Instructions with _32 take 32-bit operands.
// Instructions with _64 take 64-bit operands.
//
// VOP_* instructions can use either a 32-bit or 64-bit encoding.  The 32-bit
// encoding is the standard encoding, but instruction that make use of
// any of the instruction modifiers must use the 64-bit encoding.
//
// Instructions with _e32 use the 32-bit encoding.
// Instructions with _e64 use the 64-bit encoding.
//
//===----------------------------------------------------------------------===//

class SIMCInstr <string pseudo, int subtarget> {
  string PseudoInstr = pseudo;
  int Subtarget = subtarget;
}

//===----------------------------------------------------------------------===//
// EXP classes
//===----------------------------------------------------------------------===//

class EXP_Helper<bit done, SDPatternOperator node = null_frag> : EXPCommon<
  (outs),
  (ins exp_tgt:$tgt,
       ExpSrc0:$src0, ExpSrc1:$src1, ExpSrc2:$src2, ExpSrc3:$src3,
       exp_vm:$vm, exp_compr:$compr, i8imm:$en),
  "exp$tgt $src0, $src1, $src2, $src3"#!if(done, " done", "")#"$compr$vm",
  [(node (i8 timm:$tgt), (i8 timm:$en),
         f32:$src0, f32:$src1, f32:$src2, f32:$src3,
         (i1 timm:$compr), (i1 timm:$vm))]> {
  let AsmMatchConverter = "cvtExp";
}

// Split EXP instruction into EXP and EXP_DONE so we can set
// mayLoad for done=1.
multiclass EXP_m<bit done, SDPatternOperator node> {
  let mayLoad = done, DisableWQM = 1 in {
    let isPseudo = 1, isCodeGenOnly = 1 in {
      def "" : EXP_Helper<done, node>,
               SIMCInstr <"exp"#!if(done, "_done", ""), SIEncodingFamily.NONE>;
    }

    let done = done in {
      def _si : EXP_Helper<done>,
                SIMCInstr <"exp"#!if(done, "_done", ""), SIEncodingFamily.SI>,
                EXPe {
        let AssemblerPredicates = [isSICI];
        let DecoderNamespace = "SICI";
        let DisableDecoder = DisableSIDecoder;
      }

      def _vi : EXP_Helper<done>,
                SIMCInstr <"exp"#!if(done, "_done", ""), SIEncodingFamily.VI>,
                EXPe_vi {
        let AssemblerPredicates = [isVI];
        let DecoderNamespace = "VI";
        let DisableDecoder = DisableVIDecoder;
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Vector ALU classes
//===----------------------------------------------------------------------===//

class getNumSrcArgs<ValueType Src0, ValueType Src1, ValueType Src2> {
  int ret =
    !if (!eq(Src0.Value, untyped.Value),      0,
      !if (!eq(Src1.Value, untyped.Value),    1,   // VOP1
         !if (!eq(Src2.Value, untyped.Value), 2,   // VOP2
                                              3))); // VOP3
}

// Returns the register class to use for the destination of VOP[123C]
// instructions for the given VT.
class getVALUDstForVT<ValueType VT> {
  RegisterOperand ret = !if(!eq(VT.Size, 32), VOPDstOperand<VGPR_32>,
                          !if(!eq(VT.Size, 128), VOPDstOperand<VReg_128>,
                            !if(!eq(VT.Size, 64), VOPDstOperand<VReg_64>,
                              !if(!eq(VT.Size, 16), VOPDstOperand<VGPR_32>,
                              VOPDstOperand<SReg_64>)))); // else VT == i1
}

// Returns the register class to use for the destination of VOP[12C]
// instructions with SDWA extension
class getSDWADstForVT<ValueType VT> {
  RegisterOperand ret = !if(!eq(VT.Size, 1),
                            SDWAVopcDst, // VOPC
                            VOPDstOperand<VGPR_32>); // VOP1/2 32-bit dst
}

// Returns the register class to use for source 0 of VOP[12C]
// instructions for the given VT.
class getVOPSrc0ForVT<ValueType VT> {
  bit isFP = !if(!eq(VT.Value, f16.Value), 1,
             !if(!eq(VT.Value, v2f16.Value), 1,
             !if(!eq(VT.Value, f32.Value), 1,
             !if(!eq(VT.Value, f64.Value), 1,
             0))));

  RegisterOperand ret =
    !if(isFP,
      !if(!eq(VT.Size, 64),
         VSrc_f64,
         !if(!eq(VT.Value, f16.Value),
            VSrc_f16,
            !if(!eq(VT.Value, v2f16.Value),
               VCSrc_v2f16,
               VSrc_f32
            )
         )
       ),
       !if(!eq(VT.Size, 64),
          VSrc_b64,
          !if(!eq(VT.Value, i16.Value),
             VSrc_b16,
             !if(!eq(VT.Value, v2i16.Value),
                VCSrc_v2b16,
                VSrc_b32
             )
          )
       )
    );
}

// Returns the vreg register class to use for source operand given VT
class getVregSrcForVT<ValueType VT> {
  RegisterClass ret = !if(!eq(VT.Size, 128), VReg_128,
                        !if(!eq(VT.Size, 64), VReg_64, VGPR_32));
}

class getSDWASrcForVT <ValueType VT> {
  bit isFP = !if(!eq(VT.Value, f16.Value), 1,
             !if(!eq(VT.Value, f32.Value), 1,
             0));
  RegisterOperand retFlt = !if(!eq(VT.Size, 16), SDWASrc_f16, SDWASrc_f32);
  RegisterOperand retInt = !if(!eq(VT.Size, 16), SDWASrc_i16, SDWASrc_i32);
  RegisterOperand ret = !if(isFP, retFlt, retInt);
}

// Returns the register class to use for sources of VOP3 instructions for the
// given VT.
class getVOP3SrcForVT<ValueType VT> {
  bit isFP = !if(!eq(VT.Value, f16.Value), 1,
             !if(!eq(VT.Value, v2f16.Value), 1,
             !if(!eq(VT.Value, f32.Value), 1,
             !if(!eq(VT.Value, f64.Value), 1,
             0))));
  RegisterOperand ret =
  !if(!eq(VT.Size, 128),
     VSrc_128,
     !if(!eq(VT.Size, 64),
        !if(isFP,
           VCSrc_f64,
           VCSrc_b64),
        !if(!eq(VT.Value, i1.Value),
           SCSrc_i1,
           !if(isFP,
              !if(!eq(VT.Value, f16.Value),
                 VCSrc_f16,
                 !if(!eq(VT.Value, v2f16.Value),
                    VCSrc_v2f16,
                    VCSrc_f32
                 )
              ),
              !if(!eq(VT.Value, i16.Value),
                 VCSrc_b16,
                 !if(!eq(VT.Value, v2i16.Value),
                    VCSrc_v2b16,
                    VCSrc_b32
                 )
              )
           )
        )
     )
  );
}

// Float or packed int
class isModifierType<ValueType SrcVT> {
  bit ret =
    !if(!eq(SrcVT.Value, f16.Value), 1,
    !if(!eq(SrcVT.Value, f32.Value), 1,
    !if(!eq(SrcVT.Value, f64.Value), 1,
    !if(!eq(SrcVT.Value, v2f16.Value), 1,
    !if(!eq(SrcVT.Value, v2i16.Value), 1,
    0)))));
}

// Return type of input modifiers operand for specified input operand
class getSrcMod <ValueType VT> {
  bit isFP = !if(!eq(VT.Value, f16.Value), 1,
               !if(!eq(VT.Value, f32.Value), 1,
               !if(!eq(VT.Value, f64.Value), 1,
               0)));
  bit isPacked = isPackedType<VT>.ret;
  Operand ret =  !if(!eq(VT.Size, 64),
                     !if(isFP, FP64InputMods, Int64InputMods),
                       !if(isFP,
                         !if(!eq(VT.Value, f16.Value),
                            FP16InputMods,
                            FP32InputMods
                          ),
                         Int32InputMods)
                     );
}

class getOpSelMod <ValueType VT> {
  Operand ret = !if(!eq(VT.Value, f16.Value), FP16InputMods, IntOpSelMods);
}

// Return type of input modifiers operand specified input operand for DPP
class getSrcModExt <ValueType VT> {
    bit isFP = !if(!eq(VT.Value, f16.Value), 1,
               !if(!eq(VT.Value, f32.Value), 1,
               !if(!eq(VT.Value, f64.Value), 1,
               0)));
  Operand ret = !if(isFP, FPVRegInputMods, IntVRegInputMods);
}

// Return type of input modifiers operand specified input operand for SDWA
class getSrcModSDWA <ValueType VT> {
  Operand ret = !if(!eq(VT.Value, f16.Value), FP16SDWAInputMods,
                !if(!eq(VT.Value, f32.Value), FP32SDWAInputMods,
                !if(!eq(VT.Value, i16.Value), Int16SDWAInputMods,
                Int32SDWAInputMods)));
}

// Returns the input arguments for VOP[12C] instructions for the given SrcVT.
class getIns32 <RegisterOperand Src0RC, RegisterClass Src1RC, int NumSrcArgs> {
  dag ret = !if(!eq(NumSrcArgs, 1), (ins Src0RC:$src0),               // VOP1
            !if(!eq(NumSrcArgs, 2), (ins Src0RC:$src0, Src1RC:$src1), // VOP2
                                    (ins)));
}

// Returns the input arguments for VOP3 instructions for the given SrcVT.
class getIns64 <RegisterOperand Src0RC, RegisterOperand Src1RC,
                RegisterOperand Src2RC, int NumSrcArgs,
                bit HasIntClamp, bit HasModifiers, bit HasOMod,
                Operand Src0Mod, Operand Src1Mod, Operand Src2Mod> {

  dag ret =
    !if (!eq(NumSrcArgs, 0),
      // VOP1 without input operands (V_NOP, V_CLREXCP)
      (ins),
      /* else */
    !if (!eq(NumSrcArgs, 1),
      !if (!eq(HasModifiers, 1),
        // VOP1 with modifiers
        (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
             clampmod:$clamp, omod:$omod)
      /* else */,
        // VOP1 without modifiers
        !if (!eq(HasIntClamp, 1),
          (ins Src0RC:$src0, clampmod:$clamp),
          (ins Src0RC:$src0))
      /* endif */ ),
    !if (!eq(NumSrcArgs, 2),
      !if (!eq(HasModifiers, 1),
        // VOP 2 with modifiers
        !if( !eq(HasOMod, 1),
          (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
               Src1Mod:$src1_modifiers, Src1RC:$src1,
               clampmod:$clamp, omod:$omod),
           (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
               Src1Mod:$src1_modifiers, Src1RC:$src1,
               clampmod:$clamp))
      /* else */,
        // VOP2 without modifiers
        !if (!eq(HasIntClamp, 1),
          (ins Src0RC:$src0, Src1RC:$src1, clampmod:$clamp),
          (ins Src0RC:$src0, Src1RC:$src1))

      /* endif */ )
    /* NumSrcArgs == 3 */,
      !if (!eq(HasModifiers, 1),
        // VOP3 with modifiers
        !if (!eq(HasOMod, 1),
          (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
               Src1Mod:$src1_modifiers, Src1RC:$src1,
               Src2Mod:$src2_modifiers, Src2RC:$src2,
               clampmod:$clamp, omod:$omod),
          (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
               Src1Mod:$src1_modifiers, Src1RC:$src1,
               Src2Mod:$src2_modifiers, Src2RC:$src2,
               clampmod:$clamp))
      /* else */,
        // VOP3 without modifiers
        !if (!eq(HasIntClamp, 1),
          (ins Src0RC:$src0, Src1RC:$src1, Src2RC:$src2, clampmod:$clamp),
          (ins Src0RC:$src0, Src1RC:$src1, Src2RC:$src2))
      /* endif */ ))));
}

/// XXX - src1 may only allow VGPRs?

// The modifiers (except clamp) are dummy operands for the benefit of
// printing and parsing. They defer their values to looking at the
// srcN_modifiers for what to print.
class getInsVOP3P <RegisterOperand Src0RC, RegisterOperand Src1RC,
                   RegisterOperand Src2RC, int NumSrcArgs,
                   bit HasClamp,
                   Operand Src0Mod, Operand Src1Mod, Operand Src2Mod> {
  dag ret = !if (!eq(NumSrcArgs, 2),
    !if (HasClamp,
      (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
           Src1Mod:$src1_modifiers, Src1RC:$src1,
           clampmod:$clamp,
           op_sel:$op_sel, op_sel_hi:$op_sel_hi,
           neg_lo:$neg_lo, neg_hi:$neg_hi),
      (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
           Src1Mod:$src1_modifiers, Src1RC:$src1,
           op_sel:$op_sel, op_sel_hi:$op_sel_hi,
           neg_lo:$neg_lo, neg_hi:$neg_hi)),
    // else NumSrcArgs == 3
    !if (HasClamp,
      (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
           Src1Mod:$src1_modifiers, Src1RC:$src1,
           Src2Mod:$src2_modifiers, Src2RC:$src2,
           clampmod:$clamp,
           op_sel:$op_sel, op_sel_hi:$op_sel_hi,
           neg_lo:$neg_lo, neg_hi:$neg_hi),
      (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
           Src1Mod:$src1_modifiers, Src1RC:$src1,
           Src2Mod:$src2_modifiers, Src2RC:$src2,
           op_sel:$op_sel, op_sel_hi:$op_sel_hi,
           neg_lo:$neg_lo, neg_hi:$neg_hi))
  );
}

class getInsVOP3OpSel <RegisterOperand Src0RC,
                       RegisterOperand Src1RC,
                       RegisterOperand Src2RC,
                       int NumSrcArgs,
                       bit HasClamp,
                       Operand Src0Mod,
                       Operand Src1Mod,
                       Operand Src2Mod> {
  dag ret = !if (!eq(NumSrcArgs, 2),
    !if (HasClamp,
      (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
           Src1Mod:$src1_modifiers, Src1RC:$src1,
           clampmod:$clamp,
           op_sel:$op_sel),
      (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
           Src1Mod:$src1_modifiers, Src1RC:$src1,
           op_sel:$op_sel)),
    // else NumSrcArgs == 3
    !if (HasClamp,
      (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
           Src1Mod:$src1_modifiers, Src1RC:$src1,
           Src2Mod:$src2_modifiers, Src2RC:$src2,
           clampmod:$clamp,
           op_sel:$op_sel),
      (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
           Src1Mod:$src1_modifiers, Src1RC:$src1,
           Src2Mod:$src2_modifiers, Src2RC:$src2,
           op_sel:$op_sel))
  );
}

class getInsDPP <RegisterOperand DstRC, RegisterClass Src0RC, RegisterClass Src1RC,
                 int NumSrcArgs, bit HasModifiers,
                 Operand Src0Mod, Operand Src1Mod> {

  dag ret = !if (!eq(NumSrcArgs, 0),
                // VOP1 without input operands (V_NOP)
                (ins dpp_ctrl:$dpp_ctrl, row_mask:$row_mask,
                     bank_mask:$bank_mask, bound_ctrl:$bound_ctrl),
            !if (!eq(NumSrcArgs, 1),
              !if (!eq(HasModifiers, 1),
                // VOP1_DPP with modifiers
                (ins DstRC:$old, Src0Mod:$src0_modifiers,
                     Src0RC:$src0, dpp_ctrl:$dpp_ctrl, row_mask:$row_mask,
                     bank_mask:$bank_mask, bound_ctrl:$bound_ctrl)
              /* else */,
                // VOP1_DPP without modifiers
                (ins DstRC:$old, Src0RC:$src0,
                     dpp_ctrl:$dpp_ctrl, row_mask:$row_mask,
                     bank_mask:$bank_mask, bound_ctrl:$bound_ctrl)
              /* endif */)
              /* NumSrcArgs == 2 */,
              !if (!eq(HasModifiers, 1),
                // VOP2_DPP with modifiers
                (ins DstRC:$old,
                     Src0Mod:$src0_modifiers, Src0RC:$src0,
                     Src1Mod:$src1_modifiers, Src1RC:$src1,
                     dpp_ctrl:$dpp_ctrl, row_mask:$row_mask,
                     bank_mask:$bank_mask, bound_ctrl:$bound_ctrl)
              /* else */,
                // VOP2_DPP without modifiers
                (ins DstRC:$old,
                     Src0RC:$src0, Src1RC:$src1, dpp_ctrl:$dpp_ctrl,
                     row_mask:$row_mask, bank_mask:$bank_mask,
                     bound_ctrl:$bound_ctrl)
             /* endif */)));
}



// Ins for SDWA
class getInsSDWA <RegisterOperand Src0RC, RegisterOperand Src1RC, int NumSrcArgs,
                  bit HasSDWAOMod, Operand Src0Mod, Operand Src1Mod,
                  ValueType DstVT> {

  dag ret = !if(!eq(NumSrcArgs, 0),
               // VOP1 without input operands (V_NOP)
               (ins),
            !if(!eq(NumSrcArgs, 1),
               // VOP1
               !if(!eq(HasSDWAOMod, 0),
                  // VOP1_SDWA without omod
                  (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
                       clampmod:$clamp,
                       dst_sel:$dst_sel, dst_unused:$dst_unused,
                       src0_sel:$src0_sel),
                  // VOP1_SDWA with omod
                  (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
                       clampmod:$clamp, omod:$omod,
                       dst_sel:$dst_sel, dst_unused:$dst_unused,
                       src0_sel:$src0_sel)),
            !if(!eq(NumSrcArgs, 2),
               !if(!eq(DstVT.Size, 1),
                  // VOPC_SDWA
                  (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
                       Src1Mod:$src1_modifiers, Src1RC:$src1,
                       clampmod:$clamp, src0_sel:$src0_sel, src1_sel:$src1_sel),
                  // VOP2_SDWA
                  !if(!eq(HasSDWAOMod, 0),
                     // VOP2_SDWA without omod
                     (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
                          Src1Mod:$src1_modifiers, Src1RC:$src1,
                          clampmod:$clamp,
                          dst_sel:$dst_sel, dst_unused:$dst_unused,
                          src0_sel:$src0_sel, src1_sel:$src1_sel),
                     // VOP2_SDWA with omod
                     (ins Src0Mod:$src0_modifiers, Src0RC:$src0,
                          Src1Mod:$src1_modifiers, Src1RC:$src1,
                          clampmod:$clamp, omod:$omod,
                          dst_sel:$dst_sel, dst_unused:$dst_unused,
                          src0_sel:$src0_sel, src1_sel:$src1_sel))),
            (ins)/* endif */)));
}

// Outs for DPP and SDWA
class getOutsExt <bit HasDst, ValueType DstVT, RegisterOperand DstRCExt> {
  dag ret = !if(HasDst,
                !if(!eq(DstVT.Size, 1),
                    (outs), // no dst for VOPC, we use "vcc"-token as dst in SDWA VOPC instructions
                    (outs DstRCExt:$vdst)),
                (outs)); // V_NOP
}

// Outs for SDWA
class getOutsSDWA <bit HasDst, ValueType DstVT, RegisterOperand DstRCSDWA> {
  dag ret = !if(HasDst,
                !if(!eq(DstVT.Size, 1),
                    (outs DstRCSDWA:$sdst),
                    (outs DstRCSDWA:$vdst)),
                (outs)); // V_NOP
}

// Returns the assembly string for the inputs and outputs of a VOP[12C]
// instruction.  This does not add the _e32 suffix, so it can be reused
// by getAsm64.
class getAsm32 <bit HasDst, int NumSrcArgs, ValueType DstVT = i32> {
  string dst = !if(!eq(DstVT.Size, 1), "$sdst", "$vdst"); // use $sdst for VOPC
  string src0 = ", $src0";
  string src1 = ", $src1";
  string src2 = ", $src2";
  string ret = !if(HasDst, dst, "") #
               !if(!eq(NumSrcArgs, 1), src0, "") #
               !if(!eq(NumSrcArgs, 2), src0#src1, "") #
               !if(!eq(NumSrcArgs, 3), src0#src1#src2, "");
}

// Returns the assembly string for the inputs and outputs of a VOP3
// instruction.
class getAsm64 <bit HasDst, int NumSrcArgs, bit HasIntClamp, bit HasModifiers,
                bit HasOMod, ValueType DstVT = i32> {
  string dst = !if(!eq(DstVT.Size, 1), "$sdst", "$vdst"); // use $sdst for VOPC
  string src0 = !if(!eq(NumSrcArgs, 1), "$src0_modifiers", "$src0_modifiers,");
  string src1 = !if(!eq(NumSrcArgs, 1), "",
                   !if(!eq(NumSrcArgs, 2), " $src1_modifiers",
                                           " $src1_modifiers,"));
  string src2 = !if(!eq(NumSrcArgs, 3), " $src2_modifiers", "");
  string iclamp = !if(HasIntClamp, "$clamp", "");
  string ret =
  !if(!eq(HasModifiers, 0),
      getAsm32<HasDst, NumSrcArgs, DstVT>.ret # iclamp,
      dst#", "#src0#src1#src2#"$clamp"#!if(HasOMod, "$omod", ""));
}

// Returns the assembly string for the inputs and outputs of a VOP3P
// instruction.
class getAsmVOP3P <bit HasDst, int NumSrcArgs, bit HasModifiers,
                   bit HasClamp, ValueType DstVT = i32> {
  string dst = " $vdst";
  string src0 = !if(!eq(NumSrcArgs, 1), "$src0", "$src0,");
  string src1 = !if(!eq(NumSrcArgs, 1), "",
                   !if(!eq(NumSrcArgs, 2), " $src1",
                                           " $src1,"));
  string src2 = !if(!eq(NumSrcArgs, 3), " $src2", "");

  string mods = !if(HasModifiers, "$neg_lo$neg_hi", "");
  string clamp = !if(HasClamp, "$clamp", "");

  // Each modifier is printed as an array of bits for each operand, so
  // all operands are printed as part of src0_modifiers.
  string ret = dst#", "#src0#src1#src2#"$op_sel$op_sel_hi"#mods#clamp;
}

class getAsmVOP3OpSel <int NumSrcArgs,
                       bit HasClamp,
                       bit Src0HasMods,
                       bit Src1HasMods,
                       bit Src2HasMods> {
  string dst = " $vdst";

  string isrc0 = !if(!eq(NumSrcArgs, 1), "$src0", "$src0,");
  string isrc1 = !if(!eq(NumSrcArgs, 1), "",
                     !if(!eq(NumSrcArgs, 2), " $src1",
                                             " $src1,"));
  string isrc2 = !if(!eq(NumSrcArgs, 3), " $src2", "");

  string fsrc0 = !if(!eq(NumSrcArgs, 1), "$src0_modifiers", "$src0_modifiers,");
  string fsrc1 = !if(!eq(NumSrcArgs, 1), "",
                     !if(!eq(NumSrcArgs, 2), " $src1_modifiers",
                                             " $src1_modifiers,"));
  string fsrc2 = !if(!eq(NumSrcArgs, 3), " $src2_modifiers", "");

  string src0 = !if(Src0HasMods, fsrc0, isrc0);
  string src1 = !if(Src1HasMods, fsrc1, isrc1);
  string src2 = !if(Src2HasMods, fsrc2, isrc2);

  string clamp = !if(HasClamp, "$clamp", "");

  string ret = dst#", "#src0#src1#src2#"$op_sel"#clamp;
}

class getAsmDPP <bit HasDst, int NumSrcArgs, bit HasModifiers, ValueType DstVT = i32> {
  string dst = !if(HasDst,
                   !if(!eq(DstVT.Size, 1),
                       "$sdst",
                       "$vdst"),
                    ""); // use $sdst for VOPC
  string src0 = !if(!eq(NumSrcArgs, 1), "$src0_modifiers", "$src0_modifiers,");
  string src1 = !if(!eq(NumSrcArgs, 1), "",
                   !if(!eq(NumSrcArgs, 2), " $src1_modifiers",
                                           " $src1_modifiers,"));
  string args = !if(!eq(HasModifiers, 0),
                     getAsm32<0, NumSrcArgs, DstVT>.ret,
                     ", "#src0#src1);
  string ret = dst#args#" $dpp_ctrl$row_mask$bank_mask$bound_ctrl";
}

class getAsmSDWA <bit HasDst, int NumSrcArgs, ValueType DstVT = i32> {
  string dst = !if(HasDst,
                   !if(!eq(DstVT.Size, 1),
                       " vcc", // use vcc token as dst for VOPC instructioins
                       "$vdst"),
                    "");
  string src0 = "$src0_modifiers";
  string src1 = "$src1_modifiers";
  string args = !if(!eq(NumSrcArgs, 0),
                    "",
                    !if(!eq(NumSrcArgs, 1),
                        ", "#src0#"$clamp",
                        ", "#src0#", "#src1#"$clamp"
                     )
                );
  string sdwa = !if(!eq(NumSrcArgs, 0),
                    "",
                    !if(!eq(NumSrcArgs, 1),
                        " $dst_sel $dst_unused $src0_sel",
                        !if(!eq(DstVT.Size, 1),
                            " $src0_sel $src1_sel", // No dst_sel and dst_unused for VOPC
                            " $dst_sel $dst_unused $src0_sel $src1_sel"
                        )
                    )
                );
  string ret = dst#args#sdwa;
}

class getAsmSDWA9 <bit HasDst, bit HasOMod, int NumSrcArgs,
                   ValueType DstVT = i32> {
  string dst = !if(HasDst,
                   !if(!eq(DstVT.Size, 1),
                       "$sdst", // VOPC
                       "$vdst"), // VOP1/2
                    "");
  string src0 = "$src0_modifiers";
  string src1 = "$src1_modifiers";
  string out_mods = !if(!eq(HasOMod, 0), "$clamp", "$clamp$omod");
  string args = !if(!eq(NumSrcArgs, 0), "",
                    !if(!eq(NumSrcArgs, 1),
                        ", "#src0,
                        ", "#src0#", "#src1
                     )
                );
  string sdwa = !if(!eq(NumSrcArgs, 0), "",
                    !if(!eq(NumSrcArgs, 1),
                        out_mods#" $dst_sel $dst_unused $src0_sel",
                        !if(!eq(DstVT.Size, 1),
                            " $src0_sel $src1_sel", // No dst_sel, dst_unused and output modifiers for VOPC
                            out_mods#" $dst_sel $dst_unused $src0_sel $src1_sel"
                        )
                    )
                );
  string ret = dst#args#sdwa;
}


// Function that checks if instruction supports DPP and SDWA
class getHasExt <int NumSrcArgs, ValueType DstVT = i32, ValueType Src0VT = i32,
                 ValueType Src1VT = i32> {
  bit ret = !if(!eq(NumSrcArgs, 3),
                0, // NumSrcArgs == 3 - No DPP or SDWA for VOP3
                !if(!eq(DstVT.Size, 64),
                    0, // 64-bit dst - No DPP or SDWA for 64-bit operands
                    !if(!eq(Src0VT.Size, 64),
                        0, // 64-bit src0
                        !if(!eq(Src1VT.Size, 64),
                            0, // 64-bit src2
                            1
                        )
                    )
                )
            );
}

class getHasDPP <int NumSrcArgs, ValueType DstVT = i32, ValueType Src0VT = i32,
                 ValueType Src1VT = i32> {
  bit ret = !if(!eq(NumSrcArgs, 0), 0,
                getHasExt<NumSrcArgs, DstVT, Src0VT, Src1VT>.ret);
}

class BitOr<bit a, bit b> {
  bit ret = !if(a, 1, !if(b, 1, 0));
}

class BitAnd<bit a, bit b> {
  bit ret = !if(a, !if(b, 1, 0), 0);
}

def PatGenMode {
  int NoPattern = 0;
  int Pattern   = 1;
}

class VOPProfile <list<ValueType> _ArgVT> {

  field list<ValueType> ArgVT = _ArgVT;

  field ValueType DstVT = ArgVT[0];
  field ValueType Src0VT = ArgVT[1];
  field ValueType Src1VT = ArgVT[2];
  field ValueType Src2VT = ArgVT[3];
  field RegisterOperand DstRC = getVALUDstForVT<DstVT>.ret;
  field RegisterOperand DstRCDPP = getVALUDstForVT<DstVT>.ret;
  field RegisterOperand DstRCSDWA = getSDWADstForVT<DstVT>.ret;
  field RegisterOperand Src0RC32 = getVOPSrc0ForVT<Src0VT>.ret;
  field RegisterClass Src1RC32 = getVregSrcForVT<Src1VT>.ret;
  field RegisterOperand Src0RC64 = getVOP3SrcForVT<Src0VT>.ret;
  field RegisterOperand Src1RC64 = getVOP3SrcForVT<Src1VT>.ret;
  field RegisterOperand Src2RC64 = getVOP3SrcForVT<Src2VT>.ret;
  field RegisterClass Src0DPP = getVregSrcForVT<Src0VT>.ret;
  field RegisterClass Src1DPP = getVregSrcForVT<Src1VT>.ret;
  field RegisterOperand Src0SDWA = getSDWASrcForVT<Src0VT>.ret;
  field RegisterOperand Src1SDWA = getSDWASrcForVT<Src0VT>.ret;
  field Operand Src0Mod = getSrcMod<Src0VT>.ret;
  field Operand Src1Mod = getSrcMod<Src1VT>.ret;
  field Operand Src2Mod = getSrcMod<Src2VT>.ret;
  field Operand Src0ModDPP = getSrcModExt<Src0VT>.ret;
  field Operand Src1ModDPP = getSrcModExt<Src1VT>.ret;
  field Operand Src0ModSDWA = getSrcModSDWA<Src0VT>.ret;
  field Operand Src1ModSDWA = getSrcModSDWA<Src1VT>.ret;


  field bit HasDst = !if(!eq(DstVT.Value, untyped.Value), 0, 1);
  field bit HasDst32 = HasDst;
  field bit EmitDst = HasDst; // force dst encoding, see v_movreld_b32 special case
  field int NumSrcArgs = getNumSrcArgs<Src0VT, Src1VT, Src2VT>.ret;
  field bit HasSrc0 = !if(!eq(Src0VT.Value, untyped.Value), 0, 1);
  field bit HasSrc1 = !if(!eq(Src1VT.Value, untyped.Value), 0, 1);
  field bit HasSrc2 = !if(!eq(Src2VT.Value, untyped.Value), 0, 1);

  // TODO: Modifiers logic is somewhat adhoc here, to be refined later
  field bit HasModifiers = isModifierType<Src0VT>.ret;

  field bit HasSrc0FloatMods = isFloatType<Src0VT>.ret;
  field bit HasSrc1FloatMods = isFloatType<Src1VT>.ret;
  field bit HasSrc2FloatMods = isFloatType<Src2VT>.ret;

  field bit HasSrc0IntMods = isIntType<Src0VT>.ret;
  field bit HasSrc1IntMods = isIntType<Src1VT>.ret;
  field bit HasSrc2IntMods = isIntType<Src2VT>.ret;

  field bit HasSrc0Mods = HasModifiers;
  field bit HasSrc1Mods = !if(HasModifiers, BitOr<HasSrc1FloatMods, HasSrc1IntMods>.ret, 0);
  field bit HasSrc2Mods = !if(HasModifiers, BitOr<HasSrc2FloatMods, HasSrc2IntMods>.ret, 0);

  field bit HasClamp = HasModifiers;
  field bit HasSDWAClamp = EmitDst;
  field bit HasFPClamp = BitAnd<isFloatType<DstVT>.ret, HasClamp>.ret;
  field bit HasIntClamp = !if(isFloatType<DstVT>.ret, 0, HasClamp);
  field bit HasClampLo = HasClamp;
  field bit HasClampHi = BitAnd<isPackedType<DstVT>.ret, HasClamp>.ret;
  field bit HasHigh = 0;

  field bit IsPacked = isPackedType<Src0VT>.ret;
  field bit HasOpSel = IsPacked;
  field bit HasOMod = !if(HasOpSel, 0, isFloatType<DstVT>.ret);
  field bit HasSDWAOMod = isFloatType<DstVT>.ret;

  field bit HasExt = getHasExt<NumSrcArgs, DstVT, Src0VT, Src1VT>.ret;
  field bit HasExtDPP = getHasDPP<NumSrcArgs, DstVT, Src0VT, Src1VT>.ret;
  field bit HasExtSDWA = HasExt;
  field bit HasExtSDWA9 = HasExt;
  field int NeedPatGen = PatGenMode.NoPattern;

  field Operand Src0PackedMod = !if(HasSrc0FloatMods, PackedF16InputMods, PackedI16InputMods);
  field Operand Src1PackedMod = !if(HasSrc1FloatMods, PackedF16InputMods, PackedI16InputMods);
  field Operand Src2PackedMod = !if(HasSrc2FloatMods, PackedF16InputMods, PackedI16InputMods);

  field dag Outs = !if(HasDst,(outs DstRC:$vdst),(outs));

  // VOP3b instructions are a special case with a second explicit
  // output. This is manually overridden for them.
  field dag Outs32 = Outs;
  field dag Outs64 = Outs;
  field dag OutsDPP = getOutsExt<HasDst, DstVT, DstRCDPP>.ret;
  field dag OutsSDWA = getOutsSDWA<HasDst, DstVT, DstRCSDWA>.ret;

  field dag Ins32 = getIns32<Src0RC32, Src1RC32, NumSrcArgs>.ret;
  field dag Ins64 = getIns64<Src0RC64, Src1RC64, Src2RC64, NumSrcArgs,
                             HasIntClamp, HasModifiers, HasOMod, Src0Mod, Src1Mod,
                             Src2Mod>.ret;
  field dag InsVOP3P = getInsVOP3P<Src0RC64, Src1RC64, Src2RC64,
                                   NumSrcArgs, HasClamp,
                                   Src0PackedMod, Src1PackedMod, Src2PackedMod>.ret;
  field dag InsVOP3OpSel = getInsVOP3OpSel<Src0RC64, Src1RC64, Src2RC64,
                                           NumSrcArgs,
                                           HasClamp,
                                           getOpSelMod<Src0VT>.ret,
                                           getOpSelMod<Src1VT>.ret,
                                           getOpSelMod<Src2VT>.ret>.ret;
  field dag InsDPP = !if(HasExtDPP,
                         getInsDPP<DstRCDPP, Src0DPP, Src1DPP, NumSrcArgs,
                                   HasModifiers, Src0ModDPP, Src1ModDPP>.ret,
                         (ins));
  field dag InsSDWA = getInsSDWA<Src0SDWA, Src1SDWA, NumSrcArgs,
                                 HasSDWAOMod, Src0ModSDWA, Src1ModSDWA,
                                 DstVT>.ret;


  field string Asm32 = getAsm32<HasDst, NumSrcArgs, DstVT>.ret;
  field string Asm64 = getAsm64<HasDst, NumSrcArgs, HasIntClamp, HasModifiers, HasOMod, DstVT>.ret;
  field string AsmVOP3P = getAsmVOP3P<HasDst, NumSrcArgs, HasModifiers, HasClamp, DstVT>.ret;
  field string AsmVOP3OpSel = getAsmVOP3OpSel<NumSrcArgs,
                                              HasClamp,
                                              HasSrc0FloatMods,
                                              HasSrc1FloatMods,
                                              HasSrc2FloatMods>.ret;
  field string AsmDPP = !if(HasExtDPP,
                            getAsmDPP<HasDst, NumSrcArgs, HasModifiers, DstVT>.ret, "");
  field string AsmSDWA = getAsmSDWA<HasDst, NumSrcArgs, DstVT>.ret;
  field string AsmSDWA9 = getAsmSDWA9<HasDst, HasSDWAOMod, NumSrcArgs, DstVT>.ret;
}

class VOP_NO_EXT <VOPProfile p> : VOPProfile <p.ArgVT> {
  let HasExt = 0;
  let HasExtDPP = 0;
  let HasExtSDWA = 0;
  let HasExtSDWA9 = 0;
}

class VOP_PAT_GEN <VOPProfile p, int mode=PatGenMode.Pattern> : VOPProfile <p.ArgVT> {
  let NeedPatGen = mode;
}

def VOP_F16_F16 : VOPProfile <[f16, f16, untyped, untyped]>;
def VOP_F16_I16 : VOPProfile <[f16, i16, untyped, untyped]>;
def VOP_I16_F16 : VOPProfile <[i16, f16, untyped, untyped]>;

def VOP_F16_F16_F16 : VOPProfile <[f16, f16, f16, untyped]>;
def VOP_F16_F16_I16 : VOPProfile <[f16, f16, i16, untyped]>;
def VOP_F16_F16_I32 : VOPProfile <[f16, f16, i32, untyped]>;
def VOP_I16_I16_I16 : VOPProfile <[i16, i16, i16, untyped]>;

def VOP_I16_I16_I16_I16 : VOPProfile <[i16, i16, i16, i16, untyped]>;
def VOP_F16_F16_F16_F16 : VOPProfile <[f16, f16, f16, f16, untyped]>;

def VOP_I32_I16_I16_I32 : VOPProfile <[i32, i16, i16, i32, untyped]>;

def VOP_V2F16_V2F16_V2F16 : VOPProfile <[v2f16, v2f16, v2f16, untyped]>;
def VOP_V2I16_V2I16_V2I16 : VOPProfile <[v2i16, v2i16, v2i16, untyped]>;
def VOP_B32_F16_F16 : VOPProfile <[i32, f16, f16, untyped]>;

def VOP_V2F16_V2F16_V2F16_V2F16 : VOPProfile <[v2f16, v2f16, v2f16, v2f16]>;
def VOP_V2I16_V2I16_V2I16_V2I16 : VOPProfile <[v2i16, v2i16, v2i16, v2i16]>;
def VOP_V2I16_F32_F32 : VOPProfile <[v2i16, f32, f32, untyped]>;
def VOP_V2I16_I32_I32 : VOPProfile <[v2i16, i32, i32, untyped]>;

def VOP_F32_V2F16_V2F16_V2F16 : VOPProfile <[f32, v2f16, v2f16, v2f16]>;

def VOP_NONE : VOPProfile <[untyped, untyped, untyped, untyped]>;

def VOP_F32_F32 : VOPProfile <[f32, f32, untyped, untyped]>;
def VOP_F32_F64 : VOPProfile <[f32, f64, untyped, untyped]>;
def VOP_F32_I32 : VOPProfile <[f32, i32, untyped, untyped]>;
def VOP_F64_F32 : VOPProfile <[f64, f32, untyped, untyped]>;
def VOP_F64_F64 : VOPProfile <[f64, f64, untyped, untyped]>;
def VOP_F64_I32 : VOPProfile <[f64, i32, untyped, untyped]>;
def VOP_I32_F32 : VOPProfile <[i32, f32, untyped, untyped]>;
def VOP_I32_F64 : VOPProfile <[i32, f64, untyped, untyped]>;
def VOP_I32_I32 : VOPProfile <[i32, i32, untyped, untyped]>;
def VOP_F16_F32 : VOPProfile <[f16, f32, untyped, untyped]>;
def VOP_F32_F16 : VOPProfile <[f32, f16, untyped, untyped]>;

def VOP_F32_F32_F16 : VOPProfile <[f32, f32, f16, untyped]>;
def VOP_F32_F32_F32 : VOPProfile <[f32, f32, f32, untyped]>;
def VOP_F32_F32_I32 : VOPProfile <[f32, f32, i32, untyped]>;
def VOP_F64_F64_F64 : VOPProfile <[f64, f64, f64, untyped]>;
def VOP_F64_F64_I32 : VOPProfile <[f64, f64, i32, untyped]>;
def VOP_I32_F32_F32 : VOPProfile <[i32, f32, f32, untyped]>;
def VOP_I32_F32_I32 : VOPProfile <[i32, f32, i32, untyped]>;
def VOP_I32_I32_I32 : VOPProfile <[i32, i32, i32, untyped]>;
def VOP_V2F16_F32_F32 : VOPProfile <[v2f16, f32, f32, untyped]>;
def VOP_F32_F16_F16_F16 : VOPProfile <[f32, f16, f16, f16]>;

def VOP_I64_I64_I32 : VOPProfile <[i64, i64, i32, untyped]>;
def VOP_I64_I32_I64 : VOPProfile <[i64, i32, i64, untyped]>;
def VOP_I64_I64_I64 : VOPProfile <[i64, i64, i64, untyped]>;

def VOP_F16_F32_F16_F32 : VOPProfile <[f16, f32, f16, f32]>;
def VOP_F32_F32_F16_F16 : VOPProfile <[f32, f32, f16, f16]>;
def VOP_F32_F32_F32_F32 : VOPProfile <[f32, f32, f32, f32]>;
def VOP_F64_F64_F64_F64 : VOPProfile <[f64, f64, f64, f64]>;
def VOP_I32_I32_I32_I32 : VOPProfile <[i32, i32, i32, i32]>;
def VOP_I64_I32_I32_I64 : VOPProfile <[i64, i32, i32, i64]>;
def VOP_I32_F32_I32_I32 : VOPProfile <[i32, f32, i32, i32]>;
def VOP_I64_I64_I32_I64 : VOPProfile <[i64, i64, i32, i64]>;
def VOP_V4I32_I64_I32_V4I32 : VOPProfile <[v4i32, i64, i32, v4i32]>;

def VOP_F32_V2F16_V2F16_F32 : VOPProfile <[f32, v2f16, v2f16, f32]>;
def VOP_I32_V2I16_V2I16_I32 : VOPProfile <[i32, v2i16, v2i16, i32]>;

class Commutable_REV <string revOp, bit isOrig> {
  string RevOp = revOp;
  bit IsOrig = isOrig;
}

class AtomicNoRet <string noRetOp, bit isRet> {
  string NoRetOp = noRetOp;
  bit IsRet = isRet;
}

//===----------------------------------------------------------------------===//
// Interpolation opcodes
//===----------------------------------------------------------------------===//

class VINTRPDstOperand <RegisterClass rc> : RegisterOperand <rc, "printVINTRPDst">;

class VINTRP_Pseudo <string opName, dag outs, dag ins, list<dag> pattern> :
  VINTRPCommon <outs, ins, "", pattern>,
  SIMCInstr<opName, SIEncodingFamily.NONE> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

class VINTRP_Real_si <bits <2> op, string opName, dag outs, dag ins,
                      string asm> :
  VINTRPCommon <outs, ins, asm, []>,
  VINTRPe <op>,
  SIMCInstr<opName, SIEncodingFamily.SI> {
  let AssemblerPredicate = SIAssemblerPredicate;
  let DecoderNamespace = "SICI";
  let DisableDecoder = DisableSIDecoder;
}

class VINTRP_Real_vi <bits <2> op, string opName, dag outs, dag ins,
                      string asm> :
  VINTRPCommon <outs, ins, asm, []>,
  VINTRPe_vi <op>,
  SIMCInstr<opName, SIEncodingFamily.VI> {
  let AssemblerPredicate = VIAssemblerPredicate;
  let DecoderNamespace = "VI";
  let DisableDecoder = DisableVIDecoder;
}

multiclass VINTRP_m <bits <2> op, dag outs, dag ins, string asm,
                     list<dag> pattern = []> {
  def "" : VINTRP_Pseudo <NAME, outs, ins, pattern>;

  def _si : VINTRP_Real_si <op, NAME, outs, ins, asm>;

  def _vi : VINTRP_Real_vi <op, NAME, outs, ins, asm>;
}

//===----------------------------------------------------------------------===//
// Vector instruction mappings
//===----------------------------------------------------------------------===//

// Maps an opcode in e32 form to its e64 equivalent
def getVOPe64 : InstrMapping {
  let FilterClass = "VOP";
  let RowFields = ["OpName"];
  let ColFields = ["Size", "VOP3"];
  let KeyCol = ["4", "0"];
  let ValueCols = [["8", "1"]];
}

// Maps an opcode in e64 form to its e32 equivalent
def getVOPe32 : InstrMapping {
  let FilterClass = "VOP";
  let RowFields = ["OpName"];
  let ColFields = ["Size", "VOP3"];
  let KeyCol = ["8", "1"];
  let ValueCols = [["4", "0"]];
}

// Maps ordinary instructions to their SDWA counterparts
def getSDWAOp : InstrMapping {
  let FilterClass = "VOP";
  let RowFields = ["OpName"];
  let ColFields = ["AsmVariantName"];
  let KeyCol = ["Default"];
  let ValueCols = [["SDWA"]];
}

// Maps SDWA instructions to their ordinary counterparts
def getBasicFromSDWAOp : InstrMapping {
  let FilterClass = "VOP";
  let RowFields = ["OpName"];
  let ColFields = ["AsmVariantName"];
  let KeyCol = ["SDWA"];
  let ValueCols = [["Default"]];
}

// Maps ordinary instructions to their DPP counterparts
def getDPPOp32 : InstrMapping {
  let FilterClass = "VOP";
  let RowFields = ["OpName"];
  let ColFields = ["AsmVariantName"];
  let KeyCol = ["Default"];
  let ValueCols = [["DPP"]];
}

// Maps an commuted opcode to its original version
def getCommuteOrig : InstrMapping {
  let FilterClass = "Commutable_REV";
  let RowFields = ["RevOp"];
  let ColFields = ["IsOrig"];
  let KeyCol = ["0"];
  let ValueCols = [["1"]];
}

// Maps an original opcode to its commuted version
def getCommuteRev : InstrMapping {
  let FilterClass = "Commutable_REV";
  let RowFields = ["RevOp"];
  let ColFields = ["IsOrig"];
  let KeyCol = ["1"];
  let ValueCols = [["0"]];
}

def getMCOpcodeGen : InstrMapping {
  let FilterClass = "SIMCInstr";
  let RowFields = ["PseudoInstr"];
  let ColFields = ["Subtarget"];
  let KeyCol = [!cast<string>(SIEncodingFamily.NONE)];
  let ValueCols = [[!cast<string>(SIEncodingFamily.SI)],
                   [!cast<string>(SIEncodingFamily.VI)],
                   [!cast<string>(SIEncodingFamily.SDWA)],
                   [!cast<string>(SIEncodingFamily.SDWA9)],
                   // GFX80 encoding is added to work around a multiple matching
                   // issue for buffer instructions with unpacked d16 data. This
                   // does not actually change the encoding, and thus may be
                   // removed later.
                   [!cast<string>(SIEncodingFamily.GFX80)],
                   [!cast<string>(SIEncodingFamily.GFX9)]];
}

// Get equivalent SOPK instruction.
def getSOPKOp : InstrMapping {
  let FilterClass = "SOPKInstTable";
  let RowFields = ["BaseCmpOp"];
  let ColFields = ["IsSOPK"];
  let KeyCol = ["0"];
  let ValueCols = [["1"]];
}

def getAddr64Inst : InstrMapping {
  let FilterClass = "MUBUFAddr64Table";
  let RowFields = ["OpName"];
  let ColFields = ["IsAddr64"];
  let KeyCol = ["0"];
  let ValueCols = [["1"]];
}

def getIfAddr64Inst : InstrMapping {
  let FilterClass = "MUBUFAddr64Table";
  let RowFields = ["OpName"];
  let ColFields = ["IsAddr64"];
  let KeyCol = ["1"];
  let ValueCols = [["1"]];
}

def getMUBUFNoLdsInst : InstrMapping {
  let FilterClass = "MUBUFLdsTable";
  let RowFields = ["OpName"];
  let ColFields = ["IsLds"];
  let KeyCol = ["1"];
  let ValueCols = [["0"]];
}

// Maps an atomic opcode to its version with a return value.
def getAtomicRetOp : InstrMapping {
  let FilterClass = "AtomicNoRet";
  let RowFields = ["NoRetOp"];
  let ColFields = ["IsRet"];
  let KeyCol = ["0"];
  let ValueCols = [["1"]];
}

// Maps an atomic opcode to its returnless version.
def getAtomicNoRetOp : InstrMapping {
  let FilterClass = "AtomicNoRet";
  let RowFields = ["NoRetOp"];
  let ColFields = ["IsRet"];
  let KeyCol = ["1"];
  let ValueCols = [["0"]];
}

// Maps a GLOBAL to its SADDR form.
def getGlobalSaddrOp : InstrMapping {
  let FilterClass = "GlobalSaddrTable";
  let RowFields = ["SaddrOp"];
  let ColFields = ["IsSaddr"];
  let KeyCol = ["0"];
  let ValueCols = [["1"]];
}

include "SIInstructions.td"

include "DSInstructions.td"
include "MIMGInstructions.td"
