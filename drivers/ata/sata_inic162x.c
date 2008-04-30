/*
 * sata_inic162x.c - Driver for Initio 162x SATA controllers
 *
 * Copyright 2006  SUSE Linux Products GmbH
 * Copyright 2006  Tejun Heo <teheo@novell.com>
 *
 * This file is released under GPL v2.
 *
 * This controller is eccentric and easily locks up if something isn't
 * right.  Documentation is available at initio's website but it only
 * documents registers (not programming model).
 *
 * - ATA disks work.
 * - Hotplug works.
 * - ATAPI read works but burning doesn't.  This thing is really
 *   peculiar about ATAPI and I couldn't figure out how ATAPI PIO and
 *   ATAPI DMA WRITE should be programmed.  If you've got a clue, be
 *   my guest.
 * - Both STR and STD work.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/blkdev.h>
#include <scsi/scsi_device.h>

#define DRV_NAME	"sata_inic162x"
#define DRV_VERSION	"0.3"

enum {
	MMIO_BAR		= 5,

	NR_PORTS		= 2,

	IDMA_CPB_TBL_SIZE	= 4 * 32,

	INIC_DMA_BOUNDARY	= 0xffffff,

	HOST_ACTRL		= 0x08,
	HOST_CTL		= 0x7c,
	HOST_STAT		= 0x7e,
	HOST_IRQ_STAT		= 0xbc,
	HOST_IRQ_MASK		= 0xbe,

	PORT_SIZE		= 0x40,

	/* registers for ATA TF operation */
	PORT_TF_DATA		= 0x00,
	PORT_TF_FEATURE		= 0x01,
	PORT_TF_NSECT		= 0x02,
	PORT_TF_LBAL		= 0x03,
	PORT_TF_LBAM		= 0x04,
	PORT_TF_LBAH		= 0x05,
	PORT_TF_DEVICE		= 0x06,
	PORT_TF_COMMAND		= 0x07,
	PORT_TF_ALT_STAT	= 0x08,
	PORT_IRQ_STAT		= 0x09,
	PORT_IRQ_MASK		= 0x0a,
	PORT_PRD_CTL		= 0x0b,
	PORT_PRD_ADDR		= 0x0c,
	PORT_PRD_XFERLEN	= 0x10,
	PORT_CPB_CPBLAR		= 0x18,
	PORT_CPB_PTQFIFO	= 0x1c,

	/* IDMA register */
	PORT_IDMA_CTL		= 0x14,
	PORT_IDMA_STAT		= 0x16,

	PORT_RPQ_FIFO		= 0x1e,
	PORT_RPQ_CNT		= 0x1f,

	PORT_SCR		= 0x20,

	/* HOST_CTL bits */
	HCTL_IRQOFF		= (1 << 8),  /* global IRQ off */
	HCTL_FTHD0		= (1 << 10), /* fifo threshold 0 */
	HCTL_FTHD1		= (1 << 11), /* fifo threshold 1*/
	HCTL_PWRDWN		= (1 << 12), /* power down PHYs */
	HCTL_SOFTRST		= (1 << 13), /* global reset (no phy reset) */
	HCTL_RPGSEL		= (1 << 15), /* register page select */

	HCTL_KNOWN_BITS		= HCTL_IRQOFF | HCTL_PWRDWN | HCTL_SOFTRST |
				  HCTL_RPGSEL,

	/* HOST_IRQ_(STAT|MASK) bits */
	HIRQ_PORT0		= (1 << 0),
	HIRQ_PORT1		= (1 << 1),
	HIRQ_SOFT		= (1 << 14),
	HIRQ_GLOBAL		= (1 << 15), /* STAT only */

	/* PORT_IRQ_(STAT|MASK) bits */
	PIRQ_OFFLINE		= (1 << 0),  /* device unplugged */
	PIRQ_ONLINE		= (1 << 1),  /* device plugged */
	PIRQ_COMPLETE		= (1 << 2),  /* completion interrupt */
	PIRQ_FATAL		= (1 << 3),  /* fatal error */
	PIRQ_ATA		= (1 << 4),  /* ATA interrupt */
	PIRQ_REPLY		= (1 << 5),  /* reply FIFO not empty */
	PIRQ_PENDING		= (1 << 7),  /* port IRQ pending (STAT only) */

	PIRQ_ERR		= PIRQ_OFFLINE | PIRQ_ONLINE | PIRQ_FATAL,

	PIRQ_MASK_DMA_READ	= PIRQ_REPLY | PIRQ_ATA,
	PIRQ_MASK_OTHER		= PIRQ_REPLY | PIRQ_COMPLETE,
	PIRQ_MASK_FREEZE	= 0xff,

	/* PORT_PRD_CTL bits */
	PRD_CTL_START		= (1 << 0),
	PRD_CTL_WR		= (1 << 3),
	PRD_CTL_DMAEN		= (1 << 7),  /* DMA enable */

	/* PORT_IDMA_CTL bits */
	IDMA_CTL_RST_ATA	= (1 << 2),  /* hardreset ATA bus */
	IDMA_CTL_RST_IDMA	= (1 << 5),  /* reset IDMA machinary */
	IDMA_CTL_GO		= (1 << 7),  /* IDMA mode go */
	IDMA_CTL_ATA_NIEN	= (1 << 8),  /* ATA IRQ disable */

	/* PORT_IDMA_STAT bits */
	IDMA_STAT_PERR		= (1 << 0),  /* PCI ERROR MODE */
	IDMA_STAT_CPBERR	= (1 << 1),  /* ADMA CPB error */
	IDMA_STAT_LGCY		= (1 << 3),  /* ADMA legacy */
	IDMA_STAT_UIRQ		= (1 << 4),  /* ADMA unsolicited irq */
	IDMA_STAT_STPD		= (1 << 5),  /* ADMA stopped */
	IDMA_STAT_PSD		= (1 << 6),  /* ADMA pause */
	IDMA_STAT_DONE		= (1 << 7),  /* ADMA done */

	IDMA_STAT_ERR		= IDMA_STAT_PERR | IDMA_STAT_CPBERR,

	/* CPB Control Flags*/
	CPB_CTL_VALID		= (1 << 0),  /* CPB valid */
	CPB_CTL_QUEUED		= (1 << 1),  /* queued command */
	CPB_CTL_DATA		= (1 << 2),  /* data, rsvd in datasheet */
	CPB_CTL_IEN		= (1 << 3),  /* PCI interrupt enable */
	CPB_CTL_DEVDIR		= (1 << 4),  /* device direction control */

	/* CPB Response Flags */
	CPB_RESP_DONE		= (1 << 0),  /* ATA command complete */
	CPB_RESP_REL		= (1 << 1),  /* ATA release */
	CPB_RESP_IGNORED	= (1 << 2),  /* CPB ignored */
	CPB_RESP_ATA_ERR	= (1 << 3),  /* ATA command error */
	CPB_RESP_SPURIOUS	= (1 << 4),  /* ATA spurious interrupt error */
	CPB_RESP_UNDERFLOW	= (1 << 5),  /* APRD deficiency length error */
	CPB_RESP_OVERFLOW	= (1 << 6),  /* APRD exccess length error */
	CPB_RESP_CPB_ERR	= (1 << 7),  /* CPB error flag */

	/* PRD Control Flags */
	PRD_DRAIN		= (1 << 1),  /* ignore data excess */
	PRD_CDB			= (1 << 2),  /* atapi packet command pointer */
	PRD_DIRECT_INTR		= (1 << 3),  /* direct interrupt */
	PRD_DMA			= (1 << 4),  /* data transfer method */
	PRD_WRITE		= (1 << 5),  /* data dir, rsvd in datasheet */
	PRD_IOM			= (1 << 6),  /* io/memory transfer */
	PRD_END			= (1 << 7),  /* APRD chain end */
};

/* Comman Parameter Block */
struct inic_cpb {
	u8		resp_flags;	/* Response Flags */
	u8		error;		/* ATA Error */
	u8		status;		/* ATA Status */
	u8		ctl_flags;	/* Control Flags */
	__le32		len;		/* Total Transfer Length */
	__le32		prd;		/* First PRD pointer */
	u8		rsvd[4];
	/* 16 bytes */
	u8		feature;	/* ATA Feature */
	u8		hob_feature;	/* ATA Ex. Feature */
	u8		device;		/* ATA Device/Head */
	u8		mirctl;		/* Mirror Control */
	u8		nsect;		/* ATA Sector Count */
	u8		hob_nsect;	/* ATA Ex. Sector Count */
	u8		lbal;		/* ATA Sector Number */
	u8		hob_lbal;	/* ATA Ex. Sector Number */
	u8		lbam;		/* ATA Cylinder Low */
	u8		hob_lbam;	/* ATA Ex. Cylinder Low */
	u8		lbah;		/* ATA Cylinder High */
	u8		hob_lbah;	/* ATA Ex. Cylinder High */
	u8		command;	/* ATA Command */
	u8		ctl;		/* ATA Control */
	u8		slave_error;	/* Slave ATA Error */
	u8		slave_status;	/* Slave ATA Status */
	/* 32 bytes */
} __packed;

/* Physical Region Descriptor */
struct inic_prd {
	__le32		mad;		/* Physical Memory Address */
	__le16		len;		/* Transfer Length */
	u8		rsvd;
	u8		flags;		/* Control Flags */
} __packed;

struct inic_pkt {
	struct inic_cpb	cpb;
	struct inic_prd	prd[LIBATA_MAX_PRD];
} __packed;

struct inic_host_priv {
	u16		cached_hctl;
};

struct inic_port_priv {
	struct inic_pkt	*pkt;
	dma_addr_t	pkt_dma;
	u32		*cpb_tbl;
	dma_addr_t	cpb_tbl_dma;
	u8		dfl_prdctl;
	u8		cached_prdctl;
	u8		cached_pirq_mask;
};

static struct scsi_host_template inic_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
	.dma_boundary	= INIC_DMA_BOUNDARY,
};

static const int scr_map[] = {
	[SCR_STATUS]	= 0,
	[SCR_ERROR]	= 1,
	[SCR_CONTROL]	= 2,
};

static void __iomem *inic_port_base(struct ata_port *ap)
{
	return ap->host->iomap[MMIO_BAR] + ap->port_no * PORT_SIZE;
}

static void __inic_set_pirq_mask(struct ata_port *ap, u8 mask)
{
	void __iomem *port_base = inic_port_base(ap);
	struct inic_port_priv *pp = ap->private_data;

	writeb(mask, port_base + PORT_IRQ_MASK);
	pp->cached_pirq_mask = mask;
}

static void inic_set_pirq_mask(struct ata_port *ap, u8 mask)
{
	struct inic_port_priv *pp = ap->private_data;

	if (pp->cached_pirq_mask != mask)
		__inic_set_pirq_mask(ap, mask);
}

static void inic_reset_port(void __iomem *port_base)
{
	void __iomem *idma_ctl = port_base + PORT_IDMA_CTL;
	u16 ctl;

	ctl = readw(idma_ctl);
	ctl &= ~(IDMA_CTL_RST_IDMA | IDMA_CTL_ATA_NIEN | IDMA_CTL_GO);

	/* mask IRQ and assert reset */
	writew(ctl | IDMA_CTL_RST_IDMA | IDMA_CTL_ATA_NIEN, idma_ctl);
	readw(idma_ctl); /* flush */

	/* give it some time */
	msleep(1);

	/* release reset */
	writew(ctl | IDMA_CTL_ATA_NIEN, idma_ctl);

	/* clear irq */
	writeb(0xff, port_base + PORT_IRQ_STAT);

	/* reenable ATA IRQ, turn off IDMA mode */
	writew(ctl, idma_ctl);
}

static int inic_scr_read(struct ata_port *ap, unsigned sc_reg, u32 *val)
{
	void __iomem *scr_addr = ap->ioaddr.scr_addr;
	void __iomem *addr;

	if (unlikely(sc_reg >= ARRAY_SIZE(scr_map)))
		return -EINVAL;

	addr = scr_addr + scr_map[sc_reg] * 4;
	*val = readl(scr_addr + scr_map[sc_reg] * 4);

	/* this controller has stuck DIAG.N, ignore it */
	if (sc_reg == SCR_ERROR)
		*val &= ~SERR_PHYRDY_CHG;
	return 0;
}

static int inic_scr_write(struct ata_port *ap, unsigned sc_reg, u32 val)
{
	void __iomem *scr_addr = ap->ioaddr.scr_addr;

	if (unlikely(sc_reg >= ARRAY_SIZE(scr_map)))
		return -EINVAL;

	writel(val, scr_addr + scr_map[sc_reg] * 4);
	return 0;
}

/*
 * In TF mode, inic162x is very similar to SFF device.  TF registers
 * function the same.  DMA engine behaves similary using the same PRD
 * format as BMDMA but different command register, interrupt and event
 * notification methods are used.  The following inic_bmdma_*()
 * functions do the impedance matching.
 */
static void inic_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct inic_port_priv *pp = ap->private_data;
	void __iomem *port_base = inic_port_base(ap);
	int rw = qc->tf.flags & ATA_TFLAG_WRITE;

	/* make sure device sees PRD table writes */
	wmb();

	/* load transfer length */
	writel(qc->nbytes, port_base + PORT_PRD_XFERLEN);

	/* turn on DMA and specify data direction */
	pp->cached_prdctl = pp->dfl_prdctl | PRD_CTL_DMAEN;
	if (!rw)
		pp->cached_prdctl |= PRD_CTL_WR;
	writeb(pp->cached_prdctl, port_base + PORT_PRD_CTL);

	/* issue r/w command */
	ap->ops->sff_exec_command(ap, &qc->tf);
}

static void inic_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct inic_port_priv *pp = ap->private_data;
	void __iomem *port_base = inic_port_base(ap);

	/* start host DMA transaction */
	pp->cached_prdctl |= PRD_CTL_START;
	writeb(pp->cached_prdctl, port_base + PORT_PRD_CTL);
}

static void inic_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct inic_port_priv *pp = ap->private_data;
	void __iomem *port_base = inic_port_base(ap);

	/* stop DMA engine */
	writeb(pp->dfl_prdctl, port_base + PORT_PRD_CTL);
}

static u8 inic_bmdma_status(struct ata_port *ap)
{
	/* event is already verified by the interrupt handler */
	return ATA_DMA_INTR;
}

static void inic_stop_idma(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);

	readb(port_base + PORT_RPQ_FIFO);
	readb(port_base + PORT_RPQ_CNT);
	writew(0, port_base + PORT_IDMA_CTL);
}

static void inic_host_err_intr(struct ata_port *ap, u8 irq_stat, u16 idma_stat)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	struct inic_port_priv *pp = ap->private_data;
	struct inic_cpb *cpb = &pp->pkt->cpb;
	bool freeze = false;

	ata_ehi_clear_desc(ehi);
	ata_ehi_push_desc(ehi, "irq_stat=0x%x idma_stat=0x%x",
			  irq_stat, idma_stat);

	inic_stop_idma(ap);

	if (irq_stat & (PIRQ_OFFLINE | PIRQ_ONLINE)) {
		ata_ehi_push_desc(ehi, "hotplug");
		ata_ehi_hotplugged(ehi);
		freeze = true;
	}

	if (idma_stat & IDMA_STAT_PERR) {
		ata_ehi_push_desc(ehi, "PCI error");
		freeze = true;
	}

	if (idma_stat & IDMA_STAT_CPBERR) {
		ata_ehi_push_desc(ehi, "CPB error");

		if (cpb->resp_flags & CPB_RESP_IGNORED) {
			__ata_ehi_push_desc(ehi, " ignored");
			ehi->err_mask |= AC_ERR_INVALID;
			freeze = true;
		}

		if (cpb->resp_flags & CPB_RESP_ATA_ERR)
			ehi->err_mask |= AC_ERR_DEV;

		if (cpb->resp_flags & CPB_RESP_SPURIOUS) {
			__ata_ehi_push_desc(ehi, " spurious-intr");
			ehi->err_mask |= AC_ERR_HSM;
			freeze = true;
		}

		if (cpb->resp_flags &
		    (CPB_RESP_UNDERFLOW | CPB_RESP_OVERFLOW)) {
			__ata_ehi_push_desc(ehi, " data-over/underflow");
			ehi->err_mask |= AC_ERR_HSM;
			freeze = true;
		}
	}

	if (freeze)
		ata_port_freeze(ap);
	else
		ata_port_abort(ap);
}

static void inic_host_intr(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->link.active_tag);
	u8 irq_stat;
	u16 idma_stat;

	/* read and clear IRQ status */
	irq_stat = readb(port_base + PORT_IRQ_STAT);
	writeb(irq_stat, port_base + PORT_IRQ_STAT);
	idma_stat = readw(port_base + PORT_IDMA_STAT);

	if (unlikely((irq_stat & PIRQ_ERR) || (idma_stat & IDMA_STAT_ERR)))
		inic_host_err_intr(ap, irq_stat, idma_stat);

	if (unlikely(!qc || (qc->tf.flags & ATA_TFLAG_POLLING))) {
		ap->ops->sff_check_status(ap); /* clear ATA interrupt */
		goto spurious;
	}

	if (qc->tf.protocol == ATA_PROT_DMA) {
		if (likely(idma_stat & IDMA_STAT_DONE)) {
			inic_stop_idma(ap);

			/* Depending on circumstances, device error
			 * isn't reported by IDMA, check it explicitly.
			 */
			if (unlikely(readb(port_base + PORT_TF_COMMAND) &
				     (ATA_DF | ATA_ERR)))
				qc->err_mask |= AC_ERR_DEV;

			ata_qc_complete(qc);
			return;
		}
	} else {
		if (likely(ata_sff_host_intr(ap, qc)))
			return;
	}

 spurious:
	ap->ops->sff_check_status(ap); /* clear ATA interrupt */
}

static irqreturn_t inic_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	void __iomem *mmio_base = host->iomap[MMIO_BAR];
	u16 host_irq_stat;
	int i, handled = 0;;

	host_irq_stat = readw(mmio_base + HOST_IRQ_STAT);

	if (unlikely(!(host_irq_stat & HIRQ_GLOBAL)))
		goto out;

	spin_lock(&host->lock);

	for (i = 0; i < NR_PORTS; i++) {
		struct ata_port *ap = host->ports[i];

		if (!(host_irq_stat & (HIRQ_PORT0 << i)))
			continue;

		if (likely(ap && !(ap->flags & ATA_FLAG_DISABLED))) {
			inic_host_intr(ap);
			handled++;
		} else {
			if (ata_ratelimit())
				dev_printk(KERN_ERR, host->dev, "interrupt "
					   "from disabled port %d (0x%x)\n",
					   i, host_irq_stat);
		}
	}

	spin_unlock(&host->lock);

 out:
	return IRQ_RETVAL(handled);
}

static void inic_fill_sg(struct inic_prd *prd, struct ata_queued_cmd *qc)
{
	struct scatterlist *sg;
	unsigned int si;
	u8 flags = PRD_DMA;

	if (qc->tf.flags & ATA_TFLAG_WRITE)
		flags |= PRD_WRITE;

	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		prd->mad = cpu_to_le32(sg_dma_address(sg));
		prd->len = cpu_to_le16(sg_dma_len(sg));
		prd->flags = flags;
		prd++;
	}

	WARN_ON(!si);
	prd[-1].flags |= PRD_END;
}

static void inic_qc_prep(struct ata_queued_cmd *qc)
{
	struct inic_port_priv *pp = qc->ap->private_data;
	struct inic_pkt *pkt = pp->pkt;
	struct inic_cpb *cpb = &pkt->cpb;
	struct inic_prd *prd = pkt->prd;

	VPRINTK("ENTER\n");

	if (qc->tf.protocol != ATA_PROT_DMA)
		return;

	/* prepare packet, based on initio driver */
	memset(pkt, 0, sizeof(struct inic_pkt));

	cpb->ctl_flags = CPB_CTL_VALID | CPB_CTL_IEN | CPB_CTL_DATA;

	cpb->len = cpu_to_le32(qc->nbytes);
	cpb->prd = cpu_to_le32(pp->pkt_dma + offsetof(struct inic_pkt, prd));

	cpb->device = qc->tf.device;
	cpb->feature = qc->tf.feature;
	cpb->nsect = qc->tf.nsect;
	cpb->lbal = qc->tf.lbal;
	cpb->lbam = qc->tf.lbam;
	cpb->lbah = qc->tf.lbah;

	if (qc->tf.flags & ATA_TFLAG_LBA48) {
		cpb->hob_feature = qc->tf.hob_feature;
		cpb->hob_nsect = qc->tf.hob_nsect;
		cpb->hob_lbal = qc->tf.hob_lbal;
		cpb->hob_lbam = qc->tf.hob_lbam;
		cpb->hob_lbah = qc->tf.hob_lbah;
	}

	cpb->command = qc->tf.command;
	/* don't load ctl - dunno why.  it's like that in the initio driver */

	/* setup sg table */
	inic_fill_sg(prd, qc);

	pp->cpb_tbl[0] = pp->pkt_dma;
}

static unsigned int inic_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *port_base = inic_port_base(ap);

	if (qc->tf.protocol == ATA_PROT_DMA) {
		/* fire up the ADMA engine */
		writew(HCTL_FTHD0, port_base + HOST_CTL);
		writew(IDMA_CTL_GO, port_base + PORT_IDMA_CTL);
		writeb(0, port_base + PORT_CPB_PTQFIFO);

		return 0;
	}

	/* Issuing a command to yet uninitialized port locks up the
	 * controller.  Most of the time, this happens for the first
	 * command after reset which are ATA and ATAPI IDENTIFYs.
	 * Fast fail if stat is 0x7f or 0xff for those commands.
	 */
	if (unlikely(qc->tf.command == ATA_CMD_ID_ATA ||
		     qc->tf.command == ATA_CMD_ID_ATAPI)) {
		u8 stat = ap->ops->sff_check_status(ap);
		if (stat == 0x7f || stat == 0xff)
			return AC_ERR_HSM;
	}

	return ata_sff_qc_issue(qc);
}

static void inic_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	void __iomem *port_base = inic_port_base(ap);

	tf->feature	= readb(port_base + PORT_TF_FEATURE);
	tf->nsect	= readb(port_base + PORT_TF_NSECT);
	tf->lbal	= readb(port_base + PORT_TF_LBAL);
	tf->lbam	= readb(port_base + PORT_TF_LBAM);
	tf->lbah	= readb(port_base + PORT_TF_LBAH);
	tf->device	= readb(port_base + PORT_TF_DEVICE);
	tf->command	= readb(port_base + PORT_TF_COMMAND);
}

static bool inic_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *rtf = &qc->result_tf;
	struct ata_taskfile tf;

	/* FIXME: Except for status and error, result TF access
	 * doesn't work.  I tried reading from BAR0/2, CPB and BAR5.
	 * None works regardless of which command interface is used.
	 * For now return true iff status indicates device error.
	 * This means that we're reporting bogus sector for RW
	 * failures.  Eeekk....
	 */
	inic_tf_read(qc->ap, &tf);

	if (!(tf.command & ATA_ERR))
		return false;

	rtf->command = tf.command;
	rtf->feature = tf.feature;
	return true;
}

static void inic_freeze(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);

	__inic_set_pirq_mask(ap, PIRQ_MASK_FREEZE);

	ap->ops->sff_check_status(ap);
	writeb(0xff, port_base + PORT_IRQ_STAT);
}

static void inic_thaw(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);

	ap->ops->sff_check_status(ap);
	writeb(0xff, port_base + PORT_IRQ_STAT);

	__inic_set_pirq_mask(ap, PIRQ_MASK_OTHER);
}

static int inic_check_ready(struct ata_link *link)
{
	void __iomem *port_base = inic_port_base(link->ap);

	return ata_check_ready(readb(port_base + PORT_TF_COMMAND));
}

/*
 * SRST and SControl hardreset don't give valid signature on this
 * controller.  Only controller specific hardreset mechanism works.
 */
static int inic_hardreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	void __iomem *port_base = inic_port_base(ap);
	void __iomem *idma_ctl = port_base + PORT_IDMA_CTL;
	const unsigned long *timing = sata_ehc_deb_timing(&link->eh_context);
	u16 val;
	int rc;

	/* hammer it into sane state */
	inic_reset_port(port_base);

	val = readw(idma_ctl);
	writew(val | IDMA_CTL_RST_ATA, idma_ctl);
	readw(idma_ctl);	/* flush */
	msleep(1);
	writew(val & ~IDMA_CTL_RST_ATA, idma_ctl);

	rc = sata_link_resume(link, timing, deadline);
	if (rc) {
		ata_link_printk(link, KERN_WARNING, "failed to resume "
				"link after reset (errno=%d)\n", rc);
		return rc;
	}

	*class = ATA_DEV_NONE;
	if (ata_link_online(link)) {
		struct ata_taskfile tf;

		/* wait for link to become ready */
		rc = ata_wait_after_reset(link, deadline, inic_check_ready);
		/* link occupied, -ENODEV too is an error */
		if (rc) {
			ata_link_printk(link, KERN_WARNING, "device not ready "
					"after hardreset (errno=%d)\n", rc);
			return rc;
		}

		inic_tf_read(ap, &tf);
		*class = ata_dev_classify(&tf);
	}

	return 0;
}

static void inic_error_handler(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);
	struct inic_port_priv *pp = ap->private_data;
	unsigned long flags;

	/* reset PIO HSM and stop DMA engine */
	inic_reset_port(port_base);

	spin_lock_irqsave(ap->lock, flags);
	ap->hsm_task_state = HSM_ST_IDLE;
	writeb(pp->dfl_prdctl, port_base + PORT_PRD_CTL);
	spin_unlock_irqrestore(ap->lock, flags);

	/* PIO and DMA engines have been stopped, perform recovery */
	ata_std_error_handler(ap);
}

static void inic_post_internal_cmd(struct ata_queued_cmd *qc)
{
	/* make DMA engine forget about the failed command */
	if (qc->flags & ATA_QCFLAG_FAILED)
		inic_reset_port(inic_port_base(qc->ap));
}

static void inic_dev_config(struct ata_device *dev)
{
	/* inic can only handle upto LBA28 max sectors */
	if (dev->max_sectors > ATA_MAX_SECTORS)
		dev->max_sectors = ATA_MAX_SECTORS;

	if (dev->n_sectors >= 1 << 28) {
		ata_dev_printk(dev, KERN_ERR,
	"ERROR: This driver doesn't support LBA48 yet and may cause\n"
	"                data corruption on such devices.  Disabling.\n");
		ata_dev_disable(dev);
	}
}

static void init_port(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);
	struct inic_port_priv *pp = ap->private_data;

	/* clear packet and CPB table */
	memset(pp->pkt, 0, sizeof(struct inic_pkt));
	memset(pp->cpb_tbl, 0, IDMA_CPB_TBL_SIZE);

	/* setup PRD and CPB lookup table addresses */
	writel(ap->prd_dma, port_base + PORT_PRD_ADDR);
	writel(pp->cpb_tbl_dma, port_base + PORT_CPB_CPBLAR);
}

static int inic_port_resume(struct ata_port *ap)
{
	init_port(ap);
	return 0;
}

static int inic_port_start(struct ata_port *ap)
{
	void __iomem *port_base = inic_port_base(ap);
	struct device *dev = ap->host->dev;
	struct inic_port_priv *pp;
	u8 tmp;
	int rc;

	/* alloc and initialize private data */
	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;
	ap->private_data = pp;

	/* default PRD_CTL value, DMAEN, WR and START off */
	tmp = readb(port_base + PORT_PRD_CTL);
	tmp &= ~(PRD_CTL_DMAEN | PRD_CTL_WR | PRD_CTL_START);
	pp->dfl_prdctl = tmp;

	/* Alloc resources */
	rc = ata_port_start(ap);
	if (rc)
		return rc;

	pp->pkt = dmam_alloc_coherent(dev, sizeof(struct inic_pkt),
				      &pp->pkt_dma, GFP_KERNEL);
	if (!pp->pkt)
		return -ENOMEM;

	pp->cpb_tbl = dmam_alloc_coherent(dev, IDMA_CPB_TBL_SIZE,
					  &pp->cpb_tbl_dma, GFP_KERNEL);
	if (!pp->cpb_tbl)
		return -ENOMEM;

	init_port(ap);

	return 0;
}

static struct ata_port_operations inic_port_ops = {
	.inherits		= &ata_sff_port_ops,

	.bmdma_setup		= inic_bmdma_setup,
	.bmdma_start		= inic_bmdma_start,
	.bmdma_stop		= inic_bmdma_stop,
	.bmdma_status		= inic_bmdma_status,
	.qc_prep		= inic_qc_prep,
	.qc_issue		= inic_qc_issue,
	.qc_fill_rtf		= inic_qc_fill_rtf,

	.freeze			= inic_freeze,
	.thaw			= inic_thaw,
	.softreset		= ATA_OP_NULL,	/* softreset is broken */
	.hardreset		= inic_hardreset,
	.error_handler		= inic_error_handler,
	.post_internal_cmd	= inic_post_internal_cmd,
	.dev_config		= inic_dev_config,

	.scr_read		= inic_scr_read,
	.scr_write		= inic_scr_write,

	.port_resume		= inic_port_resume,
	.port_start		= inic_port_start,
};

static struct ata_port_info inic_port_info = {
	/* For some reason, ATAPI_PROT_PIO is broken on this
	 * controller, and no, PIO_POLLING does't fix it.  It somehow
	 * manages to report the wrong ireason and ignoring ireason
	 * results in machine lock up.  Tell libata to always prefer
	 * DMA.
	 */
	.flags			= ATA_FLAG_SATA | ATA_FLAG_PIO_DMA,
	.pio_mask		= 0x1f,	/* pio0-4 */
	.mwdma_mask		= 0x07, /* mwdma0-2 */
	.udma_mask		= ATA_UDMA6,
	.port_ops		= &inic_port_ops
};

static int init_controller(void __iomem *mmio_base, u16 hctl)
{
	int i;
	u16 val;

	hctl &= ~HCTL_KNOWN_BITS;

	/* Soft reset whole controller.  Spec says reset duration is 3
	 * PCI clocks, be generous and give it 10ms.
	 */
	writew(hctl | HCTL_SOFTRST, mmio_base + HOST_CTL);
	readw(mmio_base + HOST_CTL); /* flush */

	for (i = 0; i < 10; i++) {
		msleep(1);
		val = readw(mmio_base + HOST_CTL);
		if (!(val & HCTL_SOFTRST))
			break;
	}

	if (val & HCTL_SOFTRST)
		return -EIO;

	/* mask all interrupts and reset ports */
	for (i = 0; i < NR_PORTS; i++) {
		void __iomem *port_base = mmio_base + i * PORT_SIZE;

		writeb(0xff, port_base + PORT_IRQ_MASK);
		inic_reset_port(port_base);
	}

	/* port IRQ is masked now, unmask global IRQ */
	writew(hctl & ~HCTL_IRQOFF, mmio_base + HOST_CTL);
	val = readw(mmio_base + HOST_IRQ_MASK);
	val &= ~(HIRQ_PORT0 | HIRQ_PORT1);
	writew(val, mmio_base + HOST_IRQ_MASK);

	return 0;
}

#ifdef CONFIG_PM
static int inic_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	struct inic_host_priv *hpriv = host->private_data;
	void __iomem *mmio_base = host->iomap[MMIO_BAR];
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	if (pdev->dev.power.power_state.event == PM_EVENT_SUSPEND) {
		rc = init_controller(mmio_base, hpriv->cached_hctl);
		if (rc)
			return rc;
	}

	ata_host_resume(host);

	return 0;
}
#endif

static int inic_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	const struct ata_port_info *ppi[] = { &inic_port_info, NULL };
	struct ata_host *host;
	struct inic_host_priv *hpriv;
	void __iomem * const *iomap;
	int i, rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* alloc host */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, NR_PORTS);
	hpriv = devm_kzalloc(&pdev->dev, sizeof(*hpriv), GFP_KERNEL);
	if (!host || !hpriv)
		return -ENOMEM;

	host->private_data = hpriv;

	/* acquire resources and fill host */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, 0x3f, DRV_NAME);
	if (rc)
		return rc;
	host->iomap = iomap = pcim_iomap_table(pdev);

	for (i = 0; i < NR_PORTS; i++) {
		struct ata_port *ap = host->ports[i];
		struct ata_ioports *port = &ap->ioaddr;
		unsigned int offset = i * PORT_SIZE;

		port->cmd_addr = iomap[2 * i];
		port->altstatus_addr =
		port->ctl_addr = (void __iomem *)
			((unsigned long)iomap[2 * i + 1] | ATA_PCI_CTL_OFS);
		port->scr_addr = iomap[MMIO_BAR] + offset + PORT_SCR;

		ata_sff_std_ports(port);

		ata_port_pbar_desc(ap, MMIO_BAR, -1, "mmio");
		ata_port_pbar_desc(ap, MMIO_BAR, offset, "port");
		ata_port_desc(ap, "cmd 0x%llx ctl 0x%llx",
		  (unsigned long long)pci_resource_start(pdev, 2 * i),
		  (unsigned long long)pci_resource_start(pdev, (2 * i + 1)) |
				      ATA_PCI_CTL_OFS);
	}

	hpriv->cached_hctl = readw(iomap[MMIO_BAR] + HOST_CTL);

	/* Set dma_mask.  This devices doesn't support 64bit addressing. */
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

	/*
	 * This controller is braindamaged.  dma_boundary is 0xffff
	 * like others but it will lock up the whole machine HARD if
	 * 65536 byte PRD entry is fed. Reduce maximum segment size.
	 */
	rc = pci_set_dma_max_seg_size(pdev, 65536 - 512);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to set the maximum segment size.\n");
		return rc;
	}

	rc = init_controller(iomap[MMIO_BAR], hpriv->cached_hctl);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to initialize controller\n");
		return rc;
	}

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, inic_interrupt, IRQF_SHARED,
				 &inic_sht);
}

static const struct pci_device_id inic_pci_tbl[] = {
	{ PCI_VDEVICE(INIT, 0x1622), },
	{ },
};

static struct pci_driver inic_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= inic_pci_tbl,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= inic_pci_device_resume,
#endif
	.probe 		= inic_init_one,
	.remove		= ata_pci_remove_one,
};

static int __init inic_init(void)
{
	return pci_register_driver(&inic_pci_driver);
}

static void __exit inic_exit(void)
{
	pci_unregister_driver(&inic_pci_driver);
}

MODULE_AUTHOR("Tejun Heo");
MODULE_DESCRIPTION("low-level driver for Initio 162x SATA");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, inic_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(inic_init);
module_exit(inic_exit);
