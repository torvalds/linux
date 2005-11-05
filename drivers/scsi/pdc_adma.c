/*
 *  pdc_adma.c - Pacific Digital Corporation ADMA
 *
 *  Maintained by:  Mark Lord <mlord@pobox.com>
 *
 *  Copyright 2005 Mark Lord
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *
 *  Supports ATA disks in single-packet ADMA mode.
 *  Uses PIO for everything else.
 *
 *  TODO:  Use ADMA transfers for ATAPI devices, when possible.
 *  This requires careful attention to a number of quirks of the chip.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/device.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <asm/io.h>
#include <linux/libata.h>

#define DRV_NAME	"pdc_adma"
#define DRV_VERSION	"0.03"

/* macro to calculate base address for ATA regs */
#define ADMA_ATA_REGS(base,port_no)	((base) + ((port_no) * 0x40))

/* macro to calculate base address for ADMA regs */
#define ADMA_REGS(base,port_no)	((base) + 0x80 + ((port_no) * 0x20))

enum {
	ADMA_PORTS		= 2,
	ADMA_CPB_BYTES		= 40,
	ADMA_PRD_BYTES		= LIBATA_MAX_PRD * 16,
	ADMA_PKT_BYTES		= ADMA_CPB_BYTES + ADMA_PRD_BYTES,

	ADMA_DMA_BOUNDARY	= 0xffffffff,

	/* global register offsets */
	ADMA_MODE_LOCK		= 0x00c7,

	/* per-channel register offsets */
	ADMA_CONTROL		= 0x0000, /* ADMA control */
	ADMA_STATUS		= 0x0002, /* ADMA status */
	ADMA_CPB_COUNT		= 0x0004, /* CPB count */
	ADMA_CPB_CURRENT	= 0x000c, /* current CPB address */
	ADMA_CPB_NEXT		= 0x000c, /* next CPB address */
	ADMA_CPB_LOOKUP		= 0x0010, /* CPB lookup table */
	ADMA_FIFO_IN		= 0x0014, /* input FIFO threshold */
	ADMA_FIFO_OUT		= 0x0016, /* output FIFO threshold */

	/* ADMA_CONTROL register bits */
	aNIEN			= (1 << 8), /* irq mask: 1==masked */
	aGO			= (1 << 7), /* packet trigger ("Go!") */
	aRSTADM			= (1 << 5), /* ADMA logic reset */
	aPIOMD4			= 0x0003,   /* PIO mode 4 */

	/* ADMA_STATUS register bits */
	aPSD			= (1 << 6),
	aUIRQ			= (1 << 4),
	aPERR			= (1 << 0),

	/* CPB bits */
	cDONE			= (1 << 0),
	cVLD			= (1 << 0),
	cDAT			= (1 << 2),
	cIEN			= (1 << 3),

	/* PRD bits */
	pORD			= (1 << 4),
	pDIRO			= (1 << 5),
	pEND			= (1 << 7),

	/* ATA register flags */
	rIGN			= (1 << 5),
	rEND			= (1 << 7),

	/* ATA register addresses */
	ADMA_REGS_CONTROL	= 0x0e,
	ADMA_REGS_SECTOR_COUNT	= 0x12,
	ADMA_REGS_LBA_LOW	= 0x13,
	ADMA_REGS_LBA_MID	= 0x14,
	ADMA_REGS_LBA_HIGH	= 0x15,
	ADMA_REGS_DEVICE	= 0x16,
	ADMA_REGS_COMMAND	= 0x17,

	/* PCI device IDs */
	board_1841_idx		= 0,	/* ADMA 2-port controller */
};

typedef enum { adma_state_idle, adma_state_pkt, adma_state_mmio } adma_state_t;

struct adma_port_priv {
	u8			*pkt;
	dma_addr_t		pkt_dma;
	adma_state_t		state;
};

static int adma_ata_init_one (struct pci_dev *pdev,
				const struct pci_device_id *ent);
static irqreturn_t adma_intr (int irq, void *dev_instance,
				struct pt_regs *regs);
static int adma_port_start(struct ata_port *ap);
static void adma_host_stop(struct ata_host_set *host_set);
static void adma_port_stop(struct ata_port *ap);
static void adma_phy_reset(struct ata_port *ap);
static void adma_qc_prep(struct ata_queued_cmd *qc);
static int adma_qc_issue(struct ata_queued_cmd *qc);
static int adma_check_atapi_dma(struct ata_queued_cmd *qc);
static void adma_bmdma_stop(struct ata_queued_cmd *qc);
static u8 adma_bmdma_status(struct ata_port *ap);
static void adma_irq_clear(struct ata_port *ap);
static void adma_eng_timeout(struct ata_port *ap);

static Scsi_Host_Template adma_ata_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ENABLE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ADMA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations adma_ata_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.check_atapi_dma	= adma_check_atapi_dma,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,
	.phy_reset		= adma_phy_reset,
	.qc_prep		= adma_qc_prep,
	.qc_issue		= adma_qc_issue,
	.eng_timeout		= adma_eng_timeout,
	.irq_handler		= adma_intr,
	.irq_clear		= adma_irq_clear,
	.port_start		= adma_port_start,
	.port_stop		= adma_port_stop,
	.host_stop		= adma_host_stop,
	.bmdma_stop		= adma_bmdma_stop,
	.bmdma_status		= adma_bmdma_status,
};

static struct ata_port_info adma_port_info[] = {
	/* board_1841_idx */
	{
		.sht		= &adma_ata_sht,
		.host_flags	= ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST |
				  ATA_FLAG_NO_LEGACY | ATA_FLAG_MMIO,
		.pio_mask	= 0x10, /* pio4 */
		.udma_mask	= 0x1f, /* udma0-4 */
		.port_ops	= &adma_ata_ops,
	},
};

static struct pci_device_id adma_ata_pci_tbl[] = {
	{ PCI_VENDOR_ID_PDC, 0x1841, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_1841_idx },

	{ }	/* terminate list */
};

static struct pci_driver adma_ata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= adma_ata_pci_tbl,
	.probe			= adma_ata_init_one,
	.remove			= ata_pci_remove_one,
};

static int adma_check_atapi_dma(struct ata_queued_cmd *qc)
{
	return 1;	/* ATAPI DMA not yet supported */
}

static void adma_bmdma_stop(struct ata_queued_cmd *qc)
{
	/* nothing */
}

static u8 adma_bmdma_status(struct ata_port *ap)
{
	return 0;
}

static void adma_irq_clear(struct ata_port *ap)
{
	/* nothing */
}

static void adma_reset_engine(void __iomem *chan)
{
	/* reset ADMA to idle state */
	writew(aPIOMD4 | aNIEN | aRSTADM, chan + ADMA_CONTROL);
	udelay(2);
	writew(aPIOMD4, chan + ADMA_CONTROL);
	udelay(2);
}

static void adma_reinit_engine(struct ata_port *ap)
{
	struct adma_port_priv *pp = ap->private_data;
	void __iomem *mmio_base = ap->host_set->mmio_base;
	void __iomem *chan = ADMA_REGS(mmio_base, ap->port_no);

	/* mask/clear ATA interrupts */
	writeb(ATA_NIEN, (void __iomem *)ap->ioaddr.ctl_addr);
	ata_check_status(ap);

	/* reset the ADMA engine */
	adma_reset_engine(chan);

	/* set in-FIFO threshold to 0x100 */
	writew(0x100, chan + ADMA_FIFO_IN);

	/* set CPB pointer */
	writel((u32)pp->pkt_dma, chan + ADMA_CPB_NEXT);

	/* set out-FIFO threshold to 0x100 */
	writew(0x100, chan + ADMA_FIFO_OUT);

	/* set CPB count */
	writew(1, chan + ADMA_CPB_COUNT);

	/* read/discard ADMA status */
	readb(chan + ADMA_STATUS);
}

static inline void adma_enter_reg_mode(struct ata_port *ap)
{
	void __iomem *chan = ADMA_REGS(ap->host_set->mmio_base, ap->port_no);

	writew(aPIOMD4, chan + ADMA_CONTROL);
	readb(chan + ADMA_STATUS);	/* flush */
}

static void adma_phy_reset(struct ata_port *ap)
{
	struct adma_port_priv *pp = ap->private_data;

	pp->state = adma_state_idle;
	adma_reinit_engine(ap);
	ata_port_probe(ap);
	ata_bus_reset(ap);
}

static void adma_eng_timeout(struct ata_port *ap)
{
	struct adma_port_priv *pp = ap->private_data;

	if (pp->state != adma_state_idle) /* healthy paranoia */
		pp->state = adma_state_mmio;
	adma_reinit_engine(ap);
	ata_eng_timeout(ap);
}

static int adma_fill_sg(struct ata_queued_cmd *qc)
{
	struct scatterlist *sg;
	struct ata_port *ap = qc->ap;
	struct adma_port_priv *pp = ap->private_data;
	u8  *buf = pp->pkt;
	int i = (2 + buf[3]) * 8;
	u8 pFLAGS = pORD | ((qc->tf.flags & ATA_TFLAG_WRITE) ? pDIRO : 0);

	ata_for_each_sg(sg, qc) {
		u32 addr;
		u32 len;

		addr = (u32)sg_dma_address(sg);
		*(__le32 *)(buf + i) = cpu_to_le32(addr);
		i += 4;

		len = sg_dma_len(sg) >> 3;
		*(__le32 *)(buf + i) = cpu_to_le32(len);
		i += 4;

		if (ata_sg_is_last(sg, qc))
			pFLAGS |= pEND;
		buf[i++] = pFLAGS;
		buf[i++] = qc->dev->dma_mode & 0xf;
		buf[i++] = 0;	/* pPKLW */
		buf[i++] = 0;	/* reserved */

		*(__le32 *)(buf + i)
			= (pFLAGS & pEND) ? 0 : cpu_to_le32(pp->pkt_dma + i + 4);
		i += 4;

		VPRINTK("PRD[%u] = (0x%lX, 0x%X)\n", nelem,
					(unsigned long)addr, len);
	}
	return i;
}

static void adma_qc_prep(struct ata_queued_cmd *qc)
{
	struct adma_port_priv *pp = qc->ap->private_data;
	u8  *buf = pp->pkt;
	u32 pkt_dma = (u32)pp->pkt_dma;
	int i = 0;

	VPRINTK("ENTER\n");

	adma_enter_reg_mode(qc->ap);
	if (qc->tf.protocol != ATA_PROT_DMA) {
		ata_qc_prep(qc);
		return;
	}

	buf[i++] = 0;	/* Response flags */
	buf[i++] = 0;	/* reserved */
	buf[i++] = cVLD | cDAT | cIEN;
	i++;		/* cLEN, gets filled in below */

	*(__le32 *)(buf+i) = cpu_to_le32(pkt_dma);	/* cNCPB */
	i += 4;		/* cNCPB */
	i += 4;		/* cPRD, gets filled in below */

	buf[i++] = 0;	/* reserved */
	buf[i++] = 0;	/* reserved */
	buf[i++] = 0;	/* reserved */
	buf[i++] = 0;	/* reserved */

	/* ATA registers; must be a multiple of 4 */
	buf[i++] = qc->tf.device;
	buf[i++] = ADMA_REGS_DEVICE;
	if ((qc->tf.flags & ATA_TFLAG_LBA48)) {
		buf[i++] = qc->tf.hob_nsect;
		buf[i++] = ADMA_REGS_SECTOR_COUNT;
		buf[i++] = qc->tf.hob_lbal;
		buf[i++] = ADMA_REGS_LBA_LOW;
		buf[i++] = qc->tf.hob_lbam;
		buf[i++] = ADMA_REGS_LBA_MID;
		buf[i++] = qc->tf.hob_lbah;
		buf[i++] = ADMA_REGS_LBA_HIGH;
	}
	buf[i++] = qc->tf.nsect;
	buf[i++] = ADMA_REGS_SECTOR_COUNT;
	buf[i++] = qc->tf.lbal;
	buf[i++] = ADMA_REGS_LBA_LOW;
	buf[i++] = qc->tf.lbam;
	buf[i++] = ADMA_REGS_LBA_MID;
	buf[i++] = qc->tf.lbah;
	buf[i++] = ADMA_REGS_LBA_HIGH;
	buf[i++] = 0;
	buf[i++] = ADMA_REGS_CONTROL;
	buf[i++] = rIGN;
	buf[i++] = 0;
	buf[i++] = qc->tf.command;
	buf[i++] = ADMA_REGS_COMMAND | rEND;

	buf[3] = (i >> 3) - 2;				/* cLEN */
	*(__le32 *)(buf+8) = cpu_to_le32(pkt_dma + i);	/* cPRD */

	i = adma_fill_sg(qc);
	wmb();	/* flush PRDs and pkt to memory */
#if 0
	/* dump out CPB + PRDs for debug */
	{
		int j, len = 0;
		static char obuf[2048];
		for (j = 0; j < i; ++j) {
			len += sprintf(obuf+len, "%02x ", buf[j]);
			if ((j & 7) == 7) {
				printk("%s\n", obuf);
				len = 0;
			}
		}
		if (len)
			printk("%s\n", obuf);
	}
#endif
}

static inline void adma_packet_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *chan = ADMA_REGS(ap->host_set->mmio_base, ap->port_no);

	VPRINTK("ENTER, ap %p\n", ap);

	/* fire up the ADMA engine */
	writew(aPIOMD4 | aGO, chan + ADMA_CONTROL);
}

static int adma_qc_issue(struct ata_queued_cmd *qc)
{
	struct adma_port_priv *pp = qc->ap->private_data;

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
		pp->state = adma_state_pkt;
		adma_packet_start(qc);
		return 0;

	case ATA_PROT_ATAPI_DMA:
		BUG();
		break;

	default:
		break;
	}

	pp->state = adma_state_mmio;
	return ata_qc_issue_prot(qc);
}

static inline unsigned int adma_intr_pkt(struct ata_host_set *host_set)
{
	unsigned int handled = 0, port_no;
	u8 __iomem *mmio_base = host_set->mmio_base;

	for (port_no = 0; port_no < host_set->n_ports; ++port_no) {
		struct ata_port *ap = host_set->ports[port_no];
		struct adma_port_priv *pp;
		struct ata_queued_cmd *qc;
		void __iomem *chan = ADMA_REGS(mmio_base, port_no);
		u8 status = readb(chan + ADMA_STATUS);

		if (status == 0)
			continue;
		handled = 1;
		adma_enter_reg_mode(ap);
		if (ap->flags & ATA_FLAG_PORT_DISABLED)
			continue;
		pp = ap->private_data;
		if (!pp || pp->state != adma_state_pkt)
			continue;
		qc = ata_qc_from_tag(ap, ap->active_tag);
		if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING))) {
			unsigned int err_mask = 0;

			if ((status & (aPERR | aPSD | aUIRQ)))
				err_mask = AC_ERR_OTHER;
			else if (pp->pkt[0] != cDONE)
				err_mask = AC_ERR_OTHER;

			ata_qc_complete(qc, err_mask);
		}
	}
	return handled;
}

static inline unsigned int adma_intr_mmio(struct ata_host_set *host_set)
{
	unsigned int handled = 0, port_no;

	for (port_no = 0; port_no < host_set->n_ports; ++port_no) {
		struct ata_port *ap;
		ap = host_set->ports[port_no];
		if (ap && (!(ap->flags & ATA_FLAG_PORT_DISABLED))) {
			struct ata_queued_cmd *qc;
			struct adma_port_priv *pp = ap->private_data;
			if (!pp || pp->state != adma_state_mmio)
				continue;
			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING))) {

				/* check main status, clearing INTRQ */
				u8 status = ata_check_status(ap);
				if ((status & ATA_BUSY))
					continue;
				DPRINTK("ata%u: protocol %d (dev_stat 0x%X)\n",
					ap->id, qc->tf.protocol, status);
		
				/* complete taskfile transaction */
				pp->state = adma_state_idle;
				ata_qc_complete(qc, ac_err_mask(status));
				handled = 1;
			}
		}
	}
	return handled;
}

static irqreturn_t adma_intr(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned int handled = 0;

	VPRINTK("ENTER\n");

	spin_lock(&host_set->lock);
	handled  = adma_intr_pkt(host_set) | adma_intr_mmio(host_set);
	spin_unlock(&host_set->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static void adma_ata_setup_port(struct ata_ioports *port, unsigned long base)
{
	port->cmd_addr		=
	port->data_addr		= base + 0x000;
	port->error_addr	=
	port->feature_addr	= base + 0x004;
	port->nsect_addr	= base + 0x008;
	port->lbal_addr		= base + 0x00c;
	port->lbam_addr		= base + 0x010;
	port->lbah_addr		= base + 0x014;
	port->device_addr	= base + 0x018;
	port->status_addr	=
	port->command_addr	= base + 0x01c;
	port->altstatus_addr	=
	port->ctl_addr		= base + 0x038;
}

static int adma_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct adma_port_priv *pp;
	int rc;

	rc = ata_port_start(ap);
	if (rc)
		return rc;
	adma_enter_reg_mode(ap);
	rc = -ENOMEM;
	pp = kcalloc(1, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		goto err_out;
	pp->pkt = dma_alloc_coherent(dev, ADMA_PKT_BYTES, &pp->pkt_dma,
								GFP_KERNEL);
	if (!pp->pkt)
		goto err_out_kfree;
	/* paranoia? */
	if ((pp->pkt_dma & 7) != 0) {
		printk("bad alignment for pp->pkt_dma: %08x\n",
						(u32)pp->pkt_dma);
		dma_free_coherent(dev, ADMA_PKT_BYTES,
						pp->pkt, pp->pkt_dma);
		goto err_out_kfree;
	}
	memset(pp->pkt, 0, ADMA_PKT_BYTES);
	ap->private_data = pp;
	adma_reinit_engine(ap);
	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}

static void adma_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct adma_port_priv *pp = ap->private_data;

	adma_reset_engine(ADMA_REGS(ap->host_set->mmio_base, ap->port_no));
	if (pp != NULL) {
		ap->private_data = NULL;
		if (pp->pkt != NULL)
			dma_free_coherent(dev, ADMA_PKT_BYTES,
					pp->pkt, pp->pkt_dma);
		kfree(pp);
	}
	ata_port_stop(ap);
}

static void adma_host_stop(struct ata_host_set *host_set)
{
	unsigned int port_no;

	for (port_no = 0; port_no < ADMA_PORTS; ++port_no)
		adma_reset_engine(ADMA_REGS(host_set->mmio_base, port_no));

	ata_pci_host_stop(host_set);
}

static void adma_host_init(unsigned int chip_id,
				struct ata_probe_ent *probe_ent)
{
	unsigned int port_no;
	void __iomem *mmio_base = probe_ent->mmio_base;

	/* enable/lock aGO operation */
	writeb(7, mmio_base + ADMA_MODE_LOCK);

	/* reset the ADMA logic */
	for (port_no = 0; port_no < ADMA_PORTS; ++port_no)
		adma_reset_engine(ADMA_REGS(mmio_base, port_no));
}

static int adma_set_dma_masks(struct pci_dev *pdev, void __iomem *mmio_base)
{
	int rc;

	rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			"32-bit DMA enable failed\n");
		return rc;
	}
	rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			"32-bit consistent DMA enable failed\n");
		return rc;
	}
	return 0;
}

static int adma_ata_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	void __iomem *mmio_base;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	int rc, port_no;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

	if ((pci_resource_flags(pdev, 4) & IORESOURCE_MEM) == 0) {
		rc = -ENODEV;
		goto err_out_regions;
	}

	mmio_base = pci_iomap(pdev, 4, 0);
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	rc = adma_set_dma_masks(pdev, mmio_base);
	if (rc)
		goto err_out_iounmap;

	probe_ent = kcalloc(1, sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_iounmap;
	}

	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	probe_ent->sht		= adma_port_info[board_idx].sht;
	probe_ent->host_flags	= adma_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= adma_port_info[board_idx].pio_mask;
	probe_ent->mwdma_mask	= adma_port_info[board_idx].mwdma_mask;
	probe_ent->udma_mask	= adma_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= adma_port_info[board_idx].port_ops;

	probe_ent->irq		= pdev->irq;
	probe_ent->irq_flags	= SA_SHIRQ;
	probe_ent->mmio_base	= mmio_base;
	probe_ent->n_ports	= ADMA_PORTS;

	for (port_no = 0; port_no < probe_ent->n_ports; ++port_no) {
		adma_ata_setup_port(&probe_ent->port[port_no],
			ADMA_ATA_REGS((unsigned long)mmio_base, port_no));
	}

	pci_set_master(pdev);

	/* initialize adapter */
	adma_host_init(board_idx, probe_ent);

	rc = ata_device_add(probe_ent);
	kfree(probe_ent);
	if (rc != ADMA_PORTS)
		goto err_out_iounmap;
	return 0;

err_out_iounmap:
	pci_iounmap(pdev, mmio_base);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}

static int __init adma_ata_init(void)
{
	return pci_module_init(&adma_ata_pci_driver);
}

static void __exit adma_ata_exit(void)
{
	pci_unregister_driver(&adma_ata_pci_driver);
}

MODULE_AUTHOR("Mark Lord");
MODULE_DESCRIPTION("Pacific Digital Corporation ADMA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, adma_ata_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(adma_ata_init);
module_exit(adma_ata_exit);
