/*
 * Renesas R-Car SATA driver
 *
 * Author: Vladimir Barinov <source@cogentembedded.com>
 * Copyright (C) 2013 Cogent Embedded, Inc.
 * Copyright (C) 2013 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>

#define DRV_NAME "sata_rcar"

/* SH-Navi2G/ATAPI-ATA compatible task registers */
#define DATA_REG			0x100
#define SDEVCON_REG			0x138

/* SH-Navi2G/ATAPI module compatible control registers */
#define ATAPI_CONTROL1_REG		0x180
#define ATAPI_STATUS_REG		0x184
#define ATAPI_INT_ENABLE_REG		0x188
#define ATAPI_DTB_ADR_REG		0x198
#define ATAPI_DMA_START_ADR_REG		0x19C
#define ATAPI_DMA_TRANS_CNT_REG		0x1A0
#define ATAPI_CONTROL2_REG		0x1A4
#define ATAPI_SIG_ST_REG		0x1B0
#define ATAPI_BYTE_SWAP_REG		0x1BC

/* ATAPI control 1 register (ATAPI_CONTROL1) bits */
#define ATAPI_CONTROL1_ISM		BIT(16)
#define ATAPI_CONTROL1_DTA32M		BIT(11)
#define ATAPI_CONTROL1_RESET		BIT(7)
#define ATAPI_CONTROL1_DESE		BIT(3)
#define ATAPI_CONTROL1_RW		BIT(2)
#define ATAPI_CONTROL1_STOP		BIT(1)
#define ATAPI_CONTROL1_START		BIT(0)

/* ATAPI status register (ATAPI_STATUS) bits */
#define ATAPI_STATUS_SATAINT		BIT(11)
#define ATAPI_STATUS_DNEND		BIT(6)
#define ATAPI_STATUS_DEVTRM		BIT(5)
#define ATAPI_STATUS_DEVINT		BIT(4)
#define ATAPI_STATUS_ERR		BIT(2)
#define ATAPI_STATUS_NEND		BIT(1)
#define ATAPI_STATUS_ACT		BIT(0)

/* Interrupt enable register (ATAPI_INT_ENABLE) bits */
#define ATAPI_INT_ENABLE_SATAINT	BIT(11)
#define ATAPI_INT_ENABLE_DNEND		BIT(6)
#define ATAPI_INT_ENABLE_DEVTRM		BIT(5)
#define ATAPI_INT_ENABLE_DEVINT		BIT(4)
#define ATAPI_INT_ENABLE_ERR		BIT(2)
#define ATAPI_INT_ENABLE_NEND		BIT(1)
#define ATAPI_INT_ENABLE_ACT		BIT(0)

/* Access control registers for physical layer control register */
#define SATAPHYADDR_REG			0x200
#define SATAPHYWDATA_REG		0x204
#define SATAPHYACCEN_REG		0x208
#define SATAPHYRESET_REG		0x20C
#define SATAPHYRDATA_REG		0x210
#define SATAPHYACK_REG			0x214

/* Physical layer control address command register (SATAPHYADDR) bits */
#define SATAPHYADDR_PHYRATEMODE		BIT(10)
#define SATAPHYADDR_PHYCMD_READ		BIT(9)
#define SATAPHYADDR_PHYCMD_WRITE	BIT(8)

/* Physical layer control enable register (SATAPHYACCEN) bits */
#define SATAPHYACCEN_PHYLANE		BIT(0)

/* Physical layer control reset register (SATAPHYRESET) bits */
#define SATAPHYRESET_PHYRST		BIT(1)
#define SATAPHYRESET_PHYSRES		BIT(0)

/* Physical layer control acknowledge register (SATAPHYACK) bits */
#define SATAPHYACK_PHYACK		BIT(0)

/* Serial-ATA HOST control registers */
#define BISTCONF_REG			0x102C
#define SDATA_REG			0x1100
#define SSDEVCON_REG			0x1204

#define SCRSSTS_REG			0x1400
#define SCRSERR_REG			0x1404
#define SCRSCON_REG			0x1408
#define SCRSACT_REG			0x140C

#define SATAINTSTAT_REG			0x1508
#define SATAINTMASK_REG			0x150C

/* SATA INT status register (SATAINTSTAT) bits */
#define SATAINTSTAT_SERR		BIT(3)
#define SATAINTSTAT_ATA			BIT(0)

/* SATA INT mask register (SATAINTSTAT) bits */
#define SATAINTMASK_SERRMSK		BIT(3)
#define SATAINTMASK_ERRMSK		BIT(2)
#define SATAINTMASK_ERRCRTMSK		BIT(1)
#define SATAINTMASK_ATAMSK		BIT(0)

#define SATA_RCAR_INT_MASK		(SATAINTMASK_SERRMSK | \
					 SATAINTMASK_ATAMSK)

/* Physical Layer Control Registers */
#define SATAPCTLR1_REG			0x43
#define SATAPCTLR2_REG			0x52
#define SATAPCTLR3_REG			0x5A
#define SATAPCTLR4_REG			0x60

/* Descriptor table word 0 bit (when DTA32M = 1) */
#define SATA_RCAR_DTEND			BIT(0)

struct sata_rcar_priv {
	void __iomem *base;
	struct clk *clk;
};

static void sata_rcar_phy_initialize(struct sata_rcar_priv *priv)
{
	/* idle state */
	iowrite32(0, priv->base + SATAPHYADDR_REG);
	/* reset */
	iowrite32(SATAPHYRESET_PHYRST, priv->base + SATAPHYRESET_REG);
	udelay(10);
	/* deassert reset */
	iowrite32(0, priv->base + SATAPHYRESET_REG);
}

static void sata_rcar_phy_write(struct sata_rcar_priv *priv, u16 reg, u32 val,
				int group)
{
	int timeout;

	/* deassert reset */
	iowrite32(0, priv->base + SATAPHYRESET_REG);
	/* lane 1 */
	iowrite32(SATAPHYACCEN_PHYLANE, priv->base + SATAPHYACCEN_REG);
	/* write phy register value */
	iowrite32(val, priv->base + SATAPHYWDATA_REG);
	/* set register group */
	if (group)
		reg |= SATAPHYADDR_PHYRATEMODE;
	/* write command */
	iowrite32(SATAPHYADDR_PHYCMD_WRITE | reg, priv->base + SATAPHYADDR_REG);
	/* wait for ack */
	for (timeout = 0; timeout < 100; timeout++) {
		val = ioread32(priv->base + SATAPHYACK_REG);
		if (val & SATAPHYACK_PHYACK)
			break;
	}
	if (timeout >= 100)
		pr_err("%s timeout\n", __func__);
	/* idle state */
	iowrite32(0, priv->base + SATAPHYADDR_REG);
}

static void sata_rcar_freeze(struct ata_port *ap)
{
	struct sata_rcar_priv *priv = ap->host->private_data;

	/* mask */
	iowrite32(0x7ff, priv->base + SATAINTMASK_REG);

	ata_sff_freeze(ap);
}

static void sata_rcar_thaw(struct ata_port *ap)
{
	struct sata_rcar_priv *priv = ap->host->private_data;

	/* ack */
	iowrite32(~SATA_RCAR_INT_MASK, priv->base + SATAINTSTAT_REG);

	ata_sff_thaw(ap);

	/* unmask */
	iowrite32(0x7ff & ~SATA_RCAR_INT_MASK, priv->base + SATAINTMASK_REG);
}

static void sata_rcar_ioread16_rep(void __iomem *reg, void *buffer, int count)
{
	u16 *ptr = buffer;

	while (count--) {
		u16 data = ioread32(reg);

		*ptr++ = data;
	}
}

static void sata_rcar_iowrite16_rep(void __iomem *reg, void *buffer, int count)
{
	const u16 *ptr = buffer;

	while (count--)
		iowrite32(*ptr++, reg);
}

static u8 sata_rcar_check_status(struct ata_port *ap)
{
	return ioread32(ap->ioaddr.status_addr);
}

static u8 sata_rcar_check_altstatus(struct ata_port *ap)
{
	return ioread32(ap->ioaddr.altstatus_addr);
}

static void sata_rcar_set_devctl(struct ata_port *ap, u8 ctl)
{
	iowrite32(ctl, ap->ioaddr.ctl_addr);
}

static void sata_rcar_dev_select(struct ata_port *ap, unsigned int device)
{
	iowrite32(ATA_DEVICE_OBS, ap->ioaddr.device_addr);
	ata_sff_pause(ap);	/* needed; also flushes, for mmio */
}

static unsigned int sata_rcar_ata_devchk(struct ata_port *ap,
					 unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	sata_rcar_dev_select(ap, device);

	iowrite32(0x55, ioaddr->nsect_addr);
	iowrite32(0xaa, ioaddr->lbal_addr);

	iowrite32(0xaa, ioaddr->nsect_addr);
	iowrite32(0x55, ioaddr->lbal_addr);

	iowrite32(0x55, ioaddr->nsect_addr);
	iowrite32(0xaa, ioaddr->lbal_addr);

	nsect = ioread32(ioaddr->nsect_addr);
	lbal  = ioread32(ioaddr->lbal_addr);

	if (nsect == 0x55 && lbal == 0xaa)
		return 1;	/* found a device */

	return 0;		/* nothing found */
}

static int sata_rcar_wait_after_reset(struct ata_link *link,
				      unsigned long deadline)
{
	struct ata_port *ap = link->ap;

	ata_msleep(ap, ATA_WAIT_AFTER_RESET);

	return ata_sff_wait_ready(link, deadline);
}

static int sata_rcar_bus_softreset(struct ata_port *ap, unsigned long deadline)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	DPRINTK("ata%u: bus reset via SRST\n", ap->print_id);

	/* software reset.  causes dev0 to be selected */
	iowrite32(ap->ctl, ioaddr->ctl_addr);
	udelay(20);
	iowrite32(ap->ctl | ATA_SRST, ioaddr->ctl_addr);
	udelay(20);
	iowrite32(ap->ctl, ioaddr->ctl_addr);
	ap->last_ctl = ap->ctl;

	/* wait the port to become ready */
	return sata_rcar_wait_after_reset(&ap->link, deadline);
}

static int sata_rcar_softreset(struct ata_link *link, unsigned int *classes,
			       unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	unsigned int devmask = 0;
	int rc;
	u8 err;

	/* determine if device 0 is present */
	if (sata_rcar_ata_devchk(ap, 0))
		devmask |= 1 << 0;

	/* issue bus reset */
	DPRINTK("about to softreset, devmask=%x\n", devmask);
	rc = sata_rcar_bus_softreset(ap, deadline);
	/* if link is occupied, -ENODEV too is an error */
	if (rc && (rc != -ENODEV || sata_scr_valid(link))) {
		ata_link_err(link, "SRST failed (errno=%d)\n", rc);
		return rc;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_sff_dev_classify(&link->device[0], devmask, &err);

	DPRINTK("classes[0]=%u\n", classes[0]);
	return 0;
}

static void sata_rcar_tf_load(struct ata_port *ap,
			      const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		iowrite32(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		iowrite32(tf->hob_feature, ioaddr->feature_addr);
		iowrite32(tf->hob_nsect, ioaddr->nsect_addr);
		iowrite32(tf->hob_lbal, ioaddr->lbal_addr);
		iowrite32(tf->hob_lbam, ioaddr->lbam_addr);
		iowrite32(tf->hob_lbah, ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		iowrite32(tf->feature, ioaddr->feature_addr);
		iowrite32(tf->nsect, ioaddr->nsect_addr);
		iowrite32(tf->lbal, ioaddr->lbal_addr);
		iowrite32(tf->lbam, ioaddr->lbam_addr);
		iowrite32(tf->lbah, ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		iowrite32(tf->device, ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}

static void sata_rcar_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = sata_rcar_check_status(ap);
	tf->feature = ioread32(ioaddr->error_addr);
	tf->nsect = ioread32(ioaddr->nsect_addr);
	tf->lbal = ioread32(ioaddr->lbal_addr);
	tf->lbam = ioread32(ioaddr->lbam_addr);
	tf->lbah = ioread32(ioaddr->lbah_addr);
	tf->device = ioread32(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		iowrite32(tf->ctl | ATA_HOB, ioaddr->ctl_addr);
		tf->hob_feature = ioread32(ioaddr->error_addr);
		tf->hob_nsect = ioread32(ioaddr->nsect_addr);
		tf->hob_lbal = ioread32(ioaddr->lbal_addr);
		tf->hob_lbam = ioread32(ioaddr->lbam_addr);
		tf->hob_lbah = ioread32(ioaddr->lbah_addr);
		iowrite32(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
	}
}

static void sata_rcar_exec_command(struct ata_port *ap,
				   const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->print_id, tf->command);

	iowrite32(tf->command, ap->ioaddr.command_addr);
	ata_sff_pause(ap);
}

static unsigned int sata_rcar_data_xfer(struct ata_device *dev,
					      unsigned char *buf,
					      unsigned int buflen, int rw)
{
	struct ata_port *ap = dev->link->ap;
	void __iomem *data_addr = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 1;

	/* Transfer multiple of 2 bytes */
	if (rw == READ)
		sata_rcar_ioread16_rep(data_addr, buf, words);
	else
		sata_rcar_iowrite16_rep(data_addr, buf, words);

	/* Transfer trailing byte, if any. */
	if (unlikely(buflen & 0x01)) {
		unsigned char pad[2] = { };

		/* Point buf to the tail of buffer */
		buf += buflen - 1;

		/*
		 * Use io*16_rep() accessors here as well to avoid pointlessly
		 * swapping bytes to and from on the big endian machines...
		 */
		if (rw == READ) {
			sata_rcar_ioread16_rep(data_addr, pad, 1);
			*buf = pad[0];
		} else {
			pad[0] = *buf;
			sata_rcar_iowrite16_rep(data_addr, pad, 1);
		}
		words++;
	}

	return words << 1;
}

static void sata_rcar_drain_fifo(struct ata_queued_cmd *qc)
{
	int count;
	struct ata_port *ap;

	/* We only need to flush incoming data when a command was running */
	if (qc == NULL || qc->dma_dir == DMA_TO_DEVICE)
		return;

	ap = qc->ap;
	/* Drain up to 64K of data before we give up this recovery method */
	for (count = 0; (ap->ops->sff_check_status(ap) & ATA_DRQ) &&
			count < 65536; count += 2)
		ioread32(ap->ioaddr.data_addr);

	/* Can become DEBUG later */
	if (count)
		ata_port_dbg(ap, "drained %d bytes to clear DRQ\n", count);
}

static int sata_rcar_scr_read(struct ata_link *link, unsigned int sc_reg,
			      u32 *val)
{
	if (sc_reg > SCR_ACTIVE)
		return -EINVAL;

	*val = ioread32(link->ap->ioaddr.scr_addr + (sc_reg << 2));
	return 0;
}

static int sata_rcar_scr_write(struct ata_link *link, unsigned int sc_reg,
			       u32 val)
{
	if (sc_reg > SCR_ACTIVE)
		return -EINVAL;

	iowrite32(val, link->ap->ioaddr.scr_addr + (sc_reg << 2));
	return 0;
}

static void sata_rcar_bmdma_fill_sg(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_bmdma_prd *prd = ap->bmdma_prd;
	struct scatterlist *sg;
	unsigned int si, pi;

	pi = 0;
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		u32 addr, sg_len, len;

		/*
		 * Note: h/w doesn't support 64-bit, so we unconditionally
		 * truncate dma_addr_t to u32.
		 */
		addr = (u32)sg_dma_address(sg);
		sg_len = sg_dma_len(sg);

		/* H/w transfer count is only 29 bits long, let's be careful */
		while (sg_len) {
			len = sg_len;
			if (len > 0x1ffffffe)
				len = 0x1ffffffe;

			prd[pi].addr = cpu_to_le32(addr);
			prd[pi].flags_len = cpu_to_le32(len);
			VPRINTK("PRD[%u] = (0x%X, 0x%X)\n", pi, addr, len);

			pi++;
			sg_len -= len;
			addr += len;
		}
	}

	/* end-of-table flag */
	prd[pi - 1].addr |= cpu_to_le32(SATA_RCAR_DTEND);
}

static void sata_rcar_qc_prep(struct ata_queued_cmd *qc)
{
	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;

	sata_rcar_bmdma_fill_sg(qc);
}

static void sata_rcar_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = qc->tf.flags & ATA_TFLAG_WRITE;
	u32 dmactl;
	struct sata_rcar_priv *priv = ap->host->private_data;

	/* load PRD table addr. */
	mb();   /* make sure PRD table writes are visible to controller */
	iowrite32(ap->bmdma_prd_dma, priv->base + ATAPI_DTB_ADR_REG);

	/* specify data direction, triple-check start bit is clear */
	dmactl = ioread32(priv->base + ATAPI_CONTROL1_REG);
	dmactl &= ~(ATAPI_CONTROL1_RW | ATAPI_CONTROL1_STOP);
	if (dmactl & ATAPI_CONTROL1_START) {
		dmactl &= ~ATAPI_CONTROL1_START;
		dmactl |= ATAPI_CONTROL1_STOP;
	}
	if (!rw)
		dmactl |= ATAPI_CONTROL1_RW;
	iowrite32(dmactl, priv->base + ATAPI_CONTROL1_REG);

	/* issue r/w command */
	ap->ops->sff_exec_command(ap, &qc->tf);
}

static void sata_rcar_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u32 dmactl;
	struct sata_rcar_priv *priv = ap->host->private_data;

	/* start host DMA transaction */
	dmactl = ioread32(priv->base + ATAPI_CONTROL1_REG);
	dmactl &= ~ATAPI_CONTROL1_STOP;
	dmactl |= ATAPI_CONTROL1_START;
	iowrite32(dmactl, priv->base + ATAPI_CONTROL1_REG);
}

static void sata_rcar_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct sata_rcar_priv *priv = ap->host->private_data;
	u32 dmactl;

	/* force termination of DMA transfer if active */
	dmactl = ioread32(priv->base + ATAPI_CONTROL1_REG);
	if (dmactl & ATAPI_CONTROL1_START) {
		dmactl &= ~ATAPI_CONTROL1_START;
		dmactl |= ATAPI_CONTROL1_STOP;
		iowrite32(dmactl, priv->base + ATAPI_CONTROL1_REG);
	}

	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	ata_sff_dma_pause(ap);
}

static u8 sata_rcar_bmdma_status(struct ata_port *ap)
{
	struct sata_rcar_priv *priv = ap->host->private_data;
	u32 status;
	u8 host_stat = 0;

	status = ioread32(priv->base + ATAPI_STATUS_REG);
	if (status & ATAPI_STATUS_DEVINT)
		host_stat |= ATA_DMA_INTR;
	if (status & ATAPI_STATUS_ACT)
		host_stat |= ATA_DMA_ACTIVE;

	return host_stat;
}

static struct scsi_host_template sata_rcar_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations sata_rcar_port_ops = {
	.inherits		= &ata_bmdma_port_ops,

	.freeze			= sata_rcar_freeze,
	.thaw			= sata_rcar_thaw,
	.softreset		= sata_rcar_softreset,

	.scr_read		= sata_rcar_scr_read,
	.scr_write		= sata_rcar_scr_write,

	.sff_dev_select		= sata_rcar_dev_select,
	.sff_set_devctl		= sata_rcar_set_devctl,
	.sff_check_status	= sata_rcar_check_status,
	.sff_check_altstatus	= sata_rcar_check_altstatus,
	.sff_tf_load		= sata_rcar_tf_load,
	.sff_tf_read		= sata_rcar_tf_read,
	.sff_exec_command	= sata_rcar_exec_command,
	.sff_data_xfer		= sata_rcar_data_xfer,
	.sff_drain_fifo		= sata_rcar_drain_fifo,

	.qc_prep		= sata_rcar_qc_prep,

	.bmdma_setup		= sata_rcar_bmdma_setup,
	.bmdma_start		= sata_rcar_bmdma_start,
	.bmdma_stop		= sata_rcar_bmdma_stop,
	.bmdma_status		= sata_rcar_bmdma_status,
};

static void sata_rcar_serr_interrupt(struct ata_port *ap)
{
	struct sata_rcar_priv *priv = ap->host->private_data;
	struct ata_eh_info *ehi = &ap->link.eh_info;
	int freeze = 0;
	u32 serror;

	serror = ioread32(priv->base + SCRSERR_REG);
	if (!serror)
		return;

	DPRINTK("SError @host_intr: 0x%x\n", serror);

	/* first, analyze and record host port events */
	ata_ehi_clear_desc(ehi);

	if (serror & (SERR_DEV_XCHG | SERR_PHYRDY_CHG)) {
		/* Setup a soft-reset EH action */
		ata_ehi_hotplugged(ehi);
		ata_ehi_push_desc(ehi, "%s", "hotplug");

		freeze = serror & SERR_COMM_WAKE ? 0 : 1;
	}

	/* freeze or abort */
	if (freeze)
		ata_port_freeze(ap);
	else
		ata_port_abort(ap);
}

static void sata_rcar_ata_interrupt(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	int handled = 0;

	qc = ata_qc_from_tag(ap, ap->link.active_tag);
	if (qc)
		handled |= ata_bmdma_port_intr(ap, qc);

	/* be sure to clear ATA interrupt */
	if (!handled)
		sata_rcar_check_status(ap);
}

static irqreturn_t sata_rcar_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	struct sata_rcar_priv *priv = host->private_data;
	struct ata_port *ap;
	unsigned int handled = 0;
	u32 sataintstat;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	sataintstat = ioread32(priv->base + SATAINTSTAT_REG);
	sataintstat &= SATA_RCAR_INT_MASK;
	if (!sataintstat)
		goto done;
	/* ack */
	iowrite32(~sataintstat & 0x7ff, priv->base + SATAINTSTAT_REG);

	ap = host->ports[0];

	if (sataintstat & SATAINTSTAT_ATA)
		sata_rcar_ata_interrupt(ap);

	if (sataintstat & SATAINTSTAT_SERR)
		sata_rcar_serr_interrupt(ap);

	handled = 1;
done:
	spin_unlock_irqrestore(&host->lock, flags);

	return IRQ_RETVAL(handled);
}

static void sata_rcar_setup_port(struct ata_host *host)
{
	struct ata_port *ap = host->ports[0];
	struct ata_ioports *ioaddr = &ap->ioaddr;
	struct sata_rcar_priv *priv = host->private_data;

	ap->ops		= &sata_rcar_port_ops;
	ap->pio_mask	= ATA_PIO4;
	ap->udma_mask	= ATA_UDMA6;
	ap->flags	|= ATA_FLAG_SATA;

	ioaddr->cmd_addr = priv->base + SDATA_REG;
	ioaddr->ctl_addr = priv->base + SSDEVCON_REG;
	ioaddr->scr_addr = priv->base + SCRSSTS_REG;
	ioaddr->altstatus_addr = ioaddr->ctl_addr;

	ioaddr->data_addr	= ioaddr->cmd_addr + (ATA_REG_DATA << 2);
	ioaddr->error_addr	= ioaddr->cmd_addr + (ATA_REG_ERR << 2);
	ioaddr->feature_addr	= ioaddr->cmd_addr + (ATA_REG_FEATURE << 2);
	ioaddr->nsect_addr	= ioaddr->cmd_addr + (ATA_REG_NSECT << 2);
	ioaddr->lbal_addr	= ioaddr->cmd_addr + (ATA_REG_LBAL << 2);
	ioaddr->lbam_addr	= ioaddr->cmd_addr + (ATA_REG_LBAM << 2);
	ioaddr->lbah_addr	= ioaddr->cmd_addr + (ATA_REG_LBAH << 2);
	ioaddr->device_addr	= ioaddr->cmd_addr + (ATA_REG_DEVICE << 2);
	ioaddr->status_addr	= ioaddr->cmd_addr + (ATA_REG_STATUS << 2);
	ioaddr->command_addr	= ioaddr->cmd_addr + (ATA_REG_CMD << 2);
}

static void sata_rcar_init_controller(struct ata_host *host)
{
	struct sata_rcar_priv *priv = host->private_data;
	u32 val;

	/* reset and setup phy */
	sata_rcar_phy_initialize(priv);
	sata_rcar_phy_write(priv, SATAPCTLR1_REG, 0x00200188, 0);
	sata_rcar_phy_write(priv, SATAPCTLR1_REG, 0x00200188, 1);
	sata_rcar_phy_write(priv, SATAPCTLR3_REG, 0x0000A061, 0);
	sata_rcar_phy_write(priv, SATAPCTLR2_REG, 0x20000000, 0);
	sata_rcar_phy_write(priv, SATAPCTLR2_REG, 0x20000000, 1);
	sata_rcar_phy_write(priv, SATAPCTLR4_REG, 0x28E80000, 0);

	/* SATA-IP reset state */
	val = ioread32(priv->base + ATAPI_CONTROL1_REG);
	val |= ATAPI_CONTROL1_RESET;
	iowrite32(val, priv->base + ATAPI_CONTROL1_REG);

	/* ISM mode, PRD mode, DTEND flag at bit 0 */
	val = ioread32(priv->base + ATAPI_CONTROL1_REG);
	val |= ATAPI_CONTROL1_ISM;
	val |= ATAPI_CONTROL1_DESE;
	val |= ATAPI_CONTROL1_DTA32M;
	iowrite32(val, priv->base + ATAPI_CONTROL1_REG);

	/* Release the SATA-IP from the reset state */
	val = ioread32(priv->base + ATAPI_CONTROL1_REG);
	val &= ~ATAPI_CONTROL1_RESET;
	iowrite32(val, priv->base + ATAPI_CONTROL1_REG);

	/* ack and mask */
	iowrite32(0, priv->base + SATAINTSTAT_REG);
	iowrite32(0x7ff, priv->base + SATAINTMASK_REG);
	/* enable interrupts */
	iowrite32(ATAPI_INT_ENABLE_SATAINT, priv->base + ATAPI_INT_ENABLE_REG);
}

static int sata_rcar_probe(struct platform_device *pdev)
{
	struct ata_host *host;
	struct sata_rcar_priv *priv;
	struct resource *mem;
	int irq;
	int ret = 0;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct sata_rcar_priv),
			   GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "failed to get access to sata clock\n");
		return PTR_ERR(priv->clk);
	}
	clk_enable(priv->clk);

	host = ata_host_alloc(&pdev->dev, 1);
	if (!host) {
		dev_err(&pdev->dev, "ata_host_alloc failed\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	host->private_data = priv;

	priv->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto cleanup;
	}

	/* setup port */
	sata_rcar_setup_port(host);

	/* initialize host controller */
	sata_rcar_init_controller(host);

	ret = ata_host_activate(host, irq, sata_rcar_interrupt, 0,
				&sata_rcar_sht);
	if (!ret)
		return 0;

cleanup:
	clk_disable(priv->clk);

	return ret;
}

static int sata_rcar_remove(struct platform_device *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	struct sata_rcar_priv *priv = host->private_data;

	ata_host_detach(host);

	/* disable interrupts */
	iowrite32(0, priv->base + ATAPI_INT_ENABLE_REG);
	/* ack and mask */
	iowrite32(0, priv->base + SATAINTSTAT_REG);
	iowrite32(0x7ff, priv->base + SATAINTMASK_REG);

	clk_disable(priv->clk);

	return 0;
}

#ifdef CONFIG_PM
static int sata_rcar_suspend(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct sata_rcar_priv *priv = host->private_data;
	int ret;

	ret = ata_host_suspend(host, PMSG_SUSPEND);
	if (!ret) {
		/* disable interrupts */
		iowrite32(0, priv->base + ATAPI_INT_ENABLE_REG);
		/* mask */
		iowrite32(0x7ff, priv->base + SATAINTMASK_REG);

		clk_disable(priv->clk);
	}

	return ret;
}

static int sata_rcar_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct sata_rcar_priv *priv = host->private_data;

	clk_enable(priv->clk);

	/* ack and mask */
	iowrite32(0, priv->base + SATAINTSTAT_REG);
	iowrite32(0x7ff, priv->base + SATAINTMASK_REG);
	/* enable interrupts */
	iowrite32(ATAPI_INT_ENABLE_SATAINT, priv->base + ATAPI_INT_ENABLE_REG);

	ata_host_resume(host);

	return 0;
}

static const struct dev_pm_ops sata_rcar_pm_ops = {
	.suspend	= sata_rcar_suspend,
	.resume		= sata_rcar_resume,
};
#endif

static struct of_device_id sata_rcar_match[] = {
	{ .compatible = "renesas,rcar-sata", },
	{},
};
MODULE_DEVICE_TABLE(of, sata_rcar_match);

static struct platform_driver sata_rcar_driver = {
	.probe		= sata_rcar_probe,
	.remove		= sata_rcar_remove,
	.driver = {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= sata_rcar_match,
#ifdef CONFIG_PM
		.pm		= &sata_rcar_pm_ops,
#endif
	},
};

module_platform_driver(sata_rcar_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("Renesas R-Car SATA controller low level driver");
