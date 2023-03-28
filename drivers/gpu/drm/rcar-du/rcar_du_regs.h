/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car Display Unit Registers Definitions
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __RCAR_DU_REGS_H__
#define __RCAR_DU_REGS_H__

#define DU0_REG_OFFSET		0x00000
#define DU1_REG_OFFSET		0x30000
#define DU2_REG_OFFSET		0x40000
#define DU3_REG_OFFSET		0x70000

/* -----------------------------------------------------------------------------
 * Display Control Registers
 */

#define DSYSR			0x00000	/* display 1 */
#define DSYSR_ILTS		(1 << 29)
#define DSYSR_DSEC		(1 << 20)
#define DSYSR_IUPD		(1 << 16)
#define DSYSR_DRES		(1 << 9)
#define DSYSR_DEN		(1 << 8)
#define DSYSR_TVM_MASTER	(0 << 6)
#define DSYSR_TVM_SWITCH	(1 << 6)
#define DSYSR_TVM_TVSYNC	(2 << 6)
#define DSYSR_TVM_MASK		(3 << 6)
#define DSYSR_SCM_INT_NONE	(0 << 4)
#define DSYSR_SCM_INT_SYNC	(2 << 4)
#define DSYSR_SCM_INT_VIDEO	(3 << 4)
#define DSYSR_SCM_MASK		(3 << 4)

#define DSMR			0x00004
#define DSMR_VSPM		(1 << 28)
#define DSMR_ODPM		(1 << 27)
#define DSMR_DIPM_DISP		(0 << 25)
#define DSMR_DIPM_CSYNC		(1 << 25)
#define DSMR_DIPM_DE		(3 << 25)
#define DSMR_DIPM_MASK		(3 << 25)
#define DSMR_CSPM		(1 << 24)
#define DSMR_DIL		(1 << 19)
#define DSMR_VSL		(1 << 18)
#define DSMR_HSL		(1 << 17)
#define DSMR_DDIS		(1 << 16)
#define DSMR_CDEL		(1 << 15)
#define DSMR_CDEM_CDE		(0 << 13)
#define DSMR_CDEM_LOW		(2 << 13)
#define DSMR_CDEM_HIGH		(3 << 13)
#define DSMR_CDEM_MASK		(3 << 13)
#define DSMR_CDED		(1 << 12)
#define DSMR_ODEV		(1 << 8)
#define DSMR_CSY_VH_OR		(0 << 6)
#define DSMR_CSY_333		(2 << 6)
#define DSMR_CSY_222		(3 << 6)
#define DSMR_CSY_MASK		(3 << 6)

#define DSSR			0x00008
#define DSSR_VC1FB_DSA0		(0 << 30)
#define DSSR_VC1FB_DSA1		(1 << 30)
#define DSSR_VC1FB_DSA2		(2 << 30)
#define DSSR_VC1FB_INIT		(3 << 30)
#define DSSR_VC1FB_MASK		(3 << 30)
#define DSSR_VC0FB_DSA0		(0 << 28)
#define DSSR_VC0FB_DSA1		(1 << 28)
#define DSSR_VC0FB_DSA2		(2 << 28)
#define DSSR_VC0FB_INIT		(3 << 28)
#define DSSR_VC0FB_MASK		(3 << 28)
#define DSSR_DFB(n)		(1 << ((n)+15))
#define DSSR_TVR		(1 << 15)
#define DSSR_FRM		(1 << 14)
#define DSSR_VBK		(1 << 11)
#define DSSR_RINT		(1 << 9)
#define DSSR_HBK		(1 << 8)
#define DSSR_ADC(n)		(1 << ((n)-1))

#define DSRCR			0x0000c
#define DSRCR_TVCL		(1 << 15)
#define DSRCR_FRCL		(1 << 14)
#define DSRCR_VBCL		(1 << 11)
#define DSRCR_RICL		(1 << 9)
#define DSRCR_HBCL		(1 << 8)
#define DSRCR_ADCL(n)		(1 << ((n)-1))
#define DSRCR_MASK		0x0000cbff

#define DIER			0x00010
#define DIER_TVE		(1 << 15)
#define DIER_FRE		(1 << 14)
#define DIER_VBE		(1 << 11)
#define DIER_RIE		(1 << 9)
#define DIER_HBE		(1 << 8)
#define DIER_ADCE(n)		(1 << ((n)-1))

#define CPCR			0x00014
#define CPCR_CP4CE		(1 << 19)
#define CPCR_CP3CE		(1 << 18)
#define CPCR_CP2CE		(1 << 17)
#define CPCR_CP1CE		(1 << 16)

#define DPPR			0x00018
#define DPPR_DPE(n)		(1 << ((n)*4-1))
#define DPPR_DPS(n, p)		(((p)-1) << DPPR_DPS_SHIFT(n))
#define DPPR_DPS_SHIFT(n)	(((n)-1)*4)
#define DPPR_BPP16		(DPPR_DPE(8) | DPPR_DPS(8, 1))	/* plane1 */
#define DPPR_BPP32_P1		(DPPR_DPE(7) | DPPR_DPS(7, 1))
#define DPPR_BPP32_P2		(DPPR_DPE(8) | DPPR_DPS(8, 2))
#define DPPR_BPP32		(DPPR_BPP32_P1 | DPPR_BPP32_P2)	/* plane1 & 2 */

#define DEFR			0x00020
#define DEFR_CODE		(0x7773 << 16)
#define DEFR_EXSL		(1 << 12)
#define DEFR_EXVL		(1 << 11)
#define DEFR_EXUP		(1 << 5)
#define DEFR_VCUP		(1 << 4)
#define DEFR_DEFE		(1 << 0)

#define DAPCR			0x00024
#define DAPCR_CODE		(0x7773 << 16)
#define DAPCR_AP2E		(1 << 4)
#define DAPCR_AP1E		(1 << 0)

#define DCPCR			0x00028
#define DCPCR_CODE		(0x7773 << 16)
#define DCPCR_CA2B		(1 << 13)
#define DCPCR_CD2F		(1 << 12)
#define DCPCR_DC2E		(1 << 8)
#define DCPCR_CAB		(1 << 5)
#define DCPCR_CDF		(1 << 4)
#define DCPCR_DCE		(1 << 0)

#define DEFR2			0x00034
#define DEFR2_CODE		(0x7775 << 16)
#define DEFR2_DEFE2G		(1 << 0)

#define DEFR3			0x00038
#define DEFR3_CODE		(0x7776 << 16)
#define DEFR3_EVDA		(1 << 14)
#define DEFR3_EVDM_1		(1 << 12)
#define DEFR3_EVDM_2		(2 << 12)
#define DEFR3_EVDM_3		(3 << 12)
#define DEFR3_VMSM2_EMA		(1 << 6)
#define DEFR3_VMSM1_ENA		(1 << 4)
#define DEFR3_DEFE3		(1 << 0)

#define DEFR4			0x0003c
#define DEFR4_CODE		(0x7777 << 16)
#define DEFR4_LRUO		(1 << 5)
#define DEFR4_SPCE		(1 << 4)

#define DVCSR			0x000d0
#define DVCSR_VCnFB2_DSA0(n)	(0 << ((n)*2+16))
#define DVCSR_VCnFB2_DSA1(n)	(1 << ((n)*2+16))
#define DVCSR_VCnFB2_DSA2(n)	(2 << ((n)*2+16))
#define DVCSR_VCnFB2_INIT(n)	(3 << ((n)*2+16))
#define DVCSR_VCnFB2_MASK(n)	(3 << ((n)*2+16))
#define DVCSR_VCnFB_DSA0(n)	(0 << ((n)*2))
#define DVCSR_VCnFB_DSA1(n)	(1 << ((n)*2))
#define DVCSR_VCnFB_DSA2(n)	(2 << ((n)*2))
#define DVCSR_VCnFB_INIT(n)	(3 << ((n)*2))
#define DVCSR_VCnFB_MASK(n)	(3 << ((n)*2))

#define DEFR5			0x000e0
#define DEFR5_CODE		(0x66 << 24)
#define DEFR5_YCRGB2_DIS	(0 << 14)
#define DEFR5_YCRGB2_PRI1	(1 << 14)
#define DEFR5_YCRGB2_PRI2	(2 << 14)
#define DEFR5_YCRGB2_PRI3	(3 << 14)
#define DEFR5_YCRGB2_MASK	(3 << 14)
#define DEFR5_YCRGB1_DIS	(0 << 12)
#define DEFR5_YCRGB1_PRI1	(1 << 12)
#define DEFR5_YCRGB1_PRI2	(2 << 12)
#define DEFR5_YCRGB1_PRI3	(3 << 12)
#define DEFR5_YCRGB1_MASK	(3 << 12)
#define DEFR5_DEFE5		(1 << 0)

#define DDLTR			0x000e4
#define DDLTR_CODE		(0x7766 << 16)
#define DDLTR_DLAR2		(1 << 6)
#define DDLTR_DLAY2		(1 << 5)
#define DDLTR_DLAY1		(1 << 1)

#define DEFR6			0x000e8
#define DEFR6_CODE		(0x7778 << 16)
#define DEFR6_ODPM12_DSMR	(0 << 10)
#define DEFR6_ODPM12_DISP	(2 << 10)
#define DEFR6_ODPM12_CDE	(3 << 10)
#define DEFR6_ODPM12_MASK	(3 << 10)
#define DEFR6_ODPM02_DSMR	(0 << 8)
#define DEFR6_ODPM02_DISP	(2 << 8)
#define DEFR6_ODPM02_CDE	(3 << 8)
#define DEFR6_ODPM02_MASK	(3 << 8)
#define DEFR6_TCNE1		(1 << 6)
#define DEFR6_TCNE0		(1 << 4)
#define DEFR6_MLOS1		(1 << 2)
#define DEFR6_DEFAULT		(DEFR6_CODE | DEFR6_TCNE1)

#define DEFR7			0x000ec
#define DEFR7_CODE		(0x7779 << 16)
#define DEFR7_CMME1		BIT(6)
#define DEFR7_CMME0		BIT(4)

/* -----------------------------------------------------------------------------
 * R8A7790-only Control Registers
 */

#define DD1SSR			0x20008
#define DD1SSR_TVR		(1 << 15)
#define DD1SSR_FRM		(1 << 14)
#define DD1SSR_BUF		(1 << 12)
#define DD1SSR_VBK		(1 << 11)
#define DD1SSR_RINT		(1 << 9)
#define DD1SSR_HBK		(1 << 8)
#define DD1SSR_ADC(n)		(1 << ((n)-1))

#define DD1SRCR			0x2000c
#define DD1SRCR_TVR		(1 << 15)
#define DD1SRCR_FRM		(1 << 14)
#define DD1SRCR_BUF		(1 << 12)
#define DD1SRCR_VBK		(1 << 11)
#define DD1SRCR_RINT		(1 << 9)
#define DD1SRCR_HBK		(1 << 8)
#define DD1SRCR_ADC(n)		(1 << ((n)-1))

#define DD1IER			0x20010
#define DD1IER_TVR		(1 << 15)
#define DD1IER_FRM		(1 << 14)
#define DD1IER_BUF		(1 << 12)
#define DD1IER_VBK		(1 << 11)
#define DD1IER_RINT		(1 << 9)
#define DD1IER_HBK		(1 << 8)
#define DD1IER_ADC(n)		(1 << ((n)-1))

#define DEFR8			0x20020
#define DEFR8_CODE		(0x7790 << 16)
#define DEFR8_VSCS		(1 << 6)
#define DEFR8_DRGBS_DU(n)	((n) << 4)
#define DEFR8_DRGBS_MASK	(3 << 4)
#define DEFR8_DEFE8		(1 << 0)

#define DOFLR			0x20024
#define DOFLR_CODE		(0x7790 << 16)
#define DOFLR_HSYCFL1		(1 << 13)
#define DOFLR_VSYCFL1		(1 << 12)
#define DOFLR_ODDFL1		(1 << 11)
#define DOFLR_DISPFL1		(1 << 10)
#define DOFLR_CDEFL1		(1 << 9)
#define DOFLR_RGBFL1		(1 << 8)
#define DOFLR_HSYCFL0		(1 << 5)
#define DOFLR_VSYCFL0		(1 << 4)
#define DOFLR_ODDFL0		(1 << 3)
#define DOFLR_DISPFL0		(1 << 2)
#define DOFLR_CDEFL0		(1 << 1)
#define DOFLR_RGBFL0		(1 << 0)

#define DIDSR			0x20028
#define DIDSR_CODE		(0x7790 << 16)
#define DIDSR_LDCS_DCLKIN(n)	(0 << (8 + (n) * 2))
#define DIDSR_LDCS_DSI(n)	(2 << (8 + (n) * 2))	/* V3U only */
#define DIDSR_LDCS_LVDS0(n)	(2 << (8 + (n) * 2))
#define DIDSR_LDCS_LVDS1(n)	(3 << (8 + (n) * 2))
#define DIDSR_LDCS_MASK(n)	(3 << (8 + (n) * 2))
#define DIDSR_PDCS_CLK(n, clk)	(clk << ((n) * 2))
#define DIDSR_PDCS_MASK(n)	(3 << ((n) * 2))

#define DEFR10			0x20038
#define DEFR10_CODE		(0x7795 << 16)
#define DEFR10_VSPF1_RGB	(0 << 14)
#define DEFR10_VSPF1_YC		(1 << 14)
#define DEFR10_DOCF1_RGB	(0 << 12)
#define DEFR10_DOCF1_YC		(1 << 12)
#define DEFR10_YCDF0_YCBCR444	(0 << 11)
#define DEFR10_YCDF0_YCBCR422	(1 << 11)
#define DEFR10_VSPF0_RGB	(0 << 10)
#define DEFR10_VSPF0_YC		(1 << 10)
#define DEFR10_DOCF0_RGB	(0 << 8)
#define DEFR10_DOCF0_YC		(1 << 8)
#define DEFR10_TSEL_H3_TCON1	(0 << 1) /* DEFR102 register only (DU2/DU3) */
#define DEFR10_DEFE10		(1 << 0)

#define DPLLCR			0x20044
#define DPLLCR_CODE		(0x95 << 24)
#define DPLLCR_PLCS1		(1 << 23)
#define DPLLCR_PLCS0_PLL	(1 << 21)
#define DPLLCR_PLCS0_H3ES1X_PLL1	(1 << 20)
#define DPLLCR_CLKE		(1 << 18)
#define DPLLCR_FDPLL(n)		((n) << 12)
#define DPLLCR_N(n)		((n) << 5)
#define DPLLCR_M(n)		((n) << 3)
#define DPLLCR_STBY		(1 << 2)
#define DPLLCR_INCS_DOTCLKIN0	(0 << 0)
#define DPLLCR_INCS_DOTCLKIN1	(1 << 1)

#define DPLLC2R			0x20048
#define DPLLC2R_CODE		(0x95 << 24)
#define DPLLC2R_SELC		(1 << 12)
#define DPLLC2R_M(n)		((n) << 8)
#define DPLLC2R_FDPLL(n)	((n) << 0)

/* -----------------------------------------------------------------------------
 * Display Timing Generation Registers
 */

#define HDSR			0x00040
#define HDER			0x00044
#define VDSR			0x00048
#define VDER			0x0004c
#define HCR			0x00050
#define HSWR			0x00054
#define VCR			0x00058
#define VSPR			0x0005c
#define EQWR			0x00060
#define SPWR			0x00064
#define CLAMPSR			0x00070
#define CLAMPWR			0x00074
#define DESR			0x00078
#define DEWR			0x0007c

/* -----------------------------------------------------------------------------
 * Display Attribute Registers
 */

#define CP1TR			0x00080
#define CP2TR			0x00084
#define CP3TR			0x00088
#define CP4TR			0x0008c

#define DOOR			0x00090
#define DOOR_RGB(r, g, b)	(((r) << 18) | ((g) << 10) | ((b) << 2))
#define CDER			0x00094
#define CDER_RGB(r, g, b)	(((r) << 18) | ((g) << 10) | ((b) << 2))
#define BPOR			0x00098
#define BPOR_RGB(r, g, b)	(((r) << 18) | ((g) << 10) | ((b) << 2))

#define RINTOFSR		0x0009c

#define DSHPR			0x000c8
#define DSHPR_CODE		(0x7776 << 16)
#define DSHPR_PRIH		(0xa << 4)
#define DSHPR_PRIL_BPP16	(0x8 << 0)
#define DSHPR_PRIL_BPP32	(0x9 << 0)

/* -----------------------------------------------------------------------------
 * Display Plane Registers
 */

#define PLANE_OFF		0x00100

#define PnMR			0x00100 /* plane 1 */
#define PnMR_VISL_VIN0		(0 << 26)	/* use Video Input 0 */
#define PnMR_VISL_VIN1		(1 << 26)	/* use Video Input 1 */
#define PnMR_VISL_VIN2		(2 << 26)	/* use Video Input 2 */
#define PnMR_VISL_VIN3		(3 << 26)	/* use Video Input 3 */
#define PnMR_YCDF_YUYV		(1 << 20)	/* YUYV format */
#define PnMR_TC_R		(0 << 17)	/* Tranparent color is PnTC1R */
#define PnMR_TC_CP		(1 << 17)	/* Tranparent color is color palette */
#define PnMR_WAE		(1 << 16)	/* Wrap around Enable */
#define PnMR_SPIM_TP		(0 << 12)	/* Transparent Color */
#define PnMR_SPIM_ALP		(1 << 12)	/* Alpha Blending */
#define PnMR_SPIM_EOR		(2 << 12)	/* EOR */
#define PnMR_SPIM_TP_OFF	(1 << 14)	/* No Transparent Color */
#define PnMR_CPSL_CP1		(0 << 8)	/* Color Palette selected 1 */
#define PnMR_CPSL_CP2		(1 << 8)	/* Color Palette selected 2 */
#define PnMR_CPSL_CP3		(2 << 8)	/* Color Palette selected 3 */
#define PnMR_CPSL_CP4		(3 << 8)	/* Color Palette selected 4 */
#define PnMR_DC			(1 << 7)	/* Display Area Change */
#define PnMR_BM_MD		(0 << 4)	/* Manual Display Change Mode */
#define PnMR_BM_AR		(1 << 4)	/* Auto Rendering Mode */
#define PnMR_BM_AD		(2 << 4)	/* Auto Display Change Mode */
#define PnMR_BM_VC		(3 << 4)	/* Video Capture Mode */
#define PnMR_DDDF_8BPP		(0 << 0)	/* 8bit */
#define PnMR_DDDF_16BPP		(1 << 0)	/* 16bit or 32bit */
#define PnMR_DDDF_ARGB		(2 << 0)	/* ARGB */
#define PnMR_DDDF_YC		(3 << 0)	/* YC */
#define PnMR_DDDF_MASK		(3 << 0)

#define PnMWR			0x00104

#define PnALPHAR		0x00108
#define PnALPHAR_ABIT_1		(0 << 12)
#define PnALPHAR_ABIT_0		(1 << 12)
#define PnALPHAR_ABIT_X		(2 << 12)

#define PnDSXR			0x00110
#define PnDSYR			0x00114
#define PnDPXR			0x00118
#define PnDPYR			0x0011c

#define PnDSA0R			0x00120
#define PnDSA1R			0x00124
#define PnDSA2R			0x00128
#define PnDSA_MASK		0xfffffff0

#define PnSPXR			0x00130
#define PnSPYR			0x00134
#define PnWASPR			0x00138
#define PnWAMWR			0x0013c

#define PnBTR			0x00140

#define PnTC1R			0x00144
#define PnTC2R			0x00148
#define PnTC3R			0x0014c
#define PnTC3R_CODE		(0x66 << 24)

#define PnMLR			0x00150

#define PnSWAPR			0x00180
#define PnSWAPR_DIGN		(1 << 4)
#define PnSWAPR_SPQW		(1 << 3)
#define PnSWAPR_SPLW		(1 << 2)
#define PnSWAPR_SPWD		(1 << 1)
#define PnSWAPR_SPBY		(1 << 0)

#define PnDDCR			0x00184
#define PnDDCR_CODE		(0x7775 << 16)
#define PnDDCR_LRGB1		(1 << 11)
#define PnDDCR_LRGB0		(1 << 10)

#define PnDDCR2			0x00188
#define PnDDCR2_CODE		(0x7776 << 16)
#define PnDDCR2_NV21		(1 << 5)
#define PnDDCR2_Y420		(1 << 4)
#define PnDDCR2_DIVU		(1 << 1)
#define PnDDCR2_DIVY		(1 << 0)

#define PnDDCR4			0x00190
#define PnDDCR4_CODE		(0x7766 << 16)
#define PnDDCR4_VSPS		(1 << 13)
#define PnDDCR4_SDFS_RGB	(0 << 4)
#define PnDDCR4_SDFS_YC		(5 << 4)
#define PnDDCR4_SDFS_MASK	(7 << 4)
#define PnDDCR4_EDF_NONE	(0 << 0)
#define PnDDCR4_EDF_ARGB8888	(1 << 0)
#define PnDDCR4_EDF_RGB888	(2 << 0)
#define PnDDCR4_EDF_RGB666	(3 << 0)
#define PnDDCR4_EDF_MASK	(7 << 0)

#define APnMR			0x0a100
#define APnMR_WAE		(1 << 16)	/* Wrap around Enable */
#define APnMR_DC		(1 << 7)	/* Display Area Change */
#define APnMR_BM_MD		(0 << 4)	/* Manual Display Change Mode */
#define APnMR_BM_AD		(2 << 4)	/* Auto Display Change Mode */

#define APnMWR			0x0a104

#define APnDSXR			0x0a110
#define APnDSYR			0x0a114
#define APnDPXR			0x0a118
#define APnDPYR			0x0a11c

#define APnDSA0R		0x0a120
#define APnDSA1R		0x0a124
#define APnDSA2R		0x0a128

#define APnSPXR			0x0a130
#define APnSPYR			0x0a134
#define APnWASPR		0x0a138
#define APnWAMWR		0x0a13c

#define APnBTR			0x0a140

#define APnMLR			0x0a150
#define APnSWAPR		0x0a180

/* -----------------------------------------------------------------------------
 * Display Capture Registers
 */

#define DCMR			0x0c100
#define DCMWR			0x0c104
#define DCSAR			0x0c120
#define DCMLR			0x0c150

/* -----------------------------------------------------------------------------
 * Color Palette Registers
 */

#define CP1_000R		0x01000
#define CP1_255R		0x013fc
#define CP2_000R		0x02000
#define CP2_255R		0x023fc
#define CP3_000R		0x03000
#define CP3_255R		0x033fc
#define CP4_000R		0x04000
#define CP4_255R		0x043fc

/* -----------------------------------------------------------------------------
 * External Synchronization Control Registers
 */

#define ESCR02			0x10000
#define ESCR13			0x01000
#define ESCR_DCLKOINV		(1 << 25)
#define ESCR_DCLKSEL_DCLKIN	(0 << 20)
#define ESCR_DCLKSEL_CLKS	(1 << 20)
#define ESCR_DCLKSEL_MASK	(1 << 20)
#define ESCR_DCLKDIS		(1 << 16)
#define ESCR_SYNCSEL_OFF	(0 << 8)
#define ESCR_SYNCSEL_EXVSYNC	(2 << 8)
#define ESCR_SYNCSEL_EXHSYNC	(3 << 8)
#define ESCR_FRQSEL_MASK	(0x3f << 0)

#define OTAR02			0x10004
#define OTAR13			0x01004

/* -----------------------------------------------------------------------------
 * Dual Display Output Control Registers
 */

#define DORCR			0x11000
#define DORCR_PG1T		(1 << 30)
#define DORCR_DK1S		(1 << 28)
#define DORCR_PG1D_DS0		(0 << 24)
#define DORCR_PG1D_DS1		(1 << 24)
#define DORCR_PG1D_FIX0		(2 << 24)
#define DORCR_PG1D_DOOR		(3 << 24)
#define DORCR_PG1D_MASK		(3 << 24)
#define DORCR_DR0D		(1 << 21)
#define DORCR_PG0D_DS0		(0 << 16)
#define DORCR_PG0D_DS1		(1 << 16)
#define DORCR_PG0D_FIX0		(2 << 16)
#define DORCR_PG0D_DOOR		(3 << 16)
#define DORCR_PG0D_MASK		(3 << 16)
#define DORCR_RGPV		(1 << 4)
#define DORCR_DPRS		(1 << 0)

#define DPTSR			0x11004
#define DPTSR_PnDK(n)		(1 << ((n) + 16))
#define DPTSR_PnTS(n)		(1 << (n))

#define DAPTSR			0x11008
#define DAPTSR_APnDK(n)		(1 << ((n) + 16))
#define DAPTSR_APnTS(n)		(1 << (n))

#define DS1PR			0x11020
#define DS2PR			0x11024

/* -----------------------------------------------------------------------------
 * YC-RGB Conversion Coefficient Registers
 */

#define YNCR			0x11080
#define YNOR			0x11084
#define CRNOR			0x11088
#define CBNOR			0x1108c
#define RCRCR			0x11090
#define GCRCR			0x11094
#define GCBCR			0x11098
#define BCBCR			0x1109c

#endif /* __RCAR_DU_REGS_H__ */
