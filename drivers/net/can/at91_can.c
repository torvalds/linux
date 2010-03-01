/*
 * at91_can.c - CAN network driver for AT91 SoC CAN controller
 *
 * (C) 2007 by Hans J. Koch <hjk@linutronix.de>
 * (C) 2008, 2009 by Marc Kleine-Budde <kernel@pengutronix.de>
 *
 * This software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2 as distributed in the 'COPYING'
 * file from the main directory of the linux kernel source.
 *
 * Send feedback to <socketcan-users@lists.berlios.de>
 *
 *
 * Your platform definition file should specify something like:
 *
 * static struct at91_can_data ek_can_data = {
 *	transceiver_switch = sam9263ek_transceiver_switch,
 * };
 *
 * at91_add_device_can(&ek_can_data);
 *
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

#include <mach/board.h>

#define DRV_NAME		"at91_can"
#define AT91_NAPI_WEIGHT	12

/*
 * RX/TX Mailbox split
 * don't dare to touch
 */
#define AT91_MB_RX_NUM		12
#define AT91_MB_TX_SHIFT	2

#define AT91_MB_RX_FIRST	0
#define AT91_MB_RX_LAST		(AT91_MB_RX_FIRST + AT91_MB_RX_NUM - 1)

#define AT91_MB_RX_MASK(i)	((1 << (i)) - 1)
#define AT91_MB_RX_SPLIT	8
#define AT91_MB_RX_LOW_LAST	(AT91_MB_RX_SPLIT - 1)
#define AT91_MB_RX_LOW_MASK	(AT91_MB_RX_MASK(AT91_MB_RX_SPLIT))

#define AT91_MB_TX_NUM		(1 << AT91_MB_TX_SHIFT)
#define AT91_MB_TX_FIRST	(AT91_MB_RX_LAST + 1)
#define AT91_MB_TX_LAST		(AT91_MB_TX_FIRST + AT91_MB_TX_NUM - 1)

#define AT91_NEXT_PRIO_SHIFT	(AT91_MB_TX_SHIFT)
#define AT91_NEXT_PRIO_MASK	(0xf << AT91_MB_TX_SHIFT)
#define AT91_NEXT_MB_MASK	(AT91_MB_TX_NUM - 1)
#define AT91_NEXT_MASK		((AT91_MB_TX_NUM - 1) | AT91_NEXT_PRIO_MASK)

/* Common registers */
enum at91_reg {
	AT91_MR		= 0x000,
	AT91_IER	= 0x004,
	AT91_IDR	= 0x008,
	AT91_IMR	= 0x00C,
	AT91_SR		= 0x010,
	AT91_BR		= 0x014,
	AT91_TIM	= 0x018,
	AT91_TIMESTP	= 0x01C,
	AT91_ECR	= 0x020,
	AT91_TCR	= 0x024,
	AT91_ACR	= 0x028,
};

/* Mailbox registers (0 <= i <= 15) */
#define AT91_MMR(i)		(enum at91_reg)(0x200 + ((i) * 0x20))
#define AT91_MAM(i)		(enum at91_reg)(0x204 + ((i) * 0x20))
#define AT91_MID(i)		(enum at91_reg)(0x208 + ((i) * 0x20))
#define AT91_MFID(i)		(enum at91_reg)(0x20C + ((i) * 0x20))
#define AT91_MSR(i)		(enum at91_reg)(0x210 + ((i) * 0x20))
#define AT91_MDL(i)		(enum at91_reg)(0x214 + ((i) * 0x20))
#define AT91_MDH(i)		(enum at91_reg)(0x218 + ((i) * 0x20))
#define AT91_MCR(i)		(enum at91_reg)(0x21C + ((i) * 0x20))

/* Register bits */
#define AT91_MR_CANEN		BIT(0)
#define AT91_MR_LPM		BIT(1)
#define AT91_MR_ABM		BIT(2)
#define AT91_MR_OVL		BIT(3)
#define AT91_MR_TEOF		BIT(4)
#define AT91_MR_TTM		BIT(5)
#define AT91_MR_TIMFRZ		BIT(6)
#define AT91_MR_DRPT		BIT(7)

#define AT91_SR_RBSY		BIT(29)

#define AT91_MMR_PRIO_SHIFT	(16)

#define AT91_MID_MIDE		BIT(29)

#define AT91_MSR_MRTR		BIT(20)
#define AT91_MSR_MABT		BIT(22)
#define AT91_MSR_MRDY		BIT(23)
#define AT91_MSR_MMI		BIT(24)

#define AT91_MCR_MRTR		BIT(20)
#define AT91_MCR_MTCR		BIT(23)

/* Mailbox Modes */
enum at91_mb_mode {
	AT91_MB_MODE_DISABLED	= 0,
	AT91_MB_MODE_RX		= 1,
	AT91_MB_MODE_RX_OVRWR	= 2,
	AT91_MB_MODE_TX		= 3,
	AT91_MB_MODE_CONSUMER	= 4,
	AT91_MB_MODE_PRODUCER	= 5,
};

/* Interrupt mask bits */
#define AT91_IRQ_MB_RX		((1 << (AT91_MB_RX_LAST + 1)) \
				 - (1 << AT91_MB_RX_FIRST))
#define AT91_IRQ_MB_TX		((1 << (AT91_MB_TX_LAST + 1)) \
				 - (1 << AT91_MB_TX_FIRST))
#define AT91_IRQ_MB_ALL		(AT91_IRQ_MB_RX | AT91_IRQ_MB_TX)

#define AT91_IRQ_ERRA		(1 << 16)
#define AT91_IRQ_WARN		(1 << 17)
#define AT91_IRQ_ERRP		(1 << 18)
#define AT91_IRQ_BOFF		(1 << 19)
#define AT91_IRQ_SLEEP		(1 << 20)
#define AT91_IRQ_WAKEUP		(1 << 21)
#define AT91_IRQ_TOVF		(1 << 22)
#define AT91_IRQ_TSTP		(1 << 23)
#define AT91_IRQ_CERR		(1 << 24)
#define AT91_IRQ_SERR		(1 << 25)
#define AT91_IRQ_AERR		(1 << 26)
#define AT91_IRQ_FERR		(1 << 27)
#define AT91_IRQ_BERR		(1 << 28)

#define AT91_IRQ_ERR_ALL	(0x1fff0000)
#define AT91_IRQ_ERR_FRAME	(AT91_IRQ_CERR | AT91_IRQ_SERR | \
				 AT91_IRQ_AERR | AT91_IRQ_FERR | AT91_IRQ_BERR)
#define AT91_IRQ_ERR_LINE	(AT91_IRQ_ERRA | AT91_IRQ_WARN | \
				 AT91_IRQ_ERRP | AT91_IRQ_BOFF)

#define AT91_IRQ_ALL		(0x1fffffff)

struct at91_priv {
	struct can_priv		can;	   /* must be the first member! */
	struct net_device	*dev;
	struct napi_struct	napi;

	void __iomem		*reg_base;

	u32			reg_sr;
	unsigned int		tx_next;
	unsigned int		tx_echo;
	unsigned int		rx_next;

	struct clk		*clk;
	struct at91_can_data	*pdata;
};

static struct can_bittiming_const at91_bittiming_const = {
	.tseg1_min	= 4,
	.tseg1_max	= 16,
	.tseg2_min	= 2,
	.tseg2_max	= 8,
	.sjw_max	= 4,
	.brp_min 	= 2,
	.brp_max	= 128,
	.brp_inc	= 1,
};

static inline int get_tx_next_mb(const struct at91_priv *priv)
{
	return (priv->tx_next & AT91_NEXT_MB_MASK) + AT91_MB_TX_FIRST;
}

static inline int get_tx_next_prio(const struct at91_priv *priv)
{
	return (priv->tx_next >> AT91_NEXT_PRIO_SHIFT) & 0xf;
}

static inline int get_tx_echo_mb(const struct at91_priv *priv)
{
	return (priv->tx_echo & AT91_NEXT_MB_MASK) + AT91_MB_TX_FIRST;
}

static inline u32 at91_read(const struct at91_priv *priv, enum at91_reg reg)
{
	return readl(priv->reg_base + reg);
}

static inline void at91_write(const struct at91_priv *priv, enum at91_reg reg,
		u32 value)
{
	writel(value, priv->reg_base + reg);
}

static inline void set_mb_mode_prio(const struct at91_priv *priv,
		unsigned int mb, enum at91_mb_mode mode, int prio)
{
	at91_write(priv, AT91_MMR(mb), (mode << 24) | (prio << 16));
}

static inline void set_mb_mode(const struct at91_priv *priv, unsigned int mb,
		enum at91_mb_mode mode)
{
	set_mb_mode_prio(priv, mb, mode, 0);
}

/*
 * Swtich transceiver on or off
 */
static void at91_transceiver_switch(const struct at91_priv *priv, int on)
{
	if (priv->pdata && priv->pdata->transceiver_switch)
		priv->pdata->transceiver_switch(on);
}

static void at91_setup_mailboxes(struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);
	unsigned int i;

	/*
	 * The first 12 mailboxes are used as a reception FIFO. The
	 * last mailbox is configured with overwrite option. The
	 * overwrite flag indicates a FIFO overflow.
	 */
	for (i = AT91_MB_RX_FIRST; i < AT91_MB_RX_LAST; i++)
		set_mb_mode(priv, i, AT91_MB_MODE_RX);
	set_mb_mode(priv, AT91_MB_RX_LAST, AT91_MB_MODE_RX_OVRWR);

	/* The last 4 mailboxes are used for transmitting. */
	for (i = AT91_MB_TX_FIRST; i <= AT91_MB_TX_LAST; i++)
		set_mb_mode_prio(priv, i, AT91_MB_MODE_TX, 0);

	/* Reset tx and rx helper pointers */
	priv->tx_next = priv->tx_echo = priv->rx_next = 0;
}

static int at91_set_bittiming(struct net_device *dev)
{
	const struct at91_priv *priv = netdev_priv(dev);
	const struct can_bittiming *bt = &priv->can.bittiming;
	u32 reg_br;

	reg_br = ((priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES) << 24) |
		((bt->brp - 1) << 16) |	((bt->sjw - 1) << 12) |
		((bt->prop_seg - 1) << 8) | ((bt->phase_seg1 - 1) << 4) |
		((bt->phase_seg2 - 1) << 0);

	dev_info(dev->dev.parent, "writing AT91_BR: 0x%08x\n", reg_br);

	at91_write(priv, AT91_BR, reg_br);

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

	at91_setup_mailboxes(dev);
	at91_transceiver_switch(priv, 1);

	/* enable chip */
	at91_write(priv, AT91_MR, AT91_MR_CANEN);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	/* Enable interrupts */
	reg_ier = AT91_IRQ_MB_RX | AT91_IRQ_ERRP | AT91_IRQ_ERR_FRAME;
	at91_write(priv, AT91_IDR, AT91_IRQ_ALL);
	at91_write(priv, AT91_IER, reg_ier);
}

static void at91_chip_stop(struct net_device *dev, enum can_state state)
{
	struct at91_priv *priv = netdev_priv(dev);
	u32 reg_mr;

	/* disable interrupts */
	at91_write(priv, AT91_IDR, AT91_IRQ_ALL);

	reg_mr = at91_read(priv, AT91_MR);
	at91_write(priv, AT91_MR, reg_mr & ~AT91_MR_CANEN);

	at91_transceiver_switch(priv, 0);
	priv->can.state = state;
}

/*
 * theory of operation:
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
 * We use the priv->tx_next as counter for the next transmission
 * mailbox, but without the offset AT91_MB_TX_FIRST. The lower bits
 * encode the mailbox number, the upper 4 bits the mailbox priority:
 *
 * priv->tx_next = (prio << AT91_NEXT_PRIO_SHIFT) ||
 *                 (mb - AT91_MB_TX_FIRST);
 *
 */
static netdev_tx_t at91_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf = (struct can_frame *)skb->data;
	unsigned int mb, prio;
	u32 reg_mid, reg_mcr;

	mb = get_tx_next_mb(priv);
	prio = get_tx_next_prio(priv);

	if (unlikely(!(at91_read(priv, AT91_MSR(mb)) & AT91_MSR_MRDY))) {
		netif_stop_queue(dev);

		dev_err(dev->dev.parent,
			"BUG! TX buffer full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}

	if (cf->can_id & CAN_EFF_FLAG)
		reg_mid = (cf->can_id & CAN_EFF_MASK) | AT91_MID_MIDE;
	else
		reg_mid = (cf->can_id & CAN_SFF_MASK) << 18;

	reg_mcr = ((cf->can_id & CAN_RTR_FLAG) ? AT91_MCR_MRTR : 0) |
		(cf->can_dlc << 16) | AT91_MCR_MTCR;

	/* disable MB while writing ID (see datasheet) */
	set_mb_mode(priv, mb, AT91_MB_MODE_DISABLED);
	at91_write(priv, AT91_MID(mb), reg_mid);
	set_mb_mode_prio(priv, mb, AT91_MB_MODE_TX, prio);

	at91_write(priv, AT91_MDL(mb), *(u32 *)(cf->data + 0));
	at91_write(priv, AT91_MDH(mb), *(u32 *)(cf->data + 4));

	/* This triggers transmission */
	at91_write(priv, AT91_MCR(mb), reg_mcr);

	stats->tx_bytes += cf->can_dlc;
	dev->trans_start = jiffies;

	/* _NOTE_: substract AT91_MB_TX_FIRST offset from mb! */
	can_put_echo_skb(skb, dev, mb - AT91_MB_TX_FIRST);

	/*
	 * we have to stop the queue and deliver all messages in case
	 * of a prio+mb counter wrap around. This is the case if
	 * tx_next buffer prio and mailbox equals 0.
	 *
	 * also stop the queue if next buffer is still in use
	 * (== not ready)
	 */
	priv->tx_next++;
	if (!(at91_read(priv, AT91_MSR(get_tx_next_mb(priv))) &
	      AT91_MSR_MRDY) ||
	    (priv->tx_next & AT91_NEXT_MASK) == 0)
		netif_stop_queue(dev);

	/* Enable interrupt for this mailbox */
	at91_write(priv, AT91_IER, 1 << mb);

	return NETDEV_TX_OK;
}

/**
 * at91_activate_rx_low - activate lower rx mailboxes
 * @priv: a91 context
 *
 * Reenables the lower mailboxes for reception of new CAN messages
 */
static inline void at91_activate_rx_low(const struct at91_priv *priv)
{
	u32 mask = AT91_MB_RX_LOW_MASK;
	at91_write(priv, AT91_TCR, mask);
}

/**
 * at91_activate_rx_mb - reactive single rx mailbox
 * @priv: a91 context
 * @mb: mailbox to reactivate
 *
 * Reenables given mailbox for reception of new CAN messages
 */
static inline void at91_activate_rx_mb(const struct at91_priv *priv,
		unsigned int mb)
{
	u32 mask = 1 << mb;
	at91_write(priv, AT91_TCR, mask);
}

/**
 * at91_rx_overflow_err - send error frame due to rx overflow
 * @dev: net device
 */
static void at91_rx_overflow_err(struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	struct sk_buff *skb;
	struct can_frame *cf;

	dev_dbg(dev->dev.parent, "RX buffer overflow\n");
	stats->rx_over_errors++;
	stats->rx_errors++;

	skb = alloc_can_err_skb(dev, &cf);
	if (unlikely(!skb))
		return;

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
	netif_receive_skb(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
}

/**
 * at91_read_mb - read CAN msg from mailbox (lowlevel impl)
 * @dev: net device
 * @mb: mailbox number to read from
 * @cf: can frame where to store message
 *
 * Reads a CAN message from the given mailbox and stores data into
 * given can frame. "mb" and "cf" must be valid.
 */
static void at91_read_mb(struct net_device *dev, unsigned int mb,
		struct can_frame *cf)
{
	const struct at91_priv *priv = netdev_priv(dev);
	u32 reg_msr, reg_mid;

	reg_mid = at91_read(priv, AT91_MID(mb));
	if (reg_mid & AT91_MID_MIDE)
		cf->can_id = ((reg_mid >> 0) & CAN_EFF_MASK) | CAN_EFF_FLAG;
	else
		cf->can_id = (reg_mid >> 18) & CAN_SFF_MASK;

	reg_msr = at91_read(priv, AT91_MSR(mb));
	if (reg_msr & AT91_MSR_MRTR)
		cf->can_id |= CAN_RTR_FLAG;
	cf->can_dlc = get_can_dlc((reg_msr >> 16) & 0xf);

	*(u32 *)(cf->data + 0) = at91_read(priv, AT91_MDL(mb));
	*(u32 *)(cf->data + 4) = at91_read(priv, AT91_MDH(mb));

	if (unlikely(mb == AT91_MB_RX_LAST && reg_msr & AT91_MSR_MMI))
		at91_rx_overflow_err(dev);
}

/**
 * at91_read_msg - read CAN message from mailbox
 * @dev: net device
 * @mb: mail box to read from
 *
 * Reads a CAN message from given mailbox, and put into linux network
 * RX queue, does all housekeeping chores (stats, ...)
 */
static void at91_read_msg(struct net_device *dev, unsigned int mb)
{
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;

	skb = alloc_can_skb(dev, &cf);
	if (unlikely(!skb)) {
		stats->rx_dropped++;
		return;
	}

	at91_read_mb(dev, mb, cf);
	netif_receive_skb(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
}

/**
 * at91_poll_rx - read multiple CAN messages from mailboxes
 * @dev: net device
 * @quota: max number of pkgs we're allowed to receive
 *
 * Theory of Operation:
 *
 * 12 of the 16 mailboxes on the chip are reserved for RX. we split
 * them into 2 groups. The lower group holds 8 and upper 4 mailboxes.
 *
 * Like it or not, but the chip always saves a received CAN message
 * into the first free mailbox it finds (starting with the
 * lowest). This makes it very difficult to read the messages in the
 * right order from the chip. This is how we work around that problem:
 *
 * The first message goes into mb nr. 0 and issues an interrupt. All
 * rx ints are disabled in the interrupt handler and a napi poll is
 * scheduled. We read the mailbox, but do _not_ reenable the mb (to
 * receive another message).
 *
 *    lower mbxs      upper
 *   ______^______    __^__
 *  /             \  /     \
 * +-+-+-+-+-+-+-+-++-+-+-+-+
 * |x|x|x|x|x|x|x|x|| | | | |
 * +-+-+-+-+-+-+-+-++-+-+-+-+
 *  0 0 0 0 0 0  0 0 0 0 1 1  \ mail
 *  0 1 2 3 4 5  6 7 8 9 0 1  / box
 *
 * The variable priv->rx_next points to the next mailbox to read a
 * message from. As long we're in the lower mailboxes we just read the
 * mailbox but not reenable it.
 *
 * With completion of the last of the lower mailboxes, we reenable the
 * whole first group, but continue to look for filled mailboxes in the
 * upper mailboxes. Imagine the second group like overflow mailboxes,
 * which takes CAN messages if the lower goup is full. While in the
 * upper group we reenable the mailbox right after reading it. Giving
 * the chip more room to store messages.
 *
 * After finishing we look again in the lower group if we've still
 * quota.
 *
 */
static int at91_poll_rx(struct net_device *dev, int quota)
{
	struct at91_priv *priv = netdev_priv(dev);
	u32 reg_sr = at91_read(priv, AT91_SR);
	const unsigned long *addr = (unsigned long *)&reg_sr;
	unsigned int mb;
	int received = 0;

	if (priv->rx_next > AT91_MB_RX_LOW_LAST &&
	    reg_sr & AT91_MB_RX_LOW_MASK)
		dev_info(dev->dev.parent,
			 "order of incoming frames cannot be guaranteed\n");

 again:
	for (mb = find_next_bit(addr, AT91_MB_RX_NUM, priv->rx_next);
	     mb < AT91_MB_RX_NUM && quota > 0;
	     reg_sr = at91_read(priv, AT91_SR),
	     mb = find_next_bit(addr, AT91_MB_RX_NUM, ++priv->rx_next)) {
		at91_read_msg(dev, mb);

		/* reactivate mailboxes */
		if (mb == AT91_MB_RX_LOW_LAST)
			/* all lower mailboxed, if just finished it */
			at91_activate_rx_low(priv);
		else if (mb > AT91_MB_RX_LOW_LAST)
			/* only the mailbox we read */
			at91_activate_rx_mb(priv, mb);

		received++;
		quota--;
	}

	/* upper group completed, look again in lower */
	if (priv->rx_next > AT91_MB_RX_LOW_LAST &&
	    quota > 0 && mb >= AT91_MB_RX_NUM) {
		priv->rx_next = 0;
		goto again;
	}

	return received;
}

static void at91_poll_err_frame(struct net_device *dev,
		struct can_frame *cf, u32 reg_sr)
{
	struct at91_priv *priv = netdev_priv(dev);

	/* CRC error */
	if (reg_sr & AT91_IRQ_CERR) {
		dev_dbg(dev->dev.parent, "CERR irq\n");
		dev->stats.rx_errors++;
		priv->can.can_stats.bus_error++;
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
	}

	/* Stuffing Error */
	if (reg_sr & AT91_IRQ_SERR) {
		dev_dbg(dev->dev.parent, "SERR irq\n");
		dev->stats.rx_errors++;
		priv->can.can_stats.bus_error++;
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
		cf->data[2] |= CAN_ERR_PROT_STUFF;
	}

	/* Acknowledgement Error */
	if (reg_sr & AT91_IRQ_AERR) {
		dev_dbg(dev->dev.parent, "AERR irq\n");
		dev->stats.tx_errors++;
		cf->can_id |= CAN_ERR_ACK;
	}

	/* Form error */
	if (reg_sr & AT91_IRQ_FERR) {
		dev_dbg(dev->dev.parent, "FERR irq\n");
		dev->stats.rx_errors++;
		priv->can.can_stats.bus_error++;
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
		cf->data[2] |= CAN_ERR_PROT_FORM;
	}

	/* Bit Error */
	if (reg_sr & AT91_IRQ_BERR) {
		dev_dbg(dev->dev.parent, "BERR irq\n");
		dev->stats.tx_errors++;
		priv->can.can_stats.bus_error++;
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
		cf->data[2] |= CAN_ERR_PROT_BIT;
	}
}

static int at91_poll_err(struct net_device *dev, int quota, u32 reg_sr)
{
	struct sk_buff *skb;
	struct can_frame *cf;

	if (quota == 0)
		return 0;

	skb = alloc_can_err_skb(dev, &cf);
	if (unlikely(!skb))
		return 0;

	at91_poll_err_frame(dev, cf, reg_sr);
	netif_receive_skb(skb);

	dev->last_rx = jiffies;
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += cf->can_dlc;

	return 1;
}

static int at91_poll(struct napi_struct *napi, int quota)
{
	struct net_device *dev = napi->dev;
	const struct at91_priv *priv = netdev_priv(dev);
	u32 reg_sr = at91_read(priv, AT91_SR);
	int work_done = 0;

	if (reg_sr & AT91_IRQ_MB_RX)
		work_done += at91_poll_rx(dev, quota - work_done);

	/*
	 * The error bits are clear on read,
	 * so use saved value from irq handler.
	 */
	reg_sr |= priv->reg_sr;
	if (reg_sr & AT91_IRQ_ERR_FRAME)
		work_done += at91_poll_err(dev, quota - work_done, reg_sr);

	if (work_done < quota) {
		/* enable IRQs for frame errors and all mailboxes >= rx_next */
		u32 reg_ier = AT91_IRQ_ERR_FRAME;
		reg_ier |= AT91_IRQ_MB_RX & ~AT91_MB_RX_MASK(priv->rx_next);

		napi_complete(napi);
		at91_write(priv, AT91_IER, reg_ier);
	}

	return work_done;
}

/*
 * theory of operation:
 *
 * priv->tx_echo holds the number of the oldest can_frame put for
 * transmission into the hardware, but not yet ACKed by the CAN tx
 * complete IRQ.
 *
 * We iterate from priv->tx_echo to priv->tx_next and check if the
 * packet has been transmitted, echo it back to the CAN framework. If
 * we discover a not yet transmitted package, stop looking for more.
 *
 */
static void at91_irq_tx(struct net_device *dev, u32 reg_sr)
{
	struct at91_priv *priv = netdev_priv(dev);
	u32 reg_msr;
	unsigned int mb;

	/* masking of reg_sr not needed, already done by at91_irq */

	for (/* nix */; (priv->tx_next - priv->tx_echo) > 0; priv->tx_echo++) {
		mb = get_tx_echo_mb(priv);

		/* no event in mailbox? */
		if (!(reg_sr & (1 << mb)))
			break;

		/* Disable irq for this TX mailbox */
		at91_write(priv, AT91_IDR, 1 << mb);

		/*
		 * only echo if mailbox signals us a transfer
		 * complete (MSR_MRDY). Otherwise it's a tansfer
		 * abort. "can_bus_off()" takes care about the skbs
		 * parked in the echo queue.
		 */
		reg_msr = at91_read(priv, AT91_MSR(mb));
		if (likely(reg_msr & AT91_MSR_MRDY &&
			   ~reg_msr & AT91_MSR_MABT)) {
			/* _NOTE_: substract AT91_MB_TX_FIRST offset from mb! */
			can_get_echo_skb(dev, mb - AT91_MB_TX_FIRST);
			dev->stats.tx_packets++;
		}
	}

	/*
	 * restart queue if we don't have a wrap around but restart if
	 * we get a TX int for the last can frame directly before a
	 * wrap around.
	 */
	if ((priv->tx_next & AT91_NEXT_MASK) != 0 ||
	    (priv->tx_echo & AT91_NEXT_MASK) == 0)
		netif_wake_queue(dev);
}

static void at91_irq_err_state(struct net_device *dev,
		struct can_frame *cf, enum can_state new_state)
{
	struct at91_priv *priv = netdev_priv(dev);
	u32 reg_idr, reg_ier, reg_ecr;
	u8 tec, rec;

	reg_ecr = at91_read(priv, AT91_ECR);
	rec = reg_ecr & 0xff;
	tec = reg_ecr >> 16;

	switch (priv->can.state) {
	case CAN_STATE_ERROR_ACTIVE:
		/*
		 * from: ERROR_ACTIVE
		 * to  : ERROR_WARNING, ERROR_PASSIVE, BUS_OFF
		 * =>  : there was a warning int
		 */
		if (new_state >= CAN_STATE_ERROR_WARNING &&
		    new_state <= CAN_STATE_BUS_OFF) {
			dev_dbg(dev->dev.parent, "Error Warning IRQ\n");
			priv->can.can_stats.error_warning++;

			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = (tec > rec) ?
				CAN_ERR_CRTL_TX_WARNING :
				CAN_ERR_CRTL_RX_WARNING;
		}
	case CAN_STATE_ERROR_WARNING:	/* fallthrough */
		/*
		 * from: ERROR_ACTIVE, ERROR_WARNING
		 * to  : ERROR_PASSIVE, BUS_OFF
		 * =>  : error passive int
		 */
		if (new_state >= CAN_STATE_ERROR_PASSIVE &&
		    new_state <= CAN_STATE_BUS_OFF) {
			dev_dbg(dev->dev.parent, "Error Passive IRQ\n");
			priv->can.can_stats.error_passive++;

			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = (tec > rec) ?
				CAN_ERR_CRTL_TX_PASSIVE :
				CAN_ERR_CRTL_RX_PASSIVE;
		}
		break;
	case CAN_STATE_BUS_OFF:
		/*
		 * from: BUS_OFF
		 * to  : ERROR_ACTIVE, ERROR_WARNING, ERROR_PASSIVE
		 */
		if (new_state <= CAN_STATE_ERROR_PASSIVE) {
			cf->can_id |= CAN_ERR_RESTARTED;

			dev_dbg(dev->dev.parent, "restarted\n");
			priv->can.can_stats.restarts++;

			netif_carrier_on(dev);
			netif_wake_queue(dev);
		}
		break;
	default:
		break;
	}


	/* process state changes depending on the new state */
	switch (new_state) {
	case CAN_STATE_ERROR_ACTIVE:
		/*
		 * actually we want to enable AT91_IRQ_WARN here, but
		 * it screws up the system under certain
		 * circumstances. so just enable AT91_IRQ_ERRP, thus
		 * the "fallthrough"
		 */
		dev_dbg(dev->dev.parent, "Error Active\n");
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] = CAN_ERR_PROT_ACTIVE;
	case CAN_STATE_ERROR_WARNING:	/* fallthrough */
		reg_idr = AT91_IRQ_ERRA | AT91_IRQ_WARN | AT91_IRQ_BOFF;
		reg_ier = AT91_IRQ_ERRP;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		reg_idr = AT91_IRQ_ERRA | AT91_IRQ_WARN | AT91_IRQ_ERRP;
		reg_ier = AT91_IRQ_BOFF;
		break;
	case CAN_STATE_BUS_OFF:
		reg_idr = AT91_IRQ_ERRA | AT91_IRQ_ERRP |
			AT91_IRQ_WARN | AT91_IRQ_BOFF;
		reg_ier = 0;

		cf->can_id |= CAN_ERR_BUSOFF;

		dev_dbg(dev->dev.parent, "bus-off\n");
		netif_carrier_off(dev);
		priv->can.can_stats.bus_off++;

		/* turn off chip, if restart is disabled */
		if (!priv->can.restart_ms) {
			at91_chip_stop(dev, CAN_STATE_BUS_OFF);
			return;
		}
		break;
	default:
		break;
	}

	at91_write(priv, AT91_IDR, reg_idr);
	at91_write(priv, AT91_IER, reg_ier);
}

static void at91_irq_err(struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	struct can_frame *cf;
	enum can_state new_state;
	u32 reg_sr;

	reg_sr = at91_read(priv, AT91_SR);

	/* we need to look at the unmasked reg_sr */
	if (unlikely(reg_sr & AT91_IRQ_BOFF))
		new_state = CAN_STATE_BUS_OFF;
	else if (unlikely(reg_sr & AT91_IRQ_ERRP))
		new_state = CAN_STATE_ERROR_PASSIVE;
	else if (unlikely(reg_sr & AT91_IRQ_WARN))
		new_state = CAN_STATE_ERROR_WARNING;
	else if (likely(reg_sr & AT91_IRQ_ERRA))
		new_state = CAN_STATE_ERROR_ACTIVE;
	else {
		dev_err(dev->dev.parent, "BUG! hardware in undefined state\n");
		return;
	}

	/* state hasn't changed */
	if (likely(new_state == priv->can.state))
		return;

	skb = alloc_can_err_skb(dev, &cf);
	if (unlikely(!skb))
		return;

	at91_irq_err_state(dev, cf, new_state);
	netif_rx(skb);

	dev->last_rx = jiffies;
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += cf->can_dlc;

	priv->can.state = new_state;
}

/*
 * interrupt handler
 */
static irqreturn_t at91_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct at91_priv *priv = netdev_priv(dev);
	irqreturn_t handled = IRQ_NONE;
	u32 reg_sr, reg_imr;

	reg_sr = at91_read(priv, AT91_SR);
	reg_imr = at91_read(priv, AT91_IMR);

	/* Ignore masked interrupts */
	reg_sr &= reg_imr;
	if (!reg_sr)
		goto exit;

	handled = IRQ_HANDLED;

	/* Receive or error interrupt? -> napi */
	if (reg_sr & (AT91_IRQ_MB_RX | AT91_IRQ_ERR_FRAME)) {
		/*
		 * The error bits are clear on read,
		 * save for later use.
		 */
		priv->reg_sr = reg_sr;
		at91_write(priv, AT91_IDR,
			   AT91_IRQ_MB_RX | AT91_IRQ_ERR_FRAME);
		napi_schedule(&priv->napi);
	}

	/* Transmission complete interrupt */
	if (reg_sr & AT91_IRQ_MB_TX)
		at91_irq_tx(dev, reg_sr);

	at91_irq_err(dev);

 exit:
	return handled;
}

static int at91_open(struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);
	int err;

	clk_enable(priv->clk);

	/* check or determine and set bittime */
	err = open_candev(dev);
	if (err)
		goto out;

	/* register interrupt handler */
	if (request_irq(dev->irq, at91_irq, IRQF_SHARED,
			dev->name, dev)) {
		err = -EAGAIN;
		goto out_close;
	}

	/* start chip and queuing */
	at91_chip_start(dev);
	napi_enable(&priv->napi);
	netif_start_queue(dev);

	return 0;

 out_close:
	close_candev(dev);
 out:
	clk_disable(priv->clk);

	return err;
}

/*
 * stop CAN bus activity
 */
static int at91_close(struct net_device *dev)
{
	struct at91_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	napi_disable(&priv->napi);
	at91_chip_stop(dev, CAN_STATE_STOPPED);

	free_irq(dev->irq, dev);
	clk_disable(priv->clk);

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
};

static int __init at91_can_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct at91_priv *priv;
	struct resource *res;
	struct clk *clk;
	void __iomem *addr;
	int err, irq;

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

	addr = ioremap_nocache(res->start, resource_size(res));
	if (!addr) {
		err = -ENOMEM;
		goto exit_release;
	}

	dev = alloc_candev(sizeof(struct at91_priv), AT91_MB_TX_NUM);
	if (!dev) {
		err = -ENOMEM;
		goto exit_iounmap;
	}

	dev->netdev_ops	= &at91_netdev_ops;
	dev->irq = irq;
	dev->flags |= IFF_ECHO;

	priv = netdev_priv(dev);
	priv->can.clock.freq = clk_get_rate(clk);
	priv->can.bittiming_const = &at91_bittiming_const;
	priv->can.do_set_bittiming = at91_set_bittiming;
	priv->can.do_set_mode = at91_set_mode;
	priv->reg_base = addr;
	priv->dev = dev;
	priv->clk = clk;
	priv->pdata = pdev->dev.platform_data;

	netif_napi_add(dev, &priv->napi, at91_poll, AT91_NAPI_WEIGHT);

	dev_set_drvdata(&pdev->dev, dev);
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
	free_netdev(dev);
 exit_iounmap:
	iounmap(addr);
 exit_release:
	release_mem_region(res->start, resource_size(res));
 exit_put:
	clk_put(clk);
 exit:
	return err;
}

static int __devexit at91_can_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct at91_priv *priv = netdev_priv(dev);
	struct resource *res;

	unregister_netdev(dev);

	platform_set_drvdata(pdev, NULL);

	free_netdev(dev);

	iounmap(priv->reg_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	clk_put(priv->clk);

	return 0;
}

static struct platform_driver at91_can_driver = {
	.probe		= at91_can_probe,
	.remove		= __devexit_p(at91_can_remove),
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init at91_can_module_init(void)
{
	printk(KERN_INFO "%s netdevice driver\n", DRV_NAME);
	return platform_driver_register(&at91_can_driver);
}

static void __exit at91_can_module_exit(void)
{
	platform_driver_unregister(&at91_can_driver);
	printk(KERN_INFO "%s: driver removed\n", DRV_NAME);
}

module_init(at91_can_module_init);
module_exit(at91_can_module_exit);

MODULE_AUTHOR("Marc Kleine-Budde <mkl@pengutronix.de>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRV_NAME " CAN netdevice driver");
