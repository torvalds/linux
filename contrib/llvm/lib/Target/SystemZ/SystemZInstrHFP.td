//==- SystemZInstrHFP.td - Floating-point SystemZ instructions -*- tblgen-*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The instructions in this file implement SystemZ hexadecimal floating-point
// arithmetic.  Since this format is not mapped to any source-language data
// type, these instructions are not used for code generation, but are provided
// for use with the assembler and disassembler only.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

// Load and test.
let Defs = [CC] in {
  def LTER : UnaryRR <"lter", 0x32,   null_frag, FP32,  FP32>;
  def LTDR : UnaryRR <"ltdr", 0x22,   null_frag, FP64,  FP64>;
  def LTXR : UnaryRRE<"ltxr", 0xB362, null_frag, FP128, FP128>;
}

//===----------------------------------------------------------------------===//
// Conversion instructions
//===----------------------------------------------------------------------===//

// Convert floating-point values to narrower representations.
def LEDR : UnaryRR <"ledr", 0x35,   null_frag, FP32, FP64>;
def LEXR : UnaryRRE<"lexr", 0xB366, null_frag, FP32, FP128>;
def LDXR : UnaryRR <"ldxr", 0x25,   null_frag, FP64, FP128>;
let isAsmParserOnly = 1 in {
  def LRER : UnaryRR <"lrer", 0x35, null_frag, FP32, FP64>;
  def LRDR : UnaryRR <"lrdr", 0x25, null_frag, FP64, FP128>;
}

// Extend floating-point values to wider representations.
def LDER : UnaryRRE<"lder", 0xB324, null_frag, FP64,  FP32>;
def LXER : UnaryRRE<"lxer", 0xB326, null_frag, FP128, FP32>;
def LXDR : UnaryRRE<"lxdr", 0xB325, null_frag, FP128, FP64>;

def LDE : UnaryRXE<"lde", 0xED24, null_frag, FP64,  4>;
def LXE : UnaryRXE<"lxe", 0xED26, null_frag, FP128, 4>;
def LXD : UnaryRXE<"lxd", 0xED25, null_frag, FP128, 8>;

// Convert a signed integer register value to a floating-point one.
def CEFR : UnaryRRE<"cefr", 0xB3B4, null_frag, FP32,  GR32>;
def CDFR : UnaryRRE<"cdfr", 0xB3B5, null_frag, FP64,  GR32>;
def CXFR : UnaryRRE<"cxfr", 0xB3B6, null_frag, FP128, GR32>;

def CEGR : UnaryRRE<"cegr", 0xB3C4, null_frag, FP32,  GR64>;
def CDGR : UnaryRRE<"cdgr", 0xB3C5, null_frag, FP64,  GR64>;
def CXGR : UnaryRRE<"cxgr", 0xB3C6, null_frag, FP128, GR64>;

// Convert a floating-point register value to a signed integer value,
// with the second operand (modifier M3) specifying the rounding mode.
let Defs = [CC] in {
  def CFER : BinaryRRFe<"cfer", 0xB3B8, GR32, FP32>;
  def CFDR : BinaryRRFe<"cfdr", 0xB3B9, GR32, FP64>;
  def CFXR : BinaryRRFe<"cfxr", 0xB3BA, GR32, FP128>;

  def CGER : BinaryRRFe<"cger", 0xB3C8, GR64, FP32>;
  def CGDR : BinaryRRFe<"cgdr", 0xB3C9, GR64, FP64>;
  def CGXR : BinaryRRFe<"cgxr", 0xB3CA, GR64, FP128>;
}

// Convert BFP to HFP.
let Defs = [CC] in {
  def THDER : UnaryRRE<"thder", 0xB358, null_frag, FP64, FP32>;
  def THDR  : UnaryRRE<"thdr",  0xB359, null_frag, FP64, FP64>;
}

// Convert HFP to BFP.
let Defs = [CC] in {
  def TBEDR : BinaryRRFe<"tbedr", 0xB350, FP32, FP64>;
  def TBDR  : BinaryRRFe<"tbdr",  0xB351, FP64, FP64>;
}


//===----------------------------------------------------------------------===//
// Unary arithmetic
//===----------------------------------------------------------------------===//

// Negation (Load Complement).
let Defs = [CC] in {
  def LCER : UnaryRR <"lcer", 0x33,   null_frag, FP32,  FP32>;
  def LCDR : UnaryRR <"lcdr", 0x23,   null_frag, FP64,  FP64>;
  def LCXR : UnaryRRE<"lcxr", 0xB363, null_frag, FP128, FP128>;
}

// Absolute value (Load Positive).
let Defs = [CC] in {
  def LPER : UnaryRR <"lper", 0x30,   null_frag, FP32,  FP32>;
  def LPDR : UnaryRR <"lpdr", 0x20,   null_frag, FP64,  FP64>;
  def LPXR : UnaryRRE<"lpxr", 0xB360, null_frag, FP128, FP128>;
}

// Negative absolute value (Load Negative).
let Defs = [CC] in {
  def LNER : UnaryRR <"lner", 0x31,   null_frag, FP32,  FP32>;
  def LNDR : UnaryRR <"lndr", 0x21,   null_frag, FP64,  FP64>;
  def LNXR : UnaryRRE<"lnxr", 0xB361, null_frag, FP128, FP128>;
}

// Halve.
def HER : UnaryRR <"her", 0x34, null_frag, FP32, FP32>;
def HDR : UnaryRR <"hdr", 0x24, null_frag, FP64, FP64>;

// Square root.
def SQER : UnaryRRE<"sqer", 0xB245, null_frag, FP32,  FP32>;
def SQDR : UnaryRRE<"sqdr", 0xB244, null_frag, FP64,  FP64>;
def SQXR : UnaryRRE<"sqxr", 0xB336, null_frag, FP128, FP128>;

def SQE : UnaryRXE<"sqe", 0xED34, null_frag, FP32, 4>;
def SQD : UnaryRXE<"sqd", 0xED35, null_frag, FP64, 8>;

// Round to an integer (rounding towards zero).
def FIER : UnaryRRE<"fier", 0xB377, null_frag, FP32,  FP32>;
def FIDR : UnaryRRE<"fidr", 0xB37F, null_frag, FP64,  FP64>;
def FIXR : UnaryRRE<"fixr", 0xB367, null_frag, FP128, FP128>;


//===----------------------------------------------------------------------===//
// Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition.
let Defs = [CC] in {
  let isCommutable = 1 in {
    def AER : BinaryRR<"aer", 0x3A, null_frag, FP32,  FP32>;
    def ADR : BinaryRR<"adr", 0x2A, null_frag, FP64,  FP64>;
    def AXR : BinaryRR<"axr", 0x36, null_frag, FP128, FP128>;
  }
  def AE : BinaryRX<"ae", 0x7A, null_frag, FP32, load, 4>;
  def AD : BinaryRX<"ad", 0x6A, null_frag, FP64, load, 8>;
}

// Addition (unnormalized).
let Defs = [CC] in {
  let isCommutable = 1 in {
    def AUR : BinaryRR<"aur", 0x3E, null_frag, FP32, FP32>;
    def AWR : BinaryRR<"awr", 0x2E, null_frag, FP64, FP64>;
  }
  def AU : BinaryRX<"au", 0x7E, null_frag, FP32, load, 4>;
  def AW : BinaryRX<"aw", 0x6E, null_frag, FP64, load, 8>;
}

// Subtraction.
let Defs = [CC] in {
  def SER : BinaryRR<"ser", 0x3B, null_frag, FP32,  FP32>;
  def SDR : BinaryRR<"sdr", 0x2B, null_frag, FP64,  FP64>;
  def SXR : BinaryRR<"sxr", 0x37, null_frag, FP128, FP128>;

  def SE : BinaryRX<"se", 0x7B, null_frag, FP32, load, 4>;
  def SD : BinaryRX<"sd", 0x6B, null_frag, FP64, load, 8>;
}

// Subtraction (unnormalized).
let Defs = [CC] in {
  def SUR : BinaryRR<"sur", 0x3F, null_frag, FP32, FP32>;
  def SWR : BinaryRR<"swr", 0x2F, null_frag, FP64, FP64>;

  def SU : BinaryRX<"su", 0x7F, null_frag, FP32, load, 4>;
  def SW : BinaryRX<"sw", 0x6F, null_frag, FP64, load, 8>;
}

// Multiplication.
let isCommutable = 1 in {
  def MEER : BinaryRRE<"meer", 0xB337, null_frag, FP32,  FP32>;
  def MDR  : BinaryRR <"mdr",  0x2C,   null_frag, FP64,  FP64>;
  def MXR  : BinaryRR <"mxr",  0x26,   null_frag, FP128, FP128>;
}
def MEE : BinaryRXE<"mee", 0xED37, null_frag, FP32, load, 4>;
def MD  : BinaryRX <"md",  0x6C,   null_frag, FP64, load, 8>;

// Extending multiplication (f32 x f32 -> f64).
def MDER : BinaryRR<"mder", 0x3C, null_frag, FP64, FP32>;
def MDE  : BinaryRX<"mde",  0x7C, null_frag, FP64, load, 4>;
let isAsmParserOnly = 1 in {
  def MER : BinaryRR<"mer", 0x3C, null_frag, FP64, FP32>;
  def ME  : BinaryRX<"me",  0x7C, null_frag, FP64, load, 4>;
}

// Extending multiplication (f64 x f64 -> f128).
def MXDR : BinaryRR<"mxdr", 0x27, null_frag, FP128, FP64>;
def MXD  : BinaryRX<"mxd",  0x67, null_frag, FP128, load, 8>;

// Fused multiply-add.
def MAER : TernaryRRD<"maer", 0xB32E, null_frag, FP32, FP32>;
def MADR : TernaryRRD<"madr", 0xB33E, null_frag, FP64, FP64>;
def MAE  : TernaryRXF<"mae",  0xED2E, null_frag, FP32, FP32, load, 4>;
def MAD  : TernaryRXF<"mad",  0xED3E, null_frag, FP64, FP64, load, 8>;

// Fused multiply-subtract.
def MSER : TernaryRRD<"mser", 0xB32F, null_frag, FP32, FP32>;
def MSDR : TernaryRRD<"msdr", 0xB33F, null_frag, FP64, FP64>;
def MSE  : TernaryRXF<"mse",  0xED2F, null_frag, FP32, FP32, load, 4>;
def MSD  : TernaryRXF<"msd",  0xED3F, null_frag, FP64, FP64, load, 8>;

// Multiplication (unnormalized).
def MYR  : BinaryRRD<"myr",  0xB33B, null_frag, FP128, FP64>;
def MYHR : BinaryRRD<"myhr", 0xB33D, null_frag, FP64,  FP64>;
def MYLR : BinaryRRD<"mylr", 0xB339, null_frag, FP64,  FP64>;
def MY   : BinaryRXF<"my",   0xED3B, null_frag, FP128, FP64, load, 8>;
def MYH  : BinaryRXF<"myh",  0xED3D, null_frag, FP64,  FP64, load, 8>;
def MYL  : BinaryRXF<"myl",  0xED39, null_frag, FP64,  FP64, load, 8>;

// Fused multiply-add (unnormalized).
def MAYR  : TernaryRRD<"mayr",  0xB33A, null_frag, FP128, FP64>;
def MAYHR : TernaryRRD<"mayhr", 0xB33C, null_frag, FP64,  FP64>;
def MAYLR : TernaryRRD<"maylr", 0xB338, null_frag, FP64,  FP64>;
def MAY   : TernaryRXF<"may",   0xED3A, null_frag, FP128, FP64, load, 8>;
def MAYH  : TernaryRXF<"mayh",  0xED3C, null_frag, FP64,  FP64, load, 8>;
def MAYL  : TernaryRXF<"mayl",  0xED38, null_frag, FP64,  FP64, load, 8>;

// Division.
def DER : BinaryRR <"der", 0x3D,   null_frag, FP32,  FP32>;
def DDR : BinaryRR <"ddr", 0x2D,   null_frag, FP64,  FP64>;
def DXR : BinaryRRE<"dxr", 0xB22D, null_frag, FP128, FP128>;
def DE  : BinaryRX <"de",  0x7D,   null_frag, FP32, load, 4>;
def DD  : BinaryRX <"dd",  0x6D,   null_frag, FP64, load, 8>;


//===----------------------------------------------------------------------===//
// Comparisons
//===----------------------------------------------------------------------===//

let Defs = [CC] in {
  def CER : CompareRR <"cer", 0x39,   null_frag, FP32,  FP32>;
  def CDR : CompareRR <"cdr", 0x29,   null_frag, FP64,  FP64>;
  def CXR : CompareRRE<"cxr", 0xB369, null_frag, FP128, FP128>;

  def CE : CompareRX<"ce", 0x79, null_frag, FP32, load, 4>;
  def CD : CompareRX<"cd", 0x69, null_frag, FP64, load, 8>;
}

