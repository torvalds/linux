/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#ifndef _DAVINCI_VPFE_DM365_ISIF_REGS_H
#define _DAVINCI_VPFE_DM365_ISIF_REGS_H

/* ISIF registers relative offsets */
#define SYNCEN					0x00
#define MODESET					0x04
#define HDW					0x08
#define VDW					0x0c
#define PPLN					0x10
#define LPFR					0x14
#define SPH					0x18
#define LNH					0x1c
#define SLV0					0x20
#define SLV1					0x24
#define LNV					0x28
#define CULH					0x2c
#define CULV					0x30
#define HSIZE					0x34
#define SDOFST					0x38
#define CADU					0x3c
#define CADL					0x40
#define LINCFG0					0x44
#define LINCFG1					0x48
#define CCOLP					0x4c
#define CRGAIN					0x50
#define CGRGAIN					0x54
#define CGBGAIN					0x58
#define CBGAIN					0x5c
#define COFSTA					0x60
#define FLSHCFG0				0x64
#define FLSHCFG1				0x68
#define FLSHCFG2				0x6c
#define VDINT0					0x70
#define VDINT1					0x74
#define VDINT2					0x78
#define MISC					0x7c
#define CGAMMAWD				0x80
#define REC656IF				0x84
#define CCDCFG					0x88
/*****************************************************
* Defect Correction registers
*****************************************************/
#define DFCCTL					0x8c
#define VDFSATLV				0x90
#define DFCMEMCTL				0x94
#define DFCMEM0					0x98
#define DFCMEM1					0x9c
#define DFCMEM2					0xa0
#define DFCMEM3					0xa4
#define DFCMEM4					0xa8
/****************************************************
* Black Clamp registers
****************************************************/
#define CLAMPCFG				0xac
#define CLDCOFST				0xb0
#define CLSV					0xb4
#define CLHWIN0					0xb8
#define CLHWIN1					0xbc
#define CLHWIN2					0xc0
#define CLVRV					0xc4
#define CLVWIN0					0xc8
#define CLVWIN1					0xcc
#define CLVWIN2					0xd0
#define CLVWIN3					0xd4
/****************************************************
* Lense Shading Correction
****************************************************/
#define DATAHOFST				0xd8
#define DATAVOFST				0xdc
#define LSCHVAL					0xe0
#define LSCVVAL					0xe4
#define TWODLSCCFG				0xe8
#define TWODLSCOFST				0xec
#define TWODLSCINI				0xf0
#define TWODLSCGRBU				0xf4
#define TWODLSCGRBL				0xf8
#define TWODLSCGROF				0xfc
#define TWODLSCORBU				0x100
#define TWODLSCORBL				0x104
#define TWODLSCOROF				0x108
#define TWODLSCIRQEN				0x10c
#define TWODLSCIRQST				0x110
/****************************************************
* Data formatter
****************************************************/
#define FMTCFG					0x114
#define FMTPLEN					0x118
#define FMTSPH					0x11c
#define FMTLNH					0x120
#define FMTSLV					0x124
#define FMTLNV					0x128
#define FMTRLEN					0x12c
#define FMTHCNT					0x130
#define FMTAPTR_BASE				0x134
/* Below macro for addresses FMTAPTR0 - FMTAPTR15 */
#define FMTAPTR(i)			(FMTAPTR_BASE + (i * 4))
#define FMTPGMVF0				0x174
#define FMTPGMVF1				0x178
#define FMTPGMAPU0				0x17c
#define FMTPGMAPU1				0x180
#define FMTPGMAPS0				0x184
#define FMTPGMAPS1				0x188
#define FMTPGMAPS2				0x18c
#define FMTPGMAPS3				0x190
#define FMTPGMAPS4				0x194
#define FMTPGMAPS5				0x198
#define FMTPGMAPS6				0x19c
#define FMTPGMAPS7				0x1a0
/************************************************
* Color Space Converter
************************************************/
#define CSCCTL					0x1a4
#define CSCM0					0x1a8
#define CSCM1					0x1ac
#define CSCM2					0x1b0
#define CSCM3					0x1b4
#define CSCM4					0x1b8
#define CSCM5					0x1bc
#define CSCM6					0x1c0
#define CSCM7					0x1c4
#define OBWIN0					0x1c8
#define OBWIN1					0x1cc
#define OBWIN2					0x1d0
#define OBWIN3					0x1d4
#define OBVAL0					0x1d8
#define OBVAL1					0x1dc
#define OBVAL2					0x1e0
#define OBVAL3					0x1e4
#define OBVAL4					0x1e8
#define OBVAL5					0x1ec
#define OBVAL6					0x1f0
#define OBVAL7					0x1f4
#define CLKCTL					0x1f8

/* Masks & Shifts below */
#define START_PX_HOR_MASK			0x7fff
#define NUM_PX_HOR_MASK				0x7fff
#define START_VER_ONE_MASK			0x7fff
#define START_VER_TWO_MASK			0x7fff
#define NUM_LINES_VER				0x7fff

/* gain - offset masks */
#define OFFSET_MASK				0xfff
#define GAIN_SDRAM_EN_SHIFT			12
#define GAIN_IPIPE_EN_SHIFT			13
#define GAIN_H3A_EN_SHIFT			14
#define OFST_SDRAM_EN_SHIFT			8
#define OFST_IPIPE_EN_SHIFT			9
#define OFST_H3A_EN_SHIFT			10
#define GAIN_OFFSET_EN_MASK			0x7700

/* Culling */
#define CULL_PAT_EVEN_LINE_SHIFT		8

/* CCDCFG register */
#define ISIF_YCINSWP_RAW			(0x00 << 4)
#define ISIF_YCINSWP_YCBCR			(0x01 << 4)
#define ISIF_CCDCFG_FIDMD_LATCH_VSYNC		(0x00 << 6)
#define ISIF_CCDCFG_WENLOG_AND			(0x00 << 8)
#define ISIF_CCDCFG_TRGSEL_WEN			(0x00 << 9)
#define ISIF_CCDCFG_EXTRG_DISABLE		(0x00 << 10)
#define ISIF_LATCH_ON_VSYNC_DISABLE		(0x01 << 15)
#define ISIF_LATCH_ON_VSYNC_ENABLE		(0x00 << 15)
#define ISIF_DATA_PACK_MASK			0x03
#define ISIF_PIX_ORDER_SHIFT			11
#define ISIF_PIX_ORDER_MASK			0x01
#define ISIF_BW656_ENABLE			(0x01 << 5)

/* MODESET registers */
#define ISIF_VDHDOUT_INPUT			(0x00 << 0)
#define ISIF_INPUT_MASK				0x03
#define ISIF_INPUT_SHIFT			12
#define ISIF_FID_POL_MASK			0x01
#define ISIF_FID_POL_SHIFT			4
#define ISIF_HD_POL_MASK			0x01
#define ISIF_HD_POL_SHIFT			3
#define ISIF_VD_POL_MASK			0x01
#define ISIF_VD_POL_SHIFT			2
#define ISIF_DATAPOL_NORMAL			0x00
#define ISIF_DATAPOL_MASK			0x01
#define ISIF_DATAPOL_SHIFT			6
#define ISIF_EXWEN_DISABLE			0x00
#define ISIF_EXWEN_MASK				0x01
#define ISIF_EXWEN_SHIFT			5
#define ISIF_FRM_FMT_MASK			0x01
#define ISIF_FRM_FMT_SHIFT			7
#define ISIF_DATASFT_MASK			0x07
#define ISIF_DATASFT_SHIFT			8
#define ISIF_LPF_SHIFT				14
#define ISIF_LPF_MASK				0x1

/* GAMMAWD registers */
#define ISIF_ALAW_GAMA_WD_MASK			0xf
#define ISIF_ALAW_GAMA_WD_SHIFT			1
#define ISIF_ALAW_ENABLE			0x01
#define ISIF_GAMMAWD_CFA_MASK			0x01
#define ISIF_GAMMAWD_CFA_SHIFT			5

/* HSIZE registers */
#define ISIF_HSIZE_FLIP_MASK			0x01
#define ISIF_HSIZE_FLIP_SHIFT			12
#define ISIF_LINEOFST_MASK			0xfff

/* MISC registers */
#define ISIF_DPCM_EN_SHIFT			12
#define ISIF_DPCM_PREDICTOR_SHIFT		13
#define ISIF_DPCM_PREDICTOR_MASK		1

/* Black clamp related */
#define ISIF_BC_DCOFFSET_MASK			0x1fff
#define ISIF_BC_MODE_COLOR_MASK			1
#define ISIF_BC_MODE_COLOR_SHIFT		4
#define ISIF_HORZ_BC_MODE_MASK			3
#define ISIF_HORZ_BC_MODE_SHIFT			1
#define ISIF_HORZ_BC_WIN_COUNT_MASK		0x1f
#define ISIF_HORZ_BC_WIN_SEL_SHIFT		5
#define ISIF_HORZ_BC_PIX_LIMIT_SHIFT		6
#define ISIF_HORZ_BC_WIN_H_SIZE_MASK		3
#define ISIF_HORZ_BC_WIN_H_SIZE_SHIFT		8
#define ISIF_HORZ_BC_WIN_V_SIZE_MASK		3
#define ISIF_HORZ_BC_WIN_V_SIZE_SHIFT		12
#define ISIF_HORZ_BC_WIN_START_H_MASK		0x1fff
#define ISIF_HORZ_BC_WIN_START_V_MASK		0x1fff
#define ISIF_VERT_BC_OB_H_SZ_MASK		7
#define ISIF_VERT_BC_RST_VAL_SEL_MASK		3
#define ISIF_VERT_BC_RST_VAL_SEL_SHIFT		4
#define ISIF_VERT_BC_LINE_AVE_COEF_SHIFT	8
#define ISIF_VERT_BC_OB_START_HORZ_MASK		0x1fff
#define ISIF_VERT_BC_OB_START_VERT_MASK		0x1fff
#define ISIF_VERT_BC_OB_VERT_SZ_MASK		0x1fff
#define ISIF_VERT_BC_RST_VAL_MASK		0xfff
#define ISIF_BC_VERT_START_SUB_V_MASK		0x1fff

/* VDFC registers */
#define ISIF_VDFC_EN_SHIFT			4
#define ISIF_VDFC_CORR_MOD_MASK			3
#define ISIF_VDFC_CORR_MOD_SHIFT		5
#define ISIF_VDFC_CORR_WHOLE_LN_SHIFT		7
#define ISIF_VDFC_LEVEL_SHFT_MASK		7
#define ISIF_VDFC_LEVEL_SHFT_SHIFT		8
#define ISIF_VDFC_SAT_LEVEL_MASK		0xfff
#define ISIF_VDFC_POS_MASK			0x1fff
#define ISIF_DFCMEMCTL_DFCMARST_SHIFT		2

/* CSC registers */
#define ISIF_CSC_COEF_INTEG_MASK		7
#define ISIF_CSC_COEF_DECIMAL_MASK		0x1f
#define ISIF_CSC_COEF_INTEG_SHIFT		5
#define ISIF_CSCM_MSB_SHIFT			8
#define ISIF_DF_CSC_SPH_MASK			0x1fff
#define ISIF_DF_CSC_LNH_MASK			0x1fff
#define ISIF_DF_CSC_SLV_MASK			0x1fff
#define ISIF_DF_CSC_LNV_MASK			0x1fff
#define ISIF_DF_NUMLINES			0x7fff
#define ISIF_DF_NUMPIX				0x1fff

/* Offsets for LSC/DFC/Gain */
#define ISIF_DATA_H_OFFSET_MASK			0x1fff
#define ISIF_DATA_V_OFFSET_MASK			0x1fff

/* Linearization */
#define ISIF_LIN_CORRSFT_MASK			7
#define ISIF_LIN_CORRSFT_SHIFT			4
#define ISIF_LIN_SCALE_FACT_INTEG_SHIFT		10
#define ISIF_LIN_SCALE_FACT_DECIMAL_MASK	0x3ff
#define ISIF_LIN_ENTRY_MASK			0x3ff

/* masks and shifts*/
#define ISIF_SYNCEN_VDHDEN_MASK			(1 << 0)
#define ISIF_SYNCEN_WEN_MASK			(1 << 1)
#define ISIF_SYNCEN_WEN_SHIFT			1

#endif		/* _DAVINCI_VPFE_DM365_ISIF_REGS_H */
