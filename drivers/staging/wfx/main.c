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
#include <linux/of_net.h>
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
#include "sta.h"
#include "debug.h"
#include "secure_link.h"
#include "hif_api_cmd.h"
#include "wfx_version.h"

MODULE_DESCRIPTION("Silicon Labs 802.11 Wireless LAN driver for WFx");
MODULE_AUTHOR("Jérôme Pouiller <jerome.pouiller@silabs.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(WFX_LABEL);

static int gpio_wakeup = -2;
module_param(gpio_wakeup, int, 0644);
MODULE_PARM_DESC(gpio_wakeup, "gpio number for wakeup. -1 for none.");

static char *slk_key;
module_param(slk_key, charp, 0600);
MODULE_PARM_DESC(slk_key, "secret key for secure link (expect 64 hexdecimal digits).");

static const struct ieee80211_ops wfx_ops = {
	.start			= wfx_start,
	.stop			= wfx_stop,
	.add_interface		= wfx_add_interface,
	.remove_interface	= wfx_remove_interface,
};

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

static void wfx_fill_sl_key(struct device *dev, struct wfx_platform_data *pdata)
{
	const char *ascii_key = NULL;
	int ret = 0;

	if (slk_key)
		ascii_key = slk_key;
	if (!ascii_key)
		ret = of_property_read_string(dev->of_node, "slk_key", &ascii_key);
	if (ret == -EILSEQ || ret == -ENODATA)
		dev_err(dev, "ignoring malformatted key from DT\n");
	if (!ascii_key)
		return;

	ret = hex2bin(pdata->slk_key, ascii_key, sizeof(pdata->slk_key));
	if (ret) {
		dev_err(dev, "ignoring malformatted key: %s\n", ascii_key);
		memset(pdata->slk_key, 0, sizeof(pdata->slk_key));
		return;
	}
	dev_err(dev, "secure link is not supported by this driver, ignoring provided key\n");
}

struct wfx_dev *wfx_init_common(struct device *dev,
				const struct wfx_platform_data *pdata,
				const struct hwbus_ops *hwbus_ops,
				void *hwbus_priv)
{
	struct ieee80211_hw *hw;
	struct wfx_dev *wdev;

	hw = ieee80211_alloc_hw(sizeof(struct wfx_dev), &wfx_ops);
	if (!hw)
		return NULL;

	SET_IEEE80211_DEV(hw, dev);

	hw->vif_data_size = sizeof(struct wfx_vif);
	hw->sta_data_size = sizeof(struct wfx_sta_priv);
	hw->queues = 4;
	hw->max_rates = 8;
	hw->max_rate_tries = 15;
	hw->extra_tx_headroom = sizeof(struct hif_sl_msg_hdr) + sizeof(struct hif_msg)
				+ sizeof(struct hif_req_tx)
				+ 4 /* alignment */ + 8 /* TKIP IV */;

	wdev = hw->priv;
	wdev->hw = hw;
	wdev->dev = dev;
	wdev->hwbus_ops = hwbus_ops;
	wdev->hwbus_priv = hwbus_priv;
	memcpy(&wdev->pdata, pdata, sizeof(*pdata));
	wfx_fill_sl_key(dev, &wdev->pdata);

	init_completion(&wdev->firmware_ready);
	wfx_init_hif_cmd(&wdev->hif_cmd);

	return wdev;
}

void wfx_free_common(struct wfx_dev *wdev)
{
	ieee80211_free_hw(wdev->hw);
}

int wfx_probe(struct wfx_dev *wdev)
{
	int i;
	int err;
	const void *macaddr;

	wfx_bh_register(wdev);

	err = wfx_init_device(wdev);
	if (err)
		goto err1;

	err = wait_for_completion_interruptible_timeout(&wdev->firmware_ready, 10 * HZ);
	if (err <= 0) {
		if (err == 0) {
			dev_err(wdev->dev, "timeout while waiting for startup indication. IRQ configuration error?\n");
			err = -ETIMEDOUT;
		} else if (err == -ERESTARTSYS) {
			dev_info(wdev->dev, "probe interrupted by user\n");
		}
		goto err1;
	}

	// FIXME: fill wiphy::hw_version
	dev_info(wdev->dev, "started firmware %d.%d.%d \"%s\" (API: %d.%d, keyset: %02X, caps: 0x%.8X)\n",
		 wdev->hw_caps.firmware_major, wdev->hw_caps.firmware_minor,
		 wdev->hw_caps.firmware_build, wdev->hw_caps.firmware_label,
		 wdev->hw_caps.api_version_major, wdev->hw_caps.api_version_minor,
		 wdev->keyset, *((u32 *) &wdev->hw_caps.capabilities));
	snprintf(wdev->hw->wiphy->fw_version, sizeof(wdev->hw->wiphy->fw_version),
		 "%d.%d.%d",
		 wdev->hw_caps.firmware_major,
		 wdev->hw_caps.firmware_minor,
		 wdev->hw_caps.firmware_build);

	if (wfx_api_older_than(wdev, 1, 0)) {
		dev_err(wdev->dev, "unsupported firmware API version (expect 1 while firmware returns %d)\n",
			wdev->hw_caps.api_version_major);
		err = -ENOTSUPP;
		goto err1;
	}

	err = wfx_sl_init(wdev);
	if (err && wdev->hw_caps.capabilities.link_mode == SEC_LINK_ENFORCED) {
		dev_err(wdev->dev, "chip require secure_link, but can't negociate it\n");
		goto err1;
	}

	for (i = 0; i < ARRAY_SIZE(wdev->addresses); i++) {
		eth_zero_addr(wdev->addresses[i].addr);
		macaddr = of_get_mac_address(wdev->dev->of_node);
		if (macaddr) {
			ether_addr_copy(wdev->addresses[i].addr, macaddr);
			wdev->addresses[i].addr[ETH_ALEN - 1] += i;
		}
		ether_addr_copy(wdev->addresses[i].addr, wdev->hw_caps.mac_addr[i]);
		if (!is_valid_ether_addr(wdev->addresses[i].addr)) {
			dev_warn(wdev->dev, "using random MAC address\n");
			eth_random_addr(wdev->addresses[i].addr);
		}
		dev_info(wdev->dev, "MAC address %d: %pM\n", i, wdev->addresses[i].addr);
	}

	err = wfx_debug_init(wdev);
	if (err)
		goto err2;

	return 0;

err2:
	ieee80211_free_hw(wdev->hw);
err1:
	wfx_bh_unregister(wdev);
	return err;
}

void wfx_release(struct wfx_dev *wdev)
{
	wfx_bh_unregister(wdev);
	wfx_sl_deinit(wdev);
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
