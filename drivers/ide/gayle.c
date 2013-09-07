/*
 *  Amiga Gayle IDE Driver
 *
 *     Created 9 Jul 1997 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/zorro.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amigayle.h>


    /*
     *  Offsets from one of the above bases
     */

#define GAYLE_CONTROL	0x101a

    /*
     *  These are at different offsets from the base
     */

#define GAYLE_IRQ_4000	0xdd3020	/* MSB = 1, Harddisk is source of */
#define GAYLE_IRQ_1200	0xda9000	/* interrupt */


    /*
     *  Offset of the secondary port for IDE doublers
     *  Note that GAYLE_CONTROL is NOT available then!
     */

#define GAYLE_NEXT_PORT	0x1000

#define GAYLE_NUM_HWIFS		2
#define GAYLE_NUM_PROBE_HWIFS	(ide_doubler ? GAYLE_NUM_HWIFS : \
					       GAYLE_NUM_HWIFS-1)
#define GAYLE_HAS_CONTROL_REG	(!ide_doubler)

static bool ide_doubler;
module_param_named(doubler, ide_doubler, bool, 0);
MODULE_PARM_DESC(doubler, "enable support for IDE doublers");

    /*
     *  Check and acknowledge the interrupt status
     */

static int gayle_test_irq(ide_hwif_t *hwif)
{
	unsigned char ch;

	ch = z_readb(hwif->io_ports.irq_addr);
	if (!(ch & GAYLE_IRQ_IDE))
		return 0;
	return 1;
}

static void gayle_a1200_clear_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;

	(void)z_readb(hwif->io_ports.status_addr);
	z_writeb(0x7c, hwif->io_ports.irq_addr);
}

static void __init gayle_setup_ports(struct ide_hw *hw, unsigned long base,
				     unsigned long ctl, unsigned long irq_port)
{
	int i;

	memset(hw, 0, sizeof(*hw));

	hw->io_ports.data_addr = base;

	for (i = 1; i < 8; i++)
		hw->io_ports_array[i] = base + 2 + i * 4;

	hw->io_ports.ctl_addr = ctl;
	hw->io_ports.irq_addr = irq_port;

	hw->irq = IRQ_AMIGA_PORTS;
}

static const struct ide_port_ops gayle_a4000_port_ops = {
	.test_irq		= gayle_test_irq,
};

static const struct ide_port_ops gayle_a1200_port_ops = {
	.clear_irq		= gayle_a1200_clear_irq,
	.test_irq		= gayle_test_irq,
};

static const struct ide_port_info gayle_port_info = {
	.host_flags		= IDE_HFLAG_MMIO | IDE_HFLAG_SERIALIZE |
				  IDE_HFLAG_NO_DMA,
	.irq_flags		= IRQF_SHARED,
	.chipset		= ide_generic,
};

    /*
     *  Probe for a Gayle IDE interface (and optionally for an IDE doubler)
     */

static int __init amiga_gayle_ide_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct gayle_ide_platform_data *pdata;
	unsigned long base, ctrlport, irqport;
	unsigned int i;
	int error;
	struct ide_hw hw[GAYLE_NUM_HWIFS], *hws[GAYLE_NUM_HWIFS];
	struct ide_port_info d = gayle_port_info;
	struct ide_host *host;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	if (!request_mem_region(res->start, resource_size(res), "IDE"))
		return -EBUSY;

	pdata = pdev->dev.platform_data;
	pr_info("ide: Gayle IDE controller (A%u style%s)\n",
		pdata->explicit_ack ? 1200 : 4000,
		ide_doubler ? ", IDE doubler" : "");

	base = (unsigned long)ZTWO_VADDR(pdata->base);
	ctrlport = 0;
	irqport = (unsigned long)ZTWO_VADDR(pdata->irqport);
	if (pdata->explicit_ack)
		d.port_ops = &gayle_a1200_port_ops;
	else
		d.port_ops = &gayle_a4000_port_ops;

	for (i = 0; i < GAYLE_NUM_PROBE_HWIFS; i++, base += GAYLE_NEXT_PORT) {
		if (GAYLE_HAS_CONTROL_REG)
			ctrlport = base + GAYLE_CONTROL;

		gayle_setup_ports(&hw[i], base, ctrlport, irqport);
		hws[i] = &hw[i];
	}

	error = ide_host_add(&d, hws, i, &host);
	if (error)
		goto out;

	platform_set_drvdata(pdev, host);
	return 0;

out:
	release_mem_region(res->start, resource_size(res));
	return error;
}

static int __exit amiga_gayle_ide_remove(struct platform_device *pdev)
{
	struct ide_host *host = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ide_host_remove(host);
	release_mem_region(res->start, resource_size(res));
	return 0;
}

static struct platform_driver amiga_gayle_ide_driver = {
	.remove = __exit_p(amiga_gayle_ide_remove),
	.driver   = {
		.name	= "amiga-gayle-ide",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver_probe(amiga_gayle_ide_driver, amiga_gayle_ide_probe);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:amiga-gayle-ide");
