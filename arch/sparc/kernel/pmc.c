/* pmc - Driver implementation for power management functions
 * of Power Management Controller (PMC) on SPARCstation-Voyager.
 *
 * Copyright (c) 2002 Eric Brower (ebrower@usa.net)
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/oplib.h>
#include <linux/uaccess.h>
#include <asm/auxio.h>
#include <asm/processor.h>

/* Debug
 *
 * #define PMC_DEBUG_LED
 * #define PMC_NO_IDLE
 */

#define PMC_OBPNAME	"SUNW,pmc"
#define PMC_DEVNAME	"pmc"

#define PMC_IDLE_REG	0x00
#define PMC_IDLE_ON	0x01

static u8 __iomem *regs;

#define pmc_readb(offs)		(sbus_readb(regs+offs))
#define pmc_writeb(val, offs)	(sbus_writeb(val, regs+offs))

/*
 * CPU idle callback function
 * See .../arch/sparc/kernel/process.c
 */
static void pmc_swift_idle(void)
{
#ifdef PMC_DEBUG_LED
	set_auxio(0x00, AUXIO_LED);
#endif

	pmc_writeb(pmc_readb(PMC_IDLE_REG) | PMC_IDLE_ON, PMC_IDLE_REG);

#ifdef PMC_DEBUG_LED
	set_auxio(AUXIO_LED, 0x00);
#endif
}

static int pmc_probe(struct platform_device *op)
{
	regs = of_ioremap(&op->resource[0], 0,
			  resource_size(&op->resource[0]), PMC_OBPNAME);
	if (!regs) {
		printk(KERN_ERR "%s: unable to map registers\n", PMC_DEVNAME);
		return -ENODEV;
	}

#ifndef PMC_NO_IDLE
	/* Assign power management IDLE handler */
	sparc_idle = pmc_swift_idle;
#endif

	printk(KERN_INFO "%s: power management initialized\n", PMC_DEVNAME);
	return 0;
}

static struct of_device_id pmc_match[] = {
	{
		.name = PMC_OBPNAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, pmc_match);

static struct platform_driver pmc_driver = {
	.driver = {
		.name = "pmc",
		.of_match_table = pmc_match,
	},
	.probe		= pmc_probe,
};

static int __init pmc_init(void)
{
	return platform_driver_register(&pmc_driver);
}

/* This driver is not critical to the boot process
 * and is easiest to ioremap when SBus is already
 * initialized, so we install ourselves thusly:
 */
__initcall(pmc_init);
