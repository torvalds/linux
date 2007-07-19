/*
 *    pata_artop.c - ARTOP ATA controller driver
 *
 *	(C) 2006 Red Hat <alan@redhat.com>
 *
 *    Based in part on drivers/ide/pci/aec62xx.c
 *	Copyright (C) 1999-2002	Andre Hedrick <andre@linux-ide.org>
 *	865/865R fixes for Macintosh card version from a patch to the old
 *		driver by Thibaut VARENE <varenet@parisc-linux.org>
 *	When setting the PCI latency we must set 0x80 or higher for burst
 *		performance Alessandro Zummo <alessandro.zummo@towertech.it>
 *
 *	TODO
 *	850 serialization once the core supports it
 *	Investigate no_dsc on 850R
 *	Clock detect
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/ata.h>

#define DRV_NAME	"pata_artop"
#define DRV_VERSION	"0.4.3"

/*
 *	The ARTOP has 33 Mhz and "over clocked" timing tables. Until we
 *	get PCI bus speed functionality we leave this as 0. Its a variable
 *	for when we get the functionality and also for folks wanting to
 *	test stuff.
 */

static int clock = 0;

static int artop6210_pre_reset(struct ata_port *ap, unsigned long deadline)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	const struct pci_bits artop_enable_bits[] = {
		{ 0x4AU, 1U, 0x02UL, 0x02UL },	/* port 0 */
		{ 0x4AU, 1U, 0x04UL, 0x04UL },	/* port 1 */
	};

	if (!pci_test_config_bits(pdev, &artop_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_std_prereset(ap, deadline);
}

/**
 *	artop6210_error_handler - Probe specified port on PATA host controller
 *	@ap: Port to probe
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void artop6210_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, artop6210_pre_reset,
				    ata_std_softreset, NULL,
				    ata_std_postreset);
}

/**
 *	artop6260_pre_reset	-	check for 40/80 pin
 *	@ap: Port
 *	@deadline: deadline jiffies for the operation
 *
 *	The ARTOP hardware reports the cable detect bits in register 0x49.
 *	Nothing complicated needed here.
 */

static int artop6260_pre_reset(struct ata_port *ap, unsigned long deadline)
{
	static const struct pci_bits artop_enable_bits[] = {
		{ 0x4AU, 1U, 0x02UL, 0x02UL },	/* port 0 */
		{ 0x4AU, 1U, 0x04UL, 0x04UL },	/* port 1 */
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	/* Odd numbered device ids are the units with enable bits (the -R cards) */
	if (pdev->device % 1 && !pci_test_config_bits(pdev, &artop_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_std_prereset(ap, deadline);
}

/**
 *	artop6260_cable_detect	-	identify cable type
 *	@ap: Port
 *
 *	Identify the cable type for the ARTOP interface in question
 */

static int artop6260_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 tmp;
	pci_read_config_byte(pdev, 0x49, &tmp);
	if (tmp & (1 << ap->port_no))
		return ATA_CBL_PATA40;
	return ATA_CBL_PATA80;
}

/**
 *	artop6260_error_handler - Probe specified port on PATA host controller
 *	@ap: Port to probe
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void artop6260_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, artop6260_pre_reset,
				    ata_std_softreset, NULL,
				    ata_std_postreset);
}

/**
 *	artop6210_load_piomode - Load a set of PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device
 *	@pio: PIO mode
 *
 *	Set PIO mode for device, in host controller PCI config space. This
 *	is used both to set PIO timings in PIO mode and also to set the
 *	matching PIO clocking for UDMA, as well as the MWDMA timings.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void artop6210_load_piomode(struct ata_port *ap, struct ata_device *adev, unsigned int pio)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int dn = adev->devno + 2 * ap->port_no;
	const u16 timing[2][5] = {
		{ 0x0000, 0x000A, 0x0008, 0x0303, 0x0301 },
		{ 0x0700, 0x070A, 0x0708, 0x0403, 0x0401 }

	};
	/* Load the PIO timing active/recovery bits */
	pci_write_config_word(pdev, 0x40 + 2 * dn, timing[clock][pio]);
}

/**
 *	artop6210_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device we are configuring
 *
 *	Set PIO mode for device, in host controller PCI config space. For
 *	ARTOP we must also clear the UDMA bits if we are not doing UDMA. In
 *	the event UDMA is used the later call to set_dmamode will set the
 *	bits as required.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void artop6210_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int dn = adev->devno + 2 * ap->port_no;
	u8 ultra;

	artop6210_load_piomode(ap, adev, adev->pio_mode - XFER_PIO_0);

	/* Clear the UDMA mode bits (set_dmamode will redo this if needed) */
	pci_read_config_byte(pdev, 0x54, &ultra);
	ultra &= ~(3 << (2 * dn));
	pci_write_config_byte(pdev, 0x54, ultra);
}

/**
 *	artop6260_load_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device we are configuring
 *	@pio: PIO mode
 *
 *	Set PIO mode for device, in host controller PCI config space. The
 *	ARTOP6260 and relatives store the timing data differently.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void artop6260_load_piomode (struct ata_port *ap, struct ata_device *adev, unsigned int pio)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int dn = adev->devno + 2 * ap->port_no;
	const u8 timing[2][5] = {
		{ 0x00, 0x0A, 0x08, 0x33, 0x31 },
		{ 0x70, 0x7A, 0x78, 0x43, 0x41 }

	};
	/* Load the PIO timing active/recovery bits */
	pci_write_config_byte(pdev, 0x40 + dn, timing[clock][pio]);
}

/**
 *	artop6260_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device we are configuring
 *
 *	Set PIO mode for device, in host controller PCI config space. For
 *	ARTOP we must also clear the UDMA bits if we are not doing UDMA. In
 *	the event UDMA is used the later call to set_dmamode will set the
 *	bits as required.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void artop6260_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	u8 ultra;

	artop6260_load_piomode(ap, adev, adev->pio_mode - XFER_PIO_0);

	/* Clear the UDMA mode bits (set_dmamode will redo this if needed) */
	pci_read_config_byte(pdev, 0x44 + ap->port_no, &ultra);
	ultra &= ~(7 << (4  * adev->devno));	/* One nibble per drive */
	pci_write_config_byte(pdev, 0x44 + ap->port_no, ultra);
}

/**
 *	artop6210_set_dmamode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device whose timings we are configuring
 *
 *	Set DMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void artop6210_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio;
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int dn = adev->devno + 2 * ap->port_no;
	u8 ultra;

	if (adev->dma_mode == XFER_MW_DMA_0)
		pio = 1;
	else
		pio = 4;

	/* Load the PIO timing active/recovery bits */
	artop6210_load_piomode(ap, adev, pio);

	pci_read_config_byte(pdev, 0x54, &ultra);
	ultra &= ~(3 << (2 * dn));

	/* Add ultra DMA bits if in UDMA mode */
	if (adev->dma_mode >= XFER_UDMA_0) {
		u8 mode = (adev->dma_mode - XFER_UDMA_0) + 1 - clock;
		if (mode == 0)
			mode = 1;
		ultra |= (mode << (2 * dn));
	}
	pci_write_config_byte(pdev, 0x54, ultra);
}

/**
 *	artop6260_set_dmamode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device we are configuring
 *
 *	Set DMA mode for device, in host controller PCI config space. The
 *	ARTOP6260 and relatives store the timing data differently.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void artop6260_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	u8 ultra;

	if (adev->dma_mode == XFER_MW_DMA_0)
		pio = 1;
	else
		pio = 4;

	/* Load the PIO timing active/recovery bits */
	artop6260_load_piomode(ap, adev, pio);

	/* Add ultra DMA bits if in UDMA mode */
	pci_read_config_byte(pdev, 0x44 + ap->port_no, &ultra);
	ultra &= ~(7 << (4  * adev->devno));	/* One nibble per drive */
	if (adev->dma_mode >= XFER_UDMA_0) {
		u8 mode = adev->dma_mode - XFER_UDMA_0 + 1 - clock;
		if (mode == 0)
			mode = 1;
		ultra |= (mode << (4 * adev->devno));
	}
	pci_write_config_byte(pdev, 0x44 + ap->port_no, ultra);
}

static struct scsi_host_template artop_sht = {
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

static const struct ata_port_operations artop6210_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= artop6210_set_piomode,
	.set_dmamode		= artop6210_set_dmamode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= artop6210_error_handler,
	.post_internal_cmd 	= ata_bmdma_post_internal_cmd,
	.cable_detect		= ata_cable_40wire,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,

	.data_xfer		= ata_data_xfer,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};

static const struct ata_port_operations artop6260_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= artop6260_set_piomode,
	.set_dmamode		= artop6260_set_dmamode,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= artop6260_error_handler,
	.post_internal_cmd 	= ata_bmdma_post_internal_cmd,
	.cable_detect		= artop6260_cable_detect,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};


/**
 *	artop_init_one - Register ARTOP ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in artop_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int artop_init_one (struct pci_dev *pdev, const struct pci_device_id *id)
{
	static int printed_version;
	static const struct ata_port_info info_6210 = {
		.sht		= &artop_sht,
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask 	= ATA_UDMA2,
		.port_ops	= &artop6210_ops,
	};
	static const struct ata_port_info info_626x = {
		.sht		= &artop_sht,
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask 	= ATA_UDMA4,
		.port_ops	= &artop6260_ops,
	};
	static const struct ata_port_info info_626x_fast = {
		.sht		= &artop_sht,
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask 	= ATA_UDMA5,
		.port_ops	= &artop6260_ops,
	};
	const struct ata_port_info *ppi[] = { NULL, NULL };

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	if (id->driver_data == 0) {	/* 6210 variant */
		ppi[0] = &info_6210;
		ppi[1] = &ata_dummy_port_info;
		/* BIOS may have left us in UDMA, clear it before libata probe */
		pci_write_config_byte(pdev, 0x54, 0);
		/* For the moment (also lacks dsc) */
		printk(KERN_WARNING "ARTOP 6210 requires serialize functionality not yet supported by libata.\n");
		printk(KERN_WARNING "Secondary ATA ports will not be activated.\n");
	}
	else if (id->driver_data == 1)	/* 6260 */
		ppi[0] = &info_626x;
	else if (id->driver_data == 2)	{ /* 6260 or 6260 + fast */
		unsigned long io = pci_resource_start(pdev, 4);
		u8 reg;

		ppi[0] = &info_626x;
		if (inb(io) & 0x10)
			ppi[0] = &info_626x_fast;
		/* Mac systems come up with some registers not set as we
		   will need them */

		/* Clear reset & test bits */
		pci_read_config_byte(pdev, 0x49, &reg);
		pci_write_config_byte(pdev, 0x49, reg & ~ 0x30);

		/* PCI latency must be > 0x80 for burst mode, tweak it
		 * if required.
		 */
		pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &reg);
		if (reg <= 0x80)
			pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x90);

		/* Enable IRQ output and burst mode */
		pci_read_config_byte(pdev, 0x4a, &reg);
		pci_write_config_byte(pdev, 0x4a, (reg & ~0x01) | 0x80);

	}

	BUG_ON(ppi[0] == NULL);

	return ata_pci_init_one(pdev, ppi);
}

static const struct pci_device_id artop_pci_tbl[] = {
	{ PCI_VDEVICE(ARTOP, 0x0005), 0 },
	{ PCI_VDEVICE(ARTOP, 0x0006), 1 },
	{ PCI_VDEVICE(ARTOP, 0x0007), 1 },
	{ PCI_VDEVICE(ARTOP, 0x0008), 2 },
	{ PCI_VDEVICE(ARTOP, 0x0009), 2 },

	{ }	/* terminate list */
};

static struct pci_driver artop_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= artop_pci_tbl,
	.probe			= artop_init_one,
	.remove			= ata_pci_remove_one,
};

static int __init artop_init(void)
{
	return pci_register_driver(&artop_pci_driver);
}

static void __exit artop_exit(void)
{
	pci_unregister_driver(&artop_pci_driver);
}

module_init(artop_init);
module_exit(artop_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for ARTOP PATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, artop_pci_tbl);
MODULE_VERSION(DRV_VERSION);

