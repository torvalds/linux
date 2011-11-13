/*
 * SDIO testing driver for wl12xx
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Roger Quadros <roger.quadros@nokia.com>
 *
 * wl12xx read/write routines taken from the main module
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
#include <linux/crc7.h>
#include <linux/vmalloc.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/gpio.h>
#include <linux/wl12xx.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>

#include "wl12xx.h"
#include "io.h"
#include "boot.h"

#ifndef SDIO_VENDOR_ID_TI
#define SDIO_VENDOR_ID_TI		0x0097
#endif

#ifndef SDIO_DEVICE_ID_TI_WL1271
#define SDIO_DEVICE_ID_TI_WL1271	0x4076
#endif

static bool rx, tx;

module_param(rx, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(rx, "Perform rx test. Default (0). "
	"This test continuously reads data from the SDIO device.\n");

module_param(tx, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(tx, "Perform tx test. Default (0). "
	"This test continuously writes data to the SDIO device.\n");

struct wl1271_test {
	struct wl1271 wl;
	struct task_struct *test_task;
};

static const struct sdio_device_id wl1271_devices[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_TI, SDIO_DEVICE_ID_TI_WL1271) },
	{}
};

static inline struct sdio_func *wl_to_func(struct wl1271 *wl)
{
	return wl->if_priv;
}

static struct device *wl1271_sdio_wl_to_dev(struct wl1271 *wl)
{
	return &(wl_to_func(wl)->dev);
}

static void wl1271_sdio_raw_read(struct wl1271 *wl, int addr, void *buf,
		size_t len, bool fixed)
{
	int ret = 0;
	struct sdio_func *func = wl_to_func(wl);

	if (unlikely(addr == HW_ACCESS_ELP_CTRL_REG_ADDR)) {
		((u8 *)buf)[0] = sdio_f0_readb(func, addr, &ret);
		wl1271_debug(DEBUG_SDIO, "sdio read 52 addr 0x%x, byte 0x%02x",
				addr, ((u8 *)buf)[0]);
	} else {
		if (fixed)
			ret = sdio_readsb(func, buf, addr, len);
		else
			ret = sdio_memcpy_fromio(func, buf, addr, len);

		wl1271_debug(DEBUG_SDIO, "sdio read 53 addr 0x%x, %zu bytes",
				addr, len);
		wl1271_dump_ascii(DEBUG_SDIO, "data: ", buf, len);
	}

	if (ret)
		wl1271_error("sdio read failed (%d)", ret);
}

static void wl1271_sdio_raw_write(struct wl1271 *wl, int addr, void *buf,
		size_t len, bool fixed)
{
	int ret = 0;
	struct sdio_func *func = wl_to_func(wl);

	if (unlikely(addr == HW_ACCESS_ELP_CTRL_REG_ADDR)) {
		sdio_f0_writeb(func, ((u8 *)buf)[0], addr, &ret);
		wl1271_debug(DEBUG_SDIO, "sdio write 52 addr 0x%x, byte 0x%02x",
				addr, ((u8 *)buf)[0]);
	} else {
		wl1271_debug(DEBUG_SDIO, "sdio write 53 addr 0x%x, %zu bytes",
				addr, len);
		wl1271_dump_ascii(DEBUG_SDIO, "data: ", buf, len);

		if (fixed)
			ret = sdio_writesb(func, addr, buf, len);
		else
			ret = sdio_memcpy_toio(func, addr, buf, len);
	}
	if (ret)
		wl1271_error("sdio write failed (%d)", ret);

}

static int wl1271_sdio_set_power(struct wl1271 *wl, bool enable)
{
	struct sdio_func *func = wl_to_func(wl);
	int ret;

	/* Let the SDIO stack handle wlan_enable control, so we
	 * keep host claimed while wlan is in use to keep wl1271
	 * alive.
	 */
	if (enable) {
		/* Power up the card */
		ret = pm_runtime_get_sync(&func->dev);
		if (ret < 0)
			goto out;

		/* Runtime PM might be disabled, power up the card manually */
		ret = mmc_power_restore_host(func->card->host);
		if (ret < 0)
			goto out;

		sdio_claim_host(func);
		sdio_enable_func(func);
	} else {
		sdio_disable_func(func);
		sdio_release_host(func);

		/* Runtime PM might be disabled, power off the card manually */
		ret = mmc_power_save_host(func->card->host);
		if (ret < 0)
			goto out;

		/* Power down the card */
		ret = pm_runtime_put_sync(&func->dev);
	}

out:
	return ret;
}

static void wl1271_sdio_disable_interrupts(struct wl1271 *wl)
{
}

static void wl1271_sdio_enable_interrupts(struct wl1271 *wl)
{
}


static struct wl1271_if_operations sdio_ops = {
	.read		= wl1271_sdio_raw_read,
	.write		= wl1271_sdio_raw_write,
	.power		= wl1271_sdio_set_power,
	.dev		= wl1271_sdio_wl_to_dev,
	.enable_irq	= wl1271_sdio_enable_interrupts,
	.disable_irq	= wl1271_sdio_disable_interrupts,
};

static void wl1271_fw_wakeup(struct wl1271 *wl)
{
	u32 elp_reg;

	elp_reg = ELPCTRL_WAKE_UP;
	wl1271_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, elp_reg);
}

static int wl1271_fetch_firmware(struct wl1271 *wl)
{
	const struct firmware *fw;
	int ret;

	if (wl->chip.id == CHIP_ID_1283_PG20)
		ret = request_firmware(&fw, WL128X_FW_NAME,
				       wl1271_wl_to_dev(wl));
	else
		ret = request_firmware(&fw, WL127X_FW_NAME,
				       wl1271_wl_to_dev(wl));

	if (ret < 0) {
		wl1271_error("could not get firmware: %d", ret);
		return ret;
	}

	if (fw->size % 4) {
		wl1271_error("firmware size is not multiple of 32 bits: %zu",
				fw->size);
		ret = -EILSEQ;
		goto out;
	}

	wl->fw_len = fw->size;
	wl->fw = vmalloc(wl->fw_len);

	if (!wl->fw) {
		wl1271_error("could not allocate memory for the firmware");
		ret = -ENOMEM;
		goto out;
	}

	memcpy(wl->fw, fw->data, wl->fw_len);

	ret = 0;

out:
	release_firmware(fw);

	return ret;
}

static int wl1271_fetch_nvs(struct wl1271 *wl)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, WL12XX_NVS_NAME, wl1271_wl_to_dev(wl));

	if (ret < 0) {
		wl1271_error("could not get nvs file: %d", ret);
		return ret;
	}

	wl->nvs = kmemdup(fw->data, fw->size, GFP_KERNEL);

	if (!wl->nvs) {
		wl1271_error("could not allocate memory for the nvs file");
		ret = -ENOMEM;
		goto out;
	}

	wl->nvs_len = fw->size;

out:
	release_firmware(fw);

	return ret;
}

static int wl1271_chip_wakeup(struct wl1271 *wl)
{
	struct wl1271_partition_set partition;
	int ret;

	msleep(WL1271_PRE_POWER_ON_SLEEP);
	ret = wl1271_power_on(wl);
	if (ret)
		return ret;

	msleep(WL1271_POWER_ON_SLEEP);

	/* We don't need a real memory partition here, because we only want
	 * to use the registers at this point. */
	memset(&partition, 0, sizeof(partition));
	partition.reg.start = REGISTERS_BASE;
	partition.reg.size = REGISTERS_DOWN_SIZE;
	wl1271_set_partition(wl, &partition);

	/* ELP module wake up */
	wl1271_fw_wakeup(wl);

	/* whal_FwCtrl_BootSm() */

	/* 0. read chip id from CHIP_ID */
	wl->chip.id = wl1271_read32(wl, CHIP_ID_B);

	/* 1. check if chip id is valid */

	switch (wl->chip.id) {
	case CHIP_ID_1271_PG10:
		wl1271_warning("chip id 0x%x (1271 PG10) support is obsolete",
				wl->chip.id);
		break;
	case CHIP_ID_1271_PG20:
		wl1271_notice("chip id 0x%x (1271 PG20)",
				wl->chip.id);
		break;
	case CHIP_ID_1283_PG20:
		wl1271_notice("chip id 0x%x (1283 PG20)",
				wl->chip.id);
		break;
	case CHIP_ID_1283_PG10:
	default:
		wl1271_warning("unsupported chip id: 0x%x", wl->chip.id);
		return -ENODEV;
	}

	return ret;
}

static struct wl1271_partition_set part_down = {
	.mem = {
		.start = 0x00000000,
		.size  = 0x000177c0
	},
	.reg = {
		.start = REGISTERS_BASE,
		.size  = 0x00008800
	},
	.mem2 = {
		.start = 0x00000000,
		.size  = 0x00000000
	},
	.mem3 = {
		.start = 0x00000000,
		.size  = 0x00000000
	},
};

static int tester(void *data)
{
	struct wl1271 *wl = data;
	struct sdio_func *func = wl_to_func(wl);
	struct device *pdev = &func->dev;
	int ret = 0;
	bool rx_started = 0;
	bool tx_started = 0;
	uint8_t *tx_buf, *rx_buf;
	int test_size = PAGE_SIZE;
	u32 addr = 0;
	struct wl1271_partition_set partition;

	/* We assume chip is powered up and firmware fetched */

	memcpy(&partition, &part_down, sizeof(partition));
	partition.mem.start = addr;
	wl1271_set_partition(wl, &partition);

	tx_buf = kmalloc(test_size, GFP_KERNEL);
	rx_buf = kmalloc(test_size, GFP_KERNEL);
	if (!tx_buf || !rx_buf) {
		dev_err(pdev,
			"Could not allocate memory. Test will not run.\n");
		ret = -ENOMEM;
		goto free;
	}

	memset(tx_buf, 0x5a, test_size);

	/* write something in data area so we can read it back */
	wl1271_write(wl, addr, tx_buf, test_size, false);

	while (!kthread_should_stop()) {
		if (rx && !rx_started) {
			dev_info(pdev, "starting rx test\n");
			rx_started = 1;
		} else if (!rx && rx_started) {
			dev_info(pdev, "stopping rx test\n");
			rx_started = 0;
		}

		if (tx && !tx_started) {
			dev_info(pdev, "starting tx test\n");
			tx_started = 1;
		} else if (!tx && tx_started) {
			dev_info(pdev, "stopping tx test\n");
			tx_started = 0;
		}

		if (rx_started)
			wl1271_read(wl, addr, rx_buf, test_size, false);

		if (tx_started)
			wl1271_write(wl, addr, tx_buf, test_size, false);

		if (!rx_started && !tx_started)
			msleep(100);
	}

free:
	kfree(tx_buf);
	kfree(rx_buf);
	return ret;
}

static int __devinit wl1271_probe(struct sdio_func *func,
		const struct sdio_device_id *id)
{
	const struct wl12xx_platform_data *wlan_data;
	struct wl1271 *wl;
	struct wl1271_test *wl_test;
	int ret = 0;

	/* wl1271 has 2 sdio functions we handle just the wlan part */
	if (func->num != 0x02)
		return -ENODEV;

	wl_test = kzalloc(sizeof(struct wl1271_test), GFP_KERNEL);
	if (!wl_test) {
		dev_err(&func->dev, "Could not allocate memory\n");
		return -ENOMEM;
	}

	wl = &wl_test->wl;

	wl->if_priv = func;
	wl->if_ops = &sdio_ops;

	/* Grab access to FN0 for ELP reg. */
	func->card->quirks |= MMC_QUIRK_LENIENT_FN0;

	/* Use block mode for transferring over one block size of data */
	func->card->quirks |= MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;

	wlan_data = wl12xx_get_platform_data();
	if (IS_ERR(wlan_data)) {
		ret = PTR_ERR(wlan_data);
		dev_err(&func->dev, "missing wlan platform data: %d\n", ret);
		goto out_free;
	}

	wl->irq = wlan_data->irq;
	wl->ref_clock = wlan_data->board_ref_clock;
	wl->tcxo_clock = wlan_data->board_tcxo_clock;

	sdio_set_drvdata(func, wl_test);

	/* power up the device */
	ret = wl1271_chip_wakeup(wl);
	if (ret) {
		dev_err(&func->dev, "could not wake up chip\n");
		goto out_free;
	}

	if (wl->fw == NULL) {
		ret = wl1271_fetch_firmware(wl);
		if (ret < 0) {
			dev_err(&func->dev, "firmware fetch error\n");
			goto out_off;
		}
	}

	/* fetch NVS */
	if (wl->nvs == NULL) {
		ret = wl1271_fetch_nvs(wl);
		if (ret < 0) {
			dev_err(&func->dev, "NVS fetch error\n");
			goto out_off;
		}
	}

	ret = wl1271_load_firmware(wl);
	if (ret < 0) {
		dev_err(&func->dev, "firmware load error: %d\n", ret);
		goto out_free;
	}

	dev_info(&func->dev, "initialized\n");

	/* I/O testing will be done in the tester thread */

	wl_test->test_task = kthread_run(tester, wl, "sdio_tester");
	if (IS_ERR(wl_test->test_task)) {
		dev_err(&func->dev, "unable to create kernel thread\n");
		ret = PTR_ERR(wl_test->test_task);
		goto out_free;
	}

	return 0;

out_off:
	/* power off the chip */
	wl1271_power_off(wl);

out_free:
	kfree(wl_test);
	return ret;
}

static void __devexit wl1271_remove(struct sdio_func *func)
{
	struct wl1271_test *wl_test = sdio_get_drvdata(func);

	/* stop the I/O test thread */
	kthread_stop(wl_test->test_task);

	/* power off the chip */
	wl1271_power_off(&wl_test->wl);

	vfree(wl_test->wl.fw);
	wl_test->wl.fw = NULL;
	kfree(wl_test->wl.nvs);
	wl_test->wl.nvs = NULL;

	kfree(wl_test);
}

static struct sdio_driver wl1271_sdio_driver = {
	.name		= "wl12xx_sdio_test",
	.id_table	= wl1271_devices,
	.probe		= wl1271_probe,
	.remove		= __devexit_p(wl1271_remove),
};

static int __init wl1271_init(void)
{
	int ret;

	ret = sdio_register_driver(&wl1271_sdio_driver);
	if (ret < 0)
		pr_err("failed to register sdio driver: %d\n", ret);

	return ret;
}
module_init(wl1271_init);

static void __exit wl1271_exit(void)
{
	sdio_unregister_driver(&wl1271_sdio_driver);
}
module_exit(wl1271_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roger Quadros <roger.quadros@nokia.com>");

