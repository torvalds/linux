/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <mach/clk.h>

#include "hdmi.h"
#include "drm.h"
#include "dc.h"

struct tegra_hdmi {
	struct host1x_client client;
	struct tegra_output output;
	struct device *dev;

	struct regulator *vdd;
	struct regulator *pll;

	void __iomem *regs;
	unsigned int irq;

	struct clk *clk_parent;
	struct clk *clk;

	unsigned int audio_source;
	unsigned int audio_freq;
	bool stereo;
	bool dvi;

	struct drm_info_list *debugfs_files;
	struct drm_minor *minor;
	struct dentry *debugfs;
};

static inline struct tegra_hdmi *
host1x_client_to_hdmi(struct host1x_client *client)
{
	return container_of(client, struct tegra_hdmi, client);
}

static inline struct tegra_hdmi *to_hdmi(struct tegra_output *output)
{
	return container_of(output, struct tegra_hdmi, output);
}

#define HDMI_AUDIOCLK_FREQ 216000000
#define HDMI_REKEY_DEFAULT 56

enum {
	AUTO = 0,
	SPDIF,
	HDA,
};

static inline unsigned long tegra_hdmi_readl(struct tegra_hdmi *hdmi,
					     unsigned long reg)
{
	return readl(hdmi->regs + (reg << 2));
}

static inline void tegra_hdmi_writel(struct tegra_hdmi *hdmi, unsigned long val,
				     unsigned long reg)
{
	writel(val, hdmi->regs + (reg << 2));
}

struct tegra_hdmi_audio_config {
	unsigned int pclk;
	unsigned int n;
	unsigned int cts;
	unsigned int aval;
};

static const struct tegra_hdmi_audio_config tegra_hdmi_audio_32k[] = {
	{  25200000, 4096,  25200, 24000 },
	{  27000000, 4096,  27000, 24000 },
	{  74250000, 4096,  74250, 24000 },
	{ 148500000, 4096, 148500, 24000 },
	{         0,    0,      0,     0 },
};

static const struct tegra_hdmi_audio_config tegra_hdmi_audio_44_1k[] = {
	{  25200000, 5880,  26250, 25000 },
	{  27000000, 5880,  28125, 25000 },
	{  74250000, 4704,  61875, 20000 },
	{ 148500000, 4704, 123750, 20000 },
	{         0,    0,      0,     0 },
};

static const struct tegra_hdmi_audio_config tegra_hdmi_audio_48k[] = {
	{  25200000, 6144,  25200, 24000 },
	{  27000000, 6144,  27000, 24000 },
	{  74250000, 6144,  74250, 24000 },
	{ 148500000, 6144, 148500, 24000 },
	{         0,    0,      0,     0 },
};

static const struct tegra_hdmi_audio_config tegra_hdmi_audio_88_2k[] = {
	{  25200000, 11760,  26250, 25000 },
	{  27000000, 11760,  28125, 25000 },
	{  74250000,  9408,  61875, 20000 },
	{ 148500000,  9408, 123750, 20000 },
	{         0,     0,      0,     0 },
};

static const struct tegra_hdmi_audio_config tegra_hdmi_audio_96k[] = {
	{  25200000, 12288,  25200, 24000 },
	{  27000000, 12288,  27000, 24000 },
	{  74250000, 12288,  74250, 24000 },
	{ 148500000, 12288, 148500, 24000 },
	{         0,     0,      0,     0 },
};

static const struct tegra_hdmi_audio_config tegra_hdmi_audio_176_4k[] = {
	{  25200000, 23520,  26250, 25000 },
	{  27000000, 23520,  28125, 25000 },
	{  74250000, 18816,  61875, 20000 },
	{ 148500000, 18816, 123750, 20000 },
	{         0,     0,      0,     0 },
};

static const struct tegra_hdmi_audio_config tegra_hdmi_audio_192k[] = {
	{  25200000, 24576,  25200, 24000 },
	{  27000000, 24576,  27000, 24000 },
	{  74250000, 24576,  74250, 24000 },
	{ 148500000, 24576, 148500, 24000 },
	{         0,     0,      0,     0 },
};

struct tmds_config {
	unsigned int pclk;
	u32 pll0;
	u32 pll1;
	u32 pe_current;
	u32 drive_current;
};

static const struct tmds_config tegra2_tmds_config[] = {
	{ /* slow pixel clock modes */
		.pclk = 27000000,
		.pll0 = SOR_PLL_BG_V17_S(3) | SOR_PLL_ICHPMP(1) |
			SOR_PLL_RESISTORSEL | SOR_PLL_VCOCAP(0) |
			SOR_PLL_TX_REG_LOAD(3),
		.pll1 = SOR_PLL_TMDS_TERM_ENABLE,
		.pe_current = PE_CURRENT0(PE_CURRENT_0_0_mA) |
			PE_CURRENT1(PE_CURRENT_0_0_mA) |
			PE_CURRENT2(PE_CURRENT_0_0_mA) |
			PE_CURRENT3(PE_CURRENT_0_0_mA),
		.drive_current = DRIVE_CURRENT_LANE0(DRIVE_CURRENT_7_125_mA) |
			DRIVE_CURRENT_LANE1(DRIVE_CURRENT_7_125_mA) |
			DRIVE_CURRENT_LANE2(DRIVE_CURRENT_7_125_mA) |
			DRIVE_CURRENT_LANE3(DRIVE_CURRENT_7_125_mA),
	},
	{ /* high pixel clock modes */
		.pclk = UINT_MAX,
		.pll0 = SOR_PLL_BG_V17_S(3) | SOR_PLL_ICHPMP(1) |
			SOR_PLL_RESISTORSEL | SOR_PLL_VCOCAP(1) |
			SOR_PLL_TX_REG_LOAD(3),
		.pll1 = SOR_PLL_TMDS_TERM_ENABLE | SOR_PLL_PE_EN,
		.pe_current = PE_CURRENT0(PE_CURRENT_6_0_mA) |
			PE_CURRENT1(PE_CURRENT_6_0_mA) |
			PE_CURRENT2(PE_CURRENT_6_0_mA) |
			PE_CURRENT3(PE_CURRENT_6_0_mA),
		.drive_current = DRIVE_CURRENT_LANE0(DRIVE_CURRENT_7_125_mA) |
			DRIVE_CURRENT_LANE1(DRIVE_CURRENT_7_125_mA) |
			DRIVE_CURRENT_LANE2(DRIVE_CURRENT_7_125_mA) |
			DRIVE_CURRENT_LANE3(DRIVE_CURRENT_7_125_mA),
	},
};

static const struct tmds_config tegra3_tmds_config[] = {
	{ /* 480p modes */
		.pclk = 27000000,
		.pll0 = SOR_PLL_BG_V17_S(3) | SOR_PLL_ICHPMP(1) |
			SOR_PLL_RESISTORSEL | SOR_PLL_VCOCAP(0) |
			SOR_PLL_TX_REG_LOAD(0),
		.pll1 = SOR_PLL_TMDS_TERM_ENABLE,
		.pe_current = PE_CURRENT0(PE_CURRENT_0_0_mA) |
			PE_CURRENT1(PE_CURRENT_0_0_mA) |
			PE_CURRENT2(PE_CURRENT_0_0_mA) |
			PE_CURRENT3(PE_CURRENT_0_0_mA),
		.drive_current = DRIVE_CURRENT_LANE0(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE1(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE2(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE3(DRIVE_CURRENT_5_250_mA),
	}, { /* 720p modes */
		.pclk = 74250000,
		.pll0 = SOR_PLL_BG_V17_S(3) | SOR_PLL_ICHPMP(1) |
			SOR_PLL_RESISTORSEL | SOR_PLL_VCOCAP(1) |
			SOR_PLL_TX_REG_LOAD(0),
		.pll1 = SOR_PLL_TMDS_TERM_ENABLE | SOR_PLL_PE_EN,
		.pe_current = PE_CURRENT0(PE_CURRENT_5_0_mA) |
			PE_CURRENT1(PE_CURRENT_5_0_mA) |
			PE_CURRENT2(PE_CURRENT_5_0_mA) |
			PE_CURRENT3(PE_CURRENT_5_0_mA),
		.drive_current = DRIVE_CURRENT_LANE0(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE1(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE2(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE3(DRIVE_CURRENT_5_250_mA),
	}, { /* 1080p modes */
		.pclk = UINT_MAX,
		.pll0 = SOR_PLL_BG_V17_S(3) | SOR_PLL_ICHPMP(1) |
			SOR_PLL_RESISTORSEL | SOR_PLL_VCOCAP(3) |
			SOR_PLL_TX_REG_LOAD(0),
		.pll1 = SOR_PLL_TMDS_TERM_ENABLE | SOR_PLL_PE_EN,
		.pe_current = PE_CURRENT0(PE_CURRENT_5_0_mA) |
			PE_CURRENT1(PE_CURRENT_5_0_mA) |
			PE_CURRENT2(PE_CURRENT_5_0_mA) |
			PE_CURRENT3(PE_CURRENT_5_0_mA),
		.drive_current = DRIVE_CURRENT_LANE0(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE1(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE2(DRIVE_CURRENT_5_250_mA) |
			DRIVE_CURRENT_LANE3(DRIVE_CURRENT_5_250_mA),
	},
};

static const struct tegra_hdmi_audio_config *
tegra_hdmi_get_audio_config(unsigned int audio_freq, unsigned int pclk)
{
	const struct tegra_hdmi_audio_config *table;

	switch (audio_freq) {
	case 32000:
		table = tegra_hdmi_audio_32k;
		break;

	case 44100:
		table = tegra_hdmi_audio_44_1k;
		break;

	case 48000:
		table = tegra_hdmi_audio_48k;
		break;

	case 88200:
		table = tegra_hdmi_audio_88_2k;
		break;

	case 96000:
		table = tegra_hdmi_audio_96k;
		break;

	case 176400:
		table = tegra_hdmi_audio_176_4k;
		break;

	case 192000:
		table = tegra_hdmi_audio_192k;
		break;

	default:
		return NULL;
	}

	while (table->pclk) {
		if (table->pclk == pclk)
			return table;

		table++;
	}

	return NULL;
}

static void tegra_hdmi_setup_audio_fs_tables(struct tegra_hdmi *hdmi)
{
	const unsigned int freqs[] = {
		32000, 44100, 48000, 88200, 96000, 176400, 192000
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(freqs); i++) {
		unsigned int f = freqs[i];
		unsigned int eight_half;
		unsigned long value;
		unsigned int delta;

		if (f > 96000)
			delta = 2;
		else if (f > 480000)
			delta = 6;
		else
			delta = 9;

		eight_half = (8 * HDMI_AUDIOCLK_FREQ) / (f * 128);
		value = AUDIO_FS_LOW(eight_half - delta) |
			AUDIO_FS_HIGH(eight_half + delta);
		tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_AUDIO_FS(i));
	}
}

static int tegra_hdmi_setup_audio(struct tegra_hdmi *hdmi, unsigned int pclk)
{
	struct device_node *node = hdmi->dev->of_node;
	const struct tegra_hdmi_audio_config *config;
	unsigned int offset = 0;
	unsigned long value;

	switch (hdmi->audio_source) {
	case HDA:
		value = AUDIO_CNTRL0_SOURCE_SELECT_HDAL;
		break;

	case SPDIF:
		value = AUDIO_CNTRL0_SOURCE_SELECT_SPDIF;
		break;

	default:
		value = AUDIO_CNTRL0_SOURCE_SELECT_AUTO;
		break;
	}

	if (of_device_is_compatible(node, "nvidia,tegra30-hdmi")) {
		value |= AUDIO_CNTRL0_ERROR_TOLERANCE(6) |
			 AUDIO_CNTRL0_FRAMES_PER_BLOCK(0xc0);
		tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_AUDIO_CNTRL0);
	} else {
		value |= AUDIO_CNTRL0_INJECT_NULLSMPL;
		tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_AUDIO_CNTRL0);

		value = AUDIO_CNTRL0_ERROR_TOLERANCE(6) |
			AUDIO_CNTRL0_FRAMES_PER_BLOCK(0xc0);
		tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_AUDIO_CNTRL0);
	}

	config = tegra_hdmi_get_audio_config(hdmi->audio_freq, pclk);
	if (!config) {
		dev_err(hdmi->dev, "cannot set audio to %u at %u pclk\n",
			hdmi->audio_freq, pclk);
		return -EINVAL;
	}

	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_HDMI_ACR_CTRL);

	value = AUDIO_N_RESETF | AUDIO_N_GENERATE_ALTERNATE |
		AUDIO_N_VALUE(config->n - 1);
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_AUDIO_N);

	tegra_hdmi_writel(hdmi, ACR_SUBPACK_N(config->n) | ACR_ENABLE,
			  HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_HIGH);

	value = ACR_SUBPACK_CTS(config->cts);
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW);

	value = SPARE_HW_CTS | SPARE_FORCE_SW_CTS | SPARE_CTS_RESET_VAL(1);
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_SPARE);

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_AUDIO_N);
	value &= ~AUDIO_N_RESETF;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_AUDIO_N);

	if (of_device_is_compatible(node, "nvidia,tegra30-hdmi")) {
		switch (hdmi->audio_freq) {
		case 32000:
			offset = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0320;
			break;

		case 44100:
			offset = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0441;
			break;

		case 48000:
			offset = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0480;
			break;

		case 88200:
			offset = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0882;
			break;

		case 96000:
			offset = HDMI_NV_PDISP_SOR_AUDIO_AVAL_0960;
			break;

		case 176400:
			offset = HDMI_NV_PDISP_SOR_AUDIO_AVAL_1764;
			break;

		case 192000:
			offset = HDMI_NV_PDISP_SOR_AUDIO_AVAL_1920;
			break;
		}

		tegra_hdmi_writel(hdmi, config->aval, offset);
	}

	tegra_hdmi_setup_audio_fs_tables(hdmi);

	return 0;
}

static void tegra_hdmi_write_infopack(struct tegra_hdmi *hdmi,
				      unsigned int offset, u8 type,
				      u8 version, void *data, size_t size)
{
	unsigned long value;
	u8 *ptr = data;
	u32 subpack[2];
	size_t i;
	u8 csum;

	/* first byte of data is the checksum */
	csum = type + version + size - 1;

	for (i = 1; i < size; i++)
		csum += ptr[i];

	ptr[0] = 0x100 - csum;

	value = INFOFRAME_HEADER_TYPE(type) |
		INFOFRAME_HEADER_VERSION(version) |
		INFOFRAME_HEADER_LEN(size - 1);
	tegra_hdmi_writel(hdmi, value, offset);

	/* The audio inforame only has one set of subpack registers.  The hdmi
	 * block pads the rest of the data as per the spec so we have to fixup
	 * the length before filling in the subpacks.
	 */
	if (offset == HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER)
		size = 6;

	/* each subpack 7 bytes devided into:
	 *   subpack_low - bytes 0 - 3
	 *   subpack_high - bytes 4 - 6 (with byte 7 padded to 0x00)
	 */
	for (i = 0; i < size; i++) {
		size_t index = i % 7;

		if (index == 0)
			memset(subpack, 0x0, sizeof(subpack));

		((u8 *)subpack)[index] = ptr[i];

		if (index == 6 || (i + 1 == size)) {
			unsigned int reg = offset + 1 + (i / 7) * 2;

			tegra_hdmi_writel(hdmi, subpack[0], reg);
			tegra_hdmi_writel(hdmi, subpack[1], reg + 1);
		}
	}
}

static void tegra_hdmi_setup_avi_infoframe(struct tegra_hdmi *hdmi,
					   struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	unsigned int h_front_porch;
	unsigned int hsize = 16;
	unsigned int vsize = 9;

	if (hdmi->dvi) {
		tegra_hdmi_writel(hdmi, 0,
				  HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
		return;
	}

	h_front_porch = mode->hsync_start - mode->hdisplay;
	memset(&frame, 0, sizeof(frame));
	frame.r = HDMI_AVI_R_SAME;

	switch (mode->vdisplay) {
	case 480:
		if (mode->hdisplay == 640) {
			frame.m = HDMI_AVI_M_4_3;
			frame.vic = 1;
		} else {
			frame.m = HDMI_AVI_M_16_9;
			frame.vic = 3;
		}
		break;

	case 576:
		if (((hsize * 10) / vsize) > 14) {
			frame.m = HDMI_AVI_M_16_9;
			frame.vic = 18;
		} else {
			frame.m = HDMI_AVI_M_4_3;
			frame.vic = 17;
		}
		break;

	case 720:
	case 1470: /* stereo mode */
		frame.m = HDMI_AVI_M_16_9;

		if (h_front_porch == 110)
			frame.vic = 4;
		else
			frame.vic = 19;
		break;

	case 1080:
	case 2205: /* stereo mode */
		frame.m = HDMI_AVI_M_16_9;

		switch (h_front_porch) {
		case 88:
			frame.vic = 16;
			break;

		case 528:
			frame.vic = 31;
			break;

		default:
			frame.vic = 32;
			break;
		}
		break;

	default:
		frame.m = HDMI_AVI_M_16_9;
		frame.vic = 0;
		break;
	}

	tegra_hdmi_write_infopack(hdmi, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_HEADER,
				  HDMI_INFOFRAME_TYPE_AVI, HDMI_AVI_VERSION,
				  &frame, sizeof(frame));

	tegra_hdmi_writel(hdmi, INFOFRAME_CTRL_ENABLE,
			  HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
}

static void tegra_hdmi_setup_audio_infoframe(struct tegra_hdmi *hdmi)
{
	struct hdmi_audio_infoframe frame;

	if (hdmi->dvi) {
		tegra_hdmi_writel(hdmi, 0,
				  HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
		return;
	}

	memset(&frame, 0, sizeof(frame));
	frame.cc = HDMI_AUDIO_CC_2;

	tegra_hdmi_write_infopack(hdmi,
				  HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER,
				  HDMI_INFOFRAME_TYPE_AUDIO,
				  HDMI_AUDIO_VERSION,
				  &frame, sizeof(frame));

	tegra_hdmi_writel(hdmi, INFOFRAME_CTRL_ENABLE,
			  HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
}

static void tegra_hdmi_setup_stereo_infoframe(struct tegra_hdmi *hdmi)
{
	struct hdmi_stereo_infoframe frame;
	unsigned long value;

	if (!hdmi->stereo) {
		value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
		value &= ~GENERIC_CTRL_ENABLE;
		tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
		return;
	}

	memset(&frame, 0, sizeof(frame));
	frame.regid0 = 0x03;
	frame.regid1 = 0x0c;
	frame.regid2 = 0x00;
	frame.hdmi_video_format = 2;

	/* TODO: 74 MHz limit? */
	if (1) {
		frame._3d_structure = 0;
	} else {
		frame._3d_structure = 8;
		frame._3d_ext_data = 0;
	}

	tegra_hdmi_write_infopack(hdmi, HDMI_NV_PDISP_HDMI_GENERIC_HEADER,
				  HDMI_INFOFRAME_TYPE_VENDOR,
				  HDMI_VENDOR_VERSION, &frame, 6);

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	value |= GENERIC_CTRL_ENABLE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
}

static void tegra_hdmi_setup_tmds(struct tegra_hdmi *hdmi,
				  const struct tmds_config *tmds)
{
	unsigned long value;

	tegra_hdmi_writel(hdmi, tmds->pll0, HDMI_NV_PDISP_SOR_PLL0);
	tegra_hdmi_writel(hdmi, tmds->pll1, HDMI_NV_PDISP_SOR_PLL1);
	tegra_hdmi_writel(hdmi, tmds->pe_current, HDMI_NV_PDISP_PE_CURRENT);

	value = tmds->drive_current | DRIVE_CURRENT_FUSE_OVERRIDE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT);
}

static int tegra_output_hdmi_enable(struct tegra_output *output)
{
	unsigned int h_sync_width, h_front_porch, h_back_porch, i, rekey;
	struct tegra_dc *dc = to_tegra_dc(output->encoder.crtc);
	struct drm_display_mode *mode = &dc->base.mode;
	struct tegra_hdmi *hdmi = to_hdmi(output);
	struct device_node *node = hdmi->dev->of_node;
	unsigned int pulse_start, div82, pclk;
	const struct tmds_config *tmds;
	unsigned int num_tmds;
	unsigned long value;
	int retries = 1000;
	int err;

	pclk = mode->clock * 1000;
	h_sync_width = mode->hsync_end - mode->hsync_start;
	h_back_porch = mode->htotal - mode->hsync_end;
	h_front_porch = mode->hsync_start - mode->hdisplay;

	err = regulator_enable(hdmi->vdd);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to enable VDD regulator: %d\n", err);
		return err;
	}

	err = regulator_enable(hdmi->pll);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to enable PLL regulator: %d\n", err);
		return err;
	}

	/*
	 * This assumes that the display controller will divide its parent
	 * clock by 2 to generate the pixel clock.
	 */
	err = tegra_output_setup_clock(output, hdmi->clk, pclk * 2);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to setup clock: %d\n", err);
		return err;
	}

	err = clk_set_rate(hdmi->clk, pclk);
	if (err < 0)
		return err;

	err = clk_enable(hdmi->clk);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to enable clock: %d\n", err);
		return err;
	}

	tegra_periph_reset_assert(hdmi->clk);
	usleep_range(1000, 2000);
	tegra_periph_reset_deassert(hdmi->clk);

	tegra_dc_writel(dc, VSYNC_H_POSITION(1),
			DC_DISP_DISP_TIMING_OPTIONS);
	tegra_dc_writel(dc, DITHER_CONTROL_DISABLE | BASE_COLOR_SIZE888,
			DC_DISP_DISP_COLOR_CONTROL);

	/* video_preamble uses h_pulse2 */
	pulse_start = 1 + h_sync_width + h_back_porch - 10;

	tegra_dc_writel(dc, H_PULSE_2_ENABLE, DC_DISP_DISP_SIGNAL_OPTIONS0);

	value = PULSE_MODE_NORMAL | PULSE_POLARITY_HIGH | PULSE_QUAL_VACTIVE |
		PULSE_LAST_END_A;
	tegra_dc_writel(dc, value, DC_DISP_H_PULSE2_CONTROL);

	value = PULSE_START(pulse_start) | PULSE_END(pulse_start + 8);
	tegra_dc_writel(dc, value, DC_DISP_H_PULSE2_POSITION_A);

	value = VSYNC_WINDOW_END(0x210) | VSYNC_WINDOW_START(0x200) |
		VSYNC_WINDOW_ENABLE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_VSYNC_WINDOW);

	if (dc->pipe)
		value = HDMI_SRC_DISPLAYB;
	else
		value = HDMI_SRC_DISPLAYA;

	if ((mode->hdisplay == 720) && ((mode->vdisplay == 480) ||
					(mode->vdisplay == 576)))
		tegra_hdmi_writel(hdmi,
				  value | ARM_VIDEO_RANGE_FULL,
				  HDMI_NV_PDISP_INPUT_CONTROL);
	else
		tegra_hdmi_writel(hdmi,
				  value | ARM_VIDEO_RANGE_LIMITED,
				  HDMI_NV_PDISP_INPUT_CONTROL);

	div82 = clk_get_rate(hdmi->clk) / 1000000 * 4;
	value = SOR_REFCLK_DIV_INT(div82 >> 2) | SOR_REFCLK_DIV_FRAC(div82);
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_REFCLK);

	if (!hdmi->dvi) {
		err = tegra_hdmi_setup_audio(hdmi, pclk);
		if (err < 0)
			hdmi->dvi = true;
	}

	if (of_device_is_compatible(node, "nvidia,tegra20-hdmi")) {
		/*
		 * TODO: add ELD support
		 */
	}

	rekey = HDMI_REKEY_DEFAULT;
	value = HDMI_CTRL_REKEY(rekey);
	value |= HDMI_CTRL_MAX_AC_PACKET((h_sync_width + h_back_porch +
					  h_front_porch - rekey - 18) / 32);

	if (!hdmi->dvi)
		value |= HDMI_CTRL_ENABLE;

	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_CTRL);

	if (hdmi->dvi)
		tegra_hdmi_writel(hdmi, 0x0,
				  HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	else
		tegra_hdmi_writel(hdmi, GENERIC_CTRL_AUDIO,
				  HDMI_NV_PDISP_HDMI_GENERIC_CTRL);

	tegra_hdmi_setup_avi_infoframe(hdmi, mode);
	tegra_hdmi_setup_audio_infoframe(hdmi);
	tegra_hdmi_setup_stereo_infoframe(hdmi);

	/* TMDS CONFIG */
	if (of_device_is_compatible(node, "nvidia,tegra30-hdmi")) {
		num_tmds = ARRAY_SIZE(tegra3_tmds_config);
		tmds = tegra3_tmds_config;
	} else {
		num_tmds = ARRAY_SIZE(tegra2_tmds_config);
		tmds = tegra2_tmds_config;
	}

	for (i = 0; i < num_tmds; i++) {
		if (pclk <= tmds[i].pclk) {
			tegra_hdmi_setup_tmds(hdmi, &tmds[i]);
			break;
		}
	}

	tegra_hdmi_writel(hdmi,
			  SOR_SEQ_CTL_PU_PC(0) |
			  SOR_SEQ_PU_PC_ALT(0) |
			  SOR_SEQ_PD_PC(8) |
			  SOR_SEQ_PD_PC_ALT(8),
			  HDMI_NV_PDISP_SOR_SEQ_CTL);

	value = SOR_SEQ_INST_WAIT_TIME(1) |
		SOR_SEQ_INST_WAIT_UNITS_VSYNC |
		SOR_SEQ_INST_HALT |
		SOR_SEQ_INST_PIN_A_LOW |
		SOR_SEQ_INST_PIN_B_LOW |
		SOR_SEQ_INST_DRIVE_PWM_OUT_LO;

	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_SEQ_INST(0));
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_SEQ_INST(8));

	value = 0x1c800;
	value &= ~SOR_CSTM_ROTCLK(~0);
	value |= SOR_CSTM_ROTCLK(2);
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_CSTM);

	tegra_dc_writel(dc, DISP_CTRL_MODE_STOP, DC_CMD_DISPLAY_COMMAND);
	tegra_dc_writel(dc, GENERAL_ACT_REQ << 8, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, GENERAL_ACT_REQ, DC_CMD_STATE_CONTROL);

	/* start SOR */
	tegra_hdmi_writel(hdmi,
			  SOR_PWR_NORMAL_STATE_PU |
			  SOR_PWR_NORMAL_START_NORMAL |
			  SOR_PWR_SAFE_STATE_PD |
			  SOR_PWR_SETTING_NEW_TRIGGER,
			  HDMI_NV_PDISP_SOR_PWR);
	tegra_hdmi_writel(hdmi,
			  SOR_PWR_NORMAL_STATE_PU |
			  SOR_PWR_NORMAL_START_NORMAL |
			  SOR_PWR_SAFE_STATE_PD |
			  SOR_PWR_SETTING_NEW_DONE,
			  HDMI_NV_PDISP_SOR_PWR);

	do {
		BUG_ON(--retries < 0);
		value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_SOR_PWR);
	} while (value & SOR_PWR_SETTING_NEW_PENDING);

	value = SOR_STATE_ASY_CRCMODE_COMPLETE |
		SOR_STATE_ASY_OWNER_HEAD0 |
		SOR_STATE_ASY_SUBOWNER_BOTH |
		SOR_STATE_ASY_PROTOCOL_SINGLE_TMDS_A |
		SOR_STATE_ASY_DEPOL_POS;

	/* setup sync polarities */
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		value |= SOR_STATE_ASY_HSYNCPOL_POS;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		value |= SOR_STATE_ASY_HSYNCPOL_NEG;

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		value |= SOR_STATE_ASY_VSYNCPOL_POS;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		value |= SOR_STATE_ASY_VSYNCPOL_NEG;

	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_STATE2);

	value = SOR_STATE_ASY_HEAD_OPMODE_AWAKE | SOR_STATE_ASY_ORMODE_NORMAL;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_STATE1);

	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_SOR_STATE0);
	tegra_hdmi_writel(hdmi, SOR_STATE_UPDATE, HDMI_NV_PDISP_SOR_STATE0);
	tegra_hdmi_writel(hdmi, value | SOR_STATE_ATTACHED,
			  HDMI_NV_PDISP_SOR_STATE1);
	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_SOR_STATE0);

	tegra_dc_writel(dc, HDMI_ENABLE, DC_DISP_DISP_WIN_OPTIONS);

	value = PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
		PW4_ENABLE | PM0_ENABLE | PM1_ENABLE;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_POWER_CONTROL);

	value = DISP_CTRL_MODE_C_DISPLAY;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_COMMAND);

	tegra_dc_writel(dc, GENERAL_ACT_REQ << 8, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, GENERAL_ACT_REQ, DC_CMD_STATE_CONTROL);

	/* TODO: add HDCP support */

	return 0;
}

static int tegra_output_hdmi_disable(struct tegra_output *output)
{
	struct tegra_hdmi *hdmi = to_hdmi(output);

	tegra_periph_reset_assert(hdmi->clk);
	clk_disable(hdmi->clk);
	regulator_disable(hdmi->pll);
	regulator_disable(hdmi->vdd);

	return 0;
}

static int tegra_output_hdmi_setup_clock(struct tegra_output *output,
					 struct clk *clk, unsigned long pclk)
{
	struct tegra_hdmi *hdmi = to_hdmi(output);
	struct clk *base;
	int err;

	err = clk_set_parent(clk, hdmi->clk_parent);
	if (err < 0) {
		dev_err(output->dev, "failed to set parent: %d\n", err);
		return err;
	}

	base = clk_get_parent(hdmi->clk_parent);

	/*
	 * This assumes that the parent clock is pll_d_out0 or pll_d2_out
	 * respectively, each of which divides the base pll_d by 2.
	 */
	err = clk_set_rate(base, pclk * 2);
	if (err < 0)
		dev_err(output->dev,
			"failed to set base clock rate to %lu Hz\n",
			pclk * 2);

	return 0;
}

static int tegra_output_hdmi_check_mode(struct tegra_output *output,
					struct drm_display_mode *mode,
					enum drm_mode_status *status)
{
	struct tegra_hdmi *hdmi = to_hdmi(output);
	unsigned long pclk = mode->clock * 1000;
	struct clk *parent;
	long err;

	parent = clk_get_parent(hdmi->clk_parent);

	err = clk_round_rate(parent, pclk * 4);
	if (err < 0)
		*status = MODE_NOCLOCK;
	else
		*status = MODE_OK;

	return 0;
}

static const struct tegra_output_ops hdmi_ops = {
	.enable = tegra_output_hdmi_enable,
	.disable = tegra_output_hdmi_disable,
	.setup_clock = tegra_output_hdmi_setup_clock,
	.check_mode = tegra_output_hdmi_check_mode,
};

static int tegra_hdmi_show_regs(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_hdmi *hdmi = node->info_ent->data;

#define DUMP_REG(name)						\
	seq_printf(s, "%-56s %#05x %08lx\n", #name, name,	\
		tegra_hdmi_readl(hdmi, name))

	DUMP_REG(HDMI_CTXSW);
	DUMP_REG(HDMI_NV_PDISP_SOR_STATE0);
	DUMP_REG(HDMI_NV_PDISP_SOR_STATE1);
	DUMP_REG(HDMI_NV_PDISP_SOR_STATE2);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_AN_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_AN_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CN_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CN_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_AKSV_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_AKSV_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_BKSV_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_BKSV_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CKSV_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CKSV_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_DKSV_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_DKSV_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CTRL);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CMODE);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_MPRIME_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_MPRIME_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_SPRIME_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_SPRIME_LSB2);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_SPRIME_LSB1);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_RI);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CS_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CS_LSB);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_EMU0);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_EMU_RDATA0);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_EMU1);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_EMU2);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_STATUS);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_STATUS);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_HEADER);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_STATUS);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_HEADER);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_VSYNC_KEEPOUT);
	DUMP_REG(HDMI_NV_PDISP_HDMI_VSYNC_WINDOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GCP_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GCP_STATUS);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GCP_SUBPACK);
	DUMP_REG(HDMI_NV_PDISP_HDMI_CHANNEL_STATUS1);
	DUMP_REG(HDMI_NV_PDISP_HDMI_CHANNEL_STATUS2);
	DUMP_REG(HDMI_NV_PDISP_HDMI_EMU0);
	DUMP_REG(HDMI_NV_PDISP_HDMI_EMU1);
	DUMP_REG(HDMI_NV_PDISP_HDMI_EMU1_RDATA);
	DUMP_REG(HDMI_NV_PDISP_HDMI_SPARE);
	DUMP_REG(HDMI_NV_PDISP_HDMI_SPDIF_CHN_STATUS1);
	DUMP_REG(HDMI_NV_PDISP_HDMI_SPDIF_CHN_STATUS2);
	DUMP_REG(HDMI_NV_PDISP_HDMI_HDCPRIF_ROM_CTRL);
	DUMP_REG(HDMI_NV_PDISP_SOR_CAP);
	DUMP_REG(HDMI_NV_PDISP_SOR_PWR);
	DUMP_REG(HDMI_NV_PDISP_SOR_TEST);
	DUMP_REG(HDMI_NV_PDISP_SOR_PLL0);
	DUMP_REG(HDMI_NV_PDISP_SOR_PLL1);
	DUMP_REG(HDMI_NV_PDISP_SOR_PLL2);
	DUMP_REG(HDMI_NV_PDISP_SOR_CSTM);
	DUMP_REG(HDMI_NV_PDISP_SOR_LVDS);
	DUMP_REG(HDMI_NV_PDISP_SOR_CRCA);
	DUMP_REG(HDMI_NV_PDISP_SOR_CRCB);
	DUMP_REG(HDMI_NV_PDISP_SOR_BLANK);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_CTL);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(0));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(1));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(2));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(3));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(4));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(5));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(6));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(7));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(8));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(9));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(10));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(11));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(12));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(13));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(14));
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST(15));
	DUMP_REG(HDMI_NV_PDISP_SOR_VCRCA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_VCRCA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_CCRCA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_CCRCA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_EDATAA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_EDATAA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_COUNTA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_COUNTA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_DEBUGA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_DEBUGA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_TRIG);
	DUMP_REG(HDMI_NV_PDISP_SOR_MSCHECK);
	DUMP_REG(HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_DEBUG0);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_DEBUG1);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_DEBUG2);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS(0));
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS(1));
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS(2));
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS(3));
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS(4));
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS(5));
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS(6));
	DUMP_REG(HDMI_NV_PDISP_AUDIO_PULSE_WIDTH);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_THRESHOLD);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_CNTRL0);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_N);
	DUMP_REG(HDMI_NV_PDISP_HDCPRIF_ROM_TIMING);
	DUMP_REG(HDMI_NV_PDISP_SOR_REFCLK);
	DUMP_REG(HDMI_NV_PDISP_CRC_CONTROL);
	DUMP_REG(HDMI_NV_PDISP_INPUT_CONTROL);
	DUMP_REG(HDMI_NV_PDISP_SCRATCH);
	DUMP_REG(HDMI_NV_PDISP_PE_CURRENT);
	DUMP_REG(HDMI_NV_PDISP_KEY_CTRL);
	DUMP_REG(HDMI_NV_PDISP_KEY_DEBUG0);
	DUMP_REG(HDMI_NV_PDISP_KEY_DEBUG1);
	DUMP_REG(HDMI_NV_PDISP_KEY_DEBUG2);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_0);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_1);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_2);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_3);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_TRIG);
	DUMP_REG(HDMI_NV_PDISP_KEY_SKEY_INDEX);
	DUMP_REG(HDMI_NV_PDISP_SOR_AUDIO_CNTRL0);
	DUMP_REG(HDMI_NV_PDISP_SOR_AUDIO_HDA_ELD_BUFWR);
	DUMP_REG(HDMI_NV_PDISP_SOR_AUDIO_HDA_PRESENSE);

#undef DUMP_REG

	return 0;
}

static struct drm_info_list debugfs_files[] = {
	{ "regs", tegra_hdmi_show_regs, 0, NULL },
};

static int tegra_hdmi_debugfs_init(struct tegra_hdmi *hdmi,
				   struct drm_minor *minor)
{
	unsigned int i;
	int err;

	hdmi->debugfs = debugfs_create_dir("hdmi", minor->debugfs_root);
	if (!hdmi->debugfs)
		return -ENOMEM;

	hdmi->debugfs_files = kmemdup(debugfs_files, sizeof(debugfs_files),
				      GFP_KERNEL);
	if (!hdmi->debugfs_files) {
		err = -ENOMEM;
		goto remove;
	}

	for (i = 0; i < ARRAY_SIZE(debugfs_files); i++)
		hdmi->debugfs_files[i].data = hdmi;

	err = drm_debugfs_create_files(hdmi->debugfs_files,
				       ARRAY_SIZE(debugfs_files),
				       hdmi->debugfs, minor);
	if (err < 0)
		goto free;

	hdmi->minor = minor;

	return 0;

free:
	kfree(hdmi->debugfs_files);
	hdmi->debugfs_files = NULL;
remove:
	debugfs_remove(hdmi->debugfs);
	hdmi->debugfs = NULL;

	return err;
}

static int tegra_hdmi_debugfs_exit(struct tegra_hdmi *hdmi)
{
	drm_debugfs_remove_files(hdmi->debugfs_files, ARRAY_SIZE(debugfs_files),
				 hdmi->minor);
	hdmi->minor = NULL;

	kfree(hdmi->debugfs_files);
	hdmi->debugfs_files = NULL;

	debugfs_remove(hdmi->debugfs);
	hdmi->debugfs = NULL;

	return 0;
}

static int tegra_hdmi_drm_init(struct host1x_client *client,
			       struct drm_device *drm)
{
	struct tegra_hdmi *hdmi = host1x_client_to_hdmi(client);
	int err;

	hdmi->output.type = TEGRA_OUTPUT_HDMI;
	hdmi->output.dev = client->dev;
	hdmi->output.ops = &hdmi_ops;

	err = tegra_output_init(drm, &hdmi->output);
	if (err < 0) {
		dev_err(client->dev, "output setup failed: %d\n", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_hdmi_debugfs_init(hdmi, drm->primary);
		if (err < 0)
			dev_err(client->dev, "debugfs setup failed: %d\n", err);
	}

	return 0;
}

static int tegra_hdmi_drm_exit(struct host1x_client *client)
{
	struct tegra_hdmi *hdmi = host1x_client_to_hdmi(client);
	int err;

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_hdmi_debugfs_exit(hdmi);
		if (err < 0)
			dev_err(client->dev, "debugfs cleanup failed: %d\n",
				err);
	}

	err = tegra_output_disable(&hdmi->output);
	if (err < 0) {
		dev_err(client->dev, "output failed to disable: %d\n", err);
		return err;
	}

	err = tegra_output_exit(&hdmi->output);
	if (err < 0) {
		dev_err(client->dev, "output cleanup failed: %d\n", err);
		return err;
	}

	return 0;
}

static const struct host1x_client_ops hdmi_client_ops = {
	.drm_init = tegra_hdmi_drm_init,
	.drm_exit = tegra_hdmi_drm_exit,
};

static int tegra_hdmi_probe(struct platform_device *pdev)
{
	struct host1x *host1x = dev_get_drvdata(pdev->dev.parent);
	struct tegra_hdmi *hdmi;
	struct resource *regs;
	int err;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = &pdev->dev;
	hdmi->audio_source = AUTO;
	hdmi->audio_freq = 44100;
	hdmi->stereo = false;
	hdmi->dvi = false;

	hdmi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(hdmi->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(hdmi->clk);
	}

	err = clk_prepare(hdmi->clk);
	if (err < 0)
		return err;

	hdmi->clk_parent = devm_clk_get(&pdev->dev, "parent");
	if (IS_ERR(hdmi->clk_parent))
		return PTR_ERR(hdmi->clk_parent);

	err = clk_prepare(hdmi->clk_parent);
	if (err < 0)
		return err;

	err = clk_set_parent(hdmi->clk, hdmi->clk_parent);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to setup clocks: %d\n", err);
		return err;
	}

	hdmi->vdd = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(hdmi->vdd)) {
		dev_err(&pdev->dev, "failed to get VDD regulator\n");
		return PTR_ERR(hdmi->vdd);
	}

	hdmi->pll = devm_regulator_get(&pdev->dev, "pll");
	if (IS_ERR(hdmi->pll)) {
		dev_err(&pdev->dev, "failed to get PLL regulator\n");
		return PTR_ERR(hdmi->pll);
	}

	hdmi->output.dev = &pdev->dev;

	err = tegra_output_parse_dt(&hdmi->output);
	if (err < 0)
		return err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	hdmi->regs = devm_request_and_ioremap(&pdev->dev, regs);
	if (!hdmi->regs)
		return -EADDRNOTAVAIL;

	err = platform_get_irq(pdev, 0);
	if (err < 0)
		return err;

	hdmi->irq = err;

	hdmi->client.ops = &hdmi_client_ops;
	INIT_LIST_HEAD(&hdmi->client.list);
	hdmi->client.dev = &pdev->dev;

	err = host1x_register_client(host1x, &hdmi->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, hdmi);

	return 0;
}

static int tegra_hdmi_remove(struct platform_device *pdev)
{
	struct host1x *host1x = dev_get_drvdata(pdev->dev.parent);
	struct tegra_hdmi *hdmi = platform_get_drvdata(pdev);
	int err;

	err = host1x_unregister_client(host1x, &hdmi->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	clk_unprepare(hdmi->clk_parent);
	clk_unprepare(hdmi->clk);

	return 0;
}

static struct of_device_id tegra_hdmi_of_match[] = {
	{ .compatible = "nvidia,tegra30-hdmi", },
	{ .compatible = "nvidia,tegra20-hdmi", },
	{ },
};

struct platform_driver tegra_hdmi_driver = {
	.driver = {
		.name = "tegra-hdmi",
		.owner = THIS_MODULE,
		.of_match_table = tegra_hdmi_of_match,
	},
	.probe = tegra_hdmi_probe,
	.remove = tegra_hdmi_remove,
};
