/*
 * vsp1_regs.h  --  R-Car VSP1 Registers Definitions
 *
 * Copyright (C) 2013 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#ifndef __VSP1_REGS_H__
#define __VSP1_REGS_H__

/* -----------------------------------------------------------------------------
 * General Control Registers
 */

#define VI6_CMD(n)			(0x0000 + (n) * 4)
#define VI6_CMD_UPDHDR			(1 << 4)
#define VI6_CMD_STRCMD			(1 << 0)

#define VI6_CLK_DCSWT			0x0018
#define VI6_CLK_DCSWT_CSTPW_MASK	(0xff << 8)
#define VI6_CLK_DCSWT_CSTPW_SHIFT	8
#define VI6_CLK_DCSWT_CSTRW_MASK	(0xff << 0)
#define VI6_CLK_DCSWT_CSTRW_SHIFT	0

#define VI6_SRESET			0x0028
#define VI6_SRESET_SRTS(n)		(1 << (n))

#define VI6_STATUS			0x0038
#define VI6_STATUS_SYS_ACT(n)		(1 << ((n) + 8))

#define VI6_WPF_IRQ_ENB(n)		(0x0048 + (n) * 12)
#define VI6_WFP_IRQ_ENB_DFEE		(1 << 1)
#define VI6_WFP_IRQ_ENB_FREE		(1 << 0)

#define VI6_WPF_IRQ_STA(n)		(0x004c + (n) * 12)
#define VI6_WFP_IRQ_STA_DFE		(1 << 1)
#define VI6_WFP_IRQ_STA_FRE		(1 << 0)

#define VI6_DISP_IRQ_ENB		0x0078
#define VI6_DISP_IRQ_ENB_DSTE		(1 << 8)
#define VI6_DISP_IRQ_ENB_MAEE		(1 << 5)
#define VI6_DISP_IRQ_ENB_LNEE(n)	(1 << (n))

#define VI6_DISP_IRQ_STA		0x007c
#define VI6_DISP_IRQ_STA_DST		(1 << 8)
#define VI6_DISP_IRQ_STA_MAE		(1 << 5)
#define VI6_DISP_IRQ_STA_LNE(n)		(1 << (n))

#define VI6_WPF_LINE_COUNT(n)		(0x0084 + (n) * 4)
#define VI6_WPF_LINE_COUNT_MASK		(0x1fffff << 0)

/* -----------------------------------------------------------------------------
 * Display List Control Registers
 */

#define VI6_DL_CTRL			0x0100
#define VI6_DL_CTRL_AR_WAIT_MASK	(0xffff << 16)
#define VI6_DL_CTRL_AR_WAIT_SHIFT	16
#define VI6_DL_CTRL_DC2			(1 << 12)
#define VI6_DL_CTRL_DC1			(1 << 8)
#define VI6_DL_CTRL_DC0			(1 << 4)
#define VI6_DL_CTRL_CFM0		(1 << 2)
#define VI6_DL_CTRL_NH0			(1 << 1)
#define VI6_DL_CTRL_DLE			(1 << 0)

#define VI6_DL_HDR_ADDR(n)		(0x0104 + (n) * 4)

#define VI6_DL_SWAP			0x0114
#define VI6_DL_SWAP_LWS			(1 << 2)
#define VI6_DL_SWAP_WDS			(1 << 1)
#define VI6_DL_SWAP_BTS			(1 << 0)

#define VI6_DL_EXT_CTRL			0x011c
#define VI6_DL_EXT_CTRL_NWE		(1 << 16)
#define VI6_DL_EXT_CTRL_POLINT_MASK	(0x3f << 8)
#define VI6_DL_EXT_CTRL_POLINT_SHIFT	8
#define VI6_DL_EXT_CTRL_DLPRI		(1 << 5)
#define VI6_DL_EXT_CTRL_EXPRI		(1 << 4)
#define VI6_DL_EXT_CTRL_EXT		(1 << 0)

#define VI6_DL_BODY_SIZE		0x0120
#define VI6_DL_BODY_SIZE_UPD		(1 << 24)
#define VI6_DL_BODY_SIZE_BS_MASK	(0x1ffff << 0)
#define VI6_DL_BODY_SIZE_BS_SHIFT	0

/* -----------------------------------------------------------------------------
 * RPF Control Registers
 */

#define VI6_RPF_OFFSET			0x100

#define VI6_RPF_SRC_BSIZE		0x0300
#define VI6_RPF_SRC_BSIZE_BHSIZE_MASK	(0x1fff << 16)
#define VI6_RPF_SRC_BSIZE_BHSIZE_SHIFT	16
#define VI6_RPF_SRC_BSIZE_BVSIZE_MASK	(0x1fff << 0)
#define VI6_RPF_SRC_BSIZE_BVSIZE_SHIFT	0

#define VI6_RPF_SRC_ESIZE		0x0304
#define VI6_RPF_SRC_ESIZE_EHSIZE_MASK	(0x1fff << 16)
#define VI6_RPF_SRC_ESIZE_EHSIZE_SHIFT	16
#define VI6_RPF_SRC_ESIZE_EVSIZE_MASK	(0x1fff << 0)
#define VI6_RPF_SRC_ESIZE_EVSIZE_SHIFT	0

#define VI6_RPF_INFMT			0x0308
#define VI6_RPF_INFMT_VIR		(1 << 28)
#define VI6_RPF_INFMT_CIPM		(1 << 16)
#define VI6_RPF_INFMT_SPYCS		(1 << 15)
#define VI6_RPF_INFMT_SPUVS		(1 << 14)
#define VI6_RPF_INFMT_CEXT_ZERO		(0 << 12)
#define VI6_RPF_INFMT_CEXT_EXT		(1 << 12)
#define VI6_RPF_INFMT_CEXT_ONE		(2 << 12)
#define VI6_RPF_INFMT_CEXT_MASK		(3 << 12)
#define VI6_RPF_INFMT_RDTM_BT601	(0 << 9)
#define VI6_RPF_INFMT_RDTM_BT601_EXT	(1 << 9)
#define VI6_RPF_INFMT_RDTM_BT709	(2 << 9)
#define VI6_RPF_INFMT_RDTM_BT709_EXT	(3 << 9)
#define VI6_RPF_INFMT_RDTM_MASK		(7 << 9)
#define VI6_RPF_INFMT_CSC		(1 << 8)
#define VI6_RPF_INFMT_RDFMT_MASK	(0x7f << 0)
#define VI6_RPF_INFMT_RDFMT_SHIFT	0

#define VI6_RPF_DSWAP			0x030c
#define VI6_RPF_DSWAP_A_LLS		(1 << 11)
#define VI6_RPF_DSWAP_A_LWS		(1 << 10)
#define VI6_RPF_DSWAP_A_WDS		(1 << 9)
#define VI6_RPF_DSWAP_A_BTS		(1 << 8)
#define VI6_RPF_DSWAP_P_LLS		(1 << 3)
#define VI6_RPF_DSWAP_P_LWS		(1 << 2)
#define VI6_RPF_DSWAP_P_WDS		(1 << 1)
#define VI6_RPF_DSWAP_P_BTS		(1 << 0)

#define VI6_RPF_LOC			0x0310
#define VI6_RPF_LOC_HCOORD_MASK		(0x1fff << 16)
#define VI6_RPF_LOC_HCOORD_SHIFT	16
#define VI6_RPF_LOC_VCOORD_MASK		(0x1fff << 0)
#define VI6_RPF_LOC_VCOORD_SHIFT	0

#define VI6_RPF_ALPH_SEL		0x0314
#define VI6_RPF_ALPH_SEL_ASEL_PACKED	(0 << 28)
#define VI6_RPF_ALPH_SEL_ASEL_8B_PLANE	(1 << 28)
#define VI6_RPF_ALPH_SEL_ASEL_SELECT	(2 << 28)
#define VI6_RPF_ALPH_SEL_ASEL_1B_PLANE	(3 << 28)
#define VI6_RPF_ALPH_SEL_ASEL_FIXED	(4 << 28)
#define VI6_RPF_ALPH_SEL_ASEL_MASK	(7 << 28)
#define VI6_RPF_ALPH_SEL_ASEL_SHIFT	28
#define VI6_RPF_ALPH_SEL_IROP_MASK	(0xf << 24)
#define VI6_RPF_ALPH_SEL_IROP_SHIFT	24
#define VI6_RPF_ALPH_SEL_BSEL		(1 << 23)
#define VI6_RPF_ALPH_SEL_AEXT_ZERO	(0 << 18)
#define VI6_RPF_ALPH_SEL_AEXT_EXT	(1 << 18)
#define VI6_RPF_ALPH_SEL_AEXT_ONE	(2 << 18)
#define VI6_RPF_ALPH_SEL_AEXT_MASK	(3 << 18)
#define VI6_RPF_ALPH_SEL_ALPHA1_MASK	(0xff << 8)
#define VI6_RPF_ALPH_SEL_ALPHA1_SHIFT	8
#define VI6_RPF_ALPH_SEL_ALPHA0_MASK	(0xff << 0)
#define VI6_RPF_ALPH_SEL_ALPHA0_SHIFT	0

#define VI6_RPF_VRTCOL_SET		0x0318
#define VI6_RPF_VRTCOL_SET_LAYA_MASK	(0xff << 24)
#define VI6_RPF_VRTCOL_SET_LAYA_SHIFT	24
#define VI6_RPF_VRTCOL_SET_LAYR_MASK	(0xff << 16)
#define VI6_RPF_VRTCOL_SET_LAYR_SHIFT	16
#define VI6_RPF_VRTCOL_SET_LAYG_MASK	(0xff << 8)
#define VI6_RPF_VRTCOL_SET_LAYG_SHIFT	8
#define VI6_RPF_VRTCOL_SET_LAYB_MASK	(0xff << 0)
#define VI6_RPF_VRTCOL_SET_LAYB_SHIFT	0

#define VI6_RPF_MSK_CTRL		0x031c
#define VI6_RPF_MSK_CTRL_MSK_EN		(1 << 24)
#define VI6_RPF_MSK_CTRL_MGR_MASK	(0xff << 16)
#define VI6_RPF_MSK_CTRL_MGR_SHIFT	16
#define VI6_RPF_MSK_CTRL_MGG_MASK	(0xff << 8)
#define VI6_RPF_MSK_CTRL_MGG_SHIFT	8
#define VI6_RPF_MSK_CTRL_MGB_MASK	(0xff << 0)
#define VI6_RPF_MSK_CTRL_MGB_SHIFT	0

#define VI6_RPF_MSK_SET0		0x0320
#define VI6_RPF_MSK_SET1		0x0324
#define VI6_RPF_MSK_SET_MSA_MASK	(0xff << 24)
#define VI6_RPF_MSK_SET_MSA_SHIFT	24
#define VI6_RPF_MSK_SET_MSR_MASK	(0xff << 16)
#define VI6_RPF_MSK_SET_MSR_SHIFT	16
#define VI6_RPF_MSK_SET_MSG_MASK	(0xff << 8)
#define VI6_RPF_MSK_SET_MSG_SHIFT	8
#define VI6_RPF_MSK_SET_MSB_MASK	(0xff << 0)
#define VI6_RPF_MSK_SET_MSB_SHIFT	0

#define VI6_RPF_CKEY_CTRL		0x0328
#define VI6_RPF_CKEY_CTRL_CV		(1 << 4)
#define VI6_RPF_CKEY_CTRL_SAPE1		(1 << 1)
#define VI6_RPF_CKEY_CTRL_SAPE0		(1 << 0)

#define VI6_RPF_CKEY_SET0		0x032c
#define VI6_RPF_CKEY_SET1		0x0330
#define VI6_RPF_CKEY_SET_AP_MASK	(0xff << 24)
#define VI6_RPF_CKEY_SET_AP_SHIFT	24
#define VI6_RPF_CKEY_SET_R_MASK		(0xff << 16)
#define VI6_RPF_CKEY_SET_R_SHIFT	16
#define VI6_RPF_CKEY_SET_GY_MASK	(0xff << 8)
#define VI6_RPF_CKEY_SET_GY_SHIFT	8
#define VI6_RPF_CKEY_SET_B_MASK		(0xff << 0)
#define VI6_RPF_CKEY_SET_B_SHIFT	0

#define VI6_RPF_SRCM_PSTRIDE		0x0334
#define VI6_RPF_SRCM_PSTRIDE_Y_SHIFT	16
#define VI6_RPF_SRCM_PSTRIDE_C_SHIFT	0

#define VI6_RPF_SRCM_ASTRIDE		0x0338
#define VI6_RPF_SRCM_PSTRIDE_A_SHIFT	0

#define VI6_RPF_SRCM_ADDR_Y		0x033c
#define VI6_RPF_SRCM_ADDR_C0		0x0340
#define VI6_RPF_SRCM_ADDR_C1		0x0344
#define VI6_RPF_SRCM_ADDR_AI		0x0348

#define VI6_RPF_MULT_ALPHA		0x036c
#define VI6_RPF_MULT_ALPHA_A_MMD_NONE	(0 << 12)
#define VI6_RPF_MULT_ALPHA_A_MMD_RATIO	(1 << 12)
#define VI6_RPF_MULT_ALPHA_P_MMD_NONE	(0 << 8)
#define VI6_RPF_MULT_ALPHA_P_MMD_RATIO	(1 << 8)
#define VI6_RPF_MULT_ALPHA_P_MMD_IMAGE	(2 << 8)
#define VI6_RPF_MULT_ALPHA_P_MMD_BOTH	(3 << 8)
#define VI6_RPF_MULT_ALPHA_RATIO_MASK	(0xff < 0)
#define VI6_RPF_MULT_ALPHA_RATIO_SHIFT	0

/* -----------------------------------------------------------------------------
 * WPF Control Registers
 */

#define VI6_WPF_OFFSET			0x100

#define VI6_WPF_SRCRPF			0x1000
#define VI6_WPF_SRCRPF_VIRACT_DIS	(0 << 28)
#define VI6_WPF_SRCRPF_VIRACT_SUB	(1 << 28)
#define VI6_WPF_SRCRPF_VIRACT_MST	(2 << 28)
#define VI6_WPF_SRCRPF_VIRACT_MASK	(3 << 28)
#define VI6_WPF_SRCRPF_VIRACT2_DIS	(0 << 24)
#define VI6_WPF_SRCRPF_VIRACT2_SUB	(1 << 24)
#define VI6_WPF_SRCRPF_VIRACT2_MST	(2 << 24)
#define VI6_WPF_SRCRPF_VIRACT2_MASK	(3 << 24)
#define VI6_WPF_SRCRPF_RPF_ACT_DIS(n)	(0 << ((n) * 2))
#define VI6_WPF_SRCRPF_RPF_ACT_SUB(n)	(1 << ((n) * 2))
#define VI6_WPF_SRCRPF_RPF_ACT_MST(n)	(2 << ((n) * 2))
#define VI6_WPF_SRCRPF_RPF_ACT_MASK(n)	(3 << ((n) * 2))

#define VI6_WPF_HSZCLIP			0x1004
#define VI6_WPF_VSZCLIP			0x1008
#define VI6_WPF_SZCLIP_EN		(1 << 28)
#define VI6_WPF_SZCLIP_OFST_MASK	(0xff << 16)
#define VI6_WPF_SZCLIP_OFST_SHIFT	16
#define VI6_WPF_SZCLIP_SIZE_MASK	(0xfff << 0)
#define VI6_WPF_SZCLIP_SIZE_SHIFT	0

#define VI6_WPF_OUTFMT			0x100c
#define VI6_WPF_OUTFMT_PDV_MASK		(0xff << 24)
#define VI6_WPF_OUTFMT_PDV_SHIFT	24
#define VI6_WPF_OUTFMT_PXA		(1 << 23)
#define VI6_WPF_OUTFMT_ROT		(1 << 18)
#define VI6_WPF_OUTFMT_HFLP		(1 << 17)
#define VI6_WPF_OUTFMT_FLP		(1 << 16)
#define VI6_WPF_OUTFMT_SPYCS		(1 << 15)
#define VI6_WPF_OUTFMT_SPUVS		(1 << 14)
#define VI6_WPF_OUTFMT_DITH_DIS		(0 << 12)
#define VI6_WPF_OUTFMT_DITH_EN		(3 << 12)
#define VI6_WPF_OUTFMT_DITH_MASK	(3 << 12)
#define VI6_WPF_OUTFMT_WRTM_BT601	(0 << 9)
#define VI6_WPF_OUTFMT_WRTM_BT601_EXT	(1 << 9)
#define VI6_WPF_OUTFMT_WRTM_BT709	(2 << 9)
#define VI6_WPF_OUTFMT_WRTM_BT709_EXT	(3 << 9)
#define VI6_WPF_OUTFMT_WRTM_MASK	(7 << 9)
#define VI6_WPF_OUTFMT_CSC		(1 << 8)
#define VI6_WPF_OUTFMT_WRFMT_MASK	(0x7f << 0)
#define VI6_WPF_OUTFMT_WRFMT_SHIFT	0

#define VI6_WPF_DSWAP			0x1010
#define VI6_WPF_DSWAP_P_LLS		(1 << 3)
#define VI6_WPF_DSWAP_P_LWS		(1 << 2)
#define VI6_WPF_DSWAP_P_WDS		(1 << 1)
#define VI6_WPF_DSWAP_P_BTS		(1 << 0)

#define VI6_WPF_RNDCTRL			0x1014
#define VI6_WPF_RNDCTRL_CBRM		(1 << 28)
#define VI6_WPF_RNDCTRL_ABRM_TRUNC	(0 << 24)
#define VI6_WPF_RNDCTRL_ABRM_ROUND	(1 << 24)
#define VI6_WPF_RNDCTRL_ABRM_THRESH	(2 << 24)
#define VI6_WPF_RNDCTRL_ABRM_MASK	(3 << 24)
#define VI6_WPF_RNDCTRL_ATHRESH_MASK	(0xff << 16)
#define VI6_WPF_RNDCTRL_ATHRESH_SHIFT	16
#define VI6_WPF_RNDCTRL_CLMD_FULL	(0 << 12)
#define VI6_WPF_RNDCTRL_CLMD_CLIP	(1 << 12)
#define VI6_WPF_RNDCTRL_CLMD_EXT	(2 << 12)
#define VI6_WPF_RNDCTRL_CLMD_MASK	(3 << 12)

#define VI6_WPF_ROT_CTRL		0x1018
#define VI6_WPF_ROT_CTRL_LN16		(1 << 17)
#define VI6_WPF_ROT_CTRL_LMEM_WD_MASK	(0x1fff << 0)
#define VI6_WPF_ROT_CTRL_LMEM_WD_SHIFT	0

#define VI6_WPF_DSTM_STRIDE_Y		0x101c
#define VI6_WPF_DSTM_STRIDE_C		0x1020
#define VI6_WPF_DSTM_ADDR_Y		0x1024
#define VI6_WPF_DSTM_ADDR_C0		0x1028
#define VI6_WPF_DSTM_ADDR_C1		0x102c

#define VI6_WPF_WRBCK_CTRL		0x1034
#define VI6_WPF_WRBCK_CTRL_WBMD		(1 << 0)

/* -----------------------------------------------------------------------------
 * DPR Control Registers
 */

#define VI6_DPR_RPF_ROUTE(n)		(0x2000 + (n) * 4)

#define VI6_DPR_WPF_FPORCH(n)		(0x2014 + (n) * 4)
#define VI6_DPR_WPF_FPORCH_FP_WPFN	(5 << 8)

#define VI6_DPR_SRU_ROUTE		0x2024
#define VI6_DPR_UDS_ROUTE(n)		(0x2028 + (n) * 4)
#define VI6_DPR_LUT_ROUTE		0x203c
#define VI6_DPR_CLU_ROUTE		0x2040
#define VI6_DPR_HST_ROUTE		0x2044
#define VI6_DPR_HSI_ROUTE		0x2048
#define VI6_DPR_BRU_ROUTE		0x204c
#define VI6_DPR_ILV_BRS_ROUTE		0x2050
#define VI6_DPR_ROUTE_BRSSEL		(1 << 28)
#define VI6_DPR_ROUTE_FXA_MASK		(0xff << 16)
#define VI6_DPR_ROUTE_FXA_SHIFT		16
#define VI6_DPR_ROUTE_FP_MASK		(0x3f << 8)
#define VI6_DPR_ROUTE_FP_SHIFT		8
#define VI6_DPR_ROUTE_RT_MASK		(0x3f << 0)
#define VI6_DPR_ROUTE_RT_SHIFT		0

#define VI6_DPR_HGO_SMPPT		0x2054
#define VI6_DPR_HGT_SMPPT		0x2058
#define VI6_DPR_SMPPT_TGW_MASK		(7 << 8)
#define VI6_DPR_SMPPT_TGW_SHIFT		8
#define VI6_DPR_SMPPT_PT_MASK		(0x3f << 0)
#define VI6_DPR_SMPPT_PT_SHIFT		0

#define VI6_DPR_NODE_RPF(n)		(n)
#define VI6_DPR_NODE_SRU		16
#define VI6_DPR_NODE_UDS(n)		(17 + (n))
#define VI6_DPR_NODE_LUT		22
#define VI6_DPR_NODE_BRU_IN(n)		(((n) <= 3) ? 23 + (n) : 49)
#define VI6_DPR_NODE_BRU_OUT		27
#define VI6_DPR_NODE_CLU		29
#define VI6_DPR_NODE_HST		30
#define VI6_DPR_NODE_HSI		31
#define VI6_DPR_NODE_BRS_IN(n)		(38 + (n))
#define VI6_DPR_NODE_LIF		55		/* Gen2 only */
#define VI6_DPR_NODE_WPF(n)		(56 + (n))
#define VI6_DPR_NODE_UNUSED		63

/* -----------------------------------------------------------------------------
 * SRU Control Registers
 */

#define VI6_SRU_CTRL0			0x2200
#define VI6_SRU_CTRL0_PARAM0_MASK	(0x1ff << 16)
#define VI6_SRU_CTRL0_PARAM0_SHIFT	16
#define VI6_SRU_CTRL0_PARAM1_MASK	(0x1f << 8)
#define VI6_SRU_CTRL0_PARAM1_SHIFT	8
#define VI6_SRU_CTRL0_MODE_UPSCALE	(4 << 4)
#define VI6_SRU_CTRL0_PARAM2		(1 << 3)
#define VI6_SRU_CTRL0_PARAM3		(1 << 2)
#define VI6_SRU_CTRL0_PARAM4		(1 << 1)
#define VI6_SRU_CTRL0_EN		(1 << 0)

#define VI6_SRU_CTRL1			0x2204
#define VI6_SRU_CTRL1_PARAM5		0x7ff

#define VI6_SRU_CTRL2			0x2208
#define VI6_SRU_CTRL2_PARAM6_SHIFT	16
#define VI6_SRU_CTRL2_PARAM7_SHIFT	8
#define VI6_SRU_CTRL2_PARAM8_SHIFT	0

/* -----------------------------------------------------------------------------
 * UDS Control Registers
 */

#define VI6_UDS_OFFSET			0x100

#define VI6_UDS_CTRL			0x2300
#define VI6_UDS_CTRL_AMD		(1 << 30)
#define VI6_UDS_CTRL_FMD		(1 << 29)
#define VI6_UDS_CTRL_BLADV		(1 << 28)
#define VI6_UDS_CTRL_AON		(1 << 25)
#define VI6_UDS_CTRL_ATHON		(1 << 24)
#define VI6_UDS_CTRL_BC			(1 << 20)
#define VI6_UDS_CTRL_NE_A		(1 << 19)
#define VI6_UDS_CTRL_NE_RCR		(1 << 18)
#define VI6_UDS_CTRL_NE_GY		(1 << 17)
#define VI6_UDS_CTRL_NE_BCB		(1 << 16)
#define VI6_UDS_CTRL_AMDSLH		(1 << 2)
#define VI6_UDS_CTRL_TDIPC		(1 << 1)

#define VI6_UDS_SCALE			0x2304
#define VI6_UDS_SCALE_HMANT_MASK	(0xf << 28)
#define VI6_UDS_SCALE_HMANT_SHIFT	28
#define VI6_UDS_SCALE_HFRAC_MASK	(0xfff << 16)
#define VI6_UDS_SCALE_HFRAC_SHIFT	16
#define VI6_UDS_SCALE_VMANT_MASK	(0xf << 12)
#define VI6_UDS_SCALE_VMANT_SHIFT	12
#define VI6_UDS_SCALE_VFRAC_MASK	(0xfff << 0)
#define VI6_UDS_SCALE_VFRAC_SHIFT	0

#define VI6_UDS_ALPTH			0x2308
#define VI6_UDS_ALPTH_TH1_MASK		(0xff << 8)
#define VI6_UDS_ALPTH_TH1_SHIFT		8
#define VI6_UDS_ALPTH_TH0_MASK		(0xff << 0)
#define VI6_UDS_ALPTH_TH0_SHIFT		0

#define VI6_UDS_ALPVAL			0x230c
#define VI6_UDS_ALPVAL_VAL2_MASK	(0xff << 16)
#define VI6_UDS_ALPVAL_VAL2_SHIFT	16
#define VI6_UDS_ALPVAL_VAL1_MASK	(0xff << 8)
#define VI6_UDS_ALPVAL_VAL1_SHIFT	8
#define VI6_UDS_ALPVAL_VAL0_MASK	(0xff << 0)
#define VI6_UDS_ALPVAL_VAL0_SHIFT	0

#define VI6_UDS_PASS_BWIDTH		0x2310
#define VI6_UDS_PASS_BWIDTH_H_MASK	(0x7f << 16)
#define VI6_UDS_PASS_BWIDTH_H_SHIFT	16
#define VI6_UDS_PASS_BWIDTH_V_MASK	(0x7f << 0)
#define VI6_UDS_PASS_BWIDTH_V_SHIFT	0

#define VI6_UDS_HPHASE			0x2314
#define VI6_UDS_HPHASE_HSTP_MASK	(0xfff << 16)
#define VI6_UDS_HPHASE_HSTP_SHIFT	16
#define VI6_UDS_HPHASE_HEDP_MASK	(0xfff << 0)
#define VI6_UDS_HPHASE_HEDP_SHIFT	0

#define VI6_UDS_IPC			0x2318
#define VI6_UDS_IPC_FIELD		(1 << 27)
#define VI6_UDS_IPC_VEDP_MASK		(0xfff << 0)
#define VI6_UDS_IPC_VEDP_SHIFT		0

#define VI6_UDS_HSZCLIP			0x231c
#define VI6_UDS_HSZCLIP_HCEN		(1 << 28)
#define VI6_UDS_HSZCLIP_HCL_OFST_MASK	(0xff << 16)
#define VI6_UDS_HSZCLIP_HCL_OFST_SHIFT	16
#define VI6_UDS_HSZCLIP_HCL_SIZE_MASK	(0x1fff << 0)
#define VI6_UDS_HSZCLIP_HCL_SIZE_SHIFT	0

#define VI6_UDS_CLIP_SIZE		0x2324
#define VI6_UDS_CLIP_SIZE_HSIZE_MASK	(0x1fff << 16)
#define VI6_UDS_CLIP_SIZE_HSIZE_SHIFT	16
#define VI6_UDS_CLIP_SIZE_VSIZE_MASK	(0x1fff << 0)
#define VI6_UDS_CLIP_SIZE_VSIZE_SHIFT	0

#define VI6_UDS_FILL_COLOR		0x2328
#define VI6_UDS_FILL_COLOR_RFILC_MASK	(0xff << 16)
#define VI6_UDS_FILL_COLOR_RFILC_SHIFT	16
#define VI6_UDS_FILL_COLOR_GFILC_MASK	(0xff << 8)
#define VI6_UDS_FILL_COLOR_GFILC_SHIFT	8
#define VI6_UDS_FILL_COLOR_BFILC_MASK	(0xff << 0)
#define VI6_UDS_FILL_COLOR_BFILC_SHIFT	0

/* -----------------------------------------------------------------------------
 * LUT Control Registers
 */

#define VI6_LUT_CTRL			0x2800
#define VI6_LUT_CTRL_EN			(1 << 0)

/* -----------------------------------------------------------------------------
 * CLU Control Registers
 */

#define VI6_CLU_CTRL			0x2900
#define VI6_CLU_CTRL_AAI		(1 << 28)
#define VI6_CLU_CTRL_MVS		(1 << 24)
#define VI6_CLU_CTRL_AX1I_2D		(3 << 14)
#define VI6_CLU_CTRL_AX2I_2D		(1 << 12)
#define VI6_CLU_CTRL_OS0_2D		(3 << 8)
#define VI6_CLU_CTRL_OS1_2D		(1 << 6)
#define VI6_CLU_CTRL_OS2_2D		(3 << 4)
#define VI6_CLU_CTRL_M2D		(1 << 1)
#define VI6_CLU_CTRL_EN			(1 << 0)

/* -----------------------------------------------------------------------------
 * HST Control Registers
 */

#define VI6_HST_CTRL			0x2a00
#define VI6_HST_CTRL_EN			(1 << 0)

/* -----------------------------------------------------------------------------
 * HSI Control Registers
 */

#define VI6_HSI_CTRL			0x2b00
#define VI6_HSI_CTRL_EN			(1 << 0)

/* -----------------------------------------------------------------------------
 * BRS and BRU Control Registers
 */

#define VI6_ROP_NOP			0
#define VI6_ROP_AND			1
#define VI6_ROP_AND_REV			2
#define VI6_ROP_COPY			3
#define VI6_ROP_AND_INV			4
#define VI6_ROP_CLEAR			5
#define VI6_ROP_XOR			6
#define VI6_ROP_OR			7
#define VI6_ROP_NOR			8
#define VI6_ROP_EQUIV			9
#define VI6_ROP_INVERT			10
#define VI6_ROP_OR_REV			11
#define VI6_ROP_COPY_INV		12
#define VI6_ROP_OR_INV			13
#define VI6_ROP_NAND			14
#define VI6_ROP_SET			15

#define VI6_BRU_BASE			0x2c00
#define VI6_BRS_BASE			0x3900

#define VI6_BRU_INCTRL			0x0000
#define VI6_BRU_INCTRL_NRM		(1 << 28)
#define VI6_BRU_INCTRL_DnON		(1 << (16 + (n)))
#define VI6_BRU_INCTRL_DITHn_OFF	(0 << ((n) * 4))
#define VI6_BRU_INCTRL_DITHn_18BPP	(1 << ((n) * 4))
#define VI6_BRU_INCTRL_DITHn_16BPP	(2 << ((n) * 4))
#define VI6_BRU_INCTRL_DITHn_15BPP	(3 << ((n) * 4))
#define VI6_BRU_INCTRL_DITHn_12BPP	(4 << ((n) * 4))
#define VI6_BRU_INCTRL_DITHn_8BPP	(5 << ((n) * 4))
#define VI6_BRU_INCTRL_DITHn_MASK	(7 << ((n) * 4))
#define VI6_BRU_INCTRL_DITHn_SHIFT	((n) * 4)

#define VI6_BRU_VIRRPF_SIZE		0x0004
#define VI6_BRU_VIRRPF_SIZE_HSIZE_MASK	(0x1fff << 16)
#define VI6_BRU_VIRRPF_SIZE_HSIZE_SHIFT	16
#define VI6_BRU_VIRRPF_SIZE_VSIZE_MASK	(0x1fff << 0)
#define VI6_BRU_VIRRPF_SIZE_VSIZE_SHIFT	0

#define VI6_BRU_VIRRPF_LOC		0x0008
#define VI6_BRU_VIRRPF_LOC_HCOORD_MASK	(0x1fff << 16)
#define VI6_BRU_VIRRPF_LOC_HCOORD_SHIFT	16
#define VI6_BRU_VIRRPF_LOC_VCOORD_MASK	(0x1fff << 0)
#define VI6_BRU_VIRRPF_LOC_VCOORD_SHIFT	0

#define VI6_BRU_VIRRPF_COL		0x000c
#define VI6_BRU_VIRRPF_COL_A_MASK	(0xff << 24)
#define VI6_BRU_VIRRPF_COL_A_SHIFT	24
#define VI6_BRU_VIRRPF_COL_RCR_MASK	(0xff << 16)
#define VI6_BRU_VIRRPF_COL_RCR_SHIFT	16
#define VI6_BRU_VIRRPF_COL_GY_MASK	(0xff << 8)
#define VI6_BRU_VIRRPF_COL_GY_SHIFT	8
#define VI6_BRU_VIRRPF_COL_BCB_MASK	(0xff << 0)
#define VI6_BRU_VIRRPF_COL_BCB_SHIFT	0

#define VI6_BRU_CTRL(n)			(0x0010 + (n) * 8 + ((n) <= 3 ? 0 : 4))
#define VI6_BRU_CTRL_RBC		(1 << 31)
#define VI6_BRU_CTRL_DSTSEL_BRUIN(n)	(((n) <= 3 ? (n) : (n)+1) << 20)
#define VI6_BRU_CTRL_DSTSEL_VRPF	(4 << 20)
#define VI6_BRU_CTRL_DSTSEL_MASK	(7 << 20)
#define VI6_BRU_CTRL_SRCSEL_BRUIN(n)	(((n) <= 3 ? (n) : (n)+1) << 16)
#define VI6_BRU_CTRL_SRCSEL_VRPF	(4 << 16)
#define VI6_BRU_CTRL_SRCSEL_MASK	(7 << 16)
#define VI6_BRU_CTRL_CROP(rop)		((rop) << 4)
#define VI6_BRU_CTRL_CROP_MASK		(0xf << 4)
#define VI6_BRU_CTRL_AROP(rop)		((rop) << 0)
#define VI6_BRU_CTRL_AROP_MASK		(0xf << 0)

#define VI6_BRU_BLD(n)			(0x0014 + (n) * 8 + ((n) <= 3 ? 0 : 4))
#define VI6_BRU_BLD_CBES		(1 << 31)
#define VI6_BRU_BLD_CCMDX_DST_A		(0 << 28)
#define VI6_BRU_BLD_CCMDX_255_DST_A	(1 << 28)
#define VI6_BRU_BLD_CCMDX_SRC_A		(2 << 28)
#define VI6_BRU_BLD_CCMDX_255_SRC_A	(3 << 28)
#define VI6_BRU_BLD_CCMDX_COEFX		(4 << 28)
#define VI6_BRU_BLD_CCMDX_MASK		(7 << 28)
#define VI6_BRU_BLD_CCMDY_DST_A		(0 << 24)
#define VI6_BRU_BLD_CCMDY_255_DST_A	(1 << 24)
#define VI6_BRU_BLD_CCMDY_SRC_A		(2 << 24)
#define VI6_BRU_BLD_CCMDY_255_SRC_A	(3 << 24)
#define VI6_BRU_BLD_CCMDY_COEFY		(4 << 24)
#define VI6_BRU_BLD_CCMDY_MASK		(7 << 24)
#define VI6_BRU_BLD_CCMDY_SHIFT		24
#define VI6_BRU_BLD_ABES		(1 << 23)
#define VI6_BRU_BLD_ACMDX_DST_A		(0 << 20)
#define VI6_BRU_BLD_ACMDX_255_DST_A	(1 << 20)
#define VI6_BRU_BLD_ACMDX_SRC_A		(2 << 20)
#define VI6_BRU_BLD_ACMDX_255_SRC_A	(3 << 20)
#define VI6_BRU_BLD_ACMDX_COEFX		(4 << 20)
#define VI6_BRU_BLD_ACMDX_MASK		(7 << 20)
#define VI6_BRU_BLD_ACMDY_DST_A		(0 << 16)
#define VI6_BRU_BLD_ACMDY_255_DST_A	(1 << 16)
#define VI6_BRU_BLD_ACMDY_SRC_A		(2 << 16)
#define VI6_BRU_BLD_ACMDY_255_SRC_A	(3 << 16)
#define VI6_BRU_BLD_ACMDY_COEFY		(4 << 16)
#define VI6_BRU_BLD_ACMDY_MASK		(7 << 16)
#define VI6_BRU_BLD_COEFX_MASK		(0xff << 8)
#define VI6_BRU_BLD_COEFX_SHIFT		8
#define VI6_BRU_BLD_COEFY_MASK		(0xff << 0)
#define VI6_BRU_BLD_COEFY_SHIFT		0

#define VI6_BRU_ROP			0x0030	/* Only available on BRU */
#define VI6_BRU_ROP_DSTSEL_BRUIN(n)	(((n) <= 3 ? (n) : (n)+1) << 20)
#define VI6_BRU_ROP_DSTSEL_VRPF		(4 << 20)
#define VI6_BRU_ROP_DSTSEL_MASK		(7 << 20)
#define VI6_BRU_ROP_CROP(rop)		((rop) << 4)
#define VI6_BRU_ROP_CROP_MASK		(0xf << 4)
#define VI6_BRU_ROP_AROP(rop)		((rop) << 0)
#define VI6_BRU_ROP_AROP_MASK		(0xf << 0)

/* -----------------------------------------------------------------------------
 * HGO Control Registers
 */

#define VI6_HGO_OFFSET			0x3000
#define VI6_HGO_OFFSET_HOFFSET_SHIFT	16
#define VI6_HGO_OFFSET_VOFFSET_SHIFT	0
#define VI6_HGO_SIZE			0x3004
#define VI6_HGO_SIZE_HSIZE_SHIFT	16
#define VI6_HGO_SIZE_VSIZE_SHIFT	0
#define VI6_HGO_MODE			0x3008
#define VI6_HGO_MODE_STEP		(1 << 10)
#define VI6_HGO_MODE_MAXRGB		(1 << 7)
#define VI6_HGO_MODE_OFSB_R		(1 << 6)
#define VI6_HGO_MODE_OFSB_G		(1 << 5)
#define VI6_HGO_MODE_OFSB_B		(1 << 4)
#define VI6_HGO_MODE_HRATIO_SHIFT	2
#define VI6_HGO_MODE_VRATIO_SHIFT	0
#define VI6_HGO_LB_TH			0x300c
#define VI6_HGO_LBn_H(n)		(0x3010 + (n) * 8)
#define VI6_HGO_LBn_V(n)		(0x3014 + (n) * 8)
#define VI6_HGO_R_HISTO(n)		(0x3030 + (n) * 4)
#define VI6_HGO_R_MAXMIN		0x3130
#define VI6_HGO_R_SUM			0x3134
#define VI6_HGO_R_LB_DET		0x3138
#define VI6_HGO_G_HISTO(n)		(0x3140 + (n) * 4)
#define VI6_HGO_G_MAXMIN		0x3240
#define VI6_HGO_G_SUM			0x3244
#define VI6_HGO_G_LB_DET		0x3248
#define VI6_HGO_B_HISTO(n)		(0x3250 + (n) * 4)
#define VI6_HGO_B_MAXMIN		0x3350
#define VI6_HGO_B_SUM			0x3354
#define VI6_HGO_B_LB_DET		0x3358
#define VI6_HGO_EXT_HIST_ADDR		0x335c
#define VI6_HGO_EXT_HIST_DATA		0x3360
#define VI6_HGO_REGRST			0x33fc
#define VI6_HGO_REGRST_RCLEA		(1 << 0)

/* -----------------------------------------------------------------------------
 * HGT Control Registers
 */

#define VI6_HGT_OFFSET			0x3400
#define VI6_HGT_OFFSET_HOFFSET_SHIFT	16
#define VI6_HGT_OFFSET_VOFFSET_SHIFT	0
#define VI6_HGT_SIZE			0x3404
#define VI6_HGT_SIZE_HSIZE_SHIFT	16
#define VI6_HGT_SIZE_VSIZE_SHIFT	0
#define VI6_HGT_MODE			0x3408
#define VI6_HGT_MODE_HRATIO_SHIFT	2
#define VI6_HGT_MODE_VRATIO_SHIFT	0
#define VI6_HGT_HUE_AREA(n)		(0x340c + (n) * 4)
#define VI6_HGT_HUE_AREA_LOWER_SHIFT	16
#define VI6_HGT_HUE_AREA_UPPER_SHIFT	0
#define VI6_HGT_LB_TH			0x3424
#define VI6_HGT_LBn_H(n)		(0x3438 + (n) * 8)
#define VI6_HGT_LBn_V(n)		(0x342c + (n) * 8)
#define VI6_HGT_HISTO(m, n)		(0x3450 + (m) * 128 + (n) * 4)
#define VI6_HGT_MAXMIN			0x3750
#define VI6_HGT_SUM			0x3754
#define VI6_HGT_LB_DET			0x3758
#define VI6_HGT_REGRST			0x37fc
#define VI6_HGT_REGRST_RCLEA		(1 << 0)

/* -----------------------------------------------------------------------------
 * LIF Control Registers
 */

#define VI6_LIF_OFFSET			(-0x100)

#define VI6_LIF_CTRL			0x3b00
#define VI6_LIF_CTRL_OBTH_MASK		(0x7ff << 16)
#define VI6_LIF_CTRL_OBTH_SHIFT		16
#define VI6_LIF_CTRL_CFMT		(1 << 4)
#define VI6_LIF_CTRL_REQSEL		(1 << 1)
#define VI6_LIF_CTRL_LIF_EN		(1 << 0)

#define VI6_LIF_CSBTH			0x3b04
#define VI6_LIF_CSBTH_HBTH_MASK		(0x7ff << 16)
#define VI6_LIF_CSBTH_HBTH_SHIFT	16
#define VI6_LIF_CSBTH_LBTH_MASK		(0x7ff << 0)
#define VI6_LIF_CSBTH_LBTH_SHIFT	0

/* -----------------------------------------------------------------------------
 * Security Control Registers
 */

#define VI6_SECURITY_CTRL0		0x3d00
#define VI6_SECURITY_CTRL1		0x3d04

/* -----------------------------------------------------------------------------
 * IP Version Registers
 */

#define VI6_IP_VERSION			0x3f00
#define VI6_IP_VERSION_MODEL_MASK	(0xff << 8)
#define VI6_IP_VERSION_MODEL_VSPS_H2	(0x09 << 8)
#define VI6_IP_VERSION_MODEL_VSPR_H2	(0x0a << 8)
#define VI6_IP_VERSION_MODEL_VSPD_GEN2	(0x0b << 8)
#define VI6_IP_VERSION_MODEL_VSPS_M2	(0x0c << 8)
#define VI6_IP_VERSION_MODEL_VSPS_V2H	(0x12 << 8)
#define VI6_IP_VERSION_MODEL_VSPD_V2H	(0x13 << 8)
#define VI6_IP_VERSION_MODEL_VSPI_GEN3	(0x14 << 8)
#define VI6_IP_VERSION_MODEL_VSPBD_GEN3	(0x15 << 8)
#define VI6_IP_VERSION_MODEL_VSPBC_GEN3	(0x16 << 8)
#define VI6_IP_VERSION_MODEL_VSPD_GEN3	(0x17 << 8)
#define VI6_IP_VERSION_MODEL_VSPD_V3	(0x18 << 8)
#define VI6_IP_VERSION_MODEL_VSPDL_GEN3	(0x19 << 8)
#define VI6_IP_VERSION_MODEL_VSPBS_GEN3	(0x1a << 8)
#define VI6_IP_VERSION_SOC_MASK		(0xff << 0)
#define VI6_IP_VERSION_SOC_H2		(0x01 << 0)
#define VI6_IP_VERSION_SOC_V2H		(0x01 << 0)
#define VI6_IP_VERSION_SOC_V3M		(0x01 << 0)
#define VI6_IP_VERSION_SOC_M2		(0x02 << 0)
#define VI6_IP_VERSION_SOC_M3W		(0x02 << 0)
#define VI6_IP_VERSION_SOC_V3H		(0x02 << 0)
#define VI6_IP_VERSION_SOC_H3		(0x03 << 0)
#define VI6_IP_VERSION_SOC_D3		(0x04 << 0)
#define VI6_IP_VERSION_SOC_M3N		(0x04 << 0)
#define VI6_IP_VERSION_SOC_E3		(0x04 << 0)

/* -----------------------------------------------------------------------------
 * RPF CLUT Registers
 */

#define VI6_CLUT_TABLE			0x4000

/* -----------------------------------------------------------------------------
 * 1D LUT Registers
 */

#define VI6_LUT_TABLE			0x7000

/* -----------------------------------------------------------------------------
 * 3D LUT Registers
 */

#define VI6_CLU_ADDR			0x7400
#define VI6_CLU_DATA			0x7404

/* -----------------------------------------------------------------------------
 * Formats
 */

#define VI6_FMT_RGB_332			0x00
#define VI6_FMT_XRGB_4444		0x01
#define VI6_FMT_RGBX_4444		0x02
#define VI6_FMT_XRGB_1555		0x04
#define VI6_FMT_RGBX_5551		0x05
#define VI6_FMT_RGB_565			0x06
#define VI6_FMT_AXRGB_86666		0x07
#define VI6_FMT_RGBXA_66668		0x08
#define VI6_FMT_XRGBA_66668		0x09
#define VI6_FMT_ARGBX_86666		0x0a
#define VI6_FMT_AXRXGXB_8262626		0x0b
#define VI6_FMT_XRXGXBA_2626268		0x0c
#define VI6_FMT_ARXGXBX_8626262		0x0d
#define VI6_FMT_RXGXBXA_6262628		0x0e
#define VI6_FMT_XRGB_6666		0x0f
#define VI6_FMT_RGBX_6666		0x10
#define VI6_FMT_XRXGXB_262626		0x11
#define VI6_FMT_RXGXBX_626262		0x12
#define VI6_FMT_ARGB_8888		0x13
#define VI6_FMT_RGBA_8888		0x14
#define VI6_FMT_RGB_888			0x15
#define VI6_FMT_XRGXGB_763763		0x16
#define VI6_FMT_XXRGB_86666		0x17
#define VI6_FMT_BGR_888			0x18
#define VI6_FMT_ARGB_4444		0x19
#define VI6_FMT_RGBA_4444		0x1a
#define VI6_FMT_ARGB_1555		0x1b
#define VI6_FMT_RGBA_5551		0x1c
#define VI6_FMT_ABGR_4444		0x1d
#define VI6_FMT_BGRA_4444		0x1e
#define VI6_FMT_ABGR_1555		0x1f
#define VI6_FMT_BGRA_5551		0x20
#define VI6_FMT_XBXGXR_262626		0x21
#define VI6_FMT_ABGR_8888		0x22
#define VI6_FMT_XXRGB_88565		0x23

#define VI6_FMT_Y_UV_444		0x40
#define VI6_FMT_Y_UV_422		0x41
#define VI6_FMT_Y_UV_420		0x42
#define VI6_FMT_YUV_444			0x46
#define VI6_FMT_YUYV_422		0x47
#define VI6_FMT_YYUV_422		0x48
#define VI6_FMT_YUV_420			0x49
#define VI6_FMT_Y_U_V_444		0x4a
#define VI6_FMT_Y_U_V_422		0x4b
#define VI6_FMT_Y_U_V_420		0x4c

#endif /* __VSP1_REGS_H__ */
