/*
 * drivers/ata/sata_dwc_460ex.c
 *
 * Synopsys DesignWare Cores (DWC) SATA host driver
 *
 * Author: Mark Miesfeld <mmiesfeld@amcc.com>
 *
 * Ported from 2.6.19.2 to 2.6.25/26 by Stefan Roese <sr@denx.de>
 * Copyright 2008 DENX Software Engineering
 *
 * Based on versions provided by AMCC and Synopsys which are:
 *          Copyright 2006 Applied Micro Circuits Corporation
 *          COPYRIGHT (C) 2005  SYNOPSYS, INC.  ALL RIGHTS RESERVED
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef CONFIG_SATA_DWC_DEBUG
#define DEBUG
#endif

#ifdef CONFIG_SATA_DWC_VDEBUG
#define VERBOSE_DEBUG
#define DEBUG_NCQ
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/slab.h>

#include "libata.h"

#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>

/* Supported DMA engine drivers */
#include <linux/platform_data/dma-dw.h>
#include <linux/dma/dw.h>

/* These two are defined in "libata.h" */
#undef	DRV_NAME
#undef	DRV_VERSION

#define DRV_NAME        "sata-dwc"
#define DRV_VERSION     "1.3"

#ifndef out_le32
#define out_le32(a, v)	__raw_writel(__cpu_to_le32(v), (void __iomem *)(a))
#endif

#ifndef in_le32
#define in_le32(a)	__le32_to_cpu(__raw_readl((void __iomem *)(a)))
#endif

#ifndef NO_IRQ
#define NO_IRQ		0
#endif

#define AHB_DMA_BRST_DFLT	64	/* 16 data items burst length*/

enum {
	SATA_DWC_MAX_PORTS = 1,

	SATA_DWC_SCR_OFFSET = 0x24,
	SATA_DWC_REG_OFFSET = 0x64,
};

/* DWC SATA Registers */
struct sata_dwc_regs {
	u32 fptagr;		/* 1st party DMA tag */
	u32 fpbor;		/* 1st party DMA buffer offset */
	u32 fptcr;		/* 1st party DMA Xfr count */
	u32 dmacr;		/* DMA Control */
	u32 dbtsr;		/* DMA Burst Transac size */
	u32 intpr;		/* Interrupt Pending */
	u32 intmr;		/* Interrupt Mask */
	u32 errmr;		/* Error Mask */
	u32 llcr;		/* Link Layer Control */
	u32 phycr;		/* PHY Control */
	u32 physr;		/* PHY Status */
	u32 rxbistpd;		/* Recvd BIST pattern def register */
	u32 rxbistpd1;		/* Recvd BIST data dword1 */
	u32 rxbistpd2;		/* Recvd BIST pattern data dword2 */
	u32 txbistpd;		/* Trans BIST pattern def register */
	u32 txbistpd1;		/* Trans BIST data dword1 */
	u32 txbistpd2;		/* Trans BIST data dword2 */
	u32 bistcr;		/* BIST Control Register */
	u32 bistfctr;		/* BIST FIS Count Register */
	u32 bistsr;		/* BIST Status Register */
	u32 bistdecr;		/* BIST Dword Error count register */
	u32 res[15];		/* Reserved locations */
	u32 testr;		/* Test Register */
	u32 versionr;		/* Version Register */
	u32 idr;		/* ID Register */
	u32 unimpl[192];	/* Unimplemented */
	u32 dmadr[256];	/* FIFO Locations in DMA Mode */
};

enum {
	SCR_SCONTROL_DET_ENABLE	=	0x00000001,
	SCR_SSTATUS_DET_PRESENT	=	0x00000001,
	SCR_SERROR_DIAG_X	=	0x04000000,
/* DWC SATA Register Operations */
	SATA_DWC_TXFIFO_DEPTH	=	0x01FF,
	SATA_DWC_RXFIFO_DEPTH	=	0x01FF,
	SATA_DWC_DMACR_TMOD_TXCHEN =	0x00000004,
	SATA_DWC_DMACR_TXCHEN	= (0x00000001 | SATA_DWC_DMACR_TMOD_TXCHEN),
	SATA_DWC_DMACR_RXCHEN	= (0x00000002 | SATA_DWC_DMACR_TMOD_TXCHEN),
	SATA_DWC_DMACR_TXRXCH_CLEAR =	SATA_DWC_DMACR_TMOD_TXCHEN,
	SATA_DWC_INTPR_DMAT	=	0x00000001,
	SATA_DWC_INTPR_NEWFP	=	0x00000002,
	SATA_DWC_INTPR_PMABRT	=	0x00000004,
	SATA_DWC_INTPR_ERR	=	0x00000008,
	SATA_DWC_INTPR_NEWBIST	=	0x00000010,
	SATA_DWC_INTPR_IPF	=	0x10000000,
	SATA_DWC_INTMR_DMATM	=	0x00000001,
	SATA_DWC_INTMR_NEWFPM	=	0x00000002,
	SATA_DWC_INTMR_PMABRTM	=	0x00000004,
	SATA_DWC_INTMR_ERRM	=	0x00000008,
	SATA_DWC_INTMR_NEWBISTM	=	0x00000010,
	SATA_DWC_LLCR_SCRAMEN	=	0x00000001,
	SATA_DWC_LLCR_DESCRAMEN	=	0x00000002,
	SATA_DWC_LLCR_RPDEN	=	0x00000004,
/* This is all error bits, zero's are reserved fields. */
	SATA_DWC_SERROR_ERR_BITS =	0x0FFF0F03
};

#define SATA_DWC_SCR0_SPD_GET(v)	(((v) >> 4) & 0x0000000F)
#define SATA_DWC_DMACR_TX_CLEAR(v)	(((v) & ~SATA_DWC_DMACR_TXCHEN) |\
						 SATA_DWC_DMACR_TMOD_TXCHEN)
#define SATA_DWC_DMACR_RX_CLEAR(v)	(((v) & ~SATA_DWC_DMACR_RXCHEN) |\
						 SATA_DWC_DMACR_TMOD_TXCHEN)
#define SATA_DWC_DBTSR_MWR(size)	(((size)/4) & SATA_DWC_TXFIFO_DEPTH)
#define SATA_DWC_DBTSR_MRD(size)	((((size)/4) & SATA_DWC_RXFIFO_DEPTH)\
						 << 16)
struct sata_dwc_device {
	struct device		*dev;		/* generic device struct */
	struct ata_probe_ent	*pe;		/* ptr to probe-ent */
	struct ata_host		*host;
	u8 __iomem		*reg_base;
	struct sata_dwc_regs	*sata_dwc_regs;	/* DW Synopsys SATA specific */
	struct dw_dma_chip	*dma;
};

#define SATA_DWC_QCMD_MAX	32

struct sata_dwc_device_port {
	struct sata_dwc_device	*hsdev;
	int			cmd_issued[SATA_DWC_QCMD_MAX];
	int			dma_pending[SATA_DWC_QCMD_MAX];

	/* DMA info */
	struct dw_dma_slave		*dws;
	struct dma_chan			*chan;
	struct dma_async_tx_descriptor	*desc[SATA_DWC_QCMD_MAX];
	u32				dma_interrupt_count;
};

/*
 * Commonly used DWC SATA driver Macros
 */
#define HSDEV_FROM_HOST(host)  ((struct sata_dwc_device *)\
					(host)->private_data)
#define HSDEV_FROM_AP(ap)  ((struct sata_dwc_device *)\
					(ap)->host->private_data)
#define HSDEVP_FROM_AP(ap)   ((struct sata_dwc_device_port *)\
					(ap)->private_data)
#define HSDEV_FROM_QC(qc)	((struct sata_dwc_device *)\
					(qc)->ap->host->private_data)
#define HSDEV_FROM_HSDEVP(p)	((struct sata_dwc_device *)\
						(hsdevp)->hsdev)

enum {
	SATA_DWC_CMD_ISSUED_NOT		= 0,
	SATA_DWC_CMD_ISSUED_PEND	= 1,
	SATA_DWC_CMD_ISSUED_EXEC	= 2,
	SATA_DWC_CMD_ISSUED_NODATA	= 3,

	SATA_DWC_DMA_PENDING_NONE	= 0,
	SATA_DWC_DMA_PENDING_TX		= 1,
	SATA_DWC_DMA_PENDING_RX		= 2,
};

struct sata_dwc_host_priv {
	void	__iomem	 *scr_addr_sstatus;
	u32	sata_dwc_sactive_issued ;
	u32	sata_dwc_sactive_queued ;
};

static struct sata_dwc_host_priv host_pvt;

static struct dw_dma_slave sata_dwc_dma_dws = {
	.src_id = 0,
	.dst_id = 0,
	.m_master = 1,
	.p_master = 0,
};

/*
 * Prototypes
 */
static void sata_dwc_bmdma_start_by_tag(struct ata_queued_cmd *qc, u8 tag);
static int sata_dwc_qc_complete(struct ata_port *ap, struct ata_queued_cmd *qc,
				u32 check_status);
static void sata_dwc_dma_xfer_complete(struct ata_port *ap, u32 check_status);
static void sata_dwc_port_stop(struct ata_port *ap);
static void sata_dwc_clear_dmacr(struct sata_dwc_device_port *hsdevp, u8 tag);

static const char *get_prot_descript(u8 protocol)
{
	switch ((enum ata_tf_protocols)protocol) {
	case ATA_PROT_NODATA:
		return "ATA no data";
	case ATA_PROT_PIO:
		return "ATA PIO";
	case ATA_PROT_DMA:
		return "ATA DMA";
	case ATA_PROT_NCQ:
		return "ATA NCQ";
	case ATAPI_PROT_NODATA:
		return "ATAPI no data";
	case ATAPI_PROT_PIO:
		return "ATAPI PIO";
	case ATAPI_PROT_DMA:
		return "ATAPI DMA";
	default:
		return "unknown";
	}
}

static const char *get_dma_dir_descript(int dma_dir)
{
	switch ((enum dma_data_direction)dma_dir) {
	case DMA_BIDIRECTIONAL:
		return "bidirectional";
	case DMA_TO_DEVICE:
		return "to device";
	case DMA_FROM_DEVICE:
		return "from device";
	default:
		return "none";
	}
}

static void sata_dwc_tf_dump(struct ata_port *ap, struct ata_taskfile *tf)
{
	dev_vdbg(ap->dev,
		"taskfile cmd: 0x%02x protocol: %s flags: 0x%lx device: %x\n",
		tf->command, get_prot_descript(tf->protocol), tf->flags,
		tf->device);
	dev_vdbg(ap->dev,
		"feature: 0x%02x nsect: 0x%x lbal: 0x%x lbam: 0x%x lbah: 0x%x\n",
		tf->feature, tf->nsect, tf->lbal, tf->lbam, tf->lbah);
	dev_vdbg(ap->dev,
		"hob_feature: 0x%02x hob_nsect: 0x%x hob_lbal: 0x%x hob_lbam: 0x%x hob_lbah: 0x%x\n",
		tf->hob_feature, tf->hob_nsect, tf->hob_lbal, tf->hob_lbam,
		tf->hob_lbah);
}

static void dma_dwc_xfer_done(void *hsdev_instance)
{
	unsigned long flags;
	struct sata_dwc_device *hsdev = hsdev_instance;
	struct ata_host *host = (struct ata_host *)hsdev->host;
	struct ata_port *ap;
	struct sata_dwc_device_port *hsdevp;
	u8 tag = 0;
	unsigned int port = 0;

	spin_lock_irqsave(&host->lock, flags);
	ap = host->ports[port];
	hsdevp = HSDEVP_FROM_AP(ap);
	tag = ap->link.active_tag;

	/*
	 * Each DMA command produces 2 interrupts.  Only
	 * complete the command after both interrupts have been
	 * seen. (See sata_dwc_isr())
	 */
	hsdevp->dma_interrupt_count++;
	sata_dwc_clear_dmacr(hsdevp, tag);

	if (hsdevp->dma_pending[tag] == SATA_DWC_DMA_PENDING_NONE) {
		dev_err(ap->dev, "DMA not pending tag=0x%02x pending=%d\n",
			tag, hsdevp->dma_pending[tag]);
	}

	if ((hsdevp->dma_interrupt_count % 2) == 0)
		sata_dwc_dma_xfer_complete(ap, 1);

	spin_unlock_irqrestore(&host->lock, flags);
}

static struct dma_async_tx_descriptor *dma_dwc_xfer_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	dma_addr_t addr = (dma_addr_t)&hsdev->sata_dwc_regs->dmadr;
	struct dma_slave_config sconf;
	struct dma_async_tx_descriptor *desc;

	if (qc->dma_dir == DMA_DEV_TO_MEM) {
		sconf.src_addr = addr;
		sconf.device_fc = true;
	} else {	/* DMA_MEM_TO_DEV */
		sconf.dst_addr = addr;
		sconf.device_fc = false;
	}

	sconf.direction = qc->dma_dir;
	sconf.src_maxburst = AHB_DMA_BRST_DFLT;
	sconf.dst_maxburst = AHB_DMA_BRST_DFLT;
	sconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	dmaengine_slave_config(hsdevp->chan, &sconf);

	/* Convert SG list to linked list of items (LLIs) for AHB DMA */
	desc = dmaengine_prep_slave_sg(hsdevp->chan, qc->sg, qc->n_elem,
				       qc->dma_dir,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!desc)
		return NULL;

	desc->callback = dma_dwc_xfer_done;
	desc->callback_param = hsdev;

	dev_dbg(hsdev->dev, "%s sg: 0x%p, count: %d addr: %pad\n",
		__func__, qc->sg, qc->n_elem, &addr);

	return desc;
}

static int sata_dwc_scr_read(struct ata_link *link, unsigned int scr, u32 *val)
{
	if (scr > SCR_NOTIFICATION) {
		dev_err(link->ap->dev, "%s: Incorrect SCR offset 0x%02x\n",
			__func__, scr);
		return -EINVAL;
	}

	*val = in_le32(link->ap->ioaddr.scr_addr + (scr * 4));
	dev_dbg(link->ap->dev, "%s: id=%d reg=%d val=val=0x%08x\n",
		__func__, link->ap->print_id, scr, *val);

	return 0;
}

static int sata_dwc_scr_write(struct ata_link *link, unsigned int scr, u32 val)
{
	dev_dbg(link->ap->dev, "%s: id=%d reg=%d val=val=0x%08x\n",
		__func__, link->ap->print_id, scr, val);
	if (scr > SCR_NOTIFICATION) {
		dev_err(link->ap->dev, "%s: Incorrect SCR offset 0x%02x\n",
			 __func__, scr);
		return -EINVAL;
	}
	out_le32(link->ap->ioaddr.scr_addr + (scr * 4), val);

	return 0;
}

static u32 core_scr_read(unsigned int scr)
{
	return in_le32(host_pvt.scr_addr_sstatus + (scr * 4));
}

static void core_scr_write(unsigned int scr, u32 val)
{
	out_le32(host_pvt.scr_addr_sstatus + (scr * 4), val);
}

static void clear_serror(void)
{
	u32 val;
	val = core_scr_read(SCR_ERROR);
	core_scr_write(SCR_ERROR, val);
}

static void clear_interrupt_bit(struct sata_dwc_device *hsdev, u32 bit)
{
	out_le32(&hsdev->sata_dwc_regs->intpr,
		 in_le32(&hsdev->sata_dwc_regs->intpr));
}

static u32 qcmd_tag_to_mask(u8 tag)
{
	return 0x00000001 << (tag & 0x1f);
}

/* See ahci.c */
static void sata_dwc_error_intr(struct ata_port *ap,
				struct sata_dwc_device *hsdev, uint intpr)
{
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);
	struct ata_eh_info *ehi = &ap->link.eh_info;
	unsigned int err_mask = 0, action = 0;
	struct ata_queued_cmd *qc;
	u32 serror;
	u8 status, tag;

	ata_ehi_clear_desc(ehi);

	serror = core_scr_read(SCR_ERROR);
	status = ap->ops->sff_check_status(ap);

	tag = ap->link.active_tag;

	dev_err(ap->dev,
		"%s SCR_ERROR=0x%08x intpr=0x%08x status=0x%08x dma_intp=%d pending=%d issued=%d",
		__func__, serror, intpr, status, hsdevp->dma_interrupt_count,
		hsdevp->dma_pending[tag], hsdevp->cmd_issued[tag]);

	/* Clear error register and interrupt bit */
	clear_serror();
	clear_interrupt_bit(hsdev, SATA_DWC_INTPR_ERR);

	/* This is the only error happening now.  TODO check for exact error */

	err_mask |= AC_ERR_HOST_BUS;
	action |= ATA_EH_RESET;

	/* Pass this on to EH */
	ehi->serror |= serror;
	ehi->action |= action;

	qc = ata_qc_from_tag(ap, tag);
	if (qc)
		qc->err_mask |= err_mask;
	else
		ehi->err_mask |= err_mask;

	ata_port_abort(ap);
}

/*
 * Function : sata_dwc_isr
 * arguments : irq, void *dev_instance, struct pt_regs *regs
 * Return value : irqreturn_t - status of IRQ
 * This Interrupt handler called via port ops registered function.
 * .irq_handler = sata_dwc_isr
 */
static irqreturn_t sata_dwc_isr(int irq, void *dev_instance)
{
	struct ata_host *host = (struct ata_host *)dev_instance;
	struct sata_dwc_device *hsdev = HSDEV_FROM_HOST(host);
	struct ata_port *ap;
	struct ata_queued_cmd *qc;
	unsigned long flags;
	u8 status, tag;
	int handled, num_processed, port = 0;
	uint intpr, sactive, sactive2, tag_mask;
	struct sata_dwc_device_port *hsdevp;
	host_pvt.sata_dwc_sactive_issued = 0;

	spin_lock_irqsave(&host->lock, flags);

	/* Read the interrupt register */
	intpr = in_le32(&hsdev->sata_dwc_regs->intpr);

	ap = host->ports[port];
	hsdevp = HSDEVP_FROM_AP(ap);

	dev_dbg(ap->dev, "%s intpr=0x%08x active_tag=%d\n", __func__, intpr,
		ap->link.active_tag);

	/* Check for error interrupt */
	if (intpr & SATA_DWC_INTPR_ERR) {
		sata_dwc_error_intr(ap, hsdev, intpr);
		handled = 1;
		goto DONE;
	}

	/* Check for DMA SETUP FIS (FP DMA) interrupt */
	if (intpr & SATA_DWC_INTPR_NEWFP) {
		clear_interrupt_bit(hsdev, SATA_DWC_INTPR_NEWFP);

		tag = (u8)(in_le32(&hsdev->sata_dwc_regs->fptagr));
		dev_dbg(ap->dev, "%s: NEWFP tag=%d\n", __func__, tag);
		if (hsdevp->cmd_issued[tag] != SATA_DWC_CMD_ISSUED_PEND)
			dev_warn(ap->dev, "CMD tag=%d not pending?\n", tag);

		host_pvt.sata_dwc_sactive_issued |= qcmd_tag_to_mask(tag);

		qc = ata_qc_from_tag(ap, tag);
		/*
		 * Start FP DMA for NCQ command.  At this point the tag is the
		 * active tag.  It is the tag that matches the command about to
		 * be completed.
		 */
		qc->ap->link.active_tag = tag;
		sata_dwc_bmdma_start_by_tag(qc, tag);

		handled = 1;
		goto DONE;
	}
	sactive = core_scr_read(SCR_ACTIVE);
	tag_mask = (host_pvt.sata_dwc_sactive_issued | sactive) ^ sactive;

	/* If no sactive issued and tag_mask is zero then this is not NCQ */
	if (host_pvt.sata_dwc_sactive_issued == 0 && tag_mask == 0) {
		if (ap->link.active_tag == ATA_TAG_POISON)
			tag = 0;
		else
			tag = ap->link.active_tag;
		qc = ata_qc_from_tag(ap, tag);

		/* DEV interrupt w/ no active qc? */
		if (unlikely(!qc || (qc->tf.flags & ATA_TFLAG_POLLING))) {
			dev_err(ap->dev,
				"%s interrupt with no active qc qc=%p\n",
				__func__, qc);
			ap->ops->sff_check_status(ap);
			handled = 1;
			goto DONE;
		}
		status = ap->ops->sff_check_status(ap);

		qc->ap->link.active_tag = tag;
		hsdevp->cmd_issued[tag] = SATA_DWC_CMD_ISSUED_NOT;

		if (status & ATA_ERR) {
			dev_dbg(ap->dev, "interrupt ATA_ERR (0x%x)\n", status);
			sata_dwc_qc_complete(ap, qc, 1);
			handled = 1;
			goto DONE;
		}

		dev_dbg(ap->dev, "%s non-NCQ cmd interrupt, protocol: %s\n",
			__func__, get_prot_descript(qc->tf.protocol));
DRVSTILLBUSY:
		if (ata_is_dma(qc->tf.protocol)) {
			/*
			 * Each DMA transaction produces 2 interrupts. The DMAC
			 * transfer complete interrupt and the SATA controller
			 * operation done interrupt. The command should be
			 * completed only after both interrupts are seen.
			 */
			hsdevp->dma_interrupt_count++;
			if (hsdevp->dma_pending[tag] == \
					SATA_DWC_DMA_PENDING_NONE) {
				dev_err(ap->dev,
					"%s: DMA not pending intpr=0x%08x status=0x%08x pending=%d\n",
					__func__, intpr, status,
					hsdevp->dma_pending[tag]);
			}

			if ((hsdevp->dma_interrupt_count % 2) == 0)
				sata_dwc_dma_xfer_complete(ap, 1);
		} else if (ata_is_pio(qc->tf.protocol)) {
			ata_sff_hsm_move(ap, qc, status, 0);
			handled = 1;
			goto DONE;
		} else {
			if (unlikely(sata_dwc_qc_complete(ap, qc, 1)))
				goto DRVSTILLBUSY;
		}

		handled = 1;
		goto DONE;
	}

	/*
	 * This is a NCQ command. At this point we need to figure out for which
	 * tags we have gotten a completion interrupt.  One interrupt may serve
	 * as completion for more than one operation when commands are queued
	 * (NCQ).  We need to process each completed command.
	 */

	 /* process completed commands */
	sactive = core_scr_read(SCR_ACTIVE);
	tag_mask = (host_pvt.sata_dwc_sactive_issued | sactive) ^ sactive;

	if (sactive != 0 || (host_pvt.sata_dwc_sactive_issued) > 1 || \
							tag_mask > 1) {
		dev_dbg(ap->dev,
			"%s NCQ:sactive=0x%08x  sactive_issued=0x%08x tag_mask=0x%08x\n",
			__func__, sactive, host_pvt.sata_dwc_sactive_issued,
			tag_mask);
	}

	if ((tag_mask | (host_pvt.sata_dwc_sactive_issued)) != \
					(host_pvt.sata_dwc_sactive_issued)) {
		dev_warn(ap->dev,
			 "Bad tag mask?  sactive=0x%08x (host_pvt.sata_dwc_sactive_issued)=0x%08x  tag_mask=0x%08x\n",
			 sactive, host_pvt.sata_dwc_sactive_issued, tag_mask);
	}

	/* read just to clear ... not bad if currently still busy */
	status = ap->ops->sff_check_status(ap);
	dev_dbg(ap->dev, "%s ATA status register=0x%x\n", __func__, status);

	tag = 0;
	num_processed = 0;
	while (tag_mask) {
		num_processed++;
		while (!(tag_mask & 0x00000001)) {
			tag++;
			tag_mask <<= 1;
		}

		tag_mask &= (~0x00000001);
		qc = ata_qc_from_tag(ap, tag);

		/* To be picked up by completion functions */
		qc->ap->link.active_tag = tag;
		hsdevp->cmd_issued[tag] = SATA_DWC_CMD_ISSUED_NOT;

		/* Let libata/scsi layers handle error */
		if (status & ATA_ERR) {
			dev_dbg(ap->dev, "%s ATA_ERR (0x%x)\n", __func__,
				status);
			sata_dwc_qc_complete(ap, qc, 1);
			handled = 1;
			goto DONE;
		}

		/* Process completed command */
		dev_dbg(ap->dev, "%s NCQ command, protocol: %s\n", __func__,
			get_prot_descript(qc->tf.protocol));
		if (ata_is_dma(qc->tf.protocol)) {
			hsdevp->dma_interrupt_count++;
			if (hsdevp->dma_pending[tag] == \
					SATA_DWC_DMA_PENDING_NONE)
				dev_warn(ap->dev, "%s: DMA not pending?\n",
					__func__);
			if ((hsdevp->dma_interrupt_count % 2) == 0)
				sata_dwc_dma_xfer_complete(ap, 1);
		} else {
			if (unlikely(sata_dwc_qc_complete(ap, qc, 1)))
				goto STILLBUSY;
		}
		continue;

STILLBUSY:
		ap->stats.idle_irq++;
		dev_warn(ap->dev, "STILL BUSY IRQ ata%d: irq trap\n",
			ap->print_id);
	} /* while tag_mask */

	/*
	 * Check to see if any commands completed while we were processing our
	 * initial set of completed commands (read status clears interrupts,
	 * so we might miss a completed command interrupt if one came in while
	 * we were processing --we read status as part of processing a completed
	 * command).
	 */
	sactive2 = core_scr_read(SCR_ACTIVE);
	if (sactive2 != sactive) {
		dev_dbg(ap->dev,
			"More completed - sactive=0x%x sactive2=0x%x\n",
			sactive, sactive2);
	}
	handled = 1;

DONE:
	spin_unlock_irqrestore(&host->lock, flags);
	return IRQ_RETVAL(handled);
}

static void sata_dwc_clear_dmacr(struct sata_dwc_device_port *hsdevp, u8 tag)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_HSDEVP(hsdevp);

	if (hsdevp->dma_pending[tag] == SATA_DWC_DMA_PENDING_RX) {
		out_le32(&(hsdev->sata_dwc_regs->dmacr),
			 SATA_DWC_DMACR_RX_CLEAR(
				 in_le32(&(hsdev->sata_dwc_regs->dmacr))));
	} else if (hsdevp->dma_pending[tag] == SATA_DWC_DMA_PENDING_TX) {
		out_le32(&(hsdev->sata_dwc_regs->dmacr),
			 SATA_DWC_DMACR_TX_CLEAR(
				 in_le32(&(hsdev->sata_dwc_regs->dmacr))));
	} else {
		/*
		 * This should not happen, it indicates the driver is out of
		 * sync.  If it does happen, clear dmacr anyway.
		 */
		dev_err(hsdev->dev,
			"%s DMA protocol RX and TX DMA not pending tag=0x%02x pending=%d dmacr: 0x%08x\n",
			__func__, tag, hsdevp->dma_pending[tag],
			in_le32(&hsdev->sata_dwc_regs->dmacr));
		out_le32(&(hsdev->sata_dwc_regs->dmacr),
			SATA_DWC_DMACR_TXRXCH_CLEAR);
	}
}

static void sata_dwc_dma_xfer_complete(struct ata_port *ap, u32 check_status)
{
	struct ata_queued_cmd *qc;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(ap);
	u8 tag = 0;

	tag = ap->link.active_tag;
	qc = ata_qc_from_tag(ap, tag);
	if (!qc) {
		dev_err(ap->dev, "failed to get qc");
		return;
	}

#ifdef DEBUG_NCQ
	if (tag > 0) {
		dev_info(ap->dev,
			 "%s tag=%u cmd=0x%02x dma dir=%s proto=%s dmacr=0x%08x\n",
			 __func__, qc->tag, qc->tf.command,
			 get_dma_dir_descript(qc->dma_dir),
			 get_prot_descript(qc->tf.protocol),
			 in_le32(&(hsdev->sata_dwc_regs->dmacr)));
	}
#endif

	if (ata_is_dma(qc->tf.protocol)) {
		if (hsdevp->dma_pending[tag] == SATA_DWC_DMA_PENDING_NONE) {
			dev_err(ap->dev,
				"%s DMA protocol RX and TX DMA not pending dmacr: 0x%08x\n",
				__func__,
				in_le32(&(hsdev->sata_dwc_regs->dmacr)));
		}

		hsdevp->dma_pending[tag] = SATA_DWC_DMA_PENDING_NONE;
		sata_dwc_qc_complete(ap, qc, check_status);
		ap->link.active_tag = ATA_TAG_POISON;
	} else {
		sata_dwc_qc_complete(ap, qc, check_status);
	}
}

static int sata_dwc_qc_complete(struct ata_port *ap, struct ata_queued_cmd *qc,
				u32 check_status)
{
	u8 status = 0;
	u32 mask = 0x0;
	u8 tag = qc->tag;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);
	host_pvt.sata_dwc_sactive_queued = 0;
	dev_dbg(ap->dev, "%s checkstatus? %x\n", __func__, check_status);

	if (hsdevp->dma_pending[tag] == SATA_DWC_DMA_PENDING_TX)
		dev_err(ap->dev, "TX DMA PENDING\n");
	else if (hsdevp->dma_pending[tag] == SATA_DWC_DMA_PENDING_RX)
		dev_err(ap->dev, "RX DMA PENDING\n");
	dev_dbg(ap->dev,
		"QC complete cmd=0x%02x status=0x%02x ata%u: protocol=%d\n",
		qc->tf.command, status, ap->print_id, qc->tf.protocol);

	/* clear active bit */
	mask = (~(qcmd_tag_to_mask(tag)));
	host_pvt.sata_dwc_sactive_queued = (host_pvt.sata_dwc_sactive_queued) \
						& mask;
	host_pvt.sata_dwc_sactive_issued = (host_pvt.sata_dwc_sactive_issued) \
						& mask;
	ata_qc_complete(qc);
	return 0;
}

static void sata_dwc_enable_interrupts(struct sata_dwc_device *hsdev)
{
	/* Enable selective interrupts by setting the interrupt maskregister*/
	out_le32(&hsdev->sata_dwc_regs->intmr,
		 SATA_DWC_INTMR_ERRM |
		 SATA_DWC_INTMR_NEWFPM |
		 SATA_DWC_INTMR_PMABRTM |
		 SATA_DWC_INTMR_DMATM);
	/*
	 * Unmask the error bits that should trigger an error interrupt by
	 * setting the error mask register.
	 */
	out_le32(&hsdev->sata_dwc_regs->errmr, SATA_DWC_SERROR_ERR_BITS);

	dev_dbg(hsdev->dev, "%s: INTMR = 0x%08x, ERRMR = 0x%08x\n",
		 __func__, in_le32(&hsdev->sata_dwc_regs->intmr),
		in_le32(&hsdev->sata_dwc_regs->errmr));
}

static bool sata_dwc_dma_filter(struct dma_chan *chan, void *param)
{
	struct sata_dwc_device_port *hsdevp = param;
	struct dw_dma_slave *dws = hsdevp->dws;

	if (dws->dma_dev != chan->device->dev)
		return false;

	chan->private = dws;
	return true;
}

static void sata_dwc_setup_port(struct ata_ioports *port, unsigned long base)
{
	port->cmd_addr = (void __iomem *)base + 0x00;
	port->data_addr = (void __iomem *)base + 0x00;

	port->error_addr = (void __iomem *)base + 0x04;
	port->feature_addr = (void __iomem *)base + 0x04;

	port->nsect_addr = (void __iomem *)base + 0x08;

	port->lbal_addr = (void __iomem *)base + 0x0c;
	port->lbam_addr = (void __iomem *)base + 0x10;
	port->lbah_addr = (void __iomem *)base + 0x14;

	port->device_addr = (void __iomem *)base + 0x18;
	port->command_addr = (void __iomem *)base + 0x1c;
	port->status_addr = (void __iomem *)base + 0x1c;

	port->altstatus_addr = (void __iomem *)base + 0x20;
	port->ctl_addr = (void __iomem *)base + 0x20;
}

/*
 * Function : sata_dwc_port_start
 * arguments : struct ata_ioports *port
 * Return value : returns 0 if success, error code otherwise
 * This function allocates the scatter gather LLI table for AHB DMA
 */
static int sata_dwc_port_start(struct ata_port *ap)
{
	int err = 0;
	struct sata_dwc_device *hsdev;
	struct sata_dwc_device_port *hsdevp = NULL;
	struct device *pdev;
	dma_cap_mask_t mask;
	int i;

	hsdev = HSDEV_FROM_AP(ap);

	dev_dbg(ap->dev, "%s: port_no=%d\n", __func__, ap->port_no);

	hsdev->host = ap->host;
	pdev = ap->host->dev;
	if (!pdev) {
		dev_err(ap->dev, "%s: no ap->host->dev\n", __func__);
		err = -ENODEV;
		goto CLEANUP;
	}

	/* Allocate Port Struct */
	hsdevp = kzalloc(sizeof(*hsdevp), GFP_KERNEL);
	if (!hsdevp) {
		dev_err(ap->dev, "%s: kmalloc failed for hsdevp\n", __func__);
		err = -ENOMEM;
		goto CLEANUP;
	}
	hsdevp->hsdev = hsdev;

	hsdevp->dws = &sata_dwc_dma_dws;
	hsdevp->dws->dma_dev = hsdev->dev;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Acquire DMA channel */
	hsdevp->chan = dma_request_channel(mask, sata_dwc_dma_filter, hsdevp);
	if (!hsdevp->chan) {
		dev_err(hsdev->dev, "%s: dma channel unavailable\n",
			 __func__);
		err = -EAGAIN;
		goto CLEANUP_ALLOC;
	}

	for (i = 0; i < SATA_DWC_QCMD_MAX; i++)
		hsdevp->cmd_issued[i] = SATA_DWC_CMD_ISSUED_NOT;

	ap->bmdma_prd = NULL;	/* set these so libata doesn't use them */
	ap->bmdma_prd_dma = 0;

	if (ap->port_no == 0)  {
		dev_dbg(ap->dev, "%s: clearing TXCHEN, RXCHEN in DMAC\n",
			__func__);
		out_le32(&hsdev->sata_dwc_regs->dmacr,
			 SATA_DWC_DMACR_TXRXCH_CLEAR);

		dev_dbg(ap->dev, "%s: setting burst size in DBTSR\n",
			 __func__);
		out_le32(&hsdev->sata_dwc_regs->dbtsr,
			 (SATA_DWC_DBTSR_MWR(AHB_DMA_BRST_DFLT) |
			  SATA_DWC_DBTSR_MRD(AHB_DMA_BRST_DFLT)));
	}

	/* Clear any error bits before libata starts issuing commands */
	clear_serror();
	ap->private_data = hsdevp;
	dev_dbg(ap->dev, "%s: done\n", __func__);
	return 0;

CLEANUP_ALLOC:
	kfree(hsdevp);
CLEANUP:
	dev_dbg(ap->dev, "%s: fail. ap->id = %d\n", __func__, ap->print_id);
	return err;
}

static void sata_dwc_port_stop(struct ata_port *ap)
{
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);

	dev_dbg(ap->dev, "%s: ap->id = %d\n", __func__, ap->print_id);

	dmaengine_terminate_all(hsdevp->chan);
	dma_release_channel(hsdevp->chan);

	kfree(hsdevp);
	ap->private_data = NULL;
}

/*
 * Function : sata_dwc_exec_command_by_tag
 * arguments : ata_port *ap, ata_taskfile *tf, u8 tag, u32 cmd_issued
 * Return value : None
 * This function keeps track of individual command tag ids and calls
 * ata_exec_command in libata
 */
static void sata_dwc_exec_command_by_tag(struct ata_port *ap,
					 struct ata_taskfile *tf,
					 u8 tag, u32 cmd_issued)
{
	unsigned long flags;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);

	dev_dbg(ap->dev, "%s cmd(0x%02x): %s tag=%d\n", __func__, tf->command,
		ata_get_cmd_descript(tf->command), tag);

	spin_lock_irqsave(&ap->host->lock, flags);
	hsdevp->cmd_issued[tag] = cmd_issued;
	spin_unlock_irqrestore(&ap->host->lock, flags);
	/*
	 * Clear SError before executing a new command.
	 * sata_dwc_scr_write and read can not be used here. Clearing the PM
	 * managed SError register for the disk needs to be done before the
	 * task file is loaded.
	 */
	clear_serror();
	ata_sff_exec_command(ap, tf);
}

static void sata_dwc_bmdma_setup_by_tag(struct ata_queued_cmd *qc, u8 tag)
{
	sata_dwc_exec_command_by_tag(qc->ap, &qc->tf, tag,
				     SATA_DWC_CMD_ISSUED_PEND);
}

static void sata_dwc_bmdma_setup(struct ata_queued_cmd *qc)
{
	u8 tag = qc->tag;

	if (ata_is_ncq(qc->tf.protocol)) {
		dev_dbg(qc->ap->dev, "%s: ap->link.sactive=0x%08x tag=%d\n",
			__func__, qc->ap->link.sactive, tag);
	} else {
		tag = 0;
	}
	sata_dwc_bmdma_setup_by_tag(qc, tag);
}

static void sata_dwc_bmdma_start_by_tag(struct ata_queued_cmd *qc, u8 tag)
{
	int start_dma;
	u32 reg;
	struct sata_dwc_device *hsdev = HSDEV_FROM_QC(qc);
	struct ata_port *ap = qc->ap;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);
	struct dma_async_tx_descriptor *desc = hsdevp->desc[tag];
	int dir = qc->dma_dir;

	if (hsdevp->cmd_issued[tag] != SATA_DWC_CMD_ISSUED_NOT) {
		start_dma = 1;
		if (dir == DMA_TO_DEVICE)
			hsdevp->dma_pending[tag] = SATA_DWC_DMA_PENDING_TX;
		else
			hsdevp->dma_pending[tag] = SATA_DWC_DMA_PENDING_RX;
	} else {
		dev_err(ap->dev,
			"%s: Command not pending cmd_issued=%d (tag=%d) DMA NOT started\n",
			__func__, hsdevp->cmd_issued[tag], tag);
		start_dma = 0;
	}

	dev_dbg(ap->dev,
		"%s qc=%p tag: %x cmd: 0x%02x dma_dir: %s start_dma? %x\n",
		__func__, qc, tag, qc->tf.command,
		get_dma_dir_descript(qc->dma_dir), start_dma);
	sata_dwc_tf_dump(ap, &qc->tf);

	if (start_dma) {
		reg = core_scr_read(SCR_ERROR);
		if (reg & SATA_DWC_SERROR_ERR_BITS) {
			dev_err(ap->dev, "%s: ****** SError=0x%08x ******\n",
				__func__, reg);
		}

		if (dir == DMA_TO_DEVICE)
			out_le32(&hsdev->sata_dwc_regs->dmacr,
				SATA_DWC_DMACR_TXCHEN);
		else
			out_le32(&hsdev->sata_dwc_regs->dmacr,
				SATA_DWC_DMACR_RXCHEN);

		/* Enable AHB DMA transfer on the specified channel */
		dmaengine_submit(desc);
		dma_async_issue_pending(hsdevp->chan);
	}
}

static void sata_dwc_bmdma_start(struct ata_queued_cmd *qc)
{
	u8 tag = qc->tag;

	if (ata_is_ncq(qc->tf.protocol)) {
		dev_dbg(qc->ap->dev, "%s: ap->link.sactive=0x%08x tag=%d\n",
			__func__, qc->ap->link.sactive, tag);
	} else {
		tag = 0;
	}
	dev_dbg(qc->ap->dev, "%s\n", __func__);
	sata_dwc_bmdma_start_by_tag(qc, tag);
}

/*
 * Function : sata_dwc_qc_prep_by_tag
 * arguments : ata_queued_cmd *qc, u8 tag
 * Return value : None
 * qc_prep for a particular queued command based on tag
 */
static void sata_dwc_qc_prep_by_tag(struct ata_queued_cmd *qc, u8 tag)
{
	struct dma_async_tx_descriptor *desc;
	struct ata_port *ap = qc->ap;
	struct sata_dwc_device_port *hsdevp = HSDEVP_FROM_AP(ap);

	dev_dbg(ap->dev, "%s: port=%d dma dir=%s n_elem=%d\n",
		__func__, ap->port_no, get_dma_dir_descript(qc->dma_dir),
		 qc->n_elem);

	desc = dma_dwc_xfer_setup(qc);
	if (!desc) {
		dev_err(ap->dev, "%s: dma_dwc_xfer_setup returns NULL\n",
			__func__);
		return;
	}
	hsdevp->desc[tag] = desc;
}

static unsigned int sata_dwc_qc_issue(struct ata_queued_cmd *qc)
{
	u32 sactive;
	u8 tag = qc->tag;
	struct ata_port *ap = qc->ap;

#ifdef DEBUG_NCQ
	if (qc->tag > 0 || ap->link.sactive > 1)
		dev_info(ap->dev,
			 "%s ap id=%d cmd(0x%02x)=%s qc tag=%d prot=%s ap active_tag=0x%08x ap sactive=0x%08x\n",
			 __func__, ap->print_id, qc->tf.command,
			 ata_get_cmd_descript(qc->tf.command),
			 qc->tag, get_prot_descript(qc->tf.protocol),
			 ap->link.active_tag, ap->link.sactive);
#endif

	if (!ata_is_ncq(qc->tf.protocol))
		tag = 0;
	sata_dwc_qc_prep_by_tag(qc, tag);

	if (ata_is_ncq(qc->tf.protocol)) {
		sactive = core_scr_read(SCR_ACTIVE);
		sactive |= (0x00000001 << tag);
		core_scr_write(SCR_ACTIVE, sactive);

		dev_dbg(qc->ap->dev,
			"%s: tag=%d ap->link.sactive = 0x%08x sactive=0x%08x\n",
			__func__, tag, qc->ap->link.sactive, sactive);

		ap->ops->sff_tf_load(ap, &qc->tf);
		sata_dwc_exec_command_by_tag(ap, &qc->tf, qc->tag,
					     SATA_DWC_CMD_ISSUED_PEND);
	} else {
		ata_sff_qc_issue(qc);
	}
	return 0;
}

/*
 * Function : sata_dwc_qc_prep
 * arguments : ata_queued_cmd *qc
 * Return value : None
 * qc_prep for a particular queued command
 */

static void sata_dwc_qc_prep(struct ata_queued_cmd *qc)
{
	if ((qc->dma_dir == DMA_NONE) || (qc->tf.protocol == ATA_PROT_PIO))
		return;

#ifdef DEBUG_NCQ
	if (qc->tag > 0)
		dev_info(qc->ap->dev, "%s: qc->tag=%d ap->active_tag=0x%08x\n",
			 __func__, qc->tag, qc->ap->link.active_tag);

	return ;
#endif
}

static void sata_dwc_error_handler(struct ata_port *ap)
{
	ata_sff_error_handler(ap);
}

static int sata_dwc_hardreset(struct ata_link *link, unsigned int *class,
			      unsigned long deadline)
{
	struct sata_dwc_device *hsdev = HSDEV_FROM_AP(link->ap);
	int ret;

	ret = sata_sff_hardreset(link, class, deadline);

	sata_dwc_enable_interrupts(hsdev);

	/* Reconfigure the DMA control register */
	out_le32(&hsdev->sata_dwc_regs->dmacr,
		 SATA_DWC_DMACR_TXRXCH_CLEAR);

	/* Reconfigure the DMA Burst Transaction Size register */
	out_le32(&hsdev->sata_dwc_regs->dbtsr,
		 SATA_DWC_DBTSR_MWR(AHB_DMA_BRST_DFLT) |
		 SATA_DWC_DBTSR_MRD(AHB_DMA_BRST_DFLT));

	return ret;
}

/*
 * scsi mid-layer and libata interface structures
 */
static struct scsi_host_template sata_dwc_sht = {
	ATA_NCQ_SHT(DRV_NAME),
	/*
	 * test-only: Currently this driver doesn't handle NCQ
	 * correctly. We enable NCQ but set the queue depth to a
	 * max of 1. This will get fixed in in a future release.
	 */
	.sg_tablesize		= LIBATA_MAX_PRD,
	/* .can_queue		= ATA_MAX_QUEUE, */
	.dma_boundary		= ATA_DMA_BOUNDARY,
};

static struct ata_port_operations sata_dwc_ops = {
	.inherits		= &ata_sff_port_ops,

	.error_handler		= sata_dwc_error_handler,
	.hardreset		= sata_dwc_hardreset,

	.qc_prep		= sata_dwc_qc_prep,
	.qc_issue		= sata_dwc_qc_issue,

	.scr_read		= sata_dwc_scr_read,
	.scr_write		= sata_dwc_scr_write,

	.port_start		= sata_dwc_port_start,
	.port_stop		= sata_dwc_port_stop,

	.bmdma_setup		= sata_dwc_bmdma_setup,
	.bmdma_start		= sata_dwc_bmdma_start,
};

static const struct ata_port_info sata_dwc_port_info[] = {
	{
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NCQ,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &sata_dwc_ops,
	},
};

static int sata_dwc_probe(struct platform_device *ofdev)
{
	struct sata_dwc_device *hsdev;
	u32 idr, versionr;
	char *ver = (char *)&versionr;
	u8 __iomem *base;
	int err = 0;
	int irq;
	struct ata_host *host;
	struct ata_port_info pi = sata_dwc_port_info[0];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct device_node *np = ofdev->dev.of_node;

	/* Allocate DWC SATA device */
	host = ata_host_alloc_pinfo(&ofdev->dev, ppi, SATA_DWC_MAX_PORTS);
	hsdev = devm_kzalloc(&ofdev->dev, sizeof(*hsdev), GFP_KERNEL);
	if (!host || !hsdev)
		return -ENOMEM;

	host->private_data = hsdev;

	/* Ioremap SATA registers */
	base = of_iomap(np, 0);
	if (!base) {
		dev_err(&ofdev->dev,
			"ioremap failed for SATA register address\n");
		return -ENODEV;
	}
	hsdev->reg_base = base;
	dev_dbg(&ofdev->dev, "ioremap done for SATA register address\n");

	/* Synopsys DWC SATA specific Registers */
	hsdev->sata_dwc_regs = (void *__iomem)(base + SATA_DWC_REG_OFFSET);

	/* Setup port */
	host->ports[0]->ioaddr.cmd_addr = base;
	host->ports[0]->ioaddr.scr_addr = base + SATA_DWC_SCR_OFFSET;
	host_pvt.scr_addr_sstatus = base + SATA_DWC_SCR_OFFSET;
	sata_dwc_setup_port(&host->ports[0]->ioaddr, (unsigned long)base);

	/* Read the ID and Version Registers */
	idr = in_le32(&hsdev->sata_dwc_regs->idr);
	versionr = in_le32(&hsdev->sata_dwc_regs->versionr);
	dev_notice(&ofdev->dev, "id %d, controller version %c.%c%c\n",
		   idr, ver[0], ver[1], ver[2]);

	/* Get SATA DMA interrupt number */
	hsdev->dma->irq = irq_of_parse_and_map(np, 1);
	if (hsdev->dma->irq == NO_IRQ) {
		dev_err(&ofdev->dev, "no SATA DMA irq\n");
		err = -ENODEV;
		goto error_iomap;
	}

	/* Get physical SATA DMA register base address */
	hsdev->dma->regs = of_iomap(np, 1);
	if (!hsdev->dma->regs) {
		dev_err(&ofdev->dev,
			"ioremap failed for AHBDMA register address\n");
		err = -ENODEV;
		goto error_iomap;
	}

	/* Save dev for later use in dev_xxx() routines */
	hsdev->dev = &ofdev->dev;

	hsdev->dma->dev = &ofdev->dev;

	/* Initialize AHB DMAC */
	err = dw_dma_probe(hsdev->dma);
	if (err)
		goto error_dma_iomap;

	/* Enable SATA Interrupts */
	sata_dwc_enable_interrupts(hsdev);

	/* Get SATA interrupt number */
	irq = irq_of_parse_and_map(np, 0);
	if (irq == NO_IRQ) {
		dev_err(&ofdev->dev, "no SATA DMA irq\n");
		err = -ENODEV;
		goto error_out;
	}

	/*
	 * Now, register with libATA core, this will also initiate the
	 * device discovery process, invoking our port_start() handler &
	 * error_handler() to execute a dummy Softreset EH session
	 */
	err = ata_host_activate(host, irq, sata_dwc_isr, 0, &sata_dwc_sht);
	if (err)
		dev_err(&ofdev->dev, "failed to activate host");

	dev_set_drvdata(&ofdev->dev, host);
	return 0;

error_out:
	/* Free SATA DMA resources */
	dw_dma_remove(hsdev->dma);
error_dma_iomap:
	iounmap(hsdev->dma->regs);
error_iomap:
	iounmap(base);
	return err;
}

static int sata_dwc_remove(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct ata_host *host = dev_get_drvdata(dev);
	struct sata_dwc_device *hsdev = host->private_data;

	ata_host_detach(host);

	/* Free SATA DMA resources */
	dw_dma_remove(hsdev->dma);

	iounmap(hsdev->dma->regs);
	iounmap(hsdev->reg_base);
	dev_dbg(&ofdev->dev, "done\n");
	return 0;
}

static const struct of_device_id sata_dwc_match[] = {
	{ .compatible = "amcc,sata-460ex", },
	{}
};
MODULE_DEVICE_TABLE(of, sata_dwc_match);

static struct platform_driver sata_dwc_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = sata_dwc_match,
	},
	.probe = sata_dwc_probe,
	.remove = sata_dwc_remove,
};

module_platform_driver(sata_dwc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Miesfeld <mmiesfeld@amcc.com>");
MODULE_DESCRIPTION("DesignWare Cores SATA controller low level driver");
MODULE_VERSION(DRV_VERSION);
