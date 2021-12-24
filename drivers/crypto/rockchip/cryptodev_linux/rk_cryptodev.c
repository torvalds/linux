// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip crypto
 *
 * Copyright (c) 2021, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */
#include <linux/kernel.h>
#include "rk_cryptodev_int.h"

#define MAX_CRYPTO_DEV		1
#define MAX_CRYPTO_NAME_LEN	64

struct crypto_dev_info {
	struct device *dev;
	char name[MAX_CRYPTO_NAME_LEN];
};

static struct crypto_dev_info g_dev_infos[MAX_CRYPTO_DEV];

/*
 * rk_cryptodev_register_dev - register crypto device into rk_cryptodev.
 * @dev:	[in]	crypto device to register
 * @name:	[in]	crypto device name to register
 */
int rk_cryptodev_register_dev(struct device *dev, const char *name)
{
	uint32_t i;

	if (WARN_ON(!dev))
		return -EINVAL;

	if (WARN_ON(!name))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(g_dev_infos); i++) {
		if (!g_dev_infos[i].dev) {
			memset(&g_dev_infos[i], 0x00, sizeof(g_dev_infos[i]));

			g_dev_infos[i].dev = dev;
			strncpy(g_dev_infos[i].name, name, sizeof(g_dev_infos[i].name));
			dev_info(dev, "register to cryptodev ok!\n");
			return 0;
		}
	}

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(rk_cryptodev_register_dev);

/*
 * rk_cryptodev_unregister_dev - unregister crypto device from rk_cryptodev
 * @dev:	[in]	crypto device to unregister
 */
int rk_cryptodev_unregister_dev(struct device *dev)
{
	uint32_t i;

	if (WARN_ON(!dev))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(g_dev_infos); i++) {
		if (g_dev_infos[i].dev == dev) {
			memset(&g_dev_infos[i], 0x00, sizeof(g_dev_infos[i]));
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(rk_cryptodev_unregister_dev);
