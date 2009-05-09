/*
 * pata_cmd640.c 	- CMD640 PCI PATA for new ATA layer
 *			  (C) 2007 Red Hat Inc
 *
 * Based upon
 *  linux/drivers/ide/pci/cmd640.c		Version 1.02  Sep 01, 1996
 *
 *  Copyright (C) 1995-1996  Linus Torvalds & authors (see driver)
 *
 *	This drives only the PCI version of the controller. If you have a
 *	VLB one then we have enough docs to support it but you can write
 *	your own code.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_cmd640"
#define DRV_VERSION "0.0.5"

struct cmd640_reg {
	int last;
	u8 reg58[ATA_MAX_DEVICES];
};

enum {
	CFR = 0x50,
	CNTRL = 0x51,
	CMDTIM = 0x52,
	ARTIM0 = 0x53,
	DRWTIM0 = 0x54,
	ARTIM23 = 0x57,
	DRWTIM23 = 0x58,
	BRST = 0x59
};

/**
 *	cmd640_set_piomode	-	set initial PIO mode data
 *	@ap: ATA port
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup.
 */

static void cmd640_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct cmd640_reg *timing = ap->private_data;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct ata_timing t;
	const unsigned long T = 1000000 / 33;
	const u8 setup_data[] = { 0x40, 0x40, 0x40, 0x80, 0x00 };
	u8 reg;
	int arttim = ARTIM0 + 2 * adev->devno;
	struct ata_device *pair = ata_dev_pair(adev);

	if (ata_timing_compute(adev, adev->pio_mode, &t, T, 0) < 0) {
		printk(KERN_ERR DRV_NAME ": mode computation failed.\n");
		return;
	}

	/* The second channel has shared timings and the setup timing is
	   messy to switch to merge it for worst case */
	if (ap->port_no && pair) {
		struct ata_timing p;
		ata_timing_compute(pair, pair->pio_mode, &p, T, 1);
		ata_timing_merge(&p, &t, &t, ATA_TIMING_SETUP);
	}

	/* Make the timings fit */
	if (t.recover > 16) {
		t.active += t.recover - 16;
		t.recover = 16;
	}
	if (t.active > 16)
		t.active = 16;

	/* Now convert the clocks into values we can actually stuff into
	   the chip */

	if (t.recover > 1)
		t.recover--;	/* 640B only */
	else
		t.recover = 15;

	if (t.setup > 4)
		t.setup = 0xC0;
	else
		t.setup = setup_data[t.setup];

	if (ap->port_no == 0) {
		t.active &= 0x0F;	/* 0 = 16 */

		/* Load setup timing */
		pci_read_config_byte(pdev, arttim, &reg);
		reg &= 0x3F;
		reg |= t.setup;
		pci_write_config_byte(pdev, arttim, reg);

		/* Load active/recovery */
		pci_write_config_byte(pdev, arttim + 1, (t.active << 4) | t.recover);
	} else {
		/* Save the shared timings for channel, they will be loaded
		   by qc_issue. Reloading the setup time is expensive so we
		   keep a merged one loaded */
		pci_read_config_byte(pdev, ARTIM23, &reg);
		reg &= 0x3F;
		reg |= t.setup;
		pci_write_config_byte(pdev, ARTIM23, reg);
		timing->reg58[adev->devno] = (t.active << 4) | t.recover;
	}
}


/**
 *	cmd640_qc_issue	-	command preparation hook
 *	@qc: Command to be issued
 *
 *	Channel 1 has shared timings. We must reprogram the
 *	clock each drive 2/3 switch we do.
 */

static unsigned int cmd640_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct cmd640_reg *timing = ap->private_data;

	if (ap->port_no != 0 && adev->devno != timing->last) {
		pci_write_config_byte(pdev, DRWTIM23, timing->reg58[adev->devno]);
		timing->last = adev->devno;
	}
	return ata_sff_qc_issue(qc);
}

/**
 *	cmd640_port_start	-	port setup
 *	@ap: ATA port being set up
 *
 *	The CMD640 needs to maintain private data structures so we
 *	allocate space here.
 */

static int cmd640_port_start(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct cmd640_reg *timing;

	int ret = ata_sff_port_start(ap);
	if (ret < 0)
		return ret;

	timing = devm_kzalloc(&pdev->dev, sizeof(struct cmd640_reg), GFP_KERNEL);
	if (timing == NULL)
		return -ENOMEM;
	timing->last = -1;	/* Force a load */
	ap->private_data = timing;
	return ret;
}

static struct scsi_host_template cmd640_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations cmd640_port_ops = {
	.inherits	= &ata_bmdma_port_ops,
	/* In theory xfer_noirq is not needed once we kill the prefetcher */
	.sff_data_xfer	= ata_sff_data_xfer_noirq,
	.qc_issue	= cmd640_qc_issue,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= cmd640_set_piomode,
	.port_start	= cmd640_port_start,
};

static void cmd640_hardware_init(struct pci_dev *pdev)
{
	u8 r;
	u8 ctrl;

	/* CMD640 detected, commiserations */
	pci_write_config_byte(pdev, 0x5B, 0x00);
	/* Get version info */
	pci_read_config_byte(pdev, CFR, &r);
	/* PIO0 command cycles */
	pci_write_config_byte(pdev, CMDTIM, 0);
	/* 512 byte bursts (sector) */
	pci_write_config_byte(pdev, BRST, 0x40);
	/*
	 * A reporter a long time ago
	 * Had problems with the data fifo
	 * So don't run the risk
	 * Of putting crap on the disk
	 * For its better just to go slow
	 */
	/* Do channel 0 */
	pci_read_config_byte(pdev, CNTRL, &ctrl);
	pci_write_config_byte(pdev, CNTRL, ctrl | 0xC0);
	/* Ditto for channel 1 */
	pci_read_config_byte(pdev, ARTIM23, &ctrl);
	ctrl |= 0x0C;
	pci_write_config_byte(pdev, ARTIM23, ctrl);
}

static int cmd640_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.port_ops = &cmd640_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	cmd640_hardware_init(pdev);

	return ata_pci_sff_init_one(pdev, ppi, &cmd640_sht, NULL);
}

#ifdef CONFIG_PM
static int cmd640_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;
	cmd640_hardware_init(pdev);
	ata_host_resume(host);
	return 0;
}
#endif

static const struct pci_device_id cmd640[] = {
	{ PCI_VDEVICE(CMD, 0x640), 0 },
	{ },
};

static struct pci_driver cmd640_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= cmd640,
	.probe 		= cmd640_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= cmd640_reinit_one,
#endif
};

static int __init cmd640_init(void)
{
	return pci_register_driver(&cmd640_pci_driver);
}

static void __exit cmd640_exit(void)
{
	pci_unregister_driver(&cmd640_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for CMD640 PATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, cmd640);
MODULE_VERSION(DRV_VERSION);

module_init(cmd640_init);
module_exit(cmd640_exit);
