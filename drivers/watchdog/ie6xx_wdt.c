// SPDX-License-Identifier: GPL-2.0-only
/*
 *      Intel Atom E6xx Watchdog driver
 *
 *      Copyright (C) 2011 Alexander Stein
 *                <alexander.stein@systec-electronic.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>

#define DRIVER_NAME "ie6xx_wdt"

#define PV1	0x00
#define PV2	0x04

#define RR0	0x0c
#define RR1	0x0d
#define WDT_RELOAD	0x01
#define WDT_TOUT	0x02

#define WDTCR	0x10
#define WDT_PRE_SEL	0x04
#define WDT_RESET_SEL	0x08
#define WDT_RESET_EN	0x10
#define WDT_TOUT_EN	0x20

#define DCR	0x14

#define WDTLR	0x18
#define WDT_LOCK	0x01
#define WDT_ENABLE	0x02
#define WDT_TOUT_CNF	0x03

#define MIN_TIME	1
#define MAX_TIME	(10 * 60) /* 10 minutes */
#define DEFAULT_TIME	60

static unsigned int timeout = DEFAULT_TIME;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
		"Default Watchdog timer setting ("
		__MODULE_STRING(DEFAULT_TIME) "s)."
		"The range is from 1 to 600");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static u8 resetmode = 0x10;
module_param(resetmode, byte, 0);
MODULE_PARM_DESC(resetmode,
	"Resetmode bits: 0x08 warm reset (cold reset otherwise), "
	"0x10 reset enable, 0x20 disable toggle GPIO[4] (default=0x10)");

static struct {
	unsigned short sch_wdtba;
	spinlock_t unlock_sequence;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
} ie6xx_wdt_data;

/*
 * This is needed to write to preload and reload registers
 * struct ie6xx_wdt_data.unlock_sequence must be used
 * to prevent sequence interrupts
 */
static void ie6xx_wdt_unlock_registers(void)
{
	outb(0x80, ie6xx_wdt_data.sch_wdtba + RR0);
	outb(0x86, ie6xx_wdt_data.sch_wdtba + RR0);
}

static int ie6xx_wdt_ping(struct watchdog_device *wdd)
{
	spin_lock(&ie6xx_wdt_data.unlock_sequence);
	ie6xx_wdt_unlock_registers();
	outb(WDT_RELOAD, ie6xx_wdt_data.sch_wdtba + RR1);
	spin_unlock(&ie6xx_wdt_data.unlock_sequence);
	return 0;
}

static int ie6xx_wdt_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	u32 preload;
	u64 clock;
	u8 wdtcr;

	/* Watchdog clock is PCI Clock (33MHz) */
	clock = 33000000;
	/* and the preload value is loaded into [34:15] of the down counter */
	preload = (t * clock) >> 15;
	/*
	 * Manual states preload must be one less.
	 * Does not wrap as t is at least 1
	 */
	preload -= 1;

	spin_lock(&ie6xx_wdt_data.unlock_sequence);

	/* Set ResetMode & Enable prescaler for range 10ms to 10 min */
	wdtcr = resetmode & 0x38;
	outb(wdtcr, ie6xx_wdt_data.sch_wdtba + WDTCR);

	ie6xx_wdt_unlock_registers();
	outl(0, ie6xx_wdt_data.sch_wdtba + PV1);

	ie6xx_wdt_unlock_registers();
	outl(preload, ie6xx_wdt_data.sch_wdtba + PV2);

	ie6xx_wdt_unlock_registers();
	outb(WDT_RELOAD | WDT_TOUT, ie6xx_wdt_data.sch_wdtba + RR1);

	spin_unlock(&ie6xx_wdt_data.unlock_sequence);

	wdd->timeout = t;
	return 0;
}

static int ie6xx_wdt_start(struct watchdog_device *wdd)
{
	ie6xx_wdt_set_timeout(wdd, wdd->timeout);

	/* Enable the watchdog timer */
	spin_lock(&ie6xx_wdt_data.unlock_sequence);
	outb(WDT_ENABLE, ie6xx_wdt_data.sch_wdtba + WDTLR);
	spin_unlock(&ie6xx_wdt_data.unlock_sequence);

	return 0;
}

static int ie6xx_wdt_stop(struct watchdog_device *wdd)
{
	if (inb(ie6xx_wdt_data.sch_wdtba + WDTLR) & WDT_LOCK)
		return -1;

	/* Disable the watchdog timer */
	spin_lock(&ie6xx_wdt_data.unlock_sequence);
	outb(0, ie6xx_wdt_data.sch_wdtba + WDTLR);
	spin_unlock(&ie6xx_wdt_data.unlock_sequence);

	return 0;
}

static const struct watchdog_info ie6xx_wdt_info = {
	.identity =	"Intel Atom E6xx Watchdog",
	.options =	WDIOF_SETTIMEOUT |
			WDIOF_MAGICCLOSE |
			WDIOF_KEEPALIVEPING,
};

static const struct watchdog_ops ie6xx_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	ie6xx_wdt_start,
	.stop =		ie6xx_wdt_stop,
	.ping =		ie6xx_wdt_ping,
	.set_timeout =	ie6xx_wdt_set_timeout,
};

static struct watchdog_device ie6xx_wdt_dev = {
	.info =		&ie6xx_wdt_info,
	.ops =		&ie6xx_wdt_ops,
	.min_timeout =	MIN_TIME,
	.max_timeout =	MAX_TIME,
};

#ifdef CONFIG_DEBUG_FS

static int ie6xx_wdt_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "PV1   = 0x%08x\n",
		inl(ie6xx_wdt_data.sch_wdtba + PV1));
	seq_printf(s, "PV2   = 0x%08x\n",
		inl(ie6xx_wdt_data.sch_wdtba + PV2));
	seq_printf(s, "RR    = 0x%08x\n",
		inw(ie6xx_wdt_data.sch_wdtba + RR0));
	seq_printf(s, "WDTCR = 0x%08x\n",
		inw(ie6xx_wdt_data.sch_wdtba + WDTCR));
	seq_printf(s, "DCR   = 0x%08x\n",
		inl(ie6xx_wdt_data.sch_wdtba + DCR));
	seq_printf(s, "WDTLR = 0x%08x\n",
		inw(ie6xx_wdt_data.sch_wdtba + WDTLR));

	seq_printf(s, "\n");
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ie6xx_wdt);

static void ie6xx_wdt_debugfs_init(void)
{
	/* /sys/kernel/debug/ie6xx_wdt */
	ie6xx_wdt_data.debugfs = debugfs_create_file("ie6xx_wdt",
		S_IFREG | S_IRUGO, NULL, NULL, &ie6xx_wdt_fops);
}

static void ie6xx_wdt_debugfs_exit(void)
{
	debugfs_remove(ie6xx_wdt_data.debugfs);
}

#else
static void ie6xx_wdt_debugfs_init(void)
{
}

static void ie6xx_wdt_debugfs_exit(void)
{
}
#endif

static int ie6xx_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	u8 wdtlr;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -ENODEV;

	if (!request_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "Watchdog region 0x%llx already in use!\n",
			(u64)res->start);
		return -EBUSY;
	}

	ie6xx_wdt_data.sch_wdtba = res->start;
	dev_dbg(&pdev->dev, "WDT = 0x%X\n", ie6xx_wdt_data.sch_wdtba);

	ie6xx_wdt_dev.timeout = timeout;
	watchdog_set_nowayout(&ie6xx_wdt_dev, nowayout);
	ie6xx_wdt_dev.parent = &pdev->dev;

	spin_lock_init(&ie6xx_wdt_data.unlock_sequence);

	wdtlr = inb(ie6xx_wdt_data.sch_wdtba + WDTLR);
	if (wdtlr & WDT_LOCK)
		dev_warn(&pdev->dev,
			"Watchdog Timer is Locked (Reg=0x%x)\n", wdtlr);

	ie6xx_wdt_debugfs_init();

	ret = watchdog_register_device(&ie6xx_wdt_dev);
	if (ret)
		goto misc_register_error;

	return 0;

misc_register_error:
	ie6xx_wdt_debugfs_exit();
	release_region(res->start, resource_size(res));
	ie6xx_wdt_data.sch_wdtba = 0;
	return ret;
}

static void ie6xx_wdt_remove(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	ie6xx_wdt_stop(NULL);
	watchdog_unregister_device(&ie6xx_wdt_dev);
	ie6xx_wdt_debugfs_exit();
	release_region(res->start, resource_size(res));
	ie6xx_wdt_data.sch_wdtba = 0;
}

static struct platform_driver ie6xx_wdt_driver = {
	.probe		= ie6xx_wdt_probe,
	.remove_new	= ie6xx_wdt_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __init ie6xx_wdt_init(void)
{
	/* Check boot parameters to verify that their initial values */
	/* are in range. */
	if ((timeout < MIN_TIME) ||
	    (timeout > MAX_TIME)) {
		pr_err("Watchdog timer: value of timeout %d (dec) "
		  "is out of range from %d to %d (dec)\n",
		  timeout, MIN_TIME, MAX_TIME);
		return -EINVAL;
	}

	return platform_driver_register(&ie6xx_wdt_driver);
}

static void __exit ie6xx_wdt_exit(void)
{
	platform_driver_unregister(&ie6xx_wdt_driver);
}

late_initcall(ie6xx_wdt_init);
module_exit(ie6xx_wdt_exit);

MODULE_AUTHOR("Alexander Stein <alexander.stein@systec-electronic.com>");
MODULE_DESCRIPTION("Intel Atom E6xx Watchdog Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
