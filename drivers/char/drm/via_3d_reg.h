/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef VIA_3D_REG_H
#define VIA_3D_REG_H
#define HC_REG_BASE             0x0400

#define HC_REG_TRANS_SPACE      0x0040

#define HC_ParaN_MASK           0xffffffff
#define HC_Para_MASK            0x00ffffff
#define HC_SubA_MASK            0xff000000
#define HC_SubA_SHIFT           24
/* Transmission Setting
 */
#define HC_REG_TRANS_SET        0x003c
#define HC_ParaSubType_MASK     0xff000000
#define HC_ParaType_MASK        0x00ff0000
#define HC_ParaOS_MASK          0x0000ff00
#define HC_ParaAdr_MASK         0x000000ff
#define HC_ParaSubType_SHIFT    24
#define HC_ParaType_SHIFT       16
#define HC_ParaOS_SHIFT         8
#define HC_ParaAdr_SHIFT        0

#define HC_ParaType_CmdVdata    0x0000
#define HC_ParaType_NotTex      0x0001
#define HC_ParaType_Tex         0x0002
#define HC_ParaType_Palette     0x0003
#define HC_ParaType_PreCR       0x0010
#define HC_ParaType_Auto        0x00fe

/* Transmission Space
 */
#define HC_REG_Hpara0           0x0040
#define HC_REG_HpataAF          0x02fc

/* Read
 */
#define HC_REG_HREngSt          0x0000
#define HC_REG_HRFIFOempty      0x0004
#define HC_REG_HRFIFOfull       0x0008
#define HC_REG_HRErr            0x000c
#define HC_REG_FIFOstatus       0x0010
/* HC_REG_HREngSt          0x0000
 */
#define HC_HDASZC_MASK          0x00010000
#define HC_HSGEMI_MASK          0x0000f000
#define HC_HLGEMISt_MASK        0x00000f00
#define HC_HCRSt_MASK           0x00000080
#define HC_HSE0St_MASK          0x00000040
#define HC_HSE1St_MASK          0x00000020
#define HC_HPESt_MASK           0x00000010
#define HC_HXESt_MASK           0x00000008
#define HC_HBESt_MASK           0x00000004
#define HC_HE2St_MASK           0x00000002
#define HC_HE3St_MASK           0x00000001
/* HC_REG_HRFIFOempty      0x0004
 */
#define HC_HRZDempty_MASK       0x00000010
#define HC_HRTXAempty_MASK      0x00000008
#define HC_HRTXDempty_MASK      0x00000004
#define HC_HWZDempty_MASK       0x00000002
#define HC_HWCDempty_MASK       0x00000001
/* HC_REG_HRFIFOfull       0x0008
 */
#define HC_HRZDfull_MASK        0x00000010
#define HC_HRTXAfull_MASK       0x00000008
#define HC_HRTXDfull_MASK       0x00000004
#define HC_HWZDfull_MASK        0x00000002
#define HC_HWCDfull_MASK        0x00000001
/* HC_REG_HRErr            0x000c
 */
#define HC_HAGPCMErr_MASK       0x80000000
#define HC_HAGPCMErrC_MASK      0x70000000
/* HC_REG_FIFOstatus       0x0010
 */
#define HC_HRFIFOATall_MASK     0x80000000
#define HC_HRFIFOATbusy_MASK    0x40000000
#define HC_HRATFGMDo_MASK       0x00000100
#define HC_HRATFGMDi_MASK       0x00000080
#define HC_HRATFRZD_MASK        0x00000040
#define HC_HRATFRTXA_MASK       0x00000020
#define HC_HRATFRTXD_MASK       0x00000010
#define HC_HRATFWZD_MASK        0x00000008
#define HC_HRATFWCD_MASK        0x00000004
#define HC_HRATTXTAG_MASK       0x00000002
#define HC_HRATTXCH_MASK        0x00000001

/* AGP Command Setting
 */
#define HC_SubA_HAGPBstL        0x0060
#define HC_SubA_HAGPBendL       0x0061
#define HC_SubA_HAGPCMNT        0x0062
#define HC_SubA_HAGPBpL         0x0063
#define HC_SubA_HAGPBpH         0x0064
/* HC_SubA_HAGPCMNT        0x0062
 */
#define HC_HAGPCMNT_MASK        0x00800000
#define HC_HCmdErrClr_MASK      0x00400000
#define HC_HAGPBendH_MASK       0x0000ff00
#define HC_HAGPBstH_MASK        0x000000ff
#define HC_HAGPBendH_SHIFT      8
#define HC_HAGPBstH_SHIFT       0
/* HC_SubA_HAGPBpL         0x0063
 */
#define HC_HAGPBpL_MASK         0x00fffffc
#define HC_HAGPBpID_MASK        0x00000003
#define HC_HAGPBpID_PAUSE       0x00000000
#define HC_HAGPBpID_JUMP        0x00000001
#define HC_HAGPBpID_STOP        0x00000002
/* HC_SubA_HAGPBpH         0x0064
 */
#define HC_HAGPBpH_MASK         0x00ffffff

/* Miscellaneous Settings
 */
#define HC_SubA_HClipTB         0x0070
#define HC_SubA_HClipLR         0x0071
#define HC_SubA_HFPClipTL       0x0072
#define HC_SubA_HFPClipBL       0x0073
#define HC_SubA_HFPClipLL       0x0074
#define HC_SubA_HFPClipRL       0x0075
#define HC_SubA_HFPClipTBH      0x0076
#define HC_SubA_HFPClipLRH      0x0077
#define HC_SubA_HLP             0x0078
#define HC_SubA_HLPRF           0x0079
#define HC_SubA_HSolidCL        0x007a
#define HC_SubA_HPixGC          0x007b
#define HC_SubA_HSPXYOS         0x007c
#define HC_SubA_HVertexCNT      0x007d

#define HC_HClipT_MASK          0x00fff000
#define HC_HClipT_SHIFT         12
#define HC_HClipB_MASK          0x00000fff
#define HC_HClipB_SHIFT         0
#define HC_HClipL_MASK          0x00fff000
#define HC_HClipL_SHIFT         12
#define HC_HClipR_MASK          0x00000fff
#define HC_HClipR_SHIFT         0
#define HC_HFPClipBH_MASK       0x0000ff00
#define HC_HFPClipBH_SHIFT      8
#define HC_HFPClipTH_MASK       0x000000ff
#define HC_HFPClipTH_SHIFT      0
#define HC_HFPClipRH_MASK       0x0000ff00
#define HC_HFPClipRH_SHIFT      8
#define HC_HFPClipLH_MASK       0x000000ff
#define HC_HFPClipLH_SHIFT      0
#define HC_HSolidCH_MASK        0x000000ff
#define HC_HPixGC_MASK          0x00800000
#define HC_HSPXOS_MASK          0x00fff000
#define HC_HSPXOS_SHIFT         12
#define HC_HSPYOS_MASK          0x00000fff

/* Command
 * Command A
 */
#define HC_HCmdHeader_MASK      0xfe000000	/*0xffe00000 */
#define HC_HE3Fire_MASK         0x00100000
#define HC_HPMType_MASK         0x000f0000
#define HC_HEFlag_MASK          0x0000e000
#define HC_HShading_MASK        0x00001c00
#define HC_HPMValidN_MASK       0x00000200
#define HC_HPLEND_MASK          0x00000100
#define HC_HVCycle_MASK         0x000000ff
#define HC_HVCycle_Style_MASK   0x000000c0
#define HC_HVCycle_ChgA_MASK    0x00000030
#define HC_HVCycle_ChgB_MASK    0x0000000c
#define HC_HVCycle_ChgC_MASK    0x00000003
#define HC_HPMType_Point        0x00000000
#define HC_HPMType_Line         0x00010000
#define HC_HPMType_Tri          0x00020000
#define HC_HPMType_TriWF        0x00040000
#define HC_HEFlag_NoAA          0x00000000
#define HC_HEFlag_ab            0x00008000
#define HC_HEFlag_bc            0x00004000
#define HC_HEFlag_ca            0x00002000
#define HC_HShading_Solid       0x00000000
#define HC_HShading_FlatA       0x00000400
#define HC_HShading_FlatB       0x00000800
#define HC_HShading_FlatC       0x00000c00
#define HC_HShading_Gouraud     0x00001000
#define HC_HVCycle_Full         0x00000000
#define HC_HVCycle_AFP          0x00000040
#define HC_HVCycle_One          0x000000c0
#define HC_HVCycle_NewA         0x00000000
#define HC_HVCycle_AA           0x00000010
#define HC_HVCycle_AB           0x00000020
#define HC_HVCycle_AC           0x00000030
#define HC_HVCycle_NewB         0x00000000
#define HC_HVCycle_BA           0x00000004
#define HC_HVCycle_BB           0x00000008
#define HC_HVCycle_BC           0x0000000c
#define HC_HVCycle_NewC         0x00000000
#define HC_HVCycle_CA           0x00000001
#define HC_HVCycle_CB           0x00000002
#define HC_HVCycle_CC           0x00000003

/* Command B
 */
#define HC_HLPrst_MASK          0x00010000
#define HC_HLLastP_MASK         0x00008000
#define HC_HVPMSK_MASK          0x00007f80
#define HC_HBFace_MASK          0x00000040
#define HC_H2nd1VT_MASK         0x0000003f
#define HC_HVPMSK_X             0x00004000
#define HC_HVPMSK_Y             0x00002000
#define HC_HVPMSK_Z             0x00001000
#define HC_HVPMSK_W             0x00000800
#define HC_HVPMSK_Cd            0x00000400
#define HC_HVPMSK_Cs            0x00000200
#define HC_HVPMSK_S             0x00000100
#define HC_HVPMSK_T             0x00000080

/* Enable Setting
 */
#define HC_SubA_HEnable         0x0000
#define HC_HenTXEnvMap_MASK     0x00200000
#define HC_HenVertexCNT_MASK    0x00100000
#define HC_HenCPUDAZ_MASK       0x00080000
#define HC_HenDASZWC_MASK       0x00040000
#define HC_HenFBCull_MASK       0x00020000
#define HC_HenCW_MASK           0x00010000
#define HC_HenAA_MASK           0x00008000
#define HC_HenST_MASK           0x00004000
#define HC_HenZT_MASK           0x00002000
#define HC_HenZW_MASK           0x00001000
#define HC_HenAT_MASK           0x00000800
#define HC_HenAW_MASK           0x00000400
#define HC_HenSP_MASK           0x00000200
#define HC_HenLP_MASK           0x00000100
#define HC_HenTXCH_MASK         0x00000080
#define HC_HenTXMP_MASK         0x00000040
#define HC_HenTXPP_MASK         0x00000020
#define HC_HenTXTR_MASK         0x00000010
#define HC_HenCS_MASK           0x00000008
#define HC_HenFOG_MASK          0x00000004
#define HC_HenABL_MASK          0x00000002
#define HC_HenDT_MASK           0x00000001

/* Z Setting
 */
#define HC_SubA_HZWBBasL        0x0010
#define HC_SubA_HZWBBasH        0x0011
#define HC_SubA_HZWBType        0x0012
#define HC_SubA_HZBiasL         0x0013
#define HC_SubA_HZWBend         0x0014
#define HC_SubA_HZWTMD          0x0015
#define HC_SubA_HZWCDL          0x0016
#define HC_SubA_HZWCTAGnum      0x0017
#define HC_SubA_HZCYNum         0x0018
#define HC_SubA_HZWCFire        0x0019
/* HC_SubA_HZWBType
 */
#define HC_HZWBType_MASK        0x00800000
#define HC_HZBiasedWB_MASK      0x00400000
#define HC_HZONEasFF_MASK       0x00200000
#define HC_HZOONEasFF_MASK      0x00100000
#define HC_HZWBFM_MASK          0x00030000
#define HC_HZWBLoc_MASK         0x0000c000
#define HC_HZWBPit_MASK         0x00003fff
#define HC_HZWBFM_16            0x00000000
#define HC_HZWBFM_32            0x00020000
#define HC_HZWBFM_24            0x00030000
#define HC_HZWBLoc_Local        0x00000000
#define HC_HZWBLoc_SyS          0x00004000
/* HC_SubA_HZWBend
 */
#define HC_HZWBend_MASK         0x00ffe000
#define HC_HZBiasH_MASK         0x000000ff
#define HC_HZWBend_SHIFT        10
/* HC_SubA_HZWTMD
 */
#define HC_HZWTMD_MASK          0x00070000
#define HC_HEBEBias_MASK        0x00007f00
#define HC_HZNF_MASK            0x000000ff
#define HC_HZWTMD_NeverPass     0x00000000
#define HC_HZWTMD_LT            0x00010000
#define HC_HZWTMD_EQ            0x00020000
#define HC_HZWTMD_LE            0x00030000
#define HC_HZWTMD_GT            0x00040000
#define HC_HZWTMD_NE            0x00050000
#define HC_HZWTMD_GE            0x00060000
#define HC_HZWTMD_AllPass       0x00070000
#define HC_HEBEBias_SHIFT       8
/* HC_SubA_HZWCDL          0x0016
 */
#define HC_HZWCDL_MASK          0x00ffffff
/* HC_SubA_HZWCTAGnum      0x0017
 */
#define HC_HZWCTAGnum_MASK      0x00ff0000
#define HC_HZWCTAGnum_SHIFT     16
#define HC_HZWCDH_MASK          0x000000ff
#define HC_HZWCDH_SHIFT         0
/* HC_SubA_HZCYNum         0x0018
 */
#define HC_HZCYNum_MASK         0x00030000
#define HC_HZCYNum_SHIFT        16
#define HC_HZWCQWnum_MASK       0x00003fff
#define HC_HZWCQWnum_SHIFT      0
/* HC_SubA_HZWCFire        0x0019
 */
#define HC_ZWCFire_MASK         0x00010000
#define HC_HZWCQWnumLast_MASK   0x00003fff
#define HC_HZWCQWnumLast_SHIFT  0

/* Stencil Setting
 */
#define HC_SubA_HSTREF          0x0023
#define HC_SubA_HSTMD           0x0024
/* HC_SubA_HSBFM
 */
#define HC_HSBFM_MASK           0x00030000
#define HC_HSBLoc_MASK          0x0000c000
#define HC_HSBPit_MASK          0x00003fff
/* HC_SubA_HSTREF
 */
#define HC_HSTREF_MASK          0x00ff0000
#define HC_HSTOPMSK_MASK        0x0000ff00
#define HC_HSTBMSK_MASK         0x000000ff
#define HC_HSTREF_SHIFT         16
#define HC_HSTOPMSK_SHIFT       8
/* HC_SubA_HSTMD
 */
#define HC_HSTMD_MASK           0x00070000
#define HC_HSTOPSF_MASK         0x000001c0
#define HC_HSTOPSPZF_MASK       0x00000038
#define HC_HSTOPSPZP_MASK       0x00000007
#define HC_HSTMD_NeverPass      0x00000000
#define HC_HSTMD_LT             0x00010000
#define HC_HSTMD_EQ             0x00020000
#define HC_HSTMD_LE             0x00030000
#define HC_HSTMD_GT             0x00040000
#define HC_HSTMD_NE             0x00050000
#define HC_HSTMD_GE             0x00060000
#define HC_HSTMD_AllPass        0x00070000
#define HC_HSTOPSF_KEEP         0x00000000
#define HC_HSTOPSF_ZERO         0x00000040
#define HC_HSTOPSF_REPLACE      0x00000080
#define HC_HSTOPSF_INCRSAT      0x000000c0
#define HC_HSTOPSF_DECRSAT      0x00000100
#define HC_HSTOPSF_INVERT       0x00000140
#define HC_HSTOPSF_INCR         0x00000180
#define HC_HSTOPSF_DECR         0x000001c0
#define HC_HSTOPSPZF_KEEP       0x00000000
#define HC_HSTOPSPZF_ZERO       0x00000008
#define HC_HSTOPSPZF_REPLACE    0x00000010
#define HC_HSTOPSPZF_INCRSAT    0x00000018
#define HC_HSTOPSPZF_DECRSAT    0x00000020
#define HC_HSTOPSPZF_INVERT     0x00000028
#define HC_HSTOPSPZF_INCR       0x00000030
#define HC_HSTOPSPZF_DECR       0x00000038
#define HC_HSTOPSPZP_KEEP       0x00000000
#define HC_HSTOPSPZP_ZERO       0x00000001
#define HC_HSTOPSPZP_REPLACE    0x00000002
#define HC_HSTOPSPZP_INCRSAT    0x00000003
#define HC_HSTOPSPZP_DECRSAT    0x00000004
#define HC_HSTOPSPZP_INVERT     0x00000005
#define HC_HSTOPSPZP_INCR       0x00000006
#define HC_HSTOPSPZP_DECR       0x00000007

/* Alpha Setting
 */
#define HC_SubA_HABBasL         0x0030
#define HC_SubA_HABBasH         0x0031
#define HC_SubA_HABFM           0x0032
#define HC_SubA_HATMD           0x0033
#define HC_SubA_HABLCsat        0x0034
#define HC_SubA_HABLCop         0x0035
#define HC_SubA_HABLAsat        0x0036
#define HC_SubA_HABLAop         0x0037
#define HC_SubA_HABLRCa         0x0038
#define HC_SubA_HABLRFCa        0x0039
#define HC_SubA_HABLRCbias      0x003a
#define HC_SubA_HABLRCb         0x003b
#define HC_SubA_HABLRFCb        0x003c
#define HC_SubA_HABLRAa         0x003d
#define HC_SubA_HABLRAb         0x003e
/* HC_SubA_HABFM
 */
#define HC_HABFM_MASK           0x00030000
#define HC_HABLoc_MASK          0x0000c000
#define HC_HABPit_MASK          0x000007ff
/* HC_SubA_HATMD
 */
#define HC_HATMD_MASK           0x00000700
#define HC_HATREF_MASK          0x000000ff
#define HC_HATMD_NeverPass      0x00000000
#define HC_HATMD_LT             0x00000100
#define HC_HATMD_EQ             0x00000200
#define HC_HATMD_LE             0x00000300
#define HC_HATMD_GT             0x00000400
#define HC_HATMD_NE             0x00000500
#define HC_HATMD_GE             0x00000600
#define HC_HATMD_AllPass        0x00000700
/* HC_SubA_HABLCsat
 */
#define HC_HABLCsat_MASK        0x00010000
#define HC_HABLCa_MASK          0x0000fc00
#define HC_HABLCa_C_MASK        0x0000c000
#define HC_HABLCa_OPC_MASK      0x00003c00
#define HC_HABLFCa_MASK         0x000003f0
#define HC_HABLFCa_C_MASK       0x00000300
#define HC_HABLFCa_OPC_MASK     0x000000f0
#define HC_HABLCbias_MASK       0x0000000f
#define HC_HABLCbias_C_MASK     0x00000008
#define HC_HABLCbias_OPC_MASK   0x00000007
/*-- Define the input color.
 */
#define HC_XC_Csrc              0x00000000
#define HC_XC_Cdst              0x00000001
#define HC_XC_Asrc              0x00000002
#define HC_XC_Adst              0x00000003
#define HC_XC_Fog               0x00000004
#define HC_XC_HABLRC            0x00000005
#define HC_XC_minSrcDst         0x00000006
#define HC_XC_maxSrcDst         0x00000007
#define HC_XC_mimAsrcInvAdst    0x00000008
#define HC_XC_OPC               0x00000000
#define HC_XC_InvOPC            0x00000010
#define HC_XC_OPCp5             0x00000020
/*-- Define the input Alpha
 */
#define HC_XA_OPA               0x00000000
#define HC_XA_InvOPA            0x00000010
#define HC_XA_OPAp5             0x00000020
#define HC_XA_0                 0x00000000
#define HC_XA_Asrc              0x00000001
#define HC_XA_Adst              0x00000002
#define HC_XA_Fog               0x00000003
#define HC_XA_minAsrcFog        0x00000004
#define HC_XA_minAsrcAdst       0x00000005
#define HC_XA_maxAsrcFog        0x00000006
#define HC_XA_maxAsrcAdst       0x00000007
#define HC_XA_HABLRA            0x00000008
#define HC_XA_minAsrcInvAdst    0x00000008
#define HC_XA_HABLFRA           0x00000009
/*--
 */
#define HC_HABLCa_OPC           (HC_XC_OPC << 10)
#define HC_HABLCa_InvOPC        (HC_XC_InvOPC << 10)
#define HC_HABLCa_OPCp5         (HC_XC_OPCp5 << 10)
#define HC_HABLCa_Csrc          (HC_XC_Csrc << 10)
#define HC_HABLCa_Cdst          (HC_XC_Cdst << 10)
#define HC_HABLCa_Asrc          (HC_XC_Asrc << 10)
#define HC_HABLCa_Adst          (HC_XC_Adst << 10)
#define HC_HABLCa_Fog           (HC_XC_Fog << 10)
#define HC_HABLCa_HABLRCa       (HC_XC_HABLRC << 10)
#define HC_HABLCa_minSrcDst     (HC_XC_minSrcDst << 10)
#define HC_HABLCa_maxSrcDst     (HC_XC_maxSrcDst << 10)
#define HC_HABLFCa_OPC              (HC_XC_OPC << 4)
#define HC_HABLFCa_InvOPC           (HC_XC_InvOPC << 4)
#define HC_HABLFCa_OPCp5            (HC_XC_OPCp5 << 4)
#define HC_HABLFCa_Csrc             (HC_XC_Csrc << 4)
#define HC_HABLFCa_Cdst             (HC_XC_Cdst << 4)
#define HC_HABLFCa_Asrc             (HC_XC_Asrc << 4)
#define HC_HABLFCa_Adst             (HC_XC_Adst << 4)
#define HC_HABLFCa_Fog              (HC_XC_Fog << 4)
#define HC_HABLFCa_HABLRCa          (HC_XC_HABLRC << 4)
#define HC_HABLFCa_minSrcDst        (HC_XC_minSrcDst << 4)
#define HC_HABLFCa_maxSrcDst        (HC_XC_maxSrcDst << 4)
#define HC_HABLFCa_mimAsrcInvAdst   (HC_XC_mimAsrcInvAdst << 4)
#define HC_HABLCbias_HABLRCbias 0x00000000
#define HC_HABLCbias_Asrc       0x00000001
#define HC_HABLCbias_Adst       0x00000002
#define HC_HABLCbias_Fog        0x00000003
#define HC_HABLCbias_Cin        0x00000004
/* HC_SubA_HABLCop         0x0035
 */
#define HC_HABLdot_MASK         0x00010000
#define HC_HABLCop_MASK         0x00004000
#define HC_HABLCb_MASK          0x00003f00
#define HC_HABLCb_C_MASK        0x00003000
#define HC_HABLCb_OPC_MASK      0x00000f00
#define HC_HABLFCb_MASK         0x000000fc
#define HC_HABLFCb_C_MASK       0x000000c0
#define HC_HABLFCb_OPC_MASK     0x0000003c
#define HC_HABLCshift_MASK      0x00000003
#define HC_HABLCb_OPC           (HC_XC_OPC << 8)
#define HC_HABLCb_InvOPC        (HC_XC_InvOPC << 8)
#define HC_HABLCb_OPCp5         (HC_XC_OPCp5 << 8)
#define HC_HABLCb_Csrc          (HC_XC_Csrc << 8)
#define HC_HABLCb_Cdst          (HC_XC_Cdst << 8)
#define HC_HABLCb_Asrc          (HC_XC_Asrc << 8)
#define HC_HABLCb_Adst          (HC_XC_Adst << 8)
#define HC_HABLCb_Fog           (HC_XC_Fog << 8)
#define HC_HABLCb_HABLRCa       (HC_XC_HABLRC << 8)
#define HC_HABLCb_minSrcDst     (HC_XC_minSrcDst << 8)
#define HC_HABLCb_maxSrcDst     (HC_XC_maxSrcDst << 8)
#define HC_HABLFCb_OPC              (HC_XC_OPC << 2)
#define HC_HABLFCb_InvOPC           (HC_XC_InvOPC << 2)
#define HC_HABLFCb_OPCp5            (HC_XC_OPCp5 << 2)
#define HC_HABLFCb_Csrc             (HC_XC_Csrc << 2)
#define HC_HABLFCb_Cdst             (HC_XC_Cdst << 2)
#define HC_HABLFCb_Asrc             (HC_XC_Asrc << 2)
#define HC_HABLFCb_Adst             (HC_XC_Adst << 2)
#define HC_HABLFCb_Fog              (HC_XC_Fog << 2)
#define HC_HABLFCb_HABLRCb          (HC_XC_HABLRC << 2)
#define HC_HABLFCb_minSrcDst        (HC_XC_minSrcDst << 2)
#define HC_HABLFCb_maxSrcDst        (HC_XC_maxSrcDst << 2)
#define HC_HABLFCb_mimAsrcInvAdst   (HC_XC_mimAsrcInvAdst << 2)
/* HC_SubA_HABLAsat        0x0036
 */
#define HC_HABLAsat_MASK        0x00010000
#define HC_HABLAa_MASK          0x0000fc00
#define HC_HABLAa_A_MASK        0x0000c000
#define HC_HABLAa_OPA_MASK      0x00003c00
#define HC_HABLFAa_MASK         0x000003f0
#define HC_HABLFAa_A_MASK       0x00000300
#define HC_HABLFAa_OPA_MASK     0x000000f0
#define HC_HABLAbias_MASK       0x0000000f
#define HC_HABLAbias_A_MASK     0x00000008
#define HC_HABLAbias_OPA_MASK   0x00000007
#define HC_HABLAa_OPA           (HC_XA_OPA << 10)
#define HC_HABLAa_InvOPA        (HC_XA_InvOPA << 10)
#define HC_HABLAa_OPAp5         (HC_XA_OPAp5 << 10)
#define HC_HABLAa_0             (HC_XA_0 << 10)
#define HC_HABLAa_Asrc          (HC_XA_Asrc << 10)
#define HC_HABLAa_Adst          (HC_XA_Adst << 10)
#define HC_HABLAa_Fog           (HC_XA_Fog << 10)
#define HC_HABLAa_minAsrcFog    (HC_XA_minAsrcFog << 10)
#define HC_HABLAa_minAsrcAdst   (HC_XA_minAsrcAdst << 10)
#define HC_HABLAa_maxAsrcFog    (HC_XA_maxAsrcFog << 10)
#define HC_HABLAa_maxAsrcAdst   (HC_XA_maxAsrcAdst << 10)
#define HC_HABLAa_HABLRA        (HC_XA_HABLRA << 10)
#define HC_HABLFAa_OPA          (HC_XA_OPA << 4)
#define HC_HABLFAa_InvOPA       (HC_XA_InvOPA << 4)
#define HC_HABLFAa_OPAp5        (HC_XA_OPAp5 << 4)
#define HC_HABLFAa_0            (HC_XA_0 << 4)
#define HC_HABLFAa_Asrc         (HC_XA_Asrc << 4)
#define HC_HABLFAa_Adst         (HC_XA_Adst << 4)
#define HC_HABLFAa_Fog          (HC_XA_Fog << 4)
#define HC_HABLFAa_minAsrcFog   (HC_XA_minAsrcFog << 4)
#define HC_HABLFAa_minAsrcAdst  (HC_XA_minAsrcAdst << 4)
#define HC_HABLFAa_maxAsrcFog   (HC_XA_maxAsrcFog << 4)
#define HC_HABLFAa_maxAsrcAdst  (HC_XA_maxAsrcAdst << 4)
#define HC_HABLFAa_minAsrcInvAdst   (HC_XA_minAsrcInvAdst << 4)
#define HC_HABLFAa_HABLFRA          (HC_XA_HABLFRA << 4)
#define HC_HABLAbias_HABLRAbias 0x00000000
#define HC_HABLAbias_Asrc       0x00000001
#define HC_HABLAbias_Adst       0x00000002
#define HC_HABLAbias_Fog        0x00000003
#define HC_HABLAbias_Aaa        0x00000004
/* HC_SubA_HABLAop         0x0037
 */
#define HC_HABLAop_MASK         0x00004000
#define HC_HABLAb_MASK          0x00003f00
#define HC_HABLAb_OPA_MASK      0x00000f00
#define HC_HABLFAb_MASK         0x000000fc
#define HC_HABLFAb_OPA_MASK     0x0000003c
#define HC_HABLAshift_MASK      0x00000003
#define HC_HABLAb_OPA           (HC_XA_OPA << 8)
#define HC_HABLAb_InvOPA        (HC_XA_InvOPA << 8)
#define HC_HABLAb_OPAp5         (HC_XA_OPAp5 << 8)
#define HC_HABLAb_0             (HC_XA_0 << 8)
#define HC_HABLAb_Asrc          (HC_XA_Asrc << 8)
#define HC_HABLAb_Adst          (HC_XA_Adst << 8)
#define HC_HABLAb_Fog           (HC_XA_Fog << 8)
#define HC_HABLAb_minAsrcFog    (HC_XA_minAsrcFog << 8)
#define HC_HABLAb_minAsrcAdst   (HC_XA_minAsrcAdst << 8)
#define HC_HABLAb_maxAsrcFog    (HC_XA_maxAsrcFog << 8)
#define HC_HABLAb_maxAsrcAdst   (HC_XA_maxAsrcAdst << 8)
#define HC_HABLAb_HABLRA        (HC_XA_HABLRA << 8)
#define HC_HABLFAb_OPA          (HC_XA_OPA << 2)
#define HC_HABLFAb_InvOPA       (HC_XA_InvOPA << 2)
#define HC_HABLFAb_OPAp5        (HC_XA_OPAp5 << 2)
#define HC_HABLFAb_0            (HC_XA_0 << 2)
#define HC_HABLFAb_Asrc         (HC_XA_Asrc << 2)
#define HC_HABLFAb_Adst         (HC_XA_Adst << 2)
#define HC_HABLFAb_Fog          (HC_XA_Fog << 2)
#define HC_HABLFAb_minAsrcFog   (HC_XA_minAsrcFog << 2)
#define HC_HABLFAb_minAsrcAdst  (HC_XA_minAsrcAdst << 2)
#define HC_HABLFAb_maxAsrcFog   (HC_XA_maxAsrcFog << 2)
#define HC_HABLFAb_maxAsrcAdst  (HC_XA_maxAsrcAdst << 2)
#define HC_HABLFAb_minAsrcInvAdst   (HC_XA_minAsrcInvAdst << 2)
#define HC_HABLFAb_HABLFRA          (HC_XA_HABLFRA << 2)
/* HC_SubA_HABLRAa         0x003d
 */
#define HC_HABLRAa_MASK         0x00ff0000
#define HC_HABLRFAa_MASK        0x0000ff00
#define HC_HABLRAbias_MASK      0x000000ff
#define HC_HABLRAa_SHIFT        16
#define HC_HABLRFAa_SHIFT       8
/* HC_SubA_HABLRAb         0x003e
 */
#define HC_HABLRAb_MASK         0x0000ff00
#define HC_HABLRFAb_MASK        0x000000ff
#define HC_HABLRAb_SHIFT        8

/* Destination Setting
 */
#define HC_SubA_HDBBasL         0x0040
#define HC_SubA_HDBBasH         0x0041
#define HC_SubA_HDBFM           0x0042
#define HC_SubA_HFBBMSKL        0x0043
#define HC_SubA_HROP            0x0044
/* HC_SubA_HDBFM           0x0042
 */
#define HC_HDBFM_MASK           0x001f0000
#define HC_HDBLoc_MASK          0x0000c000
#define HC_HDBPit_MASK          0x00003fff
#define HC_HDBFM_RGB555         0x00000000
#define HC_HDBFM_RGB565         0x00010000
#define HC_HDBFM_ARGB4444       0x00020000
#define HC_HDBFM_ARGB1555       0x00030000
#define HC_HDBFM_BGR555         0x00040000
#define HC_HDBFM_BGR565         0x00050000
#define HC_HDBFM_ABGR4444       0x00060000
#define HC_HDBFM_ABGR1555       0x00070000
#define HC_HDBFM_ARGB0888       0x00080000
#define HC_HDBFM_ARGB8888       0x00090000
#define HC_HDBFM_ABGR0888       0x000a0000
#define HC_HDBFM_ABGR8888       0x000b0000
#define HC_HDBLoc_Local         0x00000000
#define HC_HDBLoc_Sys           0x00004000
/* HC_SubA_HROP            0x0044
 */
#define HC_HROP_MASK            0x00000f00
#define HC_HFBBMSKH_MASK        0x000000ff
#define HC_HROP_BLACK           0x00000000
#define HC_HROP_DPon            0x00000100
#define HC_HROP_DPna            0x00000200
#define HC_HROP_Pn              0x00000300
#define HC_HROP_PDna            0x00000400
#define HC_HROP_Dn              0x00000500
#define HC_HROP_DPx             0x00000600
#define HC_HROP_DPan            0x00000700
#define HC_HROP_DPa             0x00000800
#define HC_HROP_DPxn            0x00000900
#define HC_HROP_D               0x00000a00
#define HC_HROP_DPno            0x00000b00
#define HC_HROP_P               0x00000c00
#define HC_HROP_PDno            0x00000d00
#define HC_HROP_DPo             0x00000e00
#define HC_HROP_WHITE           0x00000f00

/* Fog Setting
 */
#define HC_SubA_HFogLF          0x0050
#define HC_SubA_HFogCL          0x0051
#define HC_SubA_HFogCH          0x0052
#define HC_SubA_HFogStL         0x0053
#define HC_SubA_HFogStH         0x0054
#define HC_SubA_HFogOOdMF       0x0055
#define HC_SubA_HFogOOdEF       0x0056
#define HC_SubA_HFogEndL        0x0057
#define HC_SubA_HFogDenst       0x0058
/* HC_SubA_FogLF           0x0050
 */
#define HC_FogLF_MASK           0x00000010
#define HC_FogEq_MASK           0x00000008
#define HC_FogMD_MASK           0x00000007
#define HC_FogMD_LocalFog        0x00000000
#define HC_FogMD_LinearFog       0x00000002
#define HC_FogMD_ExponentialFog  0x00000004
#define HC_FogMD_Exponential2Fog 0x00000005
/* #define HC_FogMD_FogTable       0x00000003 */

/* HC_SubA_HFogDenst        0x0058
 */
#define HC_FogDenst_MASK        0x001fff00
#define HC_FogEndL_MASK         0x000000ff

/* Texture subtype definitions
 */
#define HC_SubType_Tex0         0x00000000
#define HC_SubType_Tex1         0x00000001
#define HC_SubType_TexGeneral   0x000000fe

/* Attribute of texture n
 */
#define HC_SubA_HTXnL0BasL      0x0000
#define HC_SubA_HTXnL1BasL      0x0001
#define HC_SubA_HTXnL2BasL      0x0002
#define HC_SubA_HTXnL3BasL      0x0003
#define HC_SubA_HTXnL4BasL      0x0004
#define HC_SubA_HTXnL5BasL      0x0005
#define HC_SubA_HTXnL6BasL      0x0006
#define HC_SubA_HTXnL7BasL      0x0007
#define HC_SubA_HTXnL8BasL      0x0008
#define HC_SubA_HTXnL9BasL      0x0009
#define HC_SubA_HTXnLaBasL      0x000a
#define HC_SubA_HTXnLbBasL      0x000b
#define HC_SubA_HTXnLcBasL      0x000c
#define HC_SubA_HTXnLdBasL      0x000d
#define HC_SubA_HTXnLeBasL      0x000e
#define HC_SubA_HTXnLfBasL      0x000f
#define HC_SubA_HTXnL10BasL     0x0010
#define HC_SubA_HTXnL11BasL     0x0011
#define HC_SubA_HTXnL012BasH    0x0020
#define HC_SubA_HTXnL345BasH    0x0021
#define HC_SubA_HTXnL678BasH    0x0022
#define HC_SubA_HTXnL9abBasH    0x0023
#define HC_SubA_HTXnLcdeBasH    0x0024
#define HC_SubA_HTXnLf1011BasH  0x0025
#define HC_SubA_HTXnL0Pit       0x002b
#define HC_SubA_HTXnL1Pit       0x002c
#define HC_SubA_HTXnL2Pit       0x002d
#define HC_SubA_HTXnL3Pit       0x002e
#define HC_SubA_HTXnL4Pit       0x002f
#define HC_SubA_HTXnL5Pit       0x0030
#define HC_SubA_HTXnL6Pit       0x0031
#define HC_SubA_HTXnL7Pit       0x0032
#define HC_SubA_HTXnL8Pit       0x0033
#define HC_SubA_HTXnL9Pit       0x0034
#define HC_SubA_HTXnLaPit       0x0035
#define HC_SubA_HTXnLbPit       0x0036
#define HC_SubA_HTXnLcPit       0x0037
#define HC_SubA_HTXnLdPit       0x0038
#define HC_SubA_HTXnLePit       0x0039
#define HC_SubA_HTXnLfPit       0x003a
#define HC_SubA_HTXnL10Pit      0x003b
#define HC_SubA_HTXnL11Pit      0x003c
#define HC_SubA_HTXnL0_5WE      0x004b
#define HC_SubA_HTXnL6_bWE      0x004c
#define HC_SubA_HTXnLc_11WE     0x004d
#define HC_SubA_HTXnL0_5HE      0x0051
#define HC_SubA_HTXnL6_bHE      0x0052
#define HC_SubA_HTXnLc_11HE     0x0053
#define HC_SubA_HTXnL0OS        0x0077
#define HC_SubA_HTXnTB          0x0078
#define HC_SubA_HTXnMPMD        0x0079
#define HC_SubA_HTXnCLODu       0x007a
#define HC_SubA_HTXnFM          0x007b
#define HC_SubA_HTXnTRCH        0x007c
#define HC_SubA_HTXnTRCL        0x007d
#define HC_SubA_HTXnTBC         0x007e
#define HC_SubA_HTXnTRAH        0x007f
#define HC_SubA_HTXnTBLCsat     0x0080
#define HC_SubA_HTXnTBLCop      0x0081
#define HC_SubA_HTXnTBLMPfog    0x0082
#define HC_SubA_HTXnTBLAsat     0x0083
#define HC_SubA_HTXnTBLRCa      0x0085
#define HC_SubA_HTXnTBLRCb      0x0086
#define HC_SubA_HTXnTBLRCc      0x0087
#define HC_SubA_HTXnTBLRCbias   0x0088
#define HC_SubA_HTXnTBLRAa      0x0089
#define HC_SubA_HTXnTBLRFog     0x008a
#define HC_SubA_HTXnBumpM00     0x0090
#define HC_SubA_HTXnBumpM01     0x0091
#define HC_SubA_HTXnBumpM10     0x0092
#define HC_SubA_HTXnBumpM11     0x0093
#define HC_SubA_HTXnLScale      0x0094
#define HC_SubA_HTXSMD          0x0000
/* HC_SubA_HTXnL012BasH    0x0020
 */
#define HC_HTXnL0BasH_MASK      0x000000ff
#define HC_HTXnL1BasH_MASK      0x0000ff00
#define HC_HTXnL2BasH_MASK      0x00ff0000
#define HC_HTXnL1BasH_SHIFT     8
#define HC_HTXnL2BasH_SHIFT     16
/* HC_SubA_HTXnL345BasH    0x0021
 */
#define HC_HTXnL3BasH_MASK      0x000000ff
#define HC_HTXnL4BasH_MASK      0x0000ff00
#define HC_HTXnL5BasH_MASK      0x00ff0000
#define HC_HTXnL4BasH_SHIFT     8
#define HC_HTXnL5BasH_SHIFT     16
/* HC_SubA_HTXnL678BasH    0x0022
 */
#define HC_HTXnL6BasH_MASK      0x000000ff
#define HC_HTXnL7BasH_MASK      0x0000ff00
#define HC_HTXnL8BasH_MASK      0x00ff0000
#define HC_HTXnL7BasH_SHIFT     8
#define HC_HTXnL8BasH_SHIFT     16
/* HC_SubA_HTXnL9abBasH    0x0023
 */
#define HC_HTXnL9BasH_MASK      0x000000ff
#define HC_HTXnLaBasH_MASK      0x0000ff00
#define HC_HTXnLbBasH_MASK      0x00ff0000
#define HC_HTXnLaBasH_SHIFT     8
#define HC_HTXnLbBasH_SHIFT     16
/* HC_SubA_HTXnLcdeBasH    0x0024
 */
#define HC_HTXnLcBasH_MASK      0x000000ff
#define HC_HTXnLdBasH_MASK      0x0000ff00
#define HC_HTXnLeBasH_MASK      0x00ff0000
#define HC_HTXnLdBasH_SHIFT     8
#define HC_HTXnLeBasH_SHIFT     16
/* HC_SubA_HTXnLcdeBasH    0x0025
 */
#define HC_HTXnLfBasH_MASK      0x000000ff
#define HC_HTXnL10BasH_MASK      0x0000ff00
#define HC_HTXnL11BasH_MASK      0x00ff0000
#define HC_HTXnL10BasH_SHIFT     8
#define HC_HTXnL11BasH_SHIFT     16
/* HC_SubA_HTXnL0Pit       0x002b
 */
#define HC_HTXnLnPit_MASK       0x00003fff
#define HC_HTXnEnPit_MASK       0x00080000
#define HC_HTXnLnPitE_MASK      0x00f00000
#define HC_HTXnLnPitE_SHIFT     20
/* HC_SubA_HTXnL0_5WE      0x004b
 */
#define HC_HTXnL0WE_MASK        0x0000000f
#define HC_HTXnL1WE_MASK        0x000000f0
#define HC_HTXnL2WE_MASK        0x00000f00
#define HC_HTXnL3WE_MASK        0x0000f000
#define HC_HTXnL4WE_MASK        0x000f0000
#define HC_HTXnL5WE_MASK        0x00f00000
#define HC_HTXnL1WE_SHIFT       4
#define HC_HTXnL2WE_SHIFT       8
#define HC_HTXnL3WE_SHIFT       12
#define HC_HTXnL4WE_SHIFT       16
#define HC_HTXnL5WE_SHIFT       20
/* HC_SubA_HTXnL6_bWE      0x004c
 */
#define HC_HTXnL6WE_MASK        0x0000000f
#define HC_HTXnL7WE_MASK        0x000000f0
#define HC_HTXnL8WE_MASK        0x00000f00
#define HC_HTXnL9WE_MASK        0x0000f000
#define HC_HTXnLaWE_MASK        0x000f0000
#define HC_HTXnLbWE_MASK        0x00f00000
#define HC_HTXnL7WE_SHIFT       4
#define HC_HTXnL8WE_SHIFT       8
#define HC_HTXnL9WE_SHIFT       12
#define HC_HTXnLaWE_SHIFT       16
#define HC_HTXnLbWE_SHIFT       20
/* HC_SubA_HTXnLc_11WE      0x004d
 */
#define HC_HTXnLcWE_MASK        0x0000000f
#define HC_HTXnLdWE_MASK        0x000000f0
#define HC_HTXnLeWE_MASK        0x00000f00
#define HC_HTXnLfWE_MASK        0x0000f000
#define HC_HTXnL10WE_MASK       0x000f0000
#define HC_HTXnL11WE_MASK       0x00f00000
#define HC_HTXnLdWE_SHIFT       4
#define HC_HTXnLeWE_SHIFT       8
#define HC_HTXnLfWE_SHIFT       12
#define HC_HTXnL10WE_SHIFT      16
#define HC_HTXnL11WE_SHIFT      20
/* HC_SubA_HTXnL0_5HE      0x0051
 */
#define HC_HTXnL0HE_MASK        0x0000000f
#define HC_HTXnL1HE_MASK        0x000000f0
#define HC_HTXnL2HE_MASK        0x00000f00
#define HC_HTXnL3HE_MASK        0x0000f000
#define HC_HTXnL4HE_MASK        0x000f0000
#define HC_HTXnL5HE_MASK        0x00f00000
#define HC_HTXnL1HE_SHIFT       4
#define HC_HTXnL2HE_SHIFT       8
#define HC_HTXnL3HE_SHIFT       12
#define HC_HTXnL4HE_SHIFT       16
#define HC_HTXnL5HE_SHIFT       20
/* HC_SubA_HTXnL6_bHE      0x0052
 */
#define HC_HTXnL6HE_MASK        0x0000000f
#define HC_HTXnL7HE_MASK        0x000000f0
#define HC_HTXnL8HE_MASK        0x00000f00
#define HC_HTXnL9HE_MASK        0x0000f000
#define HC_HTXnLaHE_MASK        0x000f0000
#define HC_HTXnLbHE_MASK        0x00f00000
#define HC_HTXnL7HE_SHIFT       4
#define HC_HTXnL8HE_SHIFT       8
#define HC_HTXnL9HE_SHIFT       12
#define HC_HTXnLaHE_SHIFT       16
#define HC_HTXnLbHE_SHIFT       20
/* HC_SubA_HTXnLc_11HE      0x0053
 */
#define HC_HTXnLcHE_MASK        0x0000000f
#define HC_HTXnLdHE_MASK        0x000000f0
#define HC_HTXnLeHE_MASK        0x00000f00
#define HC_HTXnLfHE_MASK        0x0000f000
#define HC_HTXnL10HE_MASK       0x000f0000
#define HC_HTXnL11HE_MASK       0x00f00000
#define HC_HTXnLdHE_SHIFT       4
#define HC_HTXnLeHE_SHIFT       8
#define HC_HTXnLfHE_SHIFT       12
#define HC_HTXnL10HE_SHIFT      16
#define HC_HTXnL11HE_SHIFT      20
/* HC_SubA_HTXnL0OS        0x0077
 */
#define HC_HTXnL0OS_MASK        0x003ff000
#define HC_HTXnLVmax_MASK       0x00000fc0
#define HC_HTXnLVmin_MASK       0x0000003f
#define HC_HTXnL0OS_SHIFT       12
#define HC_HTXnLVmax_SHIFT      6
/* HC_SubA_HTXnTB          0x0078
 */
#define HC_HTXnTB_MASK          0x00f00000
#define HC_HTXnFLSe_MASK        0x0000e000
#define HC_HTXnFLSs_MASK        0x00001c00
#define HC_HTXnFLTe_MASK        0x00000380
#define HC_HTXnFLTs_MASK        0x00000070
#define HC_HTXnFLDs_MASK        0x0000000f
#define HC_HTXnTB_NoTB          0x00000000
#define HC_HTXnTB_TBC_S         0x00100000
#define HC_HTXnTB_TBC_T         0x00200000
#define HC_HTXnTB_TB_S          0x00400000
#define HC_HTXnTB_TB_T          0x00800000
#define HC_HTXnFLSe_Nearest     0x00000000
#define HC_HTXnFLSe_Linear      0x00002000
#define HC_HTXnFLSe_NonLinear   0x00004000
#define HC_HTXnFLSe_Sharp       0x00008000
#define HC_HTXnFLSe_Flat_Gaussian_Cubic 0x0000c000
#define HC_HTXnFLSs_Nearest     0x00000000
#define HC_HTXnFLSs_Linear      0x00000400
#define HC_HTXnFLSs_NonLinear   0x00000800
#define HC_HTXnFLSs_Flat_Gaussian_Cubic 0x00001800
#define HC_HTXnFLTe_Nearest     0x00000000
#define HC_HTXnFLTe_Linear      0x00000080
#define HC_HTXnFLTe_NonLinear   0x00000100
#define HC_HTXnFLTe_Sharp       0x00000180
#define HC_HTXnFLTe_Flat_Gaussian_Cubic 0x00000300
#define HC_HTXnFLTs_Nearest     0x00000000
#define HC_HTXnFLTs_Linear      0x00000010
#define HC_HTXnFLTs_NonLinear   0x00000020
#define HC_HTXnFLTs_Flat_Gaussian_Cubic 0x00000060
#define HC_HTXnFLDs_Tex0        0x00000000
#define HC_HTXnFLDs_Nearest     0x00000001
#define HC_HTXnFLDs_Linear      0x00000002
#define HC_HTXnFLDs_NonLinear   0x00000003
#define HC_HTXnFLDs_Dither      0x00000004
#define HC_HTXnFLDs_ConstLOD    0x00000005
#define HC_HTXnFLDs_Ani         0x00000006
#define HC_HTXnFLDs_AniDither   0x00000007
/* HC_SubA_HTXnMPMD        0x0079
 */
#define HC_HTXnMPMD_SMASK       0x00070000
#define HC_HTXnMPMD_TMASK       0x00380000
#define HC_HTXnLODDTf_MASK      0x00000007
#define HC_HTXnXY2ST_MASK       0x00000008
#define HC_HTXnMPMD_Tsingle     0x00000000
#define HC_HTXnMPMD_Tclamp      0x00080000
#define HC_HTXnMPMD_Trepeat     0x00100000
#define HC_HTXnMPMD_Tmirror     0x00180000
#define HC_HTXnMPMD_Twrap       0x00200000
#define HC_HTXnMPMD_Ssingle     0x00000000
#define HC_HTXnMPMD_Sclamp      0x00010000
#define HC_HTXnMPMD_Srepeat     0x00020000
#define HC_HTXnMPMD_Smirror     0x00030000
#define HC_HTXnMPMD_Swrap       0x00040000
/* HC_SubA_HTXnCLODu       0x007a
 */
#define HC_HTXnCLODu_MASK       0x000ffc00
#define HC_HTXnCLODd_MASK       0x000003ff
#define HC_HTXnCLODu_SHIFT      10
/* HC_SubA_HTXnFM          0x007b
 */
#define HC_HTXnFM_MASK          0x00ff0000
#define HC_HTXnLoc_MASK         0x00000003
#define HC_HTXnFM_INDEX         0x00000000
#define HC_HTXnFM_Intensity     0x00080000
#define HC_HTXnFM_Lum           0x00100000
#define HC_HTXnFM_Alpha         0x00180000
#define HC_HTXnFM_DX            0x00280000
#define HC_HTXnFM_ARGB16        0x00880000
#define HC_HTXnFM_ARGB32        0x00980000
#define HC_HTXnFM_ABGR16        0x00a80000
#define HC_HTXnFM_ABGR32        0x00b80000
#define HC_HTXnFM_RGBA16        0x00c80000
#define HC_HTXnFM_RGBA32        0x00d80000
#define HC_HTXnFM_BGRA16        0x00e80000
#define HC_HTXnFM_BGRA32        0x00f80000
#define HC_HTXnFM_BUMPMAP       0x00380000
#define HC_HTXnFM_Index1        (HC_HTXnFM_INDEX     | 0x00000000)
#define HC_HTXnFM_Index2        (HC_HTXnFM_INDEX     | 0x00010000)
#define HC_HTXnFM_Index4        (HC_HTXnFM_INDEX     | 0x00020000)
#define HC_HTXnFM_Index8        (HC_HTXnFM_INDEX     | 0x00030000)
#define HC_HTXnFM_T1            (HC_HTXnFM_Intensity | 0x00000000)
#define HC_HTXnFM_T2            (HC_HTXnFM_Intensity | 0x00010000)
#define HC_HTXnFM_T4            (HC_HTXnFM_Intensity | 0x00020000)
#define HC_HTXnFM_T8            (HC_HTXnFM_Intensity | 0x00030000)
#define HC_HTXnFM_L1            (HC_HTXnFM_Lum       | 0x00000000)
#define HC_HTXnFM_L2            (HC_HTXnFM_Lum       | 0x00010000)
#define HC_HTXnFM_L4            (HC_HTXnFM_Lum       | 0x00020000)
#define HC_HTXnFM_L8            (HC_HTXnFM_Lum       | 0x00030000)
#define HC_HTXnFM_AL44          (HC_HTXnFM_Lum       | 0x00040000)
#define HC_HTXnFM_AL88          (HC_HTXnFM_Lum       | 0x00050000)
#define HC_HTXnFM_A1            (HC_HTXnFM_Alpha     | 0x00000000)
#define HC_HTXnFM_A2            (HC_HTXnFM_Alpha     | 0x00010000)
#define HC_HTXnFM_A4            (HC_HTXnFM_Alpha     | 0x00020000)
#define HC_HTXnFM_A8            (HC_HTXnFM_Alpha     | 0x00030000)
#define HC_HTXnFM_DX1           (HC_HTXnFM_DX        | 0x00010000)
#define HC_HTXnFM_DX23          (HC_HTXnFM_DX        | 0x00020000)
#define HC_HTXnFM_DX45          (HC_HTXnFM_DX        | 0x00030000)
#define HC_HTXnFM_RGB555        (HC_HTXnFM_ARGB16    | 0x00000000)
#define HC_HTXnFM_RGB565        (HC_HTXnFM_ARGB16    | 0x00010000)
#define HC_HTXnFM_ARGB1555      (HC_HTXnFM_ARGB16    | 0x00020000)
#define HC_HTXnFM_ARGB4444      (HC_HTXnFM_ARGB16    | 0x00030000)
#define HC_HTXnFM_ARGB0888      (HC_HTXnFM_ARGB32    | 0x00000000)
#define HC_HTXnFM_ARGB8888      (HC_HTXnFM_ARGB32    | 0x00010000)
#define HC_HTXnFM_BGR555        (HC_HTXnFM_ABGR16    | 0x00000000)
#define HC_HTXnFM_BGR565        (HC_HTXnFM_ABGR16    | 0x00010000)
#define HC_HTXnFM_ABGR1555      (HC_HTXnFM_ABGR16    | 0x00020000)
#define HC_HTXnFM_ABGR4444      (HC_HTXnFM_ABGR16    | 0x00030000)
#define HC_HTXnFM_ABGR0888      (HC_HTXnFM_ABGR32    | 0x00000000)
#define HC_HTXnFM_ABGR8888      (HC_HTXnFM_ABGR32    | 0x00010000)
#define HC_HTXnFM_RGBA5550      (HC_HTXnFM_RGBA16    | 0x00000000)
#define HC_HTXnFM_RGBA5551      (HC_HTXnFM_RGBA16    | 0x00020000)
#define HC_HTXnFM_RGBA4444      (HC_HTXnFM_RGBA16    | 0x00030000)
#define HC_HTXnFM_RGBA8880      (HC_HTXnFM_RGBA32    | 0x00000000)
#define HC_HTXnFM_RGBA8888      (HC_HTXnFM_RGBA32    | 0x00010000)
#define HC_HTXnFM_BGRA5550      (HC_HTXnFM_BGRA16    | 0x00000000)
#define HC_HTXnFM_BGRA5551      (HC_HTXnFM_BGRA16    | 0x00020000)
#define HC_HTXnFM_BGRA4444      (HC_HTXnFM_BGRA16    | 0x00030000)
#define HC_HTXnFM_BGRA8880      (HC_HTXnFM_BGRA32    | 0x00000000)
#define HC_HTXnFM_BGRA8888      (HC_HTXnFM_BGRA32    | 0x00010000)
#define HC_HTXnFM_VU88          (HC_HTXnFM_BUMPMAP   | 0x00000000)
#define HC_HTXnFM_LVU655        (HC_HTXnFM_BUMPMAP   | 0x00010000)
#define HC_HTXnFM_LVU888        (HC_HTXnFM_BUMPMAP   | 0x00020000)
#define HC_HTXnLoc_Local        0x00000000
#define HC_HTXnLoc_Sys          0x00000002
#define HC_HTXnLoc_AGP          0x00000003
/* HC_SubA_HTXnTRAH        0x007f
 */
#define HC_HTXnTRAH_MASK        0x00ff0000
#define HC_HTXnTRAL_MASK        0x0000ff00
#define HC_HTXnTBA_MASK         0x000000ff
#define HC_HTXnTRAH_SHIFT       16
#define HC_HTXnTRAL_SHIFT       8
/* HC_SubA_HTXnTBLCsat     0x0080
 *-- Define the input texture.
 */
#define HC_XTC_TOPC             0x00000000
#define HC_XTC_InvTOPC          0x00000010
#define HC_XTC_TOPCp5           0x00000020
#define HC_XTC_Cbias            0x00000000
#define HC_XTC_InvCbias         0x00000010
#define HC_XTC_0                0x00000000
#define HC_XTC_Dif              0x00000001
#define HC_XTC_Spec             0x00000002
#define HC_XTC_Tex              0x00000003
#define HC_XTC_Cur              0x00000004
#define HC_XTC_Adif             0x00000005
#define HC_XTC_Fog              0x00000006
#define HC_XTC_Atex             0x00000007
#define HC_XTC_Acur             0x00000008
#define HC_XTC_HTXnTBLRC        0x00000009
#define HC_XTC_Ctexnext         0x0000000a
/*--
 */
#define HC_HTXnTBLCsat_MASK     0x00800000
#define HC_HTXnTBLCa_MASK       0x000fc000
#define HC_HTXnTBLCb_MASK       0x00001f80
#define HC_HTXnTBLCc_MASK       0x0000003f
#define HC_HTXnTBLCa_TOPC       (HC_XTC_TOPC << 14)
#define HC_HTXnTBLCa_InvTOPC    (HC_XTC_InvTOPC << 14)
#define HC_HTXnTBLCa_TOPCp5     (HC_XTC_TOPCp5 << 14)
#define HC_HTXnTBLCa_0          (HC_XTC_0 << 14)
#define HC_HTXnTBLCa_Dif        (HC_XTC_Dif << 14)
#define HC_HTXnTBLCa_Spec       (HC_XTC_Spec << 14)
#define HC_HTXnTBLCa_Tex        (HC_XTC_Tex << 14)
#define HC_HTXnTBLCa_Cur        (HC_XTC_Cur << 14)
#define HC_HTXnTBLCa_Adif       (HC_XTC_Adif << 14)
#define HC_HTXnTBLCa_Fog        (HC_XTC_Fog << 14)
#define HC_HTXnTBLCa_Atex       (HC_XTC_Atex << 14)
#define HC_HTXnTBLCa_Acur       (HC_XTC_Acur << 14)
#define HC_HTXnTBLCa_HTXnTBLRC  (HC_XTC_HTXnTBLRC << 14)
#define HC_HTXnTBLCa_Ctexnext   (HC_XTC_Ctexnext << 14)
#define HC_HTXnTBLCb_TOPC       (HC_XTC_TOPC << 7)
#define HC_HTXnTBLCb_InvTOPC    (HC_XTC_InvTOPC << 7)
#define HC_HTXnTBLCb_TOPCp5     (HC_XTC_TOPCp5 << 7)
#define HC_HTXnTBLCb_0          (HC_XTC_0 << 7)
#define HC_HTXnTBLCb_Dif        (HC_XTC_Dif << 7)
#define HC_HTXnTBLCb_Spec       (HC_XTC_Spec << 7)
#define HC_HTXnTBLCb_Tex        (HC_XTC_Tex << 7)
#define HC_HTXnTBLCb_Cur        (HC_XTC_Cur << 7)
#define HC_HTXnTBLCb_Adif       (HC_XTC_Adif << 7)
#define HC_HTXnTBLCb_Fog        (HC_XTC_Fog << 7)
#define HC_HTXnTBLCb_Atex       (HC_XTC_Atex << 7)
#define HC_HTXnTBLCb_Acur       (HC_XTC_Acur << 7)
#define HC_HTXnTBLCb_HTXnTBLRC  (HC_XTC_HTXnTBLRC << 7)
#define HC_HTXnTBLCb_Ctexnext   (HC_XTC_Ctexnext << 7)
#define HC_HTXnTBLCc_TOPC       (HC_XTC_TOPC << 0)
#define HC_HTXnTBLCc_InvTOPC    (HC_XTC_InvTOPC << 0)
#define HC_HTXnTBLCc_TOPCp5     (HC_XTC_TOPCp5 << 0)
#define HC_HTXnTBLCc_0          (HC_XTC_0 << 0)
#define HC_HTXnTBLCc_Dif        (HC_XTC_Dif << 0)
#define HC_HTXnTBLCc_Spec       (HC_XTC_Spec << 0)
#define HC_HTXnTBLCc_Tex        (HC_XTC_Tex << 0)
#define HC_HTXnTBLCc_Cur        (HC_XTC_Cur << 0)
#define HC_HTXnTBLCc_Adif       (HC_XTC_Adif << 0)
#define HC_HTXnTBLCc_Fog        (HC_XTC_Fog << 0)
#define HC_HTXnTBLCc_Atex       (HC_XTC_Atex << 0)
#define HC_HTXnTBLCc_Acur       (HC_XTC_Acur << 0)
#define HC_HTXnTBLCc_HTXnTBLRC  (HC_XTC_HTXnTBLRC << 0)
#define HC_HTXnTBLCc_Ctexnext   (HC_XTC_Ctexnext << 0)
/* HC_SubA_HTXnTBLCop      0x0081
 */
#define HC_HTXnTBLdot_MASK      0x00c00000
#define HC_HTXnTBLCop_MASK      0x00380000
#define HC_HTXnTBLCbias_MASK    0x0007c000
#define HC_HTXnTBLCshift_MASK   0x00001800
#define HC_HTXnTBLAop_MASK      0x00000380
#define HC_HTXnTBLAbias_MASK    0x00000078
#define HC_HTXnTBLAshift_MASK   0x00000003
#define HC_HTXnTBLCop_Add       0x00000000
#define HC_HTXnTBLCop_Sub       0x00080000
#define HC_HTXnTBLCop_Min       0x00100000
#define HC_HTXnTBLCop_Max       0x00180000
#define HC_HTXnTBLCop_Mask      0x00200000
#define HC_HTXnTBLCbias_Cbias           (HC_XTC_Cbias << 14)
#define HC_HTXnTBLCbias_InvCbias        (HC_XTC_InvCbias << 14)
#define HC_HTXnTBLCbias_0               (HC_XTC_0 << 14)
#define HC_HTXnTBLCbias_Dif             (HC_XTC_Dif << 14)
#define HC_HTXnTBLCbias_Spec            (HC_XTC_Spec << 14)
#define HC_HTXnTBLCbias_Tex             (HC_XTC_Tex << 14)
#define HC_HTXnTBLCbias_Cur             (HC_XTC_Cur << 14)
#define HC_HTXnTBLCbias_Adif            (HC_XTC_Adif << 14)
#define HC_HTXnTBLCbias_Fog             (HC_XTC_Fog << 14)
#define HC_HTXnTBLCbias_Atex            (HC_XTC_Atex << 14)
#define HC_HTXnTBLCbias_Acur            (HC_XTC_Acur << 14)
#define HC_HTXnTBLCbias_HTXnTBLRC       (HC_XTC_HTXnTBLRC << 14)
#define HC_HTXnTBLCshift_1      0x00000000
#define HC_HTXnTBLCshift_2      0x00000800
#define HC_HTXnTBLCshift_No     0x00001000
#define HC_HTXnTBLCshift_DotP   0x00001800
/*=* John Sheng [2003.7.18] texture combine *=*/
#define HC_HTXnTBLDOT3   0x00080000
#define HC_HTXnTBLDOT4   0x000C0000

#define HC_HTXnTBLAop_Add       0x00000000
#define HC_HTXnTBLAop_Sub       0x00000080
#define HC_HTXnTBLAop_Min       0x00000100
#define HC_HTXnTBLAop_Max       0x00000180
#define HC_HTXnTBLAop_Mask      0x00000200
#define HC_HTXnTBLAbias_Inv             0x00000040
#define HC_HTXnTBLAbias_Adif            0x00000000
#define HC_HTXnTBLAbias_Fog             0x00000008
#define HC_HTXnTBLAbias_Acur            0x00000010
#define HC_HTXnTBLAbias_HTXnTBLRAbias   0x00000018
#define HC_HTXnTBLAbias_Atex            0x00000020
#define HC_HTXnTBLAshift_1      0x00000000
#define HC_HTXnTBLAshift_2      0x00000001
#define HC_HTXnTBLAshift_No     0x00000002
/* #define HC_HTXnTBLAshift_DotP   0x00000003 */
/* HC_SubA_HTXnTBLMPFog    0x0082
 */
#define HC_HTXnTBLMPfog_MASK    0x00e00000
#define HC_HTXnTBLMPfog_0       0x00000000
#define HC_HTXnTBLMPfog_Adif    0x00200000
#define HC_HTXnTBLMPfog_Fog     0x00400000
#define HC_HTXnTBLMPfog_Atex    0x00600000
#define HC_HTXnTBLMPfog_Acur    0x00800000
#define HC_HTXnTBLMPfog_GHTXnTBLRFog    0x00a00000
/* HC_SubA_HTXnTBLAsat     0x0083
 *-- Define the texture alpha input.
 */
#define HC_XTA_TOPA             0x00000000
#define HC_XTA_InvTOPA          0x00000008
#define HC_XTA_TOPAp5           0x00000010
#define HC_XTA_Adif             0x00000000
#define HC_XTA_Fog              0x00000001
#define HC_XTA_Acur             0x00000002
#define HC_XTA_HTXnTBLRA        0x00000003
#define HC_XTA_Atex             0x00000004
#define HC_XTA_Atexnext         0x00000005
/*--
 */
#define HC_HTXnTBLAsat_MASK     0x00800000
#define HC_HTXnTBLAMB_MASK      0x00700000
#define HC_HTXnTBLAa_MASK       0x0007c000
#define HC_HTXnTBLAb_MASK       0x00000f80
#define HC_HTXnTBLAc_MASK       0x0000001f
#define HC_HTXnTBLAMB_SHIFT     20
#define HC_HTXnTBLAa_TOPA       (HC_XTA_TOPA << 14)
#define HC_HTXnTBLAa_InvTOPA    (HC_XTA_InvTOPA << 14)
#define HC_HTXnTBLAa_TOPAp5     (HC_XTA_TOPAp5 << 14)
#define HC_HTXnTBLAa_Adif       (HC_XTA_Adif << 14)
#define HC_HTXnTBLAa_Fog        (HC_XTA_Fog << 14)
#define HC_HTXnTBLAa_Acur       (HC_XTA_Acur << 14)
#define HC_HTXnTBLAa_HTXnTBLRA  (HC_XTA_HTXnTBLRA << 14)
#define HC_HTXnTBLAa_Atex       (HC_XTA_Atex << 14)
#define HC_HTXnTBLAa_Atexnext   (HC_XTA_Atexnext << 14)
#define HC_HTXnTBLAb_TOPA       (HC_XTA_TOPA << 7)
#define HC_HTXnTBLAb_InvTOPA    (HC_XTA_InvTOPA << 7)
#define HC_HTXnTBLAb_TOPAp5     (HC_XTA_TOPAp5 << 7)
#define HC_HTXnTBLAb_Adif       (HC_XTA_Adif << 7)
#define HC_HTXnTBLAb_Fog        (HC_XTA_Fog << 7)
#define HC_HTXnTBLAb_Acur       (HC_XTA_Acur << 7)
#define HC_HTXnTBLAb_HTXnTBLRA  (HC_XTA_HTXnTBLRA << 7)
#define HC_HTXnTBLAb_Atex       (HC_XTA_Atex << 7)
#define HC_HTXnTBLAb_Atexnext   (HC_XTA_Atexnext << 7)
#define HC_HTXnTBLAc_TOPA       (HC_XTA_TOPA << 0)
#define HC_HTXnTBLAc_InvTOPA    (HC_XTA_InvTOPA << 0)
#define HC_HTXnTBLAc_TOPAp5     (HC_XTA_TOPAp5 << 0)
#define HC_HTXnTBLAc_Adif       (HC_XTA_Adif << 0)
#define HC_HTXnTBLAc_Fog        (HC_XTA_Fog << 0)
#define HC_HTXnTBLAc_Acur       (HC_XTA_Acur << 0)
#define HC_HTXnTBLAc_HTXnTBLRA  (HC_XTA_HTXnTBLRA << 0)
#define HC_HTXnTBLAc_Atex       (HC_XTA_Atex << 0)
#define HC_HTXnTBLAc_Atexnext   (HC_XTA_Atexnext << 0)
/* HC_SubA_HTXnTBLRAa      0x0089
 */
#define HC_HTXnTBLRAa_MASK      0x00ff0000
#define HC_HTXnTBLRAb_MASK      0x0000ff00
#define HC_HTXnTBLRAc_MASK      0x000000ff
#define HC_HTXnTBLRAa_SHIFT     16
#define HC_HTXnTBLRAb_SHIFT     8
#define HC_HTXnTBLRAc_SHIFT     0
/* HC_SubA_HTXnTBLRFog     0x008a
 */
#define HC_HTXnTBLRFog_MASK     0x0000ff00
#define HC_HTXnTBLRAbias_MASK   0x000000ff
#define HC_HTXnTBLRFog_SHIFT    8
#define HC_HTXnTBLRAbias_SHIFT  0
/* HC_SubA_HTXnLScale      0x0094
 */
#define HC_HTXnLScale_MASK      0x0007fc00
#define HC_HTXnLOff_MASK        0x000001ff
#define HC_HTXnLScale_SHIFT     10
/* HC_SubA_HTXSMD          0x0000
 */
#define HC_HTXSMD_MASK          0x00000080
#define HC_HTXTMD_MASK          0x00000040
#define HC_HTXNum_MASK          0x00000038
#define HC_HTXTRMD_MASK         0x00000006
#define HC_HTXCHCLR_MASK        0x00000001
#define HC_HTXNum_SHIFT         3

/* Texture Palette n
 */
#define HC_SubType_TexPalette0  0x00000000
#define HC_SubType_TexPalette1  0x00000001
#define HC_SubType_FogTable     0x00000010
#define HC_SubType_Stipple      0x00000014
/* HC_SubA_TexPalette0     0x0000
 */
#define HC_HTPnA_MASK           0xff000000
#define HC_HTPnR_MASK           0x00ff0000
#define HC_HTPnG_MASK           0x0000ff00
#define HC_HTPnB_MASK           0x000000ff
/* HC_SubA_FogTable        0x0010
 */
#define HC_HFPn3_MASK           0xff000000
#define HC_HFPn2_MASK           0x00ff0000
#define HC_HFPn1_MASK           0x0000ff00
#define HC_HFPn_MASK            0x000000ff
#define HC_HFPn3_SHIFT          24
#define HC_HFPn2_SHIFT          16
#define HC_HFPn1_SHIFT          8

/* Auto Testing & Security
 */
#define HC_SubA_HenFIFOAT       0x0000
#define HC_SubA_HFBDrawFirst    0x0004
#define HC_SubA_HFBBasL         0x0005
#define HC_SubA_HFBDst          0x0006
/* HC_SubA_HenFIFOAT       0x0000
 */
#define HC_HenFIFOAT_MASK       0x00000020
#define HC_HenGEMILock_MASK     0x00000010
#define HC_HenFBASwap_MASK      0x00000008
#define HC_HenOT_MASK           0x00000004
#define HC_HenCMDQ_MASK         0x00000002
#define HC_HenTXCTSU_MASK       0x00000001
/* HC_SubA_HFBDrawFirst    0x0004
 */
#define HC_HFBDrawFirst_MASK    0x00000800
#define HC_HFBQueue_MASK        0x00000400
#define HC_HFBLock_MASK         0x00000200
#define HC_HEOF_MASK            0x00000100
#define HC_HFBBasH_MASK         0x000000ff

/* GEMI Setting
 */
#define HC_SubA_HTArbRCM        0x0008
#define HC_SubA_HTArbRZ         0x000a
#define HC_SubA_HTArbWZ         0x000b
#define HC_SubA_HTArbRTX        0x000c
#define HC_SubA_HTArbRCW        0x000d
#define HC_SubA_HTArbE2         0x000e
#define HC_SubA_HArbRQCM        0x0010
#define HC_SubA_HArbWQCM        0x0011
#define HC_SubA_HGEMITout       0x0020
#define HC_SubA_HFthRTXD        0x0040
#define HC_SubA_HFthRTXA        0x0044
#define HC_SubA_HCMDQstL        0x0050
#define HC_SubA_HCMDQendL       0x0051
#define HC_SubA_HCMDQLen        0x0052
/* HC_SubA_HTArbRCM        0x0008
 */
#define HC_HTArbRCM_MASK        0x0000ffff
/* HC_SubA_HTArbRZ         0x000a
 */
#define HC_HTArbRZ_MASK         0x0000ffff
/* HC_SubA_HTArbWZ         0x000b
 */
#define HC_HTArbWZ_MASK         0x0000ffff
/* HC_SubA_HTArbRTX        0x000c
 */
#define HC_HTArbRTX_MASK        0x0000ffff
/* HC_SubA_HTArbRCW        0x000d
 */
#define HC_HTArbRCW_MASK        0x0000ffff
/* HC_SubA_HTArbE2         0x000e
 */
#define HC_HTArbE2_MASK         0x0000ffff
/* HC_SubA_HArbRQCM        0x0010
 */
#define HC_HTArbRQCM_MASK       0x0000ffff
/* HC_SubA_HArbWQCM        0x0011
 */
#define HC_HArbWQCM_MASK        0x0000ffff
/* HC_SubA_HGEMITout       0x0020
 */
#define HC_HGEMITout_MASK       0x000f0000
#define HC_HNPArbZC_MASK        0x0000ffff
#define HC_HGEMITout_SHIFT      16
/* HC_SubA_HFthRTXD        0x0040
 */
#define HC_HFthRTXD_MASK        0x00ff0000
#define HC_HFthRZD_MASK         0x0000ff00
#define HC_HFthWZD_MASK         0x000000ff
#define HC_HFthRTXD_SHIFT       16
#define HC_HFthRZD_SHIFT        8
/* HC_SubA_HFthRTXA        0x0044
 */
#define HC_HFthRTXA_MASK        0x000000ff

/******************************************************************************
** Define the Halcyon Internal register access constants. For simulator only.
******************************************************************************/
#define HC_SIMA_HAGPBstL        0x0000
#define HC_SIMA_HAGPBendL       0x0001
#define HC_SIMA_HAGPCMNT        0x0002
#define HC_SIMA_HAGPBpL         0x0003
#define HC_SIMA_HAGPBpH         0x0004
#define HC_SIMA_HClipTB         0x0005
#define HC_SIMA_HClipLR         0x0006
#define HC_SIMA_HFPClipTL       0x0007
#define HC_SIMA_HFPClipBL       0x0008
#define HC_SIMA_HFPClipLL       0x0009
#define HC_SIMA_HFPClipRL       0x000a
#define HC_SIMA_HFPClipTBH      0x000b
#define HC_SIMA_HFPClipLRH      0x000c
#define HC_SIMA_HLP             0x000d
#define HC_SIMA_HLPRF           0x000e
#define HC_SIMA_HSolidCL        0x000f
#define HC_SIMA_HPixGC          0x0010
#define HC_SIMA_HSPXYOS         0x0011
#define HC_SIMA_HCmdA           0x0012
#define HC_SIMA_HCmdB           0x0013
#define HC_SIMA_HEnable         0x0014
#define HC_SIMA_HZWBBasL        0x0015
#define HC_SIMA_HZWBBasH        0x0016
#define HC_SIMA_HZWBType        0x0017
#define HC_SIMA_HZBiasL         0x0018
#define HC_SIMA_HZWBend         0x0019
#define HC_SIMA_HZWTMD          0x001a
#define HC_SIMA_HZWCDL          0x001b
#define HC_SIMA_HZWCTAGnum      0x001c
#define HC_SIMA_HZCYNum         0x001d
#define HC_SIMA_HZWCFire        0x001e
/* #define HC_SIMA_HSBBasL         0x001d */
/* #define HC_SIMA_HSBBasH         0x001e */
/* #define HC_SIMA_HSBFM           0x001f */
#define HC_SIMA_HSTREF          0x0020
#define HC_SIMA_HSTMD           0x0021
#define HC_SIMA_HABBasL         0x0022
#define HC_SIMA_HABBasH         0x0023
#define HC_SIMA_HABFM           0x0024
#define HC_SIMA_HATMD           0x0025
#define HC_SIMA_HABLCsat        0x0026
#define HC_SIMA_HABLCop         0x0027
#define HC_SIMA_HABLAsat        0x0028
#define HC_SIMA_HABLAop         0x0029
#define HC_SIMA_HABLRCa         0x002a
#define HC_SIMA_HABLRFCa        0x002b
#define HC_SIMA_HABLRCbias      0x002c
#define HC_SIMA_HABLRCb         0x002d
#define HC_SIMA_HABLRFCb        0x002e
#define HC_SIMA_HABLRAa         0x002f
#define HC_SIMA_HABLRAb         0x0030
#define HC_SIMA_HDBBasL         0x0031
#define HC_SIMA_HDBBasH         0x0032
#define HC_SIMA_HDBFM           0x0033
#define HC_SIMA_HFBBMSKL        0x0034
#define HC_SIMA_HROP            0x0035
#define HC_SIMA_HFogLF          0x0036
#define HC_SIMA_HFogCL          0x0037
#define HC_SIMA_HFogCH          0x0038
#define HC_SIMA_HFogStL         0x0039
#define HC_SIMA_HFogStH         0x003a
#define HC_SIMA_HFogOOdMF       0x003b
#define HC_SIMA_HFogOOdEF       0x003c
#define HC_SIMA_HFogEndL        0x003d
#define HC_SIMA_HFogDenst       0x003e
/*---- start of texture 0 setting ----
 */
#define HC_SIMA_HTX0L0BasL      0x0040
#define HC_SIMA_HTX0L1BasL      0x0041
#define HC_SIMA_HTX0L2BasL      0x0042
#define HC_SIMA_HTX0L3BasL      0x0043
#define HC_SIMA_HTX0L4BasL      0x0044
#define HC_SIMA_HTX0L5BasL      0x0045
#define HC_SIMA_HTX0L6BasL      0x0046
#define HC_SIMA_HTX0L7BasL      0x0047
#define HC_SIMA_HTX0L8BasL      0x0048
#define HC_SIMA_HTX0L9BasL      0x0049
#define HC_SIMA_HTX0LaBasL      0x004a
#define HC_SIMA_HTX0LbBasL      0x004b
#define HC_SIMA_HTX0LcBasL      0x004c
#define HC_SIMA_HTX0LdBasL      0x004d
#define HC_SIMA_HTX0LeBasL      0x004e
#define HC_SIMA_HTX0LfBasL      0x004f
#define HC_SIMA_HTX0L10BasL     0x0050
#define HC_SIMA_HTX0L11BasL     0x0051
#define HC_SIMA_HTX0L012BasH    0x0052
#define HC_SIMA_HTX0L345BasH    0x0053
#define HC_SIMA_HTX0L678BasH    0x0054
#define HC_SIMA_HTX0L9abBasH    0x0055
#define HC_SIMA_HTX0LcdeBasH    0x0056
#define HC_SIMA_HTX0Lf1011BasH  0x0057
#define HC_SIMA_HTX0L0Pit       0x0058
#define HC_SIMA_HTX0L1Pit       0x0059
#define HC_SIMA_HTX0L2Pit       0x005a
#define HC_SIMA_HTX0L3Pit       0x005b
#define HC_SIMA_HTX0L4Pit       0x005c
#define HC_SIMA_HTX0L5Pit       0x005d
#define HC_SIMA_HTX0L6Pit       0x005e
#define HC_SIMA_HTX0L7Pit       0x005f
#define HC_SIMA_HTX0L8Pit       0x0060
#define HC_SIMA_HTX0L9Pit       0x0061
#define HC_SIMA_HTX0LaPit       0x0062
#define HC_SIMA_HTX0LbPit       0x0063
#define HC_SIMA_HTX0LcPit       0x0064
#define HC_SIMA_HTX0LdPit       0x0065
#define HC_SIMA_HTX0LePit       0x0066
#define HC_SIMA_HTX0LfPit       0x0067
#define HC_SIMA_HTX0L10Pit      0x0068
#define HC_SIMA_HTX0L11Pit      0x0069
#define HC_SIMA_HTX0L0_5WE      0x006a
#define HC_SIMA_HTX0L6_bWE      0x006b
#define HC_SIMA_HTX0Lc_11WE     0x006c
#define HC_SIMA_HTX0L0_5HE      0x006d
#define HC_SIMA_HTX0L6_bHE      0x006e
#define HC_SIMA_HTX0Lc_11HE     0x006f
#define HC_SIMA_HTX0L0OS        0x0070
#define HC_SIMA_HTX0TB          0x0071
#define HC_SIMA_HTX0MPMD        0x0072
#define HC_SIMA_HTX0CLODu       0x0073
#define HC_SIMA_HTX0FM          0x0074
#define HC_SIMA_HTX0TRCH        0x0075
#define HC_SIMA_HTX0TRCL        0x0076
#define HC_SIMA_HTX0TBC         0x0077
#define HC_SIMA_HTX0TRAH        0x0078
#define HC_SIMA_HTX0TBLCsat     0x0079
#define HC_SIMA_HTX0TBLCop      0x007a
#define HC_SIMA_HTX0TBLMPfog    0x007b
#define HC_SIMA_HTX0TBLAsat     0x007c
#define HC_SIMA_HTX0TBLRCa      0x007d
#define HC_SIMA_HTX0TBLRCb      0x007e
#define HC_SIMA_HTX0TBLRCc      0x007f
#define HC_SIMA_HTX0TBLRCbias   0x0080
#define HC_SIMA_HTX0TBLRAa      0x0081
#define HC_SIMA_HTX0TBLRFog     0x0082
#define HC_SIMA_HTX0BumpM00     0x0083
#define HC_SIMA_HTX0BumpM01     0x0084
#define HC_SIMA_HTX0BumpM10     0x0085
#define HC_SIMA_HTX0BumpM11     0x0086
#define HC_SIMA_HTX0LScale      0x0087
/*---- end of texture 0 setting ----      0x008f
 */
#define HC_SIMA_TX0TX1_OFF      0x0050
/*---- start of texture 1 setting ----
 */
#define HC_SIMA_HTX1L0BasL      (HC_SIMA_HTX0L0BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L1BasL      (HC_SIMA_HTX0L1BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L2BasL      (HC_SIMA_HTX0L2BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L3BasL      (HC_SIMA_HTX0L3BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L4BasL      (HC_SIMA_HTX0L4BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L5BasL      (HC_SIMA_HTX0L5BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L6BasL      (HC_SIMA_HTX0L6BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L7BasL      (HC_SIMA_HTX0L7BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L8BasL      (HC_SIMA_HTX0L8BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L9BasL      (HC_SIMA_HTX0L9BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LaBasL      (HC_SIMA_HTX0LaBasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LbBasL      (HC_SIMA_HTX0LbBasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LcBasL      (HC_SIMA_HTX0LcBasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LdBasL      (HC_SIMA_HTX0LdBasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LeBasL      (HC_SIMA_HTX0LeBasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LfBasL      (HC_SIMA_HTX0LfBasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L10BasL     (HC_SIMA_HTX0L10BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L11BasL     (HC_SIMA_HTX0L11BasL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L012BasH    (HC_SIMA_HTX0L012BasH + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L345BasH    (HC_SIMA_HTX0L345BasH + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L678BasH    (HC_SIMA_HTX0L678BasH + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L9abBasH    (HC_SIMA_HTX0L9abBasH + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LcdeBasH    (HC_SIMA_HTX0LcdeBasH + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1Lf1011BasH  (HC_SIMA_HTX0Lf1011BasH + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L0Pit       (HC_SIMA_HTX0L0Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L1Pit       (HC_SIMA_HTX0L1Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L2Pit       (HC_SIMA_HTX0L2Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L3Pit       (HC_SIMA_HTX0L3Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L4Pit       (HC_SIMA_HTX0L4Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L5Pit       (HC_SIMA_HTX0L5Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L6Pit       (HC_SIMA_HTX0L6Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L7Pit       (HC_SIMA_HTX0L7Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L8Pit       (HC_SIMA_HTX0L8Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L9Pit       (HC_SIMA_HTX0L9Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LaPit       (HC_SIMA_HTX0LaPit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LbPit       (HC_SIMA_HTX0LbPit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LcPit       (HC_SIMA_HTX0LcPit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LdPit       (HC_SIMA_HTX0LdPit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LePit       (HC_SIMA_HTX0LePit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LfPit       (HC_SIMA_HTX0LfPit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L10Pit      (HC_SIMA_HTX0L10Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L11Pit      (HC_SIMA_HTX0L11Pit + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L0_5WE      (HC_SIMA_HTX0L0_5WE + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L6_bWE      (HC_SIMA_HTX0L6_bWE + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1Lc_11WE     (HC_SIMA_HTX0Lc_11WE + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L0_5HE      (HC_SIMA_HTX0L0_5HE + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L6_bHE      (HC_SIMA_HTX0L6_bHE + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1Lc_11HE      (HC_SIMA_HTX0Lc_11HE + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1L0OS        (HC_SIMA_HTX0L0OS + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TB          (HC_SIMA_HTX0TB + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1MPMD        (HC_SIMA_HTX0MPMD + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1CLODu       (HC_SIMA_HTX0CLODu + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1FM          (HC_SIMA_HTX0FM + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TRCH        (HC_SIMA_HTX0TRCH + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TRCL        (HC_SIMA_HTX0TRCL + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBC         (HC_SIMA_HTX0TBC + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TRAH        (HC_SIMA_HTX0TRAH + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LTC         (HC_SIMA_HTX0LTC + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LTA         (HC_SIMA_HTX0LTA + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLCsat     (HC_SIMA_HTX0TBLCsat + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLCop      (HC_SIMA_HTX0TBLCop + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLMPfog    (HC_SIMA_HTX0TBLMPfog + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLAsat     (HC_SIMA_HTX0TBLAsat + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLRCa      (HC_SIMA_HTX0TBLRCa + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLRCb      (HC_SIMA_HTX0TBLRCb + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLRCc      (HC_SIMA_HTX0TBLRCc + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLRCbias   (HC_SIMA_HTX0TBLRCbias + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLRAa      (HC_SIMA_HTX0TBLRAa + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1TBLRFog     (HC_SIMA_HTX0TBLRFog + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1BumpM00     (HC_SIMA_HTX0BumpM00 + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1BumpM01     (HC_SIMA_HTX0BumpM01 + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1BumpM10     (HC_SIMA_HTX0BumpM10 + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1BumpM11     (HC_SIMA_HTX0BumpM11 + HC_SIMA_TX0TX1_OFF)
#define HC_SIMA_HTX1LScale      (HC_SIMA_HTX0LScale + HC_SIMA_TX0TX1_OFF)
/*---- end of texture 1 setting ---- 0xaf
 */
#define HC_SIMA_HTXSMD          0x00b0
#define HC_SIMA_HenFIFOAT       0x00b1
#define HC_SIMA_HFBDrawFirst    0x00b2
#define HC_SIMA_HFBBasL         0x00b3
#define HC_SIMA_HTArbRCM        0x00b4
#define HC_SIMA_HTArbRZ         0x00b5
#define HC_SIMA_HTArbWZ         0x00b6
#define HC_SIMA_HTArbRTX        0x00b7
#define HC_SIMA_HTArbRCW        0x00b8
#define HC_SIMA_HTArbE2         0x00b9
#define HC_SIMA_HGEMITout       0x00ba
#define HC_SIMA_HFthRTXD        0x00bb
#define HC_SIMA_HFthRTXA        0x00bc
/* Define the texture palette 0
 */
#define HC_SIMA_HTP0            0x0100
#define HC_SIMA_HTP1            0x0200
#define HC_SIMA_FOGTABLE        0x0300
#define HC_SIMA_STIPPLE         0x0400
#define HC_SIMA_HE3Fire         0x0440
#define HC_SIMA_TRANS_SET       0x0441
#define HC_SIMA_HREngSt         0x0442
#define HC_SIMA_HRFIFOempty     0x0443
#define HC_SIMA_HRFIFOfull      0x0444
#define HC_SIMA_HRErr           0x0445
#define HC_SIMA_FIFOstatus      0x0446

/******************************************************************************
** Define the AGP command header.
******************************************************************************/
#define HC_ACMD_MASK            0xfe000000
#define HC_ACMD_SUB_MASK        0x0c000000
#define HC_ACMD_HCmdA           0xee000000
#define HC_ACMD_HCmdB           0xec000000
#define HC_ACMD_HCmdC           0xea000000
#define HC_ACMD_H1              0xf0000000
#define HC_ACMD_H2              0xf2000000
#define HC_ACMD_H3              0xf4000000
#define HC_ACMD_H4              0xf6000000

#define HC_ACMD_H1IO_MASK       0x000001ff
#define HC_ACMD_H2IO1_MASK      0x001ff000
#define HC_ACMD_H2IO2_MASK      0x000001ff
#define HC_ACMD_H2IO1_SHIFT     12
#define HC_ACMD_H2IO2_SHIFT     0
#define HC_ACMD_H3IO_MASK       0x000001ff
#define HC_ACMD_H3COUNT_MASK    0x01fff000
#define HC_ACMD_H3COUNT_SHIFT   12
#define HC_ACMD_H4ID_MASK       0x000001ff
#define HC_ACMD_H4COUNT_MASK    0x01fffe00
#define HC_ACMD_H4COUNT_SHIFT   9

/********************************************************************************
** Define Header
********************************************************************************/
#define HC_HEADER2		0xF210F110

/********************************************************************************
** Define Dummy Value
********************************************************************************/
#define HC_DUMMY		0xCCCCCCCC
/********************************************************************************
** Define for DMA use
********************************************************************************/
#define HALCYON_HEADER2     0XF210F110
#define HALCYON_FIRECMD     0XEE100000
#define HALCYON_FIREMASK    0XFFF00000
#define HALCYON_CMDB        0XEC000000
#define HALCYON_CMDBMASK    0XFFFE0000
#define HALCYON_SUB_ADDR0   0X00000000
#define HALCYON_HEADER1MASK 0XFFFFFC00
#define HALCYON_HEADER1     0XF0000000
#define HC_SubA_HAGPBstL        0x0060
#define HC_SubA_HAGPBendL       0x0061
#define HC_SubA_HAGPCMNT        0x0062
#define HC_SubA_HAGPBpL         0x0063
#define HC_SubA_HAGPBpH         0x0064
#define HC_HAGPCMNT_MASK        0x00800000
#define HC_HCmdErrClr_MASK      0x00400000
#define HC_HAGPBendH_MASK       0x0000ff00
#define HC_HAGPBstH_MASK        0x000000ff
#define HC_HAGPBendH_SHIFT      8
#define HC_HAGPBstH_SHIFT       0
#define HC_HAGPBpL_MASK         0x00fffffc
#define HC_HAGPBpID_MASK        0x00000003
#define HC_HAGPBpID_PAUSE       0x00000000
#define HC_HAGPBpID_JUMP        0x00000001
#define HC_HAGPBpID_STOP        0x00000002
#define HC_HAGPBpH_MASK         0x00ffffff


#define VIA_VIDEO_HEADER5       0xFE040000
#define VIA_VIDEO_HEADER6       0xFE050000
#define VIA_VIDEO_HEADER7       0xFE060000
#define VIA_VIDEOMASK           0xFFFF0000
#endif
