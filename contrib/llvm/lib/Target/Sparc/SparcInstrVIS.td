//===---- SparcInstrVIS.td - Visual Instruction Set extensions (VIS) -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains instruction formats, definitions and patterns needed for
// VIS, VIS II, VIS II instructions on SPARC.
//===----------------------------------------------------------------------===//

// VIS Instruction Format.
class VISInstFormat<bits<9> opfval, dag outs, dag ins, string asmstr,
      list<dag> pattern>
      : F3_3<0b10, 0b110110, opfval, outs, ins, asmstr, pattern>;

class VISInst<bits<9> opfval, string OpcStr, RegisterClass RC = DFPRegs>
       : VISInstFormat<opfval,
        (outs RC:$rd), (ins RC:$rs1, RC:$rs2),
        !strconcat(OpcStr, " $rs1, $rs2, $rd"), []>;

// VIS Instruction with integer destination register.
class VISInstID<bits<9> opfval, string OpcStr>
       : VISInstFormat<opfval,
        (outs I64Regs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
        !strconcat(OpcStr, " $rs1, $rs2, $rd"), []>;

// For VIS Instructions with no operand.
let rd = 0, rs1 = 0, rs2 = 0 in
class VISInst0<bits<9> opfval, string asmstr>
       : VISInstFormat<opfval, (outs), (ins), asmstr, []>;

// For VIS Instructions with only rs1, rd operands.
let rs2 = 0 in
class VISInst1<bits<9> opfval, string OpcStr, RegisterClass RC = DFPRegs>
       : VISInstFormat<opfval,
        (outs RC:$rd), (ins RC:$rs1),
        !strconcat(OpcStr, " $rs1, $rd"), []>;

// For VIS Instructions with only rs2, rd operands.
let rs1 = 0 in
class VISInst2<bits<9> opfval, string OpcStr, RegisterClass RC = DFPRegs>
       : VISInstFormat<opfval,
        (outs RC:$rd), (ins RC:$rs2),
        !strconcat(OpcStr, " $rs2, $rd"), []>;

// For VIS Instructions with only rd operand.
let Constraints = "$rd = $f", rs1 = 0, rs2 = 0 in
class VISInstD<bits<9> opfval, string OpcStr, RegisterClass RC = DFPRegs>
       : VISInstFormat<opfval,
        (outs RC:$rd), (ins RC:$f),
        !strconcat(OpcStr, " $rd"), []>;

// VIS 1 Instructions
let Predicates = [HasVIS] in {

def FPADD16     : VISInst<0b001010000, "fpadd16">;
def FPADD16S    : VISInst<0b001010001, "fpadd16s">;
def FPADD32     : VISInst<0b001010010, "fpadd32">;
def FPADD32S    : VISInst<0b001010011, "fpadd32s">;
def FPSUB16     : VISInst<0b001010100, "fpsub16">;
def FPSUB16S    : VISInst<0b001010101, "fpsub16S">;
def FPSUB32     : VISInst<0b001010110, "fpsub32">;
def FPSUB32S    : VISInst<0b001010111, "fpsub32S">;

def FPACK16     : VISInst2<0b000111011, "fpack16">;
def FPACK32     : VISInst <0b000111010, "fpack32">;
def FPACKFIX    : VISInst2<0b000111101, "fpackfix">;
def FEXPAND     : VISInst2<0b001001101, "fexpand">;
def FPMERGE     : VISInst <0b001001011, "fpmerge">;

def FMUL8X16    : VISInst<0b000110001, "fmul8x16">;
def FMUL8X16AU  : VISInst<0b000110011, "fmul8x16au">;
def FMUL8X16AL  : VISInst<0b000110101, "fmul8x16al">;
def FMUL8SUX16  : VISInst<0b000110110, "fmul8sux16">;
def FMUL8ULX16  : VISInst<0b000110111, "fmul8ulx16">;
def FMULD8SUX16 : VISInst<0b000111000, "fmuld8sux16">;
def FMULD8ULX16 : VISInst<0b000111001, "fmuld8ulx16">;

def ALIGNADDR   : VISInst<0b000011000, "alignaddr", I64Regs>;
def ALIGNADDRL  : VISInst<0b000011010, "alignaddrl", I64Regs>;
def FALIGNADATA : VISInst<0b001001000, "faligndata">;

def FZERO       : VISInstD<0b001100000, "fzero">;
def FZEROS      : VISInstD<0b001100001, "fzeros", FPRegs>;
def FONE        : VISInstD<0b001111110, "fone">;
def FONES       : VISInstD<0b001111111, "fones", FPRegs>;
def FSRC1       : VISInst1<0b001110100, "fsrc1">;
def FSRC1S      : VISInst1<0b001110101, "fsrc1s", FPRegs>;
def FSRC2       : VISInst2<0b001111000, "fsrc2">;
def FSRC2S      : VISInst2<0b001111001, "fsrc2s", FPRegs>;
def FNOT1       : VISInst1<0b001101010, "fnot1">;
def FNOT1S      : VISInst1<0b001101011, "fnot1s", FPRegs>;
def FNOT2       : VISInst2<0b001100110, "fnot2">;
def FNOT2S      : VISInst2<0b001100111, "fnot2s", FPRegs>;
def FOR         : VISInst<0b001111100,  "for">;
def FORS        : VISInst<0b001111101,  "fors",  FPRegs>;
def FNOR        : VISInst<0b001100010,  "fnor">;
def FNORS       : VISInst<0b001100011,  "fnors", FPRegs>;
def FAND        : VISInst<0b001110000,  "fand">;
def FANDS       : VISInst<0b001110001,  "fands", FPRegs>;
def FNAND       : VISInst<0b001101110,  "fnand">;
def FNANDS      : VISInst<0b001101111,  "fnands", FPRegs>;
def FXOR        : VISInst<0b001101100,  "fxor">;
def FXORS       : VISInst<0b001101101,  "fxors", FPRegs>;
def FXNOR       : VISInst<0b001110010,  "fxnor">;
def FXNORS      : VISInst<0b001110011,  "fxnors", FPRegs>;

def FORNOT1     : VISInst<0b001111010,  "fornot1">;
def FORNOT1S    : VISInst<0b001111011,  "fornot1s",  FPRegs>;
def FORNOT2     : VISInst<0b001110110,  "fornot2">;
def FORNOT2S    : VISInst<0b001110111,  "fornot2s",  FPRegs>;
def FANDNOT1    : VISInst<0b001101000,  "fandnot1">;
def FANDNOT1S   : VISInst<0b001101001,  "fandnot1s", FPRegs>;
def FANDNOT2    : VISInst<0b001100100,  "fandnot2">;
def FANDNOT2S   : VISInst<0b001100101,  "fandnot2s", FPRegs>;

def FCMPGT16    : VISInstID<0b000101000,  "fcmpgt16">;
def FCMPGT32    : VISInstID<0b000101100,  "fcmpgt32">;
def FCMPLE16    : VISInstID<0b000100000,  "fcmple16">;
def FCMPLE32    : VISInstID<0b000100100,  "fcmple32">;
def FCMPNE16    : VISInstID<0b000100010,  "fcmpne16">;
def FCMPNE32    : VISInstID<0b000100110,  "fcmpne32">;
def FCMPEQ16    : VISInstID<0b000101010,  "fcmpeq16">;
def FCMPEQ32    : VISInstID<0b000101110,  "fcmpeq32">;


def EDGE8       : VISInst<0b000000000,  "edge8",   I64Regs>;
def EDGE8L      : VISInst<0b000000010,  "edge8l",  I64Regs>;
def EDGE16      : VISInst<0b000000100,  "edge16",  I64Regs>;
def EDGE16L     : VISInst<0b000000110,  "edge16l", I64Regs>;
def EDGE32      : VISInst<0b000001000,  "edge32",  I64Regs>;
def EDGE32L     : VISInst<0b000001010,  "edge32l", I64Regs>;

def PDIST       : VISInst<0b000111110, "pdist">;

def ARRAY8      : VISInst<0b000010000, "array8",  I64Regs>;
def ARRAY16     : VISInst<0b000010010, "array16", I64Regs>;
def ARRAY32     : VISInst<0b000010100, "array32", I64Regs>;

def SHUTDOWN    : VISInst0<0b010000000, "shutdown">;

} // Predicates = [HasVIS]


// VIS 2 Instructions.
let Predicates = [HasVIS2] in {

def BMASK     : VISInst<0b000011001, "bmask", I64Regs>;
def BSHUFFLE  : VISInst<0b000011100, "bshuffle">;

def SIAM      : VISInst0<0b010000001, "siam">;

def EDGE8N    : VISInst<0b000000001,  "edge8n",   I64Regs>;
def EDGE8LN   : VISInst<0b000000011,  "edge8ln",  I64Regs>;
def EDGE16N   : VISInst<0b000000101,  "edge16n",  I64Regs>;
def EDGE16LN  : VISInst<0b000000111,  "edge16ln", I64Regs>;
def EDGE32N   : VISInst<0b000001001,  "edge32n",  I64Regs>;
def EDGE32LN  : VISInst<0b000001011,  "edge32ln", I64Regs>;
} // Predicates = [HasVIS2]


// VIS 3 Instructions.
let Predicates = [HasVIS3] in {

let Uses = [ICC] in
def ADDXC : VISInst<0b000010001, "addxc", I64Regs>;

let Defs = [ICC], Uses = [ICC] in
def ADDXCCC : VISInst<0b000010011, "addxccc", I64Regs>;

let rd = 0, rs1 = 0 in {
def CMASK8  : VISInstFormat<0b000011011, (outs), (ins I64Regs:$rs2),
              "cmask8 $rs2", []>;
def CMASK16  : VISInstFormat<0b000011101, (outs), (ins I64Regs:$rs2),
              "cmask16 $rs2", []>;
def CMASK32  : VISInstFormat<0b000011111, (outs), (ins I64Regs:$rs2),
              "cmask32 $rs2", []>;

}

def FCHKSM16 : VISInst<0b001000100, "fchksm16">;

def FHADDS   : F3_3<0b10, 0b110100, 0b001100001,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fhadds $rs1, $rs2, $rd", []>;
def FHADDD   : F3_3<0b10, 0b110100, 0b001100010,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fhaddd $rs1, $rs2, $rd", []>;
def FHSUBS   : F3_3<0b10, 0b110100, 0b001100101,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fhsubs $rs1, $rs2, $rd", []>;
def FHSUBD   : F3_3<0b10, 0b110100, 0b001100110,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fhsubd $rs1, $rs2, $rd", []>;
def FLCMPS   : VISInstFormat<0b101010001, (outs FCCRegs:$rd),
                     (ins DFPRegs:$rs1, DFPRegs:$rs2),
                     "flcmps $rd, $rs1, $rs2", []>;
def FLCMPD   : VISInstFormat<0b101010010, (outs FCCRegs:$rd),
                     (ins DFPRegs:$rs1, DFPRegs:$rs2),
                     "flcmpd $rd, $rs1, $rs2", []>;

def FMEAN16  : VISInst<0b001000000, "fmean16">;

def FNADDS   : F3_3<0b10, 0b110100, 0b001010001,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fnadds $rs1, $rs2, $rd", []>;
def FNADDD   : F3_3<0b10, 0b110100, 0b001010010,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fnaddd $rs1, $rs2, $rd", []>;
def FNHADDS  : F3_3<0b10, 0b110100, 0b001110001,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fnhadds $rs1, $rs2, $rd", []>;
def FNHADDD  : F3_3<0b10, 0b110100, 0b001110010,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fnhaddd $rs1, $rs2, $rd", []>;

def FNMULS   : F3_3<0b10, 0b110100, 0b001011001,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fnhadds $rs1, $rs2, $rd", []>;
def FNMULD   : F3_3<0b10, 0b110100, 0b001011010,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fnhaddd $rs1, $rs2, $rd", []>;
def FNSMULD  : F3_3<0b10, 0b110100, 0b001111001,
                    (outs DFPRegs:$rd), (ins DFPRegs:$rs1, DFPRegs:$rs2),
                    "fnhadds $rs1, $rs2, $rd", []>;

def FPADD64   : VISInst<0b001000010, "fpadd64">;

def FSLL16    : VISInst<0b000100001, "fsll16">;
def FSRL16    : VISInst<0b000100011, "fsrl16">;
def FSLL32    : VISInst<0b000100101, "fsll32">;
def FSRL32    : VISInst<0b000100111, "fsrl32">;
def FSLAS16   : VISInst<0b000101001, "fslas16">;
def FSRA16    : VISInst<0b000101011, "fsra16">;
def FSLAS32   : VISInst<0b000101101, "fslas32">;
def FSRA32    : VISInst<0b000101111, "fsra32">;

let rs1 = 0 in
def LZCNT     : VISInstFormat<0b000010111, (outs I64Regs:$rd),
                   (ins I64Regs:$rs2), "lzcnt $rs2, $rd", []>;

let rs1 = 0 in {
def MOVSTOSW : VISInstFormat<0b100010011, (outs I64Regs:$rd),
                   (ins DFPRegs:$rs2), "movstosw $rs2, $rd", []>;
def MOVSTOUW : VISInstFormat<0b100010001, (outs I64Regs:$rd),
                   (ins DFPRegs:$rs2), "movstouw $rs2, $rd", []>;
def MOVDTOX  : VISInstFormat<0b100010000, (outs I64Regs:$rd),
                   (ins DFPRegs:$rs2), "movdtox $rs2, $rd", []>;
def MOVWTOS  :  VISInstFormat<0b100011001, (outs DFPRegs:$rd),
                   (ins I64Regs:$rs2), "movdtox $rs2, $rd", []>;
def MOVXTOD  :  VISInstFormat<0b100011000, (outs DFPRegs:$rd),
                   (ins I64Regs:$rs2), "movdtox $rs2, $rd", []>;
}

def PDISTN   : VISInst<0b000111111, "pdistn">;

def UMULXHI  : VISInst<0b000010110, "umulxhi", I64Regs>;
def XMULX    : VISInst<0b100010101, "xmulx",   I64Regs>;
def XMULXHI  : VISInst<0b100010111, "xmulxhi", I64Regs>;
} // Predicates = [IsVIS3]
