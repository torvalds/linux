//===- MipsEVAInstrInfo.td - EVA ASE instructions -*- tablegen ------------*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes Mips EVA ASE instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// Instruction encodings
//
//===----------------------------------------------------------------------===//

// Memory Load/Store EVA encodings
class LBE_ENC     : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_LBE>;
class LBuE_ENC    : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_LBuE>;
class LHE_ENC     : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_LHE>;
class LHuE_ENC    : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_LHuE>;
class LWE_ENC     : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_LWE>;

class SBE_ENC     : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_SBE>;
class SHE_ENC     : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_SHE>;
class SWE_ENC     : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_SWE>;

// load/store left/right EVA encodings
class LWLE_ENC    : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_LWLE>;
class LWRE_ENC    : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_LWRE>;
class SWLE_ENC    : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_SWLE>;
class SWRE_ENC    : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_SWRE>;

// Load-linked EVA, Store-conditional EVA encodings
class LLE_ENC     : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_LLE>;
class SCE_ENC     : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_SCE>;

class TLBINV_ENC  : TLB_FM<OPCODE6_TLBINV>;
class TLBINVF_ENC : TLB_FM<OPCODE6_TLBINVF>;

class CACHEE_ENC  : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_CACHEE>;
class PREFE_ENC   : SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6_PREFE>;

//===----------------------------------------------------------------------===//
//
// Instruction descriptions
//
//===----------------------------------------------------------------------===//

// Memory Load/Store EVA descriptions
class LOAD_EVA_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                         InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins mem_simm9:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  string DecoderMethod = "DecodeMemEVA";
  bit canFoldAsLoad = 1;
  string BaseOpcode = instr_asm;
  bit mayLoad = 1;
  InstrItinClass Itinerary = itin;
}

class LBE_DESC  : LOAD_EVA_DESC_BASE<"lbe",  GPR32Opnd, II_LBE>;
class LBuE_DESC : LOAD_EVA_DESC_BASE<"lbue", GPR32Opnd, II_LBUE>;
class LHE_DESC  : LOAD_EVA_DESC_BASE<"lhe",  GPR32Opnd, II_LHE>;
class LHuE_DESC : LOAD_EVA_DESC_BASE<"lhue", GPR32Opnd, II_LHUE>;
class LWE_DESC  : LOAD_EVA_DESC_BASE<"lwe",  GPR32Opnd, II_LWE>;

class STORE_EVA_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                          SDPatternOperator OpNode = null_frag,
                          InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins GPROpnd:$rt, mem_simm9:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  string DecoderMethod = "DecodeMemEVA";
  string BaseOpcode = instr_asm;
  bit mayStore = 1;
  InstrItinClass Itinerary = itin;
}

class SBE_DESC  : STORE_EVA_DESC_BASE<"sbe",  GPR32Opnd, null_frag, II_SBE>;
class SHE_DESC  : STORE_EVA_DESC_BASE<"she",  GPR32Opnd, null_frag, II_SHE>;
class SWE_DESC  : STORE_EVA_DESC_BASE<"swe",  GPR32Opnd, null_frag, II_SWE>;

// Load/Store Left/Right EVA descriptions
class LOAD_LEFT_RIGHT_EVA_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                                    InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins mem_simm9:$addr, GPROpnd:$src);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  string DecoderMethod = "DecodeMemEVA";
  string BaseOpcode = instr_asm;
  string Constraints = "$src = $rt";
  bit canFoldAsLoad = 1;
  InstrItinClass Itinerary = itin;
  bit mayLoad = 1;
  bit mayStore = 0;
}

class LWLE_DESC : LOAD_LEFT_RIGHT_EVA_DESC_BASE<"lwle", GPR32Opnd, II_LWLE>;
class LWRE_DESC : LOAD_LEFT_RIGHT_EVA_DESC_BASE<"lwre", GPR32Opnd, II_LWRE>;

class STORE_LEFT_RIGHT_EVA_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                                     InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins GPROpnd:$rt, mem_simm9:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  string DecoderMethod = "DecodeMemEVA";
  string BaseOpcode = instr_asm;
  InstrItinClass Itinerary = itin;
  bit mayLoad = 0;
  bit mayStore = 1;
}

class SWLE_DESC : STORE_LEFT_RIGHT_EVA_DESC_BASE<"swle", GPR32Opnd, II_SWLE>;
class SWRE_DESC : STORE_LEFT_RIGHT_EVA_DESC_BASE<"swre", GPR32Opnd, II_SWRE>;

// Load-linked EVA, Store-conditional EVA descriptions
class LLE_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                    InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins mem_simm9:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  string BaseOpcode = instr_asm;
  bit mayLoad = 1;
  string DecoderMethod = "DecodeMemEVA";
  InstrItinClass Itinerary = itin;
}

class LLE_DESC : LLE_DESC_BASE<"lle", GPR32Opnd, II_LLE>;

class SCE_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                    InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs GPROpnd:$dst);
  dag InOperandList = (ins GPROpnd:$rt, mem_simm9:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  string BaseOpcode = instr_asm;
  bit mayStore = 1;
  string Constraints = "$rt = $dst";
  string DecoderMethod = "DecodeMemEVA";
  InstrItinClass Itinerary = itin;
}

class SCE_DESC : SCE_DESC_BASE<"sce", GPR32Opnd, II_SCE>;

class TLB_DESC_BASE<string instr_asm, InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins);
  string AsmString = instr_asm;
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class TLBINV_DESC  : TLB_DESC_BASE<"tlbinv", II_TLBINV>;
class TLBINVF_DESC : TLB_DESC_BASE<"tlbinvf", II_TLBINVF>;

class CACHEE_DESC_BASE<string instr_asm, Operand MemOpnd,
                       InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins  MemOpnd:$addr, uimm5:$hint);
  string AsmString = !strconcat(instr_asm, "\t$hint, $addr");
  list<dag> Pattern = [];
  string BaseOpcode = instr_asm;
  string DecoderMethod = "DecodeCacheeOp_CacheOpR6";
  InstrItinClass Itinerary = itin;
}

class CACHEE_DESC  : CACHEE_DESC_BASE<"cachee", mem_simm9, II_CACHEE>;
class PREFE_DESC   : CACHEE_DESC_BASE<"prefe", mem_simm9, II_PREFE>;

//===----------------------------------------------------------------------===//
//
// Instruction definitions
//
//===----------------------------------------------------------------------===//

let AdditionalPredicates = [NotInMicroMips] in {
  /// Load and Store EVA Instructions
  def LBE     : MMRel, LBE_ENC, LBE_DESC, ISA_MIPS32R2, ASE_EVA;
  def LBuE    : MMRel, LBuE_ENC, LBuE_DESC, ISA_MIPS32R2, ASE_EVA;
  def LHE     : MMRel, LHE_ENC, LHE_DESC, ISA_MIPS32R2, ASE_EVA;
  def LHuE    : MMRel, LHuE_ENC, LHuE_DESC, ISA_MIPS32R2, ASE_EVA;
  def LWE     : MMRel, LWE_ENC, LWE_DESC, ISA_MIPS32R2, ASE_EVA;
  def SBE     : MMRel, SBE_ENC, SBE_DESC, ISA_MIPS32R2, ASE_EVA;
  def SHE     : MMRel, SHE_ENC, SHE_DESC, ISA_MIPS32R2, ASE_EVA;
  def SWE     : MMRel, SWE_ENC, SWE_DESC, ISA_MIPS32R2, ASE_EVA;

  /// load/store left/right EVA
  def LWLE    : MMRel, LWLE_ENC, LWLE_DESC, ISA_MIPS32R2_NOT_32R6_64R6, ASE_EVA;
  def LWRE    : MMRel, LWRE_ENC, LWRE_DESC, ISA_MIPS32R2_NOT_32R6_64R6, ASE_EVA;
  def SWLE    : MMRel, SWLE_ENC, SWLE_DESC, ISA_MIPS32R2_NOT_32R6_64R6, ASE_EVA;
  def SWRE    : MMRel, SWRE_ENC, SWRE_DESC, ISA_MIPS32R2_NOT_32R6_64R6, ASE_EVA;

  /// Load-linked EVA, Store-conditional EVA
  def LLE     : MMRel, LLE_ENC, LLE_DESC, ISA_MIPS32R2, ASE_EVA;
  def SCE     : MMRel, SCE_ENC, SCE_DESC, ISA_MIPS32R2, ASE_EVA;

  /// TLB invalidate instructions
  def TLBINV  : TLBINV_ENC, TLBINV_DESC, ISA_MIPS32R2, ASE_EVA;
  def TLBINVF : TLBINVF_ENC, TLBINVF_DESC, ISA_MIPS32R2, ASE_EVA;

  /// EVA versions of cache and pref
  def CACHEE  : MMRel, CACHEE_ENC, CACHEE_DESC, ISA_MIPS32R2, ASE_EVA;
  def PREFE   : MMRel, PREFE_ENC, PREFE_DESC, ISA_MIPS32R2, ASE_EVA;
}
