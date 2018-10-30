// SPDX-License-Identifier: GPL-2.0
/*
 * camss-csiphy-3ph-1-0.c
 *
 * Qualcomm MSM Camera Subsystem - CSIPHY Module 3phase v1.0
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2018 Linaro Ltd.
 */

#include "camss-csiphy.h"

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#define CSIPHY_3PH_LNn_CFG1(n)			(0x000 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG1_SWI_REC_DLY_PRG	(BIT(7) | BIT(6))
#define CSIPHY_3PH_LNn_CFG2(n)			(0x004 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG2_LP_REC_EN_INT	BIT(3)
#define CSIPHY_3PH_LNn_CFG3(n)			(0x008 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG4(n)			(0x00c + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG4_T_HS_CLK_MISS	0xa4
#define CSIPHY_3PH_LNn_CFG5(n)			(0x010 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG5_T_HS_DTERM		0x02
#define CSIPHY_3PH_LNn_CFG5_HS_REC_EQ_FQ_INT	0x50
#define CSIPHY_3PH_LNn_TEST_IMP(n)		(0x01c + 0x100 * (n))
#define CSIPHY_3PH_LNn_TEST_IMP_HS_TERM_IMP	0xa
#define CSIPHY_3PH_LNn_MISC1(n)			(0x028 + 0x100 * (n))
#define CSIPHY_3PH_LNn_MISC1_IS_CLKLANE		BIT(2)
#define CSIPHY_3PH_LNn_CFG6(n)			(0x02c + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG6_SWI_FORCE_INIT_EXIT	BIT(0)
#define CSIPHY_3PH_LNn_CFG7(n)			(0x030 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG7_SWI_T_INIT		0x2
#define CSIPHY_3PH_LNn_CFG8(n)			(0x034 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG8_SWI_SKIP_WAKEUP	BIT(0)
#define CSIPHY_3PH_LNn_CFG8_SKEW_FILTER_ENABLE	BIT(1)
#define CSIPHY_3PH_LNn_CFG9(n)			(0x038 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG9_SWI_T_WAKEUP	0x1
#define CSIPHY_3PH_LNn_CSI_LANE_CTRL15(n)	(0x03c + 0x100 * (n))
#define CSIPHY_3PH_LNn_CSI_LANE_CTRL15_SWI_SOT_SYMBOL	0xb8

#define CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(n)	(0x800 + 0x4 * (n))
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_COMMON_PWRDN_B	BIT(0)
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_SHOW_REV_ID	BIT(1)
#define CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(n)	(0x8b0 + 0x4 * (n))

static void csiphy_hw_version_read(struct csiphy_device *csiphy,
				   struct device *dev)
{
	u32 hw_version;

	writel(CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_SHOW_REV_ID,
	       csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(6));

	hw_version = readl_relaxed(csiphy->base +
				   CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(12));
	hw_version |= readl_relaxed(csiphy->base +
				   CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(13)) << 8;
	hw_version |= readl_relaxed(csiphy->base +
				   CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(14)) << 16;
	hw_version |= readl_relaxed(csiphy->base +
				   CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(15)) << 24;

	dev_err(dev, "CSIPHY 3PH HW Version = 0x%08x\n", hw_version);
}

/*
 * csiphy_reset - Perform software reset on CSIPHY module
 * @csiphy: CSIPHY device
 */
static void csiphy_reset(struct csiphy_device *csiphy)
{
	writel_relaxed(0x1, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(0));
	usleep_range(5000, 8000);
	writel_relaxed(0x0, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(0));
}

static irqreturn_t csiphy_isr(int irq, void *dev)
{
	struct csiphy_device *csiphy = dev;
	int i;

	for (i = 0; i < 11; i++) {
		int c = i + 22;
		u8 val = readl_relaxed(csiphy->base +
				       CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(i));

		writel_relaxed(val, csiphy->base +
				    CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(c));
	}

	writel_relaxed(0x1, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(10));
	writel_relaxed(0x0, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(10));

	for (i = 22; i < 33; i++)
		writel_relaxed(0x0, csiphy->base +
				    CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(i));

	return IRQ_HANDLED;
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
	u32 t_hs_settle; /* ps */
	u8 settle_cnt;

	mipi_clock = pixel_clock * bpp / (2 * num_lanes);
	ui = div_u64(1000000000000LL, mipi_clock);
	ui /= 2;
	t_hs_prepare_max = 85000 + 6 * ui;
	t_hs_settle = t_hs_prepare_max;

	timer_period = div_u64(1000000000000LL, timer_clk_rate);
	settle_cnt = t_hs_settle / timer_period - 6;

	return settle_cnt;
}

static void csiphy_lanes_enable(struct csiphy_device *csiphy,
				struct csiphy_config *cfg,
				u32 pixel_clock, u8 bpp, u8 lane_mask)
{
	struct csiphy_lanes_cfg *c = &cfg->csi2->lane_cfg;
	u8 settle_cnt;
	u8 val, l = 0;
	int i;

	settle_cnt = csiphy_settle_cnt_calc(pixel_clock, bpp, c->num_data,
					    csiphy->timer_clk_rate);

	val = BIT(c->clk.pos);
	for (i = 0; i < c->num_data; i++)
		val |= BIT(c->data[i].pos * 2);

	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(5));

	val = CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_COMMON_PWRDN_B;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(6));

	for (i = 0; i <= c->num_data; i++) {
		if (i == c->num_data)
			l = 7;
		else
			l = c->data[i].pos * 2;

		val = CSIPHY_3PH_LNn_CFG1_SWI_REC_DLY_PRG;
		val |= 0x17;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG1(l));

		val = CSIPHY_3PH_LNn_CFG2_LP_REC_EN_INT;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG2(l));

		val = settle_cnt;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG3(l));

		val = CSIPHY_3PH_LNn_CFG5_T_HS_DTERM |
			CSIPHY_3PH_LNn_CFG5_HS_REC_EQ_FQ_INT;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG5(l));

		val = CSIPHY_3PH_LNn_CFG6_SWI_FORCE_INIT_EXIT;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG6(l));

		val = CSIPHY_3PH_LNn_CFG7_SWI_T_INIT;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG7(l));

		val = CSIPHY_3PH_LNn_CFG8_SWI_SKIP_WAKEUP |
			CSIPHY_3PH_LNn_CFG8_SKEW_FILTER_ENABLE;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG8(l));

		val = CSIPHY_3PH_LNn_CFG9_SWI_T_WAKEUP;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG9(l));

		val = CSIPHY_3PH_LNn_TEST_IMP_HS_TERM_IMP;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_TEST_IMP(l));

		val = CSIPHY_3PH_LNn_CSI_LANE_CTRL15_SWI_SOT_SYMBOL;
		writel_relaxed(val, csiphy->base +
				    CSIPHY_3PH_LNn_CSI_LANE_CTRL15(l));
	}

	val = CSIPHY_3PH_LNn_CFG1_SWI_REC_DLY_PRG;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG1(l));

	val = CSIPHY_3PH_LNn_CFG4_T_HS_CLK_MISS;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG4(l));

	val = CSIPHY_3PH_LNn_MISC1_IS_CLKLANE;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_MISC1(l));

	val = 0xff;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(11));

	val = 0xff;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(12));

	val = 0xfb;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(13));

	val = 0xff;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(14));

	val = 0x7f;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(15));

	val = 0xff;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(16));

	val = 0xff;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(17));

	val = 0xef;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(18));

	val = 0xff;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(19));

	val = 0xff;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(20));

	val = 0xff;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(21));
}

static void csiphy_lanes_disable(struct csiphy_device *csiphy,
				 struct csiphy_config *cfg)
{
	writel_relaxed(0, csiphy->base +
			  CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(5));

	writel_relaxed(0, csiphy->base +
			  CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(6));
}

const struct csiphy_hw_ops csiphy_ops_3ph_1_0 = {
	.hw_version_read = csiphy_hw_version_read,
	.reset = csiphy_reset,
	.lanes_enable = csiphy_lanes_enable,
	.lanes_disable = csiphy_lanes_disable,
	.isr = csiphy_isr,
};
