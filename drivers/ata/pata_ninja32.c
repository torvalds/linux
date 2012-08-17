/*
 * pata_ninja32.c 	- Ninja32 PATA for new ATA layer
 *			  (C) 2007 Red Hat Inc
 *
 * Note: The controller like many controllers has shared timings for
 * PIO and DMA. We thus flip to the DMA timings in dma_start and flip back
 * in the dma_stop function. Thus we actually don't need a set_dmamode
 * method as the PIO method is always called and will set the right PIO
 * timing parameters.
 *
 * The Ninja32 Cardbus is not a generic SFF controller. Instead it is
 * laid out as follows off BAR 0. This is based upon Mark Lord's delkin
 * driver and the extensive analysis done by the BSD developers, notably
 * ITOH Yasufumi.
 *
 *	Base + 0x00 IRQ Status
 *	Base + 0x01 IRQ control
 *	Base + 0x02 Chipset control
 *	Base + 0x03 Unknown
 *	Base + 0x04 VDMA and reset control + wait bits
 *	Base + 0x08 BMIMBA
 *	Base + 0x0C DMA Length
 *	Base + 0x10 Taskfile
 *	Base + 0x18 BMDMA Status ?
 *	Base + 0x1C
 *	Base + 0x1D Bus master control
 *		bit 0 = enable
 *		bit 1 = 0 write/1 read
 *		bit 2 = 1 sgtable
 *		bit 3 = go
 *		bit 4-6 wait bits
 *		bit 7 = done
 *	Base + 0x1E AltStatus
 *	Base + 0x1F timing register
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_ninja32"
#define DRV_VERSION "0.1.5"


/**
 *	ninja32_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup. Our timing registers are shared
 *	but we want to set the PIO timing by default.
 */

static void ninja32_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	static u16 pio_timing[5] = {
		0xd6, 0x85, 0x44, 0x33, 0x13
	};
	iowrite8(pio_timing[adev->pio_mode - XFER_PIO_0],
		 ap->ioaddr.bmdma_addr + 0x1f);
	ap->private_data = adev;
}


static void ninja32_dev_select(struct ata_port *ap, unsigned int device)
{
	struct ata_device *adev = &ap->link.device[device];
	if (ap->private_data != adev) {
		iowrite8(0xd6, ap->ioaddr.bmdma_addr + 0x1f);
		ata_sff_dev_select(ap, device);
		ninja32_set_piomode(ap, adev);
	}
}

static struct scsi_host_template ninja32_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations ninja32_port_ops = {
	.inherits	= &ata_bmdma_port_ops,
	.sff_dev_select = ninja32_dev_select,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= ninja32_set_piomode,
	.sff_data_xfer	= ata_sff_data_xfer32
};

static void ninja32_program(void __iomem *base)
{
	iowrite8(0x05, base + 0x01);	/* Enable interrupt lines */
	iowrite8(0xBE, base + 0x02);	/* Burst, ?? setup */
	iowrite8(0x01, base + 0x03);	/* Unknown */
	iowrite8(0x20, base + 0x04);	/* WAIT0 */
	iowrite8(0x8f, base + 0x05);	/* Unknown */
	iowrite8(0xa4, base + 0x1c);	/* Unknown */
	iowrite8(0x83, base + 0x1d);	/* BMDMA control: WAIT0 */
}

static int ninja32_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct ata_host *host;
	struct ata_port *ap;
	void __iomem *base;
	int rc;

	host = ata_host_alloc(&dev->dev, 1);
	if (!host)
		return -ENOMEM;
	ap = host->ports[0];

	/* Set up the PCI device */
	rc = pcim_enable_device(dev);
	if (rc)
		return rc;
	rc = pcim_iomap_regions(dev, 1 << 0, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(dev);
	if (rc)
		return rc;

	host->iomap = pcim_iomap_table(dev);
	rc = pci_set_dma_mask(dev, ATA_DMA_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(dev, ATA_DMA_MASK);
	if (rc)
		return rc;
	pci_set_master(dev);

	/* Set up the register mappings. We use the I/O mapping as only the
	   older chips also have MMIO on BAR 1 */
	base = host->iomap[0];
	if (!base)
		return -ENOMEM;
	ap->ops = &ninja32_port_ops;
	ap->pio_mask = ATA_PIO4;
	ap->flags |= ATA_FLAG_SLAVE_POSS;

	ap->ioaddr.cmd_addr = base + 0x10;
	ap->ioaddr.ctl_addr = base + 0x1E;
	ap->ioaddr.altstatus_addr = base + 0x1E;
	ap->ioaddr.bmdma_addr = base;
	ata_sff_std_ports(&ap->ioaddr);
	ap->pflags = ATA_PFLAG_PIO32 | ATA_PFLAG_PIO32CHANGE;

	ninja32_program(base);
	/* FIXME: Should we disable them at remove ? */
	return ata_host_activate(host, dev->irq, ata_bmdma_interrupt,
				 IRQF_SHARED, &ninja32_sht);
}

#ifdef CONFIG_PM

static int ninja32_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;
	ninja32_program(host->iomap[0]);
	ata_host_resume(host);
	return 0;
}
#endif

static const struct pci_device_id ninja32[] = {
	{ 0x10FC, 0x0003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1145, 0x8008, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1145, 0xf008, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1145, 0xf021, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1145, 0xf024, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1145, 0xf02C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ },
};

static struct pci_driver ninja32_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= ninja32,
	.probe 		= ninja32_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ninja32_reinit_one,
#endif
};

module_pci_driver(ninja32_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Ninja32 ATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ninja32);
MODULE_VERSION(DRV_VERSION);
