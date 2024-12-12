// SPDX-License-Identifier: GPL-2.0
/*
 * QMC driver
 *
 * Copyright 2022 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include <soc/fsl/qe/qmc.h>
#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/hdlc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <soc/fsl/cpm.h>
#include <soc/fsl/qe/ucc_slow.h>
#include <soc/fsl/qe/qe.h>
#include <sysdev/fsl_soc.h>
#include "tsa.h"

/* SCC general mode register low (32 bits) (GUMR_L in QE) */
#define SCC_GSMRL	0x00
#define SCC_GSMRL_ENR		BIT(5)
#define SCC_GSMRL_ENT		BIT(4)
#define SCC_GSMRL_MODE_MASK	GENMASK(3, 0)
#define SCC_CPM1_GSMRL_MODE_QMC	FIELD_PREP_CONST(SCC_GSMRL_MODE_MASK, 0x0A)
#define SCC_QE_GSMRL_MODE_QMC	FIELD_PREP_CONST(SCC_GSMRL_MODE_MASK, 0x02)

/* SCC general mode register high (32 bits) (identical to GUMR_H in QE) */
#define SCC_GSMRH	0x04
#define   SCC_GSMRH_CTSS	BIT(7)
#define   SCC_GSMRH_CDS		BIT(8)
#define   SCC_GSMRH_CTSP	BIT(9)
#define   SCC_GSMRH_CDP		BIT(10)
#define   SCC_GSMRH_TTX		BIT(11)
#define   SCC_GSMRH_TRX		BIT(12)

/* SCC event register (16 bits) (identical to UCCE in QE) */
#define SCC_SCCE	0x10
#define   SCC_SCCE_IQOV		BIT(3)
#define   SCC_SCCE_GINT		BIT(2)
#define   SCC_SCCE_GUN		BIT(1)
#define   SCC_SCCE_GOV		BIT(0)

/* SCC mask register (16 bits) */
#define SCC_SCCM	0x14

/* UCC Extended Mode Register (8 bits, QE only) */
#define SCC_QE_UCC_GUEMR	0x90

/* Multichannel base pointer (32 bits) */
#define QMC_GBL_MCBASE		0x00
/* Multichannel controller state (16 bits) */
#define QMC_GBL_QMCSTATE	0x04
/* Maximum receive buffer length (16 bits) */
#define QMC_GBL_MRBLR		0x06
/* Tx time-slot assignment table pointer (16 bits) */
#define QMC_GBL_TX_S_PTR	0x08
/* Rx pointer (16 bits) */
#define QMC_GBL_RXPTR		0x0A
/* Global receive frame threshold (16 bits) */
#define QMC_GBL_GRFTHR		0x0C
/* Global receive frame count (16 bits) */
#define QMC_GBL_GRFCNT		0x0E
/* Multichannel interrupt base address (32 bits) */
#define QMC_GBL_INTBASE		0x10
/* Multichannel interrupt pointer (32 bits) */
#define QMC_GBL_INTPTR		0x14
/* Rx time-slot assignment table pointer (16 bits) */
#define QMC_GBL_RX_S_PTR	0x18
/* Tx pointer (16 bits) */
#define QMC_GBL_TXPTR		0x1A
/* CRC constant (32 bits) */
#define QMC_GBL_C_MASK32	0x1C
/* Time slot assignment table Rx (32 x 16 bits) */
#define QMC_GBL_TSATRX		0x20
/* Time slot assignment table Tx (32 x 16 bits) */
#define QMC_GBL_TSATTX		0x60
/* CRC constant (16 bits) */
#define QMC_GBL_C_MASK16	0xA0
/* Rx framer base pointer (16 bits, QE only) */
#define QMC_QE_GBL_RX_FRM_BASE	0xAC
/* Tx framer base pointer (16 bits, QE only) */
#define QMC_QE_GBL_TX_FRM_BASE	0xAE
/* A reserved area (0xB0 -> 0xC3) that must be initialized to 0 (QE only) */
#define QMC_QE_GBL_RSV_B0_START	0xB0
#define QMC_QE_GBL_RSV_B0_SIZE	0x14
/* QMC Global Channel specific base (32 bits, QE only) */
#define QMC_QE_GBL_GCSBASE	0xC4

/* TSA entry (16bit entry in TSATRX and TSATTX) */
#define QMC_TSA_VALID		BIT(15)
#define QMC_TSA_WRAP		BIT(14)
#define QMC_TSA_MASK_MASKH	GENMASK(13, 12)
#define QMC_TSA_MASK_MASKL	GENMASK(5, 0)
#define QMC_TSA_MASK_8BIT	(FIELD_PREP_CONST(QMC_TSA_MASK_MASKH, 0x3) | \
				 FIELD_PREP_CONST(QMC_TSA_MASK_MASKL, 0x3F))
#define QMC_TSA_CHANNEL_MASK	GENMASK(11, 6)
#define QMC_TSA_CHANNEL(x)	FIELD_PREP(QMC_TSA_CHANNEL_MASK, x)

/* Tx buffer descriptor base address (16 bits, offset from MCBASE) */
#define QMC_SPE_TBASE	0x00

/* Channel mode register (16 bits) */
#define QMC_SPE_CHAMR	0x02
#define   QMC_SPE_CHAMR_MODE_MASK	GENMASK(15, 15)
#define   QMC_SPE_CHAMR_MODE_HDLC	FIELD_PREP_CONST(QMC_SPE_CHAMR_MODE_MASK, 1)
#define   QMC_SPE_CHAMR_MODE_TRANSP	(FIELD_PREP_CONST(QMC_SPE_CHAMR_MODE_MASK, 0) | BIT(13))
#define   QMC_SPE_CHAMR_ENT		BIT(12)
#define   QMC_SPE_CHAMR_POL		BIT(8)
#define   QMC_SPE_CHAMR_HDLC_IDLM	BIT(13)
#define   QMC_SPE_CHAMR_HDLC_CRC	BIT(7)
#define   QMC_SPE_CHAMR_HDLC_NOF_MASK	GENMASK(3, 0)
#define   QMC_SPE_CHAMR_HDLC_NOF(x)	FIELD_PREP(QMC_SPE_CHAMR_HDLC_NOF_MASK, x)
#define   QMC_SPE_CHAMR_TRANSP_RD	BIT(14)
#define   QMC_SPE_CHAMR_TRANSP_SYNC	BIT(10)

/* Tx internal state (32 bits) */
#define QMC_SPE_TSTATE	0x04
/* Tx buffer descriptor pointer (16 bits) */
#define QMC_SPE_TBPTR	0x0C
/* Zero-insertion state (32 bits) */
#define QMC_SPE_ZISTATE	0x14
/* Channel’s interrupt mask flags (16 bits) */
#define QMC_SPE_INTMSK	0x1C
/* Rx buffer descriptor base address (16 bits, offset from MCBASE) */
#define QMC_SPE_RBASE	0x20
/* HDLC: Maximum frame length register (16 bits) */
#define QMC_SPE_MFLR	0x22
/* TRANSPARENT: Transparent maximum receive length (16 bits) */
#define QMC_SPE_TMRBLR	0x22
/* Rx internal state (32 bits) */
#define QMC_SPE_RSTATE	0x24
/* Rx buffer descriptor pointer (16 bits) */
#define QMC_SPE_RBPTR	0x2C
/* Packs 4 bytes to 1 long word before writing to buffer (32 bits) */
#define QMC_SPE_RPACK	0x30
/* Zero deletion state (32 bits) */
#define QMC_SPE_ZDSTATE	0x34

/* Transparent synchronization (16 bits) */
#define QMC_SPE_TRNSYNC 0x3C
#define   QMC_SPE_TRNSYNC_RX_MASK	GENMASK(15, 8)
#define   QMC_SPE_TRNSYNC_RX(x)		FIELD_PREP(QMC_SPE_TRNSYNC_RX_MASK, x)
#define   QMC_SPE_TRNSYNC_TX_MASK	GENMASK(7, 0)
#define   QMC_SPE_TRNSYNC_TX(x)		FIELD_PREP(QMC_SPE_TRNSYNC_TX_MASK, x)

/* Interrupt related registers bits */
#define QMC_INT_V		BIT(15)
#define QMC_INT_W		BIT(14)
#define QMC_INT_NID		BIT(13)
#define QMC_INT_IDL		BIT(12)
#define QMC_INT_CHANNEL_MASK	GENMASK(11, 6)
#define QMC_INT_GET_CHANNEL(x)	FIELD_GET(QMC_INT_CHANNEL_MASK, x)
#define QMC_INT_MRF		BIT(5)
#define QMC_INT_UN		BIT(4)
#define QMC_INT_RXF		BIT(3)
#define QMC_INT_BSY		BIT(2)
#define QMC_INT_TXB		BIT(1)
#define QMC_INT_RXB		BIT(0)

/* BD related registers bits */
#define QMC_BD_RX_E	BIT(15)
#define QMC_BD_RX_W	BIT(13)
#define QMC_BD_RX_I	BIT(12)
#define QMC_BD_RX_L	BIT(11)
#define QMC_BD_RX_F	BIT(10)
#define QMC_BD_RX_CM	BIT(9)
#define QMC_BD_RX_UB	BIT(7)
#define QMC_BD_RX_LG	BIT(5)
#define QMC_BD_RX_NO	BIT(4)
#define QMC_BD_RX_AB	BIT(3)
#define QMC_BD_RX_CR	BIT(2)

#define QMC_BD_TX_R		BIT(15)
#define QMC_BD_TX_W		BIT(13)
#define QMC_BD_TX_I		BIT(12)
#define QMC_BD_TX_L		BIT(11)
#define QMC_BD_TX_TC		BIT(10)
#define QMC_BD_TX_CM		BIT(9)
#define QMC_BD_TX_UB		BIT(7)
#define QMC_BD_TX_PAD_MASK	GENMASK(3, 0)
#define QMC_BD_TX_PAD(x)	FIELD_PREP(QMC_BD_TX_PAD_MASK, x)

/* Numbers of BDs and interrupt items */
#define QMC_NB_TXBDS	8
#define QMC_NB_RXBDS	8
#define QMC_NB_INTS	128

struct qmc_xfer_desc {
	union {
		void (*tx_complete)(void *context);
		void (*rx_complete)(void *context, size_t length, unsigned int flags);
	};
	void *context;
};

struct qmc_chan {
	struct list_head list;
	unsigned int id;
	struct qmc *qmc;
	void __iomem *s_param;
	enum qmc_mode mode;
	spinlock_t	ts_lock; /* Protect timeslots */
	u64	tx_ts_mask_avail;
	u64	tx_ts_mask;
	u64	rx_ts_mask_avail;
	u64	rx_ts_mask;
	bool is_reverse_data;

	spinlock_t	tx_lock; /* Protect Tx related data */
	cbd_t __iomem *txbds;
	cbd_t __iomem *txbd_free;
	cbd_t __iomem *txbd_done;
	struct qmc_xfer_desc tx_desc[QMC_NB_TXBDS];
	u64	nb_tx_underrun;
	bool	is_tx_stopped;

	spinlock_t	rx_lock; /* Protect Rx related data */
	cbd_t __iomem *rxbds;
	cbd_t __iomem *rxbd_free;
	cbd_t __iomem *rxbd_done;
	struct qmc_xfer_desc rx_desc[QMC_NB_RXBDS];
	u64	nb_rx_busy;
	int	rx_pending;
	bool	is_rx_halted;
	bool	is_rx_stopped;
};

enum qmc_version {
	QMC_CPM1,
	QMC_QE,
};

struct qmc_data {
	enum qmc_version version;
	u32 tstate; /* Initial TSTATE value */
	u32 rstate; /* Initial RSTATE value */
	u32 zistate; /* Initial ZISTATE value */
	u32 zdstate_hdlc; /* Initial ZDSTATE value (HDLC mode) */
	u32 zdstate_transp; /* Initial ZDSTATE value (Transparent mode) */
	u32 rpack; /* Initial RPACK value */
};

struct qmc {
	struct device *dev;
	const struct qmc_data *data;
	struct tsa_serial *tsa_serial;
	void __iomem *scc_regs;
	void __iomem *scc_pram;
	void __iomem *dpram;
	u16 scc_pram_offset;
	u32 dpram_offset;
	u32 qe_subblock;
	cbd_t __iomem *bd_table;
	dma_addr_t bd_dma_addr;
	size_t bd_size;
	u16 __iomem *int_table;
	u16 __iomem *int_curr;
	dma_addr_t int_dma_addr;
	size_t int_size;
	bool is_tsa_64rxtx;
	struct list_head chan_head;
	struct qmc_chan *chans[64];
};

static void qmc_write8(void __iomem *addr, u8 val)
{
	iowrite8(val, addr);
}

static void qmc_write16(void __iomem *addr, u16 val)
{
	iowrite16be(val, addr);
}

static u16 qmc_read16(void __iomem *addr)
{
	return ioread16be(addr);
}

static void qmc_setbits16(void __iomem *addr, u16 set)
{
	qmc_write16(addr, qmc_read16(addr) | set);
}

static void qmc_clrbits16(void __iomem *addr, u16 clr)
{
	qmc_write16(addr, qmc_read16(addr) & ~clr);
}

static void qmc_clrsetbits16(void __iomem *addr, u16 clr, u16 set)
{
	qmc_write16(addr, (qmc_read16(addr) & ~clr) | set);
}

static void qmc_write32(void __iomem *addr, u32 val)
{
	iowrite32be(val, addr);
}

static u32 qmc_read32(void __iomem *addr)
{
	return ioread32be(addr);
}

static void qmc_setbits32(void __iomem *addr, u32 set)
{
	qmc_write32(addr, qmc_read32(addr) | set);
}

static bool qmc_is_qe(const struct qmc *qmc)
{
	if (IS_ENABLED(CONFIG_QUICC_ENGINE) && IS_ENABLED(CONFIG_CPM))
		return qmc->data->version == QMC_QE;

	return IS_ENABLED(CONFIG_QUICC_ENGINE);
}

int qmc_chan_get_info(struct qmc_chan *chan, struct qmc_chan_info *info)
{
	struct tsa_serial_info tsa_info;
	unsigned long flags;
	int ret;

	/* Retrieve info from the TSA related serial */
	ret = tsa_serial_get_info(chan->qmc->tsa_serial, &tsa_info);
	if (ret)
		return ret;

	spin_lock_irqsave(&chan->ts_lock, flags);

	info->mode = chan->mode;
	info->rx_fs_rate = tsa_info.rx_fs_rate;
	info->rx_bit_rate = tsa_info.rx_bit_rate;
	info->nb_tx_ts = hweight64(chan->tx_ts_mask);
	info->tx_fs_rate = tsa_info.tx_fs_rate;
	info->tx_bit_rate = tsa_info.tx_bit_rate;
	info->nb_rx_ts = hweight64(chan->rx_ts_mask);

	spin_unlock_irqrestore(&chan->ts_lock, flags);

	return 0;
}
EXPORT_SYMBOL(qmc_chan_get_info);

int qmc_chan_get_ts_info(struct qmc_chan *chan, struct qmc_chan_ts_info *ts_info)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->ts_lock, flags);

	ts_info->rx_ts_mask_avail = chan->rx_ts_mask_avail;
	ts_info->tx_ts_mask_avail = chan->tx_ts_mask_avail;
	ts_info->rx_ts_mask = chan->rx_ts_mask;
	ts_info->tx_ts_mask = chan->tx_ts_mask;

	spin_unlock_irqrestore(&chan->ts_lock, flags);

	return 0;
}
EXPORT_SYMBOL(qmc_chan_get_ts_info);

int qmc_chan_set_ts_info(struct qmc_chan *chan, const struct qmc_chan_ts_info *ts_info)
{
	unsigned long flags;
	int ret;

	/* Only a subset of available timeslots is allowed */
	if ((ts_info->rx_ts_mask & chan->rx_ts_mask_avail) != ts_info->rx_ts_mask)
		return -EINVAL;
	if ((ts_info->tx_ts_mask & chan->tx_ts_mask_avail) != ts_info->tx_ts_mask)
		return -EINVAL;

	/* In case of common rx/tx table, rx/tx masks must be identical */
	if (chan->qmc->is_tsa_64rxtx) {
		if (ts_info->rx_ts_mask != ts_info->tx_ts_mask)
			return -EINVAL;
	}

	spin_lock_irqsave(&chan->ts_lock, flags);

	if ((chan->tx_ts_mask != ts_info->tx_ts_mask && !chan->is_tx_stopped) ||
	    (chan->rx_ts_mask != ts_info->rx_ts_mask && !chan->is_rx_stopped)) {
		dev_err(chan->qmc->dev, "Channel rx and/or tx not stopped\n");
		ret = -EBUSY;
	} else {
		chan->tx_ts_mask = ts_info->tx_ts_mask;
		chan->rx_ts_mask = ts_info->rx_ts_mask;
		ret = 0;
	}
	spin_unlock_irqrestore(&chan->ts_lock, flags);

	return ret;
}
EXPORT_SYMBOL(qmc_chan_set_ts_info);

int qmc_chan_set_param(struct qmc_chan *chan, const struct qmc_chan_param *param)
{
	if (param->mode != chan->mode)
		return -EINVAL;

	switch (param->mode) {
	case QMC_HDLC:
		if (param->hdlc.max_rx_buf_size % 4 ||
		    param->hdlc.max_rx_buf_size < 8)
			return -EINVAL;

		qmc_write16(chan->qmc->scc_pram + QMC_GBL_MRBLR,
			    param->hdlc.max_rx_buf_size - 8);
		qmc_write16(chan->s_param + QMC_SPE_MFLR,
			    param->hdlc.max_rx_frame_size);
		if (param->hdlc.is_crc32) {
			qmc_setbits16(chan->s_param + QMC_SPE_CHAMR,
				      QMC_SPE_CHAMR_HDLC_CRC);
		} else {
			qmc_clrbits16(chan->s_param + QMC_SPE_CHAMR,
				      QMC_SPE_CHAMR_HDLC_CRC);
		}
		break;

	case QMC_TRANSPARENT:
		qmc_write16(chan->s_param + QMC_SPE_TMRBLR,
			    param->transp.max_rx_buf_size);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(qmc_chan_set_param);

int qmc_chan_write_submit(struct qmc_chan *chan, dma_addr_t addr, size_t length,
			  void (*complete)(void *context), void *context)
{
	struct qmc_xfer_desc *xfer_desc;
	unsigned long flags;
	cbd_t __iomem *bd;
	u16 ctrl;
	int ret;

	/*
	 * R bit  UB bit
	 *   0       0  : The BD is free
	 *   1       1  : The BD is in used, waiting for transfer
	 *   0       1  : The BD is in used, waiting for completion
	 *   1       0  : Should not append
	 */

	spin_lock_irqsave(&chan->tx_lock, flags);
	bd = chan->txbd_free;

	ctrl = qmc_read16(&bd->cbd_sc);
	if (ctrl & (QMC_BD_TX_R | QMC_BD_TX_UB)) {
		/* We are full ... */
		ret = -EBUSY;
		goto end;
	}

	qmc_write16(&bd->cbd_datlen, length);
	qmc_write32(&bd->cbd_bufaddr, addr);

	xfer_desc = &chan->tx_desc[bd - chan->txbds];
	xfer_desc->tx_complete = complete;
	xfer_desc->context = context;

	/* Activate the descriptor */
	ctrl |= (QMC_BD_TX_R | QMC_BD_TX_UB);
	wmb(); /* Be sure to flush the descriptor before control update */
	qmc_write16(&bd->cbd_sc, ctrl);

	if (!chan->is_tx_stopped)
		qmc_setbits16(chan->s_param + QMC_SPE_CHAMR, QMC_SPE_CHAMR_POL);

	if (ctrl & QMC_BD_TX_W)
		chan->txbd_free = chan->txbds;
	else
		chan->txbd_free++;

	ret = 0;

end:
	spin_unlock_irqrestore(&chan->tx_lock, flags);
	return ret;
}
EXPORT_SYMBOL(qmc_chan_write_submit);

static void qmc_chan_write_done(struct qmc_chan *chan)
{
	struct qmc_xfer_desc *xfer_desc;
	void (*complete)(void *context);
	unsigned long flags;
	void *context;
	cbd_t __iomem *bd;
	u16 ctrl;

	/*
	 * R bit  UB bit
	 *   0       0  : The BD is free
	 *   1       1  : The BD is in used, waiting for transfer
	 *   0       1  : The BD is in used, waiting for completion
	 *   1       0  : Should not append
	 */

	spin_lock_irqsave(&chan->tx_lock, flags);
	bd = chan->txbd_done;

	ctrl = qmc_read16(&bd->cbd_sc);
	while (!(ctrl & QMC_BD_TX_R)) {
		if (!(ctrl & QMC_BD_TX_UB))
			goto end;

		xfer_desc = &chan->tx_desc[bd - chan->txbds];
		complete = xfer_desc->tx_complete;
		context = xfer_desc->context;
		xfer_desc->tx_complete = NULL;
		xfer_desc->context = NULL;

		qmc_write16(&bd->cbd_sc, ctrl & ~QMC_BD_TX_UB);

		if (ctrl & QMC_BD_TX_W)
			chan->txbd_done = chan->txbds;
		else
			chan->txbd_done++;

		if (complete) {
			spin_unlock_irqrestore(&chan->tx_lock, flags);
			complete(context);
			spin_lock_irqsave(&chan->tx_lock, flags);
		}

		bd = chan->txbd_done;
		ctrl = qmc_read16(&bd->cbd_sc);
	}

end:
	spin_unlock_irqrestore(&chan->tx_lock, flags);
}

int qmc_chan_read_submit(struct qmc_chan *chan, dma_addr_t addr, size_t length,
			 void (*complete)(void *context, size_t length, unsigned int flags),
			 void *context)
{
	struct qmc_xfer_desc *xfer_desc;
	unsigned long flags;
	cbd_t __iomem *bd;
	u16 ctrl;
	int ret;

	/*
	 * E bit  UB bit
	 *   0       0  : The BD is free
	 *   1       1  : The BD is in used, waiting for transfer
	 *   0       1  : The BD is in used, waiting for completion
	 *   1       0  : Should not append
	 */

	spin_lock_irqsave(&chan->rx_lock, flags);
	bd = chan->rxbd_free;

	ctrl = qmc_read16(&bd->cbd_sc);
	if (ctrl & (QMC_BD_RX_E | QMC_BD_RX_UB)) {
		/* We are full ... */
		ret = -EBUSY;
		goto end;
	}

	qmc_write16(&bd->cbd_datlen, 0); /* data length is updated by the QMC */
	qmc_write32(&bd->cbd_bufaddr, addr);

	xfer_desc = &chan->rx_desc[bd - chan->rxbds];
	xfer_desc->rx_complete = complete;
	xfer_desc->context = context;

	/* Clear previous status flags */
	ctrl &= ~(QMC_BD_RX_L | QMC_BD_RX_F | QMC_BD_RX_LG | QMC_BD_RX_NO |
		  QMC_BD_RX_AB | QMC_BD_RX_CR);

	/* Activate the descriptor */
	ctrl |= (QMC_BD_RX_E | QMC_BD_RX_UB);
	wmb(); /* Be sure to flush data before descriptor activation */
	qmc_write16(&bd->cbd_sc, ctrl);

	/* Restart receiver if needed */
	if (chan->is_rx_halted && !chan->is_rx_stopped) {
		/* Restart receiver */
		qmc_write32(chan->s_param + QMC_SPE_RPACK, chan->qmc->data->rpack);
		qmc_write32(chan->s_param + QMC_SPE_ZDSTATE,
			    chan->mode == QMC_TRANSPARENT ?
				chan->qmc->data->zdstate_transp :
				chan->qmc->data->zdstate_hdlc);
		qmc_write32(chan->s_param + QMC_SPE_RSTATE, chan->qmc->data->rstate);
		chan->is_rx_halted = false;
	}
	chan->rx_pending++;

	if (ctrl & QMC_BD_RX_W)
		chan->rxbd_free = chan->rxbds;
	else
		chan->rxbd_free++;

	ret = 0;
end:
	spin_unlock_irqrestore(&chan->rx_lock, flags);
	return ret;
}
EXPORT_SYMBOL(qmc_chan_read_submit);

static void qmc_chan_read_done(struct qmc_chan *chan)
{
	void (*complete)(void *context, size_t size, unsigned int flags);
	struct qmc_xfer_desc *xfer_desc;
	unsigned long flags;
	cbd_t __iomem *bd;
	void *context;
	u16 datalen;
	u16 ctrl;

	/*
	 * E bit  UB bit
	 *   0       0  : The BD is free
	 *   1       1  : The BD is in used, waiting for transfer
	 *   0       1  : The BD is in used, waiting for completion
	 *   1       0  : Should not append
	 */

	spin_lock_irqsave(&chan->rx_lock, flags);
	bd = chan->rxbd_done;

	ctrl = qmc_read16(&bd->cbd_sc);
	while (!(ctrl & QMC_BD_RX_E)) {
		if (!(ctrl & QMC_BD_RX_UB))
			goto end;

		xfer_desc = &chan->rx_desc[bd - chan->rxbds];
		complete = xfer_desc->rx_complete;
		context = xfer_desc->context;
		xfer_desc->rx_complete = NULL;
		xfer_desc->context = NULL;

		datalen = qmc_read16(&bd->cbd_datlen);
		qmc_write16(&bd->cbd_sc, ctrl & ~QMC_BD_RX_UB);

		if (ctrl & QMC_BD_RX_W)
			chan->rxbd_done = chan->rxbds;
		else
			chan->rxbd_done++;

		chan->rx_pending--;

		if (complete) {
			spin_unlock_irqrestore(&chan->rx_lock, flags);

			/*
			 * Avoid conversion between internal hardware flags and
			 * the software API flags.
			 * -> Be sure that the software API flags are consistent
			 *    with the hardware flags
			 */
			BUILD_BUG_ON(QMC_RX_FLAG_HDLC_LAST  != QMC_BD_RX_L);
			BUILD_BUG_ON(QMC_RX_FLAG_HDLC_FIRST != QMC_BD_RX_F);
			BUILD_BUG_ON(QMC_RX_FLAG_HDLC_OVF   != QMC_BD_RX_LG);
			BUILD_BUG_ON(QMC_RX_FLAG_HDLC_UNA   != QMC_BD_RX_NO);
			BUILD_BUG_ON(QMC_RX_FLAG_HDLC_ABORT != QMC_BD_RX_AB);
			BUILD_BUG_ON(QMC_RX_FLAG_HDLC_CRC   != QMC_BD_RX_CR);

			complete(context, datalen,
				 ctrl & (QMC_BD_RX_L | QMC_BD_RX_F | QMC_BD_RX_LG |
					 QMC_BD_RX_NO | QMC_BD_RX_AB | QMC_BD_RX_CR));
			spin_lock_irqsave(&chan->rx_lock, flags);
		}

		bd = chan->rxbd_done;
		ctrl = qmc_read16(&bd->cbd_sc);
	}

end:
	spin_unlock_irqrestore(&chan->rx_lock, flags);
}

static int qmc_chan_setup_tsa_64rxtx(struct qmc_chan *chan, const struct tsa_serial_info *info,
				     bool enable)
{
	unsigned int i;
	u16 curr;
	u16 val;

	/*
	 * Use a common Tx/Rx 64 entries table.
	 * Tx and Rx related stuffs must be identical
	 */
	if (chan->tx_ts_mask != chan->rx_ts_mask) {
		dev_err(chan->qmc->dev, "chan %u uses different Rx and Tx TS\n", chan->id);
		return -EINVAL;
	}

	val = QMC_TSA_VALID | QMC_TSA_MASK_8BIT | QMC_TSA_CHANNEL(chan->id);

	/* Check entries based on Rx stuff*/
	for (i = 0; i < info->nb_rx_ts; i++) {
		if (!(chan->rx_ts_mask & (((u64)1) << i)))
			continue;

		curr = qmc_read16(chan->qmc->scc_pram + QMC_GBL_TSATRX + (i * 2));
		if (curr & QMC_TSA_VALID && (curr & ~QMC_TSA_WRAP) != val) {
			dev_err(chan->qmc->dev, "chan %u TxRx entry %d already used\n",
				chan->id, i);
			return -EBUSY;
		}
	}

	/* Set entries based on Rx stuff*/
	for (i = 0; i < info->nb_rx_ts; i++) {
		if (!(chan->rx_ts_mask & (((u64)1) << i)))
			continue;

		qmc_clrsetbits16(chan->qmc->scc_pram + QMC_GBL_TSATRX + (i * 2),
				 (u16)~QMC_TSA_WRAP, enable ? val : 0x0000);
	}

	return 0;
}

static int qmc_chan_setup_tsa_32rx(struct qmc_chan *chan, const struct tsa_serial_info *info,
				   bool enable)
{
	unsigned int i;
	u16 curr;
	u16 val;

	/* Use a Rx 32 entries table */

	val = QMC_TSA_VALID | QMC_TSA_MASK_8BIT | QMC_TSA_CHANNEL(chan->id);

	/* Check entries based on Rx stuff */
	for (i = 0; i < info->nb_rx_ts; i++) {
		if (!(chan->rx_ts_mask & (((u64)1) << i)))
			continue;

		curr = qmc_read16(chan->qmc->scc_pram + QMC_GBL_TSATRX + (i * 2));
		if (curr & QMC_TSA_VALID && (curr & ~QMC_TSA_WRAP) != val) {
			dev_err(chan->qmc->dev, "chan %u Rx entry %d already used\n",
				chan->id, i);
			return -EBUSY;
		}
	}

	/* Set entries based on Rx stuff */
	for (i = 0; i < info->nb_rx_ts; i++) {
		if (!(chan->rx_ts_mask & (((u64)1) << i)))
			continue;

		qmc_clrsetbits16(chan->qmc->scc_pram + QMC_GBL_TSATRX + (i * 2),
				 (u16)~QMC_TSA_WRAP, enable ? val : 0x0000);
	}

	return 0;
}

static int qmc_chan_setup_tsa_32tx(struct qmc_chan *chan, const struct tsa_serial_info *info,
				   bool enable)
{
	unsigned int i;
	u16 curr;
	u16 val;

	/* Use a Tx 32 entries table */

	val = QMC_TSA_VALID | QMC_TSA_MASK_8BIT | QMC_TSA_CHANNEL(chan->id);

	/* Check entries based on Tx stuff */
	for (i = 0; i < info->nb_tx_ts; i++) {
		if (!(chan->tx_ts_mask & (((u64)1) << i)))
			continue;

		curr = qmc_read16(chan->qmc->scc_pram + QMC_GBL_TSATTX + (i * 2));
		if (curr & QMC_TSA_VALID && (curr & ~QMC_TSA_WRAP) != val) {
			dev_err(chan->qmc->dev, "chan %u Tx entry %d already used\n",
				chan->id, i);
			return -EBUSY;
		}
	}

	/* Set entries based on Tx stuff */
	for (i = 0; i < info->nb_tx_ts; i++) {
		if (!(chan->tx_ts_mask & (((u64)1) << i)))
			continue;

		qmc_clrsetbits16(chan->qmc->scc_pram + QMC_GBL_TSATTX + (i * 2),
				 (u16)~QMC_TSA_WRAP, enable ? val : 0x0000);
	}

	return 0;
}

static int qmc_chan_setup_tsa_tx(struct qmc_chan *chan, bool enable)
{
	struct tsa_serial_info info;
	int ret;

	/* Retrieve info from the TSA related serial */
	ret = tsa_serial_get_info(chan->qmc->tsa_serial, &info);
	if (ret)
		return ret;

	/* Setup entries */
	if (chan->qmc->is_tsa_64rxtx)
		return qmc_chan_setup_tsa_64rxtx(chan, &info, enable);

	return qmc_chan_setup_tsa_32tx(chan, &info, enable);
}

static int qmc_chan_setup_tsa_rx(struct qmc_chan *chan, bool enable)
{
	struct tsa_serial_info info;
	int ret;

	/* Retrieve info from the TSA related serial */
	ret = tsa_serial_get_info(chan->qmc->tsa_serial, &info);
	if (ret)
		return ret;

	/* Setup entries */
	if (chan->qmc->is_tsa_64rxtx)
		return qmc_chan_setup_tsa_64rxtx(chan, &info, enable);

	return qmc_chan_setup_tsa_32rx(chan, &info, enable);
}

static int qmc_chan_cpm1_command(struct qmc_chan *chan, u8 qmc_opcode)
{
	return cpm_command(chan->id << 2, (qmc_opcode << 4) | 0x0E);
}

static int qmc_chan_qe_command(struct qmc_chan *chan, u32 cmd)
{
	if (!qe_issue_cmd(cmd, chan->qmc->qe_subblock, chan->id, 0))
		return -EIO;
	return 0;
}

static int qmc_chan_stop_rx(struct qmc_chan *chan)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&chan->rx_lock, flags);

	if (chan->is_rx_stopped) {
		/* The channel is already stopped -> simply return ok */
		ret = 0;
		goto end;
	}

	/* Send STOP RECEIVE command */
	ret = qmc_is_qe(chan->qmc) ?
		qmc_chan_qe_command(chan, QE_QMC_STOP_RX) :
		qmc_chan_cpm1_command(chan, 0x0);
	if (ret) {
		dev_err(chan->qmc->dev, "chan %u: Send STOP RECEIVE failed (%d)\n",
			chan->id, ret);
		goto end;
	}

	chan->is_rx_stopped = true;

	if (!chan->qmc->is_tsa_64rxtx || chan->is_tx_stopped) {
		ret = qmc_chan_setup_tsa_rx(chan, false);
		if (ret) {
			dev_err(chan->qmc->dev, "chan %u: Disable tsa entries failed (%d)\n",
				chan->id, ret);
			goto end;
		}
	}

end:
	spin_unlock_irqrestore(&chan->rx_lock, flags);
	return ret;
}

static int qmc_chan_stop_tx(struct qmc_chan *chan)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&chan->tx_lock, flags);

	if (chan->is_tx_stopped) {
		/* The channel is already stopped -> simply return ok */
		ret = 0;
		goto end;
	}

	/* Send STOP TRANSMIT command */
	ret = qmc_is_qe(chan->qmc) ?
		qmc_chan_qe_command(chan, QE_QMC_STOP_TX) :
		qmc_chan_cpm1_command(chan, 0x1);
	if (ret) {
		dev_err(chan->qmc->dev, "chan %u: Send STOP TRANSMIT failed (%d)\n",
			chan->id, ret);
		goto end;
	}

	chan->is_tx_stopped = true;

	if (!chan->qmc->is_tsa_64rxtx || chan->is_rx_stopped) {
		ret = qmc_chan_setup_tsa_tx(chan, false);
		if (ret) {
			dev_err(chan->qmc->dev, "chan %u: Disable tsa entries failed (%d)\n",
				chan->id, ret);
			goto end;
		}
	}

end:
	spin_unlock_irqrestore(&chan->tx_lock, flags);
	return ret;
}

static int qmc_chan_start_rx(struct qmc_chan *chan);

int qmc_chan_stop(struct qmc_chan *chan, int direction)
{
	bool is_rx_rollback_needed = false;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&chan->ts_lock, flags);

	if (direction & QMC_CHAN_READ) {
		is_rx_rollback_needed = !chan->is_rx_stopped;
		ret = qmc_chan_stop_rx(chan);
		if (ret)
			goto end;
	}

	if (direction & QMC_CHAN_WRITE) {
		ret = qmc_chan_stop_tx(chan);
		if (ret) {
			/* Restart rx if needed */
			if (is_rx_rollback_needed)
				qmc_chan_start_rx(chan);
			goto end;
		}
	}

end:
	spin_unlock_irqrestore(&chan->ts_lock, flags);
	return ret;
}
EXPORT_SYMBOL(qmc_chan_stop);

static int qmc_setup_chan_trnsync(struct qmc *qmc, struct qmc_chan *chan)
{
	struct tsa_serial_info info;
	unsigned int w_rx, w_tx;
	u16 first_rx, last_tx;
	u16 trnsync;
	int ret;

	/* Retrieve info from the TSA related serial */
	ret = tsa_serial_get_info(chan->qmc->tsa_serial, &info);
	if (ret)
		return ret;

	w_rx = hweight64(chan->rx_ts_mask);
	w_tx = hweight64(chan->tx_ts_mask);
	if (w_rx <= 1 && w_tx <= 1) {
		dev_dbg(qmc->dev, "only one or zero ts -> disable trnsync\n");
		qmc_clrbits16(chan->s_param + QMC_SPE_CHAMR, QMC_SPE_CHAMR_TRANSP_SYNC);
		return 0;
	}

	/* Find the first Rx TS allocated to the channel */
	first_rx = chan->rx_ts_mask ? __ffs64(chan->rx_ts_mask) + 1 : 0;

	/* Find the last Tx TS allocated to the channel */
	last_tx = fls64(chan->tx_ts_mask);

	trnsync = 0;
	if (info.nb_rx_ts)
		trnsync |= QMC_SPE_TRNSYNC_RX((first_rx % info.nb_rx_ts) * 2);
	if (info.nb_tx_ts)
		trnsync |= QMC_SPE_TRNSYNC_TX((last_tx % info.nb_tx_ts) * 2);

	qmc_write16(chan->s_param + QMC_SPE_TRNSYNC, trnsync);
	qmc_setbits16(chan->s_param + QMC_SPE_CHAMR, QMC_SPE_CHAMR_TRANSP_SYNC);

	dev_dbg(qmc->dev, "chan %u: trnsync=0x%04x, rx %u/%u 0x%llx, tx %u/%u 0x%llx\n",
		chan->id, trnsync,
		first_rx, info.nb_rx_ts, chan->rx_ts_mask,
		last_tx, info.nb_tx_ts, chan->tx_ts_mask);

	return 0;
}

static int qmc_chan_start_rx(struct qmc_chan *chan)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&chan->rx_lock, flags);

	if (!chan->is_rx_stopped) {
		/* The channel is already started -> simply return ok */
		ret = 0;
		goto end;
	}

	ret = qmc_chan_setup_tsa_rx(chan, true);
	if (ret) {
		dev_err(chan->qmc->dev, "chan %u: Enable tsa entries failed (%d)\n",
			chan->id, ret);
		goto end;
	}

	if (chan->mode == QMC_TRANSPARENT) {
		ret = qmc_setup_chan_trnsync(chan->qmc, chan);
		if (ret) {
			dev_err(chan->qmc->dev, "chan %u: setup TRNSYNC failed (%d)\n",
				chan->id, ret);
			goto end;
		}
	}

	/* Restart the receiver */
	qmc_write32(chan->s_param + QMC_SPE_RPACK, chan->qmc->data->rpack);
	qmc_write32(chan->s_param + QMC_SPE_ZDSTATE,
		    chan->mode == QMC_TRANSPARENT ?
			chan->qmc->data->zdstate_transp :
			chan->qmc->data->zdstate_hdlc);
	qmc_write32(chan->s_param + QMC_SPE_RSTATE, chan->qmc->data->rstate);
	chan->is_rx_halted = false;

	chan->is_rx_stopped = false;

end:
	spin_unlock_irqrestore(&chan->rx_lock, flags);
	return ret;
}

static int qmc_chan_start_tx(struct qmc_chan *chan)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&chan->tx_lock, flags);

	if (!chan->is_tx_stopped) {
		/* The channel is already started -> simply return ok */
		ret = 0;
		goto end;
	}

	ret = qmc_chan_setup_tsa_tx(chan, true);
	if (ret) {
		dev_err(chan->qmc->dev, "chan %u: Enable tsa entries failed (%d)\n",
			chan->id, ret);
		goto end;
	}

	if (chan->mode == QMC_TRANSPARENT) {
		ret = qmc_setup_chan_trnsync(chan->qmc, chan);
		if (ret) {
			dev_err(chan->qmc->dev, "chan %u: setup TRNSYNC failed (%d)\n",
				chan->id, ret);
			goto end;
		}
	}

	/*
	 * Enable channel transmitter as it could be disabled if
	 * qmc_chan_reset() was called.
	 */
	qmc_setbits16(chan->s_param + QMC_SPE_CHAMR, QMC_SPE_CHAMR_ENT);

	/* Set the POL bit in the channel mode register */
	qmc_setbits16(chan->s_param + QMC_SPE_CHAMR, QMC_SPE_CHAMR_POL);

	chan->is_tx_stopped = false;

end:
	spin_unlock_irqrestore(&chan->tx_lock, flags);
	return ret;
}

int qmc_chan_start(struct qmc_chan *chan, int direction)
{
	bool is_rx_rollback_needed = false;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&chan->ts_lock, flags);

	if (direction & QMC_CHAN_READ) {
		is_rx_rollback_needed = chan->is_rx_stopped;
		ret = qmc_chan_start_rx(chan);
		if (ret)
			goto end;
	}

	if (direction & QMC_CHAN_WRITE) {
		ret = qmc_chan_start_tx(chan);
		if (ret) {
			/* Restop rx if needed */
			if (is_rx_rollback_needed)
				qmc_chan_stop_rx(chan);
			goto end;
		}
	}

end:
	spin_unlock_irqrestore(&chan->ts_lock, flags);
	return ret;
}
EXPORT_SYMBOL(qmc_chan_start);

static void qmc_chan_reset_rx(struct qmc_chan *chan)
{
	struct qmc_xfer_desc *xfer_desc;
	unsigned long flags;
	cbd_t __iomem *bd;
	u16 ctrl;

	spin_lock_irqsave(&chan->rx_lock, flags);
	bd = chan->rxbds;
	do {
		ctrl = qmc_read16(&bd->cbd_sc);
		qmc_write16(&bd->cbd_sc, ctrl & ~(QMC_BD_RX_UB | QMC_BD_RX_E));

		xfer_desc = &chan->rx_desc[bd - chan->rxbds];
		xfer_desc->rx_complete = NULL;
		xfer_desc->context = NULL;

		bd++;
	} while (!(ctrl & QMC_BD_RX_W));

	chan->rxbd_free = chan->rxbds;
	chan->rxbd_done = chan->rxbds;
	qmc_write16(chan->s_param + QMC_SPE_RBPTR,
		    qmc_read16(chan->s_param + QMC_SPE_RBASE));

	chan->rx_pending = 0;

	spin_unlock_irqrestore(&chan->rx_lock, flags);
}

static void qmc_chan_reset_tx(struct qmc_chan *chan)
{
	struct qmc_xfer_desc *xfer_desc;
	unsigned long flags;
	cbd_t __iomem *bd;
	u16 ctrl;

	spin_lock_irqsave(&chan->tx_lock, flags);

	/* Disable transmitter. It will be re-enable on qmc_chan_start() */
	qmc_clrbits16(chan->s_param + QMC_SPE_CHAMR, QMC_SPE_CHAMR_ENT);

	bd = chan->txbds;
	do {
		ctrl = qmc_read16(&bd->cbd_sc);
		qmc_write16(&bd->cbd_sc, ctrl & ~(QMC_BD_TX_UB | QMC_BD_TX_R));

		xfer_desc = &chan->tx_desc[bd - chan->txbds];
		xfer_desc->tx_complete = NULL;
		xfer_desc->context = NULL;

		bd++;
	} while (!(ctrl & QMC_BD_TX_W));

	chan->txbd_free = chan->txbds;
	chan->txbd_done = chan->txbds;
	qmc_write16(chan->s_param + QMC_SPE_TBPTR,
		    qmc_read16(chan->s_param + QMC_SPE_TBASE));

	/* Reset TSTATE and ZISTATE to their initial value */
	qmc_write32(chan->s_param + QMC_SPE_TSTATE, chan->qmc->data->tstate);
	qmc_write32(chan->s_param + QMC_SPE_ZISTATE, chan->qmc->data->zistate);

	spin_unlock_irqrestore(&chan->tx_lock, flags);
}

int qmc_chan_reset(struct qmc_chan *chan, int direction)
{
	if (direction & QMC_CHAN_READ)
		qmc_chan_reset_rx(chan);

	if (direction & QMC_CHAN_WRITE)
		qmc_chan_reset_tx(chan);

	return 0;
}
EXPORT_SYMBOL(qmc_chan_reset);

static int qmc_check_chans(struct qmc *qmc)
{
	struct tsa_serial_info info;
	struct qmc_chan *chan;
	u64 tx_ts_assigned_mask;
	u64 rx_ts_assigned_mask;
	int ret;

	/* Retrieve info from the TSA related serial */
	ret = tsa_serial_get_info(qmc->tsa_serial, &info);
	if (ret)
		return ret;

	if (info.nb_tx_ts > 64 || info.nb_rx_ts > 64) {
		dev_err(qmc->dev, "Number of TSA Tx/Rx TS assigned not supported\n");
		return -EINVAL;
	}

	/*
	 * If more than 32 TS are assigned to this serial, one common table is
	 * used for Tx and Rx and so masks must be equal for all channels.
	 */
	if (info.nb_tx_ts > 32 || info.nb_rx_ts > 32) {
		if (info.nb_tx_ts != info.nb_rx_ts) {
			dev_err(qmc->dev, "Number of TSA Tx/Rx TS assigned are not equal\n");
			return -EINVAL;
		}
	}

	tx_ts_assigned_mask = info.nb_tx_ts == 64 ? U64_MAX : (((u64)1) << info.nb_tx_ts) - 1;
	rx_ts_assigned_mask = info.nb_rx_ts == 64 ? U64_MAX : (((u64)1) << info.nb_rx_ts) - 1;

	list_for_each_entry(chan, &qmc->chan_head, list) {
		if (chan->tx_ts_mask_avail > tx_ts_assigned_mask) {
			dev_err(qmc->dev, "chan %u can use TSA unassigned Tx TS\n", chan->id);
			return -EINVAL;
		}

		if (chan->rx_ts_mask_avail > rx_ts_assigned_mask) {
			dev_err(qmc->dev, "chan %u can use TSA unassigned Rx TS\n", chan->id);
			return -EINVAL;
		}
	}

	return 0;
}

static unsigned int qmc_nb_chans(struct qmc *qmc)
{
	unsigned int count = 0;
	struct qmc_chan *chan;

	list_for_each_entry(chan, &qmc->chan_head, list)
		count++;

	return count;
}

static int qmc_of_parse_chans(struct qmc *qmc, struct device_node *np)
{
	struct device_node *chan_np;
	struct qmc_chan *chan;
	const char *mode;
	u32 chan_id;
	u64 ts_mask;
	int ret;

	for_each_available_child_of_node(np, chan_np) {
		ret = of_property_read_u32(chan_np, "reg", &chan_id);
		if (ret) {
			dev_err(qmc->dev, "%pOF: failed to read reg\n", chan_np);
			of_node_put(chan_np);
			return ret;
		}
		if (chan_id > 63) {
			dev_err(qmc->dev, "%pOF: Invalid chan_id\n", chan_np);
			of_node_put(chan_np);
			return -EINVAL;
		}

		chan = devm_kzalloc(qmc->dev, sizeof(*chan), GFP_KERNEL);
		if (!chan) {
			of_node_put(chan_np);
			return -ENOMEM;
		}

		chan->id = chan_id;
		spin_lock_init(&chan->ts_lock);
		spin_lock_init(&chan->rx_lock);
		spin_lock_init(&chan->tx_lock);

		ret = of_property_read_u64(chan_np, "fsl,tx-ts-mask", &ts_mask);
		if (ret) {
			dev_err(qmc->dev, "%pOF: failed to read fsl,tx-ts-mask\n",
				chan_np);
			of_node_put(chan_np);
			return ret;
		}
		chan->tx_ts_mask_avail = ts_mask;
		chan->tx_ts_mask = chan->tx_ts_mask_avail;

		ret = of_property_read_u64(chan_np, "fsl,rx-ts-mask", &ts_mask);
		if (ret) {
			dev_err(qmc->dev, "%pOF: failed to read fsl,rx-ts-mask\n",
				chan_np);
			of_node_put(chan_np);
			return ret;
		}
		chan->rx_ts_mask_avail = ts_mask;
		chan->rx_ts_mask = chan->rx_ts_mask_avail;

		mode = "transparent";
		ret = of_property_read_string(chan_np, "fsl,operational-mode", &mode);
		if (ret && ret != -EINVAL) {
			dev_err(qmc->dev, "%pOF: failed to read fsl,operational-mode\n",
				chan_np);
			of_node_put(chan_np);
			return ret;
		}
		if (!strcmp(mode, "transparent")) {
			chan->mode = QMC_TRANSPARENT;
		} else if (!strcmp(mode, "hdlc")) {
			chan->mode = QMC_HDLC;
		} else {
			dev_err(qmc->dev, "%pOF: Invalid fsl,operational-mode (%s)\n",
				chan_np, mode);
			of_node_put(chan_np);
			return -EINVAL;
		}

		chan->is_reverse_data = of_property_read_bool(chan_np,
							      "fsl,reverse-data");

		list_add_tail(&chan->list, &qmc->chan_head);
		qmc->chans[chan->id] = chan;
	}

	return qmc_check_chans(qmc);
}

static int qmc_init_tsa_64rxtx(struct qmc *qmc, const struct tsa_serial_info *info)
{
	unsigned int i;
	u16 val;

	/*
	 * Use a common Tx/Rx 64 entries table.
	 * Everything was previously checked, Tx and Rx related stuffs are
	 * identical -> Used Rx related stuff to build the table
	 */
	qmc->is_tsa_64rxtx = true;

	/* Invalidate all entries */
	for (i = 0; i < 64; i++)
		qmc_write16(qmc->scc_pram + QMC_GBL_TSATRX + (i * 2), 0x0000);

	/* Set Wrap bit on last entry */
	qmc_setbits16(qmc->scc_pram + QMC_GBL_TSATRX + ((info->nb_rx_ts - 1) * 2),
		      QMC_TSA_WRAP);

	/* Init pointers to the table */
	val = qmc->scc_pram_offset + QMC_GBL_TSATRX;
	qmc_write16(qmc->scc_pram + QMC_GBL_RX_S_PTR, val);
	qmc_write16(qmc->scc_pram + QMC_GBL_RXPTR, val);
	qmc_write16(qmc->scc_pram + QMC_GBL_TX_S_PTR, val);
	qmc_write16(qmc->scc_pram + QMC_GBL_TXPTR, val);

	return 0;
}

static int qmc_init_tsa_32rx_32tx(struct qmc *qmc, const struct tsa_serial_info *info)
{
	unsigned int i;
	u16 val;

	/*
	 * Use a Tx 32 entries table and a Rx 32 entries table.
	 * Everything was previously checked.
	 */
	qmc->is_tsa_64rxtx = false;

	/* Invalidate all entries */
	for (i = 0; i < 32; i++) {
		qmc_write16(qmc->scc_pram + QMC_GBL_TSATRX + (i * 2), 0x0000);
		qmc_write16(qmc->scc_pram + QMC_GBL_TSATTX + (i * 2), 0x0000);
	}

	/* Set Wrap bit on last entries */
	qmc_setbits16(qmc->scc_pram + QMC_GBL_TSATRX + ((info->nb_rx_ts - 1) * 2),
		      QMC_TSA_WRAP);
	qmc_setbits16(qmc->scc_pram + QMC_GBL_TSATTX + ((info->nb_tx_ts - 1) * 2),
		      QMC_TSA_WRAP);

	/* Init Rx pointers ...*/
	val = qmc->scc_pram_offset + QMC_GBL_TSATRX;
	qmc_write16(qmc->scc_pram + QMC_GBL_RX_S_PTR, val);
	qmc_write16(qmc->scc_pram + QMC_GBL_RXPTR, val);

	/* ... and Tx pointers */
	val = qmc->scc_pram_offset + QMC_GBL_TSATTX;
	qmc_write16(qmc->scc_pram + QMC_GBL_TX_S_PTR, val);
	qmc_write16(qmc->scc_pram + QMC_GBL_TXPTR, val);

	return 0;
}

static int qmc_init_tsa(struct qmc *qmc)
{
	struct tsa_serial_info info;
	int ret;

	/* Retrieve info from the TSA related serial */
	ret = tsa_serial_get_info(qmc->tsa_serial, &info);
	if (ret)
		return ret;

	/*
	 * Initialize one common 64 entries table or two 32 entries (one for Tx
	 * and one for Tx) according to assigned TS numbers.
	 */
	return ((info.nb_tx_ts > 32) || (info.nb_rx_ts > 32)) ?
		qmc_init_tsa_64rxtx(qmc, &info) :
		qmc_init_tsa_32rx_32tx(qmc, &info);
}

static int qmc_setup_chan(struct qmc *qmc, struct qmc_chan *chan)
{
	unsigned int i;
	cbd_t __iomem *bd;
	int ret;
	u16 val;

	chan->qmc = qmc;

	/* Set channel specific parameter base address */
	chan->s_param = qmc->dpram + (chan->id * 64);
	/* 16 bd per channel (8 rx and 8 tx) */
	chan->txbds = qmc->bd_table + (chan->id * (QMC_NB_TXBDS + QMC_NB_RXBDS));
	chan->rxbds = qmc->bd_table + (chan->id * (QMC_NB_TXBDS + QMC_NB_RXBDS)) + QMC_NB_TXBDS;

	chan->txbd_free = chan->txbds;
	chan->txbd_done = chan->txbds;
	chan->rxbd_free = chan->rxbds;
	chan->rxbd_done = chan->rxbds;

	/* TBASE and TBPTR*/
	val = chan->id * (QMC_NB_TXBDS + QMC_NB_RXBDS) * sizeof(cbd_t);
	qmc_write16(chan->s_param + QMC_SPE_TBASE, val);
	qmc_write16(chan->s_param + QMC_SPE_TBPTR, val);

	/* RBASE and RBPTR*/
	val = ((chan->id * (QMC_NB_TXBDS + QMC_NB_RXBDS)) + QMC_NB_TXBDS) * sizeof(cbd_t);
	qmc_write16(chan->s_param + QMC_SPE_RBASE, val);
	qmc_write16(chan->s_param + QMC_SPE_RBPTR, val);
	qmc_write32(chan->s_param + QMC_SPE_TSTATE, chan->qmc->data->tstate);
	qmc_write32(chan->s_param + QMC_SPE_RSTATE, chan->qmc->data->rstate);
	qmc_write32(chan->s_param + QMC_SPE_ZISTATE, chan->qmc->data->zistate);
	qmc_write32(chan->s_param + QMC_SPE_RPACK, chan->qmc->data->rpack);
	if (chan->mode == QMC_TRANSPARENT) {
		qmc_write32(chan->s_param + QMC_SPE_ZDSTATE, chan->qmc->data->zdstate_transp);
		qmc_write16(chan->s_param + QMC_SPE_TMRBLR, 60);
		val = QMC_SPE_CHAMR_MODE_TRANSP;
		if (chan->is_reverse_data)
			val |= QMC_SPE_CHAMR_TRANSP_RD;
		qmc_write16(chan->s_param + QMC_SPE_CHAMR, val);
		ret = qmc_setup_chan_trnsync(qmc, chan);
		if (ret)
			return ret;
	} else {
		qmc_write32(chan->s_param + QMC_SPE_ZDSTATE, chan->qmc->data->zdstate_hdlc);
		qmc_write16(chan->s_param + QMC_SPE_MFLR, 60);
		qmc_write16(chan->s_param + QMC_SPE_CHAMR,
			    QMC_SPE_CHAMR_MODE_HDLC | QMC_SPE_CHAMR_HDLC_IDLM);
	}

	/* Do not enable interrupts now. They will be enabled later */
	qmc_write16(chan->s_param + QMC_SPE_INTMSK, 0x0000);

	/* Init Rx BDs and set Wrap bit on last descriptor */
	BUILD_BUG_ON(QMC_NB_RXBDS == 0);
	val = QMC_BD_RX_I;
	for (i = 0; i < QMC_NB_RXBDS; i++) {
		bd = chan->rxbds + i;
		qmc_write16(&bd->cbd_sc, val);
	}
	bd = chan->rxbds + QMC_NB_RXBDS - 1;
	qmc_write16(&bd->cbd_sc, val | QMC_BD_RX_W);

	/* Init Tx BDs and set Wrap bit on last descriptor */
	BUILD_BUG_ON(QMC_NB_TXBDS == 0);
	val = QMC_BD_TX_I;
	if (chan->mode == QMC_HDLC)
		val |= QMC_BD_TX_L | QMC_BD_TX_TC;
	for (i = 0; i < QMC_NB_TXBDS; i++) {
		bd = chan->txbds + i;
		qmc_write16(&bd->cbd_sc, val);
	}
	bd = chan->txbds + QMC_NB_TXBDS - 1;
	qmc_write16(&bd->cbd_sc, val | QMC_BD_TX_W);

	return 0;
}

static int qmc_setup_chans(struct qmc *qmc)
{
	struct qmc_chan *chan;
	int ret;

	list_for_each_entry(chan, &qmc->chan_head, list) {
		ret = qmc_setup_chan(qmc, chan);
		if (ret)
			return ret;
	}

	return 0;
}

static int qmc_finalize_chans(struct qmc *qmc)
{
	struct qmc_chan *chan;
	int ret;

	list_for_each_entry(chan, &qmc->chan_head, list) {
		/* Unmask channel interrupts */
		if (chan->mode == QMC_HDLC) {
			qmc_write16(chan->s_param + QMC_SPE_INTMSK,
				    QMC_INT_NID | QMC_INT_IDL | QMC_INT_MRF |
				    QMC_INT_UN | QMC_INT_RXF | QMC_INT_BSY |
				    QMC_INT_TXB | QMC_INT_RXB);
		} else {
			qmc_write16(chan->s_param + QMC_SPE_INTMSK,
				    QMC_INT_UN | QMC_INT_BSY |
				    QMC_INT_TXB | QMC_INT_RXB);
		}

		/* Forced stop the channel */
		ret = qmc_chan_stop(chan, QMC_CHAN_ALL);
		if (ret)
			return ret;
	}

	return 0;
}

static int qmc_setup_ints(struct qmc *qmc)
{
	unsigned int i;
	u16 __iomem *last;

	/* Raz all entries */
	for (i = 0; i < (qmc->int_size / sizeof(u16)); i++)
		qmc_write16(qmc->int_table + i, 0x0000);

	/* Set Wrap bit on last entry */
	if (qmc->int_size >= sizeof(u16)) {
		last = qmc->int_table + (qmc->int_size / sizeof(u16)) - 1;
		qmc_write16(last, QMC_INT_W);
	}

	return 0;
}

static void qmc_irq_gint(struct qmc *qmc)
{
	struct qmc_chan *chan;
	unsigned int chan_id;
	unsigned long flags;
	u16 int_entry;

	int_entry = qmc_read16(qmc->int_curr);
	while (int_entry & QMC_INT_V) {
		/* Clear all but the Wrap bit */
		qmc_write16(qmc->int_curr, int_entry & QMC_INT_W);

		chan_id = QMC_INT_GET_CHANNEL(int_entry);
		chan = qmc->chans[chan_id];
		if (!chan) {
			dev_err(qmc->dev, "interrupt on invalid chan %u\n", chan_id);
			goto int_next;
		}

		if (int_entry & QMC_INT_TXB)
			qmc_chan_write_done(chan);

		if (int_entry & QMC_INT_UN) {
			dev_info(qmc->dev, "intr chan %u, 0x%04x (UN)\n", chan_id,
				 int_entry);
			chan->nb_tx_underrun++;
		}

		if (int_entry & QMC_INT_BSY) {
			dev_info(qmc->dev, "intr chan %u, 0x%04x (BSY)\n", chan_id,
				 int_entry);
			chan->nb_rx_busy++;
			/* Restart the receiver if needed */
			spin_lock_irqsave(&chan->rx_lock, flags);
			if (chan->rx_pending && !chan->is_rx_stopped) {
				qmc_write32(chan->s_param + QMC_SPE_RPACK,
					    chan->qmc->data->rpack);
				qmc_write32(chan->s_param + QMC_SPE_ZDSTATE,
					    chan->mode == QMC_TRANSPARENT ?
						chan->qmc->data->zdstate_transp :
						chan->qmc->data->zdstate_hdlc);
				qmc_write32(chan->s_param + QMC_SPE_RSTATE,
					    chan->qmc->data->rstate);
				chan->is_rx_halted = false;
			} else {
				chan->is_rx_halted = true;
			}
			spin_unlock_irqrestore(&chan->rx_lock, flags);
		}

		if (int_entry & QMC_INT_RXB)
			qmc_chan_read_done(chan);

int_next:
		if (int_entry & QMC_INT_W)
			qmc->int_curr = qmc->int_table;
		else
			qmc->int_curr++;
		int_entry = qmc_read16(qmc->int_curr);
	}
}

static irqreturn_t qmc_irq_handler(int irq, void *priv)
{
	struct qmc *qmc = (struct qmc *)priv;
	u16 scce;

	scce = qmc_read16(qmc->scc_regs + SCC_SCCE);
	qmc_write16(qmc->scc_regs + SCC_SCCE, scce);

	if (unlikely(scce & SCC_SCCE_IQOV))
		dev_info(qmc->dev, "IRQ queue overflow\n");

	if (unlikely(scce & SCC_SCCE_GUN))
		dev_err(qmc->dev, "Global transmitter underrun\n");

	if (unlikely(scce & SCC_SCCE_GOV))
		dev_err(qmc->dev, "Global receiver overrun\n");

	/* normal interrupt */
	if (likely(scce & SCC_SCCE_GINT))
		qmc_irq_gint(qmc);

	return IRQ_HANDLED;
}

static int qmc_qe_soft_qmc_init(struct qmc *qmc, struct device_node *np)
{
	struct qe_firmware_info *qe_fw_info;
	const struct qe_firmware *qe_fw;
	const struct firmware *fw;
	const char *filename;
	int ret;

	ret = of_property_read_string(np, "fsl,soft-qmc", &filename);
	switch (ret) {
	case 0:
		break;
	case -EINVAL:
		/* fsl,soft-qmc property not set -> Simply do nothing */
		return 0;
	default:
		dev_err(qmc->dev, "%pOF: failed to read fsl,soft-qmc\n",
			np);
		return ret;
	}

	qe_fw_info = qe_get_firmware_info();
	if (qe_fw_info) {
		if (!strstr(qe_fw_info->id, "Soft-QMC")) {
			dev_err(qmc->dev, "Another Firmware is already loaded\n");
			return -EALREADY;
		}
		dev_info(qmc->dev, "Firmware already loaded\n");
		return 0;
	}

	dev_info(qmc->dev, "Using firmware %s\n", filename);

	ret = request_firmware(&fw, filename, qmc->dev);
	if (ret) {
		dev_err(qmc->dev, "Failed to request firmware %s\n", filename);
		return ret;
	}

	qe_fw = (const struct qe_firmware *)fw->data;

	if (fw->size < sizeof(qe_fw->header) ||
	    be32_to_cpu(qe_fw->header.length) != fw->size) {
		dev_err(qmc->dev, "Invalid firmware %s\n", filename);
		ret = -EINVAL;
		goto end;
	}

	ret = qe_upload_firmware(qe_fw);
	if (ret) {
		dev_err(qmc->dev, "Failed to load firmware %s\n", filename);
		goto end;
	}

	ret = 0;
end:
	release_firmware(fw);
	return ret;
}

static int qmc_cpm1_init_resources(struct qmc *qmc, struct platform_device *pdev)
{
	struct resource *res;

	qmc->scc_regs = devm_platform_ioremap_resource_byname(pdev, "scc_regs");
	if (IS_ERR(qmc->scc_regs))
		return PTR_ERR(qmc->scc_regs);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "scc_pram");
	if (!res)
		return -EINVAL;
	qmc->scc_pram_offset = res->start - get_immrbase();
	qmc->scc_pram = devm_ioremap_resource(qmc->dev, res);
	if (IS_ERR(qmc->scc_pram))
		return PTR_ERR(qmc->scc_pram);

	qmc->dpram  = devm_platform_ioremap_resource_byname(pdev, "dpram");
	if (IS_ERR(qmc->dpram))
		return PTR_ERR(qmc->dpram);

	return 0;
}

static int qmc_qe_init_resources(struct qmc *qmc, struct platform_device *pdev)
{
	struct resource *res;
	int ucc_num;
	s32 info;

	qmc->scc_regs = devm_platform_ioremap_resource_byname(pdev, "ucc_regs");
	if (IS_ERR(qmc->scc_regs))
		return PTR_ERR(qmc->scc_regs);

	ucc_num = tsa_serial_get_num(qmc->tsa_serial);
	if (ucc_num < 0)
		return dev_err_probe(qmc->dev, ucc_num, "Failed to get UCC num\n");

	qmc->qe_subblock = ucc_slow_get_qe_cr_subblock(ucc_num);
	if (qmc->qe_subblock == QE_CR_SUBBLOCK_INVALID) {
		dev_err(qmc->dev, "Unsupported ucc num %u\n", ucc_num);
		return -EINVAL;
	}
	/* Allocate the 'Global Multichannel Parameters' and the
	 * 'Framer parameters' areas. The 'Framer parameters' area
	 * is located right after the 'Global Multichannel Parameters'.
	 * The 'Framer parameters' need 1 byte per receive and transmit
	 * channel. The maximum number of receive or transmit channel
	 * is 64. So reserve 2 * 64 bytes for the 'Framer parameters'.
	 */
	info = devm_qe_muram_alloc(qmc->dev, UCC_SLOW_PRAM_SIZE + 2 * 64,
				   ALIGNMENT_OF_UCC_SLOW_PRAM);
	if (info < 0)
		return info;

	if (!qe_issue_cmd(QE_ASSIGN_PAGE_TO_DEVICE, qmc->qe_subblock,
			  QE_CR_PROTOCOL_UNSPECIFIED, info)) {
		dev_err(qmc->dev, "QE_ASSIGN_PAGE_TO_DEVICE cmd failed");
		return -EIO;
	}
	qmc->scc_pram = qe_muram_addr(info);
	qmc->scc_pram_offset = info;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dpram");
	if (!res)
		return -EINVAL;
	qmc->dpram_offset = res->start - qe_muram_dma(qe_muram_addr(0));
	qmc->dpram = devm_ioremap_resource(qmc->dev, res);
	if (IS_ERR(qmc->scc_pram))
		return PTR_ERR(qmc->scc_pram);

	return 0;
}

static int qmc_init_resources(struct qmc *qmc, struct platform_device *pdev)
{
	return qmc_is_qe(qmc) ?
		qmc_qe_init_resources(qmc, pdev) :
		qmc_cpm1_init_resources(qmc, pdev);
}

static int qmc_cpm1_init_scc(struct qmc *qmc)
{
	u32 val;
	int ret;

	/* Connect the serial (SCC) to TSA */
	ret = tsa_serial_connect(qmc->tsa_serial);
	if (ret)
		return dev_err_probe(qmc->dev, ret, "Failed to connect TSA serial\n");

	/* Init GMSR_H and GMSR_L registers */
	val = SCC_GSMRH_CDS | SCC_GSMRH_CTSS | SCC_GSMRH_CDP | SCC_GSMRH_CTSP;
	qmc_write32(qmc->scc_regs + SCC_GSMRH, val);

	/* enable QMC mode */
	qmc_write32(qmc->scc_regs + SCC_GSMRL, SCC_CPM1_GSMRL_MODE_QMC);

	/* Disable and clear interrupts */
	qmc_write16(qmc->scc_regs + SCC_SCCM, 0x0000);
	qmc_write16(qmc->scc_regs + SCC_SCCE, 0x000F);

	return 0;
}

static int qmc_qe_init_ucc(struct qmc *qmc)
{
	u32 val;
	int ret;

	/* Set the UCC in slow mode */
	qmc_write8(qmc->scc_regs + SCC_QE_UCC_GUEMR,
		   UCC_GUEMR_SET_RESERVED3 | UCC_GUEMR_MODE_SLOW_RX | UCC_GUEMR_MODE_SLOW_TX);

	/* Connect the serial (UCC) to TSA */
	ret = tsa_serial_connect(qmc->tsa_serial);
	if (ret)
		return dev_err_probe(qmc->dev, ret, "Failed to connect TSA serial\n");

	/* Initialize the QMC tx startup addresses */
	if (!qe_issue_cmd(QE_PUSHSCHED, qmc->qe_subblock,
			  QE_CR_PROTOCOL_UNSPECIFIED, 0x80)) {
		dev_err(qmc->dev, "QE_CMD_PUSH_SCHED tx cmd failed");
		ret = -EIO;
		goto err_tsa_serial_disconnect;
	}

	/* Initialize the QMC rx startup addresses */
	if (!qe_issue_cmd(QE_PUSHSCHED, qmc->qe_subblock | 0x00020000,
			  QE_CR_PROTOCOL_UNSPECIFIED, 0x82)) {
		dev_err(qmc->dev, "QE_CMD_PUSH_SCHED rx cmd failed");
		ret = -EIO;
		goto err_tsa_serial_disconnect;
	}

	/* Re-init RXPTR and TXPTR with the content of RX_S_PTR and
	 * TX_S_PTR (RX_S_PTR and TX_S_PTR are initialized during
	 * qmc_setup_tsa() call
	 */
	val = qmc_read16(qmc->scc_pram + QMC_GBL_RX_S_PTR);
	qmc_write16(qmc->scc_pram + QMC_GBL_RXPTR, val);
	val = qmc_read16(qmc->scc_pram + QMC_GBL_TX_S_PTR);
	qmc_write16(qmc->scc_pram + QMC_GBL_TXPTR, val);

	/* Init GUMR_H and GUMR_L registers (SCC GSMR_H and GSMR_L) */
	val = SCC_GSMRH_CDS | SCC_GSMRH_CTSS | SCC_GSMRH_CDP | SCC_GSMRH_CTSP |
	      SCC_GSMRH_TRX | SCC_GSMRH_TTX;
	qmc_write32(qmc->scc_regs + SCC_GSMRH, val);

	/* enable QMC mode */
	qmc_write32(qmc->scc_regs + SCC_GSMRL, SCC_QE_GSMRL_MODE_QMC);

	/* Disable and clear interrupts */
	qmc_write16(qmc->scc_regs + SCC_SCCM, 0x0000);
	qmc_write16(qmc->scc_regs + SCC_SCCE, 0x000F);

	return 0;

err_tsa_serial_disconnect:
	tsa_serial_disconnect(qmc->tsa_serial);
	return ret;
}

static int qmc_init_xcc(struct qmc *qmc)
{
	return qmc_is_qe(qmc) ?
		qmc_qe_init_ucc(qmc) :
		qmc_cpm1_init_scc(qmc);
}

static void qmc_exit_xcc(struct qmc *qmc)
{
	/* Disconnect the serial from TSA */
	tsa_serial_disconnect(qmc->tsa_serial);
}

static int qmc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	unsigned int nb_chans;
	struct qmc *qmc;
	int irq;
	int ret;

	qmc = devm_kzalloc(&pdev->dev, sizeof(*qmc), GFP_KERNEL);
	if (!qmc)
		return -ENOMEM;

	qmc->dev = &pdev->dev;
	qmc->data = of_device_get_match_data(&pdev->dev);
	if (!qmc->data) {
		dev_err(qmc->dev, "Missing match data\n");
		return -EINVAL;
	}
	INIT_LIST_HEAD(&qmc->chan_head);

	qmc->tsa_serial = devm_tsa_serial_get_byphandle(qmc->dev, np, "fsl,tsa-serial");
	if (IS_ERR(qmc->tsa_serial)) {
		return dev_err_probe(qmc->dev, PTR_ERR(qmc->tsa_serial),
				     "Failed to get TSA serial\n");
	}

	ret = qmc_init_resources(qmc, pdev);
	if (ret)
		return ret;

	if (qmc_is_qe(qmc)) {
		ret = qmc_qe_soft_qmc_init(qmc, np);
		if (ret)
			return ret;
	}

	/* Parse channels informationss */
	ret = qmc_of_parse_chans(qmc, np);
	if (ret)
		return ret;

	nb_chans = qmc_nb_chans(qmc);

	/*
	 * Allocate the buffer descriptor table
	 * 8 rx and 8 tx descriptors per channel
	 */
	qmc->bd_size = (nb_chans * (QMC_NB_TXBDS + QMC_NB_RXBDS)) * sizeof(cbd_t);
	qmc->bd_table = dmam_alloc_coherent(qmc->dev, qmc->bd_size,
					    &qmc->bd_dma_addr, GFP_KERNEL);
	if (!qmc->bd_table) {
		dev_err(qmc->dev, "Failed to allocate bd table\n");
		return -ENOMEM;
	}
	memset(qmc->bd_table, 0, qmc->bd_size);

	qmc_write32(qmc->scc_pram + QMC_GBL_MCBASE, qmc->bd_dma_addr);

	/* Allocate the interrupt table */
	qmc->int_size = QMC_NB_INTS * sizeof(u16);
	qmc->int_table = dmam_alloc_coherent(qmc->dev, qmc->int_size,
					     &qmc->int_dma_addr, GFP_KERNEL);
	if (!qmc->int_table) {
		dev_err(qmc->dev, "Failed to allocate interrupt table\n");
		return -ENOMEM;
	}
	memset(qmc->int_table, 0, qmc->int_size);

	qmc->int_curr = qmc->int_table;
	qmc_write32(qmc->scc_pram + QMC_GBL_INTBASE, qmc->int_dma_addr);
	qmc_write32(qmc->scc_pram + QMC_GBL_INTPTR, qmc->int_dma_addr);

	/* Set MRBLR (valid for HDLC only) max MRU + max CRC */
	qmc_write16(qmc->scc_pram + QMC_GBL_MRBLR, HDLC_MAX_MRU + 4);

	qmc_write16(qmc->scc_pram + QMC_GBL_GRFTHR, 1);
	qmc_write16(qmc->scc_pram + QMC_GBL_GRFCNT, 1);

	qmc_write32(qmc->scc_pram + QMC_GBL_C_MASK32, 0xDEBB20E3);
	qmc_write16(qmc->scc_pram + QMC_GBL_C_MASK16, 0xF0B8);

	if (qmc_is_qe(qmc)) {
		/* Zeroed the reserved area */
		memset_io(qmc->scc_pram + QMC_QE_GBL_RSV_B0_START, 0,
			  QMC_QE_GBL_RSV_B0_SIZE);

		qmc_write32(qmc->scc_pram + QMC_QE_GBL_GCSBASE, qmc->dpram_offset);

		/* Init 'framer parameters' area and set the base addresses */
		memset_io(qmc->scc_pram + UCC_SLOW_PRAM_SIZE, 0x01, 64);
		memset_io(qmc->scc_pram + UCC_SLOW_PRAM_SIZE + 64, 0x01, 64);
		qmc_write16(qmc->scc_pram + QMC_QE_GBL_RX_FRM_BASE,
			    qmc->scc_pram_offset + UCC_SLOW_PRAM_SIZE);
		qmc_write16(qmc->scc_pram + QMC_QE_GBL_TX_FRM_BASE,
			    qmc->scc_pram_offset + UCC_SLOW_PRAM_SIZE + 64);
	}

	ret = qmc_init_tsa(qmc);
	if (ret)
		return ret;

	qmc_write16(qmc->scc_pram + QMC_GBL_QMCSTATE, 0x8000);

	ret = qmc_setup_chans(qmc);
	if (ret)
		return ret;

	/* Init interrupts table */
	ret = qmc_setup_ints(qmc);
	if (ret)
		return ret;

	/* Init SCC (CPM1) or UCC (QE) */
	ret = qmc_init_xcc(qmc);
	if (ret)
		return ret;

	/* Set the irq handler */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		goto err_exit_xcc;
	ret = devm_request_irq(qmc->dev, irq, qmc_irq_handler, 0, "qmc", qmc);
	if (ret < 0)
		goto err_exit_xcc;

	/* Enable interrupts */
	qmc_write16(qmc->scc_regs + SCC_SCCM,
		    SCC_SCCE_IQOV | SCC_SCCE_GINT | SCC_SCCE_GUN | SCC_SCCE_GOV);

	ret = qmc_finalize_chans(qmc);
	if (ret < 0)
		goto err_disable_intr;

	/* Enable transmitter and receiver */
	qmc_setbits32(qmc->scc_regs + SCC_GSMRL, SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	platform_set_drvdata(pdev, qmc);

	/* Populate channel related devices */
	ret = devm_of_platform_populate(qmc->dev);
	if (ret)
		goto err_disable_txrx;

	return 0;

err_disable_txrx:
	qmc_setbits32(qmc->scc_regs + SCC_GSMRL, 0);

err_disable_intr:
	qmc_write16(qmc->scc_regs + SCC_SCCM, 0);

err_exit_xcc:
	qmc_exit_xcc(qmc);
	return ret;
}

static void qmc_remove(struct platform_device *pdev)
{
	struct qmc *qmc = platform_get_drvdata(pdev);

	/* Disable transmitter and receiver */
	qmc_setbits32(qmc->scc_regs + SCC_GSMRL, 0);

	/* Disable interrupts */
	qmc_write16(qmc->scc_regs + SCC_SCCM, 0);

	/* Exit SCC (CPM1) or UCC (QE) */
	qmc_exit_xcc(qmc);
}

static const struct qmc_data qmc_data_cpm1 __maybe_unused = {
	.version = QMC_CPM1,
	.tstate = 0x30000000,
	.rstate = 0x31000000,
	.zistate = 0x00000100,
	.zdstate_hdlc = 0x00000080,
	.zdstate_transp = 0x18000080,
	.rpack = 0x00000000,
};

static const struct qmc_data qmc_data_qe __maybe_unused = {
	.version = QMC_QE,
	.tstate = 0x30000000,
	.rstate = 0x30000000,
	.zistate = 0x00000200,
	.zdstate_hdlc = 0x80FFFFE0,
	.zdstate_transp = 0x003FFFE2,
	.rpack = 0x80000000,
};

static const struct of_device_id qmc_id_table[] = {
#if IS_ENABLED(CONFIG_CPM1)
	{ .compatible = "fsl,cpm1-scc-qmc", .data = &qmc_data_cpm1 },
#endif
#if IS_ENABLED(CONFIG_QUICC_ENGINE)
	{ .compatible = "fsl,qe-ucc-qmc", .data = &qmc_data_qe },
#endif
	{} /* sentinel */
};
MODULE_DEVICE_TABLE(of, qmc_id_table);

static struct platform_driver qmc_driver = {
	.driver = {
		.name = "fsl-qmc",
		.of_match_table = of_match_ptr(qmc_id_table),
	},
	.probe = qmc_probe,
	.remove_new = qmc_remove,
};
module_platform_driver(qmc_driver);

static struct qmc_chan *qmc_chan_get_from_qmc(struct device_node *qmc_np, unsigned int chan_index)
{
	struct platform_device *pdev;
	struct qmc_chan *qmc_chan;
	struct qmc *qmc;

	if (!of_match_node(qmc_driver.driver.of_match_table, qmc_np))
		return ERR_PTR(-EINVAL);

	pdev = of_find_device_by_node(qmc_np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	qmc = platform_get_drvdata(pdev);
	if (!qmc) {
		platform_device_put(pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	if (chan_index >= ARRAY_SIZE(qmc->chans)) {
		platform_device_put(pdev);
		return ERR_PTR(-EINVAL);
	}

	qmc_chan = qmc->chans[chan_index];
	if (!qmc_chan) {
		platform_device_put(pdev);
		return ERR_PTR(-ENOENT);
	}

	return qmc_chan;
}

int qmc_chan_count_phandles(struct device_node *np, const char *phandles_name)
{
	int count;

	/* phandles are fixed args phandles with one arg */
	count = of_count_phandle_with_args(np, phandles_name, NULL);
	if (count < 0)
		return count;

	return count / 2;
}
EXPORT_SYMBOL(qmc_chan_count_phandles);

struct qmc_chan *qmc_chan_get_byphandles_index(struct device_node *np,
					       const char *phandles_name,
					       int index)
{
	struct of_phandle_args out_args;
	struct qmc_chan *qmc_chan;
	int ret;

	ret = of_parse_phandle_with_fixed_args(np, phandles_name, 1, index,
					       &out_args);
	if (ret < 0)
		return ERR_PTR(ret);

	if (out_args.args_count != 1) {
		of_node_put(out_args.np);
		return ERR_PTR(-EINVAL);
	}

	qmc_chan = qmc_chan_get_from_qmc(out_args.np, out_args.args[0]);
	of_node_put(out_args.np);
	return qmc_chan;
}
EXPORT_SYMBOL(qmc_chan_get_byphandles_index);

struct qmc_chan *qmc_chan_get_bychild(struct device_node *np)
{
	struct device_node *qmc_np;
	u32 chan_index;
	int ret;

	qmc_np = np->parent;
	ret = of_property_read_u32(np, "reg", &chan_index);
	if (ret)
		return ERR_PTR(-EINVAL);

	return qmc_chan_get_from_qmc(qmc_np, chan_index);
}
EXPORT_SYMBOL(qmc_chan_get_bychild);

void qmc_chan_put(struct qmc_chan *chan)
{
	put_device(chan->qmc->dev);
}
EXPORT_SYMBOL(qmc_chan_put);

static void devm_qmc_chan_release(struct device *dev, void *res)
{
	struct qmc_chan **qmc_chan = res;

	qmc_chan_put(*qmc_chan);
}

struct qmc_chan *devm_qmc_chan_get_byphandles_index(struct device *dev,
						    struct device_node *np,
						    const char *phandles_name,
						    int index)
{
	struct qmc_chan *qmc_chan;
	struct qmc_chan **dr;

	dr = devres_alloc(devm_qmc_chan_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	qmc_chan = qmc_chan_get_byphandles_index(np, phandles_name, index);
	if (!IS_ERR(qmc_chan)) {
		*dr = qmc_chan;
		devres_add(dev, dr);
	} else {
		devres_free(dr);
	}

	return qmc_chan;
}
EXPORT_SYMBOL(devm_qmc_chan_get_byphandles_index);

struct qmc_chan *devm_qmc_chan_get_bychild(struct device *dev,
					   struct device_node *np)
{
	struct qmc_chan *qmc_chan;
	struct qmc_chan **dr;

	dr = devres_alloc(devm_qmc_chan_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	qmc_chan = qmc_chan_get_bychild(np);
	if (!IS_ERR(qmc_chan)) {
		*dr = qmc_chan;
		devres_add(dev, dr);
	} else {
		devres_free(dr);
	}

	return qmc_chan;
}
EXPORT_SYMBOL(devm_qmc_chan_get_bychild);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("CPM/QE QMC driver");
MODULE_LICENSE("GPL");
