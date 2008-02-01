/*
 * Copyright (C) 1997-1998	Mark Lord <mlord@pobox.com>
 * Copyright (C) 1998		Eddie C. Dost <ecd@skynet.be>
 * Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2004		Grant Grundler <grundler at parisc-linux.org>
 *
 * Inspired by an earlier effort from David S. Miller <davem@redhat.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#ifdef CONFIG_SUPERIO
/* SUPERIO 87560 is a PoS chip that NatSem denies exists.
 * Unfortunately, it's built-in on all Astro-based PA-RISC workstations
 * which use the integrated NS87514 cell for CD-ROM support.
 * i.e we have to support for CD-ROM installs.
 * See drivers/parisc/superio.c for more gory details.
 */
#include <asm/superio.h>

static unsigned long superio_ide_status[2];
static unsigned long superio_ide_select[2];
static unsigned long superio_ide_dma_status[2];

#define SUPERIO_IDE_MAX_RETRIES 25

/* Because of a defect in Super I/O, all reads of the PCI DMA status 
 * registers, IDE status register and the IDE select register need to be 
 * retried
 */
static u8 superio_ide_inb (unsigned long port)
{
	if (port == superio_ide_status[0] ||
	    port == superio_ide_status[1] ||
	    port == superio_ide_select[0] ||
	    port == superio_ide_select[1] ||
	    port == superio_ide_dma_status[0] ||
	    port == superio_ide_dma_status[1]) {
		u8 tmp;
		int retries = SUPERIO_IDE_MAX_RETRIES;

		/* printk(" [ reading port 0x%x with retry ] ", port); */

		do {
			tmp = inb(port);
			if (tmp == 0)
				udelay(50);
		} while (tmp == 0 && retries-- > 0);

		return tmp;
	}

	return inb(port);
}

static void __devinit superio_ide_init_iops (struct hwif_s *hwif)
{
	struct pci_dev *pdev = to_pci_dev(hwif->dev);
	u32 base, dmabase;
	u8 port = hwif->channel, tmp;

	base = pci_resource_start(pdev, port * 2) & ~3;
	dmabase = pci_resource_start(pdev, 4) & ~3;

	superio_ide_status[port] = base + IDE_STATUS_OFFSET;
	superio_ide_select[port] = base + IDE_SELECT_OFFSET;
	superio_ide_dma_status[port] = dmabase + (!port ? 2 : 0xa);

	/* Clear error/interrupt, enable dma */
	tmp = superio_ide_inb(superio_ide_dma_status[port]);
	outb(tmp | 0x66, superio_ide_dma_status[port]);

	/* We need to override inb to workaround a SuperIO errata */
	hwif->INB = superio_ide_inb;
}

static void __devinit init_iops_ns87415(ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);

	if (PCI_SLOT(dev->devfn) == 0xE)
		/* Built-in - assume it's under superio. */
		superio_ide_init_iops(hwif);
}
#endif

static unsigned int ns87415_count = 0, ns87415_control[MAX_HWIFS] = { 0 };

/*
 * This routine either enables/disables (according to drive->present)
 * the IRQ associated with the port (HWIF(drive)),
 * and selects either PIO or DMA handshaking for the next I/O operation.
 */
static void ns87415_prepare_drive (ide_drive_t *drive, unsigned int use_dma)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	unsigned int bit, other, new, *old = (unsigned int *) hwif->select_data;
	unsigned long flags;

	local_irq_save(flags);
	new = *old;

	/* Adjust IRQ enable bit */
	bit = 1 << (8 + hwif->channel);
	new = drive->present ? (new & ~bit) : (new | bit);

	/* Select PIO or DMA, DMA may only be selected for one drive/channel. */
	bit   = 1 << (20 + drive->select.b.unit       + (hwif->channel << 1));
	other = 1 << (20 + (1 - drive->select.b.unit) + (hwif->channel << 1));
	new = use_dma ? ((new & ~other) | bit) : (new & ~bit);

	if (new != *old) {
		unsigned char stat;

		/*
		 * Don't change DMA engine settings while Write Buffers
		 * are busy.
		 */
		(void) pci_read_config_byte(dev, 0x43, &stat);
		while (stat & 0x03) {
			udelay(1);
			(void) pci_read_config_byte(dev, 0x43, &stat);
		}

		*old = new;
		(void) pci_write_config_dword(dev, 0x40, new);

		/*
		 * And let things settle...
		 */
		udelay(10);
	}

	local_irq_restore(flags);
}

static void ns87415_selectproc (ide_drive_t *drive)
{
	ns87415_prepare_drive (drive, drive->using_dma);
}

static int ns87415_ide_dma_end (ide_drive_t *drive)
{
	ide_hwif_t      *hwif = HWIF(drive);
	u8 dma_stat = 0, dma_cmd = 0;

	drive->waiting_for_dma = 0;
	dma_stat = hwif->INB(hwif->dma_status);
	/* get dma command mode */
	dma_cmd = hwif->INB(hwif->dma_command);
	/* stop DMA */
	outb(dma_cmd & ~1, hwif->dma_command);
	/* from ERRATA: clear the INTR & ERROR bits */
	dma_cmd = hwif->INB(hwif->dma_command);
	outb(dma_cmd | 6, hwif->dma_command);
	/* and free any DMA resources */
	ide_destroy_dmatable(drive);
	/* verify good DMA status */
	return (dma_stat & 7) != 4;
}

static int ns87415_ide_dma_setup(ide_drive_t *drive)
{
	/* select DMA xfer */
	ns87415_prepare_drive(drive, 1);
	if (!ide_dma_setup(drive))
		return 0;
	/* DMA failed: select PIO xfer */
	ns87415_prepare_drive(drive, 0);
	return 1;
}

static void __devinit init_hwif_ns87415 (ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	unsigned int ctrl, using_inta;
	u8 progif;
#ifdef __sparc_v9__
	int timeout;
	u8 stat;
#endif

	hwif->selectproc = &ns87415_selectproc;

	/*
	 * We cannot probe for IRQ: both ports share common IRQ on INTA.
	 * Also, leave IRQ masked during drive probing, to prevent infinite
	 * interrupts from a potentially floating INTA..
	 *
	 * IRQs get unmasked in selectproc when drive is first used.
	 */
	(void) pci_read_config_dword(dev, 0x40, &ctrl);
	(void) pci_read_config_byte(dev, 0x09, &progif);
	/* is irq in "native" mode? */
	using_inta = progif & (1 << (hwif->channel << 1));
	if (!using_inta)
		using_inta = ctrl & (1 << (4 + hwif->channel));
	if (hwif->mate) {
		hwif->select_data = hwif->mate->select_data;
	} else {
		hwif->select_data = (unsigned long)
					&ns87415_control[ns87415_count++];
		ctrl |= (1 << 8) | (1 << 9);	/* mask both IRQs */
		if (using_inta)
			ctrl &= ~(1 << 6);	/* unmask INTA */
		*((unsigned int *)hwif->select_data) = ctrl;
		(void) pci_write_config_dword(dev, 0x40, ctrl);

		/*
		 * Set prefetch size to 512 bytes for both ports,
		 * but don't turn on/off prefetching here.
		 */
		pci_write_config_byte(dev, 0x55, 0xee);

#ifdef __sparc_v9__
		/*
		 * XXX: Reset the device, if we don't it will not respond to
		 *      SELECT_DRIVE() properly during first ide_probe_port().
		 */
		timeout = 10000;
		outb(12, hwif->io_ports[IDE_CONTROL_OFFSET]);
		udelay(10);
		outb(8, hwif->io_ports[IDE_CONTROL_OFFSET]);
		do {
			udelay(50);
			stat = hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
                	if (stat == 0xff)
                        	break;
        	} while ((stat & BUSY_STAT) && --timeout);
#endif
	}

	if (!using_inta)
		hwif->irq = ide_default_irq(hwif->io_ports[IDE_DATA_OFFSET]);
	else if (!hwif->irq && hwif->mate && hwif->mate->irq)
		hwif->irq = hwif->mate->irq;	/* share IRQ with mate */

	if (!hwif->dma_base)
		return;

	outb(0x60, hwif->dma_status);
	hwif->dma_setup = &ns87415_ide_dma_setup;
	hwif->ide_dma_end = &ns87415_ide_dma_end;
}

static const struct ide_port_info ns87415_chipset __devinitdata = {
	.name		= "NS87415",
#ifdef CONFIG_SUPERIO
	.init_iops	= init_iops_ns87415,
#endif
	.init_hwif	= init_hwif_ns87415,
	.host_flags	= IDE_HFLAG_TRUST_BIOS_FOR_DMA |
			  IDE_HFLAG_NO_ATAPI_DMA |
			  IDE_HFLAG_BOOTABLE,
};

static int __devinit ns87415_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &ns87415_chipset);
}

static const struct pci_device_id ns87415_pci_tbl[] = {
	{ PCI_VDEVICE(NS, PCI_DEVICE_ID_NS_87415), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, ns87415_pci_tbl);

static struct pci_driver driver = {
	.name		= "NS87415_IDE",
	.id_table	= ns87415_pci_tbl,
	.probe		= ns87415_init_one,
};

static int __init ns87415_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(ns87415_ide_init);

MODULE_AUTHOR("Mark Lord, Eddie Dost, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for NS87415 IDE");
MODULE_LICENSE("GPL");
