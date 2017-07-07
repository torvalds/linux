/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/gpio.h>
#include <linux/pm_runtime.h>
#include <linux/printk.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include "wlcore.h"
#include "wl12xx_80211.h"
#include "io.h"

#ifndef SDIO_VENDOR_ID_TI
#define SDIO_VENDOR_ID_TI		0x0097
#endif

#ifndef SDIO_DEVICE_ID_TI_WL1271
#define SDIO_DEVICE_ID_TI_WL1271	0x4076
#endif

static bool dump = false;

struct wl12xx_sdio_glue {
	struct device *dev;
	struct platform_device *core;
};

static const struct sdio_device_id wl1271_devices[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_TI, SDIO_DEVICE_ID_TI_WL1271) },
	{}
};
MODULE_DEVICE_TABLE(sdio, wl1271_devices);

static void wl1271_sdio_set_block_size(struct device *child,
				       unsigned int blksz)
{
	struct wl12xx_sdio_glue *glue = dev_get_drvdata(child->parent);
	struct sdio_func *func = dev_to_sdio_func(glue->dev);

	sdio_claim_host(func);
	sdio_set_block_size(func, blksz);
	sdio_release_host(func);
}

static int __must_check wl12xx_sdio_raw_read(struct device *child, int addr,
					     void *buf, size_t len, bool fixed)
{
	int ret;
	struct wl12xx_sdio_glue *glue = dev_get_drvdata(child->parent);
	struct sdio_func *func = dev_to_sdio_func(glue->dev);

	sdio_claim_host(func);

	if (unlikely(addr == HW_ACCESS_ELP_CTRL_REG)) {
		((u8 *)buf)[0] = sdio_f0_readb(func, addr, &ret);
		dev_dbg(child->parent, "sdio read 52 addr 0x%x, byte 0x%02x\n",
			addr, ((u8 *)buf)[0]);
	} else {
		if (fixed)
			ret = sdio_readsb(func, buf, addr, len);
		else
			ret = sdio_memcpy_fromio(func, buf, addr, len);

		dev_dbg(child->parent, "sdio read 53 addr 0x%x, %zu bytes\n",
			addr, len);
	}

	sdio_release_host(func);

	if (WARN_ON(ret))
		dev_err(child->parent, "sdio read failed (%d)\n", ret);

	if (unlikely(dump)) {
		printk(KERN_DEBUG "wlcore_sdio: READ from 0x%04x\n", addr);
		print_hex_dump(KERN_DEBUG, "wlcore_sdio: READ ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buf, len, false);
	}

	return ret;
}

static int __must_check wl12xx_sdio_raw_write(struct device *child, int addr,
					      void *buf, size_t len, bool fixed)
{
	int ret;
	struct wl12xx_sdio_glue *glue = dev_get_drvdata(child->parent);
	struct sdio_func *func = dev_to_sdio_func(glue->dev);

	sdio_claim_host(func);

	if (unlikely(dump)) {
		printk(KERN_DEBUG "wlcore_sdio: WRITE to 0x%04x\n", addr);
		print_hex_dump(KERN_DEBUG, "wlcore_sdio: WRITE ",
				DUMP_PREFIX_OFFSET, 16, 1,
				buf, len, false);
	}

	if (unlikely(addr == HW_ACCESS_ELP_CTRL_REG)) {
		sdio_f0_writeb(func, ((u8 *)buf)[0], addr, &ret);
		dev_dbg(child->parent, "sdio write 52 addr 0x%x, byte 0x%02x\n",
			addr, ((u8 *)buf)[0]);
	} else {
		dev_dbg(child->parent, "sdio write 53 addr 0x%x, %zu bytes\n",
			addr, len);

		if (fixed)
			ret = sdio_writesb(func, addr, buf, len);
		else
			ret = sdio_memcpy_toio(func, addr, buf, len);
	}

	sdio_release_host(func);

	if (WARN_ON(ret))
		dev_err(child->parent, "sdio write failed (%d)\n", ret);

	return ret;
}

static int wl12xx_sdio_power_on(struct wl12xx_sdio_glue *glue)
{
	int ret;
	struct sdio_func *func = dev_to_sdio_func(glue->dev);
	struct mmc_card *card = func->card;

	ret = pm_runtime_get_sync(&card->dev);
	if (ret) {
		/*
		 * Runtime PM might be temporarily disabled, or the device
		 * might have a positive reference counter. Make sure it is
		 * really powered on.
		 */
		ret = mmc_power_restore_host(card->host);
		if (ret < 0) {
			pm_runtime_put_sync(&card->dev);
			goto out;
		}
	}

	sdio_claim_host(func);
	sdio_enable_func(func);
	sdio_release_host(func);

out:
	return ret;
}

static int wl12xx_sdio_power_off(struct wl12xx_sdio_glue *glue)
{
	int ret;
	struct sdio_func *func = dev_to_sdio_func(glue->dev);
	struct mmc_card *card = func->card;

	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	/* Power off the card manually in case it wasn't powered off above */
	ret = mmc_power_save_host(card->host);
	if (ret < 0)
		goto out;

	/* Let runtime PM know the card is powered off */
	pm_runtime_put_sync(&card->dev);

out:
	return ret;
}

static int wl12xx_sdio_set_power(struct device *child, bool enable)
{
	struct wl12xx_sdio_glue *glue = dev_get_drvdata(child->parent);

	if (enable)
		return wl12xx_sdio_power_on(glue);
	else
		return wl12xx_sdio_power_off(glue);
}

static struct wl1271_if_operations sdio_ops = {
	.read		= wl12xx_sdio_raw_read,
	.write		= wl12xx_sdio_raw_write,
	.power		= wl12xx_sdio_set_power,
	.set_block_size = wl1271_sdio_set_block_size,
};

#ifdef CONFIG_OF

static const struct wilink_family_data wl127x_data = {
	.name = "wl127x",
	.nvs_name = "ti-connectivity/wl127x-nvs.bin",
};

static const struct wilink_family_data wl128x_data = {
	.name = "wl128x",
	.nvs_name = "ti-connectivity/wl128x-nvs.bin",
};

static const struct wilink_family_data wl18xx_data = {
	.name = "wl18xx",
	.cfg_name = "ti-connectivity/wl18xx-conf.bin",
};

static const struct of_device_id wlcore_sdio_of_match_table[] = {
	{ .compatible = "ti,wl1271", .data = &wl127x_data },
	{ .compatible = "ti,wl1273", .data = &wl127x_data },
	{ .compatible = "ti,wl1281", .data = &wl128x_data },
	{ .compatible = "ti,wl1283", .data = &wl128x_data },
	{ .compatible = "ti,wl1285", .data = &wl128x_data },
	{ .compatible = "ti,wl1801", .data = &wl18xx_data },
	{ .compatible = "ti,wl1805", .data = &wl18xx_data },
	{ .compatible = "ti,wl1807", .data = &wl18xx_data },
	{ .compatible = "ti,wl1831", .data = &wl18xx_data },
	{ .compatible = "ti,wl1835", .data = &wl18xx_data },
	{ .compatible = "ti,wl1837", .data = &wl18xx_data },
	{ }
};

static int wlcore_probe_of(struct device *dev, int *irq,
			   struct wlcore_platdev_data *pdev_data)
{
	struct device_node *np = dev->of_node;
	const struct of_device_id *of_id;

	of_id = of_match_node(wlcore_sdio_of_match_table, np);
	if (!of_id)
		return -ENODEV;

	pdev_data->family = of_id->data;

	*irq = irq_of_parse_and_map(np, 0);
	if (!*irq) {
		dev_err(dev, "No irq in platform data\n");
		return -EINVAL;
	}

	/* optional clock frequency params */
	of_property_read_u32(np, "ref-clock-frequency",
			     &pdev_data->ref_clock_freq);
	of_property_read_u32(np, "tcxo-clock-frequency",
			     &pdev_data->tcxo_clock_freq);

	return 0;
}
#else
static int wlcore_probe_of(struct device *dev, int *irq,
			   struct wlcore_platdev_data *pdev_data)
{
	return -ENODATA;
}
#endif

static int wl1271_probe(struct sdio_func *func,
				  const struct sdio_device_id *id)
{
	struct wlcore_platdev_data *pdev_data;
	struct wl12xx_sdio_glue *glue;
	struct resource res[1];
	mmc_pm_flag_t mmcflags;
	int ret = -ENOMEM;
	int irq;
	const char *chip_family;

	/* We are only able to handle the wlan function */
	if (func->num != 0x02)
		return -ENODEV;

	pdev_data = devm_kzalloc(&func->dev, sizeof(*pdev_data), GFP_KERNEL);
	if (!pdev_data)
		return -ENOMEM;

	pdev_data->if_ops = &sdio_ops;

	glue = devm_kzalloc(&func->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	glue->dev = &func->dev;

	/* Grab access to FN0 for ELP reg. */
	func->card->quirks |= MMC_QUIRK_LENIENT_FN0;

	/* Use block mode for transferring over one block size of data */
	func->card->quirks |= MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;

	ret = wlcore_probe_of(&func->dev, &irq, pdev_data);
	if (ret)
		goto out;

	/* if sdio can keep power while host is suspended, enable wow */
	mmcflags = sdio_get_host_pm_caps(func);
	dev_dbg(glue->dev, "sdio PM caps = 0x%x\n", mmcflags);

	if (mmcflags & MMC_PM_KEEP_POWER)
		pdev_data->pwr_in_suspend = true;

	sdio_set_drvdata(func, glue);

	/* Tell PM core that we don't need the card to be powered now */
	pm_runtime_put_noidle(&func->dev);

	/*
	 * Due to a hardware bug, we can't differentiate wl18xx from
	 * wl12xx, because both report the same device ID.  The only
	 * way to differentiate is by checking the SDIO revision,
	 * which is 3.00 on the wl18xx chips.
	 */
	if (func->card->cccr.sdio_vsn == SDIO_SDIO_REV_3_00)
		chip_family = "wl18xx";
	else
		chip_family = "wl12xx";

	glue->core = platform_device_alloc(chip_family, PLATFORM_DEVID_AUTO);
	if (!glue->core) {
		dev_err(glue->dev, "can't allocate platform_device");
		ret = -ENOMEM;
		goto out;
	}

	glue->core->dev.parent = &func->dev;

	memset(res, 0x00, sizeof(res));

	res[0].start = irq;
	res[0].flags = IORESOURCE_IRQ |
		       irqd_get_trigger_type(irq_get_irq_data(irq));
	res[0].name = "irq";

	ret = platform_device_add_resources(glue->core, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(glue->dev, "can't add resources\n");
		goto out_dev_put;
	}

	ret = platform_device_add_data(glue->core, pdev_data,
				       sizeof(*pdev_data));
	if (ret) {
		dev_err(glue->dev, "can't add platform data\n");
		goto out_dev_put;
	}

	ret = platform_device_add(glue->core);
	if (ret) {
		dev_err(glue->dev, "can't add platform device\n");
		goto out_dev_put;
	}
	return 0;

out_dev_put:
	platform_device_put(glue->core);

out:
	return ret;
}

static void wl1271_remove(struct sdio_func *func)
{
	struct wl12xx_sdio_glue *glue = sdio_get_drvdata(func);

	/* Undo decrement done above in wl1271_probe */
	pm_runtime_get_noresume(&func->dev);

	platform_device_unregister(glue->core);
}

#ifdef CONFIG_PM
static int wl1271_suspend(struct device *dev)
{
	/* Tell MMC/SDIO core it's OK to power down the card
	 * (if it isn't already), but not to remove it completely */
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct wl12xx_sdio_glue *glue = sdio_get_drvdata(func);
	struct wl1271 *wl = platform_get_drvdata(glue->core);
	mmc_pm_flag_t sdio_flags;
	int ret = 0;

	dev_dbg(dev, "wl1271 suspend. wow_enabled: %d\n",
		wl->wow_enabled);

	/* check whether sdio should keep power */
	if (wl->wow_enabled) {
		sdio_flags = sdio_get_host_pm_caps(func);

		if (!(sdio_flags & MMC_PM_KEEP_POWER)) {
			dev_err(dev, "can't keep power while host "
				     "is suspended\n");
			ret = -EINVAL;
			goto out;
		}

		/* keep power while host suspended */
		ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
		if (ret) {
			dev_err(dev, "error while trying to keep power\n");
			goto out;
		}
	}
out:
	return ret;
}

static int wl1271_resume(struct device *dev)
{
	dev_dbg(dev, "wl1271 resume\n");

	return 0;
}

static const struct dev_pm_ops wl1271_sdio_pm_ops = {
	.suspend	= wl1271_suspend,
	.resume		= wl1271_resume,
};
#endif

static struct sdio_driver wl1271_sdio_driver = {
	.name		= "wl1271_sdio",
	.id_table	= wl1271_devices,
	.probe		= wl1271_probe,
	.remove		= wl1271_remove,
#ifdef CONFIG_PM
	.drv = {
		.pm = &wl1271_sdio_pm_ops,
	},
#endif
};

static int __init wl1271_init(void)
{
	return sdio_register_driver(&wl1271_sdio_driver);
}

static void __exit wl1271_exit(void)
{
	sdio_unregister_driver(&wl1271_sdio_driver);
}

module_init(wl1271_init);
module_exit(wl1271_exit);

module_param(dump, bool, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(dump, "Enable sdio read/write dumps.");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luciano Coelho <coelho@ti.com>");
MODULE_AUTHOR("Juuso Oikarinen <juuso.oikarinen@nokia.com>");
