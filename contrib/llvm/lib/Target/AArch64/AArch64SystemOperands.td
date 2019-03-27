//===- AArch64SystemOperands.td ----------------------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the symbolic operands permitted for various kinds of
// AArch64 system instruction.
//
//===----------------------------------------------------------------------===//

include "llvm/TableGen/SearchableTable.td"

//===----------------------------------------------------------------------===//
// Features that, for the compiler, only enable system operands and PStates
//===----------------------------------------------------------------------===//

def HasCCPP    : Predicate<"Subtarget->hasCCPP()">,
                 AssemblerPredicate<"FeatureCCPP", "ccpp">;

def HasPAN     : Predicate<"Subtarget->hasPAN()">,
                 AssemblerPredicate<"FeaturePAN",
                 "ARM v8.1  Privileged Access-Never extension">;

def HasPsUAO   : Predicate<"Subtarget->hasPsUAO()">,
                 AssemblerPredicate<"FeaturePsUAO",
                 "ARM v8.2 UAO PState extension (psuao)">;

def HasPAN_RWV : Predicate<"Subtarget->hasPAN_RWV()">,
                 AssemblerPredicate<"FeaturePAN_RWV",
                 "ARM v8.2 PAN AT S1E1R and AT S1E1W Variation">;

//===----------------------------------------------------------------------===//
// AT (address translate) instruction options.
//===----------------------------------------------------------------------===//

class AT<string name, bits<3> op1, bits<4> crn, bits<4> crm,
         bits<3> op2> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<14> Encoding;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  code Requires = [{ {} }];
}

def : AT<"S1E1R",  0b000, 0b0111, 0b1000, 0b000>;
def : AT<"S1E2R",  0b100, 0b0111, 0b1000, 0b000>;
def : AT<"S1E3R",  0b110, 0b0111, 0b1000, 0b000>;
def : AT<"S1E1W",  0b000, 0b0111, 0b1000, 0b001>;
def : AT<"S1E2W",  0b100, 0b0111, 0b1000, 0b001>;
def : AT<"S1E3W",  0b110, 0b0111, 0b1000, 0b001>;
def : AT<"S1E0R",  0b000, 0b0111, 0b1000, 0b010>;
def : AT<"S1E0W",  0b000, 0b0111, 0b1000, 0b011>;
def : AT<"S12E1R", 0b100, 0b0111, 0b1000, 0b100>;
def : AT<"S12E1W", 0b100, 0b0111, 0b1000, 0b101>;
def : AT<"S12E0R", 0b100, 0b0111, 0b1000, 0b110>;
def : AT<"S12E0W", 0b100, 0b0111, 0b1000, 0b111>;

let Requires = [{ {AArch64::FeaturePAN_RWV} }] in {
def : AT<"S1E1RP", 0b000, 0b0111, 0b1001, 0b000>;
def : AT<"S1E1WP", 0b000, 0b0111, 0b1001, 0b001>;
}

//===----------------------------------------------------------------------===//
// DMB/DSB (data barrier) instruction options.
//===----------------------------------------------------------------------===//

class DB<string name, bits<4> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding = encoding;
}

def : DB<"oshld", 0x1>;
def : DB<"oshst", 0x2>;
def : DB<"osh",   0x3>;
def : DB<"nshld", 0x5>;
def : DB<"nshst", 0x6>;
def : DB<"nsh",   0x7>;
def : DB<"ishld", 0x9>;
def : DB<"ishst", 0xa>;
def : DB<"ish",   0xb>;
def : DB<"ld",    0xd>;
def : DB<"st",    0xe>;
def : DB<"sy",    0xf>;

//===----------------------------------------------------------------------===//
// DC (data cache maintenance) instruction options.
//===----------------------------------------------------------------------===//

class DC<string name, bits<3> op1, bits<4> crn, bits<4> crm,
         bits<3> op2> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<14> Encoding;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  code Requires = [{ {} }];
}

def : DC<"ZVA",   0b011, 0b0111, 0b0100, 0b001>;
def : DC<"IVAC",  0b000, 0b0111, 0b0110, 0b001>;
def : DC<"ISW",   0b000, 0b0111, 0b0110, 0b010>;
def : DC<"CVAC",  0b011, 0b0111, 0b1010, 0b001>;
def : DC<"CSW",   0b000, 0b0111, 0b1010, 0b010>;
def : DC<"CVAU",  0b011, 0b0111, 0b1011, 0b001>;
def : DC<"CIVAC", 0b011, 0b0111, 0b1110, 0b001>;
def : DC<"CISW",  0b000, 0b0111, 0b1110, 0b010>;

let Requires = [{ {AArch64::FeatureCCPP} }] in
def : DC<"CVAP",  0b011, 0b0111, 0b1100, 0b001>;

let Requires = [{ {AArch64::FeatureCacheDeepPersist} }] in
def : DC<"CVADP",  0b011, 0b0111, 0b1101, 0b001>;

let Requires = [{ {AArch64::FeatureMTE} }] in {
def : DC<"IGVAC",   0b000, 0b0111, 0b0110, 0b011>;
def : DC<"IGSW",    0b000, 0b0111, 0b0110, 0b100>;
def : DC<"CGSW",    0b000, 0b0111, 0b1010, 0b100>;
def : DC<"CIGSW",   0b000, 0b0111, 0b1110, 0b100>;
def : DC<"CGVAC",   0b011, 0b0111, 0b1010, 0b011>;
def : DC<"CGVAP",   0b011, 0b0111, 0b1100, 0b011>;
def : DC<"CGVADP",  0b011, 0b0111, 0b1101, 0b011>;
def : DC<"CIGVAC",  0b011, 0b0111, 0b1110, 0b011>;
def : DC<"GVA",     0b011, 0b0111, 0b0100, 0b011>;
def : DC<"IGDVAC",  0b000, 0b0111, 0b0110, 0b101>;
def : DC<"IGDSW",   0b000, 0b0111, 0b0110, 0b110>;
def : DC<"CGDSW",   0b000, 0b0111, 0b1010, 0b110>;
def : DC<"CIGDSW",  0b000, 0b0111, 0b1110, 0b110>;
def : DC<"CGDVAC",  0b011, 0b0111, 0b1010, 0b101>;
def : DC<"CGDVAP",  0b011, 0b0111, 0b1100, 0b101>;
def : DC<"CGDVADP", 0b011, 0b0111, 0b1101, 0b101>;
def : DC<"CIGDVAC", 0b011, 0b0111, 0b1110, 0b101>;
def : DC<"GZVA",    0b011, 0b0111, 0b0100, 0b100>;
}

//===----------------------------------------------------------------------===//
// IC (instruction cache maintenance) instruction options.
//===----------------------------------------------------------------------===//

class IC<string name, bits<3> op1, bits<4> crn, bits<4> crm, bits<3> op2,
         bit needsreg> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<14> Encoding;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  bit NeedsReg = needsreg;
}

def : IC<"IALLUIS", 0b000, 0b0111, 0b0001, 0b000, 0>;
def : IC<"IALLU",   0b000, 0b0111, 0b0101, 0b000, 0>;
def : IC<"IVAU",    0b011, 0b0111, 0b0101, 0b001, 1>;

//===----------------------------------------------------------------------===//
// ISB (instruction-fetch barrier) instruction options.
//===----------------------------------------------------------------------===//

class ISB<string name, bits<4> encoding> : SearchableTable{
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding;
  let Encoding = encoding;
}

def : ISB<"sy", 0xf>;

//===----------------------------------------------------------------------===//
// TSB (Trace synchronization barrier) instruction options.
//===----------------------------------------------------------------------===//

class TSB<string name, bits<4> encoding> : SearchableTable{
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding;
  let Encoding = encoding;

  code Requires = [{ {AArch64::FeatureTRACEV8_4} }];
}

def : TSB<"csync", 0>;

//===----------------------------------------------------------------------===//
// PRFM (prefetch) instruction options.
//===----------------------------------------------------------------------===//

class PRFM<string name, bits<5> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<5> Encoding;
  let Encoding = encoding;
}

def : PRFM<"pldl1keep", 0x00>;
def : PRFM<"pldl1strm", 0x01>;
def : PRFM<"pldl2keep", 0x02>;
def : PRFM<"pldl2strm", 0x03>;
def : PRFM<"pldl3keep", 0x04>;
def : PRFM<"pldl3strm", 0x05>;
def : PRFM<"plil1keep", 0x08>;
def : PRFM<"plil1strm", 0x09>;
def : PRFM<"plil2keep", 0x0a>;
def : PRFM<"plil2strm", 0x0b>;
def : PRFM<"plil3keep", 0x0c>;
def : PRFM<"plil3strm", 0x0d>;
def : PRFM<"pstl1keep", 0x10>;
def : PRFM<"pstl1strm", 0x11>;
def : PRFM<"pstl2keep", 0x12>;
def : PRFM<"pstl2strm", 0x13>;
def : PRFM<"pstl3keep", 0x14>;
def : PRFM<"pstl3strm", 0x15>;

//===----------------------------------------------------------------------===//
// SVE Prefetch instruction options.
//===----------------------------------------------------------------------===//

class SVEPRFM<string name, bits<4> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding;
  let Encoding = encoding;
  code Requires = [{ {} }];
}

let Requires = [{ {AArch64::FeatureSVE} }] in {
def : SVEPRFM<"pldl1keep", 0x00>;
def : SVEPRFM<"pldl1strm", 0x01>;
def : SVEPRFM<"pldl2keep", 0x02>;
def : SVEPRFM<"pldl2strm", 0x03>;
def : SVEPRFM<"pldl3keep", 0x04>;
def : SVEPRFM<"pldl3strm", 0x05>;
def : SVEPRFM<"pstl1keep", 0x08>;
def : SVEPRFM<"pstl1strm", 0x09>;
def : SVEPRFM<"pstl2keep", 0x0a>;
def : SVEPRFM<"pstl2strm", 0x0b>;
def : SVEPRFM<"pstl3keep", 0x0c>;
def : SVEPRFM<"pstl3strm", 0x0d>;
}

//===----------------------------------------------------------------------===//
// SVE Predicate patterns
//===----------------------------------------------------------------------===//

class SVEPREDPAT<string name, bits<5> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<5> Encoding;
  let Encoding = encoding;
}

def : SVEPREDPAT<"pow2",  0x00>;
def : SVEPREDPAT<"vl1",   0x01>;
def : SVEPREDPAT<"vl2",   0x02>;
def : SVEPREDPAT<"vl3",   0x03>;
def : SVEPREDPAT<"vl4",   0x04>;
def : SVEPREDPAT<"vl5",   0x05>;
def : SVEPREDPAT<"vl6",   0x06>;
def : SVEPREDPAT<"vl7",   0x07>;
def : SVEPREDPAT<"vl8",   0x08>;
def : SVEPREDPAT<"vl16",  0x09>;
def : SVEPREDPAT<"vl32",  0x0a>;
def : SVEPREDPAT<"vl64",  0x0b>;
def : SVEPREDPAT<"vl128", 0x0c>;
def : SVEPREDPAT<"vl256", 0x0d>;
def : SVEPREDPAT<"mul4",  0x1d>;
def : SVEPREDPAT<"mul3",  0x1e>;
def : SVEPREDPAT<"all",   0x1f>;

//===----------------------------------------------------------------------===//
// Exact FP Immediates.
//
// These definitions are used to create a lookup table with FP Immediates that
// is used for a few instructions that only accept a limited set of exact FP
// immediates values.
//===----------------------------------------------------------------------===//
class ExactFPImm<string name, string repr, bits<4> enum > : SearchableTable {
  let SearchableFields = ["Enum", "Repr"];
  let EnumValueField = "Enum";

  string Name = name;
  bits<4> Enum = enum;
  string Repr = repr;
}

def : ExactFPImm<"zero", "0.0", 0x0>;
def : ExactFPImm<"half", "0.5", 0x1>;
def : ExactFPImm<"one",  "1.0", 0x2>;
def : ExactFPImm<"two",  "2.0", 0x3>;

//===----------------------------------------------------------------------===//
// PState instruction options.
//===----------------------------------------------------------------------===//

class PState<string name, bits<5> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<5> Encoding;
  let Encoding = encoding;
  code Requires = [{ {} }];
}

def : PState<"SPSel",   0b00101>;
def : PState<"DAIFSet", 0b11110>;
def : PState<"DAIFClr", 0b11111>;
// v8.1a "Privileged Access Never" extension-specific PStates
let Requires = [{ {AArch64::FeaturePAN} }] in
def : PState<"PAN",     0b00100>;

// v8.2a "User Access Override" extension-specific PStates
let Requires = [{ {AArch64::FeaturePsUAO} }] in
def : PState<"UAO",     0b00011>;
// v8.4a timining insensitivity of data processing instructions
let Requires = [{ {AArch64::FeatureDIT} }] in
def : PState<"DIT",     0b11010>;
// v8.5a Spectre Mitigation
let Requires = [{ {AArch64::FeatureSSBS} }] in
def : PState<"SSBS",    0b11001>;
// v8.5a Memory Tagging Extension
let Requires = [{ {AArch64::FeatureMTE} }] in
def : PState<"TCO",     0b11100>;

//===----------------------------------------------------------------------===//
// PSB instruction options.
//===----------------------------------------------------------------------===//

class PSB<string name, bits<5> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<5> Encoding;
  let Encoding = encoding;
}

def : PSB<"csync", 0x11>;

//===----------------------------------------------------------------------===//
// BTI instruction options.
//===----------------------------------------------------------------------===//

class BTI<string name, bits<2> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<2> Encoding;
  let Encoding = encoding;
}

def : BTI<"c",  0b01>;
def : BTI<"j",  0b10>;
def : BTI<"jc", 0b11>;

//===----------------------------------------------------------------------===//
// TLBI (translation lookaside buffer invalidate) instruction options.
//===----------------------------------------------------------------------===//

class TLBI<string name, bits<3> op1, bits<4> crn, bits<4> crm,
             bits<3> op2, bit needsreg = 1> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<14> Encoding;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  bit NeedsReg = needsreg;
  code Requires = [{ {} }];
}

def : TLBI<"IPAS2E1IS",    0b100, 0b1000, 0b0000, 0b001>;
def : TLBI<"IPAS2LE1IS",   0b100, 0b1000, 0b0000, 0b101>;
def : TLBI<"VMALLE1IS",    0b000, 0b1000, 0b0011, 0b000, 0>;
def : TLBI<"ALLE2IS",      0b100, 0b1000, 0b0011, 0b000, 0>;
def : TLBI<"ALLE3IS",      0b110, 0b1000, 0b0011, 0b000, 0>;
def : TLBI<"VAE1IS",       0b000, 0b1000, 0b0011, 0b001>;
def : TLBI<"VAE2IS",       0b100, 0b1000, 0b0011, 0b001>;
def : TLBI<"VAE3IS",       0b110, 0b1000, 0b0011, 0b001>;
def : TLBI<"ASIDE1IS",     0b000, 0b1000, 0b0011, 0b010>;
def : TLBI<"VAAE1IS",      0b000, 0b1000, 0b0011, 0b011>;
def : TLBI<"ALLE1IS",      0b100, 0b1000, 0b0011, 0b100, 0>;
def : TLBI<"VALE1IS",      0b000, 0b1000, 0b0011, 0b101>;
def : TLBI<"VALE2IS",      0b100, 0b1000, 0b0011, 0b101>;
def : TLBI<"VALE3IS",      0b110, 0b1000, 0b0011, 0b101>;
def : TLBI<"VMALLS12E1IS", 0b100, 0b1000, 0b0011, 0b110, 0>;
def : TLBI<"VAALE1IS",     0b000, 0b1000, 0b0011, 0b111>;
def : TLBI<"IPAS2E1",      0b100, 0b1000, 0b0100, 0b001>;
def : TLBI<"IPAS2LE1",     0b100, 0b1000, 0b0100, 0b101>;
def : TLBI<"VMALLE1",      0b000, 0b1000, 0b0111, 0b000, 0>;
def : TLBI<"ALLE2",        0b100, 0b1000, 0b0111, 0b000, 0>;
def : TLBI<"ALLE3",        0b110, 0b1000, 0b0111, 0b000, 0>;
def : TLBI<"VAE1",         0b000, 0b1000, 0b0111, 0b001>;
def : TLBI<"VAE2",         0b100, 0b1000, 0b0111, 0b001>;
def : TLBI<"VAE3",         0b110, 0b1000, 0b0111, 0b001>;
def : TLBI<"ASIDE1",       0b000, 0b1000, 0b0111, 0b010>;
def : TLBI<"VAAE1",        0b000, 0b1000, 0b0111, 0b011>;
def : TLBI<"ALLE1",        0b100, 0b1000, 0b0111, 0b100, 0>;
def : TLBI<"VALE1",        0b000, 0b1000, 0b0111, 0b101>;
def : TLBI<"VALE2",        0b100, 0b1000, 0b0111, 0b101>;
def : TLBI<"VALE3",        0b110, 0b1000, 0b0111, 0b101>;
def : TLBI<"VMALLS12E1",   0b100, 0b1000, 0b0111, 0b110, 0>;
def : TLBI<"VAALE1",       0b000, 0b1000, 0b0111, 0b111>;

// Armv8.4-A Translation Lookaside Buffer Instructions (TLBI)
let Requires = [{ {AArch64::FeatureTLB_RMI} }] in {
// Armv8.4-A Outer Sharable TLB Maintenance instructions:
//                         op1    CRn     CRm     op2
def : TLBI<"VMALLE1OS",    0b000, 0b1000, 0b0001, 0b000, 0>;
def : TLBI<"VAE1OS",       0b000, 0b1000, 0b0001, 0b001>;
def : TLBI<"ASIDE1OS",     0b000, 0b1000, 0b0001, 0b010>;
def : TLBI<"VAAE1OS",      0b000, 0b1000, 0b0001, 0b011>;
def : TLBI<"VALE1OS",      0b000, 0b1000, 0b0001, 0b101>;
def : TLBI<"VAALE1OS",     0b000, 0b1000, 0b0001, 0b111>;
def : TLBI<"IPAS2E1OS",    0b100, 0b1000, 0b0100, 0b000>;
def : TLBI<"IPAS2LE1OS",   0b100, 0b1000, 0b0100, 0b100>;
def : TLBI<"VAE2OS",       0b100, 0b1000, 0b0001, 0b001>;
def : TLBI<"VALE2OS",      0b100, 0b1000, 0b0001, 0b101>;
def : TLBI<"VMALLS12E1OS", 0b100, 0b1000, 0b0001, 0b110, 0>;
def : TLBI<"VAE3OS",       0b110, 0b1000, 0b0001, 0b001>;
def : TLBI<"VALE3OS",      0b110, 0b1000, 0b0001, 0b101>;
def : TLBI<"ALLE2OS",      0b100, 0b1000, 0b0001, 0b000, 0>;
def : TLBI<"ALLE1OS",      0b100, 0b1000, 0b0001, 0b100, 0>;
def : TLBI<"ALLE3OS",      0b110, 0b1000, 0b0001, 0b000, 0>;

// Armv8.4-A TLB Range Maintenance instructions:
//                         op1    CRn     CRm     op2
def : TLBI<"RVAE1",        0b000, 0b1000, 0b0110, 0b001>;
def : TLBI<"RVAAE1",       0b000, 0b1000, 0b0110, 0b011>;
def : TLBI<"RVALE1",       0b000, 0b1000, 0b0110, 0b101>;
def : TLBI<"RVAALE1",      0b000, 0b1000, 0b0110, 0b111>;
def : TLBI<"RVAE1IS",      0b000, 0b1000, 0b0010, 0b001>;
def : TLBI<"RVAAE1IS",     0b000, 0b1000, 0b0010, 0b011>;
def : TLBI<"RVALE1IS",     0b000, 0b1000, 0b0010, 0b101>;
def : TLBI<"RVAALE1IS",    0b000, 0b1000, 0b0010, 0b111>;
def : TLBI<"RVAE1OS",      0b000, 0b1000, 0b0101, 0b001>;
def : TLBI<"RVAAE1OS",     0b000, 0b1000, 0b0101, 0b011>;
def : TLBI<"RVALE1OS",     0b000, 0b1000, 0b0101, 0b101>;
def : TLBI<"RVAALE1OS",    0b000, 0b1000, 0b0101, 0b111>;
def : TLBI<"RIPAS2E1IS",   0b100, 0b1000, 0b0000, 0b010>;
def : TLBI<"RIPAS2LE1IS",  0b100, 0b1000, 0b0000, 0b110>;
def : TLBI<"RIPAS2E1",     0b100, 0b1000, 0b0100, 0b010>;
def : TLBI<"RIPAS2LE1",    0b100, 0b1000, 0b0100, 0b110>;
def : TLBI<"RIPAS2E1OS",   0b100, 0b1000, 0b0100, 0b011>;
def : TLBI<"RIPAS2LE1OS",  0b100, 0b1000, 0b0100, 0b111>;
def : TLBI<"RVAE2",        0b100, 0b1000, 0b0110, 0b001>;
def : TLBI<"RVALE2",       0b100, 0b1000, 0b0110, 0b101>;
def : TLBI<"RVAE2IS",      0b100, 0b1000, 0b0010, 0b001>;
def : TLBI<"RVALE2IS",     0b100, 0b1000, 0b0010, 0b101>;
def : TLBI<"RVAE2OS",      0b100, 0b1000, 0b0101, 0b001>;
def : TLBI<"RVALE2OS",     0b100, 0b1000, 0b0101, 0b101>;
def : TLBI<"RVAE3",        0b110, 0b1000, 0b0110, 0b001>;
def : TLBI<"RVALE3",       0b110, 0b1000, 0b0110, 0b101>;
def : TLBI<"RVAE3IS",      0b110, 0b1000, 0b0010, 0b001>;
def : TLBI<"RVALE3IS",     0b110, 0b1000, 0b0010, 0b101>;
def : TLBI<"RVAE3OS",      0b110, 0b1000, 0b0101, 0b001>;
def : TLBI<"RVALE3OS",     0b110, 0b1000, 0b0101, 0b101>;
} //FeatureTLB_RMI

// Armv8.5-A Prediction Restriction by Context instruction options:
class PRCTX<string name, bits<4> crm> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<11> Encoding;
  let Encoding{10-4} = 0b0110111;
  let Encoding{3-0} = crm;
  bit NeedsReg = 1;
  code Requires = [{ {} }];
}

let Requires = [{ {AArch64::FeaturePredRes} }] in {
def : PRCTX<"RCTX", 0b0011>;
}

//===----------------------------------------------------------------------===//
// MRS/MSR (system register read/write) instruction options.
//===----------------------------------------------------------------------===//

class SysReg<string name, bits<2> op0, bits<3> op1, bits<4> crn, bits<4> crm,
             bits<3> op2> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<16> Encoding;
  let Encoding{15-14} = op0;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  bit Readable = ?;
  bit Writeable = ?;
  code Requires = [{ {} }];
}

class RWSysReg<string name, bits<2> op0, bits<3> op1, bits<4> crn, bits<4> crm,
               bits<3> op2>
    : SysReg<name, op0, op1, crn, crm, op2> {
  let Readable = 1;
  let Writeable = 1;
}

class ROSysReg<string name, bits<2> op0, bits<3> op1, bits<4> crn, bits<4> crm,
               bits<3> op2>
    : SysReg<name, op0, op1, crn, crm, op2> {
  let Readable = 1;
  let Writeable = 0;
}

class WOSysReg<string name, bits<2> op0, bits<3> op1, bits<4> crn, bits<4> crm,
               bits<3> op2>
    : SysReg<name, op0, op1, crn, crm, op2> {
  let Readable = 0;
  let Writeable = 1;
}

//===----------------------
// Read-only regs
//===----------------------

//                                    Op0    Op1     CRn     CRm    Op2
def : ROSysReg<"MDCCSR_EL0",         0b10, 0b011, 0b0000, 0b0001, 0b000>;
def : ROSysReg<"DBGDTRRX_EL0",       0b10, 0b011, 0b0000, 0b0101, 0b000>;
def : ROSysReg<"MDRAR_EL1",          0b10, 0b000, 0b0001, 0b0000, 0b000>;
def : ROSysReg<"OSLSR_EL1",          0b10, 0b000, 0b0001, 0b0001, 0b100>;
def : ROSysReg<"DBGAUTHSTATUS_EL1",  0b10, 0b000, 0b0111, 0b1110, 0b110>;
def : ROSysReg<"PMCEID0_EL0",        0b11, 0b011, 0b1001, 0b1100, 0b110>;
def : ROSysReg<"PMCEID1_EL0",        0b11, 0b011, 0b1001, 0b1100, 0b111>;
def : ROSysReg<"MIDR_EL1",           0b11, 0b000, 0b0000, 0b0000, 0b000>;
def : ROSysReg<"CCSIDR_EL1",         0b11, 0b001, 0b0000, 0b0000, 0b000>;

//v8.3 CCIDX - extending the CCsIDr number of sets
def : ROSysReg<"CCSIDR2_EL1",        0b11, 0b001, 0b0000, 0b0000, 0b010> {
  let Requires = [{ {AArch64::FeatureCCIDX} }];
}
def : ROSysReg<"CLIDR_EL1",          0b11, 0b001, 0b0000, 0b0000, 0b001>;
def : ROSysReg<"CTR_EL0",            0b11, 0b011, 0b0000, 0b0000, 0b001>;
def : ROSysReg<"MPIDR_EL1",          0b11, 0b000, 0b0000, 0b0000, 0b101>;
def : ROSysReg<"REVIDR_EL1",         0b11, 0b000, 0b0000, 0b0000, 0b110>;
def : ROSysReg<"AIDR_EL1",           0b11, 0b001, 0b0000, 0b0000, 0b111>;
def : ROSysReg<"DCZID_EL0",          0b11, 0b011, 0b0000, 0b0000, 0b111>;
def : ROSysReg<"ID_PFR0_EL1",        0b11, 0b000, 0b0000, 0b0001, 0b000>;
def : ROSysReg<"ID_PFR1_EL1",        0b11, 0b000, 0b0000, 0b0001, 0b001>;
def : ROSysReg<"ID_PFR2_EL1",        0b11, 0b000, 0b0000, 0b0011, 0b100> {
    let Requires = [{ {AArch64::FeatureSpecRestrict} }];
}
def : ROSysReg<"ID_DFR0_EL1",        0b11, 0b000, 0b0000, 0b0001, 0b010>;
def : ROSysReg<"ID_AFR0_EL1",        0b11, 0b000, 0b0000, 0b0001, 0b011>;
def : ROSysReg<"ID_MMFR0_EL1",       0b11, 0b000, 0b0000, 0b0001, 0b100>;
def : ROSysReg<"ID_MMFR1_EL1",       0b11, 0b000, 0b0000, 0b0001, 0b101>;
def : ROSysReg<"ID_MMFR2_EL1",       0b11, 0b000, 0b0000, 0b0001, 0b110>;
def : ROSysReg<"ID_MMFR3_EL1",       0b11, 0b000, 0b0000, 0b0001, 0b111>;
def : ROSysReg<"ID_ISAR0_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b000>;
def : ROSysReg<"ID_ISAR1_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b001>;
def : ROSysReg<"ID_ISAR2_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b010>;
def : ROSysReg<"ID_ISAR3_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b011>;
def : ROSysReg<"ID_ISAR4_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b100>;
def : ROSysReg<"ID_ISAR5_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b101>;
def : ROSysReg<"ID_ISAR6_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b111> {
  let Requires = [{ {AArch64::HasV8_2aOps} }];
}
def : ROSysReg<"ID_AA64PFR0_EL1",     0b11, 0b000, 0b0000, 0b0100, 0b000>;
def : ROSysReg<"ID_AA64PFR1_EL1",     0b11, 0b000, 0b0000, 0b0100, 0b001>;
def : ROSysReg<"ID_AA64DFR0_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b000>;
def : ROSysReg<"ID_AA64DFR1_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b001>;
def : ROSysReg<"ID_AA64AFR0_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b100>;
def : ROSysReg<"ID_AA64AFR1_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b101>;
def : ROSysReg<"ID_AA64ISAR0_EL1",    0b11, 0b000, 0b0000, 0b0110, 0b000>;
def : ROSysReg<"ID_AA64ISAR1_EL1",    0b11, 0b000, 0b0000, 0b0110, 0b001>;
def : ROSysReg<"ID_AA64MMFR0_EL1",    0b11, 0b000, 0b0000, 0b0111, 0b000>;
def : ROSysReg<"ID_AA64MMFR1_EL1",    0b11, 0b000, 0b0000, 0b0111, 0b001>;
def : ROSysReg<"ID_AA64MMFR2_EL1",    0b11, 0b000, 0b0000, 0b0111, 0b010>;
def : ROSysReg<"MVFR0_EL1",          0b11, 0b000, 0b0000, 0b0011, 0b000>;
def : ROSysReg<"MVFR1_EL1",          0b11, 0b000, 0b0000, 0b0011, 0b001>;
def : ROSysReg<"MVFR2_EL1",          0b11, 0b000, 0b0000, 0b0011, 0b010>;
def : ROSysReg<"RVBAR_EL1",          0b11, 0b000, 0b1100, 0b0000, 0b001>;
def : ROSysReg<"RVBAR_EL2",          0b11, 0b100, 0b1100, 0b0000, 0b001>;
def : ROSysReg<"RVBAR_EL3",          0b11, 0b110, 0b1100, 0b0000, 0b001>;
def : ROSysReg<"ISR_EL1",            0b11, 0b000, 0b1100, 0b0001, 0b000>;
def : ROSysReg<"CNTPCT_EL0",         0b11, 0b011, 0b1110, 0b0000, 0b001>;
def : ROSysReg<"CNTVCT_EL0",         0b11, 0b011, 0b1110, 0b0000, 0b010>;
def : ROSysReg<"ID_MMFR4_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b110>;

// Trace registers
//                                 Op0    Op1     CRn     CRm    Op2
def : ROSysReg<"TRCSTATR",           0b10, 0b001, 0b0000, 0b0011, 0b000>;
def : ROSysReg<"TRCIDR8",            0b10, 0b001, 0b0000, 0b0000, 0b110>;
def : ROSysReg<"TRCIDR9",            0b10, 0b001, 0b0000, 0b0001, 0b110>;
def : ROSysReg<"TRCIDR10",           0b10, 0b001, 0b0000, 0b0010, 0b110>;
def : ROSysReg<"TRCIDR11",           0b10, 0b001, 0b0000, 0b0011, 0b110>;
def : ROSysReg<"TRCIDR12",           0b10, 0b001, 0b0000, 0b0100, 0b110>;
def : ROSysReg<"TRCIDR13",           0b10, 0b001, 0b0000, 0b0101, 0b110>;
def : ROSysReg<"TRCIDR0",            0b10, 0b001, 0b0000, 0b1000, 0b111>;
def : ROSysReg<"TRCIDR1",            0b10, 0b001, 0b0000, 0b1001, 0b111>;
def : ROSysReg<"TRCIDR2",            0b10, 0b001, 0b0000, 0b1010, 0b111>;
def : ROSysReg<"TRCIDR3",            0b10, 0b001, 0b0000, 0b1011, 0b111>;
def : ROSysReg<"TRCIDR4",            0b10, 0b001, 0b0000, 0b1100, 0b111>;
def : ROSysReg<"TRCIDR5",            0b10, 0b001, 0b0000, 0b1101, 0b111>;
def : ROSysReg<"TRCIDR6",            0b10, 0b001, 0b0000, 0b1110, 0b111>;
def : ROSysReg<"TRCIDR7",            0b10, 0b001, 0b0000, 0b1111, 0b111>;
def : ROSysReg<"TRCOSLSR",           0b10, 0b001, 0b0001, 0b0001, 0b100>;
def : ROSysReg<"TRCPDSR",            0b10, 0b001, 0b0001, 0b0101, 0b100>;
def : ROSysReg<"TRCDEVAFF0",         0b10, 0b001, 0b0111, 0b1010, 0b110>;
def : ROSysReg<"TRCDEVAFF1",         0b10, 0b001, 0b0111, 0b1011, 0b110>;
def : ROSysReg<"TRCLSR",             0b10, 0b001, 0b0111, 0b1101, 0b110>;
def : ROSysReg<"TRCAUTHSTATUS",      0b10, 0b001, 0b0111, 0b1110, 0b110>;
def : ROSysReg<"TRCDEVARCH",         0b10, 0b001, 0b0111, 0b1111, 0b110>;
def : ROSysReg<"TRCDEVID",           0b10, 0b001, 0b0111, 0b0010, 0b111>;
def : ROSysReg<"TRCDEVTYPE",         0b10, 0b001, 0b0111, 0b0011, 0b111>;
def : ROSysReg<"TRCPIDR4",           0b10, 0b001, 0b0111, 0b0100, 0b111>;
def : ROSysReg<"TRCPIDR5",           0b10, 0b001, 0b0111, 0b0101, 0b111>;
def : ROSysReg<"TRCPIDR6",           0b10, 0b001, 0b0111, 0b0110, 0b111>;
def : ROSysReg<"TRCPIDR7",           0b10, 0b001, 0b0111, 0b0111, 0b111>;
def : ROSysReg<"TRCPIDR0",           0b10, 0b001, 0b0111, 0b1000, 0b111>;
def : ROSysReg<"TRCPIDR1",           0b10, 0b001, 0b0111, 0b1001, 0b111>;
def : ROSysReg<"TRCPIDR2",           0b10, 0b001, 0b0111, 0b1010, 0b111>;
def : ROSysReg<"TRCPIDR3",           0b10, 0b001, 0b0111, 0b1011, 0b111>;
def : ROSysReg<"TRCCIDR0",           0b10, 0b001, 0b0111, 0b1100, 0b111>;
def : ROSysReg<"TRCCIDR1",           0b10, 0b001, 0b0111, 0b1101, 0b111>;
def : ROSysReg<"TRCCIDR2",           0b10, 0b001, 0b0111, 0b1110, 0b111>;
def : ROSysReg<"TRCCIDR3",           0b10, 0b001, 0b0111, 0b1111, 0b111>;

// GICv3 registers
//                                 Op0    Op1     CRn     CRm    Op2
def : ROSysReg<"ICC_IAR1_EL1",       0b11, 0b000, 0b1100, 0b1100, 0b000>;
def : ROSysReg<"ICC_IAR0_EL1",       0b11, 0b000, 0b1100, 0b1000, 0b000>;
def : ROSysReg<"ICC_HPPIR1_EL1",     0b11, 0b000, 0b1100, 0b1100, 0b010>;
def : ROSysReg<"ICC_HPPIR0_EL1",     0b11, 0b000, 0b1100, 0b1000, 0b010>;
def : ROSysReg<"ICC_RPR_EL1",        0b11, 0b000, 0b1100, 0b1011, 0b011>;
def : ROSysReg<"ICH_VTR_EL2",        0b11, 0b100, 0b1100, 0b1011, 0b001>;
def : ROSysReg<"ICH_EISR_EL2",       0b11, 0b100, 0b1100, 0b1011, 0b011>;
def : ROSysReg<"ICH_ELRSR_EL2",      0b11, 0b100, 0b1100, 0b1011, 0b101>;

// SVE control registers
//                                   Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureSVE} }] in {
def : ROSysReg<"ID_AA64ZFR0_EL1",    0b11, 0b000, 0b0000, 0b0100, 0b100>;
}

// v8.1a "Limited Ordering Regions" extension-specific system register
//                         Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureLOR} }] in
def : ROSysReg<"LORID_EL1",  0b11, 0b000, 0b1010, 0b0100, 0b111>;

// v8.2a "RAS extension" registers
//                         Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureRAS} }] in {
def : ROSysReg<"ERRIDR_EL1", 0b11, 0b000, 0b0101, 0b0011, 0b000>;
def : ROSysReg<"ERXFR_EL1",  0b11, 0b000, 0b0101, 0b0100, 0b000>;
}

// v8.5a "random number" registers
//                       Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureRandGen} }] in {
def : ROSysReg<"RNDR",   0b11, 0b011, 0b0010, 0b0100, 0b000>;
def : ROSysReg<"RNDRRS", 0b11, 0b011, 0b0010, 0b0100, 0b001>;
}

// v8.5a Software Context Number registers
let Requires = [{ {AArch64::FeatureSpecRestrict} }] in {
def : RWSysReg<"SCXTNUM_EL0", 0b11, 0b011, 0b1101, 0b0000, 0b111>;
def : RWSysReg<"SCXTNUM_EL1", 0b11, 0b000, 0b1101, 0b0000, 0b111>;
def : RWSysReg<"SCXTNUM_EL2", 0b11, 0b100, 0b1101, 0b0000, 0b111>;
def : RWSysReg<"SCXTNUM_EL3", 0b11, 0b110, 0b1101, 0b0000, 0b111>;
def : RWSysReg<"SCXTNUM_EL12", 0b11, 0b101, 0b1101, 0b0000, 0b111>;
}

//===----------------------
// Write-only regs
//===----------------------

//                                 Op0    Op1     CRn     CRm    Op2
def : WOSysReg<"DBGDTRTX_EL0",       0b10, 0b011, 0b0000, 0b0101, 0b000>;
def : WOSysReg<"OSLAR_EL1",          0b10, 0b000, 0b0001, 0b0000, 0b100>;
def : WOSysReg<"PMSWINC_EL0",        0b11, 0b011, 0b1001, 0b1100, 0b100>;

// Trace Registers
//                                 Op0    Op1     CRn     CRm    Op2
def : WOSysReg<"TRCOSLAR",           0b10, 0b001, 0b0001, 0b0000, 0b100>;
def : WOSysReg<"TRCLAR",             0b10, 0b001, 0b0111, 0b1100, 0b110>;

// GICv3 registers
//                                 Op0    Op1     CRn     CRm    Op2
def : WOSysReg<"ICC_EOIR1_EL1",      0b11, 0b000, 0b1100, 0b1100, 0b001>;
def : WOSysReg<"ICC_EOIR0_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b001>;
def : WOSysReg<"ICC_DIR_EL1",        0b11, 0b000, 0b1100, 0b1011, 0b001>;
def : WOSysReg<"ICC_SGI1R_EL1",      0b11, 0b000, 0b1100, 0b1011, 0b101>;
def : WOSysReg<"ICC_ASGI1R_EL1",     0b11, 0b000, 0b1100, 0b1011, 0b110>;
def : WOSysReg<"ICC_SGI0R_EL1",      0b11, 0b000, 0b1100, 0b1011, 0b111>;

//===----------------------
// Read-write regs
//===----------------------

//                                 Op0    Op1     CRn     CRm    Op2
def : RWSysReg<"OSDTRRX_EL1",        0b10, 0b000, 0b0000, 0b0000, 0b010>;
def : RWSysReg<"OSDTRTX_EL1",        0b10, 0b000, 0b0000, 0b0011, 0b010>;
def : RWSysReg<"TEECR32_EL1",        0b10, 0b010, 0b0000, 0b0000, 0b000>;
def : RWSysReg<"MDCCINT_EL1",        0b10, 0b000, 0b0000, 0b0010, 0b000>;
def : RWSysReg<"MDSCR_EL1",          0b10, 0b000, 0b0000, 0b0010, 0b010>;
def : RWSysReg<"DBGDTR_EL0",         0b10, 0b011, 0b0000, 0b0100, 0b000>;
def : RWSysReg<"OSECCR_EL1",         0b10, 0b000, 0b0000, 0b0110, 0b010>;
def : RWSysReg<"DBGVCR32_EL2",       0b10, 0b100, 0b0000, 0b0111, 0b000>;
def : RWSysReg<"DBGBVR0_EL1",        0b10, 0b000, 0b0000, 0b0000, 0b100>;
def : RWSysReg<"DBGBVR1_EL1",        0b10, 0b000, 0b0000, 0b0001, 0b100>;
def : RWSysReg<"DBGBVR2_EL1",        0b10, 0b000, 0b0000, 0b0010, 0b100>;
def : RWSysReg<"DBGBVR3_EL1",        0b10, 0b000, 0b0000, 0b0011, 0b100>;
def : RWSysReg<"DBGBVR4_EL1",        0b10, 0b000, 0b0000, 0b0100, 0b100>;
def : RWSysReg<"DBGBVR5_EL1",        0b10, 0b000, 0b0000, 0b0101, 0b100>;
def : RWSysReg<"DBGBVR6_EL1",        0b10, 0b000, 0b0000, 0b0110, 0b100>;
def : RWSysReg<"DBGBVR7_EL1",        0b10, 0b000, 0b0000, 0b0111, 0b100>;
def : RWSysReg<"DBGBVR8_EL1",        0b10, 0b000, 0b0000, 0b1000, 0b100>;
def : RWSysReg<"DBGBVR9_EL1",        0b10, 0b000, 0b0000, 0b1001, 0b100>;
def : RWSysReg<"DBGBVR10_EL1",       0b10, 0b000, 0b0000, 0b1010, 0b100>;
def : RWSysReg<"DBGBVR11_EL1",       0b10, 0b000, 0b0000, 0b1011, 0b100>;
def : RWSysReg<"DBGBVR12_EL1",       0b10, 0b000, 0b0000, 0b1100, 0b100>;
def : RWSysReg<"DBGBVR13_EL1",       0b10, 0b000, 0b0000, 0b1101, 0b100>;
def : RWSysReg<"DBGBVR14_EL1",       0b10, 0b000, 0b0000, 0b1110, 0b100>;
def : RWSysReg<"DBGBVR15_EL1",       0b10, 0b000, 0b0000, 0b1111, 0b100>;
def : RWSysReg<"DBGBCR0_EL1",        0b10, 0b000, 0b0000, 0b0000, 0b101>;
def : RWSysReg<"DBGBCR1_EL1",        0b10, 0b000, 0b0000, 0b0001, 0b101>;
def : RWSysReg<"DBGBCR2_EL1",        0b10, 0b000, 0b0000, 0b0010, 0b101>;
def : RWSysReg<"DBGBCR3_EL1",        0b10, 0b000, 0b0000, 0b0011, 0b101>;
def : RWSysReg<"DBGBCR4_EL1",        0b10, 0b000, 0b0000, 0b0100, 0b101>;
def : RWSysReg<"DBGBCR5_EL1",        0b10, 0b000, 0b0000, 0b0101, 0b101>;
def : RWSysReg<"DBGBCR6_EL1",        0b10, 0b000, 0b0000, 0b0110, 0b101>;
def : RWSysReg<"DBGBCR7_EL1",        0b10, 0b000, 0b0000, 0b0111, 0b101>;
def : RWSysReg<"DBGBCR8_EL1",        0b10, 0b000, 0b0000, 0b1000, 0b101>;
def : RWSysReg<"DBGBCR9_EL1",        0b10, 0b000, 0b0000, 0b1001, 0b101>;
def : RWSysReg<"DBGBCR10_EL1",       0b10, 0b000, 0b0000, 0b1010, 0b101>;
def : RWSysReg<"DBGBCR11_EL1",       0b10, 0b000, 0b0000, 0b1011, 0b101>;
def : RWSysReg<"DBGBCR12_EL1",       0b10, 0b000, 0b0000, 0b1100, 0b101>;
def : RWSysReg<"DBGBCR13_EL1",       0b10, 0b000, 0b0000, 0b1101, 0b101>;
def : RWSysReg<"DBGBCR14_EL1",       0b10, 0b000, 0b0000, 0b1110, 0b101>;
def : RWSysReg<"DBGBCR15_EL1",       0b10, 0b000, 0b0000, 0b1111, 0b101>;
def : RWSysReg<"DBGWVR0_EL1",        0b10, 0b000, 0b0000, 0b0000, 0b110>;
def : RWSysReg<"DBGWVR1_EL1",        0b10, 0b000, 0b0000, 0b0001, 0b110>;
def : RWSysReg<"DBGWVR2_EL1",        0b10, 0b000, 0b0000, 0b0010, 0b110>;
def : RWSysReg<"DBGWVR3_EL1",        0b10, 0b000, 0b0000, 0b0011, 0b110>;
def : RWSysReg<"DBGWVR4_EL1",        0b10, 0b000, 0b0000, 0b0100, 0b110>;
def : RWSysReg<"DBGWVR5_EL1",        0b10, 0b000, 0b0000, 0b0101, 0b110>;
def : RWSysReg<"DBGWVR6_EL1",        0b10, 0b000, 0b0000, 0b0110, 0b110>;
def : RWSysReg<"DBGWVR7_EL1",        0b10, 0b000, 0b0000, 0b0111, 0b110>;
def : RWSysReg<"DBGWVR8_EL1",        0b10, 0b000, 0b0000, 0b1000, 0b110>;
def : RWSysReg<"DBGWVR9_EL1",        0b10, 0b000, 0b0000, 0b1001, 0b110>;
def : RWSysReg<"DBGWVR10_EL1",       0b10, 0b000, 0b0000, 0b1010, 0b110>;
def : RWSysReg<"DBGWVR11_EL1",       0b10, 0b000, 0b0000, 0b1011, 0b110>;
def : RWSysReg<"DBGWVR12_EL1",       0b10, 0b000, 0b0000, 0b1100, 0b110>;
def : RWSysReg<"DBGWVR13_EL1",       0b10, 0b000, 0b0000, 0b1101, 0b110>;
def : RWSysReg<"DBGWVR14_EL1",       0b10, 0b000, 0b0000, 0b1110, 0b110>;
def : RWSysReg<"DBGWVR15_EL1",       0b10, 0b000, 0b0000, 0b1111, 0b110>;
def : RWSysReg<"DBGWCR0_EL1",        0b10, 0b000, 0b0000, 0b0000, 0b111>;
def : RWSysReg<"DBGWCR1_EL1",        0b10, 0b000, 0b0000, 0b0001, 0b111>;
def : RWSysReg<"DBGWCR2_EL1",        0b10, 0b000, 0b0000, 0b0010, 0b111>;
def : RWSysReg<"DBGWCR3_EL1",        0b10, 0b000, 0b0000, 0b0011, 0b111>;
def : RWSysReg<"DBGWCR4_EL1",        0b10, 0b000, 0b0000, 0b0100, 0b111>;
def : RWSysReg<"DBGWCR5_EL1",        0b10, 0b000, 0b0000, 0b0101, 0b111>;
def : RWSysReg<"DBGWCR6_EL1",        0b10, 0b000, 0b0000, 0b0110, 0b111>;
def : RWSysReg<"DBGWCR7_EL1",        0b10, 0b000, 0b0000, 0b0111, 0b111>;
def : RWSysReg<"DBGWCR8_EL1",        0b10, 0b000, 0b0000, 0b1000, 0b111>;
def : RWSysReg<"DBGWCR9_EL1",        0b10, 0b000, 0b0000, 0b1001, 0b111>;
def : RWSysReg<"DBGWCR10_EL1",       0b10, 0b000, 0b0000, 0b1010, 0b111>;
def : RWSysReg<"DBGWCR11_EL1",       0b10, 0b000, 0b0000, 0b1011, 0b111>;
def : RWSysReg<"DBGWCR12_EL1",       0b10, 0b000, 0b0000, 0b1100, 0b111>;
def : RWSysReg<"DBGWCR13_EL1",       0b10, 0b000, 0b0000, 0b1101, 0b111>;
def : RWSysReg<"DBGWCR14_EL1",       0b10, 0b000, 0b0000, 0b1110, 0b111>;
def : RWSysReg<"DBGWCR15_EL1",       0b10, 0b000, 0b0000, 0b1111, 0b111>;
def : RWSysReg<"TEEHBR32_EL1",       0b10, 0b010, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"OSDLR_EL1",          0b10, 0b000, 0b0001, 0b0011, 0b100>;
def : RWSysReg<"DBGPRCR_EL1",        0b10, 0b000, 0b0001, 0b0100, 0b100>;
def : RWSysReg<"DBGCLAIMSET_EL1",    0b10, 0b000, 0b0111, 0b1000, 0b110>;
def : RWSysReg<"DBGCLAIMCLR_EL1",    0b10, 0b000, 0b0111, 0b1001, 0b110>;
def : RWSysReg<"CSSELR_EL1",         0b11, 0b010, 0b0000, 0b0000, 0b000>;
def : RWSysReg<"VPIDR_EL2",          0b11, 0b100, 0b0000, 0b0000, 0b000>;
def : RWSysReg<"VMPIDR_EL2",         0b11, 0b100, 0b0000, 0b0000, 0b101>;
def : RWSysReg<"CPACR_EL1",          0b11, 0b000, 0b0001, 0b0000, 0b010>;
def : RWSysReg<"SCTLR_EL1",          0b11, 0b000, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"SCTLR_EL2",          0b11, 0b100, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"SCTLR_EL3",          0b11, 0b110, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"ACTLR_EL1",          0b11, 0b000, 0b0001, 0b0000, 0b001>;
def : RWSysReg<"ACTLR_EL2",          0b11, 0b100, 0b0001, 0b0000, 0b001>;
def : RWSysReg<"ACTLR_EL3",          0b11, 0b110, 0b0001, 0b0000, 0b001>;
def : RWSysReg<"HCR_EL2",            0b11, 0b100, 0b0001, 0b0001, 0b000>;
def : RWSysReg<"SCR_EL3",            0b11, 0b110, 0b0001, 0b0001, 0b000>;
def : RWSysReg<"MDCR_EL2",           0b11, 0b100, 0b0001, 0b0001, 0b001>;
def : RWSysReg<"SDER32_EL3",         0b11, 0b110, 0b0001, 0b0001, 0b001>;
def : RWSysReg<"CPTR_EL2",           0b11, 0b100, 0b0001, 0b0001, 0b010>;
def : RWSysReg<"CPTR_EL3",           0b11, 0b110, 0b0001, 0b0001, 0b010>;
def : RWSysReg<"HSTR_EL2",           0b11, 0b100, 0b0001, 0b0001, 0b011>;
def : RWSysReg<"HACR_EL2",           0b11, 0b100, 0b0001, 0b0001, 0b111>;
def : RWSysReg<"MDCR_EL3",           0b11, 0b110, 0b0001, 0b0011, 0b001>;
def : RWSysReg<"TTBR0_EL1",          0b11, 0b000, 0b0010, 0b0000, 0b000>;
def : RWSysReg<"TTBR0_EL2",          0b11, 0b100, 0b0010, 0b0000, 0b000>;
def : RWSysReg<"TTBR0_EL3",          0b11, 0b110, 0b0010, 0b0000, 0b000>;
def : RWSysReg<"TTBR1_EL1",          0b11, 0b000, 0b0010, 0b0000, 0b001>;
def : RWSysReg<"TCR_EL1",            0b11, 0b000, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"TCR_EL2",            0b11, 0b100, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"TCR_EL3",            0b11, 0b110, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"VTTBR_EL2",          0b11, 0b100, 0b0010, 0b0001, 0b000>;
def : RWSysReg<"VTCR_EL2",           0b11, 0b100, 0b0010, 0b0001, 0b010>;
def : RWSysReg<"DACR32_EL2",         0b11, 0b100, 0b0011, 0b0000, 0b000>;
def : RWSysReg<"SPSR_EL1",           0b11, 0b000, 0b0100, 0b0000, 0b000>;
def : RWSysReg<"SPSR_EL2",           0b11, 0b100, 0b0100, 0b0000, 0b000>;
def : RWSysReg<"SPSR_EL3",           0b11, 0b110, 0b0100, 0b0000, 0b000>;
def : RWSysReg<"ELR_EL1",            0b11, 0b000, 0b0100, 0b0000, 0b001>;
def : RWSysReg<"ELR_EL2",            0b11, 0b100, 0b0100, 0b0000, 0b001>;
def : RWSysReg<"ELR_EL3",            0b11, 0b110, 0b0100, 0b0000, 0b001>;
def : RWSysReg<"SP_EL0",             0b11, 0b000, 0b0100, 0b0001, 0b000>;
def : RWSysReg<"SP_EL1",             0b11, 0b100, 0b0100, 0b0001, 0b000>;
def : RWSysReg<"SP_EL2",             0b11, 0b110, 0b0100, 0b0001, 0b000>;
def : RWSysReg<"SPSel",              0b11, 0b000, 0b0100, 0b0010, 0b000>;
def : RWSysReg<"NZCV",               0b11, 0b011, 0b0100, 0b0010, 0b000>;
def : RWSysReg<"DAIF",               0b11, 0b011, 0b0100, 0b0010, 0b001>;
def : RWSysReg<"CurrentEL",          0b11, 0b000, 0b0100, 0b0010, 0b010>;
def : RWSysReg<"SPSR_irq",           0b11, 0b100, 0b0100, 0b0011, 0b000>;
def : RWSysReg<"SPSR_abt",           0b11, 0b100, 0b0100, 0b0011, 0b001>;
def : RWSysReg<"SPSR_und",           0b11, 0b100, 0b0100, 0b0011, 0b010>;
def : RWSysReg<"SPSR_fiq",           0b11, 0b100, 0b0100, 0b0011, 0b011>;
def : RWSysReg<"FPCR",               0b11, 0b011, 0b0100, 0b0100, 0b000>;
def : RWSysReg<"FPSR",               0b11, 0b011, 0b0100, 0b0100, 0b001>;
def : RWSysReg<"DSPSR_EL0",          0b11, 0b011, 0b0100, 0b0101, 0b000>;
def : RWSysReg<"DLR_EL0",            0b11, 0b011, 0b0100, 0b0101, 0b001>;
def : RWSysReg<"IFSR32_EL2",         0b11, 0b100, 0b0101, 0b0000, 0b001>;
def : RWSysReg<"AFSR0_EL1",          0b11, 0b000, 0b0101, 0b0001, 0b000>;
def : RWSysReg<"AFSR0_EL2",          0b11, 0b100, 0b0101, 0b0001, 0b000>;
def : RWSysReg<"AFSR0_EL3",          0b11, 0b110, 0b0101, 0b0001, 0b000>;
def : RWSysReg<"AFSR1_EL1",          0b11, 0b000, 0b0101, 0b0001, 0b001>;
def : RWSysReg<"AFSR1_EL2",          0b11, 0b100, 0b0101, 0b0001, 0b001>;
def : RWSysReg<"AFSR1_EL3",          0b11, 0b110, 0b0101, 0b0001, 0b001>;
def : RWSysReg<"ESR_EL1",            0b11, 0b000, 0b0101, 0b0010, 0b000>;
def : RWSysReg<"ESR_EL2",            0b11, 0b100, 0b0101, 0b0010, 0b000>;
def : RWSysReg<"ESR_EL3",            0b11, 0b110, 0b0101, 0b0010, 0b000>;
def : RWSysReg<"FPEXC32_EL2",        0b11, 0b100, 0b0101, 0b0011, 0b000>;
def : RWSysReg<"FAR_EL1",            0b11, 0b000, 0b0110, 0b0000, 0b000>;
def : RWSysReg<"FAR_EL2",            0b11, 0b100, 0b0110, 0b0000, 0b000>;
def : RWSysReg<"FAR_EL3",            0b11, 0b110, 0b0110, 0b0000, 0b000>;
def : RWSysReg<"HPFAR_EL2",          0b11, 0b100, 0b0110, 0b0000, 0b100>;
def : RWSysReg<"PAR_EL1",            0b11, 0b000, 0b0111, 0b0100, 0b000>;
def : RWSysReg<"PMCR_EL0",           0b11, 0b011, 0b1001, 0b1100, 0b000>;
def : RWSysReg<"PMCNTENSET_EL0",     0b11, 0b011, 0b1001, 0b1100, 0b001>;
def : RWSysReg<"PMCNTENCLR_EL0",     0b11, 0b011, 0b1001, 0b1100, 0b010>;
def : RWSysReg<"PMOVSCLR_EL0",       0b11, 0b011, 0b1001, 0b1100, 0b011>;
def : RWSysReg<"PMSELR_EL0",         0b11, 0b011, 0b1001, 0b1100, 0b101>;
def : RWSysReg<"PMCCNTR_EL0",        0b11, 0b011, 0b1001, 0b1101, 0b000>;
def : RWSysReg<"PMXEVTYPER_EL0",     0b11, 0b011, 0b1001, 0b1101, 0b001>;
def : RWSysReg<"PMXEVCNTR_EL0",      0b11, 0b011, 0b1001, 0b1101, 0b010>;
def : RWSysReg<"PMUSERENR_EL0",      0b11, 0b011, 0b1001, 0b1110, 0b000>;
def : RWSysReg<"PMINTENSET_EL1",     0b11, 0b000, 0b1001, 0b1110, 0b001>;
def : RWSysReg<"PMINTENCLR_EL1",     0b11, 0b000, 0b1001, 0b1110, 0b010>;
def : RWSysReg<"PMOVSSET_EL0",       0b11, 0b011, 0b1001, 0b1110, 0b011>;
def : RWSysReg<"MAIR_EL1",           0b11, 0b000, 0b1010, 0b0010, 0b000>;
def : RWSysReg<"MAIR_EL2",           0b11, 0b100, 0b1010, 0b0010, 0b000>;
def : RWSysReg<"MAIR_EL3",           0b11, 0b110, 0b1010, 0b0010, 0b000>;
def : RWSysReg<"AMAIR_EL1",          0b11, 0b000, 0b1010, 0b0011, 0b000>;
def : RWSysReg<"AMAIR_EL2",          0b11, 0b100, 0b1010, 0b0011, 0b000>;
def : RWSysReg<"AMAIR_EL3",          0b11, 0b110, 0b1010, 0b0011, 0b000>;
def : RWSysReg<"VBAR_EL1",           0b11, 0b000, 0b1100, 0b0000, 0b000>;
def : RWSysReg<"VBAR_EL2",           0b11, 0b100, 0b1100, 0b0000, 0b000>;
def : RWSysReg<"VBAR_EL3",           0b11, 0b110, 0b1100, 0b0000, 0b000>;
def : RWSysReg<"RMR_EL1",            0b11, 0b000, 0b1100, 0b0000, 0b010>;
def : RWSysReg<"RMR_EL2",            0b11, 0b100, 0b1100, 0b0000, 0b010>;
def : RWSysReg<"RMR_EL3",            0b11, 0b110, 0b1100, 0b0000, 0b010>;
def : RWSysReg<"CONTEXTIDR_EL1",     0b11, 0b000, 0b1101, 0b0000, 0b001>;
def : RWSysReg<"TPIDR_EL0",          0b11, 0b011, 0b1101, 0b0000, 0b010>;
def : RWSysReg<"TPIDR_EL2",          0b11, 0b100, 0b1101, 0b0000, 0b010>;
def : RWSysReg<"TPIDR_EL3",          0b11, 0b110, 0b1101, 0b0000, 0b010>;
def : RWSysReg<"TPIDRRO_EL0",        0b11, 0b011, 0b1101, 0b0000, 0b011>;
def : RWSysReg<"TPIDR_EL1",          0b11, 0b000, 0b1101, 0b0000, 0b100>;
def : RWSysReg<"CNTFRQ_EL0",         0b11, 0b011, 0b1110, 0b0000, 0b000>;
def : RWSysReg<"CNTVOFF_EL2",        0b11, 0b100, 0b1110, 0b0000, 0b011>;
def : RWSysReg<"CNTKCTL_EL1",        0b11, 0b000, 0b1110, 0b0001, 0b000>;
def : RWSysReg<"CNTHCTL_EL2",        0b11, 0b100, 0b1110, 0b0001, 0b000>;
def : RWSysReg<"CNTP_TVAL_EL0",      0b11, 0b011, 0b1110, 0b0010, 0b000>;
def : RWSysReg<"CNTHP_TVAL_EL2",     0b11, 0b100, 0b1110, 0b0010, 0b000>;
def : RWSysReg<"CNTPS_TVAL_EL1",     0b11, 0b111, 0b1110, 0b0010, 0b000>;
def : RWSysReg<"CNTP_CTL_EL0",       0b11, 0b011, 0b1110, 0b0010, 0b001>;
def : RWSysReg<"CNTHP_CTL_EL2",      0b11, 0b100, 0b1110, 0b0010, 0b001>;
def : RWSysReg<"CNTPS_CTL_EL1",      0b11, 0b111, 0b1110, 0b0010, 0b001>;
def : RWSysReg<"CNTP_CVAL_EL0",      0b11, 0b011, 0b1110, 0b0010, 0b010>;
def : RWSysReg<"CNTHP_CVAL_EL2",     0b11, 0b100, 0b1110, 0b0010, 0b010>;
def : RWSysReg<"CNTPS_CVAL_EL1",     0b11, 0b111, 0b1110, 0b0010, 0b010>;
def : RWSysReg<"CNTV_TVAL_EL0",      0b11, 0b011, 0b1110, 0b0011, 0b000>;
def : RWSysReg<"CNTV_CTL_EL0",       0b11, 0b011, 0b1110, 0b0011, 0b001>;
def : RWSysReg<"CNTV_CVAL_EL0",      0b11, 0b011, 0b1110, 0b0011, 0b010>;
def : RWSysReg<"PMEVCNTR0_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b000>;
def : RWSysReg<"PMEVCNTR1_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b001>;
def : RWSysReg<"PMEVCNTR2_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b010>;
def : RWSysReg<"PMEVCNTR3_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b011>;
def : RWSysReg<"PMEVCNTR4_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b100>;
def : RWSysReg<"PMEVCNTR5_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b101>;
def : RWSysReg<"PMEVCNTR6_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b110>;
def : RWSysReg<"PMEVCNTR7_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b111>;
def : RWSysReg<"PMEVCNTR8_EL0",      0b11, 0b011, 0b1110, 0b1001, 0b000>;
def : RWSysReg<"PMEVCNTR9_EL0",      0b11, 0b011, 0b1110, 0b1001, 0b001>;
def : RWSysReg<"PMEVCNTR10_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b010>;
def : RWSysReg<"PMEVCNTR11_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b011>;
def : RWSysReg<"PMEVCNTR12_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b100>;
def : RWSysReg<"PMEVCNTR13_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b101>;
def : RWSysReg<"PMEVCNTR14_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b110>;
def : RWSysReg<"PMEVCNTR15_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b111>;
def : RWSysReg<"PMEVCNTR16_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b000>;
def : RWSysReg<"PMEVCNTR17_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b001>;
def : RWSysReg<"PMEVCNTR18_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b010>;
def : RWSysReg<"PMEVCNTR19_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b011>;
def : RWSysReg<"PMEVCNTR20_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b100>;
def : RWSysReg<"PMEVCNTR21_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b101>;
def : RWSysReg<"PMEVCNTR22_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b110>;
def : RWSysReg<"PMEVCNTR23_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b111>;
def : RWSysReg<"PMEVCNTR24_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b000>;
def : RWSysReg<"PMEVCNTR25_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b001>;
def : RWSysReg<"PMEVCNTR26_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b010>;
def : RWSysReg<"PMEVCNTR27_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b011>;
def : RWSysReg<"PMEVCNTR28_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b100>;
def : RWSysReg<"PMEVCNTR29_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b101>;
def : RWSysReg<"PMEVCNTR30_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b110>;
def : RWSysReg<"PMCCFILTR_EL0",      0b11, 0b011, 0b1110, 0b1111, 0b111>;
def : RWSysReg<"PMEVTYPER0_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b000>;
def : RWSysReg<"PMEVTYPER1_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b001>;
def : RWSysReg<"PMEVTYPER2_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b010>;
def : RWSysReg<"PMEVTYPER3_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b011>;
def : RWSysReg<"PMEVTYPER4_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b100>;
def : RWSysReg<"PMEVTYPER5_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b101>;
def : RWSysReg<"PMEVTYPER6_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b110>;
def : RWSysReg<"PMEVTYPER7_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b111>;
def : RWSysReg<"PMEVTYPER8_EL0",     0b11, 0b011, 0b1110, 0b1101, 0b000>;
def : RWSysReg<"PMEVTYPER9_EL0",     0b11, 0b011, 0b1110, 0b1101, 0b001>;
def : RWSysReg<"PMEVTYPER10_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b010>;
def : RWSysReg<"PMEVTYPER11_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b011>;
def : RWSysReg<"PMEVTYPER12_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b100>;
def : RWSysReg<"PMEVTYPER13_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b101>;
def : RWSysReg<"PMEVTYPER14_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b110>;
def : RWSysReg<"PMEVTYPER15_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b111>;
def : RWSysReg<"PMEVTYPER16_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b000>;
def : RWSysReg<"PMEVTYPER17_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b001>;
def : RWSysReg<"PMEVTYPER18_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b010>;
def : RWSysReg<"PMEVTYPER19_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b011>;
def : RWSysReg<"PMEVTYPER20_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b100>;
def : RWSysReg<"PMEVTYPER21_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b101>;
def : RWSysReg<"PMEVTYPER22_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b110>;
def : RWSysReg<"PMEVTYPER23_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b111>;
def : RWSysReg<"PMEVTYPER24_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b000>;
def : RWSysReg<"PMEVTYPER25_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b001>;
def : RWSysReg<"PMEVTYPER26_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b010>;
def : RWSysReg<"PMEVTYPER27_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b011>;
def : RWSysReg<"PMEVTYPER28_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b100>;
def : RWSysReg<"PMEVTYPER29_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b101>;
def : RWSysReg<"PMEVTYPER30_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b110>;

// Trace registers
//                                 Op0    Op1     CRn     CRm    Op2
def : RWSysReg<"TRCPRGCTLR",         0b10, 0b001, 0b0000, 0b0001, 0b000>;
def : RWSysReg<"TRCPROCSELR",        0b10, 0b001, 0b0000, 0b0010, 0b000>;
def : RWSysReg<"TRCCONFIGR",         0b10, 0b001, 0b0000, 0b0100, 0b000>;
def : RWSysReg<"TRCAUXCTLR",         0b10, 0b001, 0b0000, 0b0110, 0b000>;
def : RWSysReg<"TRCEVENTCTL0R",      0b10, 0b001, 0b0000, 0b1000, 0b000>;
def : RWSysReg<"TRCEVENTCTL1R",      0b10, 0b001, 0b0000, 0b1001, 0b000>;
def : RWSysReg<"TRCSTALLCTLR",       0b10, 0b001, 0b0000, 0b1011, 0b000>;
def : RWSysReg<"TRCTSCTLR",          0b10, 0b001, 0b0000, 0b1100, 0b000>;
def : RWSysReg<"TRCSYNCPR",          0b10, 0b001, 0b0000, 0b1101, 0b000>;
def : RWSysReg<"TRCCCCTLR",          0b10, 0b001, 0b0000, 0b1110, 0b000>;
def : RWSysReg<"TRCBBCTLR",          0b10, 0b001, 0b0000, 0b1111, 0b000>;
def : RWSysReg<"TRCTRACEIDR",        0b10, 0b001, 0b0000, 0b0000, 0b001>;
def : RWSysReg<"TRCQCTLR",           0b10, 0b001, 0b0000, 0b0001, 0b001>;
def : RWSysReg<"TRCVICTLR",          0b10, 0b001, 0b0000, 0b0000, 0b010>;
def : RWSysReg<"TRCVIIECTLR",        0b10, 0b001, 0b0000, 0b0001, 0b010>;
def : RWSysReg<"TRCVISSCTLR",        0b10, 0b001, 0b0000, 0b0010, 0b010>;
def : RWSysReg<"TRCVIPCSSCTLR",      0b10, 0b001, 0b0000, 0b0011, 0b010>;
def : RWSysReg<"TRCVDCTLR",          0b10, 0b001, 0b0000, 0b1000, 0b010>;
def : RWSysReg<"TRCVDSACCTLR",       0b10, 0b001, 0b0000, 0b1001, 0b010>;
def : RWSysReg<"TRCVDARCCTLR",       0b10, 0b001, 0b0000, 0b1010, 0b010>;
def : RWSysReg<"TRCSEQEVR0",         0b10, 0b001, 0b0000, 0b0000, 0b100>;
def : RWSysReg<"TRCSEQEVR1",         0b10, 0b001, 0b0000, 0b0001, 0b100>;
def : RWSysReg<"TRCSEQEVR2",         0b10, 0b001, 0b0000, 0b0010, 0b100>;
def : RWSysReg<"TRCSEQRSTEVR",       0b10, 0b001, 0b0000, 0b0110, 0b100>;
def : RWSysReg<"TRCSEQSTR",          0b10, 0b001, 0b0000, 0b0111, 0b100>;
def : RWSysReg<"TRCEXTINSELR",       0b10, 0b001, 0b0000, 0b1000, 0b100>;
def : RWSysReg<"TRCCNTRLDVR0",       0b10, 0b001, 0b0000, 0b0000, 0b101>;
def : RWSysReg<"TRCCNTRLDVR1",       0b10, 0b001, 0b0000, 0b0001, 0b101>;
def : RWSysReg<"TRCCNTRLDVR2",       0b10, 0b001, 0b0000, 0b0010, 0b101>;
def : RWSysReg<"TRCCNTRLDVR3",       0b10, 0b001, 0b0000, 0b0011, 0b101>;
def : RWSysReg<"TRCCNTCTLR0",        0b10, 0b001, 0b0000, 0b0100, 0b101>;
def : RWSysReg<"TRCCNTCTLR1",        0b10, 0b001, 0b0000, 0b0101, 0b101>;
def : RWSysReg<"TRCCNTCTLR2",        0b10, 0b001, 0b0000, 0b0110, 0b101>;
def : RWSysReg<"TRCCNTCTLR3",        0b10, 0b001, 0b0000, 0b0111, 0b101>;
def : RWSysReg<"TRCCNTVR0",          0b10, 0b001, 0b0000, 0b1000, 0b101>;
def : RWSysReg<"TRCCNTVR1",          0b10, 0b001, 0b0000, 0b1001, 0b101>;
def : RWSysReg<"TRCCNTVR2",          0b10, 0b001, 0b0000, 0b1010, 0b101>;
def : RWSysReg<"TRCCNTVR3",          0b10, 0b001, 0b0000, 0b1011, 0b101>;
def : RWSysReg<"TRCIMSPEC0",         0b10, 0b001, 0b0000, 0b0000, 0b111>;
def : RWSysReg<"TRCIMSPEC1",         0b10, 0b001, 0b0000, 0b0001, 0b111>;
def : RWSysReg<"TRCIMSPEC2",         0b10, 0b001, 0b0000, 0b0010, 0b111>;
def : RWSysReg<"TRCIMSPEC3",         0b10, 0b001, 0b0000, 0b0011, 0b111>;
def : RWSysReg<"TRCIMSPEC4",         0b10, 0b001, 0b0000, 0b0100, 0b111>;
def : RWSysReg<"TRCIMSPEC5",         0b10, 0b001, 0b0000, 0b0101, 0b111>;
def : RWSysReg<"TRCIMSPEC6",         0b10, 0b001, 0b0000, 0b0110, 0b111>;
def : RWSysReg<"TRCIMSPEC7",         0b10, 0b001, 0b0000, 0b0111, 0b111>;
def : RWSysReg<"TRCRSCTLR2",         0b10, 0b001, 0b0001, 0b0010, 0b000>;
def : RWSysReg<"TRCRSCTLR3",         0b10, 0b001, 0b0001, 0b0011, 0b000>;
def : RWSysReg<"TRCRSCTLR4",         0b10, 0b001, 0b0001, 0b0100, 0b000>;
def : RWSysReg<"TRCRSCTLR5",         0b10, 0b001, 0b0001, 0b0101, 0b000>;
def : RWSysReg<"TRCRSCTLR6",         0b10, 0b001, 0b0001, 0b0110, 0b000>;
def : RWSysReg<"TRCRSCTLR7",         0b10, 0b001, 0b0001, 0b0111, 0b000>;
def : RWSysReg<"TRCRSCTLR8",         0b10, 0b001, 0b0001, 0b1000, 0b000>;
def : RWSysReg<"TRCRSCTLR9",         0b10, 0b001, 0b0001, 0b1001, 0b000>;
def : RWSysReg<"TRCRSCTLR10",        0b10, 0b001, 0b0001, 0b1010, 0b000>;
def : RWSysReg<"TRCRSCTLR11",        0b10, 0b001, 0b0001, 0b1011, 0b000>;
def : RWSysReg<"TRCRSCTLR12",        0b10, 0b001, 0b0001, 0b1100, 0b000>;
def : RWSysReg<"TRCRSCTLR13",        0b10, 0b001, 0b0001, 0b1101, 0b000>;
def : RWSysReg<"TRCRSCTLR14",        0b10, 0b001, 0b0001, 0b1110, 0b000>;
def : RWSysReg<"TRCRSCTLR15",        0b10, 0b001, 0b0001, 0b1111, 0b000>;
def : RWSysReg<"TRCRSCTLR16",        0b10, 0b001, 0b0001, 0b0000, 0b001>;
def : RWSysReg<"TRCRSCTLR17",        0b10, 0b001, 0b0001, 0b0001, 0b001>;
def : RWSysReg<"TRCRSCTLR18",        0b10, 0b001, 0b0001, 0b0010, 0b001>;
def : RWSysReg<"TRCRSCTLR19",        0b10, 0b001, 0b0001, 0b0011, 0b001>;
def : RWSysReg<"TRCRSCTLR20",        0b10, 0b001, 0b0001, 0b0100, 0b001>;
def : RWSysReg<"TRCRSCTLR21",        0b10, 0b001, 0b0001, 0b0101, 0b001>;
def : RWSysReg<"TRCRSCTLR22",        0b10, 0b001, 0b0001, 0b0110, 0b001>;
def : RWSysReg<"TRCRSCTLR23",        0b10, 0b001, 0b0001, 0b0111, 0b001>;
def : RWSysReg<"TRCRSCTLR24",        0b10, 0b001, 0b0001, 0b1000, 0b001>;
def : RWSysReg<"TRCRSCTLR25",        0b10, 0b001, 0b0001, 0b1001, 0b001>;
def : RWSysReg<"TRCRSCTLR26",        0b10, 0b001, 0b0001, 0b1010, 0b001>;
def : RWSysReg<"TRCRSCTLR27",        0b10, 0b001, 0b0001, 0b1011, 0b001>;
def : RWSysReg<"TRCRSCTLR28",        0b10, 0b001, 0b0001, 0b1100, 0b001>;
def : RWSysReg<"TRCRSCTLR29",        0b10, 0b001, 0b0001, 0b1101, 0b001>;
def : RWSysReg<"TRCRSCTLR30",        0b10, 0b001, 0b0001, 0b1110, 0b001>;
def : RWSysReg<"TRCRSCTLR31",        0b10, 0b001, 0b0001, 0b1111, 0b001>;
def : RWSysReg<"TRCSSCCR0",          0b10, 0b001, 0b0001, 0b0000, 0b010>;
def : RWSysReg<"TRCSSCCR1",          0b10, 0b001, 0b0001, 0b0001, 0b010>;
def : RWSysReg<"TRCSSCCR2",          0b10, 0b001, 0b0001, 0b0010, 0b010>;
def : RWSysReg<"TRCSSCCR3",          0b10, 0b001, 0b0001, 0b0011, 0b010>;
def : RWSysReg<"TRCSSCCR4",          0b10, 0b001, 0b0001, 0b0100, 0b010>;
def : RWSysReg<"TRCSSCCR5",          0b10, 0b001, 0b0001, 0b0101, 0b010>;
def : RWSysReg<"TRCSSCCR6",          0b10, 0b001, 0b0001, 0b0110, 0b010>;
def : RWSysReg<"TRCSSCCR7",          0b10, 0b001, 0b0001, 0b0111, 0b010>;
def : RWSysReg<"TRCSSCSR0",          0b10, 0b001, 0b0001, 0b1000, 0b010>;
def : RWSysReg<"TRCSSCSR1",          0b10, 0b001, 0b0001, 0b1001, 0b010>;
def : RWSysReg<"TRCSSCSR2",          0b10, 0b001, 0b0001, 0b1010, 0b010>;
def : RWSysReg<"TRCSSCSR3",          0b10, 0b001, 0b0001, 0b1011, 0b010>;
def : RWSysReg<"TRCSSCSR4",          0b10, 0b001, 0b0001, 0b1100, 0b010>;
def : RWSysReg<"TRCSSCSR5",          0b10, 0b001, 0b0001, 0b1101, 0b010>;
def : RWSysReg<"TRCSSCSR6",          0b10, 0b001, 0b0001, 0b1110, 0b010>;
def : RWSysReg<"TRCSSCSR7",          0b10, 0b001, 0b0001, 0b1111, 0b010>;
def : RWSysReg<"TRCSSPCICR0",        0b10, 0b001, 0b0001, 0b0000, 0b011>;
def : RWSysReg<"TRCSSPCICR1",        0b10, 0b001, 0b0001, 0b0001, 0b011>;
def : RWSysReg<"TRCSSPCICR2",        0b10, 0b001, 0b0001, 0b0010, 0b011>;
def : RWSysReg<"TRCSSPCICR3",        0b10, 0b001, 0b0001, 0b0011, 0b011>;
def : RWSysReg<"TRCSSPCICR4",        0b10, 0b001, 0b0001, 0b0100, 0b011>;
def : RWSysReg<"TRCSSPCICR5",        0b10, 0b001, 0b0001, 0b0101, 0b011>;
def : RWSysReg<"TRCSSPCICR6",        0b10, 0b001, 0b0001, 0b0110, 0b011>;
def : RWSysReg<"TRCSSPCICR7",        0b10, 0b001, 0b0001, 0b0111, 0b011>;
def : RWSysReg<"TRCPDCR",            0b10, 0b001, 0b0001, 0b0100, 0b100>;
def : RWSysReg<"TRCACVR0",           0b10, 0b001, 0b0010, 0b0000, 0b000>;
def : RWSysReg<"TRCACVR1",           0b10, 0b001, 0b0010, 0b0010, 0b000>;
def : RWSysReg<"TRCACVR2",           0b10, 0b001, 0b0010, 0b0100, 0b000>;
def : RWSysReg<"TRCACVR3",           0b10, 0b001, 0b0010, 0b0110, 0b000>;
def : RWSysReg<"TRCACVR4",           0b10, 0b001, 0b0010, 0b1000, 0b000>;
def : RWSysReg<"TRCACVR5",           0b10, 0b001, 0b0010, 0b1010, 0b000>;
def : RWSysReg<"TRCACVR6",           0b10, 0b001, 0b0010, 0b1100, 0b000>;
def : RWSysReg<"TRCACVR7",           0b10, 0b001, 0b0010, 0b1110, 0b000>;
def : RWSysReg<"TRCACVR8",           0b10, 0b001, 0b0010, 0b0000, 0b001>;
def : RWSysReg<"TRCACVR9",           0b10, 0b001, 0b0010, 0b0010, 0b001>;
def : RWSysReg<"TRCACVR10",          0b10, 0b001, 0b0010, 0b0100, 0b001>;
def : RWSysReg<"TRCACVR11",          0b10, 0b001, 0b0010, 0b0110, 0b001>;
def : RWSysReg<"TRCACVR12",          0b10, 0b001, 0b0010, 0b1000, 0b001>;
def : RWSysReg<"TRCACVR13",          0b10, 0b001, 0b0010, 0b1010, 0b001>;
def : RWSysReg<"TRCACVR14",          0b10, 0b001, 0b0010, 0b1100, 0b001>;
def : RWSysReg<"TRCACVR15",          0b10, 0b001, 0b0010, 0b1110, 0b001>;
def : RWSysReg<"TRCACATR0",          0b10, 0b001, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"TRCACATR1",          0b10, 0b001, 0b0010, 0b0010, 0b010>;
def : RWSysReg<"TRCACATR2",          0b10, 0b001, 0b0010, 0b0100, 0b010>;
def : RWSysReg<"TRCACATR3",          0b10, 0b001, 0b0010, 0b0110, 0b010>;
def : RWSysReg<"TRCACATR4",          0b10, 0b001, 0b0010, 0b1000, 0b010>;
def : RWSysReg<"TRCACATR5",          0b10, 0b001, 0b0010, 0b1010, 0b010>;
def : RWSysReg<"TRCACATR6",          0b10, 0b001, 0b0010, 0b1100, 0b010>;
def : RWSysReg<"TRCACATR7",          0b10, 0b001, 0b0010, 0b1110, 0b010>;
def : RWSysReg<"TRCACATR8",          0b10, 0b001, 0b0010, 0b0000, 0b011>;
def : RWSysReg<"TRCACATR9",          0b10, 0b001, 0b0010, 0b0010, 0b011>;
def : RWSysReg<"TRCACATR10",         0b10, 0b001, 0b0010, 0b0100, 0b011>;
def : RWSysReg<"TRCACATR11",         0b10, 0b001, 0b0010, 0b0110, 0b011>;
def : RWSysReg<"TRCACATR12",         0b10, 0b001, 0b0010, 0b1000, 0b011>;
def : RWSysReg<"TRCACATR13",         0b10, 0b001, 0b0010, 0b1010, 0b011>;
def : RWSysReg<"TRCACATR14",         0b10, 0b001, 0b0010, 0b1100, 0b011>;
def : RWSysReg<"TRCACATR15",         0b10, 0b001, 0b0010, 0b1110, 0b011>;
def : RWSysReg<"TRCDVCVR0",          0b10, 0b001, 0b0010, 0b0000, 0b100>;
def : RWSysReg<"TRCDVCVR1",          0b10, 0b001, 0b0010, 0b0100, 0b100>;
def : RWSysReg<"TRCDVCVR2",          0b10, 0b001, 0b0010, 0b1000, 0b100>;
def : RWSysReg<"TRCDVCVR3",          0b10, 0b001, 0b0010, 0b1100, 0b100>;
def : RWSysReg<"TRCDVCVR4",          0b10, 0b001, 0b0010, 0b0000, 0b101>;
def : RWSysReg<"TRCDVCVR5",          0b10, 0b001, 0b0010, 0b0100, 0b101>;
def : RWSysReg<"TRCDVCVR6",          0b10, 0b001, 0b0010, 0b1000, 0b101>;
def : RWSysReg<"TRCDVCVR7",          0b10, 0b001, 0b0010, 0b1100, 0b101>;
def : RWSysReg<"TRCDVCMR0",          0b10, 0b001, 0b0010, 0b0000, 0b110>;
def : RWSysReg<"TRCDVCMR1",          0b10, 0b001, 0b0010, 0b0100, 0b110>;
def : RWSysReg<"TRCDVCMR2",          0b10, 0b001, 0b0010, 0b1000, 0b110>;
def : RWSysReg<"TRCDVCMR3",          0b10, 0b001, 0b0010, 0b1100, 0b110>;
def : RWSysReg<"TRCDVCMR4",          0b10, 0b001, 0b0010, 0b0000, 0b111>;
def : RWSysReg<"TRCDVCMR5",          0b10, 0b001, 0b0010, 0b0100, 0b111>;
def : RWSysReg<"TRCDVCMR6",          0b10, 0b001, 0b0010, 0b1000, 0b111>;
def : RWSysReg<"TRCDVCMR7",          0b10, 0b001, 0b0010, 0b1100, 0b111>;
def : RWSysReg<"TRCCIDCVR0",         0b10, 0b001, 0b0011, 0b0000, 0b000>;
def : RWSysReg<"TRCCIDCVR1",         0b10, 0b001, 0b0011, 0b0010, 0b000>;
def : RWSysReg<"TRCCIDCVR2",         0b10, 0b001, 0b0011, 0b0100, 0b000>;
def : RWSysReg<"TRCCIDCVR3",         0b10, 0b001, 0b0011, 0b0110, 0b000>;
def : RWSysReg<"TRCCIDCVR4",         0b10, 0b001, 0b0011, 0b1000, 0b000>;
def : RWSysReg<"TRCCIDCVR5",         0b10, 0b001, 0b0011, 0b1010, 0b000>;
def : RWSysReg<"TRCCIDCVR6",         0b10, 0b001, 0b0011, 0b1100, 0b000>;
def : RWSysReg<"TRCCIDCVR7",         0b10, 0b001, 0b0011, 0b1110, 0b000>;
def : RWSysReg<"TRCVMIDCVR0",        0b10, 0b001, 0b0011, 0b0000, 0b001>;
def : RWSysReg<"TRCVMIDCVR1",        0b10, 0b001, 0b0011, 0b0010, 0b001>;
def : RWSysReg<"TRCVMIDCVR2",        0b10, 0b001, 0b0011, 0b0100, 0b001>;
def : RWSysReg<"TRCVMIDCVR3",        0b10, 0b001, 0b0011, 0b0110, 0b001>;
def : RWSysReg<"TRCVMIDCVR4",        0b10, 0b001, 0b0011, 0b1000, 0b001>;
def : RWSysReg<"TRCVMIDCVR5",        0b10, 0b001, 0b0011, 0b1010, 0b001>;
def : RWSysReg<"TRCVMIDCVR6",        0b10, 0b001, 0b0011, 0b1100, 0b001>;
def : RWSysReg<"TRCVMIDCVR7",        0b10, 0b001, 0b0011, 0b1110, 0b001>;
def : RWSysReg<"TRCCIDCCTLR0",       0b10, 0b001, 0b0011, 0b0000, 0b010>;
def : RWSysReg<"TRCCIDCCTLR1",       0b10, 0b001, 0b0011, 0b0001, 0b010>;
def : RWSysReg<"TRCVMIDCCTLR0",      0b10, 0b001, 0b0011, 0b0010, 0b010>;
def : RWSysReg<"TRCVMIDCCTLR1",      0b10, 0b001, 0b0011, 0b0011, 0b010>;
def : RWSysReg<"TRCITCTRL",          0b10, 0b001, 0b0111, 0b0000, 0b100>;
def : RWSysReg<"TRCCLAIMSET",        0b10, 0b001, 0b0111, 0b1000, 0b110>;
def : RWSysReg<"TRCCLAIMCLR",        0b10, 0b001, 0b0111, 0b1001, 0b110>;

// GICv3 registers
//                                 Op0    Op1     CRn     CRm    Op2
def : RWSysReg<"ICC_BPR1_EL1",       0b11, 0b000, 0b1100, 0b1100, 0b011>;
def : RWSysReg<"ICC_BPR0_EL1",       0b11, 0b000, 0b1100, 0b1000, 0b011>;
def : RWSysReg<"ICC_PMR_EL1",        0b11, 0b000, 0b0100, 0b0110, 0b000>;
def : RWSysReg<"ICC_CTLR_EL1",       0b11, 0b000, 0b1100, 0b1100, 0b100>;
def : RWSysReg<"ICC_CTLR_EL3",       0b11, 0b110, 0b1100, 0b1100, 0b100>;
def : RWSysReg<"ICC_SRE_EL1",        0b11, 0b000, 0b1100, 0b1100, 0b101>;
def : RWSysReg<"ICC_SRE_EL2",        0b11, 0b100, 0b1100, 0b1001, 0b101>;
def : RWSysReg<"ICC_SRE_EL3",        0b11, 0b110, 0b1100, 0b1100, 0b101>;
def : RWSysReg<"ICC_IGRPEN0_EL1",    0b11, 0b000, 0b1100, 0b1100, 0b110>;
def : RWSysReg<"ICC_IGRPEN1_EL1",    0b11, 0b000, 0b1100, 0b1100, 0b111>;
def : RWSysReg<"ICC_IGRPEN1_EL3",    0b11, 0b110, 0b1100, 0b1100, 0b111>;
def : RWSysReg<"ICC_SEIEN_EL1",      0b11, 0b000, 0b1100, 0b1101, 0b000>;
def : RWSysReg<"ICC_AP0R0_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b100>;
def : RWSysReg<"ICC_AP0R1_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b101>;
def : RWSysReg<"ICC_AP0R2_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b110>;
def : RWSysReg<"ICC_AP0R3_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b111>;
def : RWSysReg<"ICC_AP1R0_EL1",      0b11, 0b000, 0b1100, 0b1001, 0b000>;
def : RWSysReg<"ICC_AP1R1_EL1",      0b11, 0b000, 0b1100, 0b1001, 0b001>;
def : RWSysReg<"ICC_AP1R2_EL1",      0b11, 0b000, 0b1100, 0b1001, 0b010>;
def : RWSysReg<"ICC_AP1R3_EL1",      0b11, 0b000, 0b1100, 0b1001, 0b011>;
def : RWSysReg<"ICH_AP0R0_EL2",      0b11, 0b100, 0b1100, 0b1000, 0b000>;
def : RWSysReg<"ICH_AP0R1_EL2",      0b11, 0b100, 0b1100, 0b1000, 0b001>;
def : RWSysReg<"ICH_AP0R2_EL2",      0b11, 0b100, 0b1100, 0b1000, 0b010>;
def : RWSysReg<"ICH_AP0R3_EL2",      0b11, 0b100, 0b1100, 0b1000, 0b011>;
def : RWSysReg<"ICH_AP1R0_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b000>;
def : RWSysReg<"ICH_AP1R1_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b001>;
def : RWSysReg<"ICH_AP1R2_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b010>;
def : RWSysReg<"ICH_AP1R3_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b011>;
def : RWSysReg<"ICH_HCR_EL2",        0b11, 0b100, 0b1100, 0b1011, 0b000>;
def : RWSysReg<"ICH_MISR_EL2",       0b11, 0b100, 0b1100, 0b1011, 0b010>;
def : RWSysReg<"ICH_VMCR_EL2",       0b11, 0b100, 0b1100, 0b1011, 0b111>;
def : RWSysReg<"ICH_VSEIR_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b100>;
def : RWSysReg<"ICH_LR0_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b000>;
def : RWSysReg<"ICH_LR1_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b001>;
def : RWSysReg<"ICH_LR2_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b010>;
def : RWSysReg<"ICH_LR3_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b011>;
def : RWSysReg<"ICH_LR4_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b100>;
def : RWSysReg<"ICH_LR5_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b101>;
def : RWSysReg<"ICH_LR6_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b110>;
def : RWSysReg<"ICH_LR7_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b111>;
def : RWSysReg<"ICH_LR8_EL2",        0b11, 0b100, 0b1100, 0b1101, 0b000>;
def : RWSysReg<"ICH_LR9_EL2",        0b11, 0b100, 0b1100, 0b1101, 0b001>;
def : RWSysReg<"ICH_LR10_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b010>;
def : RWSysReg<"ICH_LR11_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b011>;
def : RWSysReg<"ICH_LR12_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b100>;
def : RWSysReg<"ICH_LR13_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b101>;
def : RWSysReg<"ICH_LR14_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b110>;
def : RWSysReg<"ICH_LR15_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b111>;

// v8.1a "Privileged Access Never" extension-specific system registers
let Requires = [{ {AArch64::FeaturePAN} }] in
def : RWSysReg<"PAN", 0b11, 0b000, 0b0100, 0b0010, 0b011>;

// v8.1a "Limited Ordering Regions" extension-specific system registers
//                         Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureLOR} }] in {
def : RWSysReg<"LORSA_EL1",  0b11, 0b000, 0b1010, 0b0100, 0b000>;
def : RWSysReg<"LOREA_EL1",  0b11, 0b000, 0b1010, 0b0100, 0b001>;
def : RWSysReg<"LORN_EL1",   0b11, 0b000, 0b1010, 0b0100, 0b010>;
def : RWSysReg<"LORC_EL1",   0b11, 0b000, 0b1010, 0b0100, 0b011>;
}

// v8.1a "Virtualization Host extensions" system registers
//                              Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureVH} }] in {
def : RWSysReg<"TTBR1_EL2",       0b11, 0b100, 0b0010, 0b0000, 0b001>;
def : RWSysReg<"CONTEXTIDR_EL2",  0b11, 0b100, 0b1101, 0b0000, 0b001>;
def : RWSysReg<"CNTHV_TVAL_EL2",  0b11, 0b100, 0b1110, 0b0011, 0b000>;
def : RWSysReg<"CNTHV_CVAL_EL2",  0b11, 0b100, 0b1110, 0b0011, 0b010>;
def : RWSysReg<"CNTHV_CTL_EL2",   0b11, 0b100, 0b1110, 0b0011, 0b001>;
def : RWSysReg<"SCTLR_EL12",      0b11, 0b101, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"CPACR_EL12",      0b11, 0b101, 0b0001, 0b0000, 0b010>;
def : RWSysReg<"TTBR0_EL12",      0b11, 0b101, 0b0010, 0b0000, 0b000>;
def : RWSysReg<"TTBR1_EL12",      0b11, 0b101, 0b0010, 0b0000, 0b001>;
def : RWSysReg<"TCR_EL12",        0b11, 0b101, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"AFSR0_EL12",      0b11, 0b101, 0b0101, 0b0001, 0b000>;
def : RWSysReg<"AFSR1_EL12",      0b11, 0b101, 0b0101, 0b0001, 0b001>;
def : RWSysReg<"ESR_EL12",        0b11, 0b101, 0b0101, 0b0010, 0b000>;
def : RWSysReg<"FAR_EL12",        0b11, 0b101, 0b0110, 0b0000, 0b000>;
def : RWSysReg<"MAIR_EL12",       0b11, 0b101, 0b1010, 0b0010, 0b000>;
def : RWSysReg<"AMAIR_EL12",      0b11, 0b101, 0b1010, 0b0011, 0b000>;
def : RWSysReg<"VBAR_EL12",       0b11, 0b101, 0b1100, 0b0000, 0b000>;
def : RWSysReg<"CONTEXTIDR_EL12", 0b11, 0b101, 0b1101, 0b0000, 0b001>;
def : RWSysReg<"CNTKCTL_EL12",    0b11, 0b101, 0b1110, 0b0001, 0b000>;
def : RWSysReg<"CNTP_TVAL_EL02",  0b11, 0b101, 0b1110, 0b0010, 0b000>;
def : RWSysReg<"CNTP_CTL_EL02",   0b11, 0b101, 0b1110, 0b0010, 0b001>;
def : RWSysReg<"CNTP_CVAL_EL02",  0b11, 0b101, 0b1110, 0b0010, 0b010>;
def : RWSysReg<"CNTV_TVAL_EL02",  0b11, 0b101, 0b1110, 0b0011, 0b000>;
def : RWSysReg<"CNTV_CTL_EL02",   0b11, 0b101, 0b1110, 0b0011, 0b001>;
def : RWSysReg<"CNTV_CVAL_EL02",  0b11, 0b101, 0b1110, 0b0011, 0b010>;
def : RWSysReg<"SPSR_EL12",       0b11, 0b101, 0b0100, 0b0000, 0b000>;
def : RWSysReg<"ELR_EL12",        0b11, 0b101, 0b0100, 0b0000, 0b001>;
}
// v8.2a registers
//                  Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeaturePsUAO} }] in
def : RWSysReg<"UAO", 0b11, 0b000, 0b0100, 0b0010, 0b100>;

// v8.2a "Statistical Profiling extension" registers
//                            Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureSPE} }] in {
def : RWSysReg<"PMBLIMITR_EL1", 0b11, 0b000, 0b1001, 0b1010, 0b000>;
def : RWSysReg<"PMBPTR_EL1",    0b11, 0b000, 0b1001, 0b1010, 0b001>;
def : RWSysReg<"PMBSR_EL1",     0b11, 0b000, 0b1001, 0b1010, 0b011>;
def : RWSysReg<"PMBIDR_EL1",    0b11, 0b000, 0b1001, 0b1010, 0b111>;
def : RWSysReg<"PMSCR_EL2",     0b11, 0b100, 0b1001, 0b1001, 0b000>;
def : RWSysReg<"PMSCR_EL12",    0b11, 0b101, 0b1001, 0b1001, 0b000>;
def : RWSysReg<"PMSCR_EL1",     0b11, 0b000, 0b1001, 0b1001, 0b000>;
def : RWSysReg<"PMSICR_EL1",    0b11, 0b000, 0b1001, 0b1001, 0b010>;
def : RWSysReg<"PMSIRR_EL1",    0b11, 0b000, 0b1001, 0b1001, 0b011>;
def : RWSysReg<"PMSFCR_EL1",    0b11, 0b000, 0b1001, 0b1001, 0b100>;
def : RWSysReg<"PMSEVFR_EL1",   0b11, 0b000, 0b1001, 0b1001, 0b101>;
def : RWSysReg<"PMSLATFR_EL1",  0b11, 0b000, 0b1001, 0b1001, 0b110>;
def : RWSysReg<"PMSIDR_EL1",    0b11, 0b000, 0b1001, 0b1001, 0b111>;
}

// v8.2a "RAS extension" registers
//                         Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureRAS} }] in {
def : RWSysReg<"ERRSELR_EL1",   0b11, 0b000, 0b0101, 0b0011, 0b001>;
def : RWSysReg<"ERXCTLR_EL1",   0b11, 0b000, 0b0101, 0b0100, 0b001>;
def : RWSysReg<"ERXSTATUS_EL1", 0b11, 0b000, 0b0101, 0b0100, 0b010>;
def : RWSysReg<"ERXADDR_EL1",   0b11, 0b000, 0b0101, 0b0100, 0b011>;
def : RWSysReg<"ERXMISC0_EL1",  0b11, 0b000, 0b0101, 0b0101, 0b000>;
def : RWSysReg<"ERXMISC1_EL1",  0b11, 0b000, 0b0101, 0b0101, 0b001>;
def : RWSysReg<"DISR_EL1",      0b11, 0b000, 0b1100, 0b0001, 0b001>;
def : RWSysReg<"VDISR_EL2",     0b11, 0b100, 0b1100, 0b0001, 0b001>;
def : RWSysReg<"VSESR_EL2",     0b11, 0b100, 0b0101, 0b0010, 0b011>;
}

// v8.3a "Pointer authentication extension" registers
//                              Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeaturePA} }] in {
def : RWSysReg<"APIAKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b000>;
def : RWSysReg<"APIAKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b001>;
def : RWSysReg<"APIBKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b010>;
def : RWSysReg<"APIBKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b011>;
def : RWSysReg<"APDAKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b000>;
def : RWSysReg<"APDAKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b001>;
def : RWSysReg<"APDBKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b010>;
def : RWSysReg<"APDBKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b011>;
def : RWSysReg<"APGAKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0011, 0b000>;
def : RWSysReg<"APGAKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0011, 0b001>;
}

// v8.4 "Secure Exception Level 2 extension"
let Requires = [{ {AArch64::FeatureSEL2} }] in {
// v8.4a "Virtualization secure second stage translation" registers
//                           Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"VSTCR_EL2" , 0b11, 0b100, 0b0010, 0b0110, 0b010>;
def : RWSysReg<"VSTTBR_EL2", 0b11, 0b100, 0b0010, 0b0110, 0b000>;

// v8.4a "Virtualization timer" registers
//                                Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"CNTHVS_TVAL_EL2", 0b11, 0b100, 0b1110, 0b0100, 0b000>;
def : RWSysReg<"CNTHVS_CVAL_EL2", 0b11, 0b100, 0b1110, 0b0100, 0b010>;
def : RWSysReg<"CNTHVS_CTL_EL2",  0b11, 0b100, 0b1110, 0b0100, 0b001>;
def : RWSysReg<"CNTHPS_TVAL_EL2", 0b11, 0b100, 0b1110, 0b0101, 0b000>;
def : RWSysReg<"CNTHPS_CVAL_EL2", 0b11, 0b100, 0b1110, 0b0101, 0b010>;
def : RWSysReg<"CNTHPS_CTL_EL2",  0b11, 0b100, 0b1110, 0b0101, 0b001>;

// v8.4a "Virtualization debug state" registers
//                           Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"SDER32_EL2", 0b11, 0b100, 0b0001, 0b0011, 0b001>;
} // FeatureSEL2

// v8.4a RAS registers
//                              Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureRASv8_4} }] in {
def : RWSysReg<"ERXPFGCTL_EL1", 0b11, 0b000, 0b0101, 0b0100, 0b101>;
def : RWSysReg<"ERXPFGCDN_EL1", 0b11, 0b000, 0b0101, 0b0100, 0b110>;
def : RWSysReg<"ERXTS_EL1",     0b11, 0b000, 0b0101, 0b0101, 0b111>;
def : RWSysReg<"ERXMISC2_EL1",  0b11, 0b000, 0b0101, 0b0101, 0b010>;
def : RWSysReg<"ERXMISC3_EL1",  0b11, 0b000, 0b0101, 0b0101, 0b011>;
def : ROSysReg<"ERXPFGF_EL1",   0b11, 0b000, 0b0101, 0b0100, 0b100>;
} // FeatureRASv8_4

// v8.4a MPAM registers
//                             Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureMPAM} }] in {
def : RWSysReg<"MPAM0_EL1",    0b11, 0b000, 0b1010, 0b0101, 0b001>;
def : RWSysReg<"MPAM1_EL1",    0b11, 0b000, 0b1010, 0b0101, 0b000>;
def : RWSysReg<"MPAM2_EL2",    0b11, 0b100, 0b1010, 0b0101, 0b000>;
def : RWSysReg<"MPAM3_EL3",    0b11, 0b110, 0b1010, 0b0101, 0b000>;
def : RWSysReg<"MPAM1_EL12",   0b11, 0b101, 0b1010, 0b0101, 0b000>;
def : RWSysReg<"MPAMHCR_EL2",  0b11, 0b100, 0b1010, 0b0100, 0b000>;
def : RWSysReg<"MPAMVPMV_EL2", 0b11, 0b100, 0b1010, 0b0100, 0b001>;
def : RWSysReg<"MPAMVPM0_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b000>;
def : RWSysReg<"MPAMVPM1_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b001>;
def : RWSysReg<"MPAMVPM2_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b010>;
def : RWSysReg<"MPAMVPM3_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b011>;
def : RWSysReg<"MPAMVPM4_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b100>;
def : RWSysReg<"MPAMVPM5_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b101>;
def : RWSysReg<"MPAMVPM6_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b110>;
def : RWSysReg<"MPAMVPM7_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b111>;
def : ROSysReg<"MPAMIDR_EL1",  0b11, 0b000, 0b1010, 0b0100, 0b100>;
} //FeatureMPAM

// v8.4a Activitiy Monitor registers
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureAM} }] in {
def : RWSysReg<"AMCR_EL0",         0b11, 0b011, 0b1101, 0b0010, 0b000>;
def : ROSysReg<"AMCFGR_EL0",       0b11, 0b011, 0b1101, 0b0010, 0b001>;
def : ROSysReg<"AMCGCR_EL0",       0b11, 0b011, 0b1101, 0b0010, 0b010>;
def : RWSysReg<"AMUSERENR_EL0",    0b11, 0b011, 0b1101, 0b0010, 0b011>;
def : RWSysReg<"AMCNTENCLR0_EL0",  0b11, 0b011, 0b1101, 0b0010, 0b100>;
def : RWSysReg<"AMCNTENSET0_EL0",  0b11, 0b011, 0b1101, 0b0010, 0b101>;
def : RWSysReg<"AMEVCNTR00_EL0",   0b11, 0b011, 0b1101, 0b0100, 0b000>;
def : RWSysReg<"AMEVCNTR01_EL0",   0b11, 0b011, 0b1101, 0b0100, 0b001>;
def : RWSysReg<"AMEVCNTR02_EL0",   0b11, 0b011, 0b1101, 0b0100, 0b010>;
def : RWSysReg<"AMEVCNTR03_EL0",   0b11, 0b011, 0b1101, 0b0100, 0b011>;
def : ROSysReg<"AMEVTYPER00_EL0",  0b11, 0b011, 0b1101, 0b0110, 0b000>;
def : ROSysReg<"AMEVTYPER01_EL0",  0b11, 0b011, 0b1101, 0b0110, 0b001>;
def : ROSysReg<"AMEVTYPER02_EL0",  0b11, 0b011, 0b1101, 0b0110, 0b010>;
def : ROSysReg<"AMEVTYPER03_EL0",  0b11, 0b011, 0b1101, 0b0110, 0b011>;
def : RWSysReg<"AMCNTENCLR1_EL0",  0b11, 0b011, 0b1101, 0b0011, 0b000>;
def : RWSysReg<"AMCNTENSET1_EL0",  0b11, 0b011, 0b1101, 0b0011, 0b001>;
def : RWSysReg<"AMEVCNTR10_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b000>;
def : RWSysReg<"AMEVCNTR11_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b001>;
def : RWSysReg<"AMEVCNTR12_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b010>;
def : RWSysReg<"AMEVCNTR13_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b011>;
def : RWSysReg<"AMEVCNTR14_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b100>;
def : RWSysReg<"AMEVCNTR15_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b101>;
def : RWSysReg<"AMEVCNTR16_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b110>;
def : RWSysReg<"AMEVCNTR17_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b111>;
def : RWSysReg<"AMEVCNTR18_EL0",   0b11, 0b011, 0b1101, 0b1101, 0b000>;
def : RWSysReg<"AMEVCNTR19_EL0",   0b11, 0b011, 0b1101, 0b1101, 0b001>;
def : RWSysReg<"AMEVCNTR110_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b010>;
def : RWSysReg<"AMEVCNTR111_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b011>;
def : RWSysReg<"AMEVCNTR112_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b100>;
def : RWSysReg<"AMEVCNTR113_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b101>;
def : RWSysReg<"AMEVCNTR114_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b110>;
def : RWSysReg<"AMEVCNTR115_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b111>;
def : RWSysReg<"AMEVTYPER10_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b000>;
def : RWSysReg<"AMEVTYPER11_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b001>;
def : RWSysReg<"AMEVTYPER12_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b010>;
def : RWSysReg<"AMEVTYPER13_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b011>;
def : RWSysReg<"AMEVTYPER14_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b100>;
def : RWSysReg<"AMEVTYPER15_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b101>;
def : RWSysReg<"AMEVTYPER16_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b110>;
def : RWSysReg<"AMEVTYPER17_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b111>;
def : RWSysReg<"AMEVTYPER18_EL0",  0b11, 0b011, 0b1101, 0b1111, 0b000>;
def : RWSysReg<"AMEVTYPER19_EL0",  0b11, 0b011, 0b1101, 0b1111, 0b001>;
def : RWSysReg<"AMEVTYPER110_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b010>;
def : RWSysReg<"AMEVTYPER111_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b011>;
def : RWSysReg<"AMEVTYPER112_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b100>;
def : RWSysReg<"AMEVTYPER113_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b101>;
def : RWSysReg<"AMEVTYPER114_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b110>;
def : RWSysReg<"AMEVTYPER115_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b111>;
} //FeatureAM

// v8.4a Trace Extension registers
//
// Please note that the 8.4 spec also defines these registers:
// TRCIDR1, ID_DFR0_EL1, ID_AA64DFR0_EL1, MDSCR_EL1, MDCR_EL2, and MDCR_EL3,
// but they are already defined above.
//
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureTRACEV8_4} }] in {
def : RWSysReg<"TRFCR_EL1",        0b11, 0b000, 0b0001, 0b0010, 0b001>;
def : RWSysReg<"TRFCR_EL2",        0b11, 0b100, 0b0001, 0b0010, 0b001>;
def : RWSysReg<"TRFCR_EL12",       0b11, 0b101, 0b0001, 0b0010, 0b001>;
} //FeatureTRACEV8_4

// v8.4a Timining insensitivity of data processing instructions
// DIT: Data Independent Timing instructions
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureDIT} }] in {
def : RWSysReg<"DIT",              0b11, 0b011, 0b0100, 0b0010, 0b101>;
} //FeatureDIT

// v8.4a Enhanced Support for Nested Virtualization
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureNV} }] in {
def : RWSysReg<"VNCR_EL2",         0b11, 0b100, 0b0010, 0b0010, 0b000>;
} //FeatureNV

// SVE control registers
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureSVE} }] in {
def : RWSysReg<"ZCR_EL1",          0b11, 0b000, 0b0001, 0b0010, 0b000>;
def : RWSysReg<"ZCR_EL2",          0b11, 0b100, 0b0001, 0b0010, 0b000>;
def : RWSysReg<"ZCR_EL3",          0b11, 0b110, 0b0001, 0b0010, 0b000>;
def : RWSysReg<"ZCR_EL12",         0b11, 0b101, 0b0001, 0b0010, 0b000>;
}

// V8.5a Spectre mitigation SSBS register
//                     Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureSSBS} }] in
def : RWSysReg<"SSBS", 0b11, 0b011, 0b0100, 0b0010, 0b110>;

// v8.5a Memory Tagging Extension
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureMTE} }] in {
def : RWSysReg<"TCO",              0b11, 0b011, 0b0100, 0b0010, 0b111>;
def : RWSysReg<"GCR_EL1",          0b11, 0b000, 0b0001, 0b0000, 0b110>;
def : RWSysReg<"RGSR_EL1",         0b11, 0b000, 0b0001, 0b0000, 0b101>;
def : RWSysReg<"TFSR_EL1",         0b11, 0b000, 0b0110, 0b0101, 0b000>;
def : RWSysReg<"TFSR_EL2",         0b11, 0b100, 0b0110, 0b0101, 0b000>;
def : RWSysReg<"TFSR_EL3",         0b11, 0b110, 0b0110, 0b0110, 0b000>;
def : RWSysReg<"TFSR_EL12",        0b11, 0b101, 0b0110, 0b0110, 0b000>;
def : RWSysReg<"TFSRE0_EL1",       0b11, 0b000, 0b0110, 0b0110, 0b001>;
} // HasMTE

// Cyclone specific system registers
//                                 Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::ProcCyclone} }] in
def : RWSysReg<"CPM_IOACC_CTL_EL3", 0b11, 0b111, 0b1111, 0b0010, 0b000>;
