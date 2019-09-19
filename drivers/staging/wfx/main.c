// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device probe and register.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (c) 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright (c) 2008 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (c) 2007-2009, Christian Lamparter <chunkeey@web.de>
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/mmc/sdio_func.h>
#include <linux/spi/spi.h>
#include <linux/etherdevice.h>

#include "main.h"
#include "wfx.h"
#include "fwio.h"
#include "hwio.h"
#include "bus.h"
#include "bh.h"
#include "wfx_version.h"

MODULE_DESCRIPTION("Silicon Labs 802.11 Wireless LAN driver for WFx");
MODULE_AUTHOR("Jérôme Pouiller <jerome.pouiller@silabs.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(WFX_LABEL);

static int gpio_wakeup = -2;
module_param(gpio_wakeup, int, 0644);
MODULE_PARM_DESC(gpio_wakeup, "gpio number for wakeup. -1 for none.");

bool wfx_api_older_than(struct wfx_dev *wdev, int major, int minor)
{
	if (wdev->hw_caps.api_version_major < major)
		return true;
	if (wdev->hw_caps.api_version_major > major)
		return false;
	if (wdev->hw_caps.api_version_minor < minor)
		return true;
	return false;
}

struct gpio_desc *wfx_get_gpio(struct device *dev, int override, const char *label)
{
	struct gpio_desc *ret;
	char label_buf[256];

	if (override >= 0) {
		snprintf(label_buf, sizeof(label_buf), "wfx_%s", label);
		ret = ERR_PTR(devm_gpio_request_one(dev, override, GPIOF_OUT_INIT_LOW, label_buf));
		if (!ret)
			ret = gpio_to_desc(override);
	} else if (override == -1) {
		ret = NULL;
	} else {
		ret = devm_gpiod_get(dev, label, GPIOD_OUT_LOW);
	}
	if (IS_ERR(ret) || !ret) {
		if (!ret || PTR_ERR(ret) == -ENOENT)
			dev_warn(dev, "gpio %s is not defined\n", label);
		else
			dev_warn(dev, "error while requesting gpio %s\n", label);
		ret = NULL;
	} else {
		dev_dbg(dev, "using gpio %d for %s\n", desc_to_gpio(ret), label);
	}
	return ret;
}

struct wfx_dev *wfx_init_common(struct device *dev,
				const struct wfx_platform_data *pdata,
				const struct hwbus_ops *hwbus_ops,
				void *hwbus_priv)
{
	struct wfx_dev *wdev;

	wdev = devm_kmalloc(dev, sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return NULL;
	wdev->dev = dev;
	wdev->hwbus_ops = hwbus_ops;
	wdev->hwbus_priv = hwbus_priv;
	memcpy(&wdev->pdata, pdata, sizeof(*pdata));
	return wdev;
}

void wfx_free_common(struct wfx_dev *wdev)
{
}

int wfx_probe(struct wfx_dev *wdev)
{
	int err;

	wfx_bh_register(wdev);

	err = wfx_init_device(wdev);
	if (err)
		goto err1;


	return 0;

err1:
	wfx_bh_unregister(wdev);
	return err;
}

void wfx_release(struct wfx_dev *wdev)
{
	wfx_bh_unregister(wdev);
}

static int __init wfx_core_init(void)
{
	int ret = 0;

	pr_info("wfx: Silicon Labs " WFX_LABEL "\n");

	if (IS_ENABLED(CONFIG_SPI))
		ret = spi_register_driver(&wfx_spi_driver);
	if (IS_ENABLED(CONFIG_MMC) && !ret)
		ret = sdio_register_driver(&wfx_sdio_driver);
	return ret;
}
module_init(wfx_core_init);

static void __exit wfx_core_exit(void)
{
	if (IS_ENABLED(CONFIG_MMC))
		sdio_unregister_driver(&wfx_sdio_driver);
	if (IS_ENABLED(CONFIG_SPI))
		spi_unregister_driver(&wfx_spi_driver);
}
module_exit(wfx_core_exit);
