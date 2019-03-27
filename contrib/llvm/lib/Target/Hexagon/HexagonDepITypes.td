//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Automatically generated file, please consult code owner before editing.
//===----------------------------------------------------------------------===//

class IType<bits<7> t> { bits<7> Value = t; }
def TypeALU32_2op : IType<0>;
def TypeALU32_3op : IType<1>;
def TypeALU32_ADDI : IType<2>;
def TypeALU64 : IType<3>;
def TypeCJ : IType<4>;
def TypeCOPROC_VX : IType<5>;
def TypeCR : IType<6>;
def TypeCVI_4SLOT_MPY : IType<7>;
def TypeCVI_GATHER : IType<8>;
def TypeCVI_GATHER_RST : IType<9>;
def TypeCVI_HIST : IType<10>;
def TypeCVI_SCATTER : IType<11>;
def TypeCVI_SCATTER_DV : IType<12>;
def TypeCVI_SCATTER_NEW_RST : IType<13>;
def TypeCVI_SCATTER_NEW_ST : IType<14>;
def TypeCVI_SCATTER_RST : IType<15>;
def TypeCVI_VA : IType<16>;
def TypeCVI_VA_DV : IType<17>;
def TypeCVI_VINLANESAT : IType<18>;
def TypeCVI_VM_LD : IType<19>;
def TypeCVI_VM_NEW_ST : IType<20>;
def TypeCVI_VM_ST : IType<21>;
def TypeCVI_VM_STU : IType<22>;
def TypeCVI_VM_TMP_LD : IType<23>;
def TypeCVI_VM_VP_LDU : IType<24>;
def TypeCVI_VP : IType<25>;
def TypeCVI_VP_VS : IType<26>;
def TypeCVI_VS : IType<27>;
def TypeCVI_VS_VX : IType<28>;
def TypeCVI_VX : IType<29>;
def TypeCVI_VX_DV : IType<30>;
def TypeCVI_VX_LATE : IType<31>;
def TypeCVI_ZW : IType<32>;
def TypeDUPLEX : IType<33>;
def TypeENDLOOP : IType<34>;
def TypeEXTENDER : IType<35>;
def TypeJ : IType<36>;
def TypeLD : IType<37>;
def TypeM : IType<38>;
def TypeMAPPING : IType<39>;
def TypeNCJ : IType<40>;
def TypePSEUDO : IType<41>;
def TypeST : IType<42>;
def TypeSUBINSN : IType<43>;
def TypeS_2op : IType<44>;
def TypeS_3op : IType<45>;
def TypeV2LDST : IType<48>;
def TypeV4LDST : IType<49>;
