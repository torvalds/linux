// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 NVIDIA Corporation
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include <soc/tegra/pmc.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_scdc_helper.h>

#include "dc.h"
#include "drm.h"
#include "hda.h"
#include "sor.h"
#include "trace.h"

#define SOR_REKEY 0x38

struct tegra_sor_hdmi_settings {
	unsigned long frequency;

	u8 vcocap;
	u8 filter;
	u8 ichpmp;
	u8 loadadj;
	u8 tmds_termadj;
	u8 tx_pu_value;
	u8 bg_temp_coef;
	u8 bg_vref_level;
	u8 avdd10_level;
	u8 avdd14_level;
	u8 sparepll;

	u8 drive_current[4];
	u8 preemphasis[4];
};

#if 1
static const struct tegra_sor_hdmi_settings tegra210_sor_hdmi_defaults[] = {
	{
		.frequency = 54000000,
		.vcocap = 0x0,
		.filter = 0x0,
		.ichpmp = 0x1,
		.loadadj = 0x3,
		.tmds_termadj = 0x9,
		.tx_pu_value = 0x10,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0x8,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x33, 0x3a, 0x3a, 0x3a },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 75000000,
		.vcocap = 0x3,
		.filter = 0x0,
		.ichpmp = 0x1,
		.loadadj = 0x3,
		.tmds_termadj = 0x9,
		.tx_pu_value = 0x40,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0x8,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x33, 0x3a, 0x3a, 0x3a },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 150000000,
		.vcocap = 0x3,
		.filter = 0x0,
		.ichpmp = 0x1,
		.loadadj = 0x3,
		.tmds_termadj = 0x9,
		.tx_pu_value = 0x66,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0x8,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x33, 0x3a, 0x3a, 0x3a },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 300000000,
		.vcocap = 0x3,
		.filter = 0x0,
		.ichpmp = 0x1,
		.loadadj = 0x3,
		.tmds_termadj = 0x9,
		.tx_pu_value = 0x66,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0xa,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x33, 0x3f, 0x3f, 0x3f },
		.preemphasis = { 0x00, 0x17, 0x17, 0x17 },
	}, {
		.frequency = 600000000,
		.vcocap = 0x3,
		.filter = 0x0,
		.ichpmp = 0x1,
		.loadadj = 0x3,
		.tmds_termadj = 0x9,
		.tx_pu_value = 0x66,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0x8,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x33, 0x3f, 0x3f, 0x3f },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	},
};
#else
static const struct tegra_sor_hdmi_settings tegra210_sor_hdmi_defaults[] = {
	{
		.frequency = 75000000,
		.vcocap = 0x3,
		.filter = 0x0,
		.ichpmp = 0x1,
		.loadadj = 0x3,
		.tmds_termadj = 0x9,
		.tx_pu_value = 0x40,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0x8,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x29, 0x29, 0x29, 0x29 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 150000000,
		.vcocap = 0x3,
		.filter = 0x0,
		.ichpmp = 0x1,
		.loadadj = 0x3,
		.tmds_termadj = 0x9,
		.tx_pu_value = 0x66,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0x8,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x30, 0x37, 0x37, 0x37 },
		.preemphasis = { 0x01, 0x02, 0x02, 0x02 },
	}, {
		.frequency = 300000000,
		.vcocap = 0x3,
		.filter = 0x0,
		.ichpmp = 0x6,
		.loadadj = 0x3,
		.tmds_termadj = 0x9,
		.tx_pu_value = 0x66,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0xf,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x30, 0x37, 0x37, 0x37 },
		.preemphasis = { 0x10, 0x3e, 0x3e, 0x3e },
	}, {
		.frequency = 600000000,
		.vcocap = 0x3,
		.filter = 0x0,
		.ichpmp = 0xa,
		.loadadj = 0x3,
		.tmds_termadj = 0xb,
		.tx_pu_value = 0x66,
		.bg_temp_coef = 0x3,
		.bg_vref_level = 0xe,
		.avdd10_level = 0x4,
		.avdd14_level = 0x4,
		.sparepll = 0x0,
		.drive_current = { 0x35, 0x3e, 0x3e, 0x3e },
		.preemphasis = { 0x02, 0x3f, 0x3f, 0x3f },
	},
};
#endif

static const struct tegra_sor_hdmi_settings tegra186_sor_hdmi_defaults[] = {
	{
		.frequency = 54000000,
		.vcocap = 0,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 0xf,
		.tx_pu_value = 0,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x54,
		.drive_current = { 0x3a, 0x3a, 0x3a, 0x33 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 75000000,
		.vcocap = 1,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 0xf,
		.tx_pu_value = 0,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x44,
		.drive_current = { 0x3a, 0x3a, 0x3a, 0x33 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 150000000,
		.vcocap = 3,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 15,
		.tx_pu_value = 0x66 /* 0 */,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x00, /* 0x34 */
		.drive_current = { 0x3a, 0x3a, 0x3a, 0x37 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 300000000,
		.vcocap = 3,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 15,
		.tx_pu_value = 64,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x34,
		.drive_current = { 0x3d, 0x3d, 0x3d, 0x33 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 600000000,
		.vcocap = 3,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 12,
		.tx_pu_value = 96,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x34,
		.drive_current = { 0x3d, 0x3d, 0x3d, 0x33 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}
};

static const struct tegra_sor_hdmi_settings tegra194_sor_hdmi_defaults[] = {
	{
		.frequency = 54000000,
		.vcocap = 0,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 0xf,
		.tx_pu_value = 0,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x54,
		.drive_current = { 0x3a, 0x3a, 0x3a, 0x33 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 75000000,
		.vcocap = 1,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 0xf,
		.tx_pu_value = 0,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x44,
		.drive_current = { 0x3a, 0x3a, 0x3a, 0x33 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 150000000,
		.vcocap = 3,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 15,
		.tx_pu_value = 0x66 /* 0 */,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x00, /* 0x34 */
		.drive_current = { 0x3a, 0x3a, 0x3a, 0x37 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 300000000,
		.vcocap = 3,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 15,
		.tx_pu_value = 64,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x34,
		.drive_current = { 0x3d, 0x3d, 0x3d, 0x33 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}, {
		.frequency = 600000000,
		.vcocap = 3,
		.filter = 5,
		.ichpmp = 5,
		.loadadj = 3,
		.tmds_termadj = 12,
		.tx_pu_value = 96,
		.bg_temp_coef = 3,
		.bg_vref_level = 8,
		.avdd10_level = 4,
		.avdd14_level = 4,
		.sparepll = 0x34,
		.drive_current = { 0x3d, 0x3d, 0x3d, 0x33 },
		.preemphasis = { 0x00, 0x00, 0x00, 0x00 },
	}
};

struct tegra_sor_regs {
	unsigned int head_state0;
	unsigned int head_state1;
	unsigned int head_state2;
	unsigned int head_state3;
	unsigned int head_state4;
	unsigned int head_state5;
	unsigned int pll0;
	unsigned int pll1;
	unsigned int pll2;
	unsigned int pll3;
	unsigned int dp_padctl0;
	unsigned int dp_padctl2;
};

struct tegra_sor_soc {
	bool supports_edp;
	bool supports_lvds;
	bool supports_hdmi;
	bool supports_dp;

	const struct tegra_sor_regs *regs;
	bool has_nvdisplay;

	const struct tegra_sor_hdmi_settings *settings;
	unsigned int num_settings;

	const u8 *xbar_cfg;
};

struct tegra_sor;

struct tegra_sor_ops {
	const char *name;
	int (*probe)(struct tegra_sor *sor);
	int (*remove)(struct tegra_sor *sor);
};

struct tegra_sor {
	struct host1x_client client;
	struct tegra_output output;
	struct device *dev;

	const struct tegra_sor_soc *soc;
	void __iomem *regs;
	unsigned int index;
	unsigned int irq;

	struct reset_control *rst;
	struct clk *clk_parent;
	struct clk *clk_safe;
	struct clk *clk_out;
	struct clk *clk_pad;
	struct clk *clk_dp;
	struct clk *clk;

	u8 xbar_cfg[5];

	struct drm_dp_aux *aux;

	struct drm_info_list *debugfs_files;

	const struct tegra_sor_ops *ops;
	enum tegra_io_pad pad;

	/* for HDMI 2.0 */
	struct tegra_sor_hdmi_settings *settings;
	unsigned int num_settings;

	struct regulator *avdd_io_supply;
	struct regulator *vdd_pll_supply;
	struct regulator *hdmi_supply;

	struct delayed_work scdc;
	bool scdc_enabled;

	struct tegra_hda_format format;
};

struct tegra_sor_state {
	struct drm_connector_state base;

	unsigned int link_speed;
	unsigned long pclk;
	unsigned int bpc;
};

static inline struct tegra_sor_state *
to_sor_state(struct drm_connector_state *state)
{
	return container_of(state, struct tegra_sor_state, base);
}

struct tegra_sor_config {
	u32 bits_per_pixel;

	u32 active_polarity;
	u32 active_count;
	u32 tu_size;
	u32 active_frac;
	u32 watermark;

	u32 hblank_symbols;
	u32 vblank_symbols;
};

static inline struct tegra_sor *
host1x_client_to_sor(struct host1x_client *client)
{
	return container_of(client, struct tegra_sor, client);
}

static inline struct tegra_sor *to_sor(struct tegra_output *output)
{
	return container_of(output, struct tegra_sor, output);
}

static inline u32 tegra_sor_readl(struct tegra_sor *sor, unsigned int offset)
{
	u32 value = readl(sor->regs + (offset << 2));

	trace_sor_readl(sor->dev, offset, value);

	return value;
}

static inline void tegra_sor_writel(struct tegra_sor *sor, u32 value,
				    unsigned int offset)
{
	trace_sor_writel(sor->dev, offset, value);
	writel(value, sor->regs + (offset << 2));
}

static int tegra_sor_set_parent_clock(struct tegra_sor *sor, struct clk *parent)
{
	int err;

	clk_disable_unprepare(sor->clk);

	err = clk_set_parent(sor->clk_out, parent);
	if (err < 0)
		return err;

	err = clk_prepare_enable(sor->clk);
	if (err < 0)
		return err;

	return 0;
}

struct tegra_clk_sor_pad {
	struct clk_hw hw;
	struct tegra_sor *sor;
};

static inline struct tegra_clk_sor_pad *to_pad(struct clk_hw *hw)
{
	return container_of(hw, struct tegra_clk_sor_pad, hw);
}

static const char * const tegra_clk_sor_pad_parents[] = {
	"pll_d2_out0", "pll_dp"
};

static int tegra_clk_sor_pad_set_parent(struct clk_hw *hw, u8 index)
{
	struct tegra_clk_sor_pad *pad = to_pad(hw);
	struct tegra_sor *sor = pad->sor;
	u32 value;

	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_CLK_SEL_MASK;

	switch (index) {
	case 0:
		value |= SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_PCLK;
		break;

	case 1:
		value |= SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_DPCLK;
		break;
	}

	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	return 0;
}

static u8 tegra_clk_sor_pad_get_parent(struct clk_hw *hw)
{
	struct tegra_clk_sor_pad *pad = to_pad(hw);
	struct tegra_sor *sor = pad->sor;
	u8 parent = U8_MAX;
	u32 value;

	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);

	switch (value & SOR_CLK_CNTRL_DP_CLK_SEL_MASK) {
	case SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_PCLK:
	case SOR_CLK_CNTRL_DP_CLK_SEL_DIFF_PCLK:
		parent = 0;
		break;

	case SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_DPCLK:
	case SOR_CLK_CNTRL_DP_CLK_SEL_DIFF_DPCLK:
		parent = 1;
		break;
	}

	return parent;
}

static const struct clk_ops tegra_clk_sor_pad_ops = {
	.set_parent = tegra_clk_sor_pad_set_parent,
	.get_parent = tegra_clk_sor_pad_get_parent,
};

static struct clk *tegra_clk_sor_pad_register(struct tegra_sor *sor,
					      const char *name)
{
	struct tegra_clk_sor_pad *pad;
	struct clk_init_data init;
	struct clk *clk;

	pad = devm_kzalloc(sor->dev, sizeof(*pad), GFP_KERNEL);
	if (!pad)
		return ERR_PTR(-ENOMEM);

	pad->sor = sor;

	init.name = name;
	init.flags = 0;
	init.parent_names = tegra_clk_sor_pad_parents;
	init.num_parents = ARRAY_SIZE(tegra_clk_sor_pad_parents);
	init.ops = &tegra_clk_sor_pad_ops;

	pad->hw.init = &init;

	clk = devm_clk_register(sor->dev, &pad->hw);

	return clk;
}

static int tegra_sor_dp_train_fast(struct tegra_sor *sor,
				   struct drm_dp_link *link)
{
	unsigned int i;
	u8 pattern;
	u32 value;
	int err;

	/* setup lane parameters */
	value = SOR_LANE_DRIVE_CURRENT_LANE3(0x40) |
		SOR_LANE_DRIVE_CURRENT_LANE2(0x40) |
		SOR_LANE_DRIVE_CURRENT_LANE1(0x40) |
		SOR_LANE_DRIVE_CURRENT_LANE0(0x40);
	tegra_sor_writel(sor, value, SOR_LANE_DRIVE_CURRENT0);

	value = SOR_LANE_PREEMPHASIS_LANE3(0x0f) |
		SOR_LANE_PREEMPHASIS_LANE2(0x0f) |
		SOR_LANE_PREEMPHASIS_LANE1(0x0f) |
		SOR_LANE_PREEMPHASIS_LANE0(0x0f);
	tegra_sor_writel(sor, value, SOR_LANE_PREEMPHASIS0);

	value = SOR_LANE_POSTCURSOR_LANE3(0x00) |
		SOR_LANE_POSTCURSOR_LANE2(0x00) |
		SOR_LANE_POSTCURSOR_LANE1(0x00) |
		SOR_LANE_POSTCURSOR_LANE0(0x00);
	tegra_sor_writel(sor, value, SOR_LANE_POSTCURSOR0);

	/* disable LVDS mode */
	tegra_sor_writel(sor, 0, SOR_LVDS);

	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value |= SOR_DP_PADCTL_TX_PU_ENABLE;
	value &= ~SOR_DP_PADCTL_TX_PU_MASK;
	value |= SOR_DP_PADCTL_TX_PU(2); /* XXX: don't hardcode? */
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value |= SOR_DP_PADCTL_CM_TXD_3 | SOR_DP_PADCTL_CM_TXD_2 |
		 SOR_DP_PADCTL_CM_TXD_1 | SOR_DP_PADCTL_CM_TXD_0;
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	usleep_range(10, 100);

	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value &= ~(SOR_DP_PADCTL_CM_TXD_3 | SOR_DP_PADCTL_CM_TXD_2 |
		   SOR_DP_PADCTL_CM_TXD_1 | SOR_DP_PADCTL_CM_TXD_0);
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	err = drm_dp_aux_prepare(sor->aux, DP_SET_ANSI_8B10B);
	if (err < 0)
		return err;

	for (i = 0, value = 0; i < link->num_lanes; i++) {
		unsigned long lane = SOR_DP_TPG_CHANNEL_CODING |
				     SOR_DP_TPG_SCRAMBLER_NONE |
				     SOR_DP_TPG_PATTERN_TRAIN1;
		value = (value << 8) | lane;
	}

	tegra_sor_writel(sor, value, SOR_DP_TPG);

	pattern = DP_TRAINING_PATTERN_1;

	err = drm_dp_aux_train(sor->aux, link, pattern);
	if (err < 0)
		return err;

	value = tegra_sor_readl(sor, SOR_DP_SPARE0);
	value |= SOR_DP_SPARE_SEQ_ENABLE;
	value &= ~SOR_DP_SPARE_PANEL_INTERNAL;
	value |= SOR_DP_SPARE_MACRO_SOR_CLK;
	tegra_sor_writel(sor, value, SOR_DP_SPARE0);

	for (i = 0, value = 0; i < link->num_lanes; i++) {
		unsigned long lane = SOR_DP_TPG_CHANNEL_CODING |
				     SOR_DP_TPG_SCRAMBLER_NONE |
				     SOR_DP_TPG_PATTERN_TRAIN2;
		value = (value << 8) | lane;
	}

	tegra_sor_writel(sor, value, SOR_DP_TPG);

	pattern = DP_LINK_SCRAMBLING_DISABLE | DP_TRAINING_PATTERN_2;

	err = drm_dp_aux_train(sor->aux, link, pattern);
	if (err < 0)
		return err;

	for (i = 0, value = 0; i < link->num_lanes; i++) {
		unsigned long lane = SOR_DP_TPG_CHANNEL_CODING |
				     SOR_DP_TPG_SCRAMBLER_GALIOS |
				     SOR_DP_TPG_PATTERN_NONE;
		value = (value << 8) | lane;
	}

	tegra_sor_writel(sor, value, SOR_DP_TPG);

	pattern = DP_TRAINING_PATTERN_DISABLE;

	err = drm_dp_aux_train(sor->aux, link, pattern);
	if (err < 0)
		return err;

	return 0;
}

static void tegra_sor_super_update(struct tegra_sor *sor)
{
	tegra_sor_writel(sor, 0, SOR_SUPER_STATE0);
	tegra_sor_writel(sor, 1, SOR_SUPER_STATE0);
	tegra_sor_writel(sor, 0, SOR_SUPER_STATE0);
}

static void tegra_sor_update(struct tegra_sor *sor)
{
	tegra_sor_writel(sor, 0, SOR_STATE0);
	tegra_sor_writel(sor, 1, SOR_STATE0);
	tegra_sor_writel(sor, 0, SOR_STATE0);
}

static int tegra_sor_setup_pwm(struct tegra_sor *sor, unsigned long timeout)
{
	u32 value;

	value = tegra_sor_readl(sor, SOR_PWM_DIV);
	value &= ~SOR_PWM_DIV_MASK;
	value |= 0x400; /* period */
	tegra_sor_writel(sor, value, SOR_PWM_DIV);

	value = tegra_sor_readl(sor, SOR_PWM_CTL);
	value &= ~SOR_PWM_CTL_DUTY_CYCLE_MASK;
	value |= 0x400; /* duty cycle */
	value &= ~SOR_PWM_CTL_CLK_SEL; /* clock source: PCLK */
	value |= SOR_PWM_CTL_TRIGGER;
	tegra_sor_writel(sor, value, SOR_PWM_CTL);

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_PWM_CTL);
		if ((value & SOR_PWM_CTL_TRIGGER) == 0)
			return 0;

		usleep_range(25, 100);
	}

	return -ETIMEDOUT;
}

static int tegra_sor_attach(struct tegra_sor *sor)
{
	unsigned long value, timeout;

	/* wake up in normal mode */
	value = tegra_sor_readl(sor, SOR_SUPER_STATE1);
	value |= SOR_SUPER_STATE_HEAD_MODE_AWAKE;
	value |= SOR_SUPER_STATE_MODE_NORMAL;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE1);
	tegra_sor_super_update(sor);

	/* attach */
	value = tegra_sor_readl(sor, SOR_SUPER_STATE1);
	value |= SOR_SUPER_STATE_ATTACHED;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE1);
	tegra_sor_super_update(sor);

	timeout = jiffies + msecs_to_jiffies(250);

	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_TEST);
		if ((value & SOR_TEST_ATTACHED) != 0)
			return 0;

		usleep_range(25, 100);
	}

	return -ETIMEDOUT;
}

static int tegra_sor_wakeup(struct tegra_sor *sor)
{
	unsigned long value, timeout;

	timeout = jiffies + msecs_to_jiffies(250);

	/* wait for head to wake up */
	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_TEST);
		value &= SOR_TEST_HEAD_MODE_MASK;

		if (value == SOR_TEST_HEAD_MODE_AWAKE)
			return 0;

		usleep_range(25, 100);
	}

	return -ETIMEDOUT;
}

static int tegra_sor_power_up(struct tegra_sor *sor, unsigned long timeout)
{
	u32 value;

	value = tegra_sor_readl(sor, SOR_PWR);
	value |= SOR_PWR_TRIGGER | SOR_PWR_NORMAL_STATE_PU;
	tegra_sor_writel(sor, value, SOR_PWR);

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_PWR);
		if ((value & SOR_PWR_TRIGGER) == 0)
			return 0;

		usleep_range(25, 100);
	}

	return -ETIMEDOUT;
}

struct tegra_sor_params {
	/* number of link clocks per line */
	unsigned int num_clocks;
	/* ratio between input and output */
	u64 ratio;
	/* precision factor */
	u64 precision;

	unsigned int active_polarity;
	unsigned int active_count;
	unsigned int active_frac;
	unsigned int tu_size;
	unsigned int error;
};

static int tegra_sor_compute_params(struct tegra_sor *sor,
				    struct tegra_sor_params *params,
				    unsigned int tu_size)
{
	u64 active_sym, active_count, frac, approx;
	u32 active_polarity, active_frac = 0;
	const u64 f = params->precision;
	s64 error;

	active_sym = params->ratio * tu_size;
	active_count = div_u64(active_sym, f) * f;
	frac = active_sym - active_count;

	/* fraction < 0.5 */
	if (frac >= (f / 2)) {
		active_polarity = 1;
		frac = f - frac;
	} else {
		active_polarity = 0;
	}

	if (frac != 0) {
		frac = div_u64(f * f,  frac); /* 1/fraction */
		if (frac <= (15 * f)) {
			active_frac = div_u64(frac, f);

			/* round up */
			if (active_polarity)
				active_frac++;
		} else {
			active_frac = active_polarity ? 1 : 15;
		}
	}

	if (active_frac == 1)
		active_polarity = 0;

	if (active_polarity == 1) {
		if (active_frac) {
			approx = active_count + (active_frac * (f - 1)) * f;
			approx = div_u64(approx, active_frac * f);
		} else {
			approx = active_count + f;
		}
	} else {
		if (active_frac)
			approx = active_count + div_u64(f, active_frac);
		else
			approx = active_count;
	}

	error = div_s64(active_sym - approx, tu_size);
	error *= params->num_clocks;

	if (error <= 0 && abs(error) < params->error) {
		params->active_count = div_u64(active_count, f);
		params->active_polarity = active_polarity;
		params->active_frac = active_frac;
		params->error = abs(error);
		params->tu_size = tu_size;

		if (error == 0)
			return true;
	}

	return false;
}

static int tegra_sor_compute_config(struct tegra_sor *sor,
				    const struct drm_display_mode *mode,
				    struct tegra_sor_config *config,
				    struct drm_dp_link *link)
{
	const u64 f = 100000, link_rate = link->rate * 1000;
	const u64 pclk = mode->clock * 1000;
	u64 input, output, watermark, num;
	struct tegra_sor_params params;
	u32 num_syms_per_line;
	unsigned int i;

	if (!link_rate || !link->num_lanes || !pclk || !config->bits_per_pixel)
		return -EINVAL;

	output = link_rate * 8 * link->num_lanes;
	input = pclk * config->bits_per_pixel;

	if (input >= output)
		return -ERANGE;

	memset(&params, 0, sizeof(params));
	params.ratio = div64_u64(input * f, output);
	params.num_clocks = div_u64(link_rate * mode->hdisplay, pclk);
	params.precision = f;
	params.error = 64 * f;
	params.tu_size = 64;

	for (i = params.tu_size; i >= 32; i--)
		if (tegra_sor_compute_params(sor, &params, i))
			break;

	if (params.active_frac == 0) {
		config->active_polarity = 0;
		config->active_count = params.active_count;

		if (!params.active_polarity)
			config->active_count--;

		config->tu_size = params.tu_size;
		config->active_frac = 1;
	} else {
		config->active_polarity = params.active_polarity;
		config->active_count = params.active_count;
		config->active_frac = params.active_frac;
		config->tu_size = params.tu_size;
	}

	dev_dbg(sor->dev,
		"polarity: %d active count: %d tu size: %d active frac: %d\n",
		config->active_polarity, config->active_count,
		config->tu_size, config->active_frac);

	watermark = params.ratio * config->tu_size * (f - params.ratio);
	watermark = div_u64(watermark, f);

	watermark = div_u64(watermark + params.error, f);
	config->watermark = watermark + (config->bits_per_pixel / 8) + 2;
	num_syms_per_line = (mode->hdisplay * config->bits_per_pixel) *
			    (link->num_lanes * 8);

	if (config->watermark > 30) {
		config->watermark = 30;
		dev_err(sor->dev,
			"unable to compute TU size, forcing watermark to %u\n",
			config->watermark);
	} else if (config->watermark > num_syms_per_line) {
		config->watermark = num_syms_per_line;
		dev_err(sor->dev, "watermark too high, forcing to %u\n",
			config->watermark);
	}

	/* compute the number of symbols per horizontal blanking interval */
	num = ((mode->htotal - mode->hdisplay) - 7) * link_rate;
	config->hblank_symbols = div_u64(num, pclk);

	if (link->capabilities & DP_LINK_CAP_ENHANCED_FRAMING)
		config->hblank_symbols -= 3;

	config->hblank_symbols -= 12 / link->num_lanes;

	/* compute the number of symbols per vertical blanking interval */
	num = (mode->hdisplay - 25) * link_rate;
	config->vblank_symbols = div_u64(num, pclk);
	config->vblank_symbols -= 36 / link->num_lanes + 4;

	dev_dbg(sor->dev, "blank symbols: H:%u V:%u\n", config->hblank_symbols,
		config->vblank_symbols);

	return 0;
}

static void tegra_sor_apply_config(struct tegra_sor *sor,
				   const struct tegra_sor_config *config)
{
	u32 value;

	value = tegra_sor_readl(sor, SOR_DP_LINKCTL0);
	value &= ~SOR_DP_LINKCTL_TU_SIZE_MASK;
	value |= SOR_DP_LINKCTL_TU_SIZE(config->tu_size);
	tegra_sor_writel(sor, value, SOR_DP_LINKCTL0);

	value = tegra_sor_readl(sor, SOR_DP_CONFIG0);
	value &= ~SOR_DP_CONFIG_WATERMARK_MASK;
	value |= SOR_DP_CONFIG_WATERMARK(config->watermark);

	value &= ~SOR_DP_CONFIG_ACTIVE_SYM_COUNT_MASK;
	value |= SOR_DP_CONFIG_ACTIVE_SYM_COUNT(config->active_count);

	value &= ~SOR_DP_CONFIG_ACTIVE_SYM_FRAC_MASK;
	value |= SOR_DP_CONFIG_ACTIVE_SYM_FRAC(config->active_frac);

	if (config->active_polarity)
		value |= SOR_DP_CONFIG_ACTIVE_SYM_POLARITY;
	else
		value &= ~SOR_DP_CONFIG_ACTIVE_SYM_POLARITY;

	value |= SOR_DP_CONFIG_ACTIVE_SYM_ENABLE;
	value |= SOR_DP_CONFIG_DISPARITY_NEGATIVE;
	tegra_sor_writel(sor, value, SOR_DP_CONFIG0);

	value = tegra_sor_readl(sor, SOR_DP_AUDIO_HBLANK_SYMBOLS);
	value &= ~SOR_DP_AUDIO_HBLANK_SYMBOLS_MASK;
	value |= config->hblank_symbols & 0xffff;
	tegra_sor_writel(sor, value, SOR_DP_AUDIO_HBLANK_SYMBOLS);

	value = tegra_sor_readl(sor, SOR_DP_AUDIO_VBLANK_SYMBOLS);
	value &= ~SOR_DP_AUDIO_VBLANK_SYMBOLS_MASK;
	value |= config->vblank_symbols & 0xffff;
	tegra_sor_writel(sor, value, SOR_DP_AUDIO_VBLANK_SYMBOLS);
}

static void tegra_sor_mode_set(struct tegra_sor *sor,
			       const struct drm_display_mode *mode,
			       struct tegra_sor_state *state)
{
	struct tegra_dc *dc = to_tegra_dc(sor->output.encoder.crtc);
	unsigned int vbe, vse, hbe, hse, vbs, hbs;
	u32 value;

	value = tegra_sor_readl(sor, SOR_STATE1);
	value &= ~SOR_STATE_ASY_PIXELDEPTH_MASK;
	value &= ~SOR_STATE_ASY_CRC_MODE_MASK;
	value &= ~SOR_STATE_ASY_OWNER_MASK;

	value |= SOR_STATE_ASY_CRC_MODE_COMPLETE |
		 SOR_STATE_ASY_OWNER(dc->pipe + 1);

	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		value &= ~SOR_STATE_ASY_HSYNCPOL;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		value |= SOR_STATE_ASY_HSYNCPOL;

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		value &= ~SOR_STATE_ASY_VSYNCPOL;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		value |= SOR_STATE_ASY_VSYNCPOL;

	switch (state->bpc) {
	case 16:
		value |= SOR_STATE_ASY_PIXELDEPTH_BPP_48_444;
		break;

	case 12:
		value |= SOR_STATE_ASY_PIXELDEPTH_BPP_36_444;
		break;

	case 10:
		value |= SOR_STATE_ASY_PIXELDEPTH_BPP_30_444;
		break;

	case 8:
		value |= SOR_STATE_ASY_PIXELDEPTH_BPP_24_444;
		break;

	case 6:
		value |= SOR_STATE_ASY_PIXELDEPTH_BPP_18_444;
		break;

	default:
		value |= SOR_STATE_ASY_PIXELDEPTH_BPP_24_444;
		break;
	}

	tegra_sor_writel(sor, value, SOR_STATE1);

	/*
	 * TODO: The video timing programming below doesn't seem to match the
	 * register definitions.
	 */

	value = ((mode->vtotal & 0x7fff) << 16) | (mode->htotal & 0x7fff);
	tegra_sor_writel(sor, value, sor->soc->regs->head_state1 + dc->pipe);

	/* sync end = sync width - 1 */
	vse = mode->vsync_end - mode->vsync_start - 1;
	hse = mode->hsync_end - mode->hsync_start - 1;

	value = ((vse & 0x7fff) << 16) | (hse & 0x7fff);
	tegra_sor_writel(sor, value, sor->soc->regs->head_state2 + dc->pipe);

	/* blank end = sync end + back porch */
	vbe = vse + (mode->vtotal - mode->vsync_end);
	hbe = hse + (mode->htotal - mode->hsync_end);

	value = ((vbe & 0x7fff) << 16) | (hbe & 0x7fff);
	tegra_sor_writel(sor, value, sor->soc->regs->head_state3 + dc->pipe);

	/* blank start = blank end + active */
	vbs = vbe + mode->vdisplay;
	hbs = hbe + mode->hdisplay;

	value = ((vbs & 0x7fff) << 16) | (hbs & 0x7fff);
	tegra_sor_writel(sor, value, sor->soc->regs->head_state4 + dc->pipe);

	/* XXX interlacing support */
	tegra_sor_writel(sor, 0x001, sor->soc->regs->head_state5 + dc->pipe);
}

static int tegra_sor_detach(struct tegra_sor *sor)
{
	unsigned long value, timeout;

	/* switch to safe mode */
	value = tegra_sor_readl(sor, SOR_SUPER_STATE1);
	value &= ~SOR_SUPER_STATE_MODE_NORMAL;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE1);
	tegra_sor_super_update(sor);

	timeout = jiffies + msecs_to_jiffies(250);

	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_PWR);
		if (value & SOR_PWR_MODE_SAFE)
			break;
	}

	if ((value & SOR_PWR_MODE_SAFE) == 0)
		return -ETIMEDOUT;

	/* go to sleep */
	value = tegra_sor_readl(sor, SOR_SUPER_STATE1);
	value &= ~SOR_SUPER_STATE_HEAD_MODE_MASK;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE1);
	tegra_sor_super_update(sor);

	/* detach */
	value = tegra_sor_readl(sor, SOR_SUPER_STATE1);
	value &= ~SOR_SUPER_STATE_ATTACHED;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE1);
	tegra_sor_super_update(sor);

	timeout = jiffies + msecs_to_jiffies(250);

	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_TEST);
		if ((value & SOR_TEST_ATTACHED) == 0)
			break;

		usleep_range(25, 100);
	}

	if ((value & SOR_TEST_ATTACHED) != 0)
		return -ETIMEDOUT;

	return 0;
}

static int tegra_sor_power_down(struct tegra_sor *sor)
{
	unsigned long value, timeout;
	int err;

	value = tegra_sor_readl(sor, SOR_PWR);
	value &= ~SOR_PWR_NORMAL_STATE_PU;
	value |= SOR_PWR_TRIGGER;
	tegra_sor_writel(sor, value, SOR_PWR);

	timeout = jiffies + msecs_to_jiffies(250);

	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_PWR);
		if ((value & SOR_PWR_TRIGGER) == 0)
			return 0;

		usleep_range(25, 100);
	}

	if ((value & SOR_PWR_TRIGGER) != 0)
		return -ETIMEDOUT;

	/* switch to safe parent clock */
	err = tegra_sor_set_parent_clock(sor, sor->clk_safe);
	if (err < 0) {
		dev_err(sor->dev, "failed to set safe parent clock: %d\n", err);
		return err;
	}

	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value &= ~(SOR_DP_PADCTL_PD_TXD_3 | SOR_DP_PADCTL_PD_TXD_0 |
		   SOR_DP_PADCTL_PD_TXD_1 | SOR_DP_PADCTL_PD_TXD_2);
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	/* stop lane sequencer */
	value = SOR_LANE_SEQ_CTL_TRIGGER | SOR_LANE_SEQ_CTL_SEQUENCE_UP |
		SOR_LANE_SEQ_CTL_POWER_STATE_DOWN;
	tegra_sor_writel(sor, value, SOR_LANE_SEQ_CTL);

	timeout = jiffies + msecs_to_jiffies(250);

	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_LANE_SEQ_CTL);
		if ((value & SOR_LANE_SEQ_CTL_TRIGGER) == 0)
			break;

		usleep_range(25, 100);
	}

	if ((value & SOR_LANE_SEQ_CTL_TRIGGER) != 0)
		return -ETIMEDOUT;

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value |= SOR_PLL2_PORT_POWERDOWN;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	usleep_range(20, 100);

	value = tegra_sor_readl(sor, sor->soc->regs->pll0);
	value |= SOR_PLL0_VCOPD | SOR_PLL0_PWR;
	tegra_sor_writel(sor, value, sor->soc->regs->pll0);

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value |= SOR_PLL2_SEQ_PLLCAPPD;
	value |= SOR_PLL2_SEQ_PLLCAPPD_ENFORCE;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	usleep_range(20, 100);

	return 0;
}

static int tegra_sor_crc_wait(struct tegra_sor *sor, unsigned long timeout)
{
	u32 value;

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		value = tegra_sor_readl(sor, SOR_CRCA);
		if (value & SOR_CRCA_VALID)
			return 0;

		usleep_range(100, 200);
	}

	return -ETIMEDOUT;
}

static int tegra_sor_show_crc(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_sor *sor = node->info_ent->data;
	struct drm_crtc *crtc = sor->output.encoder.crtc;
	struct drm_device *drm = node->minor->dev;
	int err = 0;
	u32 value;

	drm_modeset_lock_all(drm);

	if (!crtc || !crtc->state->active) {
		err = -EBUSY;
		goto unlock;
	}

	value = tegra_sor_readl(sor, SOR_STATE1);
	value &= ~SOR_STATE_ASY_CRC_MODE_MASK;
	tegra_sor_writel(sor, value, SOR_STATE1);

	value = tegra_sor_readl(sor, SOR_CRC_CNTRL);
	value |= SOR_CRC_CNTRL_ENABLE;
	tegra_sor_writel(sor, value, SOR_CRC_CNTRL);

	value = tegra_sor_readl(sor, SOR_TEST);
	value &= ~SOR_TEST_CRC_POST_SERIALIZE;
	tegra_sor_writel(sor, value, SOR_TEST);

	err = tegra_sor_crc_wait(sor, 100);
	if (err < 0)
		goto unlock;

	tegra_sor_writel(sor, SOR_CRCA_RESET, SOR_CRCA);
	value = tegra_sor_readl(sor, SOR_CRCB);

	seq_printf(s, "%08x\n", value);

unlock:
	drm_modeset_unlock_all(drm);
	return err;
}

#define DEBUGFS_REG32(_name) { .name = #_name, .offset = _name }

static const struct debugfs_reg32 tegra_sor_regs[] = {
	DEBUGFS_REG32(SOR_CTXSW),
	DEBUGFS_REG32(SOR_SUPER_STATE0),
	DEBUGFS_REG32(SOR_SUPER_STATE1),
	DEBUGFS_REG32(SOR_STATE0),
	DEBUGFS_REG32(SOR_STATE1),
	DEBUGFS_REG32(SOR_HEAD_STATE0(0)),
	DEBUGFS_REG32(SOR_HEAD_STATE0(1)),
	DEBUGFS_REG32(SOR_HEAD_STATE1(0)),
	DEBUGFS_REG32(SOR_HEAD_STATE1(1)),
	DEBUGFS_REG32(SOR_HEAD_STATE2(0)),
	DEBUGFS_REG32(SOR_HEAD_STATE2(1)),
	DEBUGFS_REG32(SOR_HEAD_STATE3(0)),
	DEBUGFS_REG32(SOR_HEAD_STATE3(1)),
	DEBUGFS_REG32(SOR_HEAD_STATE4(0)),
	DEBUGFS_REG32(SOR_HEAD_STATE4(1)),
	DEBUGFS_REG32(SOR_HEAD_STATE5(0)),
	DEBUGFS_REG32(SOR_HEAD_STATE5(1)),
	DEBUGFS_REG32(SOR_CRC_CNTRL),
	DEBUGFS_REG32(SOR_DP_DEBUG_MVID),
	DEBUGFS_REG32(SOR_CLK_CNTRL),
	DEBUGFS_REG32(SOR_CAP),
	DEBUGFS_REG32(SOR_PWR),
	DEBUGFS_REG32(SOR_TEST),
	DEBUGFS_REG32(SOR_PLL0),
	DEBUGFS_REG32(SOR_PLL1),
	DEBUGFS_REG32(SOR_PLL2),
	DEBUGFS_REG32(SOR_PLL3),
	DEBUGFS_REG32(SOR_CSTM),
	DEBUGFS_REG32(SOR_LVDS),
	DEBUGFS_REG32(SOR_CRCA),
	DEBUGFS_REG32(SOR_CRCB),
	DEBUGFS_REG32(SOR_BLANK),
	DEBUGFS_REG32(SOR_SEQ_CTL),
	DEBUGFS_REG32(SOR_LANE_SEQ_CTL),
	DEBUGFS_REG32(SOR_SEQ_INST(0)),
	DEBUGFS_REG32(SOR_SEQ_INST(1)),
	DEBUGFS_REG32(SOR_SEQ_INST(2)),
	DEBUGFS_REG32(SOR_SEQ_INST(3)),
	DEBUGFS_REG32(SOR_SEQ_INST(4)),
	DEBUGFS_REG32(SOR_SEQ_INST(5)),
	DEBUGFS_REG32(SOR_SEQ_INST(6)),
	DEBUGFS_REG32(SOR_SEQ_INST(7)),
	DEBUGFS_REG32(SOR_SEQ_INST(8)),
	DEBUGFS_REG32(SOR_SEQ_INST(9)),
	DEBUGFS_REG32(SOR_SEQ_INST(10)),
	DEBUGFS_REG32(SOR_SEQ_INST(11)),
	DEBUGFS_REG32(SOR_SEQ_INST(12)),
	DEBUGFS_REG32(SOR_SEQ_INST(13)),
	DEBUGFS_REG32(SOR_SEQ_INST(14)),
	DEBUGFS_REG32(SOR_SEQ_INST(15)),
	DEBUGFS_REG32(SOR_PWM_DIV),
	DEBUGFS_REG32(SOR_PWM_CTL),
	DEBUGFS_REG32(SOR_VCRC_A0),
	DEBUGFS_REG32(SOR_VCRC_A1),
	DEBUGFS_REG32(SOR_VCRC_B0),
	DEBUGFS_REG32(SOR_VCRC_B1),
	DEBUGFS_REG32(SOR_CCRC_A0),
	DEBUGFS_REG32(SOR_CCRC_A1),
	DEBUGFS_REG32(SOR_CCRC_B0),
	DEBUGFS_REG32(SOR_CCRC_B1),
	DEBUGFS_REG32(SOR_EDATA_A0),
	DEBUGFS_REG32(SOR_EDATA_A1),
	DEBUGFS_REG32(SOR_EDATA_B0),
	DEBUGFS_REG32(SOR_EDATA_B1),
	DEBUGFS_REG32(SOR_COUNT_A0),
	DEBUGFS_REG32(SOR_COUNT_A1),
	DEBUGFS_REG32(SOR_COUNT_B0),
	DEBUGFS_REG32(SOR_COUNT_B1),
	DEBUGFS_REG32(SOR_DEBUG_A0),
	DEBUGFS_REG32(SOR_DEBUG_A1),
	DEBUGFS_REG32(SOR_DEBUG_B0),
	DEBUGFS_REG32(SOR_DEBUG_B1),
	DEBUGFS_REG32(SOR_TRIG),
	DEBUGFS_REG32(SOR_MSCHECK),
	DEBUGFS_REG32(SOR_XBAR_CTRL),
	DEBUGFS_REG32(SOR_XBAR_POL),
	DEBUGFS_REG32(SOR_DP_LINKCTL0),
	DEBUGFS_REG32(SOR_DP_LINKCTL1),
	DEBUGFS_REG32(SOR_LANE_DRIVE_CURRENT0),
	DEBUGFS_REG32(SOR_LANE_DRIVE_CURRENT1),
	DEBUGFS_REG32(SOR_LANE4_DRIVE_CURRENT0),
	DEBUGFS_REG32(SOR_LANE4_DRIVE_CURRENT1),
	DEBUGFS_REG32(SOR_LANE_PREEMPHASIS0),
	DEBUGFS_REG32(SOR_LANE_PREEMPHASIS1),
	DEBUGFS_REG32(SOR_LANE4_PREEMPHASIS0),
	DEBUGFS_REG32(SOR_LANE4_PREEMPHASIS1),
	DEBUGFS_REG32(SOR_LANE_POSTCURSOR0),
	DEBUGFS_REG32(SOR_LANE_POSTCURSOR1),
	DEBUGFS_REG32(SOR_DP_CONFIG0),
	DEBUGFS_REG32(SOR_DP_CONFIG1),
	DEBUGFS_REG32(SOR_DP_MN0),
	DEBUGFS_REG32(SOR_DP_MN1),
	DEBUGFS_REG32(SOR_DP_PADCTL0),
	DEBUGFS_REG32(SOR_DP_PADCTL1),
	DEBUGFS_REG32(SOR_DP_PADCTL2),
	DEBUGFS_REG32(SOR_DP_DEBUG0),
	DEBUGFS_REG32(SOR_DP_DEBUG1),
	DEBUGFS_REG32(SOR_DP_SPARE0),
	DEBUGFS_REG32(SOR_DP_SPARE1),
	DEBUGFS_REG32(SOR_DP_AUDIO_CTRL),
	DEBUGFS_REG32(SOR_DP_AUDIO_HBLANK_SYMBOLS),
	DEBUGFS_REG32(SOR_DP_AUDIO_VBLANK_SYMBOLS),
	DEBUGFS_REG32(SOR_DP_GENERIC_INFOFRAME_HEADER),
	DEBUGFS_REG32(SOR_DP_GENERIC_INFOFRAME_SUBPACK0),
	DEBUGFS_REG32(SOR_DP_GENERIC_INFOFRAME_SUBPACK1),
	DEBUGFS_REG32(SOR_DP_GENERIC_INFOFRAME_SUBPACK2),
	DEBUGFS_REG32(SOR_DP_GENERIC_INFOFRAME_SUBPACK3),
	DEBUGFS_REG32(SOR_DP_GENERIC_INFOFRAME_SUBPACK4),
	DEBUGFS_REG32(SOR_DP_GENERIC_INFOFRAME_SUBPACK5),
	DEBUGFS_REG32(SOR_DP_GENERIC_INFOFRAME_SUBPACK6),
	DEBUGFS_REG32(SOR_DP_TPG),
	DEBUGFS_REG32(SOR_DP_TPG_CONFIG),
	DEBUGFS_REG32(SOR_DP_LQ_CSTM0),
	DEBUGFS_REG32(SOR_DP_LQ_CSTM1),
	DEBUGFS_REG32(SOR_DP_LQ_CSTM2),
};

static int tegra_sor_show_regs(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_sor *sor = node->info_ent->data;
	struct drm_crtc *crtc = sor->output.encoder.crtc;
	struct drm_device *drm = node->minor->dev;
	unsigned int i;
	int err = 0;

	drm_modeset_lock_all(drm);

	if (!crtc || !crtc->state->active) {
		err = -EBUSY;
		goto unlock;
	}

	for (i = 0; i < ARRAY_SIZE(tegra_sor_regs); i++) {
		unsigned int offset = tegra_sor_regs[i].offset;

		seq_printf(s, "%-38s %#05x %08x\n", tegra_sor_regs[i].name,
			   offset, tegra_sor_readl(sor, offset));
	}

unlock:
	drm_modeset_unlock_all(drm);
	return err;
}

static const struct drm_info_list debugfs_files[] = {
	{ "crc", tegra_sor_show_crc, 0, NULL },
	{ "regs", tegra_sor_show_regs, 0, NULL },
};

static int tegra_sor_late_register(struct drm_connector *connector)
{
	struct tegra_output *output = connector_to_output(connector);
	unsigned int i, count = ARRAY_SIZE(debugfs_files);
	struct drm_minor *minor = connector->dev->primary;
	struct dentry *root = connector->debugfs_entry;
	struct tegra_sor *sor = to_sor(output);
	int err;

	sor->debugfs_files = kmemdup(debugfs_files, sizeof(debugfs_files),
				     GFP_KERNEL);
	if (!sor->debugfs_files)
		return -ENOMEM;

	for (i = 0; i < count; i++)
		sor->debugfs_files[i].data = sor;

	err = drm_debugfs_create_files(sor->debugfs_files, count, root, minor);
	if (err < 0)
		goto free;

	return 0;

free:
	kfree(sor->debugfs_files);
	sor->debugfs_files = NULL;

	return err;
}

static void tegra_sor_early_unregister(struct drm_connector *connector)
{
	struct tegra_output *output = connector_to_output(connector);
	unsigned int count = ARRAY_SIZE(debugfs_files);
	struct tegra_sor *sor = to_sor(output);

	drm_debugfs_remove_files(sor->debugfs_files, count,
				 connector->dev->primary);
	kfree(sor->debugfs_files);
	sor->debugfs_files = NULL;
}

static void tegra_sor_connector_reset(struct drm_connector *connector)
{
	struct tegra_sor_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return;

	if (connector->state) {
		__drm_atomic_helper_connector_destroy_state(connector->state);
		kfree(connector->state);
	}

	__drm_atomic_helper_connector_reset(connector, &state->base);
}

static enum drm_connector_status
tegra_sor_connector_detect(struct drm_connector *connector, bool force)
{
	struct tegra_output *output = connector_to_output(connector);
	struct tegra_sor *sor = to_sor(output);

	if (sor->aux)
		return drm_dp_aux_detect(sor->aux);

	return tegra_output_connector_detect(connector, force);
}

static struct drm_connector_state *
tegra_sor_connector_duplicate_state(struct drm_connector *connector)
{
	struct tegra_sor_state *state = to_sor_state(connector->state);
	struct tegra_sor_state *copy;

	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &copy->base);

	return &copy->base;
}

static const struct drm_connector_funcs tegra_sor_connector_funcs = {
	.reset = tegra_sor_connector_reset,
	.detect = tegra_sor_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = tegra_output_connector_destroy,
	.atomic_duplicate_state = tegra_sor_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.late_register = tegra_sor_late_register,
	.early_unregister = tegra_sor_early_unregister,
};

static int tegra_sor_connector_get_modes(struct drm_connector *connector)
{
	struct tegra_output *output = connector_to_output(connector);
	struct tegra_sor *sor = to_sor(output);
	int err;

	if (sor->aux)
		drm_dp_aux_enable(sor->aux);

	err = tegra_output_connector_get_modes(connector);

	if (sor->aux)
		drm_dp_aux_disable(sor->aux);

	return err;
}

static enum drm_mode_status
tegra_sor_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct drm_connector_helper_funcs tegra_sor_connector_helper_funcs = {
	.get_modes = tegra_sor_connector_get_modes,
	.mode_valid = tegra_sor_connector_mode_valid,
};

static const struct drm_encoder_funcs tegra_sor_encoder_funcs = {
	.destroy = tegra_output_encoder_destroy,
};

static void tegra_sor_edp_disable(struct drm_encoder *encoder)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct tegra_sor *sor = to_sor(output);
	u32 value;
	int err;

	if (output->panel)
		drm_panel_disable(output->panel);

	err = tegra_sor_detach(sor);
	if (err < 0)
		dev_err(sor->dev, "failed to detach SOR: %d\n", err);

	tegra_sor_writel(sor, 0, SOR_STATE1);
	tegra_sor_update(sor);

	/*
	 * The following accesses registers of the display controller, so make
	 * sure it's only executed when the output is attached to one.
	 */
	if (dc) {
		value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
		value &= ~SOR_ENABLE(0);
		tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

		tegra_dc_commit(dc);
	}

	err = tegra_sor_power_down(sor);
	if (err < 0)
		dev_err(sor->dev, "failed to power down SOR: %d\n", err);

	if (sor->aux) {
		err = drm_dp_aux_disable(sor->aux);
		if (err < 0)
			dev_err(sor->dev, "failed to disable DP: %d\n", err);
	}

	err = tegra_io_pad_power_disable(sor->pad);
	if (err < 0)
		dev_err(sor->dev, "failed to power off I/O pad: %d\n", err);

	if (output->panel)
		drm_panel_unprepare(output->panel);

	pm_runtime_put(sor->dev);
}

#if 0
static int calc_h_ref_to_sync(const struct drm_display_mode *mode,
			      unsigned int *value)
{
	unsigned int hfp, hsw, hbp, a = 0, b;

	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	pr_info("hfp: %u, hsw: %u, hbp: %u\n", hfp, hsw, hbp);

	b = hfp - 1;

	pr_info("a: %u, b: %u\n", a, b);
	pr_info("a + hsw + hbp = %u\n", a + hsw + hbp);

	if (a + hsw + hbp <= 11) {
		a = 1 + 11 - hsw - hbp;
		pr_info("a: %u\n", a);
	}

	if (a > b)
		return -EINVAL;

	if (hsw < 1)
		return -EINVAL;

	if (mode->hdisplay < 16)
		return -EINVAL;

	if (value) {
		if (b > a && a % 2)
			*value = a + 1;
		else
			*value = a;
	}

	return 0;
}
#endif

static void tegra_sor_edp_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct tegra_sor *sor = to_sor(output);
	struct tegra_sor_config config;
	struct tegra_sor_state *state;
	struct drm_dp_link link;
	u8 rate, lanes;
	unsigned int i;
	int err = 0;
	u32 value;

	state = to_sor_state(output->connector.state);

	pm_runtime_get_sync(sor->dev);

	if (output->panel)
		drm_panel_prepare(output->panel);

	err = drm_dp_aux_enable(sor->aux);
	if (err < 0)
		dev_err(sor->dev, "failed to enable DP: %d\n", err);

	err = drm_dp_link_probe(sor->aux, &link);
	if (err < 0) {
		dev_err(sor->dev, "failed to probe eDP link: %d\n", err);
		return;
	}

	/* switch to safe parent clock */
	err = tegra_sor_set_parent_clock(sor, sor->clk_safe);
	if (err < 0)
		dev_err(sor->dev, "failed to set safe parent clock: %d\n", err);

	memset(&config, 0, sizeof(config));
	config.bits_per_pixel = state->bpc * 3;

	err = tegra_sor_compute_config(sor, mode, &config, &link);
	if (err < 0)
		dev_err(sor->dev, "failed to compute configuration: %d\n", err);

	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_CLK_SEL_MASK;
	value |= SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_DPCLK;
	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value &= ~SOR_PLL2_BANDGAP_POWERDOWN;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);
	usleep_range(20, 100);

	value = tegra_sor_readl(sor, sor->soc->regs->pll3);
	value |= SOR_PLL3_PLL_VDD_MODE_3V3;
	tegra_sor_writel(sor, value, sor->soc->regs->pll3);

	value = SOR_PLL0_ICHPMP(0xf) | SOR_PLL0_VCOCAP_RST |
		SOR_PLL0_PLLREG_LEVEL_V45 | SOR_PLL0_RESISTOR_EXT;
	tegra_sor_writel(sor, value, sor->soc->regs->pll0);

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value |= SOR_PLL2_SEQ_PLLCAPPD;
	value &= ~SOR_PLL2_SEQ_PLLCAPPD_ENFORCE;
	value |= SOR_PLL2_LVDS_ENABLE;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	value = SOR_PLL1_TERM_COMPOUT | SOR_PLL1_TMDS_TERM;
	tegra_sor_writel(sor, value, sor->soc->regs->pll1);

	while (true) {
		value = tegra_sor_readl(sor, sor->soc->regs->pll2);
		if ((value & SOR_PLL2_SEQ_PLLCAPPD_ENFORCE) == 0)
			break;

		usleep_range(250, 1000);
	}

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value &= ~SOR_PLL2_POWERDOWN_OVERRIDE;
	value &= ~SOR_PLL2_PORT_POWERDOWN;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	/*
	 * power up
	 */

	/* set safe link bandwidth (1.62 Gbps) */
	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_LINK_SPEED_MASK;
	value |= SOR_CLK_CNTRL_DP_LINK_SPEED_G1_62;
	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	/* step 1 */
	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value |= SOR_PLL2_SEQ_PLLCAPPD_ENFORCE | SOR_PLL2_PORT_POWERDOWN |
		 SOR_PLL2_BANDGAP_POWERDOWN;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	value = tegra_sor_readl(sor, sor->soc->regs->pll0);
	value |= SOR_PLL0_VCOPD | SOR_PLL0_PWR;
	tegra_sor_writel(sor, value, sor->soc->regs->pll0);

	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value &= ~SOR_DP_PADCTL_PAD_CAL_PD;
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	/* step 2 */
	err = tegra_io_pad_power_enable(sor->pad);
	if (err < 0)
		dev_err(sor->dev, "failed to power on I/O pad: %d\n", err);

	usleep_range(5, 100);

	/* step 3 */
	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value &= ~SOR_PLL2_BANDGAP_POWERDOWN;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	usleep_range(20, 100);

	/* step 4 */
	value = tegra_sor_readl(sor, sor->soc->regs->pll0);
	value &= ~SOR_PLL0_VCOPD;
	value &= ~SOR_PLL0_PWR;
	tegra_sor_writel(sor, value, sor->soc->regs->pll0);

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value &= ~SOR_PLL2_SEQ_PLLCAPPD_ENFORCE;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	usleep_range(200, 1000);

	/* step 5 */
	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value &= ~SOR_PLL2_PORT_POWERDOWN;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	/* XXX not in TRM */
	for (value = 0, i = 0; i < 5; i++)
		value |= SOR_XBAR_CTRL_LINK0_XSEL(i, sor->xbar_cfg[i]) |
			 SOR_XBAR_CTRL_LINK1_XSEL(i, i);

	tegra_sor_writel(sor, 0x00000000, SOR_XBAR_POL);
	tegra_sor_writel(sor, value, SOR_XBAR_CTRL);

	/* switch to DP parent clock */
	err = tegra_sor_set_parent_clock(sor, sor->clk_dp);
	if (err < 0)
		dev_err(sor->dev, "failed to set parent clock: %d\n", err);

	/* power DP lanes */
	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);

	if (link.num_lanes <= 2)
		value &= ~(SOR_DP_PADCTL_PD_TXD_3 | SOR_DP_PADCTL_PD_TXD_2);
	else
		value |= SOR_DP_PADCTL_PD_TXD_3 | SOR_DP_PADCTL_PD_TXD_2;

	if (link.num_lanes <= 1)
		value &= ~SOR_DP_PADCTL_PD_TXD_1;
	else
		value |= SOR_DP_PADCTL_PD_TXD_1;

	if (link.num_lanes == 0)
		value &= ~SOR_DP_PADCTL_PD_TXD_0;
	else
		value |= SOR_DP_PADCTL_PD_TXD_0;

	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	value = tegra_sor_readl(sor, SOR_DP_LINKCTL0);
	value &= ~SOR_DP_LINKCTL_LANE_COUNT_MASK;
	value |= SOR_DP_LINKCTL_LANE_COUNT(link.num_lanes);
	tegra_sor_writel(sor, value, SOR_DP_LINKCTL0);

	/* start lane sequencer */
	value = SOR_LANE_SEQ_CTL_TRIGGER | SOR_LANE_SEQ_CTL_SEQUENCE_DOWN |
		SOR_LANE_SEQ_CTL_POWER_STATE_UP;
	tegra_sor_writel(sor, value, SOR_LANE_SEQ_CTL);

	while (true) {
		value = tegra_sor_readl(sor, SOR_LANE_SEQ_CTL);
		if ((value & SOR_LANE_SEQ_CTL_TRIGGER) == 0)
			break;

		usleep_range(250, 1000);
	}

	/* set link bandwidth */
	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_LINK_SPEED_MASK;
	value |= drm_dp_link_rate_to_bw_code(link.rate) << 2;
	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	tegra_sor_apply_config(sor, &config);

	/* enable link */
	value = tegra_sor_readl(sor, SOR_DP_LINKCTL0);
	value |= SOR_DP_LINKCTL_ENABLE;
	value |= SOR_DP_LINKCTL_ENHANCED_FRAME;
	tegra_sor_writel(sor, value, SOR_DP_LINKCTL0);

	for (i = 0, value = 0; i < 4; i++) {
		unsigned long lane = SOR_DP_TPG_CHANNEL_CODING |
				     SOR_DP_TPG_SCRAMBLER_GALIOS |
				     SOR_DP_TPG_PATTERN_NONE;
		value = (value << 8) | lane;
	}

	tegra_sor_writel(sor, value, SOR_DP_TPG);

	/* enable pad calibration logic */
	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value |= SOR_DP_PADCTL_PAD_CAL_PD;
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	err = drm_dp_link_probe(sor->aux, &link);
	if (err < 0)
		dev_err(sor->dev, "failed to probe eDP link: %d\n", err);

	err = drm_dp_link_power_up(sor->aux, &link);
	if (err < 0)
		dev_err(sor->dev, "failed to power up eDP link: %d\n", err);

	err = drm_dp_link_configure(sor->aux, &link);
	if (err < 0)
		dev_err(sor->dev, "failed to configure eDP link: %d\n", err);

	rate = drm_dp_link_rate_to_bw_code(link.rate);
	lanes = link.num_lanes;

	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_LINK_SPEED_MASK;
	value |= SOR_CLK_CNTRL_DP_LINK_SPEED(rate);
	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	value = tegra_sor_readl(sor, SOR_DP_LINKCTL0);
	value &= ~SOR_DP_LINKCTL_LANE_COUNT_MASK;
	value |= SOR_DP_LINKCTL_LANE_COUNT(lanes);

	if (link.capabilities & DP_LINK_CAP_ENHANCED_FRAMING)
		value |= SOR_DP_LINKCTL_ENHANCED_FRAME;

	tegra_sor_writel(sor, value, SOR_DP_LINKCTL0);

	/* disable training pattern generator */

	for (i = 0; i < link.num_lanes; i++) {
		unsigned long lane = SOR_DP_TPG_CHANNEL_CODING |
				     SOR_DP_TPG_SCRAMBLER_GALIOS |
				     SOR_DP_TPG_PATTERN_NONE;
		value = (value << 8) | lane;
	}

	tegra_sor_writel(sor, value, SOR_DP_TPG);

	err = tegra_sor_dp_train_fast(sor, &link);
	if (err < 0)
		dev_err(sor->dev, "DP fast link training failed: %d\n", err);

	dev_dbg(sor->dev, "fast link training succeeded\n");

	err = tegra_sor_power_up(sor, 250);
	if (err < 0)
		dev_err(sor->dev, "failed to power up SOR: %d\n", err);

	/* CSTM (LVDS, link A/B, upper) */
	value = SOR_CSTM_LVDS | SOR_CSTM_LINK_ACT_A | SOR_CSTM_LINK_ACT_B |
		SOR_CSTM_UPPER;
	tegra_sor_writel(sor, value, SOR_CSTM);

	/* use DP-A protocol */
	value = tegra_sor_readl(sor, SOR_STATE1);
	value &= ~SOR_STATE_ASY_PROTOCOL_MASK;
	value |= SOR_STATE_ASY_PROTOCOL_DP_A;
	tegra_sor_writel(sor, value, SOR_STATE1);

	tegra_sor_mode_set(sor, mode, state);

	/* PWM setup */
	err = tegra_sor_setup_pwm(sor, 250);
	if (err < 0)
		dev_err(sor->dev, "failed to setup PWM: %d\n", err);

	tegra_sor_update(sor);

	value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
	value |= SOR_ENABLE(0);
	tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

	tegra_dc_commit(dc);

	err = tegra_sor_attach(sor);
	if (err < 0)
		dev_err(sor->dev, "failed to attach SOR: %d\n", err);

	err = tegra_sor_wakeup(sor);
	if (err < 0)
		dev_err(sor->dev, "failed to enable DC: %d\n", err);

	if (output->panel)
		drm_panel_enable(output->panel);
}

static int
tegra_sor_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_sor_state *state = to_sor_state(conn_state);
	struct tegra_dc *dc = to_tegra_dc(conn_state->crtc);
	unsigned long pclk = crtc_state->mode.clock * 1000;
	struct tegra_sor *sor = to_sor(output);
	struct drm_display_info *info;
	int err;

	info = &output->connector.display_info;

	/*
	 * For HBR2 modes, the SOR brick needs to use the x20 multiplier, so
	 * the pixel clock must be corrected accordingly.
	 */
	if (pclk >= 340000000) {
		state->link_speed = 20;
		state->pclk = pclk / 2;
	} else {
		state->link_speed = 10;
		state->pclk = pclk;
	}

	err = tegra_dc_state_setup_clock(dc, crtc_state, sor->clk_parent,
					 pclk, 0);
	if (err < 0) {
		dev_err(output->dev, "failed to setup CRTC state: %d\n", err);
		return err;
	}

	switch (info->bpc) {
	case 8:
	case 6:
		state->bpc = info->bpc;
		break;

	default:
		DRM_DEBUG_KMS("%u bits-per-color not supported\n", info->bpc);
		state->bpc = 8;
		break;
	}

	return 0;
}

static const struct drm_encoder_helper_funcs tegra_sor_edp_helpers = {
	.disable = tegra_sor_edp_disable,
	.enable = tegra_sor_edp_enable,
	.atomic_check = tegra_sor_encoder_atomic_check,
};

static inline u32 tegra_sor_hdmi_subpack(const u8 *ptr, size_t size)
{
	u32 value = 0;
	size_t i;

	for (i = size; i > 0; i--)
		value = (value << 8) | ptr[i - 1];

	return value;
}

static void tegra_sor_hdmi_write_infopack(struct tegra_sor *sor,
					  const void *data, size_t size)
{
	const u8 *ptr = data;
	unsigned long offset;
	size_t i, j;
	u32 value;

	switch (ptr[0]) {
	case HDMI_INFOFRAME_TYPE_AVI:
		offset = SOR_HDMI_AVI_INFOFRAME_HEADER;
		break;

	case HDMI_INFOFRAME_TYPE_AUDIO:
		offset = SOR_HDMI_AUDIO_INFOFRAME_HEADER;
		break;

	case HDMI_INFOFRAME_TYPE_VENDOR:
		offset = SOR_HDMI_VSI_INFOFRAME_HEADER;
		break;

	default:
		dev_err(sor->dev, "unsupported infoframe type: %02x\n",
			ptr[0]);
		return;
	}

	value = INFOFRAME_HEADER_TYPE(ptr[0]) |
		INFOFRAME_HEADER_VERSION(ptr[1]) |
		INFOFRAME_HEADER_LEN(ptr[2]);
	tegra_sor_writel(sor, value, offset);
	offset++;

	/*
	 * Each subpack contains 7 bytes, divided into:
	 * - subpack_low: bytes 0 - 3
	 * - subpack_high: bytes 4 - 6 (with byte 7 padded to 0x00)
	 */
	for (i = 3, j = 0; i < size; i += 7, j += 8) {
		size_t rem = size - i, num = min_t(size_t, rem, 4);

		value = tegra_sor_hdmi_subpack(&ptr[i], num);
		tegra_sor_writel(sor, value, offset++);

		num = min_t(size_t, rem - num, 3);

		value = tegra_sor_hdmi_subpack(&ptr[i + 4], num);
		tegra_sor_writel(sor, value, offset++);
	}
}

static int
tegra_sor_hdmi_setup_avi_infoframe(struct tegra_sor *sor,
				   const struct drm_display_mode *mode)
{
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)];
	struct hdmi_avi_infoframe frame;
	u32 value;
	int err;

	/* disable AVI infoframe */
	value = tegra_sor_readl(sor, SOR_HDMI_AVI_INFOFRAME_CTRL);
	value &= ~INFOFRAME_CTRL_SINGLE;
	value &= ~INFOFRAME_CTRL_OTHER;
	value &= ~INFOFRAME_CTRL_ENABLE;
	tegra_sor_writel(sor, value, SOR_HDMI_AVI_INFOFRAME_CTRL);

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame,
						       &sor->output.connector, mode);
	if (err < 0) {
		dev_err(sor->dev, "failed to setup AVI infoframe: %d\n", err);
		return err;
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(sor->dev, "failed to pack AVI infoframe: %d\n", err);
		return err;
	}

	tegra_sor_hdmi_write_infopack(sor, buffer, err);

	/* enable AVI infoframe */
	value = tegra_sor_readl(sor, SOR_HDMI_AVI_INFOFRAME_CTRL);
	value |= INFOFRAME_CTRL_CHECKSUM_ENABLE;
	value |= INFOFRAME_CTRL_ENABLE;
	tegra_sor_writel(sor, value, SOR_HDMI_AVI_INFOFRAME_CTRL);

	return 0;
}

static void tegra_sor_write_eld(struct tegra_sor *sor)
{
	size_t length = drm_eld_size(sor->output.connector.eld), i;

	for (i = 0; i < length; i++)
		tegra_sor_writel(sor, i << 8 | sor->output.connector.eld[i],
				 SOR_AUDIO_HDA_ELD_BUFWR);

	/*
	 * The HDA codec will always report an ELD buffer size of 96 bytes and
	 * the HDA codec driver will check that each byte read from the buffer
	 * is valid. Therefore every byte must be written, even if no 96 bytes
	 * were parsed from EDID.
	 */
	for (i = length; i < 96; i++)
		tegra_sor_writel(sor, i << 8 | 0, SOR_AUDIO_HDA_ELD_BUFWR);
}

static void tegra_sor_audio_prepare(struct tegra_sor *sor)
{
	u32 value;

	tegra_sor_write_eld(sor);

	value = SOR_AUDIO_HDA_PRESENSE_ELDV | SOR_AUDIO_HDA_PRESENSE_PD;
	tegra_sor_writel(sor, value, SOR_AUDIO_HDA_PRESENSE);
}

static void tegra_sor_audio_unprepare(struct tegra_sor *sor)
{
	tegra_sor_writel(sor, 0, SOR_AUDIO_HDA_PRESENSE);
}

static int tegra_sor_hdmi_enable_audio_infoframe(struct tegra_sor *sor)
{
	u8 buffer[HDMI_INFOFRAME_SIZE(AUDIO)];
	struct hdmi_audio_infoframe frame;
	u32 value;
	int err;

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		dev_err(sor->dev, "failed to setup audio infoframe: %d\n", err);
		return err;
	}

	frame.channels = sor->format.channels;

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(sor->dev, "failed to pack audio infoframe: %d\n", err);
		return err;
	}

	tegra_sor_hdmi_write_infopack(sor, buffer, err);

	value = tegra_sor_readl(sor, SOR_HDMI_AUDIO_INFOFRAME_CTRL);
	value |= INFOFRAME_CTRL_CHECKSUM_ENABLE;
	value |= INFOFRAME_CTRL_ENABLE;
	tegra_sor_writel(sor, value, SOR_HDMI_AUDIO_INFOFRAME_CTRL);

	return 0;
}

static void tegra_sor_hdmi_audio_enable(struct tegra_sor *sor)
{
	u32 value;

	value = tegra_sor_readl(sor, SOR_AUDIO_CNTRL);

	/* select HDA audio input */
	value &= ~SOR_AUDIO_CNTRL_SOURCE_SELECT(SOURCE_SELECT_MASK);
	value |= SOR_AUDIO_CNTRL_SOURCE_SELECT(SOURCE_SELECT_HDA);

	/* inject null samples */
	if (sor->format.channels != 2)
		value &= ~SOR_AUDIO_CNTRL_INJECT_NULLSMPL;
	else
		value |= SOR_AUDIO_CNTRL_INJECT_NULLSMPL;

	value |= SOR_AUDIO_CNTRL_AFIFO_FLUSH;

	tegra_sor_writel(sor, value, SOR_AUDIO_CNTRL);

	/* enable advertising HBR capability */
	tegra_sor_writel(sor, SOR_AUDIO_SPARE_HBR_ENABLE, SOR_AUDIO_SPARE);

	tegra_sor_writel(sor, 0, SOR_HDMI_ACR_CTRL);

	value = SOR_HDMI_SPARE_ACR_PRIORITY_HIGH |
		SOR_HDMI_SPARE_CTS_RESET(1) |
		SOR_HDMI_SPARE_HW_CTS_ENABLE;
	tegra_sor_writel(sor, value, SOR_HDMI_SPARE);

	/* enable HW CTS */
	value = SOR_HDMI_ACR_SUBPACK_LOW_SB1(0);
	tegra_sor_writel(sor, value, SOR_HDMI_ACR_0441_SUBPACK_LOW);

	/* allow packet to be sent */
	value = SOR_HDMI_ACR_SUBPACK_HIGH_ENABLE;
	tegra_sor_writel(sor, value, SOR_HDMI_ACR_0441_SUBPACK_HIGH);

	/* reset N counter and enable lookup */
	value = SOR_HDMI_AUDIO_N_RESET | SOR_HDMI_AUDIO_N_LOOKUP;
	tegra_sor_writel(sor, value, SOR_HDMI_AUDIO_N);

	value = (24000 * 4096) / (128 * sor->format.sample_rate / 1000);
	tegra_sor_writel(sor, value, SOR_AUDIO_AVAL_0320);
	tegra_sor_writel(sor, 4096, SOR_AUDIO_NVAL_0320);

	tegra_sor_writel(sor, 20000, SOR_AUDIO_AVAL_0441);
	tegra_sor_writel(sor, 4704, SOR_AUDIO_NVAL_0441);

	tegra_sor_writel(sor, 20000, SOR_AUDIO_AVAL_0882);
	tegra_sor_writel(sor, 9408, SOR_AUDIO_NVAL_0882);

	tegra_sor_writel(sor, 20000, SOR_AUDIO_AVAL_1764);
	tegra_sor_writel(sor, 18816, SOR_AUDIO_NVAL_1764);

	value = (24000 * 6144) / (128 * sor->format.sample_rate / 1000);
	tegra_sor_writel(sor, value, SOR_AUDIO_AVAL_0480);
	tegra_sor_writel(sor, 6144, SOR_AUDIO_NVAL_0480);

	value = (24000 * 12288) / (128 * sor->format.sample_rate / 1000);
	tegra_sor_writel(sor, value, SOR_AUDIO_AVAL_0960);
	tegra_sor_writel(sor, 12288, SOR_AUDIO_NVAL_0960);

	value = (24000 * 24576) / (128 * sor->format.sample_rate / 1000);
	tegra_sor_writel(sor, value, SOR_AUDIO_AVAL_1920);
	tegra_sor_writel(sor, 24576, SOR_AUDIO_NVAL_1920);

	value = tegra_sor_readl(sor, SOR_HDMI_AUDIO_N);
	value &= ~SOR_HDMI_AUDIO_N_RESET;
	tegra_sor_writel(sor, value, SOR_HDMI_AUDIO_N);

	tegra_sor_hdmi_enable_audio_infoframe(sor);
}

static void tegra_sor_hdmi_disable_audio_infoframe(struct tegra_sor *sor)
{
	u32 value;

	value = tegra_sor_readl(sor, SOR_HDMI_AUDIO_INFOFRAME_CTRL);
	value &= ~INFOFRAME_CTRL_ENABLE;
	tegra_sor_writel(sor, value, SOR_HDMI_AUDIO_INFOFRAME_CTRL);
}

static void tegra_sor_hdmi_audio_disable(struct tegra_sor *sor)
{
	tegra_sor_hdmi_disable_audio_infoframe(sor);
}

static struct tegra_sor_hdmi_settings *
tegra_sor_hdmi_find_settings(struct tegra_sor *sor, unsigned long frequency)
{
	unsigned int i;

	for (i = 0; i < sor->num_settings; i++)
		if (frequency <= sor->settings[i].frequency)
			return &sor->settings[i];

	return NULL;
}

static void tegra_sor_hdmi_disable_scrambling(struct tegra_sor *sor)
{
	u32 value;

	value = tegra_sor_readl(sor, SOR_HDMI2_CTRL);
	value &= ~SOR_HDMI2_CTRL_CLOCK_MODE_DIV_BY_4;
	value &= ~SOR_HDMI2_CTRL_SCRAMBLE;
	tegra_sor_writel(sor, value, SOR_HDMI2_CTRL);
}

static void tegra_sor_hdmi_scdc_disable(struct tegra_sor *sor)
{
	struct i2c_adapter *ddc = sor->output.ddc;

	drm_scdc_set_high_tmds_clock_ratio(ddc, false);
	drm_scdc_set_scrambling(ddc, false);

	tegra_sor_hdmi_disable_scrambling(sor);
}

static void tegra_sor_hdmi_scdc_stop(struct tegra_sor *sor)
{
	if (sor->scdc_enabled) {
		cancel_delayed_work_sync(&sor->scdc);
		tegra_sor_hdmi_scdc_disable(sor);
	}
}

static void tegra_sor_hdmi_enable_scrambling(struct tegra_sor *sor)
{
	u32 value;

	value = tegra_sor_readl(sor, SOR_HDMI2_CTRL);
	value |= SOR_HDMI2_CTRL_CLOCK_MODE_DIV_BY_4;
	value |= SOR_HDMI2_CTRL_SCRAMBLE;
	tegra_sor_writel(sor, value, SOR_HDMI2_CTRL);
}

static void tegra_sor_hdmi_scdc_enable(struct tegra_sor *sor)
{
	struct i2c_adapter *ddc = sor->output.ddc;

	drm_scdc_set_high_tmds_clock_ratio(ddc, true);
	drm_scdc_set_scrambling(ddc, true);

	tegra_sor_hdmi_enable_scrambling(sor);
}

static void tegra_sor_hdmi_scdc_work(struct work_struct *work)
{
	struct tegra_sor *sor = container_of(work, struct tegra_sor, scdc.work);
	struct i2c_adapter *ddc = sor->output.ddc;

	if (!drm_scdc_get_scrambling_status(ddc)) {
		DRM_DEBUG_KMS("SCDC not scrambled\n");
		tegra_sor_hdmi_scdc_enable(sor);
	}

	schedule_delayed_work(&sor->scdc, msecs_to_jiffies(5000));
}

static void tegra_sor_hdmi_scdc_start(struct tegra_sor *sor)
{
	struct drm_scdc *scdc = &sor->output.connector.display_info.hdmi.scdc;
	struct drm_display_mode *mode;

	mode = &sor->output.encoder.crtc->state->adjusted_mode;

	if (mode->clock >= 340000 && scdc->supported) {
		schedule_delayed_work(&sor->scdc, msecs_to_jiffies(5000));
		tegra_sor_hdmi_scdc_enable(sor);
		sor->scdc_enabled = true;
	}
}

static void tegra_sor_hdmi_disable(struct drm_encoder *encoder)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct tegra_sor *sor = to_sor(output);
	u32 value;
	int err;

	tegra_sor_audio_unprepare(sor);
	tegra_sor_hdmi_scdc_stop(sor);

	err = tegra_sor_detach(sor);
	if (err < 0)
		dev_err(sor->dev, "failed to detach SOR: %d\n", err);

	tegra_sor_writel(sor, 0, SOR_STATE1);
	tegra_sor_update(sor);

	/* disable display to SOR clock */
	value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);

	if (!sor->soc->has_nvdisplay)
		value &= ~(SOR1_TIMING_CYA | SOR_ENABLE(1));
	else
		value &= ~SOR_ENABLE(sor->index);

	tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

	tegra_dc_commit(dc);

	err = tegra_sor_power_down(sor);
	if (err < 0)
		dev_err(sor->dev, "failed to power down SOR: %d\n", err);

	err = tegra_io_pad_power_disable(sor->pad);
	if (err < 0)
		dev_err(sor->dev, "failed to power off I/O pad: %d\n", err);

	pm_runtime_put(sor->dev);
}

static void tegra_sor_hdmi_enable(struct drm_encoder *encoder)
{
	struct tegra_output *output = encoder_to_output(encoder);
	unsigned int h_ref_to_sync = 1, pulse_start, max_ac;
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct tegra_sor_hdmi_settings *settings;
	struct tegra_sor *sor = to_sor(output);
	struct tegra_sor_state *state;
	struct drm_display_mode *mode;
	unsigned long rate, pclk;
	unsigned int div, i;
	u32 value;
	int err;

	state = to_sor_state(output->connector.state);
	mode = &encoder->crtc->state->adjusted_mode;
	pclk = mode->clock * 1000;

	pm_runtime_get_sync(sor->dev);

	/* switch to safe parent clock */
	err = tegra_sor_set_parent_clock(sor, sor->clk_safe);
	if (err < 0) {
		dev_err(sor->dev, "failed to set safe parent clock: %d\n", err);
		return;
	}

	div = clk_get_rate(sor->clk) / 1000000 * 4;

	err = tegra_io_pad_power_enable(sor->pad);
	if (err < 0)
		dev_err(sor->dev, "failed to power on I/O pad: %d\n", err);

	usleep_range(20, 100);

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value &= ~SOR_PLL2_BANDGAP_POWERDOWN;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	usleep_range(20, 100);

	value = tegra_sor_readl(sor, sor->soc->regs->pll3);
	value &= ~SOR_PLL3_PLL_VDD_MODE_3V3;
	tegra_sor_writel(sor, value, sor->soc->regs->pll3);

	value = tegra_sor_readl(sor, sor->soc->regs->pll0);
	value &= ~SOR_PLL0_VCOPD;
	value &= ~SOR_PLL0_PWR;
	tegra_sor_writel(sor, value, sor->soc->regs->pll0);

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value &= ~SOR_PLL2_SEQ_PLLCAPPD_ENFORCE;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	usleep_range(200, 400);

	value = tegra_sor_readl(sor, sor->soc->regs->pll2);
	value &= ~SOR_PLL2_POWERDOWN_OVERRIDE;
	value &= ~SOR_PLL2_PORT_POWERDOWN;
	tegra_sor_writel(sor, value, sor->soc->regs->pll2);

	usleep_range(20, 100);

	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value |= SOR_DP_PADCTL_PD_TXD_3 | SOR_DP_PADCTL_PD_TXD_0 |
		 SOR_DP_PADCTL_PD_TXD_1 | SOR_DP_PADCTL_PD_TXD_2;
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	while (true) {
		value = tegra_sor_readl(sor, SOR_LANE_SEQ_CTL);
		if ((value & SOR_LANE_SEQ_CTL_STATE_BUSY) == 0)
			break;

		usleep_range(250, 1000);
	}

	value = SOR_LANE_SEQ_CTL_TRIGGER | SOR_LANE_SEQ_CTL_SEQUENCE_DOWN |
		SOR_LANE_SEQ_CTL_POWER_STATE_UP | SOR_LANE_SEQ_CTL_DELAY(5);
	tegra_sor_writel(sor, value, SOR_LANE_SEQ_CTL);

	while (true) {
		value = tegra_sor_readl(sor, SOR_LANE_SEQ_CTL);
		if ((value & SOR_LANE_SEQ_CTL_TRIGGER) == 0)
			break;

		usleep_range(250, 1000);
	}

	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_LINK_SPEED_MASK;
	value &= ~SOR_CLK_CNTRL_DP_CLK_SEL_MASK;

	if (mode->clock < 340000) {
		DRM_DEBUG_KMS("setting 2.7 GHz link speed\n");
		value |= SOR_CLK_CNTRL_DP_LINK_SPEED_G2_70;
	} else {
		DRM_DEBUG_KMS("setting 5.4 GHz link speed\n");
		value |= SOR_CLK_CNTRL_DP_LINK_SPEED_G5_40;
	}

	value |= SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_PCLK;
	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	/* SOR pad PLL stabilization time */
	usleep_range(250, 1000);

	value = tegra_sor_readl(sor, SOR_DP_LINKCTL0);
	value &= ~SOR_DP_LINKCTL_LANE_COUNT_MASK;
	value |= SOR_DP_LINKCTL_LANE_COUNT(4);
	tegra_sor_writel(sor, value, SOR_DP_LINKCTL0);

	value = tegra_sor_readl(sor, SOR_DP_SPARE0);
	value &= ~SOR_DP_SPARE_DISP_VIDEO_PREAMBLE;
	value &= ~SOR_DP_SPARE_PANEL_INTERNAL;
	value &= ~SOR_DP_SPARE_SEQ_ENABLE;
	value &= ~SOR_DP_SPARE_MACRO_SOR_CLK;
	tegra_sor_writel(sor, value, SOR_DP_SPARE0);

	value = SOR_SEQ_CTL_PU_PC(0) | SOR_SEQ_CTL_PU_PC_ALT(0) |
		SOR_SEQ_CTL_PD_PC(8) | SOR_SEQ_CTL_PD_PC_ALT(8);
	tegra_sor_writel(sor, value, SOR_SEQ_CTL);

	value = SOR_SEQ_INST_DRIVE_PWM_OUT_LO | SOR_SEQ_INST_HALT |
		SOR_SEQ_INST_WAIT_VSYNC | SOR_SEQ_INST_WAIT(1);
	tegra_sor_writel(sor, value, SOR_SEQ_INST(0));
	tegra_sor_writel(sor, value, SOR_SEQ_INST(8));

	if (!sor->soc->has_nvdisplay) {
		/* program the reference clock */
		value = SOR_REFCLK_DIV_INT(div) | SOR_REFCLK_DIV_FRAC(div);
		tegra_sor_writel(sor, value, SOR_REFCLK);
	}

	/* XXX not in TRM */
	for (value = 0, i = 0; i < 5; i++)
		value |= SOR_XBAR_CTRL_LINK0_XSEL(i, sor->xbar_cfg[i]) |
			 SOR_XBAR_CTRL_LINK1_XSEL(i, i);

	tegra_sor_writel(sor, 0x00000000, SOR_XBAR_POL);
	tegra_sor_writel(sor, value, SOR_XBAR_CTRL);

	/* switch to parent clock */
	err = clk_set_parent(sor->clk, sor->clk_parent);
	if (err < 0) {
		dev_err(sor->dev, "failed to set parent clock: %d\n", err);
		return;
	}

	err = tegra_sor_set_parent_clock(sor, sor->clk_pad);
	if (err < 0) {
		dev_err(sor->dev, "failed to set pad clock: %d\n", err);
		return;
	}

	/* adjust clock rate for HDMI 2.0 modes */
	rate = clk_get_rate(sor->clk_parent);

	if (mode->clock >= 340000)
		rate /= 2;

	DRM_DEBUG_KMS("setting clock to %lu Hz, mode: %lu Hz\n", rate, pclk);

	clk_set_rate(sor->clk, rate);

	if (!sor->soc->has_nvdisplay) {
		value = SOR_INPUT_CONTROL_HDMI_SRC_SELECT(dc->pipe);

		/* XXX is this the proper check? */
		if (mode->clock < 75000)
			value |= SOR_INPUT_CONTROL_ARM_VIDEO_RANGE_LIMITED;

		tegra_sor_writel(sor, value, SOR_INPUT_CONTROL);
	}

	max_ac = ((mode->htotal - mode->hdisplay) - SOR_REKEY - 18) / 32;

	value = SOR_HDMI_CTRL_ENABLE | SOR_HDMI_CTRL_MAX_AC_PACKET(max_ac) |
		SOR_HDMI_CTRL_AUDIO_LAYOUT | SOR_HDMI_CTRL_REKEY(SOR_REKEY);
	tegra_sor_writel(sor, value, SOR_HDMI_CTRL);

	if (!dc->soc->has_nvdisplay) {
		/* H_PULSE2 setup */
		pulse_start = h_ref_to_sync +
			      (mode->hsync_end - mode->hsync_start) +
			      (mode->htotal - mode->hsync_end) - 10;

		value = PULSE_LAST_END_A | PULSE_QUAL_VACTIVE |
			PULSE_POLARITY_HIGH | PULSE_MODE_NORMAL;
		tegra_dc_writel(dc, value, DC_DISP_H_PULSE2_CONTROL);

		value = PULSE_END(pulse_start + 8) | PULSE_START(pulse_start);
		tegra_dc_writel(dc, value, DC_DISP_H_PULSE2_POSITION_A);

		value = tegra_dc_readl(dc, DC_DISP_DISP_SIGNAL_OPTIONS0);
		value |= H_PULSE2_ENABLE;
		tegra_dc_writel(dc, value, DC_DISP_DISP_SIGNAL_OPTIONS0);
	}

	/* infoframe setup */
	err = tegra_sor_hdmi_setup_avi_infoframe(sor, mode);
	if (err < 0)
		dev_err(sor->dev, "failed to setup AVI infoframe: %d\n", err);

	/* XXX HDMI audio support not implemented yet */
	tegra_sor_hdmi_disable_audio_infoframe(sor);

	/* use single TMDS protocol */
	value = tegra_sor_readl(sor, SOR_STATE1);
	value &= ~SOR_STATE_ASY_PROTOCOL_MASK;
	value |= SOR_STATE_ASY_PROTOCOL_SINGLE_TMDS_A;
	tegra_sor_writel(sor, value, SOR_STATE1);

	/* power up pad calibration */
	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value &= ~SOR_DP_PADCTL_PAD_CAL_PD;
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	/* production settings */
	settings = tegra_sor_hdmi_find_settings(sor, mode->clock * 1000);
	if (!settings) {
		dev_err(sor->dev, "no settings for pixel clock %d Hz\n",
			mode->clock * 1000);
		return;
	}

	value = tegra_sor_readl(sor, sor->soc->regs->pll0);
	value &= ~SOR_PLL0_ICHPMP_MASK;
	value &= ~SOR_PLL0_FILTER_MASK;
	value &= ~SOR_PLL0_VCOCAP_MASK;
	value |= SOR_PLL0_ICHPMP(settings->ichpmp);
	value |= SOR_PLL0_FILTER(settings->filter);
	value |= SOR_PLL0_VCOCAP(settings->vcocap);
	tegra_sor_writel(sor, value, sor->soc->regs->pll0);

	/* XXX not in TRM */
	value = tegra_sor_readl(sor, sor->soc->regs->pll1);
	value &= ~SOR_PLL1_LOADADJ_MASK;
	value &= ~SOR_PLL1_TMDS_TERMADJ_MASK;
	value |= SOR_PLL1_LOADADJ(settings->loadadj);
	value |= SOR_PLL1_TMDS_TERMADJ(settings->tmds_termadj);
	value |= SOR_PLL1_TMDS_TERM;
	tegra_sor_writel(sor, value, sor->soc->regs->pll1);

	value = tegra_sor_readl(sor, sor->soc->regs->pll3);
	value &= ~SOR_PLL3_BG_TEMP_COEF_MASK;
	value &= ~SOR_PLL3_BG_VREF_LEVEL_MASK;
	value &= ~SOR_PLL3_AVDD10_LEVEL_MASK;
	value &= ~SOR_PLL3_AVDD14_LEVEL_MASK;
	value |= SOR_PLL3_BG_TEMP_COEF(settings->bg_temp_coef);
	value |= SOR_PLL3_BG_VREF_LEVEL(settings->bg_vref_level);
	value |= SOR_PLL3_AVDD10_LEVEL(settings->avdd10_level);
	value |= SOR_PLL3_AVDD14_LEVEL(settings->avdd14_level);
	tegra_sor_writel(sor, value, sor->soc->regs->pll3);

	value = settings->drive_current[3] << 24 |
		settings->drive_current[2] << 16 |
		settings->drive_current[1] <<  8 |
		settings->drive_current[0] <<  0;
	tegra_sor_writel(sor, value, SOR_LANE_DRIVE_CURRENT0);

	value = settings->preemphasis[3] << 24 |
		settings->preemphasis[2] << 16 |
		settings->preemphasis[1] <<  8 |
		settings->preemphasis[0] <<  0;
	tegra_sor_writel(sor, value, SOR_LANE_PREEMPHASIS0);

	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value &= ~SOR_DP_PADCTL_TX_PU_MASK;
	value |= SOR_DP_PADCTL_TX_PU_ENABLE;
	value |= SOR_DP_PADCTL_TX_PU(settings->tx_pu_value);
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl2);
	value &= ~SOR_DP_PADCTL_SPAREPLL_MASK;
	value |= SOR_DP_PADCTL_SPAREPLL(settings->sparepll);
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl2);

	/* power down pad calibration */
	value = tegra_sor_readl(sor, sor->soc->regs->dp_padctl0);
	value |= SOR_DP_PADCTL_PAD_CAL_PD;
	tegra_sor_writel(sor, value, sor->soc->regs->dp_padctl0);

	if (!dc->soc->has_nvdisplay) {
		/* miscellaneous display controller settings */
		value = VSYNC_H_POSITION(1);
		tegra_dc_writel(dc, value, DC_DISP_DISP_TIMING_OPTIONS);
	}

	value = tegra_dc_readl(dc, DC_DISP_DISP_COLOR_CONTROL);
	value &= ~DITHER_CONTROL_MASK;
	value &= ~BASE_COLOR_SIZE_MASK;

	switch (state->bpc) {
	case 6:
		value |= BASE_COLOR_SIZE_666;
		break;

	case 8:
		value |= BASE_COLOR_SIZE_888;
		break;

	case 10:
		value |= BASE_COLOR_SIZE_101010;
		break;

	case 12:
		value |= BASE_COLOR_SIZE_121212;
		break;

	default:
		WARN(1, "%u bits-per-color not supported\n", state->bpc);
		value |= BASE_COLOR_SIZE_888;
		break;
	}

	tegra_dc_writel(dc, value, DC_DISP_DISP_COLOR_CONTROL);

	/* XXX set display head owner */
	value = tegra_sor_readl(sor, SOR_STATE1);
	value &= ~SOR_STATE_ASY_OWNER_MASK;
	value |= SOR_STATE_ASY_OWNER(1 + dc->pipe);
	tegra_sor_writel(sor, value, SOR_STATE1);

	err = tegra_sor_power_up(sor, 250);
	if (err < 0)
		dev_err(sor->dev, "failed to power up SOR: %d\n", err);

	/* configure dynamic range of output */
	value = tegra_sor_readl(sor, sor->soc->regs->head_state0 + dc->pipe);
	value &= ~SOR_HEAD_STATE_RANGECOMPRESS_MASK;
	value &= ~SOR_HEAD_STATE_DYNRANGE_MASK;
	tegra_sor_writel(sor, value, sor->soc->regs->head_state0 + dc->pipe);

	/* configure colorspace */
	value = tegra_sor_readl(sor, sor->soc->regs->head_state0 + dc->pipe);
	value &= ~SOR_HEAD_STATE_COLORSPACE_MASK;
	value |= SOR_HEAD_STATE_COLORSPACE_RGB;
	tegra_sor_writel(sor, value, sor->soc->regs->head_state0 + dc->pipe);

	tegra_sor_mode_set(sor, mode, state);

	tegra_sor_update(sor);

	/* program preamble timing in SOR (XXX) */
	value = tegra_sor_readl(sor, SOR_DP_SPARE0);
	value &= ~SOR_DP_SPARE_DISP_VIDEO_PREAMBLE;
	tegra_sor_writel(sor, value, SOR_DP_SPARE0);

	err = tegra_sor_attach(sor);
	if (err < 0)
		dev_err(sor->dev, "failed to attach SOR: %d\n", err);

	/* enable display to SOR clock and generate HDMI preamble */
	value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);

	if (!sor->soc->has_nvdisplay)
		value |= SOR_ENABLE(1) | SOR1_TIMING_CYA;
	else
		value |= SOR_ENABLE(sor->index);

	tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

	if (dc->soc->has_nvdisplay) {
		value = tegra_dc_readl(dc, DC_DISP_CORE_SOR_SET_CONTROL(sor->index));
		value &= ~PROTOCOL_MASK;
		value |= PROTOCOL_SINGLE_TMDS_A;
		tegra_dc_writel(dc, value, DC_DISP_CORE_SOR_SET_CONTROL(sor->index));
	}

	tegra_dc_commit(dc);

	err = tegra_sor_wakeup(sor);
	if (err < 0)
		dev_err(sor->dev, "failed to wakeup SOR: %d\n", err);

	tegra_sor_hdmi_scdc_start(sor);
	tegra_sor_audio_prepare(sor);
}

static const struct drm_encoder_helper_funcs tegra_sor_hdmi_helpers = {
	.disable = tegra_sor_hdmi_disable,
	.enable = tegra_sor_hdmi_enable,
	.atomic_check = tegra_sor_encoder_atomic_check,
};

static int tegra_sor_init(struct host1x_client *client)
{
	struct drm_device *drm = dev_get_drvdata(client->parent);
	const struct drm_encoder_helper_funcs *helpers = NULL;
	struct tegra_sor *sor = host1x_client_to_sor(client);
	int connector = DRM_MODE_CONNECTOR_Unknown;
	int encoder = DRM_MODE_ENCODER_NONE;
	u32 value;
	int err;

	if (!sor->aux) {
		if (sor->soc->supports_hdmi) {
			connector = DRM_MODE_CONNECTOR_HDMIA;
			encoder = DRM_MODE_ENCODER_TMDS;
			helpers = &tegra_sor_hdmi_helpers;
		} else if (sor->soc->supports_lvds) {
			connector = DRM_MODE_CONNECTOR_LVDS;
			encoder = DRM_MODE_ENCODER_LVDS;
		}
	} else {
		if (sor->soc->supports_edp) {
			connector = DRM_MODE_CONNECTOR_eDP;
			encoder = DRM_MODE_ENCODER_TMDS;
			helpers = &tegra_sor_edp_helpers;
		} else if (sor->soc->supports_dp) {
			connector = DRM_MODE_CONNECTOR_DisplayPort;
			encoder = DRM_MODE_ENCODER_TMDS;
		}
	}

	sor->output.dev = sor->dev;

	drm_connector_init(drm, &sor->output.connector,
			   &tegra_sor_connector_funcs,
			   connector);
	drm_connector_helper_add(&sor->output.connector,
				 &tegra_sor_connector_helper_funcs);
	sor->output.connector.dpms = DRM_MODE_DPMS_OFF;

	drm_encoder_init(drm, &sor->output.encoder, &tegra_sor_encoder_funcs,
			 encoder, NULL);
	drm_encoder_helper_add(&sor->output.encoder, helpers);

	drm_connector_attach_encoder(&sor->output.connector,
					  &sor->output.encoder);
	drm_connector_register(&sor->output.connector);

	err = tegra_output_init(drm, &sor->output);
	if (err < 0) {
		dev_err(client->dev, "failed to initialize output: %d\n", err);
		return err;
	}

	tegra_output_find_possible_crtcs(&sor->output, drm);

	if (sor->aux) {
		err = drm_dp_aux_attach(sor->aux, &sor->output);
		if (err < 0) {
			dev_err(sor->dev, "failed to attach DP: %d\n", err);
			return err;
		}
	}

	/*
	 * XXX: Remove this reset once proper hand-over from firmware to
	 * kernel is possible.
	 */
	if (sor->rst) {
		err = reset_control_acquire(sor->rst);
		if (err < 0) {
			dev_err(sor->dev, "failed to acquire SOR reset: %d\n",
				err);
			return err;
		}

		err = reset_control_assert(sor->rst);
		if (err < 0) {
			dev_err(sor->dev, "failed to assert SOR reset: %d\n",
				err);
			return err;
		}
	}

	err = clk_prepare_enable(sor->clk);
	if (err < 0) {
		dev_err(sor->dev, "failed to enable clock: %d\n", err);
		return err;
	}

	usleep_range(1000, 3000);

	if (sor->rst) {
		err = reset_control_deassert(sor->rst);
		if (err < 0) {
			dev_err(sor->dev, "failed to deassert SOR reset: %d\n",
				err);
			return err;
		}

		reset_control_release(sor->rst);
	}

	err = clk_prepare_enable(sor->clk_safe);
	if (err < 0)
		return err;

	err = clk_prepare_enable(sor->clk_dp);
	if (err < 0)
		return err;

	/*
	 * Enable and unmask the HDA codec SCRATCH0 register interrupt. This
	 * is used for interoperability between the HDA codec driver and the
	 * HDMI/DP driver.
	 */
	value = SOR_INT_CODEC_SCRATCH1 | SOR_INT_CODEC_SCRATCH0;
	tegra_sor_writel(sor, value, SOR_INT_ENABLE);
	tegra_sor_writel(sor, value, SOR_INT_MASK);

	return 0;
}

static int tegra_sor_exit(struct host1x_client *client)
{
	struct tegra_sor *sor = host1x_client_to_sor(client);
	int err;

	tegra_sor_writel(sor, 0, SOR_INT_MASK);
	tegra_sor_writel(sor, 0, SOR_INT_ENABLE);

	tegra_output_exit(&sor->output);

	if (sor->aux) {
		err = drm_dp_aux_detach(sor->aux);
		if (err < 0) {
			dev_err(sor->dev, "failed to detach DP: %d\n", err);
			return err;
		}
	}

	clk_disable_unprepare(sor->clk_safe);
	clk_disable_unprepare(sor->clk_dp);
	clk_disable_unprepare(sor->clk);

	return 0;
}

static const struct host1x_client_ops sor_client_ops = {
	.init = tegra_sor_init,
	.exit = tegra_sor_exit,
};

static const struct tegra_sor_ops tegra_sor_edp_ops = {
	.name = "eDP",
};

static int tegra_sor_hdmi_probe(struct tegra_sor *sor)
{
	int err;

	sor->avdd_io_supply = devm_regulator_get(sor->dev, "avdd-io");
	if (IS_ERR(sor->avdd_io_supply)) {
		dev_err(sor->dev, "cannot get AVDD I/O supply: %ld\n",
			PTR_ERR(sor->avdd_io_supply));
		return PTR_ERR(sor->avdd_io_supply);
	}

	err = regulator_enable(sor->avdd_io_supply);
	if (err < 0) {
		dev_err(sor->dev, "failed to enable AVDD I/O supply: %d\n",
			err);
		return err;
	}

	sor->vdd_pll_supply = devm_regulator_get(sor->dev, "vdd-pll");
	if (IS_ERR(sor->vdd_pll_supply)) {
		dev_err(sor->dev, "cannot get VDD PLL supply: %ld\n",
			PTR_ERR(sor->vdd_pll_supply));
		return PTR_ERR(sor->vdd_pll_supply);
	}

	err = regulator_enable(sor->vdd_pll_supply);
	if (err < 0) {
		dev_err(sor->dev, "failed to enable VDD PLL supply: %d\n",
			err);
		return err;
	}

	sor->hdmi_supply = devm_regulator_get(sor->dev, "hdmi");
	if (IS_ERR(sor->hdmi_supply)) {
		dev_err(sor->dev, "cannot get HDMI supply: %ld\n",
			PTR_ERR(sor->hdmi_supply));
		return PTR_ERR(sor->hdmi_supply);
	}

	err = regulator_enable(sor->hdmi_supply);
	if (err < 0) {
		dev_err(sor->dev, "failed to enable HDMI supply: %d\n", err);
		return err;
	}

	INIT_DELAYED_WORK(&sor->scdc, tegra_sor_hdmi_scdc_work);

	return 0;
}

static int tegra_sor_hdmi_remove(struct tegra_sor *sor)
{
	regulator_disable(sor->hdmi_supply);
	regulator_disable(sor->vdd_pll_supply);
	regulator_disable(sor->avdd_io_supply);

	return 0;
}

static const struct tegra_sor_ops tegra_sor_hdmi_ops = {
	.name = "HDMI",
	.probe = tegra_sor_hdmi_probe,
	.remove = tegra_sor_hdmi_remove,
};

static const u8 tegra124_sor_xbar_cfg[5] = {
	0, 1, 2, 3, 4
};

static const struct tegra_sor_regs tegra124_sor_regs = {
	.head_state0 = 0x05,
	.head_state1 = 0x07,
	.head_state2 = 0x09,
	.head_state3 = 0x0b,
	.head_state4 = 0x0d,
	.head_state5 = 0x0f,
	.pll0 = 0x17,
	.pll1 = 0x18,
	.pll2 = 0x19,
	.pll3 = 0x1a,
	.dp_padctl0 = 0x5c,
	.dp_padctl2 = 0x73,
};

static const struct tegra_sor_soc tegra124_sor = {
	.supports_edp = true,
	.supports_lvds = true,
	.supports_hdmi = false,
	.supports_dp = false,
	.regs = &tegra124_sor_regs,
	.has_nvdisplay = false,
	.xbar_cfg = tegra124_sor_xbar_cfg,
};

static const struct tegra_sor_regs tegra210_sor_regs = {
	.head_state0 = 0x05,
	.head_state1 = 0x07,
	.head_state2 = 0x09,
	.head_state3 = 0x0b,
	.head_state4 = 0x0d,
	.head_state5 = 0x0f,
	.pll0 = 0x17,
	.pll1 = 0x18,
	.pll2 = 0x19,
	.pll3 = 0x1a,
	.dp_padctl0 = 0x5c,
	.dp_padctl2 = 0x73,
};

static const struct tegra_sor_soc tegra210_sor = {
	.supports_edp = true,
	.supports_lvds = false,
	.supports_hdmi = false,
	.supports_dp = false,
	.regs = &tegra210_sor_regs,
	.has_nvdisplay = false,
	.xbar_cfg = tegra124_sor_xbar_cfg,
};

static const u8 tegra210_sor_xbar_cfg[5] = {
	2, 1, 0, 3, 4
};

static const struct tegra_sor_soc tegra210_sor1 = {
	.supports_edp = false,
	.supports_lvds = false,
	.supports_hdmi = true,
	.supports_dp = true,

	.regs = &tegra210_sor_regs,
	.has_nvdisplay = false,

	.num_settings = ARRAY_SIZE(tegra210_sor_hdmi_defaults),
	.settings = tegra210_sor_hdmi_defaults,

	.xbar_cfg = tegra210_sor_xbar_cfg,
};

static const struct tegra_sor_regs tegra186_sor_regs = {
	.head_state0 = 0x151,
	.head_state1 = 0x154,
	.head_state2 = 0x157,
	.head_state3 = 0x15a,
	.head_state4 = 0x15d,
	.head_state5 = 0x160,
	.pll0 = 0x163,
	.pll1 = 0x164,
	.pll2 = 0x165,
	.pll3 = 0x166,
	.dp_padctl0 = 0x168,
	.dp_padctl2 = 0x16a,
};

static const struct tegra_sor_soc tegra186_sor = {
	.supports_edp = false,
	.supports_lvds = false,
	.supports_hdmi = false,
	.supports_dp = true,

	.regs = &tegra186_sor_regs,
	.has_nvdisplay = true,

	.xbar_cfg = tegra124_sor_xbar_cfg,
};

static const struct tegra_sor_soc tegra186_sor1 = {
	.supports_edp = false,
	.supports_lvds = false,
	.supports_hdmi = true,
	.supports_dp = true,

	.regs = &tegra186_sor_regs,
	.has_nvdisplay = true,

	.num_settings = ARRAY_SIZE(tegra186_sor_hdmi_defaults),
	.settings = tegra186_sor_hdmi_defaults,

	.xbar_cfg = tegra124_sor_xbar_cfg,
};

static const struct tegra_sor_regs tegra194_sor_regs = {
	.head_state0 = 0x151,
	.head_state1 = 0x155,
	.head_state2 = 0x159,
	.head_state3 = 0x15d,
	.head_state4 = 0x161,
	.head_state5 = 0x165,
	.pll0 = 0x169,
	.pll1 = 0x16a,
	.pll2 = 0x16b,
	.pll3 = 0x16c,
	.dp_padctl0 = 0x16e,
	.dp_padctl2 = 0x16f,
};

static const struct tegra_sor_soc tegra194_sor = {
	.supports_edp = true,
	.supports_lvds = false,
	.supports_hdmi = true,
	.supports_dp = true,

	.regs = &tegra194_sor_regs,
	.has_nvdisplay = true,

	.num_settings = ARRAY_SIZE(tegra194_sor_hdmi_defaults),
	.settings = tegra194_sor_hdmi_defaults,

	.xbar_cfg = tegra210_sor_xbar_cfg,
};

static const struct of_device_id tegra_sor_of_match[] = {
	{ .compatible = "nvidia,tegra194-sor", .data = &tegra194_sor },
	{ .compatible = "nvidia,tegra186-sor1", .data = &tegra186_sor1 },
	{ .compatible = "nvidia,tegra186-sor", .data = &tegra186_sor },
	{ .compatible = "nvidia,tegra210-sor1", .data = &tegra210_sor1 },
	{ .compatible = "nvidia,tegra210-sor", .data = &tegra210_sor },
	{ .compatible = "nvidia,tegra124-sor", .data = &tegra124_sor },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_sor_of_match);

static int tegra_sor_parse_dt(struct tegra_sor *sor)
{
	struct device_node *np = sor->dev->of_node;
	u32 xbar_cfg[5];
	unsigned int i;
	u32 value;
	int err;

	if (sor->soc->has_nvdisplay) {
		err = of_property_read_u32(np, "nvidia,interface", &value);
		if (err < 0)
			return err;

		sor->index = value;

		/*
		 * override the default that we already set for Tegra210 and
		 * earlier
		 */
		sor->pad = TEGRA_IO_PAD_HDMI_DP0 + sor->index;
	}

	err = of_property_read_u32_array(np, "nvidia,xbar-cfg", xbar_cfg, 5);
	if (err < 0) {
		/* fall back to default per-SoC XBAR configuration */
		for (i = 0; i < 5; i++)
			sor->xbar_cfg[i] = sor->soc->xbar_cfg[i];
	} else {
		/* copy cells to SOR XBAR configuration */
		for (i = 0; i < 5; i++)
			sor->xbar_cfg[i] = xbar_cfg[i];
	}

	return 0;
}

static irqreturn_t tegra_sor_irq(int irq, void *data)
{
	struct tegra_sor *sor = data;
	u32 value;

	value = tegra_sor_readl(sor, SOR_INT_STATUS);
	tegra_sor_writel(sor, value, SOR_INT_STATUS);

	if (value & SOR_INT_CODEC_SCRATCH0) {
		value = tegra_sor_readl(sor, SOR_AUDIO_HDA_CODEC_SCRATCH0);

		if (value & SOR_AUDIO_HDA_CODEC_SCRATCH0_VALID) {
			unsigned int format;

			format = value & SOR_AUDIO_HDA_CODEC_SCRATCH0_FMT_MASK;

			tegra_hda_parse_format(format, &sor->format);

			tegra_sor_hdmi_audio_enable(sor);
		} else {
			tegra_sor_hdmi_audio_disable(sor);
		}
	}

	return IRQ_HANDLED;
}

static int tegra_sor_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct tegra_sor *sor;
	struct resource *regs;
	int err;

	sor = devm_kzalloc(&pdev->dev, sizeof(*sor), GFP_KERNEL);
	if (!sor)
		return -ENOMEM;

	sor->soc = of_device_get_match_data(&pdev->dev);
	sor->output.dev = sor->dev = &pdev->dev;

	sor->settings = devm_kmemdup(&pdev->dev, sor->soc->settings,
				     sor->soc->num_settings *
					sizeof(*sor->settings),
				     GFP_KERNEL);
	if (!sor->settings)
		return -ENOMEM;

	sor->num_settings = sor->soc->num_settings;

	np = of_parse_phandle(pdev->dev.of_node, "nvidia,dpaux", 0);
	if (np) {
		sor->aux = drm_dp_aux_find_by_of_node(np);
		of_node_put(np);

		if (!sor->aux)
			return -EPROBE_DEFER;
	}

	if (!sor->aux) {
		if (sor->soc->supports_hdmi) {
			sor->ops = &tegra_sor_hdmi_ops;
			sor->pad = TEGRA_IO_PAD_HDMI;
		} else if (sor->soc->supports_lvds) {
			dev_err(&pdev->dev, "LVDS not supported yet\n");
			return -ENODEV;
		} else {
			dev_err(&pdev->dev, "unknown (non-DP) support\n");
			return -ENODEV;
		}
	} else {
		if (sor->soc->supports_edp) {
			sor->ops = &tegra_sor_edp_ops;
			sor->pad = TEGRA_IO_PAD_LVDS;
		} else if (sor->soc->supports_dp) {
			dev_err(&pdev->dev, "DisplayPort not supported yet\n");
			return -ENODEV;
		} else {
			dev_err(&pdev->dev, "unknown (DP) support\n");
			return -ENODEV;
		}
	}

	err = tegra_sor_parse_dt(sor);
	if (err < 0)
		return err;

	err = tegra_output_probe(&sor->output);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to probe output: %d\n", err);
		return err;
	}

	if (sor->ops && sor->ops->probe) {
		err = sor->ops->probe(sor);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to probe %s: %d\n",
				sor->ops->name, err);
			goto output;
		}
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sor->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(sor->regs)) {
		err = PTR_ERR(sor->regs);
		goto remove;
	}

	err = platform_get_irq(pdev, 0);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get IRQ: %d\n", err);
		goto remove;
	}

	sor->irq = err;

	err = devm_request_irq(sor->dev, sor->irq, tegra_sor_irq, 0,
			       dev_name(sor->dev), sor);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ: %d\n", err);
		goto remove;
	}

	sor->rst = devm_reset_control_get_exclusive_released(&pdev->dev, "sor");
	if (IS_ERR(sor->rst)) {
		err = PTR_ERR(sor->rst);

		if (err != -EBUSY || WARN_ON(!pdev->dev.pm_domain)) {
			dev_err(&pdev->dev, "failed to get reset control: %d\n",
				err);
			goto remove;
		}

		/*
		 * At this point, the reset control is most likely being used
		 * by the generic power domain implementation. With any luck
		 * the power domain will have taken care of resetting the SOR
		 * and we don't have to do anything.
		 */
		sor->rst = NULL;
	}

	sor->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sor->clk)) {
		err = PTR_ERR(sor->clk);
		dev_err(&pdev->dev, "failed to get module clock: %d\n", err);
		goto remove;
	}

	if (sor->soc->supports_hdmi || sor->soc->supports_dp) {
		struct device_node *np = pdev->dev.of_node;
		const char *name;

		/*
		 * For backwards compatibility with Tegra210 device trees,
		 * fall back to the old clock name "source" if the new "out"
		 * clock is not available.
		 */
		if (of_property_match_string(np, "clock-names", "out") < 0)
			name = "source";
		else
			name = "out";

		sor->clk_out = devm_clk_get(&pdev->dev, name);
		if (IS_ERR(sor->clk_out)) {
			err = PTR_ERR(sor->clk_out);
			dev_err(sor->dev, "failed to get %s clock: %d\n",
				name, err);
			goto remove;
		}
	} else {
		/* fall back to the module clock on SOR0 (eDP/LVDS only) */
		sor->clk_out = sor->clk;
	}

	sor->clk_parent = devm_clk_get(&pdev->dev, "parent");
	if (IS_ERR(sor->clk_parent)) {
		err = PTR_ERR(sor->clk_parent);
		dev_err(&pdev->dev, "failed to get parent clock: %d\n", err);
		goto remove;
	}

	sor->clk_safe = devm_clk_get(&pdev->dev, "safe");
	if (IS_ERR(sor->clk_safe)) {
		err = PTR_ERR(sor->clk_safe);
		dev_err(&pdev->dev, "failed to get safe clock: %d\n", err);
		goto remove;
	}

	sor->clk_dp = devm_clk_get(&pdev->dev, "dp");
	if (IS_ERR(sor->clk_dp)) {
		err = PTR_ERR(sor->clk_dp);
		dev_err(&pdev->dev, "failed to get DP clock: %d\n", err);
		goto remove;
	}

	/*
	 * Starting with Tegra186, the BPMP provides an implementation for
	 * the pad output clock, so we have to look it up from device tree.
	 */
	sor->clk_pad = devm_clk_get(&pdev->dev, "pad");
	if (IS_ERR(sor->clk_pad)) {
		if (sor->clk_pad != ERR_PTR(-ENOENT)) {
			err = PTR_ERR(sor->clk_pad);
			goto remove;
		}

		/*
		 * If the pad output clock is not available, then we assume
		 * we're on Tegra210 or earlier and have to provide our own
		 * implementation.
		 */
		sor->clk_pad = NULL;
	}

	/*
	 * The bootloader may have set up the SOR such that it's module clock
	 * is sourced by one of the display PLLs. However, that doesn't work
	 * without properly having set up other bits of the SOR.
	 */
	err = clk_set_parent(sor->clk_out, sor->clk_safe);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to use safe clock: %d\n", err);
		goto remove;
	}

	platform_set_drvdata(pdev, sor);
	pm_runtime_enable(&pdev->dev);

	/*
	 * On Tegra210 and earlier, provide our own implementation for the
	 * pad output clock.
	 */
	if (!sor->clk_pad) {
		err = pm_runtime_get_sync(&pdev->dev);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to get runtime PM: %d\n",
				err);
			goto remove;
		}

		sor->clk_pad = tegra_clk_sor_pad_register(sor,
							  "sor1_pad_clkout");
		pm_runtime_put(&pdev->dev);
	}

	if (IS_ERR(sor->clk_pad)) {
		err = PTR_ERR(sor->clk_pad);
		dev_err(&pdev->dev, "failed to register SOR pad clock: %d\n",
			err);
		goto remove;
	}

	INIT_LIST_HEAD(&sor->client.list);
	sor->client.ops = &sor_client_ops;
	sor->client.dev = &pdev->dev;

	err = host1x_client_register(&sor->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		goto remove;
	}

	return 0;

remove:
	if (sor->ops && sor->ops->remove)
		sor->ops->remove(sor);
output:
	tegra_output_remove(&sor->output);
	return err;
}

static int tegra_sor_remove(struct platform_device *pdev)
{
	struct tegra_sor *sor = platform_get_drvdata(pdev);
	int err;

	pm_runtime_disable(&pdev->dev);

	err = host1x_client_unregister(&sor->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	if (sor->ops && sor->ops->remove) {
		err = sor->ops->remove(sor);
		if (err < 0)
			dev_err(&pdev->dev, "failed to remove SOR: %d\n", err);
	}

	tegra_output_remove(&sor->output);

	return 0;
}

#ifdef CONFIG_PM
static int tegra_sor_suspend(struct device *dev)
{
	struct tegra_sor *sor = dev_get_drvdata(dev);
	int err;

	if (sor->rst) {
		err = reset_control_assert(sor->rst);
		if (err < 0) {
			dev_err(dev, "failed to assert reset: %d\n", err);
			return err;
		}

		reset_control_release(sor->rst);
	}

	usleep_range(1000, 2000);

	clk_disable_unprepare(sor->clk);

	return 0;
}

static int tegra_sor_resume(struct device *dev)
{
	struct tegra_sor *sor = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(sor->clk);
	if (err < 0) {
		dev_err(dev, "failed to enable clock: %d\n", err);
		return err;
	}

	usleep_range(1000, 2000);

	if (sor->rst) {
		err = reset_control_acquire(sor->rst);
		if (err < 0) {
			dev_err(dev, "failed to acquire reset: %d\n", err);
			clk_disable_unprepare(sor->clk);
			return err;
		}

		err = reset_control_deassert(sor->rst);
		if (err < 0) {
			dev_err(dev, "failed to deassert reset: %d\n", err);
			reset_control_release(sor->rst);
			clk_disable_unprepare(sor->clk);
			return err;
		}
	}

	return 0;
}
#endif

static const struct dev_pm_ops tegra_sor_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_sor_suspend, tegra_sor_resume, NULL)
};

struct platform_driver tegra_sor_driver = {
	.driver = {
		.name = "tegra-sor",
		.of_match_table = tegra_sor_of_match,
		.pm = &tegra_sor_pm_ops,
	},
	.probe = tegra_sor_probe,
	.remove = tegra_sor_remove,
};
