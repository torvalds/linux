/*
 * Copyright (C) 2008 Andres Salomon <dilinger@debian.org>
 *
 * Geode GX2 register tables
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _GXFB_H_
#define _GXFB_H_

#include <linux/io.h>

/* Display Controller registers (table 6-38 from the data book) */
enum dc_registers {
	DC_UNLOCK = 0,
	DC_GENERAL_CFG,
	DC_DISPLAY_CFG,
	DC_RSVD_0,

	DC_FB_ST_OFFSET,
	DC_CB_ST_OFFSET,
	DC_CURS_ST_OFFSET,
	DC_ICON_ST_OFFSET,

	DC_VID_Y_ST_OFFSET,
	DC_VID_U_ST_OFFSET,
	DC_VID_V_ST_OFFSET,
	DC_RSVD_1,

	DC_LINE_SIZE,
	DC_GFX_PITCH,
	DC_VID_YUV_PITCH,
	DC_RSVD_2,

	DC_H_ACTIVE_TIMING,
	DC_H_BLANK_TIMING,
	DC_H_SYNC_TIMING,
	DC_RSVD_3,

	DC_V_ACTIVE_TIMING,
	DC_V_BLANK_TIMING,
	DC_V_SYNC_TIMING,
	DC_RSVD_4,

	DC_CURSOR_X,
	DC_CURSOR_Y,
	DC_ICON_X,
	DC_LINE_CNT,

	DC_PAL_ADDRESS,
	DC_PAL_DATA,
	DC_DFIFO_DIAG,
	DC_CFIFO_DIAG,

	DC_VID_DS_DELTA,
	DC_GLIU0_MEM_OFFSET,
	DC_RSVD_5,
	DC_DV_ACC, /* 0x8c */
};

#define DC_UNLOCK_LOCK			0x00000000
#define DC_UNLOCK_UNLOCK		0x00004758	/* magic value */

#define DC_GENERAL_CFG_YUVM		(1 << 20)
#define DC_GENERAL_CFG_VDSE		(1 << 19)
#define DC_GENERAL_CFG_DFHPEL_SHIFT	12
#define DC_GENERAL_CFG_DFHPSL_SHIFT	8
#define DC_GENERAL_CFG_DECE		(1 << 6)
#define DC_GENERAL_CFG_CMPE		(1 << 5)
#define DC_GENERAL_CFG_VIDE		(1 << 3)
#define DC_GENERAL_CFG_ICNE		(1 << 2)
#define DC_GENERAL_CFG_CURE		(1 << 1)
#define DC_GENERAL_CFG_DFLE		(1 << 0)

#define DC_DISPLAY_CFG_A20M		(1 << 31)
#define DC_DISPLAY_CFG_A18M		(1 << 30)
#define DC_DISPLAY_CFG_PALB		(1 << 25)
#define DC_DISPLAY_CFG_DISP_MODE_24BPP	(1 << 9)
#define DC_DISPLAY_CFG_DISP_MODE_16BPP	(1 << 8)
#define DC_DISPLAY_CFG_DISP_MODE_8BPP	(0)
#define DC_DISPLAY_CFG_VDEN		(1 << 4)
#define DC_DISPLAY_CFG_GDEN		(1 << 3)
#define DC_DISPLAY_CFG_TGEN		(1 << 0)


/*
 * Video Processor registers (table 6-54).
 * There is space for 64 bit values, but we never use more than the
 * lower 32 bits.  The actual register save/restore code only bothers
 * to restore those 32 bits.
 */
enum vp_registers {
	VP_VCFG = 0,
	VP_DCFG,

	VP_VX,
	VP_VY,

	VP_VS,
	VP_VCK,

	VP_VCM,
	VP_GAR,

	VP_GDR,
	VP_RSVD_0,

	VP_MISC,
	VP_CCS,

	VP_RSVD_1,
	VP_RSVD_2,

	VP_RSVD_3,
	VP_VDC,

	VP_VCO,
	VP_CRC,

	VP_CRC32,
	VP_VDE,

	VP_CCK,
	VP_CCM,

	VP_CC1,
	VP_CC2,

	VP_A1X,
	VP_A1Y,

	VP_A1C,
	VP_A1T,

	VP_A2X,
	VP_A2Y,

	VP_A2C,
	VP_A2T,

	VP_A3X,
	VP_A3Y,

	VP_A3C,
	VP_A3T,

	VP_VRR,
	VP_AWT,

	VP_VTM, /* 0x130 */
};

#define VP_VCFG_VID_EN			(1 << 0)

#define VP_DCFG_DAC_VREF		(1 << 26)
#define VP_DCFG_GV_GAM			(1 << 21)
#define VP_DCFG_VG_CK			(1 << 20)
#define VP_DCFG_CRT_SYNC_SKW_DEFAULT	(1 << 16)
#define VP_DCFG_CRT_SYNC_SKW		((1 << 14) | (1 << 15) | (1 << 16))
#define VP_DCFG_CRT_VSYNC_POL		(1 << 9)
#define VP_DCFG_CRT_HSYNC_POL		(1 << 8)
#define VP_DCFG_FP_DATA_EN		(1 << 7)	/* undocumented */
#define VP_DCFG_FP_PWR_EN		(1 << 6)	/* undocumented */
#define VP_DCFG_DAC_BL_EN		(1 << 3)
#define VP_DCFG_VSYNC_EN		(1 << 2)
#define VP_DCFG_HSYNC_EN		(1 << 1)
#define VP_DCFG_CRT_EN			(1 << 0)

#define VP_MISC_GAM_EN			(1 << 0)
#define VP_MISC_DACPWRDN		(1 << 10)
#define VP_MISC_APWRDN			(1 << 11)


/*
 * Flat Panel registers (table 6-55).
 * Also 64 bit registers; see above note about 32-bit handling.
 */

/* we're actually in the VP register space, starting at address 0x400 */
#define VP_FP_START		0x400

enum fp_registers {
	FP_PT1 = 0,
	FP_PT2,

	FP_PM,
	FP_DFC,

	FP_BLFSR,
	FP_RLFSR,

	FP_FMI,
	FP_FMD,

	FP_RSVD_0,
	FP_DCA,

	FP_DMD,
	FP_CRC,

	FP_FBB, /* 0x460 */
};

#define FP_PT1_VSIZE_SHIFT		16		/* undocumented? */
#define FP_PT1_VSIZE_MASK		0x7FF0000	/* undocumented? */

#define FP_PT2_HSP			(1 << 22)
#define FP_PT2_VSP			(1 << 23)

#define FP_PM_P				(1 << 24)       /* panel power on */
#define FP_PM_PANEL_PWR_UP		(1 << 3)        /* r/o */
#define FP_PM_PANEL_PWR_DOWN		(1 << 2)        /* r/o */
#define FP_PM_PANEL_OFF			(1 << 1)        /* r/o */
#define FP_PM_PANEL_ON			(1 << 0)        /* r/o */

#define FP_DFC_NFI			((1 << 4) | (1 << 5) | (1 << 6))


/* register access functions */

static inline uint32_t read_dc(struct geodefb_par *par, int reg)
{
	return readl(par->dc_regs + 4*reg);
}

static inline void write_dc(struct geodefb_par *par, int reg, uint32_t val)
{
	writel(val, par->dc_regs + 4*reg);
}


static inline uint32_t read_vp(struct geodefb_par *par, int reg)
{
	return readl(par->vid_regs + 8*reg);
}

static inline void write_vp(struct geodefb_par *par, int reg, uint32_t val)
{
	writel(val, par->vid_regs + 8*reg);
}

static inline uint32_t read_fp(struct geodefb_par *par, int reg)
{
	return readl(par->vid_regs + 8*reg + VP_FP_START);
}

static inline void write_fp(struct geodefb_par *par, int reg, uint32_t val)
{
	writel(val, par->vid_regs + 8*reg + VP_FP_START);
}

#endif
