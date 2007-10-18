
/*
 * Copyright (C) 2006		Red Hat <alan@redhat.com>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

typedef enum {
	PORT_PATA0 = 0,
	PORT_PATA1 = 1,
	PORT_SATA = 2,
} port_type;

/**
 *	ata66_jmicron		-	Cable check
 *	@hwif: IDE port
 *
 *	Returns the cable type.
 */

static u8 __devinit ata66_jmicron(ide_hwif_t *hwif)
{
	struct pci_dev *pdev = hwif->pci_dev;

	u32 control;
	u32 control5;

	int port = hwif->channel;
	port_type port_map[2];

	pci_read_config_dword(pdev, 0x40, &control);

	/* There are two basic mappings. One has the two SATA ports merged
	   as master/slave and the secondary as PATA, the other has only the
	   SATA port mapped */
	if (control & (1 << 23)) {
		port_map[0] = PORT_SATA;
		port_map[1] = PORT_PATA0;
	} else {
		port_map[0] = PORT_SATA;
		port_map[1] = PORT_SATA;
	}

	/* The 365/366 may have this bit set to map the second PATA port
	   as the internal primary channel */
	pci_read_config_dword(pdev, 0x80, &control5);
	if (control5 & (1<<24))
		port_map[0] = PORT_PATA1;

	/* The two ports may then be logically swapped by the firmware */
	if (control & (1 << 22))
		port = port ^ 1;

	/*
	 *	Now we know which physical port we are talking about we can
	 *	actually do our cable checking etc. Thankfully we don't need
	 *	to do the plumbing for other cases.
	 */
	switch (port_map[port])
	{
	case PORT_PATA0:
		if (control & (1 << 3))	/* 40/80 pin primary */
			return ATA_CBL_PATA40;
		return ATA_CBL_PATA80;
	case PORT_PATA1:
		if (control5 & (1 << 19))	/* 40/80 pin secondary */
			return ATA_CBL_PATA40;
		return ATA_CBL_PATA80;
	case PORT_SATA:
		break;
	}
	/* Avoid bogus "control reaches end of non-void function" */
	return ATA_CBL_PATA80;
}

static void jmicron_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
}

/**
 *	jmicron_set_dma_mode	-	set host controller for DMA mode
 *	@drive: drive
 *	@mode: DMA mode
 *
 *	As the JMicron snoops for timings we don't need to do anything here.
 */

static void jmicron_set_dma_mode(ide_drive_t *drive, const u8 mode)
{
}

/**
 *	init_hwif_jmicron	-	set up hwif structs
 *	@hwif: interface to set up
 *
 *	Minimal set up is required for the Jmicron hardware.
 */

static void __devinit init_hwif_jmicron(ide_hwif_t *hwif)
{
	hwif->set_pio_mode = &jmicron_set_pio_mode;
	hwif->set_dma_mode = &jmicron_set_dma_mode;

	if (hwif->dma_base == 0)
		return;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT)
		hwif->cbl = ata66_jmicron(hwif);
}

static ide_pci_device_t jmicron_chipset __devinitdata = {
	.name		= "JMB",
	.init_hwif	= init_hwif_jmicron,
	.host_flags	= IDE_HFLAG_BOOTABLE,
	.enablebits	= { { 0x40, 0x01, 0x01 }, { 0x40, 0x10, 0x10 } },
	.pio_mask	= ATA_PIO5,
	.mwdma_mask	= ATA_MWDMA2,
	.udma_mask	= ATA_UDMA6,
};

/**
 *	jmicron_init_one	-	pci layer discovery entry
 *	@dev: PCI device
 *	@id: ident table entry
 *
 *	Called by the PCI code when it finds a Jmicron controller.
 *	We then use the IDE PCI generic helper to do most of the work.
 */

static int __devinit jmicron_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_setup_pci_device(dev, &jmicron_chipset);
	return 0;
}

/* All JMB PATA controllers have and will continue to have the same
 * interface.  Matching vendor and device class is enough for all
 * current and future controllers if the controller is programmed
 * properly.
 *
 * If libata is configured, jmicron PCI quirk programs the controller
 * into the correct mode.  If libata isn't configured, match known
 * device IDs too to maintain backward compatibility.
 */
static struct pci_device_id jmicron_pci_tbl[] = {
#if !defined(CONFIG_ATA) && !defined(CONFIG_ATA_MODULE)
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMB361) },
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMB363) },
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMB365) },
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMB366) },
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMB368) },
#endif
	{ PCI_VENDOR_ID_JMICRON, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_STORAGE_IDE << 8, 0xffff00, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, jmicron_pci_tbl);

static struct pci_driver driver = {
	.name		= "JMicron IDE",
	.id_table	= jmicron_pci_tbl,
	.probe		= jmicron_init_one,
};

static int __init jmicron_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(jmicron_ide_init);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("PCI driver module for the JMicron in legacy modes");
MODULE_LICENSE("GPL");
