// SPDX-License-Identifier: GPL-2.0

/*
 * Buddha, Catweasel and X-Surf PATA controller driver
 *
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Based on buddha.c:
 *
 *	Copyright (C) 1997, 2001 by Geert Uytterhoeven and others
 */

#include <linux/ata.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/libata.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/zorro.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>

#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/ide.h>
#include <asm/setup.h>

#define DRV_NAME "pata_buddha"
#define DRV_VERSION "0.1.0"

#define BUDDHA_BASE1	0x800
#define BUDDHA_BASE2	0xa00
#define BUDDHA_BASE3	0xc00
#define XSURF_BASE1	0xb000 /* 2.5" interface */
#define XSURF_BASE2	0xd000 /* 3.5" interface */
#define BUDDHA_CONTROL	0x11a
#define BUDDHA_IRQ	0xf00
#define XSURF_IRQ	0x7e
#define BUDDHA_IRQ_MR	0xfc0	/* master interrupt enable */

enum {
	BOARD_BUDDHA = 0,
	BOARD_CATWEASEL,
	BOARD_XSURF
};

static unsigned int buddha_bases[3] __initdata = {
	BUDDHA_BASE1, BUDDHA_BASE2, BUDDHA_BASE3
};

static unsigned int xsurf_bases[2] __initdata = {
	XSURF_BASE1, XSURF_BASE2
};

static struct scsi_host_template pata_buddha_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

/* FIXME: is this needed? */
static unsigned int pata_buddha_data_xfer(struct ata_queued_cmd *qc,
					 unsigned char *buf,
					 unsigned int buflen, int rw)
{
	struct ata_device *dev = qc->dev;
	struct ata_port *ap = dev->link->ap;
	void __iomem *data_addr = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 1;

	/* Transfer multiple of 2 bytes */
	if (rw == READ)
		raw_insw((u16 *)data_addr, (u16 *)buf, words);
	else
		raw_outsw((u16 *)data_addr, (u16 *)buf, words);

	/* Transfer trailing byte, if any. */
	if (unlikely(buflen & 0x01)) {
		unsigned char pad[2] = { };

		/* Point buf to the tail of buffer */
		buf += buflen - 1;

		if (rw == READ) {
			raw_insw((u16 *)data_addr, (u16 *)pad, 1);
			*buf = pad[0];
		} else {
			pad[0] = *buf;
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
static int pata_buddha_set_mode(struct ata_link *link,
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

static bool pata_buddha_irq_check(struct ata_port *ap)
{
	u8 ch;

	ch = z_readb((unsigned long)ap->private_data);

	return !!(ch & 0x80);
}

static void pata_xsurf_irq_clear(struct ata_port *ap)
{
	z_writeb(0, (unsigned long)ap->private_data);
}

static struct ata_port_operations pata_buddha_ops = {
	.inherits	= &ata_sff_port_ops,
	.sff_data_xfer	= pata_buddha_data_xfer,
	.sff_irq_check	= pata_buddha_irq_check,
	.cable_detect	= ata_cable_unknown,
	.set_mode	= pata_buddha_set_mode,
};

static struct ata_port_operations pata_xsurf_ops = {
	.inherits	= &ata_sff_port_ops,
	.sff_data_xfer	= pata_buddha_data_xfer,
	.sff_irq_check	= pata_buddha_irq_check,
	.sff_irq_clear	= pata_xsurf_irq_clear,
	.cable_detect	= ata_cable_unknown,
	.set_mode	= pata_buddha_set_mode,
};

static int __init pata_buddha_init_one(void)
{
	struct zorro_dev *z = NULL;

	while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
		static const char *board_name[]
			= { "Buddha", "Catweasel", "X-Surf" };
		struct ata_host *host;
		void __iomem *buddha_board;
		unsigned long board;
		unsigned int type, nr_ports = 2;
		int i;

		if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_BUDDHA) {
			type = BOARD_BUDDHA;
		} else if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_CATWEASEL) {
			type = BOARD_CATWEASEL;
			nr_ports++;
		} else if (z->id == ZORRO_PROD_INDIVIDUAL_COMPUTERS_X_SURF) {
			type = BOARD_XSURF;
		} else
			continue;

		dev_info(&z->dev, "%s IDE controller\n", board_name[type]);

		board = z->resource.start;

		if (type != BOARD_XSURF) {
			if (!devm_request_mem_region(&z->dev,
						     board + BUDDHA_BASE1,
						     0x800, DRV_NAME))
				continue;
		} else {
			if (!devm_request_mem_region(&z->dev,
						     board + XSURF_BASE1,
						     0x1000, DRV_NAME))
				continue;
			if (!devm_request_mem_region(&z->dev,
						     board + XSURF_BASE2,
						     0x1000, DRV_NAME))
				continue;
		}

		/* allocate host */
		host = ata_host_alloc(&z->dev, nr_ports);
		if (!host)
			continue;

		buddha_board = ZTWO_VADDR(board);

		/* enable the board IRQ on Buddha/Catweasel */
		if (type != BOARD_XSURF)
			z_writeb(0, buddha_board + BUDDHA_IRQ_MR);

		for (i = 0; i < nr_ports; i++) {
			struct ata_port *ap = host->ports[i];
			void __iomem *base, *irqport;
			unsigned long ctl = 0;

			if (type != BOARD_XSURF) {
				ap->ops = &pata_buddha_ops;
				base = buddha_board + buddha_bases[i];
				ctl = BUDDHA_CONTROL;
				irqport = buddha_board + BUDDHA_IRQ + i * 0x40;
			} else {
				ap->ops = &pata_xsurf_ops;
				base = buddha_board + xsurf_bases[i];
				/* X-Surf has no CS1* (Control/AltStat) */
				irqport = buddha_board + XSURF_IRQ;
			}

			ap->pio_mask = ATA_PIO4;
			ap->flags |= ATA_FLAG_SLAVE_POSS | ATA_FLAG_NO_IORDY;

			ap->ioaddr.data_addr		= base;
			ap->ioaddr.error_addr		= base + 2 + 1 * 4;
			ap->ioaddr.feature_addr		= base + 2 + 1 * 4;
			ap->ioaddr.nsect_addr		= base + 2 + 2 * 4;
			ap->ioaddr.lbal_addr		= base + 2 + 3 * 4;
			ap->ioaddr.lbam_addr		= base + 2 + 4 * 4;
			ap->ioaddr.lbah_addr		= base + 2 + 5 * 4;
			ap->ioaddr.device_addr		= base + 2 + 6 * 4;
			ap->ioaddr.status_addr		= base + 2 + 7 * 4;
			ap->ioaddr.command_addr		= base + 2 + 7 * 4;

			if (ctl) {
				ap->ioaddr.altstatus_addr = base + ctl;
				ap->ioaddr.ctl_addr	  = base + ctl;
			}

			ap->private_data = (void *)irqport;

			ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx", board,
				      ctl ? board + buddha_bases[i] + ctl : 0);
		}

		ata_host_activate(host, IRQ_AMIGA_PORTS, ata_sff_interrupt,
				  IRQF_SHARED, &pata_buddha_sht);

	}

	return 0;
}

module_init(pata_buddha_init_one);

MODULE_AUTHOR("Bartlomiej Zolnierkiewicz");
MODULE_DESCRIPTION("low-level driver for Buddha/Catweasel/X-Surf PATA");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
