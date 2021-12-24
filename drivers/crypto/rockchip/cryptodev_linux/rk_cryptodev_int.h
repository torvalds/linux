/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2021 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTODEV_INT_H__
#define __RK_CRYPTODEV_INT_H__

#include <linux/device.h>

#if IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP_DEV)
int rk_cryptodev_register_dev(struct device *dev, const char *name);
int rk_cryptodev_unregister_dev(struct device *dev);
#else
static inline int rk_cryptodev_register_dev(struct device *dev, const char *name)
{
	return 0;
}

static inline int rk_cryptodev_unregister_dev(struct device *dev)
{
	return 0;
}
#endif

#endif
