// SPDX-License-Identifier: GPL-2.0-only
/*
 * VIA Chipset Watchdog Driver
 *
 * Copyright (C) 2011 Sigfox
 * Author: Marc Vertes <marc.vertes@sigfox.com>
 * Based on a preliminary version from Harald Welte <HaraldWelte@viatech.com>
 * Timer code by Wim Van Sebroeck <wim@iguana.be>
 *
 * Caveat: PnP must be enabled in BIOS to allow full access to watchdog
 * control registers. If not, the watchdog must be configured in BIOS manually.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/watchdog.h>

/* Configuration registers relative to the pci device */
#define VIA_WDT_MMIO_BASE	0xe8	/* MMIO region base address */
#define VIA_WDT_CONF		0xec	/* watchdog enable state */

/* Relevant bits for the VIA_WDT_CONF register */
#define VIA_WDT_CONF_ENABLE	0x01	/* 1: enable watchdog */
#define VIA_WDT_CONF_MMIO	0x02	/* 1: enable watchdog MMIO */

/*
 * The MMIO region contains the watchdog control register and the
 * hardware timer counter.
 */
#define VIA_WDT_MMIO_LEN	8	/* MMIO region length in bytes */
#define VIA_WDT_CTL		0	/* MMIO addr+0: state/control reg. */
#define VIA_WDT_COUNT		4	/* MMIO addr+4: timer counter reg. */

/* Bits for the VIA_WDT_CTL register */
#define VIA_WDT_RUNNING		0x01	/* 0: stop, 1: running */
#define VIA_WDT_FIRED		0x02	/* 1: restarted by expired watchdog */
#define VIA_WDT_PWROFF		0x04	/* 0: reset, 1: poweroff */
#define VIA_WDT_DISABLED	0x08	/* 1: timer is disabled */
#define VIA_WDT_TRIGGER		0x80	/* 1: start a new countdown */

/* Hardware heartbeat in seconds */
#define WDT_HW_HEARTBEAT 1

/* Timer heartbeat (500ms) */
#define WDT_HEARTBEAT	(HZ/2)	/* should be <= ((WDT_HW_HEARTBEAT*HZ)/2) */

/* User space timeout in seconds */
#define WDT_TIMEOUT_MAX	1023	/* approx. 17 min. */
#define WDT_TIMEOUT	60
static int timeout = WDT_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds, between 1 and 1023 "
	"(default = " __MODULE_STRING(WDT_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default = " __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static struct watchdog_device wdt_dev;
static struct resource wdt_res;
static void __iomem *wdt_mem;
static unsigned int mmio;
static void wdt_timer_tick(struct timer_list *unused);
static DEFINE_TIMER(timer, wdt_timer_tick);
					/* The timer that pings the watchdog */
static unsigned long next_heartbeat;	/* the next_heartbeat for the timer */

static inline void wdt_reset(void)
{
	unsigned int ctl = readl(wdt_mem);

	writel(ctl | VIA_WDT_TRIGGER, wdt_mem);
}

/*
 * Timer tick: the timer will make sure that the watchdog timer hardware
 * is being reset in time. The conditions to do this are:
 *  1) the watchdog timer has been started and /dev/watchdog is open
 *     and there is still time left before userspace should send the
 *     next heartbeat/ping. (note: the internal heartbeat is much smaller
 *     then the external/userspace heartbeat).
 *  2) the watchdog timer has been stopped by userspace.
 */
static void wdt_timer_tick(struct timer_list *unused)
{
	if (time_before(jiffies, next_heartbeat) ||
	   (!watchdog_active(&wdt_dev))) {
		wdt_reset();
		mod_timer(&timer, jiffies + WDT_HEARTBEAT);
	} else
		pr_crit("I will reboot your machine !\n");
}

static int wdt_ping(struct watchdog_device *wdd)
{
	/* calculate when the next userspace timeout will be */
	next_heartbeat = jiffies + wdd->timeout * HZ;
	return 0;
}

static int wdt_start(struct watchdog_device *wdd)
{
	unsigned int ctl = readl(wdt_mem);

	writel(wdd->timeout, wdt_mem + VIA_WDT_COUNT);
	writel(ctl | VIA_WDT_RUNNING | VIA_WDT_TRIGGER, wdt_mem);
	wdt_ping(wdd);
	mod_timer(&timer, jiffies + WDT_HEARTBEAT);
	return 0;
}

static int wdt_stop(struct watchdog_device *wdd)
{
	unsigned int ctl = readl(wdt_mem);

	writel(ctl & ~VIA_WDT_RUNNING, wdt_mem);
	return 0;
}

static int wdt_set_timeout(struct watchdog_device *wdd,
			   unsigned int new_timeout)
{
	writel(new_timeout, wdt_mem + VIA_WDT_COUNT);
	wdd->timeout = new_timeout;
	return 0;
}

static const struct watchdog_info wdt_info = {
	.identity =	"VIA watchdog",
	.options =	WDIOF_CARDRESET |
			WDIOF_SETTIMEOUT |
			WDIOF_MAGICCLOSE |
			WDIOF_KEEPALIVEPING,
};

static const struct watchdog_ops wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	wdt_start,
	.stop =		wdt_stop,
	.ping =		wdt_ping,
	.set_timeout =	wdt_set_timeout,
};

static struct watchdog_device wdt_dev = {
	.info =		&wdt_info,
	.ops =		&wdt_ops,
	.min_timeout =	1,
	.max_timeout =	WDT_TIMEOUT_MAX,
};

static int wdt_probe(struct pci_dev *pdev,
			       const struct pci_device_id *ent)
{
	unsigned char conf;
	int ret = -ENODEV;

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		return -ENODEV;
	}

	/*
	 * Allocate a MMIO region which contains watchdog control register
	 * and counter, then configure the watchdog to use this region.
	 * This is possible only if PnP is properly enabled in BIOS.
	 * If not, the watchdog must be configured in BIOS manually.
	 */
	if (allocate_resource(&iomem_resource, &wdt_res, VIA_WDT_MMIO_LEN,
			      0xf0000000, 0xffffff00, 0xff, NULL, NULL)) {
		dev_err(&pdev->dev, "MMIO allocation failed\n");
		goto err_out_disable_device;
	}

	pci_write_config_dword(pdev, VIA_WDT_MMIO_BASE, wdt_res.start);
	pci_read_config_byte(pdev, VIA_WDT_CONF, &conf);
	conf |= VIA_WDT_CONF_ENABLE | VIA_WDT_CONF_MMIO;
	pci_write_config_byte(pdev, VIA_WDT_CONF, conf);

	pci_read_config_dword(pdev, VIA_WDT_MMIO_BASE, &mmio);
	if (mmio) {
		dev_info(&pdev->dev, "VIA Chipset watchdog MMIO: %x\n", mmio);
	} else {
		dev_err(&pdev->dev, "MMIO setting failed. Check BIOS.\n");
		goto err_out_resource;
	}

	if (!request_mem_region(mmio, VIA_WDT_MMIO_LEN, "via_wdt")) {
		dev_err(&pdev->dev, "MMIO region busy\n");
		goto err_out_resource;
	}

	wdt_mem = ioremap(mmio, VIA_WDT_MMIO_LEN);
	if (wdt_mem == NULL) {
		dev_err(&pdev->dev, "cannot remap VIA wdt MMIO registers\n");
		goto err_out_release;
	}

	if (timeout < 1 || timeout > WDT_TIMEOUT_MAX)
		timeout = WDT_TIMEOUT;

	wdt_dev.timeout = timeout;
	wdt_dev.parent = &pdev->dev;
	watchdog_set_nowayout(&wdt_dev, nowayout);
	if (readl(wdt_mem) & VIA_WDT_FIRED)
		wdt_dev.bootstatus |= WDIOF_CARDRESET;

	ret = watchdog_register_device(&wdt_dev);
	if (ret)
		goto err_out_iounmap;

	/* start triggering, in case of watchdog already enabled by BIOS */
	mod_timer(&timer, jiffies + WDT_HEARTBEAT);
	return 0;

err_out_iounmap:
	iounmap(wdt_mem);
err_out_release:
	release_mem_region(mmio, VIA_WDT_MMIO_LEN);
err_out_resource:
	release_resource(&wdt_res);
err_out_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void wdt_remove(struct pci_dev *pdev)
{
	watchdog_unregister_device(&wdt_dev);
	del_timer_sync(&timer);
	iounmap(wdt_mem);
	release_mem_region(mmio, VIA_WDT_MMIO_LEN);
	release_resource(&wdt_res);
	pci_disable_device(pdev);
}

static const struct pci_device_id wdt_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_CX700) },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VX800) },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VX855) },
	{ 0 }
};

static struct pci_driver wdt_driver = {
	.name		= "via_wdt",
	.id_table	= wdt_pci_table,
	.probe		= wdt_probe,
	.remove		= wdt_remove,
};

module_pci_driver(wdt_driver);

MODULE_AUTHOR("Marc Vertes");
MODULE_DESCRIPTION("Driver for watchdog timer on VIA chipset");
MODULE_LICENSE("GPL");
