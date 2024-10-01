// SPDX-License-Identifier: GPL-2.0-only
/*
 * at91_can.c - CAN network driver for AT91 SoC CAN controller
 *
 * (C) 2007 by Hans J. Koch <hjk@hansjkoch.de>
 * (C) 2008, 2009, 2010, 2011, 2023 by Marc Kleine-Budde <kernel@pengutronix.de>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/if_arp.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/rx-offload.h>

#define AT91_MB_MASK(i) ((1 << (i)) - 1)

/* Common registers */
enum at91_reg {
	AT91_MR = 0x000,
	AT91_IER = 0x004,
	AT91_IDR = 0x008,
	AT91_IMR = 0x00C,
	AT91_SR = 0x010,
	AT91_BR = 0x014,
	AT91_TIM = 0x018,
	AT91_TIMESTP = 0x01C,
	AT91_ECR = 0x020,
	AT91_TCR = 0x024,
	AT91_ACR = 0x028,
};

/* Mailbox registers (0 <= i <= 15) */
#define AT91_MMR(i) ((enum at91_reg)(0x200 + ((i) * 0x20)))
#define AT91_MAM(i) ((enum at91_reg)(0x204 + ((i) * 0x20)))
#define AT91_MID(i) ((enum at91_reg)(0x208 + ((i) * 0x20)))
#define AT91_MFID(i) ((enum at91_reg)(0x20C + ((i) * 0x20)))
#define AT91_MSR(i) ((enum at91_reg)(0x210 + ((i) * 0x20)))
#define AT91_MDL(i) ((enum at91_reg)(0x214 + ((i) * 0x20)))
#define AT91_MDH(i) ((enum at91_reg)(0x218 + ((i) * 0x20)))
#define AT91_MCR(i) ((enum at91_reg)(0x21C + ((i) * 0x20)))

/* Register bits */
#define AT91_MR_CANEN BIT(0)
#define AT91_MR_LPM BIT(1)
#define AT91_MR_ABM BIT(2)
#define AT91_MR_OVL BIT(3)
#define AT91_MR_TEOF BIT(4)
#define AT91_MR_TTM BIT(5)
#define AT91_MR_TIMFRZ BIT(6)
#define AT91_MR_DRPT BIT(7)

#define AT91_SR_RBSY BIT(29)
#define AT91_SR_TBSY BIT(30)
#define AT91_SR_OVLSY BIT(31)

#define AT91_BR_PHASE2_MASK GENMASK(2, 0)
#define AT91_BR_PHASE1_MASK GENMASK(6, 4)
#define AT91_BR_PROPAG_MASK GENMASK(10, 8)
#define AT91_BR_SJW_MASK GENMASK(13, 12)
#define AT91_BR_BRP_MASK GENMASK(22, 16)
#define AT91_BR_SMP BIT(24)

#define AT91_TIM_TIMER_MASK GENMASK(15, 0)

#define AT91_ECR_REC_MASK GENMASK(8, 0)
#define AT91_ECR_TEC_MASK GENMASK(23, 16)

#define AT91_TCR_TIMRST BIT(31)

#define AT91_MMR_MTIMEMARK_MASK GENMASK(15, 0)
#define AT91_MMR_PRIOR_MASK GENMASK(19, 16)
#define AT91_MMR_MOT_MASK GENMASK(26, 24)

#define AT91_MID_MIDVB_MASK GENMASK(17, 0)
#define AT91_MID_MIDVA_MASK GENMASK(28, 18)
#define AT91_MID_MIDE BIT(29)

#define AT91_MSR_MTIMESTAMP_MASK GENMASK(15, 0)
#define AT91_MSR_MDLC_MASK GENMASK(19, 16)
#define AT91_MSR_MRTR BIT(20)
#define AT91_MSR_MABT BIT(22)
#define AT91_MSR_MRDY BIT(23)
#define AT91_MSR_MMI BIT(24)

#define AT91_MCR_MDLC_MASK GENMASK(19, 16)
#define AT91_MCR_MRTR BIT(20)
#define AT91_MCR_MACR BIT(22)
#define AT91_MCR_MTCR BIT(23)

/* Mailbox Modes */
enum at91_mb_mode {
	AT91_MB_MODE_DISABLED = 0,
	AT91_MB_MODE_RX = 1,
	AT91_MB_MODE_RX_OVRWR = 2,
	AT91_MB_MODE_TX = 3,
	AT91_MB_MODE_CONSUMER = 4,
	AT91_MB_MODE_PRODUCER = 5,
};

/* Interrupt mask bits */
#define AT91_IRQ_ERRA BIT(16)
#define AT91_IRQ_WARN BIT(17)
#define AT91_IRQ_ERRP BIT(18)
#define AT91_IRQ_BOFF BIT(19)
#define AT91_IRQ_SLEEP BIT(20)
#define AT91_IRQ_WAKEUP BIT(21)
#define AT91_IRQ_TOVF BIT(22)
#define AT91_IRQ_TSTP BIT(23)
#define AT91_IRQ_CERR BIT(24)
#define AT91_IRQ_SERR BIT(25)
#define AT91_IRQ_AERR BIT(26)
#define AT91_IRQ_FERR BIT(27)
#define AT91_IRQ_BERR BIT(28)

#define AT91_IRQ_ERR_ALL (0x1fff0000)
#define AT91_IRQ_ERR_FRAME (AT91_IRQ_CERR | AT91_IRQ_SERR | \
			    AT91_IRQ_AERR | AT91_IRQ_FERR | AT91_IRQ_BERR)
#define AT91_IRQ_ERR_LINE (AT91_IRQ_ERRA | AT91_IRQ_WARN | \
			   AT91_IRQ_ERRP | AT91_IRQ_BOFF)

#define AT91_IRQ_ALL (0x1fffffff)

enum at91_devtype {
	AT91_DEVTYPE_SAM9263,
	AT91_DEVTYPE_SAM9X5,
};

struct at91_devtype_data {
	unsigned int rx_first;
	unsigned int rx_last;
	unsigned int tx_shift;
	enum at91_devtype type;
};

struct at91_priv {
	struct can_priv can;		/* must be the first member! */
	struct can_rx_offload offload;
	struct phy *transceiver;

	void __iomem *reg_base;

	unsigned int tx_head;
	unsigned int tx_tail;
	struct at91_devtype_data devtype_data;

	struct clk *clk;
	struct at91_can_data *pdata;

	canid_t mb0_id;
};

static inline struct at91_priv *rx_offload_to_priv(struct can_rx_offload *offload)
{
	return container_of(offload, struct at91_priv, offload);
}

static const struct at91_devtype_data at91_at91sam9263_data = {
	.rx_first = 1,
	.rx_last = 11,
	.tx_shift = 2,
	.type = AT91_DEVTYPE_SAM9263,
};

static const struct at91_devtype_data at91_at91sam9x5_data = {
	.rx_first = 0,
	.rx_last = 5,
	.tx_shift = 1,
	.type = AT91_DEVTYPE_SAM9X5,
};

static const struct can_bittiming_const at91_bittiming_const = {
	.name		= KBUILD_MODNAME,
	.tseg1_min	= 4,
	.tseg1_max	= 16,
	.tseg2_min	= 2,
	.tseg2_max	= 8,
	.sjw_max	= 4,
	.brp_min	= 2,
	.brp_max	= 128,
	.brp_inc	= 1,
};

#define AT91_IS(_model) \
static inline int __maybe_unused at91_is_sam##_model(const struct at91_priv *priv) \
{ \
	return priv->devtype_data.type == AT91_DEVTYPE_SAM##_model; \
}

AT91_IS(9263);
AT91_IS(9X5);

static inline unsigned int get_mb_rx_first(const struct at91_priv *priv)
{
	return priv->devtype_data.rx_first;
}

static inline unsigned int get_mb_rx_last(const struct at91_priv *priv)
{
	return priv->devtype_data.rx_last;
}

static inline unsigned int get_mb_tx_shift(const struct at91_priv *priv)
{
	return priv->devtype_data.tx_shift;
}

static inline unsigned int get_mb_tx_num(const struct at91_priv *priv)
{
	return 1 << get_mb_tx_shift(priv);
}

static inline unsigned int get_mb_tx_first(const struct at91_priv *priv)
{
	return get_mb_rx_last(priv) + 1;
}

static inline unsigned int get_mb_tx_last(const struct at91_priv *priv)
{
	return get_mb_tx_first(priv) + get_mb_tx_num(priv) - 1;
}

static inline unsigned int get_head_prio_shift(const struct at91_priv *priv)
{
	return get_mb_tx_shift(priv);
}

static inline unsigned int get_head_prio_mask(const struct at91_priv *priv)
{
	return 0xf << get_mb_tx_shift(priv);
}

static inline unsigned int get_head_mb_mask(const struct at91_priv *priv)
{
	return AT91_MB_MASK(get_mb_tx_shift(priv));
}

static inline unsigned int get_head_mask(const struct at91_priv *priv)
{
	return get_head_mb_mask(priv) | get_head_prio_mask(priv);
}

static inline unsigned int get_irq_mb_rx(const struct at91_priv *priv)
{
	return AT91_MB_MASK(get_mb_rx_last(priv) + 1) &
		~AT91_MB_MASK(get_mb_rx_first(priv));
}

static inline unsigned int get_irq_mb_tx(const struct at91_priv *priv)
{
	return AT91_MB_MASK(get_mb_tx_last(priv) + 1) &
		~AT91_MB_MASK(get_mb_tx_first(priv));
}

static inline unsigned int get_tx_head_mb(const struct at91_priv *priv)
{
	return (priv->tx_head & get_head_mb_mask(priv)) + get_mb_tx_first(priv);
}

static inline unsigned int get_tx_head_prio(const struct at91_priv *priv)
{
	return (priv->tx_head >> get_head_prio_shift(priv)) & 0xf;
}

static inline unsigned int get_tx_tail_mb(const struct at91_priv *priv)
{
	return (priv->tx_tail & get_head_mb_mask(priv)) + get_mb_tx_first(priv);
}

static inline u32 at91_read(const struct at91_priv *priv, enum at91_reg reg)
{
	return readl_relaxed(priv->reg_base + reg);
}

static inline void at91_write(const struct at91_priv *priv, enum at91_reg reg,
			      u32 value)
{
	writel_relaxed(value, priv->reg_base + reg);
}

static inline void set_mb_mode_prio(const struct at91_priv *priv,
				    unsigned int mb, enum at91_mb_mode mode,
				    u8 prio)
{
	const u32 reg_mmr = FIELD_PREP(AT91_MMR_MOT_MASK, mode) |
		FIELD_PREP(AT91_MMR_PRIOR_MASK, prio);

	at91_write(priv, AT91_MMR(mb), reg_mmr);
}

static inline void set_mb_mode(const struct at91_priv *priv, unsigned int mb,
			       enum at91_mb_mode mode)
{
	set_mb_mode_prio(priv, mb, mode, 0);
}

static inline u32 at91_can_id_to_reg_mid(canid_t can_id)
{
	u32 reg_mid;

	if (can_id & CAN_EFF_FLAG)
		reg_mid = FIELD_PREP(AT91_MID_MIDVA_MASK | AT91_MID_MIDVB_MASK, can_id) |
			AT91_MID_MIDE;
	else
		reg_mid = FIELD_PREP(AT91_MID_MIDVA_MASK, can_id);

	return reg_mid;
}

static void at91_setup_mailboxes(struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);
	unsigned int i;
	u32 reg_mid;

	/* Due to a chip bug (errata 50.2.6.3 & 50.3.5.3) the first
	 * mailbox is disabled. The next mailboxes are used as a
	 * reception FIFO. The last of the RX mailboxes is configured with
	 * overwrite option. The overwrite flag indicates a FIFO
	 * overflow.
	 */
	reg_mid = at91_can_id_to_reg_mid(priv->mb0_id);
	for (i = 0; i < get_mb_rx_first(priv); i++) {
		set_mb_mode(priv, i, AT91_MB_MODE_DISABLED);
		at91_write(priv, AT91_MID(i), reg_mid);
		at91_write(priv, AT91_MCR(i), 0x0);	/* clear dlc */
	}

	for (i = get_mb_rx_first(priv); i < get_mb_rx_last(priv); i++)
		set_mb_mode(priv, i, AT91_MB_MODE_RX);
	set_mb_mode(priv, get_mb_rx_last(priv), AT91_MB_MODE_RX_OVRWR);

	/* reset acceptance mask and id register */
	for (i = get_mb_rx_first(priv); i <= get_mb_rx_last(priv); i++) {
		at91_write(priv, AT91_MAM(i), 0x0);
		at91_write(priv, AT91_MID(i), AT91_MID_MIDE);
	}

	/* The last mailboxes are used for transmitting. */
	for (i = get_mb_tx_first(priv); i <= get_mb_tx_last(priv); i++)
		set_mb_mode_prio(priv, i, AT91_MB_MODE_TX, 0);

	/* Reset tx helper pointers */
	priv->tx_head = priv->tx_tail = 0;
}

static int at91_set_bittiming(struct net_device *dev)
{
	const struct at91_priv *priv = netdev_priv(dev);
	const struct can_bittiming *bt = &priv->can.bittiming;
	u32 reg_br = 0;

	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		reg_br |= AT91_BR_SMP;

	reg_br |= FIELD_PREP(AT91_BR_BRP_MASK, bt->brp - 1) |
		FIELD_PREP(AT91_BR_SJW_MASK, bt->sjw - 1) |
		FIELD_PREP(AT91_BR_PROPAG_MASK, bt->prop_seg - 1) |
		FIELD_PREP(AT91_BR_PHASE1_MASK, bt->phase_seg1 - 1) |
		FIELD_PREP(AT91_BR_PHASE2_MASK, bt->phase_seg2 - 1);

	netdev_dbg(dev, "writing AT91_BR: 0x%08x\n", reg_br);

	at91_write(priv, AT91_BR, reg_br);

	return 0;
}

static int at91_get_berr_counter(const struct net_device *dev,
				 struct can_berr_counter *bec)
{
	const struct at91_priv *priv = netdev_priv(dev);
	u32 reg_ecr = at91_read(priv, AT91_ECR);

	bec->rxerr = FIELD_GET(AT91_ECR_REC_MASK, reg_ecr);
	bec->txerr = FIELD_GET(AT91_ECR_TEC_MASK, reg_ecr);

	return 0;
}

static void at91_chip_start(struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);
	u32 reg_mr, reg_ier;

	/* disable interrupts */
	at91_write(priv, AT91_IDR, AT91_IRQ_ALL);

	/* disable chip */
	reg_mr = at91_read(priv, AT91_MR);
	at91_write(priv, AT91_MR, reg_mr & ~AT91_MR_CANEN);

	at91_set_bittiming(dev);
	at91_setup_mailboxes(dev);

	/* enable chip */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		reg_mr = AT91_MR_CANEN | AT91_MR_ABM;
	else
		reg_mr = AT91_MR_CANEN;
	at91_write(priv, AT91_MR, reg_mr);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	/* Dummy read to clear latched line error interrupts on
	 * sam9x5 and newer SoCs.
	 */
	at91_read(priv, AT91_SR);

	/* Enable interrupts */
	reg_ier = get_irq_mb_rx(priv) | AT91_IRQ_ERR_LINE | AT91_IRQ_ERR_FRAME;
	at91_write(priv, AT91_IER, reg_ier);
}

static void at91_chip_stop(struct net_device *dev, enum can_state state)
{
	struct at91_priv *priv = netdev_priv(dev);
	u32 reg_mr;

	/* Abort any pending TX requests. However this doesn't seem to
	 * work in case of bus-off on sama5d3.
	 */
	at91_write(priv, AT91_ACR, get_irq_mb_tx(priv));

	/* disable interrupts */
	at91_write(priv, AT91_IDR, AT91_IRQ_ALL);

	reg_mr = at91_read(priv, AT91_MR);
	at91_write(priv, AT91_MR, reg_mr & ~AT91_MR_CANEN);

	priv->can.state = state;
}

/* theory of operation:
 *
 * According to the datasheet priority 0 is the highest priority, 15
 * is the lowest. If two mailboxes have the same priority level the
 * message of the mailbox with the lowest number is sent first.
 *
 * We use the first TX mailbox (AT91_MB_TX_FIRST) with prio 0, then
 * the next mailbox with prio 0, and so on, until all mailboxes are
 * used. Then we start from the beginning with mailbox
 * AT91_MB_TX_FIRST, but with prio 1, mailbox AT91_MB_TX_FIRST + 1
 * prio 1. When we reach the last mailbox with prio 15, we have to
 * stop sending, waiting for all messages to be delivered, then start
 * again with mailbox AT91_MB_TX_FIRST prio 0.
 *
 * We use the priv->tx_head as counter for the next transmission
 * mailbox, but without the offset AT91_MB_TX_FIRST. The lower bits
 * encode the mailbox number, the upper 4 bits the mailbox priority:
 *
 * priv->tx_head = (prio << get_next_prio_shift(priv)) |
 *                 (mb - get_mb_tx_first(priv));
 *
 */
static netdev_tx_t at91_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	unsigned int mb, prio;
	u32 reg_mid, reg_mcr;

	if (can_dev_dropped_skb(dev, skb))
		return NETDEV_TX_OK;

	mb = get_tx_head_mb(priv);
	prio = get_tx_head_prio(priv);

	if (unlikely(!(at91_read(priv, AT91_MSR(mb)) & AT91_MSR_MRDY))) {
		netif_stop_queue(dev);

		netdev_err(dev, "BUG! TX buffer full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}
	reg_mid = at91_can_id_to_reg_mid(cf->can_id);

	reg_mcr = FIELD_PREP(AT91_MCR_MDLC_MASK, cf->len) |
		AT91_MCR_MTCR;

	if (cf->can_id & CAN_RTR_FLAG)
		reg_mcr |= AT91_MCR_MRTR;

	/* disable MB while writing ID (see datasheet) */
	set_mb_mode(priv, mb, AT91_MB_MODE_DISABLED);
	at91_write(priv, AT91_MID(mb), reg_mid);
	set_mb_mode_prio(priv, mb, AT91_MB_MODE_TX, prio);

	at91_write(priv, AT91_MDL(mb), *(u32 *)(cf->data + 0));
	at91_write(priv, AT91_MDH(mb), *(u32 *)(cf->data + 4));

	/* This triggers transmission */
	at91_write(priv, AT91_MCR(mb), reg_mcr);

	/* _NOTE_: subtract AT91_MB_TX_FIRST offset from mb! */
	can_put_echo_skb(skb, dev, mb - get_mb_tx_first(priv), 0);

	/* we have to stop the queue and deliver all messages in case
	 * of a prio+mb counter wrap around. This is the case if
	 * tx_head buffer prio and mailbox equals 0.
	 *
	 * also stop the queue if next buffer is still in use
	 * (== not ready)
	 */
	priv->tx_head++;
	if (!(at91_read(priv, AT91_MSR(get_tx_head_mb(priv))) &
	      AT91_MSR_MRDY) ||
	    (priv->tx_head & get_head_mask(priv)) == 0)
		netif_stop_queue(dev);

	/* Enable interrupt for this mailbox */
	at91_write(priv, AT91_IER, 1 << mb);

	return NETDEV_TX_OK;
}

static inline u32 at91_get_timestamp(const struct at91_priv *priv)
{
	return at91_read(priv, AT91_TIM);
}

static inline struct sk_buff *
at91_alloc_can_err_skb(struct net_device *dev,
		       struct can_frame **cf, u32 *timestamp)
{
	const struct at91_priv *priv = netdev_priv(dev);

	*timestamp = at91_get_timestamp(priv);

	return alloc_can_err_skb(dev, cf);
}

/**
 * at91_rx_overflow_err - send error frame due to rx overflow
 * @dev: net device
 */
static void at91_rx_overflow_err(struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	struct sk_buff *skb;
	struct at91_priv *priv = netdev_priv(dev);
	struct can_frame *cf;
	u32 timestamp;
	int err;

	netdev_dbg(dev, "RX buffer overflow\n");
	stats->rx_over_errors++;
	stats->rx_errors++;

	skb = at91_alloc_can_err_skb(dev, &cf, &timestamp);
	if (unlikely(!skb))
		return;

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;
}

/**
 * at91_mailbox_read - read CAN msg from mailbox
 * @offload: rx-offload
 * @mb: mailbox number to read from
 * @timestamp: pointer to 32 bit timestamp
 * @drop: true indicated mailbox to mark as read and drop frame
 *
 * Reads a CAN message from the given mailbox if not empty.
 */
static struct sk_buff *at91_mailbox_read(struct can_rx_offload *offload,
					 unsigned int mb, u32 *timestamp,
					 bool drop)
{
	const struct at91_priv *priv = rx_offload_to_priv(offload);
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 reg_msr, reg_mid;

	reg_msr = at91_read(priv, AT91_MSR(mb));
	if (!(reg_msr & AT91_MSR_MRDY))
		return NULL;

	if (unlikely(drop)) {
		skb = ERR_PTR(-ENOBUFS);
		goto mark_as_read;
	}

	skb = alloc_can_skb(offload->dev, &cf);
	if (unlikely(!skb)) {
		skb = ERR_PTR(-ENOMEM);
		goto mark_as_read;
	}

	reg_mid = at91_read(priv, AT91_MID(mb));
	if (reg_mid & AT91_MID_MIDE)
		cf->can_id = FIELD_GET(AT91_MID_MIDVA_MASK | AT91_MID_MIDVB_MASK, reg_mid) |
			CAN_EFF_FLAG;
	else
		cf->can_id = FIELD_GET(AT91_MID_MIDVA_MASK, reg_mid);

	/* extend timestamp to full 32 bit */
	*timestamp = FIELD_GET(AT91_MSR_MTIMESTAMP_MASK, reg_msr) << 16;

	cf->len = can_cc_dlc2len(FIELD_GET(AT91_MSR_MDLC_MASK, reg_msr));

	if (reg_msr & AT91_MSR_MRTR) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		*(u32 *)(cf->data + 0) = at91_read(priv, AT91_MDL(mb));
		*(u32 *)(cf->data + 4) = at91_read(priv, AT91_MDH(mb));
	}

	/* allow RX of extended frames */
	at91_write(priv, AT91_MID(mb), AT91_MID_MIDE);

	if (unlikely(mb == get_mb_rx_last(priv) && reg_msr & AT91_MSR_MMI))
		at91_rx_overflow_err(offload->dev);

 mark_as_read:
	at91_write(priv, AT91_MCR(mb), AT91_MCR_MTCR);

	return skb;
}

/* theory of operation:
 *
 * priv->tx_tail holds the number of the oldest can_frame put for
 * transmission into the hardware, but not yet ACKed by the CAN tx
 * complete IRQ.
 *
 * We iterate from priv->tx_tail to priv->tx_head and check if the
 * packet has been transmitted, echo it back to the CAN framework. If
 * we discover a not yet transmitted package, stop looking for more.
 *
 */
static void at91_irq_tx(struct net_device *dev, u32 reg_sr)
{
	struct at91_priv *priv = netdev_priv(dev);
	u32 reg_msr;
	unsigned int mb;

	for (/* nix */; (priv->tx_head - priv->tx_tail) > 0; priv->tx_tail++) {
		mb = get_tx_tail_mb(priv);

		/* no event in mailbox? */
		if (!(reg_sr & (1 << mb)))
			break;

		/* Disable irq for this TX mailbox */
		at91_write(priv, AT91_IDR, 1 << mb);

		/* only echo if mailbox signals us a transfer
		 * complete (MSR_MRDY). Otherwise it's a tansfer
		 * abort. "can_bus_off()" takes care about the skbs
		 * parked in the echo queue.
		 */
		reg_msr = at91_read(priv, AT91_MSR(mb));
		if (unlikely(!(reg_msr & AT91_MSR_MRDY &&
			       ~reg_msr & AT91_MSR_MABT)))
			continue;

		/* _NOTE_: subtract AT91_MB_TX_FIRST offset from mb! */
		dev->stats.tx_bytes +=
			can_get_echo_skb(dev, mb - get_mb_tx_first(priv), NULL);
		dev->stats.tx_packets++;
	}

	/* restart queue if we don't have a wrap around but restart if
	 * we get a TX int for the last can frame directly before a
	 * wrap around.
	 */
	if ((priv->tx_head & get_head_mask(priv)) != 0 ||
	    (priv->tx_tail & get_head_mask(priv)) == 0)
		netif_wake_queue(dev);
}

static void at91_irq_err_line(struct net_device *dev, const u32 reg_sr)
{
	struct net_device_stats *stats = &dev->stats;
	enum can_state new_state, rx_state, tx_state;
	struct at91_priv *priv = netdev_priv(dev);
	struct can_berr_counter bec;
	struct sk_buff *skb;
	struct can_frame *cf;
	u32 timestamp;
	int err;

	at91_get_berr_counter(dev, &bec);
	can_state_get_by_berr_counter(dev, &bec, &tx_state, &rx_state);

	/* The chip automatically recovers from bus-off after 128
	 * occurrences of 11 consecutive recessive bits.
	 *
	 * After an auto-recovered bus-off, the error counters no
	 * longer reflect this fact. On the sam9263 the state bits in
	 * the SR register show the current state (based on the
	 * current error counters), while on sam9x5 and newer SoCs
	 * these bits are latched.
	 *
	 * Take any latched bus-off information from the SR register
	 * into account when calculating the CAN new state, to start
	 * the standard CAN bus off handling.
	 */
	if (reg_sr & AT91_IRQ_BOFF)
		rx_state = CAN_STATE_BUS_OFF;

	new_state = max(tx_state, rx_state);

	/* state hasn't changed */
	if (likely(new_state == priv->can.state))
		return;

	/* The skb allocation might fail, but can_change_state()
	 * handles cf == NULL.
	 */
	skb = at91_alloc_can_err_skb(dev, &cf, &timestamp);
	can_change_state(dev, cf, tx_state, rx_state);

	if (new_state == CAN_STATE_BUS_OFF) {
		at91_chip_stop(dev, CAN_STATE_BUS_OFF);
		can_bus_off(dev);
	}

	if (unlikely(!skb))
		return;

	if (new_state != CAN_STATE_BUS_OFF) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
	}

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;
}

static void at91_irq_err_frame(struct net_device *dev, const u32 reg_sr)
{
	struct net_device_stats *stats = &dev->stats;
	struct at91_priv *priv = netdev_priv(dev);
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 timestamp;
	int err;

	priv->can.can_stats.bus_error++;

	skb = at91_alloc_can_err_skb(dev, &cf, &timestamp);
	if (cf)
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

	if (reg_sr & AT91_IRQ_CERR) {
		netdev_dbg(dev, "CRC error\n");

		stats->rx_errors++;
		if (cf)
			cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
	}

	if (reg_sr & AT91_IRQ_SERR) {
		netdev_dbg(dev, "Stuff error\n");

		stats->rx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_STUFF;
	}

	if (reg_sr & AT91_IRQ_AERR) {
		netdev_dbg(dev, "NACK error\n");

		stats->tx_errors++;
		if (cf) {
			cf->can_id |= CAN_ERR_ACK;
			cf->data[2] |= CAN_ERR_PROT_TX;
		}
	}

	if (reg_sr & AT91_IRQ_FERR) {
		netdev_dbg(dev, "Format error\n");

		stats->rx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_FORM;
	}

	if (reg_sr & AT91_IRQ_BERR) {
		netdev_dbg(dev, "Bit error\n");

		stats->tx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_TX | CAN_ERR_PROT_BIT;
	}

	if (!cf)
		return;

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;
}

static u32 at91_get_reg_sr_rx(const struct at91_priv *priv, u32 *reg_sr_p)
{
	const u32 reg_sr = at91_read(priv, AT91_SR);

	*reg_sr_p |= reg_sr;

	return reg_sr & get_irq_mb_rx(priv);
}

static irqreturn_t at91_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct at91_priv *priv = netdev_priv(dev);
	irqreturn_t handled = IRQ_NONE;
	u32 reg_sr = 0, reg_sr_rx;
	int ret;

	/* Receive interrupt
	 * Some bits of AT91_SR are cleared on read, keep them in reg_sr.
	 */
	while ((reg_sr_rx = at91_get_reg_sr_rx(priv, &reg_sr))) {
		ret = can_rx_offload_irq_offload_timestamp(&priv->offload,
							   reg_sr_rx);
		handled = IRQ_HANDLED;

		if (!ret)
			break;
	}

	/* Transmission complete interrupt */
	if (reg_sr & get_irq_mb_tx(priv)) {
		at91_irq_tx(dev, reg_sr);
		handled = IRQ_HANDLED;
	}

	/* Line Error interrupt */
	if (reg_sr & AT91_IRQ_ERR_LINE ||
	    priv->can.state > CAN_STATE_ERROR_ACTIVE) {
		at91_irq_err_line(dev, reg_sr);
		handled = IRQ_HANDLED;
	}

	/* Frame Error Interrupt */
	if (reg_sr & AT91_IRQ_ERR_FRAME) {
		at91_irq_err_frame(dev, reg_sr);
		handled = IRQ_HANDLED;
	}

	if (handled)
		can_rx_offload_irq_finish(&priv->offload);

	return handled;
}

static int at91_open(struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);
	int err;

	err = phy_power_on(priv->transceiver);
	if (err)
		return err;

	/* check or determine and set bittime */
	err = open_candev(dev);
	if (err)
		goto out_phy_power_off;

	err = clk_prepare_enable(priv->clk);
	if (err)
		goto out_close_candev;

	/* register interrupt handler */
	err = request_irq(dev->irq, at91_irq, IRQF_SHARED,
			  dev->name, dev);
	if (err)
		goto out_clock_disable_unprepare;

	/* start chip and queuing */
	at91_chip_start(dev);
	can_rx_offload_enable(&priv->offload);
	netif_start_queue(dev);

	return 0;

 out_clock_disable_unprepare:
	clk_disable_unprepare(priv->clk);
 out_close_candev:
	close_candev(dev);
 out_phy_power_off:
	phy_power_off(priv->transceiver);

	return err;
}

/* stop CAN bus activity
 */
static int at91_close(struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	can_rx_offload_disable(&priv->offload);
	at91_chip_stop(dev, CAN_STATE_STOPPED);

	free_irq(dev->irq, dev);
	clk_disable_unprepare(priv->clk);
	phy_power_off(priv->transceiver);

	close_candev(dev);

	return 0;
}

static int at91_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		at91_chip_start(dev);
		netif_wake_queue(dev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct net_device_ops at91_netdev_ops = {
	.ndo_open	= at91_open,
	.ndo_stop	= at91_close,
	.ndo_start_xmit	= at91_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static const struct ethtool_ops at91_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static ssize_t mb0_id_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct at91_priv *priv = netdev_priv(to_net_dev(dev));

	if (priv->mb0_id & CAN_EFF_FLAG)
		return sysfs_emit(buf, "0x%08x\n", priv->mb0_id);
	else
		return sysfs_emit(buf, "0x%03x\n", priv->mb0_id);
}

static ssize_t mb0_id_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct net_device *ndev = to_net_dev(dev);
	struct at91_priv *priv = netdev_priv(ndev);
	unsigned long can_id;
	ssize_t ret;
	int err;

	rtnl_lock();

	if (ndev->flags & IFF_UP) {
		ret = -EBUSY;
		goto out;
	}

	err = kstrtoul(buf, 0, &can_id);
	if (err) {
		ret = err;
		goto out;
	}

	if (can_id & CAN_EFF_FLAG)
		can_id &= CAN_EFF_MASK | CAN_EFF_FLAG;
	else
		can_id &= CAN_SFF_MASK;

	priv->mb0_id = can_id;
	ret = count;

 out:
	rtnl_unlock();
	return ret;
}

static DEVICE_ATTR_RW(mb0_id);

static struct attribute *at91_sysfs_attrs[] = {
	&dev_attr_mb0_id.attr,
	NULL,
};

static const struct attribute_group at91_sysfs_attr_group = {
	.attrs = at91_sysfs_attrs,
};

#if defined(CONFIG_OF)
static const struct of_device_id at91_can_dt_ids[] = {
	{
		.compatible = "atmel,at91sam9x5-can",
		.data = &at91_at91sam9x5_data,
	}, {
		.compatible = "atmel,at91sam9263-can",
		.data = &at91_at91sam9263_data,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, at91_can_dt_ids);
#endif

static const struct at91_devtype_data *at91_can_get_driver_data(struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_node(at91_can_dt_ids, pdev->dev.of_node);
		if (!match) {
			dev_err(&pdev->dev, "no matching node found in dtb\n");
			return NULL;
		}
		return (const struct at91_devtype_data *)match->data;
	}
	return (const struct at91_devtype_data *)
		platform_get_device_id(pdev)->driver_data;
}

static int at91_can_probe(struct platform_device *pdev)
{
	const struct at91_devtype_data *devtype_data;
	struct phy *transceiver;
	struct net_device *dev;
	struct at91_priv *priv;
	struct resource *res;
	struct clk *clk;
	void __iomem *addr;
	int err, irq;

	devtype_data = at91_can_get_driver_data(pdev);
	if (!devtype_data) {
		dev_err(&pdev->dev, "no driver data\n");
		err = -ENODEV;
		goto exit;
	}

	clk = clk_get(&pdev->dev, "can_clk");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		err = -ENODEV;
		goto exit;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq <= 0) {
		err = -ENODEV;
		goto exit_put;
	}

	if (!request_mem_region(res->start,
				resource_size(res),
				pdev->name)) {
		err = -EBUSY;
		goto exit_put;
	}

	addr = ioremap(res->start, resource_size(res));
	if (!addr) {
		err = -ENOMEM;
		goto exit_release;
	}

	dev = alloc_candev(sizeof(struct at91_priv),
			   1 << devtype_data->tx_shift);
	if (!dev) {
		err = -ENOMEM;
		goto exit_iounmap;
	}

	transceiver = devm_phy_optional_get(&pdev->dev, NULL);
	if (IS_ERR(transceiver)) {
		err = PTR_ERR(transceiver);
		dev_err_probe(&pdev->dev, err, "failed to get phy\n");
		goto exit_iounmap;
	}

	dev->netdev_ops	= &at91_netdev_ops;
	dev->ethtool_ops = &at91_ethtool_ops;
	dev->irq = irq;
	dev->flags |= IFF_ECHO;

	priv = netdev_priv(dev);
	priv->can.clock.freq = clk_get_rate(clk);
	priv->can.bittiming_const = &at91_bittiming_const;
	priv->can.do_set_mode = at91_set_mode;
	priv->can.do_get_berr_counter = at91_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES |
		CAN_CTRLMODE_LISTENONLY;
	priv->reg_base = addr;
	priv->devtype_data = *devtype_data;
	priv->clk = clk;
	priv->pdata = dev_get_platdata(&pdev->dev);
	priv->mb0_id = 0x7ff;
	priv->offload.mailbox_read = at91_mailbox_read;
	priv->offload.mb_first = devtype_data->rx_first;
	priv->offload.mb_last = devtype_data->rx_last;

	can_rx_offload_add_timestamp(dev, &priv->offload);

	if (transceiver)
		priv->can.bitrate_max = transceiver->attrs.max_link_rate;

	if (at91_is_sam9263(priv))
		dev->sysfs_groups[0] = &at91_sysfs_attr_group;

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_candev(dev);
	if (err) {
		dev_err(&pdev->dev, "registering netdev failed\n");
		goto exit_free;
	}

	dev_info(&pdev->dev, "device registered (reg_base=%p, irq=%d)\n",
		 priv->reg_base, dev->irq);

	return 0;

 exit_free:
	free_candev(dev);
 exit_iounmap:
	iounmap(addr);
 exit_release:
	release_mem_region(res->start, resource_size(res));
 exit_put:
	clk_put(clk);
 exit:
	return err;
}

static void at91_can_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct at91_priv *priv = netdev_priv(dev);
	struct resource *res;

	unregister_netdev(dev);

	iounmap(priv->reg_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	clk_put(priv->clk);

	free_candev(dev);
}

static const struct platform_device_id at91_can_id_table[] = {
	{
		.name = "at91sam9x5_can",
		.driver_data = (kernel_ulong_t)&at91_at91sam9x5_data,
	}, {
		.name = "at91_can",
		.driver_data = (kernel_ulong_t)&at91_at91sam9263_data,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, at91_can_id_table);

static struct platform_driver at91_can_driver = {
	.probe = at91_can_probe,
	.remove = at91_can_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(at91_can_dt_ids),
	},
	.id_table = at91_can_id_table,
};

module_platform_driver(at91_can_driver);

MODULE_AUTHOR("Marc Kleine-Budde <mkl@pengutronix.de>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(KBUILD_MODNAME " CAN netdevice driver");
