//===-- SparcInstr64Bit.td - 64-bit instructions for Sparc Target ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains instruction definitions and patterns needed for 64-bit
// code generation on SPARC v9.
//
// Some SPARC v9 instructions are defined in SparcInstrInfo.td because they can
// also be used in 32-bit code running on a SPARC v9 CPU.
//
//===----------------------------------------------------------------------===//

let Predicates = [Is64Bit] in {
// The same integer registers are used for i32 and i64 values.
// When registers hold i32 values, the high bits are don't care.
// This give us free trunc and anyext.
def : Pat<(i64 (anyext i32:$val)), (COPY_TO_REGCLASS $val, I64Regs)>;
def : Pat<(i32 (trunc i64:$val)), (COPY_TO_REGCLASS $val, IntRegs)>;

} // Predicates = [Is64Bit]


//===----------------------------------------------------------------------===//
// 64-bit Shift Instructions.
//===----------------------------------------------------------------------===//
//
// The 32-bit shift instructions are still available. The left shift srl
// instructions shift all 64 bits, but it only accepts a 5-bit shift amount.
//
// The srl instructions only shift the low 32 bits and clear the high 32 bits.
// Finally, sra shifts the low 32 bits and sign-extends to 64 bits.

let Predicates = [Is64Bit] in {

def : Pat<(i64 (zext i32:$val)), (SRLri $val, 0)>;
def : Pat<(i64 (sext i32:$val)), (SRAri $val, 0)>;

def : Pat<(i64 (and i64:$val, 0xffffffff)), (SRLri $val, 0)>;
def : Pat<(i64 (sext_inreg i64:$val, i32)), (SRAri $val, 0)>;

defm SLLX : F3_S<"sllx", 0b100101, 1, shl, i64, I64Regs>;
defm SRLX : F3_S<"srlx", 0b100110, 1, srl, i64, I64Regs>;
defm SRAX : F3_S<"srax", 0b100111, 1, sra, i64, I64Regs>;

} // Predicates = [Is64Bit]


//===----------------------------------------------------------------------===//
// 64-bit Immediates.
//===----------------------------------------------------------------------===//
//
// All 32-bit immediates can be materialized with sethi+or, but 64-bit
// immediates may require more code. There may be a point where it is
// preferable to use a constant pool load instead, depending on the
// microarchitecture.

// Single-instruction patterns.

// The ALU instructions want their simm13 operands as i32 immediates.
def as_i32imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getSExtValue(), SDLoc(N), MVT::i32);
}]>;
def : Pat<(i64 simm13:$val), (ORri (i64 G0), (as_i32imm $val))>;
def : Pat<(i64 SETHIimm:$val), (SETHIi (HI22 $val))>;

// Double-instruction patterns.

// All unsigned i32 immediates can be handled by sethi+or.
def uimm32 : PatLeaf<(imm), [{ return isUInt<32>(N->getZExtValue()); }]>;
def : Pat<(i64 uimm32:$val), (ORri (SETHIi (HI22 $val)), (LO10 $val))>,
      Requires<[Is64Bit]>;

// All negative i33 immediates can be handled by sethi+xor.
def nimm33 : PatLeaf<(imm), [{
  int64_t Imm = N->getSExtValue();
  return Imm < 0 && isInt<33>(Imm);
}]>;
// Bits 10-31 inverted. Same as assembler's %hix.
def HIX22 : SDNodeXForm<imm, [{
  uint64_t Val = (~N->getZExtValue() >> 10) & ((1u << 22) - 1);
  return CurDAG->getTargetConstant(Val, SDLoc(N), MVT::i32);
}]>;
// Bits 0-9 with ones in bits 10-31. Same as assembler's %lox.
def LOX10 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(~(~N->getZExtValue() & 0x3ff), SDLoc(N),
                                   MVT::i32);
}]>;
def : Pat<(i64 nimm33:$val), (XORri (SETHIi (HIX22 $val)), (LOX10 $val))>,
      Requires<[Is64Bit]>;

// More possible patterns:
//
//   (sllx sethi, n)
//   (sllx simm13, n)
//
// 3 instrs:
//
//   (xor (sllx sethi), simm13)
//   (sllx (xor sethi, simm13))
//
// 4 instrs:
//
//   (or sethi, (sllx sethi))
//   (xnor sethi, (sllx sethi))
//
// 5 instrs:
//
//   (or (sllx sethi), (or sethi, simm13))
//   (xnor (sllx sethi), (or sethi, simm13))
//   (or (sllx sethi), (sllx sethi))
//   (xnor (sllx sethi), (sllx sethi))
//
// Worst case is 6 instrs:
//
//   (or (sllx (or sethi, simmm13)), (or sethi, simm13))

// Bits 42-63, same as assembler's %hh.
def HH22 : SDNodeXForm<imm, [{
  uint64_t Val = (N->getZExtValue() >> 42) & ((1u << 22) - 1);
  return CurDAG->getTargetConstant(Val, SDLoc(N), MVT::i32);
}]>;
// Bits 32-41, same as assembler's %hm.
def HM10 : SDNodeXForm<imm, [{
  uint64_t Val = (N->getZExtValue() >> 32) & ((1u << 10) - 1);
  return CurDAG->getTargetConstant(Val, SDLoc(N), MVT::i32);
}]>;
def : Pat<(i64 imm:$val),
          (ORrr (SLLXri (ORri (SETHIi (HH22 $val)), (HM10 $val)), (i32 32)),
                (ORri (SETHIi (HI22 $val)), (LO10 $val)))>,
      Requires<[Is64Bit]>;


//===----------------------------------------------------------------------===//
// 64-bit Integer Arithmetic and Logic.
//===----------------------------------------------------------------------===//

let Predicates = [Is64Bit] in {

// Register-register instructions.
let isCodeGenOnly = 1 in {
defm ANDX    : F3_12<"and", 0b000001, and, I64Regs, i64, i64imm>;
defm ORX     : F3_12<"or",  0b000010, or,  I64Regs, i64, i64imm>;
defm XORX    : F3_12<"xor", 0b000011, xor, I64Regs, i64, i64imm>;

def ANDXNrr  : F3_1<2, 0b000101,
                 (outs I64Regs:$dst), (ins I64Regs:$b, I64Regs:$c),
                 "andn $b, $c, $dst",
                 [(set i64:$dst, (and i64:$b, (not i64:$c)))]>;
def ORXNrr   : F3_1<2, 0b000110,
                 (outs I64Regs:$dst), (ins I64Regs:$b, I64Regs:$c),
                 "orn $b, $c, $dst",
                 [(set i64:$dst, (or i64:$b, (not i64:$c)))]>;
def XNORXrr  : F3_1<2, 0b000111,
                   (outs I64Regs:$dst), (ins I64Regs:$b, I64Regs:$c),
                   "xnor $b, $c, $dst",
                   [(set i64:$dst, (not (xor i64:$b, i64:$c)))]>;

defm ADDX    : F3_12<"add", 0b000000, add, I64Regs, i64, i64imm>;
defm SUBX    : F3_12<"sub", 0b000100, sub, I64Regs, i64, i64imm>;

def TLS_ADDXrr : F3_1<2, 0b000000, (outs I64Regs:$rd),
                   (ins I64Regs:$rs1, I64Regs:$rs2, TLSSym:$sym),
                   "add $rs1, $rs2, $rd, $sym",
                   [(set i64:$rd,
                       (tlsadd i64:$rs1, i64:$rs2, tglobaltlsaddr:$sym))]>;

// "LEA" form of add
def LEAX_ADDri : F3_2<2, 0b000000,
                     (outs I64Regs:$dst), (ins MEMri:$addr),
                     "add ${addr:arith}, $dst",
                     [(set iPTR:$dst, ADDRri:$addr)]>;
}

def : Pat<(SPcmpicc i64:$a, i64:$b), (CMPrr $a, $b)>;
def : Pat<(SPcmpicc i64:$a, (i64 simm13:$b)), (CMPri $a, (as_i32imm $b))>;
def : Pat<(ctpop i64:$src), (POPCrr $src)>;

} // Predicates = [Is64Bit]


//===----------------------------------------------------------------------===//
// 64-bit Integer Multiply and Divide.
//===----------------------------------------------------------------------===//

let Predicates = [Is64Bit] in {

def MULXrr : F3_1<2, 0b001001,
                  (outs I64Regs:$rd), (ins I64Regs:$rs1, I64Regs:$rs2),
                  "mulx $rs1, $rs2, $rd",
                  [(set i64:$rd, (mul i64:$rs1, i64:$rs2))]>;
def MULXri : F3_2<2, 0b001001,
                  (outs IntRegs:$rd), (ins IntRegs:$rs1, i64imm:$simm13),
                  "mulx $rs1, $simm13, $rd",
                  [(set i64:$rd, (mul i64:$rs1, (i64 simm13:$simm13)))]>;

// Division can trap.
let hasSideEffects = 1 in {
def SDIVXrr : F3_1<2, 0b101101,
                   (outs I64Regs:$rd), (ins I64Regs:$rs1, I64Regs:$rs2),
                   "sdivx $rs1, $rs2, $rd",
                   [(set i64:$rd, (sdiv i64:$rs1, i64:$rs2))]>;
def SDIVXri : F3_2<2, 0b101101,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, i64imm:$simm13),
                   "sdivx $rs1, $simm13, $rd",
                   [(set i64:$rd, (sdiv i64:$rs1, (i64 simm13:$simm13)))]>;

def UDIVXrr : F3_1<2, 0b001101,
                   (outs I64Regs:$rd), (ins I64Regs:$rs1, I64Regs:$rs2),
                   "udivx $rs1, $rs2, $rd",
                   [(set i64:$rd, (udiv i64:$rs1, i64:$rs2))]>;
def UDIVXri : F3_2<2, 0b001101,
                   (outs IntRegs:$rd), (ins IntRegs:$rs1, i64imm:$simm13),
                   "udivx $rs1, $simm13, $rd",
                   [(set i64:$rd, (udiv i64:$rs1, (i64 simm13:$simm13)))]>;
} // hasSideEffects = 1

} // Predicates = [Is64Bit]


//===----------------------------------------------------------------------===//
// 64-bit Loads and Stores.
//===----------------------------------------------------------------------===//
//
// All the 32-bit loads and stores are available. The extending loads are sign
// or zero-extending to 64 bits. The LDrr and LDri instructions load 32 bits
// zero-extended to i64. Their mnemonic is lduw in SPARC v9 (Load Unsigned
// Word).
//
// SPARC v9 adds 64-bit loads as well as a sign-extending ldsw i32 loads.

let Predicates = [Is64Bit] in {

// 64-bit loads.
let DecoderMethod = "DecodeLoadInt" in
  defm LDX   : Load<"ldx", 0b001011, load, I64Regs, i64>;

let mayLoad = 1, isAsmParserOnly = 1 in
  def TLS_LDXrr : F3_1<3, 0b001011,
                       (outs IntRegs:$dst), (ins MEMrr:$addr, TLSSym:$sym),
                       "ldx [$addr], $dst, $sym",
                       [(set i64:$dst,
                           (tlsld ADDRrr:$addr, tglobaltlsaddr:$sym))]>;

// Extending loads to i64.
def : Pat<(i64 (zextloadi1 ADDRrr:$addr)), (LDUBrr ADDRrr:$addr)>;
def : Pat<(i64 (zextloadi1 ADDRri:$addr)), (LDUBri ADDRri:$addr)>;
def : Pat<(i64 (extloadi1 ADDRrr:$addr)), (LDUBrr ADDRrr:$addr)>;
def : Pat<(i64 (extloadi1 ADDRri:$addr)), (LDUBri ADDRri:$addr)>;

def : Pat<(i64 (zextloadi8 ADDRrr:$addr)), (LDUBrr ADDRrr:$addr)>;
def : Pat<(i64 (zextloadi8 ADDRri:$addr)), (LDUBri ADDRri:$addr)>;
def : Pat<(i64 (extloadi8 ADDRrr:$addr)),  (LDUBrr ADDRrr:$addr)>;
def : Pat<(i64 (extloadi8 ADDRri:$addr)),  (LDUBri ADDRri:$addr)>;
def : Pat<(i64 (sextloadi8 ADDRrr:$addr)), (LDSBrr ADDRrr:$addr)>;
def : Pat<(i64 (sextloadi8 ADDRri:$addr)), (LDSBri ADDRri:$addr)>;

def : Pat<(i64 (zextloadi16 ADDRrr:$addr)), (LDUHrr ADDRrr:$addr)>;
def : Pat<(i64 (zextloadi16 ADDRri:$addr)), (LDUHri ADDRri:$addr)>;
def : Pat<(i64 (extloadi16 ADDRrr:$addr)),  (LDUHrr ADDRrr:$addr)>;
def : Pat<(i64 (extloadi16 ADDRri:$addr)),  (LDUHri ADDRri:$addr)>;
def : Pat<(i64 (sextloadi16 ADDRrr:$addr)), (LDSHrr ADDRrr:$addr)>;
def : Pat<(i64 (sextloadi16 ADDRri:$addr)), (LDSHri ADDRri:$addr)>;

def : Pat<(i64 (zextloadi32 ADDRrr:$addr)), (LDrr ADDRrr:$addr)>;
def : Pat<(i64 (zextloadi32 ADDRri:$addr)), (LDri ADDRri:$addr)>;
def : Pat<(i64 (extloadi32 ADDRrr:$addr)),  (LDrr ADDRrr:$addr)>;
def : Pat<(i64 (extloadi32 ADDRri:$addr)),  (LDri ADDRri:$addr)>;

// Sign-extending load of i32 into i64 is a new SPARC v9 instruction.
let DecoderMethod = "DecodeLoadInt" in
  defm LDSW   : Load<"ldsw", 0b001000, sextloadi32, I64Regs, i64>;

// 64-bit stores.
let DecoderMethod = "DecodeStoreInt" in
  defm STX    : Store<"stx", 0b001110, store,  I64Regs, i64>;

// Truncating stores from i64 are identical to the i32 stores.
def : Pat<(truncstorei8  i64:$src, ADDRrr:$addr), (STBrr ADDRrr:$addr, $src)>;
def : Pat<(truncstorei8  i64:$src, ADDRri:$addr), (STBri ADDRri:$addr, $src)>;
def : Pat<(truncstorei16 i64:$src, ADDRrr:$addr), (STHrr ADDRrr:$addr, $src)>;
def : Pat<(truncstorei16 i64:$src, ADDRri:$addr), (STHri ADDRri:$addr, $src)>;
def : Pat<(truncstorei32 i64:$src, ADDRrr:$addr), (STrr  ADDRrr:$addr, $src)>;
def : Pat<(truncstorei32 i64:$src, ADDRri:$addr), (STri  ADDRri:$addr, $src)>;

// store 0, addr -> store %g0, addr
def : Pat<(store (i64 0), ADDRrr:$dst), (STXrr ADDRrr:$dst, (i64 G0))>;
def : Pat<(store (i64 0), ADDRri:$dst), (STXri ADDRri:$dst, (i64 G0))>;

} // Predicates = [Is64Bit]


//===----------------------------------------------------------------------===//
// 64-bit Conditionals.
//===----------------------------------------------------------------------===//

//
// Flag-setting instructions like subcc and addcc set both icc and xcc flags.
// The icc flags correspond to the 32-bit result, and the xcc are for the
// full 64-bit result.
//
// We reuse CMPICC SDNodes for compares, but use new BRXCC branch nodes for
// 64-bit compares. See LowerBR_CC.

let Predicates = [Is64Bit] in {

let Uses = [ICC], cc = 0b10 in
  defm BPX : IPredBranch<"%xcc", [(SPbrxcc bb:$imm19, imm:$cond)]>;

// Conditional moves on %xcc.
let Uses = [ICC], Constraints = "$f = $rd" in {
let intcc = 1, cc = 0b10 in {
def MOVXCCrr : F4_1<0b101100, (outs IntRegs:$rd),
                      (ins IntRegs:$rs2, IntRegs:$f, CCOp:$cond),
                      "mov$cond %xcc, $rs2, $rd",
                      [(set i32:$rd,
                       (SPselectxcc i32:$rs2, i32:$f, imm:$cond))]>;
def MOVXCCri : F4_2<0b101100, (outs IntRegs:$rd),
                      (ins i32imm:$simm11, IntRegs:$f, CCOp:$cond),
                      "mov$cond %xcc, $simm11, $rd",
                      [(set i32:$rd,
                       (SPselectxcc simm11:$simm11, i32:$f, imm:$cond))]>;
} // cc

let intcc = 1, opf_cc = 0b10 in {
def FMOVS_XCC : F4_3<0b110101, 0b000001, (outs FPRegs:$rd),
                      (ins FPRegs:$rs2, FPRegs:$f, CCOp:$cond),
                      "fmovs$cond %xcc, $rs2, $rd",
                      [(set f32:$rd,
                       (SPselectxcc f32:$rs2, f32:$f, imm:$cond))]>;
def FMOVD_XCC : F4_3<0b110101, 0b000010, (outs DFPRegs:$rd),
                      (ins DFPRegs:$rs2, DFPRegs:$f, CCOp:$cond),
                      "fmovd$cond %xcc, $rs2, $rd",
                      [(set f64:$rd,
                       (SPselectxcc f64:$rs2, f64:$f, imm:$cond))]>;
def FMOVQ_XCC : F4_3<0b110101, 0b000011, (outs QFPRegs:$rd),
                      (ins QFPRegs:$rs2, QFPRegs:$f, CCOp:$cond),
                      "fmovq$cond %xcc, $rs2, $rd",
                      [(set f128:$rd,
                       (SPselectxcc f128:$rs2, f128:$f, imm:$cond))]>;
} // opf_cc
} // Uses, Constraints

// Branch On integer register with Prediction (BPr).
let isBranch = 1, isTerminator = 1, hasDelaySlot = 1 in
multiclass BranchOnReg<bits<3> cond, string OpcStr> {
  def napt : F2_4<cond, 0, 1, (outs), (ins I64Regs:$rs1, bprtarget16:$imm16),
             !strconcat(OpcStr, " $rs1, $imm16"), []>;
  def apt  : F2_4<cond, 1, 1, (outs), (ins I64Regs:$rs1, bprtarget16:$imm16),
             !strconcat(OpcStr, ",a $rs1, $imm16"), []>;
  def napn  : F2_4<cond, 0, 0, (outs), (ins I64Regs:$rs1, bprtarget16:$imm16),
             !strconcat(OpcStr, ",pn $rs1, $imm16"), []>;
  def apn : F2_4<cond, 1, 0, (outs), (ins I64Regs:$rs1, bprtarget16:$imm16),
             !strconcat(OpcStr, ",a,pn $rs1, $imm16"), []>;
}

multiclass bpr_alias<string OpcStr, Instruction NAPT, Instruction APT> {
  def : InstAlias<!strconcat(OpcStr, ",pt $rs1, $imm16"),
                  (NAPT I64Regs:$rs1, bprtarget16:$imm16), 0>;
  def : InstAlias<!strconcat(OpcStr, ",a,pt $rs1, $imm16"),
                  (APT I64Regs:$rs1, bprtarget16:$imm16), 0>;
}

defm BPZ   : BranchOnReg<0b001, "brz">;
defm BPLEZ : BranchOnReg<0b010, "brlez">;
defm BPLZ  : BranchOnReg<0b011, "brlz">;
defm BPNZ  : BranchOnReg<0b101, "brnz">;
defm BPGZ  : BranchOnReg<0b110, "brgz">;
defm BPGEZ : BranchOnReg<0b111, "brgez">;

defm : bpr_alias<"brz",   BPZnapt,   BPZapt  >;
defm : bpr_alias<"brlez", BPLEZnapt, BPLEZapt>;
defm : bpr_alias<"brlz",  BPLZnapt,  BPLZapt >;
defm : bpr_alias<"brnz",  BPNZnapt,  BPNZapt >;
defm : bpr_alias<"brgz",  BPGZnapt,  BPGZapt >;
defm : bpr_alias<"brgez", BPGEZnapt, BPGEZapt>;

// Move integer register on register condition (MOVr).
multiclass MOVR< bits<3> rcond,  string OpcStr> {
  def rr : F4_4r<0b101111, 0b00000, rcond, (outs I64Regs:$rd),
                   (ins I64Regs:$rs1, IntRegs:$rs2),
                   !strconcat(OpcStr, " $rs1, $rs2, $rd"), []>;

  def ri : F4_4i<0b101111, rcond, (outs I64Regs:$rd),
                   (ins I64Regs:$rs1, i64imm:$simm10),
                   !strconcat(OpcStr, " $rs1, $simm10, $rd"), []>;
}

defm MOVRRZ  : MOVR<0b001, "movrz">;
defm MOVRLEZ : MOVR<0b010, "movrlez">;
defm MOVRLZ  : MOVR<0b011, "movrlz">;
defm MOVRNZ  : MOVR<0b101, "movrnz">;
defm MOVRGZ  : MOVR<0b110, "movrgz">;
defm MOVRGEZ : MOVR<0b111, "movrgez">;

// Move FP register on integer register condition (FMOVr).
multiclass FMOVR<bits<3> rcond, string OpcStr> {

  def S : F4_4r<0b110101, 0b00101, rcond,
                (outs FPRegs:$rd), (ins I64Regs:$rs1, FPRegs:$rs2),
                !strconcat(!strconcat("fmovrs", OpcStr)," $rs1, $rs2, $rd"),
                []>;
  def D : F4_4r<0b110101, 0b00110, rcond,
                (outs FPRegs:$rd), (ins I64Regs:$rs1, FPRegs:$rs2),
                !strconcat(!strconcat("fmovrd", OpcStr)," $rs1, $rs2, $rd"),
                []>;
  def Q : F4_4r<0b110101, 0b00111, rcond,
                (outs FPRegs:$rd), (ins I64Regs:$rs1, FPRegs:$rs2),
                !strconcat(!strconcat("fmovrq", OpcStr)," $rs1, $rs2, $rd"),
                []>, Requires<[HasHardQuad]>;
}

let Predicates = [HasV9] in {
  defm FMOVRZ   : FMOVR<0b001, "z">;
  defm FMOVRLEZ : FMOVR<0b010, "lez">;
  defm FMOVRLZ  : FMOVR<0b011, "lz">;
  defm FMOVRNZ  : FMOVR<0b101, "nz">;
  defm FMOVRGZ  : FMOVR<0b110, "gz">;
  defm FMOVRGEZ : FMOVR<0b111, "gez">;
}

//===----------------------------------------------------------------------===//
// 64-bit Floating Point Conversions.
//===----------------------------------------------------------------------===//

let Predicates = [Is64Bit] in {

def FXTOS : F3_3u<2, 0b110100, 0b010000100,
                 (outs FPRegs:$rd), (ins DFPRegs:$rs2),
                 "fxtos $rs2, $rd",
                 [(set FPRegs:$rd, (SPxtof DFPRegs:$rs2))]>;
def FXTOD : F3_3u<2, 0b110100, 0b010001000,
                 (outs DFPRegs:$rd), (ins DFPRegs:$rs2),
                 "fxtod $rs2, $rd",
                 [(set DFPRegs:$rd, (SPxtof DFPRegs:$rs2))]>;
def FXTOQ : F3_3u<2, 0b110100, 0b010001100,
                 (outs QFPRegs:$rd), (ins DFPRegs:$rs2),
                 "fxtoq $rs2, $rd",
                 [(set QFPRegs:$rd, (SPxtof DFPRegs:$rs2))]>,
                 Requires<[HasHardQuad]>;

def FSTOX : F3_3u<2, 0b110100, 0b010000001,
                 (outs DFPRegs:$rd), (ins FPRegs:$rs2),
                 "fstox $rs2, $rd",
                 [(set DFPRegs:$rd, (SPftox FPRegs:$rs2))]>;
def FDTOX : F3_3u<2, 0b110100, 0b010000010,
                 (outs DFPRegs:$rd), (ins DFPRegs:$rs2),
                 "fdtox $rs2, $rd",
                 [(set DFPRegs:$rd, (SPftox DFPRegs:$rs2))]>;
def FQTOX : F3_3u<2, 0b110100, 0b010000011,
                 (outs DFPRegs:$rd), (ins QFPRegs:$rs2),
                 "fqtox $rs2, $rd",
                 [(set DFPRegs:$rd, (SPftox QFPRegs:$rs2))]>,
                 Requires<[HasHardQuad]>;

} // Predicates = [Is64Bit]

def : Pat<(SPselectxcc i64:$t, i64:$f, imm:$cond),
          (MOVXCCrr $t, $f, imm:$cond)>;
def : Pat<(SPselectxcc (i64 simm11:$t), i64:$f, imm:$cond),
          (MOVXCCri (as_i32imm $t), $f, imm:$cond)>;

def : Pat<(SPselecticc i64:$t, i64:$f, imm:$cond),
          (MOVICCrr $t, $f, imm:$cond)>;
def : Pat<(SPselecticc (i64 simm11:$t), i64:$f, imm:$cond),
          (MOVICCri (as_i32imm $t), $f, imm:$cond)>;

def : Pat<(SPselectfcc i64:$t, i64:$f, imm:$cond),
          (MOVFCCrr $t, $f, imm:$cond)>;
def : Pat<(SPselectfcc (i64 simm11:$t), i64:$f, imm:$cond),
          (MOVFCCri (as_i32imm $t), $f, imm:$cond)>;

} // Predicates = [Is64Bit]


// 64 bit SETHI
let Predicates = [Is64Bit], isCodeGenOnly = 1 in {
def SETHIXi : F2_1<0b100,
                   (outs IntRegs:$rd), (ins i64imm:$imm22),
                   "sethi $imm22, $rd",
                   [(set i64:$rd, SETHIimm:$imm22)]>;
}

// ATOMICS.
let Predicates = [Is64Bit], Constraints = "$swap = $rd", asi = 0b10000000 in {
  def CASXrr: F3_1_asi<3, 0b111110,
                (outs I64Regs:$rd), (ins I64Regs:$rs1, I64Regs:$rs2,
                                     I64Regs:$swap),
                 "casx [$rs1], $rs2, $rd",
                 [(set i64:$rd,
                     (atomic_cmp_swap_64 i64:$rs1, i64:$rs2, i64:$swap))]>;

} // Predicates = [Is64Bit], Constraints = ...

let Predicates = [Is64Bit] in {

def : Pat<(atomic_fence imm, imm), (MEMBARi 0xf)>;

// atomic_load_64 addr -> load addr
def : Pat<(i64 (atomic_load_64 ADDRrr:$src)), (LDXrr ADDRrr:$src)>;
def : Pat<(i64 (atomic_load_64 ADDRri:$src)), (LDXri ADDRri:$src)>;

// atomic_store_64 val, addr -> store val, addr
def : Pat<(atomic_store_64 ADDRrr:$dst, i64:$val), (STXrr ADDRrr:$dst, $val)>;
def : Pat<(atomic_store_64 ADDRri:$dst, i64:$val), (STXri ADDRri:$dst, $val)>;

} // Predicates = [Is64Bit]

let Predicates = [Is64Bit], hasSideEffects = 1, Uses = [ICC], cc = 0b10 in
 defm TXCC : TRAP<"%xcc">;

// Global addresses, constant pool entries
let Predicates = [Is64Bit] in {

def : Pat<(SPhi tglobaladdr:$in), (SETHIi tglobaladdr:$in)>;
def : Pat<(SPlo tglobaladdr:$in), (ORXri (i64 G0), tglobaladdr:$in)>;
def : Pat<(SPhi tconstpool:$in), (SETHIi tconstpool:$in)>;
def : Pat<(SPlo tconstpool:$in), (ORXri (i64 G0), tconstpool:$in)>;

// GlobalTLS addresses
def : Pat<(SPhi tglobaltlsaddr:$in), (SETHIi tglobaltlsaddr:$in)>;
def : Pat<(SPlo tglobaltlsaddr:$in), (ORXri (i64 G0), tglobaltlsaddr:$in)>;
def : Pat<(add (SPhi tglobaltlsaddr:$in1), (SPlo tglobaltlsaddr:$in2)),
          (ADDXri (SETHIXi tglobaltlsaddr:$in1), (tglobaltlsaddr:$in2))>;
def : Pat<(xor (SPhi tglobaltlsaddr:$in1), (SPlo tglobaltlsaddr:$in2)),
          (XORXri  (SETHIXi tglobaltlsaddr:$in1), (tglobaltlsaddr:$in2))>;

// Blockaddress
def : Pat<(SPhi tblockaddress:$in), (SETHIi tblockaddress:$in)>;
def : Pat<(SPlo tblockaddress:$in), (ORXri (i64 G0), tblockaddress:$in)>;

// Add reg, lo.  This is used when taking the addr of a global/constpool entry.
def : Pat<(add iPTR:$r, (SPlo tglobaladdr:$in)), (ADDXri $r, tglobaladdr:$in)>;
def : Pat<(add iPTR:$r, (SPlo tconstpool:$in)),  (ADDXri $r, tconstpool:$in)>;
def : Pat<(add iPTR:$r, (SPlo tblockaddress:$in)),
                        (ADDXri $r, tblockaddress:$in)>;
}
