// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include "stfcamss.h"
#include <linux/sort.h>

static int stf_csiphy_clk_set(struct stf_csiphy_dev *csiphy_dev, int on)
{
	struct stf_vin_dev *vin = csiphy_dev->stfcamss->vin;
	static int init_flag;
	static struct mutex count_lock;
	static int count;

	if (!init_flag) {
		init_flag = 1;
		mutex_init(&count_lock);
	}
	mutex_lock(&count_lock);
	if (on) {
		if (count == 0) {
			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_CFGCLK_ISPCORE_2X_CTRL,
				0x1F, 0x08);
			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_CFGCLK_ISPCORE_2X_CTRL,
				1 << 31, 1 << 31);

			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_REFCLK_ISPCORE_2X_CTRL,
				0x1F, 0x10);
			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_REFCLK_ISPCORE_2X_CTRL,
				1 << 31, 1 << 31);

			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_TXCLKESC_IN_CTRL,
				0x3F, 0x28);
			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_TXCLKESC_IN_CTRL,
				1 << 31, 1 << 31);
		}
		count++;
	} else {
		if (count == 0)
			goto exit;
		if (count == 1) {
			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_CFGCLK_ISPCORE_2X_CTRL,
				1 << 31, 0 << 31);

			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_REFCLK_ISPCORE_2X_CTRL,
				1 << 31, 0 << 31);

			reg_set_bit(vin->clkgen_base,
				CLK_DPHY_TXCLKESC_IN_CTRL,
				1 << 31, 0 << 31);
		}
		count--;
	}
exit:
	mutex_unlock(&count_lock);
	return 0;
}

static int stf_csiphy_clk_enable(struct stf_csiphy_dev *csiphy_dev)
{
	return stf_csiphy_clk_set(csiphy_dev, 1);
}

static int stf_csiphy_clk_disable(struct stf_csiphy_dev *csiphy_dev)
{
	return stf_csiphy_clk_set(csiphy_dev, 0);
}

static int cmp_func(const void *x1, const void *x2)
{
	return *((unsigned char *)x1) - *((unsigned char *)x2);
}

int try_cfg(struct csi2phy_cfg2 *cfg, struct csi2phy_cfg *cfg0,
		struct csi2phy_cfg *cfg1)
{
	int i = 0;

	if (cfg0 && cfg1) {
		st_debug(ST_CSIPHY, "CSIPHY use 2 clk mode\n");
		cfg->num_clks = 2;
		cfg->num_data_lanes =
			cfg1->num_data_lanes + cfg0->num_data_lanes;
		if (cfg->num_data_lanes > STF_CSI2_MAX_DATA_LANES)
			return -EINVAL;
		cfg->clock_lane = cfg0->clock_lane;
		cfg->lane_polarities[0] = cfg0->lane_polarities[0];
		cfg->clock1_lane = cfg1->clock_lane;
		cfg->lane_polarities[1] = cfg1->lane_polarities[0];
		for (i = 0; i < cfg0->num_data_lanes; i++) {
			cfg->data_lanes[i] = cfg0->data_lanes[i];
			cfg->lane_polarities[i + 2] =
				cfg0->lane_polarities[i + 1];
		}

		for (i = cfg0->num_data_lanes; i < cfg->num_data_lanes; i++) {
			cfg->data_lanes[i] =
				cfg1->data_lanes[i - cfg0->num_data_lanes];
			cfg->lane_polarities[i + 2] =
				cfg1->lane_polarities[i - cfg0->num_data_lanes + 1];
		}
	} else if (cfg0 && !cfg1) {
		st_debug(ST_CSIPHY, "CSIPHY cfg0 use 1 clk mode\n");
		cfg->num_clks = 1;
		cfg->num_data_lanes = cfg0->num_data_lanes;
		cfg->clock_lane = cfg->clock1_lane  = cfg0->clock_lane;
		cfg->lane_polarities[0] = cfg->lane_polarities[1] =
						cfg0->lane_polarities[0];
		for (i = 0; i < cfg0->num_data_lanes; i++) {
			cfg->data_lanes[i] = cfg0->data_lanes[i];
			cfg->lane_polarities[i + 2] = cfg0->lane_polarities[i + 1];
		}
	} else if (!cfg0 && cfg1) {
		st_debug(ST_CSIPHY, "CSIPHY cfg1 use 1 clk mode\n");
		cfg->num_clks = 1;
		cfg->num_data_lanes = cfg1->num_data_lanes;
		cfg->clock_lane = cfg->clock1_lane  = cfg1->clock_lane;
		cfg->lane_polarities[0] = cfg->lane_polarities[1] =
						cfg1->lane_polarities[0];
		for (i = 0; i < cfg1->num_data_lanes; i++) {
			cfg->data_lanes[i] = cfg1->data_lanes[i];
			cfg->lane_polarities[i + 2] = cfg1->lane_polarities[i + 1];
		}
	} else {
		return -EINVAL;
	}

#ifndef USE_CSIDPHY_ONE_CLK_MODE
	sort(cfg->data_lanes, cfg->num_data_lanes,
			sizeof(cfg->data_lanes[0]), cmp_func, NULL);
#endif
	for (i = 0; i < cfg->num_data_lanes; i++)
		st_debug(ST_CSIPHY, "%d: %d\n", i, cfg->data_lanes[i]);
	return 0;
}

static int csi2rx_dphy_config(struct stf_vin_dev *vin,
		struct stf_csiphy_dev *csiphy_dev)
{
	struct csi2phy_cfg2 cfg2 = {0};
	struct csi2phy_cfg2 *cfg = &cfg2;
	struct stf_csiphy_dev *csiphy0_dev =
		&csiphy_dev->stfcamss->csiphy_dev[0];
	struct stf_csiphy_dev *csiphy1_dev =
		&csiphy_dev->stfcamss->csiphy_dev[1];
	struct csi2phy_cfg *phy0cfg = csiphy0_dev->csiphy;
	struct csi2phy_cfg *phy1cfg = csiphy1_dev->csiphy;
	int i;
	int id = csiphy_dev->id;
	u32 reg = 0;

	if (!phy0cfg && !phy1cfg)
		return -EINVAL;

#ifdef USE_CSIDPHY_ONE_CLK_MODE
	if (id == 0) {
		phy0cfg = csiphy0_dev->csiphy;
		phy1cfg = NULL;
	} else {
		phy0cfg = NULL;
		phy1cfg = csiphy1_dev->csiphy;
	}
#endif

	if (try_cfg(cfg, phy0cfg, phy1cfg))
		return -EINVAL;

	id = cfg->num_clks == 2 ? 1 : 0;

	reg = reg_read(vin->sysctrl_base, SYSCTRL_REG4);

	st_debug(ST_CSIPHY, "id = %d, clock_lane = %d, SYSCTRL_REG4: 0x%x\n",
			id, cfg->clock_lane, reg);
	st_debug(ST_CSIPHY, "csiphy_dev: csi_id = %d, id = %d\n",
			csiphy_dev->csi_id, csiphy_dev->id);

	reg = set_bits(reg, id, 0, 0x1);
	reg = set_bits(reg, cfg->clock_lane, 1, 0x7 << 1);
	reg = set_bits(reg, cfg->lane_polarities[0], 19, 0x1 << 19);
	reg = set_bits(reg, cfg->clock1_lane, 4, 0x7 << 4);
	reg = set_bits(reg, cfg->lane_polarities[1], 20, 0x1 << 20);

	for (i = 0; i < cfg->num_data_lanes; i++) {
		reg = set_bits(reg, cfg->data_lanes[i], (7 + i * 3),
				0x7 << (7 + i * 3));

		reg = set_bits(reg, !!cfg->lane_polarities[i + 2],
					(21 + i), 0x1 << (21 + i));
	}

	reg_write(vin->sysctrl_base, SYSCTRL_REG4, reg);

	reg = reg_read(vin->sysctrl_base, SYSCTRL_DPHY_CTRL);
	for (i = 0; i < cfg->num_data_lanes; i++) {
		reg = set_bits(reg, 1, (11 + cfg->data_lanes[i]),
				0x1 << (11 + cfg->data_lanes[i]));
	}

	reg_write(vin->sysctrl_base, SYSCTRL_DPHY_CTRL, reg);

	print_reg(ST_CSIPHY, vin->sysctrl_base, SYSCTRL_REG4);
	print_reg(ST_CSIPHY, vin->sysctrl_base, SYSCTRL_DPHY_CTRL);

	return 0;
}

static int stf_csiphy_config_set(struct stf_csiphy_dev *csiphy_dev)
{
	struct stf_vin_dev *vin = csiphy_dev->stfcamss->vin;

	st_debug(ST_CSIPHY, "%s, csiphy id = %d\n",
			__func__, csiphy_dev->id);

	csi2rx_dphy_config(vin, csiphy_dev);
	return 0;
}

static int stf_csiphy_stream_set(struct stf_csiphy_dev *csiphy_dev, int on)
{
	return 0;
}

#ifdef CONFIG_VIDEO_CADENCE_CSI2RX
static int stf_csi_clk_enable(struct stf_csiphy_dev *csiphy_dev)
{
	struct stf_vin_dev *vin = csiphy_dev->stfcamss->vin;

	reg_set_highest_bit(vin->clkgen_base, CLK_CSI2RX0_APB_CTRL);

	if (csiphy_dev->id == 0) {
		reg_set_bit(vin->clkgen_base,
				CLK_MIPI_RX0_PXL_CTRL,
				0x1F, 0x3);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_0_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_1_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_2_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_3_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_SYS0_CTRL);
	} else {
		reg_set_bit(vin->clkgen_base,
				CLK_MIPI_RX1_PXL_CTRL,
				0x1F, 0x3);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_0_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_1_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_2_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_3_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_SYS1_CTRL);
	}

	return 0;
}

static int stf_csi_clk_disable(struct stf_csiphy_dev *csiphy_dev)
{
	struct stf_vin_dev *vin = csiphy_dev->stfcamss->vin;

	reg_clr_highest_bit(vin->clkgen_base, CLK_CSI2RX0_APB_CTRL);

	if (csiphy_dev->id == 0) {
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_0_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_1_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_2_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_3_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_SYS0_CTRL);
	} else {
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_0_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_1_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_2_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_3_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_SYS1_CTRL);
	}

	return 0;
}

static int stf_csi_config_set(struct stf_csiphy_dev *csiphy_dev, int is_raw10)
{
	struct stf_vin_dev *vin = csiphy_dev->stfcamss->vin;
	u32 mipi_channel_sel, mipi_vc = 0;
	enum sensor_type s_type = SENSOR_ISP0;

	switch (s_type) {
	case SENSOR_VIN:
		break;
	case SENSOR_ISP0:
		reg_set_bit(vin->clkgen_base,
				CLK_ISP0_MIPI_CTRL,
				BIT(24), csiphy_dev->id << 24);

		reg_set_bit(vin->clkgen_base,
				CLK_C_ISP0_CTRL,
				BIT(25) | BIT(24),
				csiphy_dev->id << 24);

		mipi_channel_sel = csiphy_dev->id * 4 + mipi_vc;
		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_SRC_CHAN_SEL,
				0xF, mipi_channel_sel);

		if (is_raw10)
			reg_set_bit(vin->sysctrl_base,
					SYSCTRL_VIN_SRC_DW_SEL,
					BIT(4), 1 << 4);
		break;
	case SENSOR_ISP1:
		reg_set_bit(vin->clkgen_base,
				CLK_ISP1_MIPI_CTRL,
				BIT(24), csiphy_dev->id << 24);
		reg_set_bit(vin->clkgen_base,
				CLK_C_ISP1_CTRL,
				BIT(25) | BIT(24),
				csiphy_dev->id << 24);

		mipi_channel_sel = csiphy_dev->id * 4 + mipi_vc;
		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_SRC_CHAN_SEL,
				0xF << 4, mipi_channel_sel << 4);

		if (is_raw10)
			reg_set_bit(vin->sysctrl_base,
					SYSCTRL_VIN_SRC_DW_SEL,
					BIT(5), 1 << 5);
	default:
		break;
	}

	return 0;
}

static int stf_cdns_csi_power(struct stf_csiphy_dev *csiphy_dev, int on)
{
	if (on) {
		stf_csi_config_set(csiphy_dev, 1);
		stf_csi_clk_enable(csiphy_dev);
	} else
		stf_csi_clk_disable(csiphy_dev);

	return 0;
}
#endif

struct csiphy_hw_ops csiphy_ops = {
	.csiphy_clk_enable        = stf_csiphy_clk_enable,
	.csiphy_clk_disable       = stf_csiphy_clk_disable,
	.csiphy_config_set        = stf_csiphy_config_set,
	.csiphy_stream_set        = stf_csiphy_stream_set,
#ifdef CONFIG_VIDEO_CADENCE_CSI2RX
	.cdns_csi_power           = stf_cdns_csi_power,
#endif
};
