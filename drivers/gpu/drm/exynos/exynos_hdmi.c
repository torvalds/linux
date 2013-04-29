/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors:
 * Seung-Woo Kim <sw0312.kim@samsung.com>
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * Based on drivers/media/video/s5p-tv/hdmi_drv.c
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>

#include "regs-hdmi.h"

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/of_gpio.h>

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_hdmi.h"

#include "exynos_hdmi.h"

#include <linux/gpio.h>
#include <media/s5p_hdmi.h>

#define MAX_WIDTH		1920
#define MAX_HEIGHT		1080
#define get_hdmi_context(dev)	platform_get_drvdata(to_platform_device(dev))

/* AVI header and aspect ratio */
#define HDMI_AVI_VERSION		0x02
#define HDMI_AVI_LENGTH		0x0D
#define AVI_PIC_ASPECT_RATIO_16_9	(2 << 4)
#define AVI_SAME_AS_PIC_ASPECT_RATIO	8

/* AUI header info */
#define HDMI_AUI_VERSION	0x01
#define HDMI_AUI_LENGTH	0x0A

/* HDMI infoframe to configure HDMI out packet header, AUI and AVI */
enum HDMI_PACKET_TYPE {
	/* refer to Table 5-8 Packet Type in HDMI specification v1.4a */
	/* InfoFrame packet type */
	HDMI_PACKET_TYPE_INFOFRAME = 0x80,
	/* Vendor-Specific InfoFrame */
	HDMI_PACKET_TYPE_VSI = HDMI_PACKET_TYPE_INFOFRAME + 1,
	/* Auxiliary Video information InfoFrame */
	HDMI_PACKET_TYPE_AVI = HDMI_PACKET_TYPE_INFOFRAME + 2,
	/* Audio information InfoFrame */
	HDMI_PACKET_TYPE_AUI = HDMI_PACKET_TYPE_INFOFRAME + 4
};

enum hdmi_type {
	HDMI_TYPE13,
	HDMI_TYPE14,
};

struct hdmi_resources {
	struct clk			*hdmi;
	struct clk			*sclk_hdmi;
	struct clk			*sclk_pixel;
	struct clk			*sclk_hdmiphy;
	struct clk			*hdmiphy;
	struct regulator_bulk_data	*regul_bulk;
	int				regul_count;
};

struct hdmi_tg_regs {
	u8 cmd[1];
	u8 h_fsz[2];
	u8 hact_st[2];
	u8 hact_sz[2];
	u8 v_fsz[2];
	u8 vsync[2];
	u8 vsync2[2];
	u8 vact_st[2];
	u8 vact_sz[2];
	u8 field_chg[2];
	u8 vact_st2[2];
	u8 vact_st3[2];
	u8 vact_st4[2];
	u8 vsync_top_hdmi[2];
	u8 vsync_bot_hdmi[2];
	u8 field_top_hdmi[2];
	u8 field_bot_hdmi[2];
	u8 tg_3d[1];
};

struct hdmi_v13_core_regs {
	u8 h_blank[2];
	u8 v_blank[3];
	u8 h_v_line[3];
	u8 vsync_pol[1];
	u8 int_pro_mode[1];
	u8 v_blank_f[3];
	u8 h_sync_gen[3];
	u8 v_sync_gen1[3];
	u8 v_sync_gen2[3];
	u8 v_sync_gen3[3];
};

struct hdmi_v14_core_regs {
	u8 h_blank[2];
	u8 v2_blank[2];
	u8 v1_blank[2];
	u8 v_line[2];
	u8 h_line[2];
	u8 hsync_pol[1];
	u8 vsync_pol[1];
	u8 int_pro_mode[1];
	u8 v_blank_f0[2];
	u8 v_blank_f1[2];
	u8 h_sync_start[2];
	u8 h_sync_end[2];
	u8 v_sync_line_bef_2[2];
	u8 v_sync_line_bef_1[2];
	u8 v_sync_line_aft_2[2];
	u8 v_sync_line_aft_1[2];
	u8 v_sync_line_aft_pxl_2[2];
	u8 v_sync_line_aft_pxl_1[2];
	u8 v_blank_f2[2]; /* for 3D mode */
	u8 v_blank_f3[2]; /* for 3D mode */
	u8 v_blank_f4[2]; /* for 3D mode */
	u8 v_blank_f5[2]; /* for 3D mode */
	u8 v_sync_line_aft_3[2];
	u8 v_sync_line_aft_4[2];
	u8 v_sync_line_aft_5[2];
	u8 v_sync_line_aft_6[2];
	u8 v_sync_line_aft_pxl_3[2];
	u8 v_sync_line_aft_pxl_4[2];
	u8 v_sync_line_aft_pxl_5[2];
	u8 v_sync_line_aft_pxl_6[2];
	u8 vact_space_1[2];
	u8 vact_space_2[2];
	u8 vact_space_3[2];
	u8 vact_space_4[2];
	u8 vact_space_5[2];
	u8 vact_space_6[2];
};

struct hdmi_v13_conf {
	struct hdmi_v13_core_regs core;
	struct hdmi_tg_regs tg;
};

struct hdmi_v14_conf {
	struct hdmi_v14_core_regs core;
	struct hdmi_tg_regs tg;
};

struct hdmi_conf_regs {
	int pixel_clock;
	int cea_video_id;
	union {
		struct hdmi_v13_conf v13_conf;
		struct hdmi_v14_conf v14_conf;
	} conf;
};

struct hdmi_context {
	struct device			*dev;
	struct drm_device		*drm_dev;
	bool				hpd;
	bool				powered;
	bool				dvi_mode;
	struct mutex			hdmi_mutex;

	void __iomem			*regs;
	void				*parent_ctx;
	int				irq;

	struct i2c_client		*ddc_port;
	struct i2c_client		*hdmiphy_port;

	/* current hdmiphy conf regs */
	struct hdmi_conf_regs		mode_conf;

	struct hdmi_resources		res;

	int				hpd_gpio;

	enum hdmi_type			type;
};

struct hdmiphy_config {
	int pixel_clock;
	u8 conf[32];
};

/* list of phy config settings */
static const struct hdmiphy_config hdmiphy_v13_configs[] = {
	{
		.pixel_clock = 27000000,
		.conf = {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6B, 0x10, 0x02, 0x51, 0xDF, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xE3, 0x26, 0x00, 0x00, 0x00, 0x00,
		},
	},
	{
		.pixel_clock = 27027000,
		.conf = {
			0x01, 0x05, 0x00, 0xD4, 0x10, 0x9C, 0x09, 0x64,
			0x6B, 0x10, 0x02, 0x51, 0xDF, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xE3, 0x26, 0x00, 0x00, 0x00, 0x00,
		},
	},
	{
		.pixel_clock = 74176000,
		.conf = {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xef, 0x5B,
			0x6D, 0x10, 0x01, 0x51, 0xef, 0xF3, 0x54, 0xb9,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xa5, 0x26, 0x01, 0x00, 0x00, 0x00,
		},
	},
	{
		.pixel_clock = 74250000,
		.conf = {
			0x01, 0x05, 0x00, 0xd8, 0x10, 0x9c, 0xf8, 0x40,
			0x6a, 0x10, 0x01, 0x51, 0xff, 0xf1, 0x54, 0xba,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xe0,
			0x22, 0x40, 0xa4, 0x26, 0x01, 0x00, 0x00, 0x00,
		},
	},
	{
		.pixel_clock = 148500000,
		.conf = {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xf8, 0x40,
			0x6A, 0x18, 0x00, 0x51, 0xff, 0xF1, 0x54, 0xba,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xa4, 0x26, 0x02, 0x00, 0x00, 0x00,
		},
	},
};

static const struct hdmiphy_config hdmiphy_v14_configs[] = {
	{
		.pixel_clock = 25200000,
		.conf = {
			0x01, 0x51, 0x2A, 0x75, 0x40, 0x01, 0x00, 0x08,
			0x82, 0x80, 0xfc, 0xd8, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xf4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 27000000,
		.conf = {
			0x01, 0xd1, 0x22, 0x51, 0x40, 0x08, 0xfc, 0x20,
			0x98, 0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
			0x06, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xe4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 27027000,
		.conf = {
			0x01, 0xd1, 0x2d, 0x72, 0x40, 0x64, 0x12, 0x08,
			0x43, 0xa0, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xe3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		},
	},
	{
		.pixel_clock = 36000000,
		.conf = {
			0x01, 0x51, 0x2d, 0x55, 0x40, 0x01, 0x00, 0x08,
			0x82, 0x80, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xab, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 40000000,
		.conf = {
			0x01, 0x51, 0x32, 0x55, 0x40, 0x01, 0x00, 0x08,
			0x82, 0x80, 0x2c, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0x9a, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 65000000,
		.conf = {
			0x01, 0xd1, 0x36, 0x34, 0x40, 0x1e, 0x0a, 0x08,
			0x82, 0xa0, 0x45, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xbd, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 74176000,
		.conf = {
			0x01, 0xd1, 0x3e, 0x35, 0x40, 0x5b, 0xde, 0x08,
			0x82, 0xa0, 0x73, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x56, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xa6, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 74250000,
		.conf = {
			0x01, 0xd1, 0x1f, 0x10, 0x40, 0x40, 0xf8, 0x08,
			0x81, 0xa0, 0xba, 0xd8, 0x45, 0xa0, 0xac, 0x80,
			0x3c, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xa5, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		},
	},
	{
		.pixel_clock = 83500000,
		.conf = {
			0x01, 0xd1, 0x23, 0x11, 0x40, 0x0c, 0xfb, 0x08,
			0x85, 0xa0, 0xd1, 0xd8, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0x93, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 106500000,
		.conf = {
			0x01, 0xd1, 0x2c, 0x12, 0x40, 0x0c, 0x09, 0x08,
			0x84, 0xa0, 0x0a, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0x73, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 108000000,
		.conf = {
			0x01, 0x51, 0x2d, 0x15, 0x40, 0x01, 0x00, 0x08,
			0x82, 0x80, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xc7, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 146250000,
		.conf = {
			0x01, 0xd1, 0x3d, 0x15, 0x40, 0x18, 0xfd, 0x08,
			0x83, 0xa0, 0x6e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0x50, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 148500000,
		.conf = {
			0x01, 0xd1, 0x1f, 0x00, 0x40, 0x40, 0xf8, 0x08,
			0x81, 0xa0, 0xba, 0xd8, 0x45, 0xa0, 0xac, 0x80,
			0x3c, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0x4b, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	},
};

struct hdmi_infoframe {
	enum HDMI_PACKET_TYPE type;
	u8 ver;
	u8 len;
};

static inline u32 hdmi_reg_read(struct hdmi_context *hdata, u32 reg_id)
{
	return readl(hdata->regs + reg_id);
}

static inline void hdmi_reg_writeb(struct hdmi_context *hdata,
				 u32 reg_id, u8 value)
{
	writeb(value, hdata->regs + reg_id);
}

static inline void hdmi_reg_writemask(struct hdmi_context *hdata,
				 u32 reg_id, u32 value, u32 mask)
{
	u32 old = readl(hdata->regs + reg_id);
	value = (value & mask) | (old & ~mask);
	writel(value, hdata->regs + reg_id);
}

static void hdmi_v13_regs_dump(struct hdmi_context *hdata, char *prefix)
{
#define DUMPREG(reg_id) \
	DRM_DEBUG_KMS("%s:" #reg_id " = %08x\n", prefix, \
	readl(hdata->regs + reg_id))
	DRM_DEBUG_KMS("%s: ---- CONTROL REGISTERS ----\n", prefix);
	DUMPREG(HDMI_INTC_FLAG);
	DUMPREG(HDMI_INTC_CON);
	DUMPREG(HDMI_HPD_STATUS);
	DUMPREG(HDMI_V13_PHY_RSTOUT);
	DUMPREG(HDMI_V13_PHY_VPLL);
	DUMPREG(HDMI_V13_PHY_CMU);
	DUMPREG(HDMI_V13_CORE_RSTOUT);

	DRM_DEBUG_KMS("%s: ---- CORE REGISTERS ----\n", prefix);
	DUMPREG(HDMI_CON_0);
	DUMPREG(HDMI_CON_1);
	DUMPREG(HDMI_CON_2);
	DUMPREG(HDMI_SYS_STATUS);
	DUMPREG(HDMI_V13_PHY_STATUS);
	DUMPREG(HDMI_STATUS_EN);
	DUMPREG(HDMI_HPD);
	DUMPREG(HDMI_MODE_SEL);
	DUMPREG(HDMI_V13_HPD_GEN);
	DUMPREG(HDMI_V13_DC_CONTROL);
	DUMPREG(HDMI_V13_VIDEO_PATTERN_GEN);

	DRM_DEBUG_KMS("%s: ---- CORE SYNC REGISTERS ----\n", prefix);
	DUMPREG(HDMI_H_BLANK_0);
	DUMPREG(HDMI_H_BLANK_1);
	DUMPREG(HDMI_V13_V_BLANK_0);
	DUMPREG(HDMI_V13_V_BLANK_1);
	DUMPREG(HDMI_V13_V_BLANK_2);
	DUMPREG(HDMI_V13_H_V_LINE_0);
	DUMPREG(HDMI_V13_H_V_LINE_1);
	DUMPREG(HDMI_V13_H_V_LINE_2);
	DUMPREG(HDMI_VSYNC_POL);
	DUMPREG(HDMI_INT_PRO_MODE);
	DUMPREG(HDMI_V13_V_BLANK_F_0);
	DUMPREG(HDMI_V13_V_BLANK_F_1);
	DUMPREG(HDMI_V13_V_BLANK_F_2);
	DUMPREG(HDMI_V13_H_SYNC_GEN_0);
	DUMPREG(HDMI_V13_H_SYNC_GEN_1);
	DUMPREG(HDMI_V13_H_SYNC_GEN_2);
	DUMPREG(HDMI_V13_V_SYNC_GEN_1_0);
	DUMPREG(HDMI_V13_V_SYNC_GEN_1_1);
	DUMPREG(HDMI_V13_V_SYNC_GEN_1_2);
	DUMPREG(HDMI_V13_V_SYNC_GEN_2_0);
	DUMPREG(HDMI_V13_V_SYNC_GEN_2_1);
	DUMPREG(HDMI_V13_V_SYNC_GEN_2_2);
	DUMPREG(HDMI_V13_V_SYNC_GEN_3_0);
	DUMPREG(HDMI_V13_V_SYNC_GEN_3_1);
	DUMPREG(HDMI_V13_V_SYNC_GEN_3_2);

	DRM_DEBUG_KMS("%s: ---- TG REGISTERS ----\n", prefix);
	DUMPREG(HDMI_TG_CMD);
	DUMPREG(HDMI_TG_H_FSZ_L);
	DUMPREG(HDMI_TG_H_FSZ_H);
	DUMPREG(HDMI_TG_HACT_ST_L);
	DUMPREG(HDMI_TG_HACT_ST_H);
	DUMPREG(HDMI_TG_HACT_SZ_L);
	DUMPREG(HDMI_TG_HACT_SZ_H);
	DUMPREG(HDMI_TG_V_FSZ_L);
	DUMPREG(HDMI_TG_V_FSZ_H);
	DUMPREG(HDMI_TG_VSYNC_L);
	DUMPREG(HDMI_TG_VSYNC_H);
	DUMPREG(HDMI_TG_VSYNC2_L);
	DUMPREG(HDMI_TG_VSYNC2_H);
	DUMPREG(HDMI_TG_VACT_ST_L);
	DUMPREG(HDMI_TG_VACT_ST_H);
	DUMPREG(HDMI_TG_VACT_SZ_L);
	DUMPREG(HDMI_TG_VACT_SZ_H);
	DUMPREG(HDMI_TG_FIELD_CHG_L);
	DUMPREG(HDMI_TG_FIELD_CHG_H);
	DUMPREG(HDMI_TG_VACT_ST2_L);
	DUMPREG(HDMI_TG_VACT_ST2_H);
	DUMPREG(HDMI_TG_VSYNC_TOP_HDMI_L);
	DUMPREG(HDMI_TG_VSYNC_TOP_HDMI_H);
	DUMPREG(HDMI_TG_VSYNC_BOT_HDMI_L);
	DUMPREG(HDMI_TG_VSYNC_BOT_HDMI_H);
	DUMPREG(HDMI_TG_FIELD_TOP_HDMI_L);
	DUMPREG(HDMI_TG_FIELD_TOP_HDMI_H);
	DUMPREG(HDMI_TG_FIELD_BOT_HDMI_L);
	DUMPREG(HDMI_TG_FIELD_BOT_HDMI_H);
#undef DUMPREG
}

static void hdmi_v14_regs_dump(struct hdmi_context *hdata, char *prefix)
{
	int i;

#define DUMPREG(reg_id) \
	DRM_DEBUG_KMS("%s:" #reg_id " = %08x\n", prefix, \
	readl(hdata->regs + reg_id))

	DRM_DEBUG_KMS("%s: ---- CONTROL REGISTERS ----\n", prefix);
	DUMPREG(HDMI_INTC_CON);
	DUMPREG(HDMI_INTC_FLAG);
	DUMPREG(HDMI_HPD_STATUS);
	DUMPREG(HDMI_INTC_CON_1);
	DUMPREG(HDMI_INTC_FLAG_1);
	DUMPREG(HDMI_PHY_STATUS_0);
	DUMPREG(HDMI_PHY_STATUS_PLL);
	DUMPREG(HDMI_PHY_CON_0);
	DUMPREG(HDMI_PHY_RSTOUT);
	DUMPREG(HDMI_PHY_VPLL);
	DUMPREG(HDMI_PHY_CMU);
	DUMPREG(HDMI_CORE_RSTOUT);

	DRM_DEBUG_KMS("%s: ---- CORE REGISTERS ----\n", prefix);
	DUMPREG(HDMI_CON_0);
	DUMPREG(HDMI_CON_1);
	DUMPREG(HDMI_CON_2);
	DUMPREG(HDMI_SYS_STATUS);
	DUMPREG(HDMI_PHY_STATUS_0);
	DUMPREG(HDMI_STATUS_EN);
	DUMPREG(HDMI_HPD);
	DUMPREG(HDMI_MODE_SEL);
	DUMPREG(HDMI_ENC_EN);
	DUMPREG(HDMI_DC_CONTROL);
	DUMPREG(HDMI_VIDEO_PATTERN_GEN);

	DRM_DEBUG_KMS("%s: ---- CORE SYNC REGISTERS ----\n", prefix);
	DUMPREG(HDMI_H_BLANK_0);
	DUMPREG(HDMI_H_BLANK_1);
	DUMPREG(HDMI_V2_BLANK_0);
	DUMPREG(HDMI_V2_BLANK_1);
	DUMPREG(HDMI_V1_BLANK_0);
	DUMPREG(HDMI_V1_BLANK_1);
	DUMPREG(HDMI_V_LINE_0);
	DUMPREG(HDMI_V_LINE_1);
	DUMPREG(HDMI_H_LINE_0);
	DUMPREG(HDMI_H_LINE_1);
	DUMPREG(HDMI_HSYNC_POL);

	DUMPREG(HDMI_VSYNC_POL);
	DUMPREG(HDMI_INT_PRO_MODE);
	DUMPREG(HDMI_V_BLANK_F0_0);
	DUMPREG(HDMI_V_BLANK_F0_1);
	DUMPREG(HDMI_V_BLANK_F1_0);
	DUMPREG(HDMI_V_BLANK_F1_1);

	DUMPREG(HDMI_H_SYNC_START_0);
	DUMPREG(HDMI_H_SYNC_START_1);
	DUMPREG(HDMI_H_SYNC_END_0);
	DUMPREG(HDMI_H_SYNC_END_1);

	DUMPREG(HDMI_V_SYNC_LINE_BEF_2_0);
	DUMPREG(HDMI_V_SYNC_LINE_BEF_2_1);
	DUMPREG(HDMI_V_SYNC_LINE_BEF_1_0);
	DUMPREG(HDMI_V_SYNC_LINE_BEF_1_1);

	DUMPREG(HDMI_V_SYNC_LINE_AFT_2_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_2_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_1_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_1_1);

	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_2_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_2_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_1_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_1_1);

	DUMPREG(HDMI_V_BLANK_F2_0);
	DUMPREG(HDMI_V_BLANK_F2_1);
	DUMPREG(HDMI_V_BLANK_F3_0);
	DUMPREG(HDMI_V_BLANK_F3_1);
	DUMPREG(HDMI_V_BLANK_F4_0);
	DUMPREG(HDMI_V_BLANK_F4_1);
	DUMPREG(HDMI_V_BLANK_F5_0);
	DUMPREG(HDMI_V_BLANK_F5_1);

	DUMPREG(HDMI_V_SYNC_LINE_AFT_3_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_3_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_4_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_4_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_5_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_5_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_6_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_6_1);

	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_3_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_3_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_4_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_4_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_5_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_5_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_6_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_6_1);

	DUMPREG(HDMI_VACT_SPACE_1_0);
	DUMPREG(HDMI_VACT_SPACE_1_1);
	DUMPREG(HDMI_VACT_SPACE_2_0);
	DUMPREG(HDMI_VACT_SPACE_2_1);
	DUMPREG(HDMI_VACT_SPACE_3_0);
	DUMPREG(HDMI_VACT_SPACE_3_1);
	DUMPREG(HDMI_VACT_SPACE_4_0);
	DUMPREG(HDMI_VACT_SPACE_4_1);
	DUMPREG(HDMI_VACT_SPACE_5_0);
	DUMPREG(HDMI_VACT_SPACE_5_1);
	DUMPREG(HDMI_VACT_SPACE_6_0);
	DUMPREG(HDMI_VACT_SPACE_6_1);

	DRM_DEBUG_KMS("%s: ---- TG REGISTERS ----\n", prefix);
	DUMPREG(HDMI_TG_CMD);
	DUMPREG(HDMI_TG_H_FSZ_L);
	DUMPREG(HDMI_TG_H_FSZ_H);
	DUMPREG(HDMI_TG_HACT_ST_L);
	DUMPREG(HDMI_TG_HACT_ST_H);
	DUMPREG(HDMI_TG_HACT_SZ_L);
	DUMPREG(HDMI_TG_HACT_SZ_H);
	DUMPREG(HDMI_TG_V_FSZ_L);
	DUMPREG(HDMI_TG_V_FSZ_H);
	DUMPREG(HDMI_TG_VSYNC_L);
	DUMPREG(HDMI_TG_VSYNC_H);
	DUMPREG(HDMI_TG_VSYNC2_L);
	DUMPREG(HDMI_TG_VSYNC2_H);
	DUMPREG(HDMI_TG_VACT_ST_L);
	DUMPREG(HDMI_TG_VACT_ST_H);
	DUMPREG(HDMI_TG_VACT_SZ_L);
	DUMPREG(HDMI_TG_VACT_SZ_H);
	DUMPREG(HDMI_TG_FIELD_CHG_L);
	DUMPREG(HDMI_TG_FIELD_CHG_H);
	DUMPREG(HDMI_TG_VACT_ST2_L);
	DUMPREG(HDMI_TG_VACT_ST2_H);
	DUMPREG(HDMI_TG_VACT_ST3_L);
	DUMPREG(HDMI_TG_VACT_ST3_H);
	DUMPREG(HDMI_TG_VACT_ST4_L);
	DUMPREG(HDMI_TG_VACT_ST4_H);
	DUMPREG(HDMI_TG_VSYNC_TOP_HDMI_L);
	DUMPREG(HDMI_TG_VSYNC_TOP_HDMI_H);
	DUMPREG(HDMI_TG_VSYNC_BOT_HDMI_L);
	DUMPREG(HDMI_TG_VSYNC_BOT_HDMI_H);
	DUMPREG(HDMI_TG_FIELD_TOP_HDMI_L);
	DUMPREG(HDMI_TG_FIELD_TOP_HDMI_H);
	DUMPREG(HDMI_TG_FIELD_BOT_HDMI_L);
	DUMPREG(HDMI_TG_FIELD_BOT_HDMI_H);
	DUMPREG(HDMI_TG_3D);

	DRM_DEBUG_KMS("%s: ---- PACKET REGISTERS ----\n", prefix);
	DUMPREG(HDMI_AVI_CON);
	DUMPREG(HDMI_AVI_HEADER0);
	DUMPREG(HDMI_AVI_HEADER1);
	DUMPREG(HDMI_AVI_HEADER2);
	DUMPREG(HDMI_AVI_CHECK_SUM);
	DUMPREG(HDMI_VSI_CON);
	DUMPREG(HDMI_VSI_HEADER0);
	DUMPREG(HDMI_VSI_HEADER1);
	DUMPREG(HDMI_VSI_HEADER2);
	for (i = 0; i < 7; ++i)
		DUMPREG(HDMI_VSI_DATA(i));

#undef DUMPREG
}

static void hdmi_regs_dump(struct hdmi_context *hdata, char *prefix)
{
	if (hdata->type == HDMI_TYPE13)
		hdmi_v13_regs_dump(hdata, prefix);
	else
		hdmi_v14_regs_dump(hdata, prefix);
}

static u8 hdmi_chksum(struct hdmi_context *hdata,
			u32 start, u8 len, u32 hdr_sum)
{
	int i;

	/* hdr_sum : header0 + header1 + header2
	* start : start address of packet byte1
	* len : packet bytes - 1 */
	for (i = 0; i < len; ++i)
		hdr_sum += 0xff & hdmi_reg_read(hdata, start + i * 4);

	/* return 2's complement of 8 bit hdr_sum */
	return (u8)(~(hdr_sum & 0xff) + 1);
}

static void hdmi_reg_infoframe(struct hdmi_context *hdata,
			struct hdmi_infoframe *infoframe)
{
	u32 hdr_sum;
	u8 chksum;
	u32 aspect_ratio;
	u32 mod;
	u32 vic;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	mod = hdmi_reg_read(hdata, HDMI_MODE_SEL);
	if (hdata->dvi_mode) {
		hdmi_reg_writeb(hdata, HDMI_VSI_CON,
				HDMI_VSI_CON_DO_NOT_TRANSMIT);
		hdmi_reg_writeb(hdata, HDMI_AVI_CON,
				HDMI_AVI_CON_DO_NOT_TRANSMIT);
		hdmi_reg_writeb(hdata, HDMI_AUI_CON, HDMI_AUI_CON_NO_TRAN);
		return;
	}

	switch (infoframe->type) {
	case HDMI_PACKET_TYPE_AVI:
		hdmi_reg_writeb(hdata, HDMI_AVI_CON, HDMI_AVI_CON_EVERY_VSYNC);
		hdmi_reg_writeb(hdata, HDMI_AVI_HEADER0, infoframe->type);
		hdmi_reg_writeb(hdata, HDMI_AVI_HEADER1, infoframe->ver);
		hdmi_reg_writeb(hdata, HDMI_AVI_HEADER2, infoframe->len);
		hdr_sum = infoframe->type + infoframe->ver + infoframe->len;

		/* Output format zero hardcoded ,RGB YBCR selection */
		hdmi_reg_writeb(hdata, HDMI_AVI_BYTE(1), 0 << 5 |
			AVI_ACTIVE_FORMAT_VALID |
			AVI_UNDERSCANNED_DISPLAY_VALID);

		aspect_ratio = AVI_PIC_ASPECT_RATIO_16_9;

		hdmi_reg_writeb(hdata, HDMI_AVI_BYTE(2), aspect_ratio |
				AVI_SAME_AS_PIC_ASPECT_RATIO);

		vic = hdata->mode_conf.cea_video_id;
		hdmi_reg_writeb(hdata, HDMI_AVI_BYTE(4), vic);

		chksum = hdmi_chksum(hdata, HDMI_AVI_BYTE(1),
					infoframe->len, hdr_sum);
		DRM_DEBUG_KMS("AVI checksum = 0x%x\n", chksum);
		hdmi_reg_writeb(hdata, HDMI_AVI_CHECK_SUM, chksum);
		break;
	case HDMI_PACKET_TYPE_AUI:
		hdmi_reg_writeb(hdata, HDMI_AUI_CON, 0x02);
		hdmi_reg_writeb(hdata, HDMI_AUI_HEADER0, infoframe->type);
		hdmi_reg_writeb(hdata, HDMI_AUI_HEADER1, infoframe->ver);
		hdmi_reg_writeb(hdata, HDMI_AUI_HEADER2, infoframe->len);
		hdr_sum = infoframe->type + infoframe->ver + infoframe->len;
		chksum = hdmi_chksum(hdata, HDMI_AUI_BYTE(1),
					infoframe->len, hdr_sum);
		DRM_DEBUG_KMS("AUI checksum = 0x%x\n", chksum);
		hdmi_reg_writeb(hdata, HDMI_AUI_CHECK_SUM, chksum);
		break;
	default:
		break;
	}
}

static bool hdmi_is_connected(void *ctx)
{
	struct hdmi_context *hdata = ctx;

	return hdata->hpd;
}

static struct edid *hdmi_get_edid(void *ctx, struct drm_connector *connector)
{
	struct edid *raw_edid;
	struct hdmi_context *hdata = ctx;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (!hdata->ddc_port)
		return ERR_PTR(-ENODEV);

	raw_edid = drm_get_edid(connector, hdata->ddc_port->adapter);
	if (!raw_edid)
		return ERR_PTR(-ENODEV);

	hdata->dvi_mode = !drm_detect_hdmi_monitor(raw_edid);
	DRM_DEBUG_KMS("%s : width[%d] x height[%d]\n",
		(hdata->dvi_mode ? "dvi monitor" : "hdmi monitor"),
		raw_edid->width_cm, raw_edid->height_cm);

	return raw_edid;
}

static int hdmi_find_phy_conf(struct hdmi_context *hdata, u32 pixel_clock)
{
	const struct hdmiphy_config *confs;
	int count, i;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (hdata->type == HDMI_TYPE13) {
		confs = hdmiphy_v13_configs;
		count = ARRAY_SIZE(hdmiphy_v13_configs);
	} else if (hdata->type == HDMI_TYPE14) {
		confs = hdmiphy_v14_configs;
		count = ARRAY_SIZE(hdmiphy_v14_configs);
	} else
		return -EINVAL;

	for (i = 0; i < count; i++)
		if (confs[i].pixel_clock == pixel_clock)
			return i;

	DRM_DEBUG_KMS("Could not find phy config for %d\n", pixel_clock);
	return -EINVAL;
}

static int hdmi_check_timing(void *ctx, struct fb_videomode *timing)
{
	struct hdmi_context *hdata = ctx;
	int ret;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	DRM_DEBUG_KMS("[%d]x[%d] [%d]Hz [%x]\n", timing->xres,
			timing->yres, timing->refresh,
			timing->vmode);

	ret = hdmi_find_phy_conf(hdata, timing->pixclock);
	if (ret < 0)
		return ret;
	return 0;
}

static void hdmi_set_acr(u32 freq, u8 *acr)
{
	u32 n, cts;

	switch (freq) {
	case 32000:
		n = 4096;
		cts = 27000;
		break;
	case 44100:
		n = 6272;
		cts = 30000;
		break;
	case 88200:
		n = 12544;
		cts = 30000;
		break;
	case 176400:
		n = 25088;
		cts = 30000;
		break;
	case 48000:
		n = 6144;
		cts = 27000;
		break;
	case 96000:
		n = 12288;
		cts = 27000;
		break;
	case 192000:
		n = 24576;
		cts = 27000;
		break;
	default:
		n = 0;
		cts = 0;
		break;
	}

	acr[1] = cts >> 16;
	acr[2] = cts >> 8 & 0xff;
	acr[3] = cts & 0xff;

	acr[4] = n >> 16;
	acr[5] = n >> 8 & 0xff;
	acr[6] = n & 0xff;
}

static void hdmi_reg_acr(struct hdmi_context *hdata, u8 *acr)
{
	hdmi_reg_writeb(hdata, HDMI_ACR_N0, acr[6]);
	hdmi_reg_writeb(hdata, HDMI_ACR_N1, acr[5]);
	hdmi_reg_writeb(hdata, HDMI_ACR_N2, acr[4]);
	hdmi_reg_writeb(hdata, HDMI_ACR_MCTS0, acr[3]);
	hdmi_reg_writeb(hdata, HDMI_ACR_MCTS1, acr[2]);
	hdmi_reg_writeb(hdata, HDMI_ACR_MCTS2, acr[1]);
	hdmi_reg_writeb(hdata, HDMI_ACR_CTS0, acr[3]);
	hdmi_reg_writeb(hdata, HDMI_ACR_CTS1, acr[2]);
	hdmi_reg_writeb(hdata, HDMI_ACR_CTS2, acr[1]);

	if (hdata->type == HDMI_TYPE13)
		hdmi_reg_writeb(hdata, HDMI_V13_ACR_CON, 4);
	else
		hdmi_reg_writeb(hdata, HDMI_ACR_CON, 4);
}

static void hdmi_audio_init(struct hdmi_context *hdata)
{
	u32 sample_rate, bits_per_sample, frame_size_code;
	u32 data_num, bit_ch, sample_frq;
	u32 val;
	u8 acr[7];

	sample_rate = 44100;
	bits_per_sample = 16;
	frame_size_code = 0;

	switch (bits_per_sample) {
	case 20:
		data_num = 2;
		bit_ch  = 1;
		break;
	case 24:
		data_num = 3;
		bit_ch  = 1;
		break;
	default:
		data_num = 1;
		bit_ch  = 0;
		break;
	}

	hdmi_set_acr(sample_rate, acr);
	hdmi_reg_acr(hdata, acr);

	hdmi_reg_writeb(hdata, HDMI_I2S_MUX_CON, HDMI_I2S_IN_DISABLE
				| HDMI_I2S_AUD_I2S | HDMI_I2S_CUV_I2S_ENABLE
				| HDMI_I2S_MUX_ENABLE);

	hdmi_reg_writeb(hdata, HDMI_I2S_MUX_CH, HDMI_I2S_CH0_EN
			| HDMI_I2S_CH1_EN | HDMI_I2S_CH2_EN);

	hdmi_reg_writeb(hdata, HDMI_I2S_MUX_CUV, HDMI_I2S_CUV_RL_EN);

	sample_frq = (sample_rate == 44100) ? 0 :
			(sample_rate == 48000) ? 2 :
			(sample_rate == 32000) ? 3 :
			(sample_rate == 96000) ? 0xa : 0x0;

	hdmi_reg_writeb(hdata, HDMI_I2S_CLK_CON, HDMI_I2S_CLK_DIS);
	hdmi_reg_writeb(hdata, HDMI_I2S_CLK_CON, HDMI_I2S_CLK_EN);

	val = hdmi_reg_read(hdata, HDMI_I2S_DSD_CON) | 0x01;
	hdmi_reg_writeb(hdata, HDMI_I2S_DSD_CON, val);

	/* Configuration I2S input ports. Configure I2S_PIN_SEL_0~4 */
	hdmi_reg_writeb(hdata, HDMI_I2S_PIN_SEL_0, HDMI_I2S_SEL_SCLK(5)
			| HDMI_I2S_SEL_LRCK(6));
	hdmi_reg_writeb(hdata, HDMI_I2S_PIN_SEL_1, HDMI_I2S_SEL_SDATA1(1)
			| HDMI_I2S_SEL_SDATA2(4));
	hdmi_reg_writeb(hdata, HDMI_I2S_PIN_SEL_2, HDMI_I2S_SEL_SDATA3(1)
			| HDMI_I2S_SEL_SDATA2(2));
	hdmi_reg_writeb(hdata, HDMI_I2S_PIN_SEL_3, HDMI_I2S_SEL_DSD(0));

	/* I2S_CON_1 & 2 */
	hdmi_reg_writeb(hdata, HDMI_I2S_CON_1, HDMI_I2S_SCLK_FALLING_EDGE
			| HDMI_I2S_L_CH_LOW_POL);
	hdmi_reg_writeb(hdata, HDMI_I2S_CON_2, HDMI_I2S_MSB_FIRST_MODE
			| HDMI_I2S_SET_BIT_CH(bit_ch)
			| HDMI_I2S_SET_SDATA_BIT(data_num)
			| HDMI_I2S_BASIC_FORMAT);

	/* Configure register related to CUV information */
	hdmi_reg_writeb(hdata, HDMI_I2S_CH_ST_0, HDMI_I2S_CH_STATUS_MODE_0
			| HDMI_I2S_2AUD_CH_WITHOUT_PREEMPH
			| HDMI_I2S_COPYRIGHT
			| HDMI_I2S_LINEAR_PCM
			| HDMI_I2S_CONSUMER_FORMAT);
	hdmi_reg_writeb(hdata, HDMI_I2S_CH_ST_1, HDMI_I2S_CD_PLAYER);
	hdmi_reg_writeb(hdata, HDMI_I2S_CH_ST_2, HDMI_I2S_SET_SOURCE_NUM(0));
	hdmi_reg_writeb(hdata, HDMI_I2S_CH_ST_3, HDMI_I2S_CLK_ACCUR_LEVEL_2
			| HDMI_I2S_SET_SMP_FREQ(sample_frq));
	hdmi_reg_writeb(hdata, HDMI_I2S_CH_ST_4,
			HDMI_I2S_ORG_SMP_FREQ_44_1
			| HDMI_I2S_WORD_LEN_MAX24_24BITS
			| HDMI_I2S_WORD_LEN_MAX_24BITS);

	hdmi_reg_writeb(hdata, HDMI_I2S_CH_ST_CON, HDMI_I2S_CH_STATUS_RELOAD);
}

static void hdmi_audio_control(struct hdmi_context *hdata, bool onoff)
{
	if (hdata->dvi_mode)
		return;

	hdmi_reg_writeb(hdata, HDMI_AUI_CON, onoff ? 2 : 0);
	hdmi_reg_writemask(hdata, HDMI_CON_0, onoff ?
			HDMI_ASP_EN : HDMI_ASP_DIS, HDMI_ASP_MASK);
}

static void hdmi_conf_reset(struct hdmi_context *hdata)
{
	u32 reg;

	if (hdata->type == HDMI_TYPE13)
		reg = HDMI_V13_CORE_RSTOUT;
	else
		reg = HDMI_CORE_RSTOUT;

	/* resetting HDMI core */
	hdmi_reg_writemask(hdata, reg,  0, HDMI_CORE_SW_RSTOUT);
	usleep_range(10000, 12000);
	hdmi_reg_writemask(hdata, reg, ~0, HDMI_CORE_SW_RSTOUT);
	usleep_range(10000, 12000);
}

static void hdmi_conf_init(struct hdmi_context *hdata)
{
	struct hdmi_infoframe infoframe;

	/* disable HPD interrupts from HDMI IP block, use GPIO instead */
	hdmi_reg_writemask(hdata, HDMI_INTC_CON, 0, HDMI_INTC_EN_GLOBAL |
		HDMI_INTC_EN_HPD_PLUG | HDMI_INTC_EN_HPD_UNPLUG);

	/* choose HDMI mode */
	hdmi_reg_writemask(hdata, HDMI_MODE_SEL,
		HDMI_MODE_HDMI_EN, HDMI_MODE_MASK);
	/* disable bluescreen */
	hdmi_reg_writemask(hdata, HDMI_CON_0, 0, HDMI_BLUE_SCR_EN);

	if (hdata->dvi_mode) {
		/* choose DVI mode */
		hdmi_reg_writemask(hdata, HDMI_MODE_SEL,
				HDMI_MODE_DVI_EN, HDMI_MODE_MASK);
		hdmi_reg_writeb(hdata, HDMI_CON_2,
				HDMI_VID_PREAMBLE_DIS | HDMI_GUARD_BAND_DIS);
	}

	if (hdata->type == HDMI_TYPE13) {
		/* choose bluescreen (fecal) color */
		hdmi_reg_writeb(hdata, HDMI_V13_BLUE_SCREEN_0, 0x12);
		hdmi_reg_writeb(hdata, HDMI_V13_BLUE_SCREEN_1, 0x34);
		hdmi_reg_writeb(hdata, HDMI_V13_BLUE_SCREEN_2, 0x56);

		/* enable AVI packet every vsync, fixes purple line problem */
		hdmi_reg_writeb(hdata, HDMI_V13_AVI_CON, 0x02);
		/* force RGB, look to CEA-861-D, table 7 for more detail */
		hdmi_reg_writeb(hdata, HDMI_V13_AVI_BYTE(0), 0 << 5);
		hdmi_reg_writemask(hdata, HDMI_CON_1, 0x10 << 5, 0x11 << 5);

		hdmi_reg_writeb(hdata, HDMI_V13_SPD_CON, 0x02);
		hdmi_reg_writeb(hdata, HDMI_V13_AUI_CON, 0x02);
		hdmi_reg_writeb(hdata, HDMI_V13_ACR_CON, 0x04);
	} else {
		infoframe.type = HDMI_PACKET_TYPE_AVI;
		infoframe.ver = HDMI_AVI_VERSION;
		infoframe.len = HDMI_AVI_LENGTH;
		hdmi_reg_infoframe(hdata, &infoframe);

		infoframe.type = HDMI_PACKET_TYPE_AUI;
		infoframe.ver = HDMI_AUI_VERSION;
		infoframe.len = HDMI_AUI_LENGTH;
		hdmi_reg_infoframe(hdata, &infoframe);

		/* enable AVI packet every vsync, fixes purple line problem */
		hdmi_reg_writemask(hdata, HDMI_CON_1, 2, 3 << 5);
	}
}

static void hdmi_v13_timing_apply(struct hdmi_context *hdata)
{
	const struct hdmi_tg_regs *tg = &hdata->mode_conf.conf.v13_conf.tg;
	const struct hdmi_v13_core_regs *core =
		&hdata->mode_conf.conf.v13_conf.core;
	int tries;

	/* setting core registers */
	hdmi_reg_writeb(hdata, HDMI_H_BLANK_0, core->h_blank[0]);
	hdmi_reg_writeb(hdata, HDMI_H_BLANK_1, core->h_blank[1]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_BLANK_0, core->v_blank[0]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_BLANK_1, core->v_blank[1]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_BLANK_2, core->v_blank[2]);
	hdmi_reg_writeb(hdata, HDMI_V13_H_V_LINE_0, core->h_v_line[0]);
	hdmi_reg_writeb(hdata, HDMI_V13_H_V_LINE_1, core->h_v_line[1]);
	hdmi_reg_writeb(hdata, HDMI_V13_H_V_LINE_2, core->h_v_line[2]);
	hdmi_reg_writeb(hdata, HDMI_VSYNC_POL, core->vsync_pol[0]);
	hdmi_reg_writeb(hdata, HDMI_INT_PRO_MODE, core->int_pro_mode[0]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_BLANK_F_0, core->v_blank_f[0]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_BLANK_F_1, core->v_blank_f[1]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_BLANK_F_2, core->v_blank_f[2]);
	hdmi_reg_writeb(hdata, HDMI_V13_H_SYNC_GEN_0, core->h_sync_gen[0]);
	hdmi_reg_writeb(hdata, HDMI_V13_H_SYNC_GEN_1, core->h_sync_gen[1]);
	hdmi_reg_writeb(hdata, HDMI_V13_H_SYNC_GEN_2, core->h_sync_gen[2]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_1_0, core->v_sync_gen1[0]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_1_1, core->v_sync_gen1[1]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_1_2, core->v_sync_gen1[2]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_2_0, core->v_sync_gen2[0]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_2_1, core->v_sync_gen2[1]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_2_2, core->v_sync_gen2[2]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_3_0, core->v_sync_gen3[0]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_3_1, core->v_sync_gen3[1]);
	hdmi_reg_writeb(hdata, HDMI_V13_V_SYNC_GEN_3_2, core->v_sync_gen3[2]);
	/* Timing generator registers */
	hdmi_reg_writeb(hdata, HDMI_TG_H_FSZ_L, tg->h_fsz[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_H_FSZ_H, tg->h_fsz[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_HACT_ST_L, tg->hact_st[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_HACT_ST_H, tg->hact_st[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_HACT_SZ_L, tg->hact_sz[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_HACT_SZ_H, tg->hact_sz[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_V_FSZ_L, tg->v_fsz[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_V_FSZ_H, tg->v_fsz[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_L, tg->vsync[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_H, tg->vsync[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC2_L, tg->vsync2[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC2_H, tg->vsync2[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST_L, tg->vact_st[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST_H, tg->vact_st[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_SZ_L, tg->vact_sz[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_SZ_H, tg->vact_sz[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_CHG_L, tg->field_chg[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_CHG_H, tg->field_chg[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST2_L, tg->vact_st2[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST2_H, tg->vact_st2[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_TOP_HDMI_L, tg->vsync_top_hdmi[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_TOP_HDMI_H, tg->vsync_top_hdmi[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_BOT_HDMI_L, tg->vsync_bot_hdmi[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_BOT_HDMI_H, tg->vsync_bot_hdmi[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_TOP_HDMI_L, tg->field_top_hdmi[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_TOP_HDMI_H, tg->field_top_hdmi[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_BOT_HDMI_L, tg->field_bot_hdmi[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_BOT_HDMI_H, tg->field_bot_hdmi[1]);

	/* waiting for HDMIPHY's PLL to get to steady state */
	for (tries = 100; tries; --tries) {
		u32 val = hdmi_reg_read(hdata, HDMI_V13_PHY_STATUS);
		if (val & HDMI_PHY_STATUS_READY)
			break;
		usleep_range(1000, 2000);
	}
	/* steady state not achieved */
	if (tries == 0) {
		DRM_ERROR("hdmiphy's pll could not reach steady state.\n");
		hdmi_regs_dump(hdata, "timing apply");
	}

	clk_disable(hdata->res.sclk_hdmi);
	clk_set_parent(hdata->res.sclk_hdmi, hdata->res.sclk_hdmiphy);
	clk_enable(hdata->res.sclk_hdmi);

	/* enable HDMI and timing generator */
	hdmi_reg_writemask(hdata, HDMI_CON_0, ~0, HDMI_EN);
	if (core->int_pro_mode[0])
		hdmi_reg_writemask(hdata, HDMI_TG_CMD, ~0, HDMI_TG_EN |
				HDMI_FIELD_EN);
	else
		hdmi_reg_writemask(hdata, HDMI_TG_CMD, ~0, HDMI_TG_EN);
}

static void hdmi_v14_timing_apply(struct hdmi_context *hdata)
{
	const struct hdmi_tg_regs *tg = &hdata->mode_conf.conf.v14_conf.tg;
	const struct hdmi_v14_core_regs *core =
		&hdata->mode_conf.conf.v14_conf.core;
	int tries;

	/* setting core registers */
	hdmi_reg_writeb(hdata, HDMI_H_BLANK_0, core->h_blank[0]);
	hdmi_reg_writeb(hdata, HDMI_H_BLANK_1, core->h_blank[1]);
	hdmi_reg_writeb(hdata, HDMI_V2_BLANK_0, core->v2_blank[0]);
	hdmi_reg_writeb(hdata, HDMI_V2_BLANK_1, core->v2_blank[1]);
	hdmi_reg_writeb(hdata, HDMI_V1_BLANK_0, core->v1_blank[0]);
	hdmi_reg_writeb(hdata, HDMI_V1_BLANK_1, core->v1_blank[1]);
	hdmi_reg_writeb(hdata, HDMI_V_LINE_0, core->v_line[0]);
	hdmi_reg_writeb(hdata, HDMI_V_LINE_1, core->v_line[1]);
	hdmi_reg_writeb(hdata, HDMI_H_LINE_0, core->h_line[0]);
	hdmi_reg_writeb(hdata, HDMI_H_LINE_1, core->h_line[1]);
	hdmi_reg_writeb(hdata, HDMI_HSYNC_POL, core->hsync_pol[0]);
	hdmi_reg_writeb(hdata, HDMI_VSYNC_POL, core->vsync_pol[0]);
	hdmi_reg_writeb(hdata, HDMI_INT_PRO_MODE, core->int_pro_mode[0]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F0_0, core->v_blank_f0[0]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F0_1, core->v_blank_f0[1]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F1_0, core->v_blank_f1[0]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F1_1, core->v_blank_f1[1]);
	hdmi_reg_writeb(hdata, HDMI_H_SYNC_START_0, core->h_sync_start[0]);
	hdmi_reg_writeb(hdata, HDMI_H_SYNC_START_1, core->h_sync_start[1]);
	hdmi_reg_writeb(hdata, HDMI_H_SYNC_END_0, core->h_sync_end[0]);
	hdmi_reg_writeb(hdata, HDMI_H_SYNC_END_1, core->h_sync_end[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_BEF_2_0,
			core->v_sync_line_bef_2[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_BEF_2_1,
			core->v_sync_line_bef_2[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_BEF_1_0,
			core->v_sync_line_bef_1[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_BEF_1_1,
			core->v_sync_line_bef_1[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_2_0,
			core->v_sync_line_aft_2[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_2_1,
			core->v_sync_line_aft_2[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_1_0,
			core->v_sync_line_aft_1[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_1_1,
			core->v_sync_line_aft_1[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_2_0,
			core->v_sync_line_aft_pxl_2[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_2_1,
			core->v_sync_line_aft_pxl_2[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_1_0,
			core->v_sync_line_aft_pxl_1[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_1_1,
			core->v_sync_line_aft_pxl_1[1]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F2_0, core->v_blank_f2[0]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F2_1, core->v_blank_f2[1]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F3_0, core->v_blank_f3[0]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F3_1, core->v_blank_f3[1]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F4_0, core->v_blank_f4[0]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F4_1, core->v_blank_f4[1]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F5_0, core->v_blank_f5[0]);
	hdmi_reg_writeb(hdata, HDMI_V_BLANK_F5_1, core->v_blank_f5[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_3_0,
			core->v_sync_line_aft_3[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_3_1,
			core->v_sync_line_aft_3[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_4_0,
			core->v_sync_line_aft_4[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_4_1,
			core->v_sync_line_aft_4[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_5_0,
			core->v_sync_line_aft_5[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_5_1,
			core->v_sync_line_aft_5[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_6_0,
			core->v_sync_line_aft_6[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_6_1,
			core->v_sync_line_aft_6[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_3_0,
			core->v_sync_line_aft_pxl_3[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_3_1,
			core->v_sync_line_aft_pxl_3[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_4_0,
			core->v_sync_line_aft_pxl_4[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_4_1,
			core->v_sync_line_aft_pxl_4[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_5_0,
			core->v_sync_line_aft_pxl_5[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_5_1,
			core->v_sync_line_aft_pxl_5[1]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_6_0,
			core->v_sync_line_aft_pxl_6[0]);
	hdmi_reg_writeb(hdata, HDMI_V_SYNC_LINE_AFT_PXL_6_1,
			core->v_sync_line_aft_pxl_6[1]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_1_0, core->vact_space_1[0]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_1_1, core->vact_space_1[1]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_2_0, core->vact_space_2[0]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_2_1, core->vact_space_2[1]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_3_0, core->vact_space_3[0]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_3_1, core->vact_space_3[1]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_4_0, core->vact_space_4[0]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_4_1, core->vact_space_4[1]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_5_0, core->vact_space_5[0]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_5_1, core->vact_space_5[1]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_6_0, core->vact_space_6[0]);
	hdmi_reg_writeb(hdata, HDMI_VACT_SPACE_6_1, core->vact_space_6[1]);

	/* Timing generator registers */
	hdmi_reg_writeb(hdata, HDMI_TG_H_FSZ_L, tg->h_fsz[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_H_FSZ_H, tg->h_fsz[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_HACT_ST_L, tg->hact_st[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_HACT_ST_H, tg->hact_st[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_HACT_SZ_L, tg->hact_sz[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_HACT_SZ_H, tg->hact_sz[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_V_FSZ_L, tg->v_fsz[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_V_FSZ_H, tg->v_fsz[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_L, tg->vsync[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_H, tg->vsync[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC2_L, tg->vsync2[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC2_H, tg->vsync2[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST_L, tg->vact_st[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST_H, tg->vact_st[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_SZ_L, tg->vact_sz[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_SZ_H, tg->vact_sz[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_CHG_L, tg->field_chg[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_CHG_H, tg->field_chg[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST2_L, tg->vact_st2[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST2_H, tg->vact_st2[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST3_L, tg->vact_st3[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST3_H, tg->vact_st3[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST4_L, tg->vact_st4[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VACT_ST4_H, tg->vact_st4[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_TOP_HDMI_L, tg->vsync_top_hdmi[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_TOP_HDMI_H, tg->vsync_top_hdmi[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_BOT_HDMI_L, tg->vsync_bot_hdmi[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_VSYNC_BOT_HDMI_H, tg->vsync_bot_hdmi[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_TOP_HDMI_L, tg->field_top_hdmi[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_TOP_HDMI_H, tg->field_top_hdmi[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_BOT_HDMI_L, tg->field_bot_hdmi[0]);
	hdmi_reg_writeb(hdata, HDMI_TG_FIELD_BOT_HDMI_H, tg->field_bot_hdmi[1]);
	hdmi_reg_writeb(hdata, HDMI_TG_3D, tg->tg_3d[0]);

	/* waiting for HDMIPHY's PLL to get to steady state */
	for (tries = 100; tries; --tries) {
		u32 val = hdmi_reg_read(hdata, HDMI_PHY_STATUS_0);
		if (val & HDMI_PHY_STATUS_READY)
			break;
		usleep_range(1000, 2000);
	}
	/* steady state not achieved */
	if (tries == 0) {
		DRM_ERROR("hdmiphy's pll could not reach steady state.\n");
		hdmi_regs_dump(hdata, "timing apply");
	}

	clk_disable(hdata->res.sclk_hdmi);
	clk_set_parent(hdata->res.sclk_hdmi, hdata->res.sclk_hdmiphy);
	clk_enable(hdata->res.sclk_hdmi);

	/* enable HDMI and timing generator */
	hdmi_reg_writemask(hdata, HDMI_CON_0, ~0, HDMI_EN);
	if (core->int_pro_mode[0])
		hdmi_reg_writemask(hdata, HDMI_TG_CMD, ~0, HDMI_TG_EN |
				HDMI_FIELD_EN);
	else
		hdmi_reg_writemask(hdata, HDMI_TG_CMD, ~0, HDMI_TG_EN);
}

static void hdmi_timing_apply(struct hdmi_context *hdata)
{
	if (hdata->type == HDMI_TYPE13)
		hdmi_v13_timing_apply(hdata);
	else
		hdmi_v14_timing_apply(hdata);
}

static void hdmiphy_conf_reset(struct hdmi_context *hdata)
{
	u8 buffer[2];
	u32 reg;

	clk_disable(hdata->res.sclk_hdmi);
	clk_set_parent(hdata->res.sclk_hdmi, hdata->res.sclk_pixel);
	clk_enable(hdata->res.sclk_hdmi);

	/* operation mode */
	buffer[0] = 0x1f;
	buffer[1] = 0x00;

	if (hdata->hdmiphy_port)
		i2c_master_send(hdata->hdmiphy_port, buffer, 2);

	if (hdata->type == HDMI_TYPE13)
		reg = HDMI_V13_PHY_RSTOUT;
	else
		reg = HDMI_PHY_RSTOUT;

	/* reset hdmiphy */
	hdmi_reg_writemask(hdata, reg, ~0, HDMI_PHY_SW_RSTOUT);
	usleep_range(10000, 12000);
	hdmi_reg_writemask(hdata, reg,  0, HDMI_PHY_SW_RSTOUT);
	usleep_range(10000, 12000);
}

static void hdmiphy_poweron(struct hdmi_context *hdata)
{
	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (hdata->type == HDMI_TYPE14)
		hdmi_reg_writemask(hdata, HDMI_PHY_CON_0, 0,
			HDMI_PHY_POWER_OFF_EN);
}

static void hdmiphy_poweroff(struct hdmi_context *hdata)
{
	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (hdata->type == HDMI_TYPE14)
		hdmi_reg_writemask(hdata, HDMI_PHY_CON_0, ~0,
			HDMI_PHY_POWER_OFF_EN);
}

static void hdmiphy_conf_apply(struct hdmi_context *hdata)
{
	const u8 *hdmiphy_data;
	u8 buffer[32];
	u8 operation[2];
	u8 read_buffer[32] = {0, };
	int ret;
	int i;

	if (!hdata->hdmiphy_port) {
		DRM_ERROR("hdmiphy is not attached\n");
		return;
	}

	/* pixel clock */
	i = hdmi_find_phy_conf(hdata, hdata->mode_conf.pixel_clock);
	if (i < 0) {
		DRM_ERROR("failed to find hdmiphy conf\n");
		return;
	}

	if (hdata->type == HDMI_TYPE13)
		hdmiphy_data = hdmiphy_v13_configs[i].conf;
	else
		hdmiphy_data = hdmiphy_v14_configs[i].conf;

	memcpy(buffer, hdmiphy_data, 32);
	ret = i2c_master_send(hdata->hdmiphy_port, buffer, 32);
	if (ret != 32) {
		DRM_ERROR("failed to configure HDMIPHY via I2C\n");
		return;
	}

	usleep_range(10000, 12000);

	/* operation mode */
	operation[0] = 0x1f;
	operation[1] = 0x80;

	ret = i2c_master_send(hdata->hdmiphy_port, operation, 2);
	if (ret != 2) {
		DRM_ERROR("failed to enable hdmiphy\n");
		return;
	}

	ret = i2c_master_recv(hdata->hdmiphy_port, read_buffer, 32);
	if (ret < 0) {
		DRM_ERROR("failed to read hdmiphy config\n");
		return;
	}

	for (i = 0; i < ret; i++)
		DRM_DEBUG_KMS("hdmiphy[0x%02x] write[0x%02x] - "
			"recv [0x%02x]\n", i, buffer[i], read_buffer[i]);
}

static void hdmi_conf_apply(struct hdmi_context *hdata)
{
	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	hdmiphy_conf_reset(hdata);
	hdmiphy_conf_apply(hdata);

	mutex_lock(&hdata->hdmi_mutex);
	hdmi_conf_reset(hdata);
	hdmi_conf_init(hdata);
	mutex_unlock(&hdata->hdmi_mutex);

	hdmi_audio_init(hdata);

	/* setting core registers */
	hdmi_timing_apply(hdata);
	hdmi_audio_control(hdata, true);

	hdmi_regs_dump(hdata, "start");
}

static void hdmi_set_reg(u8 *reg_pair, int num_bytes, u32 value)
{
	int i;
	BUG_ON(num_bytes > 4);
	for (i = 0; i < num_bytes; i++)
		reg_pair[i] = (value >> (8 * i)) & 0xff;
}

static void hdmi_v13_mode_set(struct hdmi_context *hdata,
			struct drm_display_mode *m)
{
	struct hdmi_v13_core_regs *core = &hdata->mode_conf.conf.v13_conf.core;
	struct hdmi_tg_regs *tg = &hdata->mode_conf.conf.v13_conf.tg;
	unsigned int val;

	hdata->mode_conf.cea_video_id =
		drm_match_cea_mode((struct drm_display_mode *)m);
	hdata->mode_conf.pixel_clock = m->clock * 1000;

	hdmi_set_reg(core->h_blank, 2, m->htotal - m->hdisplay);
	hdmi_set_reg(core->h_v_line, 3, (m->htotal << 12) | m->vtotal);

	val = (m->flags & DRM_MODE_FLAG_NVSYNC) ? 1 : 0;
	hdmi_set_reg(core->vsync_pol, 1, val);

	val = (m->flags & DRM_MODE_FLAG_INTERLACE) ? 1 : 0;
	hdmi_set_reg(core->int_pro_mode, 1, val);

	val = (m->hsync_start - m->hdisplay - 2);
	val |= ((m->hsync_end - m->hdisplay - 2) << 10);
	val |= ((m->flags & DRM_MODE_FLAG_NHSYNC)  ? 1 : 0)<<20;
	hdmi_set_reg(core->h_sync_gen, 3, val);

	/*
	 * Quirk requirement for exynos HDMI IP design,
	 * 2 pixels less than the actual calculation for hsync_start
	 * and end.
	 */

	/* Following values & calculations differ for different type of modes */
	if (m->flags & DRM_MODE_FLAG_INTERLACE) {
		/* Interlaced Mode */
		val = ((m->vsync_end - m->vdisplay) / 2);
		val |= ((m->vsync_start - m->vdisplay) / 2) << 12;
		hdmi_set_reg(core->v_sync_gen1, 3, val);

		val = m->vtotal / 2;
		val |= ((m->vtotal - m->vdisplay) / 2) << 11;
		hdmi_set_reg(core->v_blank, 3, val);

		val = (m->vtotal +
			((m->vsync_end - m->vsync_start) * 4) + 5) / 2;
		val |= m->vtotal << 11;
		hdmi_set_reg(core->v_blank_f, 3, val);

		val = ((m->vtotal / 2) + 7);
		val |= ((m->vtotal / 2) + 2) << 12;
		hdmi_set_reg(core->v_sync_gen2, 3, val);

		val = ((m->htotal / 2) + (m->hsync_start - m->hdisplay));
		val |= ((m->htotal / 2) +
			(m->hsync_start - m->hdisplay)) << 12;
		hdmi_set_reg(core->v_sync_gen3, 3, val);

		hdmi_set_reg(tg->vact_st, 2, (m->vtotal - m->vdisplay) / 2);
		hdmi_set_reg(tg->vact_sz, 2, m->vdisplay / 2);

		hdmi_set_reg(tg->vact_st2, 2, 0x249);/* Reset value + 1*/
	} else {
		/* Progressive Mode */

		val = m->vtotal;
		val |= (m->vtotal - m->vdisplay) << 11;
		hdmi_set_reg(core->v_blank, 3, val);

		hdmi_set_reg(core->v_blank_f, 3, 0);

		val = (m->vsync_end - m->vdisplay);
		val |= ((m->vsync_start - m->vdisplay) << 12);
		hdmi_set_reg(core->v_sync_gen1, 3, val);

		hdmi_set_reg(core->v_sync_gen2, 3, 0x1001);/* Reset value  */
		hdmi_set_reg(core->v_sync_gen3, 3, 0x1001);/* Reset value  */
		hdmi_set_reg(tg->vact_st, 2, m->vtotal - m->vdisplay);
		hdmi_set_reg(tg->vact_sz, 2, m->vdisplay);
		hdmi_set_reg(tg->vact_st2, 2, 0x248); /* Reset value */
	}

	/* Timing generator registers */
	hdmi_set_reg(tg->cmd, 1, 0x0);
	hdmi_set_reg(tg->h_fsz, 2, m->htotal);
	hdmi_set_reg(tg->hact_st, 2, m->htotal - m->hdisplay);
	hdmi_set_reg(tg->hact_sz, 2, m->hdisplay);
	hdmi_set_reg(tg->v_fsz, 2, m->vtotal);
	hdmi_set_reg(tg->vsync, 2, 0x1);
	hdmi_set_reg(tg->vsync2, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->field_chg, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->vsync_top_hdmi, 2, 0x1); /* Reset value */
	hdmi_set_reg(tg->vsync_bot_hdmi, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->field_top_hdmi, 2, 0x1); /* Reset value */
	hdmi_set_reg(tg->field_bot_hdmi, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->tg_3d, 1, 0x0); /* Not used */
}

static void hdmi_v14_mode_set(struct hdmi_context *hdata,
			struct drm_display_mode *m)
{
	struct hdmi_tg_regs *tg = &hdata->mode_conf.conf.v14_conf.tg;
	struct hdmi_v14_core_regs *core =
		&hdata->mode_conf.conf.v14_conf.core;

	hdata->mode_conf.cea_video_id =
		drm_match_cea_mode((struct drm_display_mode *)m);
	hdata->mode_conf.pixel_clock = m->clock * 1000;

	hdmi_set_reg(core->h_blank, 2, m->htotal - m->hdisplay);
	hdmi_set_reg(core->v_line, 2, m->vtotal);
	hdmi_set_reg(core->h_line, 2, m->htotal);
	hdmi_set_reg(core->hsync_pol, 1,
			(m->flags & DRM_MODE_FLAG_NHSYNC)  ? 1 : 0);
	hdmi_set_reg(core->vsync_pol, 1,
			(m->flags & DRM_MODE_FLAG_NVSYNC) ? 1 : 0);
	hdmi_set_reg(core->int_pro_mode, 1,
			(m->flags & DRM_MODE_FLAG_INTERLACE) ? 1 : 0);

	/*
	 * Quirk requirement for exynos 5 HDMI IP design,
	 * 2 pixels less than the actual calculation for hsync_start
	 * and end.
	 */

	/* Following values & calculations differ for different type of modes */
	if (m->flags & DRM_MODE_FLAG_INTERLACE) {
		/* Interlaced Mode */
		hdmi_set_reg(core->v_sync_line_bef_2, 2,
			(m->vsync_end - m->vdisplay) / 2);
		hdmi_set_reg(core->v_sync_line_bef_1, 2,
			(m->vsync_start - m->vdisplay) / 2);
		hdmi_set_reg(core->v2_blank, 2, m->vtotal / 2);
		hdmi_set_reg(core->v1_blank, 2, (m->vtotal - m->vdisplay) / 2);
		hdmi_set_reg(core->v_blank_f0, 2, (m->vtotal +
			((m->vsync_end - m->vsync_start) * 4) + 5) / 2);
		hdmi_set_reg(core->v_blank_f1, 2, m->vtotal);
		hdmi_set_reg(core->v_sync_line_aft_2, 2, (m->vtotal / 2) + 7);
		hdmi_set_reg(core->v_sync_line_aft_1, 2, (m->vtotal / 2) + 2);
		hdmi_set_reg(core->v_sync_line_aft_pxl_2, 2,
			(m->htotal / 2) + (m->hsync_start - m->hdisplay));
		hdmi_set_reg(core->v_sync_line_aft_pxl_1, 2,
			(m->htotal / 2) + (m->hsync_start - m->hdisplay));
		hdmi_set_reg(tg->vact_st, 2, (m->vtotal - m->vdisplay) / 2);
		hdmi_set_reg(tg->vact_sz, 2, m->vdisplay / 2);
		hdmi_set_reg(tg->vact_st2, 2, 0x249);/* Reset value + 1*/
		hdmi_set_reg(tg->vact_st3, 2, 0x0);
		hdmi_set_reg(tg->vact_st4, 2, 0x0);
	} else {
		/* Progressive Mode */
		hdmi_set_reg(core->v_sync_line_bef_2, 2,
			m->vsync_end - m->vdisplay);
		hdmi_set_reg(core->v_sync_line_bef_1, 2,
			m->vsync_start - m->vdisplay);
		hdmi_set_reg(core->v2_blank, 2, m->vtotal);
		hdmi_set_reg(core->v1_blank, 2, m->vtotal - m->vdisplay);
		hdmi_set_reg(core->v_blank_f0, 2, 0xffff);
		hdmi_set_reg(core->v_blank_f1, 2, 0xffff);
		hdmi_set_reg(core->v_sync_line_aft_2, 2, 0xffff);
		hdmi_set_reg(core->v_sync_line_aft_1, 2, 0xffff);
		hdmi_set_reg(core->v_sync_line_aft_pxl_2, 2, 0xffff);
		hdmi_set_reg(core->v_sync_line_aft_pxl_1, 2, 0xffff);
		hdmi_set_reg(tg->vact_st, 2, m->vtotal - m->vdisplay);
		hdmi_set_reg(tg->vact_sz, 2, m->vdisplay);
		hdmi_set_reg(tg->vact_st2, 2, 0x248); /* Reset value */
		hdmi_set_reg(tg->vact_st3, 2, 0x47b); /* Reset value */
		hdmi_set_reg(tg->vact_st4, 2, 0x6ae); /* Reset value */
	}

	/* Following values & calculations are same irrespective of mode type */
	hdmi_set_reg(core->h_sync_start, 2, m->hsync_start - m->hdisplay - 2);
	hdmi_set_reg(core->h_sync_end, 2, m->hsync_end - m->hdisplay - 2);
	hdmi_set_reg(core->vact_space_1, 2, 0xffff);
	hdmi_set_reg(core->vact_space_2, 2, 0xffff);
	hdmi_set_reg(core->vact_space_3, 2, 0xffff);
	hdmi_set_reg(core->vact_space_4, 2, 0xffff);
	hdmi_set_reg(core->vact_space_5, 2, 0xffff);
	hdmi_set_reg(core->vact_space_6, 2, 0xffff);
	hdmi_set_reg(core->v_blank_f2, 2, 0xffff);
	hdmi_set_reg(core->v_blank_f3, 2, 0xffff);
	hdmi_set_reg(core->v_blank_f4, 2, 0xffff);
	hdmi_set_reg(core->v_blank_f5, 2, 0xffff);
	hdmi_set_reg(core->v_sync_line_aft_3, 2, 0xffff);
	hdmi_set_reg(core->v_sync_line_aft_4, 2, 0xffff);
	hdmi_set_reg(core->v_sync_line_aft_5, 2, 0xffff);
	hdmi_set_reg(core->v_sync_line_aft_6, 2, 0xffff);
	hdmi_set_reg(core->v_sync_line_aft_pxl_3, 2, 0xffff);
	hdmi_set_reg(core->v_sync_line_aft_pxl_4, 2, 0xffff);
	hdmi_set_reg(core->v_sync_line_aft_pxl_5, 2, 0xffff);
	hdmi_set_reg(core->v_sync_line_aft_pxl_6, 2, 0xffff);

	/* Timing generator registers */
	hdmi_set_reg(tg->cmd, 1, 0x0);
	hdmi_set_reg(tg->h_fsz, 2, m->htotal);
	hdmi_set_reg(tg->hact_st, 2, m->htotal - m->hdisplay);
	hdmi_set_reg(tg->hact_sz, 2, m->hdisplay);
	hdmi_set_reg(tg->v_fsz, 2, m->vtotal);
	hdmi_set_reg(tg->vsync, 2, 0x1);
	hdmi_set_reg(tg->vsync2, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->field_chg, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->vsync_top_hdmi, 2, 0x1); /* Reset value */
	hdmi_set_reg(tg->vsync_bot_hdmi, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->field_top_hdmi, 2, 0x1); /* Reset value */
	hdmi_set_reg(tg->field_bot_hdmi, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->tg_3d, 1, 0x0);
}

static void hdmi_mode_set(void *ctx, void *mode)
{
	struct hdmi_context *hdata = ctx;
	struct drm_display_mode *m = mode;

	DRM_DEBUG_KMS("[%s]: xres=%d, yres=%d, refresh=%d, intl=%s\n",
		__func__, m->hdisplay, m->vdisplay,
		m->vrefresh, (m->flags & DRM_MODE_FLAG_INTERLACE) ?
		"INTERLACED" : "PROGERESSIVE");

	if (hdata->type == HDMI_TYPE13)
		hdmi_v13_mode_set(hdata, mode);
	else
		hdmi_v14_mode_set(hdata, mode);
}

static void hdmi_get_max_resol(void *ctx, unsigned int *width,
					unsigned int *height)
{
	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	*width = MAX_WIDTH;
	*height = MAX_HEIGHT;
}

static void hdmi_commit(void *ctx)
{
	struct hdmi_context *hdata = ctx;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	mutex_lock(&hdata->hdmi_mutex);
	if (!hdata->powered) {
		mutex_unlock(&hdata->hdmi_mutex);
		return;
	}
	mutex_unlock(&hdata->hdmi_mutex);

	hdmi_conf_apply(hdata);
}

static void hdmi_poweron(struct hdmi_context *hdata)
{
	struct hdmi_resources *res = &hdata->res;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	mutex_lock(&hdata->hdmi_mutex);
	if (hdata->powered) {
		mutex_unlock(&hdata->hdmi_mutex);
		return;
	}

	hdata->powered = true;

	mutex_unlock(&hdata->hdmi_mutex);

	regulator_bulk_enable(res->regul_count, res->regul_bulk);
	clk_enable(res->hdmiphy);
	clk_enable(res->hdmi);
	clk_enable(res->sclk_hdmi);

	hdmiphy_poweron(hdata);
}

static void hdmi_poweroff(struct hdmi_context *hdata)
{
	struct hdmi_resources *res = &hdata->res;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	mutex_lock(&hdata->hdmi_mutex);
	if (!hdata->powered)
		goto out;
	mutex_unlock(&hdata->hdmi_mutex);

	/*
	 * The TV power domain needs any condition of hdmiphy to turn off and
	 * its reset state seems to meet the condition.
	 */
	hdmiphy_conf_reset(hdata);
	hdmiphy_poweroff(hdata);

	clk_disable(res->sclk_hdmi);
	clk_disable(res->hdmi);
	clk_disable(res->hdmiphy);
	regulator_bulk_disable(res->regul_count, res->regul_bulk);

	mutex_lock(&hdata->hdmi_mutex);

	hdata->powered = false;

out:
	mutex_unlock(&hdata->hdmi_mutex);
}

static void hdmi_dpms(void *ctx, int mode)
{
	struct hdmi_context *hdata = ctx;

	DRM_DEBUG_KMS("[%d] %s mode %d\n", __LINE__, __func__, mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (pm_runtime_suspended(hdata->dev))
			pm_runtime_get_sync(hdata->dev);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (!pm_runtime_suspended(hdata->dev))
			pm_runtime_put_sync(hdata->dev);
		break;
	default:
		DRM_DEBUG_KMS("unknown dpms mode: %d\n", mode);
		break;
	}
}

static struct exynos_hdmi_ops hdmi_ops = {
	/* display */
	.is_connected	= hdmi_is_connected,
	.get_edid	= hdmi_get_edid,
	.check_timing	= hdmi_check_timing,

	/* manager */
	.mode_set	= hdmi_mode_set,
	.get_max_resol	= hdmi_get_max_resol,
	.commit		= hdmi_commit,
	.dpms		= hdmi_dpms,
};

static irqreturn_t hdmi_irq_thread(int irq, void *arg)
{
	struct exynos_drm_hdmi_context *ctx = arg;
	struct hdmi_context *hdata = ctx->ctx;

	mutex_lock(&hdata->hdmi_mutex);
	hdata->hpd = gpio_get_value(hdata->hpd_gpio);
	mutex_unlock(&hdata->hdmi_mutex);

	if (ctx->drm_dev)
		drm_helper_hpd_irq_event(ctx->drm_dev);

	return IRQ_HANDLED;
}

static int hdmi_resources_init(struct hdmi_context *hdata)
{
	struct device *dev = hdata->dev;
	struct hdmi_resources *res = &hdata->res;
	static char *supply[] = {
		"hdmi-en",
		"vdd",
		"vdd_osc",
		"vdd_pll",
	};
	int i, ret;

	DRM_DEBUG_KMS("HDMI resource init\n");

	memset(res, 0, sizeof(*res));

	/* get clocks, power */
	res->hdmi = devm_clk_get(dev, "hdmi");
	if (IS_ERR(res->hdmi)) {
		DRM_ERROR("failed to get clock 'hdmi'\n");
		goto fail;
	}
	res->sclk_hdmi = devm_clk_get(dev, "sclk_hdmi");
	if (IS_ERR(res->sclk_hdmi)) {
		DRM_ERROR("failed to get clock 'sclk_hdmi'\n");
		goto fail;
	}
	res->sclk_pixel = devm_clk_get(dev, "sclk_pixel");
	if (IS_ERR(res->sclk_pixel)) {
		DRM_ERROR("failed to get clock 'sclk_pixel'\n");
		goto fail;
	}
	res->sclk_hdmiphy = devm_clk_get(dev, "sclk_hdmiphy");
	if (IS_ERR(res->sclk_hdmiphy)) {
		DRM_ERROR("failed to get clock 'sclk_hdmiphy'\n");
		goto fail;
	}
	res->hdmiphy = devm_clk_get(dev, "hdmiphy");
	if (IS_ERR(res->hdmiphy)) {
		DRM_ERROR("failed to get clock 'hdmiphy'\n");
		goto fail;
	}

	clk_set_parent(res->sclk_hdmi, res->sclk_pixel);

	res->regul_bulk = devm_kzalloc(dev, ARRAY_SIZE(supply) *
		sizeof(res->regul_bulk[0]), GFP_KERNEL);
	if (!res->regul_bulk) {
		DRM_ERROR("failed to get memory for regulators\n");
		goto fail;
	}
	for (i = 0; i < ARRAY_SIZE(supply); ++i) {
		res->regul_bulk[i].supply = supply[i];
		res->regul_bulk[i].consumer = NULL;
	}
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(supply), res->regul_bulk);
	if (ret) {
		DRM_ERROR("failed to get regulators\n");
		goto fail;
	}
	res->regul_count = ARRAY_SIZE(supply);

	return 0;
fail:
	DRM_ERROR("HDMI resource init - failed\n");
	return -ENODEV;
}

static struct i2c_client *hdmi_ddc, *hdmi_hdmiphy;

void hdmi_attach_ddc_client(struct i2c_client *ddc)
{
	if (ddc)
		hdmi_ddc = ddc;
}

void hdmi_attach_hdmiphy_client(struct i2c_client *hdmiphy)
{
	if (hdmiphy)
		hdmi_hdmiphy = hdmiphy;
}

#ifdef CONFIG_OF
static struct s5p_hdmi_platform_data *drm_hdmi_dt_parse_pdata
					(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct s5p_hdmi_platform_data *pd;
	enum of_gpio_flags flags;
	u32 value;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		DRM_ERROR("memory allocation for pdata failed\n");
		goto err_data;
	}

	if (!of_find_property(np, "hpd-gpio", &value)) {
		DRM_ERROR("no hpd gpio property found\n");
		goto err_data;
	}

	pd->hpd_gpio = of_get_named_gpio_flags(np, "hpd-gpio", 0, &flags);

	return pd;

err_data:
	return NULL;
}
#else
static struct s5p_hdmi_platform_data *drm_hdmi_dt_parse_pdata
					(struct device *dev)
{
	return NULL;
}
#endif

static struct platform_device_id hdmi_driver_types[] = {
	{
		.name		= "s5pv210-hdmi",
		.driver_data    = HDMI_TYPE13,
	}, {
		.name		= "exynos4-hdmi",
		.driver_data    = HDMI_TYPE13,
	}, {
		.name		= "exynos4-hdmi14",
		.driver_data	= HDMI_TYPE14,
	}, {
		.name		= "exynos5-hdmi",
		.driver_data	= HDMI_TYPE14,
	}, {
		/* end node */
	}
};

#ifdef CONFIG_OF
static struct of_device_id hdmi_match_types[] = {
	{
		.compatible = "samsung,exynos5-hdmi",
		.data	= (void	*)HDMI_TYPE14,
	}, {
		/* end node */
	}
};
#endif

static int hdmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_drm_hdmi_context *drm_hdmi_ctx;
	struct hdmi_context *hdata;
	struct s5p_hdmi_platform_data *pdata;
	struct resource *res;
	int ret;

	DRM_DEBUG_KMS("[%d]\n", __LINE__);

	if (pdev->dev.of_node) {
		pdata = drm_hdmi_dt_parse_pdata(dev);
		if (IS_ERR(pdata)) {
			DRM_ERROR("failed to parse dt\n");
			return PTR_ERR(pdata);
		}
	} else {
		pdata = pdev->dev.platform_data;
	}

	if (!pdata) {
		DRM_ERROR("no platform data specified\n");
		return -EINVAL;
	}

	drm_hdmi_ctx = devm_kzalloc(&pdev->dev, sizeof(*drm_hdmi_ctx),
								GFP_KERNEL);
	if (!drm_hdmi_ctx) {
		DRM_ERROR("failed to allocate common hdmi context.\n");
		return -ENOMEM;
	}

	hdata = devm_kzalloc(&pdev->dev, sizeof(struct hdmi_context),
								GFP_KERNEL);
	if (!hdata) {
		DRM_ERROR("out of memory\n");
		return -ENOMEM;
	}

	mutex_init(&hdata->hdmi_mutex);

	drm_hdmi_ctx->ctx = (void *)hdata;
	hdata->parent_ctx = (void *)drm_hdmi_ctx;

	platform_set_drvdata(pdev, drm_hdmi_ctx);

	if (dev->of_node) {
		const struct of_device_id *match;
		match = of_match_node(of_match_ptr(hdmi_match_types),
					pdev->dev.of_node);
		if (match == NULL)
			return -ENODEV;
		hdata->type = (enum hdmi_type)match->data;
	} else {
		hdata->type = (enum hdmi_type)platform_get_device_id
					(pdev)->driver_data;
	}

	hdata->hpd_gpio = pdata->hpd_gpio;
	hdata->dev = dev;

	ret = hdmi_resources_init(hdata);

	if (ret) {
		DRM_ERROR("hdmi_resources_init failed\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		DRM_ERROR("failed to find registers\n");
		return -ENOENT;
	}

	hdata->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hdata->regs))
		return PTR_ERR(hdata->regs);

	ret = devm_gpio_request(&pdev->dev, hdata->hpd_gpio, "HPD");
	if (ret) {
		DRM_ERROR("failed to request HPD gpio\n");
		return ret;
	}

	/* DDC i2c driver */
	if (i2c_add_driver(&ddc_driver)) {
		DRM_ERROR("failed to register ddc i2c driver\n");
		return -ENOENT;
	}

	hdata->ddc_port = hdmi_ddc;

	/* hdmiphy i2c driver */
	if (i2c_add_driver(&hdmiphy_driver)) {
		DRM_ERROR("failed to register hdmiphy i2c driver\n");
		ret = -ENOENT;
		goto err_ddc;
	}

	hdata->hdmiphy_port = hdmi_hdmiphy;

	hdata->irq = gpio_to_irq(hdata->hpd_gpio);
	if (hdata->irq < 0) {
		DRM_ERROR("failed to get GPIO irq\n");
		ret = hdata->irq;
		goto err_hdmiphy;
	}

	hdata->hpd = gpio_get_value(hdata->hpd_gpio);

	ret = request_threaded_irq(hdata->irq, NULL,
			hdmi_irq_thread, IRQF_TRIGGER_RISING |
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"hdmi", drm_hdmi_ctx);
	if (ret) {
		DRM_ERROR("failed to register hdmi interrupt\n");
		goto err_hdmiphy;
	}

	/* Attach HDMI Driver to common hdmi. */
	exynos_hdmi_drv_attach(drm_hdmi_ctx);

	/* register specific callbacks to common hdmi. */
	exynos_hdmi_ops_register(&hdmi_ops);

	pm_runtime_enable(dev);

	return 0;

err_hdmiphy:
	i2c_del_driver(&hdmiphy_driver);
err_ddc:
	i2c_del_driver(&ddc_driver);
	return ret;
}

static int hdmi_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_drm_hdmi_context *ctx = platform_get_drvdata(pdev);
	struct hdmi_context *hdata = ctx->ctx;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	pm_runtime_disable(dev);

	free_irq(hdata->irq, hdata);


	/* hdmiphy i2c driver */
	i2c_del_driver(&hdmiphy_driver);
	/* DDC i2c driver */
	i2c_del_driver(&ddc_driver);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int hdmi_suspend(struct device *dev)
{
	struct exynos_drm_hdmi_context *ctx = get_hdmi_context(dev);
	struct hdmi_context *hdata = ctx->ctx;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	disable_irq(hdata->irq);

	hdata->hpd = false;
	if (ctx->drm_dev)
		drm_helper_hpd_irq_event(ctx->drm_dev);

	if (pm_runtime_suspended(dev)) {
		DRM_DEBUG_KMS("%s : Already suspended\n", __func__);
		return 0;
	}

	hdmi_poweroff(hdata);

	return 0;
}

static int hdmi_resume(struct device *dev)
{
	struct exynos_drm_hdmi_context *ctx = get_hdmi_context(dev);
	struct hdmi_context *hdata = ctx->ctx;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	hdata->hpd = gpio_get_value(hdata->hpd_gpio);

	enable_irq(hdata->irq);

	if (!pm_runtime_suspended(dev)) {
		DRM_DEBUG_KMS("%s : Already resumed\n", __func__);
		return 0;
	}

	hdmi_poweron(hdata);

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int hdmi_runtime_suspend(struct device *dev)
{
	struct exynos_drm_hdmi_context *ctx = get_hdmi_context(dev);
	struct hdmi_context *hdata = ctx->ctx;
	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	hdmi_poweroff(hdata);

	return 0;
}

static int hdmi_runtime_resume(struct device *dev)
{
	struct exynos_drm_hdmi_context *ctx = get_hdmi_context(dev);
	struct hdmi_context *hdata = ctx->ctx;
	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	hdmi_poweron(hdata);

	return 0;
}
#endif

static const struct dev_pm_ops hdmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(hdmi_suspend, hdmi_resume)
	SET_RUNTIME_PM_OPS(hdmi_runtime_suspend, hdmi_runtime_resume, NULL)
};

struct platform_driver hdmi_driver = {
	.probe		= hdmi_probe,
	.remove		= hdmi_remove,
	.id_table = hdmi_driver_types,
	.driver		= {
		.name	= "exynos-hdmi",
		.owner	= THIS_MODULE,
		.pm	= &hdmi_pm_ops,
		.of_match_table = of_match_ptr(hdmi_match_types),
	},
};
