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
#include <drm/drm_atomic_helper.h>

#include "regs-hdmi.h"

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/hdmi.h>
#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_mixer.h"

#include <linux/gpio.h>

#define ctx_from_connector(c)	container_of(c, struct hdmi_context, connector)

#define HOTPLUG_DEBOUNCE_MS		1100

/* AVI header and aspect ratio */
#define HDMI_AVI_VERSION		0x02
#define HDMI_AVI_LENGTH		0x0D

/* AUI header info */
#define HDMI_AUI_VERSION	0x01
#define HDMI_AUI_LENGTH	0x0A
#define AVI_SAME_AS_PIC_ASPECT_RATIO 0x8
#define AVI_4_3_CENTER_RATIO	0x9
#define AVI_16_9_CENTER_RATIO	0xa

enum hdmi_type {
	HDMI_TYPE13,
	HDMI_TYPE14,
};

struct hdmi_driver_data {
	unsigned int type;
	const struct hdmiphy_config *phy_confs;
	unsigned int phy_conf_count;
	unsigned int is_apb_phy:1;
};

struct hdmi_resources {
	struct clk			*hdmi;
	struct clk			*sclk_hdmi;
	struct clk			*sclk_pixel;
	struct clk			*sclk_hdmiphy;
	struct clk			*mout_hdmi;
	struct regulator_bulk_data	*regul_bulk;
	struct regulator		*reg_hdmi_en;
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
	enum hdmi_picture_aspect aspect_ratio;
	union {
		struct hdmi_v13_conf v13_conf;
		struct hdmi_v14_conf v14_conf;
	} conf;
};

struct hdmi_context {
	struct exynos_drm_display	display;
	struct device			*dev;
	struct drm_device		*drm_dev;
	struct drm_connector		connector;
	struct drm_encoder		*encoder;
	bool				hpd;
	bool				powered;
	bool				dvi_mode;
	struct mutex			hdmi_mutex;

	void __iomem			*regs;
	int				irq;
	struct delayed_work		hotplug_work;

	struct i2c_adapter		*ddc_adpt;
	struct i2c_client		*hdmiphy_port;

	/* current hdmiphy conf regs */
	struct drm_display_mode		current_mode;
	struct hdmi_conf_regs		mode_conf;

	struct hdmi_resources		res;

	int				hpd_gpio;
	void __iomem			*regs_hdmiphy;
	const struct hdmiphy_config		*phy_confs;
	unsigned int			phy_conf_count;

	struct regmap			*pmureg;
	enum hdmi_type			type;
};

static inline struct hdmi_context *display_to_hdmi(struct exynos_drm_display *d)
{
	return container_of(d, struct hdmi_context, display);
}

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
		.pixel_clock = 71000000,
		.conf = {
			0x01, 0xd1, 0x3b, 0x35, 0x40, 0x0c, 0x04, 0x08,
			0x85, 0xa0, 0x63, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xad, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 73250000,
		.conf = {
			0x01, 0xd1, 0x3d, 0x35, 0x40, 0x18, 0x02, 0x08,
			0x83, 0xa0, 0x6e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xa8, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
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
		.pixel_clock = 115500000,
		.conf = {
			0x01, 0xd1, 0x30, 0x12, 0x40, 0x40, 0x10, 0x08,
			0x80, 0x80, 0x21, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0xaa, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 119000000,
		.conf = {
			0x01, 0xd1, 0x32, 0x1a, 0x40, 0x30, 0xd8, 0x08,
			0x04, 0xa0, 0x2a, 0xd9, 0x45, 0xa0, 0xac, 0x80,
			0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
			0x54, 0x9d, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
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

static const struct hdmiphy_config hdmiphy_5420_configs[] = {
	{
		.pixel_clock = 25200000,
		.conf = {
			0x01, 0x52, 0x3F, 0x55, 0x40, 0x01, 0x00, 0xC8,
			0x82, 0xC8, 0xBD, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x06, 0x80, 0x01, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0xF4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 27000000,
		.conf = {
			0x01, 0xD1, 0x22, 0x51, 0x40, 0x08, 0xFC, 0xE0,
			0x98, 0xE8, 0xCB, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x06, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0xE4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 27027000,
		.conf = {
			0x01, 0xD1, 0x2D, 0x72, 0x40, 0x64, 0x12, 0xC8,
			0x43, 0xE8, 0x0E, 0xD9, 0x45, 0xA0, 0xAC, 0x80,
			0x26, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0xE3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 36000000,
		.conf = {
			0x01, 0x51, 0x2D, 0x55, 0x40, 0x40, 0x00, 0xC8,
			0x02, 0xC8, 0x0E, 0xD9, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0xAB, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 40000000,
		.conf = {
			0x01, 0xD1, 0x21, 0x31, 0x40, 0x3C, 0x28, 0xC8,
			0x87, 0xE8, 0xC8, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0x9A, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 65000000,
		.conf = {
			0x01, 0xD1, 0x36, 0x34, 0x40, 0x0C, 0x04, 0xC8,
			0x82, 0xE8, 0x45, 0xD9, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0xBD, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 71000000,
		.conf = {
			0x01, 0xD1, 0x3B, 0x35, 0x40, 0x0C, 0x04, 0xC8,
			0x85, 0xE8, 0x63, 0xD9, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0x57, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 73250000,
		.conf = {
			0x01, 0xD1, 0x1F, 0x10, 0x40, 0x78, 0x8D, 0xC8,
			0x81, 0xE8, 0xB7, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x56, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0xA8, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 74176000,
		.conf = {
			0x01, 0xD1, 0x1F, 0x10, 0x40, 0x5B, 0xEF, 0xC8,
			0x81, 0xE8, 0xB9, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x56, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0xA6, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 74250000,
		.conf = {
			0x01, 0xD1, 0x1F, 0x10, 0x40, 0x40, 0xF8, 0x08,
			0x81, 0xE8, 0xBA, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x26, 0x80, 0x09, 0x84, 0x05, 0x22, 0x24, 0x66,
			0x54, 0xA5, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 83500000,
		.conf = {
			0x01, 0xD1, 0x23, 0x11, 0x40, 0x0C, 0xFB, 0xC8,
			0x85, 0xE8, 0xD1, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0x4A, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 88750000,
		.conf = {
			0x01, 0xD1, 0x25, 0x11, 0x40, 0x18, 0xFF, 0xC8,
			0x83, 0xE8, 0xDE, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0x45, 0x24, 0x00, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 106500000,
		.conf = {
			0x01, 0xD1, 0x2C, 0x12, 0x40, 0x0C, 0x09, 0xC8,
			0x84, 0xE8, 0x0A, 0xD9, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0x73, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 108000000,
		.conf = {
			0x01, 0x51, 0x2D, 0x15, 0x40, 0x01, 0x00, 0xC8,
			0x82, 0xC8, 0x0E, 0xD9, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0xC7, 0x25, 0x03, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 115500000,
		.conf = {
			0x01, 0xD1, 0x30, 0x14, 0x40, 0x0C, 0x03, 0xC8,
			0x88, 0xE8, 0x21, 0xD9, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0x6A, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 146250000,
		.conf = {
			0x01, 0xD1, 0x3D, 0x15, 0x40, 0x18, 0xFD, 0xC8,
			0x83, 0xE8, 0x6E, 0xD9, 0x45, 0xA0, 0xAC, 0x80,
			0x08, 0x80, 0x09, 0x84, 0x05, 0x02, 0x24, 0x66,
			0x54, 0x54, 0x24, 0x01, 0x00, 0x00, 0x01, 0x80,
		},
	},
	{
		.pixel_clock = 148500000,
		.conf = {
			0x01, 0xD1, 0x1F, 0x00, 0x40, 0x40, 0xF8, 0x08,
			0x81, 0xE8, 0xBA, 0xD8, 0x45, 0xA0, 0xAC, 0x80,
			0x26, 0x80, 0x09, 0x84, 0x05, 0x22, 0x24, 0x66,
			0x54, 0x4B, 0x25, 0x03, 0x00, 0x80, 0x01, 0x80,
		},
	},
};

static struct hdmi_driver_data exynos5420_hdmi_driver_data = {
	.type		= HDMI_TYPE14,
	.phy_confs	= hdmiphy_5420_configs,
	.phy_conf_count	= ARRAY_SIZE(hdmiphy_5420_configs),
	.is_apb_phy	= 1,
};

static struct hdmi_driver_data exynos4212_hdmi_driver_data = {
	.type		= HDMI_TYPE14,
	.phy_confs	= hdmiphy_v14_configs,
	.phy_conf_count	= ARRAY_SIZE(hdmiphy_v14_configs),
	.is_apb_phy	= 0,
};

static struct hdmi_driver_data exynos4210_hdmi_driver_data = {
	.type		= HDMI_TYPE13,
	.phy_confs	= hdmiphy_v13_configs,
	.phy_conf_count	= ARRAY_SIZE(hdmiphy_v13_configs),
	.is_apb_phy	= 0,
};

static struct hdmi_driver_data exynos5_hdmi_driver_data = {
	.type		= HDMI_TYPE14,
	.phy_confs	= hdmiphy_v13_configs,
	.phy_conf_count	= ARRAY_SIZE(hdmiphy_v13_configs),
	.is_apb_phy	= 0,
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

static int hdmiphy_reg_writeb(struct hdmi_context *hdata,
			u32 reg_offset, u8 value)
{
	if (hdata->hdmiphy_port) {
		u8 buffer[2];
		int ret;

		buffer[0] = reg_offset;
		buffer[1] = value;

		ret = i2c_master_send(hdata->hdmiphy_port, buffer, 2);
		if (ret == 2)
			return 0;
		return ret;
	} else {
		writeb(value, hdata->regs_hdmiphy + (reg_offset<<2));
		return 0;
	}
}

static int hdmiphy_reg_write_buf(struct hdmi_context *hdata,
			u32 reg_offset, const u8 *buf, u32 len)
{
	if ((reg_offset + len) > 32)
		return -EINVAL;

	if (hdata->hdmiphy_port) {
		int ret;

		ret = i2c_master_send(hdata->hdmiphy_port, buf, len);
		if (ret == len)
			return 0;
		return ret;
	} else {
		int i;
		for (i = 0; i < len; i++)
			writeb(buf[i], hdata->regs_hdmiphy +
				((reg_offset + i)<<2));
		return 0;
	}
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
			union hdmi_infoframe *infoframe)
{
	u32 hdr_sum;
	u8 chksum;
	u32 mod;
	u32 vic;

	mod = hdmi_reg_read(hdata, HDMI_MODE_SEL);
	if (hdata->dvi_mode) {
		hdmi_reg_writeb(hdata, HDMI_VSI_CON,
				HDMI_VSI_CON_DO_NOT_TRANSMIT);
		hdmi_reg_writeb(hdata, HDMI_AVI_CON,
				HDMI_AVI_CON_DO_NOT_TRANSMIT);
		hdmi_reg_writeb(hdata, HDMI_AUI_CON, HDMI_AUI_CON_NO_TRAN);
		return;
	}

	switch (infoframe->any.type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		hdmi_reg_writeb(hdata, HDMI_AVI_CON, HDMI_AVI_CON_EVERY_VSYNC);
		hdmi_reg_writeb(hdata, HDMI_AVI_HEADER0, infoframe->any.type);
		hdmi_reg_writeb(hdata, HDMI_AVI_HEADER1,
				infoframe->any.version);
		hdmi_reg_writeb(hdata, HDMI_AVI_HEADER2, infoframe->any.length);
		hdr_sum = infoframe->any.type + infoframe->any.version +
			  infoframe->any.length;

		/* Output format zero hardcoded ,RGB YBCR selection */
		hdmi_reg_writeb(hdata, HDMI_AVI_BYTE(1), 0 << 5 |
			AVI_ACTIVE_FORMAT_VALID |
			AVI_UNDERSCANNED_DISPLAY_VALID);

		/*
		 * Set the aspect ratio as per the mode, mentioned in
		 * Table 9 AVI InfoFrame Data Byte 2 of CEA-861-D Standard
		 */
		switch (hdata->mode_conf.aspect_ratio) {
		case HDMI_PICTURE_ASPECT_4_3:
			hdmi_reg_writeb(hdata, HDMI_AVI_BYTE(2),
					hdata->mode_conf.aspect_ratio |
					AVI_4_3_CENTER_RATIO);
			break;
		case HDMI_PICTURE_ASPECT_16_9:
			hdmi_reg_writeb(hdata, HDMI_AVI_BYTE(2),
					hdata->mode_conf.aspect_ratio |
					AVI_16_9_CENTER_RATIO);
			break;
		case HDMI_PICTURE_ASPECT_NONE:
		default:
			hdmi_reg_writeb(hdata, HDMI_AVI_BYTE(2),
					hdata->mode_conf.aspect_ratio |
					AVI_SAME_AS_PIC_ASPECT_RATIO);
			break;
		}

		vic = hdata->mode_conf.cea_video_id;
		hdmi_reg_writeb(hdata, HDMI_AVI_BYTE(4), vic);

		chksum = hdmi_chksum(hdata, HDMI_AVI_BYTE(1),
					infoframe->any.length, hdr_sum);
		DRM_DEBUG_KMS("AVI checksum = 0x%x\n", chksum);
		hdmi_reg_writeb(hdata, HDMI_AVI_CHECK_SUM, chksum);
		break;
	case HDMI_INFOFRAME_TYPE_AUDIO:
		hdmi_reg_writeb(hdata, HDMI_AUI_CON, 0x02);
		hdmi_reg_writeb(hdata, HDMI_AUI_HEADER0, infoframe->any.type);
		hdmi_reg_writeb(hdata, HDMI_AUI_HEADER1,
				infoframe->any.version);
		hdmi_reg_writeb(hdata, HDMI_AUI_HEADER2, infoframe->any.length);
		hdr_sum = infoframe->any.type + infoframe->any.version +
			  infoframe->any.length;
		chksum = hdmi_chksum(hdata, HDMI_AUI_BYTE(1),
					infoframe->any.length, hdr_sum);
		DRM_DEBUG_KMS("AUI checksum = 0x%x\n", chksum);
		hdmi_reg_writeb(hdata, HDMI_AUI_CHECK_SUM, chksum);
		break;
	default:
		break;
	}
}

static enum drm_connector_status hdmi_detect(struct drm_connector *connector,
				bool force)
{
	struct hdmi_context *hdata = ctx_from_connector(connector);

	hdata->hpd = gpio_get_value(hdata->hpd_gpio);

	return hdata->hpd ? connector_status_connected :
			connector_status_disconnected;
}

static void hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs hdmi_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = hdmi_detect,
	.destroy = hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int hdmi_get_modes(struct drm_connector *connector)
{
	struct hdmi_context *hdata = ctx_from_connector(connector);
	struct edid *edid;
	int ret;

	if (!hdata->ddc_adpt)
		return -ENODEV;

	edid = drm_get_edid(connector, hdata->ddc_adpt);
	if (!edid)
		return -ENODEV;

	hdata->dvi_mode = !drm_detect_hdmi_monitor(edid);
	DRM_DEBUG_KMS("%s : width[%d] x height[%d]\n",
		(hdata->dvi_mode ? "dvi monitor" : "hdmi monitor"),
		edid->width_cm, edid->height_cm);

	drm_mode_connector_update_edid_property(connector, edid);

	ret = drm_add_edid_modes(connector, edid);

	kfree(edid);

	return ret;
}

static int hdmi_find_phy_conf(struct hdmi_context *hdata, u32 pixel_clock)
{
	int i;

	for (i = 0; i < hdata->phy_conf_count; i++)
		if (hdata->phy_confs[i].pixel_clock == pixel_clock)
			return i;

	DRM_DEBUG_KMS("Could not find phy config for %d\n", pixel_clock);
	return -EINVAL;
}

static int hdmi_mode_valid(struct drm_connector *connector,
			struct drm_display_mode *mode)
{
	struct hdmi_context *hdata = ctx_from_connector(connector);
	int ret;

	DRM_DEBUG_KMS("xres=%d, yres=%d, refresh=%d, intl=%d clock=%d\n",
		mode->hdisplay, mode->vdisplay, mode->vrefresh,
		(mode->flags & DRM_MODE_FLAG_INTERLACE) ? true :
		false, mode->clock * 1000);

	ret = mixer_check_mode(mode);
	if (ret)
		return MODE_BAD;

	ret = hdmi_find_phy_conf(hdata, mode->clock * 1000);
	if (ret < 0)
		return MODE_BAD;

	return MODE_OK;
}

static struct drm_encoder *hdmi_best_encoder(struct drm_connector *connector)
{
	struct hdmi_context *hdata = ctx_from_connector(connector);

	return hdata->encoder;
}

static struct drm_connector_helper_funcs hdmi_connector_helper_funcs = {
	.get_modes = hdmi_get_modes,
	.mode_valid = hdmi_mode_valid,
	.best_encoder = hdmi_best_encoder,
};

static int hdmi_create_connector(struct exynos_drm_display *display,
			struct drm_encoder *encoder)
{
	struct hdmi_context *hdata = display_to_hdmi(display);
	struct drm_connector *connector = &hdata->connector;
	int ret;

	hdata->encoder = encoder;
	connector->interlace_allowed = true;
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(hdata->drm_dev, connector,
			&hdmi_connector_funcs, DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &hdmi_connector_helper_funcs);
	drm_connector_register(connector);
	drm_mode_connector_attach_encoder(connector, encoder);

	return 0;
}

static void hdmi_mode_fixup(struct exynos_drm_display *display,
				struct drm_connector *connector,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_display_mode *m;
	int mode_ok;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	drm_mode_set_crtcinfo(adjusted_mode, 0);

	mode_ok = hdmi_mode_valid(connector, adjusted_mode);

	/* just return if user desired mode exists. */
	if (mode_ok == MODE_OK)
		return;

	/*
	 * otherwise, find the most suitable mode among modes and change it
	 * to adjusted_mode.
	 */
	list_for_each_entry(m, &connector->modes, head) {
		mode_ok = hdmi_mode_valid(connector, m);

		if (mode_ok == MODE_OK) {
			DRM_INFO("desired mode doesn't exist so\n");
			DRM_INFO("use the most suitable mode among modes.\n");

			DRM_DEBUG_KMS("Adjusted Mode: [%d]x[%d] [%d]Hz\n",
				m->hdisplay, m->vdisplay, m->vrefresh);

			drm_mode_copy(adjusted_mode, m);
			break;
		}
	}
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
	u32 sample_rate, bits_per_sample;
	u32 data_num, bit_ch, sample_frq;
	u32 val;
	u8 acr[7];

	sample_rate = 44100;
	bits_per_sample = 16;

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

static void hdmi_start(struct hdmi_context *hdata, bool start)
{
	u32 val = start ? HDMI_TG_EN : 0;

	if (hdata->current_mode.flags & DRM_MODE_FLAG_INTERLACE)
		val |= HDMI_FIELD_EN;

	hdmi_reg_writemask(hdata, HDMI_CON_0, val, HDMI_EN);
	hdmi_reg_writemask(hdata, HDMI_TG_CMD, val, HDMI_TG_EN | HDMI_FIELD_EN);
}

static void hdmi_conf_init(struct hdmi_context *hdata)
{
	union hdmi_infoframe infoframe;

	/* disable HPD interrupts from HDMI IP block, use GPIO instead */
	hdmi_reg_writemask(hdata, HDMI_INTC_CON, 0, HDMI_INTC_EN_GLOBAL |
		HDMI_INTC_EN_HPD_PLUG | HDMI_INTC_EN_HPD_UNPLUG);

	/* choose HDMI mode */
	hdmi_reg_writemask(hdata, HDMI_MODE_SEL,
		HDMI_MODE_HDMI_EN, HDMI_MODE_MASK);
	/* Apply Video preable and Guard band in HDMI mode only */
	hdmi_reg_writeb(hdata, HDMI_CON_2, 0);
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
		infoframe.any.type = HDMI_INFOFRAME_TYPE_AVI;
		infoframe.any.version = HDMI_AVI_VERSION;
		infoframe.any.length = HDMI_AVI_LENGTH;
		hdmi_reg_infoframe(hdata, &infoframe);

		infoframe.any.type = HDMI_INFOFRAME_TYPE_AUDIO;
		infoframe.any.version = HDMI_AUI_VERSION;
		infoframe.any.length = HDMI_AUI_LENGTH;
		hdmi_reg_infoframe(hdata, &infoframe);

		/* enable AVI packet every vsync, fixes purple line problem */
		hdmi_reg_writemask(hdata, HDMI_CON_1, 2, 3 << 5);
	}
}

static void hdmi_v13_mode_apply(struct hdmi_context *hdata)
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

	clk_disable_unprepare(hdata->res.sclk_hdmi);
	clk_set_parent(hdata->res.mout_hdmi, hdata->res.sclk_hdmiphy);
	clk_prepare_enable(hdata->res.sclk_hdmi);

	/* enable HDMI and timing generator */
	hdmi_start(hdata, true);
}

static void hdmi_v14_mode_apply(struct hdmi_context *hdata)
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

	clk_disable_unprepare(hdata->res.sclk_hdmi);
	clk_set_parent(hdata->res.mout_hdmi, hdata->res.sclk_hdmiphy);
	clk_prepare_enable(hdata->res.sclk_hdmi);

	/* enable HDMI and timing generator */
	hdmi_start(hdata, true);
}

static void hdmi_mode_apply(struct hdmi_context *hdata)
{
	if (hdata->type == HDMI_TYPE13)
		hdmi_v13_mode_apply(hdata);
	else
		hdmi_v14_mode_apply(hdata);
}

static void hdmiphy_conf_reset(struct hdmi_context *hdata)
{
	u32 reg;

	clk_disable_unprepare(hdata->res.sclk_hdmi);
	clk_set_parent(hdata->res.mout_hdmi, hdata->res.sclk_pixel);
	clk_prepare_enable(hdata->res.sclk_hdmi);

	/* operation mode */
	hdmiphy_reg_writeb(hdata, HDMIPHY_MODE_SET_DONE,
				HDMI_PHY_ENABLE_MODE_SET);

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
	if (hdata->type != HDMI_TYPE14)
		return;

	DRM_DEBUG_KMS("\n");

	/* For PHY Mode Setting */
	hdmiphy_reg_writeb(hdata, HDMIPHY_MODE_SET_DONE,
				HDMI_PHY_ENABLE_MODE_SET);
	/* Phy Power On */
	hdmiphy_reg_writeb(hdata, HDMIPHY_POWER,
				HDMI_PHY_POWER_ON);
	/* For PHY Mode Setting */
	hdmiphy_reg_writeb(hdata, HDMIPHY_MODE_SET_DONE,
				HDMI_PHY_DISABLE_MODE_SET);
	/* PHY SW Reset */
	hdmiphy_conf_reset(hdata);
}

static void hdmiphy_poweroff(struct hdmi_context *hdata)
{
	if (hdata->type != HDMI_TYPE14)
		return;

	DRM_DEBUG_KMS("\n");

	/* PHY SW Reset */
	hdmiphy_conf_reset(hdata);
	/* For PHY Mode Setting */
	hdmiphy_reg_writeb(hdata, HDMIPHY_MODE_SET_DONE,
				HDMI_PHY_ENABLE_MODE_SET);

	/* PHY Power Off */
	hdmiphy_reg_writeb(hdata, HDMIPHY_POWER,
				HDMI_PHY_POWER_OFF);

	/* For PHY Mode Setting */
	hdmiphy_reg_writeb(hdata, HDMIPHY_MODE_SET_DONE,
				HDMI_PHY_DISABLE_MODE_SET);
}

static void hdmiphy_conf_apply(struct hdmi_context *hdata)
{
	int ret;
	int i;

	/* pixel clock */
	i = hdmi_find_phy_conf(hdata, hdata->mode_conf.pixel_clock);
	if (i < 0) {
		DRM_ERROR("failed to find hdmiphy conf\n");
		return;
	}

	ret = hdmiphy_reg_write_buf(hdata, 0, hdata->phy_confs[i].conf, 32);
	if (ret) {
		DRM_ERROR("failed to configure hdmiphy\n");
		return;
	}

	usleep_range(10000, 12000);

	ret = hdmiphy_reg_writeb(hdata, HDMIPHY_MODE_SET_DONE,
				HDMI_PHY_DISABLE_MODE_SET);
	if (ret) {
		DRM_ERROR("failed to enable hdmiphy\n");
		return;
	}

}

static void hdmi_conf_apply(struct hdmi_context *hdata)
{
	hdmiphy_conf_reset(hdata);
	hdmiphy_conf_apply(hdata);

	mutex_lock(&hdata->hdmi_mutex);
	hdmi_start(hdata, false);
	hdmi_conf_init(hdata);
	mutex_unlock(&hdata->hdmi_mutex);

	hdmi_audio_init(hdata);

	/* setting core registers */
	hdmi_mode_apply(hdata);
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
	hdata->mode_conf.aspect_ratio = m->picture_aspect_ratio;

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
	hdata->mode_conf.aspect_ratio = m->picture_aspect_ratio;

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
		hdmi_set_reg(core->v_blank_f0, 2, m->vtotal - m->vdisplay / 2);
		hdmi_set_reg(core->v_blank_f1, 2, m->vtotal);
		hdmi_set_reg(core->v_sync_line_aft_2, 2, (m->vtotal / 2) + 7);
		hdmi_set_reg(core->v_sync_line_aft_1, 2, (m->vtotal / 2) + 2);
		hdmi_set_reg(core->v_sync_line_aft_pxl_2, 2,
			(m->htotal / 2) + (m->hsync_start - m->hdisplay));
		hdmi_set_reg(core->v_sync_line_aft_pxl_1, 2,
			(m->htotal / 2) + (m->hsync_start - m->hdisplay));
		hdmi_set_reg(tg->vact_st, 2, (m->vtotal - m->vdisplay) / 2);
		hdmi_set_reg(tg->vact_sz, 2, m->vdisplay / 2);
		hdmi_set_reg(tg->vact_st2, 2, m->vtotal - m->vdisplay / 2);
		hdmi_set_reg(tg->vsync2, 2, (m->vtotal / 2) + 1);
		hdmi_set_reg(tg->vsync_bot_hdmi, 2, (m->vtotal / 2) + 1);
		hdmi_set_reg(tg->field_bot_hdmi, 2, (m->vtotal / 2) + 1);
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
		hdmi_set_reg(tg->vsync2, 2, 0x233); /* Reset value */
		hdmi_set_reg(tg->vsync_bot_hdmi, 2, 0x233); /* Reset value */
		hdmi_set_reg(tg->field_bot_hdmi, 2, 0x233); /* Reset value */
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
	hdmi_set_reg(tg->field_chg, 2, 0x233); /* Reset value */
	hdmi_set_reg(tg->vsync_top_hdmi, 2, 0x1); /* Reset value */
	hdmi_set_reg(tg->field_top_hdmi, 2, 0x1); /* Reset value */
	hdmi_set_reg(tg->tg_3d, 1, 0x0);
}

static void hdmi_mode_set(struct exynos_drm_display *display,
			struct drm_display_mode *mode)
{
	struct hdmi_context *hdata = display_to_hdmi(display);
	struct drm_display_mode *m = mode;

	DRM_DEBUG_KMS("xres=%d, yres=%d, refresh=%d, intl=%s\n",
		m->hdisplay, m->vdisplay,
		m->vrefresh, (m->flags & DRM_MODE_FLAG_INTERLACE) ?
		"INTERLACED" : "PROGRESSIVE");

	/* preserve mode information for later use. */
	drm_mode_copy(&hdata->current_mode, mode);

	if (hdata->type == HDMI_TYPE13)
		hdmi_v13_mode_set(hdata, mode);
	else
		hdmi_v14_mode_set(hdata, mode);
}

static void hdmi_commit(struct exynos_drm_display *display)
{
	struct hdmi_context *hdata = display_to_hdmi(display);

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

	mutex_lock(&hdata->hdmi_mutex);
	if (hdata->powered) {
		mutex_unlock(&hdata->hdmi_mutex);
		return;
	}

	hdata->powered = true;

	mutex_unlock(&hdata->hdmi_mutex);

	pm_runtime_get_sync(hdata->dev);

	if (regulator_bulk_enable(res->regul_count, res->regul_bulk))
		DRM_DEBUG_KMS("failed to enable regulator bulk\n");

	/* set pmu hdmiphy control bit to enable hdmiphy */
	regmap_update_bits(hdata->pmureg, PMU_HDMI_PHY_CONTROL,
			PMU_HDMI_PHY_ENABLE_BIT, 1);

	clk_prepare_enable(res->hdmi);
	clk_prepare_enable(res->sclk_hdmi);

	hdmiphy_poweron(hdata);
	hdmi_commit(&hdata->display);
}

static void hdmi_poweroff(struct hdmi_context *hdata)
{
	struct hdmi_resources *res = &hdata->res;

	mutex_lock(&hdata->hdmi_mutex);
	if (!hdata->powered)
		goto out;
	mutex_unlock(&hdata->hdmi_mutex);

	/* HDMI System Disable */
	hdmi_reg_writemask(hdata, HDMI_CON_0, 0, HDMI_EN);

	hdmiphy_poweroff(hdata);

	cancel_delayed_work(&hdata->hotplug_work);

	clk_disable_unprepare(res->sclk_hdmi);
	clk_disable_unprepare(res->hdmi);

	/* reset pmu hdmiphy control bit to disable hdmiphy */
	regmap_update_bits(hdata->pmureg, PMU_HDMI_PHY_CONTROL,
			PMU_HDMI_PHY_ENABLE_BIT, 0);

	regulator_bulk_disable(res->regul_count, res->regul_bulk);

	pm_runtime_put_sync(hdata->dev);

	mutex_lock(&hdata->hdmi_mutex);
	hdata->powered = false;

out:
	mutex_unlock(&hdata->hdmi_mutex);
}

static void hdmi_dpms(struct exynos_drm_display *display, int mode)
{
	struct hdmi_context *hdata = display_to_hdmi(display);
	struct drm_encoder *encoder = hdata->encoder;
	struct drm_crtc *crtc = encoder->crtc;
	const struct drm_crtc_helper_funcs *funcs = NULL;

	DRM_DEBUG_KMS("mode %d\n", mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		hdmi_poweron(hdata);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		/*
		 * The SFRs of VP and Mixer are updated by Vertical Sync of
		 * Timing generator which is a part of HDMI so the sequence
		 * to disable TV Subsystem should be as following,
		 *	VP -> Mixer -> HDMI
		 *
		 * Below codes will try to disable Mixer and VP(if used)
		 * prior to disabling HDMI.
		 */
		if (crtc)
			funcs = crtc->helper_private;
		if (funcs && funcs->disable)
			(*funcs->disable)(crtc);

		hdmi_poweroff(hdata);
		break;
	default:
		DRM_DEBUG_KMS("unknown dpms mode: %d\n", mode);
		break;
	}
}

static struct exynos_drm_display_ops hdmi_display_ops = {
	.create_connector = hdmi_create_connector,
	.mode_fixup	= hdmi_mode_fixup,
	.mode_set	= hdmi_mode_set,
	.dpms		= hdmi_dpms,
	.commit		= hdmi_commit,
};

static void hdmi_hotplug_work_func(struct work_struct *work)
{
	struct hdmi_context *hdata;

	hdata = container_of(work, struct hdmi_context, hotplug_work.work);

	mutex_lock(&hdata->hdmi_mutex);
	hdata->hpd = gpio_get_value(hdata->hpd_gpio);
	mutex_unlock(&hdata->hdmi_mutex);

	if (hdata->drm_dev)
		drm_helper_hpd_irq_event(hdata->drm_dev);
}

static irqreturn_t hdmi_irq_thread(int irq, void *arg)
{
	struct hdmi_context *hdata = arg;

	mod_delayed_work(system_wq, &hdata->hotplug_work,
			msecs_to_jiffies(HOTPLUG_DEBOUNCE_MS));

	return IRQ_HANDLED;
}

static int hdmi_resources_init(struct hdmi_context *hdata)
{
	struct device *dev = hdata->dev;
	struct hdmi_resources *res = &hdata->res;
	static char *supply[] = {
		"vdd",
		"vdd_osc",
		"vdd_pll",
	};
	int i, ret;

	DRM_DEBUG_KMS("HDMI resource init\n");

	/* get clocks, power */
	res->hdmi = devm_clk_get(dev, "hdmi");
	if (IS_ERR(res->hdmi)) {
		DRM_ERROR("failed to get clock 'hdmi'\n");
		ret = PTR_ERR(res->hdmi);
		goto fail;
	}
	res->sclk_hdmi = devm_clk_get(dev, "sclk_hdmi");
	if (IS_ERR(res->sclk_hdmi)) {
		DRM_ERROR("failed to get clock 'sclk_hdmi'\n");
		ret = PTR_ERR(res->sclk_hdmi);
		goto fail;
	}
	res->sclk_pixel = devm_clk_get(dev, "sclk_pixel");
	if (IS_ERR(res->sclk_pixel)) {
		DRM_ERROR("failed to get clock 'sclk_pixel'\n");
		ret = PTR_ERR(res->sclk_pixel);
		goto fail;
	}
	res->sclk_hdmiphy = devm_clk_get(dev, "sclk_hdmiphy");
	if (IS_ERR(res->sclk_hdmiphy)) {
		DRM_ERROR("failed to get clock 'sclk_hdmiphy'\n");
		ret = PTR_ERR(res->sclk_hdmiphy);
		goto fail;
	}
	res->mout_hdmi = devm_clk_get(dev, "mout_hdmi");
	if (IS_ERR(res->mout_hdmi)) {
		DRM_ERROR("failed to get clock 'mout_hdmi'\n");
		ret = PTR_ERR(res->mout_hdmi);
		goto fail;
	}

	clk_set_parent(res->mout_hdmi, res->sclk_pixel);

	res->regul_bulk = devm_kzalloc(dev, ARRAY_SIZE(supply) *
		sizeof(res->regul_bulk[0]), GFP_KERNEL);
	if (!res->regul_bulk) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < ARRAY_SIZE(supply); ++i) {
		res->regul_bulk[i].supply = supply[i];
		res->regul_bulk[i].consumer = NULL;
	}
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(supply), res->regul_bulk);
	if (ret) {
		DRM_ERROR("failed to get regulators\n");
		return ret;
	}
	res->regul_count = ARRAY_SIZE(supply);

	res->reg_hdmi_en = devm_regulator_get(dev, "hdmi-en");
	if (IS_ERR(res->reg_hdmi_en) && PTR_ERR(res->reg_hdmi_en) != -ENOENT) {
		DRM_ERROR("failed to get hdmi-en regulator\n");
		return PTR_ERR(res->reg_hdmi_en);
	}
	if (!IS_ERR(res->reg_hdmi_en)) {
		ret = regulator_enable(res->reg_hdmi_en);
		if (ret) {
			DRM_ERROR("failed to enable hdmi-en regulator\n");
			return ret;
		}
	} else
		res->reg_hdmi_en = NULL;

	return ret;
fail:
	DRM_ERROR("HDMI resource init - failed\n");
	return ret;
}

static struct of_device_id hdmi_match_types[] = {
	{
		.compatible = "samsung,exynos5-hdmi",
		.data = &exynos5_hdmi_driver_data,
	}, {
		.compatible = "samsung,exynos4210-hdmi",
		.data = &exynos4210_hdmi_driver_data,
	}, {
		.compatible = "samsung,exynos4212-hdmi",
		.data = &exynos4212_hdmi_driver_data,
	}, {
		.compatible = "samsung,exynos5420-hdmi",
		.data = &exynos5420_hdmi_driver_data,
	}, {
		/* end node */
	}
};
MODULE_DEVICE_TABLE (of, hdmi_match_types);

static int hdmi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct hdmi_context *hdata = dev_get_drvdata(dev);

	hdata->drm_dev = drm_dev;

	return exynos_drm_create_enc_conn(drm_dev, &hdata->display);
}

static void hdmi_unbind(struct device *dev, struct device *master, void *data)
{
}

static const struct component_ops hdmi_component_ops = {
	.bind	= hdmi_bind,
	.unbind = hdmi_unbind,
};

static struct device_node *hdmi_legacy_ddc_dt_binding(struct device *dev)
{
	const char *compatible_str = "samsung,exynos4210-hdmiddc";
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, compatible_str);
	if (np)
		return of_get_next_parent(np);

	return NULL;
}

static struct device_node *hdmi_legacy_phy_dt_binding(struct device *dev)
{
	const char *compatible_str = "samsung,exynos4212-hdmiphy";

	return of_find_compatible_node(NULL, NULL, compatible_str);
}

static int hdmi_probe(struct platform_device *pdev)
{
	struct device_node *ddc_node, *phy_node;
	struct hdmi_driver_data *drv_data;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct hdmi_context *hdata;
	struct resource *res;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	hdata = devm_kzalloc(dev, sizeof(struct hdmi_context), GFP_KERNEL);
	if (!hdata)
		return -ENOMEM;

	hdata->display.type = EXYNOS_DISPLAY_TYPE_HDMI;
	hdata->display.ops = &hdmi_display_ops;

	mutex_init(&hdata->hdmi_mutex);

	platform_set_drvdata(pdev, hdata);

	match = of_match_node(hdmi_match_types, dev->of_node);
	if (!match)
		return -ENODEV;

	drv_data = (struct hdmi_driver_data *)match->data;
	hdata->type = drv_data->type;
	hdata->phy_confs = drv_data->phy_confs;
	hdata->phy_conf_count = drv_data->phy_conf_count;

	hdata->dev = dev;
	hdata->hpd_gpio = of_get_named_gpio(dev->of_node, "hpd-gpio", 0);
	if (hdata->hpd_gpio < 0) {
		DRM_ERROR("cannot get hpd gpio property\n");
		return hdata->hpd_gpio;
	}

	ret = hdmi_resources_init(hdata);
	if (ret) {
		DRM_ERROR("hdmi_resources_init failed\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdata->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdata->regs)) {
		ret = PTR_ERR(hdata->regs);
		return ret;
	}

	ret = devm_gpio_request(dev, hdata->hpd_gpio, "HPD");
	if (ret) {
		DRM_ERROR("failed to request HPD gpio\n");
		return ret;
	}

	ddc_node = hdmi_legacy_ddc_dt_binding(dev);
	if (ddc_node)
		goto out_get_ddc_adpt;

	/* DDC i2c driver */
	ddc_node = of_parse_phandle(dev->of_node, "ddc", 0);
	if (!ddc_node) {
		DRM_ERROR("Failed to find ddc node in device tree\n");
		return -ENODEV;
	}

out_get_ddc_adpt:
	hdata->ddc_adpt = of_find_i2c_adapter_by_node(ddc_node);
	if (!hdata->ddc_adpt) {
		DRM_ERROR("Failed to get ddc i2c adapter by node\n");
		return -EPROBE_DEFER;
	}

	phy_node = hdmi_legacy_phy_dt_binding(dev);
	if (phy_node)
		goto out_get_phy_port;

	/* hdmiphy i2c driver */
	phy_node = of_parse_phandle(dev->of_node, "phy", 0);
	if (!phy_node) {
		DRM_ERROR("Failed to find hdmiphy node in device tree\n");
		ret = -ENODEV;
		goto err_ddc;
	}

out_get_phy_port:
	if (drv_data->is_apb_phy) {
		hdata->regs_hdmiphy = of_iomap(phy_node, 0);
		if (!hdata->regs_hdmiphy) {
			DRM_ERROR("failed to ioremap hdmi phy\n");
			ret = -ENOMEM;
			goto err_ddc;
		}
	} else {
		hdata->hdmiphy_port = of_find_i2c_device_by_node(phy_node);
		if (!hdata->hdmiphy_port) {
			DRM_ERROR("Failed to get hdmi phy i2c client\n");
			ret = -EPROBE_DEFER;
			goto err_ddc;
		}
	}

	hdata->irq = gpio_to_irq(hdata->hpd_gpio);
	if (hdata->irq < 0) {
		DRM_ERROR("failed to get GPIO irq\n");
		ret = hdata->irq;
		goto err_hdmiphy;
	}

	hdata->hpd = gpio_get_value(hdata->hpd_gpio);

	INIT_DELAYED_WORK(&hdata->hotplug_work, hdmi_hotplug_work_func);

	ret = devm_request_threaded_irq(dev, hdata->irq, NULL,
			hdmi_irq_thread, IRQF_TRIGGER_RISING |
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"hdmi", hdata);
	if (ret) {
		DRM_ERROR("failed to register hdmi interrupt\n");
		goto err_hdmiphy;
	}

	hdata->pmureg = syscon_regmap_lookup_by_phandle(dev->of_node,
			"samsung,syscon-phandle");
	if (IS_ERR(hdata->pmureg)) {
		DRM_ERROR("syscon regmap lookup failed.\n");
		ret = -EPROBE_DEFER;
		goto err_hdmiphy;
	}

	pm_runtime_enable(dev);

	ret = component_add(&pdev->dev, &hdmi_component_ops);
	if (ret)
		goto err_disable_pm_runtime;

	return ret;

err_disable_pm_runtime:
	pm_runtime_disable(dev);

err_hdmiphy:
	if (hdata->hdmiphy_port)
		put_device(&hdata->hdmiphy_port->dev);
err_ddc:
	put_device(&hdata->ddc_adpt->dev);

	return ret;
}

static int hdmi_remove(struct platform_device *pdev)
{
	struct hdmi_context *hdata = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&hdata->hotplug_work);

	if (hdata->res.reg_hdmi_en)
		regulator_disable(hdata->res.reg_hdmi_en);

	if (hdata->hdmiphy_port)
		put_device(&hdata->hdmiphy_port->dev);
	put_device(&hdata->ddc_adpt->dev);

	pm_runtime_disable(&pdev->dev);
	component_del(&pdev->dev, &hdmi_component_ops);

	return 0;
}

struct platform_driver hdmi_driver = {
	.probe		= hdmi_probe,
	.remove		= hdmi_remove,
	.driver		= {
		.name	= "exynos-hdmi",
		.owner	= THIS_MODULE,
		.of_match_table = hdmi_match_types,
	},
};
