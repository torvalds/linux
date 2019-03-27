//=-- SVEInstrFormats.td -  AArch64 SVE Instruction classes -*- tablegen -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AArch64 Scalable Vector Extension (SVE) Instruction Class Definitions.
//
//===----------------------------------------------------------------------===//

def SVEPatternOperand : AsmOperandClass {
  let Name = "SVEPattern";
  let ParserMethod = "tryParseSVEPattern";
  let PredicateMethod = "isSVEPattern";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidSVEPattern";
}

def sve_pred_enum : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) < 32);
  }]> {

  let PrintMethod = "printSVEPattern";
  let ParserMatchClass = SVEPatternOperand;
}

def SVEPrefetchOperand : AsmOperandClass {
  let Name = "SVEPrefetch";
  let ParserMethod = "tryParsePrefetch<true>";
  let PredicateMethod = "isPrefetch";
  let RenderMethod = "addPrefetchOperands";
}

def sve_prfop : Operand<i32>, ImmLeaf<i32, [{
    return (((uint32_t)Imm) <= 15);
  }]> {
  let PrintMethod = "printPrefetchOp<true>";
  let ParserMatchClass = SVEPrefetchOperand;
}

class SVELogicalImmOperand<int Width> : AsmOperandClass {
  let Name = "SVELogicalImm" # Width;
  let DiagnosticType = "LogicalSecondSource";
  let PredicateMethod = "isLogicalImm<int" # Width # "_t>";
  let RenderMethod = "addLogicalImmOperands<int" # Width # "_t>";
}

def sve_logical_imm8 : Operand<i64> {
  let ParserMatchClass = SVELogicalImmOperand<8>;
  let PrintMethod = "printLogicalImm<int8_t>";

  let MCOperandPredicate = [{
    if (!MCOp.isImm())
      return false;
    int64_t Val = AArch64_AM::decodeLogicalImmediate(MCOp.getImm(), 64);
    return AArch64_AM::isSVEMaskOfIdenticalElements<int8_t>(Val);
  }];
}

def sve_logical_imm16 : Operand<i64> {
  let ParserMatchClass = SVELogicalImmOperand<16>;
  let PrintMethod = "printLogicalImm<int16_t>";

  let MCOperandPredicate = [{
    if (!MCOp.isImm())
      return false;
    int64_t Val = AArch64_AM::decodeLogicalImmediate(MCOp.getImm(), 64);
    return AArch64_AM::isSVEMaskOfIdenticalElements<int16_t>(Val);
  }];
}

def sve_logical_imm32 : Operand<i64> {
  let ParserMatchClass = SVELogicalImmOperand<32>;
  let PrintMethod = "printLogicalImm<int32_t>";

  let MCOperandPredicate = [{
    if (!MCOp.isImm())
      return false;
    int64_t Val = AArch64_AM::decodeLogicalImmediate(MCOp.getImm(), 64);
    return AArch64_AM::isSVEMaskOfIdenticalElements<int32_t>(Val);
  }];
}

class SVEPreferredLogicalImmOperand<int Width> : AsmOperandClass {
  let Name = "SVEPreferredLogicalImm" # Width;
  let PredicateMethod = "isSVEPreferredLogicalImm<int" # Width # "_t>";
  let RenderMethod = "addLogicalImmOperands<int" # Width # "_t>";
}

def sve_preferred_logical_imm16 : Operand<i64> {
  let ParserMatchClass = SVEPreferredLogicalImmOperand<16>;
  let PrintMethod = "printSVELogicalImm<int16_t>";

  let MCOperandPredicate = [{
    if (!MCOp.isImm())
      return false;
    int64_t Val = AArch64_AM::decodeLogicalImmediate(MCOp.getImm(), 64);
    return AArch64_AM::isSVEMaskOfIdenticalElements<int16_t>(Val) &&
           AArch64_AM::isSVEMoveMaskPreferredLogicalImmediate(Val);
  }];
}

def sve_preferred_logical_imm32 : Operand<i64> {
  let ParserMatchClass =  SVEPreferredLogicalImmOperand<32>;
  let PrintMethod = "printSVELogicalImm<int32_t>";

  let MCOperandPredicate = [{
    if (!MCOp.isImm())
      return false;
    int64_t Val = AArch64_AM::decodeLogicalImmediate(MCOp.getImm(), 64);
    return AArch64_AM::isSVEMaskOfIdenticalElements<int32_t>(Val) &&
           AArch64_AM::isSVEMoveMaskPreferredLogicalImmediate(Val);
  }];
}

def sve_preferred_logical_imm64 : Operand<i64> {
  let ParserMatchClass = SVEPreferredLogicalImmOperand<64>;
  let PrintMethod = "printSVELogicalImm<int64_t>";

  let MCOperandPredicate = [{
    if (!MCOp.isImm())
      return false;
    int64_t Val = AArch64_AM::decodeLogicalImmediate(MCOp.getImm(), 64);
    return AArch64_AM::isSVEMaskOfIdenticalElements<int64_t>(Val) &&
           AArch64_AM::isSVEMoveMaskPreferredLogicalImmediate(Val);
  }];
}

class SVELogicalImmNotOperand<int Width> : AsmOperandClass {
  let Name = "SVELogicalImm" # Width # "Not";
  let DiagnosticType = "LogicalSecondSource";
  let PredicateMethod = "isLogicalImm<int" # Width # "_t>";
  let RenderMethod = "addLogicalImmNotOperands<int" # Width # "_t>";
}

def sve_logical_imm8_not : Operand<i64> {
  let ParserMatchClass = SVELogicalImmNotOperand<8>;
}

def sve_logical_imm16_not : Operand<i64> {
  let ParserMatchClass = SVELogicalImmNotOperand<16>;
}

def sve_logical_imm32_not : Operand<i64> {
  let ParserMatchClass = SVELogicalImmNotOperand<32>;
}

class SVEShiftedImmOperand<int ElementWidth, string Infix, string Predicate>
    : AsmOperandClass {
  let Name = "SVE" # Infix # "Imm" # ElementWidth;
  let DiagnosticType = "Invalid" # Name;
  let RenderMethod = "addImmWithOptionalShiftOperands<8>";
  let ParserMethod = "tryParseImmWithOptionalShift";
  let PredicateMethod = Predicate;
}

def SVECpyImmOperand8  : SVEShiftedImmOperand<8,  "Cpy", "isSVECpyImm<int8_t>">;
def SVECpyImmOperand16 : SVEShiftedImmOperand<16, "Cpy", "isSVECpyImm<int16_t>">;
def SVECpyImmOperand32 : SVEShiftedImmOperand<32, "Cpy", "isSVECpyImm<int32_t>">;
def SVECpyImmOperand64 : SVEShiftedImmOperand<64, "Cpy", "isSVECpyImm<int64_t>">;

def SVEAddSubImmOperand8  : SVEShiftedImmOperand<8,  "AddSub", "isSVEAddSubImm<int8_t>">;
def SVEAddSubImmOperand16 : SVEShiftedImmOperand<16, "AddSub", "isSVEAddSubImm<int16_t>">;
def SVEAddSubImmOperand32 : SVEShiftedImmOperand<32, "AddSub", "isSVEAddSubImm<int32_t>">;
def SVEAddSubImmOperand64 : SVEShiftedImmOperand<64, "AddSub", "isSVEAddSubImm<int64_t>">;

class imm8_opt_lsl<int ElementWidth, string printType,
                   AsmOperandClass OpndClass, code Predicate>
    : Operand<i32>, ImmLeaf<i32, Predicate> {
  let EncoderMethod = "getImm8OptLsl";
  let DecoderMethod = "DecodeImm8OptLsl<" # ElementWidth # ">";
  let PrintMethod = "printImm8OptLsl<" # printType # ">";
  let ParserMatchClass = OpndClass;
  let MIOperandInfo = (ops i32imm, i32imm);
}

def cpy_imm8_opt_lsl_i8  : imm8_opt_lsl<8,  "int8_t",  SVECpyImmOperand8,  [{
  return AArch64_AM::isSVECpyImm<int8_t>(Imm);
}]>;
def cpy_imm8_opt_lsl_i16 : imm8_opt_lsl<16, "int16_t", SVECpyImmOperand16, [{
  return AArch64_AM::isSVECpyImm<int16_t>(Imm);
}]>;
def cpy_imm8_opt_lsl_i32 : imm8_opt_lsl<32, "int32_t", SVECpyImmOperand32, [{
  return AArch64_AM::isSVECpyImm<int32_t>(Imm);
}]>;
def cpy_imm8_opt_lsl_i64 : imm8_opt_lsl<64, "int64_t", SVECpyImmOperand64, [{
  return AArch64_AM::isSVECpyImm<int64_t>(Imm);
}]>;

def addsub_imm8_opt_lsl_i8  : imm8_opt_lsl<8,  "uint8_t",  SVEAddSubImmOperand8,  [{
  return AArch64_AM::isSVEAddSubImm<int8_t>(Imm);
}]>;
def addsub_imm8_opt_lsl_i16 : imm8_opt_lsl<16, "uint16_t", SVEAddSubImmOperand16, [{
  return AArch64_AM::isSVEAddSubImm<int16_t>(Imm);
}]>;
def addsub_imm8_opt_lsl_i32 : imm8_opt_lsl<32, "uint32_t", SVEAddSubImmOperand32, [{
  return AArch64_AM::isSVEAddSubImm<int32_t>(Imm);
}]>;
def addsub_imm8_opt_lsl_i64 : imm8_opt_lsl<64, "uint64_t", SVEAddSubImmOperand64, [{
  return AArch64_AM::isSVEAddSubImm<int64_t>(Imm);
}]>;

class SVEExactFPImm<string Suffix, string ValA, string ValB> : AsmOperandClass {
  let Name = "SVEExactFPImmOperand" # Suffix;
  let DiagnosticType = "Invalid" # Name;
  let ParserMethod = "tryParseFPImm<false>";
  let PredicateMethod = "isExactFPImm<" # ValA # ", " # ValB # ">";
  let RenderMethod = "addExactFPImmOperands<" # ValA # ", " # ValB # ">";
}

class SVEExactFPImmOperand<string Suffix, string ValA, string ValB> : Operand<i32> {
  let PrintMethod = "printExactFPImm<" # ValA # ", " # ValB # ">";
  let ParserMatchClass = SVEExactFPImm<Suffix, ValA, ValB>;
}

def sve_fpimm_half_one
    : SVEExactFPImmOperand<"HalfOne", "AArch64ExactFPImm::half",
                           "AArch64ExactFPImm::one">;
def sve_fpimm_half_two
    : SVEExactFPImmOperand<"HalfTwo", "AArch64ExactFPImm::half",
                           "AArch64ExactFPImm::two">;
def sve_fpimm_zero_one
    : SVEExactFPImmOperand<"ZeroOne", "AArch64ExactFPImm::zero",
                           "AArch64ExactFPImm::one">;

def sve_incdec_imm : Operand<i32>, ImmLeaf<i32, [{
  return (((uint32_t)Imm) > 0) && (((uint32_t)Imm) < 17);
}]> {
  let ParserMatchClass = Imm1_16Operand;
  let EncoderMethod = "getSVEIncDecImm";
  let DecoderMethod = "DecodeSVEIncDecImm";
}

//===----------------------------------------------------------------------===//
// SVE PTrue - These are used extensively throughout the pattern matching so
//             it's important we define them first.
//===----------------------------------------------------------------------===//

class sve_int_ptrue<bits<2> sz8_64, bits<3> opc, string asm, PPRRegOp pprty>
: I<(outs pprty:$Pd), (ins sve_pred_enum:$pattern),
  asm, "\t$Pd, $pattern",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<5> pattern;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-19} = 0b011;
  let Inst{18-17} = opc{2-1};
  let Inst{16}    = opc{0};
  let Inst{15-10} = 0b111000;
  let Inst{9-5}   = pattern;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = Pd;

  let Defs = !if(!eq (opc{0}, 1), [NZCV], []);
}

multiclass sve_int_ptrue<bits<3> opc, string asm> {
  def _B : sve_int_ptrue<0b00, opc, asm, PPR8>;
  def _H : sve_int_ptrue<0b01, opc, asm, PPR16>;
  def _S : sve_int_ptrue<0b10, opc, asm, PPR32>;
  def _D : sve_int_ptrue<0b11, opc, asm, PPR64>;

  def : InstAlias<asm # "\t$Pd",
                  (!cast<Instruction>(NAME # _B) PPR8:$Pd, 0b11111), 1>;
  def : InstAlias<asm # "\t$Pd",
                  (!cast<Instruction>(NAME # _H) PPR16:$Pd, 0b11111), 1>;
  def : InstAlias<asm # "\t$Pd",
                  (!cast<Instruction>(NAME # _S) PPR32:$Pd, 0b11111), 1>;
  def : InstAlias<asm # "\t$Pd",
                  (!cast<Instruction>(NAME # _D) PPR64:$Pd, 0b11111), 1>;
}

let Predicates = [HasSVE] in {
  defm PTRUE  : sve_int_ptrue<0b000, "ptrue">;
  defm PTRUES : sve_int_ptrue<0b001, "ptrues">;
}


//===----------------------------------------------------------------------===//
// SVE Predicate Misc Group
//===----------------------------------------------------------------------===//

class sve_int_pfalse<bits<6> opc, string asm>
: I<(outs PPR8:$Pd), (ins),
  asm, "\t$Pd",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = opc{5-4};
  let Inst{21-19} = 0b011;
  let Inst{18-16} = opc{3-1};
  let Inst{15-10} = 0b111001;
  let Inst{9}     = opc{0};
  let Inst{8-4}   = 0b00000;
  let Inst{3-0}   = Pd;
}

class sve_int_ptest<bits<6> opc, string asm>
: I<(outs), (ins PPRAny:$Pg, PPR8:$Pn),
  asm, "\t$Pg, $Pn",
  "",
  []>, Sched<[]> {
  bits<4> Pg;
  bits<4> Pn;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = opc{5-4};
  let Inst{21-19} = 0b010;
  let Inst{18-16} = opc{3-1};
  let Inst{15-14} = 0b11;
  let Inst{13-10} = Pg;
  let Inst{9}     = opc{0};
  let Inst{8-5}   = Pn;
  let Inst{4-0}   = 0b00000;

  let Defs = [NZCV];
}

class sve_int_pfirst_next<bits<2> sz8_64, bits<5> opc, string asm,
                          PPRRegOp pprty>
: I<(outs pprty:$Pdn), (ins PPRAny:$Pg, pprty:$_Pdn),
  asm, "\t$Pdn, $Pg, $_Pdn",
  "",
  []>, Sched<[]> {
  bits<4> Pdn;
  bits<4> Pg;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-19} = 0b011;
  let Inst{18-16} = opc{4-2};
  let Inst{15-11} = 0b11000;
  let Inst{10-9}  = opc{1-0};
  let Inst{8-5}   = Pg;
  let Inst{4}     = 0;
  let Inst{3-0}   = Pdn;

  let Constraints = "$Pdn = $_Pdn";
  let Defs = [NZCV];
}

multiclass sve_int_pfirst<bits<5> opc, string asm> {
  def : sve_int_pfirst_next<0b01, opc, asm, PPR8>;
}

multiclass sve_int_pnext<bits<5> opc, string asm> {
  def _B : sve_int_pfirst_next<0b00, opc, asm, PPR8>;
  def _H : sve_int_pfirst_next<0b01, opc, asm, PPR16>;
  def _S : sve_int_pfirst_next<0b10, opc, asm, PPR32>;
  def _D : sve_int_pfirst_next<0b11, opc, asm, PPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Predicate Count Group
//===----------------------------------------------------------------------===//

class sve_int_count_r<bits<2> sz8_64, bits<5> opc, string asm,
                      RegisterOperand dty, PPRRegOp pprty, RegisterOperand sty>
: I<(outs dty:$Rdn), (ins pprty:$Pg, sty:$_Rdn),
  asm, "\t$Rdn, $Pg",
  "",
  []>, Sched<[]> {
  bits<5> Rdn;
  bits<4> Pg;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-19} = 0b101;
  let Inst{18-16} = opc{4-2};
  let Inst{15-11} = 0b10001;
  let Inst{10-9}  = opc{1-0};
  let Inst{8-5}   = Pg;
  let Inst{4-0}   = Rdn;

  // Signed 32bit forms require their GPR operand printed.
  let AsmString = !if(!eq(opc{4,2-0}, 0b0000),
                      !strconcat(asm, "\t$Rdn, $Pg, $_Rdn"),
                      !strconcat(asm, "\t$Rdn, $Pg"));
  let Constraints = "$Rdn = $_Rdn";
}

multiclass sve_int_count_r_s32<bits<5> opc, string asm> {
  def _B : sve_int_count_r<0b00, opc, asm, GPR64z, PPR8, GPR64as32>;
  def _H : sve_int_count_r<0b01, opc, asm, GPR64z, PPR16, GPR64as32>;
  def _S : sve_int_count_r<0b10, opc, asm, GPR64z, PPR32, GPR64as32>;
  def _D : sve_int_count_r<0b11, opc, asm, GPR64z, PPR64, GPR64as32>;
}

multiclass sve_int_count_r_u32<bits<5> opc, string asm> {
  def _B : sve_int_count_r<0b00, opc, asm, GPR32z, PPR8, GPR32z>;
  def _H : sve_int_count_r<0b01, opc, asm, GPR32z, PPR16, GPR32z>;
  def _S : sve_int_count_r<0b10, opc, asm, GPR32z, PPR32, GPR32z>;
  def _D : sve_int_count_r<0b11, opc, asm, GPR32z, PPR64, GPR32z>;
}

multiclass sve_int_count_r_x64<bits<5> opc, string asm> {
  def _B : sve_int_count_r<0b00, opc, asm, GPR64z, PPR8, GPR64z>;
  def _H : sve_int_count_r<0b01, opc, asm, GPR64z, PPR16, GPR64z>;
  def _S : sve_int_count_r<0b10, opc, asm, GPR64z, PPR32, GPR64z>;
  def _D : sve_int_count_r<0b11, opc, asm, GPR64z, PPR64, GPR64z>;
}

class sve_int_count_v<bits<2> sz8_64, bits<5> opc, string asm,
                      ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins zprty:$_Zdn, PPRAny:$Pg),
  asm, "\t$Zdn, $Pg",
  "",
  []>, Sched<[]> {
  bits<4> Pg;
  bits<5> Zdn;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-19} = 0b101;
  let Inst{18-16} = opc{4-2};
  let Inst{15-11} = 0b10000;
  let Inst{10-9}  = opc{1-0};
  let Inst{8-5}   = Pg;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_count_v<bits<5> opc, string asm> {
  def _H : sve_int_count_v<0b01, opc, asm, ZPR16>;
  def _S : sve_int_count_v<0b10, opc, asm, ZPR32>;
  def _D : sve_int_count_v<0b11, opc, asm, ZPR64>;
}

class sve_int_pcount_pred<bits<2> sz8_64, bits<4> opc, string asm,
                          PPRRegOp pprty>
: I<(outs GPR64:$Rd), (ins PPRAny:$Pg, pprty:$Pn),
  asm, "\t$Rd, $Pg, $Pn",
  "",
  []>, Sched<[]> {
  bits<4> Pg;
  bits<4> Pn;
  bits<5> Rd;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-19} = 0b100;
  let Inst{18-16} = opc{3-1};
  let Inst{15-14} = 0b10;
  let Inst{13-10} = Pg;
  let Inst{9}     = opc{0};
  let Inst{8-5}   = Pn;
  let Inst{4-0}   = Rd;
}

multiclass sve_int_pcount_pred<bits<4> opc, string asm> {
  def _B : sve_int_pcount_pred<0b00, opc, asm, PPR8>;
  def _H : sve_int_pcount_pred<0b01, opc, asm, PPR16>;
  def _S : sve_int_pcount_pred<0b10, opc, asm, PPR32>;
  def _D : sve_int_pcount_pred<0b11, opc, asm, PPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Element Count Group
//===----------------------------------------------------------------------===//

class sve_int_count<bits<3> opc, string asm>
: I<(outs GPR64:$Rd), (ins sve_pred_enum:$pattern, sve_incdec_imm:$imm4),
  asm, "\t$Rd, $pattern, mul $imm4",
  "",
  []>, Sched<[]> {
  bits<5> Rd;
  bits<4> imm4;
  bits<5> pattern;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = opc{2-1};
  let Inst{21-20} = 0b10;
  let Inst{19-16} = imm4;
  let Inst{15-11} = 0b11100;
  let Inst{10}    = opc{0};
  let Inst{9-5}   = pattern;
  let Inst{4-0}   = Rd;
}

multiclass sve_int_count<bits<3> opc, string asm> {
  def NAME : sve_int_count<opc, asm>;

  def : InstAlias<asm # "\t$Rd, $pattern",
                  (!cast<Instruction>(NAME) GPR64:$Rd, sve_pred_enum:$pattern, 1), 1>;
  def : InstAlias<asm # "\t$Rd",
                  (!cast<Instruction>(NAME) GPR64:$Rd, 0b11111, 1), 2>;
}

class sve_int_countvlv<bits<5> opc, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins zprty:$_Zdn, sve_pred_enum:$pattern, sve_incdec_imm:$imm4),
  asm, "\t$Zdn, $pattern, mul $imm4",
  "",
  []>, Sched<[]> {
  bits<5> Zdn;
  bits<5> pattern;
  bits<4> imm4;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = opc{4-3};
  let Inst{21}    = 0b1;
  let Inst{20}    = opc{2};
  let Inst{19-16} = imm4;
  let Inst{15-12} = 0b1100;
  let Inst{11-10} = opc{1-0};
  let Inst{9-5}   = pattern;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_countvlv<bits<5> opc, string asm, ZPRRegOp zprty> {
  def NAME : sve_int_countvlv<opc, asm, zprty>;

  def : InstAlias<asm # "\t$Zdn, $pattern",
                  (!cast<Instruction>(NAME) zprty:$Zdn, sve_pred_enum:$pattern, 1), 1>;
  def : InstAlias<asm # "\t$Zdn",
                  (!cast<Instruction>(NAME) zprty:$Zdn, 0b11111, 1), 2>;
}

class sve_int_pred_pattern_a<bits<3> opc, string asm>
: I<(outs GPR64:$Rdn), (ins GPR64:$_Rdn, sve_pred_enum:$pattern, sve_incdec_imm:$imm4),
  asm, "\t$Rdn, $pattern, mul $imm4",
  "",
  []>, Sched<[]> {
  bits<5> Rdn;
  bits<5> pattern;
  bits<4> imm4;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = opc{2-1};
  let Inst{21-20} = 0b11;
  let Inst{19-16} = imm4;
  let Inst{15-11} = 0b11100;
  let Inst{10}    = opc{0};
  let Inst{9-5}   = pattern;
  let Inst{4-0}   = Rdn;

  let Constraints = "$Rdn = $_Rdn";
}

multiclass sve_int_pred_pattern_a<bits<3> opc, string asm> {
  def NAME : sve_int_pred_pattern_a<opc, asm>;

  def : InstAlias<asm # "\t$Rdn, $pattern",
                  (!cast<Instruction>(NAME) GPR64:$Rdn, sve_pred_enum:$pattern, 1), 1>;
  def : InstAlias<asm # "\t$Rdn",
                  (!cast<Instruction>(NAME) GPR64:$Rdn, 0b11111, 1), 2>;
}

class sve_int_pred_pattern_b<bits<5> opc, string asm, RegisterOperand dt,
                             RegisterOperand st>
: I<(outs dt:$Rdn), (ins st:$_Rdn, sve_pred_enum:$pattern, sve_incdec_imm:$imm4),
  asm, "\t$Rdn, $pattern, mul $imm4",
  "",
  []>, Sched<[]> {
  bits<5> Rdn;
  bits<5> pattern;
  bits<4> imm4;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = opc{4-3};
  let Inst{21}    = 0b1;
  let Inst{20}    = opc{2};
  let Inst{19-16} = imm4;
  let Inst{15-12} = 0b1111;
  let Inst{11-10} = opc{1-0};
  let Inst{9-5}   = pattern;
  let Inst{4-0}   = Rdn;

  // Signed 32bit forms require their GPR operand printed.
  let AsmString = !if(!eq(opc{2,0}, 0b00),
                      !strconcat(asm, "\t$Rdn, $_Rdn, $pattern, mul $imm4"),
                      !strconcat(asm, "\t$Rdn, $pattern, mul $imm4"));

  let Constraints = "$Rdn = $_Rdn";
}

multiclass sve_int_pred_pattern_b_s32<bits<5> opc, string asm> {
  def NAME : sve_int_pred_pattern_b<opc, asm, GPR64z, GPR64as32>;

  def : InstAlias<asm # "\t$Rd, $Rn, $pattern",
                  (!cast<Instruction>(NAME) GPR64z:$Rd, GPR64as32:$Rn, sve_pred_enum:$pattern, 1), 1>;
  def : InstAlias<asm # "\t$Rd, $Rn",
                  (!cast<Instruction>(NAME) GPR64z:$Rd, GPR64as32:$Rn, 0b11111, 1), 2>;
}

multiclass sve_int_pred_pattern_b_u32<bits<5> opc, string asm> {
  def NAME : sve_int_pred_pattern_b<opc, asm, GPR32z, GPR32z>;

  def : InstAlias<asm # "\t$Rdn, $pattern",
                  (!cast<Instruction>(NAME) GPR32z:$Rdn, sve_pred_enum:$pattern, 1), 1>;
  def : InstAlias<asm # "\t$Rdn",
                  (!cast<Instruction>(NAME) GPR32z:$Rdn, 0b11111, 1), 2>;
}

multiclass sve_int_pred_pattern_b_x64<bits<5> opc, string asm> {
  def NAME : sve_int_pred_pattern_b<opc, asm, GPR64z, GPR64z>;

  def : InstAlias<asm # "\t$Rdn, $pattern",
                  (!cast<Instruction>(NAME) GPR64z:$Rdn, sve_pred_enum:$pattern, 1), 1>;
  def : InstAlias<asm # "\t$Rdn",
                  (!cast<Instruction>(NAME) GPR64z:$Rdn, 0b11111, 1), 2>;
}


//===----------------------------------------------------------------------===//
// SVE Permute - Cross Lane Group
//===----------------------------------------------------------------------===//

class sve_int_perm_dup_r<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                         RegisterClass srcRegType>
: I<(outs zprty:$Zd), (ins srcRegType:$Rn),
  asm, "\t$Zd, $Rn",
  "",
  []>, Sched<[]> {
  bits<5> Rn;
  bits<5> Zd;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-10} = 0b100000001110;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_perm_dup_r<string asm> {
  def _B : sve_int_perm_dup_r<0b00, asm, ZPR8, GPR32sp>;
  def _H : sve_int_perm_dup_r<0b01, asm, ZPR16, GPR32sp>;
  def _S : sve_int_perm_dup_r<0b10, asm, ZPR32, GPR32sp>;
  def _D : sve_int_perm_dup_r<0b11, asm, ZPR64, GPR64sp>;

  def : InstAlias<"mov $Zd, $Rn",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd, GPR32sp:$Rn), 1>;
  def : InstAlias<"mov $Zd, $Rn",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, GPR32sp:$Rn), 1>;
  def : InstAlias<"mov $Zd, $Rn",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, GPR32sp:$Rn), 1>;
  def : InstAlias<"mov $Zd, $Rn",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, GPR64sp:$Rn), 1>;
}

class sve_int_perm_dup_i<bits<5> tsz, Operand immtype, string asm,
                         ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$Zn, immtype:$idx),
  asm, "\t$Zd, $Zn$idx",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zn;
  bits<7> idx;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = {?,?}; // imm3h
  let Inst{21}    = 0b1;
  let Inst{20-16} = tsz;
  let Inst{15-10} = 0b001000;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_perm_dup_i<string asm> {
  def _B : sve_int_perm_dup_i<{?,?,?,?,1}, sve_elm_idx_extdup_b, asm, ZPR8> {
    let Inst{23-22} = idx{5-4};
    let Inst{20-17} = idx{3-0};
  }
  def _H : sve_int_perm_dup_i<{?,?,?,1,0}, sve_elm_idx_extdup_h, asm, ZPR16> {
    let Inst{23-22} = idx{4-3};
    let Inst{20-18} = idx{2-0};
  }
  def _S : sve_int_perm_dup_i<{?,?,1,0,0}, sve_elm_idx_extdup_s, asm, ZPR32> {
    let Inst{23-22} = idx{3-2};
    let Inst{20-19}    = idx{1-0};
  }
  def _D : sve_int_perm_dup_i<{?,1,0,0,0}, sve_elm_idx_extdup_d, asm, ZPR64> {
    let Inst{23-22} = idx{2-1};
    let Inst{20}    = idx{0};
  }
  def _Q : sve_int_perm_dup_i<{1,0,0,0,0}, sve_elm_idx_extdup_q, asm, ZPR128> {
    let Inst{23-22} = idx{1-0};
  }

  def : InstAlias<"mov $Zd, $Zn$idx",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd, ZPR8:$Zn, sve_elm_idx_extdup_b:$idx), 1>;
  def : InstAlias<"mov $Zd, $Zn$idx",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, ZPR16:$Zn, sve_elm_idx_extdup_h:$idx), 1>;
  def : InstAlias<"mov $Zd, $Zn$idx",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, ZPR32:$Zn, sve_elm_idx_extdup_s:$idx), 1>;
  def : InstAlias<"mov $Zd, $Zn$idx",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, ZPR64:$Zn, sve_elm_idx_extdup_d:$idx), 1>;
  def : InstAlias<"mov $Zd, $Zn$idx",
                  (!cast<Instruction>(NAME # _Q) ZPR128:$Zd, ZPR128:$Zn, sve_elm_idx_extdup_q:$idx), 1>;
  def : InstAlias<"mov $Zd, $Bn",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd, FPR8asZPR:$Bn, 0), 2>;
  def : InstAlias<"mov $Zd, $Hn",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, FPR16asZPR:$Hn, 0), 2>;
  def : InstAlias<"mov $Zd, $Sn",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, FPR32asZPR:$Sn, 0), 2>;
  def : InstAlias<"mov $Zd, $Dn",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, FPR64asZPR:$Dn, 0), 2>;
  def : InstAlias<"mov $Zd, $Qn",
                  (!cast<Instruction>(NAME # _Q) ZPR128:$Zd, FPR128asZPR:$Qn, 0), 2>;
}

class sve_int_perm_tbl<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                       RegisterOperand VecList>
: I<(outs zprty:$Zd), (ins VecList:$Zn, zprty:$Zm),
  asm, "\t$Zd, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-10} = 0b001100;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_perm_tbl<string asm> {
  def _B : sve_int_perm_tbl<0b00, asm, ZPR8, Z_b>;
  def _H : sve_int_perm_tbl<0b01, asm, ZPR16, Z_h>;
  def _S : sve_int_perm_tbl<0b10, asm, ZPR32, Z_s>;
  def _D : sve_int_perm_tbl<0b11, asm, ZPR64, Z_d>;

  def : InstAlias<asm # "\t$Zd, $Zn, $Zm",
                 (!cast<Instruction>(NAME # _B) ZPR8:$Zd, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<asm # "\t$Zd, $Zn, $Zm",
                 (!cast<Instruction>(NAME # _H) ZPR16:$Zd, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<asm # "\t$Zd, $Zn, $Zm",
                 (!cast<Instruction>(NAME # _S) ZPR32:$Zd, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<asm # "\t$Zd, $Zn, $Zm",
                 (!cast<Instruction>(NAME # _D) ZPR64:$Zd, ZPR64:$Zn, ZPR64:$Zm), 0>;
}

class sve_int_perm_reverse_z<bits<2> sz8_64, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$Zn),
  asm, "\t$Zd, $Zn",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-10} = 0b111000001110;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_perm_reverse_z<string asm> {
  def _B : sve_int_perm_reverse_z<0b00, asm, ZPR8>;
  def _H : sve_int_perm_reverse_z<0b01, asm, ZPR16>;
  def _S : sve_int_perm_reverse_z<0b10, asm, ZPR32>;
  def _D : sve_int_perm_reverse_z<0b11, asm, ZPR64>;
}

class sve_int_perm_reverse_p<bits<2> sz8_64, string asm, PPRRegOp pprty>
: I<(outs pprty:$Pd), (ins pprty:$Pn),
  asm, "\t$Pd, $Pn",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<4> Pn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-9}  = 0b1101000100000;
  let Inst{8-5}   = Pn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = Pd;
}

multiclass sve_int_perm_reverse_p<string asm> {
  def _B : sve_int_perm_reverse_p<0b00, asm, PPR8>;
  def _H : sve_int_perm_reverse_p<0b01, asm, PPR16>;
  def _S : sve_int_perm_reverse_p<0b10, asm, PPR32>;
  def _D : sve_int_perm_reverse_p<0b11, asm, PPR64>;
}

class sve_int_perm_unpk<bits<2> sz16_64, bits<2> opc, string asm,
                        ZPRRegOp zprty1, ZPRRegOp zprty2>
: I<(outs zprty1:$Zd), (ins zprty2:$Zn),
  asm, "\t$Zd, $Zn",
  "", []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz16_64;
  let Inst{21-18} = 0b1100;
  let Inst{17-16} = opc;
  let Inst{15-10} = 0b001110;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_perm_unpk<bits<2> opc, string asm> {
  def _H : sve_int_perm_unpk<0b01, opc, asm, ZPR16, ZPR8>;
  def _S : sve_int_perm_unpk<0b10, opc, asm, ZPR32, ZPR16>;
  def _D : sve_int_perm_unpk<0b11, opc, asm, ZPR64, ZPR32>;
}

class sve_int_perm_insrs<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                         RegisterClass srcRegType>
: I<(outs zprty:$Zdn), (ins zprty:$_Zdn, srcRegType:$Rm),
  asm, "\t$Zdn, $Rm",
  "",
  []>, Sched<[]> {
  bits<5> Rm;
  bits<5> Zdn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-10} = 0b100100001110;
  let Inst{9-5}   = Rm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_perm_insrs<string asm> {
  def _B : sve_int_perm_insrs<0b00, asm, ZPR8, GPR32>;
  def _H : sve_int_perm_insrs<0b01, asm, ZPR16, GPR32>;
  def _S : sve_int_perm_insrs<0b10, asm, ZPR32, GPR32>;
  def _D : sve_int_perm_insrs<0b11, asm, ZPR64, GPR64>;
}

class sve_int_perm_insrv<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                         RegisterClass srcRegType>
: I<(outs zprty:$Zdn), (ins zprty:$_Zdn, srcRegType:$Vm),
  asm, "\t$Zdn, $Vm",
  "",
  []>, Sched<[]> {
  bits<5> Vm;
  bits<5> Zdn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-10} = 0b110100001110;
  let Inst{9-5}   = Vm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_perm_insrv<string asm> {
  def _B : sve_int_perm_insrv<0b00, asm, ZPR8, FPR8>;
  def _H : sve_int_perm_insrv<0b01, asm, ZPR16, FPR16>;
  def _S : sve_int_perm_insrv<0b10, asm, ZPR32, FPR32>;
  def _D : sve_int_perm_insrv<0b11, asm, ZPR64, FPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Permute - Extract Group
//===----------------------------------------------------------------------===//

class sve_int_perm_extract_i<string asm>
: I<(outs ZPR8:$Zdn), (ins ZPR8:$_Zdn, ZPR8:$Zm, imm0_255:$imm8),
  asm, "\t$Zdn, $_Zdn, $Zm, $imm8",
  "", []>, Sched<[]> {
  bits<5> Zdn;
  bits<5> Zm;
  bits<8> imm8;
  let Inst{31-21} = 0b00000101001;
  let Inst{20-16} = imm8{7-3};
  let Inst{15-13} = 0b000;
  let Inst{12-10} = imm8{2-0};
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

//===----------------------------------------------------------------------===//
// SVE Vector Select Group
//===----------------------------------------------------------------------===//

class sve_int_sel_vvv<bits<2> sz8_64, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins PPRAny:$Pg, zprty:$Zn, zprty:$Zm),
  asm, "\t$Zd, $Pg, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<4> Pg;
  bits<5> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-14} = 0b11;
  let Inst{13-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_sel_vvv<string asm> {
  def _B : sve_int_sel_vvv<0b00, asm, ZPR8>;
  def _H : sve_int_sel_vvv<0b01, asm, ZPR16>;
  def _S : sve_int_sel_vvv<0b10, asm, ZPR32>;
  def _D : sve_int_sel_vvv<0b11, asm, ZPR64>;

  def : InstAlias<"mov $Zd, $Pg/m, $Zn",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd, PPRAny:$Pg, ZPR8:$Zn, ZPR8:$Zd), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Zn",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, PPRAny:$Pg, ZPR16:$Zn, ZPR16:$Zd), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Zn",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, PPRAny:$Pg, ZPR32:$Zn, ZPR32:$Zd), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Zn",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, PPRAny:$Pg, ZPR64:$Zn, ZPR64:$Zd), 1>;
}


//===----------------------------------------------------------------------===//
// SVE Predicate Logical Operations Group
//===----------------------------------------------------------------------===//

class sve_int_pred_log<bits<4> opc, string asm>
: I<(outs PPR8:$Pd), (ins PPRAny:$Pg, PPR8:$Pn, PPR8:$Pm),
  asm, "\t$Pd, $Pg/z, $Pn, $Pm",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<4> Pg;
  bits<4> Pm;
  bits<4> Pn;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = opc{3-2};
  let Inst{21-20} = 0b00;
  let Inst{19-16} = Pm;
  let Inst{15-14} = 0b01;
  let Inst{13-10} = Pg;
  let Inst{9}     = opc{1};
  let Inst{8-5}   = Pn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;

  // SEL has no predication qualifier.
  let AsmString = !if(!eq(opc, 0b0011),
                      !strconcat(asm, "\t$Pd, $Pg, $Pn, $Pm"),
                      !strconcat(asm, "\t$Pd, $Pg/z, $Pn, $Pm"));

  let Defs = !if(!eq (opc{2}, 1), [NZCV], []);
}


//===----------------------------------------------------------------------===//
// SVE Logical Mask Immediate Group
//===----------------------------------------------------------------------===//

class sve_int_log_imm<bits<2> opc, string asm>
: I<(outs ZPR64:$Zdn), (ins ZPR64:$_Zdn, logical_imm64:$imms13),
  asm, "\t$Zdn, $_Zdn, $imms13",
  "", []>, Sched<[]> {
  bits<5> Zdn;
  bits<13> imms13;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = opc;
  let Inst{21-18} = 0b0000;
  let Inst{17-5}  = imms13;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DecoderMethod = "DecodeSVELogicalImmInstruction";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_log_imm<bits<2> opc, string asm, string alias> {
  def NAME : sve_int_log_imm<opc, asm>;

  def : InstAlias<asm # "\t$Zdn, $Zdn, $imm",
                  (!cast<Instruction>(NAME) ZPR8:$Zdn, sve_logical_imm8:$imm), 4>;
  def : InstAlias<asm # "\t$Zdn, $Zdn, $imm",
                  (!cast<Instruction>(NAME) ZPR16:$Zdn, sve_logical_imm16:$imm), 3>;
  def : InstAlias<asm # "\t$Zdn, $Zdn, $imm",
                  (!cast<Instruction>(NAME) ZPR32:$Zdn, sve_logical_imm32:$imm), 2>;

  def : InstAlias<alias # "\t$Zdn, $Zdn, $imm",
                  (!cast<Instruction>(NAME) ZPR8:$Zdn, sve_logical_imm8_not:$imm), 0>;
  def : InstAlias<alias # "\t$Zdn, $Zdn, $imm",
                  (!cast<Instruction>(NAME) ZPR16:$Zdn, sve_logical_imm16_not:$imm), 0>;
  def : InstAlias<alias # "\t$Zdn, $Zdn, $imm",
                  (!cast<Instruction>(NAME) ZPR32:$Zdn, sve_logical_imm32_not:$imm), 0>;
  def : InstAlias<alias # "\t$Zdn, $Zdn, $imm",
                  (!cast<Instruction>(NAME) ZPR64:$Zdn, logical_imm64_not:$imm), 0>;
}

class sve_int_dup_mask_imm<string asm>
: I<(outs ZPR64:$Zd), (ins logical_imm64:$imms),
  asm, "\t$Zd, $imms",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<13> imms;
  let Inst{31-18} = 0b00000101110000;
  let Inst{17-5} = imms;
  let Inst{4-0} = Zd;

  let isReMaterializable = 1;
  let DecoderMethod = "DecodeSVELogicalImmInstruction";
}

multiclass sve_int_dup_mask_imm<string asm> {
  def NAME : sve_int_dup_mask_imm<asm>;

  def : InstAlias<"dupm $Zd, $imm",
                  (!cast<Instruction>(NAME) ZPR8:$Zd, sve_logical_imm8:$imm), 4>;
  def : InstAlias<"dupm $Zd, $imm",
                  (!cast<Instruction>(NAME) ZPR16:$Zd, sve_logical_imm16:$imm), 3>;
  def : InstAlias<"dupm $Zd, $imm",
                  (!cast<Instruction>(NAME) ZPR32:$Zd, sve_logical_imm32:$imm), 2>;

  // All Zd.b forms have a CPY/DUP equivalent, hence no byte alias here.
  def : InstAlias<"mov $Zd, $imm",
                  (!cast<Instruction>(NAME) ZPR16:$Zd, sve_preferred_logical_imm16:$imm), 7>;
  def : InstAlias<"mov $Zd, $imm",
                  (!cast<Instruction>(NAME) ZPR32:$Zd, sve_preferred_logical_imm32:$imm), 6>;
  def : InstAlias<"mov $Zd, $imm",
                  (!cast<Instruction>(NAME) ZPR64:$Zd, sve_preferred_logical_imm64:$imm), 5>;
}

//===----------------------------------------------------------------------===//
// SVE Integer Arithmetic -  Unpredicated Group.
//===----------------------------------------------------------------------===//

class sve_int_bin_cons_arit_0<bits<2> sz8_64, bits<3> opc, string asm,
                              ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$Zn, zprty:$Zm),
  asm, "\t$Zd, $Zn, $Zm",
  "", []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-13} = 0b000;
  let Inst{12-10} = opc;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_bin_cons_arit_0<bits<3> opc, string asm> {
  def _B : sve_int_bin_cons_arit_0<0b00, opc, asm, ZPR8>;
  def _H : sve_int_bin_cons_arit_0<0b01, opc, asm, ZPR16>;
  def _S : sve_int_bin_cons_arit_0<0b10, opc, asm, ZPR32>;
  def _D : sve_int_bin_cons_arit_0<0b11, opc, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Arithmetic - Predicated Group
//===----------------------------------------------------------------------===//

class sve_fp_2op_i_p_zds<bits<2> sz, bits<3> opc, string asm,
                         ZPRRegOp zprty,
                         Operand imm_ty>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, imm_ty:$i1),
  asm, "\t$Zdn, $Pg/m, $_Zdn, $i1",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zdn;
  bit i1;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21-19} = 0b011;
  let Inst{18-16} = opc;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-6}   = 0b0000;
  let Inst{5}     = i1;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_fp_2op_i_p_zds<bits<3> opc, string asm, Operand imm_ty> {
  def _H : sve_fp_2op_i_p_zds<0b01, opc, asm, ZPR16, imm_ty>;
  def _S : sve_fp_2op_i_p_zds<0b10, opc, asm, ZPR32, imm_ty>;
  def _D : sve_fp_2op_i_p_zds<0b11, opc, asm, ZPR64, imm_ty>;
}

class sve_fp_2op_p_zds<bits<2> sz, bits<4> opc, string asm,
                       ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, zprty:$Zm),
  asm, "\t$Zdn, $Pg/m, $_Zdn, $Zm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zdn;
  bits<5> Zm;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b00;
  let Inst{19-16} = opc;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_fp_2op_p_zds<bits<4> opc, string asm> {
  def _H : sve_fp_2op_p_zds<0b01, opc, asm, ZPR16>;
  def _S : sve_fp_2op_p_zds<0b10, opc, asm, ZPR32>;
  def _D : sve_fp_2op_p_zds<0b11, opc, asm, ZPR64>;
}

class sve_fp_ftmad<bits<2> sz, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins zprty:$_Zdn, zprty:$Zm, imm0_7:$imm3),
  asm, "\t$Zdn, $_Zdn, $Zm, $imm3",
  "",
  []>, Sched<[]> {
  bits<5> Zdn;
  bits<5> Zm;
  bits<3> imm3;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21-19} = 0b010;
  let Inst{18-16} = imm3;
  let Inst{15-10} = 0b100000;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_fp_ftmad<string asm> {
  def _H : sve_fp_ftmad<0b01, asm, ZPR16>;
  def _S : sve_fp_ftmad<0b10, asm, ZPR32>;
  def _D : sve_fp_ftmad<0b11, asm, ZPR64>;
}


//===----------------------------------------------------------------------===//
// SVE Floating Point Arithmetic - Unpredicated Group
//===----------------------------------------------------------------------===//

class sve_fp_3op_u_zd<bits<2> sz, bits<3> opc, string asm,
                      ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins  zprty:$Zn, zprty:$Zm),
  asm, "\t$Zd, $Zn, $Zm",
  "", []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b0;
  let Inst{20-16} = Zm;
  let Inst{15-13} = 0b000;
  let Inst{12-10} = opc;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_fp_3op_u_zd<bits<3> opc, string asm> {
  def _H : sve_fp_3op_u_zd<0b01, opc, asm, ZPR16>;
  def _S : sve_fp_3op_u_zd<0b10, opc, asm, ZPR32>;
  def _D : sve_fp_3op_u_zd<0b11, opc, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Fused Multiply-Add Group
//===----------------------------------------------------------------------===//

class sve_fp_3op_p_zds_a<bits<2> sz, bits<2> opc, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zda), (ins PPR3bAny:$Pg, zprty:$_Zda, zprty:$Zn, zprty:$Zm),
  asm, "\t$Zda, $Pg/m, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zda;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15}    = 0b0;
  let Inst{14-13} = opc;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zda;

  let Constraints = "$Zda = $_Zda";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_fp_3op_p_zds_a<bits<2> opc, string asm> {
  def _H : sve_fp_3op_p_zds_a<0b01, opc, asm, ZPR16>;
  def _S : sve_fp_3op_p_zds_a<0b10, opc, asm, ZPR32>;
  def _D : sve_fp_3op_p_zds_a<0b11, opc, asm, ZPR64>;
}

class sve_fp_3op_p_zds_b<bits<2> sz, bits<2> opc, string asm,
                         ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, zprty:$Zm, zprty:$Za),
  asm, "\t$Zdn, $Pg/m, $Zm, $Za",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Za;
  bits<5> Zdn;
  bits<5> Zm;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Za;
  let Inst{15}    = 0b1;
  let Inst{14-13} = opc;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_fp_3op_p_zds_b<bits<2> opc, string asm> {
  def _H : sve_fp_3op_p_zds_b<0b01, opc, asm, ZPR16>;
  def _S : sve_fp_3op_p_zds_b<0b10, opc, asm, ZPR32>;
  def _D : sve_fp_3op_p_zds_b<0b11, opc, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Multiply-Add - Indexed Group
//===----------------------------------------------------------------------===//

class sve_fp_fma_by_indexed_elem<bits<2> sz, bit opc, string asm,
                                 ZPRRegOp zprty1,
                                 ZPRRegOp zprty2, Operand itype>
: I<(outs zprty1:$Zda), (ins zprty1:$_Zda, zprty1:$Zn, zprty2:$Zm, itype:$iop),
  asm, "\t$Zda, $Zn, $Zm$iop", "", []>, Sched<[]> {
  bits<5> Zda;
  bits<5> Zn;
  let Inst{31-24} = 0b01100100;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{15-11} = 0;
  let Inst{10}    = opc;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zda;

  let Constraints = "$Zda = $_Zda";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_fp_fma_by_indexed_elem<bit opc, string asm> {
  def _H : sve_fp_fma_by_indexed_elem<{0, ?}, opc, asm, ZPR16, ZPR3b16, VectorIndexH> {
    bits<3> Zm;
    bits<3> iop;
    let Inst{22} = iop{2};
    let Inst{20-19} = iop{1-0};
    let Inst{18-16} = Zm;
  }
  def _S : sve_fp_fma_by_indexed_elem<0b10, opc, asm, ZPR32, ZPR3b32, VectorIndexS> {
    bits<3> Zm;
    bits<2> iop;
    let Inst{20-19} = iop;
    let Inst{18-16} = Zm;
  }
  def _D : sve_fp_fma_by_indexed_elem<0b11, opc, asm, ZPR64, ZPR4b64, VectorIndexD> {
    bits<4> Zm;
    bit iop;
    let Inst{20} = iop;
    let Inst{19-16} = Zm;
  }
}


//===----------------------------------------------------------------------===//
// SVE Floating Point Multiply - Indexed Group
//===----------------------------------------------------------------------===//

class sve_fp_fmul_by_indexed_elem<bits<2> sz, string asm, ZPRRegOp zprty,
                                      ZPRRegOp zprty2, Operand itype>
: I<(outs zprty:$Zd), (ins zprty:$Zn, zprty2:$Zm, itype:$iop),
  asm, "\t$Zd, $Zn, $Zm$iop", "", []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-24} = 0b01100100;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{15-10} = 0b001000;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_fp_fmul_by_indexed_elem<string asm> {
  def _H : sve_fp_fmul_by_indexed_elem<{0, ?}, asm, ZPR16, ZPR3b16, VectorIndexH> {
    bits<3> Zm;
    bits<3> iop;
    let Inst{22} = iop{2};
    let Inst{20-19} = iop{1-0};
    let Inst{18-16} = Zm;
  }
  def _S : sve_fp_fmul_by_indexed_elem<0b10, asm, ZPR32, ZPR3b32, VectorIndexS> {
    bits<3> Zm;
    bits<2> iop;
    let Inst{20-19} = iop;
    let Inst{18-16} = Zm;
  }
  def _D : sve_fp_fmul_by_indexed_elem<0b11, asm, ZPR64, ZPR4b64, VectorIndexD> {
    bits<4> Zm;
    bit iop;
    let Inst{20} = iop;
    let Inst{19-16} = Zm;
  }
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Complex Multiply-Add Group
//===----------------------------------------------------------------------===//

class sve_fp_fcmla<bits<2> sz, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zda), (ins PPR3bAny:$Pg, zprty:$_Zda, zprty:$Zn, zprty:$Zm,
                        complexrotateop:$imm),
  asm, "\t$Zda, $Pg/m, $Zn, $Zm, $imm",
  "", []>, Sched<[]> {
  bits<5> Zda;
  bits<3> Pg;
  bits<5> Zn;
  bits<5> Zm;
  bits<2> imm;
  let Inst{31-24} = 0b01100100;
  let Inst{23-22} = sz;
  let Inst{21}    = 0;
  let Inst{20-16} = Zm;
  let Inst{15}    = 0;
  let Inst{14-13} = imm;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zda;

  let Constraints = "$Zda = $_Zda";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_fp_fcmla<string asm> {
  def _H : sve_fp_fcmla<0b01, asm, ZPR16>;
  def _S : sve_fp_fcmla<0b10, asm, ZPR32>;
  def _D : sve_fp_fcmla<0b11, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Complex Multiply-Add - Indexed Group
//===----------------------------------------------------------------------===//

class sve_fp_fcmla_by_indexed_elem<bits<2> sz, string asm,
                                   ZPRRegOp zprty,
                                   ZPRRegOp zprty2, Operand itype>
: I<(outs zprty:$Zda), (ins zprty:$_Zda, zprty:$Zn, zprty2:$Zm, itype:$iop,
                        complexrotateop:$imm),
  asm, "\t$Zda, $Zn, $Zm$iop, $imm",
  "", []>, Sched<[]> {
  bits<5> Zda;
  bits<5> Zn;
  bits<2> imm;
  let Inst{31-24} = 0b01100100;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{15-12} = 0b0001;
  let Inst{11-10} = imm;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zda;

  let Constraints = "$Zda = $_Zda";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_fp_fcmla_by_indexed_elem<string asm> {
  def _H : sve_fp_fcmla_by_indexed_elem<0b10, asm, ZPR16, ZPR3b16, VectorIndexS> {
    bits<3> Zm;
    bits<2> iop;
    let Inst{20-19} = iop;
    let Inst{18-16} = Zm;
  }
  def _S : sve_fp_fcmla_by_indexed_elem<0b11, asm, ZPR32, ZPR4b32, VectorIndexD> {
    bits<4> Zm;
    bits<1> iop;
    let Inst{20} = iop;
    let Inst{19-16} = Zm;
  }
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Complex Addition Group
//===----------------------------------------------------------------------===//

class sve_fp_fcadd<bits<2> sz, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, zprty:$Zm,
                        complexrotateopodd:$imm),
  asm, "\t$Zdn, $Pg/m, $_Zdn, $Zm, $imm",
  "",
  []>, Sched<[]> {
  bits<5> Zdn;
  bits<5> Zm;
  bits<3> Pg;
  bit imm;
  let Inst{31-24} = 0b01100100;
  let Inst{23-22} = sz;
  let Inst{21-17} = 0;
  let Inst{16}    = imm;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_fp_fcadd<string asm> {
  def _H : sve_fp_fcadd<0b01, asm, ZPR16>;
  def _S : sve_fp_fcadd<0b10, asm, ZPR32>;
  def _D : sve_fp_fcadd<0b11, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Stack Allocation Group
//===----------------------------------------------------------------------===//

class sve_int_arith_vl<bit opc, string asm>
: I<(outs GPR64sp:$Rd), (ins GPR64sp:$Rn, simm6_32b:$imm6),
  asm, "\t$Rd, $Rn, $imm6",
  "",
  []>, Sched<[]> {
  bits<5> Rd;
  bits<5> Rn;
  bits<6> imm6;
  let Inst{31-23} = 0b000001000;
  let Inst{22}    = opc;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Rn;
  let Inst{15-11} = 0b01010;
  let Inst{10-5}  = imm6;
  let Inst{4-0}   = Rd;
}

class sve_int_read_vl_a<bit op, bits<5> opc2, string asm>
: I<(outs GPR64:$Rd), (ins simm6_32b:$imm6),
  asm, "\t$Rd, $imm6",
  "",
  []>, Sched<[]> {
  bits<5> Rd;
  bits<6> imm6;
  let Inst{31-23} = 0b000001001;
  let Inst{22}    = op;
  let Inst{21}    = 0b1;
  let Inst{20-16} = opc2{4-0};
  let Inst{15-11} = 0b01010;
  let Inst{10-5}  = imm6;
  let Inst{4-0}   = Rd;
}

//===----------------------------------------------------------------------===//
// SVE Permute - In Lane Group
//===----------------------------------------------------------------------===//

class sve_int_perm_bin_perm_zz<bits<3> opc, bits<2> sz8_64, string asm,
                               ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$Zn, zprty:$Zm),
  asm, "\t$Zd, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-13} = 0b011;
  let Inst{12-10} = opc;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_perm_bin_perm_zz<bits<3> opc, string asm> {
  def _B : sve_int_perm_bin_perm_zz<opc, 0b00, asm, ZPR8>;
  def _H : sve_int_perm_bin_perm_zz<opc, 0b01, asm, ZPR16>;
  def _S : sve_int_perm_bin_perm_zz<opc, 0b10, asm, ZPR32>;
  def _D : sve_int_perm_bin_perm_zz<opc, 0b11, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Unary Operations Group
//===----------------------------------------------------------------------===//

class sve_fp_2op_p_zd<bits<7> opc, string asm, RegisterOperand i_zprtype,
                      RegisterOperand o_zprtype, ElementSizeEnum size>
: I<(outs o_zprtype:$Zd), (ins i_zprtype:$_Zd, PPR3bAny:$Pg, i_zprtype:$Zn),
  asm, "\t$Zd, $Pg/m, $Zn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = opc{6-5};
  let Inst{21}    = 0b0;
  let Inst{20-16} = opc{4-0};
  let Inst{15-13} = 0b101;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;

  let Constraints = "$Zd = $_Zd";
  let DestructiveInstType = Destructive;
  let ElementSize = size;
}

multiclass sve_fp_2op_p_zd_HSD<bits<5> opc, string asm> {
  def _H : sve_fp_2op_p_zd<{ 0b01, opc }, asm, ZPR16, ZPR16, ElementSizeH>;
  def _S : sve_fp_2op_p_zd<{ 0b10, opc }, asm, ZPR32, ZPR32, ElementSizeS>;
  def _D : sve_fp_2op_p_zd<{ 0b11, opc }, asm, ZPR64, ZPR64, ElementSizeD>;
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Unary Operations - Unpredicated Group
//===----------------------------------------------------------------------===//

class sve_fp_2op_u_zd<bits<2> sz, bits<3> opc, string asm,
                      ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$Zn),
  asm, "\t$Zd, $Zn",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21-19} = 0b001;
  let Inst{18-16} = opc;
  let Inst{15-10} = 0b001100;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_fp_2op_u_zd<bits<3> opc, string asm> {
  def _H : sve_fp_2op_u_zd<0b01, opc, asm, ZPR16>;
  def _S : sve_fp_2op_u_zd<0b10, opc, asm, ZPR32>;
  def _D : sve_fp_2op_u_zd<0b11, opc, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Integer Arithmetic - Binary Predicated Group
//===----------------------------------------------------------------------===//

class sve_int_bin_pred_arit_log<bits<2> sz8_64, bits<2> fmt, bits<3> opc,
                                string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, zprty:$Zm),
  asm, "\t$Zdn, $Pg/m, $_Zdn, $Zm", "", []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zdn;
  bits<5> Zm;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b0;
  let Inst{20-19} = fmt;
  let Inst{18-16} = opc;
  let Inst{15-13} = 0b000;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_bin_pred_log<bits<3> opc, string asm> {
  def _B : sve_int_bin_pred_arit_log<0b00, 0b11, opc, asm, ZPR8>;
  def _H : sve_int_bin_pred_arit_log<0b01, 0b11, opc, asm, ZPR16>;
  def _S : sve_int_bin_pred_arit_log<0b10, 0b11, opc, asm, ZPR32>;
  def _D : sve_int_bin_pred_arit_log<0b11, 0b11, opc, asm, ZPR64>;
}

multiclass sve_int_bin_pred_arit_0<bits<3> opc, string asm> {
  def _B : sve_int_bin_pred_arit_log<0b00, 0b00, opc, asm, ZPR8>;
  def _H : sve_int_bin_pred_arit_log<0b01, 0b00, opc, asm, ZPR16>;
  def _S : sve_int_bin_pred_arit_log<0b10, 0b00, opc, asm, ZPR32>;
  def _D : sve_int_bin_pred_arit_log<0b11, 0b00, opc, asm, ZPR64>;
}

multiclass sve_int_bin_pred_arit_1<bits<3> opc, string asm> {
  def _B : sve_int_bin_pred_arit_log<0b00, 0b01, opc, asm, ZPR8>;
  def _H : sve_int_bin_pred_arit_log<0b01, 0b01, opc, asm, ZPR16>;
  def _S : sve_int_bin_pred_arit_log<0b10, 0b01, opc, asm, ZPR32>;
  def _D : sve_int_bin_pred_arit_log<0b11, 0b01, opc, asm, ZPR64>;
}

multiclass sve_int_bin_pred_arit_2<bits<3> opc, string asm> {
  def _B : sve_int_bin_pred_arit_log<0b00, 0b10, opc, asm, ZPR8>;
  def _H : sve_int_bin_pred_arit_log<0b01, 0b10, opc, asm, ZPR16>;
  def _S : sve_int_bin_pred_arit_log<0b10, 0b10, opc, asm, ZPR32>;
  def _D : sve_int_bin_pred_arit_log<0b11, 0b10, opc, asm, ZPR64>;
}

// Special case for divides which are not defined for 8b/16b elements.
multiclass sve_int_bin_pred_arit_2_div<bits<3> opc, string asm> {
  def _S : sve_int_bin_pred_arit_log<0b10, 0b10, opc, asm, ZPR32>;
  def _D : sve_int_bin_pred_arit_log<0b11, 0b10, opc, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Integer Multiply-Add Group
//===----------------------------------------------------------------------===//

class sve_int_mladdsub_vvv_pred<bits<2> sz8_64, bits<1> opc, string asm,
                                ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, zprty:$Zm, zprty:$Za),
  asm, "\t$Zdn, $Pg/m, $Zm, $Za",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zdn;
  bits<5> Za;
  bits<5> Zm;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b0;
  let Inst{20-16} = Zm;
  let Inst{15-14} = 0b11;
  let Inst{13}    = opc;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Za;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_mladdsub_vvv_pred<bits<1> opc, string asm> {
  def _B : sve_int_mladdsub_vvv_pred<0b00, opc, asm, ZPR8>;
  def _H : sve_int_mladdsub_vvv_pred<0b01, opc, asm, ZPR16>;
  def _S : sve_int_mladdsub_vvv_pred<0b10, opc, asm, ZPR32>;
  def _D : sve_int_mladdsub_vvv_pred<0b11, opc, asm, ZPR64>;
}

class sve_int_mlas_vvv_pred<bits<2> sz8_64, bits<1> opc, string asm,
                            ZPRRegOp zprty>
: I<(outs zprty:$Zda), (ins PPR3bAny:$Pg, zprty:$_Zda, zprty:$Zn, zprty:$Zm),
  asm, "\t$Zda, $Pg/m, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zda;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b0;
  let Inst{20-16} = Zm;
  let Inst{15-14} = 0b01;
  let Inst{13}    = opc;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zda;

  let Constraints = "$Zda = $_Zda";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_mlas_vvv_pred<bits<1> opc, string asm> {
  def _B : sve_int_mlas_vvv_pred<0b00, opc, asm, ZPR8>;
  def _H : sve_int_mlas_vvv_pred<0b01, opc, asm, ZPR16>;
  def _S : sve_int_mlas_vvv_pred<0b10, opc, asm, ZPR32>;
  def _D : sve_int_mlas_vvv_pred<0b11, opc, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Integer Dot Product Group
//===----------------------------------------------------------------------===//

class sve_intx_dot<bit sz, bit U, string asm, ZPRRegOp zprty1,
                   ZPRRegOp zprty2>
: I<(outs zprty1:$Zda), (ins zprty1:$_Zda, zprty2:$Zn, zprty2:$Zm), asm,
  "\t$Zda, $Zn, $Zm", "", []>, Sched<[]> {
  bits<5> Zda;
  bits<5> Zn;
  bits<5> Zm;
  let Inst{31-23} = 0b010001001;
  let Inst{22}    = sz;
  let Inst{21}    = 0;
  let Inst{20-16} = Zm;
  let Inst{15-11} = 0;
  let Inst{10}    = U;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zda;

  let Constraints = "$Zda = $_Zda";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty1.ElementSize;
}

multiclass sve_intx_dot<bit opc, string asm> {
  def _S : sve_intx_dot<0b0, opc, asm, ZPR32, ZPR8>;
  def _D : sve_intx_dot<0b1, opc, asm, ZPR64, ZPR16>;
}

//===----------------------------------------------------------------------===//
// SVE Integer Dot Product Group - Indexed Group
//===----------------------------------------------------------------------===//

class sve_intx_dot_by_indexed_elem<bit sz, bit U, string asm,
                                   ZPRRegOp zprty1, ZPRRegOp zprty2,
                                   ZPRRegOp zprty3, Operand itype>
: I<(outs zprty1:$Zda), (ins zprty1:$_Zda, zprty2:$Zn, zprty3:$Zm, itype:$iop),
  asm, "\t$Zda, $Zn, $Zm$iop",
  "", []>, Sched<[]> {
  bits<5> Zda;
  bits<5> Zn;
  let Inst{31-23} = 0b010001001;
  let Inst{22}    = sz;
  let Inst{21}    = 0b1;
  let Inst{15-11} = 0;
  let Inst{10}    = U;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zda;

  let Constraints = "$Zda = $_Zda";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_intx_dot_by_indexed_elem<bit opc, string asm> {
  def _S : sve_intx_dot_by_indexed_elem<0b0, opc, asm, ZPR32, ZPR8, ZPR3b8, VectorIndexS> {
    bits<2> iop;
    bits<3> Zm;
    let Inst{20-19} = iop;
    let Inst{18-16} = Zm;
  }
  def _D : sve_intx_dot_by_indexed_elem<0b1, opc, asm, ZPR64, ZPR16, ZPR4b16, VectorIndexD> {
    bits<1> iop;
    bits<4> Zm;
    let Inst{20} = iop;
    let Inst{19-16} = Zm;
  }
}

//===----------------------------------------------------------------------===//
// SVE Integer Arithmetic - Unary Predicated Group
//===----------------------------------------------------------------------===//

class sve_int_un_pred_arit<bits<2> sz8_64, bits<4> opc,
                             string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$_Zd, PPR3bAny:$Pg, zprty:$Zn),
  asm, "\t$Zd, $Pg/m, $Zn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21-20} = 0b01;
  let Inst{19}    = opc{0};
  let Inst{18-16} = opc{3-1};
  let Inst{15-13} = 0b101;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;

  let Constraints = "$Zd = $_Zd";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_un_pred_arit_0<bits<3> opc, string asm> {
  def _B : sve_int_un_pred_arit<0b00, { opc, 0b0 }, asm, ZPR8>;
  def _H : sve_int_un_pred_arit<0b01, { opc, 0b0 }, asm, ZPR16>;
  def _S : sve_int_un_pred_arit<0b10, { opc, 0b0 }, asm, ZPR32>;
  def _D : sve_int_un_pred_arit<0b11, { opc, 0b0 }, asm, ZPR64>;
}

multiclass sve_int_un_pred_arit_0_h<bits<3> opc, string asm> {
  def _H : sve_int_un_pred_arit<0b01, { opc, 0b0 }, asm, ZPR16>;
  def _S : sve_int_un_pred_arit<0b10, { opc, 0b0 }, asm, ZPR32>;
  def _D : sve_int_un_pred_arit<0b11, { opc, 0b0 }, asm, ZPR64>;
}

multiclass sve_int_un_pred_arit_0_w<bits<3> opc, string asm> {
  def _S : sve_int_un_pred_arit<0b10, { opc, 0b0 }, asm, ZPR32>;
  def _D : sve_int_un_pred_arit<0b11, { opc, 0b0 }, asm, ZPR64>;
}

multiclass sve_int_un_pred_arit_0_d<bits<3> opc, string asm> {
  def _D : sve_int_un_pred_arit<0b11, { opc, 0b0 }, asm, ZPR64>;
}

multiclass sve_int_un_pred_arit_1<bits<3> opc, string asm> {
  def _B : sve_int_un_pred_arit<0b00, { opc, 0b1 }, asm, ZPR8>;
  def _H : sve_int_un_pred_arit<0b01, { opc, 0b1 }, asm, ZPR16>;
  def _S : sve_int_un_pred_arit<0b10, { opc, 0b1 }, asm, ZPR32>;
  def _D : sve_int_un_pred_arit<0b11, { opc, 0b1 }, asm, ZPR64>;
}

multiclass sve_int_un_pred_arit_1_fp<bits<3> opc, string asm> {
  def _H : sve_int_un_pred_arit<0b01, { opc, 0b1 }, asm, ZPR16>;
  def _S : sve_int_un_pred_arit<0b10, { opc, 0b1 }, asm, ZPR32>;
  def _D : sve_int_un_pred_arit<0b11, { opc, 0b1 }, asm, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Integer Wide Immediate - Unpredicated Group
//===----------------------------------------------------------------------===//
class sve_int_dup_imm<bits<2> sz8_64, string asm,
                      ZPRRegOp zprty, Operand immtype>
: I<(outs zprty:$Zd), (ins immtype:$imm),
  asm, "\t$Zd, $imm",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<9> imm;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-14} = 0b11100011;
  let Inst{13}    = imm{8};   // sh
  let Inst{12-5}  = imm{7-0}; // imm8
  let Inst{4-0}   = Zd;

  let isReMaterializable = 1;
}

multiclass sve_int_dup_imm<string asm> {
  def _B : sve_int_dup_imm<0b00, asm, ZPR8, cpy_imm8_opt_lsl_i8>;
  def _H : sve_int_dup_imm<0b01, asm, ZPR16, cpy_imm8_opt_lsl_i16>;
  def _S : sve_int_dup_imm<0b10, asm, ZPR32, cpy_imm8_opt_lsl_i32>;
  def _D : sve_int_dup_imm<0b11, asm, ZPR64, cpy_imm8_opt_lsl_i64>;

  def : InstAlias<"mov $Zd, $imm",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd, cpy_imm8_opt_lsl_i8:$imm), 1>;
  def : InstAlias<"mov $Zd, $imm",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, cpy_imm8_opt_lsl_i16:$imm), 1>;
  def : InstAlias<"mov $Zd, $imm",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, cpy_imm8_opt_lsl_i32:$imm), 1>;
  def : InstAlias<"mov $Zd, $imm",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, cpy_imm8_opt_lsl_i64:$imm), 1>;

  def : InstAlias<"fmov $Zd, #0.0",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, 0, 0), 1>;
  def : InstAlias<"fmov $Zd, #0.0",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, 0, 0), 1>;
  def : InstAlias<"fmov $Zd, #0.0",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, 0, 0), 1>;
}

class sve_int_dup_fpimm<bits<2> sz8_64, Operand fpimmtype,
                        string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins fpimmtype:$imm8),
  asm, "\t$Zd, $imm8",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<8> imm8;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-14} = 0b11100111;
  let Inst{13}    = 0b0;
  let Inst{12-5}  = imm8;
  let Inst{4-0}   = Zd;

  let isReMaterializable = 1;
}

multiclass sve_int_dup_fpimm<string asm> {
  def _H : sve_int_dup_fpimm<0b01, fpimm16, asm, ZPR16>;
  def _S : sve_int_dup_fpimm<0b10, fpimm32, asm, ZPR32>;
  def _D : sve_int_dup_fpimm<0b11, fpimm64, asm, ZPR64>;

  def : InstAlias<"fmov $Zd, $imm8",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, fpimm16:$imm8), 1>;
  def : InstAlias<"fmov $Zd, $imm8",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, fpimm32:$imm8), 1>;
  def : InstAlias<"fmov $Zd, $imm8",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, fpimm64:$imm8), 1>;
}

class sve_int_arith_imm0<bits<2> sz8_64, bits<3> opc, string asm,
                         ZPRRegOp zprty, Operand immtype>
: I<(outs zprty:$Zdn), (ins zprty:$_Zdn, immtype:$imm),
  asm, "\t$Zdn, $_Zdn, $imm",
  "",
  []>, Sched<[]> {
  bits<5> Zdn;
  bits<9> imm;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-19} = 0b100;
  let Inst{18-16} = opc;
  let Inst{15-14} = 0b11;
  let Inst{13}    = imm{8};   // sh
  let Inst{12-5}  = imm{7-0}; // imm8
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_arith_imm0<bits<3> opc, string asm> {
  def _B : sve_int_arith_imm0<0b00, opc, asm, ZPR8, addsub_imm8_opt_lsl_i8>;
  def _H : sve_int_arith_imm0<0b01, opc, asm, ZPR16, addsub_imm8_opt_lsl_i16>;
  def _S : sve_int_arith_imm0<0b10, opc, asm, ZPR32, addsub_imm8_opt_lsl_i32>;
  def _D : sve_int_arith_imm0<0b11, opc, asm, ZPR64, addsub_imm8_opt_lsl_i64>;
}

class sve_int_arith_imm<bits<2> sz8_64, bits<6> opc, string asm,
                        ZPRRegOp zprty, Operand immtype>
: I<(outs zprty:$Zdn), (ins zprty:$_Zdn, immtype:$imm),
  asm, "\t$Zdn, $_Zdn, $imm",
  "",
  []>, Sched<[]> {
  bits<5> Zdn;
  bits<8> imm;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21-16} = opc;
  let Inst{15-13} = 0b110;
  let Inst{12-5} = imm;
  let Inst{4-0} = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_arith_imm1<bits<2> opc, string asm, Operand immtype> {
  def _B : sve_int_arith_imm<0b00, { 0b1010, opc }, asm, ZPR8, immtype>;
  def _H : sve_int_arith_imm<0b01, { 0b1010, opc }, asm, ZPR16, immtype>;
  def _S : sve_int_arith_imm<0b10, { 0b1010, opc }, asm, ZPR32, immtype>;
  def _D : sve_int_arith_imm<0b11, { 0b1010, opc }, asm, ZPR64, immtype>;
}

multiclass sve_int_arith_imm2<string asm> {
  def _B : sve_int_arith_imm<0b00, 0b110000, asm, ZPR8,  simm8>;
  def _H : sve_int_arith_imm<0b01, 0b110000, asm, ZPR16, simm8>;
  def _S : sve_int_arith_imm<0b10, 0b110000, asm, ZPR32, simm8>;
  def _D : sve_int_arith_imm<0b11, 0b110000, asm, ZPR64, simm8>;
}

//===----------------------------------------------------------------------===//
// SVE Bitwise Logical - Unpredicated Group
//===----------------------------------------------------------------------===//

class sve_int_bin_cons_log<bits<2> opc, string asm>
: I<(outs ZPR64:$Zd), (ins ZPR64:$Zn, ZPR64:$Zm),
  asm, "\t$Zd, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = opc{1-0};
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-10} = 0b001100;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}


//===----------------------------------------------------------------------===//
// SVE Integer Wide Immediate - Predicated Group
//===----------------------------------------------------------------------===//

class sve_int_dup_fpimm_pred<bits<2> sz, Operand fpimmtype,
                             string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$_Zd, PPRAny:$Pg, fpimmtype:$imm8),
  asm, "\t$Zd, $Pg/m, $imm8",
  "",
  []>, Sched<[]> {
  bits<4> Pg;
  bits<5> Zd;
  bits<8> imm8;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b01;
  let Inst{19-16} = Pg;
  let Inst{15-13} = 0b110;
  let Inst{12-5}  = imm8;
  let Inst{4-0}   = Zd;

  let Constraints = "$Zd = $_Zd";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_dup_fpimm_pred<string asm> {
  def _H : sve_int_dup_fpimm_pred<0b01, fpimm16, asm, ZPR16>;
  def _S : sve_int_dup_fpimm_pred<0b10, fpimm32, asm, ZPR32>;
  def _D : sve_int_dup_fpimm_pred<0b11, fpimm64, asm, ZPR64>;

  def : InstAlias<"fmov $Zd, $Pg/m, $imm8",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, PPRAny:$Pg, fpimm16:$imm8), 1>;
  def : InstAlias<"fmov $Zd, $Pg/m, $imm8",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, PPRAny:$Pg, fpimm32:$imm8), 1>;
  def : InstAlias<"fmov $Zd, $Pg/m, $imm8",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, PPRAny:$Pg, fpimm64:$imm8), 1>;
}

class sve_int_dup_imm_pred<bits<2> sz8_64, bit m, string asm,
                           ZPRRegOp zprty, string pred_qual, dag iops>
: I<(outs zprty:$Zd), iops,
  asm, "\t$Zd, $Pg"#pred_qual#", $imm",
  "", []>, Sched<[]> {
  bits<5> Zd;
  bits<4> Pg;
  bits<9> imm;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-20} = 0b01;
  let Inst{19-16} = Pg;
  let Inst{15}    = 0b0;
  let Inst{14}    = m;
  let Inst{13}    = imm{8};   // sh
  let Inst{12-5}  = imm{7-0}; // imm8
  let Inst{4-0}   = Zd;

  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_dup_imm_pred_merge<string asm> {
  let Constraints = "$Zd = $_Zd" in {
  def _B : sve_int_dup_imm_pred<0b00, 1, asm, ZPR8,  "/m", (ins ZPR8:$_Zd,  PPRAny:$Pg, cpy_imm8_opt_lsl_i8:$imm)>;
  def _H : sve_int_dup_imm_pred<0b01, 1, asm, ZPR16, "/m", (ins ZPR16:$_Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i16:$imm)>;
  def _S : sve_int_dup_imm_pred<0b10, 1, asm, ZPR32, "/m", (ins ZPR32:$_Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i32:$imm)>;
  def _D : sve_int_dup_imm_pred<0b11, 1, asm, ZPR64, "/m", (ins ZPR64:$_Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i64:$imm)>;
  }

  def : InstAlias<"mov $Zd, $Pg/m, $imm",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd,  PPRAny:$Pg, cpy_imm8_opt_lsl_i8:$imm), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $imm",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i16:$imm), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $imm",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i32:$imm), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $imm",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i64:$imm), 1>;

  def : InstAlias<"fmov $Zd, $Pg/m, #0.0",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, PPRAny:$Pg, 0, 0), 0>;
  def : InstAlias<"fmov $Zd, $Pg/m, #0.0",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, PPRAny:$Pg, 0, 0), 0>;
  def : InstAlias<"fmov $Zd, $Pg/m, #0.0",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, PPRAny:$Pg, 0, 0), 0>;
}

multiclass sve_int_dup_imm_pred_zero<string asm> {
  def _B : sve_int_dup_imm_pred<0b00, 0, asm, ZPR8,  "/z", (ins PPRAny:$Pg, cpy_imm8_opt_lsl_i8:$imm)>;
  def _H : sve_int_dup_imm_pred<0b01, 0, asm, ZPR16, "/z", (ins PPRAny:$Pg, cpy_imm8_opt_lsl_i16:$imm)>;
  def _S : sve_int_dup_imm_pred<0b10, 0, asm, ZPR32, "/z", (ins PPRAny:$Pg, cpy_imm8_opt_lsl_i32:$imm)>;
  def _D : sve_int_dup_imm_pred<0b11, 0, asm, ZPR64, "/z", (ins PPRAny:$Pg, cpy_imm8_opt_lsl_i64:$imm)>;

  def : InstAlias<"mov $Zd, $Pg/z, $imm",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd,  PPRAny:$Pg, cpy_imm8_opt_lsl_i8:$imm), 1>;
  def : InstAlias<"mov $Zd, $Pg/z, $imm",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i16:$imm), 1>;
  def : InstAlias<"mov $Zd, $Pg/z, $imm",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i32:$imm), 1>;
  def : InstAlias<"mov $Zd, $Pg/z, $imm",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, PPRAny:$Pg, cpy_imm8_opt_lsl_i64:$imm), 1>;
}

//===----------------------------------------------------------------------===//
// SVE Integer Compare - Vectors Group
//===----------------------------------------------------------------------===//

class sve_int_cmp<bit cmp_1, bits<2> sz8_64, bits<3> opc, string asm,
                  PPRRegOp pprty, ZPRRegOp zprty1, ZPRRegOp zprty2>
: I<(outs pprty:$Pd), (ins PPR3bAny:$Pg, zprty1:$Zn, zprty2:$Zm),
  asm, "\t$Pd, $Pg/z, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<3> Pg;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00100100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b0;
  let Inst{20-16} = Zm;
  let Inst{15}    = opc{2};
  let Inst{14}    = cmp_1;
  let Inst{13}    = opc{1};
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;

  let Defs = [NZCV];
}

multiclass sve_int_cmp_0<bits<3> opc, string asm> {
  def _B : sve_int_cmp<0b0, 0b00, opc, asm, PPR8, ZPR8, ZPR8>;
  def _H : sve_int_cmp<0b0, 0b01, opc, asm, PPR16, ZPR16, ZPR16>;
  def _S : sve_int_cmp<0b0, 0b10, opc, asm, PPR32, ZPR32, ZPR32>;
  def _D : sve_int_cmp<0b0, 0b11, opc, asm, PPR64, ZPR64, ZPR64>;
}

multiclass sve_int_cmp_0_wide<bits<3> opc, string asm> {
  def _B : sve_int_cmp<0b0, 0b00, opc, asm, PPR8, ZPR8, ZPR64>;
  def _H : sve_int_cmp<0b0, 0b01, opc, asm, PPR16, ZPR16, ZPR64>;
  def _S : sve_int_cmp<0b0, 0b10, opc, asm, PPR32, ZPR32, ZPR64>;
}

multiclass sve_int_cmp_1_wide<bits<3> opc, string asm> {
  def _B : sve_int_cmp<0b1, 0b00, opc, asm, PPR8, ZPR8, ZPR64>;
  def _H : sve_int_cmp<0b1, 0b01, opc, asm, PPR16, ZPR16, ZPR64>;
  def _S : sve_int_cmp<0b1, 0b10, opc, asm, PPR32, ZPR32, ZPR64>;
}


//===----------------------------------------------------------------------===//
// SVE Integer Compare - Signed Immediate Group
//===----------------------------------------------------------------------===//

class sve_int_scmp_vi<bits<2> sz8_64, bits<3> opc, string asm, PPRRegOp pprty,
                      ZPRRegOp zprty,
                      Operand immtype>
: I<(outs pprty:$Pd), (ins PPR3bAny:$Pg, zprty:$Zn, immtype:$imm5),
  asm, "\t$Pd, $Pg/z, $Zn, $imm5",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<3> Pg;
  bits<5> Zn;
  bits<5> imm5;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b0;
  let Inst{20-16} = imm5;
  let Inst{15}    = opc{2};
  let Inst{14}    = 0b0;
  let Inst{13}    = opc{1};
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;

  let Defs = [NZCV];
}

multiclass sve_int_scmp_vi<bits<3> opc, string asm> {
  def _B : sve_int_scmp_vi<0b00, opc, asm, PPR8, ZPR8, simm5_32b>;
  def _H : sve_int_scmp_vi<0b01, opc, asm, PPR16, ZPR16, simm5_32b>;
  def _S : sve_int_scmp_vi<0b10, opc, asm, PPR32, ZPR32, simm5_32b>;
  def _D : sve_int_scmp_vi<0b11, opc, asm, PPR64, ZPR64, simm5_64b>;
}


//===----------------------------------------------------------------------===//
// SVE Integer Compare - Unsigned Immediate Group
//===----------------------------------------------------------------------===//

class sve_int_ucmp_vi<bits<2> sz8_64, bits<2> opc, string asm, PPRRegOp pprty,
                      ZPRRegOp zprty, Operand immtype>
: I<(outs pprty:$Pd), (ins PPR3bAny:$Pg, zprty:$Zn, immtype:$imm7),
  asm, "\t$Pd, $Pg/z, $Zn, $imm7",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<3> Pg;
  bits<5> Zn;
  bits<7> imm7;
  let Inst{31-24} = 0b00100100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 1;
  let Inst{20-14} = imm7;
  let Inst{13}    = opc{1};
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;

  let Defs = [NZCV];
}

multiclass sve_int_ucmp_vi<bits<2> opc, string asm> {
  def _B : sve_int_ucmp_vi<0b00, opc, asm, PPR8, ZPR8, imm0_127>;
  def _H : sve_int_ucmp_vi<0b01, opc, asm, PPR16, ZPR16, imm0_127>;
  def _S : sve_int_ucmp_vi<0b10, opc, asm, PPR32, ZPR32, imm0_127>;
  def _D : sve_int_ucmp_vi<0b11, opc, asm, PPR64, ZPR64, imm0_127>;
}


//===----------------------------------------------------------------------===//
// SVE Integer Compare - Scalars Group
//===----------------------------------------------------------------------===//

class sve_int_cterm<bit sz, bit opc, string asm, RegisterClass rt>
: I<(outs), (ins rt:$Rn, rt:$Rm),
  asm, "\t$Rn, $Rm",
  "",
  []>, Sched<[]> {
  bits<5> Rm;
  bits<5> Rn;
  let Inst{31-23} = 0b001001011;
  let Inst{22}    = sz;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Rm;
  let Inst{15-10} = 0b001000;
  let Inst{9-5}   = Rn;
  let Inst{4}     = opc;
  let Inst{3-0}   = 0b0000;

  let Defs = [NZCV];
}

class sve_int_while_rr<bits<2> sz8_64, bits<4> opc, string asm,
                       RegisterClass gprty, PPRRegOp pprty>
: I<(outs pprty:$Pd), (ins gprty:$Rn, gprty:$Rm),
  asm, "\t$Pd, $Rn, $Rm",
  "", []>, Sched<[]> {
  bits<4> Pd;
  bits<5> Rm;
  bits<5> Rn;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Rm;
  let Inst{15-13} = 0b000;
  let Inst{12-10} = opc{3-1};
  let Inst{9-5}   = Rn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;

  let Defs = [NZCV];
}

multiclass sve_int_while4_rr<bits<3> opc, string asm> {
  def _B : sve_int_while_rr<0b00, { 0, opc }, asm, GPR32, PPR8>;
  def _H : sve_int_while_rr<0b01, { 0, opc }, asm, GPR32, PPR16>;
  def _S : sve_int_while_rr<0b10, { 0, opc }, asm, GPR32, PPR32>;
  def _D : sve_int_while_rr<0b11, { 0, opc }, asm, GPR32, PPR64>;
}

multiclass sve_int_while8_rr<bits<3> opc, string asm> {
  def _B : sve_int_while_rr<0b00, { 1, opc }, asm, GPR64, PPR8>;
  def _H : sve_int_while_rr<0b01, { 1, opc }, asm, GPR64, PPR16>;
  def _S : sve_int_while_rr<0b10, { 1, opc }, asm, GPR64, PPR32>;
  def _D : sve_int_while_rr<0b11, { 1, opc }, asm, GPR64, PPR64>;
}


//===----------------------------------------------------------------------===//
// SVE Floating Point Fast Reduction Group
//===----------------------------------------------------------------------===//

class sve_fp_fast_red<bits<2> sz, bits<3> opc, string asm,
                      ZPRRegOp zprty, RegisterClass dstRegClass>
: I<(outs dstRegClass:$Vd), (ins PPR3bAny:$Pg, zprty:$Zn),
  asm, "\t$Vd, $Pg, $Zn",
  "",
  []>, Sched<[]> {
  bits<5> Zn;
  bits<5> Vd;
  bits<3> Pg;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21-19} = 0b000;
  let Inst{18-16} = opc;
  let Inst{15-13} = 0b001;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Vd;
}

multiclass sve_fp_fast_red<bits<3> opc, string asm> {
  def _H : sve_fp_fast_red<0b01, opc, asm, ZPR16, FPR16>;
  def _S : sve_fp_fast_red<0b10, opc, asm, ZPR32, FPR32>;
  def _D : sve_fp_fast_red<0b11, opc, asm, ZPR64, FPR64>;
}


//===----------------------------------------------------------------------===//
// SVE Floating Point Accumulating Reduction Group
//===----------------------------------------------------------------------===//

class sve_fp_2op_p_vd<bits<2> sz, bits<3> opc, string asm,
                      ZPRRegOp zprty, RegisterClass dstRegClass>
: I<(outs dstRegClass:$Vdn), (ins PPR3bAny:$Pg, dstRegClass:$_Vdn, zprty:$Zm),
  asm, "\t$Vdn, $Pg, $_Vdn, $Zm",
  "",
  []>,
  Sched<[]> {
  bits<3> Pg;
  bits<5> Vdn;
  bits<5> Zm;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21-19} = 0b011;
  let Inst{18-16} = opc;
  let Inst{15-13} = 0b001;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Vdn;

  let Constraints = "$Vdn = $_Vdn";
}

multiclass sve_fp_2op_p_vd<bits<3> opc, string asm> {
  def _H : sve_fp_2op_p_vd<0b01, opc, asm, ZPR16, FPR16>;
  def _S : sve_fp_2op_p_vd<0b10, opc, asm, ZPR32, FPR32>;
  def _D : sve_fp_2op_p_vd<0b11, opc, asm, ZPR64, FPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Floating Point Compare - Vectors Group
//===----------------------------------------------------------------------===//

class sve_fp_3op_p_pd<bits<2> sz, bits<3> opc, string asm, PPRRegOp pprty,
                      ZPRRegOp zprty>
: I<(outs pprty:$Pd), (ins PPR3bAny:$Pg, zprty:$Zn, zprty:$Zm),
  asm, "\t$Pd, $Pg/z, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<3> Pg;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b0;
  let Inst{20-16} = Zm;
  let Inst{15}    = opc{2};
  let Inst{14}    = 0b1;
  let Inst{13}    = opc{1};
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;
}

multiclass sve_fp_3op_p_pd<bits<3> opc, string asm> {
  def _H : sve_fp_3op_p_pd<0b01, opc, asm, PPR16, ZPR16>;
  def _S : sve_fp_3op_p_pd<0b10, opc, asm, PPR32, ZPR32>;
  def _D : sve_fp_3op_p_pd<0b11, opc, asm, PPR64, ZPR64>;
}


//===----------------------------------------------------------------------===//
// SVE Floating Point Compare - with Zero Group
//===----------------------------------------------------------------------===//

class sve_fp_2op_p_pd<bits<2> sz, bits<3> opc, string asm, PPRRegOp pprty,
                      ZPRRegOp zprty>
: I<(outs pprty:$Pd), (ins PPR3bAny:$Pg, zprty:$Zn),
  asm, "\t$Pd, $Pg/z, $Zn, #0.0",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<3> Pg;
  bits<5> Zn;
  let Inst{31-24} = 0b01100101;
  let Inst{23-22} = sz;
  let Inst{21-18} = 0b0100;
  let Inst{17-16} = opc{2-1};
  let Inst{15-13} = 0b001;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;
}

multiclass sve_fp_2op_p_pd<bits<3> opc, string asm> {
  def _H : sve_fp_2op_p_pd<0b01, opc, asm, PPR16, ZPR16>;
  def _S : sve_fp_2op_p_pd<0b10, opc, asm, PPR32, ZPR32>;
  def _D : sve_fp_2op_p_pd<0b11, opc, asm, PPR64, ZPR64>;
}


//===----------------------------------------------------------------------===//
//SVE Index Generation Group
//===----------------------------------------------------------------------===//

class sve_int_index_ii<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                       Operand imm_ty>
: I<(outs zprty:$Zd), (ins imm_ty:$imm5, imm_ty:$imm5b),
  asm, "\t$Zd, $imm5, $imm5b",
  "", []>, Sched<[]> {
  bits<5> Zd;
  bits<5> imm5;
  bits<5> imm5b;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = imm5b;
  let Inst{15-10} = 0b010000;
  let Inst{9-5}   = imm5;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_index_ii<string asm> {
  def _B : sve_int_index_ii<0b00, asm, ZPR8, simm5_32b>;
  def _H : sve_int_index_ii<0b01, asm, ZPR16, simm5_32b>;
  def _S : sve_int_index_ii<0b10, asm, ZPR32, simm5_32b>;
  def _D : sve_int_index_ii<0b11, asm, ZPR64, simm5_64b>;
}

class sve_int_index_ir<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                       RegisterClass srcRegType, Operand imm_ty>
: I<(outs zprty:$Zd), (ins imm_ty:$imm5, srcRegType:$Rm),
  asm, "\t$Zd, $imm5, $Rm",
  "", []>, Sched<[]> {
  bits<5> Rm;
  bits<5> Zd;
  bits<5> imm5;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Rm;
  let Inst{15-10} = 0b010010;
  let Inst{9-5}   = imm5;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_index_ir<string asm> {
  def _B : sve_int_index_ir<0b00, asm, ZPR8, GPR32, simm5_32b>;
  def _H : sve_int_index_ir<0b01, asm, ZPR16, GPR32, simm5_32b>;
  def _S : sve_int_index_ir<0b10, asm, ZPR32, GPR32, simm5_32b>;
  def _D : sve_int_index_ir<0b11, asm, ZPR64, GPR64, simm5_64b>;
}

class sve_int_index_ri<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                       RegisterClass srcRegType, Operand imm_ty>
: I<(outs zprty:$Zd), (ins srcRegType:$Rn, imm_ty:$imm5),
  asm, "\t$Zd, $Rn, $imm5",
  "", []>, Sched<[]> {
  bits<5> Rn;
  bits<5> Zd;
  bits<5> imm5;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = imm5;
  let Inst{15-10} = 0b010001;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_index_ri<string asm> {
  def _B : sve_int_index_ri<0b00, asm, ZPR8, GPR32, simm5_32b>;
  def _H : sve_int_index_ri<0b01, asm, ZPR16, GPR32, simm5_32b>;
  def _S : sve_int_index_ri<0b10, asm, ZPR32, GPR32, simm5_32b>;
  def _D : sve_int_index_ri<0b11, asm, ZPR64, GPR64, simm5_64b>;
}

class sve_int_index_rr<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                       RegisterClass srcRegType>
: I<(outs zprty:$Zd), (ins srcRegType:$Rn, srcRegType:$Rm),
  asm, "\t$Zd, $Rn, $Rm",
  "", []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Rm;
  bits<5> Rn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Rm;
  let Inst{15-10} = 0b010011;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_index_rr<string asm> {
  def _B : sve_int_index_rr<0b00, asm, ZPR8, GPR32>;
  def _H : sve_int_index_rr<0b01, asm, ZPR16, GPR32>;
  def _S : sve_int_index_rr<0b10, asm, ZPR32, GPR32>;
  def _D : sve_int_index_rr<0b11, asm, ZPR64, GPR64>;
}
//
//===----------------------------------------------------------------------===//
// SVE Bitwise Shift - Predicated Group
//===----------------------------------------------------------------------===//
class sve_int_bin_pred_shift_imm<bits<4> tsz8_64, bits<3> opc, string asm,
                               ZPRRegOp zprty, Operand immtype,
                               ElementSizeEnum size>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, immtype:$imm),
  asm, "\t$Zdn, $Pg/m, $_Zdn, $imm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zdn;
  bits<6> imm;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = tsz8_64{3-2};
  let Inst{21-19} = 0b000;
  let Inst{18-16} = opc;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-8}   = tsz8_64{1-0};
  let Inst{7-5}   = imm{2-0}; // imm3
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = size;
}

multiclass sve_int_bin_pred_shift_imm_left<bits<3> opc, string asm> {
  def _B : sve_int_bin_pred_shift_imm<{0,0,0,1}, opc, asm, ZPR8, vecshiftL8,
                                      ElementSizeB>;
  def _H : sve_int_bin_pred_shift_imm<{0,0,1,?}, opc, asm, ZPR16, vecshiftL16,
                                      ElementSizeH> {
    let Inst{8} = imm{3};
  }
  def _S : sve_int_bin_pred_shift_imm<{0,1,?,?}, opc, asm, ZPR32, vecshiftL32,
                                      ElementSizeS> {
    let Inst{9-8} = imm{4-3};
  }
  def _D : sve_int_bin_pred_shift_imm<{1,?,?,?}, opc, asm, ZPR64, vecshiftL64,
                                      ElementSizeD> {
    let Inst{22}  = imm{5};
    let Inst{9-8} = imm{4-3};
  }
}

multiclass sve_int_bin_pred_shift_imm_right<bits<3> opc, string asm> {
  def _B : sve_int_bin_pred_shift_imm<{0,0,0,1}, opc, asm, ZPR8, vecshiftR8,
                                      ElementSizeB>;
  def _H : sve_int_bin_pred_shift_imm<{0,0,1,?}, opc, asm, ZPR16, vecshiftR16,
                                      ElementSizeH> {
    let Inst{8} = imm{3};
  }
  def _S : sve_int_bin_pred_shift_imm<{0,1,?,?}, opc, asm, ZPR32, vecshiftR32,
                                      ElementSizeS> {
    let Inst{9-8} = imm{4-3};
  }
  def _D : sve_int_bin_pred_shift_imm<{1,?,?,?}, opc, asm, ZPR64, vecshiftR64,
                                      ElementSizeD> {
    let Inst{22}  = imm{5};
    let Inst{9-8} = imm{4-3};
  }
}

class sve_int_bin_pred_shift<bits<2> sz8_64, bit wide, bits<3> opc,
                             string asm, ZPRRegOp zprty, ZPRRegOp zprty2>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, zprty2:$Zm),
  asm, "\t$Zdn, $Pg/m, $_Zdn, $Zm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zdn;
  bits<5> Zm;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21-20} = 0b01;
  let Inst{19}    = wide;
  let Inst{18-16} = opc;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_bin_pred_shift<bits<3> opc, string asm> {
  def _B : sve_int_bin_pred_shift<0b00, 0b0, opc, asm, ZPR8, ZPR8>;
  def _H : sve_int_bin_pred_shift<0b01, 0b0, opc, asm, ZPR16, ZPR16>;
  def _S : sve_int_bin_pred_shift<0b10, 0b0, opc, asm, ZPR32, ZPR32>;
  def _D : sve_int_bin_pred_shift<0b11, 0b0, opc, asm, ZPR64, ZPR64>;
}

multiclass sve_int_bin_pred_shift_wide<bits<3> opc, string asm> {
  def _B : sve_int_bin_pred_shift<0b00, 0b1, opc, asm, ZPR8, ZPR64>;
  def _H : sve_int_bin_pred_shift<0b01, 0b1, opc, asm, ZPR16, ZPR64>;
  def _S : sve_int_bin_pred_shift<0b10, 0b1, opc, asm, ZPR32, ZPR64>;
}

//===----------------------------------------------------------------------===//
// SVE Shift - Unpredicated Group
//===----------------------------------------------------------------------===//

class sve_int_bin_cons_shift_wide<bits<2> sz8_64, bits<2> opc, string asm,
                               ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$Zn, ZPR64:$Zm),
  asm, "\t$Zd, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_64;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-12} = 0b1000;
  let Inst{11-10} = opc;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_bin_cons_shift_wide<bits<2> opc, string asm> {
  def _B : sve_int_bin_cons_shift_wide<0b00, opc, asm, ZPR8>;
  def _H : sve_int_bin_cons_shift_wide<0b01, opc, asm, ZPR16>;
  def _S : sve_int_bin_cons_shift_wide<0b10, opc, asm, ZPR32>;
}

class sve_int_bin_cons_shift_imm<bits<4> tsz8_64, bits<2> opc, string asm,
                               ZPRRegOp zprty, Operand immtype>
: I<(outs zprty:$Zd), (ins zprty:$Zn, immtype:$imm),
  asm, "\t$Zd, $Zn, $imm",
  "", []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zn;
  bits<6> imm;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = tsz8_64{3-2};
  let Inst{21}    = 0b1;
  let Inst{20-19} = tsz8_64{1-0};
  let Inst{18-16} = imm{2-0}; // imm3
  let Inst{15-12} = 0b1001;
  let Inst{11-10} = opc;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_bin_cons_shift_imm_left<bits<2> opc, string asm> {
  def _B : sve_int_bin_cons_shift_imm<{0,0,0,1}, opc, asm, ZPR8, vecshiftL8>;
  def _H : sve_int_bin_cons_shift_imm<{0,0,1,?}, opc, asm, ZPR16, vecshiftL16> {
    let Inst{19} = imm{3};
  }
  def _S : sve_int_bin_cons_shift_imm<{0,1,?,?}, opc, asm, ZPR32, vecshiftL32> {
    let Inst{20-19} = imm{4-3};
  }
  def _D : sve_int_bin_cons_shift_imm<{1,?,?,?}, opc, asm, ZPR64, vecshiftL64> {
    let Inst{22}    = imm{5};
    let Inst{20-19} = imm{4-3};
  }
}

multiclass sve_int_bin_cons_shift_imm_right<bits<2> opc, string asm> {
  def _B : sve_int_bin_cons_shift_imm<{0,0,0,1}, opc, asm, ZPR8, vecshiftR8>;
  def _H : sve_int_bin_cons_shift_imm<{0,0,1,?}, opc, asm, ZPR16, vecshiftR16> {
    let Inst{19} = imm{3};
  }
  def _S : sve_int_bin_cons_shift_imm<{0,1,?,?}, opc, asm, ZPR32, vecshiftR32> {
    let Inst{20-19} = imm{4-3};
  }
  def _D : sve_int_bin_cons_shift_imm<{1,?,?,?}, opc, asm, ZPR64, vecshiftR64> {
    let Inst{22}    = imm{5};
    let Inst{20-19} = imm{4-3};
  }
}
//===----------------------------------------------------------------------===//
// SVE Memory - Store Group
//===----------------------------------------------------------------------===//

class sve_mem_cst_si<bits<2> msz, bits<2> esz, string asm,
                     RegisterOperand VecList>
: I<(outs), (ins VecList:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, simm4s1:$imm4),
  asm, "\t$Zt, $Pg, [$Rn, $imm4, mul vl]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zt;
  bits<4> imm4;
  let Inst{31-25} = 0b1110010;
  let Inst{24-23} = msz;
  let Inst{22-21} = esz;
  let Inst{20}    = 0;
  let Inst{19-16} = imm4;
  let Inst{15-13} = 0b111;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_cst_si<bits<2> msz, bits<2> esz, string asm,
                          RegisterOperand listty, ZPRRegOp zprty>
{
  def NAME : sve_mem_cst_si<msz, esz, asm, listty>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $imm4, mul vl]",
                 (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, simm4s1:$imm4), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn]",
                  (!cast<Instruction>(NAME) listty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_est_si<bits<2> sz, bits<2> nregs, RegisterOperand VecList,
                     string asm, Operand immtype>
: I<(outs), (ins VecList:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, immtype:$imm4),
  asm, "\t$Zt, $Pg, [$Rn, $imm4, mul vl]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zt;
  bits<4> imm4;
  let Inst{31-25} = 0b1110010;
  let Inst{24-23} = sz;
  let Inst{22-21} = nregs;
  let Inst{20}    = 1;
  let Inst{19-16} = imm4;
  let Inst{15-13} = 0b111;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_est_si<bits<2> sz, bits<2> nregs, RegisterOperand VecList,
                          string asm, Operand immtype> {
  def NAME : sve_mem_est_si<sz, nregs, VecList, asm, immtype>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn]",
                  (!cast<Instruction>(NAME) VecList:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_est_ss<bits<2> sz, bits<2> nregs, RegisterOperand VecList,
                     string asm, RegisterOperand gprty>
: I<(outs), (ins VecList:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
  asm, "\t$Zt, $Pg, [$Rn, $Rm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rm;
  bits<5> Rn;
  bits<5> Zt;
  let Inst{31-25} = 0b1110010;
  let Inst{24-23} = sz;
  let Inst{22-21} = nregs;
  let Inst{20-16} = Rm;
  let Inst{15-13} = 0b011;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

class sve_mem_cst_ss_base<bits<4> dtype, string asm,
                          RegisterOperand listty, RegisterOperand gprty>
: I<(outs), (ins listty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
  asm, "\t$Zt, $Pg, [$Rn, $Rm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rm;
  bits<5> Rn;
  bits<5> Zt;
  let Inst{31-25} = 0b1110010;
  let Inst{24-21} = dtype;
  let Inst{20-16} = Rm;
  let Inst{15-13} = 0b010;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_cst_ss<bits<4> dtype, string asm,
                          RegisterOperand listty, ZPRRegOp zprty,
                          RegisterOperand gprty> {
  def NAME : sve_mem_cst_ss_base<dtype, asm, listty, gprty>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $Rm]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm), 0>;
}

class sve_mem_cstnt_si<bits<2> msz, string asm, RegisterOperand VecList>
: I<(outs), (ins VecList:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, simm4s1:$imm4),
  asm, "\t$Zt, $Pg, [$Rn, $imm4, mul vl]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zt;
  bits<4> imm4;
  let Inst{31-25} = 0b1110010;
  let Inst{24-23} = msz;
  let Inst{22-20} = 0b001;
  let Inst{19-16} = imm4;
  let Inst{15-13} = 0b111;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_cstnt_si<bits<2> msz, string asm, RegisterOperand listty,
                            ZPRRegOp zprty> {
  def NAME : sve_mem_cstnt_si<msz, asm, listty>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $imm4, mul vl]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, simm4s1:$imm4), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn]",
                  (!cast<Instruction>(NAME) listty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_cstnt_ss_base<bits<2> msz, string asm, RegisterOperand listty,
                            RegisterOperand gprty>
: I<(outs), (ins listty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
  asm, "\t$Zt, $Pg, [$Rn, $Rm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rm;
  bits<5> Rn;
  bits<5> Zt;
  let Inst{31-25} = 0b1110010;
  let Inst{24-23} = msz;
  let Inst{22-21} = 0b00;
  let Inst{20-16} = Rm;
  let Inst{15-13} = 0b011;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_cstnt_ss<bits<2> msz, string asm, RegisterOperand listty,
                            ZPRRegOp zprty, RegisterOperand gprty> {
  def NAME : sve_mem_cstnt_ss_base<msz, asm, listty, gprty>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $Rm]",
                 (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm), 0>;
}

class sve_mem_sst_sv<bits<3> opc, bit xs, bit scaled, string asm,
                     RegisterOperand VecList, RegisterOperand zprext>
: I<(outs), (ins VecList:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, zprext:$Zm),
  asm, "\t$Zt, $Pg, [$Rn, $Zm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zm;
  bits<5> Zt;
  let Inst{31-25} = 0b1110010;
  let Inst{24-22} = opc;
  let Inst{21}    = scaled;
  let Inst{20-16} = Zm;
  let Inst{15}    = 0b1;
  let Inst{14}    = xs;
  let Inst{13}    = 0;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_sst_sv_32_scaled<bits<3> opc, string asm,
                                    RegisterOperand listty,
                                    ZPRRegOp zprty,
                                    RegisterOperand sxtw_opnd,
                                    RegisterOperand uxtw_opnd > {
  def _UXTW_SCALED : sve_mem_sst_sv<opc, 0, 1, asm, listty, uxtw_opnd>;
  def _SXTW_SCALED : sve_mem_sst_sv<opc, 1, 1, asm, listty, sxtw_opnd>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $Zm]",
                 (!cast<Instruction>(NAME # _UXTW_SCALED) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, uxtw_opnd:$Zm), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $Zm]",
                 (!cast<Instruction>(NAME # _SXTW_SCALED) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, sxtw_opnd:$Zm), 0>;
}

multiclass sve_mem_sst_sv_32_unscaled<bits<3> opc, string asm,
                                      RegisterOperand listty,
                                      ZPRRegOp zprty,
                                      RegisterOperand sxtw_opnd,
                                      RegisterOperand uxtw_opnd> {
  def _UXTW : sve_mem_sst_sv<opc, 0, 0, asm, listty, uxtw_opnd>;
  def _SXTW : sve_mem_sst_sv<opc, 1, 0, asm, listty, sxtw_opnd>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $Zm]",
                 (!cast<Instruction>(NAME # _UXTW) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, uxtw_opnd:$Zm), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $Zm]",
                 (!cast<Instruction>(NAME # _SXTW) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, sxtw_opnd:$Zm), 0>;
}

class sve_mem_sst_sv2<bits<2> msz, bit scaled, string asm,
                      RegisterOperand zprext>
: I<(outs), (ins Z_d:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, zprext:$Zm),
  asm, "\t$Zt, $Pg, [$Rn, $Zm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zm;
  bits<5> Zt;
  let Inst{31-25} = 0b1110010;
  let Inst{24-23} = msz;
  let Inst{22}    = 0b0;
  let Inst{21}    = scaled;
  let Inst{20-16} = Zm;
  let Inst{15-13} = 0b101;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_sst_sv_64_scaled<bits<2> msz, string asm,
                                    RegisterOperand zprext> {
  def "" : sve_mem_sst_sv2<msz, 1, asm, zprext>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $Zm]",
                 (!cast<Instruction>(NAME) ZPR64:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, zprext:$Zm), 0>;

}

multiclass sve_mem_sst_sv_64_unscaled<bits<2> msz, string asm> {
  def "" : sve_mem_sst_sv2<msz, 0, asm, ZPR64ExtLSL8>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Rn, $Zm]",
                 (!cast<Instruction>(NAME) ZPR64:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, ZPR64ExtLSL8:$Zm), 0>;
}

class sve_mem_sst_vi<bits<3> opc, string asm, ZPRRegOp zprty,
                     RegisterOperand VecList, Operand imm_ty>
: I<(outs), (ins VecList:$Zt, PPR3bAny:$Pg, zprty:$Zn, imm_ty:$imm5),
  asm, "\t$Zt, $Pg, [$Zn, $imm5]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> imm5;
  bits<5> Zn;
  bits<5> Zt;
  let Inst{31-25} = 0b1110010;
  let Inst{24-23} = opc{2-1};
  let Inst{22}    = 0b1;
  let Inst{21}    = opc{0};
  let Inst{20-16} = imm5;
  let Inst{15-13} = 0b101;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_sst_vi_ptrs<bits<3> opc, string asm, RegisterOperand listty,
                               ZPRRegOp zprty, Operand imm_ty> {
  def _IMM : sve_mem_sst_vi<opc, asm, zprty, listty, imm_ty>;

  def : InstAlias<asm # "\t$Zt, $Pg, [$Zn]",
                  (!cast<Instruction>(NAME # _IMM) zprty:$Zt, PPR3bAny:$Pg, zprty:$Zn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg, [$Zn, $imm5]",
                  (!cast<Instruction>(NAME # _IMM) zprty:$Zt, PPR3bAny:$Pg, zprty:$Zn, imm_ty:$imm5), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg, [$Zn]",
                  (!cast<Instruction>(NAME # _IMM) listty:$Zt, PPR3bAny:$Pg, zprty:$Zn, 0), 1>;
}

class sve_mem_z_spill<string asm>
: I<(outs), (ins ZPRAny:$Zt, GPR64sp:$Rn, simm9:$imm9),
  asm, "\t$Zt, [$Rn, $imm9, mul vl]",
  "",
  []>, Sched<[]> {
  bits<5> Rn;
  bits<5> Zt;
  bits<9> imm9;
  let Inst{31-22} = 0b1110010110;
  let Inst{21-16} = imm9{8-3};
  let Inst{15-13} = 0b010;
  let Inst{12-10} = imm9{2-0};
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayStore = 1;
}

multiclass sve_mem_z_spill<string asm> {
  def NAME : sve_mem_z_spill<asm>;

  def : InstAlias<asm # "\t$Zt, [$Rn]",
                  (!cast<Instruction>(NAME) ZPRAny:$Zt, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_p_spill<string asm>
: I<(outs), (ins PPRAny:$Pt, GPR64sp:$Rn, simm9:$imm9),
  asm, "\t$Pt, [$Rn, $imm9, mul vl]",
  "",
  []>, Sched<[]> {
  bits<4> Pt;
  bits<5> Rn;
  bits<9> imm9;
  let Inst{31-22} = 0b1110010110;
  let Inst{21-16} = imm9{8-3};
  let Inst{15-13} = 0b000;
  let Inst{12-10} = imm9{2-0};
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = Pt;

  let mayStore = 1;
}

multiclass sve_mem_p_spill<string asm> {
  def NAME : sve_mem_p_spill<asm>;

  def : InstAlias<asm # "\t$Pt, [$Rn]",
                  (!cast<Instruction>(NAME) PPRAny:$Pt, GPR64sp:$Rn, 0), 1>;
}

//===----------------------------------------------------------------------===//
// SVE Permute - Predicates Group
//===----------------------------------------------------------------------===//

class sve_int_perm_bin_perm_pp<bits<3> opc, bits<2> sz8_64, string asm,
                               PPRRegOp pprty>
: I<(outs pprty:$Pd), (ins pprty:$Pn, pprty:$Pm),
  asm, "\t$Pd, $Pn, $Pm",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<4> Pm;
  bits<4> Pn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-20} = 0b10;
  let Inst{19-16} = Pm;
  let Inst{15-13} = 0b010;
  let Inst{12-10} = opc;
  let Inst{9}     = 0b0;
  let Inst{8-5}   = Pn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = Pd;
}

multiclass sve_int_perm_bin_perm_pp<bits<3> opc, string asm> {
  def _B : sve_int_perm_bin_perm_pp<opc, 0b00, asm, PPR8>;
  def _H : sve_int_perm_bin_perm_pp<opc, 0b01, asm, PPR16>;
  def _S : sve_int_perm_bin_perm_pp<opc, 0b10, asm, PPR32>;
  def _D : sve_int_perm_bin_perm_pp<opc, 0b11, asm, PPR64>;
}

class sve_int_perm_punpk<bit opc, string asm>
: I<(outs PPR16:$Pd), (ins PPR8:$Pn),
  asm, "\t$Pd, $Pn",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<4> Pn;
  let Inst{31-17} = 0b000001010011000;
  let Inst{16}    = opc;
  let Inst{15-9}  = 0b0100000;
  let Inst{8-5}   = Pn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = Pd;
}

class sve_int_rdffr_pred<bit s, string asm>
: I<(outs PPR8:$Pd), (ins PPRAny:$Pg),
  asm, "\t$Pd, $Pg/z",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<4> Pg;
  let Inst{31-23} = 0b001001010;
  let Inst{22}    = s;
  let Inst{21-9}  = 0b0110001111000;
  let Inst{8-5}   = Pg;
  let Inst{4}     = 0;
  let Inst{3-0}   = Pd;

  let Defs = !if(!eq (s, 1), [NZCV], []);
  let Uses = [FFR];
}

class sve_int_rdffr_unpred<string asm> : I<
  (outs PPR8:$Pd), (ins),
  asm, "\t$Pd",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  let Inst{31-4} = 0b0010010100011001111100000000;
  let Inst{3-0}   = Pd;

  let Uses = [FFR];
}

class sve_int_wrffr<string asm>
: I<(outs), (ins PPR8:$Pn),
  asm, "\t$Pn",
  "",
  []>, Sched<[]> {
  bits<4> Pn;
  let Inst{31-9} = 0b00100101001010001001000;
  let Inst{8-5}  = Pn;
  let Inst{4-0}  = 0b00000;

  let hasSideEffects = 1;
  let Defs = [FFR];
}

class sve_int_setffr<string asm>
: I<(outs), (ins),
  asm, "",
  "",
  []>, Sched<[]> {
  let Inst{31-0} = 0b00100101001011001001000000000000;

  let hasSideEffects = 1;
  let Defs = [FFR];
}

//===----------------------------------------------------------------------===//
// SVE Permute Vector - Predicated Group
//===----------------------------------------------------------------------===//

class sve_int_perm_clast_rz<bits<2> sz8_64, bit ab, string asm,
                            ZPRRegOp zprty, RegisterClass rt>
: I<(outs rt:$Rdn), (ins PPR3bAny:$Pg, rt:$_Rdn, zprty:$Zm),
  asm, "\t$Rdn, $Pg, $_Rdn, $Zm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rdn;
  bits<5> Zm;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-17} = 0b11000;
  let Inst{16}    = ab;
  let Inst{15-13} = 0b101;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Rdn;

  let Constraints = "$Rdn = $_Rdn";
}

multiclass sve_int_perm_clast_rz<bit ab, string asm> {
  def _B : sve_int_perm_clast_rz<0b00, ab, asm, ZPR8, GPR32>;
  def _H : sve_int_perm_clast_rz<0b01, ab, asm, ZPR16, GPR32>;
  def _S : sve_int_perm_clast_rz<0b10, ab, asm, ZPR32, GPR32>;
  def _D : sve_int_perm_clast_rz<0b11, ab, asm, ZPR64, GPR64>;
}

class sve_int_perm_clast_vz<bits<2> sz8_64, bit ab, string asm,
                            ZPRRegOp zprty, RegisterClass rt>
: I<(outs rt:$Vdn), (ins PPR3bAny:$Pg, rt:$_Vdn, zprty:$Zm),
  asm, "\t$Vdn, $Pg, $_Vdn, $Zm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Vdn;
  bits<5> Zm;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-17} = 0b10101;
  let Inst{16}    = ab;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Vdn;

  let Constraints = "$Vdn = $_Vdn";
}

multiclass sve_int_perm_clast_vz<bit ab, string asm> {
  def _B : sve_int_perm_clast_vz<0b00, ab, asm, ZPR8, FPR8>;
  def _H : sve_int_perm_clast_vz<0b01, ab, asm, ZPR16, FPR16>;
  def _S : sve_int_perm_clast_vz<0b10, ab, asm, ZPR32, FPR32>;
  def _D : sve_int_perm_clast_vz<0b11, ab, asm, ZPR64, FPR64>;
}

class sve_int_perm_clast_zz<bits<2> sz8_64, bit ab, string asm,
                            ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, zprty:$Zm),
  asm, "\t$Zdn, $Pg, $_Zdn, $Zm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zdn;
  bits<5> Zm;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-17} = 0b10100;
  let Inst{16}    = ab;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_perm_clast_zz<bit ab, string asm> {
  def _B : sve_int_perm_clast_zz<0b00, ab, asm, ZPR8>;
  def _H : sve_int_perm_clast_zz<0b01, ab, asm, ZPR16>;
  def _S : sve_int_perm_clast_zz<0b10, ab, asm, ZPR32>;
  def _D : sve_int_perm_clast_zz<0b11, ab, asm, ZPR64>;
}

class sve_int_perm_last_r<bits<2> sz8_64, bit ab, string asm,
                          ZPRRegOp zprty, RegisterClass resultRegType>
: I<(outs resultRegType:$Rd), (ins PPR3bAny:$Pg, zprty:$Zn),
  asm, "\t$Rd, $Pg, $Zn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rd;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-17} = 0b10000;
  let Inst{16}    = ab;
  let Inst{15-13} = 0b101;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Rd;
}

multiclass sve_int_perm_last_r<bit ab, string asm> {
  def _B : sve_int_perm_last_r<0b00, ab, asm, ZPR8, GPR32>;
  def _H : sve_int_perm_last_r<0b01, ab, asm, ZPR16, GPR32>;
  def _S : sve_int_perm_last_r<0b10, ab, asm, ZPR32, GPR32>;
  def _D : sve_int_perm_last_r<0b11, ab, asm, ZPR64, GPR64>;
}

class sve_int_perm_last_v<bits<2> sz8_64, bit ab, string asm,
                          ZPRRegOp zprty, RegisterClass dstRegtype>
: I<(outs dstRegtype:$Vd), (ins PPR3bAny:$Pg, zprty:$Zn),
  asm, "\t$Vd, $Pg, $Zn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Vd;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-17} = 0b10001;
  let Inst{16}    = ab;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Vd;
}

multiclass sve_int_perm_last_v<bit ab, string asm> {
  def _B : sve_int_perm_last_v<0b00, ab, asm, ZPR8, FPR8>;
  def _H : sve_int_perm_last_v<0b01, ab, asm, ZPR16, FPR16>;
  def _S : sve_int_perm_last_v<0b10, ab, asm, ZPR32, FPR32>;
  def _D : sve_int_perm_last_v<0b11, ab, asm, ZPR64, FPR64>;
}

class sve_int_perm_splice<bits<2> sz8_64, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zdn), (ins PPR3bAny:$Pg, zprty:$_Zdn, zprty:$Zm),
  asm, "\t$Zdn, $Pg, $_Zdn, $Zm",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zdn;
  bits<5> Zm;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-13} = 0b101100100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zm;
  let Inst{4-0}   = Zdn;

  let Constraints = "$Zdn = $_Zdn";
  let DestructiveInstType = Destructive;
  let ElementSize = ElementSizeNone;
}

multiclass sve_int_perm_splice<string asm> {
  def _B : sve_int_perm_splice<0b00, asm, ZPR8>;
  def _H : sve_int_perm_splice<0b01, asm, ZPR16>;
  def _S : sve_int_perm_splice<0b10, asm, ZPR32>;
  def _D : sve_int_perm_splice<0b11, asm, ZPR64>;
}

class sve_int_perm_rev<bits<2> sz8_64, bits<2> opc, string asm,
                       ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$_Zd, PPR3bAny:$Pg, zprty:$Zn),
  asm, "\t$Zd, $Pg/m, $Zn",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<3> Pg;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-18} = 0b1001;
  let Inst{17-16} = opc;
  let Inst{15-13} = 0b100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;

  let Constraints = "$Zd = $_Zd";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_perm_rev_rbit<string asm> {
  def _B : sve_int_perm_rev<0b00, 0b11, asm, ZPR8>;
  def _H : sve_int_perm_rev<0b01, 0b11, asm, ZPR16>;
  def _S : sve_int_perm_rev<0b10, 0b11, asm, ZPR32>;
  def _D : sve_int_perm_rev<0b11, 0b11, asm, ZPR64>;
}

multiclass sve_int_perm_rev_revb<string asm> {
  def _H : sve_int_perm_rev<0b01, 0b00, asm, ZPR16>;
  def _S : sve_int_perm_rev<0b10, 0b00, asm, ZPR32>;
  def _D : sve_int_perm_rev<0b11, 0b00, asm, ZPR64>;
}

multiclass sve_int_perm_rev_revh<string asm> {
  def _S : sve_int_perm_rev<0b10, 0b01, asm, ZPR32>;
  def _D : sve_int_perm_rev<0b11, 0b01, asm, ZPR64>;
}

multiclass sve_int_perm_rev_revw<string asm> {
  def _D : sve_int_perm_rev<0b11, 0b10, asm, ZPR64>;
}

class sve_int_perm_cpy_r<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                         RegisterClass srcRegType>
: I<(outs zprty:$Zd), (ins zprty:$_Zd, PPR3bAny:$Pg, srcRegType:$Rn),
  asm, "\t$Zd, $Pg/m, $Rn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zd;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-13} = 0b101000101;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zd;

  let Constraints = "$Zd = $_Zd";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_perm_cpy_r<string asm> {
  def _B : sve_int_perm_cpy_r<0b00, asm, ZPR8, GPR32sp>;
  def _H : sve_int_perm_cpy_r<0b01, asm, ZPR16, GPR32sp>;
  def _S : sve_int_perm_cpy_r<0b10, asm, ZPR32, GPR32sp>;
  def _D : sve_int_perm_cpy_r<0b11, asm, ZPR64, GPR64sp>;

  def : InstAlias<"mov $Zd, $Pg/m, $Rn",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd, PPR3bAny:$Pg, GPR32sp:$Rn), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Rn",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, PPR3bAny:$Pg, GPR32sp:$Rn), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Rn",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, PPR3bAny:$Pg, GPR32sp:$Rn), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Rn",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, PPR3bAny:$Pg, GPR64sp:$Rn), 1>;
}

class sve_int_perm_cpy_v<bits<2> sz8_64, string asm, ZPRRegOp zprty,
                         RegisterClass srcRegtype>
: I<(outs zprty:$Zd), (ins zprty:$_Zd, PPR3bAny:$Pg, srcRegtype:$Vn),
  asm, "\t$Zd, $Pg/m, $Vn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Vn;
  bits<5> Zd;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = sz8_64;
  let Inst{21-13} = 0b100000100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Vn;
  let Inst{4-0}   = Zd;

  let Constraints = "$Zd = $_Zd";
  let DestructiveInstType = Destructive;
  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_perm_cpy_v<string asm> {
  def _B : sve_int_perm_cpy_v<0b00, asm, ZPR8, FPR8>;
  def _H : sve_int_perm_cpy_v<0b01, asm, ZPR16, FPR16>;
  def _S : sve_int_perm_cpy_v<0b10, asm, ZPR32, FPR32>;
  def _D : sve_int_perm_cpy_v<0b11, asm, ZPR64, FPR64>;

  def : InstAlias<"mov $Zd, $Pg/m, $Vn",
                  (!cast<Instruction>(NAME # _B) ZPR8:$Zd, PPR3bAny:$Pg, FPR8:$Vn), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Vn",
                  (!cast<Instruction>(NAME # _H) ZPR16:$Zd, PPR3bAny:$Pg, FPR16:$Vn), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Vn",
                  (!cast<Instruction>(NAME # _S) ZPR32:$Zd, PPR3bAny:$Pg, FPR32:$Vn), 1>;
  def : InstAlias<"mov $Zd, $Pg/m, $Vn",
                  (!cast<Instruction>(NAME # _D) ZPR64:$Zd, PPR3bAny:$Pg, FPR64:$Vn), 1>;
}

class sve_int_perm_compact<bit sz, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins PPR3bAny:$Pg, zprty:$Zn),
  asm, "\t$Zd, $Pg, $Zn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-23} = 0b000001011;
  let Inst{22}    = sz;
  let Inst{21-13} = 0b100001100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_perm_compact<string asm> {
  def _S : sve_int_perm_compact<0b0, asm, ZPR32>;
  def _D : sve_int_perm_compact<0b1, asm, ZPR64>;
}


//===----------------------------------------------------------------------===//
// SVE Memory - Contiguous Load Group
//===----------------------------------------------------------------------===//

class sve_mem_cld_si_base<bits<4> dtype, bit nf, string asm,
                          RegisterOperand VecList>
: I<(outs VecList:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, simm4s1:$imm4),
  asm, "\t$Zt, $Pg/z, [$Rn, $imm4, mul vl]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zt;
  bits<4> imm4;
  let Inst{31-25} = 0b1010010;
  let Inst{24-21} = dtype;
  let Inst{20}    = nf;
  let Inst{19-16} = imm4;
  let Inst{15-13} = 0b101;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
  let Uses = !if(!eq(nf, 1), [FFR], []);
  let Defs = !if(!eq(nf, 1), [FFR], []);
}

multiclass sve_mem_cld_si_base<bits<4> dtype, bit nf, string asm,
                               RegisterOperand listty, ZPRRegOp zprty> {
  def _REAL : sve_mem_cld_si_base<dtype, nf, asm, listty>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME # _REAL) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $imm4, mul vl]",
                  (!cast<Instruction>(NAME # _REAL) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, simm4s1:$imm4), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME # _REAL) listty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
}

multiclass sve_mem_cld_si<bits<4> dtype, string asm, RegisterOperand listty,
                          ZPRRegOp zprty>
: sve_mem_cld_si_base<dtype, 0, asm, listty, zprty>;

class sve_mem_cldnt_si_base<bits<2> msz, string asm, RegisterOperand VecList>
: I<(outs VecList:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, simm4s1:$imm4),
  asm, "\t$Zt, $Pg/z, [$Rn, $imm4, mul vl]",
  "",
  []>, Sched<[]> {
  bits<5> Zt;
  bits<3> Pg;
  bits<5> Rn;
  bits<4> imm4;
  let Inst{31-25} = 0b1010010;
  let Inst{24-23} = msz;
  let Inst{22-20} = 0b000;
  let Inst{19-16} = imm4;
  let Inst{15-13} = 0b111;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
}

multiclass sve_mem_cldnt_si<bits<2> msz, string asm, RegisterOperand listty,
                            ZPRRegOp zprty> {
  def NAME : sve_mem_cldnt_si_base<msz, asm, listty>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $imm4, mul vl]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, simm4s1:$imm4), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME) listty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_cldnt_ss_base<bits<2> msz, string asm, RegisterOperand VecList,
                            RegisterOperand gprty>
: I<(outs VecList:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
  asm, "\t$Zt, $Pg/z, [$Rn, $Rm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rm;
  bits<5> Rn;
  bits<5> Zt;
  let Inst{31-25} = 0b1010010;
  let Inst{24-23} = msz;
  let Inst{22-21} = 0b00;
  let Inst{20-16} = Rm;
  let Inst{15-13} = 0b110;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
}

multiclass sve_mem_cldnt_ss<bits<2> msz, string asm, RegisterOperand listty,
                            ZPRRegOp zprty, RegisterOperand gprty> {
  def NAME : sve_mem_cldnt_ss_base<msz, asm, listty, gprty>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Rm]",
                 (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm), 0>;
}

class sve_mem_ldqr_si<bits<2> sz, string asm, RegisterOperand VecList>
: I<(outs VecList:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, simm4s16:$imm4),
  asm, "\t$Zt, $Pg/z, [$Rn, $imm4]", "", []>, Sched<[]> {
  bits<5> Zt;
  bits<5> Rn;
  bits<3> Pg;
  bits<4> imm4;
  let Inst{31-25} = 0b1010010;
  let Inst{24-23} = sz;
  let Inst{22-20} = 0;
  let Inst{19-16} = imm4;
  let Inst{15-13} = 0b001;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
}

multiclass sve_mem_ldqr_si<bits<2> sz, string asm, RegisterOperand listty,
                           ZPRRegOp zprty> {
  def NAME : sve_mem_ldqr_si<sz, asm, listty>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME) listty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $imm4]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, simm4s16:$imm4), 0>;
}

class sve_mem_ldqr_ss<bits<2> sz, string asm, RegisterOperand VecList,
                      RegisterOperand gprty>
: I<(outs VecList:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
  asm, "\t$Zt, $Pg/z, [$Rn, $Rm]", "", []>, Sched<[]> {
  bits<5> Zt;
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{31-25} = 0b1010010;
  let Inst{24-23} = sz;
  let Inst{22-21} = 0;
  let Inst{20-16} = Rm;
  let Inst{15-13} = 0;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
}

multiclass sve_mem_ldqr_ss<bits<2> sz, string asm, RegisterOperand listty,
                           ZPRRegOp zprty, RegisterOperand gprty> {
  def NAME : sve_mem_ldqr_ss<sz, asm, listty, gprty>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Rm]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm), 0>;
}

class sve_mem_ld_dup<bits<2> dtypeh, bits<2> dtypel, string asm,
                     RegisterOperand VecList, Operand immtype>
: I<(outs VecList:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, immtype:$imm6),
  asm, "\t$Zt, $Pg/z, [$Rn, $imm6]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zt;
  bits<6> imm6;
  let Inst{31-25} = 0b1000010;
  let Inst{24-23} = dtypeh;
  let Inst{22}    = 1;
  let Inst{21-16} = imm6;
  let Inst{15}    = 0b1;
  let Inst{14-13} = dtypel;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
}

multiclass sve_mem_ld_dup<bits<2> dtypeh, bits<2> dtypel, string asm,
                          RegisterOperand zlistty, ZPRRegOp zprty, Operand immtype> {
  def NAME : sve_mem_ld_dup<dtypeh, dtypel, asm, zlistty, immtype>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $imm6]",
                  (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, immtype:$imm6), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME) zlistty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_cld_ss_base<bits<4> dtype, bit ff, dag iops, string asm,
                          RegisterOperand VecList>
: I<(outs VecList:$Zt), iops,
  asm, "\t$Zt, $Pg/z, [$Rn, $Rm]",
  "",
  []>, Sched<[]> {
  bits<5> Zt;
  bits<3> Pg;
  bits<5> Rm;
  bits<5> Rn;
  let Inst{31-25} = 0b1010010;
  let Inst{24-21} = dtype;
  let Inst{20-16} = Rm;
  let Inst{15-14} = 0b01;
  let Inst{13}    = ff;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
  let Uses = !if(!eq(ff, 1), [FFR], []);
  let Defs = !if(!eq(ff, 1), [FFR], []);
}

multiclass sve_mem_cld_ss<bits<4> dtype, string asm, RegisterOperand listty,
                          ZPRRegOp zprty, RegisterOperand gprty> {
  def "" : sve_mem_cld_ss_base<dtype, 0, (ins PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
                               asm, listty>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Rm]",
                 (!cast<Instruction>(NAME) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm), 0>;
}

multiclass sve_mem_cldff_ss<bits<4> dtype, string asm, RegisterOperand listty,
                            ZPRRegOp zprty, RegisterOperand gprty> {
  def _REAL : sve_mem_cld_ss_base<dtype, 1, (ins PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
                                  asm, listty>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Rm]",
                 (!cast<Instruction>(NAME # _REAL) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm), 0>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                 (!cast<Instruction>(NAME # _REAL) listty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, XZR), 1>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                 (!cast<Instruction>(NAME # _REAL) zprty:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, XZR), 0>;
}

multiclass sve_mem_cldnf_si<bits<4> dtype, string asm, RegisterOperand listty,
                            ZPRRegOp zprty>
: sve_mem_cld_si_base<dtype, 1, asm, listty, zprty>;

class sve_mem_eld_si<bits<2> sz, bits<2> nregs, RegisterOperand VecList,
                     string asm, Operand immtype>
: I<(outs VecList:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, immtype:$imm4),
  asm, "\t$Zt, $Pg/z, [$Rn, $imm4, mul vl]",
  "",
  []>, Sched<[]> {
  bits<5> Zt;
  bits<3> Pg;
  bits<5> Rn;
  bits<4> imm4;
  let Inst{31-25} = 0b1010010;
  let Inst{24-23} = sz;
  let Inst{22-21} = nregs;
  let Inst{20}    = 0;
  let Inst{19-16} = imm4;
  let Inst{15-13} = 0b111;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
}

multiclass sve_mem_eld_si<bits<2> sz, bits<2> nregs, RegisterOperand VecList,
                          string asm, Operand immtype> {
  def NAME : sve_mem_eld_si<sz, nregs, VecList, asm, immtype>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn]",
                  (!cast<Instruction>(NAME) VecList:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_eld_ss<bits<2> sz, bits<2> nregs, RegisterOperand VecList,
                     string asm, RegisterOperand gprty>
: I<(outs VecList:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
  asm, "\t$Zt, $Pg/z, [$Rn, $Rm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rm;
  bits<5> Rn;
  bits<5> Zt;
  let Inst{31-25} = 0b1010010;
  let Inst{24-23} = sz;
  let Inst{22-21} = nregs;
  let Inst{20-16} = Rm;
  let Inst{15-13} = 0b110;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
}

//===----------------------------------------------------------------------===//
// SVE Memory - 32-bit Gather and Unsized Contiguous Group
//===----------------------------------------------------------------------===//

// bit xs      is '1' if offsets are signed
// bit scaled  is '1' if the offsets are scaled
class sve_mem_32b_gld_sv<bits<4> opc, bit xs, bit scaled, string asm,
                         RegisterOperand zprext>
: I<(outs Z_s:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, zprext:$Zm),
  asm, "\t$Zt, $Pg/z, [$Rn, $Zm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zm;
  bits<5> Zt;
  let Inst{31-25} = 0b1000010;
  let Inst{24-23} = opc{3-2};
  let Inst{22}    = xs;
  let Inst{21}    = scaled;
  let Inst{20-16} = Zm;
  let Inst{15}    = 0b0;
  let Inst{14-13} = opc{1-0};
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
  let Defs = !if(!eq(opc{0}, 1), [FFR], []);
  let Uses = !if(!eq(opc{0}, 1), [FFR], []);
}

multiclass sve_mem_32b_gld_sv_32_scaled<bits<4> opc, string asm,
                                        RegisterOperand sxtw_opnd,
                                        RegisterOperand uxtw_opnd> {
  def _UXTW_SCALED_REAL : sve_mem_32b_gld_sv<opc, 0, 1, asm, uxtw_opnd>;
  def _SXTW_SCALED_REAL : sve_mem_32b_gld_sv<opc, 1, 1, asm, sxtw_opnd>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _UXTW_SCALED_REAL) ZPR32:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, uxtw_opnd:$Zm), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _SXTW_SCALED_REAL) ZPR32:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, sxtw_opnd:$Zm), 0>;
}

multiclass sve_mem_32b_gld_vs_32_unscaled<bits<4> opc, string asm,
                                          RegisterOperand sxtw_opnd,
                                          RegisterOperand uxtw_opnd> {
  def _UXTW_REAL : sve_mem_32b_gld_sv<opc, 0, 0, asm, uxtw_opnd>;
  def _SXTW_REAL : sve_mem_32b_gld_sv<opc, 1, 0, asm, sxtw_opnd>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _UXTW_REAL) ZPR32:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, uxtw_opnd:$Zm), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _SXTW_REAL) ZPR32:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, sxtw_opnd:$Zm), 0>;
}


class sve_mem_32b_gld_vi<bits<4> opc, string asm, Operand imm_ty>
: I<(outs Z_s:$Zt), (ins PPR3bAny:$Pg, ZPR32:$Zn, imm_ty:$imm5),
  asm, "\t$Zt, $Pg/z, [$Zn, $imm5]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zn;
  bits<5> Zt;
  bits<5> imm5;
  let Inst{31-25} = 0b1000010;
  let Inst{24-23} = opc{3-2};
  let Inst{22-21} = 0b01;
  let Inst{20-16} = imm5;
  let Inst{15}    = 0b1;
  let Inst{14-13} = opc{1-0};
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
  let Defs = !if(!eq(opc{0}, 1), [FFR], []);
  let Uses = !if(!eq(opc{0}, 1), [FFR], []);
}

multiclass sve_mem_32b_gld_vi_32_ptrs<bits<4> opc, string asm, Operand imm_ty> {
  def _IMM_REAL : sve_mem_32b_gld_vi<opc, asm, imm_ty>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Zn]",
                  (!cast<Instruction>(NAME # _IMM_REAL) ZPR32:$Zt, PPR3bAny:$Pg, ZPR32:$Zn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Zn, $imm5]",
                  (!cast<Instruction>(NAME # _IMM_REAL) ZPR32:$Zt, PPR3bAny:$Pg, ZPR32:$Zn, imm_ty:$imm5), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Zn]",
                  (!cast<Instruction>(NAME # _IMM_REAL) Z_s:$Zt, PPR3bAny:$Pg, ZPR32:$Zn, 0), 1>;
}

class sve_mem_prfm_si<bits<2> msz, string asm>
: I<(outs), (ins sve_prfop:$prfop, PPR3bAny:$Pg, GPR64sp:$Rn, simm6s1:$imm6),
  asm, "\t$prfop, $Pg, [$Rn, $imm6, mul vl]",
  "",
  []>, Sched<[]> {
  bits<5> Rn;
  bits<3> Pg;
  bits<6> imm6;
  bits<4> prfop;
  let Inst{31-22} = 0b1000010111;
  let Inst{21-16} = imm6;
  let Inst{15}    = 0b0;
  let Inst{14-13} = msz;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = prfop;

  let hasSideEffects = 1;
}

multiclass sve_mem_prfm_si<bits<2> msz, string asm> {
  def NAME : sve_mem_prfm_si<msz, asm>;

  def : InstAlias<asm # "\t$prfop, $Pg, [$Rn]",
                  (!cast<Instruction>(NAME) sve_prfop:$prfop, PPR3bAny:$Pg, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_prfm_ss<bits<3> opc, string asm, RegisterOperand gprty>
: I<(outs), (ins sve_prfop:$prfop, PPR3bAny:$Pg, GPR64sp:$Rn, gprty:$Rm),
  asm, "\t$prfop, $Pg, [$Rn, $Rm]",
  "",
  []>, Sched<[]> {
  bits<5> Rm;
  bits<5> Rn;
  bits<3> Pg;
  bits<4> prfop;
  let Inst{31-25} = 0b1000010;
  let Inst{24-23} = opc{2-1};
  let Inst{22-21} = 0b00;
  let Inst{20-16} = Rm;
  let Inst{15}    = 0b1;
  let Inst{14}    = opc{0};
  let Inst{13}    = 0b0;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = prfop;

  let hasSideEffects = 1;
}

class sve_mem_32b_prfm_sv<bits<2> msz, bit xs, string asm,
                          RegisterOperand zprext>
: I<(outs), (ins sve_prfop:$prfop, PPR3bAny:$Pg, GPR64sp:$Rn, zprext:$Zm),
  asm, "\t$prfop, $Pg, [$Rn, $Zm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zm;
  bits<4> prfop;
  let Inst{31-23} = 0b100001000;
  let Inst{22}    = xs;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15}    = 0b0;
  let Inst{14-13} = msz;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = prfop;

  let hasSideEffects = 1;
}

multiclass sve_mem_32b_prfm_sv_scaled<bits<2> msz, string asm,
                                      RegisterOperand sxtw_opnd,
                                      RegisterOperand uxtw_opnd> {
  def _UXTW_SCALED : sve_mem_32b_prfm_sv<msz, 0, asm, uxtw_opnd>;
  def _SXTW_SCALED : sve_mem_32b_prfm_sv<msz, 1, asm, sxtw_opnd>;
}

class sve_mem_32b_prfm_vi<bits<2> msz, string asm, Operand imm_ty>
: I<(outs), (ins sve_prfop:$prfop, PPR3bAny:$Pg, ZPR32:$Zn, imm_ty:$imm5),
  asm, "\t$prfop, $Pg, [$Zn, $imm5]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zn;
  bits<5> imm5;
  bits<4> prfop;
  let Inst{31-25} = 0b1000010;
  let Inst{24-23} = msz;
  let Inst{22-21} = 0b00;
  let Inst{20-16} = imm5;
  let Inst{15-13} = 0b111;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = prfop;
}

multiclass sve_mem_32b_prfm_vi<bits<2> msz, string asm, Operand imm_ty> {
  def NAME : sve_mem_32b_prfm_vi<msz, asm, imm_ty>;

  def : InstAlias<asm # "\t$prfop, $Pg, [$Zn]",
                  (!cast<Instruction>(NAME) sve_prfop:$prfop, PPR3bAny:$Pg, ZPR32:$Zn, 0), 1>;
}

class sve_mem_z_fill<string asm>
: I<(outs ZPRAny:$Zt), (ins GPR64sp:$Rn, simm9:$imm9),
  asm, "\t$Zt, [$Rn, $imm9, mul vl]",
  "",
  []>, Sched<[]> {
  bits<5> Rn;
  bits<5> Zt;
  bits<9> imm9;
  let Inst{31-22} = 0b1000010110;
  let Inst{21-16} = imm9{8-3};
  let Inst{15-13} = 0b010;
  let Inst{12-10} = imm9{2-0};
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
}

multiclass sve_mem_z_fill<string asm> {
  def NAME : sve_mem_z_fill<asm>;

  def : InstAlias<asm # "\t$Zt, [$Rn]",
                  (!cast<Instruction>(NAME) ZPRAny:$Zt, GPR64sp:$Rn, 0), 1>;
}

class sve_mem_p_fill<string asm>
: I<(outs PPRAny:$Pt), (ins GPR64sp:$Rn, simm9:$imm9),
  asm, "\t$Pt, [$Rn, $imm9, mul vl]",
  "",
  []>, Sched<[]> {
  bits<4> Pt;
  bits<5> Rn;
  bits<9> imm9;
  let Inst{31-22} = 0b1000010110;
  let Inst{21-16} = imm9{8-3};
  let Inst{15-13} = 0b000;
  let Inst{12-10} = imm9{2-0};
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = Pt;

  let mayLoad = 1;
}

multiclass sve_mem_p_fill<string asm> {
  def NAME : sve_mem_p_fill<asm>;

  def : InstAlias<asm # "\t$Pt, [$Rn]",
                  (!cast<Instruction>(NAME) PPRAny:$Pt, GPR64sp:$Rn, 0), 1>;
}

//===----------------------------------------------------------------------===//
// SVE Memory - 64-bit Gather Group
//===----------------------------------------------------------------------===//

// bit xs      is '1' if offsets are signed
// bit scaled  is '1' if the offsets are scaled
// bit lsl     is '0' if the offsets are extended (uxtw/sxtw), '1' if shifted (lsl)
class sve_mem_64b_gld_sv<bits<4> opc, bit xs, bit scaled, bit lsl, string asm,
                         RegisterOperand zprext>
: I<(outs Z_d:$Zt), (ins PPR3bAny:$Pg, GPR64sp:$Rn, zprext:$Zm),
  asm, "\t$Zt, $Pg/z, [$Rn, $Zm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zm;
  bits<5> Zt;
  let Inst{31-25} = 0b1100010;
  let Inst{24-23} = opc{3-2};
  let Inst{22}    = xs;
  let Inst{21}    = scaled;
  let Inst{20-16} = Zm;
  let Inst{15}    = lsl;
  let Inst{14-13} = opc{1-0};
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
  let Defs = !if(!eq(opc{0}, 1), [FFR], []);
  let Uses = !if(!eq(opc{0}, 1), [FFR], []);
}

multiclass sve_mem_64b_gld_sv_32_scaled<bits<4> opc, string asm,
                                        RegisterOperand sxtw_opnd,
                                        RegisterOperand uxtw_opnd> {
  def _UXTW_SCALED_REAL : sve_mem_64b_gld_sv<opc, 0, 1, 0, asm, uxtw_opnd>;
  def _SXTW_SCALED_REAL : sve_mem_64b_gld_sv<opc, 1, 1, 0, asm, sxtw_opnd>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _UXTW_SCALED_REAL) ZPR64:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, uxtw_opnd:$Zm), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _SXTW_SCALED_REAL) ZPR64:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, sxtw_opnd:$Zm), 0>;
}

multiclass sve_mem_64b_gld_vs_32_unscaled<bits<4> opc, string asm,
                                          RegisterOperand sxtw_opnd,
                                          RegisterOperand uxtw_opnd> {
  def _UXTW_REAL : sve_mem_64b_gld_sv<opc, 0, 0, 0, asm, uxtw_opnd>;
  def _SXTW_REAL : sve_mem_64b_gld_sv<opc, 1, 0, 0, asm, sxtw_opnd>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _UXTW_REAL) ZPR64:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, uxtw_opnd:$Zm), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _SXTW_REAL) ZPR64:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, sxtw_opnd:$Zm), 0>;
}

multiclass sve_mem_64b_gld_sv2_64_scaled<bits<4> opc, string asm,
                                         RegisterOperand zprext> {
  def _SCALED_REAL : sve_mem_64b_gld_sv<opc, 1, 1, 1, asm, zprext>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _SCALED_REAL) ZPR64:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, zprext:$Zm), 0>;
}

multiclass sve_mem_64b_gld_vs2_64_unscaled<bits<4> opc, string asm> {
  def _REAL : sve_mem_64b_gld_sv<opc, 1, 0, 1, asm, ZPR64ExtLSL8>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Rn, $Zm]",
                  (!cast<Instruction>(NAME # _REAL) ZPR64:$Zt, PPR3bAny:$Pg, GPR64sp:$Rn, ZPR64ExtLSL8:$Zm), 0>;
}

class sve_mem_64b_gld_vi<bits<4> opc, string asm, Operand imm_ty>
: I<(outs Z_d:$Zt), (ins PPR3bAny:$Pg, ZPR64:$Zn, imm_ty:$imm5),
  asm, "\t$Zt, $Pg/z, [$Zn, $imm5]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zn;
  bits<5> Zt;
  bits<5> imm5;
  let Inst{31-25} = 0b1100010;
  let Inst{24-23} = opc{3-2};
  let Inst{22-21} = 0b01;
  let Inst{20-16} = imm5;
  let Inst{15}    = 0b1;
  let Inst{14-13} = opc{1-0};
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zt;

  let mayLoad = 1;
  let Defs = !if(!eq(opc{0}, 1), [FFR], []);
  let Uses = !if(!eq(opc{0}, 1), [FFR], []);
}

multiclass sve_mem_64b_gld_vi_64_ptrs<bits<4> opc, string asm, Operand imm_ty> {
  def _IMM_REAL : sve_mem_64b_gld_vi<opc, asm, imm_ty>;

  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Zn]",
                  (!cast<Instruction>(NAME # _IMM_REAL) ZPR64:$Zt, PPR3bAny:$Pg, ZPR64:$Zn, 0), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Zn, $imm5]",
                 (!cast<Instruction>(NAME # _IMM_REAL) ZPR64:$Zt, PPR3bAny:$Pg, ZPR64:$Zn, imm_ty:$imm5), 0>;
  def : InstAlias<asm # "\t$Zt, $Pg/z, [$Zn]",
                  (!cast<Instruction>(NAME # _IMM_REAL) Z_d:$Zt, PPR3bAny:$Pg, ZPR64:$Zn, 0), 1>;
}

// bit lsl is '0' if the offsets are extended (uxtw/sxtw), '1' if shifted (lsl)
class sve_mem_64b_prfm_sv<bits<2> msz, bit xs, bit lsl, string asm,
                          RegisterOperand zprext>
: I<(outs), (ins sve_prfop:$prfop, PPR3bAny:$Pg, GPR64sp:$Rn, zprext:$Zm),
  asm, "\t$prfop, $Pg, [$Rn, $Zm]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Rn;
  bits<5> Zm;
  bits<4> prfop;
  let Inst{31-23} = 0b110001000;
  let Inst{22}    = xs;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15}    = lsl;
  let Inst{14-13} = msz;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = prfop;

  let hasSideEffects = 1;
}

multiclass sve_mem_64b_prfm_sv_ext_scaled<bits<2> msz, string asm,
                                          RegisterOperand sxtw_opnd,
                                          RegisterOperand uxtw_opnd> {
  def _UXTW_SCALED : sve_mem_64b_prfm_sv<msz, 0, 0, asm, uxtw_opnd>;
  def _SXTW_SCALED : sve_mem_64b_prfm_sv<msz, 1, 0, asm, sxtw_opnd>;
}

multiclass sve_mem_64b_prfm_sv_lsl_scaled<bits<2> msz, string asm,
                                          RegisterOperand zprext> {
  def NAME : sve_mem_64b_prfm_sv<msz, 1, 1, asm, zprext>;
}


class sve_mem_64b_prfm_vi<bits<2> msz, string asm, Operand imm_ty>
: I<(outs), (ins sve_prfop:$prfop, PPR3bAny:$Pg, ZPR64:$Zn, imm_ty:$imm5),
  asm, "\t$prfop, $Pg, [$Zn, $imm5]",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zn;
  bits<5> imm5;
  bits<4> prfop;
  let Inst{31-25} = 0b1100010;
  let Inst{24-23} = msz;
  let Inst{22-21} = 0b00;
  let Inst{20-16} = imm5;
  let Inst{15-13} = 0b111;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = prfop;

  let hasSideEffects = 1;
}

multiclass sve_mem_64b_prfm_vi<bits<2> msz, string asm, Operand imm_ty> {
  def NAME : sve_mem_64b_prfm_vi<msz, asm, imm_ty>;

  def : InstAlias<asm # "\t$prfop, $Pg, [$Zn]",
                  (!cast<Instruction>(NAME) sve_prfop:$prfop, PPR3bAny:$Pg, ZPR64:$Zn, 0), 1>;
}


//===----------------------------------------------------------------------===//
// SVE Compute Vector Address Group
//===----------------------------------------------------------------------===//

class sve_int_bin_cons_misc_0_a<bits<2> opc, bits<2> msz, string asm,
                                ZPRRegOp zprty, RegisterOperand zprext>
: I<(outs zprty:$Zd), (ins zprty:$Zn, zprext:$Zm),
  asm, "\t$Zd, [$Zn, $Zm]",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zn;
  bits<5> Zm;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = opc;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-12} = 0b1010;
  let Inst{11-10} = msz;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_bin_cons_misc_0_a_uxtw<bits<2> opc, string asm> {
  def _0 : sve_int_bin_cons_misc_0_a<opc, 0b00, asm, ZPR64, ZPR64ExtUXTW8>;
  def _1 : sve_int_bin_cons_misc_0_a<opc, 0b01, asm, ZPR64, ZPR64ExtUXTW16>;
  def _2 : sve_int_bin_cons_misc_0_a<opc, 0b10, asm, ZPR64, ZPR64ExtUXTW32>;
  def _3 : sve_int_bin_cons_misc_0_a<opc, 0b11, asm, ZPR64, ZPR64ExtUXTW64>;
}

multiclass sve_int_bin_cons_misc_0_a_sxtw<bits<2> opc, string asm> {
  def _0 : sve_int_bin_cons_misc_0_a<opc, 0b00, asm, ZPR64, ZPR64ExtSXTW8>;
  def _1 : sve_int_bin_cons_misc_0_a<opc, 0b01, asm, ZPR64, ZPR64ExtSXTW16>;
  def _2 : sve_int_bin_cons_misc_0_a<opc, 0b10, asm, ZPR64, ZPR64ExtSXTW32>;
  def _3 : sve_int_bin_cons_misc_0_a<opc, 0b11, asm, ZPR64, ZPR64ExtSXTW64>;
}

multiclass sve_int_bin_cons_misc_0_a_32_lsl<bits<2> opc, string asm> {
  def _0 : sve_int_bin_cons_misc_0_a<opc, 0b00, asm, ZPR32, ZPR32ExtLSL8>;
  def _1 : sve_int_bin_cons_misc_0_a<opc, 0b01, asm, ZPR32, ZPR32ExtLSL16>;
  def _2 : sve_int_bin_cons_misc_0_a<opc, 0b10, asm, ZPR32, ZPR32ExtLSL32>;
  def _3 : sve_int_bin_cons_misc_0_a<opc, 0b11, asm, ZPR32, ZPR32ExtLSL64>;
}

multiclass sve_int_bin_cons_misc_0_a_64_lsl<bits<2> opc, string asm> {
  def _0 : sve_int_bin_cons_misc_0_a<opc, 0b00, asm, ZPR64, ZPR64ExtLSL8>;
  def _1 : sve_int_bin_cons_misc_0_a<opc, 0b01, asm, ZPR64, ZPR64ExtLSL16>;
  def _2 : sve_int_bin_cons_misc_0_a<opc, 0b10, asm, ZPR64, ZPR64ExtLSL32>;
  def _3 : sve_int_bin_cons_misc_0_a<opc, 0b11, asm, ZPR64, ZPR64ExtLSL64>;
}


//===----------------------------------------------------------------------===//
// SVE Integer Misc - Unpredicated Group
//===----------------------------------------------------------------------===//

class sve_int_bin_cons_misc_0_b<bits<2> sz, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$Zn, zprty:$Zm),
  asm, "\t$Zd, $Zn, $Zm",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-10} = 0b101100;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

multiclass sve_int_bin_cons_misc_0_b<string asm> {
  def _H : sve_int_bin_cons_misc_0_b<0b01, asm, ZPR16>;
  def _S : sve_int_bin_cons_misc_0_b<0b10, asm, ZPR32>;
  def _D : sve_int_bin_cons_misc_0_b<0b11, asm, ZPR64>;
}

class sve_int_bin_cons_misc_0_c<bits<8> opc, string asm, ZPRRegOp zprty>
: I<(outs zprty:$Zd), (ins zprty:$Zn),
  asm, "\t$Zd, $Zn",
  "",
  []>, Sched<[]> {
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = opc{7-6};
  let Inst{21}    = 0b1;
  let Inst{20-16} = opc{5-1};
  let Inst{15-11} = 0b10111;
  let Inst{10}    = opc{0};
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

//===----------------------------------------------------------------------===//
// SVE Integer Reduction Group
//===----------------------------------------------------------------------===//

class sve_int_reduce<bits<2> sz8_32, bits<2> fmt, bits<3> opc, string asm,
                     ZPRRegOp zprty, RegisterClass regtype>
: I<(outs regtype:$Vd), (ins PPR3bAny:$Pg, zprty:$Zn),
  asm, "\t$Vd, $Pg, $Zn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Vd;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_32;
  let Inst{21}    = 0b0;
  let Inst{20-19} = fmt;
  let Inst{18-16} = opc;
  let Inst{15-13} = 0b001;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Vd;
}

multiclass sve_int_reduce_0_saddv<bits<3> opc, string asm> {
  def _B : sve_int_reduce<0b00, 0b00, opc, asm, ZPR8, FPR64>;
  def _H : sve_int_reduce<0b01, 0b00, opc, asm, ZPR16, FPR64>;
  def _S : sve_int_reduce<0b10, 0b00, opc, asm, ZPR32, FPR64>;
}

multiclass sve_int_reduce_0_uaddv<bits<3> opc, string asm> {
  def _B : sve_int_reduce<0b00, 0b00, opc, asm, ZPR8, FPR64>;
  def _H : sve_int_reduce<0b01, 0b00, opc, asm, ZPR16, FPR64>;
  def _S : sve_int_reduce<0b10, 0b00, opc, asm, ZPR32, FPR64>;
  def _D : sve_int_reduce<0b11, 0b00, opc, asm, ZPR64, FPR64>;
}

multiclass sve_int_reduce_1<bits<3> opc, string asm> {
  def _B : sve_int_reduce<0b00, 0b01, opc, asm, ZPR8, FPR8>;
  def _H : sve_int_reduce<0b01, 0b01, opc, asm, ZPR16, FPR16>;
  def _S : sve_int_reduce<0b10, 0b01, opc, asm, ZPR32, FPR32>;
  def _D : sve_int_reduce<0b11, 0b01, opc, asm, ZPR64, FPR64>;
}

multiclass sve_int_reduce_2<bits<3> opc, string asm> {
  def _B : sve_int_reduce<0b00, 0b11, opc, asm, ZPR8, FPR8>;
  def _H : sve_int_reduce<0b01, 0b11, opc, asm, ZPR16, FPR16>;
  def _S : sve_int_reduce<0b10, 0b11, opc, asm, ZPR32, FPR32>;
  def _D : sve_int_reduce<0b11, 0b11, opc, asm, ZPR64, FPR64>;
}

class sve_int_movprfx_pred<bits<2> sz8_32, bits<3> opc, string asm,
                           ZPRRegOp zprty, string pg_suffix, dag iops>
: I<(outs zprty:$Zd), iops,
  asm, "\t$Zd, $Pg"#pg_suffix#", $Zn",
  "",
  []>, Sched<[]> {
  bits<3> Pg;
  bits<5> Zd;
  bits<5> Zn;
  let Inst{31-24} = 0b00000100;
  let Inst{23-22} = sz8_32;
  let Inst{21-19} = 0b010;
  let Inst{18-16} = opc;
  let Inst{15-13} = 0b001;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;

  let ElementSize = zprty.ElementSize;
}

multiclass sve_int_movprfx_pred_merge<bits<3> opc, string asm> {
let Constraints = "$Zd = $_Zd" in {
  def _B : sve_int_movprfx_pred<0b00, opc, asm, ZPR8, "/m",
                                (ins ZPR8:$_Zd, PPR3bAny:$Pg, ZPR8:$Zn)>;
  def _H : sve_int_movprfx_pred<0b01, opc, asm, ZPR16, "/m",
                                (ins ZPR16:$_Zd, PPR3bAny:$Pg, ZPR16:$Zn)>;
  def _S : sve_int_movprfx_pred<0b10, opc, asm, ZPR32, "/m",
                                (ins ZPR32:$_Zd, PPR3bAny:$Pg, ZPR32:$Zn)>;
  def _D : sve_int_movprfx_pred<0b11, opc, asm, ZPR64, "/m",
                                (ins ZPR64:$_Zd, PPR3bAny:$Pg, ZPR64:$Zn)>;
}
}

multiclass sve_int_movprfx_pred_zero<bits<3> opc, string asm> {
  def _B : sve_int_movprfx_pred<0b00, opc, asm, ZPR8, "/z",
                                (ins PPR3bAny:$Pg, ZPR8:$Zn)>;
  def _H : sve_int_movprfx_pred<0b01, opc, asm, ZPR16, "/z",
                                (ins PPR3bAny:$Pg, ZPR16:$Zn)>;
  def _S : sve_int_movprfx_pred<0b10, opc, asm, ZPR32, "/z",
                                (ins PPR3bAny:$Pg, ZPR32:$Zn)>;
  def _D : sve_int_movprfx_pred<0b11, opc, asm, ZPR64, "/z",
                                (ins PPR3bAny:$Pg, ZPR64:$Zn)>;
}

//===----------------------------------------------------------------------===//
// SVE Propagate Break Group
//===----------------------------------------------------------------------===//

class sve_int_brkp<bits<2> opc, string asm>
: I<(outs PPR8:$Pd), (ins PPRAny:$Pg, PPR8:$Pn, PPR8:$Pm),
  asm, "\t$Pd, $Pg/z, $Pn, $Pm",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<4> Pg;
  bits<4> Pm;
  bits<4> Pn;
  let Inst{31-24} = 0b00100101;
  let Inst{23}    = 0b0;
  let Inst{22}    = opc{1};
  let Inst{21-20} = 0b00;
  let Inst{19-16} = Pm;
  let Inst{15-14} = 0b11;
  let Inst{13-10} = Pg;
  let Inst{9}     = 0b0;
  let Inst{8-5}   = Pn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;

  let Defs = !if(!eq (opc{1}, 1), [NZCV], []);
}


//===----------------------------------------------------------------------===//
// SVE Partition Break Group
//===----------------------------------------------------------------------===//

class sve_int_brkn<bit S, string asm>
: I<(outs PPR8:$Pdm), (ins PPRAny:$Pg, PPR8:$Pn, PPR8:$_Pdm),
  asm, "\t$Pdm, $Pg/z, $Pn, $_Pdm",
  "",
  []>, Sched<[]> {
  bits<4> Pdm;
  bits<4> Pg;
  bits<4> Pn;
  let Inst{31-23} = 0b001001010;
  let Inst{22}    = S;
  let Inst{21-14} = 0b01100001;
  let Inst{13-10} = Pg;
  let Inst{9}     = 0b0;
  let Inst{8-5}   = Pn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = Pdm;

  let Constraints = "$Pdm = $_Pdm";
  let Defs = !if(!eq (S, 0b1), [NZCV], []);
}

class sve_int_break<bits<3> opc, string asm, string suffix, dag iops>
: I<(outs PPR8:$Pd), iops,
  asm, "\t$Pd, $Pg"#suffix#", $Pn",
  "",
  []>, Sched<[]> {
  bits<4> Pd;
  bits<4> Pg;
  bits<4> Pn;
  let Inst{31-24} = 0b00100101;
  let Inst{23-22} = opc{2-1};
  let Inst{21-14} = 0b01000001;
  let Inst{13-10} = Pg;
  let Inst{9}     = 0b0;
  let Inst{8-5}   = Pn;
  let Inst{4}     = opc{0};
  let Inst{3-0}   = Pd;

  let Constraints = !if(!eq (opc{0}, 1), "$Pd = $_Pd", "");
  let Defs = !if(!eq (opc{1}, 1), [NZCV], []);

}

multiclass sve_int_break_m<bits<3> opc, string asm> {
  def NAME : sve_int_break<opc, asm, "/m", (ins PPR8:$_Pd, PPRAny:$Pg, PPR8:$Pn)>;
}

multiclass sve_int_break_z<bits<3> opc, string asm> {
  def NAME : sve_int_break<opc, asm, "/z", (ins PPRAny:$Pg, PPR8:$Pn)>;
}

