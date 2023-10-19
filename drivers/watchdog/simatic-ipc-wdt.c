// SPDX-License-Identifier: GPL-2.0
/*
 * Siemens SIMATIC IPC driver for Watchdogs
 *
 * Copyright (c) Siemens AG, 2020-2021
 *
 * Authors:
 *  Gerd Haeussler <gerd.haeussler.ext@siemens.com>
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/p2sb.h>
#include <linux/platform_data/x86/simatic-ipc-base.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/util_macros.h>
#include <linux/watchdog.h>

#define WD_ENABLE_IOADR			0x62
#define WD_TRIGGER_IOADR		0x66
#define GPIO_COMMUNITY0_PORT_ID		0xaf
#define PAD_CFG_DW0_GPP_A_23		0x4b8
#define SAFE_EN_N_427E			0x01
#define SAFE_EN_N_227E			0x04
#define WD_ENABLED			0x01
#define WD_TRIGGERED			0x80
#define WD_MACROMODE			0x02

#define TIMEOUT_MIN	2
#define TIMEOUT_DEF	64
#define TIMEOUT_MAX	64

#define GP_STATUS_REG_227E	0x404D	/* IO PORT for SAFE_EN_N on 227E */

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0000);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static struct resource gp_status_reg_227e_res =
	DEFINE_RES_IO_NAMED(GP_STATUS_REG_227E, SZ_1, KBUILD_MODNAME);

static struct resource io_resource_enable =
	DEFINE_RES_IO_NAMED(WD_ENABLE_IOADR, SZ_1,
			    KBUILD_MODNAME " WD_ENABLE_IOADR");

static struct resource io_resource_trigger =
	DEFINE_RES_IO_NAMED(WD_TRIGGER_IOADR, SZ_1,
			    KBUILD_MODNAME " WD_TRIGGER_IOADR");

/* the actual start will be discovered with p2sb, 0 is a placeholder */
static struct resource mem_resource =
	DEFINE_RES_MEM_NAMED(0, 0, "WD_RESET_BASE_ADR");

static u32 wd_timeout_table[] = {2, 4, 6, 8, 16, 32, 48, 64 };
static void __iomem *wd_reset_base_addr;

static int wd_start(struct watchdog_device *wdd)
{
	outb(inb(WD_ENABLE_IOADR) | WD_ENABLED, WD_ENABLE_IOADR);
	return 0;
}

static int wd_stop(struct watchdog_device *wdd)
{
	outb(inb(WD_ENABLE_IOADR) & ~WD_ENABLED, WD_ENABLE_IOADR);
	return 0;
}

static int wd_ping(struct watchdog_device *wdd)
{
	inb(WD_TRIGGER_IOADR);
	return 0;
}

static int wd_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	int timeout_idx = find_closest(t, wd_timeout_table,
				       ARRAY_SIZE(wd_timeout_table));

	outb((inb(WD_ENABLE_IOADR) & 0xc7) | timeout_idx << 3, WD_ENABLE_IOADR);
	wdd->timeout = wd_timeout_table[timeout_idx];
	return 0;
}

static const struct watchdog_info wdt_ident = {
	.options	= WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING |
			  WDIOF_SETTIMEOUT,
	.identity	= KBUILD_MODNAME,
};

static const struct watchdog_ops wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= wd_start,
	.stop		= wd_stop,
	.ping		= wd_ping,
	.set_timeout	= wd_set_timeout,
};

static void wd_secondary_enable(u32 wdtmode)
{
	u16 resetbit;

	/* set safe_en_n so we are not just WDIOF_ALARMONLY */
	if (wdtmode == SIMATIC_IPC_DEVICE_227E) {
		/* enable SAFE_EN_N on GP_STATUS_REG_227E */
		resetbit = inb(GP_STATUS_REG_227E);
		outb(resetbit & ~SAFE_EN_N_227E, GP_STATUS_REG_227E);
	} else {
		/* enable SAFE_EN_N on PCH D1600 */
		resetbit = ioread16(wd_reset_base_addr);
		iowrite16(resetbit & ~SAFE_EN_N_427E, wd_reset_base_addr);
	}
}

static int wd_setup(u32 wdtmode)
{
	unsigned int bootstatus = 0;
	int timeout_idx;

	timeout_idx = find_closest(TIMEOUT_DEF, wd_timeout_table,
				   ARRAY_SIZE(wd_timeout_table));

	if (inb(WD_ENABLE_IOADR) & WD_TRIGGERED)
		bootstatus |= WDIOF_CARDRESET;

	/* reset alarm bit, set macro mode, and set timeout */
	outb(WD_TRIGGERED | WD_MACROMODE | timeout_idx << 3, WD_ENABLE_IOADR);

	wd_secondary_enable(wdtmode);

	return bootstatus;
}

static struct watchdog_device wdd_data = {
	.info = &wdt_ident,
	.ops = &wdt_ops,
	.min_timeout = TIMEOUT_MIN,
	.max_timeout = TIMEOUT_MAX
};

static int simatic_ipc_wdt_probe(struct platform_device *pdev)
{
	struct simatic_ipc_platform *plat = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	switch (plat->devmode) {
	case SIMATIC_IPC_DEVICE_227E:
		if (!devm_request_region(dev, gp_status_reg_227e_res.start,
					 resource_size(&gp_status_reg_227e_res),
					 KBUILD_MODNAME)) {
			dev_err(dev,
				"Unable to register IO resource at %pR\n",
				&gp_status_reg_227e_res);
			return -EBUSY;
		}
		fallthrough;
	case SIMATIC_IPC_DEVICE_427E:
		wdd_data.parent = dev;
		break;
	default:
		return -EINVAL;
	}

	if (!devm_request_region(dev, io_resource_enable.start,
				 resource_size(&io_resource_enable),
				 io_resource_enable.name)) {
		dev_err(dev,
			"Unable to register IO resource at %#x\n",
			WD_ENABLE_IOADR);
		return -EBUSY;
	}

	if (!devm_request_region(dev, io_resource_trigger.start,
				 resource_size(&io_resource_trigger),
				 io_resource_trigger.name)) {
		dev_err(dev,
			"Unable to register IO resource at %#x\n",
			WD_TRIGGER_IOADR);
		return -EBUSY;
	}

	if (plat->devmode == SIMATIC_IPC_DEVICE_427E) {
		res = &mem_resource;

		ret = p2sb_bar(NULL, 0, res);
		if (ret)
			return ret;

		/* do the final address calculation */
		res->start = res->start + (GPIO_COMMUNITY0_PORT_ID << 16) +
			     PAD_CFG_DW0_GPP_A_23;
		res->end = res->start + SZ_4 - 1;

		wd_reset_base_addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(wd_reset_base_addr))
			return PTR_ERR(wd_reset_base_addr);
	}

	wdd_data.bootstatus = wd_setup(plat->devmode);
	if (wdd_data.bootstatus)
		dev_warn(dev, "last reboot caused by watchdog reset\n");

	watchdog_set_nowayout(&wdd_data, nowayout);
	watchdog_stop_on_reboot(&wdd_data);
	return devm_watchdog_register_device(dev, &wdd_data);
}

static struct platform_driver simatic_ipc_wdt_driver = {
	.probe = simatic_ipc_wdt_probe,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};

module_platform_driver(simatic_ipc_wdt_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_AUTHOR("Gerd Haeussler <gerd.haeussler.ext@siemens.com>");
