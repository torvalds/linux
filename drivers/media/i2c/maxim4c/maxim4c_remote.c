// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Quad GMSL Deserializer Remode Device Manage
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/mfd/core.h>
#include "maxim4c_api.h"

static const char *maxim4c_remote_devs_name[MAXIM4C_LINK_ID_MAX] = {
	"remote0", "remote1", "remote2", "remote3"
};

static const char *maxim4c_remote_link_compat[MAXIM4C_LINK_ID_MAX] = {
	"maxim4c,link0", "maxim4c,link1", "maxim4c,link2", "maxim4c,link3"
};

static int maxim4c_remote_dev_info_parse(struct device *dev,
			struct mfd_cell *remote_mfd_dev, u8 link_id)
{
	struct device_node *node = NULL;
	const char *remote_device_name = "serdes-remote-device";
	const char *prop_str = NULL, *link_compat = NULL;
	u32 sub_idx = 0, remote_id = 0;
	int ret = 0;

	node = NULL;
	sub_idx = 0;
	while ((node = of_get_next_child(dev->of_node, node))) {
		if (!strncasecmp(node->name,
					remote_device_name,
					strlen(remote_device_name))) {
			if (sub_idx >= MAXIM4C_LINK_ID_MAX) {
				dev_err(dev, "%pOF: Too many matching %s node\n",
						dev->of_node, remote_device_name);

				of_node_put(node);
				break;
			}

			if (!of_device_is_available(node)) {
				dev_info(dev, "%pOF is disabled\n", node);

				sub_idx++;

				continue;
			}

			/* remote id */
			ret = of_property_read_u32(node, "remote-id", &remote_id);
			if (ret) {
				sub_idx++;

				continue;
			}
			if (remote_id >= MAXIM4C_LINK_ID_MAX) {
				sub_idx++;

				continue;
			}

			if (remote_id != link_id) {
				sub_idx++;

				continue;
			}

			dev_info(dev, "remote device id = %d\n", remote_id);

			ret = of_property_read_string(node, "compatible", &prop_str);
			if (ret) {
				dev_err(dev, "%pOF no compatible error\n", node);

				of_node_put(node);
				return -EINVAL;
			}

			link_compat = maxim4c_remote_link_compat[remote_id];
			if (!strncasecmp(prop_str,
					link_compat, strlen(link_compat))) {
				dev_info(dev, "compatible property: %s\n", prop_str);

				remote_mfd_dev->name = maxim4c_remote_devs_name[remote_id];
				remote_mfd_dev->of_compatible = prop_str;

				of_node_put(node);
				return 0;
			}

			dev_err(dev, "%pOF compatible and remote_id mismatch\n", node);

			of_node_put(node);
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static int maxim4c_remote_mfd_devs_init(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct mfd_cell *remote_mfd_dev = NULL;
	int link_idx = 0, nr_mfd_cell = 0;
	int ret = 0;

	remote_mfd_dev = maxim4c->remote_mfd_devs;
	nr_mfd_cell = 0;
	for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
		remote_mfd_dev->name = NULL;
		remote_mfd_dev->of_compatible = NULL;

		if (gmsl_link->link_cfg[link_idx].link_enable == 0) {
			dev_dbg(dev, "%s: link id = %d is disabled\n",
					__func__, link_idx);
			continue;
		}

		ret = maxim4c_remote_dev_info_parse(dev, remote_mfd_dev, link_idx);
		if (ret == 0) {
			remote_mfd_dev++;
			nr_mfd_cell++;
		}
	}

	dev_info(dev, "Total number of remote devices is %d", nr_mfd_cell);

	return nr_mfd_cell;
}

int maxim4c_remote_mfd_add_devices(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	int nr_mfd_cell = 0, ret = 0;

	dev_info(dev, "=== maxim4c add remote devices ===");

	nr_mfd_cell = maxim4c_remote_mfd_devs_init(maxim4c);
	if (nr_mfd_cell == 0) {
		dev_err(dev, "%s: remote mfd devices init error\n",
				__func__);
		return -EINVAL;
	}

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
			maxim4c->remote_mfd_devs, nr_mfd_cell,
			NULL, 0, NULL);
	if (ret)
		dev_err(dev, "%s: add remote mfd devices error: %d\n",
				__func__, ret);

	return ret;
}
EXPORT_SYMBOL(maxim4c_remote_mfd_add_devices);

int maxim4c_remote_devices_init(maxim4c_t *maxim4c, u8 link_init_mask)
{
	struct device *dev = &maxim4c->client->dev;
	struct maxim4c_remote *remote_device = NULL;
	const struct maxim4c_remote_ops *remote_ops = NULL;
	u8 link_mask = 0, link_enable = 0, link_locked = 0;
	int ret = 0, i = 0;

	dev_dbg(dev, "%s: link init mask = 0x%02x\n", __func__, link_init_mask);

	for (i = 0; i < MAXIM4C_LINK_ID_MAX; i++) {
		if ((link_init_mask & BIT(i)) == 0) {
			dev_dbg(dev, "link id = %d init mask is disabled\n", i);
			continue;
		}

		link_enable = maxim4c->gmsl_link.link_cfg[i].link_enable;
		if (link_enable == 0) {
			dev_info(dev, "link id = %d is disabled\n", i);
			continue;
		}

		remote_device = maxim4c->remote_device[i];
		if (remote_device == NULL) {
			dev_info(dev, "remote device id = %d isn't detected\n", i);
			continue;
		}

		if (remote_device->remote_enable == 0) {
			dev_info(dev, "remote device id = %d isn't enabled\n", i);
			continue;
		}

		remote_ops = remote_device->remote_ops;
		if (remote_ops == NULL) {
			dev_info(dev, "remote device id = %d is no ops\n", i);
			continue;
		}

		link_mask = BIT(i);
		link_locked = maxim4c_link_get_lock_state(maxim4c, link_mask);
		if (link_locked != link_mask) {
			dev_info(dev, "link id = %d is unlocked\n", i);
			continue;
		}

		maxim4c_link_select_remote_control(maxim4c, link_mask);

		if (remote_ops->remote_init)
			ret |= remote_ops->remote_init(remote_device);
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_remote_devices_init);

int maxim4c_remote_devices_deinit(maxim4c_t *maxim4c, u8 link_init_mask)
{
	struct device *dev = &maxim4c->client->dev;
	struct maxim4c_remote *remote_device = NULL;
	const struct maxim4c_remote_ops *remote_ops = NULL;
	u8 link_mask = 0, link_enable = 0, link_locked = 0;
	int ret = 0, i = 0;

	dev_dbg(dev, "%s: link init mask = 0x%02x\n", __func__, link_init_mask);

	for (i = 0; i < MAXIM4C_LINK_ID_MAX; i++) {
		if ((link_init_mask & BIT(i)) == 0) {
			dev_dbg(dev, "link id = %d init mask is disabled\n", i);
			continue;
		}

		link_enable = maxim4c->gmsl_link.link_cfg[i].link_enable;
		if (link_enable == 0) {
			dev_info(dev, "link id = %d is disabled\n", i);
			continue;
		}

		remote_device = maxim4c->remote_device[i];
		if (remote_device == NULL) {
			dev_info(dev, "remote device id = %d isn't detected\n", i);
			continue;
		}

		if (remote_device->remote_enable == 0) {
			dev_info(dev, "remote device id = %d isn't enabled\n", i);
			continue;
		}

		remote_ops = remote_device->remote_ops;
		if (remote_ops == NULL) {
			dev_info(dev, "remote device id = %d is no ops\n", i);
			continue;
		}

		link_mask = BIT(i);
		link_locked = maxim4c_link_get_lock_state(maxim4c, link_mask);
		if (link_locked != link_mask) {
			dev_info(dev, "link id = %d is unlocked\n", i);
			continue;
		}

		maxim4c_link_select_remote_control(maxim4c, link_mask);

		if (remote_ops->remote_deinit)
			ret |= remote_ops->remote_deinit(remote_device);
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_remote_devices_deinit);

int maxim4c_remote_load_init_seq(maxim4c_remote_t *remote_device)
{
	struct device *dev = remote_device->dev;
	struct device_node *node = NULL;
	int ret = 0;

	node = of_get_child_by_name(dev->of_node, "remote-init-sequence");
	if (!IS_ERR_OR_NULL(node)) {
		dev_info(dev, "load remote-init-sequence\n");

		ret = maxim4c_i2c_load_init_seq(dev, node,
					&remote_device->remote_init_seq);

		of_node_put(node);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(maxim4c_remote_load_init_seq);

int maxim4c_remote_i2c_addr_select(maxim4c_remote_t *remote_device, u32 i2c_id)
{
	struct device *dev = remote_device->dev;
	struct i2c_client *client = remote_device->client;

	if (i2c_id == MAXIM4C_I2C_SER_DEF) {
		client->addr = remote_device->ser_i2c_addr_def;
		dev_info(dev, "ser select default i2c addr = 0x%02x\n", client->addr);
	} else if (i2c_id == MAXIM4C_I2C_SER_MAP) {
		client->addr = remote_device->ser_i2c_addr_map;
		dev_info(dev, "ser select mapping i2c addr = 0x%02x\n", client->addr);
	} else {
		dev_err(dev, "i2c select id = %d error\n", i2c_id);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(maxim4c_remote_i2c_addr_select);

int maxim4c_remote_i2c_client_init(maxim4c_remote_t *remote_device,
				struct i2c_client *des_client)
{
	struct device *dev = remote_device->dev;
	struct i2c_client *ser_client = NULL;
	u16 ser_client_addr = 0;

	if (remote_device->ser_i2c_addr_map)
		ser_client_addr = remote_device->ser_i2c_addr_map;
	else
		ser_client_addr = remote_device->ser_i2c_addr_def;
	ser_client = devm_i2c_new_dummy_device(&des_client->dev,
				des_client->adapter, ser_client_addr);
	if (IS_ERR(ser_client)) {
		dev_err(dev, "failed to alloc i2c client.\n");
		return -PTR_ERR(ser_client);
	}
	ser_client->addr = remote_device->ser_i2c_addr_def;

	remote_device->client = ser_client;
	i2c_set_clientdata(ser_client, remote_device);

	dev_info(dev, "remote i2c client init, i2c_addr = 0x%02x\n",
			ser_client_addr);

	return 0;
}
EXPORT_SYMBOL(maxim4c_remote_i2c_client_init);

static int maxim4c_remote_device_chain_check(maxim4c_remote_t *remote_device)
{
	struct device *dev = NULL;
	struct device_node *endpoint = NULL;
	struct device_node *link_node = NULL;
	u8 remote_id, link_id;
	u32 value;
	int ret = 0;

	if (remote_device == NULL) {
		dev_err(dev, "%s: input parameter is error\n", __func__);
		return -EINVAL;
	}

	dev = remote_device->dev;
	remote_id = remote_device->remote_id;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "%s: no endpoint error\n", __func__);
		return -EINVAL;
	}

	link_node = of_graph_get_remote_port_parent(endpoint);
	if (!link_node) {
		dev_err(dev, "%pOF: endpoint has no remote port parent error\n",
				endpoint);
		return -EINVAL;
	}

	ret = of_property_read_u32(link_node, "link-id", &value);
	if (ret) {
		dev_err(dev, "%pOF: no property link_id error\n", link_node);

		of_node_put(link_node);
		return -EINVAL;
	}
	of_node_put(link_node);
	link_id = value;

	if (remote_id != link_id) {
		dev_err(dev, "remote_id (%d) != link_id (%d) of %pOF\n",
				remote_id, link_id, link_node);
		return -EINVAL;
	}

	return 0;
}

int maxim4c_remote_device_register(maxim4c_t *maxim4c,
		maxim4c_remote_t *remote_device)
{
	struct device *dev = NULL;
	u8 remote_id;
	int ret = 0;

	if ((maxim4c == NULL) || (remote_device == NULL)) {
		dev_err(dev, "%s: input parameter is error!\n", __func__);

		return -EINVAL;
	}

	dev = remote_device->dev;
	remote_id = remote_device->remote_id;
	if (remote_id >= MAXIM4C_LINK_ID_MAX) {
		dev_err(dev, "%s: remote_id = %d is error\n",
				__func__, remote_id);

		return -EINVAL;
	}

	if (maxim4c->remote_device[remote_id] != NULL) {
		dev_err(dev, "%s: remote_id = %d is conflict\n",
				__func__, remote_id);

		return -EINVAL;
	}

	ret = maxim4c_remote_device_chain_check(remote_device);
	if (ret) {
		dev_err(dev, "%s: remote device id = %d chain error\n",
				__func__, remote_id);
		return -EINVAL;
	}

	remote_device->remote_enable = 1;
	maxim4c->remote_device[remote_id] = remote_device;

	return 0;
}
EXPORT_SYMBOL(maxim4c_remote_device_register);
