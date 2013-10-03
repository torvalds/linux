/*
 *	pata_hpt3x3		-	HPT3x3 driver
 *	(c) Copyright 2005-2006 Red Hat
 *
 *	Was pata_hpt34x but the naming was confusing as it supported the
 *	343 and 363 so it has been renamed.
 *
 *	Based on:
 *	linux/drivers/ide/pci/hpt34x.c		Version 0.40	Sept 10, 2002
 *	Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 *
 *	May be copied or modified under the terms of the GNU General Public
 *	License
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"pata_hpt3x3"
#define DRV_VERSION	"0.6.1"

/**
 *	hpt3x3_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Set our PIO requirements. This is fairly simple on the HPT3x3 as
 *	all we have to do is clear the MWDMA and UDMA bits then load the
 *	mode number.
 */

static void hpt3x3_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 r1, r2;
	int dn = 2 * ap->port_no + adev->devno;

	pci_read_config_dword(pdev, 0x44, &r1);
	pci_read_config_dword(pdev, 0x48, &r2);
	/* Load the PIO timing number */
	r1 &= ~(7 << (3 * dn));
	r1 |= (adev->pio_mode - XFER_PIO_0) << (3 * dn);
	r2 &= ~(0x11 << dn);	/* Clear MWDMA and UDMA bits */

	pci_write_config_dword(pdev, 0x44, r1);
	pci_write_config_dword(pdev, 0x48, r2);
}

#if defined(CONFIG_PATA_HPT3X3_DMA)
/**
 *	hpt3x3_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	Set up the channel for MWDMA or UDMA modes. Much the same as with
 *	PIO, load the mode number and then set MWDMA or UDMA flag.
 *
 *	0x44 : bit 0-2 master mode, 3-5 slave mode, etc
 *	0x48 : bit 4/0 DMA/UDMA bit 5/1 for slave etc
 */

static void hpt3x3_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 r1, r2;
	int dn = 2 * ap->port_no + adev->devno;
	int mode_num = adev->dma_mode & 0x0F;

	pci_read_config_dword(pdev, 0x44, &r1);
	pci_read_config_dword(pdev, 0x48, &r2);
	/* Load the timing number */
	r1 &= ~(7 << (3 * dn));
	r1 |= (mode_num << (3 * dn));
	r2 &= ~(0x11 << dn);	/* Clear MWDMA and UDMA bits */

	if (adev->dma_mode >= XFER_UDMA_0)
		r2 |= (0x01 << dn);	/* Ultra mode */
	else
		r2 |= (0x10 << dn);	/* MWDMA */

	pci_write_config_dword(pdev, 0x44, r1);
	pci_write_config_dword(pdev, 0x48, r2);
}

/**
 *	hpt3x3_freeze		-	DMA workaround
 *	@ap: port to freeze
 *
 *	When freezing an HPT3x3 we must stop any pending DMA before
 *	writing to the control register or the chip will hang
 */

static void hpt3x3_freeze(struct ata_port *ap)
{
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	iowrite8(ioread8(mmio + ATA_DMA_CMD) & ~ ATA_DMA_START,
			mmio + ATA_DMA_CMD);
	ata_sff_dma_pause(ap);
	ata_sff_freeze(ap);
}

/**
 *	hpt3x3_bmdma_setup	-	DMA workaround
 *	@qc: Queued command
 *
 *	When issuing BMDMA we must clean up the error/active bits in
 *	software on this device
 */

static void hpt3x3_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 r = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	r |= ATA_DMA_INTR | ATA_DMA_ERR;
	iowrite8(r, ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	return ata_bmdma_setup(qc);
}

/**
 *	hpt3x3_atapi_dma	-	ATAPI DMA check
 *	@qc: Queued command
 *
 *	Just say no - we don't do ATAPI DMA
 */

static int hpt3x3_atapi_dma(struct ata_queued_cmd *qc)
{
	return 1;
}

#endif /* CONFIG_PATA_HPT3X3_DMA */

static struct scsi_host_template hpt3x3_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations hpt3x3_port_ops = {
	.inherits	= &ata_bmdma_port_ops,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= hpt3x3_set_piomode,
#if defined(CONFIG_PATA_HPT3X3_DMA)
	.set_dmamode	= hpt3x3_set_dmamode,
	.bmdma_setup	= hpt3x3_bmdma_setup,
	.check_atapi_dma= hpt3x3_atapi_dma,
	.freeze		= hpt3x3_freeze,
#endif

};

/**
 *	hpt3x3_init_chipset	-	chip setup
 *	@dev: PCI device
 *
 *	Perform the setup required at boot and on resume.
 */

static void hpt3x3_init_chipset(struct pci_dev *dev)
{
	u16 cmd;
	/* Initialize the board */
	pci_write_config_word(dev, 0x80, 0x00);
	/* Check if it is a 343 or a 363. 363 has COMMAND_MEMORY set */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (cmd & PCI_COMMAND_MEMORY)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0xF0);
	else
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x20);
}

/**
 *	hpt3x3_init_one		-	Initialise an HPT343/363
 *	@pdev: PCI device
 *	@id: Entry in match table
 *
 *	Perform basic initialisation. We set the device up so we access all
 *	ports via BAR4. This is necessary to work around errata.
 */

static int hpt3x3_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
#if defined(CONFIG_PATA_HPT3X3_DMA)
		/* Further debug needed */
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA2,
#endif
		.port_ops = &hpt3x3_port_ops
	};
	/* Register offsets of taskfiles in BAR4 area */
	static const u8 offset_cmd[2] = { 0x20, 0x28 };
	static const u8 offset_ctl[2] = { 0x36, 0x3E };
	const struct ata_port_info *ppi[] = { &info, NULL };
	struct ata_host *host;
	int i, rc;
	void __iomem *base;

	hpt3x3_init_chipset(pdev);

	ata_print_version_once(&pdev->dev, DRV_VERSION);

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 2);
	if (!host)
		return -ENOMEM;
	/* acquire resources and fill host */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* Everything is relative to BAR4 if we set up this way */
	rc = pcim_iomap_regions(pdev, 1 << 4, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);
	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;

	base = host->iomap[4];	/* Bus mastering base */

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		struct ata_ioports *ioaddr = &ap->ioaddr;

		ioaddr->cmd_addr = base + offset_cmd[i];
		ioaddr->altstatus_addr =
		ioaddr->ctl_addr = base + offset_ctl[i];
		ioaddr->scr_addr = NULL;
		ata_sff_std_ports(ioaddr);
		ioaddr->bmdma_addr = base + 8 * i;

		ata_port_pbar_desc(ap, 4, -1, "ioport");
		ata_port_pbar_desc(ap, 4, offset_cmd[i], "cmd");
	}
	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, ata_bmdma_interrupt,
				 IRQF_SHARED, &hpt3x3_sht);
}

#ifdef CONFIG_PM
static int hpt3x3_reinit_one(struct pci_dev *dev)
{
	struct ata_host *host = pci_get_drvdata(dev);
	int rc;

	rc = ata_pci_device_do_resume(dev);
	if (rc)
		return rc;

	hpt3x3_init_chipset(dev);

	ata_host_resume(host);
	return 0;
}
#endif

static const struct pci_device_id hpt3x3[] = {
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT343), },

	{ },
};

static struct pci_driver hpt3x3_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= hpt3x3,
	.probe 		= hpt3x3_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= hpt3x3_reinit_one,
#endif
};

module_pci_driver(hpt3x3_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the Highpoint HPT343/363");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, hpt3x3);
MODULE_VERSION(DRV_VERSION);
