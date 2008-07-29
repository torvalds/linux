/*
 *  Created 20 Oct 2004 by Mark Lord
 *
 *  Basic support for Delkin/ASKA/Workbit Cardbus CompactFlash adapter
 *
 *  Modeled after the 16-bit PCMCIA driver: ide-cs.c
 *
 *  This is slightly peculiar, in that it is a PCI driver,
 *  but is NOT an IDE PCI driver -- the IDE layer does not directly
 *  support hot insertion/removal of PCI interfaces, so this driver
 *  is unable to use the IDE PCI interfaces.  Instead, it uses the
 *  same interfaces as the ide-cs (PCMCIA) driver uses.
 *  On the plus side, the driver is also smaller/simpler this way.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/io.h>

/*
 * No chip documentation has yet been found,
 * so these configuration values were pulled from
 * a running Win98 system using "debug".
 * This gives around 3MByte/second read performance,
 * which is about 2/3 of what the chip is capable of.
 *
 * There is also a 4KByte mmio region on the card,
 * but its purpose has yet to be reverse-engineered.
 */
static const u8 setup[] = {
	0x00, 0x05, 0xbe, 0x01, 0x20, 0x8f, 0x00, 0x00,
	0xa4, 0x1f, 0xb3, 0x1b, 0x00, 0x00, 0x00, 0x80,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xa4, 0x83, 0x02, 0x13,
};

static const struct ide_port_ops delkin_cb_port_ops = {
	.quirkproc		= ide_undecoded_slave,
};

static const struct ide_port_info delkin_cb_port_info = {
	.port_ops		= &delkin_cb_port_ops,
	.host_flags		= IDE_HFLAG_IO_32BIT | IDE_HFLAG_UNMASK_IRQS |
				  IDE_HFLAG_NO_DMA,
};

static int __devinit
delkin_cb_probe (struct pci_dev *dev, const struct pci_device_id *id)
{
	struct ide_host *host;
	unsigned long base;
	int i, rc;
	hw_regs_t hw, *hws[] = { &hw, NULL, NULL, NULL };

	rc = pci_enable_device(dev);
	if (rc) {
		printk(KERN_ERR "delkin_cb: pci_enable_device failed (%d)\n", rc);
		return rc;
	}
	rc = pci_request_regions(dev, "delkin_cb");
	if (rc) {
		printk(KERN_ERR "delkin_cb: pci_request_regions failed (%d)\n", rc);
		pci_disable_device(dev);
		return rc;
	}
	base = pci_resource_start(dev, 0);
	outb(0x02, base + 0x1e);	/* set nIEN to block interrupts */
	inb(base + 0x17);		/* read status to clear interrupts */
	for (i = 0; i < sizeof(setup); ++i) {
		if (setup[i])
			outb(setup[i], base + i);
	}

	memset(&hw, 0, sizeof(hw));
	ide_std_init_ports(&hw, base + 0x10, base + 0x1e);
	hw.irq = dev->irq;
	hw.dev = &dev->dev;
	hw.chipset = ide_pci;		/* this enables IRQ sharing */

	rc = ide_host_add(&delkin_cb_port_info, hws, &host);
	if (rc)
		goto out_disable;

	pci_set_drvdata(dev, host);

	return 0;

out_disable:
	pci_release_regions(dev);
	pci_disable_device(dev);
	return rc;
}

static void
delkin_cb_remove (struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);

	ide_host_remove(host);

	pci_release_regions(dev);
	pci_disable_device(dev);
}

static struct pci_device_id delkin_cb_pci_tbl[] __devinitdata = {
	{ 0x1145, 0xf021, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0x1145, 0xf024, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, delkin_cb_pci_tbl);

static struct pci_driver driver = {
	.name		= "Delkin-ASKA-Workbit Cardbus IDE",
	.id_table	= delkin_cb_pci_tbl,
	.probe		= delkin_cb_probe,
	.remove		= delkin_cb_remove,
};

static int __init delkin_cb_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit delkin_cb_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(delkin_cb_init);
module_exit(delkin_cb_exit);

MODULE_AUTHOR("Mark Lord");
MODULE_DESCRIPTION("Basic support for Delkin/ASKA/Workbit Cardbus IDE");
MODULE_LICENSE("GPL");

