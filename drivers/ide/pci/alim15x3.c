/*
 *  Copyright (C) 1998-2000 Michel Aubry, Maintainer
 *  Copyright (C) 1998-2000 Andrzej Krzysztofowicz, Maintainer
 *  Copyright (C) 1999-2000 CJ, cjtsai@ali.com.tw, Maintainer
 *
 *  Copyright (C) 1998-2000 Andre Hedrick (andre@linux-ide.org)
 *  May be copied or modified under the terms of the GNU General Public License
 *  Copyright (C) 2002 Alan Cox <alan@redhat.com>
 *  ALi (now ULi M5228) support by Clear Zhang <Clear.Zhang@ali.com.tw>
 *  Copyright (C) 2007 MontaVista Software, Inc. <source@mvista.com>
 *  Copyright (C) 2007 Bartlomiej Zolnierkiewicz <bzolnier@gmail.com>
 *
 *  (U)DMA capable version of ali 1533/1543(C), 1535(D)
 *
 **********************************************************************
 *  9/7/99 --Parts from the above author are included and need to be
 *  converted into standard interface, once I finish the thought.
 *
 *  Recent changes
 *	Don't use LBA48 mode on ALi <= 0xC4
 *	Don't poke 0x79 with a non ALi northbridge
 *	Don't flip undefined bits on newer chipsets (fix Fujitsu laptop hang)
 *	Allow UDMA6 on revisions > 0xC4
 *
 *  Documentation
 *	Chipset documentation available under NDA only
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/dmi.h>

#include <asm/io.h>

#define DRV_NAME "alim15x3"

/*
 * Allow UDMA on M1543C-E chipset for WDC disks that ignore CRC checking
 * (this is DANGEROUS and could result in data corruption).
 */
static int wdc_udma;

module_param(wdc_udma, bool, 0);
MODULE_PARM_DESC(wdc_udma,
		 "allow UDMA on M1543C-E chipset for WDC disks (DANGEROUS)");

/*
 *	ALi devices are not plug in. Otherwise these static values would
 *	need to go. They ought to go away anyway
 */
 
static u8 m5229_revision;
static u8 chip_is_1543c_e;
static struct pci_dev *isa_dev;

/**
 *	ali_set_pio_mode	-	set host controller for PIO mode
 *	@drive: drive
 *	@pio: PIO mode number
 *
 *	Program the controller for the given PIO mode.
 */

static void ali_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct ide_timing *t = ide_timing_find_mode(XFER_PIO_0 + pio);
	int s_time = t->setup, a_time = t->active, c_time = t->cycle;
	u8 s_clc, a_clc, r_clc;
	unsigned long flags;
	int bus_speed = ide_pci_clk ? ide_pci_clk : 33;
	int port = hwif->channel ? 0x5c : 0x58;
	int portFIFO = hwif->channel ? 0x55 : 0x54;
	u8 cd_dma_fifo = 0;
	int unit = drive->select.b.unit & 1;

	if ((s_clc = (s_time * bus_speed + 999) / 1000) >= 8)
		s_clc = 0;
	if ((a_clc = (a_time * bus_speed + 999) / 1000) >= 8)
		a_clc = 0;

	if (!(r_clc = (c_time * bus_speed + 999) / 1000 - a_clc - s_clc)) {
		r_clc = 1;
	} else {
		if (r_clc >= 16)
			r_clc = 0;
	}
	local_irq_save(flags);
	
	/* 
	 * PIO mode => ATA FIFO on, ATAPI FIFO off
	 */
	pci_read_config_byte(dev, portFIFO, &cd_dma_fifo);
	if (drive->media==ide_disk) {
		if (unit) {
			pci_write_config_byte(dev, portFIFO, (cd_dma_fifo & 0x0F) | 0x50);
		} else {
			pci_write_config_byte(dev, portFIFO, (cd_dma_fifo & 0xF0) | 0x05);
		}
	} else {
		if (unit) {
			pci_write_config_byte(dev, portFIFO, cd_dma_fifo & 0x0F);
		} else {
			pci_write_config_byte(dev, portFIFO, cd_dma_fifo & 0xF0);
		}
	}
	
	pci_write_config_byte(dev, port, s_clc);
	pci_write_config_byte(dev, port+drive->select.b.unit+2, (a_clc << 4) | r_clc);
	local_irq_restore(flags);
}

/**
 *	ali_udma_filter		-	compute UDMA mask
 *	@drive: IDE device
 *
 *	Return available UDMA modes.
 *
 *	The actual rules for the ALi are:
 *		No UDMA on revisions <= 0x20
 *		Disk only for revisions < 0xC2
 *		Not WDC drives on M1543C-E (?)
 */

static u8 ali_udma_filter(ide_drive_t *drive)
{
	if (m5229_revision > 0x20 && m5229_revision < 0xC2) {
		if (drive->media != ide_disk)
			return 0;
		if (wdc_udma == 0 && chip_is_1543c_e &&
		    strstr((char *)&drive->id[ATA_ID_PROD], "WDC "))
			return 0;
	}

	return drive->hwif->ultra_mask;
}

/**
 *	ali_set_dma_mode	-	set host controller for DMA mode
 *	@drive: drive
 *	@speed: DMA mode
 *
 *	Configure the hardware for the desired IDE transfer mode.
 */

static void ali_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u8 speed1		= speed;
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 tmpbyte		= 0x00;
	int m5229_udma		= (hwif->channel) ? 0x57 : 0x56;

	if (speed == XFER_UDMA_6)
		speed1 = 0x47;

	if (speed < XFER_UDMA_0) {
		u8 ultra_enable	= (unit) ? 0x7f : 0xf7;
		/*
		 * clear "ultra enable" bit
		 */
		pci_read_config_byte(dev, m5229_udma, &tmpbyte);
		tmpbyte &= ultra_enable;
		pci_write_config_byte(dev, m5229_udma, tmpbyte);

		/*
		 * FIXME: Oh, my... DMA timings are never set.
		 */
	} else {
		pci_read_config_byte(dev, m5229_udma, &tmpbyte);
		tmpbyte &= (0x0f << ((1-unit) << 2));
		/*
		 * enable ultra dma and set timing
		 */
		tmpbyte |= ((0x08 | ((4-speed1)&0x07)) << (unit << 2));
		pci_write_config_byte(dev, m5229_udma, tmpbyte);
		if (speed >= XFER_UDMA_3) {
			pci_read_config_byte(dev, 0x4b, &tmpbyte);
			tmpbyte |= 1;
			pci_write_config_byte(dev, 0x4b, tmpbyte);
		}
	}
}

/**
 *	ali15x3_dma_setup	-	begin a DMA phase
 *	@drive:	target device
 *
 *	Returns 1 if the DMA cannot be performed, zero on success.
 */

static int ali15x3_dma_setup(ide_drive_t *drive)
{
	if (m5229_revision < 0xC2 && drive->media != ide_disk) {
		if (rq_data_dir(drive->hwif->hwgroup->rq))
			return 1;	/* try PIO instead of DMA */
	}
	return ide_dma_setup(drive);
}

/**
 *	init_chipset_ali15x3	-	Initialise an ALi IDE controller
 *	@dev: PCI device
 *
 *	This function initializes the ALI IDE controller and where 
 *	appropriate also sets up the 1533 southbridge.
 */

static unsigned int __devinit init_chipset_ali15x3(struct pci_dev *dev)
{
	unsigned long flags;
	u8 tmpbyte;
	struct pci_dev *north = pci_get_slot(dev->bus, PCI_DEVFN(0,0));

	m5229_revision = dev->revision;

	isa_dev = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL);

	local_irq_save(flags);

	if (m5229_revision < 0xC2) {
		/*
		 * revision 0x20 (1543-E, 1543-F)
		 * revision 0xC0, 0xC1 (1543C-C, 1543C-D, 1543C-E)
		 * clear CD-ROM DMA write bit, m5229, 0x4b, bit 7
		 */
		pci_read_config_byte(dev, 0x4b, &tmpbyte);
		/*
		 * clear bit 7
		 */
		pci_write_config_byte(dev, 0x4b, tmpbyte & 0x7F);
		/*
		 * check m1533, 0x5e, bit 1~4 == 1001 => & 00011110 = 00010010
		 */
		if (m5229_revision >= 0x20 && isa_dev) {
			pci_read_config_byte(isa_dev, 0x5e, &tmpbyte);
			chip_is_1543c_e = ((tmpbyte & 0x1e) == 0x12) ? 1: 0;
		}
		goto out;
	}

	/*
	 * 1543C-B?, 1535, 1535D, 1553
	 * Note 1: not all "motherboard" support this detection
	 * Note 2: if no udma 66 device, the detection may "error".
	 *         but in this case, we will not set the device to
	 *         ultra 66, the detection result is not important
	 */

	/*
	 * enable "Cable Detection", m5229, 0x4b, bit3
	 */
	pci_read_config_byte(dev, 0x4b, &tmpbyte);
	pci_write_config_byte(dev, 0x4b, tmpbyte | 0x08);

	/*
	 * We should only tune the 1533 enable if we are using an ALi
	 * North bridge. We might have no north found on some zany
	 * box without a device at 0:0.0. The ALi bridge will be at
	 * 0:0.0 so if we didn't find one we know what is cooking.
	 */
	if (north && north->vendor != PCI_VENDOR_ID_AL)
		goto out;

	if (m5229_revision < 0xC5 && isa_dev)
	{	
		/*
		 * set south-bridge's enable bit, m1533, 0x79
		 */

		pci_read_config_byte(isa_dev, 0x79, &tmpbyte);
		if (m5229_revision == 0xC2) {
			/*
			 * 1543C-B0 (m1533, 0x79, bit 2)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x04);
		} else if (m5229_revision >= 0xC3) {
			/*
			 * 1553/1535 (m1533, 0x79, bit 1)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x02);
		}
	}

out:
	/*
	 * CD_ROM DMA on (m5229, 0x53, bit0)
	 *      Enable this bit even if we want to use PIO.
	 * PIO FIFO off (m5229, 0x53, bit1)
	 *      The hardware will use 0x54h and 0x55h to control PIO FIFO.
	 *	(Not on later devices it seems)
	 *
	 *	0x53 changes meaning on later revs - we must no touch
	 *	bit 1 on them.  Need to check if 0x20 is the right break.
	 */
	if (m5229_revision >= 0x20) {
		pci_read_config_byte(dev, 0x53, &tmpbyte);

		if (m5229_revision <= 0x20)
			tmpbyte = (tmpbyte & (~0x02)) | 0x01;
		else if (m5229_revision == 0xc7 || m5229_revision == 0xc8)
			tmpbyte |= 0x03;
		else
			tmpbyte |= 0x01;

		pci_write_config_byte(dev, 0x53, tmpbyte);
	}
	pci_dev_put(north);
	pci_dev_put(isa_dev);
	local_irq_restore(flags);
	return 0;
}

/*
 *	Cable special cases
 */

static const struct dmi_system_id cable_dmi_table[] = {
	{
		.ident = "HP Pavilion N5430",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_BOARD_VERSION, "OmniBook N32N-736"),
		},
	},
	{
		.ident = "Toshiba Satellite S1800-814",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1800-814"),
		},
	},
	{ }
};

static int ali_cable_override(struct pci_dev *pdev)
{
	/* Fujitsu P2000 */
	if (pdev->subsystem_vendor == 0x10CF &&
	    pdev->subsystem_device == 0x10AF)
		return 1;

	/* Mitac 8317 (Winbook-A) and relatives */
	if (pdev->subsystem_vendor == 0x1071 &&
	    pdev->subsystem_device == 0x8317)
		return 1;

	/* Systems by DMI */
	if (dmi_check_system(cable_dmi_table))
		return 1;

	return 0;
}

/**
 *	ali_cable_detect	-	cable detection
 *	@hwif: IDE interface
 *
 *	This checks if the controller and the cable are capable
 *	of UDMA66 transfers. It doesn't check the drives.
 *	But see note 2 below!
 *
 *	FIXME: frobs bits that are not defined on newer ALi devicea
 */

static u8 ali_cable_detect(ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	unsigned long flags;
	u8 cbl = ATA_CBL_PATA40, tmpbyte;

	local_irq_save(flags);

	if (m5229_revision >= 0xC2) {
		/*
		 * m5229 80-pin cable detection (from Host View)
		 *
		 * 0x4a bit0 is 0 => primary channel has 80-pin
		 * 0x4a bit1 is 0 => secondary channel has 80-pin
		 *
		 * Certain laptops use short but suitable cables
		 * and don't implement the detect logic.
		 */
		if (ali_cable_override(dev))
			cbl = ATA_CBL_PATA40_SHORT;
		else {
			pci_read_config_byte(dev, 0x4a, &tmpbyte);
			if ((tmpbyte & (1 << hwif->channel)) == 0)
				cbl = ATA_CBL_PATA80;
		}
	}

	local_irq_restore(flags);

	return cbl;
}

#if !defined(CONFIG_SPARC64) && !defined(CONFIG_PPC)
/**
 *	init_hwif_ali15x3	-	Initialize the ALI IDE x86 stuff
 *	@hwif: interface to configure
 *
 *	Obtain the IRQ tables for an ALi based IDE solution on the PC
 *	class platforms. This part of the code isn't applicable to the
 *	Sparc and PowerPC systems.
 */

static void __devinit init_hwif_ali15x3 (ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	u8 ideic, inmir;
	s8 irq_routing_table[] = { -1,  9, 3, 10, 4,  5, 7,  6,
				      1, 11, 0, 12, 0, 14, 0, 15 };
	int irq = -1;

	if (dev->device == PCI_DEVICE_ID_AL_M5229)
		hwif->irq = hwif->channel ? 15 : 14;

	if (isa_dev) {
		/*
		 * read IDE interface control
		 */
		pci_read_config_byte(isa_dev, 0x58, &ideic);

		/* bit0, bit1 */
		ideic = ideic & 0x03;

		/* get IRQ for IDE Controller */
		if ((hwif->channel && ideic == 0x03) ||
		    (!hwif->channel && !ideic)) {
			/*
			 * get SIRQ1 routing table
			 */
			pci_read_config_byte(isa_dev, 0x44, &inmir);
			inmir = inmir & 0x0f;
			irq = irq_routing_table[inmir];
		} else if (hwif->channel && !(ideic & 0x01)) {
			/*
			 * get SIRQ2 routing table
			 */
			pci_read_config_byte(isa_dev, 0x75, &inmir);
			inmir = inmir & 0x0f;
			irq = irq_routing_table[inmir];
		}
		if(irq >= 0)
			hwif->irq = irq;
	}
}
#else
#define init_hwif_ali15x3 NULL
#endif /* !defined(CONFIG_SPARC64) && !defined(CONFIG_PPC) */

/**
 *	init_dma_ali15x3	-	set up DMA on ALi15x3
 *	@hwif: IDE interface
 *	@d: IDE port info
 *
 *	Set up the DMA functionality on the ALi 15x3.
 */

static int __devinit init_dma_ali15x3(ide_hwif_t *hwif,
				      const struct ide_port_info *d)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	unsigned long base = ide_pci_dma_base(hwif, d);

	if (base == 0)
		return -1;

	hwif->dma_base = base;

	if (ide_pci_check_simplex(hwif, d) < 0)
		return -1;

	if (ide_pci_set_master(dev, d->name) < 0)
		return -1;

	if (!hwif->channel)
		outb(inb(base + 2) & 0x60, base + 2);

	printk(KERN_INFO "    %s: BM-DMA at 0x%04lx-0x%04lx\n",
			 hwif->name, base, base + 7);

	if (ide_allocate_dma_engine(hwif))
		return -1;

	hwif->dma_ops = &sff_dma_ops;

	return 0;
}

static const struct ide_port_ops ali_port_ops = {
	.set_pio_mode		= ali_set_pio_mode,
	.set_dma_mode		= ali_set_dma_mode,
	.udma_filter		= ali_udma_filter,
	.cable_detect		= ali_cable_detect,
};

static const struct ide_dma_ops ali_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ali15x3_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= ide_dma_start,
	.dma_end		= __ide_dma_end,
	.dma_test_irq		= ide_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timeout		= ide_dma_timeout,
};

static const struct ide_port_info ali15x3_chipset __devinitdata = {
	.name		= DRV_NAME,
	.init_chipset	= init_chipset_ali15x3,
	.init_hwif	= init_hwif_ali15x3,
	.init_dma	= init_dma_ali15x3,
	.port_ops	= &ali_port_ops,
	.pio_mask	= ATA_PIO5,
	.swdma_mask	= ATA_SWDMA2,
	.mwdma_mask	= ATA_MWDMA2,
};

/**
 *	alim15x3_init_one	-	set up an ALi15x3 IDE controller
 *	@dev: PCI device to set up
 *
 *	Perform the actual set up for an ALi15x3 that has been found by the
 *	hot plug layer.
 */
 
static int __devinit alim15x3_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct ide_port_info d = ali15x3_chipset;
	u8 rev = dev->revision, idx = id->driver_data;

	/* don't use LBA48 DMA on ALi devices before rev 0xC5 */
	if (rev <= 0xC4)
		d.host_flags |= IDE_HFLAG_NO_LBA48_DMA;

	if (rev >= 0x20) {
		if (rev == 0x20)
			d.host_flags |= IDE_HFLAG_NO_ATAPI_DMA;

		if (rev < 0xC2)
			d.udma_mask = ATA_UDMA2;
		else if (rev == 0xC2 || rev == 0xC3)
			d.udma_mask = ATA_UDMA4;
		else if (rev == 0xC4)
			d.udma_mask = ATA_UDMA5;
		else
			d.udma_mask = ATA_UDMA6;

		d.dma_ops = &ali_dma_ops;
	} else {
		d.host_flags |= IDE_HFLAG_NO_DMA;

		d.mwdma_mask = d.swdma_mask = 0;
	}

	if (idx == 0)
		d.host_flags |= IDE_HFLAG_CLEAR_SIMPLEX;

	return ide_pci_init_one(dev, &d, NULL);
}


static const struct pci_device_id alim15x3_pci_tbl[] = {
	{ PCI_VDEVICE(AL, PCI_DEVICE_ID_AL_M5229), 0 },
	{ PCI_VDEVICE(AL, PCI_DEVICE_ID_AL_M5228), 1 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, alim15x3_pci_tbl);

static struct pci_driver driver = {
	.name		= "ALI15x3_IDE",
	.id_table	= alim15x3_pci_tbl,
	.probe		= alim15x3_init_one,
	.remove		= ide_pci_remove,
};

static int __init ali15x3_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void __exit ali15x3_ide_exit(void)
{
	return pci_unregister_driver(&driver);
}

module_init(ali15x3_ide_init);
module_exit(ali15x3_ide_exit);

MODULE_AUTHOR("Michael Aubry, Andrzej Krzysztofowicz, CJ, Andre Hedrick, Alan Cox");
MODULE_DESCRIPTION("PCI driver module for ALi 15x3 IDE");
MODULE_LICENSE("GPL");
