/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2005-2009 Texas Instruments Inc
 */
#ifndef _DM355_CCDC_REGS_H
#define _DM355_CCDC_REGS_H

/**************************************************************************\
* Register OFFSET Definitions
\**************************************************************************/
#define SYNCEN				0x00
#define MODESET				0x04
#define HDWIDTH				0x08
#define VDWIDTH				0x0c
#define PPLN				0x10
#define LPFR				0x14
#define SPH				0x18
#define NPH				0x1c
#define SLV0				0x20
#define SLV1				0x24
#define NLV				0x28
#define CULH				0x2c
#define CULV				0x30
#define HSIZE				0x34
#define SDOFST				0x38
#define STADRH				0x3c
#define STADRL				0x40
#define CLAMP				0x44
#define DCSUB				0x48
#define COLPTN				0x4c
#define BLKCMP0				0x50
#define BLKCMP1				0x54
#define MEDFILT				0x58
#define RYEGAIN				0x5c
#define GRCYGAIN			0x60
#define GBGGAIN				0x64
#define BMGGAIN				0x68
#define OFFSET				0x6c
#define OUTCLIP				0x70
#define VDINT0				0x74
#define VDINT1				0x78
#define RSV0				0x7c
#define GAMMAWD				0x80
#define REC656IF			0x84
#define CCDCFG				0x88
#define FMTCFG				0x8c
#define FMTPLEN				0x90
#define FMTSPH				0x94
#define FMTLNH				0x98
#define FMTSLV				0x9c
#define FMTLNV				0xa0
#define FMTRLEN				0xa4
#define FMTHCNT				0xa8
#define FMT_ADDR_PTR_B			0xac
#define FMT_ADDR_PTR(i)			(FMT_ADDR_PTR_B + (i * 4))
#define FMTPGM_VF0			0xcc
#define FMTPGM_VF1			0xd0
#define FMTPGM_AP0			0xd4
#define FMTPGM_AP1			0xd8
#define FMTPGM_AP2			0xdc
#define FMTPGM_AP3                      0xe0
#define FMTPGM_AP4                      0xe4
#define FMTPGM_AP5                      0xe8
#define FMTPGM_AP6                      0xec
#define FMTPGM_AP7                      0xf0
#define LSCCFG1                         0xf4
#define LSCCFG2                         0xf8
#define LSCH0                           0xfc
#define LSCV0                           0x100
#define LSCKH                           0x104
#define LSCKV                           0x108
#define LSCMEMCTL                       0x10c
#define LSCMEMD                         0x110
#define LSCMEMQ                         0x114
#define DFCCTL                          0x118
#define DFCVSAT                         0x11c
#define DFCMEMCTL                       0x120
#define DFCMEM0                         0x124
#define DFCMEM1                         0x128
#define DFCMEM2                         0x12c
#define DFCMEM3                         0x130
#define DFCMEM4                         0x134
#define CSCCTL                          0x138
#define CSCM0                           0x13c
#define CSCM1                           0x140
#define CSCM2                           0x144
#define CSCM3                           0x148
#define CSCM4                           0x14c
#define CSCM5                           0x150
#define CSCM6                           0x154
#define CSCM7                           0x158
#define DATAOFST			0x15c
#define CCDC_REG_LAST			DATAOFST
/**************************************************************
*	Define for various register bit mask and shifts for CCDC
*
**************************************************************/
#define CCDC_RAW_IP_MODE			0
#define CCDC_VDHDOUT_INPUT			0
#define CCDC_YCINSWP_RAW			(0 << 4)
#define CCDC_EXWEN_DISABLE			0
#define CCDC_DATAPOL_NORMAL			0
#define CCDC_CCDCFG_FIDMD_LATCH_VSYNC		0
#define CCDC_CCDCFG_FIDMD_NO_LATCH_VSYNC	(1 << 6)
#define CCDC_CCDCFG_WENLOG_AND			0
#define CCDC_CCDCFG_TRGSEL_WEN			0
#define CCDC_CCDCFG_EXTRG_DISABLE		0
#define CCDC_CFA_MOSAIC				0
#define CCDC_Y8POS_SHIFT			11

#define CCDC_VDC_DFCVSAT_MASK			0x3fff
#define CCDC_DATAOFST_MASK			0x0ff
#define CCDC_DATAOFST_H_SHIFT			0
#define CCDC_DATAOFST_V_SHIFT			8
#define CCDC_GAMMAWD_CFA_MASK			1
#define CCDC_GAMMAWD_CFA_SHIFT			5
#define CCDC_GAMMAWD_INPUT_SHIFT		2
#define CCDC_FID_POL_MASK			1
#define CCDC_FID_POL_SHIFT			4
#define CCDC_HD_POL_MASK			1
#define CCDC_HD_POL_SHIFT			3
#define CCDC_VD_POL_MASK			1
#define CCDC_VD_POL_SHIFT			2
#define CCDC_VD_POL_NEGATIVE			(1 << 2)
#define CCDC_FRM_FMT_MASK			1
#define CCDC_FRM_FMT_SHIFT			7
#define CCDC_DATA_SZ_MASK			7
#define CCDC_DATA_SZ_SHIFT			8
#define CCDC_VDHDOUT_MASK			1
#define CCDC_VDHDOUT_SHIFT			0
#define CCDC_EXWEN_MASK				1
#define CCDC_EXWEN_SHIFT			5
#define CCDC_INPUT_MODE_MASK			3
#define CCDC_INPUT_MODE_SHIFT			12
#define CCDC_PIX_FMT_MASK			3
#define CCDC_PIX_FMT_SHIFT			12
#define CCDC_DATAPOL_MASK			1
#define CCDC_DATAPOL_SHIFT			6
#define CCDC_WEN_ENABLE				(1 << 1)
#define CCDC_VDHDEN_ENABLE			(1 << 16)
#define CCDC_LPF_ENABLE				(1 << 14)
#define CCDC_ALAW_ENABLE			1
#define CCDC_ALAW_GAMMA_WD_MASK			7
#define CCDC_REC656IF_BT656_EN			3

#define CCDC_FMTCFG_FMTMODE_MASK		3
#define CCDC_FMTCFG_FMTMODE_SHIFT		1
#define CCDC_FMTCFG_LNUM_MASK			3
#define CCDC_FMTCFG_LNUM_SHIFT			4
#define CCDC_FMTCFG_ADDRINC_MASK		7
#define CCDC_FMTCFG_ADDRINC_SHIFT		8

#define CCDC_CCDCFG_FIDMD_SHIFT			6
#define	CCDC_CCDCFG_WENLOG_SHIFT		8
#define CCDC_CCDCFG_TRGSEL_SHIFT		9
#define CCDC_CCDCFG_EXTRG_SHIFT			10
#define CCDC_CCDCFG_MSBINVI_SHIFT		13

#define CCDC_HSIZE_FLIP_SHIFT			12
#define CCDC_HSIZE_FLIP_MASK			1
#define CCDC_HSIZE_VAL_MASK			0xFFF
#define CCDC_SDOFST_FIELD_INTERLEAVED		0x249
#define CCDC_SDOFST_INTERLACE_INVERSE		0x4B6D
#define CCDC_SDOFST_INTERLACE_NORMAL		0x0B6D
#define CCDC_SDOFST_PROGRESSIVE_INVERSE		0x4000
#define CCDC_SDOFST_PROGRESSIVE_NORMAL		0
#define CCDC_START_PX_HOR_MASK			0x7FFF
#define CCDC_NUM_PX_HOR_MASK			0x7FFF
#define CCDC_START_VER_ONE_MASK			0x7FFF
#define CCDC_START_VER_TWO_MASK			0x7FFF
#define CCDC_NUM_LINES_VER			0x7FFF

#define CCDC_BLK_CLAMP_ENABLE			(1 << 15)
#define CCDC_BLK_SGAIN_MASK			0x1F
#define CCDC_BLK_ST_PXL_MASK			0x1FFF
#define CCDC_BLK_SAMPLE_LN_MASK			3
#define CCDC_BLK_SAMPLE_LN_SHIFT		13

#define CCDC_NUM_LINE_CALC_MASK			3
#define CCDC_NUM_LINE_CALC_SHIFT		14

#define CCDC_BLK_DC_SUB_MASK			0x3FFF
#define CCDC_BLK_COMP_MASK			0xFF
#define CCDC_BLK_COMP_GB_COMP_SHIFT		8
#define CCDC_BLK_COMP_GR_COMP_SHIFT		0
#define CCDC_BLK_COMP_R_COMP_SHIFT		8
#define CCDC_LATCH_ON_VSYNC_DISABLE		(1 << 15)
#define CCDC_LATCH_ON_VSYNC_ENABLE		(0 << 15)
#define CCDC_FPC_ENABLE				(1 << 15)
#define CCDC_FPC_FPC_NUM_MASK			0x7FFF
#define CCDC_DATA_PACK_ENABLE			(1 << 11)
#define CCDC_FMT_HORZ_FMTLNH_MASK		0x1FFF
#define CCDC_FMT_HORZ_FMTSPH_MASK		0x1FFF
#define CCDC_FMT_HORZ_FMTSPH_SHIFT		16
#define CCDC_FMT_VERT_FMTLNV_MASK		0x1FFF
#define CCDC_FMT_VERT_FMTSLV_MASK		0x1FFF
#define CCDC_FMT_VERT_FMTSLV_SHIFT		16
#define CCDC_VP_OUT_VERT_NUM_MASK		0x3FFF
#define CCDC_VP_OUT_VERT_NUM_SHIFT		17
#define CCDC_VP_OUT_HORZ_NUM_MASK		0x1FFF
#define CCDC_VP_OUT_HORZ_NUM_SHIFT		4
#define CCDC_VP_OUT_HORZ_ST_MASK		0xF

#define CCDC_CSC_COEF_INTEG_MASK		7
#define CCDC_CSC_COEF_DECIMAL_MASK		0x1f
#define CCDC_CSC_COEF_INTEG_SHIFT		5
#define CCDC_CSCM_MSB_SHIFT			8
#define CCDC_CSC_ENABLE				1
#define CCDC_CSC_DEC_MAX			32

#define CCDC_MFILT1_SHIFT			10
#define CCDC_MFILT2_SHIFT			8
#define CCDC_MED_FILT_THRESH			0x3FFF
#define CCDC_LPF_MASK				1
#define CCDC_LPF_SHIFT				14
#define CCDC_OFFSET_MASK			0x3FF
#define CCDC_DATASFT_MASK			7
#define CCDC_DATASFT_SHIFT			8

#define CCDC_DF_ENABLE				1

#define CCDC_FMTPLEN_P0_MASK			0xF
#define CCDC_FMTPLEN_P1_MASK			0xF
#define CCDC_FMTPLEN_P2_MASK			7
#define CCDC_FMTPLEN_P3_MASK			7
#define CCDC_FMTPLEN_P0_SHIFT			0
#define CCDC_FMTPLEN_P1_SHIFT			4
#define CCDC_FMTPLEN_P2_SHIFT			8
#define CCDC_FMTPLEN_P3_SHIFT			12

#define CCDC_FMTSPH_MASK			0x1FFF
#define CCDC_FMTLNH_MASK			0x1FFF
#define CCDC_FMTSLV_MASK			0x1FFF
#define CCDC_FMTLNV_MASK			0x7FFF
#define CCDC_FMTRLEN_MASK			0x1FFF
#define CCDC_FMTHCNT_MASK			0x1FFF

#define CCDC_ADP_INIT_MASK			0x1FFF
#define CCDC_ADP_LINE_SHIFT			13
#define CCDC_ADP_LINE_MASK			3
#define CCDC_FMTPGN_APTR_MASK			7

#define CCDC_DFCCTL_GDFCEN_MASK			1
#define CCDC_DFCCTL_VDFCEN_MASK			1
#define CCDC_DFCCTL_VDFC_DISABLE		(0 << 4)
#define CCDC_DFCCTL_VDFCEN_SHIFT		4
#define CCDC_DFCCTL_VDFCSL_MASK			3
#define CCDC_DFCCTL_VDFCSL_SHIFT		5
#define CCDC_DFCCTL_VDFCUDA_MASK		1
#define CCDC_DFCCTL_VDFCUDA_SHIFT		7
#define CCDC_DFCCTL_VDFLSFT_MASK		3
#define CCDC_DFCCTL_VDFLSFT_SHIFT		8
#define CCDC_DFCMEMCTL_DFCMARST_MASK		1
#define CCDC_DFCMEMCTL_DFCMARST_SHIFT		2
#define CCDC_DFCMEMCTL_DFCMWR_MASK		1
#define CCDC_DFCMEMCTL_DFCMWR_SHIFT		0
#define CCDC_DFCMEMCTL_INC_ADDR			(0 << 2)

#define CCDC_LSCCFG_GFTSF_MASK			7
#define CCDC_LSCCFG_GFTSF_SHIFT			1
#define CCDC_LSCCFG_GFTINV_MASK			0xf
#define CCDC_LSCCFG_GFTINV_SHIFT		4
#define CCDC_LSC_GFTABLE_SEL_MASK		3
#define CCDC_LSC_GFTABLE_EPEL_SHIFT		8
#define CCDC_LSC_GFTABLE_OPEL_SHIFT		10
#define CCDC_LSC_GFTABLE_EPOL_SHIFT		12
#define CCDC_LSC_GFTABLE_OPOL_SHIFT		14
#define CCDC_LSC_GFMODE_MASK			3
#define CCDC_LSC_GFMODE_SHIFT			4
#define CCDC_LSC_DISABLE			0
#define CCDC_LSC_ENABLE				1
#define CCDC_LSC_TABLE1_SLC			0
#define CCDC_LSC_TABLE2_SLC			1
#define CCDC_LSC_TABLE3_SLC			2
#define CCDC_LSC_MEMADDR_RESET			(1 << 2)
#define CCDC_LSC_MEMADDR_INCR			(0 << 2)
#define CCDC_LSC_FRAC_MASK_T1			0xFF
#define CCDC_LSC_INT_MASK			3
#define CCDC_LSC_FRAC_MASK			0x3FFF
#define CCDC_LSC_CENTRE_MASK			0x3FFF
#define CCDC_LSC_COEF_MASK			0xff
#define CCDC_LSC_COEFL_SHIFT			0
#define CCDC_LSC_COEFU_SHIFT			8
#define CCDC_GAIN_MASK				0x7FF
#define CCDC_SYNCEN_VDHDEN_MASK			(1 << 0)
#define CCDC_SYNCEN_WEN_MASK			(1 << 1)
#define CCDC_SYNCEN_WEN_SHIFT			1

/* Power on Defaults in hardware */
#define MODESET_DEFAULT				0x200
#define CULH_DEFAULT				0xFFFF
#define CULV_DEFAULT				0xFF
#define GAIN_DEFAULT				256
#define OUTCLIP_DEFAULT				0x3FFF
#define LSCCFG2_DEFAULT				0xE

#endif
