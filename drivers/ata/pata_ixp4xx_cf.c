/*
 * ixp4xx PATA/Compact Flash driver
 * Copyright (C) 2006-07 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * An ATA driver to handle a Compact Flash connected
 * to the ixp4xx expansion bus in TrueIDE mode. The CF
 * must have it chip selects connected to two CS lines
 * on the ixp4xx. In the irq is not available, you might
 * want to modify both this driver and libata to run in
 * polling mode.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/libata.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <scsi/scsi_host.h>

#define DRV_NAME	"pata_ixp4xx_cf"
#define DRV_VERSION	"0.2"

static int ixp4xx_set_mode(struct ata_link *link, struct ata_device **error)
{
	struct ata_device *dev;

	ata_link_for_each_dev(dev, link) {
		if (ata_dev_enabled(dev)) {
			ata_dev_printk(dev, KERN_INFO, "configured for PIO0\n");
			dev->pio_mode = XFER_PIO_0;
			dev->xfer_mode = XFER_PIO_0;
			dev->xfer_shift = ATA_SHIFT_PIO;
			dev->flags |= ATA_DFLAG_PIO;
		}
	}
	return 0;
}

static void ixp4xx_mmio_data_xfer(struct ata_device *adev, unsigned char *buf,
				unsigned int buflen, int write_data)
{
	unsigned int i;
	unsigned int words = buflen >> 1;
	u16 *buf16 = (u16 *) buf;
	struct ata_port *ap = adev->link->ap;
	void __iomem *mmio = ap->ioaddr.data_addr;
	struct ixp4xx_pata_data *data = ap->host->dev->platform_data;

	/* set the expansion bus in 16bit mode and restore
	 * 8 bit mode after the transaction.
	 */
	*data->cs0_cfg &= ~(0x01);
	udelay(100);

	/* Transfer multiple of 2 bytes */
	if (write_data) {
		for (i = 0; i < words; i++)
			writew(buf16[i], mmio);
	} else {
		for (i = 0; i < words; i++)
			buf16[i] = readw(mmio);
	}

	/* Transfer trailing 1 byte, if any. */
	if (unlikely(buflen & 0x01)) {
		u16 align_buf[1] = { 0 };
		unsigned char *trailing_buf = buf + buflen - 1;

		if (write_data) {
			memcpy(align_buf, trailing_buf, 1);
			writew(align_buf[0], mmio);
		} else {
			align_buf[0] = readw(mmio);
			memcpy(trailing_buf, align_buf, 1);
		}
	}

	udelay(100);
	*data->cs0_cfg |= 0x01;
}

static struct scsi_host_template ixp4xx_sht = {
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

static struct ata_port_operations ixp4xx_port_ops = {
	.set_mode		= ixp4xx_set_mode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status 		= ata_check_status,
	.dev_select 		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= ata_bmdma_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,

	.qc_prep 		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ixp4xx_mmio_data_xfer,
	.cable_detect		= ata_cable_40wire,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,

	.port_start		= ata_port_start,
};

static void ixp4xx_setup_port(struct ata_ioports *ioaddr,
			      struct ixp4xx_pata_data *data,
			      unsigned long raw_cs0, unsigned long raw_cs1)
{
	unsigned long raw_cmd = raw_cs0;
	unsigned long raw_ctl = raw_cs1 + 0x06;

	ioaddr->cmd_addr	= data->cs0;
	ioaddr->altstatus_addr	= data->cs1 + 0x06;
	ioaddr->ctl_addr	= data->cs1 + 0x06;

	ata_std_ports(ioaddr);

#ifndef __ARMEB__

	/* adjust the addresses to handle the address swizzling of the
	 * ixp4xx in little endian mode.
	 */

	*(unsigned long *)&ioaddr->data_addr		^= 0x02;
	*(unsigned long *)&ioaddr->cmd_addr		^= 0x03;
	*(unsigned long *)&ioaddr->altstatus_addr	^= 0x03;
	*(unsigned long *)&ioaddr->ctl_addr		^= 0x03;
	*(unsigned long *)&ioaddr->error_addr		^= 0x03;
	*(unsigned long *)&ioaddr->feature_addr		^= 0x03;
	*(unsigned long *)&ioaddr->nsect_addr		^= 0x03;
	*(unsigned long *)&ioaddr->lbal_addr		^= 0x03;
	*(unsigned long *)&ioaddr->lbam_addr		^= 0x03;
	*(unsigned long *)&ioaddr->lbah_addr		^= 0x03;
	*(unsigned long *)&ioaddr->device_addr		^= 0x03;
	*(unsigned long *)&ioaddr->status_addr		^= 0x03;
	*(unsigned long *)&ioaddr->command_addr		^= 0x03;

	raw_cmd ^= 0x03;
	raw_ctl ^= 0x03;
#endif

	ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx", raw_cmd, raw_ctl);
}

static __devinit int ixp4xx_pata_probe(struct platform_device *pdev)
{
	unsigned int irq;
	struct resource *cs0, *cs1;
	struct ata_host *host;
	struct ata_port *ap;
	struct ixp4xx_pata_data *data = pdev->dev.platform_data;

	cs0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cs1 = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	if (!cs0 || !cs1)
		return -EINVAL;

	/* allocate host */
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		return -ENOMEM;

	/* acquire resources and fill host */
	pdev->dev.coherent_dma_mask = DMA_32BIT_MASK;

	data->cs0 = devm_ioremap(&pdev->dev, cs0->start, 0x1000);
	data->cs1 = devm_ioremap(&pdev->dev, cs1->start, 0x1000);

	if (!data->cs0 || !data->cs1)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq)
		set_irq_type(irq, IRQT_RISING);

	/* Setup expansion bus chip selects */
	*data->cs0_cfg = data->cs0_bits;
	*data->cs1_cfg = data->cs1_bits;

	ap = host->ports[0];

	ap->ops	= &ixp4xx_port_ops;
	ap->pio_mask = 0x1f; /* PIO4 */
	ap->flags |= ATA_FLAG_MMIO | ATA_FLAG_NO_LEGACY | ATA_FLAG_NO_ATAPI;

	ixp4xx_setup_port(ap, data, cs0->start, cs1->start);

	dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	/* activate host */
	return ata_host_activate(host, irq, ata_interrupt, 0, &ixp4xx_sht);
}

static __devexit int ixp4xx_pata_remove(struct platform_device *dev)
{
	struct ata_host *host = platform_get_drvdata(dev);

	ata_host_detach(host);

	return 0;
}

static struct platform_driver ixp4xx_pata_platform_driver = {
	.driver	 = {
		.name   = DRV_NAME,
		.owner  = THIS_MODULE,
	},
	.probe		= ixp4xx_pata_probe,
	.remove		= __devexit_p(ixp4xx_pata_remove),
};

static int __init ixp4xx_pata_init(void)
{
	return platform_driver_register(&ixp4xx_pata_platform_driver);
}

static void __exit ixp4xx_pata_exit(void)
{
	platform_driver_unregister(&ixp4xx_pata_platform_driver);
}

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("low-level driver for ixp4xx Compact Flash PATA");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(ixp4xx_pata_init);
module_exit(ixp4xx_pata_exit);
