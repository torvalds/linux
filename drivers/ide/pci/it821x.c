/*
 * Copyright (C) 2004		Red Hat <alan@redhat.com>
 * Copyright (C) 2007		Bartlomiej Zolnierkiewicz
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *  Based in part on the ITE vendor provided SCSI driver.
 *
 *  Documentation available from
 * 	http://www.ite.com.tw/pc/IT8212F_V04.pdf
 *  Some other documents are NDA.
 *
 *  The ITE8212 isn't exactly a standard IDE controller. It has two
 *  modes. In pass through mode then it is an IDE controller. In its smart
 *  mode its actually quite a capable hardware raid controller disguised
 *  as an IDE controller. Smart mode only understands DMA read/write and
 *  identify, none of the fancier commands apply. The IT8211 is identical
 *  in other respects but lacks the raid mode.
 *
 *  Errata:
 *  o	Rev 0x10 also requires master/slave hold the same DMA timings and
 *	cannot do ATAPI MWDMA.
 *  o	The identify data for raid volumes lacks CHS info (technically ok)
 *	but also fails to set the LBA28 and other bits. We fix these in
 *	the IDE probe quirk code.
 *  o	If you write LBA48 sized I/O's (ie > 256 sector) in smart mode
 *	raid then the controller firmware dies
 *  o	Smart mode without RAID doesn't clear all the necessary identify
 *	bits to reduce the command set to the one used
 *
 *  This has a few impacts on the driver
 *  - In pass through mode we do all the work you would expect
 *  - In smart mode the clocking set up is done by the controller generally
 *    but we must watch the other limits and filter.
 *  - There are a few extra vendor commands that actually talk to the
 *    controller but only work PIO with no IRQ.
 *
 *  Vendor areas of the identify block in smart mode are used for the
 *  timing and policy set up. Each HDD in raid mode also has a serial
 *  block on the disk. The hardware extra commands are get/set chip status,
 *  rebuild, get rebuild status.
 *
 *  In Linux the driver supports pass through mode as if the device was
 *  just another IDE controller. If the smart mode is running then
 *  volumes are managed by the controller firmware and each IDE "disk"
 *  is a raid volume. Even more cute - the controller can do automated
 *  hotplug and rebuild.
 *
 *  The pass through controller itself is a little demented. It has a
 *  flaw that it has a single set of PIO/MWDMA timings per channel so
 *  non UDMA devices restrict each others performance. It also has a
 *  single clock source per channel so mixed UDMA100/133 performance
 *  isn't perfect and we have to pick a clock. Thankfully none of this
 *  matters in smart mode. ATAPI DMA is not currently supported.
 *
 *  It seems the smart mode is a win for RAID1/RAID10 but otherwise not.
 *
 *  TODO
 *	-	ATAPI UDMA is ok but not MWDMA it seems
 *	-	RAID configuration ioctls
 *	-	Move to libata once it grows up
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#define DRV_NAME "it821x"

struct it821x_dev
{
	unsigned int smart:1,		/* Are we in smart raid mode */
		timing10:1;		/* Rev 0x10 */
	u8	clock_mode;		/* 0, ATA_50 or ATA_66 */
	u8	want[2][2];		/* Mode/Pri log for master slave */
	/* We need these for switching the clock when DMA goes on/off
	   The high byte is the 66Mhz timing */
	u16	pio[2];			/* Cached PIO values */
	u16	mwdma[2];		/* Cached MWDMA values */
	u16	udma[2];		/* Cached UDMA values (per drive) */
};

#define ATA_66		0
#define ATA_50		1
#define ATA_ANY		2

#define UDMA_OFF	0
#define MWDMA_OFF	0

/*
 *	We allow users to force the card into non raid mode without
 *	flashing the alternative BIOS. This is also necessary right now
 *	for embedded platforms that cannot run a PC BIOS but are using this
 *	device.
 */

static int it8212_noraid;

/**
 *	it821x_program	-	program the PIO/MWDMA registers
 *	@drive: drive to tune
 *	@timing: timing info
 *
 *	Program the PIO/MWDMA timing for this channel according to the
 *	current clock.
 */

static void it821x_program(ide_drive_t *drive, u16 timing)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct it821x_dev *itdev = ide_get_hwifdata(hwif);
	int channel = hwif->channel;
	u8 conf;

	/* Program PIO/MWDMA timing bits */
	if(itdev->clock_mode == ATA_66)
		conf = timing >> 8;
	else
		conf = timing & 0xFF;

	pci_write_config_byte(dev, 0x54 + 4 * channel, conf);
}

/**
 *	it821x_program_udma	-	program the UDMA registers
 *	@drive: drive to tune
 *	@timing: timing info
 *
 *	Program the UDMA timing for this drive according to the
 *	current clock.
 */

static void it821x_program_udma(ide_drive_t *drive, u16 timing)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct it821x_dev *itdev = ide_get_hwifdata(hwif);
	int channel = hwif->channel;
	int unit = drive->select.b.unit;
	u8 conf;

	/* Program UDMA timing bits */
	if(itdev->clock_mode == ATA_66)
		conf = timing >> 8;
	else
		conf = timing & 0xFF;

	if (itdev->timing10 == 0)
		pci_write_config_byte(dev, 0x56 + 4 * channel + unit, conf);
	else {
		pci_write_config_byte(dev, 0x56 + 4 * channel, conf);
		pci_write_config_byte(dev, 0x56 + 4 * channel + 1, conf);
	}
}

/**
 *	it821x_clock_strategy
 *	@drive: drive to set up
 *
 *	Select between the 50 and 66Mhz base clocks to get the best
 *	results for this interface.
 */

static void it821x_clock_strategy(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct it821x_dev *itdev = ide_get_hwifdata(hwif);

	u8 unit = drive->select.b.unit;
	ide_drive_t *pair = &hwif->drives[1-unit];

	int clock, altclock;
	u8 v;
	int sel = 0;

	if(itdev->want[0][0] > itdev->want[1][0]) {
		clock = itdev->want[0][1];
		altclock = itdev->want[1][1];
	} else {
		clock = itdev->want[1][1];
		altclock = itdev->want[0][1];
	}

	/*
	 * if both clocks can be used for the mode with the higher priority
	 * use the clock needed by the mode with the lower priority
	 */
	if (clock == ATA_ANY)
		clock = altclock;

	/* Nobody cares - keep the same clock */
	if(clock == ATA_ANY)
		return;
	/* No change */
	if(clock == itdev->clock_mode)
		return;

	/* Load this into the controller ? */
	if(clock == ATA_66)
		itdev->clock_mode = ATA_66;
	else {
		itdev->clock_mode = ATA_50;
		sel = 1;
	}

	pci_read_config_byte(dev, 0x50, &v);
	v &= ~(1 << (1 + hwif->channel));
	v |= sel << (1 + hwif->channel);
	pci_write_config_byte(dev, 0x50, v);

	/*
	 *	Reprogram the UDMA/PIO of the pair drive for the switch
	 *	MWDMA will be dealt with by the dma switcher
	 */
	if(pair && itdev->udma[1-unit] != UDMA_OFF) {
		it821x_program_udma(pair, itdev->udma[1-unit]);
		it821x_program(pair, itdev->pio[1-unit]);
	}
	/*
	 *	Reprogram the UDMA/PIO of our drive for the switch.
	 *	MWDMA will be dealt with by the dma switcher
	 */
	if(itdev->udma[unit] != UDMA_OFF) {
		it821x_program_udma(drive, itdev->udma[unit]);
		it821x_program(drive, itdev->pio[unit]);
	}
}

/**
 *	it821x_set_pio_mode	-	set host controller for PIO mode
 *	@drive: drive
 *	@pio: PIO mode number
 *
 *	Tune the host to the desired PIO mode taking into the consideration
 *	the maximum PIO mode supported by the other device on the cable.
 */

static void it821x_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct it821x_dev *itdev = ide_get_hwifdata(hwif);
	int unit = drive->select.b.unit;
	ide_drive_t *pair = &hwif->drives[1 - unit];
	u8 set_pio = pio;

	/* Spec says 89 ref driver uses 88 */
	static u16 pio_timings[]= { 0xAA88, 0xA382, 0xA181, 0x3332, 0x3121 };
	static u8 pio_want[]    = { ATA_66, ATA_66, ATA_66, ATA_66, ATA_ANY };

	/*
	 * Compute the best PIO mode we can for a given device. We must
	 * pick a speed that does not cause problems with the other device
	 * on the cable.
	 */
	if (pair) {
		u8 pair_pio = ide_get_best_pio_mode(pair, 255, 4);
		/* trim PIO to the slowest of the master/slave */
		if (pair_pio < set_pio)
			set_pio = pair_pio;
	}

	/* We prefer 66Mhz clock for PIO 0-3, don't care for PIO4 */
	itdev->want[unit][1] = pio_want[set_pio];
	itdev->want[unit][0] = 1;	/* PIO is lowest priority */
	itdev->pio[unit] = pio_timings[set_pio];
	it821x_clock_strategy(drive);
	it821x_program(drive, itdev->pio[unit]);
}

/**
 *	it821x_tune_mwdma	-	tune a channel for MWDMA
 *	@drive: drive to set up
 *	@mode_wanted: the target operating mode
 *
 *	Load the timing settings for this device mode into the
 *	controller when doing MWDMA in pass through mode. The caller
 *	must manage the whole lack of per device MWDMA/PIO timings and
 *	the shared MWDMA/PIO timing register.
 */

static void it821x_tune_mwdma (ide_drive_t *drive, byte mode_wanted)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct it821x_dev *itdev = (void *)ide_get_hwifdata(hwif);
	int unit = drive->select.b.unit;
	int channel = hwif->channel;
	u8 conf;

	static u16 dma[]	= { 0x8866, 0x3222, 0x3121 };
	static u8 mwdma_want[]	= { ATA_ANY, ATA_66, ATA_ANY };

	itdev->want[unit][1] = mwdma_want[mode_wanted];
	itdev->want[unit][0] = 2;	/* MWDMA is low priority */
	itdev->mwdma[unit] = dma[mode_wanted];
	itdev->udma[unit] = UDMA_OFF;

	/* UDMA bits off - Revision 0x10 do them in pairs */
	pci_read_config_byte(dev, 0x50, &conf);
	if (itdev->timing10)
		conf |= channel ? 0x60: 0x18;
	else
		conf |= 1 << (3 + 2 * channel + unit);
	pci_write_config_byte(dev, 0x50, conf);

	it821x_clock_strategy(drive);
	/* FIXME: do we need to program this ? */
	/* it821x_program(drive, itdev->mwdma[unit]); */
}

/**
 *	it821x_tune_udma	-	tune a channel for UDMA
 *	@drive: drive to set up
 *	@mode_wanted: the target operating mode
 *
 *	Load the timing settings for this device mode into the
 *	controller when doing UDMA modes in pass through.
 */

static void it821x_tune_udma (ide_drive_t *drive, byte mode_wanted)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct it821x_dev *itdev = ide_get_hwifdata(hwif);
	int unit = drive->select.b.unit;
	int channel = hwif->channel;
	u8 conf;

	static u16 udma[]	= { 0x4433, 0x4231, 0x3121, 0x2121, 0x1111, 0x2211, 0x1111 };
	static u8 udma_want[]	= { ATA_ANY, ATA_50, ATA_ANY, ATA_66, ATA_66, ATA_50, ATA_66 };

	itdev->want[unit][1] = udma_want[mode_wanted];
	itdev->want[unit][0] = 3;	/* UDMA is high priority */
	itdev->mwdma[unit] = MWDMA_OFF;
	itdev->udma[unit] = udma[mode_wanted];
	if(mode_wanted >= 5)
		itdev->udma[unit] |= 0x8080;	/* UDMA 5/6 select on */

	/* UDMA on. Again revision 0x10 must do the pair */
	pci_read_config_byte(dev, 0x50, &conf);
	if (itdev->timing10)
		conf &= channel ? 0x9F: 0xE7;
	else
		conf &= ~ (1 << (3 + 2 * channel + unit));
	pci_write_config_byte(dev, 0x50, conf);

	it821x_clock_strategy(drive);
	it821x_program_udma(drive, itdev->udma[unit]);

}

/**
 *	it821x_dma_read	-	DMA hook
 *	@drive: drive for DMA
 *
 *	The IT821x has a single timing register for MWDMA and for PIO
 *	operations. As we flip back and forth we have to reload the
 *	clock. In addition the rev 0x10 device only works if the same
 *	timing value is loaded into the master and slave UDMA clock
 * 	so we must also reload that.
 *
 *	FIXME: we could figure out in advance if we need to do reloads
 */

static void it821x_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct it821x_dev *itdev = ide_get_hwifdata(hwif);
	int unit = drive->select.b.unit;
	if(itdev->mwdma[unit] != MWDMA_OFF)
		it821x_program(drive, itdev->mwdma[unit]);
	else if(itdev->udma[unit] != UDMA_OFF && itdev->timing10)
		it821x_program_udma(drive, itdev->udma[unit]);
	ide_dma_start(drive);
}

/**
 *	it821x_dma_write	-	DMA hook
 *	@drive: drive for DMA stop
 *
 *	The IT821x has a single timing register for MWDMA and for PIO
 *	operations. As we flip back and forth we have to reload the
 *	clock.
 */

static int it821x_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	int unit = drive->select.b.unit;
	struct it821x_dev *itdev = ide_get_hwifdata(hwif);
	int ret = __ide_dma_end(drive);
	if(itdev->mwdma[unit] != MWDMA_OFF)
		it821x_program(drive, itdev->pio[unit]);
	return ret;
}

/**
 *	it821x_set_dma_mode	-	set host controller for DMA mode
 *	@drive: drive
 *	@speed: DMA mode
 *
 *	Tune the ITE chipset for the desired DMA mode.
 */

static void it821x_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	/*
	 * MWDMA tuning is really hard because our MWDMA and PIO
	 * timings are kept in the same place.  We can switch in the
	 * host dma on/off callbacks.
	 */
	if (speed >= XFER_UDMA_0 && speed <= XFER_UDMA_6)
		it821x_tune_udma(drive, speed - XFER_UDMA_0);
	else if (speed >= XFER_MW_DMA_0 && speed <= XFER_MW_DMA_2)
		it821x_tune_mwdma(drive, speed - XFER_MW_DMA_0);
}

/**
 *	it821x_cable_detect	-	cable detection
 *	@hwif: interface to check
 *
 *	Check for the presence of an ATA66 capable cable on the
 *	interface. Problematic as it seems some cards don't have
 *	the needed logic onboard.
 */

static u8 it821x_cable_detect(ide_hwif_t *hwif)
{
	/* The reference driver also only does disk side */
	return ATA_CBL_PATA80;
}

/**
 *	it821x_quirkproc	-	post init callback
 *	@drive: drive
 *
 *	This callback is run after the drive has been probed but
 *	before anything gets attached. It allows drivers to do any
 *	final tuning that is needed, or fixups to work around bugs.
 */

static void it821x_quirkproc(ide_drive_t *drive)
{
	struct it821x_dev *itdev = ide_get_hwifdata(drive->hwif);
	struct hd_driveid *id = drive->id;
	u16 *idbits = (u16 *)drive->id;

	if (!itdev->smart) {
		/*
		 *	If we are in pass through mode then not much
		 *	needs to be done, but we do bother to clear the
		 *	IRQ mask as we may well be in PIO (eg rev 0x10)
		 *	for now and we know unmasking is safe on this chipset.
		 */
		drive->unmask = 1;
	} else {
	/*
	 *	Perform fixups on smart mode. We need to "lose" some
	 *	capabilities the firmware lacks but does not filter, and
	 *	also patch up some capability bits that it forgets to set
	 *	in RAID mode.
	 */

		/* Check for RAID v native */
		if(strstr(id->model, "Integrated Technology Express")) {
			/* In raid mode the ident block is slightly buggy
			   We need to set the bits so that the IDE layer knows
			   LBA28. LBA48 and DMA ar valid */
			id->capability |= 3;		/* LBA28, DMA */
			id->command_set_2 |= 0x0400;	/* LBA48 valid */
			id->cfs_enable_2 |= 0x0400;	/* LBA48 on */
			/* Reporting logic */
			printk(KERN_INFO "%s: IT8212 %sRAID %d volume",
				drive->name,
				idbits[147] ? "Bootable ":"",
				idbits[129]);
				if(idbits[129] != 1)
					printk("(%dK stripe)", idbits[146]);
				printk(".\n");
		} else {
			/* Non RAID volume. Fixups to stop the core code
			   doing unsupported things */
			id->field_valid &= 3;
			id->queue_depth = 0;
			id->command_set_1 = 0;
			id->command_set_2 &= 0xC400;
			id->cfsse &= 0xC000;
			id->cfs_enable_1 = 0;
			id->cfs_enable_2 &= 0xC400;
			id->csf_default &= 0xC000;
			id->word127 = 0;
			id->dlf = 0;
			id->csfo = 0;
			id->cfa_power = 0;
			printk(KERN_INFO "%s: Performing identify fixups.\n",
				drive->name);
		}

		/*
		 * Set MWDMA0 mode as enabled/support - just to tell
		 * IDE core that DMA is supported (it821x hardware
		 * takes care of DMA mode programming).
		 */
		if (id->capability & 1) {
			id->dma_mword |= 0x0101;
			drive->current_speed = XFER_MW_DMA_0;
		}
	}

}

static struct ide_dma_ops it821x_pass_through_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= it821x_dma_start,
	.dma_end		= it821x_dma_end,
	.dma_test_irq		= ide_dma_test_irq,
	.dma_timeout		= ide_dma_timeout,
	.dma_lost_irq		= ide_dma_lost_irq,
};

/**
 *	init_hwif_it821x	-	set up hwif structs
 *	@hwif: interface to set up
 *
 *	We do the basic set up of the interface structure. The IT8212
 *	requires several custom handlers so we override the default
 *	ide DMA handlers appropriately
 */

static void __devinit init_hwif_it821x(ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct ide_host *host = pci_get_drvdata(dev);
	struct it821x_dev *itdevs = host->host_priv;
	struct it821x_dev *idev = itdevs + hwif->channel;
	u8 conf;

	ide_set_hwifdata(hwif, idev);

	pci_read_config_byte(dev, 0x50, &conf);
	if (conf & 1) {
		idev->smart = 1;
		hwif->host_flags |= IDE_HFLAG_NO_ATAPI_DMA;
		/* Long I/O's although allowed in LBA48 space cause the
		   onboard firmware to enter the twighlight zone */
		hwif->rqsize = 256;
	}

	/* Pull the current clocks from 0x50 also */
	if (conf & (1 << (1 + hwif->channel)))
		idev->clock_mode = ATA_50;
	else
		idev->clock_mode = ATA_66;

	idev->want[0][1] = ATA_ANY;
	idev->want[1][1] = ATA_ANY;

	/*
	 *	Not in the docs but according to the reference driver
	 *	this is necessary.
	 */

	pci_read_config_byte(dev, 0x08, &conf);
	if (conf == 0x10) {
		idev->timing10 = 1;
		hwif->host_flags |= IDE_HFLAG_NO_ATAPI_DMA;
		if (idev->smart == 0)
			printk(KERN_WARNING DRV_NAME " %s: revision 0x10, "
				"workarounds activated\n", pci_name(dev));
	}

	if (idev->smart == 0) {
		/* MWDMA/PIO clock switching for pass through mode */
		hwif->dma_ops = &it821x_pass_through_dma_ops;
	} else
		hwif->host_flags |= IDE_HFLAG_NO_SET_MODE;

	if (hwif->dma_base == 0)
		return;

	hwif->ultra_mask = ATA_UDMA6;
	hwif->mwdma_mask = ATA_MWDMA2;
}

static void __devinit it8212_disable_raid(struct pci_dev *dev)
{
	/* Reset local CPU, and set BIOS not ready */
	pci_write_config_byte(dev, 0x5E, 0x01);

	/* Set to bypass mode, and reset PCI bus */
	pci_write_config_byte(dev, 0x50, 0x00);
	pci_write_config_word(dev, PCI_COMMAND,
			      PCI_COMMAND_PARITY | PCI_COMMAND_IO |
			      PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	pci_write_config_word(dev, 0x40, 0xA0F3);

	pci_write_config_dword(dev,0x4C, 0x02040204);
	pci_write_config_byte(dev, 0x42, 0x36);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x20);
}

static unsigned int __devinit init_chipset_it821x(struct pci_dev *dev)
{
	u8 conf;
	static char *mode[2] = { "pass through", "smart" };

	/* Force the card into bypass mode if so requested */
	if (it8212_noraid) {
		printk(KERN_INFO DRV_NAME " %s: forcing bypass mode\n",
			pci_name(dev));
		it8212_disable_raid(dev);
	}
	pci_read_config_byte(dev, 0x50, &conf);
	printk(KERN_INFO DRV_NAME " %s: controller in %s mode\n",
		pci_name(dev), mode[conf & 1]);
	return 0;
}

static const struct ide_port_ops it821x_port_ops = {
	/* it821x_set_{pio,dma}_mode() are only used in pass-through mode */
	.set_pio_mode		= it821x_set_pio_mode,
	.set_dma_mode		= it821x_set_dma_mode,
	.quirkproc		= it821x_quirkproc,
	.cable_detect		= it821x_cable_detect,
};

static const struct ide_port_info it821x_chipset __devinitdata = {
	.name		= DRV_NAME,
	.init_chipset	= init_chipset_it821x,
	.init_hwif	= init_hwif_it821x,
	.port_ops	= &it821x_port_ops,
	.pio_mask	= ATA_PIO4,
};

/**
 *	it821x_init_one	-	pci layer discovery entry
 *	@dev: PCI device
 *	@id: ident table entry
 *
 *	Called by the PCI code when it finds an ITE821x controller.
 *	We then use the IDE PCI generic helper to do most of the work.
 */

static int __devinit it821x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct it821x_dev *itdevs;
	int rc;

	itdevs = kzalloc(2 * sizeof(*itdevs), GFP_KERNEL);
	if (itdevs == NULL) {
		printk(KERN_ERR DRV_NAME " %s: out of memory\n", pci_name(dev));
		return -ENOMEM;
	}

	rc = ide_pci_init_one(dev, &it821x_chipset, itdevs);
	if (rc)
		kfree(itdevs);

	return rc;
}

static void __devexit it821x_remove(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	struct it821x_dev *itdevs = host->host_priv;

	ide_pci_remove(dev);
	kfree(itdevs);
}

static const struct pci_device_id it821x_pci_tbl[] = {
	{ PCI_VDEVICE(ITE, PCI_DEVICE_ID_ITE_8211), 0 },
	{ PCI_VDEVICE(ITE, PCI_DEVICE_ID_ITE_8212), 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, it821x_pci_tbl);

static struct pci_driver driver = {
	.name		= "ITE821x IDE",
	.id_table	= it821x_pci_tbl,
	.probe		= it821x_init_one,
	.remove		= __devexit_p(it821x_remove),
};

static int __init it821x_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void __exit it821x_ide_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(it821x_ide_init);
module_exit(it821x_ide_exit);

module_param_named(noraid, it8212_noraid, int, S_IRUGO);
MODULE_PARM_DESC(noraid, "Force card into bypass mode");

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("PCI driver module for the ITE 821x");
MODULE_LICENSE("GPL");
