// SPDX-License-Identifier: GPL-2.0-only
/*
 *	HPE WatchDog Driver
 *	based on
 *
 *	SoftDog	0.05:	A Software Watchdog Device
 *
 *	(c) Copyright 2018 Hewlett Packard Enterprise Development LP
 *	Thomas Mingarelli <thomas.mingarelli@hpe.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <asm/nmi.h>
#include <linux/crash_dump.h>

#define HPWDT_VERSION			"2.0.4"
#define SECS_TO_TICKS(secs)		((secs) * 1000 / 128)
#define TICKS_TO_SECS(ticks)		((ticks) * 128 / 1000)
#define HPWDT_MAX_TICKS			65535
#define HPWDT_MAX_TIMER			TICKS_TO_SECS(HPWDT_MAX_TICKS)
#define DEFAULT_MARGIN			30
#define PRETIMEOUT_SEC			9

static bool ilo5;
static unsigned int soft_margin = DEFAULT_MARGIN;	/* in seconds */
static bool nowayout = WATCHDOG_NOWAYOUT;
static bool pretimeout = IS_ENABLED(CONFIG_HPWDT_NMI_DECODING);
static int kdumptimeout = -1;

static void __iomem *pci_mem_addr;		/* the PCI-memory address */
static unsigned long __iomem *hpwdt_nmistat;
static unsigned long __iomem *hpwdt_timer_reg;
static unsigned long __iomem *hpwdt_timer_con;

static const struct pci_device_id hpwdt_devices[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_COMPAQ, 0xB203) },	/* iLO2 */
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, 0x3306) },	/* iLO3 */
	{0},			/* terminate list */
};
MODULE_DEVICE_TABLE(pci, hpwdt_devices);

static const struct pci_device_id hpwdt_blacklist[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_HP, 0x3306, PCI_VENDOR_ID_HP, 0x1979) }, /* auxilary iLO */
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_HP, 0x3306, PCI_VENDOR_ID_HP_3PAR, 0x0289) },  /* CL */
	{0},			/* terminate list */
};

static struct watchdog_device hpwdt_dev;
/*
 *	Watchdog operations
 */
static int hpwdt_hw_is_running(void)
{
	return ioread8(hpwdt_timer_con) & 0x01;
}

static int hpwdt_start(struct watchdog_device *wdd)
{
	int control = 0x81 | (pretimeout ? 0x4 : 0);
	int reload = SECS_TO_TICKS(min(wdd->timeout, wdd->max_hw_heartbeat_ms/1000));

	dev_dbg(wdd->parent, "start watchdog 0x%08x:0x%08x:0x%02x\n", wdd->timeout, reload, control);
	iowrite16(reload, hpwdt_timer_reg);
	iowrite8(control, hpwdt_timer_con);

	return 0;
}

static void hpwdt_stop(void)
{
	unsigned long data;

	pr_debug("stop  watchdog\n");

	data = ioread8(hpwdt_timer_con);
	data &= 0xFE;
	iowrite8(data, hpwdt_timer_con);
}

static int hpwdt_stop_core(struct watchdog_device *wdd)
{
	hpwdt_stop();

	return 0;
}

static void hpwdt_ping_ticks(int val)
{
	val = min(val, HPWDT_MAX_TICKS);
	iowrite16(val, hpwdt_timer_reg);
}

static int hpwdt_ping(struct watchdog_device *wdd)
{
	int reload = SECS_TO_TICKS(min(wdd->timeout, wdd->max_hw_heartbeat_ms/1000));

	dev_dbg(wdd->parent, "ping  watchdog 0x%08x:0x%08x\n", wdd->timeout, reload);
	hpwdt_ping_ticks(reload);

	return 0;
}

static unsigned int hpwdt_gettimeleft(struct watchdog_device *wdd)
{
	return TICKS_TO_SECS(ioread16(hpwdt_timer_reg));
}

static int hpwdt_settimeout(struct watchdog_device *wdd, unsigned int val)
{
	dev_dbg(wdd->parent, "set_timeout = %d\n", val);

	wdd->timeout = val;
	if (val <= wdd->pretimeout) {
		dev_dbg(wdd->parent, "pretimeout < timeout. Setting to zero\n");
		wdd->pretimeout = 0;
		pretimeout = 0;
		if (watchdog_active(wdd))
			hpwdt_start(wdd);
	}
	hpwdt_ping(wdd);

	return 0;
}

#ifdef CONFIG_HPWDT_NMI_DECODING
static int hpwdt_set_pretimeout(struct watchdog_device *wdd, unsigned int req)
{
	unsigned int val = 0;

	dev_dbg(wdd->parent, "set_pretimeout = %d\n", req);
	if (req) {
		val = PRETIMEOUT_SEC;
		if (val >= wdd->timeout)
			return -EINVAL;
	}

	if (val != req)
		dev_dbg(wdd->parent, "Rounding pretimeout to: %d\n", val);

	wdd->pretimeout = val;
	pretimeout = !!val;

	if (watchdog_active(wdd))
		hpwdt_start(wdd);

	return 0;
}

static int hpwdt_my_nmi(void)
{
	return ioread8(hpwdt_nmistat) & 0x6;
}

/*
 *	NMI Handler
 */
static int hpwdt_pretimeout(unsigned int ulReason, struct pt_regs *regs)
{
	unsigned int mynmi = hpwdt_my_nmi();
	static char panic_msg[] =
		"00: An NMI occurred. Depending on your system the reason "
		"for the NMI is logged in any one of the following resources:\n"
		"1. Integrated Management Log (IML)\n"
		"2. OA Syslog\n"
		"3. OA Forward Progress Log\n"
		"4. iLO Event Log";

	if (ilo5 && ulReason == NMI_UNKNOWN && !mynmi)
		return NMI_DONE;

	if (ilo5 && !pretimeout && !mynmi)
		return NMI_DONE;

	if (kdumptimeout < 0)
		hpwdt_stop();
	else if (kdumptimeout == 0)
		;
	else {
		unsigned int val = max((unsigned int)kdumptimeout, hpwdt_dev.timeout);
		hpwdt_ping_ticks(SECS_TO_TICKS(val));
	}

	hex_byte_pack(panic_msg, mynmi);
	nmi_panic(regs, panic_msg);

	return NMI_HANDLED;
}
#endif /* CONFIG_HPWDT_NMI_DECODING */


static const struct watchdog_info ident = {
	.options = WDIOF_PRETIMEOUT    |
		   WDIOF_SETTIMEOUT    |
		   WDIOF_KEEPALIVEPING |
		   WDIOF_MAGICCLOSE,
	.identity = "HPE iLO2+ HW Watchdog Timer",
};

/*
 *	Kernel interfaces
 */

static const struct watchdog_ops hpwdt_ops = {
	.owner		= THIS_MODULE,
	.start		= hpwdt_start,
	.stop		= hpwdt_stop_core,
	.ping		= hpwdt_ping,
	.set_timeout	= hpwdt_settimeout,
	.get_timeleft	= hpwdt_gettimeleft,
#ifdef CONFIG_HPWDT_NMI_DECODING
	.set_pretimeout	= hpwdt_set_pretimeout,
#endif
};

static struct watchdog_device hpwdt_dev = {
	.info		= &ident,
	.ops		= &hpwdt_ops,
	.min_timeout	= 1,
	.timeout	= DEFAULT_MARGIN,
	.pretimeout	= PRETIMEOUT_SEC,
	.max_hw_heartbeat_ms	= HPWDT_MAX_TIMER * 1000,
};


/*
 *	Init & Exit
 */

static int hpwdt_init_nmi_decoding(struct pci_dev *dev)
{
#ifdef CONFIG_HPWDT_NMI_DECODING
	int retval;
	/*
	 * Only one function can register for NMI_UNKNOWN
	 */
	retval = register_nmi_handler(NMI_UNKNOWN, hpwdt_pretimeout, 0, "hpwdt");
	if (retval)
		goto error;
	retval = register_nmi_handler(NMI_SERR, hpwdt_pretimeout, 0, "hpwdt");
	if (retval)
		goto error1;
	retval = register_nmi_handler(NMI_IO_CHECK, hpwdt_pretimeout, 0, "hpwdt");
	if (retval)
		goto error2;

	dev_info(&dev->dev,
		"HPE Watchdog Timer Driver: NMI decoding initialized\n");

	return 0;

error2:
	unregister_nmi_handler(NMI_SERR, "hpwdt");
error1:
	unregister_nmi_handler(NMI_UNKNOWN, "hpwdt");
error:
	dev_warn(&dev->dev,
		"Unable to register a die notifier (err=%d).\n",
		retval);
	return retval;
#endif	/* CONFIG_HPWDT_NMI_DECODING */
	return 0;
}

static void hpwdt_exit_nmi_decoding(void)
{
#ifdef CONFIG_HPWDT_NMI_DECODING
	unregister_nmi_handler(NMI_UNKNOWN, "hpwdt");
	unregister_nmi_handler(NMI_SERR, "hpwdt");
	unregister_nmi_handler(NMI_IO_CHECK, "hpwdt");
#endif
}

static int hpwdt_init_one(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	int retval;

	/*
	 * First let's find out if we are on an iLO2+ server. We will
	 * not run on a legacy ASM box.
	 * So we only support the G5 ProLiant servers and higher.
	 */
	if (dev->subsystem_vendor != PCI_VENDOR_ID_HP &&
	    dev->subsystem_vendor != PCI_VENDOR_ID_HP_3PAR) {
		dev_warn(&dev->dev,
			"This server does not have an iLO2+ ASIC.\n");
		return -ENODEV;
	}

	if (pci_match_id(hpwdt_blacklist, dev)) {
		dev_dbg(&dev->dev, "Not supported on this device\n");
		return -ENODEV;
	}

	if (pci_enable_device(dev)) {
		dev_warn(&dev->dev,
			"Not possible to enable PCI Device: 0x%x:0x%x.\n",
			ent->vendor, ent->device);
		return -ENODEV;
	}

	pci_mem_addr = pci_iomap(dev, 1, 0x80);
	if (!pci_mem_addr) {
		dev_warn(&dev->dev,
			"Unable to detect the iLO2+ server memory.\n");
		retval = -ENOMEM;
		goto error_pci_iomap;
	}
	hpwdt_nmistat	= pci_mem_addr + 0x6e;
	hpwdt_timer_reg = pci_mem_addr + 0x70;
	hpwdt_timer_con = pci_mem_addr + 0x72;

	/* Have the core update running timer until user space is ready */
	if (hpwdt_hw_is_running()) {
		dev_info(&dev->dev, "timer is running\n");
		set_bit(WDOG_HW_RUNNING, &hpwdt_dev.status);
	}

	/* Initialize NMI Decoding functionality */
	retval = hpwdt_init_nmi_decoding(dev);
	if (retval != 0)
		goto error_init_nmi_decoding;

	watchdog_stop_on_unregister(&hpwdt_dev);
	watchdog_set_nowayout(&hpwdt_dev, nowayout);
	watchdog_init_timeout(&hpwdt_dev, soft_margin, NULL);

	if (is_kdump_kernel()) {
		pretimeout = 0;
		kdumptimeout = 0;
	}

	if (pretimeout && hpwdt_dev.timeout <= PRETIMEOUT_SEC) {
		dev_warn(&dev->dev, "timeout <= pretimeout. Setting pretimeout to zero\n");
		pretimeout = 0;
	}
	hpwdt_dev.pretimeout = pretimeout ? PRETIMEOUT_SEC : 0;
	kdumptimeout = min(kdumptimeout, HPWDT_MAX_TIMER);

	hpwdt_dev.parent = &dev->dev;
	retval = watchdog_register_device(&hpwdt_dev);
	if (retval < 0)
		goto error_wd_register;

	dev_info(&dev->dev, "HPE Watchdog Timer Driver: Version: %s\n",
				HPWDT_VERSION);
	dev_info(&dev->dev, "timeout: %d seconds (nowayout=%d)\n",
				hpwdt_dev.timeout, nowayout);
	dev_info(&dev->dev, "pretimeout: %s.\n",
				pretimeout ? "on" : "off");
	dev_info(&dev->dev, "kdumptimeout: %d.\n", kdumptimeout);

	if (dev->subsystem_vendor == PCI_VENDOR_ID_HP_3PAR)
		ilo5 = true;

	return 0;

error_wd_register:
	hpwdt_exit_nmi_decoding();
error_init_nmi_decoding:
	pci_iounmap(dev, pci_mem_addr);
error_pci_iomap:
	pci_disable_device(dev);
	return retval;
}

static void hpwdt_exit(struct pci_dev *dev)
{
	watchdog_unregister_device(&hpwdt_dev);
	hpwdt_exit_nmi_decoding();
	pci_iounmap(dev, pci_mem_addr);
	pci_disable_device(dev);
}

static struct pci_driver hpwdt_driver = {
	.name = "hpwdt",
	.id_table = hpwdt_devices,
	.probe = hpwdt_init_one,
	.remove = hpwdt_exit,
};

MODULE_AUTHOR("Tom Mingarelli");
MODULE_DESCRIPTION("hpe watchdog driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(HPWDT_VERSION);

module_param(soft_margin, int, 0);
MODULE_PARM_DESC(soft_margin, "Watchdog timeout in seconds");

module_param_named(timeout, soft_margin, int, 0);
MODULE_PARM_DESC(timeout, "Alias of soft_margin");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

module_param(kdumptimeout, int, 0444);
MODULE_PARM_DESC(kdumptimeout, "Timeout applied for crash kernel transition in seconds");

#ifdef CONFIG_HPWDT_NMI_DECODING
module_param(pretimeout, bool, 0);
MODULE_PARM_DESC(pretimeout, "Watchdog pretimeout enabled");
#endif

module_pci_driver(hpwdt_driver);
