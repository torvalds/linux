// SPDX-License-Identifier: GPL-2.0

/*
 * Atari Falcon PATA controller driver
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Based on falconide.c:
 *
 *     Created 12 Jul 1997 by Geert Uytterhoeven
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#include <asm/ide.h>

#define DRV_NAME "pata_falcon"
#define DRV_VERSION "0.1.0"

static struct scsi_host_template pata_falcon_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static unsigned int pata_falcon_data_xfer(struct ata_queued_cmd *qc,
					  unsigned char *buf,
					  unsigned int buflen, int rw)
{
	struct ata_device *dev = qc->dev;
	struct ata_port *ap = dev->link->ap;
	void __iomem *data_addr = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 1;
	struct scsi_cmnd *cmd = qc->scsicmd;
	bool swap = 1;

	if (dev->class == ATA_DEV_ATA && cmd &&
	    !blk_rq_is_passthrough(scsi_cmd_to_rq(cmd)))
		swap = 0;

	/* Transfer multiple of 2 bytes */
	if (rw == READ) {
		if (swap)
			raw_insw_swapw((u16 *)data_addr, (u16 *)buf, words);
		else
			raw_insw((u16 *)data_addr, (u16 *)buf, words);
	} else {
		if (swap)
			raw_outsw_swapw((u16 *)data_addr, (u16 *)buf, words);
		else
			raw_outsw((u16 *)data_addr, (u16 *)buf, words);
	}

	/* Transfer trailing byte, if any. */
	if (unlikely(buflen & 0x01)) {
		unsigned char pad[2] = { };

		/* Point buf to the tail of buffer */
		buf += buflen - 1;

		if (rw == READ) {
			if (swap)
				raw_insw_swapw((u16 *)data_addr, (u16 *)pad, 1);
			else
				raw_insw((u16 *)data_addr, (u16 *)pad, 1);
			*buf = pad[0];
		} else {
			pad[0] = *buf;
			if (swap)
				raw_outsw_swapw((u16 *)data_addr, (u16 *)pad, 1);
			else
				raw_outsw((u16 *)data_addr, (u16 *)pad, 1);
		}
		words++;
	}

	return words << 1;
}

/*
 * Provide our own set_mode() as we don't want to change anything that has
 * already been configured..
 */
static int pata_falcon_set_mode(struct ata_link *link,
				struct ata_device **unused)
{
	struct ata_device *dev;

	ata_for_each_dev(dev, link, ENABLED) {
		/* We don't really care */
		dev->pio_mode = dev->xfer_mode = XFER_PIO_0;
		dev->xfer_shift = ATA_SHIFT_PIO;
		dev->flags |= ATA_DFLAG_PIO;
		ata_dev_info(dev, "configured for PIO\n");
	}
	return 0;
}

static struct ata_port_operations pata_falcon_ops = {
	.inherits	= &ata_sff_port_ops,
	.sff_data_xfer	= pata_falcon_data_xfer,
	.cable_detect	= ata_cable_unknown,
	.set_mode	= pata_falcon_set_mode,
};

static int __init pata_falcon_init_one(struct platform_device *pdev)
{
	struct resource *base_mem_res, *ctl_mem_res;
	struct resource *base_res, *ctl_res, *irq_res;
	struct ata_host *host;
	struct ata_port *ap;
	void __iomem *base;
	int irq = 0;

	dev_info(&pdev->dev, "Atari Falcon and Q40/Q60 PATA controller\n");

	base_res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (base_res && !devm_request_region(&pdev->dev, base_res->start,
					   resource_size(base_res), DRV_NAME)) {
		dev_err(&pdev->dev, "resources busy\n");
		return -EBUSY;
	}

	ctl_res = platform_get_resource(pdev, IORESOURCE_IO, 1);
	if (ctl_res && !devm_request_region(&pdev->dev, ctl_res->start,
					    resource_size(ctl_res), DRV_NAME)) {
		dev_err(&pdev->dev, "resources busy\n");
		return -EBUSY;
	}

	base_mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!base_mem_res)
		return -ENODEV;
	if (!devm_request_mem_region(&pdev->dev, base_mem_res->start,
				     resource_size(base_mem_res), DRV_NAME)) {
		dev_err(&pdev->dev, "resources busy\n");
		return -EBUSY;
	}

	ctl_mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!ctl_mem_res)
		return -ENODEV;

	/* allocate host */
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		return -ENOMEM;
	ap = host->ports[0];

	ap->ops = &pata_falcon_ops;
	ap->pio_mask = ATA_PIO4;
	ap->flags |= ATA_FLAG_SLAVE_POSS | ATA_FLAG_NO_IORDY;

	base = (void __iomem *)base_mem_res->start;
	/* N.B. this assumes data_addr will be used for word-sized I/O only */
	ap->ioaddr.data_addr		= base + 0 + 0 * 4;
	ap->ioaddr.error_addr		= base + 1 + 1 * 4;
	ap->ioaddr.feature_addr		= base + 1 + 1 * 4;
	ap->ioaddr.nsect_addr		= base + 1 + 2 * 4;
	ap->ioaddr.lbal_addr		= base + 1 + 3 * 4;
	ap->ioaddr.lbam_addr		= base + 1 + 4 * 4;
	ap->ioaddr.lbah_addr		= base + 1 + 5 * 4;
	ap->ioaddr.device_addr		= base + 1 + 6 * 4;
	ap->ioaddr.status_addr		= base + 1 + 7 * 4;
	ap->ioaddr.command_addr		= base + 1 + 7 * 4;

	base = (void __iomem *)ctl_mem_res->start;
	ap->ioaddr.altstatus_addr	= base + 1;
	ap->ioaddr.ctl_addr		= base + 1;

	ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx",
		      (unsigned long)base_mem_res->start,
		      (unsigned long)ctl_mem_res->start);

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq_res && irq_res->start > 0) {
		irq = irq_res->start;
	} else {
		ap->flags |= ATA_FLAG_PIO_POLLING;
		ata_port_desc(ap, "no IRQ, using PIO polling");
	}

	/* activate */
	return ata_host_activate(host, irq, irq ? ata_sff_interrupt : NULL,
				 IRQF_SHARED, &pata_falcon_sht);
}

static int __exit pata_falcon_remove_one(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);

	ata_host_detach(host);

	return 0;
}

static struct platform_driver pata_falcon_driver = {
	.remove = __exit_p(pata_falcon_remove_one),
	.driver   = {
		.name	= "atari-falcon-ide",
	},
};

module_platform_driver_probe(pata_falcon_driver, pata_falcon_init_one);

MODULE_AUTHOR("Bartlomiej Zolnierkiewicz");
MODULE_DESCRIPTION("low-level driver for Atari Falcon PATA");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:atari-falcon-ide");
MODULE_VERSION(DRV_VERSION);
