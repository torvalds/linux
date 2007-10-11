/*
 * linux/drivers/ide/pci/hpt34x.c		Version 0.40	Sept 10, 2002
 *
 * Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 * May be copied or modified under the terms of the GNU General Public License
 *
 *
 * 00:12.0 Unknown mass storage controller:
 * Triones Technologies, Inc.
 * Unknown device 0003 (rev 01)
 *
 * hde: UDMA 2 (0x0000 0x0002) (0x0000 0x0010)
 * hdf: UDMA 2 (0x0002 0x0012) (0x0010 0x0030)
 * hde: DMA 2  (0x0000 0x0002) (0x0000 0x0010)
 * hdf: DMA 2  (0x0002 0x0012) (0x0010 0x0030)
 * hdg: DMA 1  (0x0012 0x0052) (0x0030 0x0070)
 * hdh: DMA 1  (0x0052 0x0252) (0x0070 0x00f0)
 *
 * ide-pci.c reference
 *
 * Since there are two cards that report almost identically,
 * the only discernable difference is the values reported in pcicmd.
 * Booting-BIOS card or HPT363 :: pcicmd == 0x07
 * Non-bootable card or HPT343 :: pcicmd == 0x05
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#define HPT343_DEBUG_DRIVE_INFO		0

static int hpt34x_tune_chipset(ide_drive_t *drive, const u8 speed)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	u32 reg1= 0, tmp1 = 0, reg2 = 0, tmp2 = 0;
	u8			hi_speed, lo_speed;

	hi_speed = speed >> 4;
	lo_speed = speed & 0x0f;

	if (hi_speed & 7) {
		hi_speed = (hi_speed & 4) ? 0x01 : 0x10;
	} else {
		lo_speed <<= 5;
		lo_speed >>= 5;
	}

	pci_read_config_dword(dev, 0x44, &reg1);
	pci_read_config_dword(dev, 0x48, &reg2);
	tmp1 = ((lo_speed << (3*drive->dn)) | (reg1 & ~(7 << (3*drive->dn))));
	tmp2 = ((hi_speed << drive->dn) | (reg2 & ~(0x11 << drive->dn)));
	pci_write_config_dword(dev, 0x44, tmp1);
	pci_write_config_dword(dev, 0x48, tmp2);

#if HPT343_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d (0x%04x 0x%04x) (0x%04x 0x%04x)" \
		" (0x%02x 0x%02x)\n",
		drive->name, ide_xfer_verbose(speed),
		drive->dn, reg1, tmp1, reg2, tmp2,
		hi_speed, lo_speed);
#endif /* HPT343_DEBUG_DRIVE_INFO */

	return(ide_config_drive_speed(drive, speed));
}

static void hpt34x_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	(void) hpt34x_tune_chipset(drive, (XFER_PIO_0 + pio));
}

static int hpt34x_config_drive_xfer_rate (ide_drive_t *drive)
{
	drive->init_speed = 0;

	if (ide_tune_dma(drive))
		return -1;

	if (ide_use_fast_pio(drive))
		ide_set_max_pio(drive);

	return -1;
}

/*
 * If the BIOS does not set the IO base addaress to XX00, 343 will fail.
 */
#define	HPT34X_PCI_INIT_REG		0x80

static unsigned int __devinit init_chipset_hpt34x(struct pci_dev *dev, const char *name)
{
	int i = 0;
	unsigned long hpt34xIoBase = pci_resource_start(dev, 4);
	unsigned long hpt_addr[4] = { 0x20, 0x34, 0x28, 0x3c };
	unsigned long hpt_addr_len[4] = { 7, 3, 7, 3 };
	u16 cmd;
	unsigned long flags;

	local_irq_save(flags);

	pci_write_config_byte(dev, HPT34X_PCI_INIT_REG, 0x00);
	pci_read_config_word(dev, PCI_COMMAND, &cmd);

	if (cmd & PCI_COMMAND_MEMORY)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0xF0);
	else
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x20);

	/*
	 * Since 20-23 can be assigned and are R/W, we correct them.
	 */
	pci_write_config_word(dev, PCI_COMMAND, cmd & ~PCI_COMMAND_IO);
	for(i=0; i<4; i++) {
		dev->resource[i].start = (hpt34xIoBase + hpt_addr[i]);
		dev->resource[i].end = dev->resource[i].start + hpt_addr_len[i];
		dev->resource[i].flags = IORESOURCE_IO;
		pci_write_config_dword(dev,
				(PCI_BASE_ADDRESS_0 + (i * 4)),
				dev->resource[i].start);
	}
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	local_irq_restore(flags);

	return dev->irq;
}

static void __devinit init_hwif_hpt34x(ide_hwif_t *hwif)
{
	u16 pcicmd = 0;

	hwif->autodma = 0;

	hwif->set_pio_mode = &hpt34x_set_pio_mode;
	hwif->speedproc = &hpt34x_tune_chipset;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	pci_read_config_word(hwif->pci_dev, PCI_COMMAND, &pcicmd);

	if (!hwif->dma_base)
		return;

#ifdef CONFIG_HPT34X_AUTODMA
	hwif->ultra_mask = 0x07;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;
#endif

	hwif->ide_dma_check = &hpt34x_config_drive_xfer_rate;
	if (!noautodma)
		hwif->autodma = (pcicmd & PCI_COMMAND_MEMORY) ? 1 : 0;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static ide_pci_device_t hpt34x_chipset __devinitdata = {
	.name		= "HPT34X",
	.init_chipset	= init_chipset_hpt34x,
	.init_hwif	= init_hwif_hpt34x,
	.autodma	= NOAUTODMA,
	.bootable	= NEVER_BOARD,
	.extra		= 16,
	.pio_mask	= ATA_PIO5,
};

static int __devinit hpt34x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &hpt34x_chipset;
	static char *chipset_names[] = {"HPT343", "HPT345"};
	u16 pcicmd = 0;

	pci_read_config_word(dev, PCI_COMMAND, &pcicmd);

	d->name = chipset_names[(pcicmd & PCI_COMMAND_MEMORY) ? 1 : 0];
	d->bootable = (pcicmd & PCI_COMMAND_MEMORY) ? OFF_BOARD : NEVER_BOARD;

	return ide_setup_pci_device(dev, d);
}

static struct pci_device_id hpt34x_pci_tbl[] = {
	{ PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT343, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, hpt34x_pci_tbl);

static struct pci_driver driver = {
	.name		= "HPT34x_IDE",
	.id_table	= hpt34x_pci_tbl,
	.probe		= hpt34x_init_one,
};

static int __init hpt34x_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(hpt34x_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for Highpoint 34x IDE");
MODULE_LICENSE("GPL");
