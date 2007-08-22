/*
 *    pata_it8213.c - iTE Tech. Inc.  IT8213 PATA driver
 *
 *    The IT8213 is a very Intel ICH like device for timing purposes, having
 *    a similar register layout and the same split clock arrangement. Cable
 *    detection is different, and it does not have slave channels or all the
 *    clutter of later ICH/SATA setups.
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

#define DRV_NAME	"pata_it8213"
#define DRV_VERSION	"0.0.3"

/**
 *	it8213_pre_reset	-	check for 40/80 pin
 *	@link: link
 *	@deadline: deadline jiffies for the operation
 *
 *	Filter out ports by the enable bits before doing the normal reset
 *	and probe.
 */

static int it8213_pre_reset(struct ata_link *link, unsigned long deadline)
{
	static const struct pci_bits it8213_enable_bits[] = {
		{ 0x41U, 1U, 0x80UL, 0x80UL },	/* port 0 */
	};
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	if (!pci_test_config_bits(pdev, &it8213_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_std_prereset(link, deadline);
}

/**
 *	it8213_error_handler - Probe specified port on PATA host controller
 *	@ap: Port to probe
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void it8213_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, it8213_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	it8213_cable_detect	-	check for 40/80 pin
 *	@ap: Port
 *
 *	Perform cable detection for the 8213 ATA interface. This is
 *	different to the PIIX arrangement
 */

static int it8213_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 tmp;
	pci_read_config_byte(pdev, 0x42, &tmp);
	if (tmp & 2)	/* The initial docs are incorrect */
		return ATA_CBL_PATA40;
	return ATA_CBL_PATA80;
}

/**
 *	it8213_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device whose timings we are configuring
 *
 *	Set PIO mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void it8213_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	unsigned int idetm_port= ap->port_no ? 0x42 : 0x40;
	u16 idetm_data;
	int control = 0;

	/*
	 *	See Intel Document 298600-004 for the timing programing rules
	 *	for PIIX/ICH. The 8213 is a clone so very similar
	 */

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	if (pio > 2)
		control |= 1;	/* TIME1 enable */
	if (ata_pio_need_iordy(adev))	/* PIO 3/4 require IORDY */
		control |= 2;	/* IORDY enable */
	/* Bit 2 is set for ATAPI on the IT8213 - reverse of ICH/PIIX */
	if (adev->class != ATA_DEV_ATA)
		control |= 4;

	pci_read_config_word(dev, idetm_port, &idetm_data);

	/* Enable PPE, IE and TIME as appropriate */

	if (adev->devno == 0) {
		idetm_data &= 0xCCF0;
		idetm_data |= control;
		idetm_data |= (timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	} else {
		u8 slave_data;

		idetm_data &= 0xCC0F;
		idetm_data |= (control << 4);

		/* Slave timing in seperate register */
		pci_read_config_byte(dev, 0x44, &slave_data);
		slave_data &= 0xF0;
		slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) << 4;
		pci_write_config_byte(dev, 0x44, slave_data);
	}

	idetm_data |= 0x4000;	/* Ensure SITRE is enabled */
	pci_write_config_word(dev, idetm_port, idetm_data);
}

/**
 *	it8213_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set UDMA/MWDMA mode for device, in host controller PCI config space.
 *	This device is basically an ICH alike.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void it8213_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	u16 master_data;
	u8 speed		= adev->dma_mode;
	int devid		= adev->devno;
	u8 udma_enable;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(dev, 0x40, &master_data);
	pci_read_config_byte(dev, 0x48, &udma_enable);

	if (speed >= XFER_UDMA_0) {
		unsigned int udma = adev->dma_mode - XFER_UDMA_0;
		u16 udma_timing;
		u16 ideconf;
		int u_clock, u_speed;

		/* Clocks follow the PIIX style */
		u_speed = min(2 - (udma & 1), udma);
		if (udma == 5)
			u_clock = 0x1000;	/* 100Mhz */
		else if (udma > 2)
			u_clock = 1;		/* 66Mhz */
		else
			u_clock = 0;		/* 33Mhz */

		udma_enable |= (1 << devid);

		/* Load the UDMA mode number */
		pci_read_config_word(dev, 0x4A, &udma_timing);
		udma_timing &= ~(3 << (4 * devid));
		udma_timing |= (udma & 3) << (4 * devid);
		pci_write_config_word(dev, 0x4A, udma_timing);

		/* Load the clock selection */
		pci_read_config_word(dev, 0x54, &ideconf);
		ideconf &= ~(0x1001 << devid);
		ideconf |= u_clock << devid;
		pci_write_config_word(dev, 0x54, ideconf);
	} else {
		/*
		 * MWDMA is driven by the PIO timings. We must also enable
		 * IORDY unconditionally along with TIME1. PPE has already
		 * been set when the PIO timing was set.
		 */
		unsigned int mwdma	= adev->dma_mode - XFER_MW_DMA_0;
		unsigned int control;
		u8 slave_data;
		static const unsigned int needed_pio[3] = {
			XFER_PIO_0, XFER_PIO_3, XFER_PIO_4
		};
		int pio = needed_pio[mwdma] - XFER_PIO_0;

		control = 3;	/* IORDY|TIME1 */

		/* If the drive MWDMA is faster than it can do PIO then
		   we must force PIO into PIO0 */

		if (adev->pio_mode < needed_pio[mwdma])
			/* Enable DMA timing only */
			control |= 8;	/* PIO cycles in PIO0 */

		if (devid) {	/* Slave */
			master_data &= 0xFF4F;  /* Mask out IORDY|TIME1|DMAONLY */
			master_data |= control << 4;
			pci_read_config_byte(dev, 0x44, &slave_data);
			slave_data &= (0x0F + 0xE1 * ap->port_no);
			/* Load the matching timing */
			slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) << (ap->port_no ? 4 : 0);
			pci_write_config_byte(dev, 0x44, slave_data);
		} else { 	/* Master */
			master_data &= 0xCCF4;	/* Mask out IORDY|TIME1|DMAONLY
						   and master timing bits */
			master_data |= control;
			master_data |=
				(timings[pio][0] << 12) |
				(timings[pio][1] << 8);
		}
		udma_enable &= ~(1 << devid);
		pci_write_config_word(dev, 0x40, master_data);
	}
	pci_write_config_byte(dev, 0x48, udma_enable);
}

static struct scsi_host_template it8213_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations it8213_ops = {
	.set_piomode		= it8213_set_piomode,
	.set_dmamode		= it8213_set_dmamode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= it8213_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= it8213_cable_detect,

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

	.port_start		= ata_sff_port_start,
};


/**
 *	it8213_init_one - Register 8213 ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in it8213_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int it8213_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	static const struct ata_port_info info = {
		.sht		= &it8213_sht,
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask 	= ATA_UDMA4, /* FIXME: want UDMA 100? */
		.port_ops	= &it8213_ops,
	};
	/* Current IT8213 stuff is single port */
	const struct ata_port_info *ppi[] = { &info, &ata_dummy_port_info };

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	return ata_pci_init_one(pdev, ppi);
}

static const struct pci_device_id it8213_pci_tbl[] = {
	{ PCI_VDEVICE(ITE, PCI_DEVICE_ID_ITE_8213), },

	{ }	/* terminate list */
};

static struct pci_driver it8213_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= it8213_pci_tbl,
	.probe			= it8213_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
#endif
};

static int __init it8213_init(void)
{
	return pci_register_driver(&it8213_pci_driver);
}

static void __exit it8213_exit(void)
{
	pci_unregister_driver(&it8213_pci_driver);
}

module_init(it8213_init);
module_exit(it8213_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for the ITE 8213");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, it8213_pci_tbl);
MODULE_VERSION(DRV_VERSION);
