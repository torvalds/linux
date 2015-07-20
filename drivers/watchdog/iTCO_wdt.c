/*
 *	intel TCO Watchdog Driver
 *
 *	(c) Copyright 2006-2011 Wim Van Sebroeck <wim@iguana.be>.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Wim Van Sebroeck nor Iguana vzw. admit liability nor
 *	provide warranty for any of this software. This material is
 *	provided "AS-IS" and at no charge.
 *
 *	The TCO watchdog is implemented in the following I/O controller hubs:
 *	(See the intel documentation on http://developer.intel.com.)
 *	document number 290655-003, 290677-014: 82801AA (ICH), 82801AB (ICHO)
 *	document number 290687-002, 298242-027: 82801BA (ICH2)
 *	document number 290733-003, 290739-013: 82801CA (ICH3-S)
 *	document number 290716-001, 290718-007: 82801CAM (ICH3-M)
 *	document number 290744-001, 290745-025: 82801DB (ICH4)
 *	document number 252337-001, 252663-008: 82801DBM (ICH4-M)
 *	document number 273599-001, 273645-002: 82801E (C-ICH)
 *	document number 252516-001, 252517-028: 82801EB (ICH5), 82801ER (ICH5R)
 *	document number 300641-004, 300884-013: 6300ESB
 *	document number 301473-002, 301474-026: 82801F (ICH6)
 *	document number 313082-001, 313075-006: 631xESB, 632xESB
 *	document number 307013-003, 307014-024: 82801G (ICH7)
 *	document number 322896-001, 322897-001: NM10
 *	document number 313056-003, 313057-017: 82801H (ICH8)
 *	document number 316972-004, 316973-012: 82801I (ICH9)
 *	document number 319973-002, 319974-002: 82801J (ICH10)
 *	document number 322169-001, 322170-003: 5 Series, 3400 Series (PCH)
 *	document number 320066-003, 320257-008: EP80597 (IICH)
 *	document number 324645-001, 324646-001: Cougar Point (CPT)
 *	document number TBD                   : Patsburg (PBG)
 *	document number TBD                   : DH89xxCC
 *	document number TBD                   : Panther Point
 *	document number TBD                   : Lynx Point
 *	document number TBD                   : Lynx Point-LP
 */

/*
 *	Includes, defines, variables, module parameters, ...
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* Module and version information */
#define DRV_NAME	"iTCO_wdt"
#define DRV_VERSION	"1.11"

/* Includes */
#include <linux/acpi.h>			/* For ACPI support */
#include <linux/module.h>		/* For module specific items */
#include <linux/moduleparam.h>		/* For new moduleparam's */
#include <linux/types.h>		/* For standard types (like size_t) */
#include <linux/errno.h>		/* For the -ENODEV/... values */
#include <linux/kernel.h>		/* For printk/panic/... */
#include <linux/watchdog.h>		/* For the watchdog specific items */
#include <linux/init.h>			/* For __init/__exit/... */
#include <linux/fs.h>			/* For file operations */
#include <linux/platform_device.h>	/* For platform_driver framework */
#include <linux/pci.h>			/* For pci functions */
#include <linux/ioport.h>		/* For io-port access */
#include <linux/spinlock.h>		/* For spin_lock/spin_unlock/... */
#include <linux/uaccess.h>		/* For copy_to_user/put_user/... */
#include <linux/io.h>			/* For inb/outb/... */
#include <linux/mfd/core.h>
#include <linux/mfd/lpc_ich.h>

#include "iTCO_vendor.h"

/* Address definitions for the TCO */
/* TCO base address */
#define TCOBASE		(iTCO_wdt_private.tco_res->start)
/* SMI Control and Enable Register */
#define SMI_EN		(iTCO_wdt_private.smi_res->start)

#define TCO_RLD		(TCOBASE + 0x00) /* TCO Timer Reload and Curr. Value */
#define TCOv1_TMR	(TCOBASE + 0x01) /* TCOv1 Timer Initial Value	*/
#define TCO_DAT_IN	(TCOBASE + 0x02) /* TCO Data In Register	*/
#define TCO_DAT_OUT	(TCOBASE + 0x03) /* TCO Data Out Register	*/
#define TCO1_STS	(TCOBASE + 0x04) /* TCO1 Status Register	*/
#define TCO2_STS	(TCOBASE + 0x06) /* TCO2 Status Register	*/
#define TCO1_CNT	(TCOBASE + 0x08) /* TCO1 Control Register	*/
#define TCO2_CNT	(TCOBASE + 0x0a) /* TCO2 Control Register	*/
#define TCOv2_TMR	(TCOBASE + 0x12) /* TCOv2 Timer Initial Value	*/

/* internal variables */
static struct {		/* this is private data for the iTCO_wdt device */
	/* TCO version/generation */
	unsigned int iTCO_version;
	struct resource *tco_res;
	struct resource *smi_res;
	/*
	 * NO_REBOOT flag is Memory-Mapped GCS register bit 5 (TCO version 2),
	 * or memory-mapped PMC register bit 4 (TCO version 3).
	 */
	struct resource *gcs_pmc_res;
	unsigned long __iomem *gcs_pmc;
	/* the lock for io operations */
	spinlock_t io_lock;
	struct platform_device *dev;
	/* the PCI-device */
	struct pci_dev *pdev;
	/* whether or not the watchdog has been suspended */
	bool suspended;
} iTCO_wdt_private;

/* module parameters */
#define WATCHDOG_TIMEOUT 30	/* 30 sec default heartbeat */
static int heartbeat = WATCHDOG_TIMEOUT;  /* in seconds */
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog timeout in seconds. "
	"5..76 (TCO v1) or 3..614 (TCO v2), default="
				__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int turn_SMI_watchdog_clear_off = 1;
module_param(turn_SMI_watchdog_clear_off, int, 0);
MODULE_PARM_DESC(turn_SMI_watchdog_clear_off,
	"Turn off SMI clearing watchdog (depends on TCO-version)(default=1)");

/*
 * Some TCO specific functions
 */

/*
 * The iTCO v1 and v2's internal timer is stored as ticks which decrement
 * every 0.6 seconds.  v3's internal timer is stored as seconds (some
 * datasheets incorrectly state 0.6 seconds).
 */
static inline unsigned int seconds_to_ticks(int secs)
{
	return iTCO_wdt_private.iTCO_version == 3 ? secs : (secs * 10) / 6;
}

static inline unsigned int ticks_to_seconds(int ticks)
{
	return iTCO_wdt_private.iTCO_version == 3 ? ticks : (ticks * 6) / 10;
}

static void iTCO_wdt_set_NO_REBOOT_bit(void)
{
	u32 val32;

	/* Set the NO_REBOOT bit: this disables reboots */
	if (iTCO_wdt_private.iTCO_version == 3) {
		val32 = readl(iTCO_wdt_private.gcs_pmc);
		val32 |= 0x00000010;
		writel(val32, iTCO_wdt_private.gcs_pmc);
	} else if (iTCO_wdt_private.iTCO_version == 2) {
		val32 = readl(iTCO_wdt_private.gcs_pmc);
		val32 |= 0x00000020;
		writel(val32, iTCO_wdt_private.gcs_pmc);
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		pci_read_config_dword(iTCO_wdt_private.pdev, 0xd4, &val32);
		val32 |= 0x00000002;
		pci_write_config_dword(iTCO_wdt_private.pdev, 0xd4, val32);
	}
}

static int iTCO_wdt_unset_NO_REBOOT_bit(void)
{
	int ret = 0;
	u32 val32;

	/* Unset the NO_REBOOT bit: this enables reboots */
	if (iTCO_wdt_private.iTCO_version == 3) {
		val32 = readl(iTCO_wdt_private.gcs_pmc);
		val32 &= 0xffffffef;
		writel(val32, iTCO_wdt_private.gcs_pmc);

		val32 = readl(iTCO_wdt_private.gcs_pmc);
		if (val32 & 0x00000010)
			ret = -EIO;
	} else if (iTCO_wdt_private.iTCO_version == 2) {
		val32 = readl(iTCO_wdt_private.gcs_pmc);
		val32 &= 0xffffffdf;
		writel(val32, iTCO_wdt_private.gcs_pmc);

		val32 = readl(iTCO_wdt_private.gcs_pmc);
		if (val32 & 0x00000020)
			ret = -EIO;
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		pci_read_config_dword(iTCO_wdt_private.pdev, 0xd4, &val32);
		val32 &= 0xfffffffd;
		pci_write_config_dword(iTCO_wdt_private.pdev, 0xd4, val32);

		pci_read_config_dword(iTCO_wdt_private.pdev, 0xd4, &val32);
		if (val32 & 0x00000002)
			ret = -EIO;
	}

	return ret; /* returns: 0 = OK, -EIO = Error */
}

static int iTCO_wdt_start(struct watchdog_device *wd_dev)
{
	unsigned int val;

	spin_lock(&iTCO_wdt_private.io_lock);

	iTCO_vendor_pre_start(iTCO_wdt_private.smi_res, wd_dev->timeout);

	/* disable chipset's NO_REBOOT bit */
	if (iTCO_wdt_unset_NO_REBOOT_bit()) {
		spin_unlock(&iTCO_wdt_private.io_lock);
		pr_err("failed to reset NO_REBOOT flag, reboot disabled by hardware/BIOS\n");
		return -EIO;
	}

	/* Force the timer to its reload value by writing to the TCO_RLD
	   register */
	if (iTCO_wdt_private.iTCO_version >= 2)
		outw(0x01, TCO_RLD);
	else if (iTCO_wdt_private.iTCO_version == 1)
		outb(0x01, TCO_RLD);

	/* Bit 11: TCO Timer Halt -> 0 = The TCO timer is enabled to count */
	val = inw(TCO1_CNT);
	val &= 0xf7ff;
	outw(val, TCO1_CNT);
	val = inw(TCO1_CNT);
	spin_unlock(&iTCO_wdt_private.io_lock);

	if (val & 0x0800)
		return -1;
	return 0;
}

static int iTCO_wdt_stop(struct watchdog_device *wd_dev)
{
	unsigned int val;

	spin_lock(&iTCO_wdt_private.io_lock);

	iTCO_vendor_pre_stop(iTCO_wdt_private.smi_res);

	/* Bit 11: TCO Timer Halt -> 1 = The TCO timer is disabled */
	val = inw(TCO1_CNT);
	val |= 0x0800;
	outw(val, TCO1_CNT);
	val = inw(TCO1_CNT);

	/* Set the NO_REBOOT bit to prevent later reboots, just for sure */
	iTCO_wdt_set_NO_REBOOT_bit();

	spin_unlock(&iTCO_wdt_private.io_lock);

	if ((val & 0x0800) == 0)
		return -1;
	return 0;
}

static int iTCO_wdt_ping(struct watchdog_device *wd_dev)
{
	spin_lock(&iTCO_wdt_private.io_lock);

	iTCO_vendor_pre_keepalive(iTCO_wdt_private.smi_res, wd_dev->timeout);

	/* Reload the timer by writing to the TCO Timer Counter register */
	if (iTCO_wdt_private.iTCO_version >= 2) {
		outw(0x01, TCO_RLD);
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		/* Reset the timeout status bit so that the timer
		 * needs to count down twice again before rebooting */
		outw(0x0008, TCO1_STS);	/* write 1 to clear bit */

		outb(0x01, TCO_RLD);
	}

	spin_unlock(&iTCO_wdt_private.io_lock);
	return 0;
}

static int iTCO_wdt_set_timeout(struct watchdog_device *wd_dev, unsigned int t)
{
	unsigned int val16;
	unsigned char val8;
	unsigned int tmrval;

	tmrval = seconds_to_ticks(t);

	/* For TCO v1 the timer counts down twice before rebooting */
	if (iTCO_wdt_private.iTCO_version == 1)
		tmrval /= 2;

	/* from the specs: */
	/* "Values of 0h-3h are ignored and should not be attempted" */
	if (tmrval < 0x04)
		return -EINVAL;
	if (((iTCO_wdt_private.iTCO_version >= 2) && (tmrval > 0x3ff)) ||
	    ((iTCO_wdt_private.iTCO_version == 1) && (tmrval > 0x03f)))
		return -EINVAL;

	iTCO_vendor_pre_set_heartbeat(tmrval);

	/* Write new heartbeat to watchdog */
	if (iTCO_wdt_private.iTCO_version >= 2) {
		spin_lock(&iTCO_wdt_private.io_lock);
		val16 = inw(TCOv2_TMR);
		val16 &= 0xfc00;
		val16 |= tmrval;
		outw(val16, TCOv2_TMR);
		val16 = inw(TCOv2_TMR);
		spin_unlock(&iTCO_wdt_private.io_lock);

		if ((val16 & 0x3ff) != tmrval)
			return -EINVAL;
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		spin_lock(&iTCO_wdt_private.io_lock);
		val8 = inb(TCOv1_TMR);
		val8 &= 0xc0;
		val8 |= (tmrval & 0xff);
		outb(val8, TCOv1_TMR);
		val8 = inb(TCOv1_TMR);
		spin_unlock(&iTCO_wdt_private.io_lock);

		if ((val8 & 0x3f) != tmrval)
			return -EINVAL;
	}

	wd_dev->timeout = t;
	return 0;
}

static unsigned int iTCO_wdt_get_timeleft(struct watchdog_device *wd_dev)
{
	unsigned int val16;
	unsigned char val8;
	unsigned int time_left = 0;

	/* read the TCO Timer */
	if (iTCO_wdt_private.iTCO_version >= 2) {
		spin_lock(&iTCO_wdt_private.io_lock);
		val16 = inw(TCO_RLD);
		val16 &= 0x3ff;
		spin_unlock(&iTCO_wdt_private.io_lock);

		time_left = ticks_to_seconds(val16);
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		spin_lock(&iTCO_wdt_private.io_lock);
		val8 = inb(TCO_RLD);
		val8 &= 0x3f;
		if (!(inw(TCO1_STS) & 0x0008))
			val8 += (inb(TCOv1_TMR) & 0x3f);
		spin_unlock(&iTCO_wdt_private.io_lock);

		time_left = ticks_to_seconds(val8);
	}
	return time_left;
}

/*
 *	Kernel Interfaces
 */

static const struct watchdog_info ident = {
	.options =		WDIOF_SETTIMEOUT |
				WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
	.firmware_version =	0,
	.identity =		DRV_NAME,
};

static const struct watchdog_ops iTCO_wdt_ops = {
	.owner =		THIS_MODULE,
	.start =		iTCO_wdt_start,
	.stop =			iTCO_wdt_stop,
	.ping =			iTCO_wdt_ping,
	.set_timeout =		iTCO_wdt_set_timeout,
	.get_timeleft =		iTCO_wdt_get_timeleft,
};

static struct watchdog_device iTCO_wdt_watchdog_dev = {
	.info =		&ident,
	.ops =		&iTCO_wdt_ops,
};

/*
 *	Init & exit routines
 */

static void iTCO_wdt_cleanup(void)
{
	/* Stop the timer before we leave */
	if (!nowayout)
		iTCO_wdt_stop(&iTCO_wdt_watchdog_dev);

	/* Deregister */
	watchdog_unregister_device(&iTCO_wdt_watchdog_dev);

	/* release resources */
	release_region(iTCO_wdt_private.tco_res->start,
			resource_size(iTCO_wdt_private.tco_res));
	release_region(iTCO_wdt_private.smi_res->start,
			resource_size(iTCO_wdt_private.smi_res));
	if (iTCO_wdt_private.iTCO_version >= 2) {
		iounmap(iTCO_wdt_private.gcs_pmc);
		release_mem_region(iTCO_wdt_private.gcs_pmc_res->start,
				resource_size(iTCO_wdt_private.gcs_pmc_res));
	}

	iTCO_wdt_private.tco_res = NULL;
	iTCO_wdt_private.smi_res = NULL;
	iTCO_wdt_private.gcs_pmc_res = NULL;
	iTCO_wdt_private.gcs_pmc = NULL;
}

static int iTCO_wdt_probe(struct platform_device *dev)
{
	int ret = -ENODEV;
	unsigned long val32;
	struct lpc_ich_info *ich_info = dev_get_platdata(&dev->dev);

	if (!ich_info)
		goto out;

	spin_lock_init(&iTCO_wdt_private.io_lock);

	iTCO_wdt_private.tco_res =
		platform_get_resource(dev, IORESOURCE_IO, ICH_RES_IO_TCO);
	if (!iTCO_wdt_private.tco_res)
		goto out;

	iTCO_wdt_private.smi_res =
		platform_get_resource(dev, IORESOURCE_IO, ICH_RES_IO_SMI);
	if (!iTCO_wdt_private.smi_res)
		goto out;

	iTCO_wdt_private.iTCO_version = ich_info->iTCO_version;
	iTCO_wdt_private.dev = dev;
	iTCO_wdt_private.pdev = to_pci_dev(dev->dev.parent);

	/*
	 * Get the Memory-Mapped GCS or PMC register, we need it for the
	 * NO_REBOOT flag (TCO v2 and v3).
	 */
	if (iTCO_wdt_private.iTCO_version >= 2) {
		iTCO_wdt_private.gcs_pmc_res = platform_get_resource(dev,
							IORESOURCE_MEM,
							ICH_RES_MEM_GCS_PMC);

		if (!iTCO_wdt_private.gcs_pmc_res)
			goto out;

		if (!request_mem_region(iTCO_wdt_private.gcs_pmc_res->start,
			resource_size(iTCO_wdt_private.gcs_pmc_res), dev->name)) {
			ret = -EBUSY;
			goto out;
		}
		iTCO_wdt_private.gcs_pmc = ioremap(iTCO_wdt_private.gcs_pmc_res->start,
			resource_size(iTCO_wdt_private.gcs_pmc_res));
		if (!iTCO_wdt_private.gcs_pmc) {
			ret = -EIO;
			goto unreg_gcs_pmc;
		}
	}

	/* Check chipset's NO_REBOOT bit */
	if (iTCO_wdt_unset_NO_REBOOT_bit() && iTCO_vendor_check_noreboot_on()) {
		pr_info("unable to reset NO_REBOOT flag, device disabled by hardware/BIOS\n");
		ret = -ENODEV;	/* Cannot reset NO_REBOOT bit */
		goto unmap_gcs_pmc;
	}

	/* Set the NO_REBOOT bit to prevent later reboots, just for sure */
	iTCO_wdt_set_NO_REBOOT_bit();

	/* The TCO logic uses the TCO_EN bit in the SMI_EN register */
	if (!request_region(iTCO_wdt_private.smi_res->start,
			resource_size(iTCO_wdt_private.smi_res), dev->name)) {
		pr_err("I/O address 0x%04llx already in use, device disabled\n",
		       (u64)SMI_EN);
		ret = -EBUSY;
		goto unmap_gcs_pmc;
	}
	if (turn_SMI_watchdog_clear_off >= iTCO_wdt_private.iTCO_version) {
		/*
		 * Bit 13: TCO_EN -> 0
		 * Disables TCO logic generating an SMI#
		 */
		val32 = inl(SMI_EN);
		val32 &= 0xffffdfff;	/* Turn off SMI clearing watchdog */
		outl(val32, SMI_EN);
	}

	if (!request_region(iTCO_wdt_private.tco_res->start,
			resource_size(iTCO_wdt_private.tco_res), dev->name)) {
		pr_err("I/O address 0x%04llx already in use, device disabled\n",
		       (u64)TCOBASE);
		ret = -EBUSY;
		goto unreg_smi;
	}

	pr_info("Found a %s TCO device (Version=%d, TCOBASE=0x%04llx)\n",
		ich_info->name, ich_info->iTCO_version, (u64)TCOBASE);

	/* Clear out the (probably old) status */
	if (iTCO_wdt_private.iTCO_version == 3) {
		outl(0x20008, TCO1_STS);
	} else {
		outw(0x0008, TCO1_STS);	/* Clear the Time Out Status bit */
		outw(0x0002, TCO2_STS);	/* Clear SECOND_TO_STS bit */
		outw(0x0004, TCO2_STS);	/* Clear BOOT_STS bit */
	}

	iTCO_wdt_watchdog_dev.bootstatus = 0;
	iTCO_wdt_watchdog_dev.timeout = WATCHDOG_TIMEOUT;
	watchdog_set_nowayout(&iTCO_wdt_watchdog_dev, nowayout);
	iTCO_wdt_watchdog_dev.parent = &dev->dev;

	/* Make sure the watchdog is not running */
	iTCO_wdt_stop(&iTCO_wdt_watchdog_dev);

	/* Check that the heartbeat value is within it's range;
	   if not reset to the default */
	if (iTCO_wdt_set_timeout(&iTCO_wdt_watchdog_dev, heartbeat)) {
		iTCO_wdt_set_timeout(&iTCO_wdt_watchdog_dev, WATCHDOG_TIMEOUT);
		pr_info("timeout value out of range, using %d\n",
			WATCHDOG_TIMEOUT);
	}

	ret = watchdog_register_device(&iTCO_wdt_watchdog_dev);
	if (ret != 0) {
		pr_err("cannot register watchdog device (err=%d)\n", ret);
		goto unreg_tco;
	}

	pr_info("initialized. heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;

unreg_tco:
	release_region(iTCO_wdt_private.tco_res->start,
			resource_size(iTCO_wdt_private.tco_res));
unreg_smi:
	release_region(iTCO_wdt_private.smi_res->start,
			resource_size(iTCO_wdt_private.smi_res));
unmap_gcs_pmc:
	if (iTCO_wdt_private.iTCO_version >= 2)
		iounmap(iTCO_wdt_private.gcs_pmc);
unreg_gcs_pmc:
	if (iTCO_wdt_private.iTCO_version >= 2)
		release_mem_region(iTCO_wdt_private.gcs_pmc_res->start,
				resource_size(iTCO_wdt_private.gcs_pmc_res));
out:
	iTCO_wdt_private.tco_res = NULL;
	iTCO_wdt_private.smi_res = NULL;
	iTCO_wdt_private.gcs_pmc_res = NULL;
	iTCO_wdt_private.gcs_pmc = NULL;

	return ret;
}

static int iTCO_wdt_remove(struct platform_device *dev)
{
	if (iTCO_wdt_private.tco_res || iTCO_wdt_private.smi_res)
		iTCO_wdt_cleanup();

	return 0;
}

static void iTCO_wdt_shutdown(struct platform_device *dev)
{
	iTCO_wdt_stop(NULL);
}

#ifdef CONFIG_PM_SLEEP
/*
 * Suspend-to-idle requires this, because it stops the ticks and timekeeping, so
 * the watchdog cannot be pinged while in that state.  In ACPI sleep states the
 * watchdog is stopped by the platform firmware.
 */

#ifdef CONFIG_ACPI
static inline bool need_suspend(void)
{
	return acpi_target_system_state() == ACPI_STATE_S0;
}
#else
static inline bool need_suspend(void) { return true; }
#endif

static int iTCO_wdt_suspend_noirq(struct device *dev)
{
	int ret = 0;

	iTCO_wdt_private.suspended = false;
	if (watchdog_active(&iTCO_wdt_watchdog_dev) && need_suspend()) {
		ret = iTCO_wdt_stop(&iTCO_wdt_watchdog_dev);
		if (!ret)
			iTCO_wdt_private.suspended = true;
	}
	return ret;
}

static int iTCO_wdt_resume_noirq(struct device *dev)
{
	if (iTCO_wdt_private.suspended)
		iTCO_wdt_start(&iTCO_wdt_watchdog_dev);

	return 0;
}

static struct dev_pm_ops iTCO_wdt_pm = {
	.suspend_noirq = iTCO_wdt_suspend_noirq,
	.resume_noirq = iTCO_wdt_resume_noirq,
};

#define ITCO_WDT_PM_OPS	(&iTCO_wdt_pm)
#else
#define ITCO_WDT_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver iTCO_wdt_driver = {
	.probe          = iTCO_wdt_probe,
	.remove         = iTCO_wdt_remove,
	.shutdown       = iTCO_wdt_shutdown,
	.driver         = {
		.name   = DRV_NAME,
		.pm     = ITCO_WDT_PM_OPS,
	},
};

static int __init iTCO_wdt_init_module(void)
{
	int err;

	pr_info("Intel TCO WatchDog Timer Driver v%s\n", DRV_VERSION);

	err = platform_driver_register(&iTCO_wdt_driver);
	if (err)
		return err;

	return 0;
}

static void __exit iTCO_wdt_cleanup_module(void)
{
	platform_driver_unregister(&iTCO_wdt_driver);
	pr_info("Watchdog Module Unloaded\n");
}

module_init(iTCO_wdt_init_module);
module_exit(iTCO_wdt_cleanup_module);

MODULE_AUTHOR("Wim Van Sebroeck <wim@iguana.be>");
MODULE_DESCRIPTION("Intel TCO WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
