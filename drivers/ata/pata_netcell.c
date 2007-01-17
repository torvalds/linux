/*
 *    pata_netcell.c - Netcell PATA driver
 *
 *	(c) 2006 Red Hat  <alan@redhat.com>
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

#define DRV_NAME	"pata_netcell"
#define DRV_VERSION	"0.1.6"

/**
 *	netcell_probe_init	-	check for 40/80 pin
 *	@ap: Port
 *
 *	Cables are handled by the RAID controller. Report 80 pin.
 */

static int netcell_pre_reset(struct ata_port *ap)
{
	ap->cbl = ATA_CBL_PATA80;
	return ata_std_prereset(ap);
}

/**
 *	netcell_probe_reset - Probe specified port on PATA host controller
 *	@ap: Port to probe
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void netcell_error_handler(struct ata_port *ap)
{
	return ata_bmdma_drive_eh(ap, netcell_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/* No PIO or DMA methods needed for this device */

static struct scsi_host_template netcell_sht = {
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
	/* Use standard CHS mapping rules */
	.bios_param		= ata_std_bios_param,
	.resume			= ata_scsi_device_resume,
	.suspend		= ata_scsi_device_suspend,
};

static const struct ata_port_operations netcell_ops = {
	.port_disable		= ata_port_disable,

	/* Task file is PCI ATA format, use helpers */
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= netcell_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,

	/* BMDMA handling is PCI ATA format, use helpers */
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_pio_data_xfer,

	/* IRQ-related hooks */
	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,

	/* Generic PATA PCI ATA helpers */
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= ata_host_stop,
};


/**
 *	netcell_init_one - Register Netcell ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in netcell_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int netcell_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	static struct ata_port_info info = {
		.sht		= &netcell_sht,
		.flags		= ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		/* Actually we don't really care about these as the
		   firmware deals with it */
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask 	= 0x3f, /* UDMA 133 */
		.port_ops	= &netcell_ops,
	};
	static struct ata_port_info *port_info[2] = { &info, &info };

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	/* Any chip specific setup/optimisation/messages here */
	ata_pci_clear_simplex(pdev);

	/* And let the library code do the work */
	return ata_pci_init_one(pdev, port_info, 2);
}

static const struct pci_device_id netcell_pci_tbl[] = {
	{ PCI_VDEVICE(NETCELL, PCI_DEVICE_ID_REVOLUTION), },

	{ }	/* terminate list */
};

static struct pci_driver netcell_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= netcell_pci_tbl,
	.probe			= netcell_init_one,
	.remove			= ata_pci_remove_one,
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
};

static int __init netcell_init(void)
{
	return pci_register_driver(&netcell_pci_driver);
}

static void __exit netcell_exit(void)
{
	pci_unregister_driver(&netcell_pci_driver);
}

module_init(netcell_init);
module_exit(netcell_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for Netcell PATA RAID");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, netcell_pci_tbl);
MODULE_VERSION(DRV_VERSION);

