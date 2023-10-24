// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Dual GMSL Deserializer Link driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include <linux/delay.h>
#include "maxim2c_api.h"

static int maxim2c_link_enable_vdd_ldo1(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	int ret = 0;

	/* IF VDD = 1.2V: Enable REG_ENABLE and REG_MNL
	 *	CTRL0: Enable REG_ENABLE
	 *	CTRL2: Enable REG_MNL
	 */
	ret |= maxim2c_i2c_update_byte(client,
			0x0010, MAXIM2C_I2C_REG_ADDR_16BITS, BIT(2), BIT(2));
	ret |= maxim2c_i2c_update_byte(client,
			0x0012, MAXIM2C_I2C_REG_ADDR_16BITS, BIT(4), BIT(4));

	return ret;
}

static int maxim2c_link_set_rate(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	struct maxim2c_link_cfg *link_cfg = NULL;
	u8 link_rate = 0;
	int ret = 0;

	/* Link A rate setting */
	link_rate = 0; /* default Transmitter Rate is 187.5Mbps */
	link_cfg = &gmsl_link->link_cfg[MAXIM2C_LINK_ID_A];
	if (link_cfg->link_enable) {
		/* Link A: Receiver Rate */
		if (link_cfg->link_rx_rate == MAXIM2C_LINK_RX_RATE_3GBPS)
			link_rate |= (0x1 << 0);
		else
			link_rate |= (0x2 << 0);

		ret |= maxim2c_i2c_update_byte(client,
				0x0001, MAXIM2C_I2C_REG_ADDR_16BITS,
				0x0F, link_rate);
	}

	/* Link B rate setting */
	link_rate = 0; /* default Transmitter Rate is 187.5Mbps */
	link_cfg = &gmsl_link->link_cfg[MAXIM2C_LINK_ID_B];
	if (link_cfg->link_enable) {
		/* Link B: Receiver Rate */
		if (link_cfg->link_rx_rate == MAXIM2C_LINK_RX_RATE_3GBPS)
			link_rate |= (0x1 << 0);
		else
			link_rate |= (0x2 << 0);

		ret |= maxim2c_i2c_update_byte(client,
				0x0004, MAXIM2C_I2C_REG_ADDR_16BITS,
				0x0F, link_rate);
	}

	return ret;
}

static int maxim2c_link_run_init_seq(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	struct maxim2c_link_cfg *link_cfg = NULL;
	struct maxim2c_i2c_init_seq *init_seq = NULL;
	int link_idx = 0;
	int ret = 0;

	// link init sequence
	for (link_idx = 0; link_idx < MAXIM2C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];
		init_seq = &link_cfg->link_init_seq;
		ret = maxim2c_i2c_run_init_seq(client, init_seq);
		if (ret) {
			dev_err(dev, "link id = %d init sequence error\n", link_idx);
			return ret;
		}
	}

	return 0;
}

static int maxim2c_link_status_init(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	struct maxim2c_link_cfg *link_cfg = NULL;
	u8 reg_mask = 0, reg_value = 0;
	u16 reg_addr = 0;
	int ret = 0, link_idx = 0;

	gmsl_link->link_enable_mask = 0x00;
	gmsl_link->link_type_mask = 0x03;
	gmsl_link->link_locked_mask = 0;

	reg_mask = 0xC0;
	reg_value = 0xC0; /* default GMSL2 */
	for (link_idx = 0; link_idx < MAXIM2C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable) {
			gmsl_link->link_enable_mask |= BIT(link_idx);

			if (link_cfg->link_type == MAXIM2C_GMSL1) {
				gmsl_link->link_type_mask &= ~BIT(link_idx);
				reg_value &= ~BIT(6 + link_idx);
			}
		}
	}
	ret |= maxim2c_i2c_update_byte(client,
			0x0006, MAXIM2C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_value);

	// AUTO_LINK disable, LINK_CFG for Link A and Link B select
	reg_mask = BIT(4) | BIT(1) | BIT(0);
	reg_value = 0;
	for (link_idx = 0; link_idx < MAXIM2C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable)
			reg_value |= BIT(link_idx);
	}
	ret |= maxim2c_i2c_update_byte(client,
			0x0010, MAXIM2C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_value);

	// GMSL1 Link disable forward and reverse control channel
	reg_mask = BIT(1) | BIT(0);
	reg_value = 0;
	for (link_idx = 0; link_idx < MAXIM2C_LINK_ID_MAX; link_idx++) {
		reg_addr = 0x0B04 + 0x100 * link_idx;
		ret |= maxim2c_i2c_update_byte(client,
				reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	// GMSL2 Link disable remote control channel
	reg_mask = BIT(4);
	reg_value = BIT(4);
	reg_addr = 0x0001;
	ret |= maxim2c_i2c_update_byte(client,
			reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_value);

	reg_mask = BIT(2);
	reg_value = BIT(2);
	reg_addr = 0x0003;
	ret |= maxim2c_i2c_update_byte(client,
			reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_value);

	return ret;
}

u8 maxim2c_link_get_lock_state(maxim2c_t *maxim2c, u8 link_mask)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	u8 link_type = 0, link_lock = 0, lock_state = 0;

	dev_dbg(dev, "%s, link_mask = 0x%x\n", __func__, link_mask);

	// Link A
	if (link_mask & MAXIM2C_LINK_MASK_A) {
		link_type = gmsl_link->link_cfg[MAXIM2C_LINK_ID_A].link_type;
		if (link_type == MAXIM2C_GMSL2) {
			// GMSL2 Link A
			maxim2c_i2c_read_byte(client,
				0x0013, MAXIM2C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(3)) {
				lock_state |= MAXIM2C_LINK_MASK_A;
				dev_dbg(dev, "GMSL2 Link A locked\n");
			}
		} else {
			// GMSL1 Link A
			maxim2c_i2c_read_byte(client,
				0x0BCB, MAXIM2C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(0)) {
				lock_state |= MAXIM2C_LINK_MASK_A;
				dev_dbg(dev, "GMSL1 Link A locked\n");
			}
		}

		// record link lock
		if (lock_state & MAXIM2C_LINK_MASK_A)
			gmsl_link->link_locked_mask |= MAXIM2C_LINK_MASK_A;
		else
			gmsl_link->link_locked_mask &= ~MAXIM2C_LINK_MASK_A;
	}

	// Link B
	if (link_mask & MAXIM2C_LINK_MASK_B) {
		link_type = gmsl_link->link_cfg[MAXIM2C_LINK_ID_B].link_type;
		if (link_type == MAXIM2C_GMSL2) {
			// GMSL2 Link B
			maxim2c_i2c_read_byte(client,
				0x5009, MAXIM2C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(3)) {
				lock_state |= MAXIM2C_LINK_MASK_B;
				dev_dbg(dev, "GMSL2 Link B locked\n");
			}
		} else {
			// GMSL1 Link B
			maxim2c_i2c_read_byte(client,
				0x0CCB, MAXIM2C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(0)) {
				lock_state |= MAXIM2C_LINK_MASK_B;
				dev_dbg(dev, "GMSL1 Link B locked\n");
			}
		}

		// record link lock
		if (lock_state & MAXIM2C_LINK_MASK_B)
			gmsl_link->link_locked_mask |= MAXIM2C_LINK_MASK_B;
		else
			gmsl_link->link_locked_mask &= ~MAXIM2C_LINK_MASK_B;
	}

	return lock_state;
}
EXPORT_SYMBOL(maxim2c_link_get_lock_state);

int maxim2c_link_oneshot_reset(struct maxim2c *maxim2c, u8 link_mask)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	struct maxim2c_link_cfg *link_cfg = NULL;
	int ret = 0, link_idx = 0;

	dev_dbg(dev, "%s, link_mask = 0x%x\n", __func__, link_mask);

	// Link A
	if (link_mask & MAXIM2C_LINK_MASK_A) {
		link_idx = MAXIM2C_LINK_ID_A;
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable && (link_mask & BIT(link_idx))) {
			ret = maxim2c_i2c_update_byte(client,
					0x0010, MAXIM2C_I2C_REG_ADDR_16BITS,
					BIT(5), BIT(5));
			if (ret) {
				dev_err(dev, "Link A oneshot reset error\n");
				return ret;
			}
		}
	}

	// Link B
	if (link_mask & MAXIM2C_LINK_MASK_B) {
		link_idx = MAXIM2C_LINK_ID_B;
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable && (link_mask & BIT(link_idx))) {
			ret = maxim2c_i2c_update_byte(client,
					0x0012, MAXIM2C_I2C_REG_ADDR_16BITS,
					BIT(5), BIT(5));
			if (ret) {
				dev_err(dev, "Link B oneshot reset error\n");
				return ret;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(maxim2c_link_oneshot_reset);

int maxim2c_link_mask_enable(struct maxim2c *maxim2c, u8 link_mask, bool enable)
{
	return 0;
}
EXPORT_SYMBOL(maxim2c_link_mask_enable);

int maxim2c_link_wait_linklock(struct maxim2c *maxim2c, u8 link_mask)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	u8 lock_state = 0, link_bit_mask = 0;
	int loop_idx = 0, time_ms = 0, link_idx = 0;

	time_ms = 50;
	msleep(time_ms);

	for (loop_idx = 0; loop_idx < 20; loop_idx++) {
		if (loop_idx != 0) {
			msleep(10);
			time_ms += 10;
		}

		for (link_idx = 0; link_idx < MAXIM2C_LINK_ID_MAX; link_idx++) {
			link_bit_mask = BIT(link_idx);

			if ((link_mask & link_bit_mask)
					&& ((lock_state & link_bit_mask) == 0)) {
				if (maxim2c_link_get_lock_state(maxim2c, link_bit_mask)) {
					lock_state |= link_bit_mask;
					dev_info(dev, "Link %c locked time: %d ms\n",
						'A' + link_idx, time_ms);
				}
			}
		}

		if ((lock_state & link_mask) == link_mask) {
			dev_info(dev, "All Links are locked: 0x%x, time_ms = %d\n",
				lock_state, time_ms);
			maxim2c->link_lock_state = lock_state;
			return 0;
		}
	}

	if ((lock_state & link_mask) != 0) {
		dev_info(dev, "Partial links are locked: 0x%x, time_ms = %d\n",
			lock_state, time_ms);
		maxim2c->link_lock_state = lock_state;
		return 0;
	} else {
		dev_err(dev, "Failed to detect remote link, time_ms = %d!\n", time_ms);
		maxim2c->link_lock_state = 0;
		return -ENODEV;
	}
}
EXPORT_SYMBOL(maxim2c_link_wait_linklock);

int maxim2c_link_select_remote_enable(struct maxim2c *maxim2c, u8 link_mask)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	int ret = 0;

	dev_dbg(dev, "%s, link_mask = 0x%x\n", __func__, link_mask);

	ret = maxim2c_link_oneshot_reset(maxim2c, link_mask);
	if (ret) {
		dev_err(dev, "%s: link oneshot reset error, link mask = 0x%x\n",
				__func__, link_mask);
		return ret;
	}

	ret = maxim2c_link_mask_enable(maxim2c, link_mask, true);
	if (ret) {
		dev_err(dev, "%s: link enable error, link mask = 0x%x\n",
				__func__, link_mask);
		return ret;
	}

	maxim2c_link_wait_linklock(maxim2c, link_mask);
	dev_info(dev, "link_mask = 0x%02x, link_lock = 0x%02x\n",
			link_mask, maxim2c->link_lock_state);

	return 0;
}
EXPORT_SYMBOL(maxim2c_link_select_remote_enable);

int maxim2c_link_select_remote_control(struct maxim2c *maxim2c, u8 link_mask)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	struct maxim2c_link_cfg *link_cfg = NULL;
	u8 reg_mask = 0, reg_value = 0;
	u16 reg_addr = 0;
	int link_idx = 0, ret = 0;

	dev_dbg(dev, "%s, link mask = 0x%x\n", __func__, link_mask);

	for (link_idx = 0; link_idx < MAXIM2C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable == 0)
			continue;

		if (link_cfg->link_type == MAXIM2C_GMSL1) {
			// GMSL1 Link forward and reverse control channel
			reg_mask = BIT(1) | BIT(0);

			if (link_mask & BIT(link_idx))
				// GMSL1: Enable control channel transmitter
				reg_value = BIT(1) | BIT(0);
			else
				// GMSL1: Disable control channel transmitter
				reg_value = 0;

			reg_addr = 0x0B04 + 0x100 * link_idx;
			ret |= maxim2c_i2c_update_byte(client,
					reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
					reg_mask, reg_value);
		} else {
			// GMSL2 Link remote control channel
			if (link_idx == MAXIM2C_LINK_ID_A) {
				reg_addr = 0x0001;
				reg_mask = BIT(4);

				if (link_mask & BIT(link_idx))
					reg_value = 0;
				else
					reg_value = BIT(4); // Link A remote control channel disabled
				ret |= maxim2c_i2c_update_byte(client,
						reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
						reg_mask, reg_value);
			} else {
				reg_addr = 0x0003;
				reg_mask = BIT(2);
				if (link_mask & BIT(link_idx))
					reg_value = 0;
				else
					reg_value = BIT(2); // Link B remote control channel disabled
				ret |= maxim2c_i2c_update_byte(client,
						reg_addr, MAXIM2C_I2C_REG_ADDR_16BITS,
						reg_mask, reg_value);
			}
		}
	}

	return ret;
}
EXPORT_SYMBOL(maxim2c_link_select_remote_control);

static int maxim2c_gmsl_link_config_parse_dt(struct device *dev,
			maxim2c_gmsl_link_t *gmsl_link,
			struct device_node *parent_node)
{
	struct device_node *node = NULL;
	struct device_node *init_seq_node = NULL;
	struct maxim2c_i2c_init_seq *init_seq = NULL;
	struct maxim2c_link_cfg *link_cfg = NULL;
	const char *link_cfg_name = "gmsl-link-config";
	u32 value = 0;
	u32 sub_idx = 0, link_id = 0;
	int ret = 0;

	node = NULL;
	sub_idx = 0;
	while ((node = of_get_next_child(parent_node, node))) {
		if (!strncasecmp(node->name,
				 link_cfg_name,
				 strlen(link_cfg_name))) {
			if (sub_idx >= MAXIM2C_LINK_ID_MAX) {
				dev_err(dev, "%pOF: Too many matching %s node\n",
						parent_node, link_cfg_name);

				of_node_put(node);
				break;
			}

			if (!of_device_is_available(node)) {
				dev_info(dev, "%pOF is disabled\n", node);

				sub_idx++;

				continue;
			}

			/* GMSL LINK: link id */
			ret = of_property_read_u32(node, "link-id", &link_id);
			if (ret) {
				// if link_id is error, parse next node
				dev_err(dev, "Can not get link-id property!");

				sub_idx++;

				continue;
			}
			if (link_id >= MAXIM2C_LINK_ID_MAX) {
				// if link_id is error, parse next node
				dev_err(dev, "Error link-id = %d!", link_id);

				sub_idx++;

				continue;
			}

			link_cfg = &gmsl_link->link_cfg[link_id];

			/* GMSL LINK: link enable */
			link_cfg->link_enable = 1;

			dev_info(dev, "gmsl link id = %d: link_enable = %d\n",
					link_id, link_cfg->link_enable);

			/* GMSL LINK: other config */
			ret = of_property_read_u32(node, "link-type", &value);
			if (ret == 0) {
				dev_info(dev, "link-type property: %d", value);
				link_cfg->link_type = value;
			}

			ret = of_property_read_u32(node, "link-rx-rate", &value);
			if (ret == 0) {
				dev_info(dev, "link-rx-rate property: %d", value);
				link_cfg->link_rx_rate = value;
			}

			/* link init sequence */
			init_seq_node = of_get_child_by_name(node, "link-init-sequence");
			if (!IS_ERR_OR_NULL(init_seq_node)) {
				dev_info(dev, "load pipe-init-sequence\n");

				init_seq = &link_cfg->link_init_seq;
				maxim2c_i2c_load_init_seq(dev,
						init_seq_node, init_seq);

				of_node_put(init_seq_node);
			}

			sub_idx++;
		}
	}

	return 0;
}

int maxim2c_link_parse_dt(maxim2c_t *maxim2c, struct device_node *of_node)
{
	struct device *dev = &maxim2c->client->dev;
	struct device_node *node = NULL;
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	u32 value = 0;
	int ret = 0;

	dev_info(dev, "=== maxim2c link parse dt ===\n");

	node = of_get_child_by_name(of_node, "gmsl-links");
	if (IS_ERR_OR_NULL(node)) {
		dev_err(dev, "%pOF has no child node: gmsl-links\n",
				of_node);
		return -ENODEV;
	}

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled\n", node);
		of_node_put(node);
		return -ENODEV;
	}

	/* vdd 1.2v ldo1 enable */
	ret = of_property_read_u32(node, "link-vdd-ldo1-en", &value);
	if (ret == 0) {
		dev_info(dev, "link-vdd-ldo1-en property: %d\n", value);
		gmsl_link->link_vdd_ldo1_en = value;
	}

	ret = maxim2c_gmsl_link_config_parse_dt(dev, gmsl_link, node);

	of_node_put(node);

	return ret;
}
EXPORT_SYMBOL(maxim2c_link_parse_dt);

int maxim2c_link_hw_init(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	int ret = 0;

	// All links disable at beginning.
	ret = maxim2c_link_status_init(maxim2c);
	if (ret) {
		dev_err(dev, "%s: link status error\n", __func__);
		return ret;
	}

	if (gmsl_link->link_vdd_ldo1_en)
		ret |= maxim2c_link_enable_vdd_ldo1(maxim2c);
	if (ret) {
		dev_err(dev, "%s: link vdd ldo enable error\n", __func__);
		return ret;
	}

	// Link Rate Setting
	ret = maxim2c_link_set_rate(maxim2c);
	if (ret) {
		dev_err(dev, "%s: link set rate error\n", __func__);
		return ret;
	}

	// link init sequence
	ret = maxim2c_link_run_init_seq(maxim2c);
	if (ret) {
		dev_err(dev, "%s: link run init seq error\n", __func__);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(maxim2c_link_hw_init);

void maxim2c_link_data_init(maxim2c_t *maxim2c)
{
	maxim2c_gmsl_link_t *gmsl_link = &maxim2c->gmsl_link;
	struct maxim2c_link_cfg *link_cfg = NULL;
	int i = 0;

	gmsl_link->link_vdd_ldo1_en = 0;

	for (i = 0; i < MAXIM2C_LINK_ID_MAX; i++) {
		link_cfg = &gmsl_link->link_cfg[i];

		link_cfg->link_enable = 0;
		link_cfg->link_type = MAXIM2C_GMSL2;
		link_cfg->link_tx_rate = MAXIM2C_LINK_TX_RATE_187_5MPS;
		link_cfg->link_init_seq.reg_init_seq = NULL;
	}
}
EXPORT_SYMBOL(maxim2c_link_data_init);
