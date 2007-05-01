/*
 * Generic platform device PATA driver
 *
 * Copyright (C) 2006  Paul Mundt
 *
 * Based on pata_pcmcia:
 *
 *   Copyright 2005-2006 Red Hat Inc <alan@redhat.com>, all rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <scsi/scsi_host.h>
#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/pata_platform.h>

#define DRV_NAME "pata_platform"
#define DRV_VERSION "0.1.2"

static int pio_mask = 1;

/*
 * Provide our own set_mode() as we don't want to change anything that has
 * already been configured..
 */
static int pata_platform_set_mode(struct ata_port *ap, struct ata_device **unused)
{
	int i;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &ap->device[i];

		if (ata_dev_enabled(dev)) {
			/* We don't really care */
			dev->pio_mode = dev->xfer_mode = XFER_PIO_0;
			dev->xfer_shift = ATA_SHIFT_PIO;
			dev->flags |= ATA_DFLAG_PIO;
			ata_dev_printk(dev, KERN_INFO, "configured for PIO\n");
		}
	}
	return 0;
}

static struct scsi_host_template pata_platform_sht = {
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

static struct ata_port_operations pata_platform_port_ops = {
	.set_mode		= pata_platform_set_mode,

	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= ata_bmdma_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= ata_cable_unknown,

	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,

	.data_xfer		= ata_data_xfer_noirq,

	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};

static void pata_platform_setup_port(struct ata_ioports *ioaddr,
				     struct pata_platform_info *info)
{
	unsigned int shift = 0;

	/* Fixup the port shift for platforms that need it */
	if (info && info->ioport_shift)
		shift = info->ioport_shift;

	ioaddr->data_addr	= ioaddr->cmd_addr + (ATA_REG_DATA    << shift);
	ioaddr->error_addr	= ioaddr->cmd_addr + (ATA_REG_ERR     << shift);
	ioaddr->feature_addr	= ioaddr->cmd_addr + (ATA_REG_FEATURE << shift);
	ioaddr->nsect_addr	= ioaddr->cmd_addr + (ATA_REG_NSECT   << shift);
	ioaddr->lbal_addr	= ioaddr->cmd_addr + (ATA_REG_LBAL    << shift);
	ioaddr->lbam_addr	= ioaddr->cmd_addr + (ATA_REG_LBAM    << shift);
	ioaddr->lbah_addr	= ioaddr->cmd_addr + (ATA_REG_LBAH    << shift);
	ioaddr->device_addr	= ioaddr->cmd_addr + (ATA_REG_DEVICE  << shift);
	ioaddr->status_addr	= ioaddr->cmd_addr + (ATA_REG_STATUS  << shift);
	ioaddr->command_addr	= ioaddr->cmd_addr + (ATA_REG_CMD     << shift);
}

/**
 *	pata_platform_probe		-	attach a platform interface
 *	@pdev: platform device
 *
 *	Register a platform bus IDE interface. Such interfaces are PIO and we
 *	assume do not support IRQ sharing.
 *
 *	Platform devices are expected to contain 3 resources per port:
 *
 *		- I/O Base (IORESOURCE_IO or IORESOURCE_MEM)
 *		- CTL Base (IORESOURCE_IO or IORESOURCE_MEM)
 *		- IRQ	   (IORESOURCE_IRQ)
 *
 *	If the base resources are both mem types, the ioremap() is handled
 *	here. For IORESOURCE_IO, it's assumed that there's no remapping
 *	necessary.
 */
static int __devinit pata_platform_probe(struct platform_device *pdev)
{
	struct resource *io_res, *ctl_res;
	struct ata_host *host;
	struct ata_port *ap;
	unsigned int mmio;

	/*
	 * Simple resource validation ..
	 */
	if (unlikely(pdev->num_resources != 3)) {
		dev_err(&pdev->dev, "invalid number of resources\n");
		return -EINVAL;
	}

	/*
	 * Get the I/O base first
	 */
	io_res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (io_res == NULL) {
		io_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (unlikely(io_res == NULL))
			return -EINVAL;
	}

	/*
	 * Then the CTL base
	 */
	ctl_res = platform_get_resource(pdev, IORESOURCE_IO, 1);
	if (ctl_res == NULL) {
		ctl_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (unlikely(ctl_res == NULL))
			return -EINVAL;
	}

	/*
	 * Check for MMIO
	 */
	mmio = (( io_res->flags == IORESOURCE_MEM) &&
		(ctl_res->flags == IORESOURCE_MEM));

	/*
	 * Now that that's out of the way, wire up the port..
	 */
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		return -ENOMEM;
	ap = host->ports[0];

	ap->ops = &pata_platform_port_ops;
	ap->pio_mask = pio_mask;
	ap->flags |= ATA_FLAG_SLAVE_POSS;

	/*
	 * Handle the MMIO case
	 */
	if (mmio) {
		ap->ioaddr.cmd_addr = devm_ioremap(&pdev->dev, io_res->start,
				io_res->end - io_res->start + 1);
		ap->ioaddr.ctl_addr = devm_ioremap(&pdev->dev, ctl_res->start,
				ctl_res->end - ctl_res->start + 1);
	} else {
		ap->ioaddr.cmd_addr = devm_ioport_map(&pdev->dev, io_res->start,
				io_res->end - io_res->start + 1);
		ap->ioaddr.ctl_addr = devm_ioport_map(&pdev->dev, ctl_res->start,
				ctl_res->end - ctl_res->start + 1);
	}
	if (!ap->ioaddr.cmd_addr || !ap->ioaddr.ctl_addr) {
		dev_err(&pdev->dev, "failed to map IO/CTL base\n");
		return -ENOMEM;
	}

	ap->ioaddr.altstatus_addr = ap->ioaddr.ctl_addr;

	pata_platform_setup_port(&ap->ioaddr, pdev->dev.platform_data);

	/* activate */
	return ata_host_activate(host, platform_get_irq(pdev, 0), ata_interrupt,
				 0, &pata_platform_sht);
}

/**
 *	pata_platform_remove	-	unplug a platform interface
 *	@pdev: platform device
 *
 *	A platform bus ATA device has been unplugged. Perform the needed
 *	cleanup. Also called on module unload for any active devices.
 */
static int __devexit pata_platform_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ata_host *host = dev_get_drvdata(dev);

	ata_host_detach(host);

	return 0;
}

static struct platform_driver pata_platform_driver = {
	.probe		= pata_platform_probe,
	.remove		= __devexit_p(pata_platform_remove),
	.driver = {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
	},
};

static int __init pata_platform_init(void)
{
	return platform_driver_register(&pata_platform_driver);
}

static void __exit pata_platform_exit(void)
{
	platform_driver_unregister(&pata_platform_driver);
}
module_init(pata_platform_init);
module_exit(pata_platform_exit);

module_param(pio_mask, int, 0);

MODULE_AUTHOR("Paul Mundt");
MODULE_DESCRIPTION("low-level driver for platform device ATA");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
