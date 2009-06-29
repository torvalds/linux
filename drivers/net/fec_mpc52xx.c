/*
 * Driver for the MPC5200 Fast Ethernet Controller
 *
 * Originally written by Dale Farnsworth <dfarnsworth@mvista.com> and
 * now maintained by Sylvain Munaut <tnt@246tNt.com>
 *
 * Copyright (C) 2007  Domen Puncer, Telargo, Inc.
 * Copyright (C) 2007  Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003-2004  MontaVista, Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <linux/hardirq.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <asm/mpc52xx.h>

#include <sysdev/bestcomm/bestcomm.h>
#include <sysdev/bestcomm/fec.h>

#include "fec_mpc52xx.h"

#define DRIVER_NAME "mpc52xx-fec"

/* Private driver data structure */
struct mpc52xx_fec_priv {
	struct net_device *ndev;
	int duplex;
	int speed;
	int r_irq;
	int t_irq;
	struct mpc52xx_fec __iomem *fec;
	struct bcom_task *rx_dmatsk;
	struct bcom_task *tx_dmatsk;
	spinlock_t lock;
	int msg_enable;

	/* MDIO link details */
	unsigned int mdio_speed;
	struct device_node *phy_node;
	struct phy_device *phydev;
	enum phy_state link;
	int seven_wire_mode;
};


static irqreturn_t mpc52xx_fec_interrupt(int, void *);
static irqreturn_t mpc52xx_fec_rx_interrupt(int, void *);
static irqreturn_t mpc52xx_fec_tx_interrupt(int, void *);
static void mpc52xx_fec_stop(struct net_device *dev);
static void mpc52xx_fec_start(struct net_device *dev);
static void mpc52xx_fec_reset(struct net_device *dev);

static u8 mpc52xx_fec_mac_addr[6];
module_param_array_named(mac, mpc52xx_fec_mac_addr, byte, NULL, 0);
MODULE_PARM_DESC(mac, "six hex digits, ie. 0x1,0x2,0xc0,0x01,0xba,0xbe");

#define MPC52xx_MESSAGES_DEFAULT ( NETIF_MSG_DRV | NETIF_MSG_PROBE | \
		NETIF_MSG_LINK | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP)
static int debug = -1;	/* the above default */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "debugging messages level");

static void mpc52xx_fec_tx_timeout(struct net_device *dev)
{
	dev_warn(&dev->dev, "transmit timed out\n");

	mpc52xx_fec_reset(dev);

	dev->stats.tx_errors++;

	netif_wake_queue(dev);
}

static void mpc52xx_fec_set_paddr(struct net_device *dev, u8 *mac)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;

	out_be32(&fec->paddr1, *(u32 *)(&mac[0]));
	out_be32(&fec->paddr2, (*(u16 *)(&mac[4]) << 16) | FEC_PADDR2_TYPE);
}

static void mpc52xx_fec_get_paddr(struct net_device *dev, u8 *mac)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;

	*(u32 *)(&mac[0]) = in_be32(&fec->paddr1);
	*(u16 *)(&mac[4]) = in_be32(&fec->paddr2) >> 16;
}

static int mpc52xx_fec_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sock = addr;

	memcpy(dev->dev_addr, sock->sa_data, dev->addr_len);

	mpc52xx_fec_set_paddr(dev, sock->sa_data);
	return 0;
}

static void mpc52xx_fec_free_rx_buffers(struct net_device *dev, struct bcom_task *s)
{
	while (!bcom_queue_empty(s)) {
		struct bcom_fec_bd *bd;
		struct sk_buff *skb;

		skb = bcom_retrieve_buffer(s, NULL, (struct bcom_bd **)&bd);
		dma_unmap_single(dev->dev.parent, bd->skb_pa, skb->len,
				 DMA_FROM_DEVICE);
		kfree_skb(skb);
	}
}

static int mpc52xx_fec_alloc_rx_buffers(struct net_device *dev, struct bcom_task *rxtsk)
{
	while (!bcom_queue_full(rxtsk)) {
		struct sk_buff *skb;
		struct bcom_fec_bd *bd;

		skb = dev_alloc_skb(FEC_RX_BUFFER_SIZE);
		if (skb == NULL)
			return -EAGAIN;

		/* zero out the initial receive buffers to aid debugging */
		memset(skb->data, 0, FEC_RX_BUFFER_SIZE);

		bd = (struct bcom_fec_bd *)bcom_prepare_next_buffer(rxtsk);

		bd->status = FEC_RX_BUFFER_SIZE;
		bd->skb_pa = dma_map_single(dev->dev.parent, skb->data,
				FEC_RX_BUFFER_SIZE, DMA_FROM_DEVICE);

		bcom_submit_next_buffer(rxtsk, skb);
	}

	return 0;
}

/* based on generic_adjust_link from fs_enet-main.c */
static void mpc52xx_fec_adjust_link(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;
	int new_state = 0;

	if (phydev->link != PHY_DOWN) {
		if (phydev->duplex != priv->duplex) {
			struct mpc52xx_fec __iomem *fec = priv->fec;
			u32 rcntrl;
			u32 tcntrl;

			new_state = 1;
			priv->duplex = phydev->duplex;

			rcntrl = in_be32(&fec->r_cntrl);
			tcntrl = in_be32(&fec->x_cntrl);

			rcntrl &= ~FEC_RCNTRL_DRT;
			tcntrl &= ~FEC_TCNTRL_FDEN;
			if (phydev->duplex == DUPLEX_FULL)
				tcntrl |= FEC_TCNTRL_FDEN;	/* FD enable */
			else
				rcntrl |= FEC_RCNTRL_DRT;	/* disable Rx on Tx (HD) */

			out_be32(&fec->r_cntrl, rcntrl);
			out_be32(&fec->x_cntrl, tcntrl);
		}

		if (phydev->speed != priv->speed) {
			new_state = 1;
			priv->speed = phydev->speed;
		}

		if (priv->link == PHY_DOWN) {
			new_state = 1;
			priv->link = phydev->link;
		}

	} else if (priv->link) {
		new_state = 1;
		priv->link = PHY_DOWN;
		priv->speed = 0;
		priv->duplex = -1;
	}

	if (new_state && netif_msg_link(priv))
		phy_print_status(phydev);
}

static int mpc52xx_fec_open(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	int err = -EBUSY;

	if (priv->phy_node) {
		priv->phydev = of_phy_connect(priv->ndev, priv->phy_node,
					      mpc52xx_fec_adjust_link, 0, 0);
		if (!priv->phydev) {
			dev_err(&dev->dev, "of_phy_connect failed\n");
			return -ENODEV;
		}
		phy_start(priv->phydev);
	}

	if (request_irq(dev->irq, &mpc52xx_fec_interrupt, IRQF_SHARED,
	                DRIVER_NAME "_ctrl", dev)) {
		dev_err(&dev->dev, "ctrl interrupt request failed\n");
		goto free_phy;
	}
	if (request_irq(priv->r_irq, &mpc52xx_fec_rx_interrupt, 0,
	                DRIVER_NAME "_rx", dev)) {
		dev_err(&dev->dev, "rx interrupt request failed\n");
		goto free_ctrl_irq;
	}
	if (request_irq(priv->t_irq, &mpc52xx_fec_tx_interrupt, 0,
	                DRIVER_NAME "_tx", dev)) {
		dev_err(&dev->dev, "tx interrupt request failed\n");
		goto free_2irqs;
	}

	bcom_fec_rx_reset(priv->rx_dmatsk);
	bcom_fec_tx_reset(priv->tx_dmatsk);

	err = mpc52xx_fec_alloc_rx_buffers(dev, priv->rx_dmatsk);
	if (err) {
		dev_err(&dev->dev, "mpc52xx_fec_alloc_rx_buffers failed\n");
		goto free_irqs;
	}

	bcom_enable(priv->rx_dmatsk);
	bcom_enable(priv->tx_dmatsk);

	mpc52xx_fec_start(dev);

	netif_start_queue(dev);

	return 0;

 free_irqs:
	free_irq(priv->t_irq, dev);
 free_2irqs:
	free_irq(priv->r_irq, dev);
 free_ctrl_irq:
	free_irq(dev->irq, dev);
 free_phy:
	if (priv->phydev) {
		phy_stop(priv->phydev);
		phy_disconnect(priv->phydev);
		priv->phydev = NULL;
	}

	return err;
}

static int mpc52xx_fec_close(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);

	mpc52xx_fec_stop(dev);

	mpc52xx_fec_free_rx_buffers(dev, priv->rx_dmatsk);

	free_irq(dev->irq, dev);
	free_irq(priv->r_irq, dev);
	free_irq(priv->t_irq, dev);

	if (priv->phydev) {
		/* power down phy */
		phy_stop(priv->phydev);
		phy_disconnect(priv->phydev);
		priv->phydev = NULL;
	}

	return 0;
}

/* This will only be invoked if your driver is _not_ in XOFF state.
 * What this means is that you need not check it, and that this
 * invariant will hold if you make sure that the netif_*_queue()
 * calls are done at the proper times.
 */
static int mpc52xx_fec_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct bcom_fec_bd *bd;

	if (bcom_queue_full(priv->tx_dmatsk)) {
		if (net_ratelimit())
			dev_err(&dev->dev, "transmit queue overrun\n");
		return NETDEV_TX_BUSY;
	}

	spin_lock_irq(&priv->lock);
	dev->trans_start = jiffies;

	bd = (struct bcom_fec_bd *)
		bcom_prepare_next_buffer(priv->tx_dmatsk);

	bd->status = skb->len | BCOM_FEC_TX_BD_TFD | BCOM_FEC_TX_BD_TC;
	bd->skb_pa = dma_map_single(dev->dev.parent, skb->data, skb->len,
				    DMA_TO_DEVICE);

	bcom_submit_next_buffer(priv->tx_dmatsk, skb);

	if (bcom_queue_full(priv->tx_dmatsk)) {
		netif_stop_queue(dev);
	}

	spin_unlock_irq(&priv->lock);

	return NETDEV_TX_OK;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mpc52xx_fec_poll_controller(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);

	disable_irq(priv->t_irq);
	mpc52xx_fec_tx_interrupt(priv->t_irq, dev);
	enable_irq(priv->t_irq);
	disable_irq(priv->r_irq);
	mpc52xx_fec_rx_interrupt(priv->r_irq, dev);
	enable_irq(priv->r_irq);
}
#endif


/* This handles BestComm transmit task interrupts
 */
static irqreturn_t mpc52xx_fec_tx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);

	spin_lock(&priv->lock);

	while (bcom_buffer_done(priv->tx_dmatsk)) {
		struct sk_buff *skb;
		struct bcom_fec_bd *bd;
		skb = bcom_retrieve_buffer(priv->tx_dmatsk, NULL,
				(struct bcom_bd **)&bd);
		dma_unmap_single(dev->dev.parent, bd->skb_pa, skb->len,
				 DMA_TO_DEVICE);

		dev_kfree_skb_irq(skb);
	}

	netif_wake_queue(dev);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static irqreturn_t mpc52xx_fec_rx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);

	while (bcom_buffer_done(priv->rx_dmatsk)) {
		struct sk_buff *skb;
		struct sk_buff *rskb;
		struct bcom_fec_bd *bd;
		u32 status;

		rskb = bcom_retrieve_buffer(priv->rx_dmatsk, &status,
				(struct bcom_bd **)&bd);
		dma_unmap_single(dev->dev.parent, bd->skb_pa, rskb->len,
				 DMA_FROM_DEVICE);

		/* Test for errors in received frame */
		if (status & BCOM_FEC_RX_BD_ERRORS) {
			/* Drop packet and reuse the buffer */
			bd = (struct bcom_fec_bd *)
				bcom_prepare_next_buffer(priv->rx_dmatsk);

			bd->status = FEC_RX_BUFFER_SIZE;
			bd->skb_pa = dma_map_single(dev->dev.parent,
					rskb->data,
					FEC_RX_BUFFER_SIZE, DMA_FROM_DEVICE);

			bcom_submit_next_buffer(priv->rx_dmatsk, rskb);

			dev->stats.rx_dropped++;

			continue;
		}

		/* skbs are allocated on open, so now we allocate a new one,
		 * and remove the old (with the packet) */
		skb = dev_alloc_skb(FEC_RX_BUFFER_SIZE);
		if (skb) {
			/* Process the received skb */
			int length = status & BCOM_FEC_RX_BD_LEN_MASK;

			skb_put(rskb, length - 4);	/* length without CRC32 */

			rskb->dev = dev;
			rskb->protocol = eth_type_trans(rskb, dev);

			netif_rx(rskb);
		} else {
			/* Can't get a new one : reuse the same & drop pkt */
			dev_notice(&dev->dev, "Memory squeeze, dropping packet.\n");
			dev->stats.rx_dropped++;

			skb = rskb;
		}

		bd = (struct bcom_fec_bd *)
			bcom_prepare_next_buffer(priv->rx_dmatsk);

		bd->status = FEC_RX_BUFFER_SIZE;
		bd->skb_pa = dma_map_single(dev->dev.parent, skb->data,
				FEC_RX_BUFFER_SIZE, DMA_FROM_DEVICE);

		bcom_submit_next_buffer(priv->rx_dmatsk, skb);
	}

	return IRQ_HANDLED;
}

static irqreturn_t mpc52xx_fec_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;
	u32 ievent;

	ievent = in_be32(&fec->ievent);

	ievent &= ~FEC_IEVENT_MII;	/* mii is handled separately */
	if (!ievent)
		return IRQ_NONE;

	out_be32(&fec->ievent, ievent);		/* clear pending events */

	/* on fifo error, soft-reset fec */
	if (ievent & (FEC_IEVENT_RFIFO_ERROR | FEC_IEVENT_XFIFO_ERROR)) {

		if (net_ratelimit() && (ievent & FEC_IEVENT_RFIFO_ERROR))
			dev_warn(&dev->dev, "FEC_IEVENT_RFIFO_ERROR\n");
		if (net_ratelimit() && (ievent & FEC_IEVENT_XFIFO_ERROR))
			dev_warn(&dev->dev, "FEC_IEVENT_XFIFO_ERROR\n");

		mpc52xx_fec_reset(dev);

		netif_wake_queue(dev);
		return IRQ_HANDLED;
	}

	if (ievent & ~FEC_IEVENT_TFINT)
		dev_dbg(&dev->dev, "ievent: %08x\n", ievent);

	return IRQ_HANDLED;
}

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */
static struct net_device_stats *mpc52xx_fec_get_stats(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct mpc52xx_fec __iomem *fec = priv->fec;

	stats->rx_bytes = in_be32(&fec->rmon_r_octets);
	stats->rx_packets = in_be32(&fec->rmon_r_packets);
	stats->rx_errors = in_be32(&fec->rmon_r_crc_align) +
		in_be32(&fec->rmon_r_undersize) +
		in_be32(&fec->rmon_r_oversize) +
		in_be32(&fec->rmon_r_frag) +
		in_be32(&fec->rmon_r_jab);

	stats->tx_bytes = in_be32(&fec->rmon_t_octets);
	stats->tx_packets = in_be32(&fec->rmon_t_packets);
	stats->tx_errors = in_be32(&fec->rmon_t_crc_align) +
		in_be32(&fec->rmon_t_undersize) +
		in_be32(&fec->rmon_t_oversize) +
		in_be32(&fec->rmon_t_frag) +
		in_be32(&fec->rmon_t_jab);

	stats->multicast = in_be32(&fec->rmon_r_mc_pkt);
	stats->collisions = in_be32(&fec->rmon_t_col);

	/* detailed rx_errors: */
	stats->rx_length_errors = in_be32(&fec->rmon_r_undersize)
					+ in_be32(&fec->rmon_r_oversize)
					+ in_be32(&fec->rmon_r_frag)
					+ in_be32(&fec->rmon_r_jab);
	stats->rx_over_errors = in_be32(&fec->r_macerr);
	stats->rx_crc_errors = in_be32(&fec->ieee_r_crc);
	stats->rx_frame_errors = in_be32(&fec->ieee_r_align);
	stats->rx_fifo_errors = in_be32(&fec->rmon_r_drop);
	stats->rx_missed_errors = in_be32(&fec->rmon_r_drop);

	/* detailed tx_errors: */
	stats->tx_aborted_errors = 0;
	stats->tx_carrier_errors = in_be32(&fec->ieee_t_cserr);
	stats->tx_fifo_errors = in_be32(&fec->rmon_t_drop);
	stats->tx_heartbeat_errors = in_be32(&fec->ieee_t_sqe);
	stats->tx_window_errors = in_be32(&fec->ieee_t_lcol);

	return stats;
}

/*
 * Read MIB counters in order to reset them,
 * then zero all the stats fields in memory
 */
static void mpc52xx_fec_reset_stats(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;

	out_be32(&fec->mib_control, FEC_MIB_DISABLE);
	memset_io(&fec->rmon_t_drop, 0,
		   offsetof(struct mpc52xx_fec, reserved10) -
		   offsetof(struct mpc52xx_fec, rmon_t_drop));
	out_be32(&fec->mib_control, 0);

	memset(&dev->stats, 0, sizeof(dev->stats));
}

/*
 * Set or clear the multicast filter for this adaptor.
 */
static void mpc52xx_fec_set_multicast_list(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;
	u32 rx_control;

	rx_control = in_be32(&fec->r_cntrl);

	if (dev->flags & IFF_PROMISC) {
		rx_control |= FEC_RCNTRL_PROM;
		out_be32(&fec->r_cntrl, rx_control);
	} else {
		rx_control &= ~FEC_RCNTRL_PROM;
		out_be32(&fec->r_cntrl, rx_control);

		if (dev->flags & IFF_ALLMULTI) {
			out_be32(&fec->gaddr1, 0xffffffff);
			out_be32(&fec->gaddr2, 0xffffffff);
		} else {
			u32 crc;
			int i;
			struct dev_mc_list *dmi;
			u32 gaddr1 = 0x00000000;
			u32 gaddr2 = 0x00000000;

			dmi = dev->mc_list;
			for (i=0; i<dev->mc_count; i++) {
				crc = ether_crc_le(6, dmi->dmi_addr) >> 26;
				if (crc >= 32)
					gaddr1 |= 1 << (crc-32);
				else
					gaddr2 |= 1 << crc;
				dmi = dmi->next;
			}
			out_be32(&fec->gaddr1, gaddr1);
			out_be32(&fec->gaddr2, gaddr2);
		}
	}
}

/**
 * mpc52xx_fec_hw_init
 * @dev: network device
 *
 * Setup various hardware setting, only needed once on start
 */
static void mpc52xx_fec_hw_init(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;
	int i;

	/* Whack a reset.  We should wait for this. */
	out_be32(&fec->ecntrl, FEC_ECNTRL_RESET);
	for (i = 0; i < FEC_RESET_DELAY; ++i) {
		if ((in_be32(&fec->ecntrl) & FEC_ECNTRL_RESET) == 0)
			break;
		udelay(1);
	}
	if (i == FEC_RESET_DELAY)
		dev_err(&dev->dev, "FEC Reset timeout!\n");

	/* set pause to 0x20 frames */
	out_be32(&fec->op_pause, FEC_OP_PAUSE_OPCODE | 0x20);

	/* high service request will be deasserted when there's < 7 bytes in fifo
	 * low service request will be deasserted when there's < 4*7 bytes in fifo
	 */
	out_be32(&fec->rfifo_cntrl, FEC_FIFO_CNTRL_FRAME | FEC_FIFO_CNTRL_LTG_7);
	out_be32(&fec->tfifo_cntrl, FEC_FIFO_CNTRL_FRAME | FEC_FIFO_CNTRL_LTG_7);

	/* alarm when <= x bytes in FIFO */
	out_be32(&fec->rfifo_alarm, 0x0000030c);
	out_be32(&fec->tfifo_alarm, 0x00000100);

	/* begin transmittion when 256 bytes are in FIFO (or EOF or FIFO full) */
	out_be32(&fec->x_wmrk, FEC_FIFO_WMRK_256B);

	/* enable crc generation */
	out_be32(&fec->xmit_fsm, FEC_XMIT_FSM_APPEND_CRC | FEC_XMIT_FSM_ENABLE_CRC);
	out_be32(&fec->iaddr1, 0x00000000);	/* No individual filter */
	out_be32(&fec->iaddr2, 0x00000000);	/* No individual filter */

	/* set phy speed.
	 * this can't be done in phy driver, since it needs to be called
	 * before fec stuff (even on resume) */
	out_be32(&fec->mii_speed, priv->mdio_speed);
}

/**
 * mpc52xx_fec_start
 * @dev: network device
 *
 * This function is called to start or restart the FEC during a link
 * change.  This happens on fifo errors or when switching between half
 * and full duplex.
 */
static void mpc52xx_fec_start(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;
	u32 rcntrl;
	u32 tcntrl;
	u32 tmp;

	/* clear sticky error bits */
	tmp = FEC_FIFO_STATUS_ERR | FEC_FIFO_STATUS_UF | FEC_FIFO_STATUS_OF;
	out_be32(&fec->rfifo_status, in_be32(&fec->rfifo_status) & tmp);
	out_be32(&fec->tfifo_status, in_be32(&fec->tfifo_status) & tmp);

	/* FIFOs will reset on mpc52xx_fec_enable */
	out_be32(&fec->reset_cntrl, FEC_RESET_CNTRL_ENABLE_IS_RESET);

	/* Set station address. */
	mpc52xx_fec_set_paddr(dev, dev->dev_addr);

	mpc52xx_fec_set_multicast_list(dev);

	/* set max frame len, enable flow control, select mii mode */
	rcntrl = FEC_RX_BUFFER_SIZE << 16;	/* max frame length */
	rcntrl |= FEC_RCNTRL_FCE;

	if (!priv->seven_wire_mode)
		rcntrl |= FEC_RCNTRL_MII_MODE;

	if (priv->duplex == DUPLEX_FULL)
		tcntrl = FEC_TCNTRL_FDEN;	/* FD enable */
	else {
		rcntrl |= FEC_RCNTRL_DRT;	/* disable Rx on Tx (HD) */
		tcntrl = 0;
	}
	out_be32(&fec->r_cntrl, rcntrl);
	out_be32(&fec->x_cntrl, tcntrl);

	/* Clear any outstanding interrupt. */
	out_be32(&fec->ievent, 0xffffffff);

	/* Enable interrupts we wish to service. */
	out_be32(&fec->imask, FEC_IMASK_ENABLE);

	/* And last, enable the transmit and receive processing. */
	out_be32(&fec->ecntrl, FEC_ECNTRL_ETHER_EN);
	out_be32(&fec->r_des_active, 0x01000000);
}

/**
 * mpc52xx_fec_stop
 * @dev: network device
 *
 * stop all activity on fec and empty dma buffers
 */
static void mpc52xx_fec_stop(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;
	unsigned long timeout;

	/* disable all interrupts */
	out_be32(&fec->imask, 0);

	/* Disable the rx task. */
	bcom_disable(priv->rx_dmatsk);

	/* Wait for tx queue to drain, but only if we're in process context */
	if (!in_interrupt()) {
		timeout = jiffies + msecs_to_jiffies(2000);
		while (time_before(jiffies, timeout) &&
				!bcom_queue_empty(priv->tx_dmatsk))
			msleep(100);

		if (time_after_eq(jiffies, timeout))
			dev_err(&dev->dev, "queues didn't drain\n");
#if 1
		if (time_after_eq(jiffies, timeout)) {
			dev_err(&dev->dev, "  tx: index: %i, outdex: %i\n",
					priv->tx_dmatsk->index,
					priv->tx_dmatsk->outdex);
			dev_err(&dev->dev, "  rx: index: %i, outdex: %i\n",
					priv->rx_dmatsk->index,
					priv->rx_dmatsk->outdex);
		}
#endif
	}

	bcom_disable(priv->tx_dmatsk);

	/* Stop FEC */
	out_be32(&fec->ecntrl, in_be32(&fec->ecntrl) & ~FEC_ECNTRL_ETHER_EN);
}

/* reset fec and bestcomm tasks */
static void mpc52xx_fec_reset(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	struct mpc52xx_fec __iomem *fec = priv->fec;

	mpc52xx_fec_stop(dev);

	out_be32(&fec->rfifo_status, in_be32(&fec->rfifo_status));
	out_be32(&fec->reset_cntrl, FEC_RESET_CNTRL_RESET_FIFO);

	mpc52xx_fec_free_rx_buffers(dev, priv->rx_dmatsk);

	mpc52xx_fec_hw_init(dev);

	if (priv->phydev) {
		phy_stop(priv->phydev);
		phy_write(priv->phydev, MII_BMCR, BMCR_RESET);
		phy_start(priv->phydev);
	}

	bcom_fec_rx_reset(priv->rx_dmatsk);
	bcom_fec_tx_reset(priv->tx_dmatsk);

	mpc52xx_fec_alloc_rx_buffers(dev, priv->rx_dmatsk);

	bcom_enable(priv->rx_dmatsk);
	bcom_enable(priv->tx_dmatsk);

	mpc52xx_fec_start(dev);
}


/* ethtool interface */
static void mpc52xx_fec_get_drvinfo(struct net_device *dev,
		struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRIVER_NAME);
}

static int mpc52xx_fec_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);

	if (!priv->phydev)
		return -ENODEV;

	return phy_ethtool_gset(priv->phydev, cmd);
}

static int mpc52xx_fec_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);

	if (!priv->phydev)
		return -ENODEV;

	return phy_ethtool_sset(priv->phydev, cmd);
}

static u32 mpc52xx_fec_get_msglevel(struct net_device *dev)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	return priv->msg_enable;
}

static void mpc52xx_fec_set_msglevel(struct net_device *dev, u32 level)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);
	priv->msg_enable = level;
}

static const struct ethtool_ops mpc52xx_fec_ethtool_ops = {
	.get_drvinfo = mpc52xx_fec_get_drvinfo,
	.get_settings = mpc52xx_fec_get_settings,
	.set_settings = mpc52xx_fec_set_settings,
	.get_link = ethtool_op_get_link,
	.get_msglevel = mpc52xx_fec_get_msglevel,
	.set_msglevel = mpc52xx_fec_set_msglevel,
};


static int mpc52xx_fec_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct mpc52xx_fec_priv *priv = netdev_priv(dev);

	if (!priv->phydev)
		return -ENOTSUPP;

	return phy_mii_ioctl(priv->phydev, if_mii(rq), cmd);
}

static const struct net_device_ops mpc52xx_fec_netdev_ops = {
	.ndo_open = mpc52xx_fec_open,
	.ndo_stop = mpc52xx_fec_close,
	.ndo_start_xmit = mpc52xx_fec_start_xmit,
	.ndo_set_multicast_list = mpc52xx_fec_set_multicast_list,
	.ndo_set_mac_address = mpc52xx_fec_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_do_ioctl = mpc52xx_fec_ioctl,
	.ndo_change_mtu = eth_change_mtu,
	.ndo_tx_timeout = mpc52xx_fec_tx_timeout,
	.ndo_get_stats = mpc52xx_fec_get_stats,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = mpc52xx_fec_poll_controller,
#endif
};

/* ======================================================================== */
/* OF Driver                                                                */
/* ======================================================================== */

static int __devinit
mpc52xx_fec_probe(struct of_device *op, const struct of_device_id *match)
{
	int rv;
	struct net_device *ndev;
	struct mpc52xx_fec_priv *priv = NULL;
	struct resource mem;
	const u32 *prop;
	int prop_size;

	phys_addr_t rx_fifo;
	phys_addr_t tx_fifo;

	/* Get the ether ndev & it's private zone */
	ndev = alloc_etherdev(sizeof(struct mpc52xx_fec_priv));
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->ndev = ndev;

	/* Reserve FEC control zone */
	rv = of_address_to_resource(op->node, 0, &mem);
	if (rv) {
		printk(KERN_ERR DRIVER_NAME ": "
				"Error while parsing device node resource\n" );
		return rv;
	}
	if ((mem.end - mem.start + 1) < sizeof(struct mpc52xx_fec)) {
		printk(KERN_ERR DRIVER_NAME
			" - invalid resource size (%lx < %x), check mpc52xx_devices.c\n",
			(unsigned long)(mem.end - mem.start + 1), sizeof(struct mpc52xx_fec));
		return -EINVAL;
	}

	if (!request_mem_region(mem.start, sizeof(struct mpc52xx_fec), DRIVER_NAME))
		return -EBUSY;

	/* Init ether ndev with what we have */
	ndev->netdev_ops	= &mpc52xx_fec_netdev_ops;
	ndev->ethtool_ops	= &mpc52xx_fec_ethtool_ops;
	ndev->watchdog_timeo	= FEC_WATCHDOG_TIMEOUT;
	ndev->base_addr		= mem.start;
	SET_NETDEV_DEV(ndev, &op->dev);

	spin_lock_init(&priv->lock);

	/* ioremap the zones */
	priv->fec = ioremap(mem.start, sizeof(struct mpc52xx_fec));

	if (!priv->fec) {
		rv = -ENOMEM;
		goto probe_error;
	}

	/* Bestcomm init */
	rx_fifo = ndev->base_addr + offsetof(struct mpc52xx_fec, rfifo_data);
	tx_fifo = ndev->base_addr + offsetof(struct mpc52xx_fec, tfifo_data);

	priv->rx_dmatsk = bcom_fec_rx_init(FEC_RX_NUM_BD, rx_fifo, FEC_RX_BUFFER_SIZE);
	priv->tx_dmatsk = bcom_fec_tx_init(FEC_TX_NUM_BD, tx_fifo);

	if (!priv->rx_dmatsk || !priv->tx_dmatsk) {
		printk(KERN_ERR DRIVER_NAME ": Can not init SDMA tasks\n" );
		rv = -ENOMEM;
		goto probe_error;
	}

	/* Get the IRQ we need one by one */
		/* Control */
	ndev->irq = irq_of_parse_and_map(op->node, 0);

		/* RX */
	priv->r_irq = bcom_get_task_irq(priv->rx_dmatsk);

		/* TX */
	priv->t_irq = bcom_get_task_irq(priv->tx_dmatsk);

	/* MAC address init */
	if (!is_zero_ether_addr(mpc52xx_fec_mac_addr))
		memcpy(ndev->dev_addr, mpc52xx_fec_mac_addr, 6);
	else
		mpc52xx_fec_get_paddr(ndev, ndev->dev_addr);

	priv->msg_enable = netif_msg_init(debug, MPC52xx_MESSAGES_DEFAULT);

	/*
	 * Link mode configuration
	 */

	/* Start with safe defaults for link connection */
	priv->speed = 100;
	priv->duplex = DUPLEX_HALF;
	priv->mdio_speed = ((mpc5xxx_get_bus_frequency(op->node) >> 20) / 5) << 1;

	/* The current speed preconfigures the speed of the MII link */
	prop = of_get_property(op->node, "current-speed", &prop_size);
	if (prop && (prop_size >= sizeof(u32) * 2)) {
		priv->speed = prop[0];
		priv->duplex = prop[1] ? DUPLEX_FULL : DUPLEX_HALF;
	}

	/* If there is a phy handle, then get the PHY node */
	priv->phy_node = of_parse_phandle(op->node, "phy-handle", 0);

	/* the 7-wire property means don't use MII mode */
	if (of_find_property(op->node, "fsl,7-wire-mode", NULL)) {
		priv->seven_wire_mode = 1;
		dev_info(&ndev->dev, "using 7-wire PHY mode\n");
	}

	/* Hardware init */
	mpc52xx_fec_hw_init(ndev);
	mpc52xx_fec_reset_stats(ndev);

	rv = register_netdev(ndev);
	if (rv < 0)
		goto probe_error;

	/* We're done ! */
	dev_set_drvdata(&op->dev, ndev);

	return 0;


	/* Error handling - free everything that might be allocated */
probe_error:

	if (priv->phy_node)
		of_node_put(priv->phy_node);
	priv->phy_node = NULL;

	irq_dispose_mapping(ndev->irq);

	if (priv->rx_dmatsk)
		bcom_fec_rx_release(priv->rx_dmatsk);
	if (priv->tx_dmatsk)
		bcom_fec_tx_release(priv->tx_dmatsk);

	if (priv->fec)
		iounmap(priv->fec);

	release_mem_region(mem.start, sizeof(struct mpc52xx_fec));

	free_netdev(ndev);

	return rv;
}

static int
mpc52xx_fec_remove(struct of_device *op)
{
	struct net_device *ndev;
	struct mpc52xx_fec_priv *priv;

	ndev = dev_get_drvdata(&op->dev);
	priv = netdev_priv(ndev);

	unregister_netdev(ndev);

	if (priv->phy_node)
		of_node_put(priv->phy_node);
	priv->phy_node = NULL;

	irq_dispose_mapping(ndev->irq);

	bcom_fec_rx_release(priv->rx_dmatsk);
	bcom_fec_tx_release(priv->tx_dmatsk);

	iounmap(priv->fec);

	release_mem_region(ndev->base_addr, sizeof(struct mpc52xx_fec));

	free_netdev(ndev);

	dev_set_drvdata(&op->dev, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int mpc52xx_fec_of_suspend(struct of_device *op, pm_message_t state)
{
	struct net_device *dev = dev_get_drvdata(&op->dev);

	if (netif_running(dev))
		mpc52xx_fec_close(dev);

	return 0;
}

static int mpc52xx_fec_of_resume(struct of_device *op)
{
	struct net_device *dev = dev_get_drvdata(&op->dev);

	mpc52xx_fec_hw_init(dev);
	mpc52xx_fec_reset_stats(dev);

	if (netif_running(dev))
		mpc52xx_fec_open(dev);

	return 0;
}
#endif

static struct of_device_id mpc52xx_fec_match[] = {
	{ .compatible = "fsl,mpc5200b-fec", },
	{ .compatible = "fsl,mpc5200-fec", },
	{ .compatible = "mpc5200-fec", },
	{ }
};

MODULE_DEVICE_TABLE(of, mpc52xx_fec_match);

static struct of_platform_driver mpc52xx_fec_driver = {
	.owner		= THIS_MODULE,
	.name		= DRIVER_NAME,
	.match_table	= mpc52xx_fec_match,
	.probe		= mpc52xx_fec_probe,
	.remove		= mpc52xx_fec_remove,
#ifdef CONFIG_PM
	.suspend	= mpc52xx_fec_of_suspend,
	.resume		= mpc52xx_fec_of_resume,
#endif
};


/* ======================================================================== */
/* Module                                                                   */
/* ======================================================================== */

static int __init
mpc52xx_fec_init(void)
{
#ifdef CONFIG_FEC_MPC52xx_MDIO
	int ret;
	ret = of_register_platform_driver(&mpc52xx_fec_mdio_driver);
	if (ret) {
		printk(KERN_ERR DRIVER_NAME ": failed to register mdio driver\n");
		return ret;
	}
#endif
	return of_register_platform_driver(&mpc52xx_fec_driver);
}

static void __exit
mpc52xx_fec_exit(void)
{
	of_unregister_platform_driver(&mpc52xx_fec_driver);
#ifdef CONFIG_FEC_MPC52xx_MDIO
	of_unregister_platform_driver(&mpc52xx_fec_mdio_driver);
#endif
}


module_init(mpc52xx_fec_init);
module_exit(mpc52xx_fec_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dale Farnsworth");
MODULE_DESCRIPTION("Ethernet driver for the Freescale MPC52xx FEC");
