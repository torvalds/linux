//===-- R600InstrFormats.td - R600 Instruction Encodings ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// R600 Instruction format definitions.
//
//===----------------------------------------------------------------------===//

def isR600 : Predicate<"Subtarget->getGeneration() <= AMDGPUSubtarget::R700">;

def isR600toCayman : Predicate<
    "Subtarget->getGeneration() <= AMDGPUSubtarget::NORTHERN_ISLANDS">;

class R600Pat<dag pattern, dag result> : AMDGPUPat<pattern, result> {
  let SubtargetPredicate = isR600toCayman;
}

class InstR600 <dag outs, dag ins, string asm, list<dag> pattern,
                InstrItinClass itin = NoItinerary>
    : AMDGPUInst <outs, ins, asm, pattern>, PredicateControl {

  field bits<64> Inst;
  bit Trig = 0;
  bit Op3 = 0;
  bit isVector = 0;
  bits<2> FlagOperandIdx = 0;
  bit Op1 = 0;
  bit Op2 = 0;
  bit LDS_1A = 0;
  bit LDS_1A1D = 0;
  bit HasNativeOperands = 0;
  bit VTXInst = 0;
  bit TEXInst = 0;
  bit ALUInst = 0;
  bit IsExport = 0;
  bit LDS_1A2D = 0;

  let SubtargetPredicate = isR600toCayman;
  let Namespace = "R600";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asm;
  let Pattern = pattern;
  let Itinerary = itin;

  // No AsmMatcher support.
  let isCodeGenOnly = 1;

  let TSFlags{4} = Trig;
  let TSFlags{5} = Op3;

  // Vector instructions are instructions that must fill all slots in an
  // instruction group
  let TSFlags{6} = isVector;
  let TSFlags{8-7} = FlagOperandIdx;
  let TSFlags{9} = HasNativeOperands;
  let TSFlags{10} = Op1;
  let TSFlags{11} = Op2;
  let TSFlags{12} = VTXInst;
  let TSFlags{13} = TEXInst;
  let TSFlags{14} = ALUInst;
  let TSFlags{15} = LDS_1A;
  let TSFlags{16} = LDS_1A1D;
  let TSFlags{17} = IsExport;
  let TSFlags{18} = LDS_1A2D;
}

//===----------------------------------------------------------------------===//
// ALU instructions
//===----------------------------------------------------------------------===//

class R600_ALU_LDS_Word0 {
  field bits<32> Word0;

  bits<11> src0;
  bits<1>  src0_rel;
  bits<11> src1;
  bits<1>  src1_rel;
  bits<3>  index_mode = 0;
  bits<2>  pred_sel;
  bits<1>  last;

  bits<9>  src0_sel  = src0{8-0};
  bits<2>  src0_chan = src0{10-9};
  bits<9>  src1_sel  = src1{8-0};
  bits<2>  src1_chan = src1{10-9};

  let Word0{8-0}   = src0_sel;
  let Word0{9}     = src0_rel;
  let Word0{11-10} = src0_chan;
  let Word0{21-13} = src1_sel;
  let Word0{22}    = src1_rel;
  let Word0{24-23} = src1_chan;
  let Word0{28-26} = index_mode;
  let Word0{30-29} = pred_sel;
  let Word0{31}    = last;
}

class R600ALU_Word0 : R600_ALU_LDS_Word0 {

  bits<1>  src0_neg;
  bits<1>  src1_neg;

  let Word0{12}    = src0_neg;
  let Word0{25}    = src1_neg;
}

class R600ALU_Word1 {
  field bits<32> Word1;

  bits<11> dst;
  bits<3>  bank_swizzle;
  bits<1>  dst_rel;
  bits<1>  clamp;

  bits<7>  dst_sel  = dst{6-0};
  bits<2>  dst_chan = dst{10-9};

  let Word1{20-18} = bank_swizzle;
  let Word1{27-21} = dst_sel;
  let Word1{28}    = dst_rel;
  let Word1{30-29} = dst_chan;
  let Word1{31}    = clamp;
}

class R600ALU_Word1_OP2 <bits<11> alu_inst> : R600ALU_Word1{

  bits<1>  src0_abs;
  bits<1>  src1_abs;
  bits<1>  update_exec_mask;
  bits<1>  update_pred;
  bits<1>  write;
  bits<2>  omod;

  let Word1{0}     = src0_abs;
  let Word1{1}     = src1_abs;
  let Word1{2}     = update_exec_mask;
  let Word1{3}     = update_pred;
  let Word1{4}     = write;
  let Word1{6-5}   = omod;
  let Word1{17-7}  = alu_inst;
}

class R600ALU_Word1_OP3 <bits<5> alu_inst> : R600ALU_Word1{

  bits<11> src2;
  bits<1>  src2_rel;
  bits<1>  src2_neg;

  bits<9>  src2_sel = src2{8-0};
  bits<2>  src2_chan = src2{10-9};

  let Word1{8-0}   = src2_sel;
  let Word1{9}     = src2_rel;
  let Word1{11-10} = src2_chan;
  let Word1{12}    = src2_neg;
  let Word1{17-13} = alu_inst;
}

class R600LDS_Word1 {
  field bits<32> Word1;

  bits<11> src2;
  bits<9>  src2_sel  = src2{8-0};
  bits<2>  src2_chan = src2{10-9};
  bits<1>  src2_rel;
  // offset specifies the stride offset to the second set of data to be read
  // from.  This is a dword offset.
  bits<5>  alu_inst = 17; // OP3_INST_LDS_IDX_OP
  bits<3>  bank_swizzle;
  bits<6>  lds_op;
  bits<2>  dst_chan = 0;

  let Word1{8-0}   = src2_sel;
  let Word1{9}     = src2_rel;
  let Word1{11-10} = src2_chan;
  let Word1{17-13} = alu_inst;
  let Word1{20-18} = bank_swizzle;
  let Word1{26-21} = lds_op;
  let Word1{30-29} = dst_chan;
}


/*
XXX: R600 subtarget uses a slightly different encoding than the other
subtargets.  We currently handle this in R600MCCodeEmitter, but we may
want to use these instruction classes in the future.

class R600ALU_Word1_OP2_r600 : R600ALU_Word1_OP2 {

  bits<1>  fog_merge;
  bits<10> alu_inst;

  let Inst{37}    = fog_merge;
  let Inst{39-38} = omod;
  let Inst{49-40} = alu_inst;
}

class R600ALU_Word1_OP2_r700 : R600ALU_Word1_OP2 {

  bits<11> alu_inst;

  let Inst{38-37} = omod;
  let Inst{49-39} = alu_inst;
}
*/

//===----------------------------------------------------------------------===//
// Vertex Fetch instructions
//===----------------------------------------------------------------------===//

class VTX_WORD0 {
  field bits<32> Word0;
  bits<7> src_gpr;
  bits<5> VC_INST;
  bits<2> FETCH_TYPE;
  bits<1> FETCH_WHOLE_QUAD;
  bits<8> buffer_id;
  bits<1> SRC_REL;
  bits<2> SRC_SEL_X;

  let Word0{4-0}   = VC_INST;
  let Word0{6-5}   = FETCH_TYPE;
  let Word0{7}     = FETCH_WHOLE_QUAD;
  let Word0{15-8}  = buffer_id;
  let Word0{22-16} = src_gpr;
  let Word0{23}    = SRC_REL;
  let Word0{25-24} = SRC_SEL_X;
}

class VTX_WORD0_eg : VTX_WORD0 {

  bits<6> MEGA_FETCH_COUNT;

  let Word0{31-26} = MEGA_FETCH_COUNT;
}

class VTX_WORD0_cm : VTX_WORD0 {

  bits<2> SRC_SEL_Y;
  bits<2> STRUCTURED_READ;
  bits<1> LDS_REQ;
  bits<1> COALESCED_READ;

  let Word0{27-26} = SRC_SEL_Y;
  let Word0{29-28} = STRUCTURED_READ;
  let Word0{30}    = LDS_REQ;
  let Word0{31}    = COALESCED_READ;
}

class VTX_WORD1_GPR {
  field bits<32> Word1;
  bits<7> dst_gpr;
  bits<1> DST_REL;
  bits<3> DST_SEL_X;
  bits<3> DST_SEL_Y;
  bits<3> DST_SEL_Z;
  bits<3> DST_SEL_W;
  bits<1> USE_CONST_FIELDS;
  bits<6> DATA_FORMAT;
  bits<2> NUM_FORMAT_ALL;
  bits<1> FORMAT_COMP_ALL;
  bits<1> SRF_MODE_ALL;

  let Word1{6-0} = dst_gpr;
  let Word1{7}    = DST_REL;
  let Word1{8}    = 0; // Reserved
  let Word1{11-9} = DST_SEL_X;
  let Word1{14-12} = DST_SEL_Y;
  let Word1{17-15} = DST_SEL_Z;
  let Word1{20-18} = DST_SEL_W;
  let Word1{21}    = USE_CONST_FIELDS;
  let Word1{27-22} = DATA_FORMAT;
  let Word1{29-28} = NUM_FORMAT_ALL;
  let Word1{30}    = FORMAT_COMP_ALL;
  let Word1{31}    = SRF_MODE_ALL;
}

//===----------------------------------------------------------------------===//
// Texture fetch instructions
//===----------------------------------------------------------------------===//

class TEX_WORD0 {
  field bits<32> Word0;

  bits<5> TEX_INST;
  bits<2> INST_MOD;
  bits<1> FETCH_WHOLE_QUAD;
  bits<8> RESOURCE_ID;
  bits<7> SRC_GPR;
  bits<1> SRC_REL;
  bits<1> ALT_CONST;
  bits<2> RESOURCE_INDEX_MODE;
  bits<2> SAMPLER_INDEX_MODE;

  let Word0{4-0} = TEX_INST;
  let Word0{6-5} = INST_MOD;
  let Word0{7} = FETCH_WHOLE_QUAD;
  let Word0{15-8} = RESOURCE_ID;
  let Word0{22-16} = SRC_GPR;
  let Word0{23} = SRC_REL;
  let Word0{24} = ALT_CONST;
  let Word0{26-25} = RESOURCE_INDEX_MODE;
  let Word0{28-27} = SAMPLER_INDEX_MODE;
}

class TEX_WORD1 {
  field bits<32> Word1;

  bits<7> DST_GPR;
  bits<1> DST_REL;
  bits<3> DST_SEL_X;
  bits<3> DST_SEL_Y;
  bits<3> DST_SEL_Z;
  bits<3> DST_SEL_W;
  bits<7> LOD_BIAS;
  bits<1> COORD_TYPE_X;
  bits<1> COORD_TYPE_Y;
  bits<1> COORD_TYPE_Z;
  bits<1> COORD_TYPE_W;

  let Word1{6-0} = DST_GPR;
  let Word1{7} = DST_REL;
  let Word1{11-9} = DST_SEL_X;
  let Word1{14-12} = DST_SEL_Y;
  let Word1{17-15} = DST_SEL_Z;
  let Word1{20-18} = DST_SEL_W;
  let Word1{27-21} = LOD_BIAS;
  let Word1{28} = COORD_TYPE_X;
  let Word1{29} = COORD_TYPE_Y;
  let Word1{30} = COORD_TYPE_Z;
  let Word1{31} = COORD_TYPE_W;
}

class TEX_WORD2 {
  field bits<32> Word2;

  bits<5> OFFSET_X;
  bits<5> OFFSET_Y;
  bits<5> OFFSET_Z;
  bits<5> SAMPLER_ID;
  bits<3> SRC_SEL_X;
  bits<3> SRC_SEL_Y;
  bits<3> SRC_SEL_Z;
  bits<3> SRC_SEL_W;

  let Word2{4-0} = OFFSET_X;
  let Word2{9-5} = OFFSET_Y;
  let Word2{14-10} = OFFSET_Z;
  let Word2{19-15} = SAMPLER_ID;
  let Word2{22-20} = SRC_SEL_X;
  let Word2{25-23} = SRC_SEL_Y;
  let Word2{28-26} = SRC_SEL_Z;
  let Word2{31-29} = SRC_SEL_W;
}

//===----------------------------------------------------------------------===//
// Control Flow Instructions
//===----------------------------------------------------------------------===//

class CF_WORD1_R600 {
  field bits<32> Word1;

  bits<3> POP_COUNT;
  bits<5> CF_CONST;
  bits<2> COND;
  bits<3> COUNT;
  bits<6> CALL_COUNT;
  bits<1> COUNT_3;
  bits<1> END_OF_PROGRAM;
  bits<1> VALID_PIXEL_MODE;
  bits<7> CF_INST;
  bits<1> WHOLE_QUAD_MODE;
  bits<1> BARRIER;

  let Word1{2-0} = POP_COUNT;
  let Word1{7-3} = CF_CONST;
  let Word1{9-8} = COND;
  let Word1{12-10} = COUNT;
  let Word1{18-13} = CALL_COUNT;
  let Word1{19} = COUNT_3;
  let Word1{21} = END_OF_PROGRAM;
  let Word1{22} = VALID_PIXEL_MODE;
  let Word1{29-23} = CF_INST;
  let Word1{30} = WHOLE_QUAD_MODE;
  let Word1{31} = BARRIER;
}

class CF_WORD0_EG {
  field bits<32> Word0;

  bits<24> ADDR;
  bits<3> JUMPTABLE_SEL;

  let Word0{23-0} = ADDR;
  let Word0{26-24} = JUMPTABLE_SEL;
}

class CF_WORD1_EG {
  field bits<32> Word1;

  bits<3> POP_COUNT;
  bits<5> CF_CONST;
  bits<2> COND;
  bits<6> COUNT;
  bits<1> VALID_PIXEL_MODE;
  bits<1> END_OF_PROGRAM;
  bits<8> CF_INST;
  bits<1> BARRIER;

  let Word1{2-0} = POP_COUNT;
  let Word1{7-3} = CF_CONST;
  let Word1{9-8} = COND;
  let Word1{15-10} = COUNT;
  let Word1{20} = VALID_PIXEL_MODE;
  let Word1{21} = END_OF_PROGRAM;
  let Word1{29-22} = CF_INST;
  let Word1{31} = BARRIER;
}

class CF_ALU_WORD0 {
  field bits<32> Word0;

  bits<22> ADDR;
  bits<4> KCACHE_BANK0;
  bits<4> KCACHE_BANK1;
  bits<2> KCACHE_MODE0;

  let Word0{21-0} = ADDR;
  let Word0{25-22} = KCACHE_BANK0;
  let Word0{29-26} = KCACHE_BANK1;
  let Word0{31-30} = KCACHE_MODE0;
}

class CF_ALU_WORD1 {
  field bits<32> Word1;

  bits<2> KCACHE_MODE1;
  bits<8> KCACHE_ADDR0;
  bits<8> KCACHE_ADDR1;
  bits<7> COUNT;
  bits<1> ALT_CONST;
  bits<4> CF_INST;
  bits<1> WHOLE_QUAD_MODE;
  bits<1> BARRIER;

  let Word1{1-0} = KCACHE_MODE1;
  let Word1{9-2} = KCACHE_ADDR0;
  let Word1{17-10} = KCACHE_ADDR1;
  let Word1{24-18} = COUNT;
  let Word1{25} = ALT_CONST;
  let Word1{29-26} = CF_INST;
  let Word1{30} = WHOLE_QUAD_MODE;
  let Word1{31} = BARRIER;
}

class CF_ALLOC_EXPORT_WORD0_RAT {
  field bits<32> Word0;

  bits<4> rat_id;
  bits<6> rat_inst;
  bits<2> rim;
  bits<2> type;
  bits<7> rw_gpr;
  bits<1> rw_rel;
  bits<7> index_gpr;
  bits<2> elem_size;

  let Word0{3-0}   = rat_id;
  let Word0{9-4}   = rat_inst;
  let Word0{10}    = 0; // Reserved
  let Word0{12-11} = rim;
  let Word0{14-13} = type;
  let Word0{21-15} = rw_gpr;
  let Word0{22}    = rw_rel;
  let Word0{29-23} = index_gpr;
  let Word0{31-30} = elem_size;
}

class CF_ALLOC_EXPORT_WORD1_BUF {
  field bits<32> Word1;

  bits<12> array_size;
  bits<4>  comp_mask;
  bits<4>  burst_count;
  bits<1>  vpm;
  bits<1>  eop;
  bits<8>  cf_inst;
  bits<1>  mark;
  bits<1>  barrier;

  let Word1{11-0} = array_size;
  let Word1{15-12} = comp_mask;
  let Word1{19-16} = burst_count;
  let Word1{20}    = vpm;
  let Word1{21}    = eop;
  let Word1{29-22} = cf_inst;
  let Word1{30}    = mark;
  let Word1{31}    = barrier;
}
