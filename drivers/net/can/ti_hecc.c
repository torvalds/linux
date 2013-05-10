/*
 * TI HECC (CAN) device driver
 *
 * This driver supports TI's HECC (High End CAN Controller module) and the
 * specs for the same is available at <http://www.ti.com>
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed as is WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * Your platform definitions should specify module ram offsets and interrupt
 * number to use as follows:
 *
 * static struct ti_hecc_platform_data am3517_evm_hecc_pdata = {
 *         .scc_hecc_offset        = 0,
 *         .scc_ram_offset         = 0x3000,
 *         .hecc_ram_offset        = 0x3000,
 *         .mbx_offset             = 0x2000,
 *         .int_line               = 0,
 *         .revision               = 1,
 *         .transceiver_switch     = hecc_phy_control,
 * };
 *
 * Please see include/linux/can/platform/ti_hecc.h for description of
 * above fields.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/platform/ti_hecc.h>

#define DRV_NAME "ti_hecc"
#define HECC_MODULE_VERSION     "0.7"
MODULE_VERSION(HECC_MODULE_VERSION);
#define DRV_DESC "TI High End CAN Controller Driver " HECC_MODULE_VERSION

/* TX / RX Mailbox Configuration */
#define HECC_MAX_MAILBOXES	32	/* hardware mailboxes - do not change */
#define MAX_TX_PRIO		0x3F	/* hardware value - do not change */

/*
 * Important Note: TX mailbox configuration
 * TX mailboxes should be restricted to the number of SKB buffers to avoid
 * maintaining SKB buffers separately. TX mailboxes should be a power of 2
 * for the mailbox logic to work.  Top mailbox numbers are reserved for RX
 * and lower mailboxes for TX.
 *
 * HECC_MAX_TX_MBOX	HECC_MB_TX_SHIFT
 * 4 (default)		2
 * 8			3
 * 16			4
 */
#define HECC_MB_TX_SHIFT	2 /* as per table above */
#define HECC_MAX_TX_MBOX	BIT(HECC_MB_TX_SHIFT)

#define HECC_TX_PRIO_SHIFT	(HECC_MB_TX_SHIFT)
#define HECC_TX_PRIO_MASK	(MAX_TX_PRIO << HECC_MB_TX_SHIFT)
#define HECC_TX_MB_MASK		(HECC_MAX_TX_MBOX - 1)
#define HECC_TX_MASK		((HECC_MAX_TX_MBOX - 1) | HECC_TX_PRIO_MASK)
#define HECC_TX_MBOX_MASK	(~(BIT(HECC_MAX_TX_MBOX) - 1))
#define HECC_DEF_NAPI_WEIGHT	HECC_MAX_RX_MBOX

/*
 * Important Note: RX mailbox configuration
 * RX mailboxes are further logically split into two - main and buffer
 * mailboxes. The goal is to get all packets into main mailboxes as
 * driven by mailbox number and receive priority (higher to lower) and
 * buffer mailboxes are used to receive pkts while main mailboxes are being
 * processed. This ensures in-order packet reception.
 *
 * Here are the recommended values for buffer mailbox. Note that RX mailboxes
 * start after TX mailboxes:
 *
 * HECC_MAX_RX_MBOX		HECC_RX_BUFFER_MBOX	No of buffer mailboxes
 * 28				12			8
 * 16				20			4
 */

#define HECC_MAX_RX_MBOX	(HECC_MAX_MAILBOXES - HECC_MAX_TX_MBOX)
#define HECC_RX_BUFFER_MBOX	12 /* as per table above */
#define HECC_RX_FIRST_MBOX	(HECC_MAX_MAILBOXES - 1)
#define HECC_RX_HIGH_MBOX_MASK	(~(BIT(HECC_RX_BUFFER_MBOX) - 1))

/* TI HECC module registers */
#define HECC_CANME		0x0	/* Mailbox enable */
#define HECC_CANMD		0x4	/* Mailbox direction */
#define HECC_CANTRS		0x8	/* Transmit request set */
#define HECC_CANTRR		0xC	/* Transmit request */
#define HECC_CANTA		0x10	/* Transmission acknowledge */
#define HECC_CANAA		0x14	/* Abort acknowledge */
#define HECC_CANRMP		0x18	/* Receive message pending */
#define HECC_CANRML		0x1C	/* Remote message lost */
#define HECC_CANRFP		0x20	/* Remote frame pending */
#define HECC_CANGAM		0x24	/* SECC only:Global acceptance mask */
#define HECC_CANMC		0x28	/* Master control */
#define HECC_CANBTC		0x2C	/* Bit timing configuration */
#define HECC_CANES		0x30	/* Error and status */
#define HECC_CANTEC		0x34	/* Transmit error counter */
#define HECC_CANREC		0x38	/* Receive error counter */
#define HECC_CANGIF0		0x3C	/* Global interrupt flag 0 */
#define HECC_CANGIM		0x40	/* Global interrupt mask */
#define HECC_CANGIF1		0x44	/* Global interrupt flag 1 */
#define HECC_CANMIM		0x48	/* Mailbox interrupt mask */
#define HECC_CANMIL		0x4C	/* Mailbox interrupt level */
#define HECC_CANOPC		0x50	/* Overwrite protection control */
#define HECC_CANTIOC		0x54	/* Transmit I/O control */
#define HECC_CANRIOC		0x58	/* Receive I/O control */
#define HECC_CANLNT		0x5C	/* HECC only: Local network time */
#define HECC_CANTOC		0x60	/* HECC only: Time-out control */
#define HECC_CANTOS		0x64	/* HECC only: Time-out status */
#define HECC_CANTIOCE		0x68	/* SCC only:Enhanced TX I/O control */
#define HECC_CANRIOCE		0x6C	/* SCC only:Enhanced RX I/O control */

/* Mailbox registers */
#define HECC_CANMID		0x0
#define HECC_CANMCF		0x4
#define HECC_CANMDL		0x8
#define HECC_CANMDH		0xC

#define HECC_SET_REG		0xFFFFFFFF
#define HECC_CANID_MASK		0x3FF	/* 18 bits mask for extended id's */
#define HECC_CCE_WAIT_COUNT     100	/* Wait for ~1 sec for CCE bit */

#define HECC_CANMC_SCM		BIT(13)	/* SCC compat mode */
#define HECC_CANMC_CCR		BIT(12)	/* Change config request */
#define HECC_CANMC_PDR		BIT(11)	/* Local Power down - for sleep mode */
#define HECC_CANMC_ABO		BIT(7)	/* Auto Bus On */
#define HECC_CANMC_STM		BIT(6)	/* Self test mode - loopback */
#define HECC_CANMC_SRES		BIT(5)	/* Software reset */

#define HECC_CANTIOC_EN		BIT(3)	/* Enable CAN TX I/O pin */
#define HECC_CANRIOC_EN		BIT(3)	/* Enable CAN RX I/O pin */

#define HECC_CANMID_IDE		BIT(31)	/* Extended frame format */
#define HECC_CANMID_AME		BIT(30)	/* Acceptance mask enable */
#define HECC_CANMID_AAM		BIT(29)	/* Auto answer mode */

#define HECC_CANES_FE		BIT(24)	/* form error */
#define HECC_CANES_BE		BIT(23)	/* bit error */
#define HECC_CANES_SA1		BIT(22)	/* stuck at dominant error */
#define HECC_CANES_CRCE		BIT(21)	/* CRC error */
#define HECC_CANES_SE		BIT(20)	/* stuff bit error */
#define HECC_CANES_ACKE		BIT(19)	/* ack error */
#define HECC_CANES_BO		BIT(18)	/* Bus off status */
#define HECC_CANES_EP		BIT(17)	/* Error passive status */
#define HECC_CANES_EW		BIT(16)	/* Error warning status */
#define HECC_CANES_SMA		BIT(5)	/* suspend mode ack */
#define HECC_CANES_CCE		BIT(4)	/* Change config enabled */
#define HECC_CANES_PDA		BIT(3)	/* Power down mode ack */

#define HECC_CANBTC_SAM		BIT(7)	/* sample points */

#define HECC_BUS_ERROR		(HECC_CANES_FE | HECC_CANES_BE |\
				HECC_CANES_CRCE | HECC_CANES_SE |\
				HECC_CANES_ACKE)

#define HECC_CANMCF_RTR		BIT(4)	/* Remote transmit request */

#define HECC_CANGIF_MAIF	BIT(17)	/* Message alarm interrupt */
#define HECC_CANGIF_TCOIF	BIT(16) /* Timer counter overflow int */
#define HECC_CANGIF_GMIF	BIT(15)	/* Global mailbox interrupt */
#define HECC_CANGIF_AAIF	BIT(14)	/* Abort ack interrupt */
#define HECC_CANGIF_WDIF	BIT(13)	/* Write denied interrupt */
#define HECC_CANGIF_WUIF	BIT(12)	/* Wake up interrupt */
#define HECC_CANGIF_RMLIF	BIT(11)	/* Receive message lost interrupt */
#define HECC_CANGIF_BOIF	BIT(10)	/* Bus off interrupt */
#define HECC_CANGIF_EPIF	BIT(9)	/* Error passive interrupt */
#define HECC_CANGIF_WLIF	BIT(8)	/* Warning level interrupt */
#define HECC_CANGIF_MBOX_MASK	0x1F	/* Mailbox number mask */
#define HECC_CANGIM_I1EN	BIT(1)	/* Int line 1 enable */
#define HECC_CANGIM_I0EN	BIT(0)	/* Int line 0 enable */
#define HECC_CANGIM_DEF_MASK	0x700	/* only busoff/warning/passive */
#define HECC_CANGIM_SIL		BIT(2)	/* system interrupts to int line 1 */

/* CAN Bittiming constants as per HECC specs */
static struct can_bittiming_const ti_hecc_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

struct ti_hecc_priv {
	struct can_priv can;	/* MUST be first member/field */
	struct napi_struct napi;
	struct net_device *ndev;
	struct clk *clk;
	void __iomem *base;
	u32 scc_ram_offset;
	u32 hecc_ram_offset;
	u32 mbx_offset;
	u32 int_line;
	spinlock_t mbx_lock; /* CANME register needs protection */
	u32 tx_head;
	u32 tx_tail;
	u32 rx_next;
	void (*transceiver_switch)(int);
};

static inline int get_tx_head_mb(struct ti_hecc_priv *priv)
{
	return priv->tx_head & HECC_TX_MB_MASK;
}

static inline int get_tx_tail_mb(struct ti_hecc_priv *priv)
{
	return priv->tx_tail & HECC_TX_MB_MASK;
}

static inline int get_tx_head_prio(struct ti_hecc_priv *priv)
{
	return (priv->tx_head >> HECC_TX_PRIO_SHIFT) & MAX_TX_PRIO;
}

static inline void hecc_write_lam(struct ti_hecc_priv *priv, u32 mbxno, u32 val)
{
	__raw_writel(val, priv->base + priv->hecc_ram_offset + mbxno * 4);
}

static inline void hecc_write_mbx(struct ti_hecc_priv *priv, u32 mbxno,
	u32 reg, u32 val)
{
	__raw_writel(val, priv->base + priv->mbx_offset + mbxno * 0x10 +
			reg);
}

static inline u32 hecc_read_mbx(struct ti_hecc_priv *priv, u32 mbxno, u32 reg)
{
	return __raw_readl(priv->base + priv->mbx_offset + mbxno * 0x10 +
			reg);
}

static inline void hecc_write(struct ti_hecc_priv *priv, u32 reg, u32 val)
{
	__raw_writel(val, priv->base + reg);
}

static inline u32 hecc_read(struct ti_hecc_priv *priv, int reg)
{
	return __raw_readl(priv->base + reg);
}

static inline void hecc_set_bit(struct ti_hecc_priv *priv, int reg,
	u32 bit_mask)
{
	hecc_write(priv, reg, hecc_read(priv, reg) | bit_mask);
}

static inline void hecc_clear_bit(struct ti_hecc_priv *priv, int reg,
	u32 bit_mask)
{
	hecc_write(priv, reg, hecc_read(priv, reg) & ~bit_mask);
}

static inline u32 hecc_get_bit(struct ti_hecc_priv *priv, int reg, u32 bit_mask)
{
	return (hecc_read(priv, reg) & bit_mask) ? 1 : 0;
}

static int ti_hecc_get_state(const struct net_device *ndev,
	enum can_state *state)
{
	struct ti_hecc_priv *priv = netdev_priv(ndev);

	*state = priv->can.state;
	return 0;
}

static int ti_hecc_set_btc(struct ti_hecc_priv *priv)
{
	struct can_bittiming *bit_timing = &priv->can.bittiming;
	u32 can_btc;

	can_btc = (bit_timing->phase_seg2 - 1) & 0x7;
	can_btc |= ((bit_timing->phase_seg1 + bit_timing->prop_seg - 1)
			& 0xF) << 3;
	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES) {
		if (bit_timing->brp > 4)
			can_btc |= HECC_CANBTC_SAM;
		else
			dev_warn(priv->ndev->dev.parent, "WARN: Triple" \
				"sampling not set due to h/w limitations");
	}
	can_btc |= ((bit_timing->sjw - 1) & 0x3) << 8;
	can_btc |= ((bit_timing->brp - 1) & 0xFF) << 16;

	/* ERM being set to 0 by default meaning resync at falling edge */

	hecc_write(priv, HECC_CANBTC, can_btc);
	dev_info(priv->ndev->dev.parent, "setting CANBTC=%#x\n", can_btc);

	return 0;
}

static void ti_hecc_transceiver_switch(const struct ti_hecc_priv *priv,
					int on)
{
	if (priv->transceiver_switch)
		priv->transceiver_switch(on);
}

static void ti_hecc_reset(struct net_device *ndev)
{
	u32 cnt;
	struct ti_hecc_priv *priv = netdev_priv(ndev);

	dev_dbg(ndev->dev.parent, "resetting hecc ...\n");
	hecc_set_bit(priv, HECC_CANMC, HECC_CANMC_SRES);

	/* Set change control request and wait till enabled */
	hecc_set_bit(priv, HECC_CANMC, HECC_CANMC_CCR);

	/*
	 * INFO: It has been observed that at times CCE bit may not be
	 * set and hw seems to be ok even if this bit is not set so
	 * timing out with a timing of 1ms to respect the specs
	 */
	cnt = HECC_CCE_WAIT_COUNT;
	while (!hecc_get_bit(priv, HECC_CANES, HECC_CANES_CCE) && cnt != 0) {
		--cnt;
		udelay(10);
	}

	/*
	 * Note: On HECC, BTC can be programmed only in initialization mode, so
	 * it is expected that the can bittiming parameters are set via ip
	 * utility before the device is opened
	 */
	ti_hecc_set_btc(priv);

	/* Clear CCR (and CANMC register) and wait for CCE = 0 enable */
	hecc_write(priv, HECC_CANMC, 0);

	/*
	 * INFO: CAN net stack handles bus off and hence disabling auto-bus-on
	 * hecc_set_bit(priv, HECC_CANMC, HECC_CANMC_ABO);
	 */

	/*
	 * INFO: It has been observed that at times CCE bit may not be
	 * set and hw seems to be ok even if this bit is not set so
	 */
	cnt = HECC_CCE_WAIT_COUNT;
	while (hecc_get_bit(priv, HECC_CANES, HECC_CANES_CCE) && cnt != 0) {
		--cnt;
		udelay(10);
	}

	/* Enable TX and RX I/O Control pins */
	hecc_write(priv, HECC_CANTIOC, HECC_CANTIOC_EN);
	hecc_write(priv, HECC_CANRIOC, HECC_CANRIOC_EN);

	/* Clear registers for clean operation */
	hecc_write(priv, HECC_CANTA, HECC_SET_REG);
	hecc_write(priv, HECC_CANRMP, HECC_SET_REG);
	hecc_write(priv, HECC_CANGIF0, HECC_SET_REG);
	hecc_write(priv, HECC_CANGIF1, HECC_SET_REG);
	hecc_write(priv, HECC_CANME, 0);
	hecc_write(priv, HECC_CANMD, 0);

	/* SCC compat mode NOT supported (and not needed too) */
	hecc_set_bit(priv, HECC_CANMC, HECC_CANMC_SCM);
}

static void ti_hecc_start(struct net_device *ndev)
{
	struct ti_hecc_priv *priv = netdev_priv(ndev);
	u32 cnt, mbxno, mbx_mask;

	/* put HECC in initialization mode and set btc */
	ti_hecc_reset(ndev);

	priv->tx_head = priv->tx_tail = HECC_TX_MASK;
	priv->rx_next = HECC_RX_FIRST_MBOX;

	/* Enable local and global acceptance mask registers */
	hecc_write(priv, HECC_CANGAM, HECC_SET_REG);

	/* Prepare configured mailboxes to receive messages */
	for (cnt = 0; cnt < HECC_MAX_RX_MBOX; cnt++) {
		mbxno = HECC_MAX_MAILBOXES - 1 - cnt;
		mbx_mask = BIT(mbxno);
		hecc_clear_bit(priv, HECC_CANME, mbx_mask);
		hecc_write_mbx(priv, mbxno, HECC_CANMID, HECC_CANMID_AME);
		hecc_write_lam(priv, mbxno, HECC_SET_REG);
		hecc_set_bit(priv, HECC_CANMD, mbx_mask);
		hecc_set_bit(priv, HECC_CANME, mbx_mask);
		hecc_set_bit(priv, HECC_CANMIM, mbx_mask);
	}

	/* Prevent message over-write & Enable interrupts */
	hecc_write(priv, HECC_CANOPC, HECC_SET_REG);
	if (priv->int_line) {
		hecc_write(priv, HECC_CANMIL, HECC_SET_REG);
		hecc_write(priv, HECC_CANGIM, HECC_CANGIM_DEF_MASK |
			HECC_CANGIM_I1EN | HECC_CANGIM_SIL);
	} else {
		hecc_write(priv, HECC_CANMIL, 0);
		hecc_write(priv, HECC_CANGIM,
			HECC_CANGIM_DEF_MASK | HECC_CANGIM_I0EN);
	}
	priv->can.state = CAN_STATE_ERROR_ACTIVE;
}

static void ti_hecc_stop(struct net_device *ndev)
{
	struct ti_hecc_priv *priv = netdev_priv(ndev);

	/* Disable interrupts and disable mailboxes */
	hecc_write(priv, HECC_CANGIM, 0);
	hecc_write(priv, HECC_CANMIM, 0);
	hecc_write(priv, HECC_CANME, 0);
	priv->can.state = CAN_STATE_STOPPED;
}

static int ti_hecc_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int ret = 0;

	switch (mode) {
	case CAN_MODE_START:
		ti_hecc_start(ndev);
		netif_wake_queue(ndev);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

/*
 * ti_hecc_xmit: HECC Transmit
 *
 * The transmit mailboxes start from 0 to HECC_MAX_TX_MBOX. In HECC the
 * priority of the mailbox for tranmission is dependent upon priority setting
 * field in mailbox registers. The mailbox with highest value in priority field
 * is transmitted first. Only when two mailboxes have the same value in
 * priority field the highest numbered mailbox is transmitted first.
 *
 * To utilize the HECC priority feature as described above we start with the
 * highest numbered mailbox with highest priority level and move on to the next
 * mailbox with the same priority level and so on. Once we loop through all the
 * transmit mailboxes we choose the next priority level (lower) and so on
 * until we reach the lowest priority level on the lowest numbered mailbox
 * when we stop transmission until all mailboxes are transmitted and then
 * restart at highest numbered mailbox with highest priority.
 *
 * Two counters (head and tail) are used to track the next mailbox to transmit
 * and to track the echo buffer for already transmitted mailbox. The queue
 * is stopped when all the mailboxes are busy or when there is a priority
 * value roll-over happens.
 */
static netdev_tx_t ti_hecc_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ti_hecc_priv *priv = netdev_priv(ndev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	u32 mbxno, mbx_mask, data;
	unsigned long flags;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	mbxno = get_tx_head_mb(priv);
	mbx_mask = BIT(mbxno);
	spin_lock_irqsave(&priv->mbx_lock, flags);
	if (unlikely(hecc_read(priv, HECC_CANME) & mbx_mask)) {
		spin_unlock_irqrestore(&priv->mbx_lock, flags);
		netif_stop_queue(ndev);
		dev_err(priv->ndev->dev.parent,
			"BUG: TX mbx not ready tx_head=%08X, tx_tail=%08X\n",
			priv->tx_head, priv->tx_tail);
		return NETDEV_TX_BUSY;
	}
	spin_unlock_irqrestore(&priv->mbx_lock, flags);

	/* Prepare mailbox for transmission */
	if (cf->can_id & CAN_RTR_FLAG) /* Remote transmission request */
		data |= HECC_CANMCF_RTR;
	data |= get_tx_head_prio(priv) << 8;
	hecc_write_mbx(priv, mbxno, HECC_CANMCF, data);

	if (cf->can_id & CAN_EFF_FLAG) /* Extended frame format */
		data = (cf->can_id & CAN_EFF_MASK) | HECC_CANMID_IDE;
	else /* Standard frame format */
		data = (cf->can_id & CAN_SFF_MASK) << 18;
	hecc_write_mbx(priv, mbxno, HECC_CANMID, data);
	hecc_write_mbx(priv, mbxno, HECC_CANMDL,
		be32_to_cpu(*(u32 *)(cf->data)));
	if (cf->can_dlc > 4)
		hecc_write_mbx(priv, mbxno, HECC_CANMDH,
			be32_to_cpu(*(u32 *)(cf->data + 4)));
	else
		*(u32 *)(cf->data + 4) = 0;
	can_put_echo_skb(skb, ndev, mbxno);

	spin_lock_irqsave(&priv->mbx_lock, flags);
	--priv->tx_head;
	if ((hecc_read(priv, HECC_CANME) & BIT(get_tx_head_mb(priv))) ||
		(priv->tx_head & HECC_TX_MASK) == HECC_TX_MASK) {
		netif_stop_queue(ndev);
	}
	hecc_set_bit(priv, HECC_CANME, mbx_mask);
	spin_unlock_irqrestore(&priv->mbx_lock, flags);

	hecc_clear_bit(priv, HECC_CANMD, mbx_mask);
	hecc_set_bit(priv, HECC_CANMIM, mbx_mask);
	hecc_write(priv, HECC_CANTRS, mbx_mask);

	return NETDEV_TX_OK;
}

static int ti_hecc_rx_pkt(struct ti_hecc_priv *priv, int mbxno)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 data, mbx_mask;
	unsigned long flags;

	skb = alloc_can_skb(priv->ndev, &cf);
	if (!skb) {
		if (printk_ratelimit())
			dev_err(priv->ndev->dev.parent,
				"ti_hecc_rx_pkt: alloc_can_skb() failed\n");
		return -ENOMEM;
	}

	mbx_mask = BIT(mbxno);
	data = hecc_read_mbx(priv, mbxno, HECC_CANMID);
	if (data & HECC_CANMID_IDE)
		cf->can_id = (data & CAN_EFF_MASK) | CAN_EFF_FLAG;
	else
		cf->can_id = (data >> 18) & CAN_SFF_MASK;
	data = hecc_read_mbx(priv, mbxno, HECC_CANMCF);
	if (data & HECC_CANMCF_RTR)
		cf->can_id |= CAN_RTR_FLAG;
	cf->can_dlc = get_can_dlc(data & 0xF);
	data = hecc_read_mbx(priv, mbxno, HECC_CANMDL);
	*(u32 *)(cf->data) = cpu_to_be32(data);
	if (cf->can_dlc > 4) {
		data = hecc_read_mbx(priv, mbxno, HECC_CANMDH);
		*(u32 *)(cf->data + 4) = cpu_to_be32(data);
	} else {
		*(u32 *)(cf->data + 4) = 0;
	}
	spin_lock_irqsave(&priv->mbx_lock, flags);
	hecc_clear_bit(priv, HECC_CANME, mbx_mask);
	hecc_write(priv, HECC_CANRMP, mbx_mask);
	/* enable mailbox only if it is part of rx buffer mailboxes */
	if (priv->rx_next < HECC_RX_BUFFER_MBOX)
		hecc_set_bit(priv, HECC_CANME, mbx_mask);
	spin_unlock_irqrestore(&priv->mbx_lock, flags);

	stats->rx_bytes += cf->can_dlc;
	netif_receive_skb(skb);
	stats->rx_packets++;

	return 0;
}

/*
 * ti_hecc_rx_poll - HECC receive pkts
 *
 * The receive mailboxes start from highest numbered mailbox till last xmit
 * mailbox. On CAN frame reception the hardware places the data into highest
 * numbered mailbox that matches the CAN ID filter. Since all receive mailboxes
 * have same filtering (ALL CAN frames) packets will arrive in the highest
 * available RX mailbox and we need to ensure in-order packet reception.
 *
 * To ensure the packets are received in the right order we logically divide
 * the RX mailboxes into main and buffer mailboxes. Packets are received as per
 * mailbox priotity (higher to lower) in the main bank and once it is full we
 * disable further reception into main mailboxes. While the main mailboxes are
 * processed in NAPI, further packets are received in buffer mailboxes.
 *
 * We maintain a RX next mailbox counter to process packets and once all main
 * mailboxe packets are passed to the upper stack we enable all of them but
 * continue to process packets received in buffer mailboxes. With each packet
 * received from buffer mailbox we enable it immediately so as to handle the
 * overflow from higher mailboxes.
 */
static int ti_hecc_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct ti_hecc_priv *priv = netdev_priv(ndev);
	u32 num_pkts = 0;
	u32 mbx_mask;
	unsigned long pending_pkts, flags;

	if (!netif_running(ndev))
		return 0;

	while ((pending_pkts = hecc_read(priv, HECC_CANRMP)) &&
		num_pkts < quota) {
		mbx_mask = BIT(priv->rx_next); /* next rx mailbox to process */
		if (mbx_mask & pending_pkts) {
			if (ti_hecc_rx_pkt(priv, priv->rx_next) < 0)
				return num_pkts;
			++num_pkts;
		} else if (priv->rx_next > HECC_RX_BUFFER_MBOX) {
			break; /* pkt not received yet */
		}
		--priv->rx_next;
		if (priv->rx_next == HECC_RX_BUFFER_MBOX) {
			/* enable high bank mailboxes */
			spin_lock_irqsave(&priv->mbx_lock, flags);
			mbx_mask = hecc_read(priv, HECC_CANME);
			mbx_mask |= HECC_RX_HIGH_MBOX_MASK;
			hecc_write(priv, HECC_CANME, mbx_mask);
			spin_unlock_irqrestore(&priv->mbx_lock, flags);
		} else if (priv->rx_next == HECC_MAX_TX_MBOX - 1) {
			priv->rx_next = HECC_RX_FIRST_MBOX;
			break;
		}
	}

	/* Enable packet interrupt if all pkts are handled */
	if (hecc_read(priv, HECC_CANRMP) == 0) {
		napi_complete(napi);
		/* Re-enable RX mailbox interrupts */
		mbx_mask = hecc_read(priv, HECC_CANMIM);
		mbx_mask |= HECC_TX_MBOX_MASK;
		hecc_write(priv, HECC_CANMIM, mbx_mask);
	}

	return num_pkts;
}

static int ti_hecc_error(struct net_device *ndev, int int_status,
	int err_status)
{
	struct ti_hecc_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;

	/* propagate the error condition to the can stack */
	skb = alloc_can_err_skb(ndev, &cf);
	if (!skb) {
		if (printk_ratelimit())
			dev_err(priv->ndev->dev.parent,
				"ti_hecc_error: alloc_can_err_skb() failed\n");
		return -ENOMEM;
	}

	if (int_status & HECC_CANGIF_WLIF) { /* warning level int */
		if ((int_status & HECC_CANGIF_BOIF) == 0) {
			priv->can.state = CAN_STATE_ERROR_WARNING;
			++priv->can.can_stats.error_warning;
			cf->can_id |= CAN_ERR_CRTL;
			if (hecc_read(priv, HECC_CANTEC) > 96)
				cf->data[1] |= CAN_ERR_CRTL_TX_WARNING;
			if (hecc_read(priv, HECC_CANREC) > 96)
				cf->data[1] |= CAN_ERR_CRTL_RX_WARNING;
		}
		hecc_set_bit(priv, HECC_CANES, HECC_CANES_EW);
		dev_dbg(priv->ndev->dev.parent, "Error Warning interrupt\n");
		hecc_clear_bit(priv, HECC_CANMC, HECC_CANMC_CCR);
	}

	if (int_status & HECC_CANGIF_EPIF) { /* error passive int */
		if ((int_status & HECC_CANGIF_BOIF) == 0) {
			priv->can.state = CAN_STATE_ERROR_PASSIVE;
			++priv->can.can_stats.error_passive;
			cf->can_id |= CAN_ERR_CRTL;
			if (hecc_read(priv, HECC_CANTEC) > 127)
				cf->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
			if (hecc_read(priv, HECC_CANREC) > 127)
				cf->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
		}
		hecc_set_bit(priv, HECC_CANES, HECC_CANES_EP);
		dev_dbg(priv->ndev->dev.parent, "Error passive interrupt\n");
		hecc_clear_bit(priv, HECC_CANMC, HECC_CANMC_CCR);
	}

	/*
	 * Need to check busoff condition in error status register too to
	 * ensure warning interrupts don't hog the system
	 */
	if ((int_status & HECC_CANGIF_BOIF) || (err_status & HECC_CANES_BO)) {
		priv->can.state = CAN_STATE_BUS_OFF;
		cf->can_id |= CAN_ERR_BUSOFF;
		hecc_set_bit(priv, HECC_CANES, HECC_CANES_BO);
		hecc_clear_bit(priv, HECC_CANMC, HECC_CANMC_CCR);
		/* Disable all interrupts in bus-off to avoid int hog */
		hecc_write(priv, HECC_CANGIM, 0);
		can_bus_off(ndev);
	}

	if (err_status & HECC_BUS_ERROR) {
		++priv->can.can_stats.bus_error;
		cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_PROT;
		cf->data[2] |= CAN_ERR_PROT_UNSPEC;
		if (err_status & HECC_CANES_FE) {
			hecc_set_bit(priv, HECC_CANES, HECC_CANES_FE);
			cf->data[2] |= CAN_ERR_PROT_FORM;
		}
		if (err_status & HECC_CANES_BE) {
			hecc_set_bit(priv, HECC_CANES, HECC_CANES_BE);
			cf->data[2] |= CAN_ERR_PROT_BIT;
		}
		if (err_status & HECC_CANES_SE) {
			hecc_set_bit(priv, HECC_CANES, HECC_CANES_SE);
			cf->data[2] |= CAN_ERR_PROT_STUFF;
		}
		if (err_status & HECC_CANES_CRCE) {
			hecc_set_bit(priv, HECC_CANES, HECC_CANES_CRCE);
			cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ |
					CAN_ERR_PROT_LOC_CRC_DEL;
		}
		if (err_status & HECC_CANES_ACKE) {
			hecc_set_bit(priv, HECC_CANES, HECC_CANES_ACKE);
			cf->data[3] |= CAN_ERR_PROT_LOC_ACK |
					CAN_ERR_PROT_LOC_ACK_DEL;
		}
	}

	netif_receive_skb(skb);
	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	return 0;
}

static irqreturn_t ti_hecc_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct ti_hecc_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	u32 mbxno, mbx_mask, int_status, err_status;
	unsigned long ack, flags;

	int_status = hecc_read(priv,
		(priv->int_line) ? HECC_CANGIF1 : HECC_CANGIF0);

	if (!int_status)
		return IRQ_NONE;

	err_status = hecc_read(priv, HECC_CANES);
	if (err_status & (HECC_BUS_ERROR | HECC_CANES_BO |
		HECC_CANES_EP | HECC_CANES_EW))
			ti_hecc_error(ndev, int_status, err_status);

	if (int_status & HECC_CANGIF_GMIF) {
		while (priv->tx_tail - priv->tx_head > 0) {
			mbxno = get_tx_tail_mb(priv);
			mbx_mask = BIT(mbxno);
			if (!(mbx_mask & hecc_read(priv, HECC_CANTA)))
				break;
			hecc_clear_bit(priv, HECC_CANMIM, mbx_mask);
			hecc_write(priv, HECC_CANTA, mbx_mask);
			spin_lock_irqsave(&priv->mbx_lock, flags);
			hecc_clear_bit(priv, HECC_CANME, mbx_mask);
			spin_unlock_irqrestore(&priv->mbx_lock, flags);
			stats->tx_bytes += hecc_read_mbx(priv, mbxno,
						HECC_CANMCF) & 0xF;
			stats->tx_packets++;
			can_get_echo_skb(ndev, mbxno);
			--priv->tx_tail;
		}

		/* restart queue if wrap-up or if queue stalled on last pkt */
		if (((priv->tx_head == priv->tx_tail) &&
		((priv->tx_head & HECC_TX_MASK) != HECC_TX_MASK)) ||
		(((priv->tx_tail & HECC_TX_MASK) == HECC_TX_MASK) &&
		((priv->tx_head & HECC_TX_MASK) == HECC_TX_MASK)))
			netif_wake_queue(ndev);

		/* Disable RX mailbox interrupts and let NAPI reenable them */
		if (hecc_read(priv, HECC_CANRMP)) {
			ack = hecc_read(priv, HECC_CANMIM);
			ack &= BIT(HECC_MAX_TX_MBOX) - 1;
			hecc_write(priv, HECC_CANMIM, ack);
			napi_schedule(&priv->napi);
		}
	}

	/* clear all interrupt conditions - read back to avoid spurious ints */
	if (priv->int_line) {
		hecc_write(priv, HECC_CANGIF1, HECC_SET_REG);
		int_status = hecc_read(priv, HECC_CANGIF1);
	} else {
		hecc_write(priv, HECC_CANGIF0, HECC_SET_REG);
		int_status = hecc_read(priv, HECC_CANGIF0);
	}

	return IRQ_HANDLED;
}

static int ti_hecc_open(struct net_device *ndev)
{
	struct ti_hecc_priv *priv = netdev_priv(ndev);
	int err;

	err = request_irq(ndev->irq, ti_hecc_interrupt, IRQF_SHARED,
			ndev->name, ndev);
	if (err) {
		dev_err(ndev->dev.parent, "error requesting interrupt\n");
		return err;
	}

	ti_hecc_transceiver_switch(priv, 1);

	/* Open common can device */
	err = open_candev(ndev);
	if (err) {
		dev_err(ndev->dev.parent, "open_candev() failed %d\n", err);
		ti_hecc_transceiver_switch(priv, 0);
		free_irq(ndev->irq, ndev);
		return err;
	}

	ti_hecc_start(ndev);
	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	return 0;
}

static int ti_hecc_close(struct net_device *ndev)
{
	struct ti_hecc_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	ti_hecc_stop(ndev);
	free_irq(ndev->irq, ndev);
	close_candev(ndev);
	ti_hecc_transceiver_switch(priv, 0);

	return 0;
}

static const struct net_device_ops ti_hecc_netdev_ops = {
	.ndo_open		= ti_hecc_open,
	.ndo_stop		= ti_hecc_close,
	.ndo_start_xmit		= ti_hecc_xmit,
};

static int ti_hecc_probe(struct platform_device *pdev)
{
	struct net_device *ndev = (struct net_device *)0;
	struct ti_hecc_priv *priv;
	struct ti_hecc_platform_data *pdata;
	struct resource *mem, *irq;
	void __iomem *addr;
	int err = -ENODEV;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data\n");
		goto probe_exit;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "No mem resources\n");
		goto probe_exit;
	}
	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq) {
		dev_err(&pdev->dev, "No irq resource\n");
		goto probe_exit;
	}
	if (!request_mem_region(mem->start, resource_size(mem), pdev->name)) {
		dev_err(&pdev->dev, "HECC region already claimed\n");
		err = -EBUSY;
		goto probe_exit;
	}
	addr = ioremap(mem->start, resource_size(mem));
	if (!addr) {
		dev_err(&pdev->dev, "ioremap failed\n");
		err = -ENOMEM;
		goto probe_exit_free_region;
	}

	ndev = alloc_candev(sizeof(struct ti_hecc_priv), HECC_MAX_TX_MBOX);
	if (!ndev) {
		dev_err(&pdev->dev, "alloc_candev failed\n");
		err = -ENOMEM;
		goto probe_exit_iounmap;
	}

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->base = addr;
	priv->scc_ram_offset = pdata->scc_ram_offset;
	priv->hecc_ram_offset = pdata->hecc_ram_offset;
	priv->mbx_offset = pdata->mbx_offset;
	priv->int_line = pdata->int_line;
	priv->transceiver_switch = pdata->transceiver_switch;

	priv->can.bittiming_const = &ti_hecc_bittiming_const;
	priv->can.do_set_mode = ti_hecc_do_set_mode;
	priv->can.do_get_state = ti_hecc_get_state;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES;

	ndev->irq = irq->start;
	ndev->flags |= IFF_ECHO;
	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &ti_hecc_netdev_ops;

	priv->clk = clk_get(&pdev->dev, "hecc_ck");
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "No clock available\n");
		err = PTR_ERR(priv->clk);
		priv->clk = NULL;
		goto probe_exit_candev;
	}
	priv->can.clock.freq = clk_get_rate(priv->clk);
	netif_napi_add(ndev, &priv->napi, ti_hecc_rx_poll,
		HECC_DEF_NAPI_WEIGHT);

	clk_enable(priv->clk);
	err = register_candev(ndev);
	if (err) {
		dev_err(&pdev->dev, "register_candev() failed\n");
		goto probe_exit_clk;
	}
	dev_info(&pdev->dev, "device registered (reg_base=%p, irq=%u)\n",
		priv->base, (u32) ndev->irq);

	return 0;

probe_exit_clk:
	clk_put(priv->clk);
probe_exit_candev:
	free_candev(ndev);
probe_exit_iounmap:
	iounmap(addr);
probe_exit_free_region:
	release_mem_region(mem->start, resource_size(mem));
probe_exit:
	return err;
}

static int __devexit ti_hecc_remove(struct platform_device *pdev)
{
	struct resource *res;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ti_hecc_priv *priv = netdev_priv(ndev);

	unregister_candev(ndev);
	clk_disable(priv->clk);
	clk_put(priv->clk);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iounmap(priv->base);
	release_mem_region(res->start, resource_size(res));
	free_candev(ndev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}


#ifdef CONFIG_PM
static int ti_hecc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct ti_hecc_priv *priv = netdev_priv(dev);

	if (netif_running(dev)) {
		netif_stop_queue(dev);
		netif_device_detach(dev);
	}

	hecc_set_bit(priv, HECC_CANMC, HECC_CANMC_PDR);
	priv->can.state = CAN_STATE_SLEEPING;

	clk_disable(priv->clk);

	return 0;
}

static int ti_hecc_resume(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct ti_hecc_priv *priv = netdev_priv(dev);

	clk_enable(priv->clk);

	hecc_clear_bit(priv, HECC_CANMC, HECC_CANMC_PDR);
	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	if (netif_running(dev)) {
		netif_device_attach(dev);
		netif_start_queue(dev);
	}

	return 0;
}
#else
#define ti_hecc_suspend NULL
#define ti_hecc_resume NULL
#endif

/* TI HECC netdevice driver: platform driver structure */
static struct platform_driver ti_hecc_driver = {
	.driver = {
		.name    = DRV_NAME,
		.owner   = THIS_MODULE,
	},
	.probe = ti_hecc_probe,
	.remove = __devexit_p(ti_hecc_remove),
	.suspend = ti_hecc_suspend,
	.resume = ti_hecc_resume,
};

static int __init ti_hecc_init_driver(void)
{
	printk(KERN_INFO DRV_DESC "\n");
	return platform_driver_register(&ti_hecc_driver);
}

static void __exit ti_hecc_exit_driver(void)
{
	printk(KERN_INFO DRV_DESC " unloaded\n");
	platform_driver_unregister(&ti_hecc_driver);
}

module_exit(ti_hecc_exit_driver);
module_init(ti_hecc_init_driver);

MODULE_AUTHOR("Anant Gole <anantgole@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRV_DESC);
