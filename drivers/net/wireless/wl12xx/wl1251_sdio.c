/*
 * wl12xx SDIO routines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
 * Copyright (C) 2005 Texas Instruments Incorporated
 * Copyright (C) 2008 Google Inc
 * Copyright (C) 2009 Bob Copeland (me@bobcopeland.com)
 */
#include <linux/module.h>
#include <linux/crc7.h>
#include <linux/mod_devicetable.h>
#include <linux/irq.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/platform_device.h>

#include "wl1251.h"
#include "wl12xx_80211.h"
#include "wl1251_reg.h"
#include "wl1251_ps.h"
#include "wl1251_io.h"
#include "wl1251_tx.h"
#include "wl1251_debugfs.h"

#ifndef SDIO_VENDOR_ID_TI
#define SDIO_VENDOR_ID_TI		0x104c
#endif

#ifndef SDIO_DEVICE_ID_TI_WL1251
#define SDIO_DEVICE_ID_TI_WL1251	0x9066
#endif

static struct sdio_func *wl_to_func(struct wl1251 *wl)
{
	return wl->if_priv;
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


void wl1251_sdio_read(struct wl1251 *wl, int addr, void *buf, size_t len)
{
	int ret;
	struct sdio_func *func = wl_to_func(wl);

	sdio_claim_host(func);
	ret = sdio_memcpy_fromio(func, buf, addr, len);
	if (ret)
		wl1251_error("sdio read failed (%d)", ret);
	sdio_release_host(func);
}

void wl1251_sdio_write(struct wl1251 *wl, int addr, void *buf, size_t len)
{
	int ret;
	struct sdio_func *func = wl_to_func(wl);

	sdio_claim_host(func);
	ret = sdio_memcpy_toio(func, addr, buf, len);
	if (ret)
		wl1251_error("sdio write failed (%d)", ret);
	sdio_release_host(func);
}

void wl1251_sdio_reset(struct wl1251 *wl)
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

void wl1251_sdio_set_power(bool enable)
{
}

struct wl1251_if_operations wl1251_sdio_ops = {
	.read = wl1251_sdio_read,
	.write = wl1251_sdio_write,
	.reset = wl1251_sdio_reset,
	.enable_irq = wl1251_sdio_enable_irq,
	.disable_irq = wl1251_sdio_disable_irq,
};

int wl1251_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	int ret;
	struct wl1251 *wl;
	struct ieee80211_hw *hw;

	hw = wl1251_alloc_hw();
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	wl = hw->priv;

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	if (ret)
		goto release;

	sdio_set_block_size(func, 512);

	SET_IEEE80211_DEV(hw, &func->dev);
	wl->if_priv = func;
	wl->if_ops = &wl1251_sdio_ops;
	wl->set_power = wl1251_sdio_set_power;

	sdio_release_host(func);
	ret = wl1251_init_ieee80211(wl);
	if (ret)
		goto disable;

	sdio_set_drvdata(func, wl);
	return ret;

disable:
	sdio_claim_host(func);
	sdio_disable_func(func);
release:
	sdio_release_host(func);
	return ret;
}

static void __devexit wl1251_sdio_remove(struct sdio_func *func)
{
	struct wl1251 *wl = sdio_get_drvdata(func);

	wl1251_free_hw(wl);

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);
}

static struct sdio_driver wl1251_sdio_driver = {
	.name		= "wl1251_sdio",
	.id_table	= wl1251_devices,
	.probe		= wl1251_sdio_probe,
	.remove		= __devexit_p(wl1251_sdio_remove),
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
MODULE_AUTHOR("Kalle Valo <kalle.valo@nokia.com>");
