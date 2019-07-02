// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/hdmi.h>
#include <linux/math64.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_probe_helper.h>

#include "hda.h"
#include "hdmi.h"
#include "drm.h"
#include "dc.h"
#include "trace.h"

#define HDMI_ELD_BUFFER_SIZE 96

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
	bool has_hda;
	bool has_hbr;
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
	struct tegra_hda_format format;

	unsigned int pixel_clock;
	bool stereo;
	bool dvi;

	struct drm_info_list *debugfs_files;
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
				   unsigned int offset)
{
	u32 value = readl(hdmi->regs + (offset << 2));

	trace_hdmi_readl(hdmi->dev, offset, value);

	return value;
}

static inline void tegra_hdmi_writel(struct tegra_hdmi *hdmi, u32 value,
				     unsigned int offset)
{
	trace_hdmi_writel(hdmi->dev, offset, value);
	writel(value, hdmi->regs + (offset << 2));
}

struct tegra_hdmi_audio_config {
	unsigned int n;
	unsigned int cts;
	unsigned int aval;
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

static int
tegra_hdmi_get_audio_config(unsigned int audio_freq, unsigned int pix_clock,
			    struct tegra_hdmi_audio_config *config)
{
	const unsigned int afreq = 128 * audio_freq;
	const unsigned int min_n = afreq / 1500;
	const unsigned int max_n = afreq / 300;
	const unsigned int ideal_n = afreq / 1000;
	int64_t min_err = (uint64_t)-1 >> 1;
	unsigned int min_delta = -1;
	int n;

	memset(config, 0, sizeof(*config));
	config->n = -1;

	for (n = min_n; n <= max_n; n++) {
		uint64_t cts_f, aval_f;
		unsigned int delta;
		int64_t cts, err;

		/* compute aval in 48.16 fixed point */
		aval_f = ((int64_t)24000000 << 16) * n;
		do_div(aval_f, afreq);
		/* It should round without any rest */
		if (aval_f & 0xFFFF)
			continue;

		/* Compute cts in 48.16 fixed point */
		cts_f = ((int64_t)pix_clock << 16) * n;
		do_div(cts_f, afreq);
		/* Round it to the nearest integer */
		cts = (cts_f & ~0xFFFF) + ((cts_f & BIT(15)) << 1);

		delta = abs(n - ideal_n);

		/* Compute the absolute error */
		err = abs((int64_t)cts_f - cts);
		if (err < min_err || (err == min_err && delta < min_delta)) {
			config->n = n;
			config->cts = cts >> 16;
			config->aval = aval_f >> 16;
			min_delta = delta;
			min_err = err;
		}
	}

	return config->n != -1 ? 0 : -EINVAL;
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

static void tegra_hdmi_write_aval(struct tegra_hdmi *hdmi, u32 value)
{
	static const struct {
		unsigned int sample_rate;
		unsigned int offset;
	} regs[] = {
		{  32000, HDMI_NV_PDISP_SOR_AUDIO_AVAL_0320 },
		{  44100, HDMI_NV_PDISP_SOR_AUDIO_AVAL_0441 },
		{  48000, HDMI_NV_PDISP_SOR_AUDIO_AVAL_0480 },
		{  88200, HDMI_NV_PDISP_SOR_AUDIO_AVAL_0882 },
		{  96000, HDMI_NV_PDISP_SOR_AUDIO_AVAL_0960 },
		{ 176400, HDMI_NV_PDISP_SOR_AUDIO_AVAL_1764 },
		{ 192000, HDMI_NV_PDISP_SOR_AUDIO_AVAL_1920 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (regs[i].sample_rate == hdmi->format.sample_rate) {
			tegra_hdmi_writel(hdmi, value, regs[i].offset);
			break;
		}
	}
}

static int tegra_hdmi_setup_audio(struct tegra_hdmi *hdmi)
{
	struct tegra_hdmi_audio_config config;
	u32 source, value;
	int err;

	switch (hdmi->audio_source) {
	case HDA:
		if (hdmi->config->has_hda)
			source = SOR_AUDIO_CNTRL0_SOURCE_SELECT_HDAL;
		else
			return -EINVAL;

		break;

	case SPDIF:
		if (hdmi->config->has_hda)
			source = SOR_AUDIO_CNTRL0_SOURCE_SELECT_SPDIF;
		else
			source = AUDIO_CNTRL0_SOURCE_SELECT_SPDIF;
		break;

	default:
		if (hdmi->config->has_hda)
			source = SOR_AUDIO_CNTRL0_SOURCE_SELECT_AUTO;
		else
			source = AUDIO_CNTRL0_SOURCE_SELECT_AUTO;
		break;
	}

	/*
	 * Tegra30 and later use a slightly modified version of the register
	 * layout to accomodate for changes related to supporting HDA as the
	 * audio input source for HDMI. The source select field has moved to
	 * the SOR_AUDIO_CNTRL0 register, but the error tolerance and frames
	 * per block fields remain in the AUDIO_CNTRL0 register.
	 */
	if (hdmi->config->has_hda) {
		/*
		 * Inject null samples into the audio FIFO for every frame in
		 * which the codec did not receive any samples. This applies
		 * to stereo LPCM only.
		 *
		 * XXX: This seems to be a remnant of MCP days when this was
		 * used to work around issues with monitors not being able to
		 * play back system startup sounds early. It is possibly not
		 * needed on Linux at all.
		 */
		if (hdmi->format.channels == 2)
			value = SOR_AUDIO_CNTRL0_INJECT_NULLSMPL;
		else
			value = 0;

		value |= source;

		tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_AUDIO_CNTRL0);
	}

	/*
	 * On Tegra20, HDA is not a supported audio source and the source
	 * select field is part of the AUDIO_CNTRL0 register.
	 */
	value = AUDIO_CNTRL0_FRAMES_PER_BLOCK(0xc0) |
		AUDIO_CNTRL0_ERROR_TOLERANCE(6);

	if (!hdmi->config->has_hda)
		value |= source;

	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_AUDIO_CNTRL0);

	/*
	 * Advertise support for High Bit-Rate on Tegra114 and later.
	 */
	if (hdmi->config->has_hbr) {
		value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_SOR_AUDIO_SPARE0);
		value |= SOR_AUDIO_SPARE0_HBR_ENABLE;
		tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_AUDIO_SPARE0);
	}

	err = tegra_hdmi_get_audio_config(hdmi->format.sample_rate,
					  hdmi->pixel_clock, &config);
	if (err < 0) {
		dev_err(hdmi->dev,
			"cannot set audio to %u Hz at %u Hz pixel clock\n",
			hdmi->format.sample_rate, hdmi->pixel_clock);
		return err;
	}

	dev_dbg(hdmi->dev, "audio: pixclk=%u, n=%u, cts=%u, aval=%u\n",
		hdmi->pixel_clock, config.n, config.cts, config.aval);

	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_HDMI_ACR_CTRL);

	value = AUDIO_N_RESETF | AUDIO_N_GENERATE_ALTERNATE |
		AUDIO_N_VALUE(config.n - 1);
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_AUDIO_N);

	tegra_hdmi_writel(hdmi, ACR_SUBPACK_N(config.n) | ACR_ENABLE,
			  HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_HIGH);

	tegra_hdmi_writel(hdmi, ACR_SUBPACK_CTS(config.cts),
			  HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW);

	value = SPARE_HW_CTS | SPARE_FORCE_SW_CTS | SPARE_CTS_RESET_VAL(1);
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_SPARE);

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_AUDIO_N);
	value &= ~AUDIO_N_RESETF;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_AUDIO_N);

	if (hdmi->config->has_hda)
		tegra_hdmi_write_aval(hdmi, config.aval);

	tegra_hdmi_setup_audio_fs_tables(hdmi);

	return 0;
}

static void tegra_hdmi_disable_audio(struct tegra_hdmi *hdmi)
{
	u32 value;

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	value &= ~GENERIC_CTRL_AUDIO;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
}

static void tegra_hdmi_enable_audio(struct tegra_hdmi *hdmi)
{
	u32 value;

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	value |= GENERIC_CTRL_AUDIO;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
}

static void tegra_hdmi_write_eld(struct tegra_hdmi *hdmi)
{
	size_t length = drm_eld_size(hdmi->output.connector.eld), i;
	u32 value;

	for (i = 0; i < length; i++)
		tegra_hdmi_writel(hdmi, i << 8 | hdmi->output.connector.eld[i],
				  HDMI_NV_PDISP_SOR_AUDIO_HDA_ELD_BUFWR);

	/*
	 * The HDA codec will always report an ELD buffer size of 96 bytes and
	 * the HDA codec driver will check that each byte read from the buffer
	 * is valid. Therefore every byte must be written, even if no 96 bytes
	 * were parsed from EDID.
	 */
	for (i = length; i < HDMI_ELD_BUFFER_SIZE; i++)
		tegra_hdmi_writel(hdmi, i << 8 | 0,
				  HDMI_NV_PDISP_SOR_AUDIO_HDA_ELD_BUFWR);

	value = SOR_AUDIO_HDA_PRESENSE_VALID | SOR_AUDIO_HDA_PRESENSE_PRESENT;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_SOR_AUDIO_HDA_PRESENSE);
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

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame,
						       &hdmi->output.connector, mode);
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
}

static void tegra_hdmi_disable_avi_infoframe(struct tegra_hdmi *hdmi)
{
	u32 value;

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
	value &= ~INFOFRAME_CTRL_ENABLE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
}

static void tegra_hdmi_enable_avi_infoframe(struct tegra_hdmi *hdmi)
{
	u32 value;

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
	value |= INFOFRAME_CTRL_ENABLE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
}

static void tegra_hdmi_setup_audio_infoframe(struct tegra_hdmi *hdmi)
{
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t err;

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		dev_err(hdmi->dev, "failed to setup audio infoframe: %zd\n",
			err);
		return;
	}

	frame.channels = hdmi->format.channels;

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
}

static void tegra_hdmi_disable_audio_infoframe(struct tegra_hdmi *hdmi)
{
	u32 value;

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
	value &= ~INFOFRAME_CTRL_ENABLE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
}

static void tegra_hdmi_enable_audio_infoframe(struct tegra_hdmi *hdmi)
{
	u32 value;

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
	value |= INFOFRAME_CTRL_ENABLE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
}

static void tegra_hdmi_setup_stereo_infoframe(struct tegra_hdmi *hdmi)
{
	struct hdmi_vendor_infoframe frame;
	u8 buffer[10];
	ssize_t err;

	hdmi_vendor_infoframe_init(&frame);
	frame.s3d_struct = HDMI_3D_STRUCTURE_FRAME_PACKING;

	err = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "failed to pack vendor infoframe: %zd\n",
			err);
		return;
	}

	tegra_hdmi_write_infopack(hdmi, buffer, err);
}

static void tegra_hdmi_disable_stereo_infoframe(struct tegra_hdmi *hdmi)
{
	u32 value;

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	value &= ~GENERIC_CTRL_ENABLE;
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
}

static void tegra_hdmi_enable_stereo_infoframe(struct tegra_hdmi *hdmi)
{
	u32 value;

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

static enum drm_connector_status
tegra_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct tegra_output *output = connector_to_output(connector);
	struct tegra_hdmi *hdmi = to_hdmi(output);
	enum drm_connector_status status;

	status = tegra_output_connector_detect(connector, force);
	if (status == connector_status_connected)
		return status;

	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_SOR_AUDIO_HDA_PRESENSE);
	return status;
}

#define DEBUGFS_REG32(_name) { .name = #_name, .offset = _name }

static const struct debugfs_reg32 tegra_hdmi_regs[] = {
	DEBUGFS_REG32(HDMI_CTXSW),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_STATE0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_STATE1),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_STATE2),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_AN_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_AN_LSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_CN_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_CN_LSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_AKSV_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_AKSV_LSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_BKSV_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_BKSV_LSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_CKSV_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_CKSV_LSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_DKSV_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_DKSV_LSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_CMODE),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_MPRIME_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_MPRIME_LSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_SPRIME_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_SPRIME_LSB2),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_SPRIME_LSB1),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_RI),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_CS_MSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_RG_HDCP_CS_LSB),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_EMU0),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_EMU_RDATA0),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_EMU1),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_EMU2),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_STATUS),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_STATUS),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_HEADER),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_STATUS),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_HEADER),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_LOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_HIGH),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_VSYNC_KEEPOUT),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_VSYNC_WINDOW),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GCP_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GCP_STATUS),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_GCP_SUBPACK),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_CHANNEL_STATUS1),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_CHANNEL_STATUS2),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_EMU0),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_EMU1),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_EMU1_RDATA),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_SPARE),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_SPDIF_CHN_STATUS1),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_SPDIF_CHN_STATUS2),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDMI_HDCPRIF_ROM_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_CAP),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_PWR),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_TEST),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_PLL0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_PLL1),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_PLL2),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_CSTM),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_LVDS),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_CRCA),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_CRCB),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_BLANK),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_CTL),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(0)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(1)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(2)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(3)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(4)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(5)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(6)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(7)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(8)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(9)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(10)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(11)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(12)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(13)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(14)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_SEQ_INST(15)),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_VCRCA0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_VCRCA1),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_CCRCA0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_CCRCA1),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_EDATAA0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_EDATAA1),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_COUNTA0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_COUNTA1),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_DEBUGA0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_DEBUGA1),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_TRIG),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_MSCHECK),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_DEBUG0),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_DEBUG1),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_DEBUG2),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_FS(0)),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_FS(1)),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_FS(2)),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_FS(3)),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_FS(4)),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_FS(5)),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_FS(6)),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_PULSE_WIDTH),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_THRESHOLD),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_CNTRL0),
	DEBUGFS_REG32(HDMI_NV_PDISP_AUDIO_N),
	DEBUGFS_REG32(HDMI_NV_PDISP_HDCPRIF_ROM_TIMING),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_REFCLK),
	DEBUGFS_REG32(HDMI_NV_PDISP_CRC_CONTROL),
	DEBUGFS_REG32(HDMI_NV_PDISP_INPUT_CONTROL),
	DEBUGFS_REG32(HDMI_NV_PDISP_SCRATCH),
	DEBUGFS_REG32(HDMI_NV_PDISP_PE_CURRENT),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_CTRL),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_DEBUG0),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_DEBUG1),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_DEBUG2),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_HDCP_KEY_0),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_HDCP_KEY_1),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_HDCP_KEY_2),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_HDCP_KEY_3),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_HDCP_KEY_TRIG),
	DEBUGFS_REG32(HDMI_NV_PDISP_KEY_SKEY_INDEX),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_AUDIO_CNTRL0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_AUDIO_SPARE0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_AUDIO_HDA_CODEC_SCRATCH0),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_AUDIO_HDA_CODEC_SCRATCH1),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_AUDIO_HDA_ELD_BUFWR),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_AUDIO_HDA_PRESENSE),
	DEBUGFS_REG32(HDMI_NV_PDISP_INT_STATUS),
	DEBUGFS_REG32(HDMI_NV_PDISP_INT_MASK),
	DEBUGFS_REG32(HDMI_NV_PDISP_INT_ENABLE),
	DEBUGFS_REG32(HDMI_NV_PDISP_SOR_IO_PEAK_CURRENT),
};

static int tegra_hdmi_show_regs(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_hdmi *hdmi = node->info_ent->data;
	struct drm_crtc *crtc = hdmi->output.encoder.crtc;
	struct drm_device *drm = node->minor->dev;
	unsigned int i;
	int err = 0;

	drm_modeset_lock_all(drm);

	if (!crtc || !crtc->state->active) {
		err = -EBUSY;
		goto unlock;
	}

	for (i = 0; i < ARRAY_SIZE(tegra_hdmi_regs); i++) {
		unsigned int offset = tegra_hdmi_regs[i].offset;

		seq_printf(s, "%-56s %#05x %08x\n", tegra_hdmi_regs[i].name,
			   offset, tegra_hdmi_readl(hdmi, offset));
	}

unlock:
	drm_modeset_unlock_all(drm);
	return err;
}

static struct drm_info_list debugfs_files[] = {
	{ "regs", tegra_hdmi_show_regs, 0, NULL },
};

static int tegra_hdmi_late_register(struct drm_connector *connector)
{
	struct tegra_output *output = connector_to_output(connector);
	unsigned int i, count = ARRAY_SIZE(debugfs_files);
	struct drm_minor *minor = connector->dev->primary;
	struct dentry *root = connector->debugfs_entry;
	struct tegra_hdmi *hdmi = to_hdmi(output);
	int err;

	hdmi->debugfs_files = kmemdup(debugfs_files, sizeof(debugfs_files),
				      GFP_KERNEL);
	if (!hdmi->debugfs_files)
		return -ENOMEM;

	for (i = 0; i < count; i++)
		hdmi->debugfs_files[i].data = hdmi;

	err = drm_debugfs_create_files(hdmi->debugfs_files, count, root, minor);
	if (err < 0)
		goto free;

	return 0;

free:
	kfree(hdmi->debugfs_files);
	hdmi->debugfs_files = NULL;

	return err;
}

static void tegra_hdmi_early_unregister(struct drm_connector *connector)
{
	struct tegra_output *output = connector_to_output(connector);
	struct drm_minor *minor = connector->dev->primary;
	unsigned int count = ARRAY_SIZE(debugfs_files);
	struct tegra_hdmi *hdmi = to_hdmi(output);

	drm_debugfs_remove_files(hdmi->debugfs_files, count, minor);
	kfree(hdmi->debugfs_files);
	hdmi->debugfs_files = NULL;
}

static const struct drm_connector_funcs tegra_hdmi_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = tegra_hdmi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = tegra_output_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.late_register = tegra_hdmi_late_register,
	.early_unregister = tegra_hdmi_early_unregister,
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
};

static const struct drm_encoder_funcs tegra_hdmi_encoder_funcs = {
	.destroy = tegra_output_encoder_destroy,
};

static void tegra_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct tegra_hdmi *hdmi = to_hdmi(output);
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

	if (!hdmi->dvi) {
		if (hdmi->stereo)
			tegra_hdmi_disable_stereo_infoframe(hdmi);

		tegra_hdmi_disable_audio_infoframe(hdmi);
		tegra_hdmi_disable_avi_infoframe(hdmi);
		tegra_hdmi_disable_audio(hdmi);
	}

	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_INT_ENABLE);
	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_INT_MASK);

	pm_runtime_put(hdmi->dev);
}

static void tegra_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	unsigned int h_sync_width, h_front_porch, h_back_porch, i, rekey;
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct tegra_hdmi *hdmi = to_hdmi(output);
	unsigned int pulse_start, div82;
	int retries = 1000;
	u32 value;
	int err;

	pm_runtime_get_sync(hdmi->dev);

	/*
	 * Enable and unmask the HDA codec SCRATCH0 register interrupt. This
	 * is used for interoperability between the HDA codec driver and the
	 * HDMI driver.
	 */
	tegra_hdmi_writel(hdmi, INT_CODEC_SCRATCH0, HDMI_NV_PDISP_INT_ENABLE);
	tegra_hdmi_writel(hdmi, INT_CODEC_SCRATCH0, HDMI_NV_PDISP_INT_MASK);

	hdmi->pixel_clock = mode->clock * 1000;
	h_sync_width = mode->hsync_end - mode->hsync_start;
	h_back_porch = mode->htotal - mode->hsync_end;
	h_front_porch = mode->hsync_start - mode->hdisplay;

	err = clk_set_rate(hdmi->clk, hdmi->pixel_clock);
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

	hdmi->dvi = !tegra_output_is_hdmi(output);
	if (!hdmi->dvi) {
		/*
		 * Make sure that the audio format has been configured before
		 * enabling audio, otherwise we may try to divide by zero.
		*/
		if (hdmi->format.sample_rate > 0) {
			err = tegra_hdmi_setup_audio(hdmi);
			if (err < 0)
				hdmi->dvi = true;
		}
	}

	if (hdmi->config->has_hda)
		tegra_hdmi_write_eld(hdmi);

	rekey = HDMI_REKEY_DEFAULT;
	value = HDMI_CTRL_REKEY(rekey);
	value |= HDMI_CTRL_MAX_AC_PACKET((h_sync_width + h_back_porch +
					  h_front_porch - rekey - 18) / 32);

	if (!hdmi->dvi)
		value |= HDMI_CTRL_ENABLE;

	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_HDMI_CTRL);

	if (!hdmi->dvi) {
		tegra_hdmi_setup_avi_infoframe(hdmi, mode);
		tegra_hdmi_setup_audio_infoframe(hdmi);

		if (hdmi->stereo)
			tegra_hdmi_setup_stereo_infoframe(hdmi);
	}

	/* TMDS CONFIG */
	for (i = 0; i < hdmi->config->num_tmds; i++) {
		if (hdmi->pixel_clock <= hdmi->config->tmds[i].pclk) {
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

	if (!hdmi->dvi) {
		tegra_hdmi_enable_avi_infoframe(hdmi);
		tegra_hdmi_enable_audio_infoframe(hdmi);
		tegra_hdmi_enable_audio(hdmi);

		if (hdmi->stereo)
			tegra_hdmi_enable_stereo_infoframe(hdmi);
	}

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
			 DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(&hdmi->output.encoder,
			       &tegra_hdmi_encoder_helper_funcs);

	drm_connector_attach_encoder(&hdmi->output.connector,
					  &hdmi->output.encoder);
	drm_connector_register(&hdmi->output.connector);

	err = tegra_output_init(drm, &hdmi->output);
	if (err < 0) {
		dev_err(client->dev, "failed to initialize output: %d\n", err);
		return err;
	}

	hdmi->output.encoder.possible_crtcs = 0x3;

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

	return 0;
}

static int tegra_hdmi_exit(struct host1x_client *client)
{
	struct tegra_hdmi *hdmi = host1x_client_to_hdmi(client);

	tegra_output_exit(&hdmi->output);

	regulator_disable(hdmi->vdd);
	regulator_disable(hdmi->pll);
	regulator_disable(hdmi->hdmi);

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
	.has_hda = false,
	.has_hbr = false,
};

static const struct tegra_hdmi_config tegra30_hdmi_config = {
	.tmds = tegra30_tmds_config,
	.num_tmds = ARRAY_SIZE(tegra30_tmds_config),
	.fuse_override_offset = HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT,
	.fuse_override_value = 1 << 31,
	.has_sor_io_peak_current = false,
	.has_hda = true,
	.has_hbr = false,
};

static const struct tegra_hdmi_config tegra114_hdmi_config = {
	.tmds = tegra114_tmds_config,
	.num_tmds = ARRAY_SIZE(tegra114_tmds_config),
	.fuse_override_offset = HDMI_NV_PDISP_SOR_PAD_CTLS0,
	.fuse_override_value = 1 << 31,
	.has_sor_io_peak_current = true,
	.has_hda = true,
	.has_hbr = true,
};

static const struct tegra_hdmi_config tegra124_hdmi_config = {
	.tmds = tegra124_tmds_config,
	.num_tmds = ARRAY_SIZE(tegra124_tmds_config),
	.fuse_override_offset = HDMI_NV_PDISP_SOR_PAD_CTLS0,
	.fuse_override_value = 1 << 31,
	.has_sor_io_peak_current = true,
	.has_hda = true,
	.has_hbr = true,
};

static const struct of_device_id tegra_hdmi_of_match[] = {
	{ .compatible = "nvidia,tegra124-hdmi", .data = &tegra124_hdmi_config },
	{ .compatible = "nvidia,tegra114-hdmi", .data = &tegra114_hdmi_config },
	{ .compatible = "nvidia,tegra30-hdmi", .data = &tegra30_hdmi_config },
	{ .compatible = "nvidia,tegra20-hdmi", .data = &tegra20_hdmi_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_hdmi_of_match);

static irqreturn_t tegra_hdmi_irq(int irq, void *data)
{
	struct tegra_hdmi *hdmi = data;
	u32 value;
	int err;

	value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_INT_STATUS);
	tegra_hdmi_writel(hdmi, value, HDMI_NV_PDISP_INT_STATUS);

	if (value & INT_CODEC_SCRATCH0) {
		unsigned int format;
		u32 value;

		value = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_SOR_AUDIO_HDA_CODEC_SCRATCH0);

		if (value & SOR_AUDIO_HDA_CODEC_SCRATCH0_VALID) {
			format = value & SOR_AUDIO_HDA_CODEC_SCRATCH0_FMT_MASK;

			tegra_hda_parse_format(format, &hdmi->format);

			err = tegra_hdmi_setup_audio(hdmi);
			if (err < 0) {
				tegra_hdmi_disable_audio_infoframe(hdmi);
				tegra_hdmi_disable_audio(hdmi);
			} else {
				tegra_hdmi_setup_audio_infoframe(hdmi);
				tegra_hdmi_enable_audio_infoframe(hdmi);
				tegra_hdmi_enable_audio(hdmi);
			}
		} else {
			tegra_hdmi_disable_audio_infoframe(hdmi);
			tegra_hdmi_disable_audio(hdmi);
		}
	}

	return IRQ_HANDLED;
}

static int tegra_hdmi_probe(struct platform_device *pdev)
{
	struct tegra_hdmi *hdmi;
	struct resource *regs;
	int err;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->config = of_device_get_match_data(&pdev->dev);
	hdmi->dev = &pdev->dev;

	hdmi->audio_source = AUTO;
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

	err = devm_request_irq(hdmi->dev, hdmi->irq, tegra_hdmi_irq, 0,
			       dev_name(hdmi->dev), hdmi);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ#%u: %d\n",
			hdmi->irq, err);
		return err;
	}

	platform_set_drvdata(pdev, hdmi);
	pm_runtime_enable(&pdev->dev);

	INIT_LIST_HEAD(&hdmi->client.list);
	hdmi->client.ops = &hdmi_client_ops;
	hdmi->client.dev = &pdev->dev;

	err = host1x_client_register(&hdmi->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		return err;
	}

	return 0;
}

static int tegra_hdmi_remove(struct platform_device *pdev)
{
	struct tegra_hdmi *hdmi = platform_get_drvdata(pdev);
	int err;

	pm_runtime_disable(&pdev->dev);

	err = host1x_client_unregister(&hdmi->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	tegra_output_remove(&hdmi->output);

	return 0;
}

#ifdef CONFIG_PM
static int tegra_hdmi_suspend(struct device *dev)
{
	struct tegra_hdmi *hdmi = dev_get_drvdata(dev);
	int err;

	err = reset_control_assert(hdmi->rst);
	if (err < 0) {
		dev_err(dev, "failed to assert reset: %d\n", err);
		return err;
	}

	usleep_range(1000, 2000);

	clk_disable_unprepare(hdmi->clk);

	return 0;
}

static int tegra_hdmi_resume(struct device *dev)
{
	struct tegra_hdmi *hdmi = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(hdmi->clk);
	if (err < 0) {
		dev_err(dev, "failed to enable clock: %d\n", err);
		return err;
	}

	usleep_range(1000, 2000);

	err = reset_control_deassert(hdmi->rst);
	if (err < 0) {
		dev_err(dev, "failed to deassert reset: %d\n", err);
		clk_disable_unprepare(hdmi->clk);
		return err;
	}

	return 0;
}
#endif

static const struct dev_pm_ops tegra_hdmi_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_hdmi_suspend, tegra_hdmi_resume, NULL)
};

struct platform_driver tegra_hdmi_driver = {
	.driver = {
		.name = "tegra-hdmi",
		.of_match_table = tegra_hdmi_of_match,
		.pm = &tegra_hdmi_pm_ops,
	},
	.probe = tegra_hdmi_probe,
	.remove = tegra_hdmi_remove,
};
