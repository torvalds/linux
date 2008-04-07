/*
 * pata_it821x.c 	- IT821x PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *			  (C) 2007 Bartlomiej Zolnierkiewicz
 *
 * based upon
 *
 * it821x.c
 *
 * linux/drivers/ide/pci/it821x.c		Version 0.09	December 2004
 *
 * Copyright (C) 2004		Red Hat <alan@redhat.com>
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
 *	-	ATAPI and other speed filtering
 *	-	RAID configuration ioctls
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>


#define DRV_NAME "pata_it821x"
#define DRV_VERSION "0.3.8"

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
	u16	last_device;		/* Master or slave loaded ? */
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
 *	@ap: ATA port
 *	@adev: Device to program
 *	@timing: Timing value (66Mhz in top 8bits, 50 in the low 8)
 *
 *	Program the PIO/MWDMA timing for this channel according to the
 *	current clock. These share the same register so are managed by
 *	the DMA start/stop sequence as with the old driver.
 */

static void it821x_program(struct ata_port *ap, struct ata_device *adev, u16 timing)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct it821x_dev *itdev = ap->private_data;
	int channel = ap->port_no;
	u8 conf;

	/* Program PIO/MWDMA timing bits */
	if (itdev->clock_mode == ATA_66)
		conf = timing >> 8;
	else
		conf = timing & 0xFF;
	pci_write_config_byte(pdev, 0x54 + 4 * channel, conf);
}


/**
 *	it821x_program_udma	-	program the UDMA registers
 *	@ap: ATA port
 *	@adev: ATA device to update
 *	@timing: Timing bits. Top 8 are for 66Mhz bottom for 50Mhz
 *
 *	Program the UDMA timing for this drive according to the
 *	current clock. Handles the dual clocks and also knows about
 *	the errata on the 0x10 revision. The UDMA errata is partly handled
 *	here and partly in start_dma.
 */

static void it821x_program_udma(struct ata_port *ap, struct ata_device *adev, u16 timing)
{
	struct it821x_dev *itdev = ap->private_data;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int channel = ap->port_no;
	int unit = adev->devno;
	u8 conf;

	/* Program UDMA timing bits */
	if (itdev->clock_mode == ATA_66)
		conf = timing >> 8;
	else
		conf = timing & 0xFF;
	if (itdev->timing10 == 0)
		pci_write_config_byte(pdev, 0x56 + 4 * channel + unit, conf);
	else {
		/* Early revision must be programmed for both together */
		pci_write_config_byte(pdev, 0x56 + 4 * channel, conf);
		pci_write_config_byte(pdev, 0x56 + 4 * channel + 1, conf);
	}
}

/**
 *	it821x_clock_strategy
 *	@ap: ATA interface
 *	@adev: ATA device being updated
 *
 *	Select between the 50 and 66Mhz base clocks to get the best
 *	results for this interface.
 */

static void it821x_clock_strategy(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct it821x_dev *itdev = ap->private_data;
	u8 unit = adev->devno;
	struct ata_device *pair = ata_dev_pair(adev);

	int clock, altclock;
	u8 v;
	int sel = 0;

	/* Look for the most wanted clocking */
	if (itdev->want[0][0] > itdev->want[1][0]) {
		clock = itdev->want[0][1];
		altclock = itdev->want[1][1];
	} else {
		clock = itdev->want[1][1];
		altclock = itdev->want[0][1];
	}

	/* Master doesn't care does the slave ? */
	if (clock == ATA_ANY)
		clock = altclock;

	/* Nobody cares - keep the same clock */
	if (clock == ATA_ANY)
		return;
	/* No change */
	if (clock == itdev->clock_mode)
		return;

	/* Load this into the controller */
	if (clock == ATA_66)
		itdev->clock_mode = ATA_66;
	else {
		itdev->clock_mode = ATA_50;
		sel = 1;
	}
	pci_read_config_byte(pdev, 0x50, &v);
	v &= ~(1 << (1 + ap->port_no));
	v |= sel << (1 + ap->port_no);
	pci_write_config_byte(pdev, 0x50, v);

	/*
	 *	Reprogram the UDMA/PIO of the pair drive for the switch
	 *	MWDMA will be dealt with by the dma switcher
	 */
	if (pair && itdev->udma[1-unit] != UDMA_OFF) {
		it821x_program_udma(ap, pair, itdev->udma[1-unit]);
		it821x_program(ap, pair, itdev->pio[1-unit]);
	}
	/*
	 *	Reprogram the UDMA/PIO of our drive for the switch.
	 *	MWDMA will be dealt with by the dma switcher
	 */
	if (itdev->udma[unit] != UDMA_OFF) {
		it821x_program_udma(ap, adev, itdev->udma[unit]);
		it821x_program(ap, adev, itdev->pio[unit]);
	}
}

/**
 *	it821x_passthru_set_piomode	-	set PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Configure for PIO mode. This is complicated as the register is
 *	shared by PIO and MWDMA and for both channels.
 */

static void it821x_passthru_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	/* Spec says 89 ref driver uses 88 */
	static const u16 pio[]	= { 0xAA88, 0xA382, 0xA181, 0x3332, 0x3121 };
	static const u8 pio_want[]    = { ATA_66, ATA_66, ATA_66, ATA_66, ATA_ANY };

	struct it821x_dev *itdev = ap->private_data;
	int unit = adev->devno;
	int mode_wanted = adev->pio_mode - XFER_PIO_0;

	/* We prefer 66Mhz clock for PIO 0-3, don't care for PIO4 */
	itdev->want[unit][1] = pio_want[mode_wanted];
	itdev->want[unit][0] = 1;	/* PIO is lowest priority */
	itdev->pio[unit] = pio[mode_wanted];
	it821x_clock_strategy(ap, adev);
	it821x_program(ap, adev, itdev->pio[unit]);
}

/**
 *	it821x_passthru_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Set up the DMA modes. The actions taken depend heavily on the mode
 *	to use. If UDMA is used as is hopefully the usual case then the
 *	timing register is private and we need only consider the clock. If
 *	we are using MWDMA then we have to manage the setting ourself as
 *	we switch devices and mode.
 */

static void it821x_passthru_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static const u16 dma[]	= 	{ 0x8866, 0x3222, 0x3121 };
	static const u8 mwdma_want[] =  { ATA_ANY, ATA_66, ATA_ANY };
	static const u16 udma[]	= 	{ 0x4433, 0x4231, 0x3121, 0x2121, 0x1111, 0x2211, 0x1111 };
	static const u8 udma_want[] =   { ATA_ANY, ATA_50, ATA_ANY, ATA_66, ATA_66, ATA_50, ATA_66 };

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct it821x_dev *itdev = ap->private_data;
	int channel = ap->port_no;
	int unit = adev->devno;
	u8 conf;

	if (adev->dma_mode >= XFER_UDMA_0) {
		int mode_wanted = adev->dma_mode - XFER_UDMA_0;

		itdev->want[unit][1] = udma_want[mode_wanted];
		itdev->want[unit][0] = 3;	/* UDMA is high priority */
		itdev->mwdma[unit] = MWDMA_OFF;
		itdev->udma[unit] = udma[mode_wanted];
		if (mode_wanted >= 5)
			itdev->udma[unit] |= 0x8080;	/* UDMA 5/6 select on */

		/* UDMA on. Again revision 0x10 must do the pair */
		pci_read_config_byte(pdev, 0x50, &conf);
		if (itdev->timing10)
			conf &= channel ? 0x9F: 0xE7;
		else
			conf &= ~ (1 << (3 + 2 * channel + unit));
		pci_write_config_byte(pdev, 0x50, conf);
		it821x_clock_strategy(ap, adev);
		it821x_program_udma(ap, adev, itdev->udma[unit]);
	} else {
		int mode_wanted = adev->dma_mode - XFER_MW_DMA_0;

		itdev->want[unit][1] = mwdma_want[mode_wanted];
		itdev->want[unit][0] = 2;	/* MWDMA is low priority */
		itdev->mwdma[unit] = dma[mode_wanted];
		itdev->udma[unit] = UDMA_OFF;

		/* UDMA bits off - Revision 0x10 do them in pairs */
		pci_read_config_byte(pdev, 0x50, &conf);
		if (itdev->timing10)
			conf |= channel ? 0x60: 0x18;
		else
			conf |= 1 << (3 + 2 * channel + unit);
		pci_write_config_byte(pdev, 0x50, conf);
		it821x_clock_strategy(ap, adev);
	}
}

/**
 *	it821x_passthru_dma_start	-	DMA start callback
 *	@qc: Command in progress
 *
 *	Usually drivers set the DMA timing at the point the set_dmamode call
 *	is made. IT821x however requires we load new timings on the
 *	transitions in some cases.
 */

static void it821x_passthru_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct it821x_dev *itdev = ap->private_data;
	int unit = adev->devno;

	if (itdev->mwdma[unit] != MWDMA_OFF)
		it821x_program(ap, adev, itdev->mwdma[unit]);
	else if (itdev->udma[unit] != UDMA_OFF && itdev->timing10)
		it821x_program_udma(ap, adev, itdev->udma[unit]);
	ata_bmdma_start(qc);
}

/**
 *	it821x_passthru_dma_stop	-	DMA stop callback
 *	@qc: ATA command
 *
 *	We loaded new timings in dma_start, as a result we need to restore
 *	the PIO timings in dma_stop so that the next command issue gets the
 *	right clock values.
 */

static void it821x_passthru_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct it821x_dev *itdev = ap->private_data;
	int unit = adev->devno;

	ata_bmdma_stop(qc);
	if (itdev->mwdma[unit] != MWDMA_OFF)
		it821x_program(ap, adev, itdev->pio[unit]);
}


/**
 *	it821x_passthru_dev_select	-	Select master/slave
 *	@ap: ATA port
 *	@device: Device number (not pointer)
 *
 *	Device selection hook. If necessary perform clock switching
 */

static void it821x_passthru_dev_select(struct ata_port *ap,
				       unsigned int device)
{
	struct it821x_dev *itdev = ap->private_data;
	if (itdev && device != itdev->last_device) {
		struct ata_device *adev = &ap->link.device[device];
		it821x_program(ap, adev, itdev->pio[adev->devno]);
		itdev->last_device = device;
	}
	ata_sff_dev_select(ap, device);
}

/**
 *	it821x_smart_qc_issue		-	wrap qc issue prot
 *	@qc: command
 *
 *	Wrap the command issue sequence for the IT821x. We need to
 *	perform out own device selection timing loads before the
 *	usual happenings kick off
 */

static unsigned int it821x_smart_qc_issue(struct ata_queued_cmd *qc)
{
	switch(qc->tf.command)
	{
		/* Commands the firmware supports */
		case ATA_CMD_READ:
		case ATA_CMD_READ_EXT:
		case ATA_CMD_WRITE:
		case ATA_CMD_WRITE_EXT:
		case ATA_CMD_PIO_READ:
		case ATA_CMD_PIO_READ_EXT:
		case ATA_CMD_PIO_WRITE:
		case ATA_CMD_PIO_WRITE_EXT:
		case ATA_CMD_READ_MULTI:
		case ATA_CMD_READ_MULTI_EXT:
		case ATA_CMD_WRITE_MULTI:
		case ATA_CMD_WRITE_MULTI_EXT:
		case ATA_CMD_ID_ATA:
		/* Arguably should just no-op this one */
		case ATA_CMD_SET_FEATURES:
			return ata_sff_qc_issue(qc);
	}
	printk(KERN_DEBUG "it821x: can't process command 0x%02X\n", qc->tf.command);
	return AC_ERR_DEV;
}

/**
 *	it821x_passthru_qc_issue	-	wrap qc issue prot
 *	@qc: command
 *
 *	Wrap the command issue sequence for the IT821x. We need to
 *	perform out own device selection timing loads before the
 *	usual happenings kick off
 */

static unsigned int it821x_passthru_qc_issue(struct ata_queued_cmd *qc)
{
	it821x_passthru_dev_select(qc->ap, qc->dev->devno);
	return ata_sff_qc_issue(qc);
}

/**
 *	it821x_smart_set_mode	-	mode setting
 *	@link: interface to set up
 *	@unused: device that failed (error only)
 *
 *	Use a non standard set_mode function. We don't want to be tuned.
 *	The BIOS configured everything. Our job is not to fiddle. We
 *	read the dma enabled bits from the PCI configuration of the device
 *	and respect them.
 */

static int it821x_smart_set_mode(struct ata_link *link, struct ata_device **unused)
{
	struct ata_device *dev;

	ata_link_for_each_dev(dev, link) {
		if (ata_dev_enabled(dev)) {
			/* We don't really care */
			dev->pio_mode = XFER_PIO_0;
			dev->dma_mode = XFER_MW_DMA_0;
			/* We do need the right mode information for DMA or PIO
			   and this comes from the current configuration flags */
			if (ata_id_has_dma(dev->id)) {
				ata_dev_printk(dev, KERN_INFO, "configured for DMA\n");
				dev->xfer_mode = XFER_MW_DMA_0;
				dev->xfer_shift = ATA_SHIFT_MWDMA;
				dev->flags &= ~ATA_DFLAG_PIO;
			} else {
				ata_dev_printk(dev, KERN_INFO, "configured for PIO\n");
				dev->xfer_mode = XFER_PIO_0;
				dev->xfer_shift = ATA_SHIFT_PIO;
				dev->flags |= ATA_DFLAG_PIO;
			}
		}
	}
	return 0;
}

/**
 *	it821x_dev_config	-	Called each device identify
 *	@adev: Device that has just been identified
 *
 *	Perform the initial setup needed for each device that is chip
 *	special. In our case we need to lock the sector count to avoid
 *	blowing the brains out of the firmware with large LBA48 requests
 *
 *	FIXME: When FUA appears we need to block FUA too. And SMART and
 *	basically we need to filter commands for this chip.
 */

static void it821x_dev_config(struct ata_device *adev)
{
	unsigned char model_num[ATA_ID_PROD_LEN + 1];

	ata_id_c_string(adev->id, model_num, ATA_ID_PROD, sizeof(model_num));

	if (adev->max_sectors > 255)
		adev->max_sectors = 255;

	if (strstr(model_num, "Integrated Technology Express")) {
		/* RAID mode */
		printk(KERN_INFO "IT821x %sRAID%d volume",
			adev->id[147]?"Bootable ":"",
			adev->id[129]);
		if (adev->id[129] != 1)
			printk("(%dK stripe)", adev->id[146]);
		printk(".\n");
	}
	/* This is a controller firmware triggered funny, don't
	   report the drive faulty! */
	adev->horkage &= ~ATA_HORKAGE_DIAGNOSTIC;
}

/**
 *	it821x_ident_hack	-	Hack identify data up
 *	@ap: Port
 *
 *	Walk the devices on this firmware driven port and slightly
 *	mash the identify data to stop us and common tools trying to
 *	use features not firmware supported. The firmware itself does
 *	some masking (eg SMART) but not enough.
 *
 *	This is a bit of an abuse of the cable method, but it is the
 *	only method called at the right time. We could modify the libata
 *	core specifically for ident hacking but while we have one offender
 *	it seems better to keep the fallout localised.
 */

static int it821x_ident_hack(struct ata_port *ap)
{
	struct ata_device *adev;
	ata_link_for_each_dev(adev, &ap->link) {
		if (ata_dev_enabled(adev)) {
			adev->id[84] &= ~(1 << 6);	/* No FUA */
			adev->id[85] &= ~(1 << 10);	/* No HPA */
			adev->id[76] = 0;		/* No NCQ/AN etc */
		}
	}
	return ata_cable_unknown(ap);
}


/**
 *	it821x_check_atapi_dma	-	ATAPI DMA handler
 *	@qc: Command we are about to issue
 *
 *	Decide if this ATAPI command can be issued by DMA on this
 *	controller. Return 0 if it can be.
 */

static int it821x_check_atapi_dma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct it821x_dev *itdev = ap->private_data;

	/* Only use dma for transfers to/from the media. */
	if (ata_qc_raw_nbytes(qc) < 2048)
		return -EOPNOTSUPP;

	/* No ATAPI DMA in smart mode */
	if (itdev->smart)
		return -EOPNOTSUPP;
	/* No ATAPI DMA on rev 10 */
	if (itdev->timing10)
		return -EOPNOTSUPP;
	/* Cool */
	return 0;
}


/**
 *	it821x_port_start	-	port setup
 *	@ap: ATA port being set up
 *
 *	The it821x needs to maintain private data structures and also to
 *	use the standard PCI interface which lacks support for this
 *	functionality. We instead set up the private data on the port
 *	start hook, and tear it down on port stop
 */

static int it821x_port_start(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct it821x_dev *itdev;
	u8 conf;

	int ret = ata_sff_port_start(ap);
	if (ret < 0)
		return ret;

	itdev = devm_kzalloc(&pdev->dev, sizeof(struct it821x_dev), GFP_KERNEL);
	if (itdev == NULL)
		return -ENOMEM;
	ap->private_data = itdev;

	pci_read_config_byte(pdev, 0x50, &conf);

	if (conf & 1) {
		itdev->smart = 1;
		/* Long I/O's although allowed in LBA48 space cause the
		   onboard firmware to enter the twighlight zone */
		/* No ATAPI DMA in this mode either */
	}
	/* Pull the current clocks from 0x50 */
	if (conf & (1 << (1 + ap->port_no)))
		itdev->clock_mode = ATA_50;
	else
		itdev->clock_mode = ATA_66;

	itdev->want[0][1] = ATA_ANY;
	itdev->want[1][1] = ATA_ANY;
	itdev->last_device = -1;

	if (pdev->revision == 0x10) {
		itdev->timing10 = 1;
		/* Need to disable ATAPI DMA for this case */
		if (!itdev->smart)
			printk(KERN_WARNING DRV_NAME": Revision 0x10, workarounds activated.\n");
	}

	return 0;
}

static struct scsi_host_template it821x_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations it821x_smart_port_ops = {
	.inherits	= &ata_bmdma_port_ops,

	.check_atapi_dma= it821x_check_atapi_dma,
	.qc_issue	= it821x_smart_qc_issue,

	.cable_detect	= it821x_ident_hack,
	.set_mode	= it821x_smart_set_mode,
	.dev_config	= it821x_dev_config,

	.port_start	= it821x_port_start,
};

static struct ata_port_operations it821x_passthru_port_ops = {
	.inherits	= &ata_bmdma_port_ops,

	.check_atapi_dma= it821x_check_atapi_dma,
	.sff_dev_select	= it821x_passthru_dev_select,
	.bmdma_start 	= it821x_passthru_bmdma_start,
	.bmdma_stop	= it821x_passthru_bmdma_stop,
	.qc_issue	= it821x_passthru_qc_issue,

	.cable_detect	= ata_cable_unknown,
	.set_piomode	= it821x_passthru_set_piomode,
	.set_dmamode	= it821x_passthru_set_dmamode,

	.port_start	= it821x_port_start,
};

static void it821x_disable_raid(struct pci_dev *pdev)
{
	/* Reset local CPU, and set BIOS not ready */
	pci_write_config_byte(pdev, 0x5E, 0x01);

	/* Set to bypass mode, and reset PCI bus */
	pci_write_config_byte(pdev, 0x50, 0x00);
	pci_write_config_word(pdev, PCI_COMMAND,
			      PCI_COMMAND_PARITY | PCI_COMMAND_IO |
			      PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	pci_write_config_word(pdev, 0x40, 0xA0F3);

	pci_write_config_dword(pdev,0x4C, 0x02040204);
	pci_write_config_byte(pdev, 0x42, 0x36);
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x20);
}


static int it821x_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	u8 conf;

	static const struct ata_port_info info_smart = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.port_ops = &it821x_smart_port_ops
	};
	static const struct ata_port_info info_passthru = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA6,
		.port_ops = &it821x_passthru_port_ops
	};

	const struct ata_port_info *ppi[] = { NULL, NULL };
	static char *mode[2] = { "pass through", "smart" };
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* Force the card into bypass mode if so requested */
	if (it8212_noraid) {
		printk(KERN_INFO DRV_NAME ": forcing bypass mode.\n");
		it821x_disable_raid(pdev);
	}
	pci_read_config_byte(pdev, 0x50, &conf);
	conf &= 1;

	printk(KERN_INFO DRV_NAME ": controller in %s mode.\n", mode[conf]);
	if (conf == 0)
		ppi[0] = &info_passthru;
	else
		ppi[0] = &info_smart;

	return ata_pci_sff_init_one(pdev, ppi, &it821x_sht, NULL);
}

#ifdef CONFIG_PM
static int it821x_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;
	/* Resume - turn raid back off if need be */
	if (it8212_noraid)
		it821x_disable_raid(pdev);
	ata_host_resume(host);
	return rc;
}
#endif

static const struct pci_device_id it821x[] = {
	{ PCI_VDEVICE(ITE, PCI_DEVICE_ID_ITE_8211), },
	{ PCI_VDEVICE(ITE, PCI_DEVICE_ID_ITE_8212), },

	{ },
};

static struct pci_driver it821x_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= it821x,
	.probe 		= it821x_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= it821x_reinit_one,
#endif
};

static int __init it821x_init(void)
{
	return pci_register_driver(&it821x_pci_driver);
}

static void __exit it821x_exit(void)
{
	pci_unregister_driver(&it821x_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the IT8211/IT8212 IDE RAID controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, it821x);
MODULE_VERSION(DRV_VERSION);


module_param_named(noraid, it8212_noraid, int, S_IRUGO);
MODULE_PARM_DESC(noraid, "Force card into bypass mode");

module_init(it821x_init);
module_exit(it821x_exit);
