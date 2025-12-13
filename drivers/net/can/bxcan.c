// SPDX-License-Identifier: GPL-2.0
//
// bxcan.c - STM32 Basic Extended CAN controller driver
//
// Copyright (c) 2022 Dario Binacchi <dario.binacchi@amarulasolutions.com>
//
// NOTE: The ST documentation uses the terms master/slave instead of
// primary/secondary.

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/rx-offload.h>
#include <linux/clk.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define BXCAN_NAPI_WEIGHT 3
#define BXCAN_TIMEOUT_US 10000

#define BXCAN_RX_MB_NUM 2
#define BXCAN_TX_MB_NUM 3

/* Primary control register (MCR) bits */
#define BXCAN_MCR_RESET BIT(15)
#define BXCAN_MCR_TTCM BIT(7)
#define BXCAN_MCR_ABOM BIT(6)
#define BXCAN_MCR_AWUM BIT(5)
#define BXCAN_MCR_NART BIT(4)
#define BXCAN_MCR_RFLM BIT(3)
#define BXCAN_MCR_TXFP BIT(2)
#define BXCAN_MCR_SLEEP BIT(1)
#define BXCAN_MCR_INRQ BIT(0)

/* Primary status register (MSR) bits */
#define BXCAN_MSR_ERRI BIT(2)
#define BXCAN_MSR_SLAK BIT(1)
#define BXCAN_MSR_INAK BIT(0)

/* Transmit status register (TSR) bits */
#define BXCAN_TSR_RQCP2 BIT(16)
#define BXCAN_TSR_RQCP1 BIT(8)
#define BXCAN_TSR_RQCP0 BIT(0)

/* Receive FIFO 0 register (RF0R) bits */
#define BXCAN_RF0R_RFOM0 BIT(5)
#define BXCAN_RF0R_FMP0_MASK GENMASK(1, 0)

/* Interrupt enable register (IER) bits */
#define BXCAN_IER_SLKIE BIT(17)
#define BXCAN_IER_WKUIE BIT(16)
#define BXCAN_IER_ERRIE BIT(15)
#define BXCAN_IER_LECIE BIT(11)
#define BXCAN_IER_BOFIE BIT(10)
#define BXCAN_IER_EPVIE BIT(9)
#define BXCAN_IER_EWGIE BIT(8)
#define BXCAN_IER_FOVIE1 BIT(6)
#define BXCAN_IER_FFIE1 BIT(5)
#define BXCAN_IER_FMPIE1 BIT(4)
#define BXCAN_IER_FOVIE0 BIT(3)
#define BXCAN_IER_FFIE0 BIT(2)
#define BXCAN_IER_FMPIE0 BIT(1)
#define BXCAN_IER_TMEIE BIT(0)

/* Error status register (ESR) bits */
#define BXCAN_ESR_REC_MASK GENMASK(31, 24)
#define BXCAN_ESR_TEC_MASK GENMASK(23, 16)
#define BXCAN_ESR_LEC_MASK GENMASK(6, 4)
#define BXCAN_ESR_BOFF BIT(2)
#define BXCAN_ESR_EPVF BIT(1)
#define BXCAN_ESR_EWGF BIT(0)

/* Bit timing register (BTR) bits */
#define BXCAN_BTR_SILM BIT(31)
#define BXCAN_BTR_LBKM BIT(30)
#define BXCAN_BTR_SJW_MASK GENMASK(25, 24)
#define BXCAN_BTR_TS2_MASK GENMASK(22, 20)
#define BXCAN_BTR_TS1_MASK GENMASK(19, 16)
#define BXCAN_BTR_BRP_MASK GENMASK(9, 0)

/* TX mailbox identifier register (TIxR, x = 0..2) bits */
#define BXCAN_TIxR_STID_MASK GENMASK(31, 21)
#define BXCAN_TIxR_EXID_MASK GENMASK(31, 3)
#define BXCAN_TIxR_IDE BIT(2)
#define BXCAN_TIxR_RTR BIT(1)
#define BXCAN_TIxR_TXRQ BIT(0)

/* TX mailbox data length and time stamp register (TDTxR, x = 0..2 bits */
#define BXCAN_TDTxR_DLC_MASK GENMASK(3, 0)

/* RX FIFO mailbox identifier register (RIxR, x = 0..1 */
#define BXCAN_RIxR_STID_MASK GENMASK(31, 21)
#define BXCAN_RIxR_EXID_MASK GENMASK(31, 3)
#define BXCAN_RIxR_IDE BIT(2)
#define BXCAN_RIxR_RTR BIT(1)

/* RX FIFO mailbox data length and timestamp register (RDTxR, x = 0..1) bits */
#define BXCAN_RDTxR_TIME_MASK GENMASK(31, 16)
#define BXCAN_RDTxR_DLC_MASK GENMASK(3, 0)

#define BXCAN_FMR_REG 0x00
#define BXCAN_FM1R_REG 0x04
#define BXCAN_FS1R_REG 0x0c
#define BXCAN_FFA1R_REG 0x14
#define BXCAN_FA1R_REG 0x1c
#define BXCAN_FiR1_REG(b) (0x40 + (b) * 8)
#define BXCAN_FiR2_REG(b) (0x44 + (b) * 8)

#define BXCAN_FILTER_ID(cfg) ((cfg) == BXCAN_CFG_DUAL_SECONDARY ? 14 : 0)

/* Filter primary register (FMR) bits */
#define BXCAN_FMR_CANSB_MASK GENMASK(13, 8)
#define BXCAN_FMR_FINIT BIT(0)

enum bxcan_lec_code {
	BXCAN_LEC_NO_ERROR = 0,
	BXCAN_LEC_STUFF_ERROR,
	BXCAN_LEC_FORM_ERROR,
	BXCAN_LEC_ACK_ERROR,
	BXCAN_LEC_BIT1_ERROR,
	BXCAN_LEC_BIT0_ERROR,
	BXCAN_LEC_CRC_ERROR,
	BXCAN_LEC_UNUSED
};

enum bxcan_cfg {
	BXCAN_CFG_SINGLE = 0,
	BXCAN_CFG_DUAL_PRIMARY,
	BXCAN_CFG_DUAL_SECONDARY
};

/* Structure of the message buffer */
struct bxcan_mb {
	u32 id;			/* can identifier */
	u32 dlc;		/* data length control and timestamp */
	u32 data[2];		/* data */
};

/* Structure of the hardware registers */
struct bxcan_regs {
	u32 mcr;			/* 0x00 - primary control */
	u32 msr;			/* 0x04 - primary status */
	u32 tsr;			/* 0x08 - transmit status */
	u32 rf0r;			/* 0x0c - FIFO 0 */
	u32 rf1r;			/* 0x10 - FIFO 1 */
	u32 ier;			/* 0x14 - interrupt enable */
	u32 esr;			/* 0x18 - error status */
	u32 btr;			/* 0x1c - bit timing*/
	u32 reserved0[88];		/* 0x20 */
	struct bxcan_mb tx_mb[BXCAN_TX_MB_NUM];	/* 0x180 - tx mailbox */
	struct bxcan_mb rx_mb[BXCAN_RX_MB_NUM];	/* 0x1b0 - rx mailbox */
};

struct bxcan_priv {
	struct can_priv can;
	struct can_rx_offload offload;
	struct device *dev;
	struct net_device *ndev;

	struct bxcan_regs __iomem *regs;
	struct regmap *gcan;
	int tx_irq;
	int sce_irq;
	enum bxcan_cfg cfg;
	struct clk *clk;
	spinlock_t rmw_lock;	/* lock for read-modify-write operations */
	unsigned int tx_head;
	unsigned int tx_tail;
	u32 timestamp;
};

static const struct can_bittiming_const bxcan_bittiming_const = {
	.name = KBUILD_MODNAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

static inline void bxcan_rmw(struct bxcan_priv *priv, void __iomem *addr,
			     u32 clear, u32 set)
{
	unsigned long flags;
	u32 old, val;

	spin_lock_irqsave(&priv->rmw_lock, flags);
	old = readl(addr);
	val = (old & ~clear) | set;
	if (val != old)
		writel(val, addr);

	spin_unlock_irqrestore(&priv->rmw_lock, flags);
}

static void bxcan_disable_filters(struct bxcan_priv *priv, enum bxcan_cfg cfg)
{
	unsigned int fid = BXCAN_FILTER_ID(cfg);
	u32 fmask = BIT(fid);

	regmap_update_bits(priv->gcan, BXCAN_FA1R_REG, fmask, 0);
}

static void bxcan_enable_filters(struct bxcan_priv *priv, enum bxcan_cfg cfg)
{
	unsigned int fid = BXCAN_FILTER_ID(cfg);
	u32 fmask = BIT(fid);

	/* Filter settings:
	 *
	 * Accept all messages.
	 * Assign filter 0 to CAN1 and filter 14 to CAN2 in identifier
	 * mask mode with 32 bits width.
	 */

	/* Enter filter initialization mode and assing filters to CAN
	 * controllers.
	 */
	regmap_update_bits(priv->gcan, BXCAN_FMR_REG,
			   BXCAN_FMR_CANSB_MASK | BXCAN_FMR_FINIT,
			   FIELD_PREP(BXCAN_FMR_CANSB_MASK, 14) |
			   BXCAN_FMR_FINIT);

	/* Deactivate filter */
	regmap_update_bits(priv->gcan, BXCAN_FA1R_REG, fmask, 0);

	/* Two 32-bit registers in identifier mask mode */
	regmap_update_bits(priv->gcan, BXCAN_FM1R_REG, fmask, 0);

	/* Single 32-bit scale configuration */
	regmap_update_bits(priv->gcan, BXCAN_FS1R_REG, fmask, fmask);

	/* Assign filter to FIFO 0 */
	regmap_update_bits(priv->gcan, BXCAN_FFA1R_REG, fmask, 0);

	/* Accept all messages */
	regmap_write(priv->gcan, BXCAN_FiR1_REG(fid), 0);
	regmap_write(priv->gcan, BXCAN_FiR2_REG(fid), 0);

	/* Activate filter */
	regmap_update_bits(priv->gcan, BXCAN_FA1R_REG, fmask, fmask);

	/* Exit filter initialization mode */
	regmap_update_bits(priv->gcan, BXCAN_FMR_REG, BXCAN_FMR_FINIT, 0);
}

static inline u8 bxcan_get_tx_head(const struct bxcan_priv *priv)
{
	return priv->tx_head % BXCAN_TX_MB_NUM;
}

static inline u8 bxcan_get_tx_tail(const struct bxcan_priv *priv)
{
	return priv->tx_tail % BXCAN_TX_MB_NUM;
}

static inline u8 bxcan_get_tx_free(const struct bxcan_priv *priv)
{
	return BXCAN_TX_MB_NUM - (priv->tx_head - priv->tx_tail);
}

static bool bxcan_tx_busy(const struct bxcan_priv *priv)
{
	if (bxcan_get_tx_free(priv) > 0)
		return false;

	netif_stop_queue(priv->ndev);

	/* Memory barrier before checking tx_free (head and tail) */
	smp_mb();

	if (bxcan_get_tx_free(priv) == 0) {
		netdev_dbg(priv->ndev,
			   "Stopping tx-queue (tx_head=0x%08x, tx_tail=0x%08x, len=%d).\n",
			   priv->tx_head, priv->tx_tail,
			   priv->tx_head - priv->tx_tail);

		return true;
	}

	netif_start_queue(priv->ndev);

	return false;
}

static int bxcan_chip_softreset(struct bxcan_priv *priv)
{
	struct bxcan_regs __iomem *regs = priv->regs;
	u32 value;

	bxcan_rmw(priv, &regs->mcr, 0, BXCAN_MCR_RESET);
	return readx_poll_timeout(readl, &regs->msr, value,
				  value & BXCAN_MSR_SLAK, BXCAN_TIMEOUT_US,
				  USEC_PER_SEC);
}

static int bxcan_enter_init_mode(struct bxcan_priv *priv)
{
	struct bxcan_regs __iomem *regs = priv->regs;
	u32 value;

	bxcan_rmw(priv, &regs->mcr, 0, BXCAN_MCR_INRQ);
	return readx_poll_timeout(readl, &regs->msr, value,
				  value & BXCAN_MSR_INAK, BXCAN_TIMEOUT_US,
				  USEC_PER_SEC);
}

static int bxcan_leave_init_mode(struct bxcan_priv *priv)
{
	struct bxcan_regs __iomem *regs = priv->regs;
	u32 value;

	bxcan_rmw(priv, &regs->mcr, BXCAN_MCR_INRQ, 0);
	return readx_poll_timeout(readl, &regs->msr, value,
				  !(value & BXCAN_MSR_INAK), BXCAN_TIMEOUT_US,
				  USEC_PER_SEC);
}

static int bxcan_enter_sleep_mode(struct bxcan_priv *priv)
{
	struct bxcan_regs __iomem *regs = priv->regs;
	u32 value;

	bxcan_rmw(priv, &regs->mcr, 0, BXCAN_MCR_SLEEP);
	return readx_poll_timeout(readl, &regs->msr, value,
				  value & BXCAN_MSR_SLAK, BXCAN_TIMEOUT_US,
				  USEC_PER_SEC);
}

static int bxcan_leave_sleep_mode(struct bxcan_priv *priv)
{
	struct bxcan_regs __iomem *regs = priv->regs;
	u32 value;

	bxcan_rmw(priv, &regs->mcr, BXCAN_MCR_SLEEP, 0);
	return readx_poll_timeout(readl, &regs->msr, value,
				  !(value & BXCAN_MSR_SLAK), BXCAN_TIMEOUT_US,
				  USEC_PER_SEC);
}

static inline
struct bxcan_priv *rx_offload_to_priv(struct can_rx_offload *offload)
{
	return container_of(offload, struct bxcan_priv, offload);
}

static struct sk_buff *bxcan_mailbox_read(struct can_rx_offload *offload,
					  unsigned int mbxno, u32 *timestamp,
					  bool drop)
{
	struct bxcan_priv *priv = rx_offload_to_priv(offload);
	struct bxcan_regs __iomem *regs = priv->regs;
	struct bxcan_mb __iomem *mb_regs = &regs->rx_mb[0];
	struct sk_buff *skb = NULL;
	struct can_frame *cf;
	u32 rf0r, id, dlc;

	rf0r = readl(&regs->rf0r);
	if (unlikely(drop)) {
		skb = ERR_PTR(-ENOBUFS);
		goto mark_as_read;
	}

	if (!(rf0r & BXCAN_RF0R_FMP0_MASK))
		goto mark_as_read;

	skb = alloc_can_skb(offload->dev, &cf);
	if (unlikely(!skb)) {
		skb = ERR_PTR(-ENOMEM);
		goto mark_as_read;
	}

	id = readl(&mb_regs->id);
	if (id & BXCAN_RIxR_IDE)
		cf->can_id = FIELD_GET(BXCAN_RIxR_EXID_MASK, id) | CAN_EFF_FLAG;
	else
		cf->can_id = FIELD_GET(BXCAN_RIxR_STID_MASK, id) & CAN_SFF_MASK;

	dlc = readl(&mb_regs->dlc);
	priv->timestamp = FIELD_GET(BXCAN_RDTxR_TIME_MASK, dlc);
	cf->len = can_cc_dlc2len(FIELD_GET(BXCAN_RDTxR_DLC_MASK, dlc));

	if (id & BXCAN_RIxR_RTR) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		int i, j;

		for (i = 0, j = 0; i < cf->len; i += 4, j++)
			*(u32 *)(cf->data + i) = readl(&mb_regs->data[j]);
	}

 mark_as_read:
	rf0r |= BXCAN_RF0R_RFOM0;
	writel(rf0r, &regs->rf0r);
	return skb;
}

static irqreturn_t bxcan_rx_isr(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct bxcan_priv *priv = netdev_priv(ndev);
	struct bxcan_regs __iomem *regs = priv->regs;
	u32 rf0r;

	rf0r = readl(&regs->rf0r);
	if (!(rf0r & BXCAN_RF0R_FMP0_MASK))
		return IRQ_NONE;

	can_rx_offload_irq_offload_fifo(&priv->offload);
	can_rx_offload_irq_finish(&priv->offload);

	return IRQ_HANDLED;
}

static irqreturn_t bxcan_tx_isr(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct bxcan_priv *priv = netdev_priv(ndev);
	struct bxcan_regs __iomem *regs = priv->regs;
	struct net_device_stats *stats = &ndev->stats;
	u32 tsr, rqcp_bit;
	int idx;

	tsr = readl(&regs->tsr);
	if (!(tsr & (BXCAN_TSR_RQCP0 | BXCAN_TSR_RQCP1 | BXCAN_TSR_RQCP2)))
		return IRQ_NONE;

	while (priv->tx_head - priv->tx_tail > 0) {
		idx = bxcan_get_tx_tail(priv);
		rqcp_bit = BXCAN_TSR_RQCP0 << (idx << 3);
		if (!(tsr & rqcp_bit))
			break;

		stats->tx_packets++;
		stats->tx_bytes += can_get_echo_skb(ndev, idx, NULL);
		priv->tx_tail++;
	}

	writel(tsr, &regs->tsr);

	if (bxcan_get_tx_free(priv)) {
		/* Make sure that anybody stopping the queue after
		 * this sees the new tx_ring->tail.
		 */
		smp_mb();
		netif_wake_queue(ndev);
	}

	return IRQ_HANDLED;
}

static void bxcan_handle_state_change(struct net_device *ndev, u32 esr)
{
	struct bxcan_priv *priv = netdev_priv(ndev);
	enum can_state new_state = priv->can.state;
	struct can_berr_counter bec;
	enum can_state rx_state, tx_state;
	struct sk_buff *skb;
	struct can_frame *cf;

	/* Early exit if no error flag is set */
	if (!(esr & (BXCAN_ESR_EWGF | BXCAN_ESR_EPVF | BXCAN_ESR_BOFF)))
		return;

	bec.txerr = FIELD_GET(BXCAN_ESR_TEC_MASK, esr);
	bec.rxerr = FIELD_GET(BXCAN_ESR_REC_MASK, esr);

	if (esr & BXCAN_ESR_BOFF)
		new_state = CAN_STATE_BUS_OFF;
	else if (esr & BXCAN_ESR_EPVF)
		new_state = CAN_STATE_ERROR_PASSIVE;
	else if (esr & BXCAN_ESR_EWGF)
		new_state = CAN_STATE_ERROR_WARNING;

	/* state hasn't changed */
	if (unlikely(new_state == priv->can.state))
		return;

	skb = alloc_can_err_skb(ndev, &cf);

	tx_state = bec.txerr >= bec.rxerr ? new_state : 0;
	rx_state = bec.txerr <= bec.rxerr ? new_state : 0;
	can_change_state(ndev, cf, tx_state, rx_state);

	if (new_state == CAN_STATE_BUS_OFF) {
		can_bus_off(ndev);
	} else if (skb) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
	}

	if (skb) {
		int err;

		err = can_rx_offload_queue_timestamp(&priv->offload, skb,
						     priv->timestamp);
		if (err)
			ndev->stats.rx_fifo_errors++;
	}
}

static void bxcan_handle_bus_err(struct net_device *ndev, u32 esr)
{
	struct bxcan_priv *priv = netdev_priv(ndev);
	enum bxcan_lec_code lec_code;
	struct can_frame *cf;
	struct sk_buff *skb;

	lec_code = FIELD_GET(BXCAN_ESR_LEC_MASK, esr);

	/* Early exit if no lec update or no error.
	 * No lec update means that no CAN bus event has been detected
	 * since CPU wrote BXCAN_LEC_UNUSED value to status reg.
	 */
	if (lec_code == BXCAN_LEC_UNUSED || lec_code == BXCAN_LEC_NO_ERROR)
		return;

	/* Common for all type of bus errors */
	priv->can.can_stats.bus_error++;

	/* Propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(ndev, &cf);
	if (skb)
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

	switch (lec_code) {
	case BXCAN_LEC_STUFF_ERROR:
		netdev_dbg(ndev, "Stuff error\n");
		ndev->stats.rx_errors++;
		if (skb)
			cf->data[2] |= CAN_ERR_PROT_STUFF;
		break;

	case BXCAN_LEC_FORM_ERROR:
		netdev_dbg(ndev, "Form error\n");
		ndev->stats.rx_errors++;
		if (skb)
			cf->data[2] |= CAN_ERR_PROT_FORM;
		break;

	case BXCAN_LEC_ACK_ERROR:
		netdev_dbg(ndev, "Ack error\n");
		ndev->stats.tx_errors++;
		if (skb) {
			cf->can_id |= CAN_ERR_ACK;
			cf->data[3] = CAN_ERR_PROT_LOC_ACK;
		}
		break;

	case BXCAN_LEC_BIT1_ERROR:
		netdev_dbg(ndev, "Bit error (recessive)\n");
		ndev->stats.tx_errors++;
		if (skb)
			cf->data[2] |= CAN_ERR_PROT_BIT1;
		break;

	case BXCAN_LEC_BIT0_ERROR:
		netdev_dbg(ndev, "Bit error (dominant)\n");
		ndev->stats.tx_errors++;
		if (skb)
			cf->data[2] |= CAN_ERR_PROT_BIT0;
		break;

	case BXCAN_LEC_CRC_ERROR:
		netdev_dbg(ndev, "CRC error\n");
		ndev->stats.rx_errors++;
		if (skb) {
			cf->data[2] |= CAN_ERR_PROT_BIT;
			cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
		}
		break;

	default:
		break;
	}

	if (skb) {
		int err;

		err = can_rx_offload_queue_timestamp(&priv->offload, skb,
						     priv->timestamp);
		if (err)
			ndev->stats.rx_fifo_errors++;
	}
}

static irqreturn_t bxcan_state_change_isr(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct bxcan_priv *priv = netdev_priv(ndev);
	struct bxcan_regs __iomem *regs = priv->regs;
	u32 msr, esr;

	msr = readl(&regs->msr);
	if (!(msr & BXCAN_MSR_ERRI))
		return IRQ_NONE;

	esr = readl(&regs->esr);
	bxcan_handle_state_change(ndev, esr);

	if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		bxcan_handle_bus_err(ndev, esr);

	msr |= BXCAN_MSR_ERRI;
	writel(msr, &regs->msr);
	can_rx_offload_irq_finish(&priv->offload);

	return IRQ_HANDLED;
}

static int bxcan_chip_start(struct net_device *ndev)
{
	struct bxcan_priv *priv = netdev_priv(ndev);
	struct bxcan_regs __iomem *regs = priv->regs;
	struct can_bittiming *bt = &priv->can.bittiming;
	u32 clr, set;
	int err;

	err = bxcan_chip_softreset(priv);
	if (err) {
		netdev_err(ndev, "failed to reset chip, error %pe\n",
			   ERR_PTR(err));
		return err;
	}

	err = bxcan_leave_sleep_mode(priv);
	if (err) {
		netdev_err(ndev, "failed to leave sleep mode, error %pe\n",
			   ERR_PTR(err));
		goto failed_leave_sleep;
	}

	err = bxcan_enter_init_mode(priv);
	if (err) {
		netdev_err(ndev, "failed to enter init mode, error %pe\n",
			   ERR_PTR(err));
		goto failed_enter_init;
	}

	/* MCR
	 *
	 * select request order priority
	 * enable time triggered mode
	 * bus-off state left on sw request
	 * sleep mode left on sw request
	 * retransmit automatically on error
	 * do not lock RX FIFO on overrun
	 */
	bxcan_rmw(priv, &regs->mcr,
		  BXCAN_MCR_ABOM | BXCAN_MCR_AWUM | BXCAN_MCR_NART |
		  BXCAN_MCR_RFLM, BXCAN_MCR_TTCM | BXCAN_MCR_TXFP);

	/* Bit timing register settings */
	set = FIELD_PREP(BXCAN_BTR_BRP_MASK, bt->brp - 1) |
		FIELD_PREP(BXCAN_BTR_TS1_MASK, bt->phase_seg1 +
			   bt->prop_seg - 1) |
		FIELD_PREP(BXCAN_BTR_TS2_MASK, bt->phase_seg2 - 1) |
		FIELD_PREP(BXCAN_BTR_SJW_MASK, bt->sjw - 1);

	/* loopback + silent mode put the controller in test mode,
	 * useful for hot self-test
	 */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		set |= BXCAN_BTR_LBKM;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		set |= BXCAN_BTR_SILM;

	bxcan_rmw(priv, &regs->btr, BXCAN_BTR_SILM | BXCAN_BTR_LBKM |
		  BXCAN_BTR_BRP_MASK | BXCAN_BTR_TS1_MASK | BXCAN_BTR_TS2_MASK |
		  BXCAN_BTR_SJW_MASK, set);

	bxcan_enable_filters(priv, priv->cfg);

	/* Clear all internal status */
	priv->tx_head = 0;
	priv->tx_tail = 0;

	err = bxcan_leave_init_mode(priv);
	if (err) {
		netdev_err(ndev, "failed to leave init mode, error %pe\n",
			   ERR_PTR(err));
		goto failed_leave_init;
	}

	/* Set a `lec` value so that we can check for updates later */
	bxcan_rmw(priv, &regs->esr, BXCAN_ESR_LEC_MASK,
		  FIELD_PREP(BXCAN_ESR_LEC_MASK, BXCAN_LEC_UNUSED));

	/* IER
	 *
	 * Enable interrupt for:
	 * bus-off
	 * passive error
	 * warning error
	 * last error code
	 * RX FIFO pending message
	 * TX mailbox empty
	 */
	clr = BXCAN_IER_WKUIE | BXCAN_IER_SLKIE |  BXCAN_IER_FOVIE1 |
		BXCAN_IER_FFIE1 | BXCAN_IER_FMPIE1 | BXCAN_IER_FOVIE0 |
		BXCAN_IER_FFIE0;
	set = BXCAN_IER_ERRIE | BXCAN_IER_BOFIE | BXCAN_IER_EPVIE |
		BXCAN_IER_EWGIE | BXCAN_IER_FMPIE0 | BXCAN_IER_TMEIE;

	if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		set |= BXCAN_IER_LECIE;
	else
		clr |= BXCAN_IER_LECIE;

	bxcan_rmw(priv, &regs->ier, clr, set);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	return 0;

failed_leave_init:
failed_enter_init:
failed_leave_sleep:
	bxcan_chip_softreset(priv);
	return err;
}

static int bxcan_open(struct net_device *ndev)
{
	struct bxcan_priv *priv = netdev_priv(ndev);
	int err;

	err = clk_prepare_enable(priv->clk);
	if (err) {
		netdev_err(ndev, "failed to enable clock, error %pe\n",
			   ERR_PTR(err));
		return err;
	}

	err = open_candev(ndev);
	if (err) {
		netdev_err(ndev, "open_candev() failed, error %pe\n",
			   ERR_PTR(err));
		goto out_disable_clock;
	}

	can_rx_offload_enable(&priv->offload);
	err = request_irq(ndev->irq, bxcan_rx_isr, IRQF_SHARED, ndev->name,
			  ndev);
	if (err) {
		netdev_err(ndev, "failed to register rx irq(%d), error %pe\n",
			   ndev->irq, ERR_PTR(err));
		goto out_close_candev;
	}

	err = request_irq(priv->tx_irq, bxcan_tx_isr, IRQF_SHARED, ndev->name,
			  ndev);
	if (err) {
		netdev_err(ndev, "failed to register tx irq(%d), error %pe\n",
			   priv->tx_irq, ERR_PTR(err));
		goto out_free_rx_irq;
	}

	err = request_irq(priv->sce_irq, bxcan_state_change_isr, IRQF_SHARED,
			  ndev->name, ndev);
	if (err) {
		netdev_err(ndev, "failed to register sce irq(%d), error %pe\n",
			   priv->sce_irq, ERR_PTR(err));
		goto out_free_tx_irq;
	}

	err = bxcan_chip_start(ndev);
	if (err)
		goto out_free_sce_irq;

	netif_start_queue(ndev);
	return 0;

out_free_sce_irq:
	free_irq(priv->sce_irq, ndev);
out_free_tx_irq:
	free_irq(priv->tx_irq, ndev);
out_free_rx_irq:
	free_irq(ndev->irq, ndev);
out_close_candev:
	can_rx_offload_disable(&priv->offload);
	close_candev(ndev);
out_disable_clock:
	clk_disable_unprepare(priv->clk);
	return err;
}

static void bxcan_chip_stop(struct net_device *ndev)
{
	struct bxcan_priv *priv = netdev_priv(ndev);
	struct bxcan_regs __iomem *regs = priv->regs;

	/* disable all interrupts */
	bxcan_rmw(priv, &regs->ier, BXCAN_IER_SLKIE | BXCAN_IER_WKUIE |
		  BXCAN_IER_ERRIE | BXCAN_IER_LECIE | BXCAN_IER_BOFIE |
		  BXCAN_IER_EPVIE | BXCAN_IER_EWGIE | BXCAN_IER_FOVIE1 |
		  BXCAN_IER_FFIE1 | BXCAN_IER_FMPIE1 | BXCAN_IER_FOVIE0 |
		  BXCAN_IER_FFIE0 | BXCAN_IER_FMPIE0 | BXCAN_IER_TMEIE, 0);
	bxcan_disable_filters(priv, priv->cfg);
	bxcan_enter_sleep_mode(priv);
	priv->can.state = CAN_STATE_STOPPED;
}

static int bxcan_stop(struct net_device *ndev)
{
	struct bxcan_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	bxcan_chip_stop(ndev);
	free_irq(ndev->irq, ndev);
	free_irq(priv->tx_irq, ndev);
	free_irq(priv->sce_irq, ndev);
	can_rx_offload_disable(&priv->offload);
	close_candev(ndev);
	clk_disable_unprepare(priv->clk);
	return 0;
}

static netdev_tx_t bxcan_start_xmit(struct sk_buff *skb,
				    struct net_device *ndev)
{
	struct bxcan_priv *priv = netdev_priv(ndev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	struct bxcan_regs __iomem *regs = priv->regs;
	struct bxcan_mb __iomem *mb_regs;
	unsigned int idx;
	u32 id;
	int i, j;

	if (can_dev_dropped_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (bxcan_tx_busy(priv))
		return NETDEV_TX_BUSY;

	idx = bxcan_get_tx_head(priv);
	priv->tx_head++;
	if (bxcan_get_tx_free(priv) == 0)
		netif_stop_queue(ndev);

	mb_regs = &regs->tx_mb[idx];
	if (cf->can_id & CAN_EFF_FLAG)
		id = FIELD_PREP(BXCAN_TIxR_EXID_MASK, cf->can_id) |
			BXCAN_TIxR_IDE;
	else
		id = FIELD_PREP(BXCAN_TIxR_STID_MASK, cf->can_id);

	if (cf->can_id & CAN_RTR_FLAG) { /* Remote transmission request */
		id |= BXCAN_TIxR_RTR;
	} else {
		for (i = 0, j = 0; i < cf->len; i += 4, j++)
			writel(*(u32 *)(cf->data + i), &mb_regs->data[j]);
	}

	writel(FIELD_PREP(BXCAN_TDTxR_DLC_MASK, cf->len), &mb_regs->dlc);

	can_put_echo_skb(skb, ndev, idx, 0);

	/* Start transmission */
	writel(id | BXCAN_TIxR_TXRQ, &mb_regs->id);

	return NETDEV_TX_OK;
}

static const struct net_device_ops bxcan_netdev_ops = {
	.ndo_open = bxcan_open,
	.ndo_stop = bxcan_stop,
	.ndo_start_xmit = bxcan_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct ethtool_ops bxcan_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static int bxcan_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int err;

	switch (mode) {
	case CAN_MODE_START:
		err = bxcan_chip_start(ndev);
		if (err)
			return err;

		netif_wake_queue(ndev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int bxcan_get_berr_counter(const struct net_device *ndev,
				  struct can_berr_counter *bec)
{
	struct bxcan_priv *priv = netdev_priv(ndev);
	struct bxcan_regs __iomem *regs = priv->regs;
	u32 esr;
	int err;

	err = clk_prepare_enable(priv->clk);
	if (err)
		return err;

	esr = readl(&regs->esr);
	bec->txerr = FIELD_GET(BXCAN_ESR_TEC_MASK, esr);
	bec->rxerr = FIELD_GET(BXCAN_ESR_REC_MASK, esr);
	clk_disable_unprepare(priv->clk);
	return 0;
}

static int bxcan_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct net_device *ndev;
	struct bxcan_priv *priv;
	struct clk *clk = NULL;
	void __iomem *regs;
	struct regmap *gcan;
	enum bxcan_cfg cfg;
	int err, rx_irq, tx_irq, sce_irq;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs)) {
		dev_err(dev, "failed to get base address\n");
		return PTR_ERR(regs);
	}

	gcan = syscon_regmap_lookup_by_phandle(np, "st,gcan");
	if (IS_ERR(gcan)) {
		dev_err(dev, "failed to get shared memory base address\n");
		return PTR_ERR(gcan);
	}

	if (of_property_read_bool(np, "st,can-primary"))
		cfg = BXCAN_CFG_DUAL_PRIMARY;
	else if (of_property_read_bool(np, "st,can-secondary"))
		cfg = BXCAN_CFG_DUAL_SECONDARY;
	else
		cfg = BXCAN_CFG_SINGLE;

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(clk);
	}

	rx_irq = platform_get_irq_byname(pdev, "rx0");
	if (rx_irq < 0)
		return rx_irq;

	tx_irq = platform_get_irq_byname(pdev, "tx");
	if (tx_irq < 0)
		return tx_irq;

	sce_irq = platform_get_irq_byname(pdev, "sce");
	if (sce_irq < 0)
		return sce_irq;

	ndev = alloc_candev(sizeof(struct bxcan_priv), BXCAN_TX_MB_NUM);
	if (!ndev) {
		dev_err(dev, "alloc_candev() failed\n");
		return -ENOMEM;
	}

	priv = netdev_priv(ndev);
	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, dev);
	ndev->netdev_ops = &bxcan_netdev_ops;
	ndev->ethtool_ops = &bxcan_ethtool_ops;
	ndev->irq = rx_irq;
	ndev->flags |= IFF_ECHO;

	priv->dev = dev;
	priv->ndev = ndev;
	priv->regs = regs;
	priv->gcan = gcan;
	priv->clk = clk;
	priv->tx_irq = tx_irq;
	priv->sce_irq = sce_irq;
	priv->cfg = cfg;
	priv->can.clock.freq = clk_get_rate(clk);
	spin_lock_init(&priv->rmw_lock);
	priv->tx_head = 0;
	priv->tx_tail = 0;
	priv->can.bittiming_const = &bxcan_bittiming_const;
	priv->can.do_set_mode = bxcan_do_set_mode;
	priv->can.do_get_berr_counter = bxcan_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
		CAN_CTRLMODE_LISTENONLY	| CAN_CTRLMODE_BERR_REPORTING;

	priv->offload.mailbox_read = bxcan_mailbox_read;
	err = can_rx_offload_add_fifo(ndev, &priv->offload, BXCAN_NAPI_WEIGHT);
	if (err) {
		dev_err(dev, "failed to add FIFO rx_offload\n");
		goto out_free_candev;
	}

	err = register_candev(ndev);
	if (err) {
		dev_err(dev, "failed to register netdev\n");
		goto out_can_rx_offload_del;
	}

	dev_info(dev, "clk: %d Hz, IRQs: %d, %d, %d\n", priv->can.clock.freq,
		 tx_irq, rx_irq, sce_irq);
	return 0;

out_can_rx_offload_del:
	can_rx_offload_del(&priv->offload);
out_free_candev:
	free_candev(ndev);
	return err;
}

static void bxcan_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct bxcan_priv *priv = netdev_priv(ndev);

	unregister_candev(ndev);
	clk_disable_unprepare(priv->clk);
	can_rx_offload_del(&priv->offload);
	free_candev(ndev);
}

static int __maybe_unused bxcan_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct bxcan_priv *priv = netdev_priv(ndev);

	if (!netif_running(ndev))
		return 0;

	netif_stop_queue(ndev);
	netif_device_detach(ndev);

	bxcan_enter_sleep_mode(priv);
	priv->can.state = CAN_STATE_SLEEPING;
	clk_disable_unprepare(priv->clk);
	return 0;
}

static int __maybe_unused bxcan_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct bxcan_priv *priv = netdev_priv(ndev);

	if (!netif_running(ndev))
		return 0;

	clk_prepare_enable(priv->clk);
	bxcan_leave_sleep_mode(priv);
	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	netif_device_attach(ndev);
	netif_start_queue(ndev);
	return 0;
}

static SIMPLE_DEV_PM_OPS(bxcan_pm_ops, bxcan_suspend, bxcan_resume);

static const struct of_device_id bxcan_of_match[] = {
	{.compatible = "st,stm32f4-bxcan"},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, bxcan_of_match);

static struct platform_driver bxcan_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.pm = &bxcan_pm_ops,
		.of_match_table = bxcan_of_match,
	},
	.probe = bxcan_probe,
	.remove = bxcan_remove,
};

module_platform_driver(bxcan_driver);

MODULE_AUTHOR("Dario Binacchi <dario.binacchi@amarulasolutions.com>");
MODULE_DESCRIPTION("STMicroelectronics Basic Extended CAN controller driver");
MODULE_LICENSE("GPL");
