/*
 * pata_pdc202xx_old.c 	- Promise PDC202xx PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@lxorguk.ukuu.org.uk>
 *			  (C) 2007,2009,2010 Bartlomiej Zolnierkiewicz
 *
 * Based in part on linux/drivers/ide/pci/pdc202xx_old.c
 *
 * First cut with LBA48/ATAPI
 *
 * TODO:
 *	Channel interlock/reset on both required ?
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_pdc202xx_old"
#define DRV_VERSION "0.4.3"

static int pdc2026x_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u16 cis;

	pci_read_config_word(pdev, 0x50, &cis);
	if (cis & (1 << (10 + ap->port_no)))
		return ATA_CBL_PATA40;
	return ATA_CBL_PATA80;
}

static void pdc202xx_exec_command(struct ata_port *ap,
				  const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->print_id, tf->command);

	iowrite8(tf->command, ap->ioaddr.command_addr);
	ndelay(400);
}

/**
 *	pdc202xx_configure_piomode	-	set chip PIO timing
 *	@ap: ATA interface
 *	@adev: ATA device
 *	@pio: PIO mode
 *
 *	Called to do the PIO mode setup. Our timing registers are shared
 *	so a configure_dmamode call will undo any work we do here and vice
 *	versa
 */

static void pdc202xx_configure_piomode(struct ata_port *ap, struct ata_device *adev, int pio)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int port = 0x60 + 8 * ap->port_no + 4 * adev->devno;
	static u16 pio_timing[5] = {
		0x0913, 0x050C , 0x0308, 0x0206, 0x0104
	};
	u8 r_ap, r_bp;

	pci_read_config_byte(pdev, port, &r_ap);
	pci_read_config_byte(pdev, port + 1, &r_bp);
	r_ap &= ~0x3F;	/* Preserve ERRDY_EN, SYNC_IN */
	r_bp &= ~0x1F;
	r_ap |= (pio_timing[pio] >> 8);
	r_bp |= (pio_timing[pio] & 0xFF);

	if (ata_pio_need_iordy(adev))
		r_ap |= 0x20;	/* IORDY enable */
	if (adev->class == ATA_DEV_ATA)
		r_ap |= 0x10;	/* FIFO enable */
	pci_write_config_byte(pdev, port, r_ap);
	pci_write_config_byte(pdev, port + 1, r_bp);
}

/**
 *	pdc202xx_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup. Our timing registers are shared
 *	but we want to set the PIO timing by default.
 */

static void pdc202xx_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	pdc202xx_configure_piomode(ap, adev, adev->pio_mode - XFER_PIO_0);
}

/**
 *	pdc202xx_configure_dmamode	-	set DMA mode in chip
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Load DMA cycle times into the chip ready for a DMA transfer
 *	to occur.
 */

static void pdc202xx_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int port = 0x60 + 8 * ap->port_no + 4 * adev->devno;
	static u8 udma_timing[6][2] = {
		{ 0x60, 0x03 },	/* 33 Mhz Clock */
		{ 0x40, 0x02 },
		{ 0x20, 0x01 },
		{ 0x40, 0x02 },	/* 66 Mhz Clock */
		{ 0x20, 0x01 },
		{ 0x20, 0x01 }
	};
	static u8 mdma_timing[3][2] = {
		{ 0xe0, 0x0f },
		{ 0x60, 0x04 },
		{ 0x60, 0x03 },
	};
	u8 r_bp, r_cp;

	pci_read_config_byte(pdev, port + 1, &r_bp);
	pci_read_config_byte(pdev, port + 2, &r_cp);

	r_bp &= ~0xE0;
	r_cp &= ~0x0F;

	if (adev->dma_mode >= XFER_UDMA_0) {
		int speed = adev->dma_mode - XFER_UDMA_0;
		r_bp |= udma_timing[speed][0];
		r_cp |= udma_timing[speed][1];

	} else {
		int speed = adev->dma_mode - XFER_MW_DMA_0;
		r_bp |= mdma_timing[speed][0];
		r_cp |= mdma_timing[speed][1];
	}
	pci_write_config_byte(pdev, port + 1, r_bp);
	pci_write_config_byte(pdev, port + 2, r_cp);

}

/**
 *	pdc2026x_bmdma_start		-	DMA engine begin
 *	@qc: ATA command
 *
 *	In UDMA3 or higher we have to clock switch for the duration of the
 *	DMA transfer sequence.
 *
 *	Note: The host lock held by the libata layer protects
 *	us from two channels both trying to set DMA bits at once
 */

static void pdc2026x_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct ata_taskfile *tf = &qc->tf;
	int sel66 = ap->port_no ? 0x08: 0x02;

	void __iomem *master = ap->host->ports[0]->ioaddr.bmdma_addr;
	void __iomem *clock = master + 0x11;
	void __iomem *atapi_reg = master + 0x20 + (4 * ap->port_no);

	u32 len;

	/* Check we keep host level locking here */
	if (adev->dma_mode > XFER_UDMA_2)
		iowrite8(ioread8(clock) | sel66, clock);
	else
		iowrite8(ioread8(clock) & ~sel66, clock);

	/* The DMA clocks may have been trashed by a reset. FIXME: make conditional
	   and move to qc_issue ? */
	pdc202xx_set_dmamode(ap, qc->dev);

	/* Cases the state machine will not complete correctly without help */
	if ((tf->flags & ATA_TFLAG_LBA48) ||  tf->protocol == ATAPI_PROT_DMA) {
		len = qc->nbytes / 2;

		if (tf->flags & ATA_TFLAG_WRITE)
			len |= 0x06000000;
		else
			len |= 0x05000000;

		iowrite32(len, atapi_reg);
	}

	/* Activate DMA */
	ata_bmdma_start(qc);
}

/**
 *	pdc2026x_bmdma_end		-	DMA engine stop
 *	@qc: ATA command
 *
 *	After a DMA completes we need to put the clock back to 33MHz for
 *	PIO timings.
 *
 *	Note: The host lock held by the libata layer protects
 *	us from two channels both trying to set DMA bits at once
 */

static void pdc2026x_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct ata_taskfile *tf = &qc->tf;

	int sel66 = ap->port_no ? 0x08: 0x02;
	/* The clock bits are in the same register for both channels */
	void __iomem *master = ap->host->ports[0]->ioaddr.bmdma_addr;
	void __iomem *clock = master + 0x11;
	void __iomem *atapi_reg = master + 0x20 + (4 * ap->port_no);

	/* Cases the state machine will not complete correctly */
	if (tf->protocol == ATAPI_PROT_DMA || (tf->flags & ATA_TFLAG_LBA48)) {
		iowrite32(0, atapi_reg);
		iowrite8(ioread8(clock) & ~sel66, clock);
	}
	/* Flip back to 33Mhz for PIO */
	if (adev->dma_mode > XFER_UDMA_2)
		iowrite8(ioread8(clock) & ~sel66, clock);
	ata_bmdma_stop(qc);
	pdc202xx_set_piomode(ap, adev);
}

/**
 *	pdc2026x_dev_config	-	device setup hook
 *	@adev: newly found device
 *
 *	Perform chip specific early setup. We need to lock the transfer
 *	sizes to 8bit to avoid making the state engine on the 2026x cards
 *	barf.
 */

static void pdc2026x_dev_config(struct ata_device *adev)
{
	adev->max_sectors = 256;
}

static int pdc2026x_port_start(struct ata_port *ap)
{
	void __iomem *bmdma = ap->ioaddr.bmdma_addr;
	if (bmdma) {
		/* Enable burst mode */
		u8 burst = ioread8(bmdma + 0x1f);
		iowrite8(burst | 0x01, bmdma + 0x1f);
	}
	return ata_bmdma_port_start(ap);
}

/**
 *	pdc2026x_check_atapi_dma - Check whether ATAPI DMA can be supported for this command
 *	@qc: Metadata associated with taskfile to check
 *
 *	Just say no - not supported on older Promise.
 *
 *	LOCKING:
 *	None (inherited from caller).
 *
 *	RETURNS: 0 when ATAPI DMA can be used
 *		 1 otherwise
 */

static int pdc2026x_check_atapi_dma(struct ata_queued_cmd *qc)
{
	return 1;
}

static struct scsi_host_template pdc202xx_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations pdc2024x_port_ops = {
	.inherits		= &ata_bmdma_port_ops,

	.cable_detect		= ata_cable_40wire,
	.set_piomode		= pdc202xx_set_piomode,
	.set_dmamode		= pdc202xx_set_dmamode,

	.sff_exec_command	= pdc202xx_exec_command,
};

static struct ata_port_operations pdc2026x_port_ops = {
	.inherits		= &pdc2024x_port_ops,

	.check_atapi_dma	= pdc2026x_check_atapi_dma,
	.bmdma_start		= pdc2026x_bmdma_start,
	.bmdma_stop		= pdc2026x_bmdma_stop,

	.cable_detect		= pdc2026x_cable_detect,
	.dev_config		= pdc2026x_dev_config,

	.port_start		= pdc2026x_port_start,

	.sff_exec_command	= pdc202xx_exec_command,
};

static int pdc202xx_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static const struct ata_port_info info[3] = {
		{
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA2,
			.port_ops = &pdc2024x_port_ops
		},
		{
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA4,
			.port_ops = &pdc2026x_port_ops
		},
		{
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA5,
			.port_ops = &pdc2026x_port_ops
		}

	};
	const struct ata_port_info *ppi[] = { &info[id->driver_data], NULL };

	if (dev->device == PCI_DEVICE_ID_PROMISE_20265) {
		struct pci_dev *bridge = dev->bus->self;
		/* Don't grab anything behind a Promise I2O RAID */
		if (bridge && bridge->vendor == PCI_VENDOR_ID_INTEL) {
			if (bridge->device == PCI_DEVICE_ID_INTEL_I960)
				return -ENODEV;
			if (bridge->device == PCI_DEVICE_ID_INTEL_I960RM)
				return -ENODEV;
		}
	}
	return ata_pci_bmdma_init_one(dev, ppi, &pdc202xx_sht, NULL, 0);
}

static const struct pci_device_id pdc202xx[] = {
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20246), 0 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20262), 1 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20263), 1 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20265), 2 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20267), 2 },

	{ },
};

static struct pci_driver pdc202xx_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= pdc202xx,
	.probe 		= pdc202xx_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

static int __init pdc202xx_init(void)
{
	return pci_register_driver(&pdc202xx_pci_driver);
}

static void __exit pdc202xx_exit(void)
{
	pci_unregister_driver(&pdc202xx_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Promise 2024x and 20262-20267");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pdc202xx);
MODULE_VERSION(DRV_VERSION);

module_init(pdc202xx_init);
module_exit(pdc202xx_exit);
