// SPDX-License-Identifier: GPL-2.0+
/*
 *	intel TCO Watchdog Driver
 *
 *	(c) Copyright 2006-2011 Wim Van Sebroeck <wim@iguana.be>.
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

/* Module and version information */
#define DRV_NAME	"iTCO_wdt"
#define DRV_VERSION	"1.11"

/* Includes */
#include <linux/acpi.h>			/* For ACPI support */
#include <linux/bits.h>			/* For BIT() */
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
#include <linux/platform_data/itco_wdt.h>
#include <linux/mfd/intel_pmc_bxt.h>

#include "iTCO_vendor.h"

/* Address definitions for the TCO */
/* TCO base address */
#define TCOBASE(p)	((p)->tco_res->start)
/* SMI Control and Enable Register */
#define SMI_EN(p)	((p)->smi_res->start)
#define TCO_EN		(1 << 13)
#define GBL_SMI_EN	(1 << 0)

#define TCO_RLD(p)	(TCOBASE(p) + 0x00) /* TCO Timer Reload/Curr. Value */
#define TCOv1_TMR(p)	(TCOBASE(p) + 0x01) /* TCOv1 Timer Initial Value*/
#define TCO_DAT_IN(p)	(TCOBASE(p) + 0x02) /* TCO Data In Register	*/
#define TCO_DAT_OUT(p)	(TCOBASE(p) + 0x03) /* TCO Data Out Register	*/
#define TCO1_STS(p)	(TCOBASE(p) + 0x04) /* TCO1 Status Register	*/
#define TCO2_STS(p)	(TCOBASE(p) + 0x06) /* TCO2 Status Register	*/
#define TCO1_CNT(p)	(TCOBASE(p) + 0x08) /* TCO1 Control Register	*/
#define TCO2_CNT(p)	(TCOBASE(p) + 0x0a) /* TCO2 Control Register	*/
#define TCOv2_TMR(p)	(TCOBASE(p) + 0x12) /* TCOv2 Timer Initial Value*/

/* internal variables */
struct iTCO_wdt_private {
	struct watchdog_device wddev;

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
	/* the PCI-device */
	struct pci_dev *pci_dev;
	/* whether or not the watchdog has been suspended */
	bool suspended;
	/* no reboot API private data */
	void *no_reboot_priv;
	/* no reboot update function pointer */
	int (*update_no_reboot_bit)(void *p, bool set);
};

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
static inline unsigned int seconds_to_ticks(struct iTCO_wdt_private *p,
					    int secs)
{
	return p->iTCO_version == 3 ? secs : (secs * 10) / 6;
}

static inline unsigned int ticks_to_seconds(struct iTCO_wdt_private *p,
					    int ticks)
{
	return p->iTCO_version == 3 ? ticks : (ticks * 6) / 10;
}

static inline u32 no_reboot_bit(struct iTCO_wdt_private *p)
{
	u32 enable_bit;

	switch (p->iTCO_version) {
	case 5:
	case 3:
		enable_bit = 0x00000010;
		break;
	case 2:
		enable_bit = 0x00000020;
		break;
	case 4:
	case 1:
	default:
		enable_bit = 0x00000002;
		break;
	}

	return enable_bit;
}

static int update_no_reboot_bit_def(void *priv, bool set)
{
	return 0;
}

static int update_no_reboot_bit_pci(void *priv, bool set)
{
	struct iTCO_wdt_private *p = priv;
	u32 val32 = 0, newval32 = 0;

	pci_read_config_dword(p->pci_dev, 0xd4, &val32);
	if (set)
		val32 |= no_reboot_bit(p);
	else
		val32 &= ~no_reboot_bit(p);
	pci_write_config_dword(p->pci_dev, 0xd4, val32);
	pci_read_config_dword(p->pci_dev, 0xd4, &newval32);

	/* make sure the update is successful */
	if (val32 != newval32)
		return -EIO;

	return 0;
}

static int update_no_reboot_bit_mem(void *priv, bool set)
{
	struct iTCO_wdt_private *p = priv;
	u32 val32 = 0, newval32 = 0;

	val32 = readl(p->gcs_pmc);
	if (set)
		val32 |= no_reboot_bit(p);
	else
		val32 &= ~no_reboot_bit(p);
	writel(val32, p->gcs_pmc);
	newval32 = readl(p->gcs_pmc);

	/* make sure the update is successful */
	if (val32 != newval32)
		return -EIO;

	return 0;
}

static int update_no_reboot_bit_cnt(void *priv, bool set)
{
	struct iTCO_wdt_private *p = priv;
	u16 val, newval;

	val = inw(TCO1_CNT(p));
	if (set)
		val |= BIT(0);
	else
		val &= ~BIT(0);
	outw(val, TCO1_CNT(p));
	newval = inw(TCO1_CNT(p));

	/* make sure the update is successful */
	return val != newval ? -EIO : 0;
}

static int update_no_reboot_bit_pmc(void *priv, bool set)
{
	struct intel_pmc_dev *pmc = priv;
	u32 bits = PMC_CFG_NO_REBOOT_EN;
	u32 value = set ? bits : 0;

	return intel_pmc_gcr_update(pmc, PMC_GCR_PMC_CFG_REG, bits, value);
}

static void iTCO_wdt_no_reboot_bit_setup(struct iTCO_wdt_private *p,
					 struct platform_device *pdev,
					 struct itco_wdt_platform_data *pdata)
{
	if (pdata->no_reboot_use_pmc) {
		struct intel_pmc_dev *pmc = dev_get_drvdata(pdev->dev.parent);

		p->update_no_reboot_bit = update_no_reboot_bit_pmc;
		p->no_reboot_priv = pmc;
		return;
	}

	if (p->iTCO_version >= 6)
		p->update_no_reboot_bit = update_no_reboot_bit_cnt;
	else if (p->iTCO_version >= 2)
		p->update_no_reboot_bit = update_no_reboot_bit_mem;
	else if (p->iTCO_version == 1)
		p->update_no_reboot_bit = update_no_reboot_bit_pci;
	else
		p->update_no_reboot_bit = update_no_reboot_bit_def;

	p->no_reboot_priv = p;
}

static int iTCO_wdt_start(struct watchdog_device *wd_dev)
{
	struct iTCO_wdt_private *p = watchdog_get_drvdata(wd_dev);
	unsigned int val;

	spin_lock(&p->io_lock);

	iTCO_vendor_pre_start(p->smi_res, wd_dev->timeout);

	/* disable chipset's NO_REBOOT bit */
	if (p->update_no_reboot_bit(p->no_reboot_priv, false)) {
		spin_unlock(&p->io_lock);
		dev_err(wd_dev->parent, "failed to reset NO_REBOOT flag, reboot disabled by hardware/BIOS\n");
		return -EIO;
	}

	/* Force the timer to its reload value by writing to the TCO_RLD
	   register */
	if (p->iTCO_version >= 2)
		outw(0x01, TCO_RLD(p));
	else if (p->iTCO_version == 1)
		outb(0x01, TCO_RLD(p));

	/* Bit 11: TCO Timer Halt -> 0 = The TCO timer is enabled to count */
	val = inw(TCO1_CNT(p));
	val &= 0xf7ff;
	outw(val, TCO1_CNT(p));
	val = inw(TCO1_CNT(p));
	spin_unlock(&p->io_lock);

	if (val & 0x0800)
		return -1;
	return 0;
}

static int iTCO_wdt_stop(struct watchdog_device *wd_dev)
{
	struct iTCO_wdt_private *p = watchdog_get_drvdata(wd_dev);
	unsigned int val;

	spin_lock(&p->io_lock);

	iTCO_vendor_pre_stop(p->smi_res);

	/* Bit 11: TCO Timer Halt -> 1 = The TCO timer is disabled */
	val = inw(TCO1_CNT(p));
	val |= 0x0800;
	outw(val, TCO1_CNT(p));
	val = inw(TCO1_CNT(p));

	/* Set the NO_REBOOT bit to prevent later reboots, just for sure */
	p->update_no_reboot_bit(p->no_reboot_priv, true);

	spin_unlock(&p->io_lock);

	if ((val & 0x0800) == 0)
		return -1;
	return 0;
}

static int iTCO_wdt_ping(struct watchdog_device *wd_dev)
{
	struct iTCO_wdt_private *p = watchdog_get_drvdata(wd_dev);

	spin_lock(&p->io_lock);

	/* Reload the timer by writing to the TCO Timer Counter register */
	if (p->iTCO_version >= 2) {
		outw(0x01, TCO_RLD(p));
	} else if (p->iTCO_version == 1) {
		/* Reset the timeout status bit so that the timer
		 * needs to count down twice again before rebooting */
		outw(0x0008, TCO1_STS(p));	/* write 1 to clear bit */

		outb(0x01, TCO_RLD(p));
	}

	spin_unlock(&p->io_lock);
	return 0;
}

static int iTCO_wdt_set_timeout(struct watchdog_device *wd_dev, unsigned int t)
{
	struct iTCO_wdt_private *p = watchdog_get_drvdata(wd_dev);
	unsigned int val16;
	unsigned char val8;
	unsigned int tmrval;

	tmrval = seconds_to_ticks(p, t);

	/*
	 * If TCO SMIs are off, the timer counts down twice before rebooting.
	 * Otherwise, the BIOS generally reboots when the SMI triggers.
	 */
	if (p->smi_res &&
	    (SMI_EN(p) & (TCO_EN | GBL_SMI_EN)) != (TCO_EN | GBL_SMI_EN))
		tmrval /= 2;

	/* from the specs: */
	/* "Values of 0h-3h are ignored and should not be attempted" */
	if (tmrval < 0x04)
		return -EINVAL;
	if ((p->iTCO_version >= 2 && tmrval > 0x3ff) ||
	    (p->iTCO_version == 1 && tmrval > 0x03f))
		return -EINVAL;

	/* Write new heartbeat to watchdog */
	if (p->iTCO_version >= 2) {
		spin_lock(&p->io_lock);
		val16 = inw(TCOv2_TMR(p));
		val16 &= 0xfc00;
		val16 |= tmrval;
		outw(val16, TCOv2_TMR(p));
		val16 = inw(TCOv2_TMR(p));
		spin_unlock(&p->io_lock);

		if ((val16 & 0x3ff) != tmrval)
			return -EINVAL;
	} else if (p->iTCO_version == 1) {
		spin_lock(&p->io_lock);
		val8 = inb(TCOv1_TMR(p));
		val8 &= 0xc0;
		val8 |= (tmrval & 0xff);
		outb(val8, TCOv1_TMR(p));
		val8 = inb(TCOv1_TMR(p));
		spin_unlock(&p->io_lock);

		if ((val8 & 0x3f) != tmrval)
			return -EINVAL;
	}

	wd_dev->timeout = t;
	return 0;
}

static unsigned int iTCO_wdt_get_timeleft(struct watchdog_device *wd_dev)
{
	struct iTCO_wdt_private *p = watchdog_get_drvdata(wd_dev);
	unsigned int val16;
	unsigned char val8;
	unsigned int time_left = 0;

	/* read the TCO Timer */
	if (p->iTCO_version >= 2) {
		spin_lock(&p->io_lock);
		val16 = inw(TCO_RLD(p));
		val16 &= 0x3ff;
		spin_unlock(&p->io_lock);

		time_left = ticks_to_seconds(p, val16);
	} else if (p->iTCO_version == 1) {
		spin_lock(&p->io_lock);
		val8 = inb(TCO_RLD(p));
		val8 &= 0x3f;
		if (!(inw(TCO1_STS(p)) & 0x0008))
			val8 += (inb(TCOv1_TMR(p)) & 0x3f);
		spin_unlock(&p->io_lock);

		time_left = ticks_to_seconds(p, val8);
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

/*
 *	Init & exit routines
 */

static int iTCO_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct itco_wdt_platform_data *pdata = dev_get_platdata(dev);
	struct iTCO_wdt_private *p;
	unsigned long val32;
	int ret;

	if (!pdata)
		return -ENODEV;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	spin_lock_init(&p->io_lock);

	p->tco_res = platform_get_resource(pdev, IORESOURCE_IO, ICH_RES_IO_TCO);
	if (!p->tco_res)
		return -ENODEV;

	p->iTCO_version = pdata->version;
	p->pci_dev = to_pci_dev(dev->parent);

	p->smi_res = platform_get_resource(pdev, IORESOURCE_IO, ICH_RES_IO_SMI);
	if (p->smi_res) {
		/* The TCO logic uses the TCO_EN bit in the SMI_EN register */
		if (!devm_request_region(dev, p->smi_res->start,
					 resource_size(p->smi_res),
					 pdev->name)) {
			dev_err(dev, "I/O address 0x%04llx already in use, device disabled\n",
			       (u64)SMI_EN(p));
			return -EBUSY;
		}
	} else if (iTCO_vendorsupport ||
		   turn_SMI_watchdog_clear_off >= p->iTCO_version) {
		dev_err(dev, "SMI I/O resource is missing\n");
		return -ENODEV;
	}

	iTCO_wdt_no_reboot_bit_setup(p, pdev, pdata);

	/*
	 * Get the Memory-Mapped GCS or PMC register, we need it for the
	 * NO_REBOOT flag (TCO v2 and v3).
	 */
	if (p->iTCO_version >= 2 && p->iTCO_version < 6 &&
	    !pdata->no_reboot_use_pmc) {
		p->gcs_pmc_res = platform_get_resource(pdev,
						       IORESOURCE_MEM,
						       ICH_RES_MEM_GCS_PMC);
		p->gcs_pmc = devm_ioremap_resource(dev, p->gcs_pmc_res);
		if (IS_ERR(p->gcs_pmc))
			return PTR_ERR(p->gcs_pmc);
	}

	/* Check chipset's NO_REBOOT bit */
	if (p->update_no_reboot_bit(p->no_reboot_priv, false) &&
	    iTCO_vendor_check_noreboot_on()) {
		dev_info(dev, "unable to reset NO_REBOOT flag, device disabled by hardware/BIOS\n");
		return -ENODEV;	/* Cannot reset NO_REBOOT bit */
	}

	/* Set the NO_REBOOT bit to prevent later reboots, just for sure */
	p->update_no_reboot_bit(p->no_reboot_priv, true);

	if (turn_SMI_watchdog_clear_off >= p->iTCO_version) {
		/*
		 * Bit 13: TCO_EN -> 0
		 * Disables TCO logic generating an SMI#
		 */
		val32 = inl(SMI_EN(p));
		val32 &= ~TCO_EN;	/* Turn off SMI clearing watchdog */
		outl(val32, SMI_EN(p));
	}

	if (!devm_request_region(dev, p->tco_res->start,
				 resource_size(p->tco_res),
				 pdev->name)) {
		dev_err(dev, "I/O address 0x%04llx already in use, device disabled\n",
		       (u64)TCOBASE(p));
		return -EBUSY;
	}

	dev_info(dev, "Found a %s TCO device (Version=%d, TCOBASE=0x%04llx)\n",
		pdata->name, pdata->version, (u64)TCOBASE(p));

	/* Clear out the (probably old) status */
	switch (p->iTCO_version) {
	case 6:
	case 5:
	case 4:
		outw(0x0008, TCO1_STS(p)); /* Clear the Time Out Status bit */
		outw(0x0002, TCO2_STS(p)); /* Clear SECOND_TO_STS bit */
		break;
	case 3:
		outl(0x20008, TCO1_STS(p));
		break;
	case 2:
	case 1:
	default:
		outw(0x0008, TCO1_STS(p)); /* Clear the Time Out Status bit */
		outw(0x0002, TCO2_STS(p)); /* Clear SECOND_TO_STS bit */
		outw(0x0004, TCO2_STS(p)); /* Clear BOOT_STS bit */
		break;
	}

	p->wddev.info = &ident,
	p->wddev.ops = &iTCO_wdt_ops,
	p->wddev.bootstatus = 0;
	p->wddev.timeout = WATCHDOG_TIMEOUT;
	watchdog_set_nowayout(&p->wddev, nowayout);
	p->wddev.parent = dev;

	watchdog_set_drvdata(&p->wddev, p);
	platform_set_drvdata(pdev, p);

	/* Make sure the watchdog is not running */
	iTCO_wdt_stop(&p->wddev);

	/* Check that the heartbeat value is within it's range;
	   if not reset to the default */
	if (iTCO_wdt_set_timeout(&p->wddev, heartbeat)) {
		iTCO_wdt_set_timeout(&p->wddev, WATCHDOG_TIMEOUT);
		dev_info(dev, "timeout value out of range, using %d\n",
			WATCHDOG_TIMEOUT);
	}

	watchdog_stop_on_reboot(&p->wddev);
	watchdog_stop_on_unregister(&p->wddev);
	ret = devm_watchdog_register_device(dev, &p->wddev);
	if (ret != 0) {
		dev_err(dev, "cannot register watchdog device (err=%d)\n", ret);
		return ret;
	}

	dev_info(dev, "initialized. heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;
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
	struct iTCO_wdt_private *p = dev_get_drvdata(dev);
	int ret = 0;

	p->suspended = false;
	if (watchdog_active(&p->wddev) && need_suspend()) {
		ret = iTCO_wdt_stop(&p->wddev);
		if (!ret)
			p->suspended = true;
	}
	return ret;
}

static int iTCO_wdt_resume_noirq(struct device *dev)
{
	struct iTCO_wdt_private *p = dev_get_drvdata(dev);

	if (p->suspended)
		iTCO_wdt_start(&p->wddev);

	return 0;
}

static const struct dev_pm_ops iTCO_wdt_pm = {
	.suspend_noirq = iTCO_wdt_suspend_noirq,
	.resume_noirq = iTCO_wdt_resume_noirq,
};

#define ITCO_WDT_PM_OPS	(&iTCO_wdt_pm)
#else
#define ITCO_WDT_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver iTCO_wdt_driver = {
	.probe          = iTCO_wdt_probe,
	.driver         = {
		.name   = DRV_NAME,
		.pm     = ITCO_WDT_PM_OPS,
	},
};

module_platform_driver(iTCO_wdt_driver);

MODULE_AUTHOR("Wim Van Sebroeck <wim@iguana.be>");
MODULE_DESCRIPTION("Intel TCO WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
