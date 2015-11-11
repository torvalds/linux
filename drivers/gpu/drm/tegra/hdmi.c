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
#include <linux/hdmi.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "hdmi.h"
#include "drm.h"
#include "dc.h"

struct tmds_config {
	unsigned int pclk;
	u32 pll0;
	u32 pll1;
	u32 pe_current;
	u32 drive_current;
	u32 peak_current;
};

struct tegra_hdmi_config {
	const struct tmds_config *tmds;
	unsigned int num_tmds;

	unsigned long fuse_override_offset;
	u32 fuse_override_value;

	bool has_sor_io_peak_current;
};

struct tegra_hdmi {
	struct host1x_client client;
	struct tegra_output output;
	struct device *dev;

	struct regulator *hdmi;
	struct regulator *pll;
	struct regulator *vdd;

	void __iomem *regs;
	unsigned int irq;

	struct clk *clk_parent;
	struct clk *clk;
	struct reset_control *rst;

	const struct tegra_hdmi_config *config;

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

static inline u32 tegra_hdmi_readl(struct tegra_hdmi *hdmi,
				   unsigned long offset)
{
	return readl(hdmi->regs + (offset << 2));
}

static inline void tegra_hdmi_writel(struct tegra_hdmi *hdmi, u32 value,
				     unsigned long offset)
{
	writel(value, hdmi->regs + (offset << 2));
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

static const struct tmds_config tegra20_tmds_config[] = {
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

static const struct tmds_config tegra30_tmds_config[] = {
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

static const struct tmds_config tegra114_tmds_config[] = {
	{ /* 480p/576p / 25.2MHz/27MHz modes */
		.pclk = 27000000,
		.pll0 = SOR_PLL_ICHPMP(1) | SOR_PLL_BG_V17_S(3) |
			SOR_PLL_VCOCAP(0) | SOR_PLL_RESISTORSEL,
		.pll1 = SOR_PLL_LOADADJ(3) | SOR_PLL_TMDS_TERMADJ(0),
		.pe_current = PE_CURRENT0(PE_CURRENT_0_mA_T114) |
			PE_CURRENT1(PE_CURRENT_0_mA_T114) |
			PE_CURRENT2(PE_CURRENT_0_mA_T114) |
			PE_CURRENT3(PE_CURRENT_0_mA_T114),
		.drive_current =
			DRIVE_CURRENT_LANE0_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE1_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE2_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE3_T114(DRIVE_CURRENT_10_400_mA_T114),
		.peak_current = PEAK_CURRENT_LANE0(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE1(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE2(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE3(PEAK_CURRENT_0_000_mA),
	}, { /* 720p / 74.25MHz modes */
		.pclk = 74250000,
		.pll0 = SOR_PLL_ICHPMP(1) | SOR_PLL_BG_V17_S(3) |
			SOR_PLL_VCOCAP(1) | SOR_PLL_RESISTORSEL,
		.pll1 = SOR_PLL_PE_EN | SOR_PLL_LOADADJ(3) |
			SOR_PLL_TMDS_TERMADJ(0),
		.pe_current = PE_CURRENT0(PE_CURRENT_15_mA_T114) |
			PE_CURRENT1(PE_CURRENT_15_mA_T114) |
			PE_CURRENT2(PE_CURRENT_15_mA_T114) |
			PE_CURRENT3(PE_CURRENT_15_mA_T114),
		.drive_current =
			DRIVE_CURRENT_LANE0_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE1_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE2_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE3_T114(DRIVE_CURRENT_10_400_mA_T114),
		.peak_current = PEAK_CURRENT_LANE0(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE1(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE2(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE3(PEAK_CURRENT_0_000_mA),
	}, { /* 1080p / 148.5MHz modes */
		.pclk = 148500000,
		.pll0 = SOR_PLL_ICHPMP(1) | SOR_PLL_BG_V17_S(3) |
			SOR_PLL_VCOCAP(3) | SOR_PLL_RESISTORSEL,
		.pll1 = SOR_PLL_PE_EN | SOR_PLL_LOADADJ(3) |
			SOR_PLL_TMDS_TERMADJ(0),
		.pe_current = PE_CURRENT0(PE_CURRENT_10_mA_T114) |
			PE_CURRENT1(PE_CURRENT_10_mA_T114) |
			PE_CURRENT2(PE_CURRENT_10_mA_T114) |
			PE_CURRENT3(PE_CURRENT_10_mA_T114),
		.drive_current =
			DRIVE_CURRENT_LANE0_T114(DRIVE_CURRENT_12_400_mA_T114) |
			DRIVE_CURRENT_LANE1_T114(DRIVE_CURRENT_12_400_mA_T114) |
			DRIVE_CURRENT_LANE2_T114(DRIVE_CURRENT_12_400_mA_T114) |
			DRIVE_CURRENT_LANE3_T114(DRIVE_CURRENT_12_400_mA_T114),
		.peak_current = PEAK_CURRENT_LANE0(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE1(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE2(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE3(PEAK_CURRENT_0_000_mA),
	}, { /* 225/297MHz modes */
		.pclk = UINT_MAX,
		.pll0 = SOR_PLL_ICHPMP(1) | SOR_PLL_BG_V17_S(3) |
			SOR_PLL_VCOCAP(0xf) | SOR_PLL_RESISTORSEL,
		.pll1 = SOR_PLL_LOADADJ(3) | SOR_PLL_TMDS_TERMADJ(7)
			| SOR_PLL_TMDS_TERM_ENABLE,
		.pe_current = PE_CURRENT0(PE_CURRENT_0_mA_T114) |
			PE_CURRENT1(PE_CURRENT_0_mA_T114) |
			PE_CURRENT2(PE_CURRENT_0_mA_T114) |
			PE_CURRENT3(PE_CURRENT_0_mA_T114),
		.drive_current =
			DRIVE_CURRENT_LANE0_T114(DRIVE_CURRENT_25_200_mA_T114) |
			DRIVE_CURRENT_LANE1_T114(DRIVE_CURRENT_25_200_mA_T114) |
			DRIVE_CURRENT_LANE2_T114(DRIVE_CURRENT_25_200_mA_T114) |
			DRIVE_CURRENT_LANE3_T114(DRIVE_CURRENT_19_200_mA_T114),
		.peak_current = PEAK_CURRENT_LANE0(PEAK_CURRENT_3_000_mA) |
			PEAK_CURRENT_LANE1(PEAK_CURRENT_3_000_mA) |
			PEAK_CURRENT_LANE2(PEAK_CURRENT_3_000_mA) |
			PEAK_CURRENT_LANE3(PEAK_CURRENT_0_800_mA),
	},
};

static const struct tmds_config tegra124_tmds_config[] = {
	{ /* 480p/576p / 25.2MHz/27MHz modes */
		.pclk = 27000000,
		.pll0 = SOR_PLL_ICHPMP(1) | SOR_PLL_BG_V17_S(3) |
			SOR_PLL_VCOCAP(0) | SOR_PLL_RESISTORSEL,
		.pll1 = SOR_PLL_LOADADJ(3) | SOR_PLL_TMDS_TERMADJ(0),
		.pe_current = PE_CURRENT0(PE_CURRENT_0_mA_T114) |
			PE_CURRENT1(PE_CURRENT_0_mA_T114) |
			PE_CURRENT2(PE_CURRENT_0_mA_T114) |
			PE_CURRENT3(PE_CURRENT_0_mA_T114),
		.drive_current =
			DRIVE_CURRENT_LANE0_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE1_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE2_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE3_T114(DRIVE_CURRENT_10_400_mA_T114),
		.peak_current = PEAK_CURRENT_LANE0(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE1(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE2(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE3(PEAK_CURRENT_0_000_mA),
	}, { /* 720p / 74.25MHz modes */
		.pclk = 74250000,
		.pll0 = SOR_PLL_ICHPMP(1) | SOR_PLL_BG_V17_S(3) |
			SOR_PLL_VCOCAP(1) | SOR_PLL_RESISTORSEL,
		.pll1 = SOR_PLL_PE_EN | SOR_PLL_LOADADJ(3) |
			SOR_PLL_TMDS_TERMADJ(0),
		.pe_current = PE_CURRENT0(PE_CURRENT_15_mA_T114) |
			PE_CURRENT1(PE_CURRENT_15_mA_T114) |
			PE_CURRENT2(PE_CURRENT_15_mA_T114) |
			PE_CURRENT3(PE_CURRENT_15_mA_T114),
		.drive_current =
			DRIVE_CURRENT_LANE0_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE1_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE2_T114(DRIVE_CURRENT_10_400_mA_T114) |
			DRIVE_CURRENT_LANE3_T114(DRIVE_CURRENT_10_400_mA_T114),
		.peak_current = PEAK_CURRENT_LANE0(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE1(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE2(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE3(PEAK_CURRENT_0_000_mA),
	}, { /* 1080p / 148.5MHz modes */
		.pclk = 148500000,
		.pll0 = SOR_PLL_ICHPMP(1) | SOR_PLL_BG_V17_S(3) |
			SOR_PLL_VCOCAP(3) | SOR_PLL_RESISTORSEL,
		.pll1 = SOR_PLL_PE_EN | SOR_PLL_LOADADJ(3) |
			SOR_PLL_TMDS_TERMADJ(0),
		.pe_current = PE_CURRENT0(PE_CURRENT_10_mA_T114) |
			PE_CURRENT1(PE_CURRENT_10_mA_T114) |
			PE_CURRENT2(PE_CURRENT_10_mA_T114) |
			PE_CURRENT3(PE_CURRENT_10_mA_T114),
		.drive_current =
			DRIVE_CURRENT_LANE0_T114(DRIVE_CURRENT_12_400_mA_T114) |
			DRIVE_CURRENT_LANE1_T114(DRIVE_CURRENT_12_400_mA_T114) |
			DRIVE_CURRENT_LANE2_T114(DRIVE_CURRENT_12_400_mA_T114) |
			DRIVE_CURRENT_LANE3_T114(DRIVE_CURRENT_12_400_mA_T114),
		.peak_current = PEAK_CURRENT_LANE0(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE1(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE2(PEAK_CURRENT_0_000_mA) |
			PEAK_CURRENT_LANE3(PEAK_CURRENT_0_000_mA),
	}, { /* 225/297MHz modes */
		.pclk = UINT_MAX,
		.pll0 = SOR_PLL_ICHPMP(1) | SOR_PLL_BG_V17_S(3) |
			SOR_PLL_VCOCAP(0xf) | SOR_PLL_RESISTORSEL,
		.pll1 = SOR_PLL_LOADADJ(3) | SOR_PLL_TMDS_TERMADJ(7)
			| SOR_PLL_TMDS_TERM_ENABLE,
		.pe_current = PE_CURRENT0(PE_CURRENT_0_mA_T114) |
			PE_CURRENT1(PE_CURRENT_0_mA_T114) |
			PE_CURRENT2(PE_CURRENT_0_mA_T114) |
			PE_CURRENT3(PE_CURRENT_0_mA_T114),
		.drive_current =
			DRIVE_CURRENT_LANE0_T114(DRIVE_CURRENT_25_200_mA_T114) |
			DRIVE_CURRENT_LANE1_T114(DRIVE_CURRENT_25_200_mA_T114) |
			DRIVE_CURRENT_LANE2_T114(DRIVE_CURRENT_25_200_mA_T114) |
			DRIVE_CURRENT_LANE3_T114(DRIVE_CURRENT_19_200_mA_T114),
		.peak_current = PEAK_CURRENT_LANE0(PEAK_CURRENT_3_000_mA) |
			PEAK_CURRENT_LANE1(PEAK_CURRENT_3_000_mA) |
			PEAK_CURRENT_LANE2(PEAK_CURRENT_3_000_mA) |
			PEAK_CURRENT_LANE3(PEAK_CURRENT_0_800_mA),
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
		unsigned int delta;
		u32 value;

		if (f > 96000)
			delta = 2;
		else if (f > 48000)
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
	u32 value;

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

static inline u32 tegra_hdmi_subpack(const u8 *ptr, size_t size)
{
	u32 value = 0;
	size_t i;

	for (i = size; i > 0; i--)
		value = (value << 8) | ptr[i - 1];

	return value;
}

static void tegra_hdmi_write_infopack(struct tegra_hdmi *hdmi, const void *data,
				      size_t size)
{
	const u8 *ptr = data;
	unsigned long offset;
	size_t i, j;
	u32 value;

	switch (ptr[0]) {
	case HDMI_INFOFRAME_TYPE_AVI:
		offset = HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_HEADER;
		break;

	case HDMI_INFOFRAME_TYPE_AUDIO:
		offset = HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER;
		break;

	case HDMI_INFOFRAME_TYPE_VENDOR:
		offset = HDMI_NV_PDISP_HDMI_GENERIC_HEADER;
		break;

	default:
		dev_err(hdmi->dev, "unsupported infoframe type: %02x\n",
			ptr[0]);
		return;
	}

	value = INFOFRAME_HEADER_TYPE(ptr[0]) |
		INFOFRAME_HEADER_VERSION(ptr[1]) |
		INFOFRAME_HEADER_LEN(ptr[2]);
	tegra_hdmi_writel(hdmi, value, offset);
	offset++;

	/*
	 * Each subpack contains 7 bytes, divided into:
	 * - subpack_low: bytes 0 - 3
	 * - subpack_high: bytes 4 - 6 (with byte 7 padded to 0x00)
	 */
	for (i = 3, j = 0; i < size; i += 7, j += 8) {
		size_t rem = size - i, num = min_t(size_t, rem, 4);

		value = tegra_hdmi_subpack(&ptr[i], num);
		tegra_hdmi_writel(hdmi, value, offset++);

		num = min_t(size_t, rem - num, 3);

		value = tegra_hdmi_subpack(&ptr[i + 4], num);
		tegra_hdmi_writel(hdmi, value, offset++);
	}
}

static void tegra_hdmi_setup_avi_infoframe(struct tegra_hdmi *hdmi,
					   struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	u8 buffer[17];
	ssize_t err;

	if (hdmi->dvi) {
		tegra_hdmi_writel(hdmi, 0,
				  HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
		return;
	}

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to setup AVI infoframe: %zd\n", err);
		return;
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "failed to pack AVI infoframe: %zd\n", err);
		return;
	}

	tegra_hdmi_write_infopack(hdmi, buffer, err);

	tegra_hdmi_writel(hdmi, INFOFRAME_CTRL_ENABLE,
			  HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
}

static void tegra_hdmi_setup_audio_infoframe(struct tegra_hdmi *hdmi)
{
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t err;

	if (hdmi->dvi) {
		tegra_hdmi_writel(hdmi, 0,
				  HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
		return;
	}

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to setup audio infoframe: %zd\n",
			err);
		return;
	}

	frame.channels = 2;

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "failed to pack audio infoframe: %zd\n",
			err);
		return;
	}

	/*
	 * The audio infoframe has only one set of subpack registers, so the
	 * infoframe needs to be truncated. One set of subpack registers can
	 * contain 7 bytes. Including the 3 byte header only the first 10
	 * bytes can be programmed.
	 */
	tegra_hdmi_write_infopack(hdmi, buffer, min_t(size_t, 10, err));

	tegra_hdmi_writel(hdmi, INFOFRAME_CTRL_ENABLE,
			  HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
}

static void tegra_hdmi_setup_stereo_infoframe(struct tegra_hdmi *hdmi)
{
	struct hdmi_vendor_infoframe frame;
	u8 buffer[10];
	ssize_t err;
	u32 value;

	if (!hdmi->stereo) {
		value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
		value &= ~GENERIC_CTRL_ENABLE;
		tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
		return;
	}

	hdmi_vendor_infoframe_init(&frame);
	frame.s3d_struct = HDMI_3D_STRUCTURE_FRAME_PACKING;

	err = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "failed to pack vendor infoframe: %zd\n",
			err);
		return;
	}

	tegra_hdmi_write_infopack(hdmi, buffer, err);

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	value |= GENERIC_CTRL_ENABLE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
}

static void tegra_hdmi_setup_tmds(struct tegra_hdmi *hdmi,
				  const struct tmds_config *tmds)
{
	u32 value;

	tegra_hdmi_writel(hdmi, tmds->pll0, HDMI_NV_PDISP_SOR_PLL0);
	tegra_hdmi_writel(hdmi, tmds->pll1, HDMI_NV_PDISP_SOR_PLL1);
	tegra_hdmi_writel(hdmi, tmds->pe_current, HDMI_NV_PDISP_PE_CURRENT);

	tegra_hdmi_writel(hdmi, tmds->drive_current,
			  HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT);

	value = tegra_hdmi_readl(hdmi, hdmi->config->fuse_override_offset);
	value |= hdmi->config->fuse_override_value;
	tegra_hdmi_writel(hdmi, value, hdmi->config->fuse_override_offset);

	if (hdmi->config->has_sor_io_peak_current)
		tegra_hdmi_writel(hdmi, tmds->peak_current,
				  HDMI_NV_PDISP_SOR_IO_PEAK_CURRENT);
}

static bool tegra_output_is_hdmi(struct tegra_output *output)
{
	struct edid *edid;

	if (!output->connector.edid_blob_ptr)
		return false;

	edid = (struct edid *)output->connector.edid_blob_ptr->data;

	return drm_detect_hdmi_monitor(edid);
}

static const struct drm_connector_funcs tegra_hdmi_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.detect = tegra_output_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = tegra_output_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status
tegra_hdmi_connector_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	struct tegra_output *output = connector_to_output(connector);
	struct tegra_hdmi *hdmi = to_hdmi(output);
	unsigned long pclk = mode->clock * 1000;
	enum drm_mode_status status = MODE_OK;
	struct clk *parent;
	long err;

	parent = clk_get_parent(hdmi->clk_parent);

	err = clk_round_rate(parent, pclk * 4);
	if (err <= 0)
		status = MODE_NOCLOCK;

	return status;
}

static const struct drm_connector_helper_funcs
tegra_hdmi_connector_helper_funcs = {
	.get_modes = tegra_output_connector_get_modes,
	.mode_valid = tegra_hdmi_connector_mode_valid,
	.best_encoder = tegra_output_connector_best_encoder,
};

static const struct drm_encoder_funcs tegra_hdmi_encoder_funcs = {
	.destroy = tegra_output_encoder_destroy,
};

static void tegra_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	u32 value;

	/*
	 * The following accesses registers of the display controller, so make
	 * sure it's only executed when the output is attached to one.
	 */
	if (dc) {
		value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
		value &= ~HDMI_ENABLE;
		tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

		tegra_dc_commit(dc);
	}
}

static void tegra_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	unsigned int h_sync_width, h_front_porch, h_back_porch, i, rekey;
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct device_node *node = output->dev->of_node;
	struct tegra_hdmi *hdmi = to_hdmi(output);
	unsigned int pulse_start, div82, pclk;
	int retries = 1000;
	u32 value;
	int err;

	hdmi->dvi = !tegra_output_is_hdmi(output);

	pclk = mode->clock * 1000;
	h_sync_width = mode->hsync_end - mode->hsync_start;
	h_back_porch = mode->htotal - mode->hsync_end;
	h_front_porch = mode->hsync_start - mode->hdisplay;

	err = clk_set_rate(hdmi->clk, pclk);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to set HDMI clock frequency: %d\n",
			err);
	}

	DRM_DEBUG_KMS("HDMI clock rate: %lu Hz\n", clk_get_rate(hdmi->clk));

	/* power up sequence */
	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_SOR_PLL0);
	value &= ~SOR_PLL_PDBG;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_PLL0);

	usleep_range(10, 20);

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_SOR_PLL0);
	value &= ~SOR_PLL_PWR;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_PLL0);

	tegra_dc_writel(dc, VSYNC_H_POSITION(1),
			DC_DISP_DISP_TIMING_OPTIONS);
	tegra_dc_writel(dc, DITHER_CONTROL_DISABLE | BASE_COLOR_SIZE_888,
			DC_DISP_DISP_COLOR_CONTROL);

	/* video_preamble uses h_pulse2 */
	pulse_start = 1 + h_sync_width + h_back_porch - 10;

	tegra_dc_writel(dc, H_PULSE2_ENABLE, DC_DISP_DISP_SIGNAL_OPTIONS0);

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
	for (i = 0; i < hdmi->config->num_tmds; i++) {
		if (pclk <= hdmi->config->tmds[i].pclk) {
			tegra_hdmi_setup_tmds(hdmi, &hdmi->config->tmds[i]);
			break;
		}
	}

	tegra_hdmi_writel(hdmi,
			  SOR_SEQ_PU_PC(0) |
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

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_SOR_CSTM);
	value &= ~SOR_CSTM_ROTCLK(~0);
	value |= SOR_CSTM_ROTCLK(2);
	value |= SOR_CSTM_PLLDIV;
	value &= ~SOR_CSTM_LVDS_ENABLE;
	value &= ~SOR_CSTM_MODE_MASK;
	value |= SOR_CSTM_MODE_TMDS;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_CSTM);

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

	value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
	value |= HDMI_ENABLE;
	tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

	tegra_dc_commit(dc);

	/* TODO: add HDCP support */
}

static int
tegra_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(conn_state->crtc);
	unsigned long pclk = crtc_state->mode.clock * 1000;
	struct tegra_hdmi *hdmi = to_hdmi(output);
	int err;

	err = tegra_dc_state_setup_clock(dc, crtc_state, hdmi->clk_parent,
					 pclk, 0);
	if (err < 0) {
		dev_err(output->dev, "failed to setup CRTC state: %d\n", err);
		return err;
	}

	return err;
}

static const struct drm_encoder_helper_funcs tegra_hdmi_encoder_helper_funcs = {
	.disable = tegra_hdmi_encoder_disable,
	.enable = tegra_hdmi_encoder_enable,
	.atomic_check = tegra_hdmi_encoder_atomic_check,
};

static int tegra_hdmi_show_regs(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_hdmi *hdmi = node->info_ent->data;
	struct drm_crtc *crtc = hdmi->output.encoder.crtc;
	struct drm_device *drm = node->minor->dev;
	int err = 0;

	drm_modeset_lock_all(drm);

	if (!crtc || !crtc->state->active) {
		err = -EBUSY;
		goto unlock;
	}

#define DUMP_REG(name)						\
	seq_printf(s, "%-56s %#05x %08x\n", #name, name,	\
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
	DUMP_REG(HDMI_NV_PDISP_SOR_IO_PEAK_CURRENT);

#undef DUMP_REG

unlock:
	drm_modeset_unlock_all(drm);
	return err;
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

static void tegra_hdmi_debugfs_exit(struct tegra_hdmi *hdmi)
{
	drm_debugfs_remove_files(hdmi->debugfs_files, ARRAY_SIZE(debugfs_files),
				 hdmi->minor);
	hdmi->minor = NULL;

	kfree(hdmi->debugfs_files);
	hdmi->debugfs_files = NULL;

	debugfs_remove(hdmi->debugfs);
	hdmi->debugfs = NULL;
}

static int tegra_hdmi_init(struct host1x_client *client)
{
	struct drm_device *drm = dev_get_drvdata(client->parent);
	struct tegra_hdmi *hdmi = host1x_client_to_hdmi(client);
	int err;

	hdmi->output.dev = client->dev;

	drm_connector_init(drm, &hdmi->output.connector,
			   &tegra_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(&hdmi->output.connector,
				 &tegra_hdmi_connector_helper_funcs);
	hdmi->output.connector.dpms = DRM_MODE_DPMS_OFF;

	drm_encoder_init(drm, &hdmi->output.encoder, &tegra_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(&hdmi->output.encoder,
			       &tegra_hdmi_encoder_helper_funcs);

	drm_mode_connector_attach_encoder(&hdmi->output.connector,
					  &hdmi->output.encoder);
	drm_connector_register(&hdmi->output.connector);

	err = tegra_output_init(drm, &hdmi->output);
	if (err < 0) {
		dev_err(client->dev, "failed to initialize output: %d\n", err);
		return err;
	}

	hdmi->output.encoder.possible_crtcs = 0x3;

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_hdmi_debugfs_init(hdmi, drm->primary);
		if (err < 0)
			dev_err(client->dev, "debugfs setup failed: %d\n", err);
	}

	err = regulator_enable(hdmi->hdmi);
	if (err < 0) {
		dev_err(client->dev, "failed to enable HDMI regulator: %d\n",
			err);
		return err;
	}

	err = regulator_enable(hdmi->pll);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to enable PLL regulator: %d\n", err);
		return err;
	}

	err = regulator_enable(hdmi->vdd);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to enable VDD regulator: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(hdmi->clk);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to enable clock: %d\n", err);
		return err;
	}

	reset_control_deassert(hdmi->rst);

	return 0;
}

static int tegra_hdmi_exit(struct host1x_client *client)
{
	struct tegra_hdmi *hdmi = host1x_client_to_hdmi(client);

	tegra_output_exit(&hdmi->output);

	reset_control_assert(hdmi->rst);
	clk_disable_unprepare(hdmi->clk);

	regulator_disable(hdmi->vdd);
	regulator_disable(hdmi->pll);
	regulator_disable(hdmi->hdmi);

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		tegra_hdmi_debugfs_exit(hdmi);

	return 0;
}

static const struct host1x_client_ops hdmi_client_ops = {
	.init = tegra_hdmi_init,
	.exit = tegra_hdmi_exit,
};

static const struct tegra_hdmi_config tegra20_hdmi_config = {
	.tmds = tegra20_tmds_config,
	.num_tmds = ARRAY_SIZE(tegra20_tmds_config),
	.fuse_override_offset = HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT,
	.fuse_override_value = 1 << 31,
	.has_sor_io_peak_current = false,
};

static const struct tegra_hdmi_config tegra30_hdmi_config = {
	.tmds = tegra30_tmds_config,
	.num_tmds = ARRAY_SIZE(tegra30_tmds_config),
	.fuse_override_offset = HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT,
	.fuse_override_value = 1 << 31,
	.has_sor_io_peak_current = false,
};

static const struct tegra_hdmi_config tegra114_hdmi_config = {
	.tmds = tegra114_tmds_config,
	.num_tmds = ARRAY_SIZE(tegra114_tmds_config),
	.fuse_override_offset = HDMI_NV_PDISP_SOR_PAD_CTLS0,
	.fuse_override_value = 1 << 31,
	.has_sor_io_peak_current = true,
};

static const struct tegra_hdmi_config tegra124_hdmi_config = {
	.tmds = tegra124_tmds_config,
	.num_tmds = ARRAY_SIZE(tegra124_tmds_config),
	.fuse_override_offset = HDMI_NV_PDISP_SOR_PAD_CTLS0,
	.fuse_override_value = 1 << 31,
	.has_sor_io_peak_current = true,
};

static const struct of_device_id tegra_hdmi_of_match[] = {
	{ .compatible = "nvidia,tegra124-hdmi", .data = &tegra124_hdmi_config },
	{ .compatible = "nvidia,tegra114-hdmi", .data = &tegra114_hdmi_config },
	{ .compatible = "nvidia,tegra30-hdmi", .data = &tegra30_hdmi_config },
	{ .compatible = "nvidia,tegra20-hdmi", .data = &tegra20_hdmi_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_hdmi_of_match);

static int tegra_hdmi_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct tegra_hdmi *hdmi;
	struct resource *regs;
	int err;

	match = of_match_node(tegra_hdmi_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->config = match->data;
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

	hdmi->rst = devm_reset_control_get(&pdev->dev, "hdmi");
	if (IS_ERR(hdmi->rst)) {
		dev_err(&pdev->dev, "failed to get reset\n");
		return PTR_ERR(hdmi->rst);
	}

	hdmi->clk_parent = devm_clk_get(&pdev->dev, "parent");
	if (IS_ERR(hdmi->clk_parent))
		return PTR_ERR(hdmi->clk_parent);

	err = clk_set_parent(hdmi->clk, hdmi->clk_parent);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to setup clocks: %d\n", err);
		return err;
	}

	hdmi->hdmi = devm_regulator_get(&pdev->dev, "hdmi");
	if (IS_ERR(hdmi->hdmi)) {
		dev_err(&pdev->dev, "failed to get HDMI regulator\n");
		return PTR_ERR(hdmi->hdmi);
	}

	hdmi->pll = devm_regulator_get(&pdev->dev, "pll");
	if (IS_ERR(hdmi->pll)) {
		dev_err(&pdev->dev, "failed to get PLL regulator\n");
		return PTR_ERR(hdmi->pll);
	}

	hdmi->vdd = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(hdmi->vdd)) {
		dev_err(&pdev->dev, "failed to get VDD regulator\n");
		return PTR_ERR(hdmi->vdd);
	}

	hdmi->output.dev = &pdev->dev;

	err = tegra_output_probe(&hdmi->output);
	if (err < 0)
		return err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	err = platform_get_irq(pdev, 0);
	if (err < 0)
		return err;

	hdmi->irq = err;

	INIT_LIST_HEAD(&hdmi->client.list);
	hdmi->client.ops = &hdmi_client_ops;
	hdmi->client.dev = &pdev->dev;

	err = host1x_client_register(&hdmi->client);
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
	struct tegra_hdmi *hdmi = platform_get_drvdata(pdev);
	int err;

	err = host1x_client_unregister(&hdmi->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	tegra_output_remove(&hdmi->output);

	clk_disable_unprepare(hdmi->clk_parent);
	clk_disable_unprepare(hdmi->clk);

	return 0;
}

struct platform_driver tegra_hdmi_driver = {
	.driver = {
		.name = "tegra-hdmi",
		.owner = THIS_MODULE,
		.of_match_table = tegra_hdmi_of_match,
	},
	.probe = tegra_hdmi_probe,
	.remove = tegra_hdmi_remove,
};
