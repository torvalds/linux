//===---- SMInstructions.td - Scalar Memory Instruction Defintions --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def smrd_offset_8 : NamedOperandU32<"SMRDOffset8",
                                  NamedMatchClass<"SMRDOffset8">> {
  let OperandType = "OPERAND_IMMEDIATE";
}

def smrd_offset_20 : NamedOperandU32<"SMRDOffset20",
                                  NamedMatchClass<"SMRDOffset20">> {
  let OperandType = "OPERAND_IMMEDIATE";
}

//===----------------------------------------------------------------------===//
// Scalar Memory classes
//===----------------------------------------------------------------------===//

class SM_Pseudo <string opName, dag outs, dag ins, string asmOps, list<dag> pattern=[]> :
  InstSI <outs, ins, "", pattern>,
  SIMCInstr<opName, SIEncodingFamily.NONE> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;

  let LGKM_CNT = 1;
  let SMRD = 1;
  let mayStore = 0;
  let mayLoad = 1;
  let hasSideEffects = 0;
  let UseNamedOperandTable = 1;
  let SchedRW = [WriteSMEM];
  let SubtargetPredicate = isGCN;

  string Mnemonic = opName;
  string AsmOperands = asmOps;

  bits<1> has_sbase = 1;
  bits<1> has_sdst = 1;
  bit has_glc = 0;
  bits<1> has_offset = 1;
  bits<1> offset_is_imm = 0;
}

class SM_Real <SM_Pseudo ps>
  : InstSI<ps.OutOperandList, ps.InOperandList, ps.Mnemonic # ps.AsmOperands, []> {

  let isPseudo = 0;
  let isCodeGenOnly = 0;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;

  // encoding
  bits<7>  sbase;
  bits<7>  sdst;
  bits<32> offset;
  bits<1> imm = !if(ps.has_offset, ps.offset_is_imm, 0);
}

class SM_Probe_Pseudo <string opName, dag ins, bit isImm>
  : SM_Pseudo<opName, (outs), ins, " $sdata, $sbase, $offset"> {
  let mayLoad = 0;
  let mayStore = 0;
  let has_glc = 0;
  let LGKM_CNT = 0;
  let ScalarStore = 0;
  let hasSideEffects = 1;
  let offset_is_imm = isImm;
  let PseudoInstr = opName # !if(isImm, "_IMM", "_SGPR");
}

class SM_Load_Pseudo <string opName, dag outs, dag ins, string asmOps, list<dag> pattern=[]>
  : SM_Pseudo<opName, outs, ins, asmOps, pattern> {
  RegisterClass BaseClass;
  let mayLoad = 1;
  let mayStore = 0;
  let has_glc = 1;
}

class SM_Store_Pseudo <string opName, dag ins, string asmOps, list<dag> pattern = []>
  : SM_Pseudo<opName, (outs), ins, asmOps, pattern> {
  RegisterClass BaseClass;
  RegisterClass SrcClass;
  let mayLoad = 0;
  let mayStore = 1;
  let has_glc = 1;
  let ScalarStore = 1;
}

class SM_Discard_Pseudo <string opName, dag ins, bit isImm>
  : SM_Pseudo<opName, (outs), ins, " $sbase, $offset"> {
  let mayLoad = 0;
  let mayStore = 0;
  let has_glc = 0;
  let has_sdst = 0;
  let ScalarStore = 0;
  let hasSideEffects = 1;
  let offset_is_imm = isImm;
  let PseudoInstr = opName # !if(isImm, "_IMM", "_SGPR");
}

multiclass SM_Pseudo_Loads<string opName,
                           RegisterClass baseClass,
                           RegisterClass dstClass> {
  def _IMM  : SM_Load_Pseudo <opName,
                              (outs dstClass:$sdst),
                              (ins baseClass:$sbase, i32imm:$offset, i1imm:$glc),
                              " $sdst, $sbase, $offset$glc", []> {
    let offset_is_imm = 1;
    let BaseClass = baseClass;
    let PseudoInstr = opName # "_IMM";
    let has_glc = 1;
  }

  def _SGPR  : SM_Load_Pseudo <opName,
                              (outs dstClass:$sdst),
                              (ins baseClass:$sbase, SReg_32:$soff, i1imm:$glc),
                              " $sdst, $sbase, $offset$glc", []> {
    let BaseClass = baseClass;
    let PseudoInstr = opName # "_SGPR";
    let has_glc = 1;
  }
}

multiclass SM_Pseudo_Stores<string opName,
                           RegisterClass baseClass,
                           RegisterClass srcClass> {
  def _IMM  : SM_Store_Pseudo <opName,
    (ins srcClass:$sdata, baseClass:$sbase, i32imm:$offset, i1imm:$glc),
    " $sdata, $sbase, $offset$glc", []> {
    let offset_is_imm = 1;
    let BaseClass = baseClass;
    let SrcClass = srcClass;
    let PseudoInstr = opName # "_IMM";
  }

  def _SGPR  : SM_Store_Pseudo <opName,
    (ins srcClass:$sdata, baseClass:$sbase, SReg_32:$soff, i1imm:$glc),
    " $sdata, $sbase, $offset$glc", []> {
    let BaseClass = baseClass;
    let SrcClass = srcClass;
    let PseudoInstr = opName # "_SGPR";
  }
}

multiclass SM_Pseudo_Discards<string opName> {
  def _IMM  : SM_Discard_Pseudo <opName, (ins SReg_64:$sbase, smrd_offset_20:$offset), 1>;
  def _SGPR : SM_Discard_Pseudo <opName, (ins SReg_64:$sbase, SReg_32:$offset), 0>;
}

class SM_Time_Pseudo<string opName, SDPatternOperator node> : SM_Pseudo<
  opName, (outs SReg_64_XEXEC:$sdst), (ins),
  " $sdst", [(set i64:$sdst, (node))]> {
  let hasSideEffects = 1;
  let mayStore = 0;
  let mayLoad = 1;
  let has_sbase = 0;
  let has_offset = 0;
}

class SM_Inval_Pseudo <string opName, SDPatternOperator node> : SM_Pseudo<
  opName, (outs), (ins), "", [(node)]> {
  let hasSideEffects = 1;
  let mayStore = 1;
  let has_sdst = 0;
  let has_sbase = 0;
  let has_offset = 0;
}

multiclass SM_Pseudo_Probe<string opName, RegisterClass baseClass> {
  def _IMM  : SM_Probe_Pseudo <opName, (ins i8imm:$sdata, baseClass:$sbase, smrd_offset_20:$offset), 1>;
  def _SGPR : SM_Probe_Pseudo <opName, (ins i8imm:$sdata, baseClass:$sbase, SReg_32:$offset), 0>;
}

//===----------------------------------------------------------------------===//
// Scalar Atomic Memory Classes
//===----------------------------------------------------------------------===//

class SM_Atomic_Pseudo <string opName,
                        dag outs, dag ins, string asmOps, bit isRet>
  : SM_Pseudo<opName, outs, ins, asmOps, []> {

  bit glc = isRet;

  let mayLoad = 1;
  let mayStore = 1;
  let has_glc = 1;

  // Should these be set?
  let ScalarStore = 1;
  let hasSideEffects = 1;
  let maybeAtomic = 1;
}

class SM_Pseudo_Atomic<string opName,
                       RegisterClass baseClass,
                       RegisterClass dataClass,
                       bit isImm,
                       bit isRet> :
  SM_Atomic_Pseudo<opName,
                   !if(isRet, (outs dataClass:$sdst), (outs)),
                   !if(isImm,
                       (ins dataClass:$sdata, baseClass:$sbase, smrd_offset_20:$offset),
                       (ins dataClass:$sdata, baseClass:$sbase, SReg_32:$offset)),
                   !if(isRet, " $sdst", " $sdata") # ", $sbase, $offset" # !if(isRet, " glc", ""),
                   isRet> {
  let offset_is_imm = isImm;
  let PseudoInstr = opName # !if(isImm,
                                 !if(isRet, "_IMM_RTN", "_IMM"),
                                 !if(isRet, "_SGPR_RTN", "_SGPR"));

  let Constraints = !if(isRet, "$sdst = $sdata", "");
  let DisableEncoding = !if(isRet, "$sdata", "");
}

multiclass SM_Pseudo_Atomics<string opName,
                             RegisterClass baseClass,
                             RegisterClass dataClass> {
  def _IMM      : SM_Pseudo_Atomic <opName, baseClass, dataClass, 1, 0>;
  def _SGPR     : SM_Pseudo_Atomic <opName, baseClass, dataClass, 0, 0>;
  def _IMM_RTN  : SM_Pseudo_Atomic <opName, baseClass, dataClass, 1, 1>;
  def _SGPR_RTN : SM_Pseudo_Atomic <opName, baseClass, dataClass, 0, 1>;
}

//===----------------------------------------------------------------------===//
// Scalar Memory Instructions
//===----------------------------------------------------------------------===//

// We are using the SReg_32_XM0 and not the SReg_32 register class for 32-bit
// SMRD instructions, because the SReg_32_XM0 register class does not include M0
// and writing to M0 from an SMRD instruction will hang the GPU.

// XXX - SMEM instructions do not allow exec for data operand, but
// does sdst for SMRD on SI/CI?
defm S_LOAD_DWORD    : SM_Pseudo_Loads <"s_load_dword", SReg_64, SReg_32_XM0_XEXEC>;
defm S_LOAD_DWORDX2  : SM_Pseudo_Loads <"s_load_dwordx2", SReg_64, SReg_64_XEXEC>;
defm S_LOAD_DWORDX4  : SM_Pseudo_Loads <"s_load_dwordx4", SReg_64, SReg_128>;
defm S_LOAD_DWORDX8  : SM_Pseudo_Loads <"s_load_dwordx8", SReg_64, SReg_256>;
defm S_LOAD_DWORDX16 : SM_Pseudo_Loads <"s_load_dwordx16", SReg_64, SReg_512>;

defm S_BUFFER_LOAD_DWORD : SM_Pseudo_Loads <
  "s_buffer_load_dword", SReg_128, SReg_32_XM0_XEXEC
>;

// FIXME: exec_lo/exec_hi appear to be allowed for SMRD loads on
// SI/CI, bit disallowed for SMEM on VI.
defm S_BUFFER_LOAD_DWORDX2 : SM_Pseudo_Loads <
  "s_buffer_load_dwordx2", SReg_128, SReg_64_XEXEC
>;

defm S_BUFFER_LOAD_DWORDX4 : SM_Pseudo_Loads <
  "s_buffer_load_dwordx4", SReg_128, SReg_128
>;

defm S_BUFFER_LOAD_DWORDX8 : SM_Pseudo_Loads <
  "s_buffer_load_dwordx8", SReg_128, SReg_256
>;

defm S_BUFFER_LOAD_DWORDX16 : SM_Pseudo_Loads <
  "s_buffer_load_dwordx16", SReg_128, SReg_512
>;

defm S_STORE_DWORD : SM_Pseudo_Stores <"s_store_dword", SReg_64, SReg_32_XM0_XEXEC>;
defm S_STORE_DWORDX2 : SM_Pseudo_Stores <"s_store_dwordx2", SReg_64, SReg_64_XEXEC>;
defm S_STORE_DWORDX4 : SM_Pseudo_Stores <"s_store_dwordx4", SReg_64, SReg_128>;

defm S_BUFFER_STORE_DWORD : SM_Pseudo_Stores <
  "s_buffer_store_dword", SReg_128, SReg_32_XM0_XEXEC
>;

defm S_BUFFER_STORE_DWORDX2 : SM_Pseudo_Stores <
  "s_buffer_store_dwordx2", SReg_128, SReg_64_XEXEC
>;

defm S_BUFFER_STORE_DWORDX4 : SM_Pseudo_Stores <
  "s_buffer_store_dwordx4", SReg_128, SReg_128
>;


def S_MEMTIME : SM_Time_Pseudo <"s_memtime", int_amdgcn_s_memtime>;
def S_DCACHE_INV : SM_Inval_Pseudo <"s_dcache_inv", int_amdgcn_s_dcache_inv>;

let SubtargetPredicate = isCIVI in {
def S_DCACHE_INV_VOL : SM_Inval_Pseudo <"s_dcache_inv_vol", int_amdgcn_s_dcache_inv_vol>;
} // let SubtargetPredicate = isCIVI

let SubtargetPredicate = isVI in {
def S_DCACHE_WB     : SM_Inval_Pseudo <"s_dcache_wb", int_amdgcn_s_dcache_wb>;
def S_DCACHE_WB_VOL : SM_Inval_Pseudo <"s_dcache_wb_vol", int_amdgcn_s_dcache_wb_vol>;
def S_MEMREALTIME   : SM_Time_Pseudo <"s_memrealtime", int_amdgcn_s_memrealtime>;

defm S_ATC_PROBE        : SM_Pseudo_Probe <"s_atc_probe", SReg_64>;
defm S_ATC_PROBE_BUFFER : SM_Pseudo_Probe <"s_atc_probe_buffer", SReg_128>;
} // SubtargetPredicate = isVI

let SubtargetPredicate = HasFlatScratchInsts, Uses = [FLAT_SCR] in {
defm S_SCRATCH_LOAD_DWORD    : SM_Pseudo_Loads <"s_scratch_load_dword",   SReg_64, SReg_32_XM0_XEXEC>;
defm S_SCRATCH_LOAD_DWORDX2  : SM_Pseudo_Loads <"s_scratch_load_dwordx2", SReg_64, SReg_64_XEXEC>;
defm S_SCRATCH_LOAD_DWORDX4  : SM_Pseudo_Loads <"s_scratch_load_dwordx4", SReg_64, SReg_128>;

defm S_SCRATCH_STORE_DWORD   : SM_Pseudo_Stores <"s_scratch_store_dword",   SReg_64, SReg_32_XM0_XEXEC>;
defm S_SCRATCH_STORE_DWORDX2 : SM_Pseudo_Stores <"s_scratch_store_dwordx2", SReg_64, SReg_64_XEXEC>;
defm S_SCRATCH_STORE_DWORDX4 : SM_Pseudo_Stores <"s_scratch_store_dwordx4", SReg_64, SReg_128>;
} // SubtargetPredicate = HasFlatScratchInsts

let SubtargetPredicate = HasScalarAtomics in {

defm S_BUFFER_ATOMIC_SWAP         : SM_Pseudo_Atomics <"s_buffer_atomic_swap", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_CMPSWAP      : SM_Pseudo_Atomics <"s_buffer_atomic_cmpswap", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_ADD          : SM_Pseudo_Atomics <"s_buffer_atomic_add", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_SUB          : SM_Pseudo_Atomics <"s_buffer_atomic_sub", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_SMIN         : SM_Pseudo_Atomics <"s_buffer_atomic_smin", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_UMIN         : SM_Pseudo_Atomics <"s_buffer_atomic_umin", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_SMAX         : SM_Pseudo_Atomics <"s_buffer_atomic_smax", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_UMAX         : SM_Pseudo_Atomics <"s_buffer_atomic_umax", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_AND          : SM_Pseudo_Atomics <"s_buffer_atomic_and", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_OR           : SM_Pseudo_Atomics <"s_buffer_atomic_or", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_XOR          : SM_Pseudo_Atomics <"s_buffer_atomic_xor", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_INC          : SM_Pseudo_Atomics <"s_buffer_atomic_inc", SReg_128, SReg_32_XM0_XEXEC>;
defm S_BUFFER_ATOMIC_DEC          : SM_Pseudo_Atomics <"s_buffer_atomic_dec", SReg_128, SReg_32_XM0_XEXEC>;

defm S_BUFFER_ATOMIC_SWAP_X2      : SM_Pseudo_Atomics <"s_buffer_atomic_swap_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_CMPSWAP_X2   : SM_Pseudo_Atomics <"s_buffer_atomic_cmpswap_x2", SReg_128, SReg_128>;
defm S_BUFFER_ATOMIC_ADD_X2       : SM_Pseudo_Atomics <"s_buffer_atomic_add_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_SUB_X2       : SM_Pseudo_Atomics <"s_buffer_atomic_sub_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_SMIN_X2      : SM_Pseudo_Atomics <"s_buffer_atomic_smin_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_UMIN_X2      : SM_Pseudo_Atomics <"s_buffer_atomic_umin_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_SMAX_X2      : SM_Pseudo_Atomics <"s_buffer_atomic_smax_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_UMAX_X2      : SM_Pseudo_Atomics <"s_buffer_atomic_umax_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_AND_X2       : SM_Pseudo_Atomics <"s_buffer_atomic_and_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_OR_X2        : SM_Pseudo_Atomics <"s_buffer_atomic_or_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_XOR_X2       : SM_Pseudo_Atomics <"s_buffer_atomic_xor_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_INC_X2       : SM_Pseudo_Atomics <"s_buffer_atomic_inc_x2", SReg_128, SReg_64_XEXEC>;
defm S_BUFFER_ATOMIC_DEC_X2       : SM_Pseudo_Atomics <"s_buffer_atomic_dec_x2", SReg_128, SReg_64_XEXEC>;

defm S_ATOMIC_SWAP                : SM_Pseudo_Atomics <"s_atomic_swap", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_CMPSWAP             : SM_Pseudo_Atomics <"s_atomic_cmpswap", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_ADD                 : SM_Pseudo_Atomics <"s_atomic_add", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_SUB                 : SM_Pseudo_Atomics <"s_atomic_sub", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_SMIN                : SM_Pseudo_Atomics <"s_atomic_smin", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_UMIN                : SM_Pseudo_Atomics <"s_atomic_umin", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_SMAX                : SM_Pseudo_Atomics <"s_atomic_smax", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_UMAX                : SM_Pseudo_Atomics <"s_atomic_umax", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_AND                 : SM_Pseudo_Atomics <"s_atomic_and", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_OR                  : SM_Pseudo_Atomics <"s_atomic_or", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_XOR                 : SM_Pseudo_Atomics <"s_atomic_xor", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_INC                 : SM_Pseudo_Atomics <"s_atomic_inc", SReg_64, SReg_32_XM0_XEXEC>;
defm S_ATOMIC_DEC                 : SM_Pseudo_Atomics <"s_atomic_dec", SReg_64, SReg_32_XM0_XEXEC>;

defm S_ATOMIC_SWAP_X2             : SM_Pseudo_Atomics <"s_atomic_swap_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_CMPSWAP_X2          : SM_Pseudo_Atomics <"s_atomic_cmpswap_x2", SReg_64, SReg_128>;
defm S_ATOMIC_ADD_X2              : SM_Pseudo_Atomics <"s_atomic_add_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_SUB_X2              : SM_Pseudo_Atomics <"s_atomic_sub_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_SMIN_X2             : SM_Pseudo_Atomics <"s_atomic_smin_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_UMIN_X2             : SM_Pseudo_Atomics <"s_atomic_umin_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_SMAX_X2             : SM_Pseudo_Atomics <"s_atomic_smax_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_UMAX_X2             : SM_Pseudo_Atomics <"s_atomic_umax_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_AND_X2              : SM_Pseudo_Atomics <"s_atomic_and_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_OR_X2               : SM_Pseudo_Atomics <"s_atomic_or_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_XOR_X2              : SM_Pseudo_Atomics <"s_atomic_xor_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_INC_X2              : SM_Pseudo_Atomics <"s_atomic_inc_x2", SReg_64, SReg_64_XEXEC>;
defm S_ATOMIC_DEC_X2              : SM_Pseudo_Atomics <"s_atomic_dec_x2", SReg_64, SReg_64_XEXEC>;

} // let SubtargetPredicate = HasScalarAtomics

let SubtargetPredicate = isGFX9 in {
defm S_DCACHE_DISCARD    : SM_Pseudo_Discards <"s_dcache_discard">;
defm S_DCACHE_DISCARD_X2 : SM_Pseudo_Discards <"s_dcache_discard_x2">;
}

//===----------------------------------------------------------------------===//
// Targets
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SI
//===----------------------------------------------------------------------===//

class SMRD_Real_si <bits<5> op, SM_Pseudo ps>
  : SM_Real<ps>
  , SIMCInstr<ps.PseudoInstr, SIEncodingFamily.SI>
  , Enc32 {

  let AssemblerPredicates = [isSICI];
  let DecoderNamespace = "SICI";

  let Inst{7-0}   = !if(ps.has_offset, offset{7-0}, ?);
  let Inst{8}     = imm;
  let Inst{14-9}  = !if(ps.has_sbase, sbase{6-1}, ?);
  let Inst{21-15} = !if(ps.has_sdst, sdst{6-0}, ?);
  let Inst{26-22} = op;
  let Inst{31-27} = 0x18; //encoding
}

// FIXME: Assembler should reject trying to use glc on SMRD
// instructions on SI.
multiclass SM_Real_Loads_si<bits<5> op, string ps,
                            SM_Load_Pseudo immPs = !cast<SM_Load_Pseudo>(ps#_IMM),
                            SM_Load_Pseudo sgprPs = !cast<SM_Load_Pseudo>(ps#_SGPR)> {

  def _IMM_si : SMRD_Real_si <op, immPs> {
    let InOperandList = (ins immPs.BaseClass:$sbase, smrd_offset_8:$offset, GLC:$glc);
  }

  // FIXME: The operand name $offset is inconsistent with $soff used
  // in the pseudo
  def _SGPR_si : SMRD_Real_si <op, sgprPs> {
    let InOperandList = (ins sgprPs.BaseClass:$sbase, SReg_32:$offset, GLC:$glc);
  }

}

defm S_LOAD_DWORD           : SM_Real_Loads_si <0x00, "S_LOAD_DWORD">;
defm S_LOAD_DWORDX2         : SM_Real_Loads_si <0x01, "S_LOAD_DWORDX2">;
defm S_LOAD_DWORDX4         : SM_Real_Loads_si <0x02, "S_LOAD_DWORDX4">;
defm S_LOAD_DWORDX8         : SM_Real_Loads_si <0x03, "S_LOAD_DWORDX8">;
defm S_LOAD_DWORDX16        : SM_Real_Loads_si <0x04, "S_LOAD_DWORDX16">;
defm S_BUFFER_LOAD_DWORD    : SM_Real_Loads_si <0x08, "S_BUFFER_LOAD_DWORD">;
defm S_BUFFER_LOAD_DWORDX2  : SM_Real_Loads_si <0x09, "S_BUFFER_LOAD_DWORDX2">;
defm S_BUFFER_LOAD_DWORDX4  : SM_Real_Loads_si <0x0a, "S_BUFFER_LOAD_DWORDX4">;
defm S_BUFFER_LOAD_DWORDX8  : SM_Real_Loads_si <0x0b, "S_BUFFER_LOAD_DWORDX8">;
defm S_BUFFER_LOAD_DWORDX16 : SM_Real_Loads_si <0x0c, "S_BUFFER_LOAD_DWORDX16">;

def S_MEMTIME_si    : SMRD_Real_si <0x1e, S_MEMTIME>;
def S_DCACHE_INV_si : SMRD_Real_si <0x1f, S_DCACHE_INV>;


//===----------------------------------------------------------------------===//
// VI
//===----------------------------------------------------------------------===//

class SMEM_Real_vi <bits<8> op, SM_Pseudo ps>
  : SM_Real<ps>
  , SIMCInstr<ps.PseudoInstr, SIEncodingFamily.VI>
  , Enc64 {
  bit glc;

  let AssemblerPredicates = [isVI];
  let DecoderNamespace = "VI";

  let Inst{5-0}   = !if(ps.has_sbase, sbase{6-1}, ?);
  let Inst{12-6}  = !if(ps.has_sdst, sdst{6-0}, ?);

  let Inst{16} = !if(ps.has_glc, glc, ?);
  let Inst{17} = imm;
  let Inst{25-18} = op;
  let Inst{31-26} = 0x30; //encoding
  let Inst{51-32} = !if(ps.has_offset, offset{19-0}, ?);
}

multiclass SM_Real_Loads_vi<bits<8> op, string ps,
                            SM_Load_Pseudo immPs = !cast<SM_Load_Pseudo>(ps#_IMM),
                            SM_Load_Pseudo sgprPs = !cast<SM_Load_Pseudo>(ps#_SGPR)> {
  def _IMM_vi : SMEM_Real_vi <op, immPs> {
    let InOperandList = (ins immPs.BaseClass:$sbase, smrd_offset_20:$offset, GLC:$glc);
  }
  def _SGPR_vi : SMEM_Real_vi <op, sgprPs> {
    let InOperandList = (ins sgprPs.BaseClass:$sbase, SReg_32:$offset, GLC:$glc);
  }
}

class SMEM_Real_Store_vi <bits<8> op, SM_Pseudo ps> : SMEM_Real_vi <op, ps> {
  // encoding
  bits<7> sdata;

  let sdst = ?;
  let Inst{12-6}  = !if(ps.has_sdst, sdata{6-0}, ?);
}

multiclass SM_Real_Stores_vi<bits<8> op, string ps,
                            SM_Store_Pseudo immPs = !cast<SM_Store_Pseudo>(ps#_IMM),
                            SM_Store_Pseudo sgprPs = !cast<SM_Store_Pseudo>(ps#_SGPR)> {
  // FIXME: The operand name $offset is inconsistent with $soff used
  // in the pseudo
  def _IMM_vi : SMEM_Real_Store_vi <op, immPs> {
    let InOperandList = (ins immPs.SrcClass:$sdata, immPs.BaseClass:$sbase, smrd_offset_20:$offset, GLC:$glc);
  }

  def _SGPR_vi : SMEM_Real_Store_vi <op, sgprPs> {
    let InOperandList = (ins sgprPs.SrcClass:$sdata, sgprPs.BaseClass:$sbase, SReg_32:$offset, GLC:$glc);
  }
}

multiclass SM_Real_Probe_vi<bits<8> op, string ps> {
  def _IMM_vi  : SMEM_Real_Store_vi <op, !cast<SM_Probe_Pseudo>(ps#_IMM)>;
  def _SGPR_vi : SMEM_Real_Store_vi <op, !cast<SM_Probe_Pseudo>(ps#_SGPR)>;
}

defm S_LOAD_DWORD           : SM_Real_Loads_vi <0x00, "S_LOAD_DWORD">;
defm S_LOAD_DWORDX2         : SM_Real_Loads_vi <0x01, "S_LOAD_DWORDX2">;
defm S_LOAD_DWORDX4         : SM_Real_Loads_vi <0x02, "S_LOAD_DWORDX4">;
defm S_LOAD_DWORDX8         : SM_Real_Loads_vi <0x03, "S_LOAD_DWORDX8">;
defm S_LOAD_DWORDX16        : SM_Real_Loads_vi <0x04, "S_LOAD_DWORDX16">;
defm S_BUFFER_LOAD_DWORD    : SM_Real_Loads_vi <0x08, "S_BUFFER_LOAD_DWORD">;
defm S_BUFFER_LOAD_DWORDX2  : SM_Real_Loads_vi <0x09, "S_BUFFER_LOAD_DWORDX2">;
defm S_BUFFER_LOAD_DWORDX4  : SM_Real_Loads_vi <0x0a, "S_BUFFER_LOAD_DWORDX4">;
defm S_BUFFER_LOAD_DWORDX8  : SM_Real_Loads_vi <0x0b, "S_BUFFER_LOAD_DWORDX8">;
defm S_BUFFER_LOAD_DWORDX16 : SM_Real_Loads_vi <0x0c, "S_BUFFER_LOAD_DWORDX16">;

defm S_STORE_DWORD : SM_Real_Stores_vi <0x10, "S_STORE_DWORD">;
defm S_STORE_DWORDX2 : SM_Real_Stores_vi <0x11, "S_STORE_DWORDX2">;
defm S_STORE_DWORDX4 : SM_Real_Stores_vi <0x12, "S_STORE_DWORDX4">;

defm S_BUFFER_STORE_DWORD    : SM_Real_Stores_vi <0x18, "S_BUFFER_STORE_DWORD">;
defm S_BUFFER_STORE_DWORDX2  : SM_Real_Stores_vi <0x19, "S_BUFFER_STORE_DWORDX2">;
defm S_BUFFER_STORE_DWORDX4  : SM_Real_Stores_vi <0x1a, "S_BUFFER_STORE_DWORDX4">;

// These instructions use same encoding
def S_DCACHE_INV_vi         : SMEM_Real_vi <0x20, S_DCACHE_INV>;
def S_DCACHE_WB_vi          : SMEM_Real_vi <0x21, S_DCACHE_WB>;
def S_DCACHE_INV_VOL_vi     : SMEM_Real_vi <0x22, S_DCACHE_INV_VOL>;
def S_DCACHE_WB_VOL_vi      : SMEM_Real_vi <0x23, S_DCACHE_WB_VOL>;
def S_MEMTIME_vi            : SMEM_Real_vi <0x24, S_MEMTIME>;
def S_MEMREALTIME_vi        : SMEM_Real_vi <0x25, S_MEMREALTIME>;

defm S_SCRATCH_LOAD_DWORD    : SM_Real_Loads_vi <0x05, "S_SCRATCH_LOAD_DWORD">;
defm S_SCRATCH_LOAD_DWORDX2  : SM_Real_Loads_vi <0x06, "S_SCRATCH_LOAD_DWORDX2">;
defm S_SCRATCH_LOAD_DWORDX4  : SM_Real_Loads_vi <0x07, "S_SCRATCH_LOAD_DWORDX4">;

defm S_SCRATCH_STORE_DWORD   : SM_Real_Stores_vi <0x15, "S_SCRATCH_STORE_DWORD">;
defm S_SCRATCH_STORE_DWORDX2 : SM_Real_Stores_vi <0x16, "S_SCRATCH_STORE_DWORDX2">;
defm S_SCRATCH_STORE_DWORDX4 : SM_Real_Stores_vi <0x17, "S_SCRATCH_STORE_DWORDX4">;

defm S_ATC_PROBE        : SM_Real_Probe_vi <0x26, "S_ATC_PROBE">;
defm S_ATC_PROBE_BUFFER : SM_Real_Probe_vi <0x27, "S_ATC_PROBE_BUFFER">;

//===----------------------------------------------------------------------===//
// GFX9
//===----------------------------------------------------------------------===//

class SMEM_Atomic_Real_vi <bits<8> op, SM_Atomic_Pseudo ps>
  : SMEM_Real_vi <op, ps> {

  bits<7> sdata;

  let Constraints = ps.Constraints;
  let DisableEncoding = ps.DisableEncoding;

  let glc = ps.glc;
  let Inst{12-6} = !if(glc, sdst{6-0}, sdata{6-0});
}

multiclass SM_Real_Atomics_vi<bits<8> op, string ps> {
  def _IMM_vi       : SMEM_Atomic_Real_vi <op, !cast<SM_Atomic_Pseudo>(ps#_IMM)>;
  def _SGPR_vi      : SMEM_Atomic_Real_vi <op, !cast<SM_Atomic_Pseudo>(ps#_SGPR)>;
  def _IMM_RTN_vi   : SMEM_Atomic_Real_vi <op, !cast<SM_Atomic_Pseudo>(ps#_IMM_RTN)>;
  def _SGPR_RTN_vi  : SMEM_Atomic_Real_vi <op, !cast<SM_Atomic_Pseudo>(ps#_SGPR_RTN)>;
}

defm S_BUFFER_ATOMIC_SWAP         : SM_Real_Atomics_vi <0x40, "S_BUFFER_ATOMIC_SWAP">;
defm S_BUFFER_ATOMIC_CMPSWAP      : SM_Real_Atomics_vi <0x41, "S_BUFFER_ATOMIC_CMPSWAP">;
defm S_BUFFER_ATOMIC_ADD          : SM_Real_Atomics_vi <0x42, "S_BUFFER_ATOMIC_ADD">;
defm S_BUFFER_ATOMIC_SUB          : SM_Real_Atomics_vi <0x43, "S_BUFFER_ATOMIC_SUB">;
defm S_BUFFER_ATOMIC_SMIN         : SM_Real_Atomics_vi <0x44, "S_BUFFER_ATOMIC_SMIN">;
defm S_BUFFER_ATOMIC_UMIN         : SM_Real_Atomics_vi <0x45, "S_BUFFER_ATOMIC_UMIN">;
defm S_BUFFER_ATOMIC_SMAX         : SM_Real_Atomics_vi <0x46, "S_BUFFER_ATOMIC_SMAX">;
defm S_BUFFER_ATOMIC_UMAX         : SM_Real_Atomics_vi <0x47, "S_BUFFER_ATOMIC_UMAX">;
defm S_BUFFER_ATOMIC_AND          : SM_Real_Atomics_vi <0x48, "S_BUFFER_ATOMIC_AND">;
defm S_BUFFER_ATOMIC_OR           : SM_Real_Atomics_vi <0x49, "S_BUFFER_ATOMIC_OR">;
defm S_BUFFER_ATOMIC_XOR          : SM_Real_Atomics_vi <0x4a, "S_BUFFER_ATOMIC_XOR">;
defm S_BUFFER_ATOMIC_INC          : SM_Real_Atomics_vi <0x4b, "S_BUFFER_ATOMIC_INC">;
defm S_BUFFER_ATOMIC_DEC          : SM_Real_Atomics_vi <0x4c, "S_BUFFER_ATOMIC_DEC">;

defm S_BUFFER_ATOMIC_SWAP_X2      : SM_Real_Atomics_vi <0x60, "S_BUFFER_ATOMIC_SWAP_X2">;
defm S_BUFFER_ATOMIC_CMPSWAP_X2   : SM_Real_Atomics_vi <0x61, "S_BUFFER_ATOMIC_CMPSWAP_X2">;
defm S_BUFFER_ATOMIC_ADD_X2       : SM_Real_Atomics_vi <0x62, "S_BUFFER_ATOMIC_ADD_X2">;
defm S_BUFFER_ATOMIC_SUB_X2       : SM_Real_Atomics_vi <0x63, "S_BUFFER_ATOMIC_SUB_X2">;
defm S_BUFFER_ATOMIC_SMIN_X2      : SM_Real_Atomics_vi <0x64, "S_BUFFER_ATOMIC_SMIN_X2">;
defm S_BUFFER_ATOMIC_UMIN_X2      : SM_Real_Atomics_vi <0x65, "S_BUFFER_ATOMIC_UMIN_X2">;
defm S_BUFFER_ATOMIC_SMAX_X2      : SM_Real_Atomics_vi <0x66, "S_BUFFER_ATOMIC_SMAX_X2">;
defm S_BUFFER_ATOMIC_UMAX_X2      : SM_Real_Atomics_vi <0x67, "S_BUFFER_ATOMIC_UMAX_X2">;
defm S_BUFFER_ATOMIC_AND_X2       : SM_Real_Atomics_vi <0x68, "S_BUFFER_ATOMIC_AND_X2">;
defm S_BUFFER_ATOMIC_OR_X2        : SM_Real_Atomics_vi <0x69, "S_BUFFER_ATOMIC_OR_X2">;
defm S_BUFFER_ATOMIC_XOR_X2       : SM_Real_Atomics_vi <0x6a, "S_BUFFER_ATOMIC_XOR_X2">;
defm S_BUFFER_ATOMIC_INC_X2       : SM_Real_Atomics_vi <0x6b, "S_BUFFER_ATOMIC_INC_X2">;
defm S_BUFFER_ATOMIC_DEC_X2       : SM_Real_Atomics_vi <0x6c, "S_BUFFER_ATOMIC_DEC_X2">;

defm S_ATOMIC_SWAP                : SM_Real_Atomics_vi <0x80, "S_ATOMIC_SWAP">;
defm S_ATOMIC_CMPSWAP             : SM_Real_Atomics_vi <0x81, "S_ATOMIC_CMPSWAP">;
defm S_ATOMIC_ADD                 : SM_Real_Atomics_vi <0x82, "S_ATOMIC_ADD">;
defm S_ATOMIC_SUB                 : SM_Real_Atomics_vi <0x83, "S_ATOMIC_SUB">;
defm S_ATOMIC_SMIN                : SM_Real_Atomics_vi <0x84, "S_ATOMIC_SMIN">;
defm S_ATOMIC_UMIN                : SM_Real_Atomics_vi <0x85, "S_ATOMIC_UMIN">;
defm S_ATOMIC_SMAX                : SM_Real_Atomics_vi <0x86, "S_ATOMIC_SMAX">;
defm S_ATOMIC_UMAX                : SM_Real_Atomics_vi <0x87, "S_ATOMIC_UMAX">;
defm S_ATOMIC_AND                 : SM_Real_Atomics_vi <0x88, "S_ATOMIC_AND">;
defm S_ATOMIC_OR                  : SM_Real_Atomics_vi <0x89, "S_ATOMIC_OR">;
defm S_ATOMIC_XOR                 : SM_Real_Atomics_vi <0x8a, "S_ATOMIC_XOR">;
defm S_ATOMIC_INC                 : SM_Real_Atomics_vi <0x8b, "S_ATOMIC_INC">;
defm S_ATOMIC_DEC                 : SM_Real_Atomics_vi <0x8c, "S_ATOMIC_DEC">;

defm S_ATOMIC_SWAP_X2             : SM_Real_Atomics_vi <0xa0, "S_ATOMIC_SWAP_X2">;
defm S_ATOMIC_CMPSWAP_X2          : SM_Real_Atomics_vi <0xa1, "S_ATOMIC_CMPSWAP_X2">;
defm S_ATOMIC_ADD_X2              : SM_Real_Atomics_vi <0xa2, "S_ATOMIC_ADD_X2">;
defm S_ATOMIC_SUB_X2              : SM_Real_Atomics_vi <0xa3, "S_ATOMIC_SUB_X2">;
defm S_ATOMIC_SMIN_X2             : SM_Real_Atomics_vi <0xa4, "S_ATOMIC_SMIN_X2">;
defm S_ATOMIC_UMIN_X2             : SM_Real_Atomics_vi <0xa5, "S_ATOMIC_UMIN_X2">;
defm S_ATOMIC_SMAX_X2             : SM_Real_Atomics_vi <0xa6, "S_ATOMIC_SMAX_X2">;
defm S_ATOMIC_UMAX_X2             : SM_Real_Atomics_vi <0xa7, "S_ATOMIC_UMAX_X2">;
defm S_ATOMIC_AND_X2              : SM_Real_Atomics_vi <0xa8, "S_ATOMIC_AND_X2">;
defm S_ATOMIC_OR_X2               : SM_Real_Atomics_vi <0xa9, "S_ATOMIC_OR_X2">;
defm S_ATOMIC_XOR_X2              : SM_Real_Atomics_vi <0xaa, "S_ATOMIC_XOR_X2">;
defm S_ATOMIC_INC_X2              : SM_Real_Atomics_vi <0xab, "S_ATOMIC_INC_X2">;
defm S_ATOMIC_DEC_X2              : SM_Real_Atomics_vi <0xac, "S_ATOMIC_DEC_X2">;

multiclass SM_Real_Discard_vi<bits<8> op, string ps> {
  def _IMM_vi  : SMEM_Real_vi <op, !cast<SM_Discard_Pseudo>(ps#_IMM)>;
  def _SGPR_vi : SMEM_Real_vi <op, !cast<SM_Discard_Pseudo>(ps#_SGPR)>;
}

defm S_DCACHE_DISCARD    : SM_Real_Discard_vi <0x28, "S_DCACHE_DISCARD">;
defm S_DCACHE_DISCARD_X2 : SM_Real_Discard_vi <0x29, "S_DCACHE_DISCARD_X2">;

//===----------------------------------------------------------------------===//
// CI
//===----------------------------------------------------------------------===//

def smrd_literal_offset : NamedOperandU32<"SMRDLiteralOffset",
                                          NamedMatchClass<"SMRDLiteralOffset">> {
  let OperandType = "OPERAND_IMMEDIATE";
}

class SMRD_Real_Load_IMM_ci <bits<5> op, SM_Load_Pseudo ps> :
  SM_Real<ps>,
  Enc64 {

  let AssemblerPredicates = [isCIOnly];
  let DecoderNamespace = "CI";
  let InOperandList = (ins ps.BaseClass:$sbase, smrd_literal_offset:$offset, GLC:$glc);

  let LGKM_CNT = ps.LGKM_CNT;
  let SMRD = ps.SMRD;
  let mayLoad = ps.mayLoad;
  let mayStore = ps.mayStore;
  let hasSideEffects = ps.hasSideEffects;
  let SchedRW = ps.SchedRW;
  let UseNamedOperandTable = ps.UseNamedOperandTable;

  let Inst{7-0}   = 0xff;
  let Inst{8}     = 0;
  let Inst{14-9}  = sbase{6-1};
  let Inst{21-15} = sdst{6-0};
  let Inst{26-22} = op;
  let Inst{31-27} = 0x18; //encoding
  let Inst{63-32} = offset{31-0};
}

def S_LOAD_DWORD_IMM_ci           : SMRD_Real_Load_IMM_ci <0x00, S_LOAD_DWORD_IMM>;
def S_LOAD_DWORDX2_IMM_ci         : SMRD_Real_Load_IMM_ci <0x01, S_LOAD_DWORDX2_IMM>;
def S_LOAD_DWORDX4_IMM_ci         : SMRD_Real_Load_IMM_ci <0x02, S_LOAD_DWORDX4_IMM>;
def S_LOAD_DWORDX8_IMM_ci         : SMRD_Real_Load_IMM_ci <0x03, S_LOAD_DWORDX8_IMM>;
def S_LOAD_DWORDX16_IMM_ci        : SMRD_Real_Load_IMM_ci <0x04, S_LOAD_DWORDX16_IMM>;
def S_BUFFER_LOAD_DWORD_IMM_ci    : SMRD_Real_Load_IMM_ci <0x08, S_BUFFER_LOAD_DWORD_IMM>;
def S_BUFFER_LOAD_DWORDX2_IMM_ci  : SMRD_Real_Load_IMM_ci <0x09, S_BUFFER_LOAD_DWORDX2_IMM>;
def S_BUFFER_LOAD_DWORDX4_IMM_ci  : SMRD_Real_Load_IMM_ci <0x0a, S_BUFFER_LOAD_DWORDX4_IMM>;
def S_BUFFER_LOAD_DWORDX8_IMM_ci  : SMRD_Real_Load_IMM_ci <0x0b, S_BUFFER_LOAD_DWORDX8_IMM>;
def S_BUFFER_LOAD_DWORDX16_IMM_ci : SMRD_Real_Load_IMM_ci <0x0c, S_BUFFER_LOAD_DWORDX16_IMM>;

class SMRD_Real_ci <bits<5> op, SM_Pseudo ps>
  : SM_Real<ps>
  , SIMCInstr<ps.PseudoInstr, SIEncodingFamily.SI>
  , Enc32 {

  let AssemblerPredicates = [isCIOnly];
  let DecoderNamespace = "CI";

  let Inst{7-0}   = !if(ps.has_offset, offset{7-0}, ?);
  let Inst{8}     = imm;
  let Inst{14-9}  = !if(ps.has_sbase, sbase{6-1}, ?);
  let Inst{21-15} = !if(ps.has_sdst, sdst{6-0}, ?);
  let Inst{26-22} = op;
  let Inst{31-27} = 0x18; //encoding
}

def S_DCACHE_INV_VOL_ci : SMRD_Real_ci <0x1d, S_DCACHE_INV_VOL>;

//===----------------------------------------------------------------------===//
// Scalar Memory Patterns
//===----------------------------------------------------------------------===//

def smrd_load : PatFrag <(ops node:$ptr), (load node:$ptr), [{ return isUniformLoad(N);}]>;

def SMRDImm         : ComplexPattern<i64, 2, "SelectSMRDImm">;
def SMRDImm32       : ComplexPattern<i64, 2, "SelectSMRDImm32">;
def SMRDSgpr        : ComplexPattern<i64, 2, "SelectSMRDSgpr">;
def SMRDBufferImm   : ComplexPattern<i32, 1, "SelectSMRDBufferImm">;
def SMRDBufferImm32 : ComplexPattern<i32, 1, "SelectSMRDBufferImm32">;

multiclass SMRD_Pattern <string Instr, ValueType vt> {

  // 1. IMM offset
  def : GCNPat <
    (smrd_load (SMRDImm i64:$sbase, i32:$offset)),
    (vt (!cast<SM_Pseudo>(Instr#"_IMM") $sbase, $offset, 0))
  >;

  // 2. 32-bit IMM offset on CI
  def : GCNPat <
    (smrd_load (SMRDImm32 i64:$sbase, i32:$offset)),
    (vt (!cast<InstSI>(Instr#"_IMM_ci") $sbase, $offset, 0))> {
    let OtherPredicates = [isCIOnly];
  }

  // 3. SGPR offset
  def : GCNPat <
    (smrd_load (SMRDSgpr i64:$sbase, i32:$offset)),
    (vt (!cast<SM_Pseudo>(Instr#"_SGPR") $sbase, $offset, 0))
  >;
}

multiclass SMLoad_Pattern <string Instr, ValueType vt> {
  // 1. Offset as an immediate
  def : GCNPat <
    (SIsbuffer_load v4i32:$sbase, (SMRDBufferImm i32:$offset), i1:$glc),
    (vt (!cast<SM_Pseudo>(Instr#"_IMM") $sbase, $offset, (as_i1imm $glc)))
  >;

  // 2. 32-bit IMM offset on CI
  def : GCNPat <
    (vt (SIsbuffer_load v4i32:$sbase, (SMRDBufferImm32 i32:$offset), i1:$glc)),
    (!cast<InstSI>(Instr#"_IMM_ci") $sbase, $offset, (as_i1imm $glc))> {
    let OtherPredicates = [isCIOnly];
  }

  // 3. Offset loaded in an 32bit SGPR
  def : GCNPat <
    (SIsbuffer_load v4i32:$sbase, i32:$offset, i1:$glc),
    (vt (!cast<SM_Pseudo>(Instr#"_SGPR") $sbase, $offset, (as_i1imm $glc)))
  >;
}

// Global and constant loads can be selected to either MUBUF or SMRD
// instructions, but SMRD instructions are faster so we want the instruction
// selector to prefer those.
let AddedComplexity = 100 in {

defm : SMRD_Pattern <"S_LOAD_DWORD",    i32>;
defm : SMRD_Pattern <"S_LOAD_DWORDX2",  v2i32>;
defm : SMRD_Pattern <"S_LOAD_DWORDX4",  v4i32>;
defm : SMRD_Pattern <"S_LOAD_DWORDX8",  v8i32>;
defm : SMRD_Pattern <"S_LOAD_DWORDX16", v16i32>;

defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORD",     i32>;
defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORDX2",   v2i32>;
defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORDX4",   v4i32>;
defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORDX8",   v8i32>;
defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORDX16",  v16i32>;

defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORD",     f32>;
defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORDX2",   v2f32>;
defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORDX4",   v4f32>;
defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORDX8",   v8f32>;
defm : SMLoad_Pattern <"S_BUFFER_LOAD_DWORDX16",  v16f32>;
} // End let AddedComplexity = 100

let OtherPredicates = [isSICI] in {
def : GCNPat <
  (i64 (readcyclecounter)),
  (S_MEMTIME)
>;
}

let OtherPredicates = [isVI] in {

def : GCNPat <
  (i64 (readcyclecounter)),
  (S_MEMREALTIME)
>;

} // let OtherPredicates = [isVI]
