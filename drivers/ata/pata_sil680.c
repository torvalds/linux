/*
 * pata_sil680.c 	- SIL680 PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 * based upon
 *
 * linux/drivers/ide/pci/siimage.c		Version 1.07	Nov 30, 2003
 *
 * Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2003		Red Hat <alan@redhat.com>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  Documentation publically available.
 *
 *	If you have strange problems with nVidia chipset systems please
 *	see the SI support documentation and update your system BIOS
 *	if necessary
 *
 * TODO
 *	If we know all our devices are LBA28 (or LBA28 sized)  we could use
 *	the command fifo mode.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_sil680"
#define DRV_VERSION "0.4.7"

#define SIL680_MMIO_BAR		5

/**
 *	sil680_selreg		-	return register base
 *	@hwif: interface
 *	@r: config offset
 *
 *	Turn a config register offset into the right address in either
 *	PCI space or MMIO space to access the control register in question
 *	Thankfully this is a configuration operation so isnt performance
 *	criticial.
 */

static unsigned long sil680_selreg(struct ata_port *ap, int r)
{
	unsigned long base = 0xA0 + r;
	base += (ap->port_no << 4);
	return base;
}

/**
 *	sil680_seldev		-	return register base
 *	@hwif: interface
 *	@r: config offset
 *
 *	Turn a config register offset into the right address in either
 *	PCI space or MMIO space to access the control register in question
 *	including accounting for the unit shift.
 */

static unsigned long sil680_seldev(struct ata_port *ap, struct ata_device *adev, int r)
{
	unsigned long base = 0xA0 + r;
	base += (ap->port_no << 4);
	base |= adev->devno ? 2 : 0;
	return base;
}


/**
 *	sil680_cable_detect	-	cable detection
 *	@ap: ATA port
 *
 *	Perform cable detection. The SIL680 stores this in PCI config
 *	space for us.
 */

static int sil680_cable_detect(struct ata_port *ap) {
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned long addr = sil680_selreg(ap, 0);
	u8 ata66;
	pci_read_config_byte(pdev, addr, &ata66);
	if (ata66 & 1)
		return ATA_CBL_PATA80;
	else
		return ATA_CBL_PATA40;
}

/**
 *	sil680_bus_reset	-	reset the SIL680 bus
 *	@link: ATA link to reset
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform the SIL680 housekeeping when doing an ATA bus reset
 */

static int sil680_bus_reset(struct ata_link *link, unsigned int *classes,
			    unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned long addr = sil680_selreg(ap, 0);
	u8 reset;

	pci_read_config_byte(pdev, addr, &reset);
	pci_write_config_byte(pdev, addr, reset | 0x03);
	udelay(25);
	pci_write_config_byte(pdev, addr, reset);
	return ata_std_softreset(link, classes, deadline);
}

static void sil680_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, ata_std_prereset, sil680_bus_reset, NULL, ata_std_postreset);
}

/**
 *	sil680_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the SIL680 registers for PIO mode. Note that the task speed
 *	registers are shared between the devices so we must pick the lowest
 *	mode for command work.
 */

static void sil680_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	static u16 speed_p[5] = { 0x328A, 0x2283, 0x1104, 0x10C3, 0x10C1 };
	static u16 speed_t[5] = { 0x328A, 0x2283, 0x1281, 0x10C3, 0x10C1 };

	unsigned long tfaddr = sil680_selreg(ap, 0x02);
	unsigned long addr = sil680_seldev(ap, adev, 0x04);
	unsigned long addr_mask = 0x80 + 4 * ap->port_no;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int pio = adev->pio_mode - XFER_PIO_0;
	int lowest_pio = pio;
	int port_shift = 4 * adev->devno;
	u16 reg;
	u8 mode;

	struct ata_device *pair = ata_dev_pair(adev);

	if (pair != NULL && adev->pio_mode > pair->pio_mode)
		lowest_pio = pair->pio_mode - XFER_PIO_0;

	pci_write_config_word(pdev, addr, speed_p[pio]);
	pci_write_config_word(pdev, tfaddr, speed_t[lowest_pio]);

	pci_read_config_word(pdev, tfaddr-2, &reg);
	pci_read_config_byte(pdev, addr_mask, &mode);

	reg &= ~0x0200;			/* Clear IORDY */
	mode &= ~(3 << port_shift);	/* Clear IORDY and DMA bits */

	if (ata_pio_need_iordy(adev)) {
		reg |= 0x0200;		/* Enable IORDY */
		mode |= 1 << port_shift;
	}
	pci_write_config_word(pdev, tfaddr-2, reg);
	pci_write_config_byte(pdev, addr_mask, mode);
}

/**
 *	sil680_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the MWDMA/UDMA modes for the sil680 k
 *	chipset. The MWDMA mode values are pulled from a lookup table
 *	while the chipset uses mode number for UDMA.
 */

static void sil680_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static u8 ultra_table[2][7] = {
		{ 0x0C, 0x07, 0x05, 0x04, 0x02, 0x01, 0xFF },	/* 100MHz */
		{ 0x0F, 0x0B, 0x07, 0x05, 0x03, 0x02, 0x01 },	/* 133Mhz */
	};
	static u16 dma_table[3] = { 0x2208, 0x10C2, 0x10C1 };

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned long ma = sil680_seldev(ap, adev, 0x08);
	unsigned long ua = sil680_seldev(ap, adev, 0x0C);
	unsigned long addr_mask = 0x80 + 4 * ap->port_no;
	int port_shift = adev->devno * 4;
	u8 scsc, mode;
	u16 multi, ultra;

	pci_read_config_byte(pdev, 0x8A, &scsc);
	pci_read_config_byte(pdev, addr_mask, &mode);
	pci_read_config_word(pdev, ma, &multi);
	pci_read_config_word(pdev, ua, &ultra);

	/* Mask timing bits */
	ultra &= ~0x3F;
	mode &= ~(0x03 << port_shift);

	/* Extract scsc */
	scsc = (scsc & 0x30) ? 1: 0;

	if (adev->dma_mode >= XFER_UDMA_0) {
		multi = 0x10C1;
		ultra |= ultra_table[scsc][adev->dma_mode - XFER_UDMA_0];
		mode |= (0x03 << port_shift);
	} else {
		multi = dma_table[adev->dma_mode - XFER_MW_DMA_0];
		mode |= (0x02 << port_shift);
	}
	pci_write_config_byte(pdev, addr_mask, mode);
	pci_write_config_word(pdev, ma, multi);
	pci_write_config_word(pdev, ua, ultra);
}

static struct scsi_host_template sil680_sht = {
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

static struct ata_port_operations sil680_port_ops = {
	.set_piomode	= sil680_set_piomode,
	.set_dmamode	= sil680_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= sil680_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= sil680_cable_detect,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,

	.port_start	= ata_sff_port_start,
};

/**
 *	sil680_init_chip		-	chip setup
 *	@pdev: PCI device
 *
 *	Perform all the chip setup which must be done both when the device
 *	is powered up on boot and when we resume in case we resumed from RAM.
 *	Returns the final clock settings.
 */

static u8 sil680_init_chip(struct pci_dev *pdev, int *try_mmio)
{
	u32 class_rev	= 0;
	u8 tmpbyte	= 0;

        pci_read_config_dword(pdev, PCI_CLASS_REVISION, &class_rev);
        class_rev &= 0xff;
        /* FIXME: double check */
	pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, (class_rev) ? 1 : 255);

	pci_write_config_byte(pdev, 0x80, 0x00);
	pci_write_config_byte(pdev, 0x84, 0x00);

	pci_read_config_byte(pdev, 0x8A, &tmpbyte);

	dev_dbg(&pdev->dev, "sil680: BA5_EN = %d clock = %02X\n",
		tmpbyte & 1, tmpbyte & 0x30);

	*try_mmio = (tmpbyte & 1) || pci_resource_start(pdev, 5);

	switch(tmpbyte & 0x30) {
		case 0x00:
			/* 133 clock attempt to force it on */
			pci_write_config_byte(pdev, 0x8A, tmpbyte|0x10);
			break;
		case 0x30:
			/* if clocking is disabled */
			/* 133 clock attempt to force it on */
			pci_write_config_byte(pdev, 0x8A, tmpbyte & ~0x20);
			break;
		case 0x10:
			/* 133 already */
			break;
		case 0x20:
			/* BIOS set PCI x2 clocking */
			break;
	}

	pci_read_config_byte(pdev,   0x8A, &tmpbyte);
	dev_dbg(&pdev->dev, "sil680: BA5_EN = %d clock = %02X\n",
		tmpbyte & 1, tmpbyte & 0x30);

	pci_write_config_byte(pdev,  0xA1, 0x72);
	pci_write_config_word(pdev,  0xA2, 0x328A);
	pci_write_config_dword(pdev, 0xA4, 0x62DD62DD);
	pci_write_config_dword(pdev, 0xA8, 0x43924392);
	pci_write_config_dword(pdev, 0xAC, 0x40094009);
	pci_write_config_byte(pdev,  0xB1, 0x72);
	pci_write_config_word(pdev,  0xB2, 0x328A);
	pci_write_config_dword(pdev, 0xB4, 0x62DD62DD);
	pci_write_config_dword(pdev, 0xB8, 0x43924392);
	pci_write_config_dword(pdev, 0xBC, 0x40094009);

	switch(tmpbyte & 0x30) {
		case 0x00: printk(KERN_INFO "sil680: 100MHz clock.\n");break;
		case 0x10: printk(KERN_INFO "sil680: 133MHz clock.\n");break;
		case 0x20: printk(KERN_INFO "sil680: Using PCI clock.\n");break;
		/* This last case is _NOT_ ok */
		case 0x30: printk(KERN_ERR "sil680: Clock disabled ?\n");
	}
	return tmpbyte & 0x30;
}

static int __devinit sil680_init_one(struct pci_dev *pdev,
				     const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.sht = &sil680_sht,
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA6,
		.port_ops = &sil680_port_ops
	};
	static const struct ata_port_info info_slow = {
		.sht = &sil680_sht,
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA5,
		.port_ops = &sil680_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };
	static int printed_version;
	struct ata_host *host;
	void __iomem *mmio_base;
	int rc, try_mmio;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	switch (sil680_init_chip(pdev, &try_mmio)) {
		case 0:
			ppi[0] = &info_slow;
			break;
		case 0x30:
			return -ENODEV;
	}

	if (!try_mmio)
		goto use_ioports;

	/* Try to acquire MMIO resources and fallback to PIO if
	 * that fails
	 */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;
	rc = pcim_iomap_regions(pdev, 1 << SIL680_MMIO_BAR, DRV_NAME);
	if (rc)
		goto use_ioports;

	/* Allocate host and set it up */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 2);
	if (!host)
		return -ENOMEM;
	host->iomap = pcim_iomap_table(pdev);

	/* Setup DMA masks */
	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;
	pci_set_master(pdev);

	/* Get MMIO base and initialize port addresses */
	mmio_base = host->iomap[SIL680_MMIO_BAR];
	host->ports[0]->ioaddr.bmdma_addr = mmio_base + 0x00;
	host->ports[0]->ioaddr.cmd_addr = mmio_base + 0x80;
	host->ports[0]->ioaddr.ctl_addr = mmio_base + 0x8a;
	host->ports[0]->ioaddr.altstatus_addr = mmio_base + 0x8a;
	ata_std_ports(&host->ports[0]->ioaddr);
	host->ports[1]->ioaddr.bmdma_addr = mmio_base + 0x08;
	host->ports[1]->ioaddr.cmd_addr = mmio_base + 0xc0;
	host->ports[1]->ioaddr.ctl_addr = mmio_base + 0xca;
	host->ports[1]->ioaddr.altstatus_addr = mmio_base + 0xca;
	ata_std_ports(&host->ports[1]->ioaddr);

	/* Register & activate */
	return ata_host_activate(host, pdev->irq, ata_interrupt, IRQF_SHARED,
				 &sil680_sht);

use_ioports:
	return ata_pci_init_one(pdev, ppi);
}

#ifdef CONFIG_PM
static int sil680_reinit_one(struct pci_dev *pdev)
{
	int try_mmio;

	sil680_init_chip(pdev, &try_mmio);
	return ata_pci_device_resume(pdev);
}
#endif

static const struct pci_device_id sil680[] = {
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_SII_680), },

	{ },
};

static struct pci_driver sil680_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= sil680,
	.probe 		= sil680_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= sil680_reinit_one,
#endif
};

static int __init sil680_init(void)
{
	return pci_register_driver(&sil680_pci_driver);
}

static void __exit sil680_exit(void)
{
	pci_unregister_driver(&sil680_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for SI680 PATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, sil680);
MODULE_VERSION(DRV_VERSION);

module_init(sil680_init);
module_exit(sil680_exit);
