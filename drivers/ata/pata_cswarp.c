// SPDX-License-Identifier: GPL-2.0

/*
 * Amiga CS Warp PATA controller driver
 *
 * Copyright (c) 2024 CS-Lab s.c.
 *		http://www.cs-lab.eu
 *
 * Based on pata_gayle.c, pata_buddha.c and warpATA.device:
 *
 *     Created 2 Jun 2024 by Andrzej Rogozynski
 */

#include <linux/ata.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device-id/zorro.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/libata.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/zorro.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>

#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amigayle.h>
#include <asm/setup.h>

#define DRV_NAME "pata_cswarp"

#define WARP_OFFSET_ATA         0x6000

static const struct scsi_host_template pata_cswarp_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static unsigned int pata_cswarp_data_xfer(struct ata_queued_cmd *qc,
					  unsigned char *buf,
					  unsigned int buflen, int rw)
{
	struct ata_device *dev = qc->dev;
	struct ata_port *ap = dev->link->ap;
	void __iomem *data_addr = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 1;
	u16 *buf16 = (u16 *)buf;

	/* Transfer multiple of 2 bytes */
	if (rw == READ)
		raw_insw(data_addr, buf16, words);
	else
		raw_outsw(data_addr, buf16, words);

	/* Transfer trailing byte, if any. */
	if (unlikely(buflen & 0x01)) {
		if (rw == READ)
			buf[buflen - 1] = raw_inw(data_addr) >> 8;
		else
			raw_outw(buf[buflen - 1] << 8, data_addr);
		words++;
	}

	return words << 1;
}

/*
 * Provide our own set_mode() as we don't want to change anything that has
 * already been configured..
 */
static int pata_cswarp_set_mode(struct ata_link *link,
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

static struct ata_port_operations pata_cswarp_ops = {
	.inherits	= &ata_sff_port_ops,
	.sff_data_xfer	= pata_cswarp_data_xfer,
	.cable_detect	= ata_cable_unknown,
	.set_mode	= pata_cswarp_set_mode,
};

static int pata_cswarp_probe(struct zorro_dev *z,
			     const struct zorro_device_id *ent)
{
	static const char board_name[] = "csWarp";
	struct ata_host *host;
	struct ata_port *ap;
	struct resource *res;
	void __iomem *base;
	unsigned long board = z->resource.start;

	dev_info(&z->dev, "%s IDE controller (board: 0x%lx)\n", board_name,
		 board);

	res = devm_request_mem_region(&z->dev, board + WARP_OFFSET_ATA, 0x1800,
				      DRV_NAME);
	if (!res)
		return -ENXIO;

	host = ata_host_alloc(&z->dev, 1);
	if (!host)
		return -ENXIO;

	ap = host->ports[0];
	base = devm_ioremap(&z->dev, board + WARP_OFFSET_ATA, 0x1800);
	if (!base)
		return -ENOMEM;

	ap->ops = &pata_cswarp_ops;

	ap->pio_mask = ATA_PIO4;
	ap->flags |= ATA_FLAG_SLAVE_POSS | ATA_FLAG_NO_IORDY |
		ATA_FLAG_PIO_POLLING;

	ap->ioaddr.data_addr		= base;
	ap->ioaddr.error_addr		= base + 1 * 4;
	ap->ioaddr.feature_addr		= base + 1 * 4;
	ap->ioaddr.nsect_addr		= base + 2 * 4;
	ap->ioaddr.lbal_addr		= base + 3 * 4;
	ap->ioaddr.lbam_addr		= base + 4 * 4;
	ap->ioaddr.lbah_addr		= base + 5 * 4;
	ap->ioaddr.device_addr		= base + 6 * 4;
	ap->ioaddr.status_addr		= base + 7 * 4;
	ap->ioaddr.command_addr		= base + 7 * 4;

	ap->ioaddr.altstatus_addr	= base + (0x1000 | (6UL << 2));
	ap->ioaddr.ctl_addr		= base + (0x1000 | (6UL << 2));

	ata_port_desc(ap, "mmio %pR", res);

	return ata_host_activate(host, 0, NULL,
			  IRQF_SHARED, &pata_cswarp_sht);
}

static void pata_cswarp_remove(struct zorro_dev *z)
{
	struct ata_host *host = dev_get_drvdata(&z->dev);

	ata_host_detach(host);
}

static const struct zorro_device_id pata_cswarp_zorro_tbl[] = {
	{ .id = ZORRO_PROD_CSLAB_WARP_1260 },
	{ }
};
MODULE_DEVICE_TABLE(zorro, pata_cswarp_zorro_tbl);

static struct zorro_driver pata_cswarp_driver = {
	.name           = "pata_cswarp",
	.id_table       = pata_cswarp_zorro_tbl,
	.probe          = pata_cswarp_probe,
	.remove         = pata_cswarp_remove,
};

static int __init pata_cswarp_init(void)
{
	return zorro_register_driver(&pata_cswarp_driver);
}

static void __exit pata_cswarp_unregister(void)
{
	zorro_unregister_driver(&pata_cswarp_driver);
}

module_init(pata_cswarp_init);
module_exit(pata_cswarp_unregister);

MODULE_AUTHOR("Andrzej Rogozynski");
MODULE_DESCRIPTION("low-level driver for CSWarp PATA");
MODULE_LICENSE("GPL");
