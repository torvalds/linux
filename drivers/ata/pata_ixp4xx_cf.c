/*
 * ixp4xx PATA/Compact Flash driver
 * Copyright (c) 2006 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * An ATA driver to handle a Compact Flash connected
 * to the ixp4xx expansion bus in TrueIDE mode. The CF
 * must have it chip selects connected to two CS lines
 * on the ixp4xx. The interrupt line is optional, if not
 * specified the driver will run in polling mode.
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
#define DRV_VERSION	"0.1.1"

static void ixp4xx_set_mode(struct ata_port *ap)
{
	int i;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &ap->device[i];
		if (ata_dev_enabled(dev)) {
			dev->pio_mode = XFER_PIO_0;
			dev->xfer_mode = XFER_PIO_0;
			dev->xfer_shift = ATA_SHIFT_PIO;
			dev->flags |= ATA_DFLAG_PIO;
		}
	}
}

static void ixp4xx_phy_reset(struct ata_port *ap)
{
	ap->cbl = ATA_CBL_PATA40;
	ata_port_probe(ap);
	ata_bus_reset(ap);
}

static void ixp4xx_mmio_data_xfer(struct ata_device *adev, unsigned char *buf,
				unsigned int buflen, int write_data)
{
	unsigned int i;
	unsigned int words = buflen >> 1;
	u16 *buf16 = (u16 *) buf;
	struct ata_port *ap = adev->ap;
	void __iomem *mmio = (void __iomem *)ap->ioaddr.data_addr;
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

static void ixp4xx_irq_clear(struct ata_port *ap)
{
}

static void ixp4xx_host_stop (struct ata_host *host)
{
	struct ixp4xx_pata_data *data = host->dev->platform_data;

	iounmap(data->cs0);
	iounmap(data->cs1);
}

static struct scsi_host_template ixp4xx_sht = {
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
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations ixp4xx_port_ops = {
	.set_mode	= ixp4xx_set_mode,
	.mode_filter	= ata_pci_default_filter,

	.port_disable	= ata_port_disable,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,
	.eng_timeout	= ata_eng_timeout,
	.data_xfer	= ixp4xx_mmio_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ixp4xx_irq_clear,

	.port_start	= ata_port_start,
	.port_stop	= ata_port_stop,
	.host_stop	= ixp4xx_host_stop,

	.phy_reset	= ixp4xx_phy_reset,
};

static void ixp4xx_setup_port(struct ata_ioports *ioaddr,
				struct ixp4xx_pata_data *data)
{
	ioaddr->cmd_addr	= (unsigned long) data->cs0;
	ioaddr->altstatus_addr	= (unsigned long) data->cs1 + 0x06;
	ioaddr->ctl_addr	= (unsigned long) data->cs1 + 0x06;

	ata_std_ports(ioaddr);

#ifndef __ARMEB__

	/* adjust the addresses to handle the address swizzling of the
	 * ixp4xx in little endian mode.
	 */

	ioaddr->data_addr	^= 0x02;
	ioaddr->cmd_addr	^= 0x03;
	ioaddr->altstatus_addr	^= 0x03;
	ioaddr->ctl_addr	^= 0x03;
	ioaddr->error_addr	^= 0x03;
	ioaddr->feature_addr	^= 0x03;
	ioaddr->nsect_addr	^= 0x03;
	ioaddr->lbal_addr 	^= 0x03;
	ioaddr->lbam_addr	^= 0x03;
	ioaddr->lbah_addr	^= 0x03;
	ioaddr->device_addr	^= 0x03;
	ioaddr->status_addr	^= 0x03;
	ioaddr->command_addr	^= 0x03;
#endif
}

static __devinit int ixp4xx_pata_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int irq;
	struct resource *cs0, *cs1;
	struct ata_probe_ent ae;

	struct ixp4xx_pata_data *data = pdev->dev.platform_data;

	cs0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cs1 = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	if (!cs0 || !cs1)
		return -EINVAL;

	pdev->dev.coherent_dma_mask = DMA_32BIT_MASK;

	data->cs0 = ioremap(cs0->start, 0x1000);
	data->cs1 = ioremap(cs1->start, 0x1000);

	irq = platform_get_irq(pdev, 0);
	if (irq)
		set_irq_type(irq, IRQT_HIGH);

	/* Setup expansion bus chip selects */
	*data->cs0_cfg = data->cs0_bits;
	*data->cs1_cfg = data->cs1_bits;

	memset(&ae, 0, sizeof(struct ata_probe_ent));
	INIT_LIST_HEAD(&ae.node);

	ae.dev		= &pdev->dev;
	ae.port_ops	= &ixp4xx_port_ops;
	ae.sht		= &ixp4xx_sht;
	ae.n_ports	= 1;
	ae.pio_mask	= 0x1f; /* PIO4 */
	ae.irq		= irq;
	ae.irq_flags	= 0;
	ae.port_flags	= ATA_FLAG_MMIO | ATA_FLAG_NO_LEGACY
			| ATA_FLAG_NO_ATAPI | ATA_FLAG_SRST;

	/* run in polling mode if no irq has been assigned */
	if (!irq)
		ae.port_flags |= ATA_FLAG_PIO_POLLING;

	ixp4xx_setup_port(&ae.port[0], data);

	dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	ret = ata_device_add(&ae);
	if (ret == 0)
		return -ENODEV;

	return 0;
}

static __devexit int ixp4xx_pata_remove(struct platform_device *dev)
{
	struct ata_host *host = platform_get_drvdata(dev);

	ata_host_remove(host);
	platform_set_drvdata(dev, NULL);

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
