// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include "stfcamss.h"
#include <linux/sort.h>

static int stf_csiphy_clk_set(struct stf_csiphy_dev *csiphy_dev, int on)
{
	struct stfcamss *stfcamss = csiphy_dev->stfcamss;
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
		clk_set_rate(stfcamss->sys_clk[STFCLK_M31DPHY_CFGCLK_IN].clk,
			102400000);
		clk_set_rate(stfcamss->sys_clk[STFCLK_M31DPHY_REFCLK_IN].clk,
			51200000);
		clk_set_rate(stfcamss->sys_clk[STFCLK_M31DPHY_TXCLKESC_LAN0].clk,
			20480000);

		reset_control_deassert(stfcamss->sys_rst[STFRST_M31DPHY_HW].rstc);
		reset_control_deassert(stfcamss->sys_rst[STFRST_M31DPHY_B09_ALWAYS_ON].rstc);

		reg_set_bit(vin->rstgen_base,
			M31DPHY_APBCFGSAIF__SYSCFG_188,
			BIT(6), BIT(6));
//need to check one or two mipi input
#ifdef USE_CSIDPHY_ONE_CLK_MODE	//one mipi input
		reg_set_bit(vin->rstgen_base,
			M31DPHY_APBCFGSAIF__SYSCFG_188,
			BIT(7), BIT(7));
#else							//two mipi input
		reg_set_bit(vin->rstgen_base,
			M31DPHY_APBCFGSAIF__SYSCFG_188,
			BIT(7), 0x2<<7);
#endif
		count++;
	} else {
		if (count == 0)
			goto exit;
		if (count == 1) {
			reset_control_assert(stfcamss->sys_rst[STFRST_M31DPHY_HW].rstc);
			reset_control_assert(stfcamss->sys_rst[STFRST_M31DPHY_B09_ALWAYS_ON].rstc);
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

#ifndef USE_CSIDPHY_ONE_CLK_MODE
static int cmp_func(const void *x1, const void *x2)
{
	return *((unsigned char *)x1) - *((unsigned char *)x2);
}
#endif

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
	struct csi2phy_cfg *cfg;
	struct stf_csiphy_dev *csiphy0_dev =
		&csiphy_dev->stfcamss->csiphy_dev[0];
	struct csi2phy_cfg *phy0cfg = csiphy0_dev->csiphy;
	int id = csiphy_dev->id;

	if (!phy0cfg)
		return -EINVAL;

	if (id == 0)
		cfg = phy0cfg;

	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_4, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_8, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_12, 0xff0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_16, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_20, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_24, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_28, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_32, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_36, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_40, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_44, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_48, 0x24000000);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_52, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_56, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_60, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_64, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_68, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_72, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_76, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_80, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_84, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_88, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_92, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_96, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_100, 0x02000000);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_104, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_108, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_112, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_116, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_120, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_124, 0xc);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_128, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_132, 0xcc500000);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_136, 0xcc);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_140, 0x0);
	reg_write(vin->rstgen_base, M31DPHY_APBCFGSAIF__SYSCFG_144, 0x0);

	reg_set_bit(vin->rstgen_base,		//r100_ctrl0_2d1c_efuse_en
		M31DPHY_APBCFGSAIF__SYSCFG_0,
		BIT(6), 1<<6);
	reg_set_bit(vin->rstgen_base,		//r100_ctrl0_2d1c_efuse_in
		M31DPHY_APBCFGSAIF__SYSCFG_0,
		BIT(12)|BIT(11)|BIT(10)|BIT(9)|BIT(8)|BIT(7), 0x1b<<7);
	reg_set_bit(vin->rstgen_base,		//r100_ctrl1_2d1c_efuse_en
		M31DPHY_APBCFGSAIF__SYSCFG_0,
		BIT(13), 1<<13);
	reg_set_bit(vin->rstgen_base,		//r100_ctrl1_2d1c_efuse_in
		M31DPHY_APBCFGSAIF__SYSCFG_0,
		BIT(19)|BIT(18)|BIT(17)|BIT(16)|BIT(15)|BIT(14), 0x1b<<14);

	reg_set_bit(vin->rstgen_base,		//data_bus16_8
		M31DPHY_APBCFGSAIF__SYSCFG_184,
		BIT(8), 0<<8);

	reg_set_bit(vin->rstgen_base,		//debug_mode_sel
		M31DPHY_APBCFGSAIF__SYSCFG_184,
		BIT(15)|BIT(14)|BIT(13)|BIT(12)|BIT(11)|BIT(10)|BIT(9), 0x5a<<9);

	reg_set_bit(vin->rstgen_base,			//dpdn_swap_clk0
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(0), 0<<0);
	reg_set_bit(vin->rstgen_base,			//dpdn_swap_clk1
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(1), 0<<1);
	reg_set_bit(vin->rstgen_base,			//dpdn_swap_lan0
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(2), 0<<2);
	reg_set_bit(vin->rstgen_base,			//dpdn_swap_lan1
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(3), 0<<3);
	reg_set_bit(vin->rstgen_base,			//dpdn_swap_lan2
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(4), 0<<4);
	reg_set_bit(vin->rstgen_base,			//dpdn_swap_lan3
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(5), 0<<5);

	reg_set_bit(vin->rstgen_base,			//endable lan0
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(8), 1<<8);
	reg_set_bit(vin->rstgen_base,			//endable lan1
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(9), 1<<9);
	reg_set_bit(vin->rstgen_base,			//endable lan2
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(10), 1<<10);
	reg_set_bit(vin->rstgen_base,			//endable lan3
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(11), 1<<11);
	reg_set_bit(vin->rstgen_base,			//gpi_en
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(17)|BIT(16)|BIT(15)|BIT(14)|BIT(13)|BIT(12),
		0<<12);
	reg_set_bit(vin->rstgen_base,			//hs_freq_change_clk0
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(18), 0<<18);
	reg_set_bit(vin->rstgen_base,			//hs_freq_change_clk1
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(19), 0<<19);

	reg_set_bit(vin->rstgen_base,
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(22)|BIT(21)|BIT(20), cfg->clock_lane<<20);          //clock lane 0
	reg_set_bit(vin->rstgen_base,
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(25)|BIT(24)|BIT(23), 5<<23);         //clock lane 1

	reg_set_bit(vin->rstgen_base,
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(28)|BIT(27)|BIT(26), cfg->data_lanes[0]<<26);       //data lane 0
	reg_set_bit(vin->rstgen_base,
		M31DPHY_APBCFGSAIF__SYSCFG_188,
		BIT(31)|BIT(30)|BIT(29), cfg->data_lanes[1]<<29);       //data lane 1
	reg_set_bit(vin->rstgen_base,
		M31DPHY_APBCFGSAIF__SYSCFG_192,
		BIT(2)|BIT(1)|BIT(0), cfg->data_lanes[2]<<0);           //data lane 2
	reg_set_bit(vin->rstgen_base,
		M31DPHY_APBCFGSAIF__SYSCFG_192,
		BIT(5)|BIT(4)|BIT(3), cfg->data_lanes[3]<<3);           //data lane 3

	reg_set_bit(vin->rstgen_base,		//mp_test_en
		M31DPHY_APBCFGSAIF__SYSCFG_192,
		BIT(6), 0<<6);
	reg_set_bit(vin->rstgen_base,		//mp_test_mode_sel
		M31DPHY_APBCFGSAIF__SYSCFG_192,
		BIT(11)|BIT(10)|BIT(9)|BIT(8)|BIT(7), 0<<7);

	reg_set_bit(vin->rstgen_base,		//pll_clk_sel
		M31DPHY_APBCFGSAIF__SYSCFG_192,
		BIT(20)|BIT(19)|BIT(18)|BIT(17)|BIT(16)|BIT(15)|BIT(14)|BIT(13)|BIT(12),
		0x37c<<12);

	reg_set_bit(vin->rstgen_base,		//rx_1c2c_sel
		M31DPHY_APBCFGSAIF__SYSCFG_200,
		BIT(8), 0<<8);

	reg_set_bit(vin->rstgen_base,		//precounter in clk0
		M31DPHY_APBCFGSAIF__SYSCFG_192,
		BIT(29)|BIT(28)|BIT(27)|BIT(26)|BIT(25)|BIT(24)|BIT(23)|BIT(22),
		8<<22);
	reg_set_bit(vin->rstgen_base,		//precounter in clk1
		M31DPHY_APBCFGSAIF__SYSCFG_196,
		BIT(7)|BIT(6)|BIT(5)|BIT(4)|BIT(3)|BIT(2)|BIT(1)|BIT(0),
		8<<0);
	reg_set_bit(vin->rstgen_base,		//precounter in lan0
		M31DPHY_APBCFGSAIF__SYSCFG_196,
		BIT(15)|BIT(14)|BIT(13)|BIT(12)|BIT(11)|BIT(10)|BIT(9)|BIT(8),
		7<<8);
	reg_set_bit(vin->rstgen_base,		//precounter in lan1
		M31DPHY_APBCFGSAIF__SYSCFG_196,
		BIT(23)|BIT(22)|BIT(21)|BIT(20)|BIT(19)|BIT(18)|BIT(17)|BIT(16),
		7<<16);
	reg_set_bit(vin->rstgen_base,		//precounter in lan2
		M31DPHY_APBCFGSAIF__SYSCFG_196,
		BIT(31)|BIT(30)|BIT(29)|BIT(28)|BIT(27)|BIT(26)|BIT(25)|BIT(24),
		7<<24);
	reg_set_bit(vin->rstgen_base,		//precounter in lan3
		M31DPHY_APBCFGSAIF__SYSCFG_200,
		BIT(7)|BIT(6)|BIT(5)|BIT(4)|BIT(3)|BIT(2)|BIT(1)|BIT(0),
		7<<0);

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
