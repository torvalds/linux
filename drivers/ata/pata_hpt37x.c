/*
 * Libata driver for the highpoint 37x and 30x UDMA66 ATA controllers.
 *
 * This driver is heavily based upon:
 *
 * linux/drivers/ide/pci/hpt366.c		Version 0.36	April 25, 2003
 *
 * Copyright (C) 1999-2003		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
 * Portions Copyright (C) 2003		Red Hat Inc
 * Portions Copyright (C) 2005-2009	MontaVista Software, Inc.
 *
 * TODO
 *	Look into engine reset on timeout errors. Should not be	required.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"pata_hpt37x"
#define DRV_VERSION	"0.6.15"

struct hpt_clock {
	u8	xfer_speed;
	u32	timing;
};

struct hpt_chip {
	const char *name;
	unsigned int base;
	struct hpt_clock const *clocks[4];
};

/* key for bus clock timings
 * bit
 * 0:3    data_high_time. inactive time of DIOW_/DIOR_ for PIO and MW
 *        DMA. cycles = value + 1
 * 4:8    data_low_time. active time of DIOW_/DIOR_ for PIO and MW
 *        DMA. cycles = value + 1
 * 9:12   cmd_high_time. inactive time of DIOW_/DIOR_ during task file
 *        register access.
 * 13:17  cmd_low_time. active time of DIOW_/DIOR_ during task file
 *        register access.
 * 18:21  udma_cycle_time. clock freq and clock cycles for UDMA xfer.
 *        during task file register access.
 * 22:24  pre_high_time. time to initialize 1st cycle for PIO and MW DMA
 *        xfer.
 * 25:27  cmd_pre_high_time. time to initialize 1st PIO cycle for task
 *        register access.
 * 28     UDMA enable
 * 29     DMA enable
 * 30     PIO_MST enable. if set, the chip is in bus master mode during
 *        PIO.
 * 31     FIFO enable.
 */

static struct hpt_clock hpt37x_timings_33[] = {
	{ XFER_UDMA_6,		0x12446231 },	/* 0x12646231 ?? */
	{ XFER_UDMA_5,		0x12446231 },
	{ XFER_UDMA_4,		0x12446231 },
	{ XFER_UDMA_3,		0x126c6231 },
	{ XFER_UDMA_2,		0x12486231 },
	{ XFER_UDMA_1,		0x124c6233 },
	{ XFER_UDMA_0,		0x12506297 },

	{ XFER_MW_DMA_2,	0x22406c31 },
	{ XFER_MW_DMA_1,	0x22406c33 },
	{ XFER_MW_DMA_0,	0x22406c97 },

	{ XFER_PIO_4,		0x06414e31 },
	{ XFER_PIO_3,		0x06414e42 },
	{ XFER_PIO_2,		0x06414e53 },
	{ XFER_PIO_1,		0x06814e93 },
	{ XFER_PIO_0,		0x06814ea7 }
};

static struct hpt_clock hpt37x_timings_50[] = {
	{ XFER_UDMA_6,		0x12848242 },
	{ XFER_UDMA_5,		0x12848242 },
	{ XFER_UDMA_4,		0x12ac8242 },
	{ XFER_UDMA_3,		0x128c8242 },
	{ XFER_UDMA_2,		0x120c8242 },
	{ XFER_UDMA_1,		0x12148254 },
	{ XFER_UDMA_0,		0x121882ea },

	{ XFER_MW_DMA_2,	0x22808242 },
	{ XFER_MW_DMA_1,	0x22808254 },
	{ XFER_MW_DMA_0,	0x228082ea },

	{ XFER_PIO_4,		0x0a81f442 },
	{ XFER_PIO_3,		0x0a81f443 },
	{ XFER_PIO_2,		0x0a81f454 },
	{ XFER_PIO_1,		0x0ac1f465 },
	{ XFER_PIO_0,		0x0ac1f48a }
};

static struct hpt_clock hpt37x_timings_66[] = {
	{ XFER_UDMA_6,		0x1c869c62 },
	{ XFER_UDMA_5,		0x1cae9c62 },	/* 0x1c8a9c62 */
	{ XFER_UDMA_4,		0x1c8a9c62 },
	{ XFER_UDMA_3,		0x1c8e9c62 },
	{ XFER_UDMA_2,		0x1c929c62 },
	{ XFER_UDMA_1,		0x1c9a9c62 },
	{ XFER_UDMA_0,		0x1c829c62 },

	{ XFER_MW_DMA_2,	0x2c829c62 },
	{ XFER_MW_DMA_1,	0x2c829c66 },
	{ XFER_MW_DMA_0,	0x2c829d2e },

	{ XFER_PIO_4,		0x0c829c62 },
	{ XFER_PIO_3,		0x0c829c84 },
	{ XFER_PIO_2,		0x0c829ca6 },
	{ XFER_PIO_1,		0x0d029d26 },
	{ XFER_PIO_0,		0x0d029d5e }
};


static const struct hpt_chip hpt370 = {
	"HPT370",
	48,
	{
		hpt37x_timings_33,
		NULL,
		NULL,
		NULL
	}
};

static const struct hpt_chip hpt370a = {
	"HPT370A",
	48,
	{
		hpt37x_timings_33,
		NULL,
		hpt37x_timings_50,
		NULL
	}
};

static const struct hpt_chip hpt372 = {
	"HPT372",
	55,
	{
		hpt37x_timings_33,
		NULL,
		hpt37x_timings_50,
		hpt37x_timings_66
	}
};

static const struct hpt_chip hpt302 = {
	"HPT302",
	66,
	{
		hpt37x_timings_33,
		NULL,
		hpt37x_timings_50,
		hpt37x_timings_66
	}
};

static const struct hpt_chip hpt371 = {
	"HPT371",
	66,
	{
		hpt37x_timings_33,
		NULL,
		hpt37x_timings_50,
		hpt37x_timings_66
	}
};

static const struct hpt_chip hpt372a = {
	"HPT372A",
	66,
	{
		hpt37x_timings_33,
		NULL,
		hpt37x_timings_50,
		hpt37x_timings_66
	}
};

static const struct hpt_chip hpt374 = {
	"HPT374",
	48,
	{
		hpt37x_timings_33,
		NULL,
		NULL,
		NULL
	}
};

/**
 *	hpt37x_find_mode	-	reset the hpt37x bus
 *	@ap: ATA port
 *	@speed: transfer mode
 *
 *	Return the 32bit register programming information for this channel
 *	that matches the speed provided.
 */

static u32 hpt37x_find_mode(struct ata_port *ap, int speed)
{
	struct hpt_clock *clocks = ap->host->private_data;

	while(clocks->xfer_speed) {
		if (clocks->xfer_speed == speed)
			return clocks->timing;
		clocks++;
	}
	BUG();
	return 0xffffffffU;	/* silence compiler warning */
}

static int hpt_dma_blacklisted(const struct ata_device *dev, char *modestr, const char *list[])
{
	unsigned char model_num[ATA_ID_PROD_LEN + 1];
	int i = 0;

	ata_id_c_string(dev->id, model_num, ATA_ID_PROD, sizeof(model_num));

	while (list[i] != NULL) {
		if (!strcmp(list[i], model_num)) {
			printk(KERN_WARNING DRV_NAME ": %s is not supported for %s.\n",
				modestr, list[i]);
			return 1;
		}
		i++;
	}
	return 0;
}

static const char *bad_ata33[] = {
	"Maxtor 92720U8", "Maxtor 92040U6", "Maxtor 91360U4", "Maxtor 91020U3", "Maxtor 90845U3", "Maxtor 90650U2",
	"Maxtor 91360D8", "Maxtor 91190D7", "Maxtor 91020D6", "Maxtor 90845D5", "Maxtor 90680D4", "Maxtor 90510D3", "Maxtor 90340D2",
	"Maxtor 91152D8", "Maxtor 91008D7", "Maxtor 90845D6", "Maxtor 90840D6", "Maxtor 90720D5", "Maxtor 90648D5", "Maxtor 90576D4",
	"Maxtor 90510D4",
	"Maxtor 90432D3", "Maxtor 90288D2", "Maxtor 90256D2",
	"Maxtor 91000D8", "Maxtor 90910D8", "Maxtor 90875D7", "Maxtor 90840D7", "Maxtor 90750D6", "Maxtor 90625D5", "Maxtor 90500D4",
	"Maxtor 91728D8", "Maxtor 91512D7", "Maxtor 91303D6", "Maxtor 91080D5", "Maxtor 90845D4", "Maxtor 90680D4", "Maxtor 90648D3", "Maxtor 90432D2",
	NULL
};

static const char *bad_ata100_5[] = {
	"IBM-DTLA-307075",
	"IBM-DTLA-307060",
	"IBM-DTLA-307045",
	"IBM-DTLA-307030",
	"IBM-DTLA-307020",
	"IBM-DTLA-307015",
	"IBM-DTLA-305040",
	"IBM-DTLA-305030",
	"IBM-DTLA-305020",
	"IC35L010AVER07-0",
	"IC35L020AVER07-0",
	"IC35L030AVER07-0",
	"IC35L040AVER07-0",
	"IC35L060AVER07-0",
	"WDC AC310200R",
	NULL
};

/**
 *	hpt370_filter	-	mode selection filter
 *	@adev: ATA device
 *
 *	Block UDMA on devices that cause trouble with this controller.
 */

static unsigned long hpt370_filter(struct ata_device *adev, unsigned long mask)
{
	if (adev->class == ATA_DEV_ATA) {
		if (hpt_dma_blacklisted(adev, "UDMA", bad_ata33))
			mask &= ~ATA_MASK_UDMA;
		if (hpt_dma_blacklisted(adev, "UDMA100", bad_ata100_5))
			mask &= ~(0xE0 << ATA_SHIFT_UDMA);
	}
	return ata_bmdma_mode_filter(adev, mask);
}

/**
 *	hpt370a_filter	-	mode selection filter
 *	@adev: ATA device
 *
 *	Block UDMA on devices that cause trouble with this controller.
 */

static unsigned long hpt370a_filter(struct ata_device *adev, unsigned long mask)
{
	if (adev->class == ATA_DEV_ATA) {
		if (hpt_dma_blacklisted(adev, "UDMA100", bad_ata100_5))
			mask &= ~(0xE0 << ATA_SHIFT_UDMA);
	}
	return ata_bmdma_mode_filter(adev, mask);
}

/**
 *	hpt37x_cable_detect	-	Detect the cable type
 *	@ap: ATA port to detect on
 *
 *	Return the cable type attached to this port
 */

static int hpt37x_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 scr2, ata66;

	pci_read_config_byte(pdev, 0x5B, &scr2);
	pci_write_config_byte(pdev, 0x5B, scr2 & ~0x01);

	udelay(10); /* debounce */

	/* Cable register now active */
	pci_read_config_byte(pdev, 0x5A, &ata66);
	/* Restore state */
	pci_write_config_byte(pdev, 0x5B, scr2);

	if (ata66 & (2 >> ap->port_no))
		return ATA_CBL_PATA40;
	else
		return ATA_CBL_PATA80;
}

/**
 *	hpt374_fn1_cable_detect	-	Detect the cable type
 *	@ap: ATA port to detect on
 *
 *	Return the cable type attached to this port
 */

static int hpt374_fn1_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned int mcrbase = 0x50 + 4 * ap->port_no;
	u16 mcr3;
	u8 ata66;

	/* Do the extra channel work */
	pci_read_config_word(pdev, mcrbase + 2, &mcr3);
	/* Set bit 15 of 0x52 to enable TCBLID as input */
	pci_write_config_word(pdev, mcrbase + 2, mcr3 | 0x8000);
	pci_read_config_byte(pdev, 0x5A, &ata66);
	/* Reset TCBLID/FCBLID to output */
	pci_write_config_word(pdev, mcrbase + 2, mcr3);

	if (ata66 & (2 >> ap->port_no))
		return ATA_CBL_PATA40;
	else
		return ATA_CBL_PATA80;
}

/**
 *	hpt37x_pre_reset	-	reset the hpt37x bus
 *	@link: ATA link to reset
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform the initial reset handling for the HPT37x.
 */

static int hpt37x_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits hpt37x_enable_bits[] = {
		{ 0x50, 1, 0x04, 0x04 },
		{ 0x54, 1, 0x04, 0x04 }
	};
	if (!pci_test_config_bits(pdev, &hpt37x_enable_bits[ap->port_no]))
		return -ENOENT;

	/* Reset the state machine */
	pci_write_config_byte(pdev, 0x50 + 4 * ap->port_no, 0x37);
	udelay(100);

	return ata_sff_prereset(link, deadline);
}

static void hpt370_set_mode(struct ata_port *ap, struct ata_device *adev,
			    u8 mode)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 addr1, addr2;
	u32 reg, timing, mask;
	u8 fast;

	addr1 = 0x40 + 4 * (adev->devno + 2 * ap->port_no);
	addr2 = 0x51 + 4 * ap->port_no;

	/* Fast interrupt prediction disable, hold off interrupt disable */
	pci_read_config_byte(pdev, addr2, &fast);
	fast &= ~0x02;
	fast |= 0x01;
	pci_write_config_byte(pdev, addr2, fast);

	/* Determine timing mask and find matching mode entry */
	if (mode < XFER_MW_DMA_0)
		mask = 0xcfc3ffff;
	else if (mode < XFER_UDMA_0)
		mask = 0x31c001ff;
	else
		mask = 0x303c0000;

	timing = hpt37x_find_mode(ap, mode);

	pci_read_config_dword(pdev, addr1, &reg);
	reg = (reg & ~mask) | (timing & mask);
	pci_write_config_dword(pdev, addr1, reg);
}
/**
 *	hpt370_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Perform PIO mode setup.
 */

static void hpt370_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	hpt370_set_mode(ap, adev, adev->pio_mode);
}

/**
 *	hpt370_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	Set up the channel for MWDMA or UDMA modes.
 */

static void hpt370_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	hpt370_set_mode(ap, adev, adev->dma_mode);
}

/**
 *	hpt370_bmdma_end		-	DMA engine stop
 *	@qc: ATA command
 *
 *	Work around the HPT370 DMA engine.
 */

static void hpt370_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	void __iomem *bmdma = ap->ioaddr.bmdma_addr;
	u8 dma_stat = ioread8(bmdma + ATA_DMA_STATUS);
	u8 dma_cmd;

	if (dma_stat & ATA_DMA_ACTIVE) {
		udelay(20);
		dma_stat = ioread8(bmdma + ATA_DMA_STATUS);
	}
	if (dma_stat & ATA_DMA_ACTIVE) {
		/* Clear the engine */
		pci_write_config_byte(pdev, 0x50 + 4 * ap->port_no, 0x37);
		udelay(10);
		/* Stop DMA */
		dma_cmd = ioread8(bmdma + ATA_DMA_CMD);
		iowrite8(dma_cmd & ~ATA_DMA_START, bmdma + ATA_DMA_CMD);
		/* Clear Error */
		dma_stat = ioread8(bmdma + ATA_DMA_STATUS);
		iowrite8(dma_stat | ATA_DMA_INTR | ATA_DMA_ERR,
			 bmdma + ATA_DMA_STATUS);
		/* Clear the engine */
		pci_write_config_byte(pdev, 0x50 + 4 * ap->port_no, 0x37);
		udelay(10);
	}
	ata_bmdma_stop(qc);
}

static void hpt372_set_mode(struct ata_port *ap, struct ata_device *adev,
			    u8 mode)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 addr1, addr2;
	u32 reg, timing, mask;
	u8 fast;

	addr1 = 0x40 + 4 * (adev->devno + 2 * ap->port_no);
	addr2 = 0x51 + 4 * ap->port_no;

	/* Fast interrupt prediction disable, hold off interrupt disable */
	pci_read_config_byte(pdev, addr2, &fast);
	fast &= ~0x07;
	pci_write_config_byte(pdev, addr2, fast);

	/* Determine timing mask and find matching mode entry */
	if (mode < XFER_MW_DMA_0)
		mask = 0xcfc3ffff;
	else if (mode < XFER_UDMA_0)
		mask = 0x31c001ff;
	else
		mask = 0x303c0000;

	timing = hpt37x_find_mode(ap, mode);

	pci_read_config_dword(pdev, addr1, &reg);
	reg = (reg & ~mask) | (timing & mask);
	pci_write_config_dword(pdev, addr1, reg);
}

/**
 *	hpt372_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Perform PIO mode setup.
 */

static void hpt372_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	hpt372_set_mode(ap, adev, adev->pio_mode);
}

/**
 *	hpt372_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	Set up the channel for MWDMA or UDMA modes.
 */

static void hpt372_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	hpt372_set_mode(ap, adev, adev->dma_mode);
}

/**
 *	hpt37x_bmdma_end		-	DMA engine stop
 *	@qc: ATA command
 *
 *	Clean up after the HPT372 and later DMA engine
 */

static void hpt37x_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int mscreg = 0x50 + 4 * ap->port_no;
	u8 bwsr_stat, msc_stat;

	pci_read_config_byte(pdev, 0x6A, &bwsr_stat);
	pci_read_config_byte(pdev, mscreg, &msc_stat);
	if (bwsr_stat & (1 << ap->port_no))
		pci_write_config_byte(pdev, mscreg, msc_stat | 0x30);
	ata_bmdma_stop(qc);
}


static struct scsi_host_template hpt37x_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

/*
 *	Configuration for HPT370
 */

static struct ata_port_operations hpt370_port_ops = {
	.inherits	= &ata_bmdma_port_ops,

	.bmdma_stop	= hpt370_bmdma_stop,

	.mode_filter	= hpt370_filter,
	.cable_detect	= hpt37x_cable_detect,
	.set_piomode	= hpt370_set_piomode,
	.set_dmamode	= hpt370_set_dmamode,
	.prereset	= hpt37x_pre_reset,
};

/*
 *	Configuration for HPT370A. Close to 370 but less filters
 */

static struct ata_port_operations hpt370a_port_ops = {
	.inherits	= &hpt370_port_ops,
	.mode_filter	= hpt370a_filter,
};

/*
 *	Configuration for HPT372, HPT371, HPT302. Slightly different PIO
 *	and DMA mode setting functionality.
 */

static struct ata_port_operations hpt372_port_ops = {
	.inherits	= &ata_bmdma_port_ops,

	.bmdma_stop	= hpt37x_bmdma_stop,

	.cable_detect	= hpt37x_cable_detect,
	.set_piomode	= hpt372_set_piomode,
	.set_dmamode	= hpt372_set_dmamode,
	.prereset	= hpt37x_pre_reset,
};

/*
 *	Configuration for HPT374. Mode setting works like 372 and friends
 *	but we have a different cable detection procedure for function 1.
 */

static struct ata_port_operations hpt374_fn1_port_ops = {
	.inherits	= &hpt372_port_ops,
	.cable_detect	= hpt374_fn1_cable_detect,
	.prereset	= hpt37x_pre_reset,
};

/**
 *	hpt37x_clock_slot	-	Turn timing to PC clock entry
 *	@freq: Reported frequency timing
 *	@base: Base timing
 *
 *	Turn the timing data intoa clock slot (0 for 33, 1 for 40, 2 for 50
 *	and 3 for 66Mhz)
 */

static int hpt37x_clock_slot(unsigned int freq, unsigned int base)
{
	unsigned int f = (base * freq) / 192;	/* Mhz */
	if (f < 40)
		return 0;	/* 33Mhz slot */
	if (f < 45)
		return 1;	/* 40Mhz slot */
	if (f < 55)
		return 2;	/* 50Mhz slot */
	return 3;		/* 60Mhz slot */
}

/**
 *	hpt37x_calibrate_dpll		-	Calibrate the DPLL loop
 *	@dev: PCI device
 *
 *	Perform a calibration cycle on the HPT37x DPLL. Returns 1 if this
 *	succeeds
 */

static int hpt37x_calibrate_dpll(struct pci_dev *dev)
{
	u8 reg5b;
	u32 reg5c;
	int tries;

	for(tries = 0; tries < 0x5000; tries++) {
		udelay(50);
		pci_read_config_byte(dev, 0x5b, &reg5b);
		if (reg5b & 0x80) {
			/* See if it stays set */
			for(tries = 0; tries < 0x1000; tries ++) {
				pci_read_config_byte(dev, 0x5b, &reg5b);
				/* Failed ? */
				if ((reg5b & 0x80) == 0)
					return 0;
			}
			/* Turn off tuning, we have the DPLL set */
			pci_read_config_dword(dev, 0x5c, &reg5c);
			pci_write_config_dword(dev, 0x5c, reg5c & ~ 0x100);
			return 1;
		}
	}
	/* Never went stable */
	return 0;
}

static u32 hpt374_read_freq(struct pci_dev *pdev)
{
	u32 freq;
	unsigned long io_base = pci_resource_start(pdev, 4);
	if (PCI_FUNC(pdev->devfn) & 1) {
		struct pci_dev *pdev_0;

		pdev_0 = pci_get_slot(pdev->bus, pdev->devfn - 1);
		/* Someone hot plugged the controller on us ? */
		if (pdev_0 == NULL)
			return 0;
		io_base = pci_resource_start(pdev_0, 4);
		freq = inl(io_base + 0x90);
		pci_dev_put(pdev_0);
	} else
		freq = inl(io_base + 0x90);
	return freq;
}

/**
 *	hpt37x_init_one		-	Initialise an HPT37X/302
 *	@dev: PCI device
 *	@id: Entry in match table
 *
 *	Initialise an HPT37x device. There are some interesting complications
 *	here. Firstly the chip may report 366 and be one of several variants.
 *	Secondly all the timings depend on the clock for the chip which we must
 *	detect and look up
 *
 *	This is the known chip mappings. It may be missing a couple of later
 *	releases.
 *
 *	Chip version		PCI		Rev	Notes
 *	HPT366			4 (HPT366)	0	Other driver
 *	HPT366			4 (HPT366)	1	Other driver
 *	HPT368			4 (HPT366)	2	Other driver
 *	HPT370			4 (HPT366)	3	UDMA100
 *	HPT370A			4 (HPT366)	4	UDMA100
 *	HPT372			4 (HPT366)	5	UDMA133 (1)
 *	HPT372N			4 (HPT366)	6	Other driver
 *	HPT372A			5 (HPT372)	1	UDMA133 (1)
 *	HPT372N			5 (HPT372)	2	Other driver
 *	HPT302			6 (HPT302)	1	UDMA133
 *	HPT302N			6 (HPT302)	2	Other driver
 *	HPT371			7 (HPT371)	*	UDMA133
 *	HPT374			8 (HPT374)	*	UDMA133 4 channel
 *	HPT372N			9 (HPT372N)	*	Other driver
 *
 *	(1) UDMA133 support depends on the bus clock
 */

static int hpt37x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	/* HPT370 - UDMA100 */
	static const struct ata_port_info info_hpt370 = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA5,
		.port_ops = &hpt370_port_ops
	};
	/* HPT370A - UDMA100 */
	static const struct ata_port_info info_hpt370a = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA5,
		.port_ops = &hpt370a_port_ops
	};
	/* HPT370 - UDMA100 */
	static const struct ata_port_info info_hpt370_33 = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA5,
		.port_ops = &hpt370_port_ops
	};
	/* HPT370A - UDMA100 */
	static const struct ata_port_info info_hpt370a_33 = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA5,
		.port_ops = &hpt370a_port_ops
	};
	/* HPT371, 372 and friends - UDMA133 */
	static const struct ata_port_info info_hpt372 = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA6,
		.port_ops = &hpt372_port_ops
	};
	/* HPT374 - UDMA100, function 1 uses different prereset method */
	static const struct ata_port_info info_hpt374_fn0 = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA5,
		.port_ops = &hpt372_port_ops
	};
	static const struct ata_port_info info_hpt374_fn1 = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA5,
		.port_ops = &hpt374_fn1_port_ops
	};

	static const int MHz[4] = { 33, 40, 50, 66 };
	void *private_data = NULL;
	const struct ata_port_info *ppi[] = { NULL, NULL };
	u8 rev = dev->revision;
	u8 irqmask;
	u8 mcr1;
	u32 freq;
	int prefer_dpll = 1;

	unsigned long iobase = pci_resource_start(dev, 4);

	const struct hpt_chip *chip_table;
	int clock_slot;
	int rc;

	rc = pcim_enable_device(dev);
	if (rc)
		return rc;

	if (dev->device == PCI_DEVICE_ID_TTI_HPT366) {
		/* May be a later chip in disguise. Check */
		/* Older chips are in the HPT366 driver. Ignore them */
		if (rev < 3)
			return -ENODEV;
		/* N series chips have their own driver. Ignore */
		if (rev == 6)
			return -ENODEV;

		switch(rev) {
			case 3:
				ppi[0] = &info_hpt370;
				chip_table = &hpt370;
				prefer_dpll = 0;
				break;
			case 4:
				ppi[0] = &info_hpt370a;
				chip_table = &hpt370a;
				prefer_dpll = 0;
				break;
			case 5:
				ppi[0] = &info_hpt372;
				chip_table = &hpt372;
				break;
			default:
				printk(KERN_ERR "pata_hpt37x: Unknown HPT366 "
				       "subtype, please report (%d).\n", rev);
				return -ENODEV;
		}
	} else {
		switch(dev->device) {
			case PCI_DEVICE_ID_TTI_HPT372:
				/* 372N if rev >= 2*/
				if (rev >= 2)
					return -ENODEV;
				ppi[0] = &info_hpt372;
				chip_table = &hpt372a;
				break;
			case PCI_DEVICE_ID_TTI_HPT302:
				/* 302N if rev > 1 */
				if (rev > 1)
					return -ENODEV;
				ppi[0] = &info_hpt372;
				/* Check this */
				chip_table = &hpt302;
				break;
			case PCI_DEVICE_ID_TTI_HPT371:
				if (rev > 1)
					return -ENODEV;
				ppi[0] = &info_hpt372;
				chip_table = &hpt371;
				/* Single channel device, master is not present
				   but the BIOS (or us for non x86) must mark it
				   absent */
				pci_read_config_byte(dev, 0x50, &mcr1);
				mcr1 &= ~0x04;
				pci_write_config_byte(dev, 0x50, mcr1);
				break;
			case PCI_DEVICE_ID_TTI_HPT374:
				chip_table = &hpt374;
				if (!(PCI_FUNC(dev->devfn) & 1))
					*ppi = &info_hpt374_fn0;
				else
					*ppi = &info_hpt374_fn1;
				break;
			default:
				printk(KERN_ERR "pata_hpt37x: PCI table is bogus please report (%d).\n", dev->device);
				return -ENODEV;
		}
	}
	/* Ok so this is a chip we support */

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES / 4));
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);
	pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);
	pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

	pci_read_config_byte(dev, 0x5A, &irqmask);
	irqmask &= ~0x10;
	pci_write_config_byte(dev, 0x5a, irqmask);

	/*
	 * default to pci clock. make sure MA15/16 are set to output
	 * to prevent drives having problems with 40-pin cables. Needed
	 * for some drives such as IBM-DTLA which will not enter ready
	 * state on reset when PDIAG is a input.
	 */

	pci_write_config_byte(dev, 0x5b, 0x23);

	/*
	 * HighPoint does this for HPT372A.
	 * NOTE: This register is only writeable via I/O space.
	 */
	if (chip_table == &hpt372a)
		outb(0x0e, iobase + 0x9c);

	/* Some devices do not let this value be accessed via PCI space
	   according to the old driver. In addition we must use the value
	   from FN 0 on the HPT374 */

	if (chip_table == &hpt374) {
		freq = hpt374_read_freq(dev);
		if (freq == 0)
			return -ENODEV;
	} else
		freq = inl(iobase + 0x90);

	if ((freq >> 12) != 0xABCDE) {
		int i;
		u8 sr;
		u32 total = 0;

		printk(KERN_WARNING "pata_hpt37x: BIOS has not set timing clocks.\n");

		/* This is the process the HPT371 BIOS is reported to use */
		for(i = 0; i < 128; i++) {
			pci_read_config_byte(dev, 0x78, &sr);
			total += sr & 0x1FF;
			udelay(15);
		}
		freq = total / 128;
	}
	freq &= 0x1FF;

	/*
	 *	Turn the frequency check into a band and then find a timing
	 *	table to match it.
	 */

	clock_slot = hpt37x_clock_slot(freq, chip_table->base);
	if (chip_table->clocks[clock_slot] == NULL || prefer_dpll) {
		/*
		 *	We need to try PLL mode instead
		 *
		 *	For non UDMA133 capable devices we should
		 *	use a 50MHz DPLL by choice
		 */
		unsigned int f_low, f_high;
		int dpll, adjust;

		/* Compute DPLL */
		dpll = (ppi[0]->udma_mask & 0xC0) ? 3 : 2;

		f_low = (MHz[clock_slot] * 48) / MHz[dpll];
		f_high = f_low + 2;
		if (clock_slot > 1)
			f_high += 2;

		/* Select the DPLL clock. */
		pci_write_config_byte(dev, 0x5b, 0x21);
		pci_write_config_dword(dev, 0x5C, (f_high << 16) | f_low | 0x100);

		for(adjust = 0; adjust < 8; adjust++) {
			if (hpt37x_calibrate_dpll(dev))
				break;
			/* See if it'll settle at a fractionally different clock */
			if (adjust & 1)
				f_low -= adjust >> 1;
			else
				f_high += adjust >> 1;
			pci_write_config_dword(dev, 0x5C, (f_high << 16) | f_low | 0x100);
		}
		if (adjust == 8) {
			printk(KERN_ERR "pata_hpt37x: DPLL did not stabilize!\n");
			return -ENODEV;
		}
		if (dpll == 3)
			private_data = (void *)hpt37x_timings_66;
		else
			private_data = (void *)hpt37x_timings_50;

		printk(KERN_INFO "pata_hpt37x: bus clock %dMHz, using %dMHz DPLL.\n",
		       MHz[clock_slot], MHz[dpll]);
	} else {
		private_data = (void *)chip_table->clocks[clock_slot];
		/*
		 *	Perform a final fixup. Note that we will have used the
		 *	DPLL on the HPT372 which means we don't have to worry
		 *	about lack of UDMA133 support on lower clocks
 		 */

		if (clock_slot < 2 && ppi[0] == &info_hpt370)
			ppi[0] = &info_hpt370_33;
		if (clock_slot < 2 && ppi[0] == &info_hpt370a)
			ppi[0] = &info_hpt370a_33;
		printk(KERN_INFO "pata_hpt37x: %s using %dMHz bus clock.\n",
		       chip_table->name, MHz[clock_slot]);
	}

	/* Now kick off ATA set up */
	return ata_pci_sff_init_one(dev, ppi, &hpt37x_sht, private_data);
}

static const struct pci_device_id hpt37x[] = {
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT366), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT371), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT372), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT374), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT302), },

	{ },
};

static struct pci_driver hpt37x_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= hpt37x,
	.probe 		= hpt37x_init_one,
	.remove		= ata_pci_remove_one
};

static int __init hpt37x_init(void)
{
	return pci_register_driver(&hpt37x_pci_driver);
}

static void __exit hpt37x_exit(void)
{
	pci_unregister_driver(&hpt37x_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the Highpoint HPT37x/30x");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, hpt37x);
MODULE_VERSION(DRV_VERSION);

module_init(hpt37x_init);
module_exit(hpt37x_exit);
