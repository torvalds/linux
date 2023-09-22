/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register definition header for NPCM video driver
 *
 * Copyright (C) 2022 Nuvoton Technologies
 */

#ifndef _NPCM_REGS_H
#define _NPCM_REGS_H

/* VCD Registers */
#define VCD_DIFF_TBL			0x0000
#define VCD_FBA_ADR			0x8000
#define VCD_FBB_ADR			0x8004

#define VCD_FB_LP			0x8008
#define  VCD_FBA_LP			GENMASK(15, 0)
#define  VCD_FBB_LP			GENMASK(31, 16)

#define VCD_CAP_RES			0x800c
#define  VCD_CAP_RES_VERT_RES		GENMASK(10, 0)
#define  VCD_CAP_RES_HOR_RES		GENMASK(26, 16)

#define VCD_MODE			0x8014
#define  VCD_MODE_VCDE			BIT(0)
#define  VCD_MODE_CM565			BIT(1)
#define  VCD_MODE_IDBC			BIT(3)
#define  VCD_MODE_KVM_BW_SET		BIT(16)

#define VCD_CMD				0x8018
#define  VCD_CMD_GO			BIT(0)
#define  VCD_CMD_RST			BIT(1)
#define  VCD_CMD_OPERATION		GENMASK(6, 4)
#define   VCD_CMD_OPERATION_CAPTURE	0
#define   VCD_CMD_OPERATION_COMPARE	2

#define	VCD_STAT			0x801c
#define	 VCD_STAT_DONE			BIT(0)
#define	 VCD_STAT_IFOT			BIT(2)
#define	 VCD_STAT_IFOR			BIT(3)
#define	 VCD_STAT_VHT_CHG		BIT(5)
#define	 VCD_STAT_HAC_CHG		BIT(8)
#define	 VCD_STAT_BUSY			BIT(30)
#define	VCD_STAT_CLEAR			0x3fff

#define VCD_INTE			0x8020
#define  VCD_INTE_DONE_IE		BIT(0)
#define  VCD_INTE_IFOT_IE		BIT(2)
#define  VCD_INTE_IFOR_IE		BIT(3)
#define  VCD_INTE_VHT_IE		BIT(5)
#define  VCD_INTE_HAC_IE		BIT(8)

#define VCD_RCHG			0x8028
#define  VCD_RCHG_IG_CHG0		GENMASK(2, 0)
#define  VCD_RCHG_TIM_PRSCL		GENMASK(12, 9)

#define VCD_VER_HI_TIM			0x8044
#define  VCD_VER_HI_TIME		GENMASK(23, 0)

#define VCD_VER_HI_LST			0x8048
#define  VCD_VER_HI_LAST		GENMASK(23, 0)

#define VCD_HOR_AC_TIM			0x804c
#define  VCD_HOR_AC_TIME		GENMASK(13, 0)

#define VCD_HOR_AC_LST			0x8050
#define  VCD_HOR_AC_LAST		GENMASK(13, 0)

#define VCD_FIFO			0x805c
#define  VCD_FIFO_TH			0x100350ff

#define VCD_FB_SIZE			0x500000 /* support up to 1920 x 1200 */
#define VCD_KVM_BW_PCLK			120000000UL
#define VCD_TIMEOUT_US			300000

/* ECE Registers */
#define ECE_DDA_CTRL			0x0000
#define  ECE_DDA_CTRL_ECEEN		BIT(0)
#define  ECE_DDA_CTRL_INTEN		BIT(8)

#define ECE_DDA_STS			0x0004
#define  ECE_DDA_STS_CDREADY		BIT(8)
#define  ECE_DDA_STS_ACDRDY		BIT(10)

#define ECE_FBR_BA			0x0008
#define ECE_ED_BA			0x000c
#define ECE_RECT_XY			0x0010

#define ECE_RECT_DIMEN			0x0014
#define  ECE_RECT_DIMEN_WR		GENMASK(10, 0)
#define  ECE_RECT_DIMEN_WLTR		GENMASK(14, 11)
#define  ECE_RECT_DIMEN_HR		GENMASK(26, 16)
#define  ECE_RECT_DIMEN_HLTR		GENMASK(30, 27)

#define ECE_RESOL			0x001c
#define  ECE_RESOL_FB_LP_512		0
#define  ECE_RESOL_FB_LP_1024		1
#define  ECE_RESOL_FB_LP_2048		2
#define  ECE_RESOL_FB_LP_2560		3
#define  ECE_RESOL_FB_LP_4096		4

#define ECE_HEX_CTRL			0x0040
#define  ECE_HEX_CTRL_ENCDIS		BIT(0)
#define  ECE_HEX_CTRL_ENC_GAP		GENMASK(12, 8)

#define ECE_HEX_RECT_OFFSET		0x0048
#define  ECE_HEX_RECT_OFFSET_MASK	GENMASK(22, 0)

#define ECE_TILE_W			16
#define ECE_TILE_H			16
#define ECE_POLL_TIMEOUT_US		300000

/* GCR Registers */
#define INTCR				0x3c
#define  INTCR_GFXIFDIS			GENMASK(9, 8)
#define  INTCR_DEHS			BIT(27)

#define INTCR2				0x60
#define  INTCR2_GIRST2			BIT(2)
#define  INTCR2_GIHCRST			BIT(5)
#define  INTCR2_GIVCRST			BIT(6)

/* GFXI Register */
#define DISPST				0x00
#define  DISPST_HSCROFF			BIT(1)
#define  DISPST_MGAMODE			BIT(7)

#define HVCNTL				0x10
#define  HVCNTL_MASK			GENMASK(7, 0)

#define HVCNTH				0x14
#define  HVCNTH_MASK			GENMASK(2, 0)

#define VVCNTL				0x20
#define  VVCNTL_MASK			GENMASK(7, 0)

#define VVCNTH				0x24
#define  VVCNTH_MASK			GENMASK(2, 0)

#define GPLLINDIV			0x40
#define  GPLLINDIV_MASK			GENMASK(5, 0)
#define  GPLLINDIV_GPLLFBDV8		BIT(7)

#define GPLLFBDIV			0x44
#define  GPLLFBDIV_MASK			GENMASK(7, 0)

#define GPLLST				0x48
#define  GPLLST_PLLOTDIV1		GENMASK(2, 0)
#define  GPLLST_PLLOTDIV2		GENMASK(5, 3)
#define  GPLLST_GPLLFBDV109		GENMASK(7, 6)

#endif /* _NPCM_REGS_H */
