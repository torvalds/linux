/*
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/tegra-powergate.h>

#include <drm/drm_dp_helper.h>

#include "dc.h"
#include "drm.h"
#include "sor.h"

struct tegra_sor {
	struct host1x_client client;
	struct tegra_output output;
	struct device *dev;

	void __iomem *regs;

	struct reset_control *rst;
	struct clk *clk_parent;
	struct clk *clk_safe;
	struct clk *clk_dp;
	struct clk *clk;

	struct tegra_dpaux *dpaux;

	bool enabled;
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

static inline unsigned long tegra_sor_readl(struct tegra_sor *sor,
					    unsigned long offset)
{
	return readl(sor->regs + (offset << 2));
}

static inline void tegra_sor_writel(struct tegra_sor *sor, unsigned long value,
				    unsigned long offset)
{
	writel(value, sor->regs + (offset << 2));
}

static int tegra_sor_dp_train_fast(struct tegra_sor *sor,
				   struct drm_dp_link *link)
{
	unsigned long value;
	unsigned int i;
	u8 pattern;
	int err;

	/* setup lane parameters */
	value = SOR_LANE_DRIVE_CURRENT_LANE3(0x40) |
		SOR_LANE_DRIVE_CURRENT_LANE2(0x40) |
		SOR_LANE_DRIVE_CURRENT_LANE1(0x40) |
		SOR_LANE_DRIVE_CURRENT_LANE0(0x40);
	tegra_sor_writel(sor, value, SOR_LANE_DRIVE_CURRENT_0);

	value = SOR_LANE_PREEMPHASIS_LANE3(0x0f) |
		SOR_LANE_PREEMPHASIS_LANE2(0x0f) |
		SOR_LANE_PREEMPHASIS_LANE1(0x0f) |
		SOR_LANE_PREEMPHASIS_LANE0(0x0f);
	tegra_sor_writel(sor, value, SOR_LANE_PREEMPHASIS_0);

	value = SOR_LANE_POST_CURSOR_LANE3(0x00) |
		SOR_LANE_POST_CURSOR_LANE2(0x00) |
		SOR_LANE_POST_CURSOR_LANE1(0x00) |
		SOR_LANE_POST_CURSOR_LANE0(0x00);
	tegra_sor_writel(sor, value, SOR_LANE_POST_CURSOR_0);

	/* disable LVDS mode */
	tegra_sor_writel(sor, 0, SOR_LVDS);

	value = tegra_sor_readl(sor, SOR_DP_PADCTL_0);
	value |= SOR_DP_PADCTL_TX_PU_ENABLE;
	value &= ~SOR_DP_PADCTL_TX_PU_MASK;
	value |= SOR_DP_PADCTL_TX_PU(2); /* XXX: don't hardcode? */
	tegra_sor_writel(sor, value, SOR_DP_PADCTL_0);

	value = tegra_sor_readl(sor, SOR_DP_PADCTL_0);
	value |= SOR_DP_PADCTL_CM_TXD_3 | SOR_DP_PADCTL_CM_TXD_2 |
		 SOR_DP_PADCTL_CM_TXD_1 | SOR_DP_PADCTL_CM_TXD_0;
	tegra_sor_writel(sor, value, SOR_DP_PADCTL_0);

	usleep_range(10, 100);

	value = tegra_sor_readl(sor, SOR_DP_PADCTL_0);
	value &= ~(SOR_DP_PADCTL_CM_TXD_3 | SOR_DP_PADCTL_CM_TXD_2 |
		   SOR_DP_PADCTL_CM_TXD_1 | SOR_DP_PADCTL_CM_TXD_0);
	tegra_sor_writel(sor, value, SOR_DP_PADCTL_0);

	err = tegra_dpaux_prepare(sor->dpaux, DP_SET_ANSI_8B10B);
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

	err = tegra_dpaux_train(sor->dpaux, link, pattern);
	if (err < 0)
		return err;

	value = tegra_sor_readl(sor, SOR_DP_SPARE_0);
	value |= SOR_DP_SPARE_SEQ_ENABLE;
	value &= ~SOR_DP_SPARE_PANEL_INTERNAL;
	value |= SOR_DP_SPARE_MACRO_SOR_CLK;
	tegra_sor_writel(sor, value, SOR_DP_SPARE_0);

	for (i = 0, value = 0; i < link->num_lanes; i++) {
		unsigned long lane = SOR_DP_TPG_CHANNEL_CODING |
				     SOR_DP_TPG_SCRAMBLER_NONE |
				     SOR_DP_TPG_PATTERN_TRAIN2;
		value = (value << 8) | lane;
	}

	tegra_sor_writel(sor, value, SOR_DP_TPG);

	pattern = DP_LINK_SCRAMBLING_DISABLE | DP_TRAINING_PATTERN_2;

	err = tegra_dpaux_train(sor->dpaux, link, pattern);
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

	err = tegra_dpaux_train(sor->dpaux, link, pattern);
	if (err < 0)
		return err;

	return 0;
}

static void tegra_sor_super_update(struct tegra_sor *sor)
{
	tegra_sor_writel(sor, 0, SOR_SUPER_STATE_0);
	tegra_sor_writel(sor, 1, SOR_SUPER_STATE_0);
	tegra_sor_writel(sor, 0, SOR_SUPER_STATE_0);
}

static void tegra_sor_update(struct tegra_sor *sor)
{
	tegra_sor_writel(sor, 0, SOR_STATE_0);
	tegra_sor_writel(sor, 1, SOR_STATE_0);
	tegra_sor_writel(sor, 0, SOR_STATE_0);
}

static int tegra_sor_setup_pwm(struct tegra_sor *sor, unsigned long timeout)
{
	unsigned long value;

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
	value = tegra_sor_readl(sor, SOR_SUPER_STATE_1);
	value |= SOR_SUPER_STATE_HEAD_MODE_AWAKE;
	value |= SOR_SUPER_STATE_MODE_NORMAL;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE_1);
	tegra_sor_super_update(sor);

	/* attach */
	value = tegra_sor_readl(sor, SOR_SUPER_STATE_1);
	value |= SOR_SUPER_STATE_ATTACHED;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE_1);
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
	struct tegra_dc *dc = to_tegra_dc(sor->output.encoder.crtc);
	unsigned long value, timeout;

	/* enable display controller outputs */
	value = tegra_dc_readl(dc, DC_CMD_DISPLAY_POWER_CONTROL);
	value |= PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
		 PW4_ENABLE | PM0_ENABLE | PM1_ENABLE;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_POWER_CONTROL);

	tegra_dc_writel(dc, GENERAL_ACT_REQ << 8, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, GENERAL_ACT_REQ, DC_CMD_STATE_CONTROL);

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
	unsigned long value;

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

static int tegra_output_sor_enable(struct tegra_output *output)
{
	struct tegra_dc *dc = to_tegra_dc(output->encoder.crtc);
	struct drm_display_mode *mode = &dc->base.mode;
	unsigned int vbe, vse, hbe, hse, vbs, hbs, i;
	struct tegra_sor *sor = to_sor(output);
	unsigned long value;
	int err;

	if (sor->enabled)
		return 0;

	err = clk_prepare_enable(sor->clk);
	if (err < 0)
		return err;

	reset_control_deassert(sor->rst);

	if (sor->dpaux) {
		err = tegra_dpaux_enable(sor->dpaux);
		if (err < 0)
			dev_err(sor->dev, "failed to enable DP: %d\n", err);
	}

	err = clk_set_parent(sor->clk, sor->clk_safe);
	if (err < 0)
		dev_err(sor->dev, "failed to set safe parent clock: %d\n", err);

	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_CLK_SEL_MASK;
	value |= SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_DPCLK;
	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	value = tegra_sor_readl(sor, SOR_PLL_2);
	value &= ~SOR_PLL_2_BANDGAP_POWERDOWN;
	tegra_sor_writel(sor, value, SOR_PLL_2);
	usleep_range(20, 100);

	value = tegra_sor_readl(sor, SOR_PLL_3);
	value |= SOR_PLL_3_PLL_VDD_MODE_V3_3;
	tegra_sor_writel(sor, value, SOR_PLL_3);

	value = SOR_PLL_0_ICHPMP(0xf) | SOR_PLL_0_VCOCAP_RST |
		SOR_PLL_0_PLLREG_LEVEL_V45 | SOR_PLL_0_RESISTOR_EXT;
	tegra_sor_writel(sor, value, SOR_PLL_0);

	value = tegra_sor_readl(sor, SOR_PLL_2);
	value |= SOR_PLL_2_SEQ_PLLCAPPD;
	value &= ~SOR_PLL_2_SEQ_PLLCAPPD_ENFORCE;
	value |= SOR_PLL_2_LVDS_ENABLE;
	tegra_sor_writel(sor, value, SOR_PLL_2);

	value = SOR_PLL_1_TERM_COMPOUT | SOR_PLL_1_TMDS_TERM;
	tegra_sor_writel(sor, value, SOR_PLL_1);

	while (true) {
		value = tegra_sor_readl(sor, SOR_PLL_2);
		if ((value & SOR_PLL_2_SEQ_PLLCAPPD_ENFORCE) == 0)
			break;

		usleep_range(250, 1000);
	}

	value = tegra_sor_readl(sor, SOR_PLL_2);
	value &= ~SOR_PLL_2_POWERDOWN_OVERRIDE;
	value &= ~SOR_PLL_2_PORT_POWERDOWN;
	tegra_sor_writel(sor, value, SOR_PLL_2);

	/*
	 * power up
	 */

	/* set safe link bandwidth (1.62 Gbps) */
	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_LINK_SPEED_MASK;
	value |= SOR_CLK_CNTRL_DP_LINK_SPEED_G1_62;
	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	/* step 1 */
	value = tegra_sor_readl(sor, SOR_PLL_2);
	value |= SOR_PLL_2_SEQ_PLLCAPPD_ENFORCE | SOR_PLL_2_PORT_POWERDOWN |
		 SOR_PLL_2_BANDGAP_POWERDOWN;
	tegra_sor_writel(sor, value, SOR_PLL_2);

	value = tegra_sor_readl(sor, SOR_PLL_0);
	value |= SOR_PLL_0_VCOPD | SOR_PLL_0_POWER_OFF;
	tegra_sor_writel(sor, value, SOR_PLL_0);

	value = tegra_sor_readl(sor, SOR_DP_PADCTL_0);
	value &= ~SOR_DP_PADCTL_PAD_CAL_PD;
	tegra_sor_writel(sor, value, SOR_DP_PADCTL_0);

	/* step 2 */
	err = tegra_io_rail_power_on(TEGRA_IO_RAIL_LVDS);
	if (err < 0) {
		dev_err(sor->dev, "failed to power on I/O rail: %d\n", err);
		return err;
	}

	usleep_range(5, 100);

	/* step 3 */
	value = tegra_sor_readl(sor, SOR_PLL_2);
	value &= ~SOR_PLL_2_BANDGAP_POWERDOWN;
	tegra_sor_writel(sor, value, SOR_PLL_2);

	usleep_range(20, 100);

	/* step 4 */
	value = tegra_sor_readl(sor, SOR_PLL_0);
	value &= ~SOR_PLL_0_POWER_OFF;
	value &= ~SOR_PLL_0_VCOPD;
	tegra_sor_writel(sor, value, SOR_PLL_0);

	value = tegra_sor_readl(sor, SOR_PLL_2);
	value &= ~SOR_PLL_2_SEQ_PLLCAPPD_ENFORCE;
	tegra_sor_writel(sor, value, SOR_PLL_2);

	usleep_range(200, 1000);

	/* step 5 */
	value = tegra_sor_readl(sor, SOR_PLL_2);
	value &= ~SOR_PLL_2_PORT_POWERDOWN;
	tegra_sor_writel(sor, value, SOR_PLL_2);

	/* switch to DP clock */
	err = clk_set_parent(sor->clk, sor->clk_dp);
	if (err < 0)
		dev_err(sor->dev, "failed to set DP parent clock: %d\n", err);

	/* power dplanes (XXX parameterize based on link?) */
	value = tegra_sor_readl(sor, SOR_DP_PADCTL_0);
	value |= SOR_DP_PADCTL_PD_TXD_3 | SOR_DP_PADCTL_PD_TXD_0 |
		 SOR_DP_PADCTL_PD_TXD_1 | SOR_DP_PADCTL_PD_TXD_2;
	tegra_sor_writel(sor, value, SOR_DP_PADCTL_0);

	value = tegra_sor_readl(sor, SOR_DP_LINKCTL_0);
	value &= ~SOR_DP_LINKCTL_LANE_COUNT_MASK;
	value |= SOR_DP_LINKCTL_LANE_COUNT(4);
	tegra_sor_writel(sor, value, SOR_DP_LINKCTL_0);

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

	/* set link bandwidth (2.7 GHz, XXX: parameterize based on link?) */
	value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
	value &= ~SOR_CLK_CNTRL_DP_LINK_SPEED_MASK;
	value |= SOR_CLK_CNTRL_DP_LINK_SPEED_G2_70;
	tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

	/* set linkctl */
	value = tegra_sor_readl(sor, SOR_DP_LINKCTL_0);
	value |= SOR_DP_LINKCTL_ENABLE;

	value &= ~SOR_DP_LINKCTL_TU_SIZE_MASK;
	value |= SOR_DP_LINKCTL_TU_SIZE(59); /* XXX: don't hardcode? */

	value |= SOR_DP_LINKCTL_ENHANCED_FRAME;
	tegra_sor_writel(sor, value, SOR_DP_LINKCTL_0);

	for (i = 0, value = 0; i < 4; i++) {
		unsigned long lane = SOR_DP_TPG_CHANNEL_CODING |
				     SOR_DP_TPG_SCRAMBLER_GALIOS |
				     SOR_DP_TPG_PATTERN_NONE;
		value = (value << 8) | lane;
	}

	tegra_sor_writel(sor, value, SOR_DP_TPG);

	value = tegra_sor_readl(sor, SOR_DP_CONFIG_0);
	value &= ~SOR_DP_CONFIG_WATERMARK_MASK;
	value |= SOR_DP_CONFIG_WATERMARK(14); /* XXX: don't hardcode? */

	value &= ~SOR_DP_CONFIG_ACTIVE_SYM_COUNT_MASK;
	value |= SOR_DP_CONFIG_ACTIVE_SYM_COUNT(47); /* XXX: don't hardcode? */

	value &= ~SOR_DP_CONFIG_ACTIVE_SYM_FRAC_MASK;
	value |= SOR_DP_CONFIG_ACTIVE_SYM_FRAC(9); /* XXX: don't hardcode? */

	value &= ~SOR_DP_CONFIG_ACTIVE_SYM_POLARITY; /* XXX: don't hardcode? */

	value |= SOR_DP_CONFIG_ACTIVE_SYM_ENABLE;
	value |= SOR_DP_CONFIG_DISPARITY_NEGATIVE; /* XXX: don't hardcode? */
	tegra_sor_writel(sor, value, SOR_DP_CONFIG_0);

	value = tegra_sor_readl(sor, SOR_DP_AUDIO_HBLANK_SYMBOLS);
	value &= ~SOR_DP_AUDIO_HBLANK_SYMBOLS_MASK;
	value |= 137; /* XXX: don't hardcode? */
	tegra_sor_writel(sor, value, SOR_DP_AUDIO_HBLANK_SYMBOLS);

	value = tegra_sor_readl(sor, SOR_DP_AUDIO_VBLANK_SYMBOLS);
	value &= ~SOR_DP_AUDIO_VBLANK_SYMBOLS_MASK;
	value |= 2368; /* XXX: don't hardcode? */
	tegra_sor_writel(sor, value, SOR_DP_AUDIO_VBLANK_SYMBOLS);

	/* enable pad calibration logic */
	value = tegra_sor_readl(sor, SOR_DP_PADCTL_0);
	value |= SOR_DP_PADCTL_PAD_CAL_PD;
	tegra_sor_writel(sor, value, SOR_DP_PADCTL_0);

	if (sor->dpaux) {
		/* FIXME: properly convert to struct drm_dp_aux */
		struct drm_dp_aux *aux = (struct drm_dp_aux *)sor->dpaux;
		struct drm_dp_link link;
		u8 rate, lanes;

		err = drm_dp_link_probe(aux, &link);
		if (err < 0) {
			dev_err(sor->dev, "failed to probe eDP link: %d\n",
				err);
			return err;
		}

		err = drm_dp_link_power_up(aux, &link);
		if (err < 0) {
			dev_err(sor->dev, "failed to power up eDP link: %d\n",
				err);
			return err;
		}

		err = drm_dp_link_configure(aux, &link);
		if (err < 0) {
			dev_err(sor->dev, "failed to configure eDP link: %d\n",
				err);
			return err;
		}

		rate = drm_dp_link_rate_to_bw_code(link.rate);
		lanes = link.num_lanes;

		value = tegra_sor_readl(sor, SOR_CLK_CNTRL);
		value &= ~SOR_CLK_CNTRL_DP_LINK_SPEED_MASK;
		value |= SOR_CLK_CNTRL_DP_LINK_SPEED(rate);
		tegra_sor_writel(sor, value, SOR_CLK_CNTRL);

		value = tegra_sor_readl(sor, SOR_DP_LINKCTL_0);
		value &= ~SOR_DP_LINKCTL_LANE_COUNT_MASK;
		value |= SOR_DP_LINKCTL_LANE_COUNT(lanes);

		if (link.capabilities & DP_LINK_CAP_ENHANCED_FRAMING)
			value |= SOR_DP_LINKCTL_ENHANCED_FRAME;

		tegra_sor_writel(sor, value, SOR_DP_LINKCTL_0);

		/* disable training pattern generator */

		for (i = 0; i < link.num_lanes; i++) {
			unsigned long lane = SOR_DP_TPG_CHANNEL_CODING |
					     SOR_DP_TPG_SCRAMBLER_GALIOS |
					     SOR_DP_TPG_PATTERN_NONE;
			value = (value << 8) | lane;
		}

		tegra_sor_writel(sor, value, SOR_DP_TPG);

		err = tegra_sor_dp_train_fast(sor, &link);
		if (err < 0) {
			dev_err(sor->dev, "DP fast link training failed: %d\n",
				err);
			return err;
		}

		dev_dbg(sor->dev, "fast link training succeeded\n");
	}

	err = tegra_sor_power_up(sor, 250);
	if (err < 0) {
		dev_err(sor->dev, "failed to power up SOR: %d\n", err);
		return err;
	}

	/* start display controller in continuous mode */
	value = tegra_dc_readl(dc, DC_CMD_STATE_ACCESS);
	value |= WRITE_MUX;
	tegra_dc_writel(dc, value, DC_CMD_STATE_ACCESS);

	tegra_dc_writel(dc, VSYNC_H_POSITION(1), DC_DISP_DISP_TIMING_OPTIONS);
	tegra_dc_writel(dc, DISP_CTRL_MODE_C_DISPLAY, DC_CMD_DISPLAY_COMMAND);

	value = tegra_dc_readl(dc, DC_CMD_STATE_ACCESS);
	value &= ~WRITE_MUX;
	tegra_dc_writel(dc, value, DC_CMD_STATE_ACCESS);

	/*
	 * configure panel (24bpp, vsync-, hsync-, DP-A protocol, complete
	 * raster, associate with display controller)
	 */
	value = SOR_STATE_ASY_PIXELDEPTH_BPP_24_444 |
		SOR_STATE_ASY_VSYNCPOL |
		SOR_STATE_ASY_HSYNCPOL |
		SOR_STATE_ASY_PROTOCOL_DP_A |
		SOR_STATE_ASY_CRC_MODE_COMPLETE |
		SOR_STATE_ASY_OWNER(dc->pipe + 1);
	tegra_sor_writel(sor, value, SOR_STATE_1);

	/*
	 * TODO: The video timing programming below doesn't seem to match the
	 * register definitions.
	 */

	value = ((mode->vtotal & 0x7fff) << 16) | (mode->htotal & 0x7fff);
	tegra_sor_writel(sor, value, SOR_HEAD_STATE_1(0));

	vse = mode->vsync_end - mode->vsync_start - 1;
	hse = mode->hsync_end - mode->hsync_start - 1;

	value = ((vse & 0x7fff) << 16) | (hse & 0x7fff);
	tegra_sor_writel(sor, value, SOR_HEAD_STATE_2(0));

	vbe = vse + (mode->vsync_start - mode->vdisplay);
	hbe = hse + (mode->hsync_start - mode->hdisplay);

	value = ((vbe & 0x7fff) << 16) | (hbe & 0x7fff);
	tegra_sor_writel(sor, value, SOR_HEAD_STATE_3(0));

	vbs = vbe + mode->vdisplay;
	hbs = hbe + mode->hdisplay;

	value = ((vbs & 0x7fff) << 16) | (hbs & 0x7fff);
	tegra_sor_writel(sor, value, SOR_HEAD_STATE_4(0));

	/* XXX interlaced mode */
	tegra_sor_writel(sor, 0x00000001, SOR_HEAD_STATE_5(0));

	/* CSTM (LVDS, link A/B, upper) */
	value = SOR_CSTM_LVDS | SOR_CSTM_LINK_ACT_B | SOR_CSTM_LINK_ACT_B |
		SOR_CSTM_UPPER;
	tegra_sor_writel(sor, value, SOR_CSTM);

	/* PWM setup */
	err = tegra_sor_setup_pwm(sor, 250);
	if (err < 0) {
		dev_err(sor->dev, "failed to setup PWM: %d\n", err);
		return err;
	}

	value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
	value |= SOR_ENABLE;
	tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

	tegra_sor_update(sor);

	err = tegra_sor_attach(sor);
	if (err < 0) {
		dev_err(sor->dev, "failed to attach SOR: %d\n", err);
		return err;
	}

	err = tegra_sor_wakeup(sor);
	if (err < 0) {
		dev_err(sor->dev, "failed to enable DC: %d\n", err);
		return err;
	}

	sor->enabled = true;

	return 0;
}

static int tegra_sor_detach(struct tegra_sor *sor)
{
	unsigned long value, timeout;

	/* switch to safe mode */
	value = tegra_sor_readl(sor, SOR_SUPER_STATE_1);
	value &= ~SOR_SUPER_STATE_MODE_NORMAL;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE_1);
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
	value = tegra_sor_readl(sor, SOR_SUPER_STATE_1);
	value &= ~SOR_SUPER_STATE_HEAD_MODE_MASK;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE_1);
	tegra_sor_super_update(sor);

	/* detach */
	value = tegra_sor_readl(sor, SOR_SUPER_STATE_1);
	value &= ~SOR_SUPER_STATE_ATTACHED;
	tegra_sor_writel(sor, value, SOR_SUPER_STATE_1);
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

	err = clk_set_parent(sor->clk, sor->clk_safe);
	if (err < 0)
		dev_err(sor->dev, "failed to set safe parent clock: %d\n", err);

	value = tegra_sor_readl(sor, SOR_DP_PADCTL_0);
	value &= ~(SOR_DP_PADCTL_PD_TXD_3 | SOR_DP_PADCTL_PD_TXD_0 |
		   SOR_DP_PADCTL_PD_TXD_1 | SOR_DP_PADCTL_PD_TXD_2);
	tegra_sor_writel(sor, value, SOR_DP_PADCTL_0);

	/* stop lane sequencer */
	value = SOR_LANE_SEQ_CTL_TRIGGER | SOR_LANE_SEQ_CTL_SEQUENCE_DOWN |
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

	value = tegra_sor_readl(sor, SOR_PLL_2);
	value |= SOR_PLL_2_PORT_POWERDOWN;
	tegra_sor_writel(sor, value, SOR_PLL_2);

	usleep_range(20, 100);

	value = tegra_sor_readl(sor, SOR_PLL_0);
	value |= SOR_PLL_0_POWER_OFF;
	value |= SOR_PLL_0_VCOPD;
	tegra_sor_writel(sor, value, SOR_PLL_0);

	value = tegra_sor_readl(sor, SOR_PLL_2);
	value |= SOR_PLL_2_SEQ_PLLCAPPD;
	value |= SOR_PLL_2_SEQ_PLLCAPPD_ENFORCE;
	tegra_sor_writel(sor, value, SOR_PLL_2);

	usleep_range(20, 100);

	return 0;
}

static int tegra_output_sor_disable(struct tegra_output *output)
{
	struct tegra_dc *dc = to_tegra_dc(output->encoder.crtc);
	struct tegra_sor *sor = to_sor(output);
	unsigned long value;
	int err;

	if (!sor->enabled)
		return 0;

	err = tegra_sor_detach(sor);
	if (err < 0) {
		dev_err(sor->dev, "failed to detach SOR: %d\n", err);
		return err;
	}

	tegra_sor_writel(sor, 0, SOR_STATE_1);
	tegra_sor_update(sor);

	/*
	 * The following accesses registers of the display controller, so make
	 * sure it's only executed when the output is attached to one.
	 */
	if (dc) {
		/*
		 * XXX: We can't do this here because it causes the SOR to go
		 * into an erroneous state and the output will look scrambled
		 * the next time it is enabled. Presumably this is because we
		 * should be doing this only on the next VBLANK. A possible
		 * solution would be to queue a "power-off" event to trigger
		 * this code to be run during the next VBLANK.
		 */
		/*
		value = tegra_dc_readl(dc, DC_CMD_DISPLAY_POWER_CONTROL);
		value &= ~(PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
			   PW4_ENABLE | PM0_ENABLE | PM1_ENABLE);
		tegra_dc_writel(dc, value, DC_CMD_DISPLAY_POWER_CONTROL);
		*/

		value = tegra_dc_readl(dc, DC_CMD_DISPLAY_COMMAND);
		value &= ~DISP_CTRL_MODE_MASK;
		tegra_dc_writel(dc, value, DC_CMD_DISPLAY_COMMAND);

		value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
		value &= ~SOR_ENABLE;
		tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

		tegra_dc_writel(dc, GENERAL_ACT_REQ << 8, DC_CMD_STATE_CONTROL);
		tegra_dc_writel(dc, GENERAL_ACT_REQ, DC_CMD_STATE_CONTROL);
	}

	err = tegra_sor_power_down(sor);
	if (err < 0) {
		dev_err(sor->dev, "failed to power down SOR: %d\n", err);
		return err;
	}

	if (sor->dpaux) {
		err = tegra_dpaux_disable(sor->dpaux);
		if (err < 0) {
			dev_err(sor->dev, "failed to disable DP: %d\n", err);
			return err;
		}
	}

	err = tegra_io_rail_power_off(TEGRA_IO_RAIL_LVDS);
	if (err < 0) {
		dev_err(sor->dev, "failed to power off I/O rail: %d\n", err);
		return err;
	}

	reset_control_assert(sor->rst);
	clk_disable_unprepare(sor->clk);

	sor->enabled = false;

	return 0;
}

static int tegra_output_sor_setup_clock(struct tegra_output *output,
					struct clk *clk, unsigned long pclk)
{
	struct tegra_sor *sor = to_sor(output);
	int err;

	/* round to next MHz */
	pclk = DIV_ROUND_UP(pclk / 2, 1000000) * 1000000;

	err = clk_set_parent(clk, sor->clk_parent);
	if (err < 0) {
		dev_err(sor->dev, "failed to set parent clock: %d\n", err);
		return err;
	}

	err = clk_set_rate(sor->clk_parent, pclk);
	if (err < 0) {
		dev_err(sor->dev, "failed to set base clock rate to %lu Hz\n",
			pclk * 2);
		return err;
	}

	return 0;
}

static int tegra_output_sor_check_mode(struct tegra_output *output,
				       struct drm_display_mode *mode,
				       enum drm_mode_status *status)
{
	/*
	 * FIXME: For now, always assume that the mode is okay.
	 */

	*status = MODE_OK;

	return 0;
}

static enum drm_connector_status
tegra_output_sor_detect(struct tegra_output *output)
{
	struct tegra_sor *sor = to_sor(output);

	if (sor->dpaux)
		return tegra_dpaux_detect(sor->dpaux);

	return connector_status_unknown;
}

static const struct tegra_output_ops sor_ops = {
	.enable = tegra_output_sor_enable,
	.disable = tegra_output_sor_disable,
	.setup_clock = tegra_output_sor_setup_clock,
	.check_mode = tegra_output_sor_check_mode,
	.detect = tegra_output_sor_detect,
};

static int tegra_sor_init(struct host1x_client *client)
{
	struct tegra_drm *tegra = dev_get_drvdata(client->parent);
	struct tegra_sor *sor = host1x_client_to_sor(client);
	int err;

	if (!sor->dpaux)
		return -ENODEV;

	sor->output.type = TEGRA_OUTPUT_EDP;

	sor->output.dev = sor->dev;
	sor->output.ops = &sor_ops;

	err = tegra_output_init(tegra->drm, &sor->output);
	if (err < 0) {
		dev_err(sor->dev, "output setup failed: %d\n", err);
		return err;
	}

	if (sor->dpaux) {
		err = tegra_dpaux_attach(sor->dpaux, &sor->output);
		if (err < 0) {
			dev_err(sor->dev, "failed to attach DP: %d\n", err);
			return err;
		}
	}

	return 0;
}

static int tegra_sor_exit(struct host1x_client *client)
{
	struct tegra_sor *sor = host1x_client_to_sor(client);
	int err;

	err = tegra_output_disable(&sor->output);
	if (err < 0) {
		dev_err(sor->dev, "output failed to disable: %d\n", err);
		return err;
	}

	if (sor->dpaux) {
		err = tegra_dpaux_detach(sor->dpaux);
		if (err < 0) {
			dev_err(sor->dev, "failed to detach DP: %d\n", err);
			return err;
		}
	}

	err = tegra_output_exit(&sor->output);
	if (err < 0) {
		dev_err(sor->dev, "output cleanup failed: %d\n", err);
		return err;
	}

	return 0;
}

static const struct host1x_client_ops sor_client_ops = {
	.init = tegra_sor_init,
	.exit = tegra_sor_exit,
};

static int tegra_sor_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct tegra_sor *sor;
	struct resource *regs;
	int err;

	sor = devm_kzalloc(&pdev->dev, sizeof(*sor), GFP_KERNEL);
	if (!sor)
		return -ENOMEM;

	sor->output.dev = sor->dev = &pdev->dev;

	np = of_parse_phandle(pdev->dev.of_node, "nvidia,dpaux", 0);
	if (np) {
		sor->dpaux = tegra_dpaux_find_by_of_node(np);
		of_node_put(np);

		if (!sor->dpaux)
			return -EPROBE_DEFER;
	}

	err = tegra_output_probe(&sor->output);
	if (err < 0)
		return err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sor->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(sor->regs))
		return PTR_ERR(sor->regs);

	sor->rst = devm_reset_control_get(&pdev->dev, "sor");
	if (IS_ERR(sor->rst))
		return PTR_ERR(sor->rst);

	sor->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sor->clk))
		return PTR_ERR(sor->clk);

	sor->clk_parent = devm_clk_get(&pdev->dev, "parent");
	if (IS_ERR(sor->clk_parent))
		return PTR_ERR(sor->clk_parent);

	err = clk_prepare_enable(sor->clk_parent);
	if (err < 0)
		return err;

	sor->clk_safe = devm_clk_get(&pdev->dev, "safe");
	if (IS_ERR(sor->clk_safe))
		return PTR_ERR(sor->clk_safe);

	err = clk_prepare_enable(sor->clk_safe);
	if (err < 0)
		return err;

	sor->clk_dp = devm_clk_get(&pdev->dev, "dp");
	if (IS_ERR(sor->clk_dp))
		return PTR_ERR(sor->clk_dp);

	err = clk_prepare_enable(sor->clk_dp);
	if (err < 0)
		return err;

	INIT_LIST_HEAD(&sor->client.list);
	sor->client.ops = &sor_client_ops;
	sor->client.dev = &pdev->dev;

	err = host1x_client_register(&sor->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, sor);

	return 0;
}

static int tegra_sor_remove(struct platform_device *pdev)
{
	struct tegra_sor *sor = platform_get_drvdata(pdev);
	int err;

	err = host1x_client_unregister(&sor->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	clk_disable_unprepare(sor->clk_parent);
	clk_disable_unprepare(sor->clk_safe);
	clk_disable_unprepare(sor->clk_dp);
	clk_disable_unprepare(sor->clk);

	return 0;
}

static const struct of_device_id tegra_sor_of_match[] = {
	{ .compatible = "nvidia,tegra124-sor", },
	{ },
};

struct platform_driver tegra_sor_driver = {
	.driver = {
		.name = "tegra-sor",
		.of_match_table = tegra_sor_of_match,
	},
	.probe = tegra_sor_probe,
	.remove = tegra_sor_remove,
};
