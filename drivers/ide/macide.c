/*
 *  Macintosh IDE Driver
 *
 *     Copyright (C) 1998 by Michael Schmitz
 *
 *  This driver was written based on information obtained from the MacOS IDE
 *  driver binary by Mikael Forselius
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/macintosh.h>

#define DRV_NAME "mac_ide"

#define IDE_BASE 0x50F1A000	/* Base address of IDE controller */

/*
 * Generic IDE registers as offsets from the base
 * These match MkLinux so they should be correct.
 */

#define IDE_CONTROL	0x38	/* control/altstatus */

/*
 * Mac-specific registers
 */

/*
 * this register is odd; it doesn't seem to do much and it's
 * not word-aligned like virtually every other hardware register
 * on the Mac...
 */

#define IDE_IFR		0x101	/* (0x101) IDE interrupt flags on Quadra:
				 *
				 * Bit 0+1: some interrupt flags
				 * Bit 2+3: some interrupt enable
				 * Bit 4:   ??
				 * Bit 5:   IDE interrupt flag (any hwif)
				 * Bit 6:   maybe IDE interrupt enable (any hwif) ??
				 * Bit 7:   Any interrupt condition
				 */

volatile unsigned char *ide_ifr = (unsigned char *) (IDE_BASE + IDE_IFR);

int macide_test_irq(ide_hwif_t *hwif)
{
	if (*ide_ifr & 0x20)
		return 1;
	return 0;
}

static void macide_clear_irq(ide_drive_t *drive)
{
	*ide_ifr &= ~0x20;
}

static void __init macide_setup_ports(struct ide_hw *hw, unsigned long base,
				      int irq)
{
	int i;

	memset(hw, 0, sizeof(*hw));

	for (i = 0; i < 8; i++)
		hw->io_ports_array[i] = base + i * 4;

	hw->io_ports.ctl_addr = base + IDE_CONTROL;

	hw->irq = irq;
}

static const struct ide_port_ops macide_port_ops = {
	.clear_irq		= macide_clear_irq,
	.test_irq		= macide_test_irq,
};

static const struct ide_port_info macide_port_info = {
	.port_ops		= &macide_port_ops,
	.host_flags		= IDE_HFLAG_MMIO | IDE_HFLAG_NO_DMA,
	.irq_flags		= IRQF_SHARED,
	.chipset		= ide_generic,
};

static const char *mac_ide_name[] =
	{ "Quadra", "Powerbook", "Powerbook Baboon" };

/*
 * Probe for a Macintosh IDE interface
 */

static int mac_ide_probe(struct platform_device *pdev)
{
	struct resource *mem, *irq;
	struct ide_hw hw, *hws[] = { &hw };
	struct ide_port_info d = macide_port_info;
	struct ide_host *host;
	int rc;

	if (!MACH_IS_MAC)
		return -ENODEV;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -ENODEV;

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq)
		return -ENODEV;

	if (!devm_request_mem_region(&pdev->dev, mem->start,
				     resource_size(mem), DRV_NAME)) {
		dev_err(&pdev->dev, "resources busy\n");
		return -EBUSY;
	}

	printk(KERN_INFO "ide: Macintosh %s IDE controller\n",
			 mac_ide_name[macintosh_config->ide_type - 1]);

	macide_setup_ports(&hw, mem->start, irq->start);

	rc = ide_host_add(&d, hws, 1, &host);
	if (rc)
		return rc;

	platform_set_drvdata(pdev, host);
	return 0;
}

static int mac_ide_remove(struct platform_device *pdev)
{
	struct ide_host *host = platform_get_drvdata(pdev);

	ide_host_remove(host);
	return 0;
}

static struct platform_driver mac_ide_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe  = mac_ide_probe,
	.remove = mac_ide_remove,
};

module_platform_driver(mac_ide_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_LICENSE("GPL");
