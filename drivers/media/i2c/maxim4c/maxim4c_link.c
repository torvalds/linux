// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Quad GMSL Deserializer Link driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include <linux/delay.h>
#include "maxim4c_api.h"

static int maxim4c_link_enable_vdd_ldo1(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	int ret = 0;

	/* IF VDD = 1.2V: Enable REG_ENABLE and REG_MNL
	 *	CTRL0: Enable REG_ENABLE
	 *	CTRL2: Enable REG_MNL
	 */
	ret |= maxim4c_i2c_update_byte(client,
			0x0017, MAXIM4C_I2C_REG_ADDR_16BITS, BIT(2), BIT(2));
	ret |= maxim4c_i2c_update_byte(client,
			0x0019, MAXIM4C_I2C_REG_ADDR_16BITS, BIT(4), BIT(4));

	return ret;
}

static int maxim4c_link_enable_vdd_ldo2(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	int ret = 0;

	ret |= maxim4c_i2c_write_byte(client,
			0x06C2, MAXIM4C_I2C_REG_ADDR_16BITS,
			0x10);
	// delay 10ms
	msleep(10);

	return ret;
}

static int maxim4c_link_set_rate(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct maxim4c_link_cfg *link_cfg = NULL;
	u8 link_rate = 0;
	int ret = 0;

	/* Link A/B rate setting */
	link_rate = 0; /* default Transmitter Rate is 187.5Mbps */
	link_cfg = &gmsl_link->link_cfg[MAXIM4C_LINK_ID_A];
	if (link_cfg->link_enable) {
		/* Link A: Receiver Rate */
		if (link_cfg->link_rx_rate == MAXIM4C_LINK_RX_RATE_3GBPS)
			link_rate |= (0x1 << 0);
		else
			link_rate |= (0x2 << 0);
	}
	link_cfg = &gmsl_link->link_cfg[MAXIM4C_LINK_ID_B];
	if (link_cfg->link_enable) {
		/* Link B: Receiver Rate */
		if (link_cfg->link_rx_rate == MAXIM4C_LINK_RX_RATE_3GBPS)
			link_rate |= (0x1 << 4);
		else
			link_rate |= (0x2 << 4);
	}
	if (link_rate != 0) {
		ret |= maxim4c_i2c_write_byte(client,
				0x0010, MAXIM4C_I2C_REG_ADDR_16BITS,
				link_rate);
	}

	/* Link C/D rate setting */
	link_rate = 0;
	link_cfg = &gmsl_link->link_cfg[MAXIM4C_LINK_ID_C];
	if (link_cfg->link_enable) {
		/* Link C: Receiver Rate */
		if (link_cfg->link_rx_rate == MAXIM4C_LINK_RX_RATE_3GBPS)
			link_rate |= (0x1 << 0);
		else
			link_rate |= (0x2 << 0);
	}
	link_cfg = &gmsl_link->link_cfg[MAXIM4C_LINK_ID_D];
	if (link_cfg->link_enable) {
		/* Link D: Receiver Rate */
		if (link_cfg->link_rx_rate == MAXIM4C_LINK_RX_RATE_3GBPS)
			link_rate |= (0x1 << 4);
		else
			link_rate |= (0x2 << 4);
	}
	if (link_rate != 0) {
		ret |= maxim4c_i2c_write_byte(client,
				0x0011, MAXIM4C_I2C_REG_ADDR_16BITS,
				link_rate);
	}

	return ret;
}

static int maxim4c_link_run_init_seq(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct maxim4c_link_cfg *link_cfg = NULL;
	struct maxim4c_i2c_init_seq *init_seq = NULL;
	int i = 0;
	int ret = 0;

	// link init sequence
	for (i = 0; i < MAXIM4C_LINK_ID_MAX; i++) {
		link_cfg = &gmsl_link->link_cfg[i];
		init_seq = &link_cfg->link_init_seq;
		ret = maxim4c_i2c_run_init_seq(client, init_seq);
		if (ret) {
			dev_err(dev, "link id = %d init sequence error\n", i);
			return ret;
		}
	}

	return 0;
}

static int maxim4c_link_status_init(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct maxim4c_link_cfg *link_cfg = NULL;
	u8 link_type = 0, link_enable = 0;
	u8 reg_mask = 0, reg_value = 0;
	u16 reg_addr = 0;
	int ret = 0, link_idx = 0;

	gmsl_link->link_enable_mask = 0x00;
	gmsl_link->link_type_mask = 0x0F;
	gmsl_link->link_locked_mask = 0;

	link_type = 0xF0; /* default GMSL2 */
	link_enable = 0; /* default disable */
	for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable) {
			gmsl_link->link_enable_mask |= BIT(link_idx);

			if (link_cfg->link_type == MAXIM4C_GMSL1) {
				gmsl_link->link_type_mask &= ~BIT(link_idx);
				link_type &= ~BIT(4 + link_idx);
			}
		}
	}

	ret = maxim4c_i2c_write_byte(client,
			0x0006, MAXIM4C_I2C_REG_ADDR_16BITS,
			link_type | link_enable);

	reg_mask = BIT(1) | BIT(0);
	reg_value = 0;
	for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
		reg_addr = 0x0B04 + 0x100 * link_idx;
		ret |= maxim4c_i2c_update_byte(client,
				reg_addr, MAXIM4C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	if (gmsl_link->i2c_ctrl_port == MAXIM4C_I2C_PORT2) {
		reg_mask = 0x0F;
		reg_value = 0x0F;
		ret |= maxim4c_i2c_update_byte(client,
				0x000E, MAXIM4C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	reg_mask = 0xFF;
	reg_value = 0xFF;
	ret |= maxim4c_i2c_update_byte(client,
			0x0003, MAXIM4C_I2C_REG_ADDR_16BITS,
			reg_mask, reg_value);

	return ret;
}

u8 maxim4c_link_get_lock_state(maxim4c_t *maxim4c, u8 link_mask)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	u8 link_type = 0, link_lock = 0, lock_state = 0;

	dev_dbg(dev, "%s, link_mask = 0x%x\n", __func__, link_mask);

	// Link A
	if (link_mask & MAXIM4C_LINK_MASK_A) {
		link_type = gmsl_link->link_cfg[MAXIM4C_LINK_ID_A].link_type;
		if (link_type == MAXIM4C_GMSL2) {
			// GMSL2 Link A
			maxim4c_i2c_read_byte(client,
				0x001A, MAXIM4C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(3)) {
				lock_state |= MAXIM4C_LINK_MASK_A;
				dev_dbg(dev, "GMSL2 Link A locked\n");
			}
		} else {
			// GMSL1 Link A
			maxim4c_i2c_read_byte(client,
				0x0BCB, MAXIM4C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(0)) {
				lock_state |= MAXIM4C_LINK_MASK_A;
				dev_dbg(dev, "GMSL1 Link A locked\n");
			}
		}

		// record link lock
		if (lock_state & MAXIM4C_LINK_MASK_A)
			gmsl_link->link_locked_mask |= MAXIM4C_LINK_MASK_A;
		else
			gmsl_link->link_locked_mask &= ~MAXIM4C_LINK_MASK_A;
	}

	// Link B
	if (link_mask & MAXIM4C_LINK_MASK_B) {
		link_type = gmsl_link->link_cfg[MAXIM4C_LINK_ID_B].link_type;
		if (link_type == MAXIM4C_GMSL2) {
			// GMSL2 Link B
			maxim4c_i2c_read_byte(client,
				0x000A, MAXIM4C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(3)) {
				lock_state |= MAXIM4C_LINK_MASK_B;
				dev_dbg(dev, "GMSL2 Link B locked\n");
			}
		} else {
			// GMSL1 Link B
			maxim4c_i2c_read_byte(client,
				0x0CCB, MAXIM4C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(0)) {
				lock_state |= MAXIM4C_LINK_MASK_B;
				dev_dbg(dev, "GMSL1 Link B locked\n");
			}
		}

		// record link lock
		if (lock_state & MAXIM4C_LINK_MASK_B)
			gmsl_link->link_locked_mask |= MAXIM4C_LINK_MASK_B;
		else
			gmsl_link->link_locked_mask &= ~MAXIM4C_LINK_MASK_B;
	}

	// Link C
	if (link_mask & MAXIM4C_LINK_MASK_C) {
		link_type = gmsl_link->link_cfg[MAXIM4C_LINK_ID_C].link_type;
		if (link_type == MAXIM4C_GMSL2) {
			// GMSL2 Link C
			maxim4c_i2c_read_byte(client,
				0x000B, MAXIM4C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(3)) {
				lock_state |= MAXIM4C_LINK_MASK_C;
				dev_dbg(dev, "GMSL2 Link C locked\n");
			}
		} else {
			// GMSL1 Link C
			maxim4c_i2c_read_byte(client,
				0x0DCB, MAXIM4C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(0)) {
				lock_state |= MAXIM4C_LINK_MASK_C;
				dev_dbg(dev, "GMSL1 Link C locked\n");
			}
		}

		// record link lock
		if (lock_state & MAXIM4C_LINK_MASK_C)
			gmsl_link->link_locked_mask |= MAXIM4C_LINK_MASK_C;
		else
			gmsl_link->link_locked_mask &= ~MAXIM4C_LINK_MASK_C;
	}

	// Link D
	if (link_mask & MAXIM4C_LINK_MASK_D) {
		link_type = gmsl_link->link_cfg[MAXIM4C_LINK_ID_D].link_type;
		if (link_type == MAXIM4C_GMSL2) {
			// GMSL2 Link D
			maxim4c_i2c_read_byte(client,
				0x000C, MAXIM4C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(3)) {
				lock_state |= MAXIM4C_LINK_MASK_D;
				dev_dbg(dev, "GMSL2 Link D locked\n");
			}
		} else {
			// GMSL1 Link D
			maxim4c_i2c_read_byte(client,
				0x0ECB, MAXIM4C_I2C_REG_ADDR_16BITS,
				&link_lock);
			if (link_lock & BIT(0)) {
				lock_state |= MAXIM4C_LINK_MASK_D;
				dev_dbg(dev, "GMSL1 Link D locked\n");
			}
		}

		// record link lock
		if (lock_state & MAXIM4C_LINK_MASK_D)
			gmsl_link->link_locked_mask |= MAXIM4C_LINK_MASK_D;
		else
			gmsl_link->link_locked_mask &= ~MAXIM4C_LINK_MASK_D;
	}

	return lock_state;
}
EXPORT_SYMBOL(maxim4c_link_get_lock_state);

int maxim4c_link_oneshot_reset(struct maxim4c *maxim4c, u8 link_mask)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct maxim4c_link_cfg *link_cfg = NULL;
	u8 oneshot_reset = 0;
	int ret = 0, link_idx = 0;

	dev_dbg(dev, "%s, link_mask = 0x%x\n", __func__, link_mask);

	oneshot_reset = 0;
	for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable && (link_mask & BIT(link_idx)))
			oneshot_reset |= BIT(link_idx);
	}

	if (oneshot_reset != 0) {
		// One-Shot Reset
		ret = maxim4c_i2c_write_byte(client,
				0x0018, MAXIM4C_I2C_REG_ADDR_16BITS,
				oneshot_reset);
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_link_oneshot_reset);

int maxim4c_link_mask_enable(struct maxim4c *maxim4c, u8 link_mask, bool enable)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct maxim4c_link_cfg *link_cfg = NULL;
	u8 reg_mask = 0, reg_value = 0;
	int ret = 0, link_idx = 0;

	dev_dbg(dev, "%s, link_mask = 0x%x, enable = %d\n",
			__func__, link_mask, enable);

	reg_mask = 0;
	for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable && (link_mask & BIT(link_idx)))
			reg_mask |= BIT(link_idx);
	}

	if (reg_mask != 0) {
		reg_value = enable ? reg_mask : 0;

		ret = maxim4c_i2c_update_byte(client,
				0x0006, MAXIM4C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_link_mask_enable);

int maxim4c_link_wait_linklock(struct maxim4c *maxim4c, u8 link_mask)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	u8 lock_state = 0, link_bit_mask = 0;
	int loop_idx = 0, time_ms = 0, link_idx = 0;

	time_ms = 50;
	msleep(time_ms);

	for (loop_idx = 0; loop_idx < 20; loop_idx++) {
		for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
			link_bit_mask = BIT(link_idx);

			if ((link_mask & link_bit_mask)
					&& ((lock_state & link_bit_mask) == 0)) {
				if (maxim4c_link_get_lock_state(maxim4c, link_bit_mask)) {
					lock_state |= link_bit_mask;
					dev_info(dev, "Link %c locked time: %d ms\n",
						'A' + link_idx, time_ms);
				}
			}
		}

		if ((lock_state & link_mask) == link_mask) {
			dev_info(dev, "All Links are locked: 0x%x, time_ms = %d\n",
				lock_state, time_ms);
			maxim4c->link_lock_state = lock_state;
			return 0;
		}

		msleep(10);
		time_ms += 10;
	}

	if ((lock_state & link_mask) != 0) {
		dev_info(dev, "Partial links are locked: 0x%x, time_ms = %d\n",
			lock_state, time_ms);
		maxim4c->link_lock_state = lock_state;
		return 0;
	} else {
		dev_err(dev, "Failed to detect remote link, time_ms = %d!\n", time_ms);
		maxim4c->link_lock_state = 0;
		return -ENODEV;
	}
}
EXPORT_SYMBOL(maxim4c_link_wait_linklock);

int maxim4c_link_select_remote_enable(struct maxim4c *maxim4c, u8 link_mask)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	u8 link_enable = 0, link_reset = 0, link_bit_mask = 0;
	int ret = 0, link_idx = 0;

	dev_dbg(dev, "%s, link_mask = 0x%x\n", __func__, link_mask);

	ret = 0;
	link_enable = 0;
	link_reset = 0;

	for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
		link_bit_mask = BIT(link_idx);

		if (link_mask & link_bit_mask) {
			if (gmsl_link->link_cfg[link_idx].link_enable) {
				link_enable |= BIT(link_idx);
				link_reset |= BIT(link_idx);
			} else {
				link_mask &= ~link_bit_mask;
			}
		}
	}

	if (link_mask != 0) {
		// One-Shot Reset
		ret |= maxim4c_i2c_write_byte(client,
				0x0018, MAXIM4C_I2C_REG_ADDR_16BITS,
				link_reset);

		// Link Enable
		ret |= maxim4c_i2c_update_byte(client,
				0x0006, MAXIM4C_I2C_REG_ADDR_16BITS,
				link_enable, link_enable);

		ret |= maxim4c_link_wait_linklock(maxim4c, link_mask);
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_link_select_remote_enable);

int maxim4c_link_select_remote_control(struct maxim4c *maxim4c, u8 link_mask)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct maxim4c_link_cfg *link_cfg = NULL;
	u8 link_enable = 0, link_type = 0;
	u8 reg_mask = 0, reg_value = 0;
	u16 reg_addr = 0;
	int link_idx = 0, ret = 0;

	dev_dbg(dev, "%s, link mask = 0x%x\n", __func__, link_mask);

	// GMSL1 Link forward control channel
	for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];

		link_enable = link_cfg->link_enable;
		link_type = link_cfg->link_type;
		if (link_enable && (link_type == MAXIM4C_GMSL1)) {
			reg_mask = BIT(1) | BIT(0);

			if (link_mask & BIT(link_idx))
				// GMSL1: Enable forward control channel transmitter
				reg_value = BIT(1) | BIT(0);
			else
				// GMSL1: Disable forward control channel transmitter
				reg_value = 0;

			reg_addr = 0x0B04 + 0x100 * link_idx;
			ret |= maxim4c_i2c_update_byte(client,
					reg_addr, MAXIM4C_I2C_REG_ADDR_16BITS,
					reg_mask, reg_value);

			link_mask &= ~BIT(link_idx);
		}
	}

	// GMSL2 Link
	if (gmsl_link->i2c_ctrl_port == MAXIM4C_I2C_PORT2) {
		reg_mask = 0x0F;
		reg_value = ~link_mask;

		ret |= maxim4c_i2c_update_byte(client,
				0x000E, MAXIM4C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	} else if (gmsl_link->i2c_ctrl_port == MAXIM4C_I2C_PORT1) {
		reg_mask = 0xFF;
		reg_value = 0;

		for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
			if (link_mask & BIT(link_idx))
				reg_value |= BIT(1 + 2 * link_idx);
		}
		reg_value = ~reg_value;

		ret |= maxim4c_i2c_update_byte(client,
				0x0003, MAXIM4C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	} else {
		reg_mask = 0xFF;
		reg_value = 0;

		for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
			if (link_mask & BIT(link_idx))
				reg_value |= BIT(0 + 2 * link_idx);
		}
		reg_value = ~reg_value;

		ret |= maxim4c_i2c_update_byte(client,
				0x0003, MAXIM4C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_link_select_remote_control);

static int maxim4c_gmsl_link_config_parse_dt(struct device *dev,
			maxim4c_gmsl_link_t *gmsl_link,
			struct device_node *parent_node)
{
	struct device_node *node = NULL;
	struct device_node *init_seq_node = NULL;
	struct maxim4c_i2c_init_seq *init_seq = NULL;
	struct maxim4c_link_cfg *link_cfg = NULL;
	struct maxim4c_remote_info *remote_info;
	const char *link_cfg_name = "gmsl-link-config";
	const char *prop_str = NULL;
	u32 value = 0;
	u32 sub_idx = 0, link_id = 0;
	int ret = 0;

	node = NULL;
	sub_idx = 0;
	while ((node = of_get_next_child(parent_node, node))) {
		if (!strncasecmp(node->name,
				 link_cfg_name,
				 strlen(link_cfg_name))) {
			if (sub_idx >= MAXIM4C_LINK_ID_MAX) {
				dev_err(dev, "Too many matching %s node\n", link_cfg_name);

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
			if (link_id >= MAXIM4C_LINK_ID_MAX) {
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

			/* remote info */
			remote_info = &link_cfg->remote_info;
			ret = of_property_read_string(node, "remote-name", &prop_str);
			if (ret == 0) {
				dev_info(dev, "remote-name property: %s", prop_str);
				remote_info->remote_name = prop_str;
			}

			ret = of_property_read_string(node, "remote-compatible", &prop_str);
			if (ret == 0) {
				dev_info(dev, "remote-compatible property: %s", prop_str);
				remote_info->remote_compatible = prop_str;
			}

			/* link init sequence */
			init_seq_node = of_get_child_by_name(node, "link-init-sequence");
			if (!IS_ERR_OR_NULL(init_seq_node)) {
				dev_info(dev, "load pipe-init-sequence\n");

				init_seq = &link_cfg->link_init_seq;
				maxim4c_i2c_load_init_seq(dev,
						init_seq_node, init_seq);

				of_node_put(init_seq_node);
			}

			sub_idx++;
		}
	}

	return 0;
}

int maxim4c_link_parse_dt(maxim4c_t *maxim4c, struct device_node *of_node)
{
	struct device *dev = &maxim4c->client->dev;
	struct device_node *node = NULL;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	u32 value = 0;
	int ret = 0;

	dev_info(dev, "=== maxim4c link parse dt ===\n");

	node = of_get_child_by_name(of_node, "gmsl-links");
	if (IS_ERR_OR_NULL(node))
		return -ENODEV;

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

	/* vdd ldo2 enable */
	ret = of_property_read_u32(node, "link-vdd-ldo2-en", &value);
	if (ret == 0) {
		dev_info(dev, "link-vdd-ldo2-en property: %d\n", value);
		gmsl_link->link_vdd_ldo2_en = value;
	}

	ret = maxim4c_gmsl_link_config_parse_dt(dev, gmsl_link, node);

	of_node_put(node);

	return ret;
}
EXPORT_SYMBOL(maxim4c_link_parse_dt);

int maxim4c_link_hw_init(maxim4c_t *maxim4c)
{
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	int ret = 0;

	// All links disable at beginning.
	ret = maxim4c_link_status_init(maxim4c);
	if (ret)
		return ret;

	if (gmsl_link->link_vdd_ldo1_en)
		ret |= maxim4c_link_enable_vdd_ldo1(maxim4c);

	if (gmsl_link->link_vdd_ldo2_en)
		ret |= maxim4c_link_enable_vdd_ldo2(maxim4c);

	// Link Rate Setting
	ret |= maxim4c_link_set_rate(maxim4c);
	if (ret)
		return ret;

	// link init sequence
	ret = maxim4c_link_run_init_seq(maxim4c);

	return ret;
}
EXPORT_SYMBOL(maxim4c_link_hw_init);

void maxim4c_link_data_init(maxim4c_t *maxim4c)
{
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct maxim4c_link_cfg *link_cfg = NULL;
	int i = 0;

	gmsl_link->link_vdd_ldo1_en = 0;
	gmsl_link->link_vdd_ldo2_en = 0;
	gmsl_link->i2c_ctrl_port = MAXIM4C_I2C_PORT0;

	for (i = 0; i < MAXIM4C_LINK_ID_MAX; i++) {
		link_cfg = &gmsl_link->link_cfg[i];

		link_cfg->link_enable = 0;
		link_cfg->link_type = MAXIM4C_GMSL2;
		if (maxim4c->chipid == MAX96722_CHIP_ID)
			link_cfg->link_rx_rate = MAXIM4C_LINK_RX_RATE_3GBPS;
		else
			link_cfg->link_rx_rate = MAXIM4C_LINK_RX_RATE_6GBPS;
		link_cfg->link_tx_rate = MAXIM4C_LINK_TX_RATE_187_5MPS;
		link_cfg->remote_info.remote_name = NULL;
		link_cfg->remote_info.remote_compatible = NULL;
		link_cfg->link_init_seq.reg_init_seq = NULL;
	}
}
EXPORT_SYMBOL(maxim4c_link_data_init);
