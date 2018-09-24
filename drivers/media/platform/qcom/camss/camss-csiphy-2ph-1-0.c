// SPDX-License-Identifier: GPL-2.0
/*
 * camss-csiphy-2ph-1-0.c
 *
 * Qualcomm MSM Camera Subsystem - CSIPHY Module 2phase v1.0
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2018 Linaro Ltd.
 */

#include "camss-csiphy.h"

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#define CAMSS_CSI_PHY_LNn_CFG2(n)		(0x004 + 0x40 * (n))
#define CAMSS_CSI_PHY_LNn_CFG3(n)		(0x008 + 0x40 * (n))
#define CAMSS_CSI_PHY_GLBL_RESET		0x140
#define CAMSS_CSI_PHY_GLBL_PWR_CFG		0x144
#define CAMSS_CSI_PHY_GLBL_IRQ_CMD		0x164
#define CAMSS_CSI_PHY_HW_VERSION		0x188
#define CAMSS_CSI_PHY_INTERRUPT_STATUSn(n)	(0x18c + 0x4 * (n))
#define CAMSS_CSI_PHY_INTERRUPT_MASKn(n)	(0x1ac + 0x4 * (n))
#define CAMSS_CSI_PHY_INTERRUPT_CLEARn(n)	(0x1cc + 0x4 * (n))
#define CAMSS_CSI_PHY_GLBL_T_INIT_CFG0		0x1ec
#define CAMSS_CSI_PHY_T_WAKEUP_CFG0		0x1f4

static void csiphy_hw_version_read(struct csiphy_device *csiphy,
				   struct device *dev)
{
	u8 hw_version = readl_relaxed(csiphy->base +
				      CAMSS_CSI_PHY_HW_VERSION);

	dev_dbg(dev, "CSIPHY HW Version = 0x%02x\n", hw_version);
}

/*
 * csiphy_reset - Perform software reset on CSIPHY module
 * @csiphy: CSIPHY device
 */
static void csiphy_reset(struct csiphy_device *csiphy)
{
	writel_relaxed(0x1, csiphy->base + CAMSS_CSI_PHY_GLBL_RESET);
	usleep_range(5000, 8000);
	writel_relaxed(0x0, csiphy->base + CAMSS_CSI_PHY_GLBL_RESET);
}

/*
 * csiphy_settle_cnt_calc - Calculate settle count value
 *
 * Helper function to calculate settle count value. This is
 * based on the CSI2 T_hs_settle parameter which in turn
 * is calculated based on the CSI2 transmitter pixel clock
 * frequency.
 *
 * Return settle count value or 0 if the CSI2 pixel clock
 * frequency is not available
 */
static u8 csiphy_settle_cnt_calc(u32 pixel_clock, u8 bpp, u8 num_lanes,
				 u32 timer_clk_rate)
{
	u32 mipi_clock; /* Hz */
	u32 ui; /* ps */
	u32 timer_period; /* ps */
	u32 t_hs_prepare_max; /* ps */
	u32 t_hs_prepare_zero_min; /* ps */
	u32 t_hs_settle; /* ps */
	u8 settle_cnt;

	mipi_clock = pixel_clock * bpp / (2 * num_lanes);
	ui = div_u64(1000000000000LL, mipi_clock);
	ui /= 2;
	t_hs_prepare_max = 85000 + 6 * ui;
	t_hs_prepare_zero_min = 145000 + 10 * ui;
	t_hs_settle = (t_hs_prepare_max + t_hs_prepare_zero_min) / 2;

	timer_period = div_u64(1000000000000LL, timer_clk_rate);
	settle_cnt = t_hs_settle / timer_period - 1;

	return settle_cnt;
}

static void csiphy_lanes_enable(struct csiphy_device *csiphy,
				struct csiphy_config *cfg,
				u32 pixel_clock, u8 bpp, u8 lane_mask)
{
	struct csiphy_lanes_cfg *c = &cfg->csi2->lane_cfg;
	u8 settle_cnt;
	u8 val, l = 0;
	int i = 0;

	settle_cnt = csiphy_settle_cnt_calc(pixel_clock, bpp, c->num_data,
					    csiphy->timer_clk_rate);

	writel_relaxed(0x1, csiphy->base +
		       CAMSS_CSI_PHY_GLBL_T_INIT_CFG0);
	writel_relaxed(0x1, csiphy->base +
		       CAMSS_CSI_PHY_T_WAKEUP_CFG0);

	val = 0x1;
	val |= lane_mask << 1;
	writel_relaxed(val, csiphy->base + CAMSS_CSI_PHY_GLBL_PWR_CFG);

	val = cfg->combo_mode << 4;
	writel_relaxed(val, csiphy->base + CAMSS_CSI_PHY_GLBL_RESET);

	for (i = 0; i <= c->num_data; i++) {
		if (i == c->num_data)
			l = c->clk.pos;
		else
			l = c->data[i].pos;

		writel_relaxed(0x10, csiphy->base +
			       CAMSS_CSI_PHY_LNn_CFG2(l));
		writel_relaxed(settle_cnt, csiphy->base +
			       CAMSS_CSI_PHY_LNn_CFG3(l));
		writel_relaxed(0x3f, csiphy->base +
			       CAMSS_CSI_PHY_INTERRUPT_MASKn(l));
		writel_relaxed(0x3f, csiphy->base +
			       CAMSS_CSI_PHY_INTERRUPT_CLEARn(l));
	}
}

static void csiphy_lanes_disable(struct csiphy_device *csiphy,
				 struct csiphy_config *cfg)
{
	struct csiphy_lanes_cfg *c = &cfg->csi2->lane_cfg;
	u8 l = 0;
	int i = 0;

	for (i = 0; i <= c->num_data; i++) {
		if (i == c->num_data)
			l = c->clk.pos;
		else
			l = c->data[i].pos;

		writel_relaxed(0x0, csiphy->base +
			       CAMSS_CSI_PHY_LNn_CFG2(l));
	}

	writel_relaxed(0x0, csiphy->base + CAMSS_CSI_PHY_GLBL_PWR_CFG);
}

/*
 * csiphy_isr - CSIPHY module interrupt handler
 * @irq: Interrupt line
 * @dev: CSIPHY device
 *
 * Return IRQ_HANDLED on success
 */
static irqreturn_t csiphy_isr(int irq, void *dev)
{
	struct csiphy_device *csiphy = dev;
	u8 i;

	for (i = 0; i < 8; i++) {
		u8 val = readl_relaxed(csiphy->base +
				       CAMSS_CSI_PHY_INTERRUPT_STATUSn(i));
		writel_relaxed(val, csiphy->base +
			       CAMSS_CSI_PHY_INTERRUPT_CLEARn(i));
		writel_relaxed(0x1, csiphy->base + CAMSS_CSI_PHY_GLBL_IRQ_CMD);
		writel_relaxed(0x0, csiphy->base + CAMSS_CSI_PHY_GLBL_IRQ_CMD);
		writel_relaxed(0x0, csiphy->base +
			       CAMSS_CSI_PHY_INTERRUPT_CLEARn(i));
	}

	return IRQ_HANDLED;
}

const struct csiphy_hw_ops csiphy_ops_2ph_1_0 = {
	.hw_version_read = csiphy_hw_version_read,
	.reset = csiphy_reset,
	.lanes_enable = csiphy_lanes_enable,
	.lanes_disable = csiphy_lanes_disable,
	.isr = csiphy_isr,
};
