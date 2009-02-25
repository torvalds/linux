/*
 * AMD 755/756/766/8111 and nVidia nForce/2/2s/3/3s/CK804/MCP04
 * IDE driver for Linux.
 *
 * Copyright (c) 2000-2002 Vojtech Pavlik
 * Copyright (c) 2007-2008 Bartlomiej Zolnierkiewicz
 *
 * Based on the work of:
 *      Andre Hedrick
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#define DRV_NAME "amd74xx"

enum {
	AMD_IDE_CONFIG		= 0x41,
	AMD_CABLE_DETECT	= 0x42,
	AMD_DRIVE_TIMING	= 0x48,
	AMD_8BIT_TIMING		= 0x4e,
	AMD_ADDRESS_SETUP	= 0x4c,
	AMD_UDMA_TIMING		= 0x50,
};

static unsigned int amd_80w;
static unsigned int amd_clock;

static char *amd_dma[] = { "16", "25", "33", "44", "66", "100", "133" };
static unsigned char amd_cyc2udma[] = { 6, 6, 5, 4, 0, 1, 1, 2, 2, 3, 3, 3, 3, 3, 3, 7 };

static inline u8 amd_offset(struct pci_dev *dev)
{
	return (dev->vendor == PCI_VENDOR_ID_NVIDIA) ? 0x10 : 0;
}

/*
 * amd_set_speed() writes timing values to the chipset registers
 */

static void amd_set_speed(struct pci_dev *dev, u8 dn, u8 udma_mask,
			  struct ide_timing *timing)
{
	u8 t = 0, offset = amd_offset(dev);

	pci_read_config_byte(dev, AMD_ADDRESS_SETUP + offset, &t);
	t = (t & ~(3 << ((3 - dn) << 1))) | ((clamp_val(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
	pci_write_config_byte(dev, AMD_ADDRESS_SETUP + offset, t);

	pci_write_config_byte(dev, AMD_8BIT_TIMING + offset + (1 - (dn >> 1)),
		((clamp_val(timing->act8b, 1, 16) - 1) << 4) | (clamp_val(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, AMD_DRIVE_TIMING + offset + (3 - dn),
		((clamp_val(timing->active, 1, 16) - 1) << 4) | (clamp_val(timing->recover, 1, 16) - 1));

	switch (udma_mask) {
	case ATA_UDMA2: t = timing->udma ? (0xc0 | (clamp_val(timing->udma, 2, 5) - 2)) : 0x03; break;
	case ATA_UDMA4: t = timing->udma ? (0xc0 | amd_cyc2udma[clamp_val(timing->udma, 2, 10)]) : 0x03; break;
	case ATA_UDMA5: t = timing->udma ? (0xc0 | amd_cyc2udma[clamp_val(timing->udma, 1, 10)]) : 0x03; break;
	case ATA_UDMA6: t = timing->udma ? (0xc0 | amd_cyc2udma[clamp_val(timing->udma, 1, 15)]) : 0x03; break;
	default: return;
	}

	pci_write_config_byte(dev, AMD_UDMA_TIMING + offset + (3 - dn), t);
}

/*
 * amd_set_drive() computes timing values and configures the chipset
 * to a desired transfer mode.  It also can be called by upper layers.
 */

static void amd_set_drive(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	ide_drive_t *peer = ide_get_pair_dev(drive);
	struct ide_timing t, p;
	int T, UT;
	u8 udma_mask = hwif->ultra_mask;

	T = 1000000000 / amd_clock;
	UT = (udma_mask == ATA_UDMA2) ? T : (T / 2);

	ide_timing_compute(drive, speed, &t, T, UT);

	if (peer) {
		ide_timing_compute(peer, peer->current_speed, &p, T, UT);
		ide_timing_merge(&p, &t, &t, IDE_TIMING_8BIT);
	}

	if (speed == XFER_UDMA_5 && amd_clock <= 33333) t.udma = 1;
	if (speed == XFER_UDMA_6 && amd_clock <= 33333) t.udma = 15;

	amd_set_speed(dev, drive->dn, udma_mask, &t);
}

/*
 * amd_set_pio_mode() is a callback from upper layers for PIO-only tuning.
 */

static void amd_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	amd_set_drive(drive, XFER_PIO_0 + pio);
}

static void amd7409_cable_detect(struct pci_dev *dev)
{
	/* no host side cable detection */
	amd_80w = 0x03;
}

static void amd7411_cable_detect(struct pci_dev *dev)
{
	int i;
	u32 u = 0;
	u8 t = 0, offset = amd_offset(dev);

	pci_read_config_byte(dev, AMD_CABLE_DETECT + offset, &t);
	pci_read_config_dword(dev, AMD_UDMA_TIMING + offset, &u);
	amd_80w = ((t & 0x3) ? 1 : 0) | ((t & 0xc) ? 2 : 0);
	for (i = 24; i >= 0; i -= 8)
		if (((u >> i) & 4) && !(amd_80w & (1 << (1 - (i >> 4))))) {
			printk(KERN_WARNING DRV_NAME " %s: BIOS didn't set "
				"cable bits correctly. Enabling workaround.\n",
				pci_name(dev));
			amd_80w |= (1 << (1 - (i >> 4)));
		}
}

/*
 * The initialization callback.  Initialize drive independent registers.
 */

static unsigned int init_chipset_amd74xx(struct pci_dev *dev)
{
	u8 t = 0, offset = amd_offset(dev);

/*
 * Check 80-wire cable presence.
 */

	if (dev->vendor == PCI_VENDOR_ID_AMD &&
	    dev->device == PCI_DEVICE_ID_AMD_COBRA_7401)
		; /* no UDMA > 2 */
	else if (dev->vendor == PCI_VENDOR_ID_AMD &&
		 dev->device == PCI_DEVICE_ID_AMD_VIPER_7409)
		amd7409_cable_detect(dev);
	else
		amd7411_cable_detect(dev);

/*
 * Take care of prefetch & postwrite.
 */

	pci_read_config_byte(dev, AMD_IDE_CONFIG + offset, &t);
	/*
	 * Check for broken FIFO support.
	 */
	if (dev->vendor == PCI_VENDOR_ID_AMD &&
	    dev->device == PCI_DEVICE_ID_AMD_VIPER_7411)
		t &= 0x0f;
	else
		t |= 0xf0;
	pci_write_config_byte(dev, AMD_IDE_CONFIG + offset, t);

	return dev->irq;
}

static u8 amd_cable_detect(ide_hwif_t *hwif)
{
	if ((amd_80w >> hwif->channel) & 1)
		return ATA_CBL_PATA80;
	else
		return ATA_CBL_PATA40;
}

static void __devinit init_hwif_amd74xx(ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);

	if (hwif->irq == 0) /* 0 is bogus but will do for now */
		hwif->irq = pci_get_legacy_ide_irq(dev, hwif->channel);
}

static const struct ide_port_ops amd_port_ops = {
	.set_pio_mode		= amd_set_pio_mode,
	.set_dma_mode		= amd_set_drive,
	.cable_detect		= amd_cable_detect,
};

#define IDE_HFLAGS_AMD \
	(IDE_HFLAG_PIO_NO_BLACKLIST | \
	 IDE_HFLAG_POST_SET_MODE | \
	 IDE_HFLAG_IO_32BIT | \
	 IDE_HFLAG_UNMASK_IRQS)

#define DECLARE_AMD_DEV(swdma, udma)				\
	{								\
		.name		= DRV_NAME,				\
		.init_chipset	= init_chipset_amd74xx,			\
		.init_hwif	= init_hwif_amd74xx,			\
		.enablebits	= {{0x40,0x02,0x02}, {0x40,0x01,0x01}},	\
		.port_ops	= &amd_port_ops,			\
		.host_flags	= IDE_HFLAGS_AMD,			\
		.pio_mask	= ATA_PIO5,				\
		.swdma_mask	= swdma,				\
		.mwdma_mask	= ATA_MWDMA2,				\
		.udma_mask	= udma,					\
	}

#define DECLARE_NV_DEV(udma)					\
	{								\
		.name		= DRV_NAME,				\
		.init_chipset	= init_chipset_amd74xx,			\
		.init_hwif	= init_hwif_amd74xx,			\
		.enablebits	= {{0x50,0x02,0x02}, {0x50,0x01,0x01}},	\
		.port_ops	= &amd_port_ops,			\
		.host_flags	= IDE_HFLAGS_AMD,			\
		.pio_mask	= ATA_PIO5,				\
		.swdma_mask	= ATA_SWDMA2,				\
		.mwdma_mask	= ATA_MWDMA2,				\
		.udma_mask	= udma,					\
	}

static const struct ide_port_info amd74xx_chipsets[] __devinitdata = {
	/* 0: AMD7401 */	DECLARE_AMD_DEV(0x00, ATA_UDMA2),
	/* 1: AMD7409 */	DECLARE_AMD_DEV(ATA_SWDMA2, ATA_UDMA4),
	/* 2: AMD7411/7441 */	DECLARE_AMD_DEV(ATA_SWDMA2, ATA_UDMA5),
	/* 3: AMD8111 */	DECLARE_AMD_DEV(ATA_SWDMA2, ATA_UDMA6),

	/* 4: NFORCE */		DECLARE_NV_DEV(ATA_UDMA5),
	/* 5: >= NFORCE2 */	DECLARE_NV_DEV(ATA_UDMA6),

	/* 6: AMD5536 */	DECLARE_AMD_DEV(ATA_SWDMA2, ATA_UDMA5),
};

static int __devinit amd74xx_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct ide_port_info d;
	u8 idx = id->driver_data;

	d = amd74xx_chipsets[idx];

	/*
	 * Check for bad SWDMA and incorrectly wired Serenade mainboards.
	 */
	if (idx == 1) {
		if (dev->revision <= 7)
			d.swdma_mask = 0;
		d.host_flags |= IDE_HFLAG_CLEAR_SIMPLEX;
	} else if (idx == 3) {
		if (dev->subsystem_vendor == PCI_VENDOR_ID_AMD &&
		    dev->subsystem_device == PCI_DEVICE_ID_AMD_SERENADE)
			d.udma_mask = ATA_UDMA5;
	}

	/*
	 * It seems that on some nVidia controllers using AltStatus
	 * register can be unreliable so default to Status register
	 * if the device is in Compatibility Mode.
	 */
	if (dev->vendor == PCI_VENDOR_ID_NVIDIA &&
	    ide_pci_is_in_compatibility_mode(dev))
		d.host_flags |= IDE_HFLAG_BROKEN_ALTSTATUS;

	printk(KERN_INFO "%s %s: UDMA%s controller\n",
		d.name, pci_name(dev), amd_dma[fls(d.udma_mask) - 1]);

	/*
	* Determine the system bus clock.
	*/
	amd_clock = (ide_pci_clk ? ide_pci_clk : 33) * 1000;

	switch (amd_clock) {
	case 33000: amd_clock = 33333; break;
	case 37000: amd_clock = 37500; break;
	case 41000: amd_clock = 41666; break;
	}

	if (amd_clock < 20000 || amd_clock > 50000) {
		printk(KERN_WARNING "%s: User given PCI clock speed impossible"
				    " (%d), using 33 MHz instead.\n",
				    d.name, amd_clock);
		amd_clock = 33333;
	}

	return ide_pci_init_one(dev, &d, NULL);
}

static const struct pci_device_id amd74xx_pci_tbl[] = {
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_COBRA_7401),		 0 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_VIPER_7409),		 1 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_VIPER_7411),		 2 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_OPUS_7441),		 2 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_8111_IDE),		 3 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_IDE),	 4 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE2_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE2S_IDE),	 5 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA),	 5 },
#endif
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3S_IDE),	 5 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2),	 5 },
#endif
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP65_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP67_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP73_IDE),	 5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP77_IDE),	 5 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_CS5536_IDE),		 6 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, amd74xx_pci_tbl);

static struct pci_driver amd74xx_pci_driver = {
	.name		= "AMD_IDE",
	.id_table	= amd74xx_pci_tbl,
	.probe		= amd74xx_probe,
	.remove		= ide_pci_remove,
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init amd74xx_ide_init(void)
{
	return ide_pci_register_driver(&amd74xx_pci_driver);
}

static void __exit amd74xx_ide_exit(void)
{
	pci_unregister_driver(&amd74xx_pci_driver);
}

module_init(amd74xx_ide_init);
module_exit(amd74xx_ide_exit);

MODULE_AUTHOR("Vojtech Pavlik");
MODULE_DESCRIPTION("AMD PCI IDE driver");
MODULE_LICENSE("GPL");
