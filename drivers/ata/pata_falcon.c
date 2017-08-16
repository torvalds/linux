/*
 * Atari Falcon PATA controller driver
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Based on falconide.c:
 *
 *     Created 12 Jul 1997 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
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

#define ATA_HD_BASE	0xfff00000
#define ATA_HD_CONTROL	0x39

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

	if (dev->class == ATA_DEV_ATA && cmd && cmd->request &&
	    !blk_rq_is_passthrough(cmd->request))
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

static int pata_falcon_init_one(void)
{
	struct ata_host *host;
	struct ata_port *ap;
	struct platform_device *pdev;
	void __iomem *base;

	if (!MACH_IS_ATARI || !ATARIHW_PRESENT(IDE))
		return -ENODEV;

	pr_info(DRV_NAME ": Atari Falcon PATA controller\n");

	pdev = platform_device_register_simple(DRV_NAME, 0, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	if (!devm_request_mem_region(&pdev->dev, ATA_HD_BASE, 0x40, DRV_NAME)) {
		pr_err(DRV_NAME ": resources busy\n");
		return -EBUSY;
	}

	/* allocate host */
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		return -ENOMEM;
	ap = host->ports[0];

	ap->ops = &pata_falcon_ops;
	ap->pio_mask = ATA_PIO4;
	ap->flags |= ATA_FLAG_SLAVE_POSS | ATA_FLAG_NO_IORDY;
	ap->flags |= ATA_FLAG_PIO_POLLING;

	base = (void __iomem *)ATA_HD_BASE;
	ap->ioaddr.data_addr		= base;
	ap->ioaddr.error_addr		= base + 1 + 1 * 4;
	ap->ioaddr.feature_addr		= base + 1 + 1 * 4;
	ap->ioaddr.nsect_addr		= base + 1 + 2 * 4;
	ap->ioaddr.lbal_addr		= base + 1 + 3 * 4;
	ap->ioaddr.lbam_addr		= base + 1 + 4 * 4;
	ap->ioaddr.lbah_addr		= base + 1 + 5 * 4;
	ap->ioaddr.device_addr		= base + 1 + 6 * 4;
	ap->ioaddr.status_addr		= base + 1 + 7 * 4;
	ap->ioaddr.command_addr		= base + 1 + 7 * 4;

	ap->ioaddr.altstatus_addr	= base + ATA_HD_CONTROL;
	ap->ioaddr.ctl_addr		= base + ATA_HD_CONTROL;

	ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx", (unsigned long)base,
		      (unsigned long)base + ATA_HD_CONTROL);

	/* activate */
	return ata_host_activate(host, 0, NULL, 0, &pata_falcon_sht);
}

module_init(pata_falcon_init_one);

MODULE_AUTHOR("Bartlomiej Zolnierkiewicz");
MODULE_DESCRIPTION("low-level driver for Atari Falcon PATA");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
