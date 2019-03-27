//===-- SIRegisterInfo.td - SI Register defs ---------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Helpers
//===----------------------------------------------------------------------===//

class getSubRegs<int size> {
  list<SubRegIndex> ret2 = [sub0, sub1];
  list<SubRegIndex> ret3 = [sub0, sub1, sub2];
  list<SubRegIndex> ret4 = [sub0, sub1, sub2, sub3];
  list<SubRegIndex> ret8 = [sub0, sub1, sub2, sub3, sub4, sub5, sub6, sub7];
  list<SubRegIndex> ret16 = [sub0, sub1, sub2, sub3,
                             sub4, sub5, sub6, sub7,
                             sub8, sub9, sub10, sub11,
                             sub12, sub13, sub14, sub15];

  list<SubRegIndex> ret = !if(!eq(size, 2), ret2,
                              !if(!eq(size, 3), ret3,
                                  !if(!eq(size, 4), ret4,
                                      !if(!eq(size, 8), ret8, ret16))));
}

//===----------------------------------------------------------------------===//
//  Declarations that describe the SI registers
//===----------------------------------------------------------------------===//
class SIReg <string n, bits<16> regIdx = 0> : Register<n>,
  DwarfRegNum<[!cast<int>(HWEncoding)]> {
  let Namespace = "AMDGPU";

  // This is the not yet the complete register encoding. An additional
  // bit is set for VGPRs.
  let HWEncoding = regIdx;
}

// Special Registers
def VCC_LO : SIReg<"vcc_lo", 106>;
def VCC_HI : SIReg<"vcc_hi", 107>;

// Pseudo-registers: Used as placeholders during isel and immediately
// replaced, never seeing the verifier.
def PRIVATE_RSRC_REG : SIReg<"", 0>;
def FP_REG : SIReg<"", 0>;
def SP_REG : SIReg<"", 0>;
def SCRATCH_WAVE_OFFSET_REG : SIReg<"", 0>;

// VCC for 64-bit instructions
def VCC : RegisterWithSubRegs<"vcc", [VCC_LO, VCC_HI]>,
          DwarfRegAlias<VCC_LO> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = 106;
}

def EXEC_LO : SIReg<"exec_lo", 126>;
def EXEC_HI : SIReg<"exec_hi", 127>;

def EXEC : RegisterWithSubRegs<"EXEC", [EXEC_LO, EXEC_HI]>,
           DwarfRegAlias<EXEC_LO> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = 126;
}

def SCC : SIReg<"scc", 253>;
def M0 : SIReg <"m0", 124>;

def SRC_SHARED_BASE : SIReg<"src_shared_base", 235>;
def SRC_SHARED_LIMIT : SIReg<"src_shared_limit", 236>;
def SRC_PRIVATE_BASE : SIReg<"src_private_base", 237>;
def SRC_PRIVATE_LIMIT : SIReg<"src_private_limit", 238>;

def XNACK_MASK_LO : SIReg<"xnack_mask_lo", 104>;
def XNACK_MASK_HI : SIReg<"xnack_mask_hi", 105>;

def XNACK_MASK : RegisterWithSubRegs<"xnack_mask", [XNACK_MASK_LO, XNACK_MASK_HI]>,
                 DwarfRegAlias<XNACK_MASK_LO> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = 104;
}

// Trap handler registers
def TBA_LO : SIReg<"tba_lo", 108>;
def TBA_HI : SIReg<"tba_hi", 109>;

def TBA : RegisterWithSubRegs<"tba", [TBA_LO, TBA_HI]>,
          DwarfRegAlias<TBA_LO> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = 108;
}

def TMA_LO : SIReg<"tma_lo", 110>;
def TMA_HI : SIReg<"tma_hi", 111>;

def TMA : RegisterWithSubRegs<"tma", [TMA_LO, TMA_HI]>,
          DwarfRegAlias<TMA_LO> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = 110;
}

foreach Index = 0-15 in {
  def TTMP#Index#_vi   : SIReg<"ttmp"#Index, !add(112, Index)>;
  def TTMP#Index#_gfx9 : SIReg<"ttmp"#Index, !add(108, Index)>;
  def TTMP#Index       : SIReg<"", 0>;
}

multiclass FLAT_SCR_LOHI_m <string n, bits<16> ci_e, bits<16> vi_e> {
  def _ci : SIReg<n, ci_e>;
  def _vi : SIReg<n, vi_e>;
  def "" : SIReg<"", 0>;
}

class FlatReg <Register lo, Register hi, bits<16> encoding> :
    RegisterWithSubRegs<"flat_scratch", [lo, hi]>,
    DwarfRegAlias<lo> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = encoding;
}

defm FLAT_SCR_LO : FLAT_SCR_LOHI_m<"flat_scratch_lo", 104, 102>; // Offset in units of 256-bytes.
defm FLAT_SCR_HI : FLAT_SCR_LOHI_m<"flat_scratch_hi", 105, 103>; // Size is the per-thread scratch size, in bytes.

def FLAT_SCR_ci : FlatReg<FLAT_SCR_LO_ci, FLAT_SCR_HI_ci, 104>;
def FLAT_SCR_vi : FlatReg<FLAT_SCR_LO_vi, FLAT_SCR_HI_vi, 102>;
def FLAT_SCR : FlatReg<FLAT_SCR_LO, FLAT_SCR_HI, 0>;

// SGPR registers
foreach Index = 0-103 in {
  def SGPR#Index : SIReg <"SGPR"#Index, Index>;
}

// VGPR registers
foreach Index = 0-255 in {
  def VGPR#Index : SIReg <"VGPR"#Index, Index> {
    let HWEncoding{8} = 1;
  }
}

//===----------------------------------------------------------------------===//
//  Groupings using register classes and tuples
//===----------------------------------------------------------------------===//

def SCC_CLASS : RegisterClass<"AMDGPU", [i1], 1, (add SCC)> {
  let CopyCost = -1;
  let isAllocatable = 0;
}

def M0_CLASS : RegisterClass<"AMDGPU", [i32], 32, (add M0)> {
  let CopyCost = 1;
  let isAllocatable = 0;
}

// TODO: Do we need to set DwarfRegAlias on register tuples?

// SGPR 32-bit registers
def SGPR_32 : RegisterClass<"AMDGPU", [i32, f32, i16, f16, v2i16, v2f16], 32,
                            (add (sequence "SGPR%u", 0, 103))> {
  // Give all SGPR classes higher priority than VGPR classes, because
  // we want to spill SGPRs to VGPRs.
  let AllocationPriority = 7;
}

// SGPR 64-bit registers
def SGPR_64Regs : RegisterTuples<getSubRegs<2>.ret,
                             [(add (decimate SGPR_32, 2)),
                              (add (decimate (shl SGPR_32, 1), 2))]>;

// SGPR 128-bit registers
def SGPR_128Regs : RegisterTuples<getSubRegs<4>.ret,
                              [(add (decimate SGPR_32, 4)),
                               (add (decimate (shl SGPR_32, 1), 4)),
                               (add (decimate (shl SGPR_32, 2), 4)),
                               (add (decimate (shl SGPR_32, 3), 4))]>;

// SGPR 256-bit registers
def SGPR_256Regs : RegisterTuples<getSubRegs<8>.ret,
                              [(add (decimate SGPR_32, 4)),
                               (add (decimate (shl SGPR_32, 1), 4)),
                               (add (decimate (shl SGPR_32, 2), 4)),
                               (add (decimate (shl SGPR_32, 3), 4)),
                               (add (decimate (shl SGPR_32, 4), 4)),
                               (add (decimate (shl SGPR_32, 5), 4)),
                               (add (decimate (shl SGPR_32, 6), 4)),
                               (add (decimate (shl SGPR_32, 7), 4))]>;

// SGPR 512-bit registers
def SGPR_512Regs : RegisterTuples<getSubRegs<16>.ret,
                              [(add (decimate SGPR_32, 4)),
                               (add (decimate (shl SGPR_32, 1), 4)),
                               (add (decimate (shl SGPR_32, 2), 4)),
                               (add (decimate (shl SGPR_32, 3), 4)),
                               (add (decimate (shl SGPR_32, 4), 4)),
                               (add (decimate (shl SGPR_32, 5), 4)),
                               (add (decimate (shl SGPR_32, 6), 4)),
                               (add (decimate (shl SGPR_32, 7), 4)),
                               (add (decimate (shl SGPR_32, 8), 4)),
                               (add (decimate (shl SGPR_32, 9), 4)),
                               (add (decimate (shl SGPR_32, 10), 4)),
                               (add (decimate (shl SGPR_32, 11), 4)),
                               (add (decimate (shl SGPR_32, 12), 4)),
                               (add (decimate (shl SGPR_32, 13), 4)),
                               (add (decimate (shl SGPR_32, 14), 4)),
                               (add (decimate (shl SGPR_32, 15), 4))]>;

// Trap handler TMP 32-bit registers
def TTMP_32 : RegisterClass<"AMDGPU", [i32, f32, v2i16, v2f16], 32,
                            (add (sequence "TTMP%u", 0, 15))> {
  let isAllocatable = 0;
}

// Trap handler TMP 64-bit registers
def TTMP_64Regs : RegisterTuples<getSubRegs<2>.ret,
                             [(add (decimate TTMP_32, 2)),
                              (add (decimate (shl TTMP_32, 1), 2))]>;

// Trap handler TMP 128-bit registers
def TTMP_128Regs : RegisterTuples<getSubRegs<4>.ret,
                              [(add (decimate TTMP_32, 4)),
                               (add (decimate (shl TTMP_32, 1), 4)),
                               (add (decimate (shl TTMP_32, 2), 4)),
                               (add (decimate (shl TTMP_32, 3), 4))]>;

def TTMP_256Regs : RegisterTuples<getSubRegs<8>.ret,
                              [(add (decimate TTMP_32, 4)),
                               (add (decimate (shl TTMP_32, 1), 4)),
                               (add (decimate (shl TTMP_32, 2), 4)),
                               (add (decimate (shl TTMP_32, 3), 4)),
                               (add (decimate (shl TTMP_32, 4), 4)),
                               (add (decimate (shl TTMP_32, 5), 4)),
                               (add (decimate (shl TTMP_32, 6), 4)),
                               (add (decimate (shl TTMP_32, 7), 4))]>;

def TTMP_512Regs : RegisterTuples<getSubRegs<16>.ret,
                              [(add (decimate TTMP_32, 4)),
                               (add (decimate (shl TTMP_32, 1), 4)),
                               (add (decimate (shl TTMP_32, 2), 4)),
                               (add (decimate (shl TTMP_32, 3), 4)),
                               (add (decimate (shl TTMP_32, 4), 4)),
                               (add (decimate (shl TTMP_32, 5), 4)),
                               (add (decimate (shl TTMP_32, 6), 4)),
                               (add (decimate (shl TTMP_32, 7), 4)),
                               (add (decimate (shl TTMP_32, 8), 4)),
                               (add (decimate (shl TTMP_32, 9), 4)),
                               (add (decimate (shl TTMP_32, 10), 4)),
                               (add (decimate (shl TTMP_32, 11), 4)),
                               (add (decimate (shl TTMP_32, 12), 4)),
                               (add (decimate (shl TTMP_32, 13), 4)),
                               (add (decimate (shl TTMP_32, 14), 4)),
                               (add (decimate (shl TTMP_32, 15), 4))]>;

class TmpRegTuplesBase<int index, int size,
                       list<Register> subRegs,
                       list<SubRegIndex> indices = getSubRegs<size>.ret,
                       int index1 = !add(index, !add(size, -1)),
                       string name = "ttmp["#index#":"#index1#"]"> :
  RegisterWithSubRegs<name, subRegs> {
  let HWEncoding = subRegs[0].HWEncoding;
  let SubRegIndices = indices;
}

class TmpRegTuples<string tgt,
                   int size,
                   int index0,
                   int index1 = !add(index0, 1),
                   int index2 = !add(index0, !if(!eq(size, 2), 1, 2)),
                   int index3 = !add(index0, !if(!eq(size, 2), 1, 3)),
                   int index4 = !add(index0, !if(!eq(size, 8), 4, 1)),
                   int index5 = !add(index0, !if(!eq(size, 8), 5, 1)),
                   int index6 = !add(index0, !if(!eq(size, 8), 6, 1)),
                   int index7 = !add(index0, !if(!eq(size, 8), 7, 1)),
                   Register r0 = !cast<Register>("TTMP"#index0#tgt),
                   Register r1 = !cast<Register>("TTMP"#index1#tgt),
                   Register r2 = !cast<Register>("TTMP"#index2#tgt),
                   Register r3 = !cast<Register>("TTMP"#index3#tgt),
                   Register r4 = !cast<Register>("TTMP"#index4#tgt),
                   Register r5 = !cast<Register>("TTMP"#index5#tgt),
                   Register r6 = !cast<Register>("TTMP"#index6#tgt),
                   Register r7 = !cast<Register>("TTMP"#index7#tgt)> :
  TmpRegTuplesBase<index0, size,
                   !if(!eq(size, 2), [r0, r1],
                       !if(!eq(size, 4), [r0, r1, r2, r3],
                                         [r0, r1, r2, r3, r4, r5, r6, r7])),
                   getSubRegs<size>.ret>;

foreach Index = {0, 2, 4, 6, 8, 10, 12, 14} in {
  def TTMP#Index#_TTMP#!add(Index,1)#_vi   : TmpRegTuples<"_vi",   2, Index>;
  def TTMP#Index#_TTMP#!add(Index,1)#_gfx9 : TmpRegTuples<"_gfx9", 2, Index>;
}

foreach Index = {0, 4, 8, 12} in {
  def TTMP#Index#_TTMP#!add(Index,1)#
                 _TTMP#!add(Index,2)#
                 _TTMP#!add(Index,3)#_vi : TmpRegTuples<"_vi",   4, Index>;
  def TTMP#Index#_TTMP#!add(Index,1)#
                 _TTMP#!add(Index,2)#
                 _TTMP#!add(Index,3)#_gfx9 : TmpRegTuples<"_gfx9", 4, Index>;
}

foreach Index = {0, 4, 8} in {
  def TTMP#Index#_TTMP#!add(Index,1)#
                 _TTMP#!add(Index,2)#
                 _TTMP#!add(Index,3)#
                 _TTMP#!add(Index,4)#
                 _TTMP#!add(Index,5)#
                 _TTMP#!add(Index,6)#
                 _TTMP#!add(Index,7)#_vi : TmpRegTuples<"_vi",   8, Index>;
  def TTMP#Index#_TTMP#!add(Index,1)#
                 _TTMP#!add(Index,2)#
                 _TTMP#!add(Index,3)#
                 _TTMP#!add(Index,4)#
                 _TTMP#!add(Index,5)#
                 _TTMP#!add(Index,6)#
                 _TTMP#!add(Index,7)#_gfx9 : TmpRegTuples<"_gfx9", 8, Index>;
}

def TTMP0_TTMP1_TTMP2_TTMP3_TTMP4_TTMP5_TTMP6_TTMP7_TTMP8_TTMP9_TTMP10_TTMP11_TTMP12_TTMP13_TTMP14_TTMP15_vi :
  TmpRegTuplesBase<0, 16,
                   [TTMP0_vi, TTMP1_vi, TTMP2_vi, TTMP3_vi,
                    TTMP4_vi, TTMP5_vi, TTMP6_vi, TTMP7_vi,
                    TTMP8_vi, TTMP9_vi, TTMP10_vi, TTMP11_vi,
                    TTMP12_vi, TTMP13_vi, TTMP14_vi, TTMP15_vi]>;

def TTMP0_TTMP1_TTMP2_TTMP3_TTMP4_TTMP5_TTMP6_TTMP7_TTMP8_TTMP9_TTMP10_TTMP11_TTMP12_TTMP13_TTMP14_TTMP15_gfx9 :
  TmpRegTuplesBase<0, 16,
                   [TTMP0_gfx9, TTMP1_gfx9, TTMP2_gfx9, TTMP3_gfx9,
                    TTMP4_gfx9, TTMP5_gfx9, TTMP6_gfx9, TTMP7_gfx9,
                    TTMP8_gfx9, TTMP9_gfx9, TTMP10_gfx9, TTMP11_gfx9,
                    TTMP12_gfx9, TTMP13_gfx9, TTMP14_gfx9, TTMP15_gfx9]>;


// VGPR 32-bit registers
// i16/f16 only on VI+
def VGPR_32 : RegisterClass<"AMDGPU", [i32, f32, i16, f16, v2i16, v2f16], 32,
                            (add (sequence "VGPR%u", 0, 255))> {
  let AllocationPriority = 1;
  let Size = 32;
}

// VGPR 64-bit registers
def VGPR_64 : RegisterTuples<getSubRegs<2>.ret,
                             [(add (trunc VGPR_32, 255)),
                              (add (shl VGPR_32, 1))]>;

// VGPR 96-bit registers
def VGPR_96 : RegisterTuples<getSubRegs<3>.ret,
                             [(add (trunc VGPR_32, 254)),
                              (add (shl VGPR_32, 1)),
                              (add (shl VGPR_32, 2))]>;

// VGPR 128-bit registers
def VGPR_128 : RegisterTuples<getSubRegs<4>.ret,
                              [(add (trunc VGPR_32, 253)),
                               (add (shl VGPR_32, 1)),
                               (add (shl VGPR_32, 2)),
                               (add (shl VGPR_32, 3))]>;

// VGPR 256-bit registers
def VGPR_256 : RegisterTuples<getSubRegs<8>.ret,
                              [(add (trunc VGPR_32, 249)),
                               (add (shl VGPR_32, 1)),
                               (add (shl VGPR_32, 2)),
                               (add (shl VGPR_32, 3)),
                               (add (shl VGPR_32, 4)),
                               (add (shl VGPR_32, 5)),
                               (add (shl VGPR_32, 6)),
                               (add (shl VGPR_32, 7))]>;

// VGPR 512-bit registers
def VGPR_512 : RegisterTuples<getSubRegs<16>.ret,
                              [(add (trunc VGPR_32, 241)),
                               (add (shl VGPR_32, 1)),
                               (add (shl VGPR_32, 2)),
                               (add (shl VGPR_32, 3)),
                               (add (shl VGPR_32, 4)),
                               (add (shl VGPR_32, 5)),
                               (add (shl VGPR_32, 6)),
                               (add (shl VGPR_32, 7)),
                               (add (shl VGPR_32, 8)),
                               (add (shl VGPR_32, 9)),
                               (add (shl VGPR_32, 10)),
                               (add (shl VGPR_32, 11)),
                               (add (shl VGPR_32, 12)),
                               (add (shl VGPR_32, 13)),
                               (add (shl VGPR_32, 14)),
                               (add (shl VGPR_32, 15))]>;

//===----------------------------------------------------------------------===//
//  Register classes used as source and destination
//===----------------------------------------------------------------------===//

def Pseudo_SReg_32 : RegisterClass<"AMDGPU", [i32, f32, i16, f16, v2i16, v2f16], 32,
  (add FP_REG, SP_REG, SCRATCH_WAVE_OFFSET_REG)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

def Pseudo_SReg_128 : RegisterClass<"AMDGPU", [v4i32, v2i64, v2f64], 32,
  (add PRIVATE_RSRC_REG)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// Subset of SReg_32 without M0 for SMRD instructions and alike.
// See comments in SIInstructions.td for more info.
def SReg_32_XM0_XEXEC : RegisterClass<"AMDGPU", [i32, f32, i16, f16, v2i16, v2f16], 32,
  (add SGPR_32, VCC_LO, VCC_HI, FLAT_SCR_LO, FLAT_SCR_HI, XNACK_MASK_LO, XNACK_MASK_HI,
   TTMP_32, TMA_LO, TMA_HI, TBA_LO, TBA_HI, SRC_SHARED_BASE, SRC_SHARED_LIMIT,
   SRC_PRIVATE_BASE, SRC_PRIVATE_LIMIT)> {
  let AllocationPriority = 7;
}

def SReg_32_XEXEC_HI : RegisterClass<"AMDGPU", [i32, f32, i16, f16, v2i16, v2f16], 32,
  (add SReg_32_XM0_XEXEC, EXEC_LO, M0_CLASS)> {
  let AllocationPriority = 7;
}

def SReg_32_XM0 : RegisterClass<"AMDGPU", [i32, f32, i16, f16, v2i16, v2f16], 32,
  (add SReg_32_XM0_XEXEC, EXEC_LO, EXEC_HI)> {
  let AllocationPriority = 7;
}

// Register class for all scalar registers (SGPRs + Special Registers)
def SReg_32 : RegisterClass<"AMDGPU", [i32, f32, i16, f16, v2i16, v2f16], 32,
  (add SReg_32_XM0, M0_CLASS, EXEC_LO, EXEC_HI, SReg_32_XEXEC_HI)> {
  let AllocationPriority = 7;
}

def SGPR_64 : RegisterClass<"AMDGPU", [v2i32, i64, v2f32, f64, v4i16, v4f16], 32, (add SGPR_64Regs)> {
  let CopyCost = 1;
  let AllocationPriority = 8;
}

def TTMP_64 : RegisterClass<"AMDGPU", [v2i32, i64, f64, v4i16, v4f16], 32, (add TTMP_64Regs)> {
  let isAllocatable = 0;
}

def SReg_64_XEXEC : RegisterClass<"AMDGPU", [v2i32, i64, v2f32, f64, i1, v4i16, v4f16], 32,
  (add SGPR_64, VCC, FLAT_SCR, XNACK_MASK, TTMP_64, TBA, TMA)> {
  let CopyCost = 1;
  let AllocationPriority = 8;
}

def SReg_64 : RegisterClass<"AMDGPU", [v2i32, i64, v2f32, f64, i1, v4i16, v4f16], 32,
  (add SReg_64_XEXEC, EXEC)> {
  let CopyCost = 1;
  let AllocationPriority = 8;
}

// Requires 2 s_mov_b64 to copy
let CopyCost = 2 in {

def SGPR_128 : RegisterClass<"AMDGPU", [v4i32, v4f32, v2i64], 32, (add SGPR_128Regs)> {
  let AllocationPriority = 10;
}

def TTMP_128 : RegisterClass<"AMDGPU", [v4i32, v4f32, v2i64], 32, (add TTMP_128Regs)> {
  let isAllocatable = 0;
}

def SReg_128 : RegisterClass<"AMDGPU", [v4i32, v4f32, v2i64, v2f64], 32,
  (add SGPR_128, TTMP_128)> {
  let AllocationPriority = 10;
}

} // End CopyCost = 2

def SGPR_256 : RegisterClass<"AMDGPU", [v8i32, v8f32], 32, (add SGPR_256Regs)> {
  let AllocationPriority = 11;
}

def TTMP_256 : RegisterClass<"AMDGPU", [v8i32, v8f32], 32, (add TTMP_256Regs)> {
  let isAllocatable = 0;
}

def SReg_256 : RegisterClass<"AMDGPU", [v8i32, v8f32], 32,
  (add SGPR_256, TTMP_256)> {
  // Requires 4 s_mov_b64 to copy
  let CopyCost = 4;
  let AllocationPriority = 11;
}

def SGPR_512 : RegisterClass<"AMDGPU", [v16i32, v16f32], 32, (add SGPR_512Regs)> {
  let AllocationPriority = 12;
}

def TTMP_512 : RegisterClass<"AMDGPU", [v16i32, v16f32], 32, (add TTMP_512Regs)> {
  let isAllocatable = 0;
}

def SReg_512 : RegisterClass<"AMDGPU", [v16i32, v16f32], 32,
  (add SGPR_512, TTMP_512)> {
  // Requires 8 s_mov_b64 to copy
  let CopyCost = 8;
  let AllocationPriority = 12;
}

// Register class for all vector registers (VGPRs + Interploation Registers)
def VReg_64 : RegisterClass<"AMDGPU", [i64, f64, v2i32, v2f32, v4f16, v4i16], 32, (add VGPR_64)> {
  let Size = 64;

  // Requires 2 v_mov_b32 to copy
  let CopyCost = 2;
  let AllocationPriority = 2;
}

def VReg_96 : RegisterClass<"AMDGPU", [untyped], 32, (add VGPR_96)> {
  let Size = 96;

  // Requires 3 v_mov_b32 to copy
  let CopyCost = 3;
  let AllocationPriority = 3;
}

def VReg_128 : RegisterClass<"AMDGPU", [v4i32, v4f32, v2i64, v2f64], 32, (add VGPR_128)> {
  let Size = 128;

  // Requires 4 v_mov_b32 to copy
  let CopyCost = 4;
  let AllocationPriority = 4;
}

def VReg_256 : RegisterClass<"AMDGPU", [v8i32, v8f32], 32, (add VGPR_256)> {
  let Size = 256;
  let CopyCost = 8;
  let AllocationPriority = 5;
}

def VReg_512 : RegisterClass<"AMDGPU", [v16i32, v16f32], 32, (add VGPR_512)> {
  let Size = 512;
  let CopyCost = 16;
  let AllocationPriority = 6;
}

def VReg_1 : RegisterClass<"AMDGPU", [i1], 32, (add VGPR_32)> {
  let Size = 32;
}

def VS_32 : RegisterClass<"AMDGPU", [i32, f32, i16, f16, v2i16, v2f16], 32,
                          (add VGPR_32, SReg_32)> {
  let isAllocatable = 0;
}

def VS_64 : RegisterClass<"AMDGPU", [i64, f64], 32, (add VReg_64, SReg_64)> {
  let isAllocatable = 0;
}

//===----------------------------------------------------------------------===//
//  Register operands
//===----------------------------------------------------------------------===//

class RegImmMatcher<string name> : AsmOperandClass {
  let Name = name;
  let RenderMethod = "addRegOrImmOperands";
}

multiclass SIRegOperand <string rc, string MatchName, string opType> {
  let OperandNamespace = "AMDGPU" in {
    def _b16 : RegisterOperand<!cast<RegisterClass>(rc#"_32")> {
      let OperandType = opType#"_INT16";
      let ParserMatchClass = RegImmMatcher<MatchName#"B16">;
      let DecoderMethod = "decodeOperand_VSrc16";
    }

    def _f16 : RegisterOperand<!cast<RegisterClass>(rc#"_32")> {
      let OperandType = opType#"_FP16";
      let ParserMatchClass = RegImmMatcher<MatchName#"F16">;
      let DecoderMethod = "decodeOperand_VSrc16";
    }

    def _b32 : RegisterOperand<!cast<RegisterClass>(rc#"_32")> {
      let OperandType = opType#"_INT32";
      let ParserMatchClass = RegImmMatcher<MatchName#"B32">;
    }

    def _f32 : RegisterOperand<!cast<RegisterClass>(rc#"_32")> {
      let OperandType = opType#"_FP32";
      let ParserMatchClass = RegImmMatcher<MatchName#"F32">;
    }

    def _b64 : RegisterOperand<!cast<RegisterClass>(rc#"_64")> {
      let OperandType = opType#"_INT64";
      let ParserMatchClass = RegImmMatcher<MatchName#"B64">;
    }

    def _f64 : RegisterOperand<!cast<RegisterClass>(rc#"_64")> {
      let OperandType = opType#"_FP64";
      let ParserMatchClass = RegImmMatcher<MatchName#"F64">;
    }

    def _v2b16 : RegisterOperand<!cast<RegisterClass>(rc#"_32")> {
      let OperandType = opType#"_V2INT16";
      let ParserMatchClass = RegImmMatcher<MatchName#"V2B16">;
      let DecoderMethod = "decodeOperand_VSrcV216";
    }

    def _v2f16 : RegisterOperand<!cast<RegisterClass>(rc#"_32")> {
      let OperandType = opType#"_V2FP16";
      let ParserMatchClass = RegImmMatcher<MatchName#"V2F16">;
      let DecoderMethod = "decodeOperand_VSrcV216";
    }
  }
}

// FIXME: 64-bit sources can sometimes use 32-bit constants.
multiclass RegImmOperand <string rc, string MatchName>
  : SIRegOperand<rc, MatchName, "OPERAND_REG_IMM">;

multiclass RegInlineOperand <string rc, string MatchName>
  : SIRegOperand<rc, MatchName, "OPERAND_REG_INLINE_C">;

//===----------------------------------------------------------------------===//
//  SSrc_* Operands with an SGPR or a 32-bit immediate
//===----------------------------------------------------------------------===//

defm SSrc : RegImmOperand<"SReg", "SSrc">;

//===----------------------------------------------------------------------===//
//  SCSrc_* Operands with an SGPR or a inline constant
//===----------------------------------------------------------------------===//

defm SCSrc : RegInlineOperand<"SReg", "SCSrc"> ;

def SCSrc_i1 : RegisterOperand<SReg_64_XEXEC>;

//===----------------------------------------------------------------------===//
//  VSrc_* Operands with an SGPR, VGPR or a 32-bit immediate
//===----------------------------------------------------------------------===//

defm VSrc : RegImmOperand<"VS", "VSrc">;

def VSrc_128 : RegisterOperand<VReg_128> {
  let DecoderMethod = "DecodeVS_128RegisterClass";
}

//===----------------------------------------------------------------------===//
//  VSrc_* Operands with an VGPR
//===----------------------------------------------------------------------===//

// This is for operands with the enum(9), VSrc encoding restriction,
// but only allows VGPRs.
def VRegSrc_32 : RegisterOperand<VGPR_32> {
  //let ParserMatchClass = RegImmMatcher<"VRegSrc32">;
  let DecoderMethod = "DecodeVS_32RegisterClass";
}

//===----------------------------------------------------------------------===//
//  VCSrc_* Operands with an SGPR, VGPR or an inline constant
//===----------------------------------------------------------------------===//

defm VCSrc : RegInlineOperand<"VS", "VCSrc">;
