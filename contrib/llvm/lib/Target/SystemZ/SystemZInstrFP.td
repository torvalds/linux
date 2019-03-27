//==- SystemZInstrFP.td - Floating-point SystemZ instructions --*- tblgen-*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// TODO: Most floating-point instructions (except for simple moves and the
// like) can raise exceptions -- should they have hasSideEffects=1 ?

//===----------------------------------------------------------------------===//
// Select instructions
//===----------------------------------------------------------------------===//

// C's ?: operator for floating-point operands.
let Predicates = [FeatureVector] in {
  def SelectVR32 : SelectWrapper<f32, VR32>;
  def SelectVR64 : SelectWrapper<f64, VR64>;
}
def SelectF32  : SelectWrapper<f32, FP32>;
def SelectF64  : SelectWrapper<f64, FP64>;
let Predicates = [FeatureNoVectorEnhancements1] in
  def SelectF128 : SelectWrapper<f128, FP128>;
let Predicates = [FeatureVectorEnhancements1] in
  def SelectVR128 : SelectWrapper<f128, VR128>;

defm CondStoreF32 : CondStores<FP32, nonvolatile_store,
                               nonvolatile_load, bdxaddr20only>;
defm CondStoreF64 : CondStores<FP64, nonvolatile_store,
                               nonvolatile_load, bdxaddr20only>;

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

// Load zero.
let isAsCheapAsAMove = 1, isMoveImm = 1 in {
  def LZER : InherentRRE<"lzer", 0xB374, FP32,  fpimm0>;
  def LZDR : InherentRRE<"lzdr", 0xB375, FP64,  fpimm0>;
  def LZXR : InherentRRE<"lzxr", 0xB376, FP128, fpimm0>;
}

// Moves between two floating-point registers.
def LER : UnaryRR <"ler", 0x38,   null_frag, FP32,  FP32>;
def LDR : UnaryRR <"ldr", 0x28,   null_frag, FP64,  FP64>;
def LXR : UnaryRRE<"lxr", 0xB365, null_frag, FP128, FP128>;

// For z13 we prefer LDR over LER to avoid partial register dependencies.
let isCodeGenOnly = 1 in
  def LDR32 : UnaryRR<"ldr", 0x28, null_frag, FP32, FP32>;

// Moves between two floating-point registers that also set the condition
// codes.
let Defs = [CC], CCValues = 0xF, CompareZeroCCMask = 0xF in {
  defm LTEBR : LoadAndTestRRE<"ltebr", 0xB302, FP32>;
  defm LTDBR : LoadAndTestRRE<"ltdbr", 0xB312, FP64>;
  defm LTXBR : LoadAndTestRRE<"ltxbr", 0xB342, FP128>;
}
// Note that LTxBRCompare is not available if we have vector support,
// since load-and-test instructions will partially clobber the target
// (vector) register.
let Predicates = [FeatureNoVector] in {
  defm : CompareZeroFP<LTEBRCompare, FP32>;
  defm : CompareZeroFP<LTDBRCompare, FP64>;
  defm : CompareZeroFP<LTXBRCompare, FP128>;
}

// Use a normal load-and-test for compare against zero in case of
// vector support (via a pseudo to simplify instruction selection).
let Defs = [CC], usesCustomInserter = 1, hasNoSchedulingInfo = 1 in {
  def LTEBRCompare_VecPseudo : Pseudo<(outs), (ins FP32:$R1, FP32:$R2), []>;
  def LTDBRCompare_VecPseudo : Pseudo<(outs), (ins FP64:$R1, FP64:$R2), []>;
  def LTXBRCompare_VecPseudo : Pseudo<(outs), (ins FP128:$R1, FP128:$R2), []>;
}
let Predicates = [FeatureVector] in {
  defm : CompareZeroFP<LTEBRCompare_VecPseudo, FP32>;
  defm : CompareZeroFP<LTDBRCompare_VecPseudo, FP64>;
}
let Predicates = [FeatureVector, FeatureNoVectorEnhancements1] in
  defm : CompareZeroFP<LTXBRCompare_VecPseudo, FP128>;

// Moves between 64-bit integer and floating-point registers.
def LGDR : UnaryRRE<"lgdr", 0xB3CD, bitconvert, GR64, FP64>;
def LDGR : UnaryRRE<"ldgr", 0xB3C1, bitconvert, FP64, GR64>;

// fcopysign with an FP32 result.
let isCodeGenOnly = 1 in {
  def CPSDRss : BinaryRRFb<"cpsdr", 0xB372, fcopysign, FP32, FP32, FP32>;
  def CPSDRsd : BinaryRRFb<"cpsdr", 0xB372, fcopysign, FP32, FP32, FP64>;
}

// The sign of an FP128 is in the high register.
let Predicates = [FeatureNoVectorEnhancements1] in
  def : Pat<(fcopysign FP32:$src1, (f32 (fpround (f128 FP128:$src2)))),
            (CPSDRsd FP32:$src1, (EXTRACT_SUBREG FP128:$src2, subreg_h64))>;
let Predicates = [FeatureVectorEnhancements1] in
  def : Pat<(fcopysign FP32:$src1, (f32 (fpround (f128 VR128:$src2)))),
            (CPSDRsd FP32:$src1, (EXTRACT_SUBREG VR128:$src2, subreg_h64))>;

// fcopysign with an FP64 result.
let isCodeGenOnly = 1 in
  def CPSDRds : BinaryRRFb<"cpsdr", 0xB372, fcopysign, FP64, FP64, FP32>;
def CPSDRdd : BinaryRRFb<"cpsdr", 0xB372, fcopysign, FP64, FP64, FP64>;

// The sign of an FP128 is in the high register.
let Predicates = [FeatureNoVectorEnhancements1] in
  def : Pat<(fcopysign FP64:$src1, (f64 (fpround (f128 FP128:$src2)))),
            (CPSDRdd FP64:$src1, (EXTRACT_SUBREG FP128:$src2, subreg_h64))>;
let Predicates = [FeatureVectorEnhancements1] in
  def : Pat<(fcopysign FP64:$src1, (f64 (fpround (f128 VR128:$src2)))),
            (CPSDRdd FP64:$src1, (EXTRACT_SUBREG VR128:$src2, subreg_h64))>;

// fcopysign with an FP128 result.  Use "upper" as the high half and leave
// the low half as-is.
class CopySign128<RegisterOperand cls, dag upper>
  : Pat<(fcopysign FP128:$src1, cls:$src2),
        (INSERT_SUBREG FP128:$src1, upper, subreg_h64)>;

let Predicates = [FeatureNoVectorEnhancements1] in {
  def : CopySign128<FP32,  (CPSDRds (EXTRACT_SUBREG FP128:$src1, subreg_h64),
                                    FP32:$src2)>;
  def : CopySign128<FP64,  (CPSDRdd (EXTRACT_SUBREG FP128:$src1, subreg_h64),
                                    FP64:$src2)>;
  def : CopySign128<FP128, (CPSDRdd (EXTRACT_SUBREG FP128:$src1, subreg_h64),
                                    (EXTRACT_SUBREG FP128:$src2, subreg_h64))>;
}

defm LoadStoreF32  : MVCLoadStore<load, f32,  MVCSequence, 4>;
defm LoadStoreF64  : MVCLoadStore<load, f64,  MVCSequence, 8>;
defm LoadStoreF128 : MVCLoadStore<load, f128, MVCSequence, 16>;

//===----------------------------------------------------------------------===//
// Load instructions
//===----------------------------------------------------------------------===//

let canFoldAsLoad = 1, SimpleBDXLoad = 1, mayLoad = 1 in {
  defm LE : UnaryRXPair<"le", 0x78, 0xED64, load, FP32, 4>;
  defm LD : UnaryRXPair<"ld", 0x68, 0xED65, load, FP64, 8>;

  // For z13 we prefer LDE over LE to avoid partial register dependencies.
  let isCodeGenOnly = 1 in
    def LDE32 : UnaryRXE<"lde", 0xED24, null_frag, FP32, 4>;

  // These instructions are split after register allocation, so we don't
  // want a custom inserter.
  let Has20BitOffset = 1, HasIndex = 1, Is128Bit = 1 in {
    def LX : Pseudo<(outs FP128:$dst), (ins bdxaddr20only128:$src),
                     [(set FP128:$dst, (load bdxaddr20only128:$src))]>;
  }
}

//===----------------------------------------------------------------------===//
// Store instructions
//===----------------------------------------------------------------------===//

let SimpleBDXStore = 1, mayStore = 1 in {
  defm STE : StoreRXPair<"ste", 0x70, 0xED66, store, FP32, 4>;
  defm STD : StoreRXPair<"std", 0x60, 0xED67, store, FP64, 8>;

  // These instructions are split after register allocation, so we don't
  // want a custom inserter.
  let Has20BitOffset = 1, HasIndex = 1, Is128Bit = 1 in {
    def STX : Pseudo<(outs), (ins FP128:$src, bdxaddr20only128:$dst),
                     [(store FP128:$src, bdxaddr20only128:$dst)]>;
  }
}

//===----------------------------------------------------------------------===//
// Conversion instructions
//===----------------------------------------------------------------------===//

// Convert floating-point values to narrower representations, rounding
// according to the current mode.  The destination of LEXBR and LDXBR
// is a 128-bit value, but only the first register of the pair is used.
def LEDBR : UnaryRRE<"ledbr", 0xB344, fpround,    FP32,  FP64>;
def LEXBR : UnaryRRE<"lexbr", 0xB346, null_frag, FP128, FP128>;
def LDXBR : UnaryRRE<"ldxbr", 0xB345, null_frag, FP128, FP128>;

def LEDBRA : TernaryRRFe<"ledbra", 0xB344, FP32,  FP64>,
             Requires<[FeatureFPExtension]>;
def LEXBRA : TernaryRRFe<"lexbra", 0xB346, FP128, FP128>,
             Requires<[FeatureFPExtension]>;
def LDXBRA : TernaryRRFe<"ldxbra", 0xB345, FP128, FP128>,
             Requires<[FeatureFPExtension]>;

let Predicates = [FeatureNoVectorEnhancements1] in {
  def : Pat<(f32 (fpround FP128:$src)),
            (EXTRACT_SUBREG (LEXBR FP128:$src), subreg_hh32)>;
  def : Pat<(f64 (fpround FP128:$src)),
            (EXTRACT_SUBREG (LDXBR FP128:$src), subreg_h64)>;
}

// Extend register floating-point values to wider representations.
def LDEBR : UnaryRRE<"ldebr", 0xB304, fpextend,  FP64,  FP32>;
def LXEBR : UnaryRRE<"lxebr", 0xB306, null_frag, FP128, FP32>;
def LXDBR : UnaryRRE<"lxdbr", 0xB305, null_frag, FP128, FP64>;
let Predicates = [FeatureNoVectorEnhancements1] in {
  def : Pat<(f128 (fpextend (f32 FP32:$src))), (LXEBR FP32:$src)>;
  def : Pat<(f128 (fpextend (f64 FP64:$src))), (LXDBR FP64:$src)>;
}

// Extend memory floating-point values to wider representations.
def LDEB : UnaryRXE<"ldeb", 0xED04, extloadf32, FP64,  4>;
def LXEB : UnaryRXE<"lxeb", 0xED06, null_frag,  FP128, 4>;
def LXDB : UnaryRXE<"lxdb", 0xED05, null_frag,  FP128, 8>;
let Predicates = [FeatureNoVectorEnhancements1] in {
  def : Pat<(f128 (extloadf32 bdxaddr12only:$src)),
            (LXEB bdxaddr12only:$src)>;
  def : Pat<(f128 (extloadf64 bdxaddr12only:$src)),
            (LXDB bdxaddr12only:$src)>;
}

// Convert a signed integer register value to a floating-point one.
def CEFBR : UnaryRRE<"cefbr", 0xB394, sint_to_fp, FP32,  GR32>;
def CDFBR : UnaryRRE<"cdfbr", 0xB395, sint_to_fp, FP64,  GR32>;
def CXFBR : UnaryRRE<"cxfbr", 0xB396, sint_to_fp, FP128, GR32>;

def CEGBR : UnaryRRE<"cegbr", 0xB3A4, sint_to_fp, FP32,  GR64>;
def CDGBR : UnaryRRE<"cdgbr", 0xB3A5, sint_to_fp, FP64,  GR64>;
def CXGBR : UnaryRRE<"cxgbr", 0xB3A6, sint_to_fp, FP128, GR64>;

// The FP extension feature provides versions of the above that allow
// specifying rounding mode and inexact-exception suppression flags.
let Predicates = [FeatureFPExtension] in {
  def CEFBRA : TernaryRRFe<"cefbra", 0xB394, FP32,  GR32>;
  def CDFBRA : TernaryRRFe<"cdfbra", 0xB395, FP64,  GR32>;
  def CXFBRA : TernaryRRFe<"cxfbra", 0xB396, FP128, GR32>;

  def CEGBRA : TernaryRRFe<"cegbra", 0xB3A4, FP32,  GR64>;
  def CDGBRA : TernaryRRFe<"cdgbra", 0xB3A5, FP64,  GR64>;
  def CXGBRA : TernaryRRFe<"cxgbra", 0xB3A6, FP128, GR64>;
}

// Convert am unsigned integer register value to a floating-point one.
let Predicates = [FeatureFPExtension] in {
  def CELFBR : TernaryRRFe<"celfbr", 0xB390, FP32,  GR32>;
  def CDLFBR : TernaryRRFe<"cdlfbr", 0xB391, FP64,  GR32>;
  def CXLFBR : TernaryRRFe<"cxlfbr", 0xB392, FP128, GR32>;

  def CELGBR : TernaryRRFe<"celgbr", 0xB3A0, FP32,  GR64>;
  def CDLGBR : TernaryRRFe<"cdlgbr", 0xB3A1, FP64,  GR64>;
  def CXLGBR : TernaryRRFe<"cxlgbr", 0xB3A2, FP128, GR64>;

  def : Pat<(f32  (uint_to_fp GR32:$src)), (CELFBR 0, GR32:$src, 0)>;
  def : Pat<(f64  (uint_to_fp GR32:$src)), (CDLFBR 0, GR32:$src, 0)>;
  def : Pat<(f128 (uint_to_fp GR32:$src)), (CXLFBR 0, GR32:$src, 0)>;

  def : Pat<(f32  (uint_to_fp GR64:$src)), (CELGBR 0, GR64:$src, 0)>;
  def : Pat<(f64  (uint_to_fp GR64:$src)), (CDLGBR 0, GR64:$src, 0)>;
  def : Pat<(f128 (uint_to_fp GR64:$src)), (CXLGBR 0, GR64:$src, 0)>;
}

// Convert a floating-point register value to a signed integer value,
// with the second operand (modifier M3) specifying the rounding mode.
let Defs = [CC] in {
  def CFEBR : BinaryRRFe<"cfebr", 0xB398, GR32, FP32>;
  def CFDBR : BinaryRRFe<"cfdbr", 0xB399, GR32, FP64>;
  def CFXBR : BinaryRRFe<"cfxbr", 0xB39A, GR32, FP128>;

  def CGEBR : BinaryRRFe<"cgebr", 0xB3A8, GR64, FP32>;
  def CGDBR : BinaryRRFe<"cgdbr", 0xB3A9, GR64, FP64>;
  def CGXBR : BinaryRRFe<"cgxbr", 0xB3AA, GR64, FP128>;
}

// fp_to_sint always rounds towards zero, which is modifier value 5.
def : Pat<(i32 (fp_to_sint FP32:$src)),  (CFEBR 5, FP32:$src)>;
def : Pat<(i32 (fp_to_sint FP64:$src)),  (CFDBR 5, FP64:$src)>;
def : Pat<(i32 (fp_to_sint FP128:$src)), (CFXBR 5, FP128:$src)>;

def : Pat<(i64 (fp_to_sint FP32:$src)),  (CGEBR 5, FP32:$src)>;
def : Pat<(i64 (fp_to_sint FP64:$src)),  (CGDBR 5, FP64:$src)>;
def : Pat<(i64 (fp_to_sint FP128:$src)), (CGXBR 5, FP128:$src)>;

// The FP extension feature provides versions of the above that allow
// also specifying the inexact-exception suppression flag.
let Predicates = [FeatureFPExtension], Defs = [CC] in {
  def CFEBRA : TernaryRRFe<"cfebra", 0xB398, GR32, FP32>;
  def CFDBRA : TernaryRRFe<"cfdbra", 0xB399, GR32, FP64>;
  def CFXBRA : TernaryRRFe<"cfxbra", 0xB39A, GR32, FP128>;

  def CGEBRA : TernaryRRFe<"cgebra", 0xB3A8, GR64, FP32>;
  def CGDBRA : TernaryRRFe<"cgdbra", 0xB3A9, GR64, FP64>;
  def CGXBRA : TernaryRRFe<"cgxbra", 0xB3AA, GR64, FP128>;
}

// Convert a floating-point register value to an unsigned integer value.
let Predicates = [FeatureFPExtension] in {
  let Defs = [CC] in {
    def CLFEBR : TernaryRRFe<"clfebr", 0xB39C, GR32, FP32>;
    def CLFDBR : TernaryRRFe<"clfdbr", 0xB39D, GR32, FP64>;
    def CLFXBR : TernaryRRFe<"clfxbr", 0xB39E, GR32, FP128>;

    def CLGEBR : TernaryRRFe<"clgebr", 0xB3AC, GR64, FP32>;
    def CLGDBR : TernaryRRFe<"clgdbr", 0xB3AD, GR64, FP64>;
    def CLGXBR : TernaryRRFe<"clgxbr", 0xB3AE, GR64, FP128>;
  }

  def : Pat<(i32 (fp_to_uint FP32:$src)),  (CLFEBR 5, FP32:$src,  0)>;
  def : Pat<(i32 (fp_to_uint FP64:$src)),  (CLFDBR 5, FP64:$src,  0)>;
  def : Pat<(i32 (fp_to_uint FP128:$src)), (CLFXBR 5, FP128:$src, 0)>;

  def : Pat<(i64 (fp_to_uint FP32:$src)),  (CLGEBR 5, FP32:$src,  0)>;
  def : Pat<(i64 (fp_to_uint FP64:$src)),  (CLGDBR 5, FP64:$src,  0)>;
  def : Pat<(i64 (fp_to_uint FP128:$src)), (CLGXBR 5, FP128:$src, 0)>;
}


//===----------------------------------------------------------------------===//
// Unary arithmetic
//===----------------------------------------------------------------------===//

// We prefer generic instructions during isel, because they do not
// clobber CC and therefore give the scheduler more freedom. In cases
// the CC is actually useful, the SystemZElimCompare pass will try to
// convert generic instructions into opcodes that also set CC. Note
// that lcdf / lpdf / lndf only affect the sign bit, and can therefore
// be used with fp32 as well. This could be done for fp128, in which
// case the operands would have to be tied.

// Negation (Load Complement).
let Defs = [CC], CCValues = 0xF, CompareZeroCCMask = 0xF in {
  def LCEBR : UnaryRRE<"lcebr", 0xB303, null_frag, FP32,  FP32>;
  def LCDBR : UnaryRRE<"lcdbr", 0xB313, null_frag, FP64,  FP64>;
  def LCXBR : UnaryRRE<"lcxbr", 0xB343, fneg, FP128, FP128>;
}
// Generic form, which does not set CC.
def LCDFR : UnaryRRE<"lcdfr", 0xB373, fneg, FP64,  FP64>;
let isCodeGenOnly = 1 in
  def LCDFR_32 : UnaryRRE<"lcdfr", 0xB373, fneg, FP32,  FP32>;

// Absolute value (Load Positive).
let Defs = [CC], CCValues = 0xF, CompareZeroCCMask = 0xF in {
  def LPEBR : UnaryRRE<"lpebr", 0xB300, null_frag, FP32,  FP32>;
  def LPDBR : UnaryRRE<"lpdbr", 0xB310, null_frag, FP64,  FP64>;
  def LPXBR : UnaryRRE<"lpxbr", 0xB340, fabs, FP128, FP128>;
}
// Generic form, which does not set CC.
def LPDFR : UnaryRRE<"lpdfr", 0xB370, fabs, FP64,  FP64>;
let isCodeGenOnly = 1 in
  def LPDFR_32 : UnaryRRE<"lpdfr", 0xB370, fabs, FP32,  FP32>;

// Negative absolute value (Load Negative).
let Defs = [CC], CCValues = 0xF, CompareZeroCCMask = 0xF in {
  def LNEBR : UnaryRRE<"lnebr", 0xB301, null_frag, FP32,  FP32>;
  def LNDBR : UnaryRRE<"lndbr", 0xB311, null_frag, FP64,  FP64>;
  def LNXBR : UnaryRRE<"lnxbr", 0xB341, fnabs, FP128, FP128>;
}
// Generic form, which does not set CC.
def LNDFR : UnaryRRE<"lndfr", 0xB371, fnabs, FP64,  FP64>;
let isCodeGenOnly = 1 in
  def LNDFR_32 : UnaryRRE<"lndfr", 0xB371, fnabs, FP32,  FP32>;

// Square root.
def SQEBR : UnaryRRE<"sqebr", 0xB314, fsqrt, FP32,  FP32>;
def SQDBR : UnaryRRE<"sqdbr", 0xB315, fsqrt, FP64,  FP64>;
def SQXBR : UnaryRRE<"sqxbr", 0xB316, fsqrt, FP128, FP128>;

def SQEB : UnaryRXE<"sqeb", 0xED14, loadu<fsqrt>, FP32, 4>;
def SQDB : UnaryRXE<"sqdb", 0xED15, loadu<fsqrt>, FP64, 8>;

// Round to an integer, with the second operand (modifier M3) specifying
// the rounding mode.  These forms always check for inexact conditions.
def FIEBR : BinaryRRFe<"fiebr", 0xB357, FP32,  FP32>;
def FIDBR : BinaryRRFe<"fidbr", 0xB35F, FP64,  FP64>;
def FIXBR : BinaryRRFe<"fixbr", 0xB347, FP128, FP128>;

// frint rounds according to the current mode (modifier 0) and detects
// inexact conditions.
def : Pat<(frint FP32:$src),  (FIEBR 0, FP32:$src)>;
def : Pat<(frint FP64:$src),  (FIDBR 0, FP64:$src)>;
def : Pat<(frint FP128:$src), (FIXBR 0, FP128:$src)>;

let Predicates = [FeatureFPExtension] in {
  // Extended forms of the FIxBR instructions.  M4 can be set to 4
  // to suppress detection of inexact conditions.
  def FIEBRA : TernaryRRFe<"fiebra", 0xB357, FP32,  FP32>;
  def FIDBRA : TernaryRRFe<"fidbra", 0xB35F, FP64,  FP64>;
  def FIXBRA : TernaryRRFe<"fixbra", 0xB347, FP128, FP128>;

  // fnearbyint is like frint but does not detect inexact conditions.
  def : Pat<(fnearbyint FP32:$src),  (FIEBRA 0, FP32:$src,  4)>;
  def : Pat<(fnearbyint FP64:$src),  (FIDBRA 0, FP64:$src,  4)>;
  def : Pat<(fnearbyint FP128:$src), (FIXBRA 0, FP128:$src, 4)>;

  // floor is no longer allowed to raise an inexact condition,
  // so restrict it to the cases where the condition can be suppressed.
  // Mode 7 is round towards -inf.
  def : Pat<(ffloor FP32:$src),  (FIEBRA 7, FP32:$src,  4)>;
  def : Pat<(ffloor FP64:$src),  (FIDBRA 7, FP64:$src,  4)>;
  def : Pat<(ffloor FP128:$src), (FIXBRA 7, FP128:$src, 4)>;

  // Same idea for ceil, where mode 6 is round towards +inf.
  def : Pat<(fceil FP32:$src),  (FIEBRA 6, FP32:$src,  4)>;
  def : Pat<(fceil FP64:$src),  (FIDBRA 6, FP64:$src,  4)>;
  def : Pat<(fceil FP128:$src), (FIXBRA 6, FP128:$src, 4)>;

  // Same idea for trunc, where mode 5 is round towards zero.
  def : Pat<(ftrunc FP32:$src),  (FIEBRA 5, FP32:$src,  4)>;
  def : Pat<(ftrunc FP64:$src),  (FIDBRA 5, FP64:$src,  4)>;
  def : Pat<(ftrunc FP128:$src), (FIXBRA 5, FP128:$src, 4)>;

  // Same idea for round, where mode 1 is round towards nearest with
  // ties away from zero.
  def : Pat<(fround FP32:$src),  (FIEBRA 1, FP32:$src,  4)>;
  def : Pat<(fround FP64:$src),  (FIDBRA 1, FP64:$src,  4)>;
  def : Pat<(fround FP128:$src), (FIXBRA 1, FP128:$src, 4)>;
}

//===----------------------------------------------------------------------===//
// Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition.
let Defs = [CC], CCValues = 0xF, CompareZeroCCMask = 0xF in {
  let isCommutable = 1 in {
    def AEBR : BinaryRRE<"aebr", 0xB30A, fadd, FP32,  FP32>;
    def ADBR : BinaryRRE<"adbr", 0xB31A, fadd, FP64,  FP64>;
    def AXBR : BinaryRRE<"axbr", 0xB34A, fadd, FP128, FP128>;
  }
  def AEB : BinaryRXE<"aeb", 0xED0A, fadd, FP32, load, 4>;
  def ADB : BinaryRXE<"adb", 0xED1A, fadd, FP64, load, 8>;
}

// Subtraction.
let Defs = [CC], CCValues = 0xF, CompareZeroCCMask = 0xF in {
  def SEBR : BinaryRRE<"sebr", 0xB30B, fsub, FP32,  FP32>;
  def SDBR : BinaryRRE<"sdbr", 0xB31B, fsub, FP64,  FP64>;
  def SXBR : BinaryRRE<"sxbr", 0xB34B, fsub, FP128, FP128>;

  def SEB : BinaryRXE<"seb",  0xED0B, fsub, FP32, load, 4>;
  def SDB : BinaryRXE<"sdb",  0xED1B, fsub, FP64, load, 8>;
}

// Multiplication.
let isCommutable = 1 in {
  def MEEBR : BinaryRRE<"meebr", 0xB317, fmul, FP32,  FP32>;
  def MDBR  : BinaryRRE<"mdbr",  0xB31C, fmul, FP64,  FP64>;
  def MXBR  : BinaryRRE<"mxbr",  0xB34C, fmul, FP128, FP128>;
}
def MEEB : BinaryRXE<"meeb", 0xED17, fmul, FP32, load, 4>;
def MDB  : BinaryRXE<"mdb",  0xED1C, fmul, FP64, load, 8>;

// f64 multiplication of two FP32 registers.
def MDEBR : BinaryRRE<"mdebr", 0xB30C, null_frag, FP64, FP32>;
def : Pat<(fmul (f64 (fpextend FP32:$src1)), (f64 (fpextend FP32:$src2))),
          (MDEBR (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                FP32:$src1, subreg_h32), FP32:$src2)>;

// f64 multiplication of an FP32 register and an f32 memory.
def MDEB : BinaryRXE<"mdeb", 0xED0C, null_frag, FP64, load, 4>;
def : Pat<(fmul (f64 (fpextend FP32:$src1)),
                (f64 (extloadf32 bdxaddr12only:$addr))),
          (MDEB (INSERT_SUBREG (f64 (IMPLICIT_DEF)), FP32:$src1, subreg_h32),
                bdxaddr12only:$addr)>;

// f128 multiplication of two FP64 registers.
def MXDBR : BinaryRRE<"mxdbr", 0xB307, null_frag, FP128, FP64>;
let Predicates = [FeatureNoVectorEnhancements1] in
  def : Pat<(fmul (f128 (fpextend FP64:$src1)), (f128 (fpextend FP64:$src2))),
            (MXDBR (INSERT_SUBREG (f128 (IMPLICIT_DEF)),
                                  FP64:$src1, subreg_h64), FP64:$src2)>;

// f128 multiplication of an FP64 register and an f64 memory.
def MXDB : BinaryRXE<"mxdb", 0xED07, null_frag, FP128, load, 8>;
let Predicates = [FeatureNoVectorEnhancements1] in
  def : Pat<(fmul (f128 (fpextend FP64:$src1)),
                  (f128 (extloadf64 bdxaddr12only:$addr))),
            (MXDB (INSERT_SUBREG (f128 (IMPLICIT_DEF)), FP64:$src1, subreg_h64),
                  bdxaddr12only:$addr)>;

// Fused multiply-add.
def MAEBR : TernaryRRD<"maebr", 0xB30E, z_fma, FP32, FP32>;
def MADBR : TernaryRRD<"madbr", 0xB31E, z_fma, FP64, FP64>;

def MAEB : TernaryRXF<"maeb", 0xED0E, z_fma, FP32, FP32, load, 4>;
def MADB : TernaryRXF<"madb", 0xED1E, z_fma, FP64, FP64, load, 8>;

// Fused multiply-subtract.
def MSEBR : TernaryRRD<"msebr", 0xB30F, z_fms, FP32, FP32>;
def MSDBR : TernaryRRD<"msdbr", 0xB31F, z_fms, FP64, FP64>;

def MSEB : TernaryRXF<"mseb", 0xED0F, z_fms, FP32, FP32, load, 4>;
def MSDB : TernaryRXF<"msdb", 0xED1F, z_fms, FP64, FP64, load, 8>;

// Division.
def DEBR : BinaryRRE<"debr", 0xB30D, fdiv, FP32,  FP32>;
def DDBR : BinaryRRE<"ddbr", 0xB31D, fdiv, FP64,  FP64>;
def DXBR : BinaryRRE<"dxbr", 0xB34D, fdiv, FP128, FP128>;

def DEB : BinaryRXE<"deb", 0xED0D, fdiv, FP32, load, 4>;
def DDB : BinaryRXE<"ddb", 0xED1D, fdiv, FP64, load, 8>;

// Divide to integer.
let Defs = [CC] in {
  def DIEBR : TernaryRRFb<"diebr", 0xB353, FP32, FP32, FP32>;
  def DIDBR : TernaryRRFb<"didbr", 0xB35B, FP64, FP64, FP64>;
}

//===----------------------------------------------------------------------===//
// Comparisons
//===----------------------------------------------------------------------===//

let Defs = [CC], CCValues = 0xF in {
  def CEBR : CompareRRE<"cebr", 0xB309, z_fcmp, FP32,  FP32>;
  def CDBR : CompareRRE<"cdbr", 0xB319, z_fcmp, FP64,  FP64>;
  def CXBR : CompareRRE<"cxbr", 0xB349, z_fcmp, FP128, FP128>;

  def CEB : CompareRXE<"ceb", 0xED09, z_fcmp, FP32, load, 4>;
  def CDB : CompareRXE<"cdb", 0xED19, z_fcmp, FP64, load, 8>;

  def KEBR : CompareRRE<"kebr", 0xB308, null_frag, FP32,  FP32>;
  def KDBR : CompareRRE<"kdbr", 0xB318, null_frag, FP64,  FP64>;
  def KXBR : CompareRRE<"kxbr", 0xB348, null_frag, FP128, FP128>;

  def KEB : CompareRXE<"keb", 0xED08, null_frag, FP32, load, 4>;
  def KDB : CompareRXE<"kdb", 0xED18, null_frag, FP64, load, 8>;
}

// Test Data Class.
let Defs = [CC], CCValues = 0xC in {
  def TCEB : TestRXE<"tceb", 0xED10, z_tdc, FP32>;
  def TCDB : TestRXE<"tcdb", 0xED11, z_tdc, FP64>;
  def TCXB : TestRXE<"tcxb", 0xED12, z_tdc, FP128>;
}

//===----------------------------------------------------------------------===//
// Floating-point control register instructions
//===----------------------------------------------------------------------===//

let hasSideEffects = 1 in {
  let mayLoad = 1, mayStore = 1 in {
    // TODO: EFPC and SFPC do not touch memory at all
    def EFPC  : InherentRRE<"efpc", 0xB38C, GR32, int_s390_efpc>;
    def STFPC : StoreInherentS<"stfpc", 0xB29C, storei<int_s390_efpc>, 4>;

    def SFPC : SideEffectUnaryRRE<"sfpc", 0xB384, GR32, int_s390_sfpc>;
    def LFPC : SideEffectUnaryS<"lfpc", 0xB29D, loadu<int_s390_sfpc>, 4>;
  }

  def SFASR : SideEffectUnaryRRE<"sfasr", 0xB385, GR32, null_frag>;
  def LFAS  : SideEffectUnaryS<"lfas", 0xB2BD, null_frag, 4>;

  def SRNMB : SideEffectAddressS<"srnmb", 0xB2B8, null_frag, shift12only>,
              Requires<[FeatureFPExtension]>;
  def SRNM  : SideEffectAddressS<"srnm", 0xB299, null_frag, shift12only>;
  def SRNMT : SideEffectAddressS<"srnmt", 0xB2B9, null_frag, shift12only>;
}

//===----------------------------------------------------------------------===//
// Peepholes
//===----------------------------------------------------------------------===//

def : Pat<(f32  fpimmneg0), (LCDFR_32 (LZER))>;
def : Pat<(f64  fpimmneg0), (LCDFR (LZDR))>;
def : Pat<(f128 fpimmneg0), (LCXBR (LZXR))>;
