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
#include <linux/mfd/core.h>
#include "maxim4c_api.h"

static struct mfd_cell maxim4c_remote_devs[MAXIM4C_LINK_ID_MAX];

static int maxim4c_remote_mfd_devs_init(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	maxim4c_gmsl_link_t *gmsl_link = &maxim4c->gmsl_link;
	struct maxim4c_link_cfg *link_cfg = NULL;
	struct mfd_cell *remote_mfd_dev = NULL;
	const char *remote_name = NULL, *remote_compatible = NULL;
	int link_idx = 0, nr_mfd_cell = 0;

	remote_mfd_dev = maxim4c_remote_devs;
	nr_mfd_cell = 0;
	for (link_idx = 0; link_idx < MAXIM4C_LINK_ID_MAX; link_idx++) {
		link_cfg = &gmsl_link->link_cfg[link_idx];
		if (link_cfg->link_enable == 0)
			continue;

		remote_name = link_cfg->remote_info.remote_name;
		remote_compatible = link_cfg->remote_info.remote_compatible;
		if (remote_compatible == NULL) {
			dev_err(dev, "%s: link id = %d, remote compatible = NULL",
				__func__, link_idx);
			continue;
		}

		if (remote_name == NULL) {
			dev_err(dev, "%s: link id = %d, remote name = NULL",
				__func__, link_idx);
			continue;
		}

		remote_mfd_dev->name = remote_name;
		remote_mfd_dev->of_compatible = remote_compatible;

		remote_mfd_dev++;
		nr_mfd_cell++;
	}

	return nr_mfd_cell;
}

int maxim4c_remote_mfd_add_devices(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	int nr_mfd_cell = 0, ret = 0;

	nr_mfd_cell = maxim4c_remote_mfd_devs_init(maxim4c);
	if (nr_mfd_cell == 0) {
		dev_err(dev, "%s: remote mfd devices init error\n",
			__func__);
		return -EINVAL;
	}

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
			maxim4c_remote_devs, nr_mfd_cell,
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
		link_enable = maxim4c->gmsl_link.link_cfg[i].link_enable;
		if (link_enable == 0) {
			dev_info(dev, "link id = %d is disabled\n", i);
			continue;
		}

		if ((link_init_mask & BIT(i)) == 0) {
			dev_info(dev, "link id = %d init mask is disabled\n", i);
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
		link_enable = maxim4c->gmsl_link.link_cfg[i].link_enable;
		if (link_enable == 0) {
			dev_info(dev, "link id = %d is disabled\n", i);
			continue;
		}

		if ((link_init_mask & BIT(i)) == 0) {
			dev_info(dev, "link id = %d init mask is disabled\n", i);
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
		ret = maxim4c_i2c_load_init_seq(dev, node,
					&remote_device->remote_init_seq);

		of_node_put(node);

	} else {
		ret = 0;
		dev_info(dev, "no node remote-init-sequence\n");
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_remote_load_init_seq);

int maxim4c_remote_device_register(maxim4c_t *maxim4c,
		maxim4c_remote_t *remote_device)
{
	struct device *dev = NULL;
	u8 remote_id;

	if ((maxim4c == NULL) || (remote_device == NULL)) {
		dev_err(dev, "%s: input parameter is error!\n",
			__func__);

		return -EINVAL;
	}

	dev = remote_device->dev;
	remote_id = remote_device->remote_id;
	if (remote_id < MAXIM4C_LINK_ID_MAX) {
		if (maxim4c->remote_device[remote_id] == NULL) {
			remote_device->remote_enable = 1;
			maxim4c->remote_device[remote_id] = remote_device;
			dev_dbg(dev, "%s: remote_id = %d is success\n",
				__func__, remote_id);

			return 0;
		} else {
			dev_err(dev, "%s: remote_id = %d is conflict\n",
				__func__, remote_id);

			return -EINVAL;
		}
	} else {
		dev_err(dev, "%s: remote_id = %d is error\n",
			__func__, remote_id);

		return -EINVAL;
	}
}
EXPORT_SYMBOL(maxim4c_remote_device_register);
