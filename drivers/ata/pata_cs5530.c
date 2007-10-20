/*
 * pata-cs5530.c 	- CS5530 PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 * based upon cs5530.c by Mark Lord.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Loosely based on the piix & svwks drivers.
 *
 * Documentation:
 *	Available from AMD web site.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/dmi.h>

#define DRV_NAME	"pata_cs5530"
#define DRV_VERSION	"0.7.4"

static void __iomem *cs5530_port_base(struct ata_port *ap)
{
	unsigned long bmdma = (unsigned long)ap->ioaddr.bmdma_addr;

	return (void __iomem *)((bmdma & ~0x0F) + 0x20 + 0x10 * ap->port_no);
}

/**
 *	cs5530_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Set our PIO requirements. This is fairly simple on the CS5530
 *	chips.
 */

static void cs5530_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	static const unsigned int cs5530_pio_timings[2][5] = {
		{0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010},
		{0xd1329172, 0x71212171, 0x30200080, 0x20102010, 0x00100010}
	};
	void __iomem *base = cs5530_port_base(ap);
	u32 tuning;
	int format;

	/* Find out which table to use */
	tuning = ioread32(base + 0x04);
	format = (tuning & 0x80000000UL) ? 1 : 0;

	/* Now load the right timing register */
	if (adev->devno)
		base += 0x08;

	iowrite32(cs5530_pio_timings[format][adev->pio_mode - XFER_PIO_0], base);
}

/**
 *	cs5530_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	We cannot mix MWDMA and UDMA without reloading timings each switch
 *	master to slave. We track the last DMA setup in order to minimise
 *	reloads.
 */

static void cs5530_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	void __iomem *base = cs5530_port_base(ap);
	u32 tuning, timing = 0;
	u8 reg;

	/* Find out which table to use */
	tuning = ioread32(base + 0x04);

	switch(adev->dma_mode) {
		case XFER_UDMA_0:
			timing  = 0x00921250;break;
		case XFER_UDMA_1:
			timing  = 0x00911140;break;
		case XFER_UDMA_2:
			timing  = 0x00911030;break;
		case XFER_MW_DMA_0:
			timing  = 0x00077771;break;
		case XFER_MW_DMA_1:
			timing  = 0x00012121;break;
		case XFER_MW_DMA_2:
			timing  = 0x00002020;break;
		default:
			BUG();
	}
	/* Merge in the PIO format bit */
	timing |= (tuning & 0x80000000UL);
	if (adev->devno == 0) /* Master */
		iowrite32(timing, base + 0x04);
	else {
		if (timing & 0x00100000)
			tuning |= 0x00100000;	/* UDMA for both */
		else
			tuning &= ~0x00100000;	/* MWDMA for both */
		iowrite32(tuning, base + 0x04);
		iowrite32(timing, base + 0x0C);
	}

	/* Set the DMA capable bit in the BMDMA area */
	reg = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	reg |= (1 << (5 + adev->devno));
	iowrite8(reg, ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

	/* Remember the last DMA setup we did */

	ap->private_data = adev;
}

/**
 *	cs5530_qc_issue_prot	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	necessary.  Specifically we have a problem that there is only
 *	one MWDMA/UDMA bit.
 */

static unsigned int cs5530_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct ata_device *prev = ap->private_data;

	/* See if the DMA settings could be wrong */
	if (adev->dma_mode != 0 && adev != prev && prev != NULL) {
		/* Maybe, but do the channels match MWDMA/UDMA ? */
		if ((adev->dma_mode >= XFER_UDMA_0 && prev->dma_mode < XFER_UDMA_0) ||
		    (adev->dma_mode < XFER_UDMA_0 && prev->dma_mode >= XFER_UDMA_0))
		    	/* Switch the mode bits */
		    	cs5530_set_dmamode(ap, adev);
	}

	return ata_qc_issue_prot(qc);
}

static struct scsi_host_template cs5530_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_DUMB_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations cs5530_port_ops = {
	.set_piomode	= cs5530_set_piomode,
	.set_dmamode	= cs5530_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.qc_prep 	= ata_dumb_qc_prep,
	.qc_issue	= cs5530_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,

	.port_start	= ata_sff_port_start,
};

static const struct dmi_system_id palmax_dmi_table[] = {
	{
		.ident = "Palmax PD1100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Cyrix"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Caddis"),
		},
	},
	{ }
};

static int cs5530_is_palmax(void)
{
	if (dmi_check_system(palmax_dmi_table)) {
		printk(KERN_INFO "Palmax PD1100: Disabling DMA on docking port.\n");
		return 1;
	}
	return 0;
}


/**
 *	cs5530_init_chip	-	Chipset init
 *
 *	Perform the chip initialisation work that is shared between both
 *	setup and resume paths
 */

static int cs5530_init_chip(void)
{
	struct pci_dev *master_0 = NULL, *cs5530_0 = NULL, *dev = NULL;

	while ((dev = pci_get_device(PCI_VENDOR_ID_CYRIX, PCI_ANY_ID, dev)) != NULL) {
		switch (dev->device) {
			case PCI_DEVICE_ID_CYRIX_PCI_MASTER:
				master_0 = pci_dev_get(dev);
				break;
			case PCI_DEVICE_ID_CYRIX_5530_LEGACY:
				cs5530_0 = pci_dev_get(dev);
				break;
		}
	}
	if (!master_0) {
		printk(KERN_ERR DRV_NAME ": unable to locate PCI MASTER function\n");
		goto fail_put;
	}
	if (!cs5530_0) {
		printk(KERN_ERR DRV_NAME ": unable to locate CS5530 LEGACY function\n");
		goto fail_put;
	}

	pci_set_master(cs5530_0);
	pci_try_set_mwi(cs5530_0);

	/*
	 * Set PCI CacheLineSize to 16-bytes:
	 * --> Write 0x04 into 8-bit PCI CACHELINESIZE reg of function 0 of the cs5530
	 *
	 * Note: This value is constant because the 5530 is only a Geode companion
	 */

	pci_write_config_byte(cs5530_0, PCI_CACHE_LINE_SIZE, 0x04);

	/*
	 * Disable trapping of UDMA register accesses (Win98 hack):
	 * --> Write 0x5006 into 16-bit reg at offset 0xd0 of function 0 of the cs5530
	 */

	pci_write_config_word(cs5530_0, 0xd0, 0x5006);

	/*
	 * Bit-1 at 0x40 enables MemoryWriteAndInvalidate on internal X-bus:
	 * The other settings are what is necessary to get the register
	 * into a sane state for IDE DMA operation.
	 */

	pci_write_config_byte(master_0, 0x40, 0x1e);

	/*
	 * Set max PCI burst size (16-bytes seems to work best):
	 *	   16bytes: set bit-1 at 0x41 (reg value of 0x16)
	 *	all others: clear bit-1 at 0x41, and do:
	 *	  128bytes: OR 0x00 at 0x41
	 *	  256bytes: OR 0x04 at 0x41
	 *	  512bytes: OR 0x08 at 0x41
	 *	 1024bytes: OR 0x0c at 0x41
	 */

	pci_write_config_byte(master_0, 0x41, 0x14);

	/*
	 * These settings are necessary to get the chip
	 * into a sane state for IDE DMA operation.
	 */

	pci_write_config_byte(master_0, 0x42, 0x00);
	pci_write_config_byte(master_0, 0x43, 0xc1);

	pci_dev_put(master_0);
	pci_dev_put(cs5530_0);
	return 0;
fail_put:
	if (master_0)
		pci_dev_put(master_0);
	if (cs5530_0)
		pci_dev_put(cs5530_0);
	return -ENODEV;
}

/**
 *	cs5530_init_one		-	Initialise a CS5530
 *	@dev: PCI device
 *	@id: Entry in match table
 *
 *	Install a driver for the newly found CS5530 companion chip. Most of
 *	this is just housekeeping. We have to set the chip up correctly and
 *	turn off various bits of emulation magic.
 */

static int cs5530_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.sht = &cs5530_sht,
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x07,
		.port_ops = &cs5530_port_ops
	};
	/* The docking connector doesn't do UDMA, and it seems not MWDMA */
	static const struct ata_port_info info_palmax_secondary = {
		.sht = &cs5530_sht,
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.port_ops = &cs5530_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };

	/* Chip initialisation */
	if (cs5530_init_chip())
		return -ENODEV;

	if (cs5530_is_palmax())
		ppi[1] = &info_palmax_secondary;

	/* Now kick off ATA set up */
	return ata_pci_init_one(pdev, ppi);
}

#ifdef CONFIG_PM
static int cs5530_reinit_one(struct pci_dev *pdev)
{
	/* If we fail on resume we are doomed */
	if (cs5530_init_chip())
		BUG();
	return ata_pci_device_resume(pdev);
}
#endif /* CONFIG_PM */

static const struct pci_device_id cs5530[] = {
	{ PCI_VDEVICE(CYRIX, PCI_DEVICE_ID_CYRIX_5530_IDE), },

	{ },
};

static struct pci_driver cs5530_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= cs5530,
	.probe 		= cs5530_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= cs5530_reinit_one,
#endif
};

static int __init cs5530_init(void)
{
	return pci_register_driver(&cs5530_pci_driver);
}

static void __exit cs5530_exit(void)
{
	pci_unregister_driver(&cs5530_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the Cyrix/NS/AMD 5530");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, cs5530);
MODULE_VERSION(DRV_VERSION);

module_init(cs5530_init);
module_exit(cs5530_exit);
