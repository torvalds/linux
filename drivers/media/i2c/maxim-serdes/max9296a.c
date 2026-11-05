// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX9296A Dual GMSL2/3 Deserializer Driver
 *
 * Copyright (C) 2025 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>

#include <media/mipi-csi2.h>

#include "max_des.h"

#define MAX9296A_REG0				0x0

#define MAX9296A_REG1				0x1
#define MAX9296A_REG1_RX_RATE_A			GENMASK(1, 0)
#define MAX9296A_REG1_RX_RATE_3GBPS		0b01
#define MAX9296A_REG1_RX_RATE_6GBPS		0b10
#define MAX9296A_REG1_RX_RATE_12GBPS		0b11

#define MAX9296A_REG2				0x2
#define MAX9296A_REG2_VID_EN(p)			BIT((p) + 4)

#define MAX9296A_REG4				0x4
#define MAX9296A_REG4_GMSL3_X(x)		BIT((x) + 6)
#define MAX9296A_REG4_RX_RATE_B			GENMASK(1, 0)

#define MAX9296A_REG6				0x6
#define MAX9296A_REG6_GMSL2_X(x)		BIT((x) + 6)

#define MAX9296A_CTRL0				0x10
#define MAX9296A_CTRL0_LINK_CFG			GENMASK(1, 0)
#define MAX9296A_CTRL0_AUTO_LINK		BIT(4)
#define MAX9296A_CTRL0_RESET_ONESHOT		BIT(5)
#define MAX9296A_CTRL0_RESET_ALL		BIT(7)

#define MAX9296A_CTRL2				0x12
#define MAX9296A_CTRL2_RESET_ONESHOT_B		BIT(5)

#define MAX9296A_CTRL3				0x13
#define MAX9296A_CTRL3_LOCKED			BIT(3)

#define MAX9296A_MIPI_TX0(x)			(0x28 + (x) * 0x5000)
#define MAX9296A_MIPI_TX0_RX_FEC_EN		BIT(1)

#define MAX9296A_IO_CHK0			0x38
#define MAX9296A_IO_CHK0_PIN_DRV_EN_0		GENMASK(1, 0)
#define MAX9296A_IO_CHK0_PIN_DRV_EN_0_25MHZ	0b00
#define MAX9296A_IO_CHK0_PIN_DRV_EN_0_75MHZ	0b01
#define MAX9296A_IO_CHK0_PIN_DRV_EN_0_USE_PIPE	0b10

#define MAX9296A_RX50(p)			(0x50 + (p))
#define MAX9296A_RX50_STR_SEL			GENMASK(1, 0)

#define MAX9296A_VIDEO_PIPE_EN			0x160
#define MAX9296A_VIDEO_PIPE_EN_MASK(p)		BIT(p)

#define MAX9296A_VIDEO_PIPE_SEL			0x161
#define MAX9296A_VIDEO_PIPE_SEL_STREAM(p)	(GENMASK(1, 0) << ((p) * 3))
#define MAX9296A_VIDEO_PIPE_SEL_LINK(p)		BIT(2 + (p) * 3)

#define MAX9296A_VPRBS(p)			(0x1fc + (p) * 0x20)
#define MAX9296A_VPRBS_VIDEO_LOCK		BIT(0)
#define MAX9296A_VPRBS_PATGEN_CLK_SRC		BIT(7)
#define MAX9296A_VPRBS_PATGEN_CLK_SRC_150MHZ	0b0
#define MAX9296A_VPRBS_PATGEN_CLK_SRC_600MHZ	0b1

#define MAX9296A_PATGEN_0			0x240
#define MAX9296A_PATGEN_0_VTG_MODE		GENMASK(1, 0)
#define MAX9296A_PATGEN_0_VTG_MODE_FREE_RUNNING	0b11
#define MAX9296A_PATGEN_0_DE_INV		BIT(2)
#define MAX9296A_PATGEN_0_HS_INV		BIT(3)
#define MAX9296A_PATGEN_0_VS_INV		BIT(4)
#define MAX9296A_PATGEN_0_GEN_DE		BIT(5)
#define MAX9296A_PATGEN_0_GEN_HS		BIT(6)
#define MAX9296A_PATGEN_0_GEN_VS		BIT(7)

#define MAX9296A_PATGEN_1			0x241
#define MAX9296A_PATGEN_1_PATGEN_MODE		GENMASK(5, 4)
#define MAX9296A_PATGEN_1_PATGEN_MODE_DISABLED	0b00
#define MAX9296A_PATGEN_1_PATGEN_MODE_CHECKER	0b01
#define MAX9296A_PATGEN_1_PATGEN_MODE_GRADIENT	0b10

#define MAX9296A_VS_DLY_2			0x242
#define MAX9296A_VS_HIGH_2			0x245
#define MAX9296A_VS_LOW_2			0x248
#define MAX9296A_V2H_2				0x24b
#define MAX9296A_HS_HIGH_1			0x24e
#define MAX9296A_HS_LOW_1			0x250
#define MAX9296A_HS_CNT_1			0x252
#define MAX9296A_V2D_2				0x254
#define MAX9296A_DE_HIGH_1			0x257
#define MAX9296A_DE_LOW_1			0x259
#define MAX9296A_DE_CNT_1			0x25b
#define MAX9296A_GRAD_INCR			0x25d
#define MAX9296A_CHKR_COLOR_A_L			0x25e
#define MAX9296A_CHKR_COLOR_B_L			0x261
#define MAX9296A_CHKR_RPT_A			0x264
#define MAX9296A_CHKR_RPT_B			0x265
#define MAX9296A_CHKR_ALT			0x266

#define MAX9296A_BACKTOP12			0x313
#define MAX9296A_BACKTOP12_CSI_OUT_EN		BIT(1)

#define MAX9296A_BACKTOP21			0x31c
#define MAX9296A_BACKTOP21_BPP8DBL(p)		BIT(4 + (p))

#define MAX9296A_BACKTOP22(x)			(0x31d + (x) * 0x3)
#define MAX9296A_BACKTOP22_PHY_CSI_TX_DPLL	GENMASK(4, 0)
#define MAX9296A_BACKTOP22_PHY_CSI_TX_DPLL_EN	BIT(5)

#define MAX9296A_BACKTOP24			0x31f
#define MAX9296A_BACKTOP24_BPP8DBL_MODE(p)	BIT(4 + (p))

#define MAX9296A_BACKTOP30			0x325
#define MAX9296A_BACKTOP30_W_FRAME		BIT(7)

#define MAX9296A_BACKTOP32			0x327
#define MAX9296A_BACKTOP32_BPP10DBL(p)		BIT(p)
#define MAX9296A_BACKTOP32_BPP10DBL_MODE(p)	BIT(4 + (p))

#define MAX9296A_BACKTOP33			0x328
#define MAX9296A_BACKTOP32_BPP12DBL(p)		BIT(p)

#define MAX9296A_MIPI_PHY0			0x330
#define MAX9296A_MIPI_PHY0_FORCE_CSI_OUT_EN	BIT(7)

#define MAX9296A_MIPI_PHY2			0x332
#define MAX9296A_MIPI_PHY2_PHY_STDBY_N(x)	(GENMASK(5, 4) << ((x) * 2))

#define MAX9296A_MIPI_PHY3(x)			(0x333 + (x))
#define MAX9296A_MIPI_PHY3_PHY_LANE_MAP_4	GENMASK(7, 0)

#define MAX9296A_MIPI_PHY5(x)			(0x335 + (x))
#define MAX9296A_MIPI_PHY5_PHY_POL_MAP_0_1	GENMASK(1, 0)
#define MAX9296A_MIPI_PHY5_PHY_POL_MAP_2_3	GENMASK(4, 3)
#define MAX9296A_MIPI_PHY5_PHY_POL_MAP_CLK(x)	BIT((x) == 0 ? 5 : 2)

#define MAX9296A_MIPI_PHY18			0x342
#define MAX9296A_MIPI_PHY18_CSI2_TX_PKT_CNT(x)	(GENMASK(3, 0) << (4 * (x)))

#define MAX9296A_MIPI_PHY20(x)			(0x344 + (x))

#define MAX9296A_MIPI_TX3(x)			(0x403 + (x) * 0x40)
#define MAX9296A_MIPI_TX3_DESKEW_INIT_8X32K	FIELD_PREP(GENMASK(2, 0), 0b001)
#define MAX9296A_MIPI_TX3_DESKEW_INIT_AUTO	BIT(7)

#define MAX9296A_MIPI_TX4(x)			(0x404 + (x) * 0x40)
#define MAX9296A_MIPI_TX4_DESKEW_PER_2K		FIELD_PREP(GENMASK(2, 0), 0b001)
#define MAX9296A_MIPI_TX4_DESKEW_PER_AUTO	BIT(7)

#define MAX9296A_MIPI_TX10(x)			(0x40a + (x) * 0x40)
#define MAX9296A_MIPI_TX10_CSI2_LANE_CNT	GENMASK(7, 6)
#define MAX9296A_MIPI_TX10_CSI2_CPHY_EN		BIT(5)

#define MAX9296A_MIPI_TX11(p)			(0x40b + (p) * 0x40)
#define MAX9296A_MIPI_TX12(p)			(0x40c + (p) * 0x40)

#define MAX9296A_MIPI_TX13(p, x)		(0x40d + (p) * 0x40 + (x) * 0x2)
#define MAX9296A_MIPI_TX13_MAP_SRC_DT		GENMASK(5, 0)
#define MAX9296A_MIPI_TX13_MAP_SRC_VC		GENMASK(7, 6)

#define MAX9296A_MIPI_TX14(p, x)		(0x40e + (p) * 0x40 + (x) * 0x2)
#define MAX9296A_MIPI_TX14_MAP_DST_DT		GENMASK(5, 0)
#define MAX9296A_MIPI_TX14_MAP_DST_VC		GENMASK(7, 6)

#define MAX9296A_MIPI_TX45(p, x)		(0x42d + (p) * 0x40 + (x) / 4)
#define MAX9296A_MIPI_TX45_MAP_DPHY_DEST(x)	(GENMASK(1, 0) << (2 * ((x) % 4)))

#define MAX9296A_MIPI_TX51(x)			(0x433 + (x) * 0x40)
#define MAX9296A_MIPI_TX51_ALT_MEM_MAP_12	BIT(0)
#define MAX9296A_MIPI_TX51_ALT_MEM_MAP_8	BIT(1)
#define MAX9296A_MIPI_TX51_ALT_MEM_MAP_10	BIT(2)
#define MAX9296A_MIPI_TX51_ALT2_MEM_MAP_8	BIT(4)

#define MAX9296A_MIPI_TX52(x)			(0x434 +  (x) * 0x40)
#define MAX9296A_MIPI_TX52_TUN_DEST		BIT(1)
#define MAX9296A_MIPI_TX52_TUN_EN		BIT(0)

#define MAX9296A_GMSL1_EN			0xf00
#define MAX9296A_GMSL1_EN_LINK_EN		GENMASK(1, 0)

#define MAX9296A_RLMS3E(x)			(0x143e + (x) * 0x100)
#define MAX9296A_RLMS3F(x)			(0x143f + (x) * 0x100)
#define MAX9296A_RLMS49(x)			(0x1449 + (x) * 0x100)
#define MAX9296A_RLMS7E(x)			(0x147e + (x) * 0x100)
#define MAX9296A_RLMS7F(x)			(0x147f + (x) * 0x100)
#define MAX9296A_RLMSA3(x)			(0x14a3 + (x) * 0x100)
#define MAX9296A_RLMSA5(x)			(0x14a5 + (x) * 0x100)
#define MAX9296A_RLMSD8(x)			(0x14d8 + (x) * 0x100)

#define MAX9296A_DPLL_0(x)			(0x1c00 + (x) * 0x100)
#define MAX9296A_DPLL_0_CONFIG_SOFT_RST_N	BIT(0)

#define MAX9296A_CTRL9				0x5009
#define MAX9296A_CTRL9_LOCKED_B			BIT(3)

#define MAX9296A_PIPES_NUM			4
#define MAX9296A_PHYS_NUM			2

static const struct regmap_config max9296a_i2c_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
};

struct max9296a_priv {
	struct max_des des;
	const struct max9296a_chip_info *info;

	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;

	struct gpio_desc *gpiod_pwdn;
};

struct max9296a_chip_info {
	const struct max_des_info *info;
	const struct max_des_ops *ops;
	const struct reg_sequence *rlms_adjust_sequence;
	unsigned int rlms_adjust_sequence_len;
	unsigned int max_register;
	unsigned int pipe_hw_ids[MAX9296A_PIPES_NUM];
	unsigned int phy_hw_ids[MAX9296A_PHYS_NUM];
	bool has_per_link_lock;
	bool has_per_link_reset;
	bool phy0_lanes_0_1_on_second_phy;
	bool polarity_on_physical_lanes;
	bool supports_cphy;
	bool supports_phy_log;
};

#define des_to_priv(_des) \
	container_of(_des, struct max9296a_priv, des)

static int max9296a_wait_for_device(struct max9296a_priv *priv)
{
	unsigned int val;
	int ret, err;

	err = read_poll_timeout(regmap_read, ret,
				!ret && val,
				100 * USEC_PER_MSEC,
				1 * USEC_PER_SEC, false,
				priv->regmap, MAX9296A_REG0, &val);
	if (err)
		dev_err(priv->dev, "Timeout waiting for deserializer: %d\n", ret);

	return err;
}

static int max9296a_reset(struct max9296a_priv *priv)
{
	int ret;

	ret = max9296a_wait_for_device(priv);
	if (ret)
		return ret;

	ret = regmap_set_bits(priv->regmap, MAX9296A_CTRL0,
			      MAX9296A_CTRL0_RESET_ALL);
	if (ret)
		return ret;

	msleep(100);

	return max9296a_wait_for_device(priv);
}

static unsigned int max9296a_pipe_id(struct max9296a_priv *priv,
				     struct max_des_pipe *pipe)
{
	return priv->info->pipe_hw_ids[pipe->index];
}

static unsigned int max9296a_phy_id(struct max9296a_priv *priv,
				    struct max_des_phy *phy)
{
	return priv->info->phy_hw_ids[phy->index];
}

static int __maybe_unused max9296a_reg_read(struct max_des *des, unsigned int reg,
					    unsigned int *val)
{
	struct max9296a_priv *priv = des_to_priv(des);

	return regmap_read(priv->regmap, reg, val);
}

static int __maybe_unused max9296a_reg_write(struct max_des *des, unsigned int reg,
					     unsigned int val)
{
	struct max9296a_priv *priv = des_to_priv(des);

	return regmap_write(priv->regmap, reg, val);
}

static int max9296a_log_pipe_status(struct max_des *des,
				    struct max_des_pipe *pipe)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = max9296a_pipe_id(priv, pipe);
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, MAX9296A_VPRBS(index), &val);
	if (ret)
		return ret;

	dev_info(priv->dev, "\tvideo_lock: %u\n",
		 !!(val & MAX9296A_VPRBS_VIDEO_LOCK));

	return 0;
}

static int max9296a_log_phy_status(struct max_des *des,
				   struct max_des_phy *phy)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = phy->index;
	unsigned int val;
	int ret;

	if (!priv->info->supports_phy_log)
		return 0;

	ret = regmap_read(priv->regmap, MAX9296A_MIPI_PHY18, &val);
	if (ret)
		return ret;

	dev_info(priv->dev, "\tcsi2_pkt_cnt: %lu\n",
		 field_get(MAX9296A_MIPI_PHY18_CSI2_TX_PKT_CNT(index), val));

	ret = regmap_read(priv->regmap, MAX9296A_MIPI_PHY20(index), &val);
	if (ret)
		return ret;

	dev_info(priv->dev, "\tphy_pkt_cnt: %u\n", val);

	return 0;
}

static int max9296a_set_enable(struct max_des *des, bool enable)
{
	struct max9296a_priv *priv = des_to_priv(des);

	return regmap_assign_bits(priv->regmap, MAX9296A_BACKTOP12,
				  MAX9296A_BACKTOP12_CSI_OUT_EN, enable);
}

static int max9296a_init_tpg(struct max_des *des)
{
	const struct reg_sequence regs[] = {
		{ MAX9296A_GRAD_INCR, MAX_SERDES_GRAD_INCR },
		REG_SEQUENCE_3_LE(MAX9296A_CHKR_COLOR_A_L,
				  MAX_SERDES_CHECKER_COLOR_A),
		REG_SEQUENCE_3_LE(MAX9296A_CHKR_COLOR_B_L,
				  MAX_SERDES_CHECKER_COLOR_B),
		{ MAX9296A_CHKR_RPT_A, MAX_SERDES_CHECKER_SIZE },
		{ MAX9296A_CHKR_RPT_B, MAX_SERDES_CHECKER_SIZE },
		{ MAX9296A_CHKR_ALT, MAX_SERDES_CHECKER_SIZE },
	};
	struct max9296a_priv *priv = des_to_priv(des);

	return regmap_multi_reg_write(priv->regmap, regs, ARRAY_SIZE(regs));
}

static int max9296a_init(struct max_des *des)
{
	struct max9296a_priv *priv = des_to_priv(des);
	int ret;

	if (priv->info->rlms_adjust_sequence) {
		ret = regmap_multi_reg_write(priv->regmap,
					     priv->info->rlms_adjust_sequence,
					     priv->info->rlms_adjust_sequence_len);
		if (ret)
			return ret;
	}

	/*
	 * Hold off MIPI packet requests until a frame start is seen.
	 * Assures the first frame out of the line buffer is whole.
	 */
	ret = regmap_set_bits(priv->regmap, MAX9296A_BACKTOP30,
			      MAX9296A_BACKTOP30_W_FRAME);
	if (ret)
		return ret;

	return max9296a_init_tpg(des);
}

static int max9296a_init_phy(struct max_des *des, struct max_des_phy *phy)
{
	struct max9296a_priv *priv = des_to_priv(des);
	bool is_cphy = phy->bus_type == V4L2_MBUS_CSI2_CPHY;
	unsigned int num_data_lanes = phy->mipi.num_data_lanes;
	unsigned int dpll_freq = is_cphy ? phy->link_frequency
					 : phy->link_frequency * 2;
	unsigned int num_hw_data_lanes;
	unsigned int hw_index = max9296a_phy_id(priv, phy);
	unsigned int index = phy->index;
	unsigned int used_data_lanes = 0;
	unsigned int val;
	unsigned int i;
	int ret;

	if (is_cphy && !priv->info->supports_cphy) {
		dev_err(priv->dev, "CPHY not supported\n");
		return -EINVAL;
	}

	num_hw_data_lanes = max_des_phy_hw_data_lanes(des, phy);

	/*
	 * MAX9296A has four PHYs, but does not support single-PHY configurations,
	 * only double-PHY configurations, even when only using two lanes.
	 * For PHY 0 + PHY 1, PHY 1 is the master PHY.
	 * For PHY 2 + PHY 3, PHY 2 is the master PHY.
	 * Clock is always on the master PHY.
	 * For first pair of PHYs, first lanes are on the master PHY.
	 * For second pair of PHYs, first lanes are on the master PHY too.
	 *
	 * PHY 0 + 1
	 * CLK = PHY 1
	 * PHY1 Lane 0 = D0
	 * PHY1 Lane 1 = D1
	 * PHY0 Lane 0 = D2
	 * PHY0 Lane 1 = D3
	 *
	 * PHY 2 + 3
	 * CLK = PHY 2
	 * PHY2 Lane 0 = D0
	 * PHY2 Lane 1 = D1
	 * PHY3 Lane 0 = D2
	 * PHY3 Lane 1 = D3
	 *
	 * MAX96714 only has two PHYs which cannot support single-PHY configurations.
	 * Clock is always on the master PHY, first lanes are on PHY 0, even if
	 * PHY 1 is the master PHY.
	 *
	 * PHY 0 + 1
	 * CLK = PHY 1
	 * PHY0 Lane 0 = D0
	 * PHY0 Lane 1 = D1
	 * PHY1 Lane 0 = D2
	 * PHY1 Lane 1 = D3
	 */

	/* Configure a lane count. */
	ret = regmap_update_bits(priv->regmap, MAX9296A_MIPI_TX10(hw_index),
				 MAX9296A_MIPI_TX10_CSI2_LANE_CNT,
				 FIELD_PREP(MAX9296A_MIPI_TX10_CSI2_LANE_CNT,
					    num_data_lanes - 1));
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX9296A_MIPI_TX10(hw_index),
				 MAX9296A_MIPI_TX10_CSI2_CPHY_EN, is_cphy);
	if (ret)
		return ret;

	/* Configure lane mapping. */
	/*
	 * The lane of each PHY can be mapped to physical lanes 0, 1, 2, and 3.
	 * This mapping is exclusive, multiple lanes, even if unused cannot be
	 * mapped to the same physical lane.
	 * Each lane mapping is represented as two bits.
	 */
	val = 0;
	for (i = 0; i < num_hw_data_lanes ; i++) {
		unsigned int map;

		if (i < num_data_lanes)
			map = phy->mipi.data_lanes[i] - 1;
		else
			map = ffz(used_data_lanes);

		val |= map << (i * 2);
		used_data_lanes |= BIT(map);
	}

	if (phy->index == 0 && priv->info->phy0_lanes_0_1_on_second_phy)
		val = ((val & 0xf) << 4) | ((val >> 4) & 0xf);

	ret = regmap_update_bits(priv->regmap, MAX9296A_MIPI_PHY3(index),
				 MAX9296A_MIPI_PHY3_PHY_LANE_MAP_4,
				 FIELD_PREP(MAX9296A_MIPI_PHY3_PHY_LANE_MAP_4, val));
	if (ret)
		return ret;

	/*
	 * Configure lane polarity.
	 *
	 * PHY 0 and 1 are on register 0x335.
	 * PHY 2 and 3 are on register 0x336.
	 *
	 * Each PHY has 3 bits of polarity configuration.
	 *
	 * On MAX9296A, each bit represents the lane polarity of logical lanes.
	 * Each of these lanes can be mapped to any physical lane.
	 * 0th bit is for lane 0.
	 * 1st bit is for lane 1.
	 * 2nd bit is for clock lane.
	 *
	 * On MAX96714, each bit represents the lane polarity of physical lanes.
	 * 0th bit for physical lane 0.
	 * 1st bit for physical lane 1.
	 * 2nd bit for clock lane of PHY 0, the slave PHY, which is unused.
	 *
	 * 3rd bit for physical lane 2.
	 * 4th bit for physical lane 3.
	 * 5th bit for clock lane of PHY 1, the master PHY.
	 */

	for (i = 0, val = 0; i < num_data_lanes; i++) {
		unsigned int map;

		if (!phy->mipi.lane_polarities[i + 1])
			continue;

		/*
		 * The numbers inside the data_lanes array specify the hardware
		 * lane each logical lane maps to.
		 * If polarity is set for the physical lanes, retrieve the
		 * physical lane matching the logical lane from data_lanes.
		 * Otherwise, when polarity is set for the logical lanes
		 * the index of the polarity can be used.
		 */

		if (priv->info->polarity_on_physical_lanes)
			map = phy->mipi.data_lanes[i] - 1;
		else
			map = i;

		val |= BIT(map);
	}

	if (phy->index == 0 && priv->info->phy0_lanes_0_1_on_second_phy)
		val = ((val & 0x3) << 2) | ((val >> 2) & 0x3);

	ret = regmap_update_bits(priv->regmap, MAX9296A_MIPI_PHY5(index),
				 MAX9296A_MIPI_PHY5_PHY_POL_MAP_0_1 |
				 MAX9296A_MIPI_PHY5_PHY_POL_MAP_2_3,
				 FIELD_PREP(MAX9296A_MIPI_PHY5_PHY_POL_MAP_0_1, val) |
				 FIELD_PREP(MAX9296A_MIPI_PHY5_PHY_POL_MAP_2_3, val >> 2));
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX9296A_MIPI_PHY5(index),
				 MAX9296A_MIPI_PHY5_PHY_POL_MAP_CLK(index),
				 phy->mipi.lane_polarities[0]);
	if (ret)
		return ret;

	/* Put DPLL block into reset. */
	ret = regmap_clear_bits(priv->regmap, MAX9296A_DPLL_0(hw_index),
				MAX9296A_DPLL_0_CONFIG_SOFT_RST_N);
	if (ret)
		return ret;

	/* Set DPLL frequency. */
	ret = regmap_update_bits(priv->regmap, MAX9296A_BACKTOP22(hw_index),
				 MAX9296A_BACKTOP22_PHY_CSI_TX_DPLL,
				 FIELD_PREP(MAX9296A_BACKTOP22_PHY_CSI_TX_DPLL,
					    div_u64(dpll_freq, 100000000)));
	if (ret)
		return ret;

	/* Enable DPLL frequency. */
	ret = regmap_set_bits(priv->regmap, MAX9296A_BACKTOP22(hw_index),
			      MAX9296A_BACKTOP22_PHY_CSI_TX_DPLL_EN);
	if (ret)
		return ret;

	/* Pull DPLL block out of reset. */
	ret = regmap_set_bits(priv->regmap, MAX9296A_DPLL_0(hw_index),
			      MAX9296A_DPLL_0_CONFIG_SOFT_RST_N);
	if (ret)
		return ret;

	if (!is_cphy && dpll_freq > 1500000000ull) {
		/* Enable initial deskew with 2 x 32k UI. */
		ret = regmap_write(priv->regmap, MAX9296A_MIPI_TX3(hw_index),
				   MAX9296A_MIPI_TX3_DESKEW_INIT_AUTO |
				   MAX9296A_MIPI_TX3_DESKEW_INIT_8X32K);
		if (ret)
			return ret;

		/* Enable periodic deskew with 2 x 1k UI.. */
		ret = regmap_write(priv->regmap, MAX9296A_MIPI_TX4(hw_index),
				   MAX9296A_MIPI_TX4_DESKEW_PER_AUTO |
				   MAX9296A_MIPI_TX4_DESKEW_PER_2K);
		if (ret)
			return ret;
	} else {
		/* Disable initial deskew. */
		ret = regmap_write(priv->regmap, MAX9296A_MIPI_TX3(hw_index), 0x0);
		if (ret)
			return ret;

		/* Disable periodic deskew. */
		ret = regmap_write(priv->regmap, MAX9296A_MIPI_TX4(hw_index), 0x0);
		if (ret)
			return ret;
	}

	return 0;
}

static int max9296a_set_phy_mode(struct max_des *des, struct max_des_phy *phy,
				 struct max_des_phy_mode *mode)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int phy_id = max9296a_phy_id(priv, phy);
	int ret;

	/* Set alternate memory map modes. */
	ret = regmap_assign_bits(priv->regmap, MAX9296A_MIPI_TX51(phy_id),
				 MAX9296A_MIPI_TX51_ALT_MEM_MAP_12,
				 mode->alt_mem_map12);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX9296A_MIPI_TX51(phy_id),
				 MAX9296A_MIPI_TX51_ALT_MEM_MAP_8,
				 mode->alt_mem_map8);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX9296A_MIPI_TX51(phy_id),
				 MAX9296A_MIPI_TX51_ALT_MEM_MAP_10,
				 mode->alt_mem_map10);
	if (ret)
		return ret;

	return regmap_assign_bits(priv->regmap, MAX9296A_MIPI_TX51(phy_id),
				  MAX9296A_MIPI_TX51_ALT2_MEM_MAP_8,
				  mode->alt2_mem_map8);
}

static int max9296a_set_phy_enable(struct max_des *des, struct max_des_phy *phy,
				   bool enable)
{
	struct max9296a_priv *priv = des_to_priv(des);

	return regmap_assign_bits(priv->regmap, MAX9296A_MIPI_PHY2,
				  MAX9296A_MIPI_PHY2_PHY_STDBY_N(phy->index), enable);
}

static int max9296a_set_pipe_remap(struct max_des *des,
				   struct max_des_pipe *pipe,
				   unsigned int i,
				   struct max_des_remap *remap)
{
	struct max9296a_priv *priv = des_to_priv(des);
	struct max_des_phy *phy = &des->phys[remap->phy];
	unsigned int phy_id = max9296a_phy_id(priv, phy);
	unsigned int index = max9296a_pipe_id(priv, pipe);
	int ret;

	/* Set source Data Type and Virtual Channel. */
	/* TODO: implement extended Virtual Channel. */
	ret = regmap_write(priv->regmap, MAX9296A_MIPI_TX13(index, i),
			   FIELD_PREP(MAX9296A_MIPI_TX13_MAP_SRC_DT,
				      remap->from_dt) |
			   FIELD_PREP(MAX9296A_MIPI_TX13_MAP_SRC_VC,
				      remap->from_vc));
	if (ret)
		return ret;

	/* Set destination Data Type and Virtual Channel. */
	/* TODO: implement extended Virtual Channel. */
	ret = regmap_write(priv->regmap, MAX9296A_MIPI_TX14(index, i),
			   FIELD_PREP(MAX9296A_MIPI_TX14_MAP_DST_DT,
				      remap->to_dt) |
			   FIELD_PREP(MAX9296A_MIPI_TX14_MAP_DST_VC,
				      remap->to_vc));
	if (ret)
		return ret;

	/* Set destination PHY. */
	return regmap_update_bits(priv->regmap, MAX9296A_MIPI_TX45(index, i),
				  MAX9296A_MIPI_TX45_MAP_DPHY_DEST(i),
				  field_prep(MAX9296A_MIPI_TX45_MAP_DPHY_DEST(i),
					     phy_id));
}

static int max9296a_set_pipe_remaps_enable(struct max_des *des,
					   struct max_des_pipe *pipe,
					   unsigned int mask)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = max9296a_pipe_id(priv, pipe);
	int ret;

	ret = regmap_write(priv->regmap, MAX9296A_MIPI_TX11(index), mask);
	if (ret)
		return ret;

	return regmap_write(priv->regmap, MAX9296A_MIPI_TX12(index), mask >> 8);
}

static int max9296a_set_pipe_enable(struct max_des *des, struct max_des_pipe *pipe,
				    bool enable)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = max9296a_pipe_id(priv, pipe);

	return regmap_assign_bits(priv->regmap, MAX9296A_REG2,
				  MAX9296A_REG2_VID_EN(index), enable);
}

static int max96714_set_pipe_enable(struct max_des *des, struct max_des_pipe *pipe,
				    bool enable)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = max9296a_pipe_id(priv, pipe);

	return regmap_assign_bits(priv->regmap, MAX9296A_VIDEO_PIPE_EN,
				  MAX9296A_VIDEO_PIPE_EN_MASK(index - 1), enable);
}

static int max96714_set_pipe_tunnel_enable(struct max_des *des,
					   struct max_des_pipe *pipe, bool enable)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = max9296a_pipe_id(priv, pipe);

	return regmap_assign_bits(priv->regmap, MAX9296A_MIPI_TX52(index),
				  MAX9296A_MIPI_TX52_TUN_EN, enable);
}

static int max9296a_set_pipe_stream_id(struct max_des *des, struct max_des_pipe *pipe,
				       unsigned int stream_id)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = max9296a_pipe_id(priv, pipe);

	return regmap_update_bits(priv->regmap, MAX9296A_RX50(index), MAX9296A_RX50_STR_SEL,
				  FIELD_PREP(MAX9296A_RX50_STR_SEL, stream_id));
}

static int max96714_set_pipe_stream_id(struct max_des *des, struct max_des_pipe *pipe,
				       unsigned int stream_id)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = pipe->index;

	return regmap_update_bits(priv->regmap, MAX9296A_VIDEO_PIPE_SEL,
				  MAX9296A_VIDEO_PIPE_SEL_STREAM(index),
				  field_prep(MAX9296A_VIDEO_PIPE_SEL_STREAM(index),
					     stream_id));
}

static int max96716a_set_pipe_link(struct max_des *des, struct max_des_pipe *pipe,
				   struct max_des_link *link)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = pipe->index;

	return regmap_update_bits(priv->regmap, MAX9296A_VIDEO_PIPE_SEL,
				  MAX9296A_VIDEO_PIPE_SEL_LINK(index),
				  field_prep(MAX9296A_VIDEO_PIPE_SEL_LINK(index),
					     link->index));
}

static int max96716a_set_pipe_tunnel_phy(struct max_des *des,
					 struct max_des_pipe *pipe,
					 struct max_des_phy *phy)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = max9296a_pipe_id(priv, pipe);

	return regmap_assign_bits(priv->regmap, MAX9296A_MIPI_TX52(index),
				  MAX9296A_MIPI_TX52_TUN_DEST, phy->index);
}

static int max9296a_set_pipe_mode(struct max_des *des,
				  struct max_des_pipe *pipe,
				  struct max_des_pipe_mode *mode)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = max9296a_pipe_id(priv, pipe);
	int ret;

	/* Set 8bit double mode. */
	ret = regmap_assign_bits(priv->regmap, MAX9296A_BACKTOP21,
				 MAX9296A_BACKTOP21_BPP8DBL(index), mode->dbl8);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX9296A_BACKTOP24,
				 MAX9296A_BACKTOP24_BPP8DBL_MODE(index),
				 mode->dbl8mode);
	if (ret)
		return ret;

	/* Set 10bit double mode. */
	ret = regmap_assign_bits(priv->regmap, MAX9296A_BACKTOP32,
				 MAX9296A_BACKTOP32_BPP10DBL(index), mode->dbl10);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX9296A_BACKTOP32,
				 MAX9296A_BACKTOP32_BPP10DBL_MODE(index),
				 mode->dbl10mode);
	if (ret)
		return ret;

	/* Set 12bit double mode. */
	/* TODO: check support for double mode on MAX96714. */
	return regmap_assign_bits(priv->regmap, MAX9296A_BACKTOP33,
				  MAX9296A_BACKTOP32_BPP12DBL(index), mode->dbl12);
}

static int max9296a_reset_link(struct max9296a_priv *priv, unsigned int index)
{
	unsigned int reg, mask;

	if (index == 0) {
		reg = MAX9296A_CTRL0;
		mask = MAX9296A_CTRL0_RESET_ONESHOT;
	} else {
		reg = MAX9296A_CTRL2;
		mask = MAX9296A_CTRL2_RESET_ONESHOT_B;
	}

	return regmap_set_bits(priv->regmap, reg, mask);
}

static int max9296a_read_link_locked(struct max9296a_priv *priv,
				     unsigned int index, bool *locked)
{
	unsigned int reg, mask, val;
	int ret;

	if (index == 0) {
		reg = MAX9296A_CTRL3;
		mask = MAX9296A_CTRL3_LOCKED;
	} else {
		reg = MAX9296A_CTRL9;
		mask = MAX9296A_CTRL9_LOCKED_B;
	}

	ret = regmap_read(priv->regmap, reg, &val);
	if (ret)
		return ret;

	*locked = val & mask;

	return 0;
}

static int max9296a_wait_for_link_lock(struct max9296a_priv *priv,
				       unsigned int mask)
{
	unsigned int i;
	bool locked;
	int ret, err;

	if (!priv->info->has_per_link_lock) {
		err = read_poll_timeout(max9296a_read_link_locked, ret,
					ret || locked,
					10 * USEC_PER_MSEC,
					200 * USEC_PER_MSEC, true,
					priv, 0, &locked);
		if (ret)
			return ret;

		if (err)
			dev_dbg(priv->dev, "Timeout waiting for link lock\n");

		return err;
	}

	for (i = 0; i < priv->des.info->num_links; i++) {
		if (!(mask & BIT(i)))
			continue;

		err = read_poll_timeout(max9296a_read_link_locked, ret,
					ret || locked,
					10 * USEC_PER_MSEC,
					200 * USEC_PER_MSEC, true,
					priv, i, &locked);
		if (ret)
			return ret;

		if (err) {
			dev_dbg(priv->dev,
				"Timeout waiting for link %u lock\n", i);

			return err;
		}
	}

	return 0;
}

static int max9296a_select_links(struct max_des *des, unsigned int mask)
{
	struct max9296a_priv *priv = des_to_priv(des);
	int ret;

	if (des->info->num_links == 1) {
		ret = max9296a_reset_link(priv, 0);
		if (ret)
			return ret;

		return max9296a_wait_for_link_lock(priv, BIT(0));
	}

	if (!mask) {
		dev_err(priv->dev, "Disable all links unsupported\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap, MAX9296A_GMSL1_EN,
				 MAX9296A_GMSL1_EN_LINK_EN,
				 FIELD_PREP(MAX9296A_GMSL1_EN_LINK_EN, mask));
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, MAX9296A_CTRL0,
				 MAX9296A_CTRL0_AUTO_LINK |
				 MAX9296A_CTRL0_LINK_CFG |
				 MAX9296A_CTRL0_RESET_ONESHOT,
				 FIELD_PREP(MAX9296A_CTRL0_LINK_CFG, mask) |
				 FIELD_PREP(MAX9296A_CTRL0_RESET_ONESHOT, 1));
	if (ret)
		return ret;

	if (priv->info->has_per_link_reset) {
		ret = max9296a_reset_link(priv, 1);
		if (ret)
			return ret;
	}

	return max9296a_wait_for_link_lock(priv, mask);
}

static int max9296a_set_link_version(struct max_des *des,
				     struct max_des_link *link,
				     enum max_serdes_gmsl_version version)
{
	struct max9296a_priv *priv = des_to_priv(des);
	unsigned int index = link->index;
	bool gmsl3_en = version == MAX_SERDES_GMSL_3_12GBPS;
	unsigned int reg, mask, val;
	int ret;

	if (des->info->needs_single_link_version)
		index = 0;

	if (index == 0) {
		reg = MAX9296A_REG1;
		mask = MAX9296A_REG1_RX_RATE_A;
	} else {
		reg = MAX9296A_REG4;
		mask = MAX9296A_REG4_RX_RATE_B;
	}

	if (version == MAX_SERDES_GMSL_3_12GBPS)
		val = MAX9296A_REG1_RX_RATE_12GBPS;
	else if (version == MAX_SERDES_GMSL_2_6GBPS)
		val = MAX9296A_REG1_RX_RATE_6GBPS;
	else
		val = MAX9296A_REG1_RX_RATE_3GBPS;

	ret = regmap_update_bits(priv->regmap, reg, mask, field_prep(mask, val));
	if (ret)
		return ret;

	if (!(des->info->versions & BIT(MAX_SERDES_GMSL_3_12GBPS)))
		return 0;

	ret = regmap_assign_bits(priv->regmap, MAX9296A_MIPI_TX0(index),
				 MAX9296A_MIPI_TX0_RX_FEC_EN, gmsl3_en);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX9296A_REG6,
				 MAX9296A_REG6_GMSL2_X(index), !gmsl3_en);
	if (ret)
		return ret;

	return regmap_assign_bits(priv->regmap, MAX9296A_REG4,
				  MAX9296A_REG4_GMSL3_X(index), gmsl3_en);
}

static int max9296a_set_tpg_timings(struct max9296a_priv *priv,
				    const struct max_serdes_tpg_timings *tm)
{
	const struct reg_sequence regs[] = {
		REG_SEQUENCE_3(MAX9296A_VS_DLY_2, tm->vs_dly),
		REG_SEQUENCE_3(MAX9296A_VS_HIGH_2, tm->vs_high),
		REG_SEQUENCE_3(MAX9296A_VS_LOW_2, tm->vs_low),
		REG_SEQUENCE_3(MAX9296A_V2H_2, tm->v2h),
		REG_SEQUENCE_2(MAX9296A_HS_HIGH_1, tm->hs_high),
		REG_SEQUENCE_2(MAX9296A_HS_LOW_1, tm->hs_low),
		REG_SEQUENCE_2(MAX9296A_HS_CNT_1, tm->hs_cnt),
		REG_SEQUENCE_3(MAX9296A_V2D_2, tm->v2d),
		REG_SEQUENCE_2(MAX9296A_DE_HIGH_1, tm->de_high),
		REG_SEQUENCE_2(MAX9296A_DE_LOW_1, tm->de_low),
		REG_SEQUENCE_2(MAX9296A_DE_CNT_1, tm->de_cnt),
	};
	int ret;

	ret = regmap_multi_reg_write(priv->regmap, regs, ARRAY_SIZE(regs));
	if (ret)
		return ret;

	return regmap_write(priv->regmap, MAX9296A_PATGEN_0,
			    FIELD_PREP(MAX9296A_PATGEN_0_VTG_MODE,
				       MAX9296A_PATGEN_0_VTG_MODE_FREE_RUNNING) |
			    FIELD_PREP(MAX9296A_PATGEN_0_DE_INV, tm->de_inv) |
			    FIELD_PREP(MAX9296A_PATGEN_0_HS_INV, tm->hs_inv) |
			    FIELD_PREP(MAX9296A_PATGEN_0_VS_INV, tm->vs_inv) |
			    FIELD_PREP(MAX9296A_PATGEN_0_GEN_DE, tm->gen_de) |
			    FIELD_PREP(MAX9296A_PATGEN_0_GEN_HS, tm->gen_hs) |
			    FIELD_PREP(MAX9296A_PATGEN_0_GEN_VS, tm->gen_vs));
}

static int max9296a_set_tpg_clk(struct max9296a_priv *priv, u32 clock)
{
	bool patgen_clk_src = 0;
	unsigned int index;
	u8 pin_drv_en;
	int ret;

	switch (clock) {
	case 25000000:
		pin_drv_en = MAX9296A_IO_CHK0_PIN_DRV_EN_0_25MHZ;
		break;
	case 75000000:
		pin_drv_en = MAX9296A_IO_CHK0_PIN_DRV_EN_0_75MHZ;
		break;
	case 150000000:
		pin_drv_en = MAX9296A_IO_CHK0_PIN_DRV_EN_0_USE_PIPE;
		patgen_clk_src = MAX9296A_VPRBS_PATGEN_CLK_SRC_150MHZ;
		break;
	case 600000000:
		pin_drv_en = MAX9296A_IO_CHK0_PIN_DRV_EN_0_USE_PIPE;
		patgen_clk_src = MAX9296A_VPRBS_PATGEN_CLK_SRC_600MHZ;
		break;
	case 0:
		return 0;
	default:
		return -EINVAL;
	}

	/* TPG data is always injected on link 0, routed to the first pipe. */
	index = max9296a_pipe_id(priv, &priv->des.pipes[0]);

	ret = regmap_update_bits(priv->regmap, MAX9296A_VPRBS(index),
				 MAX9296A_VPRBS_PATGEN_CLK_SRC,
				 FIELD_PREP(MAX9296A_VPRBS_PATGEN_CLK_SRC,
					    patgen_clk_src));
	if (ret)
		return ret;

	return regmap_update_bits(priv->regmap, MAX9296A_IO_CHK0,
				  MAX9296A_IO_CHK0_PIN_DRV_EN_0,
				  FIELD_PREP(MAX9296A_IO_CHK0_PIN_DRV_EN_0,
					     pin_drv_en));
}

static int max9296a_set_tpg_mode(struct max9296a_priv *priv, bool enable)
{
	unsigned int patgen_mode;

	switch (priv->des.tpg_pattern) {
	case MAX_SERDES_TPG_PATTERN_GRADIENT:
		patgen_mode = MAX9296A_PATGEN_1_PATGEN_MODE_GRADIENT;
		break;
	case MAX_SERDES_TPG_PATTERN_CHECKERBOARD:
		patgen_mode = MAX9296A_PATGEN_1_PATGEN_MODE_CHECKER;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(priv->regmap, MAX9296A_PATGEN_1,
				  MAX9296A_PATGEN_1_PATGEN_MODE,
				  FIELD_PREP(MAX9296A_PATGEN_1_PATGEN_MODE,
					     enable ? patgen_mode
						    : MAX9296A_PATGEN_1_PATGEN_MODE_DISABLED));
}

static int max9296a_set_tpg(struct max_des *des,
			    const struct max_serdes_tpg_entry *entry)
{
	struct max9296a_priv *priv = des_to_priv(des);
	struct max_serdes_tpg_timings timings = { 0 };
	int ret;

	ret = max_serdes_get_tpg_timings(entry, &timings);
	if (ret)
		return ret;

	ret = max9296a_set_tpg_timings(priv, &timings);
	if (ret)
		return ret;

	ret = max9296a_set_tpg_clk(priv, timings.clock);
	if (ret)
		return ret;

	ret = max9296a_set_tpg_mode(priv, entry);
	if (ret)
		return ret;

	return regmap_assign_bits(priv->regmap, MAX9296A_MIPI_PHY0,
				  MAX9296A_MIPI_PHY0_FORCE_CSI_OUT_EN, !!entry);
}

static const struct max_serdes_tpg_entry max9296a_tpg_entries[] = {
	MAX_TPG_ENTRY_640X480P60_RGB888,
	MAX_TPG_ENTRY_1920X1080P30_RGB888,
	MAX_TPG_ENTRY_1920X1080P60_RGB888,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
#define MAX9296A_DEBUG_OPS \
	.reg_read = max9296a_reg_read, \
	.reg_write = max9296a_reg_write,
#else
#define MAX9296A_DEBUG_OPS
#endif

#define MAX9296A_COMMON_INFO \
	.num_remaps_per_pipe = 16, \
	.tpg_entries = { \
		.num_entries = ARRAY_SIZE(max9296a_tpg_entries), \
		.entries = max9296a_tpg_entries, \
	}, \
	.tpg_patterns = BIT(MAX_SERDES_TPG_PATTERN_CHECKERBOARD) | \
			BIT(MAX_SERDES_TPG_PATTERN_GRADIENT)

#define MAX9296A_COMMON_OPS \
	MAX9296A_DEBUG_OPS \
	.log_pipe_status = max9296a_log_pipe_status, \
	.log_phy_status = max9296a_log_phy_status, \
	.set_enable = max9296a_set_enable, \
	.init = max9296a_init, \
	.init_phy = max9296a_init_phy, \
	.set_phy_mode = max9296a_set_phy_mode, \
	.set_phy_enable = max9296a_set_phy_enable, \
	.set_pipe_remap = max9296a_set_pipe_remap, \
	.set_pipe_remaps_enable = max9296a_set_pipe_remaps_enable, \
	.set_pipe_mode = max9296a_set_pipe_mode, \
	.set_tpg = max9296a_set_tpg, \
	.select_links = max9296a_select_links, \
	.set_link_version = max9296a_set_link_version

static void max9296a_power_off(void *data)
{
	struct max9296a_priv *priv = data;

	gpiod_set_value_cansleep(priv->gpiod_pwdn, 1);
}

static int max9296a_probe(struct i2c_client *client)
{
	struct regmap_config i2c_regmap = max9296a_i2c_regmap;
	struct device *dev = &client->dev;
	struct max9296a_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = device_get_match_data(dev);
	if (!priv->info) {
		dev_err(dev, "Failed to get match data\n");
		return -ENODEV;
	}

	priv->dev = dev;
	priv->client = client;
	i2c_set_clientdata(client, priv);

	i2c_regmap.max_register = priv->info->max_register;
	priv->regmap = devm_regmap_init_i2c(client, &i2c_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->gpiod_pwdn = devm_gpiod_get_optional(&client->dev, "powerdown",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwdn))
		return PTR_ERR(priv->gpiod_pwdn);

	if (priv->gpiod_pwdn) {
		/* PWDN must be held for 1us for reset */
		udelay(1);

		gpiod_set_value_cansleep(priv->gpiod_pwdn, 0);
		/* Maximum power-up time (tLOCK) 4ms */
		usleep_range(4000, 5000);

		ret = devm_add_action_or_reset(dev, max9296a_power_off,
					       priv);
		if (ret)
			return ret;
	}

	priv->des.info = priv->info->info;
	priv->des.ops = priv->info->ops;

	ret = max9296a_reset(priv);
	if (ret)
		return ret;

	return max_des_probe(client, &priv->des);
}

static void max9296a_remove(struct i2c_client *client)
{
	struct max9296a_priv *priv = i2c_get_clientdata(client);

	max_des_remove(&priv->des);
}

static const struct max_serdes_phys_config max9296a_phys_configs[] = {
	{ { 4, 4 } },
};

static const struct max_serdes_phys_config max96714_phys_configs[] = {
	{ { 4 } },
};

static const struct max_des_info max9296a_des_info = {
	MAX9296A_COMMON_INFO,
	.versions = BIT(MAX_SERDES_GMSL_2_3GBPS) |
		    BIT(MAX_SERDES_GMSL_2_6GBPS),
	.modes = BIT(MAX_SERDES_GMSL_PIXEL_MODE),
	.needs_single_link_version = true,
	.needs_unique_stream_id = true,
	.phys_configs = {
		.num_configs = ARRAY_SIZE(max9296a_phys_configs),
		.configs = max9296a_phys_configs,
	},
	.fix_tx_ids = true,
	.use_atr = true,
	.num_pipes = 4,
	.num_phys = 2,
	.num_links = 2,
};

static const struct max_des_ops max9296a_ops = {
	MAX9296A_COMMON_OPS,
	.set_pipe_stream_id = max9296a_set_pipe_stream_id,
	.set_pipe_enable = max9296a_set_pipe_enable,
};

static const struct max9296a_chip_info max9296a_info = {
	.info = &max9296a_des_info,
	.ops = &max9296a_ops,
	.max_register = 0x1f00,
	.phy0_lanes_0_1_on_second_phy = true,
	.pipe_hw_ids = { 0, 1, 2, 3 },
	.phy_hw_ids = { 1, 2 },
};

static const struct max_des_info max96714_des_info = {
	MAX9296A_COMMON_INFO,
	.versions = BIT(MAX_SERDES_GMSL_2_3GBPS) |
		    BIT(MAX_SERDES_GMSL_2_6GBPS),
	.modes = BIT(MAX_SERDES_GMSL_PIXEL_MODE) |
		 BIT(MAX_SERDES_GMSL_TUNNEL_MODE),
	.phys_configs = {
		.num_configs = ARRAY_SIZE(max96714_phys_configs),
		.configs = max96714_phys_configs,
	},
	.tpg_mode = MAX_SERDES_GMSL_PIXEL_MODE,
	.num_pipes = 1,
	.num_phys = 1,
	.num_links = 1,
};

static const struct max_des_ops max96714_ops = {
	MAX9296A_COMMON_OPS,
	.set_pipe_stream_id = max96714_set_pipe_stream_id,
	.set_pipe_enable = max96714_set_pipe_enable,
	.set_pipe_tunnel_enable = max96714_set_pipe_tunnel_enable,
};

/*
 * These register writes are described as required in MAX96714 datasheet
 * Page 53, Section Register Map, to optimize link performance in 6Gbps
 * and 3Gbps links for all cable lengths.
 */
static const struct reg_sequence max96714_rlms_reg_sequence[] = {
	{ MAX9296A_RLMS3E(0), 0xfd },
	{ MAX9296A_RLMS3F(0), 0x3d },
	{ MAX9296A_RLMS49(0), 0xf5 },
	{ MAX9296A_RLMS7E(0), 0xa8 },
	{ MAX9296A_RLMS7F(0), 0x68 },
	{ MAX9296A_RLMSA3(0), 0x30 },
	{ MAX9296A_RLMSA5(0), 0x70 },
	{ MAX9296A_RLMSD8(0), 0x07 },
};

static const struct max9296a_chip_info max96714_info = {
	.info = &max96714_des_info,
	.ops = &max96714_ops,
	.max_register = 0x5011,
	.polarity_on_physical_lanes = true,
	.supports_phy_log = true,
	.rlms_adjust_sequence = max96714_rlms_reg_sequence,
	.rlms_adjust_sequence_len = ARRAY_SIZE(max96714_rlms_reg_sequence),
	.pipe_hw_ids = { 1 },
	.phy_hw_ids = { 1 },
};

static const struct max_des_info max96714f_des_info = {
	MAX9296A_COMMON_INFO,
	.versions = BIT(MAX_SERDES_GMSL_2_3GBPS),
	.modes = BIT(MAX_SERDES_GMSL_PIXEL_MODE) |
		 BIT(MAX_SERDES_GMSL_TUNNEL_MODE),
	.phys_configs = {
		.num_configs = ARRAY_SIZE(max96714_phys_configs),
		.configs = max96714_phys_configs,
	},
	.tpg_mode = MAX_SERDES_GMSL_PIXEL_MODE,
	.num_pipes = 1,
	.num_phys = 1,
	.num_links = 1,
};

static const struct max_des_ops max96714f_ops = {
	MAX9296A_COMMON_OPS,
	.set_pipe_stream_id = max96714_set_pipe_stream_id,
	.set_pipe_enable = max96714_set_pipe_enable,
	.set_pipe_tunnel_enable = max96714_set_pipe_tunnel_enable,
};

static const struct max9296a_chip_info max96714f_info = {
	.info = &max96714f_des_info,
	.ops = &max96714f_ops,
	.max_register = 0x5011,
	.polarity_on_physical_lanes = true,
	.supports_phy_log = true,
	.rlms_adjust_sequence = max96714_rlms_reg_sequence,
	.rlms_adjust_sequence_len = ARRAY_SIZE(max96714_rlms_reg_sequence),
	.pipe_hw_ids = { 1 },
	.phy_hw_ids = { 1 },
};

static const struct max_des_info max96716a_des_info = {
	MAX9296A_COMMON_INFO,
	.versions = BIT(MAX_SERDES_GMSL_2_3GBPS) |
		    BIT(MAX_SERDES_GMSL_2_6GBPS),
	.modes = BIT(MAX_SERDES_GMSL_PIXEL_MODE) |
		 BIT(MAX_SERDES_GMSL_TUNNEL_MODE),
	.use_atr = true,
	.phys_configs = {
		.num_configs = ARRAY_SIZE(max9296a_phys_configs),
		.configs = max9296a_phys_configs,
	},
	.tpg_mode = MAX_SERDES_GMSL_PIXEL_MODE,
	.num_pipes = 2,
	.num_phys = 2,
	.num_links = 2,
};

static const struct max_des_ops max96716a_ops = {
	MAX9296A_COMMON_OPS,
	.set_pipe_stream_id = max96714_set_pipe_stream_id,
	.set_pipe_link = max96716a_set_pipe_link,
	.set_pipe_enable = max96714_set_pipe_enable,
	.set_pipe_tunnel_phy = max96716a_set_pipe_tunnel_phy,
	.set_pipe_tunnel_enable = max96714_set_pipe_tunnel_enable,
};

static const struct max9296a_chip_info max96716a_info = {
	.info = &max96716a_des_info,
	.ops = &max96716a_ops,
	.max_register = 0x52d6,
	.has_per_link_lock = true,
	.has_per_link_reset = true,
	.phy0_lanes_0_1_on_second_phy = true,
	.supports_cphy = true,
	.supports_phy_log = true,
	.pipe_hw_ids = { 1, 2 },
	.phy_hw_ids = { 1, 2 },
};

static const struct max_des_info max96792a_des_info = {
	MAX9296A_COMMON_INFO,
	.versions = BIT(MAX_SERDES_GMSL_2_3GBPS) |
		    BIT(MAX_SERDES_GMSL_2_6GBPS) |
		    BIT(MAX_SERDES_GMSL_3_12GBPS),
	.modes = BIT(MAX_SERDES_GMSL_PIXEL_MODE) |
		 BIT(MAX_SERDES_GMSL_TUNNEL_MODE),
	.use_atr = true,
	.phys_configs = {
		.num_configs = ARRAY_SIZE(max9296a_phys_configs),
		.configs = max9296a_phys_configs,
	},
	.tpg_mode = MAX_SERDES_GMSL_PIXEL_MODE,
	.num_pipes = 2,
	.num_phys = 2,
	.num_links = 2,
};

static const struct max_des_ops max96792a_ops = {
	MAX9296A_COMMON_OPS,
	.set_pipe_stream_id = max96714_set_pipe_stream_id,
	.set_pipe_link = max96716a_set_pipe_link,
	.set_pipe_enable = max96714_set_pipe_enable,
	.set_pipe_tunnel_phy = max96716a_set_pipe_tunnel_phy,
	.set_pipe_tunnel_enable = max96714_set_pipe_tunnel_enable,
};

static const struct max9296a_chip_info max96792a_info = {
	.info = &max96792a_des_info,
	.ops = &max96792a_ops,
	.max_register = 0x52d6,
	.has_per_link_lock = true,
	.has_per_link_reset = true,
	.phy0_lanes_0_1_on_second_phy = true,
	.supports_cphy = true,
	.supports_phy_log = true,
	.pipe_hw_ids = { 1, 2 },
	.phy_hw_ids = { 1, 2 },
};

static const struct of_device_id max9296a_of_table[] = {
	{ .compatible = "maxim,max9296a", .data = &max9296a_info },
	{ .compatible = "maxim,max96714", .data = &max96714_info },
	{ .compatible = "maxim,max96714f", .data = &max96714f_info },
	{ .compatible = "maxim,max96714r", .data = &max96714f_info },
	{ .compatible = "maxim,max96716a", .data = &max96716a_info },
	{ .compatible = "maxim,max96792a", .data = &max96792a_info },
	{ },
};
MODULE_DEVICE_TABLE(of, max9296a_of_table);

static struct i2c_driver max9296a_i2c_driver = {
	.driver	= {
		.name = "max9296a",
		.of_match_table	= max9296a_of_table,
	},
	.probe = max9296a_probe,
	.remove = max9296a_remove,
};

module_i2c_driver(max9296a_i2c_driver);

MODULE_IMPORT_NS("MAX_SERDES");
MODULE_DESCRIPTION("Maxim MAX9296A Dual GMSL2/3 Deserializer Driver");
MODULE_AUTHOR("Cosmin Tanislav <cosmin.tanislav@analog.com>");
MODULE_LICENSE("GPL");
