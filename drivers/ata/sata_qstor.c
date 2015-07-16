/*
 *  sata_qstor.c - Pacific Digital Corporation QStor SATA
 *
 *  Maintained by:  Mark Lord <mlord@pobox.com>
 *
 *  Copyright 2005 Pacific Digital Corporation.
 *  (OSL/GPL code release authorized by Jalil Fadavi).
 *
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"sata_qstor"
#define DRV_VERSION	"0.09"

enum {
	QS_MMIO_BAR		= 4,

	QS_PORTS		= 4,
	QS_MAX_PRD		= LIBATA_MAX_PRD,
	QS_CPB_ORDER		= 6,
	QS_CPB_BYTES		= (1 << QS_CPB_ORDER),
	QS_PRD_BYTES		= QS_MAX_PRD * 16,
	QS_PKT_BYTES		= QS_CPB_BYTES + QS_PRD_BYTES,

	/* global register offsets */
	QS_HCF_CNFG3		= 0x0003, /* host configuration offset */
	QS_HID_HPHY		= 0x0004, /* host physical interface info */
	QS_HCT_CTRL		= 0x00e4, /* global interrupt mask offset */
	QS_HST_SFF		= 0x0100, /* host status fifo offset */
	QS_HVS_SERD3		= 0x0393, /* PHY enable offset */

	/* global control bits */
	QS_HPHY_64BIT		= (1 << 1), /* 64-bit bus detected */
	QS_CNFG3_GSRST		= 0x01,     /* global chip reset */
	QS_SERD3_PHY_ENA	= 0xf0,     /* PHY detection ENAble*/

	/* per-channel register offsets */
	QS_CCF_CPBA		= 0x0710, /* chan CPB base address */
	QS_CCF_CSEP		= 0x0718, /* chan CPB separation factor */
	QS_CFC_HUFT		= 0x0800, /* host upstream fifo threshold */
	QS_CFC_HDFT		= 0x0804, /* host downstream fifo threshold */
	QS_CFC_DUFT		= 0x0808, /* dev upstream fifo threshold */
	QS_CFC_DDFT		= 0x080c, /* dev downstream fifo threshold */
	QS_CCT_CTR0		= 0x0900, /* chan control-0 offset */
	QS_CCT_CTR1		= 0x0901, /* chan control-1 offset */
	QS_CCT_CFF		= 0x0a00, /* chan command fifo offset */

	/* channel control bits */
	QS_CTR0_REG		= (1 << 1),   /* register mode (vs. pkt mode) */
	QS_CTR0_CLER		= (1 << 2),   /* clear channel errors */
	QS_CTR1_RDEV		= (1 << 1),   /* sata phy/comms reset */
	QS_CTR1_RCHN		= (1 << 4),   /* reset channel logic */
	QS_CCF_RUN_PKT		= 0x107,      /* RUN a new dma PKT */

	/* pkt sub-field headers */
	QS_HCB_HDR		= 0x01,   /* Host Control Block header */
	QS_DCB_HDR		= 0x02,   /* Device Control Block header */

	/* pkt HCB flag bits */
	QS_HF_DIRO		= (1 << 0),   /* data DIRection Out */
	QS_HF_DAT		= (1 << 3),   /* DATa pkt */
	QS_HF_IEN		= (1 << 4),   /* Interrupt ENable */
	QS_HF_VLD		= (1 << 5),   /* VaLiD pkt */

	/* pkt DCB flag bits */
	QS_DF_PORD		= (1 << 2),   /* Pio OR Dma */
	QS_DF_ELBA		= (1 << 3),   /* Extended LBA (lba48) */

	/* PCI device IDs */
	board_2068_idx		= 0,	/* QStor 4-port SATA/RAID */
};

enum {
	QS_DMA_BOUNDARY		= ~0UL
};

typedef enum { qs_state_mmio, qs_state_pkt } qs_state_t;

struct qs_port_priv {
	u8			*pkt;
	dma_addr_t		pkt_dma;
	qs_state_t		state;
};

static int qs_scr_read(struct ata_link *link, unsigned int sc_reg, u32 *val);
static int qs_scr_write(struct ata_link *link, unsigned int sc_reg, u32 val);
static int qs_ata_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
static int qs_port_start(struct ata_port *ap);
static void qs_host_stop(struct ata_host *host);
static void qs_qc_prep(struct ata_queued_cmd *qc);
static unsigned int qs_qc_issue(struct ata_queued_cmd *qc);
static int qs_check_atapi_dma(struct ata_queued_cmd *qc);
static void qs_freeze(struct ata_port *ap);
static void qs_thaw(struct ata_port *ap);
static int qs_prereset(struct ata_link *link, unsigned long deadline);
static void qs_error_handler(struct ata_port *ap);

static struct scsi_host_template qs_ata_sht = {
	ATA_BASE_SHT(DRV_NAME),
	.sg_tablesize		= QS_MAX_PRD,
	.dma_boundary		= QS_DMA_BOUNDARY,
};

static struct ata_port_operations qs_ata_ops = {
	.inherits		= &ata_sff_port_ops,

	.check_atapi_dma	= qs_check_atapi_dma,
	.qc_prep		= qs_qc_prep,
	.qc_issue		= qs_qc_issue,

	.freeze			= qs_freeze,
	.thaw			= qs_thaw,
	.prereset		= qs_prereset,
	.softreset		= ATA_OP_NULL,
	.error_handler		= qs_error_handler,
	.lost_interrupt		= ATA_OP_NULL,

	.scr_read		= qs_scr_read,
	.scr_write		= qs_scr_write,

	.port_start		= qs_port_start,
	.host_stop		= qs_host_stop,
};

static const struct ata_port_info qs_port_info[] = {
	/* board_2068_idx */
	{
		.flags		= ATA_FLAG_SATA | ATA_FLAG_PIO_POLLING,
		.pio_mask	= ATA_PIO4_ONLY,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &qs_ata_ops,
	},
};

static const struct pci_device_id qs_ata_pci_tbl[] = {
	{ PCI_VDEVICE(PDC, 0x2068), board_2068_idx },

	{ }	/* terminate list */
};

static struct pci_driver qs_ata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= qs_ata_pci_tbl,
	.probe			= qs_ata_init_one,
	.remove			= ata_pci_remove_one,
};

static void __iomem *qs_mmio_base(struct ata_host *host)
{
	return host->iomap[QS_MMIO_BAR];
}

static int qs_check_atapi_dma(struct ata_queued_cmd *qc)
{
	return 1;	/* ATAPI DMA not supported */
}

static inline void qs_enter_reg_mode(struct ata_port *ap)
{
	u8 __iomem *chan = qs_mmio_base(ap->host) + (ap->port_no * 0x4000);
	struct qs_port_priv *pp = ap->private_data;

	pp->state = qs_state_mmio;
	writeb(QS_CTR0_REG, chan + QS_CCT_CTR0);
	readb(chan + QS_CCT_CTR0);        /* flush */
}

static inline void qs_reset_channel_logic(struct ata_port *ap)
{
	u8 __iomem *chan = qs_mmio_base(ap->host) + (ap->port_no * 0x4000);

	writeb(QS_CTR1_RCHN, chan + QS_CCT_CTR1);
	readb(chan + QS_CCT_CTR0);        /* flush */
	qs_enter_reg_mode(ap);
}

static void qs_freeze(struct ata_port *ap)
{
	u8 __iomem *mmio_base = qs_mmio_base(ap->host);

	writeb(0, mmio_base + QS_HCT_CTRL); /* disable host interrupts */
	qs_enter_reg_mode(ap);
}

static void qs_thaw(struct ata_port *ap)
{
	u8 __iomem *mmio_base = qs_mmio_base(ap->host);

	qs_enter_reg_mode(ap);
	writeb(1, mmio_base + QS_HCT_CTRL); /* enable host interrupts */
}

static int qs_prereset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;

	qs_reset_channel_logic(ap);
	return ata_sff_prereset(link, deadline);
}

static int qs_scr_read(struct ata_link *link, unsigned int sc_reg, u32 *val)
{
	if (sc_reg > SCR_CONTROL)
		return -EINVAL;
	*val = readl(link->ap->ioaddr.scr_addr + (sc_reg * 8));
	return 0;
}

static void qs_error_handler(struct ata_port *ap)
{
	qs_enter_reg_mode(ap);
	ata_sff_error_handler(ap);
}

static int qs_scr_write(struct ata_link *link, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return -EINVAL;
	writel(val, link->ap->ioaddr.scr_addr + (sc_reg * 8));
	return 0;
}

static unsigned int qs_fill_sg(struct ata_queued_cmd *qc)
{
	struct scatterlist *sg;
	struct ata_port *ap = qc->ap;
	struct qs_port_priv *pp = ap->private_data;
	u8 *prd = pp->pkt + QS_CPB_BYTES;
	unsigned int si;

	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		u64 addr;
		u32 len;

		addr = sg_dma_address(sg);
		*(__le64 *)prd = cpu_to_le64(addr);
		prd += sizeof(u64);

		len = sg_dma_len(sg);
		*(__le32 *)prd = cpu_to_le32(len);
		prd += sizeof(u64);

		VPRINTK("PRD[%u] = (0x%llX, 0x%X)\n", si,
					(unsigned long long)addr, len);
	}

	return si;
}

static void qs_qc_prep(struct ata_queued_cmd *qc)
{
	struct qs_port_priv *pp = qc->ap->private_data;
	u8 dflags = QS_DF_PORD, *buf = pp->pkt;
	u8 hflags = QS_HF_DAT | QS_HF_IEN | QS_HF_VLD;
	u64 addr;
	unsigned int nelem;

	VPRINTK("ENTER\n");

	qs_enter_reg_mode(qc->ap);
	if (qc->tf.protocol != ATA_PROT_DMA)
		return;

	nelem = qs_fill_sg(qc);

	if ((qc->tf.flags & ATA_TFLAG_WRITE))
		hflags |= QS_HF_DIRO;
	if ((qc->tf.flags & ATA_TFLAG_LBA48))
		dflags |= QS_DF_ELBA;

	/* host control block (HCB) */
	buf[ 0] = QS_HCB_HDR;
	buf[ 1] = hflags;
	*(__le32 *)(&buf[ 4]) = cpu_to_le32(qc->nbytes);
	*(__le32 *)(&buf[ 8]) = cpu_to_le32(nelem);
	addr = ((u64)pp->pkt_dma) + QS_CPB_BYTES;
	*(__le64 *)(&buf[16]) = cpu_to_le64(addr);

	/* device control block (DCB) */
	buf[24] = QS_DCB_HDR;
	buf[28] = dflags;

	/* frame information structure (FIS) */
	ata_tf_to_fis(&qc->tf, 0, 1, &buf[32]);
}

static inline void qs_packet_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 __iomem *chan = qs_mmio_base(ap->host) + (ap->port_no * 0x4000);

	VPRINTK("ENTER, ap %p\n", ap);

	writeb(QS_CTR0_CLER, chan + QS_CCT_CTR0);
	wmb();                             /* flush PRDs and pkt to memory */
	writel(QS_CCF_RUN_PKT, chan + QS_CCT_CFF);
	readl(chan + QS_CCT_CFF);          /* flush */
}

static unsigned int qs_qc_issue(struct ata_queued_cmd *qc)
{
	struct qs_port_priv *pp = qc->ap->private_data;

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
		pp->state = qs_state_pkt;
		qs_packet_start(qc);
		return 0;

	case ATAPI_PROT_DMA:
		BUG();
		break;

	default:
		break;
	}

	pp->state = qs_state_mmio;
	return ata_sff_qc_issue(qc);
}

static void qs_do_or_die(struct ata_queued_cmd *qc, u8 status)
{
	qc->err_mask |= ac_err_mask(status);

	if (!qc->err_mask) {
		ata_qc_complete(qc);
	} else {
		struct ata_port    *ap  = qc->ap;
		struct ata_eh_info *ehi = &ap->link.eh_info;

		ata_ehi_clear_desc(ehi);
		ata_ehi_push_desc(ehi, "status 0x%02X", status);

		if (qc->err_mask == AC_ERR_DEV)
			ata_port_abort(ap);
		else
			ata_port_freeze(ap);
	}
}

static inline unsigned int qs_intr_pkt(struct ata_host *host)
{
	unsigned int handled = 0;
	u8 sFFE;
	u8 __iomem *mmio_base = qs_mmio_base(host);

	do {
		u32 sff0 = readl(mmio_base + QS_HST_SFF);
		u32 sff1 = readl(mmio_base + QS_HST_SFF + 4);
		u8 sEVLD = (sff1 >> 30) & 0x01;	/* valid flag */
		sFFE  = sff1 >> 31;		/* empty flag */

		if (sEVLD) {
			u8 sDST = sff0 >> 16;	/* dev status */
			u8 sHST = sff1 & 0x3f;	/* host status */
			unsigned int port_no = (sff1 >> 8) & 0x03;
			struct ata_port *ap = host->ports[port_no];
			struct qs_port_priv *pp = ap->private_data;
			struct ata_queued_cmd *qc;

			DPRINTK("SFF=%08x%08x: sCHAN=%u sHST=%d sDST=%02x\n",
					sff1, sff0, port_no, sHST, sDST);
			handled = 1;
			if (!pp || pp->state != qs_state_pkt)
				continue;
			qc = ata_qc_from_tag(ap, ap->link.active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING))) {
				switch (sHST) {
				case 0: /* successful CPB */
				case 3: /* device error */
					qs_enter_reg_mode(qc->ap);
					qs_do_or_die(qc, sDST);
					break;
				default:
					break;
				}
			}
		}
	} while (!sFFE);
	return handled;
}

static inline unsigned int qs_intr_mmio(struct ata_host *host)
{
	unsigned int handled = 0, port_no;

	for (port_no = 0; port_no < host->n_ports; ++port_no) {
		struct ata_port *ap = host->ports[port_no];
		struct qs_port_priv *pp = ap->private_data;
		struct ata_queued_cmd *qc;

		qc = ata_qc_from_tag(ap, ap->link.active_tag);
		if (!qc) {
			/*
			 * The qstor hardware generates spurious
			 * interrupts from time to time when switching
			 * in and out of packet mode.  There's no
			 * obvious way to know if we're here now due
			 * to that, so just ack the irq and pretend we
			 * knew it was ours.. (ugh).  This does not
			 * affect packet mode.
			 */
			ata_sff_check_status(ap);
			handled = 1;
			continue;
		}

		if (!pp || pp->state != qs_state_mmio)
			continue;
		if (!(qc->tf.flags & ATA_TFLAG_POLLING))
			handled |= ata_sff_port_intr(ap, qc);
	}
	return handled;
}

static irqreturn_t qs_intr(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	unsigned int handled = 0;
	unsigned long flags;

	VPRINTK("ENTER\n");

	spin_lock_irqsave(&host->lock, flags);
	handled  = qs_intr_pkt(host) | qs_intr_mmio(host);
	spin_unlock_irqrestore(&host->lock, flags);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static void qs_ata_setup_port(struct ata_ioports *port, void __iomem *base)
{
	port->cmd_addr		=
	port->data_addr		= base + 0x400;
	port->error_addr	=
	port->feature_addr	= base + 0x408; /* hob_feature = 0x409 */
	port->nsect_addr	= base + 0x410; /* hob_nsect   = 0x411 */
	port->lbal_addr		= base + 0x418; /* hob_lbal    = 0x419 */
	port->lbam_addr		= base + 0x420; /* hob_lbam    = 0x421 */
	port->lbah_addr		= base + 0x428; /* hob_lbah    = 0x429 */
	port->device_addr	= base + 0x430;
	port->status_addr	=
	port->command_addr	= base + 0x438;
	port->altstatus_addr	=
	port->ctl_addr		= base + 0x440;
	port->scr_addr		= base + 0xc00;
}

static int qs_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct qs_port_priv *pp;
	void __iomem *mmio_base = qs_mmio_base(ap->host);
	void __iomem *chan = mmio_base + (ap->port_no * 0x4000);
	u64 addr;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;
	pp->pkt = dmam_alloc_coherent(dev, QS_PKT_BYTES, &pp->pkt_dma,
				      GFP_KERNEL);
	if (!pp->pkt)
		return -ENOMEM;
	memset(pp->pkt, 0, QS_PKT_BYTES);
	ap->private_data = pp;

	qs_enter_reg_mode(ap);
	addr = (u64)pp->pkt_dma;
	writel((u32) addr,        chan + QS_CCF_CPBA);
	writel((u32)(addr >> 32), chan + QS_CCF_CPBA + 4);
	return 0;
}

static void qs_host_stop(struct ata_host *host)
{
	void __iomem *mmio_base = qs_mmio_base(host);

	writeb(0, mmio_base + QS_HCT_CTRL); /* disable host interrupts */
	writeb(QS_CNFG3_GSRST, mmio_base + QS_HCF_CNFG3); /* global reset */
}

static void qs_host_init(struct ata_host *host, unsigned int chip_id)
{
	void __iomem *mmio_base = host->iomap[QS_MMIO_BAR];
	unsigned int port_no;

	writeb(0, mmio_base + QS_HCT_CTRL); /* disable host interrupts */
	writeb(QS_CNFG3_GSRST, mmio_base + QS_HCF_CNFG3); /* global reset */

	/* reset each channel in turn */
	for (port_no = 0; port_no < host->n_ports; ++port_no) {
		u8 __iomem *chan = mmio_base + (port_no * 0x4000);
		writeb(QS_CTR1_RDEV|QS_CTR1_RCHN, chan + QS_CCT_CTR1);
		writeb(QS_CTR0_REG, chan + QS_CCT_CTR0);
		readb(chan + QS_CCT_CTR0);        /* flush */
	}
	writeb(QS_SERD3_PHY_ENA, mmio_base + QS_HVS_SERD3); /* enable phy */

	for (port_no = 0; port_no < host->n_ports; ++port_no) {
		u8 __iomem *chan = mmio_base + (port_no * 0x4000);
		/* set FIFO depths to same settings as Windows driver */
		writew(32, chan + QS_CFC_HUFT);
		writew(32, chan + QS_CFC_HDFT);
		writew(10, chan + QS_CFC_DUFT);
		writew( 8, chan + QS_CFC_DDFT);
		/* set CPB size in bytes, as a power of two */
		writeb(QS_CPB_ORDER,    chan + QS_CCF_CSEP);
	}
	writeb(1, mmio_base + QS_HCT_CTRL); /* enable host interrupts */
}

/*
 * The QStor understands 64-bit buses, and uses 64-bit fields
 * for DMA pointers regardless of bus width.  We just have to
 * make sure our DMA masks are set appropriately for whatever
 * bridge lies between us and the QStor, and then the DMA mapping
 * code will ensure we only ever "see" appropriate buffer addresses.
 * If we're 32-bit limited somewhere, then our 64-bit fields will
 * just end up with zeros in the upper 32-bits, without any special
 * logic required outside of this routine (below).
 */
static int qs_set_dma_masks(struct pci_dev *pdev, void __iomem *mmio_base)
{
	u32 bus_info = readl(mmio_base + QS_HID_HPHY);
	int rc, have_64bit_bus = (bus_info & QS_HPHY_64BIT);

	if (have_64bit_bus &&
	    !dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		rc = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
		if (rc) {
			rc = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
			if (rc) {
				dev_err(&pdev->dev,
					"64-bit DMA enable failed\n");
				return rc;
			}
		}
	} else {
		rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(&pdev->dev, "32-bit DMA enable failed\n");
			return rc;
		}
		rc = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (rc) {
			dev_err(&pdev->dev,
				"32-bit consistent DMA enable failed\n");
			return rc;
		}
	}
	return 0;
}

static int qs_ata_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	unsigned int board_idx = (unsigned int) ent->driver_data;
	const struct ata_port_info *ppi[] = { &qs_port_info[board_idx], NULL };
	struct ata_host *host;
	int rc, port_no;

	ata_print_version_once(&pdev->dev, DRV_VERSION);

	/* alloc host */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, QS_PORTS);
	if (!host)
		return -ENOMEM;

	/* acquire resources and fill host */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	if ((pci_resource_flags(pdev, QS_MMIO_BAR) & IORESOURCE_MEM) == 0)
		return -ENODEV;

	rc = pcim_iomap_regions(pdev, 1 << QS_MMIO_BAR, DRV_NAME);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);

	rc = qs_set_dma_masks(pdev, host->iomap[QS_MMIO_BAR]);
	if (rc)
		return rc;

	for (port_no = 0; port_no < host->n_ports; ++port_no) {
		struct ata_port *ap = host->ports[port_no];
		unsigned int offset = port_no * 0x4000;
		void __iomem *chan = host->iomap[QS_MMIO_BAR] + offset;

		qs_ata_setup_port(&ap->ioaddr, chan);

		ata_port_pbar_desc(ap, QS_MMIO_BAR, -1, "mmio");
		ata_port_pbar_desc(ap, QS_MMIO_BAR, offset, "port");
	}

	/* initialize adapter */
	qs_host_init(host, board_idx);

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, qs_intr, IRQF_SHARED,
				 &qs_ata_sht);
}

module_pci_driver(qs_ata_pci_driver);

MODULE_AUTHOR("Mark Lord");
MODULE_DESCRIPTION("Pacific Digital Corporation QStor SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, qs_ata_pci_tbl);
MODULE_VERSION(DRV_VERSION);
