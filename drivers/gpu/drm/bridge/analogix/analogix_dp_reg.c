/*
 * Analogix DP (Display port) core register interface driver.
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <drm/bridge/analogix_dp.h>

#include "analogix_dp_core.h"
#include "analogix_dp_reg.h"

#define COMMON_INT_MASK_1	0
#define COMMON_INT_MASK_2	0
#define COMMON_INT_MASK_3	0
#define COMMON_INT_MASK_4	(HOTPLUG_CHG | HPD_LOST | PLUG)
#define INT_STA_MASK		INT_HPD

void analogix_dp_enable_video_mute(struct analogix_dp_device *dp, bool enable)
{
	u32 reg;

	if (enable) {
		reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_1);
		reg |= HDCP_VIDEO_MUTE;
		writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_1);
	} else {
		reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_1);
		reg &= ~HDCP_VIDEO_MUTE;
		writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_1);
	}
}

void analogix_dp_stop_video(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_1);
	reg &= ~VIDEO_EN;
	writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_1);
}

void analogix_dp_lane_swap(struct analogix_dp_device *dp, bool enable)
{
	u32 reg;

	if (enable)
		reg = LANE3_MAP_LOGIC_LANE_0 | LANE2_MAP_LOGIC_LANE_1 |
		      LANE1_MAP_LOGIC_LANE_2 | LANE0_MAP_LOGIC_LANE_3;
	else
		reg = LANE3_MAP_LOGIC_LANE_3 | LANE2_MAP_LOGIC_LANE_2 |
		      LANE1_MAP_LOGIC_LANE_1 | LANE0_MAP_LOGIC_LANE_0;

	writel(reg, dp->reg_base + ANALOGIX_DP_LANE_MAP);
}

void analogix_dp_init_analog_param(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = TX_TERMINAL_CTRL_50_OHM;
	writel(reg, dp->reg_base + ANALOGIX_DP_ANALOG_CTL_1);

	reg = SEL_24M | TX_DVDD_BIT_1_0625V;
	writel(reg, dp->reg_base + ANALOGIX_DP_ANALOG_CTL_2);

	if (dp->plat_data && is_rockchip(dp->plat_data->dev_type)) {
		reg = REF_CLK_24M;
		if (dp->plat_data->dev_type == RK3288_DP)
			reg ^= REF_CLK_MASK;

		writel(reg, dp->reg_base + ANALOGIX_DP_PLL_REG_1);
		writel(0x95, dp->reg_base + ANALOGIX_DP_PLL_REG_2);
		writel(0x40, dp->reg_base + ANALOGIX_DP_PLL_REG_3);
		writel(0x58, dp->reg_base + ANALOGIX_DP_PLL_REG_4);
		writel(0x22, dp->reg_base + ANALOGIX_DP_PLL_REG_5);
	}

	reg = DRIVE_DVDD_BIT_1_0625V | VCO_BIT_600_MICRO;
	writel(reg, dp->reg_base + ANALOGIX_DP_ANALOG_CTL_3);

	reg = PD_RING_OSC | AUX_TERMINAL_CTRL_50_OHM |
		TX_CUR1_2X | TX_CUR_16_MA;
	writel(reg, dp->reg_base + ANALOGIX_DP_PLL_FILTER_CTL_1);

	reg = CH3_AMP_400_MV | CH2_AMP_400_MV |
		CH1_AMP_400_MV | CH0_AMP_400_MV;
	writel(reg, dp->reg_base + ANALOGIX_DP_TX_AMP_TUNING_CTL);
}

void analogix_dp_init_interrupt(struct analogix_dp_device *dp)
{
	/* Set interrupt pin assertion polarity as high */
	writel(INT_POL1 | INT_POL0, dp->reg_base + ANALOGIX_DP_INT_CTL);

	/* Clear pending regisers */
	writel(0xff, dp->reg_base + ANALOGIX_DP_COMMON_INT_STA_1);
	writel(0x4f, dp->reg_base + ANALOGIX_DP_COMMON_INT_STA_2);
	writel(0xe0, dp->reg_base + ANALOGIX_DP_COMMON_INT_STA_3);
	writel(0xe7, dp->reg_base + ANALOGIX_DP_COMMON_INT_STA_4);
	writel(0x63, dp->reg_base + ANALOGIX_DP_INT_STA);

	/* 0:mask,1: unmask */
	writel(0x00, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_1);
	writel(0x00, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_2);
	writel(0x00, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_3);
	writel(0x00, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_4);
	writel(0x00, dp->reg_base + ANALOGIX_DP_INT_STA_MASK);
}

void analogix_dp_reset(struct analogix_dp_device *dp)
{
	u32 reg;

	analogix_dp_stop_video(dp);
	analogix_dp_enable_video_mute(dp, 0);

	reg = MASTER_VID_FUNC_EN_N | SLAVE_VID_FUNC_EN_N |
		AUD_FIFO_FUNC_EN_N | AUD_FUNC_EN_N |
		HDCP_FUNC_EN_N | SW_FUNC_EN_N;
	writel(reg, dp->reg_base + ANALOGIX_DP_FUNC_EN_1);

	reg = SSC_FUNC_EN_N | AUX_FUNC_EN_N |
		SERDES_FIFO_FUNC_EN_N |
		LS_CLK_DOMAIN_FUNC_EN_N;
	writel(reg, dp->reg_base + ANALOGIX_DP_FUNC_EN_2);

	usleep_range(20, 30);

	analogix_dp_lane_swap(dp, 0);

	writel(0x0, dp->reg_base + ANALOGIX_DP_SYS_CTL_1);
	writel(0x40, dp->reg_base + ANALOGIX_DP_SYS_CTL_2);
	writel(0x0, dp->reg_base + ANALOGIX_DP_SYS_CTL_3);
	writel(0x0, dp->reg_base + ANALOGIX_DP_SYS_CTL_4);

	writel(0x0, dp->reg_base + ANALOGIX_DP_PKT_SEND_CTL);
	writel(0x0, dp->reg_base + ANALOGIX_DP_HDCP_CTL);

	writel(0x5e, dp->reg_base + ANALOGIX_DP_HPD_DEGLITCH_L);
	writel(0x1a, dp->reg_base + ANALOGIX_DP_HPD_DEGLITCH_H);

	writel(0x10, dp->reg_base + ANALOGIX_DP_LINK_DEBUG_CTL);

	writel(0x0, dp->reg_base + ANALOGIX_DP_PHY_TEST);

	writel(0x0, dp->reg_base + ANALOGIX_DP_VIDEO_FIFO_THRD);
	writel(0x20, dp->reg_base + ANALOGIX_DP_AUDIO_MARGIN);

	writel(0x4, dp->reg_base + ANALOGIX_DP_M_VID_GEN_FILTER_TH);
	writel(0x2, dp->reg_base + ANALOGIX_DP_M_AUD_GEN_FILTER_TH);

	writel(0x00000101, dp->reg_base + ANALOGIX_DP_SOC_GENERAL_CTL);
}

void analogix_dp_swreset(struct analogix_dp_device *dp)
{
	writel(RESET_DP_TX, dp->reg_base + ANALOGIX_DP_TX_SW_RESET);
}

void analogix_dp_config_interrupt(struct analogix_dp_device *dp)
{
	u32 reg;

	/* 0: mask, 1: unmask */
	reg = COMMON_INT_MASK_1;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_1);

	reg = COMMON_INT_MASK_2;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_2);

	reg = COMMON_INT_MASK_3;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_3);

	reg = COMMON_INT_MASK_4;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_4);

	reg = INT_STA_MASK;
	writel(reg, dp->reg_base + ANALOGIX_DP_INT_STA_MASK);
}

void analogix_dp_mute_hpd_interrupt(struct analogix_dp_device *dp)
{
	u32 reg;

	/* 0: mask, 1: unmask */
	reg = readl(dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_4);
	reg &= ~COMMON_INT_MASK_4;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_4);

	reg = readl(dp->reg_base + ANALOGIX_DP_INT_STA_MASK);
	reg &= ~INT_STA_MASK;
	writel(reg, dp->reg_base + ANALOGIX_DP_INT_STA_MASK);
}

void analogix_dp_unmute_hpd_interrupt(struct analogix_dp_device *dp)
{
	u32 reg;

	/* 0: mask, 1: unmask */
	reg = COMMON_INT_MASK_4;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_MASK_4);

	reg = INT_STA_MASK;
	writel(reg, dp->reg_base + ANALOGIX_DP_INT_STA_MASK);
}

enum pll_status analogix_dp_get_pll_lock_status(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_DEBUG_CTL);
	if (reg & PLL_LOCK)
		return PLL_LOCKED;
	else
		return PLL_UNLOCKED;
}

void analogix_dp_set_pll_power_down(struct analogix_dp_device *dp, bool enable)
{
	u32 reg;

	if (enable) {
		reg = readl(dp->reg_base + ANALOGIX_DP_PLL_CTL);
		reg |= DP_PLL_PD;
		writel(reg, dp->reg_base + ANALOGIX_DP_PLL_CTL);
	} else {
		reg = readl(dp->reg_base + ANALOGIX_DP_PLL_CTL);
		reg &= ~DP_PLL_PD;
		writel(reg, dp->reg_base + ANALOGIX_DP_PLL_CTL);
	}
}

void analogix_dp_set_analog_power_down(struct analogix_dp_device *dp,
				       enum analog_power_block block,
				       bool enable)
{
	u32 reg;
	u32 phy_pd_addr = ANALOGIX_DP_PHY_PD;

	if (dp->plat_data && is_rockchip(dp->plat_data->dev_type))
		phy_pd_addr = ANALOGIX_DP_PD;

	switch (block) {
	case AUX_BLOCK:
		if (enable) {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg |= AUX_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		} else {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg &= ~AUX_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		}
		break;
	case CH0_BLOCK:
		if (enable) {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg |= CH0_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		} else {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg &= ~CH0_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		}
		break;
	case CH1_BLOCK:
		if (enable) {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg |= CH1_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		} else {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg &= ~CH1_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		}
		break;
	case CH2_BLOCK:
		if (enable) {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg |= CH2_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		} else {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg &= ~CH2_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		}
		break;
	case CH3_BLOCK:
		if (enable) {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg |= CH3_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		} else {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg &= ~CH3_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		}
		break;
	case ANALOG_TOTAL:
		if (enable) {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg |= DP_PHY_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		} else {
			reg = readl(dp->reg_base + phy_pd_addr);
			reg &= ~DP_PHY_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		}
		break;
	case POWER_ALL:
		if (enable) {
			reg = DP_PHY_PD | AUX_PD | CH3_PD | CH2_PD |
				CH1_PD | CH0_PD;
			writel(reg, dp->reg_base + phy_pd_addr);
		} else {
			writel(0x00, dp->reg_base + phy_pd_addr);
		}
		break;
	default:
		break;
	}
}

void analogix_dp_init_analog_func(struct analogix_dp_device *dp)
{
	u32 reg;
	int timeout_loop = 0;

	analogix_dp_set_analog_power_down(dp, POWER_ALL, 0);

	reg = PLL_LOCK_CHG;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_STA_1);

	reg = readl(dp->reg_base + ANALOGIX_DP_DEBUG_CTL);
	reg &= ~(F_PLL_LOCK | PLL_LOCK_CTRL);
	writel(reg, dp->reg_base + ANALOGIX_DP_DEBUG_CTL);

	/* Power up PLL */
	if (analogix_dp_get_pll_lock_status(dp) == PLL_UNLOCKED) {
		analogix_dp_set_pll_power_down(dp, 0);

		while (analogix_dp_get_pll_lock_status(dp) == PLL_UNLOCKED) {
			timeout_loop++;
			if (DP_TIMEOUT_LOOP_COUNT < timeout_loop) {
				dev_err(dp->dev, "failed to get pll lock status\n");
				return;
			}
			usleep_range(10, 20);
		}
	}

	/* Enable Serdes FIFO function and Link symbol clock domain module */
	reg = readl(dp->reg_base + ANALOGIX_DP_FUNC_EN_2);
	reg &= ~(SERDES_FIFO_FUNC_EN_N | LS_CLK_DOMAIN_FUNC_EN_N
		| AUX_FUNC_EN_N);
	writel(reg, dp->reg_base + ANALOGIX_DP_FUNC_EN_2);
}

void analogix_dp_clear_hotplug_interrupts(struct analogix_dp_device *dp)
{
	u32 reg;

	if (gpio_is_valid(dp->hpd_gpio))
		return;

	reg = HOTPLUG_CHG | HPD_LOST | PLUG;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_STA_4);

	reg = INT_HPD;
	writel(reg, dp->reg_base + ANALOGIX_DP_INT_STA);
}

void analogix_dp_init_hpd(struct analogix_dp_device *dp)
{
	u32 reg;

	if (gpio_is_valid(dp->hpd_gpio))
		return;

	analogix_dp_clear_hotplug_interrupts(dp);

	reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_3);
	reg &= ~(F_HPD | HPD_CTRL);
	writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_3);
}

void analogix_dp_force_hpd(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_3);
	reg = (F_HPD | HPD_CTRL);
	writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_3);
}

enum dp_irq_type analogix_dp_get_irq_type(struct analogix_dp_device *dp)
{
	u32 reg;

	if (gpio_is_valid(dp->hpd_gpio)) {
		reg = gpio_get_value(dp->hpd_gpio);
		if (reg)
			return DP_IRQ_TYPE_HP_CABLE_IN;
		else
			return DP_IRQ_TYPE_HP_CABLE_OUT;
	} else {
		/* Parse hotplug interrupt status register */
		reg = readl(dp->reg_base + ANALOGIX_DP_COMMON_INT_STA_4);

		if (reg & PLUG)
			return DP_IRQ_TYPE_HP_CABLE_IN;

		if (reg & HPD_LOST)
			return DP_IRQ_TYPE_HP_CABLE_OUT;

		if (reg & HOTPLUG_CHG)
			return DP_IRQ_TYPE_HP_CHANGE;

		return DP_IRQ_TYPE_UNKNOWN;
	}
}

void analogix_dp_reset_aux(struct analogix_dp_device *dp)
{
	u32 reg;

	/* Disable AUX channel module */
	reg = readl(dp->reg_base + ANALOGIX_DP_FUNC_EN_2);
	reg |= AUX_FUNC_EN_N;
	writel(reg, dp->reg_base + ANALOGIX_DP_FUNC_EN_2);
}

void analogix_dp_init_aux(struct analogix_dp_device *dp)
{
	u32 reg;

	/* Clear inerrupts related to AUX channel */
	reg = RPLY_RECEIV | AUX_ERR;
	writel(reg, dp->reg_base + ANALOGIX_DP_INT_STA);

	analogix_dp_reset_aux(dp);

	/* Disable AUX transaction H/W retry */
	if (dp->plat_data && is_rockchip(dp->plat_data->dev_type))
		reg = AUX_BIT_PERIOD_EXPECTED_DELAY(0) |
		      AUX_HW_RETRY_COUNT_SEL(3) |
		      AUX_HW_RETRY_INTERVAL_600_MICROSECONDS;
	else
		reg = AUX_BIT_PERIOD_EXPECTED_DELAY(3) |
		      AUX_HW_RETRY_COUNT_SEL(0) |
		      AUX_HW_RETRY_INTERVAL_600_MICROSECONDS;
	writel(reg, dp->reg_base + ANALOGIX_DP_AUX_HW_RETRY_CTL);

	/* Receive AUX Channel DEFER commands equal to DEFFER_COUNT*64 */
	reg = DEFER_CTRL_EN | DEFER_COUNT(1);
	writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_DEFER_CTL);

	/* Enable AUX channel module */
	reg = readl(dp->reg_base + ANALOGIX_DP_FUNC_EN_2);
	reg &= ~AUX_FUNC_EN_N;
	writel(reg, dp->reg_base + ANALOGIX_DP_FUNC_EN_2);
}

int analogix_dp_get_plug_in_status(struct analogix_dp_device *dp)
{
	u32 reg;

	if (gpio_is_valid(dp->hpd_gpio)) {
		if (gpio_get_value(dp->hpd_gpio))
			return 0;
	} else {
		reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_3);
		if (reg & HPD_STATUS)
			return 0;
	}

	return -EINVAL;
}

void analogix_dp_enable_sw_function(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_FUNC_EN_1);
	reg &= ~SW_FUNC_EN_N;
	writel(reg, dp->reg_base + ANALOGIX_DP_FUNC_EN_1);
}

int analogix_dp_start_aux_transaction(struct analogix_dp_device *dp)
{
	int reg;
	int retval = 0;
	int timeout_loop = 0;

	/* Enable AUX CH operation */
	reg = readl(dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_2);
	reg |= AUX_EN;
	writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_2);

	/* Is AUX CH command reply received? */
	reg = readl(dp->reg_base + ANALOGIX_DP_INT_STA);
	while (!(reg & RPLY_RECEIV)) {
		timeout_loop++;
		if (DP_TIMEOUT_LOOP_COUNT < timeout_loop) {
			dev_err(dp->dev, "AUX CH command reply failed!\n");
			return -ETIMEDOUT;
		}
		reg = readl(dp->reg_base + ANALOGIX_DP_INT_STA);
		usleep_range(10, 11);
	}

	/* Clear interrupt source for AUX CH command reply */
	writel(RPLY_RECEIV, dp->reg_base + ANALOGIX_DP_INT_STA);

	/* Clear interrupt source for AUX CH access error */
	reg = readl(dp->reg_base + ANALOGIX_DP_INT_STA);
	if (reg & AUX_ERR) {
		writel(AUX_ERR, dp->reg_base + ANALOGIX_DP_INT_STA);
		return -EREMOTEIO;
	}

	/* Check AUX CH error access status */
	reg = readl(dp->reg_base + ANALOGIX_DP_AUX_CH_STA);
	if ((reg & AUX_STATUS_MASK) != 0) {
		dev_err(dp->dev, "AUX CH error happens: %d\n\n",
			reg & AUX_STATUS_MASK);
		return -EREMOTEIO;
	}

	return retval;
}

int analogix_dp_write_byte_to_dpcd(struct analogix_dp_device *dp,
				   unsigned int reg_addr,
				   unsigned char data)
{
	u32 reg;
	int i;
	int retval;

	for (i = 0; i < 3; i++) {
		/* Clear AUX CH data buffer */
		reg = BUF_CLR;
		writel(reg, dp->reg_base + ANALOGIX_DP_BUFFER_DATA_CTL);

		/* Select DPCD device address */
		reg = AUX_ADDR_7_0(reg_addr);
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_7_0);
		reg = AUX_ADDR_15_8(reg_addr);
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_15_8);
		reg = AUX_ADDR_19_16(reg_addr);
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_19_16);

		/* Write data buffer */
		reg = (unsigned int)data;
		writel(reg, dp->reg_base + ANALOGIX_DP_BUF_DATA_0);

		/*
		 * Set DisplayPort transaction and write 1 byte
		 * If bit 3 is 1, DisplayPort transaction.
		 * If Bit 3 is 0, I2C transaction.
		 */
		reg = AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_WRITE;
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_1);

		/* Start AUX transaction */
		retval = analogix_dp_start_aux_transaction(dp);
		if (retval == 0)
			break;

		dev_dbg(dp->dev, "%s: Aux Transaction fail!\n", __func__);
	}

	return retval;
}

int analogix_dp_read_byte_from_dpcd(struct analogix_dp_device *dp,
				    unsigned int reg_addr,
				    unsigned char *data)
{
	u32 reg;
	int i;
	int retval;

	for (i = 0; i < 3; i++) {
		/* Clear AUX CH data buffer */
		reg = BUF_CLR;
		writel(reg, dp->reg_base + ANALOGIX_DP_BUFFER_DATA_CTL);

		/* Select DPCD device address */
		reg = AUX_ADDR_7_0(reg_addr);
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_7_0);
		reg = AUX_ADDR_15_8(reg_addr);
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_15_8);
		reg = AUX_ADDR_19_16(reg_addr);
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_19_16);

		/*
		 * Set DisplayPort transaction and read 1 byte
		 * If bit 3 is 1, DisplayPort transaction.
		 * If Bit 3 is 0, I2C transaction.
		 */
		reg = AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_READ;
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_1);

		/* Start AUX transaction */
		retval = analogix_dp_start_aux_transaction(dp);
		if (retval == 0)
			break;

		dev_dbg(dp->dev, "%s: Aux Transaction fail!\n", __func__);
	}

	/* Read data buffer */
	reg = readl(dp->reg_base + ANALOGIX_DP_BUF_DATA_0);
	*data = (unsigned char)(reg & 0xff);

	return retval;
}

int analogix_dp_write_bytes_to_dpcd(struct analogix_dp_device *dp,
				    unsigned int reg_addr,
				    unsigned int count,
				    unsigned char data[])
{
	u32 reg;
	unsigned int start_offset;
	unsigned int cur_data_count;
	unsigned int cur_data_idx;
	int i;
	int retval = 0;

	/* Clear AUX CH data buffer */
	reg = BUF_CLR;
	writel(reg, dp->reg_base + ANALOGIX_DP_BUFFER_DATA_CTL);

	start_offset = 0;
	while (start_offset < count) {
		/* Buffer size of AUX CH is 16 * 4bytes */
		if ((count - start_offset) > 16)
			cur_data_count = 16;
		else
			cur_data_count = count - start_offset;

		for (i = 0; i < 3; i++) {
			/* Select DPCD device address */
			reg = AUX_ADDR_7_0(reg_addr + start_offset);
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_7_0);
			reg = AUX_ADDR_15_8(reg_addr + start_offset);
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_15_8);
			reg = AUX_ADDR_19_16(reg_addr + start_offset);
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_19_16);

			for (cur_data_idx = 0; cur_data_idx < cur_data_count;
			     cur_data_idx++) {
				reg = data[start_offset + cur_data_idx];
				writel(reg, dp->reg_base +
				       ANALOGIX_DP_BUF_DATA_0 +
				       4 * cur_data_idx);
			}

			/*
			 * Set DisplayPort transaction and write
			 * If bit 3 is 1, DisplayPort transaction.
			 * If Bit 3 is 0, I2C transaction.
			 */
			reg = AUX_LENGTH(cur_data_count) |
				AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_WRITE;
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_1);

			/* Start AUX transaction */
			retval = analogix_dp_start_aux_transaction(dp);
			if (retval == 0)
				break;

			dev_dbg(dp->dev, "%s: Aux Transaction fail!\n",
				__func__);
		}

		start_offset += cur_data_count;
	}

	return retval;
}

int analogix_dp_read_bytes_from_dpcd(struct analogix_dp_device *dp,
				     unsigned int reg_addr,
				     unsigned int count,
				     unsigned char data[])
{
	u32 reg;
	unsigned int start_offset;
	unsigned int cur_data_count;
	unsigned int cur_data_idx;
	int i;
	int retval = 0;

	/* Clear AUX CH data buffer */
	reg = BUF_CLR;
	writel(reg, dp->reg_base + ANALOGIX_DP_BUFFER_DATA_CTL);

	start_offset = 0;
	while (start_offset < count) {
		/* Buffer size of AUX CH is 16 * 4bytes */
		if ((count - start_offset) > 16)
			cur_data_count = 16;
		else
			cur_data_count = count - start_offset;

		/* AUX CH Request Transaction process */
		for (i = 0; i < 3; i++) {
			/* Select DPCD device address */
			reg = AUX_ADDR_7_0(reg_addr + start_offset);
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_7_0);
			reg = AUX_ADDR_15_8(reg_addr + start_offset);
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_15_8);
			reg = AUX_ADDR_19_16(reg_addr + start_offset);
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_19_16);

			/*
			 * Set DisplayPort transaction and read
			 * If bit 3 is 1, DisplayPort transaction.
			 * If Bit 3 is 0, I2C transaction.
			 */
			reg = AUX_LENGTH(cur_data_count) |
				AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_READ;
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_1);

			/* Start AUX transaction */
			retval = analogix_dp_start_aux_transaction(dp);
			if (retval == 0)
				break;

			dev_dbg(dp->dev, "%s: Aux Transaction fail!\n",
				__func__);
		}

		for (cur_data_idx = 0; cur_data_idx < cur_data_count;
		    cur_data_idx++) {
			reg = readl(dp->reg_base + ANALOGIX_DP_BUF_DATA_0
						 + 4 * cur_data_idx);
			data[start_offset + cur_data_idx] =
				(unsigned char)reg;
		}

		start_offset += cur_data_count;
	}

	return retval;
}

int analogix_dp_select_i2c_device(struct analogix_dp_device *dp,
				  unsigned int device_addr,
				  unsigned int reg_addr)
{
	u32 reg;
	int retval;

	/* Set EDID device address */
	reg = device_addr;
	writel(reg, dp->reg_base + ANALOGIX_DP_AUX_ADDR_7_0);
	writel(0x0, dp->reg_base + ANALOGIX_DP_AUX_ADDR_15_8);
	writel(0x0, dp->reg_base + ANALOGIX_DP_AUX_ADDR_19_16);

	/* Set offset from base address of EDID device */
	writel(reg_addr, dp->reg_base + ANALOGIX_DP_BUF_DATA_0);

	/*
	 * Set I2C transaction and write address
	 * If bit 3 is 1, DisplayPort transaction.
	 * If Bit 3 is 0, I2C transaction.
	 */
	reg = AUX_TX_COMM_I2C_TRANSACTION | AUX_TX_COMM_MOT |
		AUX_TX_COMM_WRITE;
	writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_1);

	/* Start AUX transaction */
	retval = analogix_dp_start_aux_transaction(dp);
	if (retval != 0)
		dev_dbg(dp->dev, "%s: Aux Transaction fail!\n", __func__);

	return retval;
}

int analogix_dp_read_byte_from_i2c(struct analogix_dp_device *dp,
				   unsigned int device_addr,
				   unsigned int reg_addr,
				   unsigned int *data)
{
	u32 reg;
	int i;
	int retval;

	for (i = 0; i < 3; i++) {
		/* Clear AUX CH data buffer */
		reg = BUF_CLR;
		writel(reg, dp->reg_base + ANALOGIX_DP_BUFFER_DATA_CTL);

		/* Select EDID device */
		retval = analogix_dp_select_i2c_device(dp, device_addr,
						       reg_addr);
		if (retval != 0)
			continue;

		/*
		 * Set I2C transaction and read data
		 * If bit 3 is 1, DisplayPort transaction.
		 * If Bit 3 is 0, I2C transaction.
		 */
		reg = AUX_TX_COMM_I2C_TRANSACTION |
			AUX_TX_COMM_READ;
		writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_1);

		/* Start AUX transaction */
		retval = analogix_dp_start_aux_transaction(dp);
		if (retval == 0)
			break;

		dev_dbg(dp->dev, "%s: Aux Transaction fail!\n", __func__);
	}

	/* Read data */
	if (retval == 0)
		*data = readl(dp->reg_base + ANALOGIX_DP_BUF_DATA_0);

	return retval;
}

int analogix_dp_read_bytes_from_i2c(struct analogix_dp_device *dp,
				    unsigned int device_addr,
				    unsigned int reg_addr,
				    unsigned int count,
				    unsigned char edid[])
{
	u32 reg;
	unsigned int i, j;
	unsigned int cur_data_idx;
	unsigned int defer = 0;
	int retval = 0;

	for (i = 0; i < count; i += 16) {
		for (j = 0; j < 3; j++) {
			/* Clear AUX CH data buffer */
			reg = BUF_CLR;
			writel(reg, dp->reg_base + ANALOGIX_DP_BUFFER_DATA_CTL);

			/* Set normal AUX CH command */
			reg = readl(dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_2);
			reg &= ~ADDR_ONLY;
			writel(reg, dp->reg_base + ANALOGIX_DP_AUX_CH_CTL_2);

			/*
			 * If Rx sends defer, Tx sends only reads
			 * request without sending address
			 */
			if (!defer)
				retval = analogix_dp_select_i2c_device(dp,
						device_addr, reg_addr + i);
			else
				defer = 0;

			if (retval == 0) {
				/*
				 * Set I2C transaction and write data
				 * If bit 3 is 1, DisplayPort transaction.
				 * If Bit 3 is 0, I2C transaction.
				 */
				reg = AUX_LENGTH(16) |
					AUX_TX_COMM_I2C_TRANSACTION |
					AUX_TX_COMM_READ;
				writel(reg, dp->reg_base +
					ANALOGIX_DP_AUX_CH_CTL_1);

				/* Start AUX transaction */
				retval = analogix_dp_start_aux_transaction(dp);
				if (retval == 0)
					break;

				dev_dbg(dp->dev, "%s: Aux Transaction fail!\n",
					__func__);
			}
			/* Check if Rx sends defer */
			reg = readl(dp->reg_base + ANALOGIX_DP_AUX_RX_COMM);
			if (reg == AUX_RX_COMM_AUX_DEFER ||
			    reg == AUX_RX_COMM_I2C_DEFER) {
				dev_err(dp->dev, "Defer: %d\n\n", reg);
				defer = 1;
			}
		}

		for (cur_data_idx = 0; cur_data_idx < 16; cur_data_idx++) {
			reg = readl(dp->reg_base + ANALOGIX_DP_BUF_DATA_0
						 + 4 * cur_data_idx);
			edid[i + cur_data_idx] = (unsigned char)reg;
		}
	}

	return retval;
}

void analogix_dp_set_link_bandwidth(struct analogix_dp_device *dp, u32 bwtype)
{
	u32 reg;

	reg = bwtype;
	if ((bwtype == DP_LINK_BW_2_7) || (bwtype == DP_LINK_BW_1_62))
		writel(reg, dp->reg_base + ANALOGIX_DP_LINK_BW_SET);
}

void analogix_dp_get_link_bandwidth(struct analogix_dp_device *dp, u32 *bwtype)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LINK_BW_SET);
	*bwtype = reg;
}

void analogix_dp_set_lane_count(struct analogix_dp_device *dp, u32 count)
{
	u32 reg;

	reg = count;
	writel(reg, dp->reg_base + ANALOGIX_DP_LANE_COUNT_SET);
}

void analogix_dp_get_lane_count(struct analogix_dp_device *dp, u32 *count)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LANE_COUNT_SET);
	*count = reg;
}

void analogix_dp_enable_enhanced_mode(struct analogix_dp_device *dp,
				      bool enable)
{
	u32 reg;

	if (enable) {
		reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_4);
		reg |= ENHANCED;
		writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_4);
	} else {
		reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_4);
		reg &= ~ENHANCED;
		writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_4);
	}
}

void analogix_dp_set_training_pattern(struct analogix_dp_device *dp,
				      enum pattern_set pattern)
{
	u32 reg;

	switch (pattern) {
	case PRBS7:
		reg = SCRAMBLING_ENABLE | LINK_QUAL_PATTERN_SET_PRBS7;
		writel(reg, dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
		break;
	case D10_2:
		reg = SCRAMBLING_ENABLE | LINK_QUAL_PATTERN_SET_D10_2;
		writel(reg, dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
		break;
	case TRAINING_PTN1:
		reg = SCRAMBLING_DISABLE | SW_TRAINING_PATTERN_SET_PTN1;
		writel(reg, dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
		break;
	case TRAINING_PTN2:
		reg = SCRAMBLING_DISABLE | SW_TRAINING_PATTERN_SET_PTN2;
		writel(reg, dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
		break;
	case DP_NONE:
		reg = SCRAMBLING_ENABLE |
			LINK_QUAL_PATTERN_SET_DISABLE |
			SW_TRAINING_PATTERN_SET_NORMAL;
		writel(reg, dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
		break;
	default:
		break;
	}
}

void analogix_dp_set_lane0_pre_emphasis(struct analogix_dp_device *dp,
					u32 level)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LN0_LINK_TRAINING_CTL);
	reg &= ~PRE_EMPHASIS_SET_MASK;
	reg |= level << PRE_EMPHASIS_SET_SHIFT;
	writel(reg, dp->reg_base + ANALOGIX_DP_LN0_LINK_TRAINING_CTL);
}

void analogix_dp_set_lane1_pre_emphasis(struct analogix_dp_device *dp,
					u32 level)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LN1_LINK_TRAINING_CTL);
	reg &= ~PRE_EMPHASIS_SET_MASK;
	reg |= level << PRE_EMPHASIS_SET_SHIFT;
	writel(reg, dp->reg_base + ANALOGIX_DP_LN1_LINK_TRAINING_CTL);
}

void analogix_dp_set_lane2_pre_emphasis(struct analogix_dp_device *dp,
					u32 level)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LN2_LINK_TRAINING_CTL);
	reg &= ~PRE_EMPHASIS_SET_MASK;
	reg |= level << PRE_EMPHASIS_SET_SHIFT;
	writel(reg, dp->reg_base + ANALOGIX_DP_LN2_LINK_TRAINING_CTL);
}

void analogix_dp_set_lane3_pre_emphasis(struct analogix_dp_device *dp,
					u32 level)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LN3_LINK_TRAINING_CTL);
	reg &= ~PRE_EMPHASIS_SET_MASK;
	reg |= level << PRE_EMPHASIS_SET_SHIFT;
	writel(reg, dp->reg_base + ANALOGIX_DP_LN3_LINK_TRAINING_CTL);
}

void analogix_dp_set_lane0_link_training(struct analogix_dp_device *dp,
					 u32 training_lane)
{
	u32 reg;

	reg = training_lane;
	writel(reg, dp->reg_base + ANALOGIX_DP_LN0_LINK_TRAINING_CTL);
}

void analogix_dp_set_lane1_link_training(struct analogix_dp_device *dp,
					 u32 training_lane)
{
	u32 reg;

	reg = training_lane;
	writel(reg, dp->reg_base + ANALOGIX_DP_LN1_LINK_TRAINING_CTL);
}

void analogix_dp_set_lane2_link_training(struct analogix_dp_device *dp,
					 u32 training_lane)
{
	u32 reg;

	reg = training_lane;
	writel(reg, dp->reg_base + ANALOGIX_DP_LN2_LINK_TRAINING_CTL);
}

void analogix_dp_set_lane3_link_training(struct analogix_dp_device *dp,
					 u32 training_lane)
{
	u32 reg;

	reg = training_lane;
	writel(reg, dp->reg_base + ANALOGIX_DP_LN3_LINK_TRAINING_CTL);
}

u32 analogix_dp_get_lane0_link_training(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LN0_LINK_TRAINING_CTL);
	return reg;
}

u32 analogix_dp_get_lane1_link_training(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LN1_LINK_TRAINING_CTL);
	return reg;
}

u32 analogix_dp_get_lane2_link_training(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LN2_LINK_TRAINING_CTL);
	return reg;
}

u32 analogix_dp_get_lane3_link_training(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_LN3_LINK_TRAINING_CTL);
	return reg;
}

void analogix_dp_reset_macro(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_PHY_TEST);
	reg |= MACRO_RST;
	writel(reg, dp->reg_base + ANALOGIX_DP_PHY_TEST);

	/* 10 us is the minimum reset time. */
	usleep_range(10, 20);

	reg &= ~MACRO_RST;
	writel(reg, dp->reg_base + ANALOGIX_DP_PHY_TEST);
}

void analogix_dp_init_video(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = VSYNC_DET | VID_FORMAT_CHG | VID_CLK_CHG;
	writel(reg, dp->reg_base + ANALOGIX_DP_COMMON_INT_STA_1);

	reg = 0x0;
	writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_1);

	reg = CHA_CRI(4) | CHA_CTRL;
	writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_2);

	reg = 0x0;
	writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_3);

	reg = VID_HRES_TH(2) | VID_VRES_TH(0);
	writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_8);
}

void analogix_dp_set_video_color_format(struct analogix_dp_device *dp)
{
	u32 reg;

	/* Configure the input color depth, color space, dynamic range */
	reg = (dp->video_info.dynamic_range << IN_D_RANGE_SHIFT) |
		(dp->video_info.color_depth << IN_BPC_SHIFT) |
		(dp->video_info.color_space << IN_COLOR_F_SHIFT);
	writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_2);

	/* Set Input Color YCbCr Coefficients to ITU601 or ITU709 */
	reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_3);
	reg &= ~IN_YC_COEFFI_MASK;
	if (dp->video_info.ycbcr_coeff)
		reg |= IN_YC_COEFFI_ITU709;
	else
		reg |= IN_YC_COEFFI_ITU601;
	writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_3);
}

int analogix_dp_is_slave_video_stream_clock_on(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_1);
	writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_1);

	reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_1);

	if (!(reg & DET_STA)) {
		dev_dbg(dp->dev, "Input stream clock not detected.\n");
		return -EINVAL;
	}

	reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_2);
	writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_2);

	reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_2);
	dev_dbg(dp->dev, "wait SYS_CTL_2.\n");

	if (reg & CHA_STA) {
		dev_dbg(dp->dev, "Input stream clk is changing\n");
		return -EINVAL;
	}

	return 0;
}

void analogix_dp_set_video_cr_mn(struct analogix_dp_device *dp,
				 enum clock_recovery_m_value_type type,
				 u32 m_value, u32 n_value)
{
	u32 reg;

	if (type == REGISTER_M) {
		reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_4);
		reg |= FIX_M_VID;
		writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_4);
		reg = m_value & 0xff;
		writel(reg, dp->reg_base + ANALOGIX_DP_M_VID_0);
		reg = (m_value >> 8) & 0xff;
		writel(reg, dp->reg_base + ANALOGIX_DP_M_VID_1);
		reg = (m_value >> 16) & 0xff;
		writel(reg, dp->reg_base + ANALOGIX_DP_M_VID_2);

		reg = n_value & 0xff;
		writel(reg, dp->reg_base + ANALOGIX_DP_N_VID_0);
		reg = (n_value >> 8) & 0xff;
		writel(reg, dp->reg_base + ANALOGIX_DP_N_VID_1);
		reg = (n_value >> 16) & 0xff;
		writel(reg, dp->reg_base + ANALOGIX_DP_N_VID_2);
	} else  {
		reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_4);
		reg &= ~FIX_M_VID;
		writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_4);

		writel(0x00, dp->reg_base + ANALOGIX_DP_N_VID_0);
		writel(0x80, dp->reg_base + ANALOGIX_DP_N_VID_1);
		writel(0x00, dp->reg_base + ANALOGIX_DP_N_VID_2);
	}
}

void analogix_dp_set_video_timing_mode(struct analogix_dp_device *dp, u32 type)
{
	u32 reg;

	if (type == VIDEO_TIMING_FROM_CAPTURE) {
		reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);
		reg &= ~FORMAT_SEL;
		writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);
	} else {
		reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);
		reg |= FORMAT_SEL;
		writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);
	}
}

void analogix_dp_enable_video_master(struct analogix_dp_device *dp, bool enable)
{
	u32 reg;

	if (enable) {
		reg = readl(dp->reg_base + ANALOGIX_DP_SOC_GENERAL_CTL);
		reg &= ~VIDEO_MODE_MASK;
		reg |= VIDEO_MASTER_MODE_EN | VIDEO_MODE_MASTER_MODE;
		writel(reg, dp->reg_base + ANALOGIX_DP_SOC_GENERAL_CTL);
	} else {
		reg = readl(dp->reg_base + ANALOGIX_DP_SOC_GENERAL_CTL);
		reg &= ~VIDEO_MODE_MASK;
		reg |= VIDEO_MODE_SLAVE_MODE;
		writel(reg, dp->reg_base + ANALOGIX_DP_SOC_GENERAL_CTL);
	}
}

void analogix_dp_start_video(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_1);
	reg |= VIDEO_EN;
	writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_1);
}

int analogix_dp_is_video_stream_on(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_3);
	writel(reg, dp->reg_base + ANALOGIX_DP_SYS_CTL_3);

	reg = readl(dp->reg_base + ANALOGIX_DP_SYS_CTL_3);
	if (!(reg & STRM_VALID)) {
		dev_dbg(dp->dev, "Input video stream is not detected.\n");
		return -EINVAL;
	}

	return 0;
}

void analogix_dp_config_video_slave_mode(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_FUNC_EN_1);
	reg &= ~(MASTER_VID_FUNC_EN_N | SLAVE_VID_FUNC_EN_N);
	reg |= MASTER_VID_FUNC_EN_N;
	writel(reg, dp->reg_base + ANALOGIX_DP_FUNC_EN_1);

	reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);
	reg &= ~INTERACE_SCAN_CFG;
	reg |= (dp->video_info.interlaced << 2);
	writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);

	reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);
	reg &= ~VSYNC_POLARITY_CFG;
	reg |= (dp->video_info.v_sync_polarity << 1);
	writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);

	reg = readl(dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);
	reg &= ~HSYNC_POLARITY_CFG;
	reg |= (dp->video_info.h_sync_polarity << 0);
	writel(reg, dp->reg_base + ANALOGIX_DP_VIDEO_CTL_10);

	reg = AUDIO_MODE_SPDIF_MODE | VIDEO_MODE_SLAVE_MODE;
	writel(reg, dp->reg_base + ANALOGIX_DP_SOC_GENERAL_CTL);
}

void analogix_dp_enable_scrambling(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
	reg &= ~SCRAMBLING_DISABLE;
	writel(reg, dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
}

void analogix_dp_disable_scrambling(struct analogix_dp_device *dp)
{
	u32 reg;

	reg = readl(dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
	reg |= SCRAMBLING_DISABLE;
	writel(reg, dp->reg_base + ANALOGIX_DP_TRAINING_PTN_SET);
}
