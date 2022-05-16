// SPDX-License-Identifier: GPL-2.0-only
/*
 * VIA IDE driver for Linux. Supported southbridges:
 *
 *   vt82c576, vt82c586, vt82c586a, vt82c586b, vt82c596a, vt82c596b,
 *   vt82c686, vt82c686a, vt82c686b, vt8231, vt8233, vt8233c, vt8233a,
 *   vt8235, vt8237, vt8237a
 *
 * Copyright (c) 2000-2002 Vojtech Pavlik
 * Copyright (c) 2007-2010 Bartlomiej Zolnierkiewicz
 *
 * Based on the work of:
 *	Michel Aubry
 *	Jeff Garzik
 *	Andre Hedrick
 *
 * Documentation:
 *	Obsolete device documentation publicly available from via.com.tw
 *	Current device documentation available under NDA only
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/dmi.h>

#ifdef CONFIG_PPC_CHRP
#include <asm/processor.h>
#endif

#define DRV_NAME "via82cxxx"

#define VIA_IDE_ENABLE		0x40
#define VIA_IDE_CONFIG		0x41
#define VIA_FIFO_CONFIG		0x43
#define VIA_MISC_1		0x44
#define VIA_MISC_2		0x45
#define VIA_MISC_3		0x46
#define VIA_DRIVE_TIMING	0x48
#define VIA_8BIT_TIMING		0x4e
#define VIA_ADDRESS_SETUP	0x4c
#define VIA_UDMA_TIMING		0x50

#define VIA_BAD_PREQ		0x01 /* Crashes if PREQ# till DDACK# set */
#define VIA_BAD_CLK66		0x02 /* 66 MHz clock doesn't work correctly */
#define VIA_SET_FIFO		0x04 /* Needs to have FIFO split set */
#define VIA_NO_UNMASK		0x08 /* Doesn't work with IRQ unmasking on */
#define VIA_BAD_ID		0x10 /* Has wrong vendor ID (0x1107) */
#define VIA_BAD_AST		0x20 /* Don't touch Address Setup Timing */
#define VIA_SATA_PATA		0x80 /* SATA/PATA combined configuration */

enum {
	VIA_IDFLAG_SINGLE = (1 << 1), /* single channel controller */
};

/*
 * VIA SouthBridge chips.
 */

static struct via_isa_bridge {
	char *name;
	u16 id;
	u8 rev_min;
	u8 rev_max;
	u8 udma_mask;
	u8 flags;
} via_isa_bridges[] = {
	{ "vx855",	PCI_DEVICE_ID_VIA_VX855,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST | VIA_SATA_PATA },
	{ "vx800",	PCI_DEVICE_ID_VIA_VX800,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST | VIA_SATA_PATA },
	{ "cx700",	PCI_DEVICE_ID_VIA_CX700,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST | VIA_SATA_PATA },
	{ "vt8261",	PCI_DEVICE_ID_VIA_8261,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8237s",	PCI_DEVICE_ID_VIA_8237S,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt6410",	PCI_DEVICE_ID_VIA_6410,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt6415",	PCI_DEVICE_ID_VIA_6415,     0x00, 0xff, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8251",	PCI_DEVICE_ID_VIA_8251,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8237",	PCI_DEVICE_ID_VIA_8237,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8237a",	PCI_DEVICE_ID_VIA_8237A,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8235",	PCI_DEVICE_ID_VIA_8235,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8233a",	PCI_DEVICE_ID_VIA_8233A,    0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ "vt8233c",	PCI_DEVICE_ID_VIA_8233C_0,  0x00, 0x2f, ATA_UDMA5, },
	{ "vt8233",	PCI_DEVICE_ID_VIA_8233_0,   0x00, 0x2f, ATA_UDMA5, },
	{ "vt8231",	PCI_DEVICE_ID_VIA_8231,     0x00, 0x2f, ATA_UDMA5, },
	{ "vt82c686b",	PCI_DEVICE_ID_VIA_82C686,   0x40, 0x4f, ATA_UDMA5, },
	{ "vt82c686a",	PCI_DEVICE_ID_VIA_82C686,   0x10, 0x2f, ATA_UDMA4, },
	{ "vt82c686",	PCI_DEVICE_ID_VIA_82C686,   0x00, 0x0f, ATA_UDMA2, VIA_BAD_CLK66 },
	{ "vt82c596b",	PCI_DEVICE_ID_VIA_82C596,   0x10, 0x2f, ATA_UDMA4, },
	{ "vt82c596a",	PCI_DEVICE_ID_VIA_82C596,   0x00, 0x0f, ATA_UDMA2, VIA_BAD_CLK66 },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x47, 0x4f, ATA_UDMA2, VIA_SET_FIFO },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x40, 0x46, ATA_UDMA2, VIA_SET_FIFO | VIA_BAD_PREQ },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x30, 0x3f, ATA_UDMA2, VIA_SET_FIFO },
	{ "vt82c586a",	PCI_DEVICE_ID_VIA_82C586_0, 0x20, 0x2f, ATA_UDMA2, VIA_SET_FIFO },
	{ "vt82c586",	PCI_DEVICE_ID_VIA_82C586_0, 0x00, 0x0f,      0x00, VIA_SET_FIFO },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f,      0x00, VIA_SET_FIFO | VIA_NO_UNMASK },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f,      0x00, VIA_SET_FIFO | VIA_NO_UNMASK | VIA_BAD_ID },
	{ "vtxxxx",	PCI_DEVICE_ID_VIA_ANON,     0x00, 0x2f, ATA_UDMA6, VIA_BAD_AST },
	{ NULL }
};

static unsigned int via_clock;
static char *via_dma[] = { "16", "25", "33", "44", "66", "100", "133" };

struct via82cxxx_dev
{
	struct via_isa_bridge *via_config;
	unsigned int via_80w;
};

/**
 *	via_set_speed			-	write timing registers
 *	@dev: PCI device
 *	@dn: device
 *	@timing: IDE timing data to use
 *
 *	via_set_speed writes timing values to the chipset registers
 */

static void via_set_speed(ide_hwif_t *hwif, u8 dn, struct ide_timing *timing)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct ide_host *host = pci_get_drvdata(dev);
	struct via82cxxx_dev *vdev = host->host_priv;
	u8 t;

	if (~vdev->via_config->flags & VIA_BAD_AST) {
		pci_read_config_byte(dev, VIA_ADDRESS_SETUP, &t);
		t = (t & ~(3 << ((3 - dn) << 1))) | ((clamp_val(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
		pci_write_config_byte(dev, VIA_ADDRESS_SETUP, t);
	}

	pci_write_config_byte(dev, VIA_8BIT_TIMING + (1 - (dn >> 1)),
		((clamp_val(timing->act8b, 1, 16) - 1) << 4) | (clamp_val(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, VIA_DRIVE_TIMING + (3 - dn),
		((clamp_val(timing->active, 1, 16) - 1) << 4) | (clamp_val(timing->recover, 1, 16) - 1));

	switch (vdev->via_config->udma_mask) {
	case ATA_UDMA2: t = timing->udma ? (0xe0 | (clamp_val(timing->udma, 2, 5) - 2)) : 0x03; break;
	case ATA_UDMA4: t = timing->udma ? (0xe8 | (clamp_val(timing->udma, 2, 9) - 2)) : 0x0f; break;
	case ATA_UDMA5: t = timing->udma ? (0xe0 | (clamp_val(timing->udma, 2, 9) - 2)) : 0x07; break;
	case ATA_UDMA6: t = timing->udma ? (0xe0 | (clamp_val(timing->udma, 2, 9) - 2)) : 0x07; break;
	}

	/* Set UDMA unless device is not UDMA capable */
	if (vdev->via_config->udma_mask) {
		u8 udma_etc;

		pci_read_config_byte(dev, VIA_UDMA_TIMING + 3 - dn, &udma_etc);

		/* clear transfer mode bit */
		udma_etc &= ~0x20;

		if (timing->udma) {
			/* preserve 80-wire cable detection bit */
			udma_etc &= 0x10;
			udma_etc |= t;
		}

		pci_write_config_byte(dev, VIA_UDMA_TIMING + 3 - dn, udma_etc);
	}
}

/**
 *	via_set_drive		-	configure transfer mode
 *	@hwif: port
 *	@drive: Drive to set up
 *
 *	via_set_drive() computes timing values configures the chipset to
 *	a desired transfer mode.  It also can be called by upper layers.
 */

static void via_set_drive(ide_hwif_t *hwif, ide_drive_t *drive)
{
	ide_drive_t *peer = ide_get_pair_dev(drive);
	struct ide_host *host = dev_get_drvdata(hwif->dev);
	struct via82cxxx_dev *vdev = host->host_priv;
	struct ide_timing t, p;
	unsigned int T, UT;
	const u8 speed = drive->dma_mode;

	T = 1000000000 / via_clock;

	switch (vdev->via_config->udma_mask) {
	case ATA_UDMA2: UT = T;   break;
	case ATA_UDMA4: UT = T/2; break;
	case ATA_UDMA5: UT = T/3; break;
	case ATA_UDMA6: UT = T/4; break;
	default:	UT = T;
	}

	ide_timing_compute(drive, speed, &t, T, UT);

	if (peer) {
		ide_timing_compute(peer, peer->pio_mode, &p, T, UT);
		ide_timing_merge(&p, &t, &t, IDE_TIMING_8BIT);
	}

	via_set_speed(hwif, drive->dn, &t);
}

/**
 *	via_set_pio_mode	-	set host controller for PIO mode
 *	@hwif: port
 *	@drive: drive
 *
 *	A callback from the upper layers for PIO-only tuning.
 */

static void via_set_pio_mode(ide_hwif_t *hwif, ide_drive_t *drive)
{
	drive->dma_mode = drive->pio_mode;
	via_set_drive(hwif, drive);
}

static struct via_isa_bridge *via_config_find(struct pci_dev **isa)
{
	struct via_isa_bridge *via_config;

	for (via_config = via_isa_bridges;
	     via_config->id != PCI_DEVICE_ID_VIA_ANON; via_config++)
		if ((*isa = pci_get_device(PCI_VENDOR_ID_VIA +
			!!(via_config->flags & VIA_BAD_ID),
			via_config->id, NULL))) {

			if ((*isa)->revision >= via_config->rev_min &&
			    (*isa)->revision <= via_config->rev_max)
				break;
			pci_dev_put(*isa);
		}

	return via_config;
}

/*
 * Check and handle 80-wire cable presence
 */
static void via_cable_detect(struct via82cxxx_dev *vdev, u32 u)
{
	int i;

	switch (vdev->via_config->udma_mask) {
		case ATA_UDMA4:
			for (i = 24; i >= 0; i -= 8)
				if (((u >> (i & 16)) & 8) &&
				    ((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 2)) {
					/*
					 * 2x PCI clock and
					 * UDMA w/ < 3T/cycle
					 */
					vdev->via_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case ATA_UDMA5:
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 0x10) ||
				    (((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 4))) {
					/* BIOS 80-wire bit or
					 * UDMA w/ < 60ns/cycle
					 */
					vdev->via_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case ATA_UDMA6:
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 0x10) ||
				    (((u >> i) & 0x20) &&
				     (((u >> i) & 7) < 6))) {
					/* BIOS 80-wire bit or
					 * UDMA w/ < 60ns/cycle
					 */
					vdev->via_80w |= (1 << (1 - (i >> 4)));
				}
			break;
	}
}

/**
 *	init_chipset_via82cxxx	-	initialization handler
 *	@dev: PCI device
 *
 *	The initialization callback. Here we determine the IDE chip type
 *	and initialize its drive independent registers.
 */

static int init_chipset_via82cxxx(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	struct via82cxxx_dev *vdev = host->host_priv;
	struct via_isa_bridge *via_config = vdev->via_config;
	u8 t, v;
	u32 u;

	/*
	 * Detect cable and configure Clk66
	 */
	pci_read_config_dword(dev, VIA_UDMA_TIMING, &u);

	via_cable_detect(vdev, u);

	if (via_config->udma_mask == ATA_UDMA4) {
		/* Enable Clk66 */
		pci_write_config_dword(dev, VIA_UDMA_TIMING, u|0x80008);
	} else if (via_config->flags & VIA_BAD_CLK66) {
		/* Would cause trouble on 596a and 686 */
		pci_write_config_dword(dev, VIA_UDMA_TIMING, u & ~0x80008);
	}

	/*
	 * Check whether interfaces are enabled.
	 */

	pci_read_config_byte(dev, VIA_IDE_ENABLE, &v);

	/*
	 * Set up FIFO sizes and thresholds.
	 */

	pci_read_config_byte(dev, VIA_FIFO_CONFIG, &t);

	/* Disable PREQ# till DDACK# */
	if (via_config->flags & VIA_BAD_PREQ) {
		/* Would crash on 586b rev 41 */
		t &= 0x7f;
	}

	/* Fix FIFO split between channels */
	if (via_config->flags & VIA_SET_FIFO) {
		t &= (t & 0x9f);
		switch (v & 3) {
			case 2: t |= 0x00; break;	/* 16 on primary */
			case 1: t |= 0x60; break;	/* 16 on secondary */
			case 3: t |= 0x20; break;	/* 8 pri 8 sec */
		}
	}

	pci_write_config_byte(dev, VIA_FIFO_CONFIG, t);

	return 0;
}

/*
 *	Cable special cases
 */

static const struct dmi_system_id cable_dmi_table[] = {
	{
		.ident = "Acer Ferrari 3400",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Acer,Inc."),
			DMI_MATCH(DMI_BOARD_NAME, "Ferrari 3400"),
		},
	},
	{ }
};

static int via_cable_override(struct pci_dev *pdev)
{
	/* Systems by DMI */
	if (dmi_check_system(cable_dmi_table))
		return 1;

	/* Arima W730-K8/Targa Visionary 811/... */
	if (pdev->subsystem_vendor == 0x161F &&
	    pdev->subsystem_device == 0x2032)
		return 1;

	return 0;
}

static u8 via82cxxx_cable_detect(ide_hwif_t *hwif)
{
	struct pci_dev *pdev = to_pci_dev(hwif->dev);
	struct ide_host *host = pci_get_drvdata(pdev);
	struct via82cxxx_dev *vdev = host->host_priv;

	if (via_cable_override(pdev))
		return ATA_CBL_PATA40_SHORT;

	if ((vdev->via_config->flags & VIA_SATA_PATA) && hwif->channel == 0)
		return ATA_CBL_SATA;

	if ((vdev->via_80w >> hwif->channel) & 1)
		return ATA_CBL_PATA80;
	else
		return ATA_CBL_PATA40;
}

static const struct ide_port_ops via_port_ops = {
	.set_pio_mode		= via_set_pio_mode,
	.set_dma_mode		= via_set_drive,
	.cable_detect		= via82cxxx_cable_detect,
};

static const struct ide_port_info via82cxxx_chipset = {
	.name		= DRV_NAME,
	.init_chipset	= init_chipset_via82cxxx,
	.enablebits	= { { 0x40, 0x02, 0x02 }, { 0x40, 0x01, 0x01 } },
	.port_ops	= &via_port_ops,
	.host_flags	= IDE_HFLAG_PIO_NO_BLACKLIST |
			  IDE_HFLAG_POST_SET_MODE |
			  IDE_HFLAG_IO_32BIT,
	.pio_mask	= ATA_PIO5,
	.swdma_mask	= ATA_SWDMA2,
	.mwdma_mask	= ATA_MWDMA2,
};

static int via_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct pci_dev *isa = NULL;
	struct via_isa_bridge *via_config;
	struct via82cxxx_dev *vdev;
	int rc;
	u8 idx = id->driver_data;
	struct ide_port_info d;

	d = via82cxxx_chipset;

	/*
	 * Find the ISA bridge and check we know what it is.
	 */
	via_config = via_config_find(&isa);

	/*
	 * Print the boot message.
	 */
	printk(KERN_INFO DRV_NAME " %s: VIA %s (rev %02x) IDE %sDMA%s\n",
		pci_name(dev), via_config->name, isa->revision,
		via_config->udma_mask ? "U" : "MW",
		via_dma[via_config->udma_mask ?
			(fls(via_config->udma_mask) - 1) : 0]);

	pci_dev_put(isa);

	/*
	 * Determine system bus clock.
	 */
	via_clock = (ide_pci_clk ? ide_pci_clk : 33) * 1000;

	switch (via_clock) {
	case 33000: via_clock = 33333; break;
	case 37000: via_clock = 37500; break;
	case 41000: via_clock = 41666; break;
	}

	if (via_clock < 20000 || via_clock > 50000) {
		printk(KERN_WARNING DRV_NAME ": User given PCI clock speed "
			"impossible (%d), using 33 MHz instead.\n", via_clock);
		via_clock = 33333;
	}

	if (idx == 1)
		d.enablebits[1].reg = d.enablebits[0].reg = 0;
	else
		d.host_flags |= IDE_HFLAG_NO_AUTODMA;

	if (idx == VIA_IDFLAG_SINGLE)
		d.host_flags |= IDE_HFLAG_SINGLE;

	if ((via_config->flags & VIA_NO_UNMASK) == 0)
		d.host_flags |= IDE_HFLAG_UNMASK_IRQS;

	d.udma_mask = via_config->udma_mask;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev) {
		printk(KERN_ERR DRV_NAME " %s: out of memory :(\n",
			pci_name(dev));
		return -ENOMEM;
	}

	vdev->via_config = via_config;

	rc = ide_pci_init_one(dev, &d, vdev);
	if (rc)
		kfree(vdev);

	return rc;
}

static void via_remove(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	struct via82cxxx_dev *vdev = host->host_priv;

	ide_pci_remove(dev);
	kfree(vdev);
}

static const struct pci_device_id via_pci_tbl[] = {
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_82C576_1),  0 },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_82C586_1),  0 },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_CX700_IDE), 0 },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_VX855_IDE), VIA_IDFLAG_SINGLE },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_6410),      1 },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_6415),      1 },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_SATA_EIDE), 1 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, via_pci_tbl);

static struct pci_driver via_pci_driver = {
	.name 		= "VIA_IDE",
	.id_table 	= via_pci_tbl,
	.probe 		= via_init_one,
	.remove		= via_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init via_ide_init(void)
{
	return ide_pci_register_driver(&via_pci_driver);
}

static void __exit via_ide_exit(void)
{
	pci_unregister_driver(&via_pci_driver);
}

module_init(via_ide_init);
module_exit(via_ide_exit);

MODULE_AUTHOR("Vojtech Pavlik, Bartlomiej Zolnierkiewicz, Michel Aubry, Jeff Garzik, Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for VIA IDE");
MODULE_LICENSE("GPL");
