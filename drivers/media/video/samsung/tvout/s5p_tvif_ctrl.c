/* linux/drivers/media/video/samsung/tvout/s5p_tvif_ctrl.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Tvout ctrl class for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*****************************************************************************
 * This file includes functions for ctrl classes of TVOUT driver.
 * There are 3 ctrl classes. (tvif, hdmi, sdo)
 *	- tvif ctrl class: controls hdmi and sdo ctrl class.
 *      - hdmi ctrl class: contrls hdmi hardware by using hw_if/hdmi.c
 *      - sdo  ctrl class: contrls sdo hardware by using hw_if/sdo.c
 *
 *                      +-----------------+
 *                      | tvif ctrl class |
 *                      +-----------------+
 *                             |   |
 *                  +----------+   +----------+		    ctrl class layer
 *                  |                         |
 *                  V                         V
 *         +-----------------+       +-----------------+
 *         | sdo ctrl class  |       | hdmi ctrl class |
 *         +-----------------+       +-----------------+
 *                  |                         |
 *   ---------------+-------------------------+------------------------------
 *                  V                         V
 *         +-----------------+       +-----------------+
 *         |   hw_if/sdo.c   |       |   hw_if/hdmi.c  |         hw_if layer
 *         +-----------------+       +-----------------+
 *                  |                         |
 *   ---------------+-------------------------+------------------------------
 *                  V                         V
 *         +-----------------+       +-----------------+
 *         |   sdo hardware  |       |   hdmi hardware |	  Hardware
 *         +-----------------+       +-----------------+
 *
 ****************************************************************************/
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <plat/clock.h>
#if defined(CONFIG_BUSFREQ_OPP) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
#include <mach/dev.h>
#endif
#include <mach/regs-hdmi.h>

#ifdef CONFIG_HDMI_TX_STRENGTH
#include <plat/tvout.h>
#endif


#include "s5p_tvout_common_lib.h"
#include "hw_if/hw_if.h"
#include "s5p_tvout_ctrl.h"

#ifdef CONFIG_HDMI_14A_3D
static struct s5p_hdmi_v_format s5p_hdmi_v_fmt[] = {
	[v720x480p_60Hz] = {
		.frame = {
			.vH_Line = 0x035a,
			.vV_Line = 0x020d,
			.vH_SYNC_START = 0x000e,
			.vH_SYNC_END = 0x004c,
			.vV1_Blank = 0x002d,
			.vV2_Blank = 0x020d,
			.vHBlank = 0x008a,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x9,
			.vVSYNC_LINE_BEF_2 = 0x000f,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 1,
			.Vsync_polarity = 1,
			.interlaced = 0,
			.vAVI_VIC = 2,
			.vAVI_VIC_16_9 = 3,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_27_027,
		},
		.tg_H_FSZ = 0x35a,
		.tg_HACT_ST = 0x8a,
		.tg_HACT_SZ = 0x2d0,
		.tg_V_FSZ = 0x20d,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x2d,
		.tg_VACT_SZ = 0x1e0,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_60Hz] = {
		.frame = {
			.vH_Line = 0x0672,
			.vV_Line = 0x02ee,
			.vH_SYNC_START = 0x006c,
			.vH_SYNC_END = 0x0094,
			.vV1_Blank = 0x001e,
			.vV2_Blank = 0x02ee,
			.vHBlank = 0x0172,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x5,
			.vVSYNC_LINE_BEF_2 = 0x000a,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 4,
			.vAVI_VIC_16_9 = 4,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0x672,
		.tg_HACT_ST = 0x172,
		.tg_HACT_SZ = 0x500,
		.tg_V_FSZ = 0x2ee,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x1e,
		.tg_VACT_SZ = 0x2d0,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080i_60Hz] = {
		.frame = {
			.vH_Line = 0x0898,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x0056,
			.vH_SYNC_END = 0x0082,
			.vV1_Blank = 0x0016,
			.vV2_Blank = 0x0232,
			.vHBlank = 0x0118,
			.VBLANK_F0 = 0x0249,
			.VBLANK_F1 = 0x0465,
			.vVSYNC_LINE_BEF_1 = 0x2,
			.vVSYNC_LINE_BEF_2 = 0x0007,
			.vVSYNC_LINE_AFT_1 = 0x0234,
			.vVSYNC_LINE_AFT_2 = 0x0239,
			.vVSYNC_LINE_AFT_PXL_1 = 0x04a4,
			.vVSYNC_LINE_AFT_PXL_2 = 0x04a4,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 1,
			.vAVI_VIC = 5,
			.vAVI_VIC_16_9 = 5,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0x898,
		.tg_HACT_ST = 0x118,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x16,
		.tg_VACT_SZ = 0x21c,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x249,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x233,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_60Hz] = {
		.frame = {
			.vH_Line = 0x0898,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x0056,
			.vH_SYNC_END = 0x0082,
			.vV1_Blank = 0x002d,
			.vV2_Blank = 0x0465,
			.vHBlank = 0x0118,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x4,
			.vVSYNC_LINE_BEF_2 = 0x0009,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 16,
			.vAVI_VIC_16_9 = 16,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_148_500,
		},
		.tg_H_FSZ = 0x898,
		.tg_HACT_ST = 0x118,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x2d,
		.tg_VACT_SZ = 0x438,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v720x576p_50Hz] = {
		.frame = {
			.vH_Line = 0x0360,
			.vV_Line = 0x0271,
			.vH_SYNC_START = 0x000a,
			.vH_SYNC_END = 0x004a,
			.vV1_Blank = 0x0031,
			.vV2_Blank = 0x0271,
			.vHBlank = 0x0090,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x5,
			.vVSYNC_LINE_BEF_2 = 0x000a,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 1,
			.Vsync_polarity = 1,
			.interlaced = 0,
			.vAVI_VIC = 17,
			.vAVI_VIC_16_9 = 18,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_27,
		},
		.tg_H_FSZ = 0x360,
		.tg_HACT_ST = 0x90,
		.tg_HACT_SZ = 0x2d0,
		.tg_V_FSZ = 0x271,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x31,
		.tg_VACT_SZ = 0x240,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_50Hz] = {
		.frame = {
			.vH_Line = 0x07BC,
			.vV_Line = 0x02EE,
			.vH_SYNC_START = 0x01b6,
			.vH_SYNC_END = 0x01de,
			.vV1_Blank = 0x001E,
			.vV2_Blank = 0x02EE,
			.vHBlank = 0x02BC,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x5,
			.vVSYNC_LINE_BEF_2 = 0x000a,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 19,
			.vAVI_VIC_16_9 = 19,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0x7bc,
		.tg_HACT_ST = 0x2bc,
		.tg_HACT_SZ = 0x500,
		.tg_V_FSZ = 0x2ee,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x1e,
		.tg_VACT_SZ = 0x2d0,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080i_50Hz] = {
		.frame = {
			.vH_Line = 0x0A50,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x020e,
			.vH_SYNC_END = 0x023a,
			.vV1_Blank = 0x0016,
			.vV2_Blank = 0x0232,
			.vHBlank = 0x02D0,
			.VBLANK_F0 = 0x0249,
			.VBLANK_F1 = 0x0465,
			.vVSYNC_LINE_BEF_1 = 0x2,
			.vVSYNC_LINE_BEF_2 = 0x0007,
			.vVSYNC_LINE_AFT_1 = 0x0234,
			.vVSYNC_LINE_AFT_2 = 0x0239,
			.vVSYNC_LINE_AFT_PXL_1 = 0x0738,
			.vVSYNC_LINE_AFT_PXL_2 = 0x0738,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 1,
			.vAVI_VIC = 20,
			.vAVI_VIC_16_9 = 20,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0xa50,
		.tg_HACT_ST = 0x2d0,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x16,
		.tg_VACT_SZ = 0x21c,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x249,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x233,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_50Hz] = {
		.frame = {
			.vH_Line = 0x0A50,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x020e,
			.vH_SYNC_END = 0x023a,
			.vV1_Blank = 0x002D,
			.vV2_Blank = 0x0465,
			.vHBlank = 0x02D0,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x4,
			.vVSYNC_LINE_BEF_2 = 0x0009,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 31,
			.vAVI_VIC_16_9 = 31,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_148_500,
		},
		.tg_H_FSZ = 0xa50,
		.tg_HACT_ST = 0x2d0,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x2d,
		.tg_VACT_SZ = 0x438,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_30Hz] = {
		.frame = {
			.vH_Line = 0x0898,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x056,
			.vH_SYNC_END = 0x082,
			.vV1_Blank = 0x002D,
			.vV2_Blank = 0x0465,
			.vHBlank = 0x0118,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x4,
			.vVSYNC_LINE_BEF_2 = 0x0009,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 34,
			.vAVI_VIC_16_9 = 34,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_176,
		},
		.tg_H_FSZ = 0x898,
		.tg_HACT_ST = 0x118,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x2d,
		.tg_VACT_SZ = 0x438,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v720x480p_59Hz] = {
		.frame = {
			.vH_Line = 0x035a,
			.vV_Line = 0x020d,
			.vH_SYNC_START = 0x000e,
			.vH_SYNC_END = 0x004c,
			.vV1_Blank = 0x002D,
			.vV2_Blank = 0x020d,
			.vHBlank = 0x008a,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x9,
			.vVSYNC_LINE_BEF_2 = 0x000f,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 1,
			.Vsync_polarity = 1,
			.interlaced = 0,
			.vAVI_VIC = 2,
			.vAVI_VIC_16_9 = 3,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_27,
		},
		.tg_H_FSZ = 0x35a,
		.tg_HACT_ST = 0x8a,
		.tg_HACT_SZ = 0x2d0,
		.tg_V_FSZ = 0x20d,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x2d,
		.tg_VACT_SZ = 0x1e0,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_59Hz] = {
		.frame = {
			.vH_Line = 0x0672,
			.vV_Line = 0x02ee,
			.vH_SYNC_START = 0x006c,
			.vH_SYNC_END = 0x0094,
			.vV1_Blank = 0x001e,
			.vV2_Blank = 0x02ee,
			.vHBlank = 0x0172,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x5,
			.vVSYNC_LINE_BEF_2 = 0x000a,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 4,
			.vAVI_VIC_16_9 = 4,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_176,
		},
		.tg_H_FSZ = 0x672,
		.tg_HACT_ST = 0x172,
		.tg_HACT_SZ = 0x500,
		.tg_V_FSZ = 0x2ee,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x1e,
		.tg_VACT_SZ = 0x2d0,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080i_59Hz] = {
		.frame = {
			.vH_Line = 0x0898,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x0056,
			.vH_SYNC_END = 0x0082,
			.vV1_Blank = 0x0016,
			.vV2_Blank = 0x0232,
			.vHBlank = 0x0118,
			.VBLANK_F0 = 0x0249,
			.VBLANK_F1 = 0x0465,
			.vVSYNC_LINE_BEF_1 = 0x2,
			.vVSYNC_LINE_BEF_2 = 0x0007,
			.vVSYNC_LINE_AFT_1 = 0x0234,
			.vVSYNC_LINE_AFT_2 = 0x0239,
			.vVSYNC_LINE_AFT_PXL_1 = 0x04a4,
			.vVSYNC_LINE_AFT_PXL_2 = 0x04a4,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 1,
			.vAVI_VIC = 5,
			.vAVI_VIC_16_9 = 5,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_176,
		},
		.tg_H_FSZ = 0x898,
		.tg_HACT_ST = 0x118,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x16,
		.tg_VACT_SZ = 0x21c,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x249,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x233,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_59Hz] = {
		.frame = {
			.vH_Line = 0x0898,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x0056,
			.vH_SYNC_END = 0x0082,
			.vV1_Blank = 0x002d,
			.vV2_Blank = 0x0465,
			.vHBlank = 0x0118,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x4,
			.vVSYNC_LINE_BEF_2 = 0x0009,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 16,
			.vAVI_VIC_16_9 = 16,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_148_352,
		},
		.tg_H_FSZ = 0x898,
		.tg_HACT_ST = 0x118,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x2d,
		.tg_VACT_SZ = 0x438,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_60Hz_SBS_HALF] = {
		.frame = {
			.vH_Line = 0x0672,
			.vV_Line = 0x02ee,
			.vH_SYNC_START = 0x006c,
			.vH_SYNC_END = 0x0094,
			.vV1_Blank = 0x001e,
			.vV2_Blank = 0x02ee,
			.vHBlank = 0x0172,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x5,
			.vVSYNC_LINE_BEF_2 = 0x000a,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 4,
			.vAVI_VIC_16_9 = 4,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0x672,
		.tg_HACT_ST = 0x172,
		.tg_HACT_SZ = 0x500,
		.tg_V_FSZ = 0x2ee,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x1e,
		.tg_VACT_SZ = 0x2d0,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x30c,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_59Hz_SBS_HALF] = {
		.frame = {
			.vH_Line = 0x0672,
			.vV_Line = 0x02ee,
			.vH_SYNC_START = 0x006c,
			.vH_SYNC_END = 0x0094,
			.vV1_Blank = 0x001e,
			.vV2_Blank = 0x02ee,
			.vHBlank = 0x0172,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x5,
			.vVSYNC_LINE_BEF_2 = 0x000a,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 4,
			.vAVI_VIC_16_9 = 4,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0x672,
		.tg_HACT_ST = 0x172,
		.tg_HACT_SZ = 0x500,
		.tg_V_FSZ = 0x2ee,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x1e,
		.tg_VACT_SZ = 0x2d0,
		.tg_FIELD_CHG = 0x0,
		.tg_VACT_ST2 = 0x30c,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_50Hz_TB] = {
		.frame = {
			.vH_Line = 0x07bc,
			.vV_Line = 0x02ee,
			.vH_SYNC_START = 0x01b6,
			.vH_SYNC_END = 0x01de,
			.vV1_Blank = 0x001e,
			.vV2_Blank = 0x02ee,
			.vHBlank = 0x02bc,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x5,
			.vVSYNC_LINE_BEF_2 = 0x000a,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 19,
			.vAVI_VIC_16_9 = 19,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0x7bc,
		.tg_HACT_ST = 0x2bc,
		.tg_HACT_SZ = 0x500,
		.tg_V_FSZ = 0x2ee,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x1e,
		.tg_VACT_SZ = 0x2d0,
		.tg_FIELD_CHG = 0x0,
		.tg_VACT_ST2 = 0x30c,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_24Hz_TB] = {
		.frame = {
			.vH_Line = 0x0abe,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x027c,
			.vH_SYNC_END = 0x02a8,
			.vV1_Blank = 0x002d,
			.vV2_Blank = 0x0465,
			.vHBlank = 0x033e,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x4,
			.vVSYNC_LINE_BEF_2 = 0x0009,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 32,
			.vAVI_VIC_16_9 = 32,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0xabe,
		.tg_HACT_ST = 0x33e,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x2d,
		.tg_VACT_SZ = 0x438,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_23Hz_TB] = {
		.frame = {
			.vH_Line = 0x0abe,
			.vV_Line = 0x0465,
			.vH_SYNC_START = 0x027c,
			.vH_SYNC_END = 0x02a8,
			.vV1_Blank = 0x002d,
			.vV2_Blank = 0x0465,
			.vHBlank = 0x033e,
			.VBLANK_F0 = 0xffff,
			.VBLANK_F1 = 0xffff,
			.vVSYNC_LINE_BEF_1 = 0x4,
			.vVSYNC_LINE_BEF_2 = 0x0009,
			.vVSYNC_LINE_AFT_1 = 0xffff,
			.vVSYNC_LINE_AFT_2 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_1 = 0xffff,
			.vVSYNC_LINE_AFT_PXL_2 = 0xffff,
			.vVACT_SPACE_1 = 0xffff,
			.vVACT_SPACE_2 = 0xffff,
			.Hsync_polarity = 0,
			.Vsync_polarity = 0,
			.interlaced = 0,
			.vAVI_VIC = 32,
			.vAVI_VIC_16_9 = 32,
			.repetition = 0,
			.pixel_clock = ePHY_FREQ_74_250,
		},
		.tg_H_FSZ = 0xabe,
		.tg_HACT_ST = 0x33e,
		.tg_HACT_SZ = 0x780,
		.tg_V_FSZ = 0x465,
		.tg_VSYNC = 0x1,
		.tg_VSYNC2 = 0x233,
		.tg_VACT_ST = 0x2d,
		.tg_VACT_SZ = 0x438,
		.tg_FIELD_CHG = 0x233,
		.tg_VACT_ST2 = 0x248,
		.tg_VACT_ST3 = 0x0,
		.tg_VACT_ST4 = 0x0,
		.tg_VSYNC_TOP_HDMI = 0x1,
		.tg_VSYNC_BOT_HDMI = 0x1,
		.tg_FIELD_TOP_HDMI = 0x1,
		.tg_FIELD_BOT_HDMI = 0x233,
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

};
#else
static struct s5p_hdmi_v_format s5p_hdmi_v_fmt[] = {
	[v720x480p_60Hz] = {
		.frame = {
			.vic		= 2,
			.vic_16_9	= 3,
			.repetition	= 0,
			.polarity	= 1,
			.i_p		= 0,
			.h_active	= 720,
			.v_active	= 480,
			.h_total	= 858,
			.h_blank	= 138,
			.v_total	= 525,
			.v_blank	= 45,
			.pixel_clock	= ePHY_FREQ_27_027,
		},
		.h_sync = {
			.begin		= 0xe,
			.end		= 0x4c,
		},
		.v_sync_top = {
			.begin		= 0x9,
			.end		= 0xf,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_60Hz] = {
		.frame = {
			.vic		= 4,
			.vic_16_9	= 4,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 0,
			.h_active	= 1280,
			.v_active	= 720,
			.h_total	= 1650,
			.h_blank	= 370,
			.v_total	= 750,
			.v_blank	= 30,
			.pixel_clock	= ePHY_FREQ_74_250,
		},
		.h_sync = {
			.begin		= 0x6c,
			.end		= 0x94,
		},
		.v_sync_top = {
			.begin		= 0x5,
			.end		= 0xa,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080i_60Hz] = {
		.frame = {
			.vic		= 5,
			.vic_16_9	= 5,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 1,
			.h_active	= 1920,
			.v_active	= 540,
			.h_total	= 2200,
			.h_blank	= 280,
			.v_total	= 1125,
			.v_blank	= 22,
			.pixel_clock	= ePHY_FREQ_74_250,
		},
		.h_sync = {
			.begin		= 0x56,
			.end		= 0x82,
		},
		.v_sync_top = {
			.begin		= 0x2,
			.end		= 0x7,
		},
		.v_sync_bottom = {
			.begin		= 0x234,
			.end		= 0x239,
		},
		.v_sync_h_pos = {
			.begin		= 0x4a4,
			.end		= 0x4a4,
		},
		.v_blank_f = {
			.begin		= 0x249,
			.end		= 0x465,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_60Hz] = {
		.frame = {
			.vic		= 16,
			.vic_16_9	= 16,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 0,
			.h_active	= 1920,
			.v_active	= 1080,
			.h_total	= 2200,
			.h_blank	= 280,
			.v_total	= 1125,
			.v_blank	= 45,
			.pixel_clock	= ePHY_FREQ_148_500,
		},
		.h_sync = {
			.begin		= 0x56,
			.end		= 0x82,
		},
		.v_sync_top = {
			.begin		= 0x4,
			.end		= 0x9,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v720x576p_50Hz] = {
		.frame = {
			.vic		= 17,
			.vic_16_9	= 18,
			.repetition	= 0,
			.polarity	= 1,
			.i_p		= 0,
			.h_active	= 720,
			.v_active	= 576,
			.h_total	= 864,
			.h_blank	= 144,
			.v_total	= 625,
			.v_blank	= 49,
			.pixel_clock	= ePHY_FREQ_27,
		},
		.h_sync = {
			.begin		= 0xa,
			.end		= 0x4a,
		},
		.v_sync_top = {
			.begin		= 0x5,
			.end		= 0xa,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_50Hz] = {
		.frame = {
			.vic		= 19,
			.vic_16_9	= 19,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 0,
			.h_active	= 1280,
			.v_active	= 720,
			.h_total	= 1980,
			.h_blank	= 700,
			.v_total	= 750,
			.v_blank	= 30,
			.pixel_clock	= ePHY_FREQ_74_250,
		},
		.h_sync = {
			.begin		= 0x1b6,
			.end		= 0x1de,
		},
		.v_sync_top = {
			.begin		= 0x5,
			.end		= 0xa,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080i_50Hz] = {
		.frame = {
			.vic		= 20,
			.vic_16_9	= 20,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 1,
			.h_active	= 1920,
			.v_active	= 540,
			.h_total	= 2640,
			.h_blank	= 720,
			.v_total	= 1125,
			.v_blank	= 22,
			.pixel_clock	= ePHY_FREQ_74_250,
		},
		.h_sync = {
			.begin		= 0x20e,
			.end		= 0x23a,
		},
		.v_sync_top = {
			.begin		= 0x2,
			.end		= 0x7,
		},
		.v_sync_bottom = {
			.begin		= 0x234,
			.end		= 0x239,
		},
		.v_sync_h_pos = {
			.begin		= 0x738,
			.end		= 0x738,
		},
		.v_blank_f = {
			.begin		= 0x249,
			.end		= 0x465,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_50Hz] = {
		.frame = {
			.vic		= 31,
			.vic_16_9	= 31,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 0,
			.h_active	= 1920,
			.v_active	= 1080,
			.h_total	= 2640,
			.h_blank	= 720,
			.v_total	= 1125,
			.v_blank	= 45,
			.pixel_clock	= ePHY_FREQ_148_500,
		},
		.h_sync = {
			.begin		= 0x20e,
			.end		= 0x23a,
		},
		.v_sync_top = {
			.begin		= 0x4,
			.end		= 0x9,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_30Hz] = {
		.frame = {
			.vic		= 34,
			.vic_16_9	= 34,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 0,
			.h_active	= 1920,
			.v_active	= 1080,
			.h_total	= 2200,
			.h_blank	= 280,
			.v_total	= 1125,
			.v_blank	= 45,
			.pixel_clock	= ePHY_FREQ_74_250,
		},
		.h_sync = {
			.begin		= 0x56,
			.end		= 0x82,
		},
		.v_sync_top = {
			.begin		= 0x4,
			.end		= 0x9,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v720x480p_59Hz] = {
		.frame = {
			.vic		= 2,
			.vic_16_9	= 3,
			.repetition	= 0,
			.polarity	= 1,
			.i_p		= 0,
			.h_active	= 720,
			.v_active	= 480,
			.h_total	= 858,
			.h_blank	= 138,
			.v_total	= 525,
			.v_blank	= 45,
			.pixel_clock	= ePHY_FREQ_27,
		},
		.h_sync = {
			.begin		= 0xe,
			.end		= 0x4c,
		},
		.v_sync_top = {
			.begin		= 0x9,
			.end		= 0xf,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1280x720p_59Hz] = {
		.frame = {
			.vic		= 4,
			.vic_16_9	= 4,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 0,
			.h_active	= 1280,
			.v_active	= 720,
			.h_total	= 1650,
			.h_blank	= 370,
			.v_total	= 750,
			.v_blank	= 30,
			.pixel_clock	= ePHY_FREQ_74_176,
		},
		.h_sync = {
			.begin		= 0x6c,
			.end		= 0x94,
		},
		.v_sync_top = {
			.begin		= 0x5,
			.end		= 0xa,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080i_59Hz] = {
		.frame = {
			.vic		= 5,
			.vic_16_9	= 5,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 1,
			.h_active	= 1920,
			.v_active	= 540,
			.h_total	= 2200,
			.h_blank	= 280,
			.v_total	= 1125,
			.v_blank	= 22,
			.pixel_clock	= ePHY_FREQ_74_176,
		},
		.h_sync = {
			.begin		= 0x56,
			.end		= 0x82,
		},
		.v_sync_top = {
			.begin		= 0x2,
			.end		= 0x7,
		},
		.v_sync_bottom = {
			.begin		= 0x234,
			.end		= 0x239,
		},
		.v_sync_h_pos = {
			.begin		= 0x4a4,
			.end		= 0x4a4,
		},
		.v_blank_f = {
			.begin		= 0x249,
			.end		= 0x465,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},

	[v1920x1080p_59Hz] = {
		.frame = {
			.vic		= 16,
			.vic_16_9	= 16,
			.repetition	= 0,
			.polarity	= 0,
			.i_p		= 0,
			.h_active	= 1920,
			.v_active	= 1080,
			.h_total	= 2200,
			.h_blank	= 280,
			.v_total	= 1125,
			.v_blank	= 45,
			.pixel_clock	= ePHY_FREQ_148_352,
		},
		.h_sync = {
			.begin		= 0x56,
			.end		= 0x82,
		},
		.v_sync_top = {
			.begin		= 0x4,
			.end		= 0x9,
		},
		.v_sync_bottom = {
			.begin		= 0,
			.end		= 0,
		},
		.v_sync_h_pos = {
			.begin		= 0,
			.end		= 0,
		},
		.v_blank_f = {
			.begin		= 0,
			.end		= 0,
		},
		.mhl_hsync = 0xf,
		.mhl_vsync = 0x1,
	},
};
#endif

static struct s5p_hdmi_o_params s5p_hdmi_output[] = {
	{
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00},
	}, {
		{0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04},
		{0x40, 0x00, 0x02, 0x40, 0x00},
	}, {
		{0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04},
		{0x00, 0x00, 0x02, 0x20, 0x00},
	}, {
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x22, 0x01, 0x20, 0x01},
	},
};

static struct s5p_hdmi_ctrl_private_data s5p_hdmi_ctrl_private = {
	.vendor		= "SAMSUNG",
	.product	= "S5PC210",

	.blue_screen = {
		.enable = false,
#ifdef	CONFIG_HDMI_14A_3D
		.b	= 0,
		.g	= 0,
		.r	= 0,
#else
		.cb_b	= 0,
		.y_g	= 0,
		.cr_r	= 0,
#endif
	},

	.video = {
		.color_r = {
			.y_min = 0x10,
			.y_max = 0xeb,
			.c_min = 0x10,
			.c_max = 0xf0,
		},
		.depth	= HDMI_CD_24,
		.q_range = HDMI_Q_LIMITED_RANGE,
	},

	.packet = {
		.vsi_info = {0x81, 0x1, 27},
		.avi_info = {0x82, 0x2, 13},
		.spd_info = {0x83, 0x1, 27},
		.aui_info = {0x84, 0x1, 0x0a},
		.mpg_info = {0x85, 0x1, 5},
	},

	.tg = {
		.correction_en	= false,
		.bt656_en	= false,
	},

	.hdcp_en = false,

	.audio = {
		.type	= HDMI_60958_AUDIO,
		.bit	= 16,
		.freq	= 44100,
		/* Support audio 5.1Ch */
#if defined(CONFIG_VIDEO_TVOUT_2CH_AUDIO)
		.channel = 2,
#else
		.channel = 6,
#endif
	},

	.av_mute	= false,
	.running	= false,

	.pow_name = "hdmi_pd",

	.clk[HDMI_PCLK] = {
		.name = "hdmi",
		.ptr = NULL
	},

	.clk[HDMI_MUX] = {
		.name = "sclk_hdmi",
		.ptr = NULL
	},

	.reg_mem[HDMI] = {
		.name = "s5p-hdmi",
		.res = NULL,
		.base = NULL
	},

	.reg_mem[HDMI_PHY] = {
		.name = "s5p-i2c-hdmi-phy",
		.res = NULL,
		.base = NULL
	},

	.irq = {
		.name = "s5p-hdmi",
		.handler = s5p_hdmi_irq,
		.no = -1
	}

};

static struct s5p_tvif_ctrl_private_data s5p_tvif_ctrl_private = {
	.curr_std = TVOUT_INIT_DISP_VALUE,
	.curr_if = TVOUT_INIT_O_VALUE,

	.running = false
};

#ifdef CONFIG_ANALOG_TVENC
struct s5p_sdo_ctrl_private_data {
	struct s5p_sdo_vscale_cfg		video_scale_cfg;
	struct s5p_sdo_vbi			vbi;
	struct s5p_sdo_offset_gain		offset_gain;
	struct s5p_sdo_delay			delay;
	struct s5p_sdo_bright_hue_saturation	bri_hue_sat;
	struct s5p_sdo_cvbs_compensation	cvbs_compen;
	struct s5p_sdo_component_porch		compo_porch;
	struct s5p_sdo_ch_xtalk_cancellat_coeff	xtalk_cc;
	struct s5p_sdo_closed_caption		closed_cap;
	struct s5p_sdo_525_data			wss_525;
	struct s5p_sdo_625_data			wss_625;
	struct s5p_sdo_525_data			cgms_525;
	struct s5p_sdo_625_data			cgms_625;

	bool			color_sub_carrier_phase_adj;

	bool			running;

	struct s5p_tvout_clk_info	clk[SDO_NO_OF_CLK];
	char			*pow_name;
	struct reg_mem_info	reg_mem;
};

static struct s5p_sdo_ctrl_private_data s5p_sdo_ctrl_private = {
	.clk[SDO_PCLK] = {
		.name			= "tvenc",
		.ptr			= NULL
	},
	.clk[SDO_MUX] = {
		.name			= "sclk_dac",
		.ptr			= NULL
	},
		.pow_name		= "tv_enc_pd",
	.reg_mem = {
		.name			= "s5p-sdo",
		.res			= NULL,
		.base			= NULL
	},

	.running			= false,

	.color_sub_carrier_phase_adj	= false,

	.vbi = {
		.wss_cvbs		= true,
		.caption_cvbs		= SDO_INS_OTHERS
	},

	.offset_gain = {
		.offset			= 0,
		.gain			= 0x800
	},

	.delay = {
		.delay_y		= 0x00,
		.offset_video_start	= 0xfa,
		.offset_video_end	= 0x00
	},

	.bri_hue_sat = {
		.bright_hue_sat_adj	= false,
		.gain_brightness	= 0x80,
		.offset_brightness	= 0x00,
		.gain0_cb_hue_sat	= 0x00,
		.gain1_cb_hue_sat	= 0x00,
		.gain0_cr_hue_sat	= 0x00,
		.gain1_cr_hue_sat	= 0x00,
		.offset_cb_hue_sat	= 0x00,
		.offset_cr_hue_sat	= 0x00
	},

	.cvbs_compen = {
		.cvbs_color_compen	= false,
		.y_lower_mid		= 0x200,
		.y_bottom		= 0x000,
		.y_top			= 0x3ff,
		.y_upper_mid		= 0x200,
		.radius			= 0x1ff
	},

	.compo_porch = {
		.back_525		= 0x8a,
		.front_525		= 0x359,
		.back_625		= 0x96,
		.front_625		= 0x35c
	},

	.xtalk_cc = {
		.coeff2			= 0,
		.coeff1			= 0
	},

	.closed_cap = {
		.display_cc		= 0,
		.nondisplay_cc		= 0
	},

	.wss_525 = {
		.copy_permit		= SDO_525_COPY_PERMIT,
		.mv_psp			= SDO_525_MV_PSP_OFF,
		.copy_info		= SDO_525_COPY_INFO,
		.analog_on		= false,
		.display_ratio		= SDO_525_4_3_NORMAL
	},

	.wss_625 = {
		.surround_sound		= false,
		.copyright		= false,
		.copy_protection	= false,
		.text_subtitles		= false,
		.open_subtitles		= SDO_625_NO_OPEN_SUBTITLES,
		.camera_film		= SDO_625_CAMERA,
		.color_encoding		= SDO_625_NORMAL_PAL,
		.helper_signal		= false,
		.display_ratio		= SDO_625_4_3_FULL_576
	},

	.cgms_525 = {
		.copy_permit		= SDO_525_COPY_PERMIT,
		.mv_psp			= SDO_525_MV_PSP_OFF,
		.copy_info		= SDO_525_COPY_INFO,
		.analog_on		= false,
		.display_ratio		= SDO_525_4_3_NORMAL
	},

	.cgms_625 = {
		.surround_sound		= false,
		.copyright		= false,
		.copy_protection	= false,
		.text_subtitles		= false,
		.open_subtitles		= SDO_625_NO_OPEN_SUBTITLES,
		.camera_film		= SDO_625_CAMERA,
		.color_encoding		= SDO_625_NORMAL_PAL,
		.helper_signal		= false,
		.display_ratio		= SDO_625_4_3_FULL_576
	},
};
#endif

/****************************************
 * Functions for sdo ctrl class
 ***************************************/
#ifdef CONFIG_ANALOG_TVENC

static void s5p_sdo_ctrl_init_private(void)
{
}

static int s5p_sdo_ctrl_set_reg(enum s5p_tvout_disp_mode disp_mode)
{
	struct s5p_sdo_ctrl_private_data *private = &s5p_sdo_ctrl_private;

	s5p_sdo_sw_reset(1);

	if (s5p_sdo_set_display_mode(disp_mode, SDO_O_ORDER_COMPOSITE_Y_C_CVBS))
		return -1;

	if (s5p_sdo_set_video_scale_cfg(
		private->video_scale_cfg.composite_level,
		private->video_scale_cfg.composite_ratio))
		return -1;

	if (s5p_sdo_set_vbi(
		private->vbi.wss_cvbs, private->vbi.caption_cvbs))
		return -1;

	s5p_sdo_set_offset_gain(
		private->offset_gain.offset, private->offset_gain.gain);

	s5p_sdo_set_delay(
		private->delay.delay_y,
		private->delay.offset_video_start,
		private->delay.offset_video_end);

	s5p_sdo_set_schlock(private->color_sub_carrier_phase_adj);

	s5p_sdo_set_brightness_hue_saturation(private->bri_hue_sat);

	s5p_sdo_set_cvbs_color_compensation(private->cvbs_compen);

	s5p_sdo_set_component_porch(
		private->compo_porch.back_525,
		private->compo_porch.front_525,
		private->compo_porch.back_625,
		private->compo_porch.front_625);

	s5p_sdo_set_ch_xtalk_cancel_coef(
		private->xtalk_cc.coeff2, private->xtalk_cc.coeff1);

	s5p_sdo_set_closed_caption(
		private->closed_cap.display_cc,
		private->closed_cap.nondisplay_cc);

	if (s5p_sdo_set_wss525_data(private->wss_525))
		return -1;

	if (s5p_sdo_set_wss625_data(private->wss_625))
		return -1;

	if (s5p_sdo_set_cgmsa525_data(private->cgms_525))
		return -1;

	if (s5p_sdo_set_cgmsa625_data(private->cgms_625))
		return -1;

	s5p_sdo_set_interrupt_enable(0);

	s5p_sdo_clear_interrupt_pending();

	s5p_sdo_clock_on(1);
	s5p_sdo_dac_on(1);

	return 0;
}

static void s5p_sdo_ctrl_internal_stop(void)
{
	s5p_sdo_clock_on(0);
	s5p_sdo_dac_on(0);
}

static void s5p_sdo_ctrl_clock(bool on)
{
	if (on) {
		clk_enable(s5p_sdo_ctrl_private.clk[SDO_MUX].ptr);

#ifdef CONFIG_ARCH_EXYNOS4
		s5p_tvout_pm_runtime_get();
#endif
		/* Restore sdo_base address */
		s5p_sdo_init(s5p_sdo_ctrl_private.reg_mem.base);

		clk_enable(s5p_sdo_ctrl_private.clk[SDO_PCLK].ptr);
	} else {
		clk_disable(s5p_sdo_ctrl_private.clk[SDO_PCLK].ptr);

#ifdef CONFIG_ARCH_EXYNOS4
		s5p_tvout_pm_runtime_put();
#endif

		clk_disable(s5p_sdo_ctrl_private.clk[SDO_MUX].ptr);

		/* Set sdo_base address to NULL */
		s5p_sdo_init(NULL);
	}

	mdelay(50);
}

#ifdef CONFIG_ANALOG_TVENC
#ifndef CONFIG_VPLL_USE_FOR_TVENC
static void s5p_tvenc_src_to_hdmiphy_on(void);
static void s5p_tvenc_src_to_hdmiphy_off(void);
#endif
#endif

void s5p_sdo_ctrl_stop(void)
{
	if (s5p_sdo_ctrl_private.running) {
		s5p_sdo_ctrl_internal_stop();
		s5p_sdo_ctrl_clock(0);

#ifdef CONFIG_ANALOG_TVENC
#ifndef CONFIG_VPLL_USE_FOR_TVENC
		s5p_tvenc_src_to_hdmiphy_off();
#endif
#endif

		s5p_sdo_ctrl_private.running = false;
	}
}

int s5p_sdo_ctrl_start(enum s5p_tvout_disp_mode disp_mode)
{
	struct s5p_sdo_ctrl_private_data *sdo_private = &s5p_sdo_ctrl_private;

	switch (disp_mode) {
	case TVOUT_NTSC_M:
	case TVOUT_NTSC_443:
		sdo_private->video_scale_cfg.composite_level =
			SDO_LEVEL_75IRE;
		sdo_private->video_scale_cfg.composite_ratio =
			SDO_VTOS_RATIO_10_4;
		break;

	case TVOUT_PAL_BDGHI:
	case TVOUT_PAL_M:
	case TVOUT_PAL_N:
	case TVOUT_PAL_NC:
	case TVOUT_PAL_60:
		sdo_private->video_scale_cfg.composite_level =
			SDO_LEVEL_0IRE;
		sdo_private->video_scale_cfg.composite_ratio =
			SDO_VTOS_RATIO_7_3;
		break;

	default:
		tvout_err("invalid disp_mode(%d) for SDO\n",
			disp_mode);
		goto err_on_s5p_sdo_start;
	}

	if (sdo_private->running)
		s5p_sdo_ctrl_internal_stop();
	else {
		s5p_sdo_ctrl_clock(1);

#ifdef CONFIG_ANALOG_TVENC
#ifndef CONFIG_VPLL_USE_FOR_TVENC
		s5p_tvenc_src_to_hdmiphy_on();
#endif
#endif

		sdo_private->running = true;
	}

	if (s5p_sdo_ctrl_set_reg(disp_mode))
		goto err_on_s5p_sdo_start;

	return 0;

err_on_s5p_sdo_start:
	return -1;
}

int s5p_sdo_ctrl_constructor(struct platform_device *pdev)
{
	int ret;
	int i, j;

	ret = s5p_tvout_map_resource_mem(
		pdev,
		s5p_sdo_ctrl_private.reg_mem.name,
		&(s5p_sdo_ctrl_private.reg_mem.base),
		&(s5p_sdo_ctrl_private.reg_mem.res));

	if (ret)
		goto err_on_res;

	for (i = SDO_PCLK; i < SDO_NO_OF_CLK; i++) {
		s5p_sdo_ctrl_private.clk[i].ptr =
			clk_get(&pdev->dev, s5p_sdo_ctrl_private.clk[i].name);

		if (IS_ERR(s5p_sdo_ctrl_private.clk[i].ptr)) {
			tvout_err("Failed to find clock %s\n",
					s5p_sdo_ctrl_private.clk[i].name);
			ret = -ENOENT;
			goto err_on_clk;
		}
	}

	s5p_sdo_ctrl_init_private();
	s5p_sdo_init(s5p_sdo_ctrl_private.reg_mem.base);

	return 0;

err_on_clk:
	for (j = 0; j < i; j++)
		clk_put(s5p_sdo_ctrl_private.clk[j].ptr);

	s5p_tvout_unmap_resource_mem(
		s5p_sdo_ctrl_private.reg_mem.base,
		s5p_sdo_ctrl_private.reg_mem.res);

err_on_res:
	return ret;
}

void s5p_sdo_ctrl_destructor(void)
{
	int i;

	s5p_tvout_unmap_resource_mem(
		s5p_sdo_ctrl_private.reg_mem.base,
		s5p_sdo_ctrl_private.reg_mem.res);

	for (i = SDO_PCLK; i < SDO_NO_OF_CLK; i++)
		if (s5p_sdo_ctrl_private.clk[i].ptr) {
			if (s5p_sdo_ctrl_private.running)
				clk_disable(s5p_sdo_ctrl_private.clk[i].ptr);
			clk_put(s5p_sdo_ctrl_private.clk[i].ptr);
	}
	s5p_sdo_init(NULL);
}
#endif




/****************************************
 * Functions for hdmi ctrl class
 ***************************************/

static enum s5p_hdmi_v_mode s5p_hdmi_check_v_fmt(enum s5p_tvout_disp_mode disp)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	struct s5p_hdmi_video			*video = &ctrl->video;
	enum s5p_hdmi_v_mode			mode;

	video->aspect		= HDMI_PIC_RATIO_16_9;
	video->colorimetry	= HDMI_CLRIMETRY_601;

	switch (disp) {
	case TVOUT_480P_60_16_9:
		mode = v720x480p_60Hz;
		break;

	case TVOUT_480P_60_4_3:
		mode = v720x480p_60Hz;
		video->aspect = HDMI_PIC_RATIO_4_3;
		break;

	case TVOUT_480P_59:
		mode = v720x480p_59Hz;
		break;

	case TVOUT_576P_50_16_9:
		mode = v720x576p_50Hz;
		break;

	case TVOUT_576P_50_4_3:
		mode = v720x576p_50Hz;
		video->aspect = HDMI_PIC_RATIO_4_3;
		break;

	case TVOUT_720P_60:
		mode = v1280x720p_60Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_720P_59:
		mode = v1280x720p_59Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_720P_50:
		mode = v1280x720p_50Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_1080P_30:
		mode = v1920x1080p_30Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_1080P_60:
		mode = v1920x1080p_60Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_1080P_59:
		mode = v1920x1080p_59Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_1080P_50:
		mode = v1920x1080p_50Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_1080I_60:
		mode = v1920x1080i_60Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_1080I_59:
		mode = v1920x1080i_59Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;

	case TVOUT_1080I_50:
		mode = v1920x1080i_50Hz;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;
#ifdef CONFIG_HDMI_14A_3D
	case TVOUT_720P_60_SBS_HALF:
		mode = v1280x720p_60Hz_SBS_HALF;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;
	case TVOUT_720P_59_SBS_HALF:
		mode = v1280x720p_59Hz_SBS_HALF;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;
	case TVOUT_720P_50_TB:
		mode = v1280x720p_50Hz_TB;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;
	case TVOUT_1080P_24_TB:
		mode = v1920x1080p_24Hz_TB;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;
	case TVOUT_1080P_23_TB:
		mode = v1920x1080p_23Hz_TB;
		video->colorimetry = HDMI_CLRIMETRY_709;
		break;
#endif

	default:
		mode = v720x480p_60Hz;
		tvout_err("Not supported mode : %d\n", mode);
	}

	return mode;
}

static void s5p_hdmi_set_acr(struct s5p_hdmi_audio *audio, u8 *acr)
{
	u32 n = (audio->freq == 32000) ? 4096 :
		(audio->freq == 44100) ? 6272 :
		(audio->freq == 88200) ? 12544 :
		(audio->freq == 176400) ? 25088 :
		(audio->freq == 48000) ? 6144 :
		(audio->freq == 96000) ? 12288 :
		(audio->freq == 192000) ? 24576 : 0;

	u32 cts = (audio->freq == 32000) ? 27000 :
		(audio->freq == 44100) ? 30000 :
		(audio->freq == 88200) ? 30000 :
		(audio->freq == 176400) ? 30000 :
		(audio->freq == 48000) ? 27000 :
		(audio->freq == 96000) ? 27000 :
		(audio->freq == 192000) ? 27000 : 0;

	acr[1] = cts >> 16;
	acr[2] = cts >> 8 & 0xff;
	acr[3] = cts & 0xff;

	acr[4] = n >> 16;
	acr[5] = n >> 8 & 0xff;
	acr[6] = n & 0xff;

	tvout_dbg("n value = %d\n", n);
	tvout_dbg("cts   = %d\n", cts);
}

static void s5p_hdmi_set_asp(u8 *header)
{
	header[1] = 0;
	header[2] = 0;
}

static void s5p_hdmi_set_acp(struct s5p_hdmi_audio *audio, u8 *header)
{
	header[1] = audio->type;
}

static void s5p_hdmi_set_isrc(u8 *header)
{
}

static void s5p_hdmi_set_gmp(u8 *gmp)
{
}

static void s5p_hdmi_set_avi(
	enum s5p_hdmi_v_mode mode, enum s5p_tvout_o_mode out,
	struct s5p_hdmi_video *video, u8 *avi)
{
	struct s5p_hdmi_o_params		param = s5p_hdmi_output[out];
	struct s5p_hdmi_v_frame			frame;

	frame = s5p_hdmi_v_fmt[mode].frame;
	avi[0] = param.reg.pxl_fmt;
	avi[2] &= (u8)((~0x3) << 2);
	avi[4] &= (u8)((~0x3) << 6);

	/* RGB or YCbCr */
	if (s5p_tvif_ctrl_private.curr_if == TVOUT_HDMI_RGB) {
		avi[0] |= (0x1 << 4);
		avi[4] |= frame.repetition;
		if (s5p_tvif_ctrl_private.curr_std == TVOUT_480P_60_4_3) {
			avi[2] |= HDMI_Q_DEFAULT << 2;
			avi[4] |= HDMI_AVI_YQ_FULL_RANGE << 6;
		} else {
			avi[2] |= HDMI_Q_DEFAULT << 2;
			avi[4] |= HDMI_AVI_YQ_LIMITED_RANGE << 6;
		}
	} else {
		avi[0] |= (0x5 << 4);
		avi[4] |= frame.repetition;
		if (video->q_range == HDMI_Q_FULL_RANGE) {
			tvout_dbg("Q_Range : %d\n", video->q_range);
			avi[2] |= HDMI_Q_DEFAULT << 2;
			avi[4] |= HDMI_AVI_YQ_FULL_RANGE << 6;
		} else {
			tvout_dbg("Q_Range : %d\n", video->q_range);
			avi[2] |= HDMI_Q_DEFAULT << 2;
			avi[4] |= HDMI_AVI_YQ_LIMITED_RANGE << 6;
		}
	}

	avi[1] = video->colorimetry;
	avi[1] |= video->aspect << 4;
	avi[1] |= AVI_SAME_WITH_PICTURE_AR;
#ifdef CONFIG_HDMI_14A_3D
	avi[3] = (video->aspect == HDMI_PIC_RATIO_16_9) ?
				frame.vAVI_VIC_16_9 : frame.vAVI_VIC;
#else
	avi[3] = (video->aspect == HDMI_PIC_RATIO_16_9) ?
				frame.vic_16_9 : frame.vic;
#endif
	if (s5p_tvif_ctrl_private.curr_std == TVOUT_480P_60_4_3)
		avi[3] = 0x1;

	tvout_dbg(KERN_INFO "AVI BYTE 1 : 0x%x\n", avi[0]);
	tvout_dbg(KERN_INFO "AVI BYTE 2 : 0x%x\n", avi[1]);
	tvout_dbg(KERN_INFO "AVI BYTE 3 : 0x%x\n", avi[2]);
	tvout_dbg(KERN_INFO "AVI BYTE 4 : %d\n", avi[3]);
	tvout_dbg(KERN_INFO "AVI BYTE 5 : 0x%x\n", avi[4]);
}

static void s5p_hdmi_set_aui(struct s5p_hdmi_audio *audio, u8 *aui)
{
	aui[0] = audio->channel - 1;
	if (audio->channel == 2) {
		aui[1] = 0x0;
		aui[2] = 0;
		aui[3] = 0x0;
	} else {
		aui[1] = 0x0;
		aui[2] = 0;
		aui[3] = 0x0b;
	}
}

static void s5p_hdmi_set_spd(u8 *spd)
{
	struct s5p_hdmi_ctrl_private_data *ctrl = &s5p_hdmi_ctrl_private;

	memcpy(spd, ctrl->vendor, 8);
	memcpy(&spd[8], ctrl->product, 16);

	spd[24] = 0x1; /* Digital STB */
}

static void s5p_hdmi_set_mpg(u8 *mpg)
{
}

static int s5p_hdmi_ctrl_audio_enable(bool en)
{
	if (!s5p_hdmi_output[s5p_hdmi_ctrl_private.out].reg.dvi)
		s5p_hdmi_reg_audio_enable(en);

	return 0;
}

#if 0 /* This function will be used in the future */
static void s5p_hdmi_ctrl_bluescreen_clr(u8 cb_b, u8 y_g, u8 cr_r)
{
	struct s5p_hdmi_ctrl_private_data *ctrl = &s5p_hdmi_ctrl_private;

	ctrl->blue_screen.cb_b	= cb_b;
	ctrl->blue_screen.y_g	= y_g;
	ctrl->blue_screen.cr_r	= cr_r;

	s5p_hdmi_reg_bluescreen_clr(cb_b, y_g, cr_r);
}
#endif

static void s5p_hdmi_ctrl_set_bluescreen(bool en)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;

	ctrl->blue_screen.enable = en ? true : false;

	s5p_hdmi_reg_bluescreen(en);
}

#ifndef CONFIG_HDMI_EARJACK_MUTE
static void s5p_hdmi_ctrl_set_audio(bool en)
#else
void s5p_hdmi_ctrl_set_audio(bool en)
#endif
{
	struct s5p_hdmi_ctrl_private_data       *ctrl = &s5p_hdmi_ctrl_private;

	s5p_hdmi_ctrl_private.audio.on = en ? 1 : 0;

	if (ctrl->running)
		s5p_hdmi_ctrl_audio_enable(en);
}

static void s5p_hdmi_ctrl_set_av_mute(bool en)
{
	struct s5p_hdmi_ctrl_private_data       *ctrl = &s5p_hdmi_ctrl_private;

	ctrl->av_mute = en ? 1 : 0;

	if (ctrl->running) {
		if (en) {
			s5p_hdmi_ctrl_audio_enable(false);
			s5p_hdmi_ctrl_set_bluescreen(true);
		} else {
			s5p_hdmi_ctrl_audio_enable(true);
			s5p_hdmi_ctrl_set_bluescreen(false);
		}
	}

}

u8 s5p_hdmi_ctrl_get_mute(void)
{
	return s5p_hdmi_ctrl_private.av_mute ? 1 : 0;
}

#if 0 /* This function will be used in the future */
static void s5p_hdmi_ctrl_mute(bool en)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;

	if (en) {
		s5p_hdmi_reg_bluescreen(true);
		s5p_hdmi_ctrl_audio_enable(false);
	} else {
		s5p_hdmi_reg_bluescreen(false);
		if (ctrl->audio.on)
			s5p_hdmi_ctrl_audio_enable(true);
	}
}
#endif

void s5p_hdmi_ctrl_set_hdcp(bool en)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;

	ctrl->hdcp_en = en ? 1 : 0;
}

static void s5p_hdmi_ctrl_init_private(void)
{
}

static bool s5p_hdmi_ctrl_set_reg(
		enum s5p_hdmi_v_mode mode, enum s5p_tvout_o_mode out)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	struct s5p_hdmi_packet			*packet = &ctrl->packet;

	struct s5p_hdmi_bluescreen		*bl = &ctrl->blue_screen;
	struct s5p_hdmi_color_range		*cr = &ctrl->video.color_r;
	struct s5p_hdmi_tg			*tg = &ctrl->tg;
#ifdef CONFIG_HDMI_14A_3D
	u8 type3D;
#endif

#ifdef CONFIG_HDMI_14A_3D
	s5p_hdmi_reg_bluescreen_clr(bl->b, bl->g, bl->r);
#else
	s5p_hdmi_reg_bluescreen_clr(bl->cb_b, bl->y_g, bl->cr_r);
#endif
	s5p_hdmi_reg_bluescreen(bl->enable);

	s5p_hdmi_reg_clr_range(cr->y_min, cr->y_max, cr->c_min, cr->c_max);

	s5p_hdmi_reg_acr(packet->acr);
	s5p_hdmi_reg_asp(packet->h_asp, &ctrl->audio);
#ifdef CONFIG_HDMI_14A_3D
	s5p_hdmi_reg_gcp(s5p_hdmi_v_fmt[mode].frame.interlaced, packet->gcp);
#else
	s5p_hdmi_reg_gcp(s5p_hdmi_v_fmt[mode].frame.i_p, packet->gcp);
#endif

	s5p_hdmi_reg_acp(packet->h_acp, packet->acp);
	s5p_hdmi_reg_isrc(packet->isrc1, packet->isrc2);
	s5p_hdmi_reg_gmp(packet->gmp);


#ifdef CONFIG_HDMI_14A_3D
	if ((mode == v1280x720p_60Hz_SBS_HALF) ||
		(mode == v1280x720p_59Hz_SBS_HALF))
		type3D = HDMI_3D_SSH_FORMAT;
	else if ((mode == v1280x720p_50Hz_TB) ||
		(mode == v1920x1080p_24Hz_TB) || (mode == v1920x1080p_23Hz_TB))
		type3D = HDMI_3D_TB_FORMAT;
	else
		type3D = HDMI_2D_FORMAT;

	s5p_hdmi_reg_infoframe(&packet->vsi_info, packet->vsi, type3D);
	s5p_hdmi_reg_infoframe(&packet->vsi_info, packet->vsi, type3D);
	s5p_hdmi_reg_infoframe(&packet->avi_info, packet->avi, type3D);
	s5p_hdmi_reg_infoframe(&packet->aui_info, packet->aui, type3D);
	s5p_hdmi_reg_infoframe(&packet->spd_info, packet->spd, type3D);
	s5p_hdmi_reg_infoframe(&packet->mpg_info, packet->mpg, type3D);
#else
	s5p_hdmi_reg_infoframe(&packet->avi_info, packet->avi);
	s5p_hdmi_reg_infoframe(&packet->aui_info, packet->aui);
	s5p_hdmi_reg_infoframe(&packet->spd_info, packet->spd);
	s5p_hdmi_reg_infoframe(&packet->mpg_info, packet->mpg);
#endif

	s5p_hdmi_reg_packet_trans(&s5p_hdmi_output[out].trans);
	s5p_hdmi_reg_output(&s5p_hdmi_output[out].reg);

#ifdef CONFIG_HDMI_14A_3D
	s5p_hdmi_reg_tg(&s5p_hdmi_v_fmt[mode]);
#else
	s5p_hdmi_reg_tg(&s5p_hdmi_v_fmt[mode].frame);
#endif
	s5p_hdmi_reg_v_timing(&s5p_hdmi_v_fmt[mode]);
	s5p_hdmi_reg_tg_cmd(tg->correction_en, tg->bt656_en, true);

	switch (ctrl->audio.type) {
	case HDMI_GENERIC_AUDIO:
		break;

	case HDMI_60958_AUDIO:
		s5p_hdmi_audio_init(PCM, 44100, 16, 0, &ctrl->audio);
		break;

	case HDMI_DVD_AUDIO:
	case HDMI_SUPER_AUDIO:
		break;

	default:
		tvout_err("Invalid audio type %d\n", ctrl->audio.type);
		return -1;
	}

	s5p_hdmi_reg_audio_enable(true);

	return 0;
}

static void s5p_hdmi_ctrl_internal_stop(void)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	struct s5p_hdmi_tg			*tg = &ctrl->tg;

	tvout_dbg("\n");
#ifdef CONFIG_HDMI_HPD
	s5p_hpd_set_eint();
#endif
	if (ctrl->hdcp_en) {
		s5p_hdcp_stop();
		s5p_hdcp_flush_work();
	}

	s5p_hdmi_reg_enable(false);

	s5p_hdmi_reg_tg_cmd(tg->correction_en, tg->bt656_en, false);
}

int s5p_hdmi_ctrl_phy_power(bool on)
{
	tvout_dbg("on(%d)\n", on);
	if (on) {
		/* on */
		clk_enable(s5ptv_status.i2c_phy_clk);
		/* Restore i2c_hdmi_phy_base address */
		s5p_hdmi_phy_init(s5p_hdmi_ctrl_private.reg_mem[HDMI_PHY].base);

		s5p_hdmi_phy_power(true);
#ifdef CONFIG_HDMI_TX_STRENGTH
		if (s5p_tvif_ctrl_private.tx_val)
			s5p_hdmi_phy_set_tx_strength(
			s5p_tvif_ctrl_private.tx_ch,
			s5p_tvif_ctrl_private.tx_val);
#endif

	} else {
		/*
		 * for preventing hdmi hang up when restart
		 * switch to internal clk - SCLK_DAC, SCLK_PIXEL
		 */
		s5p_mixer_ctrl_mux_clk(s5ptv_status.sclk_dac);
		if (clk_set_parent(s5ptv_status.sclk_hdmi,
				s5ptv_status.sclk_pixel)) {
			tvout_err("unable to set parent %s of clock %s.\n",
				   s5ptv_status.sclk_pixel->name,
				   s5ptv_status.sclk_hdmi->name);
			return -1;
		}

		s5p_hdmi_phy_power(false);

		clk_disable(s5ptv_status.i2c_phy_clk);
		/* Set i2c_hdmi_phy_base to NULL */
		s5p_hdmi_phy_init(NULL);
	}

	return 0;
}

void s5p_hdmi_ctrl_clock(bool on)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	struct s5p_tvout_clk_info		*clk = ctrl->clk;

	tvout_dbg("on(%d)\n", on);
	if (on) {
		clk_enable(clk[HDMI_MUX].ptr);

#ifdef CONFIG_ARCH_EXYNOS4
		s5p_tvout_pm_runtime_get();
#endif
		clk_enable(clk[HDMI_PCLK].ptr);

		/* Restore hdmi_base address */
		s5p_hdmi_init(s5p_hdmi_ctrl_private.reg_mem[HDMI].base);
	} else {
		clk_disable(clk[HDMI_PCLK].ptr);

#ifdef CONFIG_ARCH_EXYNOS4
		s5p_tvout_pm_runtime_put();
#endif

		clk_disable(clk[HDMI_MUX].ptr);

		/* Set hdmi_base to NULL */
		s5p_hdmi_init(NULL);
	}
}

bool s5p_hdmi_ctrl_status(void)
{
	return s5p_hdmi_ctrl_private.running;
}

void s5p_hdmi_ctrl_stop(void)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;

	tvout_dbg("running(%d)\n", ctrl->running);
	if (ctrl->running) {
		ctrl->running = false;
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
		if (suspend_status) {
			tvout_dbg("driver is suspend_status\n");
		} else
#endif
		{
			s5p_hdmi_ctrl_internal_stop();
			s5p_hdmi_ctrl_clock(0);
		}
	}
}

int s5p_hdmi_ctrl_start(
	enum s5p_tvout_disp_mode disp, enum s5p_tvout_o_mode out)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	struct s5p_hdmi_packet			*packet = &ctrl->packet;
	struct s5p_hdmi_v_frame			frame;

	enum s5p_hdmi_v_mode			mode;

	ctrl->out = out;
	mode = s5p_hdmi_check_v_fmt(disp);
	ctrl->mode = mode;

	tvout_dbg("\n");
	if (ctrl->running)
		s5p_hdmi_ctrl_internal_stop();
	else {
		s5p_hdmi_ctrl_clock(1);
		ctrl->running = true;
	}
	on_start_process = false;
	tvout_dbg("on_start_process(%d)\n", on_start_process);

	frame = s5p_hdmi_v_fmt[mode].frame;

	if (s5p_hdmi_phy_config(frame.pixel_clock, ctrl->video.depth) < 0) {
		tvout_err("hdmi phy configuration failed.\n");
		goto err_on_s5p_hdmi_start;
	}


	s5p_hdmi_set_acr(&ctrl->audio, packet->acr);
	s5p_hdmi_set_asp(packet->h_asp);
	s5p_hdmi_set_gcp(ctrl->video.depth, packet->gcp);

	s5p_hdmi_set_acp(&ctrl->audio, packet->h_acp);
	s5p_hdmi_set_isrc(packet->h_isrc);
	s5p_hdmi_set_gmp(packet->gmp);

	s5p_hdmi_set_avi(mode, out, &ctrl->video, packet->avi);
	s5p_hdmi_set_spd(packet->spd);
	s5p_hdmi_set_aui(&ctrl->audio, packet->aui);
	s5p_hdmi_set_mpg(packet->mpg);

	s5p_hdmi_ctrl_set_reg(mode, out);

	if (ctrl->hdcp_en)
		s5p_hdcp_start();

	s5p_hdmi_reg_enable(true);

#ifdef CONFIG_HDMI_HPD
	s5p_hpd_set_hdmiint();
#endif

	return 0;

err_on_s5p_hdmi_start:
	return -1;
}

int s5p_hdmi_ctrl_constructor(struct platform_device *pdev)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	struct reg_mem_info		*reg_mem = ctrl->reg_mem;
	struct s5p_tvout_clk_info		*clk = ctrl->clk;
	struct irq_info			*irq = &ctrl->irq;
	int ret, i, k, j;

	for (i = 0; i < HDMI_NO_OF_MEM_RES; i++) {
		ret = s5p_tvout_map_resource_mem(pdev, reg_mem[i].name,
			&(reg_mem[i].base), &(reg_mem[i].res));

		if (ret)
			goto err_on_res;
	}

	for (k = HDMI_PCLK; k < HDMI_NO_OF_CLK; k++) {
		clk[k].ptr = clk_get(&pdev->dev, clk[k].name);

		if (IS_ERR(clk[k].ptr)) {
			printk(KERN_ERR	"%s clk is not found\n", clk[k].name);
			ret = -ENOENT;
			goto err_on_clk;
		}
	}

	irq->no = platform_get_irq_byname(pdev, irq->name);

	if (irq->no < 0) {
		printk(KERN_ERR "can not get platform irq by name : %s\n",
					irq->name);
		ret = irq->no;
		goto err_on_irq;
	}

	s5p_hdmi_init(reg_mem[HDMI].base);
	s5p_hdmi_phy_init(reg_mem[HDMI_PHY].base);

	ret = request_irq(irq->no, irq->handler, IRQF_DISABLED,
			irq->name, NULL);
	if (ret) {
		printk(KERN_ERR "can not request irq : %s\n", irq->name);
		goto err_on_irq;
	}

	s5p_hdmi_ctrl_init_private();

	/* set initial state of HDMI PHY power to off */
	s5p_hdmi_ctrl_phy_power(1);
	s5p_hdmi_ctrl_phy_power(0);

	ret = s5p_hdcp_init();

	if (ret) {
		printk(KERN_ERR "HDCP init failed..\n");
		goto err_hdcp_init;
	}

	return 0;

err_hdcp_init:
err_on_irq:
err_on_clk:
	for (j = 0; j < k; j++)
		clk_put(clk[j].ptr);

err_on_res:
	for (j = 0; j < i; j++)
		s5p_tvout_unmap_resource_mem(reg_mem[j].base, reg_mem[j].res);

	return ret;
}

void s5p_hdmi_ctrl_destructor(void)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	struct reg_mem_info		*reg_mem = ctrl->reg_mem;
	struct s5p_tvout_clk_info		*clk = ctrl->clk;
	struct irq_info			*irq = &ctrl->irq;

	int i;

	if (irq->no >= 0)
		free_irq(irq->no, NULL);

	for (i = 0; i < HDMI_NO_OF_MEM_RES; i++)
		s5p_tvout_unmap_resource_mem(reg_mem[i].base, reg_mem[i].res);

	for (i = HDMI_PCLK; i < HDMI_NO_OF_CLK; i++)
		if (clk[i].ptr) {
			if (ctrl->running)
				clk_disable(clk[i].ptr);
			clk_put(clk[i].ptr);
		}

	s5p_hdmi_phy_init(NULL);
	s5p_hdmi_init(NULL);
}

void s5p_hdmi_ctrl_suspend(void)
{
}

void s5p_hdmi_ctrl_resume(void)
{
}

#ifdef CONFIG_ANALOG_TVENC
#ifndef CONFIG_VPLL_USE_FOR_TVENC
static void s5p_tvenc_src_to_hdmiphy_on(void)
{
	s5p_hdmi_ctrl_clock(1);
	s5p_hdmi_ctrl_phy_power(1);
	if (s5p_hdmi_phy_config(ePHY_FREQ_54, HDMI_CD_24) < 0)
		tvout_err("hdmi phy configuration failed.\n");
	if (clk_set_parent(s5ptv_status.sclk_dac, s5ptv_status.sclk_hdmiphy))
		tvout_err("unable to set parent %s of clock %s.\n",
			   s5ptv_status.sclk_hdmiphy->name,
			   s5ptv_status.sclk_dac->name);
}

static void s5p_tvenc_src_to_hdmiphy_off(void)
{
	s5p_hdmi_ctrl_phy_power(0);
	s5p_hdmi_ctrl_clock(0);
}
#endif
#endif

/****************************************
 * Functions for tvif ctrl class
 ***************************************/
static void s5p_tvif_ctrl_init_private(struct platform_device *pdev)
{
#if defined(CONFIG_BUSFREQ_OPP) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
	/* add bus device ptr for using bus frequency with opp */
	s5p_tvif_ctrl_private.bus_dev = dev_get("exynos-busfreq");
#endif
	s5p_tvif_ctrl_private.dev = &pdev->dev;
}

/*
 * TV cut off sequence
 * VP stop -> Mixer stop -> HDMI stop -> HDMI TG stop
 * Above sequence should be satisfied.
 */
static int s5p_tvif_ctrl_internal_stop(void)
{
	tvout_dbg("status(%d)\n", s5p_tvif_ctrl_private.curr_if);
	s5p_mixer_ctrl_stop();

	switch (s5p_tvif_ctrl_private.curr_if) {
#ifdef CONFIG_ANALOG_TVENC
	case TVOUT_COMPOSITE:
		s5p_sdo_ctrl_stop();
		break;
#endif
	case TVOUT_DVI:
	case TVOUT_HDMI:
	case TVOUT_HDMI_RGB:
		s5p_hdmi_ctrl_stop();
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
		if (suspend_status) {
			tvout_dbg("driver is suspend_status\n");
		} else
#endif
		{
			s5p_hdmi_ctrl_phy_power(0);
		}
		break;

	default:
		tvout_err("invalid out parameter(%d)\n",
			s5p_tvif_ctrl_private.curr_if);
		return -1;
	}

	return 0;
}

static void s5p_tvif_ctrl_internal_start(
		enum s5p_tvout_disp_mode std,
		enum s5p_tvout_o_mode inf)
{
	tvout_dbg("\n");
#ifdef	__CONFIG_HDMI_SUPPORT_FULL_RANGE__
	s5p_hdmi_output[inf].reg.pxl_limit =
		S5P_HDMI_PX_LMT_CTRL_BYPASS;
#endif
	s5p_mixer_ctrl_set_int_enable(false);

	/* Clear All Interrupt Pending */
	s5p_mixer_ctrl_clear_pend_all();

	switch (inf) {
#ifdef CONFIG_ANALOG_TVENC
	case TVOUT_COMPOSITE:
		if (s5p_mixer_ctrl_start(std, inf) < 0)
			goto ret_on_err;

		if (0 != s5p_sdo_ctrl_start(std))
			goto ret_on_err;

		break;
#endif
	case TVOUT_HDMI:
	case TVOUT_HDMI_RGB:
	case TVOUT_DVI:
#ifdef	__CONFIG_HDMI_SUPPORT_FULL_RANGE__
	switch (std) {
	case TVOUT_480P_60_4_3:
		if (inf == TVOUT_HDMI_RGB) /* full range */
			s5p_hdmi_output[inf].reg.pxl_limit =
				S5P_HDMI_PX_LMT_CTRL_BYPASS;
		else if (s5p_tvif_get_q_range()) /* full range */
			s5p_hdmi_output[inf].reg.pxl_limit =
				S5P_HDMI_PX_LMT_CTRL_BYPASS;
		else /* limited range */
			s5p_hdmi_output[inf].reg.pxl_limit =
				S5P_HDMI_PX_LMT_CTRL_YPBPR;
		break;
	case TVOUT_480P_60_16_9:
	case TVOUT_480P_59:
	case TVOUT_576P_50_16_9:
	case TVOUT_576P_50_4_3:
	case TVOUT_720P_60:
	case TVOUT_720P_50:
	case TVOUT_720P_59:
	case TVOUT_1080I_60:
	case TVOUT_1080I_59:
	case TVOUT_1080I_50:
	case TVOUT_1080P_60:
	case TVOUT_1080P_30:
	case TVOUT_1080P_59:
	case TVOUT_1080P_50:
		if (inf == TVOUT_HDMI_RGB) /* limited range */
			s5p_hdmi_output[inf].reg.pxl_limit =
				S5P_HDMI_PX_LMT_CTRL_RGB;
		else if (s5p_tvif_get_q_range()) /* full range */
			s5p_hdmi_output[inf].reg.pxl_limit =
				S5P_HDMI_PX_LMT_CTRL_BYPASS;
		else /* limited range */
			s5p_hdmi_output[inf].reg.pxl_limit =
				S5P_HDMI_PX_LMT_CTRL_YPBPR;
		break;
	default:
		break;
	}
#endif
		s5p_hdmi_ctrl_phy_power(1);

		if (s5p_mixer_ctrl_start(std, inf) < 0)
			goto ret_on_err;

		if (0 != s5p_hdmi_ctrl_start(std, inf))
			goto ret_on_err;
		break;
	default:
		break;
	}

ret_on_err:
	s5p_mixer_ctrl_set_int_enable(true);

	/* Clear All Interrupt Pending */
	s5p_mixer_ctrl_clear_pend_all();
}

int s5p_tvif_ctrl_set_audio(bool en)
{
	switch (s5p_tvif_ctrl_private.curr_if) {
	case TVOUT_HDMI:
	case TVOUT_HDMI_RGB:
	case TVOUT_DVI:
		s5p_hdmi_ctrl_set_audio(en);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void s5p_tvif_audio_channel(int channel)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	ctrl->audio.channel = channel;
}

void s5p_tvif_q_color_range(int range)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	if (range)
		ctrl->video.q_range = HDMI_Q_FULL_RANGE;
	else
		ctrl->video.q_range = HDMI_Q_LIMITED_RANGE;
	tvout_dbg("%s: Set Q range : %d\n", __func__, ctrl->video.q_range);
}

int s5p_tvif_get_q_range(void)
{
	struct s5p_hdmi_ctrl_private_data	*ctrl = &s5p_hdmi_ctrl_private;
	tvout_dbg("%s: Get Q range : %d\n", __func__, ctrl->video.q_range);
	if (ctrl->video.q_range == HDMI_Q_FULL_RANGE)
		return 1;
	else
		return 0;
}

int s5p_tvif_ctrl_set_av_mute(bool en)
{
	switch (s5p_tvif_ctrl_private.curr_if) {
	case TVOUT_HDMI:
	case TVOUT_HDMI_RGB:
	case TVOUT_DVI:
		s5p_hdmi_ctrl_set_av_mute(en);

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int s5p_tvif_ctrl_get_std_if(
		enum s5p_tvout_disp_mode *std, enum s5p_tvout_o_mode *inf)
{
	*std = s5p_tvif_ctrl_private.curr_std;
	*inf = s5p_tvif_ctrl_private.curr_if;

	return 0;
}

bool s5p_tvif_ctrl_get_run_state()
{
	return s5p_tvif_ctrl_private.running;
}

int s5p_tvif_ctrl_start(
		enum s5p_tvout_disp_mode std, enum s5p_tvout_o_mode inf)
{
	tvout_dbg("\n");
#if defined(CONFIG_BUSFREQ_OPP) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
	if ((std == TVOUT_1080P_60) || (std == TVOUT_1080P_59)
			|| (std == TVOUT_1080P_50)) {
		dev_lock(s5p_tvif_ctrl_private.bus_dev,
				s5p_tvif_ctrl_private.dev, BUSFREQ_400MHZ);
	}
#if defined(CONFIG_MACH_MIDAS)
	else {
		dev_lock(s5p_tvif_ctrl_private.bus_dev,
			 s5p_tvif_ctrl_private.dev, BUSFREQ_133MHZ);
	}
#endif
#endif
	if (s5p_tvif_ctrl_private.running &&
			(std == s5p_tvif_ctrl_private.curr_std) &&
			(inf == s5p_tvif_ctrl_private.curr_if))	{
		on_start_process = false;
		tvout_dbg("%s() on_start_process(%d)\n",
			__func__, on_start_process);
		goto cannot_change;
	}

	s5p_tvif_ctrl_private.curr_std = std;
	s5p_tvif_ctrl_private.curr_if = inf;

	switch (inf) {
	case TVOUT_COMPOSITE:
	case TVOUT_HDMI:
	case TVOUT_HDMI_RGB:
	case TVOUT_DVI:
		break;
	default:
		tvout_err("invalid out parameter(%d)\n", inf);
		goto cannot_change;
	}

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
	} else
#endif
	{
		/* how to control the clock path on stop time ??? */
		if (s5p_tvif_ctrl_private.running)
			s5p_tvif_ctrl_internal_stop();

		s5p_tvif_ctrl_internal_start(std, inf);
	}

	s5p_tvif_ctrl_private.running = true;

	return 0;

cannot_change:
	return -1;
}

void s5p_tvif_ctrl_stop(void)
{
	if (s5p_tvif_ctrl_private.running) {
		s5p_tvif_ctrl_internal_stop();

		s5p_tvif_ctrl_private.running = false;
	}
#if defined(CONFIG_BUSFREQ_OPP) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
	dev_unlock(s5p_tvif_ctrl_private.bus_dev, s5p_tvif_ctrl_private.dev);
#endif
}

int s5p_tvif_ctrl_constructor(struct platform_device *pdev)
{
#ifdef CONFIG_HDMI_TX_STRENGTH
	struct s5p_platform_tvout *pdata = to_tvout_plat(&pdev->dev);
	s5p_tvif_ctrl_private.tx_ch = 0x00;
	s5p_tvif_ctrl_private.tx_val = NULL;
	if ((pdata) && (pdata->tx_tune)) {
		s5p_tvif_ctrl_private.tx_ch = pdata->tx_tune->tx_ch;
		s5p_tvif_ctrl_private.tx_val = pdata->tx_tune->tx_val;
	}
#endif

#ifdef CONFIG_ANALOG_TVENC
	if (s5p_sdo_ctrl_constructor(pdev))
		goto err;
#endif

	if (s5p_hdmi_ctrl_constructor(pdev))
		goto err;

	s5p_tvif_ctrl_init_private(pdev);

	return 0;

err:
	return -1;
}

void s5p_tvif_ctrl_destructor(void)
{
#ifdef CONFIG_ANALOG_TVENC
	s5p_sdo_ctrl_destructor();
#endif
	s5p_hdmi_ctrl_destructor();
}

void s5p_tvif_ctrl_suspend(void)
{
	tvout_dbg("\n");
	if (s5p_tvif_ctrl_private.running) {
		s5p_tvif_ctrl_internal_stop();
#ifdef CONFIG_VCM
		s5p_tvout_vcm_deactivate();
#endif
	}

}

void s5p_tvif_ctrl_resume(void)
{
	if (s5p_tvif_ctrl_private.running) {
#ifdef CONFIG_VCM
		s5p_tvout_vcm_activate();
#endif
		s5p_tvif_ctrl_internal_start(
			s5p_tvif_ctrl_private.curr_std,
			s5p_tvif_ctrl_private.curr_if);
	}
}

#ifdef CONFIG_PM
void s5p_hdmi_ctrl_phy_power_resume(void)
{
	tvout_dbg("running(%d)\n", s5p_tvif_ctrl_private.running);
	if (s5p_tvif_ctrl_private.running)
		return;

	s5p_hdmi_ctrl_phy_power(1);
	s5p_hdmi_ctrl_phy_power(0);

	return;
}
#endif
