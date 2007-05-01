/*
 * pata_mpiix.c 	- Intel MPIIX PATA for new ATA layer
 *			  (C) 2005-2006 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 * The MPIIX is different enough to the PIIX4 and friends that we give it
 * a separate driver. The old ide/pci code handles this by just not tuning
 * MPIIX at all.
 *
 * The MPIIX also differs in another important way from the majority of PIIX
 * devices. The chip is a bridge (pardon the pun) between the old world of
 * ISA IDE and PCI IDE. Although the ATA timings are PCI configured the actual
 * IDE controller is not decoded in PCI space and the chip does not claim to
 * be IDE class PCI. This requires slightly non-standard probe logic compared
 * with PCI IDE and also that we do not disable the device when our driver is
 * unloaded (as it has many other functions).
 *
 * The driver conciously keeps this logic internally to avoid pushing quirky
 * PATA history into the clean libata layer.
 *
 * Thinkpad specific note: If you boot an MPIIX using a thinkpad with a PCMCIA
 * hard disk present this driver will not detect it. This is not a bug. In this
 * configuration the secondary port of the MPIIX is disabled and the addresses
 * are decoded by the PCMCIA bridge and therefore are for a generic IDE driver
 * to operate.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_mpiix"
#define DRV_VERSION "0.7.6"

enum {
	IDETIM = 0x6C,		/* IDE control register */
	IORDY = (1 << 1),
	PPE = (1 << 2),
	FTIM = (1 << 0),
	ENABLED = (1 << 15),
	SECONDARY = (1 << 14)
};

static int mpiix_pre_reset(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits mpiix_enable_bits = { 0x6D, 1, 0x80, 0x80 };

	if (!pci_test_config_bits(pdev, &mpiix_enable_bits))
		return -ENOENT;
	return ata_std_prereset(ap);
}

/**
 *	mpiix_error_handler		-	probe reset
 *	@ap: ATA port
 *
 *	Perform the ATA probe and bus reset sequence plus specific handling
 *	for this hardware. The MPIIX has the enable bits in a different place
 *	to PIIX4 and friends. As a pure PIO device it has no cable detect
 */

static void mpiix_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, mpiix_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	mpiix_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup. The MPIIX allows us to program the
 *	IORDY sample point (2-5 clocks), recovery (1-4 clocks) and whether
 *	prefetching or IORDY are used.
 *
 *	This would get very ugly because we can only program timing for one
 *	device at a time, the other gets PIO0. Fortunately libata calls
 *	our qc_issue_prot command before a command is issued so we can
 *	flip the timings back and forth to reduce the pain.
 */

static void mpiix_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	int control = 0;
	int pio = adev->pio_mode - XFER_PIO_0;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u16 idetim;
	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(pdev, IDETIM, &idetim);

	/* Mask the IORDY/TIME/PPE for this device */
	if (adev->class == ATA_DEV_ATA)
		control |= PPE;		/* Enable prefetch/posting for disk */
	if (ata_pio_need_iordy(adev))
		control |= IORDY;
	if (pio > 1)
		control |= FTIM;	/* This drive is on the fast timing bank */

	/* Mask out timing and clear both TIME bank selects */
	idetim &= 0xCCEE;
	idetim &= ~(0x07  << (4 * adev->devno));
	idetim |= control << (4 * adev->devno);

	idetim |= (timings[pio][0] << 12) | (timings[pio][1] << 8);
	pci_write_config_word(pdev, IDETIM, idetim);

	/* We use ap->private_data as a pointer to the device currently
	   loaded for timing */
	ap->private_data = adev;
}

/**
 *	mpiix_qc_issue_prot	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	neccessary. Our logic also clears TIME0/TIME1 for the other device so
 *	that, even if we get this wrong, cycles to the other device will
 *	be made PIO0.
 */

static unsigned int mpiix_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;

	/* If modes have been configured and the channel data is not loaded
	   then load it. We have to check if pio_mode is set as the core code
	   does not set adev->pio_mode to XFER_PIO_0 while probing as would be
	   logical */

	if (adev->pio_mode && adev != ap->private_data)
		mpiix_set_piomode(ap, adev);

	return ata_qc_issue_prot(qc);
}

static struct scsi_host_template mpiix_sht = {
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
#ifdef CONFIG_PM
	.resume			= ata_scsi_device_resume,
	.suspend		= ata_scsi_device_suspend,
#endif
};

static struct ata_port_operations mpiix_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= mpiix_set_piomode,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= mpiix_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= mpiix_qc_issue_prot,
	.data_xfer	= ata_data_xfer,

	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

static int mpiix_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	/* Single threaded by the PCI probe logic */
	static int printed_version;
	struct ata_host *host;
	struct ata_port *ap;
	void __iomem *cmd_addr, *ctl_addr;
	u16 idetim;
	int irq;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &dev->dev, "version " DRV_VERSION "\n");

	host = ata_host_alloc(&dev->dev, 1);
	if (!host)
		return -ENOMEM;

	/* MPIIX has many functions which can be turned on or off according
	   to other devices present. Make sure IDE is enabled before we try
	   and use it */

	pci_read_config_word(dev, IDETIM, &idetim);
	if (!(idetim & ENABLED))
		return -ENODEV;

	/* See if it's primary or secondary channel... */
	if (!(idetim & SECONDARY)) {
		irq = 14;
		cmd_addr = devm_ioport_map(&dev->dev, 0x1F0, 8);
		ctl_addr = devm_ioport_map(&dev->dev, 0x3F6, 1);
	} else {
		irq = 15;
		cmd_addr = devm_ioport_map(&dev->dev, 0x170, 8);
		ctl_addr = devm_ioport_map(&dev->dev, 0x376, 1);
	}

	if (!cmd_addr || !ctl_addr)
		return -ENOMEM;

	/* We do our own plumbing to avoid leaking special cases for whacko
	   ancient hardware into the core code. There are two issues to
	   worry about.  #1 The chip is a bridge so if in legacy mode and
	   without BARs set fools the setup.  #2 If you pci_disable_device
	   the MPIIX your box goes castors up */

	ap = host->ports[0];
	ap->ops = &mpiix_port_ops;
	ap->pio_mask = 0x1F;
	ap->flags |= ATA_FLAG_SLAVE_POSS;

	ap->ioaddr.cmd_addr = cmd_addr;
	ap->ioaddr.ctl_addr = ctl_addr;
	ap->ioaddr.altstatus_addr = ctl_addr;

	/* Let libata fill in the port details */
	ata_std_ports(&ap->ioaddr);

	/* activate host */
	return ata_host_activate(host, irq, ata_interrupt, IRQF_SHARED,
				 &mpiix_sht);
}

static const struct pci_device_id mpiix[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82371MX), },

	{ },
};

static struct pci_driver mpiix_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= mpiix,
	.probe 		= mpiix_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

static int __init mpiix_init(void)
{
	return pci_register_driver(&mpiix_pci_driver);
}

static void __exit mpiix_exit(void)
{
	pci_unregister_driver(&mpiix_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Intel MPIIX");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, mpiix);
MODULE_VERSION(DRV_VERSION);

module_init(mpiix_init);
module_exit(mpiix_exit);
