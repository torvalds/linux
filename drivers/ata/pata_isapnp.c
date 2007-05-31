
/*
 *   pata-isapnp.c - ISA PnP PATA controller driver.
 *   Copyright 2005/2006 Red Hat Inc <alan@redhat.com>, all rights reserved.
 *
 *   Based in part on ide-pnp.c by Andrey Panin <pazke@donpac.ru>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/isapnp.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/ata.h>
#include <linux/libata.h>

#define DRV_NAME "pata_isapnp"
#define DRV_VERSION "0.2.1"

static struct scsi_host_template isapnp_sht = {
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

static struct ata_port_operations isapnp_port_ops = {
	.port_disable	= ata_port_disable,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

/**
 *	isapnp_init_one		-	attach an isapnp interface
 *	@idev: PnP device
 *	@dev_id: matching detect line
 *
 *	Register an ISA bus IDE interface. Such interfaces are PIO 0 and
 *	non shared IRQ.
 */

static int isapnp_init_one(struct pnp_dev *idev, const struct pnp_device_id *dev_id)
{
	struct ata_host *host;
	struct ata_port *ap;
	void __iomem *cmd_addr, *ctl_addr;
	int rc;

	if (pnp_port_valid(idev, 0) == 0)
		return -ENODEV;

	/* FIXME: Should selected polled PIO here not fail */
	if (pnp_irq_valid(idev, 0) == 0)
		return -ENODEV;

	/* allocate host */
	host = ata_host_alloc(&idev->dev, 1);
	if (!host)
		return -ENOMEM;

	/* acquire resources and fill host */
	cmd_addr = devm_ioport_map(&idev->dev, pnp_port_start(idev, 0), 8);
	if (!cmd_addr)
		return -ENOMEM;

	ap = host->ports[0];

	ap->ops = &isapnp_port_ops;
	ap->pio_mask = 1;
	ap->flags |= ATA_FLAG_SLAVE_POSS;

	ap->ioaddr.cmd_addr = cmd_addr;

	if (pnp_port_valid(idev, 1) == 0) {
		ctl_addr = devm_ioport_map(&idev->dev,
					   pnp_port_start(idev, 1), 1);
		ap->ioaddr.altstatus_addr = ctl_addr;
		ap->ioaddr.ctl_addr = ctl_addr;
	}

	ata_std_ports(&ap->ioaddr);

	/* activate */
	return ata_host_activate(host, pnp_irq(idev, 0), ata_interrupt, 0,
				 &isapnp_sht);
}

/**
 *	isapnp_remove_one	-	unplug an isapnp interface
 *	@idev: PnP device
 *
 *	Remove a previously configured PnP ATA port. Called only on module
 *	unload events as the core does not currently deal with ISAPnP docking.
 */

static void isapnp_remove_one(struct pnp_dev *idev)
{
	struct device *dev = &idev->dev;
	struct ata_host *host = dev_get_drvdata(dev);

	ata_host_detach(host);
}

static struct pnp_device_id isapnp_devices[] = {
  	/* Generic ESDI/IDE/ATA compatible hard disk controller */
	{.id = "PNP0600", .driver_data = 0},
	{.id = ""}
};

static struct pnp_driver isapnp_driver = {
	.name		= DRV_NAME,
	.id_table	= isapnp_devices,
	.probe		= isapnp_init_one,
	.remove		= isapnp_remove_one,
};

static int __init isapnp_init(void)
{
	return pnp_register_driver(&isapnp_driver);
}

static void __exit isapnp_exit(void)
{
	pnp_unregister_driver(&isapnp_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for ISA PnP ATA");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(isapnp_init);
module_exit(isapnp_exit);
