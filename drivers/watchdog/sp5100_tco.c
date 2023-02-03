// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	sp5100_tco :	TCO timer driver for sp5100 chipsets
 *
 *	(c) Copyright 2009 Google Inc., All Rights Reserved.
 *
 *	Based on i8xx_tco.c:
 *	(c) Copyright 2000 kernel concepts <nils@kernelconcepts.de>, All Rights
 *	Reserved.
 *				https://www.kernelconcepts.de
 *
 *	See AMD Publication 43009 "AMD SB700/710/750 Register Reference Guide",
 *	    AMD Publication 44413 "AMD SP5100 Register Reference Guide"
 *	    AMD Publication 45482 "AMD SB800-Series Southbridges Register
 *	                                                      Reference Guide"
 *	    AMD Publication 48751 "BIOS and Kernel Developer’s Guide (BKDG)
 *				for AMD Family 16h Models 00h-0Fh Processors"
 *	    AMD Publication 51192 "AMD Bolton FCH Register Reference Guide"
 *	    AMD Publication 52740 "BIOS and Kernel Developer’s Guide (BKDG)
 *				for AMD Family 16h Models 30h-3Fh Processors"
 *	    AMD Publication 55570-B1-PUB "Processor Programming Reference (PPR)
 *				for AMD Family 17h Model 18h, Revision B1
 *				Processors (PUB)
 *	    AMD Publication 55772-A1-PUB "Processor Programming Reference (PPR)
 *				for AMD Family 17h Model 20h, Revision A1
 *				Processors (PUB)
 */

/*
 *	Includes, defines, variables, module parameters, ...
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#include "sp5100_tco.h"

#define TCO_DRIVER_NAME	"sp5100-tco"

/* internal variables */

enum tco_reg_layout {
	sp5100, sb800, efch, efch_mmio
};

struct sp5100_tco {
	struct watchdog_device wdd;
	void __iomem *tcobase;
	enum tco_reg_layout tco_reg_layout;
};

/* the watchdog platform device */
static struct platform_device *sp5100_tco_platform_device;
/* the associated PCI device */
static struct pci_dev *sp5100_tco_pci;

/* module parameters */

#define WATCHDOG_ACTION 0
static bool action = WATCHDOG_ACTION;
module_param(action, bool, 0);
MODULE_PARM_DESC(action, "Action taken when watchdog expires, 0 to reset, 1 to poweroff (default="
		 __MODULE_STRING(WATCHDOG_ACTION) ")");

#define WATCHDOG_HEARTBEAT 60	/* 60 sec default heartbeat. */
static int heartbeat = WATCHDOG_HEARTBEAT;  /* in seconds */
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. (default="
		 __MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started."
		" (default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/*
 * Some TCO specific functions
 */

static enum tco_reg_layout tco_reg_layout(struct pci_dev *dev)
{
	if (dev->vendor == PCI_VENDOR_ID_ATI &&
	    dev->device == PCI_DEVICE_ID_ATI_SBX00_SMBUS &&
	    dev->revision < 0x40) {
		return sp5100;
	} else if (dev->vendor == PCI_VENDOR_ID_AMD &&
	    sp5100_tco_pci->device == PCI_DEVICE_ID_AMD_KERNCZ_SMBUS &&
	    sp5100_tco_pci->revision >= AMD_ZEN_SMBUS_PCI_REV) {
		return efch_mmio;
	} else if (dev->vendor == PCI_VENDOR_ID_AMD &&
	    ((dev->device == PCI_DEVICE_ID_AMD_HUDSON2_SMBUS &&
	     dev->revision >= 0x41) ||
	    (dev->device == PCI_DEVICE_ID_AMD_KERNCZ_SMBUS &&
	     dev->revision >= 0x49))) {
		return efch;
	}
	return sb800;
}

static int tco_timer_start(struct watchdog_device *wdd)
{
	struct sp5100_tco *tco = watchdog_get_drvdata(wdd);
	u32 val;

	val = readl(SP5100_WDT_CONTROL(tco->tcobase));
	val |= SP5100_WDT_START_STOP_BIT;
	writel(val, SP5100_WDT_CONTROL(tco->tcobase));

	return 0;
}

static int tco_timer_stop(struct watchdog_device *wdd)
{
	struct sp5100_tco *tco = watchdog_get_drvdata(wdd);
	u32 val;

	val = readl(SP5100_WDT_CONTROL(tco->tcobase));
	val &= ~SP5100_WDT_START_STOP_BIT;
	writel(val, SP5100_WDT_CONTROL(tco->tcobase));

	return 0;
}

static int tco_timer_ping(struct watchdog_device *wdd)
{
	struct sp5100_tco *tco = watchdog_get_drvdata(wdd);
	u32 val;

	val = readl(SP5100_WDT_CONTROL(tco->tcobase));
	val |= SP5100_WDT_TRIGGER_BIT;
	writel(val, SP5100_WDT_CONTROL(tco->tcobase));

	return 0;
}

static int tco_timer_set_timeout(struct watchdog_device *wdd,
				 unsigned int t)
{
	struct sp5100_tco *tco = watchdog_get_drvdata(wdd);

	/* Write new heartbeat to watchdog */
	writel(t, SP5100_WDT_COUNT(tco->tcobase));

	wdd->timeout = t;

	return 0;
}

static unsigned int tco_timer_get_timeleft(struct watchdog_device *wdd)
{
	struct sp5100_tco *tco = watchdog_get_drvdata(wdd);

	return readl(SP5100_WDT_COUNT(tco->tcobase));
}

static u8 sp5100_tco_read_pm_reg8(u8 index)
{
	outb(index, SP5100_IO_PM_INDEX_REG);
	return inb(SP5100_IO_PM_DATA_REG);
}

static void sp5100_tco_update_pm_reg8(u8 index, u8 reset, u8 set)
{
	u8 val;

	outb(index, SP5100_IO_PM_INDEX_REG);
	val = inb(SP5100_IO_PM_DATA_REG);
	val &= reset;
	val |= set;
	outb(val, SP5100_IO_PM_DATA_REG);
}

static void tco_timer_enable(struct sp5100_tco *tco)
{
	u32 val;

	switch (tco->tco_reg_layout) {
	case sb800:
		/* For SB800 or later */
		/* Set the Watchdog timer resolution to 1 sec */
		sp5100_tco_update_pm_reg8(SB800_PM_WATCHDOG_CONFIG,
					  0xff, SB800_PM_WATCHDOG_SECOND_RES);

		/* Enable watchdog decode bit and watchdog timer */
		sp5100_tco_update_pm_reg8(SB800_PM_WATCHDOG_CONTROL,
					  ~SB800_PM_WATCHDOG_DISABLE,
					  SB800_PCI_WATCHDOG_DECODE_EN);
		break;
	case sp5100:
		/* For SP5100 or SB7x0 */
		/* Enable watchdog decode bit */
		pci_read_config_dword(sp5100_tco_pci,
				      SP5100_PCI_WATCHDOG_MISC_REG,
				      &val);

		val |= SP5100_PCI_WATCHDOG_DECODE_EN;

		pci_write_config_dword(sp5100_tco_pci,
				       SP5100_PCI_WATCHDOG_MISC_REG,
				       val);

		/* Enable Watchdog timer and set the resolution to 1 sec */
		sp5100_tco_update_pm_reg8(SP5100_PM_WATCHDOG_CONTROL,
					  ~SP5100_PM_WATCHDOG_DISABLE,
					  SP5100_PM_WATCHDOG_SECOND_RES);
		break;
	case efch:
		/* Set the Watchdog timer resolution to 1 sec and enable */
		sp5100_tco_update_pm_reg8(EFCH_PM_DECODEEN3,
					  ~EFCH_PM_WATCHDOG_DISABLE,
					  EFCH_PM_DECODEEN_SECOND_RES);
		break;
	default:
		break;
	}
}

static u32 sp5100_tco_read_pm_reg32(u8 index)
{
	u32 val = 0;
	int i;

	for (i = 3; i >= 0; i--)
		val = (val << 8) + sp5100_tco_read_pm_reg8(index + i);

	return val;
}

static u32 sp5100_tco_request_region(struct device *dev,
				     u32 mmio_addr,
				     const char *dev_name)
{
	if (!devm_request_mem_region(dev, mmio_addr, SP5100_WDT_MEM_MAP_SIZE,
				     dev_name)) {
		dev_dbg(dev, "MMIO address 0x%08x already in use\n", mmio_addr);
		return 0;
	}

	return mmio_addr;
}

static u32 sp5100_tco_prepare_base(struct sp5100_tco *tco,
				   u32 mmio_addr,
				   u32 alt_mmio_addr,
				   const char *dev_name)
{
	struct device *dev = tco->wdd.parent;

	dev_dbg(dev, "Got 0x%08x from SBResource_MMIO register\n", mmio_addr);

	if (!mmio_addr && !alt_mmio_addr)
		return -ENODEV;

	/* Check for MMIO address and alternate MMIO address conflicts */
	if (mmio_addr)
		mmio_addr = sp5100_tco_request_region(dev, mmio_addr, dev_name);

	if (!mmio_addr && alt_mmio_addr)
		mmio_addr = sp5100_tco_request_region(dev, alt_mmio_addr, dev_name);

	if (!mmio_addr) {
		dev_err(dev, "Failed to reserve MMIO or alternate MMIO region\n");
		return -EBUSY;
	}

	tco->tcobase = devm_ioremap(dev, mmio_addr, SP5100_WDT_MEM_MAP_SIZE);
	if (!tco->tcobase) {
		dev_err(dev, "MMIO address 0x%08x failed mapping\n", mmio_addr);
		devm_release_mem_region(dev, mmio_addr, SP5100_WDT_MEM_MAP_SIZE);
		return -ENOMEM;
	}

	dev_info(dev, "Using 0x%08x for watchdog MMIO address\n", mmio_addr);

	return 0;
}

static int sp5100_tco_timer_init(struct sp5100_tco *tco)
{
	struct watchdog_device *wdd = &tco->wdd;
	struct device *dev = wdd->parent;
	u32 val;

	val = readl(SP5100_WDT_CONTROL(tco->tcobase));
	if (val & SP5100_WDT_DISABLED) {
		dev_err(dev, "Watchdog hardware is disabled\n");
		return -ENODEV;
	}

	/*
	 * Save WatchDogFired status, because WatchDogFired flag is
	 * cleared here.
	 */
	if (val & SP5100_WDT_FIRED)
		wdd->bootstatus = WDIOF_CARDRESET;

	/* Set watchdog action */
	if (action)
		val |= SP5100_WDT_ACTION_RESET;
	else
		val &= ~SP5100_WDT_ACTION_RESET;
	writel(val, SP5100_WDT_CONTROL(tco->tcobase));

	/* Set a reasonable heartbeat before we stop the timer */
	tco_timer_set_timeout(wdd, wdd->timeout);

	/*
	 * Stop the TCO before we change anything so we don't race with
	 * a zeroed timer.
	 */
	tco_timer_stop(wdd);

	return 0;
}

static u8 efch_read_pm_reg8(void __iomem *addr, u8 index)
{
	return readb(addr + index);
}

static void efch_update_pm_reg8(void __iomem *addr, u8 index, u8 reset, u8 set)
{
	u8 val;

	val = readb(addr + index);
	val &= reset;
	val |= set;
	writeb(val, addr + index);
}

static void tco_timer_enable_mmio(void __iomem *addr)
{
	efch_update_pm_reg8(addr, EFCH_PM_DECODEEN3,
			    ~EFCH_PM_WATCHDOG_DISABLE,
			    EFCH_PM_DECODEEN_SECOND_RES);
}

static int sp5100_tco_setupdevice_mmio(struct device *dev,
				       struct watchdog_device *wdd)
{
	struct sp5100_tco *tco = watchdog_get_drvdata(wdd);
	const char *dev_name = SB800_DEVNAME;
	u32 mmio_addr = 0, alt_mmio_addr = 0;
	struct resource *res;
	void __iomem *addr;
	int ret;
	u32 val;

	res = request_mem_region_muxed(EFCH_PM_ACPI_MMIO_PM_ADDR,
				       EFCH_PM_ACPI_MMIO_PM_SIZE,
				       "sp5100_tco");

	if (!res) {
		dev_err(dev,
			"Memory region 0x%08x already in use\n",
			EFCH_PM_ACPI_MMIO_PM_ADDR);
		return -EBUSY;
	}

	addr = ioremap(EFCH_PM_ACPI_MMIO_PM_ADDR, EFCH_PM_ACPI_MMIO_PM_SIZE);
	if (!addr) {
		dev_err(dev, "Address mapping failed\n");
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * EFCH_PM_DECODEEN_WDT_TMREN is dual purpose. This bitfield
	 * enables sp5100_tco register MMIO space decoding. The bitfield
	 * also starts the timer operation. Enable if not already enabled.
	 */
	val = efch_read_pm_reg8(addr, EFCH_PM_DECODEEN);
	if (!(val & EFCH_PM_DECODEEN_WDT_TMREN)) {
		efch_update_pm_reg8(addr, EFCH_PM_DECODEEN, 0xff,
				    EFCH_PM_DECODEEN_WDT_TMREN);
	}

	/* Error if the timer could not be enabled */
	val = efch_read_pm_reg8(addr, EFCH_PM_DECODEEN);
	if (!(val & EFCH_PM_DECODEEN_WDT_TMREN)) {
		dev_err(dev, "Failed to enable the timer\n");
		ret = -EFAULT;
		goto out;
	}

	mmio_addr = EFCH_PM_WDT_ADDR;

	/* Determine alternate MMIO base address */
	val = efch_read_pm_reg8(addr, EFCH_PM_ISACONTROL);
	if (val & EFCH_PM_ISACONTROL_MMIOEN)
		alt_mmio_addr = EFCH_PM_ACPI_MMIO_ADDR +
			EFCH_PM_ACPI_MMIO_WDT_OFFSET;

	ret = sp5100_tco_prepare_base(tco, mmio_addr, alt_mmio_addr, dev_name);
	if (!ret) {
		tco_timer_enable_mmio(addr);
		ret = sp5100_tco_timer_init(tco);
	}

out:
	if (addr)
		iounmap(addr);

	release_resource(res);
	kfree(res);

	return ret;
}

static int sp5100_tco_setupdevice(struct device *dev,
				  struct watchdog_device *wdd)
{
	struct sp5100_tco *tco = watchdog_get_drvdata(wdd);
	const char *dev_name;
	u32 mmio_addr = 0, val;
	u32 alt_mmio_addr = 0;
	int ret;

	if (tco->tco_reg_layout == efch_mmio)
		return sp5100_tco_setupdevice_mmio(dev, wdd);

	/* Request the IO ports used by this driver */
	if (!request_muxed_region(SP5100_IO_PM_INDEX_REG,
				  SP5100_PM_IOPORTS_SIZE, "sp5100_tco")) {
		dev_err(dev, "I/O address 0x%04x already in use\n",
			SP5100_IO_PM_INDEX_REG);
		return -EBUSY;
	}

	/*
	 * Determine type of southbridge chipset.
	 */
	switch (tco->tco_reg_layout) {
	case sp5100:
		dev_name = SP5100_DEVNAME;
		mmio_addr = sp5100_tco_read_pm_reg32(SP5100_PM_WATCHDOG_BASE) &
								0xfffffff8;

		/*
		 * Secondly, find the watchdog timer MMIO address
		 * from SBResource_MMIO register.
		 */

		/* Read SBResource_MMIO from PCI config(PCI_Reg: 9Ch) */
		pci_read_config_dword(sp5100_tco_pci,
				      SP5100_SB_RESOURCE_MMIO_BASE,
				      &val);

		/* Verify MMIO is enabled and using bar0 */
		if ((val & SB800_ACPI_MMIO_MASK) == SB800_ACPI_MMIO_DECODE_EN)
			alt_mmio_addr = (val & ~0xfff) + SB800_PM_WDT_MMIO_OFFSET;
		break;
	case sb800:
		dev_name = SB800_DEVNAME;
		mmio_addr = sp5100_tco_read_pm_reg32(SB800_PM_WATCHDOG_BASE) &
								0xfffffff8;

		/* Read SBResource_MMIO from AcpiMmioEn(PM_Reg: 24h) */
		val = sp5100_tco_read_pm_reg32(SB800_PM_ACPI_MMIO_EN);

		/* Verify MMIO is enabled and using bar0 */
		if ((val & SB800_ACPI_MMIO_MASK) == SB800_ACPI_MMIO_DECODE_EN)
			alt_mmio_addr = (val & ~0xfff) + SB800_PM_WDT_MMIO_OFFSET;
		break;
	case efch:
		dev_name = SB800_DEVNAME;
		val = sp5100_tco_read_pm_reg8(EFCH_PM_DECODEEN);
		if (val & EFCH_PM_DECODEEN_WDT_TMREN)
			mmio_addr = EFCH_PM_WDT_ADDR;

		val = sp5100_tco_read_pm_reg8(EFCH_PM_ISACONTROL);
		if (val & EFCH_PM_ISACONTROL_MMIOEN)
			alt_mmio_addr = EFCH_PM_ACPI_MMIO_ADDR +
				EFCH_PM_ACPI_MMIO_WDT_OFFSET;
		break;
	default:
		return -ENODEV;
	}

	ret = sp5100_tco_prepare_base(tco, mmio_addr, alt_mmio_addr, dev_name);
	if (!ret) {
		/* Setup the watchdog timer */
		tco_timer_enable(tco);
		ret = sp5100_tco_timer_init(tco);
	}

	release_region(SP5100_IO_PM_INDEX_REG, SP5100_PM_IOPORTS_SIZE);
	return ret;
}

static struct watchdog_info sp5100_tco_wdt_info = {
	.identity = "SP5100 TCO timer",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops sp5100_tco_wdt_ops = {
	.owner = THIS_MODULE,
	.start = tco_timer_start,
	.stop = tco_timer_stop,
	.ping = tco_timer_ping,
	.set_timeout = tco_timer_set_timeout,
	.get_timeleft = tco_timer_get_timeleft,
};

static int sp5100_tco_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdd;
	struct sp5100_tco *tco;
	int ret;

	tco = devm_kzalloc(dev, sizeof(*tco), GFP_KERNEL);
	if (!tco)
		return -ENOMEM;

	tco->tco_reg_layout = tco_reg_layout(sp5100_tco_pci);

	wdd = &tco->wdd;
	wdd->parent = dev;
	wdd->info = &sp5100_tco_wdt_info;
	wdd->ops = &sp5100_tco_wdt_ops;
	wdd->timeout = WATCHDOG_HEARTBEAT;
	wdd->min_timeout = 1;
	wdd->max_timeout = 0xffff;

	watchdog_init_timeout(wdd, heartbeat, NULL);
	watchdog_set_nowayout(wdd, nowayout);
	watchdog_stop_on_reboot(wdd);
	watchdog_stop_on_unregister(wdd);
	watchdog_set_drvdata(wdd, tco);

	ret = sp5100_tco_setupdevice(dev, wdd);
	if (ret)
		return ret;

	ret = devm_watchdog_register_device(dev, wdd);
	if (ret)
		return ret;

	/* Show module parameters */
	dev_info(dev, "initialized. heartbeat=%d sec (nowayout=%d)\n",
		 wdd->timeout, nowayout);

	return 0;
}

static struct platform_driver sp5100_tco_driver = {
	.probe		= sp5100_tco_probe,
	.driver		= {
		.name	= TCO_DRIVER_NAME,
	},
};

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might
 * want to register another driver on the same PCI id.
 */
static const struct pci_device_id sp5100_tco_pci_tbl[] = {
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_SBX00_SMBUS, PCI_ANY_ID,
	  PCI_ANY_ID, },
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_HUDSON2_SMBUS, PCI_ANY_ID,
	  PCI_ANY_ID, },
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_KERNCZ_SMBUS, PCI_ANY_ID,
	  PCI_ANY_ID, },
	{ 0, },			/* End of list */
};
MODULE_DEVICE_TABLE(pci, sp5100_tco_pci_tbl);

static int __init sp5100_tco_init(void)
{
	struct pci_dev *dev = NULL;
	int err;

	/* Match the PCI device */
	for_each_pci_dev(dev) {
		if (pci_match_id(sp5100_tco_pci_tbl, dev) != NULL) {
			sp5100_tco_pci = dev;
			break;
		}
	}

	if (!sp5100_tco_pci)
		return -ENODEV;

	pr_info("SP5100/SB800 TCO WatchDog Timer Driver\n");

	err = platform_driver_register(&sp5100_tco_driver);
	if (err)
		return err;

	sp5100_tco_platform_device =
		platform_device_register_simple(TCO_DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(sp5100_tco_platform_device)) {
		err = PTR_ERR(sp5100_tco_platform_device);
		goto unreg_platform_driver;
	}

	return 0;

unreg_platform_driver:
	platform_driver_unregister(&sp5100_tco_driver);
	return err;
}

static void __exit sp5100_tco_exit(void)
{
	platform_device_unregister(sp5100_tco_platform_device);
	platform_driver_unregister(&sp5100_tco_driver);
}

module_init(sp5100_tco_init);
module_exit(sp5100_tco_exit);

MODULE_AUTHOR("Priyanka Gupta");
MODULE_DESCRIPTION("TCO timer driver for SP5100/SB800 chipset");
MODULE_LICENSE("GPL");
