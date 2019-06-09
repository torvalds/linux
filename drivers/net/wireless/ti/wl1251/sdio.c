// SPDX-License-Identifier: GPL-2.0-only
/*
 * wl12xx SDIO routines
 *
 * Copyright (C) 2005 Texas Instruments Incorporated
 * Copyright (C) 2008 Google Inc
 * Copyright (C) 2009 Bob Copeland (me@bobcopeland.com)
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/platform_device.h>
#include <linux/wl12xx.h>
#include <linux/irq.h>
#include <linux/pm_runtime.h>
#include <linux/gpio.h>

#include "wl1251.h"

#ifndef SDIO_VENDOR_ID_TI
#define SDIO_VENDOR_ID_TI		0x104c
#endif

#ifndef SDIO_DEVICE_ID_TI_WL1251
#define SDIO_DEVICE_ID_TI_WL1251	0x9066
#endif

struct wl1251_sdio {
	struct sdio_func *func;
	u32 elp_val;
};

static struct sdio_func *wl_to_func(struct wl1251 *wl)
{
	struct wl1251_sdio *wl_sdio = wl->if_priv;
	return wl_sdio->func;
}

static void wl1251_sdio_interrupt(struct sdio_func *func)
{
	struct wl1251 *wl = sdio_get_drvdata(func);

	wl1251_debug(DEBUG_IRQ, "IRQ");

	/* FIXME should be synchronous for sdio */
	ieee80211_queue_work(wl->hw, &wl->irq_work);
}

static const struct sdio_device_id wl1251_devices[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_TI, SDIO_DEVICE_ID_TI_WL1251) },
	{}
};
MODULE_DEVICE_TABLE(sdio, wl1251_devices);


static void wl1251_sdio_read(struct wl1251 *wl, int addr,
			     void *buf, size_t len)
{
	int ret;
	struct sdio_func *func = wl_to_func(wl);

	sdio_claim_host(func);
	ret = sdio_memcpy_fromio(func, buf, addr, len);
	if (ret)
		wl1251_error("sdio read failed (%d)", ret);
	sdio_release_host(func);
}

static void wl1251_sdio_write(struct wl1251 *wl, int addr,
			      void *buf, size_t len)
{
	int ret;
	struct sdio_func *func = wl_to_func(wl);

	sdio_claim_host(func);
	ret = sdio_memcpy_toio(func, addr, buf, len);
	if (ret)
		wl1251_error("sdio write failed (%d)", ret);
	sdio_release_host(func);
}

static void wl1251_sdio_read_elp(struct wl1251 *wl, int addr, u32 *val)
{
	int ret = 0;
	struct wl1251_sdio *wl_sdio = wl->if_priv;
	struct sdio_func *func = wl_sdio->func;

	/*
	 * The hardware only supports RAW (read after write) access for
	 * reading, regular sdio_readb won't work here (it interprets
	 * the unused bits of CMD52 as write data even if we send read
	 * request).
	 */
	sdio_claim_host(func);
	*val = sdio_writeb_readb(func, wl_sdio->elp_val, addr, &ret);
	sdio_release_host(func);

	if (ret)
		wl1251_error("sdio_readb failed (%d)", ret);
}

static void wl1251_sdio_write_elp(struct wl1251 *wl, int addr, u32 val)
{
	int ret = 0;
	struct wl1251_sdio *wl_sdio = wl->if_priv;
	struct sdio_func *func = wl_sdio->func;

	sdio_claim_host(func);
	sdio_writeb(func, val, addr, &ret);
	sdio_release_host(func);

	if (ret)
		wl1251_error("sdio_writeb failed (%d)", ret);
	else
		wl_sdio->elp_val = val;
}

static void wl1251_sdio_reset(struct wl1251 *wl)
{
}

static void wl1251_sdio_enable_irq(struct wl1251 *wl)
{
	struct sdio_func *func = wl_to_func(wl);

	sdio_claim_host(func);
	sdio_claim_irq(func, wl1251_sdio_interrupt);
	sdio_release_host(func);
}

static void wl1251_sdio_disable_irq(struct wl1251 *wl)
{
	struct sdio_func *func = wl_to_func(wl);

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_release_host(func);
}

/* Interrupts when using dedicated WLAN_IRQ pin */
static irqreturn_t wl1251_line_irq(int irq, void *cookie)
{
	struct wl1251 *wl = cookie;

	ieee80211_queue_work(wl->hw, &wl->irq_work);

	return IRQ_HANDLED;
}

static void wl1251_enable_line_irq(struct wl1251 *wl)
{
	return enable_irq(wl->irq);
}

static void wl1251_disable_line_irq(struct wl1251 *wl)
{
	return disable_irq(wl->irq);
}

static int wl1251_sdio_set_power(struct wl1251 *wl, bool enable)
{
	struct sdio_func *func = wl_to_func(wl);
	int ret;

	if (enable) {
		/*
		 * Power is controlled by runtime PM, but we still call board
		 * callback in case it wants to do any additional setup,
		 * for example enabling clock buffer for the module.
		 */
		if (gpio_is_valid(wl->power_gpio))
			gpio_set_value(wl->power_gpio, true);


		ret = pm_runtime_get_sync(&func->dev);
		if (ret < 0) {
			pm_runtime_put_sync(&func->dev);
			goto out;
		}

		sdio_claim_host(func);
		sdio_enable_func(func);
		sdio_release_host(func);
	} else {
		sdio_claim_host(func);
		sdio_disable_func(func);
		sdio_release_host(func);

		ret = pm_runtime_put_sync(&func->dev);
		if (ret < 0)
			goto out;

		if (gpio_is_valid(wl->power_gpio))
			gpio_set_value(wl->power_gpio, false);
	}

out:
	return ret;
}

static struct wl1251_if_operations wl1251_sdio_ops = {
	.read = wl1251_sdio_read,
	.write = wl1251_sdio_write,
	.write_elp = wl1251_sdio_write_elp,
	.read_elp = wl1251_sdio_read_elp,
	.reset = wl1251_sdio_reset,
	.power = wl1251_sdio_set_power,
};

static int wl1251_sdio_probe(struct sdio_func *func,
			     const struct sdio_device_id *id)
{
	int ret;
	struct wl1251 *wl;
	struct ieee80211_hw *hw;
	struct wl1251_sdio *wl_sdio;
	const struct wl1251_platform_data *wl1251_board_data;

	hw = wl1251_alloc_hw();
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	wl = hw->priv;

	wl_sdio = kzalloc(sizeof(*wl_sdio), GFP_KERNEL);
	if (wl_sdio == NULL) {
		ret = -ENOMEM;
		goto out_free_hw;
	}

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	if (ret)
		goto release;

	sdio_set_block_size(func, 512);
	sdio_release_host(func);

	SET_IEEE80211_DEV(hw, &func->dev);
	wl_sdio->func = func;
	wl->if_priv = wl_sdio;
	wl->if_ops = &wl1251_sdio_ops;

	wl1251_board_data = wl1251_get_platform_data();
	if (!IS_ERR(wl1251_board_data)) {
		wl->power_gpio = wl1251_board_data->power_gpio;
		wl->irq = wl1251_board_data->irq;
		wl->use_eeprom = wl1251_board_data->use_eeprom;
	}

	if (gpio_is_valid(wl->power_gpio)) {
		ret = devm_gpio_request(&func->dev, wl->power_gpio,
								"wl1251 power");
		if (ret) {
			wl1251_error("Failed to request gpio: %d\n", ret);
			goto disable;
		}
	}

	if (wl->irq) {
		irq_set_status_flags(wl->irq, IRQ_NOAUTOEN);
		ret = request_irq(wl->irq, wl1251_line_irq, 0, "wl1251", wl);
		if (ret < 0) {
			wl1251_error("request_irq() failed: %d", ret);
			goto disable;
		}

		irq_set_irq_type(wl->irq, IRQ_TYPE_EDGE_RISING);

		wl1251_sdio_ops.enable_irq = wl1251_enable_line_irq;
		wl1251_sdio_ops.disable_irq = wl1251_disable_line_irq;

		wl1251_info("using dedicated interrupt line");
	} else {
		wl1251_sdio_ops.enable_irq = wl1251_sdio_enable_irq;
		wl1251_sdio_ops.disable_irq = wl1251_sdio_disable_irq;

		wl1251_info("using SDIO interrupt");
	}

	ret = wl1251_init_ieee80211(wl);
	if (ret)
		goto out_free_irq;

	sdio_set_drvdata(func, wl);

	/* Tell PM core that we don't need the card to be powered now */
	pm_runtime_put_noidle(&func->dev);

	return ret;

out_free_irq:
	if (wl->irq)
		free_irq(wl->irq, wl);
disable:
	sdio_claim_host(func);
	sdio_disable_func(func);
release:
	sdio_release_host(func);
	kfree(wl_sdio);
out_free_hw:
	wl1251_free_hw(wl);
	return ret;
}

static void wl1251_sdio_remove(struct sdio_func *func)
{
	struct wl1251 *wl = sdio_get_drvdata(func);
	struct wl1251_sdio *wl_sdio = wl->if_priv;

	/* Undo decrement done above in wl1251_probe */
	pm_runtime_get_noresume(&func->dev);

	if (wl->irq)
		free_irq(wl->irq, wl);
	wl1251_free_hw(wl);
	kfree(wl_sdio);

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);
}

static int wl1251_suspend(struct device *dev)
{
	/*
	 * Tell MMC/SDIO core it's OK to power down the card
	 * (if it isn't already), but not to remove it completely.
	 */
	return 0;
}

static int wl1251_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops wl1251_sdio_pm_ops = {
	.suspend        = wl1251_suspend,
	.resume         = wl1251_resume,
};

static struct sdio_driver wl1251_sdio_driver = {
	.name		= "wl1251_sdio",
	.id_table	= wl1251_devices,
	.probe		= wl1251_sdio_probe,
	.remove		= wl1251_sdio_remove,
	.drv.pm		= &wl1251_sdio_pm_ops,
};

static int __init wl1251_sdio_init(void)
{
	int err;

	err = sdio_register_driver(&wl1251_sdio_driver);
	if (err)
		wl1251_error("failed to register sdio driver: %d", err);
	return err;
}

static void __exit wl1251_sdio_exit(void)
{
	sdio_unregister_driver(&wl1251_sdio_driver);
	wl1251_notice("unloaded");
}

module_init(wl1251_sdio_init);
module_exit(wl1251_sdio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kalle Valo <kvalo@adurom.com>");
