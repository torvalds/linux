/*
 * pata_atiixp.c 	- ATI PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 * Based on
 *
 *  linux/drivers/ide/pci/atiixp.c	Version 0.01-bart2	Feb. 26, 2004
 *
 *  Copyright (C) 2003 ATI Inc. <hyu@ati.com>
 *  Copyright (C) 2004 Bartlomiej Zolnierkiewicz
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_atiixp"
#define DRV_VERSION "0.4.6"

enum {
	ATIIXP_IDE_PIO_TIMING	= 0x40,
	ATIIXP_IDE_MWDMA_TIMING	= 0x44,
	ATIIXP_IDE_PIO_CONTROL	= 0x48,
	ATIIXP_IDE_PIO_MODE	= 0x4a,
	ATIIXP_IDE_UDMA_CONTROL	= 0x54,
	ATIIXP_IDE_UDMA_MODE 	= 0x56
};

static int atiixp_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	static const struct pci_bits atiixp_enable_bits[] = {
		{ 0x48, 1, 0x01, 0x00 },
		{ 0x48, 1, 0x08, 0x00 }
	};
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (!pci_test_config_bits(pdev, &atiixp_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_std_prereset(link, deadline);
}

static void atiixp_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, atiixp_pre_reset, ata_std_softreset, NULL,   ata_std_postreset);
}

static int atiixp_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 udma;

	/* Hack from drivers/ide/pci. Really we want to know how to do the
	   raw detection not play follow the bios mode guess */
	pci_read_config_byte(pdev, ATIIXP_IDE_UDMA_MODE + ap->port_no, &udma);
	if ((udma & 0x07) >= 0x04 || (udma & 0x70) >= 0x40)
		return  ATA_CBL_PATA80;
	return ATA_CBL_PATA40;
}

/**
 *	atiixp_set_pio_timing	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called by both the pio and dma setup functions to set the controller
 *	timings for PIO transfers. We must load both the mode number and
 *	timing values into the controller.
 */

static void atiixp_set_pio_timing(struct ata_port *ap, struct ata_device *adev, int pio)
{
	static u8 pio_timings[5] = { 0x5D, 0x47, 0x34, 0x22, 0x20 };

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int dn = 2 * ap->port_no + adev->devno;

	/* Check this is correct - the order is odd in both drivers */
	int timing_shift = (16 * ap->port_no) + 8 * (adev->devno ^ 1);
	u16 pio_mode_data, pio_timing_data;

	pci_read_config_word(pdev, ATIIXP_IDE_PIO_MODE, &pio_mode_data);
	pio_mode_data &= ~(0x7 << (4 * dn));
	pio_mode_data |= pio << (4 * dn);
	pci_write_config_word(pdev, ATIIXP_IDE_PIO_MODE, pio_mode_data);

	pci_read_config_word(pdev, ATIIXP_IDE_PIO_TIMING, &pio_timing_data);
	pio_mode_data &= ~(0xFF << timing_shift);
	pio_mode_data |= (pio_timings[pio] << timing_shift);
	pci_write_config_word(pdev, ATIIXP_IDE_PIO_TIMING, pio_timing_data);
}

/**
 *	atiixp_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup. We use a shared helper for this
 *	as the DMA setup must also adjust the PIO timing information.
 */

static void atiixp_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	atiixp_set_pio_timing(ap, adev, adev->pio_mode - XFER_PIO_0);
}

/**
 *	atiixp_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the DMA mode setup. We use timing tables for most
 *	modes but must tune an appropriate PIO mode to match.
 */

static void atiixp_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static u8 mwdma_timings[5] = { 0x77, 0x21, 0x20 };

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int dma = adev->dma_mode;
	int dn = 2 * ap->port_no + adev->devno;
	int wanted_pio;

	if (adev->dma_mode >= XFER_UDMA_0) {
		u16 udma_mode_data;

		dma -= XFER_UDMA_0;

		pci_read_config_word(pdev, ATIIXP_IDE_UDMA_MODE, &udma_mode_data);
		udma_mode_data &= ~(0x7 << (4 * dn));
		udma_mode_data |= dma << (4 * dn);
		pci_write_config_word(pdev, ATIIXP_IDE_UDMA_MODE, udma_mode_data);
	} else {
		u16 mwdma_timing_data;
		/* Check this is correct - the order is odd in both drivers */
		int timing_shift = (16 * ap->port_no) + 8 * (adev->devno ^ 1);

		dma -= XFER_MW_DMA_0;

		pci_read_config_word(pdev, ATIIXP_IDE_MWDMA_TIMING, &mwdma_timing_data);
		mwdma_timing_data &= ~(0xFF << timing_shift);
		mwdma_timing_data |= (mwdma_timings[dma] << timing_shift);
		pci_write_config_word(pdev, ATIIXP_IDE_MWDMA_TIMING, mwdma_timing_data);
	}
	/*
	 *	We must now look at the PIO mode situation. We may need to
	 *	adjust the PIO mode to keep the timings acceptable
	 */
	 if (adev->dma_mode >= XFER_MW_DMA_2)
	 	wanted_pio = 4;
	else if (adev->dma_mode == XFER_MW_DMA_1)
		wanted_pio = 3;
	else if (adev->dma_mode == XFER_MW_DMA_0)
		wanted_pio = 0;
	else BUG();

	if (adev->pio_mode != wanted_pio)
		atiixp_set_pio_timing(ap, adev, wanted_pio);
}

/**
 *	atiixp_bmdma_start	-	DMA start callback
 *	@qc: Command in progress
 *
 *	When DMA begins we need to ensure that the UDMA control
 *	register for the channel is correctly set.
 *
 *	Note: The host lock held by the libata layer protects
 *	us from two channels both trying to set DMA bits at once
 */

static void atiixp_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int dn = (2 * ap->port_no) + adev->devno;
	u16 tmp16;

	pci_read_config_word(pdev, ATIIXP_IDE_UDMA_CONTROL, &tmp16);
	if (adev->dma_mode >= XFER_UDMA_0)
		tmp16 |= (1 << dn);
	else
		tmp16 &= ~(1 << dn);
	pci_write_config_word(pdev, ATIIXP_IDE_UDMA_CONTROL, tmp16);
	ata_bmdma_start(qc);
}

/**
 *	atiixp_dma_stop	-	DMA stop callback
 *	@qc: Command in progress
 *
 *	DMA has completed. Clear the UDMA flag as the next operations will
 *	be PIO ones not UDMA data transfer.
 *
 *	Note: The host lock held by the libata layer protects
 *	us from two channels both trying to set DMA bits at once
 */

static void atiixp_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int dn = (2 * ap->port_no) + qc->dev->devno;
	u16 tmp16;

	pci_read_config_word(pdev, ATIIXP_IDE_UDMA_CONTROL, &tmp16);
	tmp16 &= ~(1 << dn);
	pci_write_config_word(pdev, ATIIXP_IDE_UDMA_CONTROL, tmp16);
	ata_bmdma_stop(qc);
}

static struct scsi_host_template atiixp_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations atiixp_port_ops = {
	.set_piomode	= atiixp_set_piomode,
	.set_dmamode	= atiixp_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= atiixp_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= atiixp_cable_detect,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= atiixp_bmdma_start,
	.bmdma_stop	= atiixp_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,

	.port_start	= ata_sff_port_start,
};

static int atiixp_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.sht = &atiixp_sht,
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x06,	/* No MWDMA0 support */
		.udma_mask = 0x3F,
		.port_ops = &atiixp_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };
	return ata_pci_init_one(dev, ppi);
}

static const struct pci_device_id atiixp[] = {
	{ PCI_VDEVICE(ATI, PCI_DEVICE_ID_ATI_IXP200_IDE), },
	{ PCI_VDEVICE(ATI, PCI_DEVICE_ID_ATI_IXP300_IDE), },
	{ PCI_VDEVICE(ATI, PCI_DEVICE_ID_ATI_IXP400_IDE), },
	{ PCI_VDEVICE(ATI, PCI_DEVICE_ID_ATI_IXP600_IDE), },
	{ PCI_VDEVICE(ATI, PCI_DEVICE_ID_ATI_IXP700_IDE), },

	{ },
};

static struct pci_driver atiixp_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= atiixp,
	.probe 		= atiixp_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.resume		= ata_pci_device_resume,
	.suspend	= ata_pci_device_suspend,
#endif
};

static int __init atiixp_init(void)
{
	return pci_register_driver(&atiixp_pci_driver);
}


static void __exit atiixp_exit(void)
{
	pci_unregister_driver(&atiixp_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for ATI IXP200/300/400");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, atiixp);
MODULE_VERSION(DRV_VERSION);

module_init(atiixp_init);
module_exit(atiixp_exit);
