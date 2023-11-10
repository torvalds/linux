// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Dual GMSL Deserializer MIPI txphy driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include <linux/iopoll.h>

#include "maxim2c_api.h"

static int maxim2c_txphy_auto_init_deskew(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	struct maxim2c_txphy_cfg *phy_cfg = NULL;
	u16 reg_addr = 0;
	u8 phy_idx = 0;
	int ret = 0;

	// D-PHY Deskew Initial Calibration Control
	for (phy_idx = 0; phy_idx < MAXIM2C_TXPHY_ID_MAX; phy_idx++) {
		// Auto Deskew can only be configured on PHY1 and PHY2
		if ((phy_idx == MAXIM2C_TXPHY_ID_A) || (phy_idx == MAXIM2C_TXPHY_ID_D))
			continue;

		phy_cfg = &mipi_txphy->phy_cfg[phy_idx];
		if (phy_cfg->phy_enable && (phy_cfg->auto_deskew & BIT(7))) {
			reg_addr = 0x0403 + 0x40 * phy_idx;
			ret |= maxim2c_i2c_write_byte(client,
					reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
					phy_cfg->auto_deskew);
		}
	}

	return ret;
}

static int maxim2c_mipi_txphy_lane_mapping(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	struct maxim2c_txphy_cfg *phy_cfg = NULL;
	u8 reg_value = 0, reg_mask = 0;
	int ret = 0;

	// MIPI TXPHY A/B: data lane mapping
	reg_mask = 0;
	reg_value = 0;
	phy_cfg = &mipi_txphy->phy_cfg[MAXIM2C_TXPHY_ID_A];
	if (phy_cfg->phy_enable) {
		reg_mask |= 0x0F;
		reg_value |= (phy_cfg->data_lane_map << 0);
	}
	phy_cfg = &mipi_txphy->phy_cfg[MAXIM2C_TXPHY_ID_B];
	if (phy_cfg->phy_enable) {
		reg_mask |= 0xF0;
		reg_value |= (phy_cfg->data_lane_map << 4);
	}
	if (reg_mask != 0) {
		ret |= maxim2c_i2c_update_byte(client,
				0x0333, MAXIM2C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	// MIPI TXPHY C/D: data lane mapping
	reg_mask = 0;
	reg_value = 0;
	phy_cfg = &mipi_txphy->phy_cfg[MAXIM2C_TXPHY_ID_C];
	if (phy_cfg->phy_enable) {
		reg_mask |= 0x0F;
		reg_value |= (phy_cfg->data_lane_map << 0);
	}
	phy_cfg = &mipi_txphy->phy_cfg[MAXIM2C_TXPHY_ID_D];
	if (phy_cfg->phy_enable) {
		reg_mask |= 0xF0;
		reg_value |= (phy_cfg->data_lane_map << 4);
	}
	if (reg_mask != 0) {
		ret |= maxim2c_i2c_update_byte(client,
				0x0334, MAXIM2C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	return ret;
}

static int maxim2c_mipi_txphy_type_vcx_lane_num(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	struct maxim2c_txphy_cfg *phy_cfg = NULL;
	u8 phy_idx = 0;
	u8 reg_mask = 0, reg_value = 0;
	u16 reg_addr = 0;
	int ret = 0;

	for (phy_idx = 0; phy_idx < MAXIM2C_TXPHY_ID_MAX; phy_idx++) {
		phy_cfg = &mipi_txphy->phy_cfg[phy_idx];
		if (phy_cfg->phy_enable == 0)
			continue;

		reg_mask = BIT(7) | BIT(6) | BIT(5) | BIT(3);
		reg_value = 0;

		if (phy_cfg->phy_type == MAXIM2C_TXPHY_TYPE_CPHY)
			reg_value |= BIT(5);

		if (phy_cfg->vc_ext_en)
			reg_value |= BIT(3);

		reg_value |= ((phy_cfg->data_lane_num - 1) << 6);

		reg_addr = 0x040A + 0x40 * phy_idx;
		ret |= maxim2c_i2c_update_byte(client,
				reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	return ret;
}

static int maxim2c_mipi_txphy_tunnel_init(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	struct maxim2c_txphy_cfg *phy_cfg = NULL;
	u8 phy_idx = 0;
	u8 reg_mask = 0, reg_value = 0;
	u16 reg_addr = 0;
	int ret = 0;

	for (phy_idx = 0; phy_idx < MAXIM2C_TXPHY_ID_MAX; phy_idx++) {
		// Tunnel mode can only be configured on PHY1 and PHY2
		if ((phy_idx == MAXIM2C_TXPHY_ID_A) || (phy_idx == MAXIM2C_TXPHY_ID_D))
			continue;

		phy_cfg = &mipi_txphy->phy_cfg[phy_idx];
		if (phy_cfg->tunnel_enable) {
			// tunnel mode: enable
			reg_mask = BIT(0);
			reg_value = BIT(0);

			// tunnel pipe destination
			reg_mask |= BIT(1);
			reg_value |= ((phy_cfg->tunnel_dest & 0x1) << 1);
		} else {
			// tunnel mode: disable
			reg_mask = BIT(0);
			reg_value = 0;
		}

		reg_addr = 0x0434 + 0x40 * phy_idx;
		ret |= maxim2c_i2c_update_byte(client,
				reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);

		if (phy_cfg->tunnel_enable) {
			reg_addr = 0x0433 + 0x40 * phy_idx;
			reg_mask = (BIT(7) | BIT(6) | BIT(5));
			reg_value = ((phy_cfg->tunnel_vs_wait & 0x07) << 5);

			ret |= maxim2c_i2c_update_byte(client,
					reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
					reg_mask, reg_value);
		}
	}

	return ret;
}

int maxim2c_mipi_txphy_enable(maxim2c_t *maxim2c, bool enable)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	u8 phy_idx = 0;
	u8 reg_mask = 0, reg_value = 0;
	int ret = 0;

	dev_dbg(dev, "%s: enable = %d\n", __func__, enable);

	reg_mask = 0xF0;
	reg_value = 0;

	if (enable) {
		for (phy_idx = 0; phy_idx < MAXIM2C_TXPHY_ID_MAX; phy_idx++) {
			if (mipi_txphy->phy_cfg[phy_idx].phy_enable)
				reg_value |= BIT(4 + phy_idx);
		}
	}

	ret |= maxim2c_i2c_update_byte(client,
			0x0332, MAXIM2C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_value);

	return ret;
}

int maxim2c_dphy_dpll_predef_set(maxim2c_t *maxim2c, s64 link_freq_hz)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	struct maxim2c_txphy_cfg *phy_cfg = NULL;
	u32 link_freq_mhz = 0;
	u16 reg_addr = 0;
	u8 phy_idx = 0;
	u8 dpll_mask = 0, dpll_val = 0, dpll_lock = 0;
	int ret = 0;

	dpll_mask = 0;

	link_freq_mhz = (u32)div_s64(link_freq_hz, 1000000L);
	dpll_val = DIV_ROUND_UP(link_freq_mhz * 2, 100) & 0x1F;
	if (dpll_val == 0)
		dpll_val = 15; /* default 1500MBps */
	// Disable software override for frequency fine tuning
	dpll_val |= BIT(5);

	for (phy_idx = 0; phy_idx < MAXIM2C_TXPHY_ID_MAX; phy_idx++) {
		phy_cfg = &mipi_txphy->phy_cfg[phy_idx];
		if ((phy_cfg->phy_enable == 0) || (phy_cfg->clock_master == 0))
			continue;

		if (phy_cfg->clock_mode != MAXIM2C_TXPHY_DPLL_PREDEF)
			continue;

		dpll_mask |= BIT(phy_idx + 4);

		// Hold DPLL in reset (config_soft_rst_n = 0) before changing the rate
		reg_addr = 0x1C00 + 0x100 * phy_idx;
		ret |= maxim2c_i2c_write_byte(client,
				reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
				0xf4);

		// Set dpll data rate
		reg_addr = 0x031D + 0x03 * phy_idx;
		ret |= maxim2c_i2c_update_byte(client,
				reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
				0x3F, dpll_val);

		// Release reset to DPLL (config_soft_rst_n = 1)
		reg_addr = 0x1C00 + 0x100 * phy_idx;
		ret |= maxim2c_i2c_write_byte(client,
				reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
				0xf5);
	}

	if (ret) {
		dev_err(dev, "DPLL predef set error!\n");
		return ret;
	}

#if 0
	ret = read_poll_timeout(maxim2c_i2c_read_byte, ret,
				!(ret < 0) && (dpll_lock & dpll_mask),
				1000, 10000, false,
				client,
				0x0308, MAXIM2C_I2C_REG_ADDR_16BITS,
				&dpll_lock);
	if (ret < 0) {
		dev_err(dev, "DPLL is unlocked: 0x%02x\n", dpll_lock);
		return ret;
	} else {
		dev_info(dev, "DPLL is locked: 0x%02x\n", dpll_lock);
		return 0;
	}
#else
	// The locking status of DPLL cannot be obtained before csi output
	usleep_range(1000, 1100);
	ret = maxim2c_i2c_read_byte(client,
				0x0308, MAXIM2C_I2C_REG_ADDR_16BITS,
				&dpll_lock);
	dev_info(dev, "DPLL lock state: 0x%02x\n", dpll_lock);

	return ret;
#endif
}
EXPORT_SYMBOL(maxim2c_dphy_dpll_predef_set);

int maxim2c_mipi_csi_output(maxim2c_t *maxim2c, bool enable)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	u8 reg_mask = 0, reg_value = 0;
	int ret = 0;

	dev_dbg(dev, "%s: enable = %d\n", __func__, enable);

	if (mipi_txphy->force_clock_out_en != 0) {
		reg_mask = BIT(7);
		reg_value = enable ? BIT(7) : 0;

		// Force all MIPI clocks running Config
		ret |= maxim2c_i2c_update_byte(client,
				0x0330, MAXIM2C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	/* Bit1 of the register 0x0313: CSI_OUT_EN
	 *     1 = CSI output enabled
	 *     0 = CSI output disabled
	 */
	reg_mask = BIT(1);
	reg_value = enable ? BIT(1) : 0;

	// MIPI CSI output Setting
	ret |= maxim2c_i2c_update_byte(client,
			0x0313, MAXIM2C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_value);

	return ret;
}
EXPORT_SYMBOL(maxim2c_mipi_csi_output);

static int maxim2c_mipi_txphy_config_parse_dt(struct device *dev,
				maxim2c_mipi_txphy_t *mipi_txphy,
				struct device_node *parent_node)
{
	struct device_node *node = NULL;
	struct maxim2c_txphy_cfg *phy_cfg = NULL;
	const char *txphy_cfg_name = "mipi-txphy-config";
	u32 value = 0;
	u32 sub_idx = 0, phy_id = 0;
	int ret;

	node = NULL;
	sub_idx = 0;
	while ((node = of_get_next_child(parent_node, node))) {
		if (!strncasecmp(node->name,
				 txphy_cfg_name,
				 strlen(txphy_cfg_name))) {
			if (sub_idx >= MAXIM2C_TXPHY_ID_MAX) {
				dev_err(dev, "%pOF: Too many matching %s node\n",
						parent_node, txphy_cfg_name);

				of_node_put(node);
				break;
			}

			if (!of_device_is_available(node)) {
				dev_info(dev, "%pOF is disabled\n", node);

				sub_idx++;

				continue;
			}

			/* MIPI TXPHY: phy id */
			ret = of_property_read_u32(node, "phy-id", &phy_id);
			if (ret) {
				// if mipi txphy phy_id is error, parse next node
				dev_err(dev, "Can not get phy-id property!");

				sub_idx++;

				continue;
			}
			if (phy_id >= MAXIM2C_TXPHY_ID_MAX) {
				// if mipi txphy phy_id is error, parse next node
				dev_err(dev, "Error phy-id = %d!", phy_id);

				sub_idx++;

				continue;
			}

			phy_cfg = &mipi_txphy->phy_cfg[phy_id];

			/* MIPI TXPHY: phy enable */
			phy_cfg->phy_enable = 1;

			dev_info(dev, "mipi txphy id = %d: phy_enable = %d\n",
					phy_id, phy_cfg->phy_enable);

			/* MIPI TXPHY: other config */
			ret = of_property_read_u32(node, "phy-type", &value);
			if (ret == 0) {
				dev_info(dev, "phy-type property: %d", value);
				phy_cfg->phy_type = value;
			}

			ret = of_property_read_u32(node, "auto-deskew", &value);
			if (ret == 0) {
				dev_info(dev, "auto-deskew property: 0x%x", value);
				phy_cfg->auto_deskew = value;
			}

			ret = of_property_read_u32(node, "data-lane-num", &value);
			if (ret == 0) {
				dev_info(dev, "data-lane-num property: %d", value);
				phy_cfg->data_lane_num = value;
			}

			ret = of_property_read_u32(node, "data-lane-map", &value);
			if (ret == 0) {
				dev_info(dev, "data-lane-map property: 0x%x", value);
				phy_cfg->data_lane_map = value;
			}

			ret = of_property_read_u32(node, "vc-ext-en", &value);
			if (ret == 0) {
				dev_info(dev, "vc-ext-en property: %d", value);
				phy_cfg->vc_ext_en = value;
			}

			ret = of_property_read_u32(node, "tunnel-enable", &value);
			if (ret == 0) {
				dev_info(dev, "tunnel-enable property: %d", value);
				phy_cfg->tunnel_enable = value;
			}

			ret = of_property_read_u32(node, "tunnel-vs-wait", &value);
			if (ret == 0) {
				dev_info(dev, "tunnel-vs-wait property: %d", value);
				phy_cfg->tunnel_vs_wait = value;
			}

			ret = of_property_read_u32(node, "tunnel-dest", &value);
			if (ret == 0) {
				dev_info(dev, "tunnel-dest property: %d", value);
				phy_cfg->tunnel_dest = value;
			}

			ret = of_property_read_u32(node, "clock-mode", &value);
			if (ret == 0) {
				dev_info(dev, "clock-mode property: %d", value);
				phy_cfg->clock_mode = value;
			}

			sub_idx++;
		}
	}

	return 0;
}

int maxim2c_mipi_txphy_parse_dt(maxim2c_t *maxim2c, struct device_node *of_node)
{
	struct device *dev = &maxim2c->client->dev;
	struct device_node *node = NULL;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	u32 value = 0;
	int ret = 0;

	dev_info(dev, "=== maxim2c mipi txphy parse dt ===\n");

	node = of_get_child_by_name(of_node, "mipi-txphys");
	if (IS_ERR_OR_NULL(node)) {
		dev_err(dev, "%pOF has no child node: mipi-txphys\n",
				of_node);
		return -ENODEV;
	}

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled\n", node);
		of_node_put(node);
		return -ENODEV;
	}

	/* mipi txphy mode */
	ret = of_property_read_u32(node, "phy-mode", &value);
	if (ret == 0) {
		dev_info(dev, "phy-mode property: %d\n", value);
		mipi_txphy->phy_mode = value;
	}
	dev_info(dev, "mipi txphy mode: %d\n", mipi_txphy->phy_mode);

	/* MIPI clocks running mode */
	ret = of_property_read_u32(node, "phy-force-clock-out", &value);
	if (ret == 0) {
		dev_info(dev, "phy-force-clock-out property: %d\n", value);
		mipi_txphy->force_clock_out_en = value;
	}
	dev_info(dev, "mipi txphy force clock out enable: %d\n",
			mipi_txphy->force_clock_out_en);

	ret = maxim2c_mipi_txphy_config_parse_dt(dev, mipi_txphy, node);

	of_node_put(node);

	return ret;
}
EXPORT_SYMBOL(maxim2c_mipi_txphy_parse_dt);

int maxim2c_mipi_txphy_hw_init(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	struct maxim2c_txphy_cfg *phy_cfg = NULL;
	u8 mode = 0;
	int ret = 0;

	switch (mipi_txphy->phy_mode) {
	case MAXIM2C_TXPHY_MODE_2X2LANES:
		mode = BIT(0);
		break;
	case MAXIM2C_TXPHY_MODE_2X4LANES:
	default:
		mode = BIT(2);
		break;
	}

	// clock master
	phy_cfg = &mipi_txphy->phy_cfg[MAXIM2C_TXPHY_ID_B];
	if (phy_cfg->phy_enable) {
		if (phy_cfg->tunnel_enable) {
			if (phy_cfg->tunnel_dest == 0) {
				phy_cfg->clock_master = 1;
			} else {
				phy_cfg->phy_enable = 0;
				phy_cfg->clock_master = 0;
			}
		} else {
			phy_cfg->clock_master = 1;
		}
	}

	phy_cfg = &mipi_txphy->phy_cfg[MAXIM2C_TXPHY_ID_C];
	if (phy_cfg->phy_enable) {
		if (phy_cfg->tunnel_enable) {
			if (phy_cfg->tunnel_dest == 1) {
				phy_cfg->clock_master = 1;
			} else {
				phy_cfg->phy_enable = 0;
				phy_cfg->clock_master = 0;
			}
		} else {
			phy_cfg->clock_master = 1;
		}
	}

	// MIPI TXPHY Mode setting
	ret |= maxim2c_i2c_write_byte(client,
			0x0330, MAXIM2C_I2C_REG_ADDR_16BITS,
			mode);

	// Waits for a frame before generating MIPI Packet requests to the MIPI TX
	ret |= maxim2c_i2c_update_byte(client,
			0x0325, MAXIM2C_I2C_REG_ADDR_16BITS,
			BIT(7), BIT(7));

	// mipi txphy data lane mapping
	ret |= maxim2c_mipi_txphy_lane_mapping(maxim2c);

	// mipi txphy type, lane number, virtual channel extension
	ret |= maxim2c_mipi_txphy_type_vcx_lane_num(maxim2c);

	// mipi txphy tunnel init
	ret |= maxim2c_mipi_txphy_tunnel_init(maxim2c);

	// mipi txphy auto init deskew
	ret |= maxim2c_txphy_auto_init_deskew(maxim2c);

	if (ret) {
		dev_err(dev, "%s: txphy hw init error\n", __func__);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(maxim2c_mipi_txphy_hw_init);

void maxim2c_mipi_txphy_data_init(maxim2c_t *maxim2c)
{
	maxim2c_mipi_txphy_t *mipi_txphy = &maxim2c->mipi_txphy;
	struct maxim2c_txphy_cfg *phy_cfg = NULL;
	int i = 0;

	mipi_txphy->phy_mode = MAXIM2C_TXPHY_MODE_2X4LANES;
	mipi_txphy->force_clock_out_en = 1;

	for (i = 0; i < MAXIM2C_TXPHY_ID_MAX; i++) {
		phy_cfg = &mipi_txphy->phy_cfg[i];

		phy_cfg->phy_enable = 0;
		phy_cfg->phy_type = MAXIM2C_TXPHY_TYPE_DPHY;
		phy_cfg->auto_deskew = 0;
		phy_cfg->data_lane_num = 4;
		phy_cfg->data_lane_map = 0xe4;
		phy_cfg->vc_ext_en = 0;
		phy_cfg->tunnel_enable = 0;
		phy_cfg->clock_master = 0;
		phy_cfg->clock_mode = MAXIM2C_TXPHY_DPLL_PREDEF;
	}
}
EXPORT_SYMBOL(maxim2c_mipi_txphy_data_init);
