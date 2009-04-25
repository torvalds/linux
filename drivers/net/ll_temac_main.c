/*
 * Driver for Xilinx TEMAC Ethernet device
 *
 * Copyright (c) 2008 Nissin Systems Co., Ltd.,  Yoshio Kashiwagi
 * Copyright (c) 2005-2008 DLA Systems,  David H. Lynch Jr. <dhlii@dlasys.net>
 * Copyright (c) 2008-2009 Secret Lab Technologies Ltd.
 *
 * This is a driver for the Xilinx ll_temac ipcore which is often used
 * in the Virtex and Spartan series of chips.
 *
 * Notes:
 * - The ll_temac hardware uses indirect access for many of the TEMAC
 *   registers, include the MDIO bus.  However, indirect access to MDIO
 *   registers take considerably more clock cycles than to TEMAC registers.
 *   MDIO accesses are long, so threads doing them should probably sleep
 *   rather than busywait.  However, since only one indirect access can be
 *   in progress at any given time, that means that *all* indirect accesses
 *   could end up sleeping (to wait for an MDIO access to complete).
 *   Fortunately none of the indirect accesses are on the 'hot' path for tx
 *   or rx, so this should be okay.
 *
 * TODO:
 * - Fix driver to work on more than just Virtex5.  Right now the driver
 *   assumes that the locallink DMA registers are accessed via DCR
 *   instructions.
 * - Factor out locallink DMA code into separate driver
 * - Fix multicast assignment.
 * - Fix support for hardware checksumming.
 * - Testing.  Lots and lots of testing.
 *
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/tcp.h>      /* needed for sizeof(tcphdr) */
#include <linux/udp.h>      /* needed for sizeof(udphdr) */
#include <linux/phy.h>
#include <linux/in.h>
#include <linux/io.h>
#include <linux/ip.h>

#include "ll_temac.h"

#define TX_BD_NUM   64
#define RX_BD_NUM   128

/* ---------------------------------------------------------------------
 * Low level register access functions
 */

u32 temac_ior(struct temac_local *lp, int offset)
{
	return in_be32((u32 *)(lp->regs + offset));
}

void temac_iow(struct temac_local *lp, int offset, u32 value)
{
	out_be32((u32 *) (lp->regs + offset), value);
}

int temac_indirect_busywait(struct temac_local *lp)
{
	long end = jiffies + 2;

	while (!(temac_ior(lp, XTE_RDY0_OFFSET) & XTE_RDY0_HARD_ACS_RDY_MASK)) {
		if (end - jiffies <= 0) {
			WARN_ON(1);
			return -ETIMEDOUT;
		}
		msleep(1);
	}
	return 0;
}

/**
 * temac_indirect_in32
 *
 * lp->indirect_mutex must be held when calling this function
 */
u32 temac_indirect_in32(struct temac_local *lp, int reg)
{
	u32 val;

	if (temac_indirect_busywait(lp))
		return -ETIMEDOUT;
	temac_iow(lp, XTE_CTL0_OFFSET, reg);
	if (temac_indirect_busywait(lp))
		return -ETIMEDOUT;
	val = temac_ior(lp, XTE_LSW0_OFFSET);

	return val;
}

/**
 * temac_indirect_out32
 *
 * lp->indirect_mutex must be held when calling this function
 */
void temac_indirect_out32(struct temac_local *lp, int reg, u32 value)
{
	if (temac_indirect_busywait(lp))
		return;
	temac_iow(lp, XTE_LSW0_OFFSET, value);
	temac_iow(lp, XTE_CTL0_OFFSET, CNTLREG_WRITE_ENABLE_MASK | reg);
}

static u32 temac_dma_in32(struct temac_local *lp, int reg)
{
	return dcr_read(lp->sdma_dcrs, reg);
}

static void temac_dma_out32(struct temac_local *lp, int reg, u32 value)
{
	dcr_write(lp->sdma_dcrs, reg, value);
}

/**
 * temac_dma_bd_init - Setup buffer descriptor rings
 */
static int temac_dma_bd_init(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);
	struct sk_buff *skb;
	int i;

	lp->rx_skb = kzalloc(sizeof(struct sk_buff)*RX_BD_NUM, GFP_KERNEL);
	/* allocate the tx and rx ring buffer descriptors. */
	/* returns a virtual addres and a physical address. */
	lp->tx_bd_v = dma_alloc_coherent(ndev->dev.parent,
					 sizeof(*lp->tx_bd_v) * TX_BD_NUM,
					 &lp->tx_bd_p, GFP_KERNEL);
	lp->rx_bd_v = dma_alloc_coherent(ndev->dev.parent,
					 sizeof(*lp->rx_bd_v) * RX_BD_NUM,
					 &lp->rx_bd_p, GFP_KERNEL);

	memset(lp->tx_bd_v, 0, sizeof(*lp->tx_bd_v) * TX_BD_NUM);
	for (i = 0; i < TX_BD_NUM; i++) {
		lp->tx_bd_v[i].next = lp->tx_bd_p +
				sizeof(*lp->tx_bd_v) * ((i + 1) % TX_BD_NUM);
	}

	memset(lp->rx_bd_v, 0, sizeof(*lp->rx_bd_v) * RX_BD_NUM);
	for (i = 0; i < RX_BD_NUM; i++) {
		lp->rx_bd_v[i].next = lp->rx_bd_p +
				sizeof(*lp->rx_bd_v) * ((i + 1) % RX_BD_NUM);

		skb = alloc_skb(XTE_MAX_JUMBO_FRAME_SIZE
				+ XTE_ALIGN, GFP_ATOMIC);
		if (skb == 0) {
			dev_err(&ndev->dev, "alloc_skb error %d\n", i);
			return -1;
		}
		lp->rx_skb[i] = skb;
		skb_reserve(skb,  BUFFER_ALIGN(skb->data));
		/* returns physical address of skb->data */
		lp->rx_bd_v[i].phys = dma_map_single(ndev->dev.parent,
						     skb->data,
						     XTE_MAX_JUMBO_FRAME_SIZE,
						     DMA_FROM_DEVICE);
		lp->rx_bd_v[i].len = XTE_MAX_JUMBO_FRAME_SIZE;
		lp->rx_bd_v[i].app0 = STS_CTRL_APP0_IRQONEND;
	}

	temac_dma_out32(lp, TX_CHNL_CTRL, 0x10220400 |
					  CHNL_CTRL_IRQ_EN |
					  CHNL_CTRL_IRQ_DLY_EN |
					  CHNL_CTRL_IRQ_COAL_EN);
	/* 0x10220483 */
	/* 0x00100483 */
	temac_dma_out32(lp, RX_CHNL_CTRL, 0xff010000 |
					  CHNL_CTRL_IRQ_EN |
					  CHNL_CTRL_IRQ_DLY_EN |
					  CHNL_CTRL_IRQ_COAL_EN |
					  CHNL_CTRL_IRQ_IOE);
	/* 0xff010283 */

	temac_dma_out32(lp, RX_CURDESC_PTR,  lp->rx_bd_p);
	temac_dma_out32(lp, RX_TAILDESC_PTR,
		       lp->rx_bd_p + (sizeof(*lp->rx_bd_v) * (RX_BD_NUM - 1)));
	temac_dma_out32(lp, TX_CURDESC_PTR, lp->tx_bd_p);

	return 0;
}

/* ---------------------------------------------------------------------
 * net_device_ops
 */

static int temac_set_mac_address(struct net_device *ndev, void *address)
{
	struct temac_local *lp = netdev_priv(ndev);

	if (address)
		memcpy(ndev->dev_addr, address, ETH_ALEN);

	if (!is_valid_ether_addr(ndev->dev_addr))
		random_ether_addr(ndev->dev_addr);

	/* set up unicast MAC address filter set its mac address */
	mutex_lock(&lp->indirect_mutex);
	temac_indirect_out32(lp, XTE_UAW0_OFFSET,
			     (ndev->dev_addr[0]) |
			     (ndev->dev_addr[1] << 8) |
			     (ndev->dev_addr[2] << 16) |
			     (ndev->dev_addr[3] << 24));
	/* There are reserved bits in EUAW1
	 * so don't affect them Set MAC bits [47:32] in EUAW1 */
	temac_indirect_out32(lp, XTE_UAW1_OFFSET,
			     (ndev->dev_addr[4] & 0x000000ff) |
			     (ndev->dev_addr[5] << 8));
	mutex_unlock(&lp->indirect_mutex);

	return 0;
}

static void temac_set_multicast_list(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);
	u32 multi_addr_msw, multi_addr_lsw, val;
	int i;

	mutex_lock(&lp->indirect_mutex);
	if (ndev->flags & (IFF_ALLMULTI | IFF_PROMISC)
			|| ndev->mc_count > MULTICAST_CAM_TABLE_NUM) {
		/*
		 *	We must make the kernel realise we had to move
		 *	into promisc mode or we start all out war on
		 *	the cable. If it was a promisc request the
		 *	flag is already set. If not we assert it.
		 */
		ndev->flags |= IFF_PROMISC;
		temac_indirect_out32(lp, XTE_AFM_OFFSET, XTE_AFM_EPPRM_MASK);
		dev_info(&ndev->dev, "Promiscuous mode enabled.\n");
	} else if (ndev->mc_count) {
		struct dev_mc_list *mclist = ndev->mc_list;
		for (i = 0; mclist && i < ndev->mc_count; i++) {

			if (i >= MULTICAST_CAM_TABLE_NUM)
				break;
			multi_addr_msw = ((mclist->dmi_addr[3] << 24) |
					  (mclist->dmi_addr[2] << 16) |
					  (mclist->dmi_addr[1] << 8) |
					  (mclist->dmi_addr[0]));
			temac_indirect_out32(lp, XTE_MAW0_OFFSET,
					     multi_addr_msw);
			multi_addr_lsw = ((mclist->dmi_addr[5] << 8) |
					  (mclist->dmi_addr[4]) | (i << 16));
			temac_indirect_out32(lp, XTE_MAW1_OFFSET,
					     multi_addr_lsw);
			mclist = mclist->next;
		}
	} else {
		val = temac_indirect_in32(lp, XTE_AFM_OFFSET);
		temac_indirect_out32(lp, XTE_AFM_OFFSET,
				     val & ~XTE_AFM_EPPRM_MASK);
		temac_indirect_out32(lp, XTE_MAW0_OFFSET, 0);
		temac_indirect_out32(lp, XTE_MAW1_OFFSET, 0);
		dev_info(&ndev->dev, "Promiscuous mode disabled.\n");
	}
	mutex_unlock(&lp->indirect_mutex);
}

struct temac_option {
	int flg;
	u32 opt;
	u32 reg;
	u32 m_or;
	u32 m_and;
} temac_options[] = {
	/* Turn on jumbo packet support for both Rx and Tx */
	{
		.opt = XTE_OPTION_JUMBO,
		.reg = XTE_TXC_OFFSET,
		.m_or = XTE_TXC_TXJMBO_MASK,
	},
	{
		.opt = XTE_OPTION_JUMBO,
		.reg = XTE_RXC1_OFFSET,
		.m_or =XTE_RXC1_RXJMBO_MASK,
	},
	/* Turn on VLAN packet support for both Rx and Tx */
	{
		.opt = XTE_OPTION_VLAN,
		.reg = XTE_TXC_OFFSET,
		.m_or =XTE_TXC_TXVLAN_MASK,
	},
	{
		.opt = XTE_OPTION_VLAN,
		.reg = XTE_RXC1_OFFSET,
		.m_or =XTE_RXC1_RXVLAN_MASK,
	},
	/* Turn on FCS stripping on receive packets */
	{
		.opt = XTE_OPTION_FCS_STRIP,
		.reg = XTE_RXC1_OFFSET,
		.m_or =XTE_RXC1_RXFCS_MASK,
	},
	/* Turn on FCS insertion on transmit packets */
	{
		.opt = XTE_OPTION_FCS_INSERT,
		.reg = XTE_TXC_OFFSET,
		.m_or =XTE_TXC_TXFCS_MASK,
	},
	/* Turn on length/type field checking on receive packets */
	{
		.opt = XTE_OPTION_LENTYPE_ERR,
		.reg = XTE_RXC1_OFFSET,
		.m_or =XTE_RXC1_RXLT_MASK,
	},
	/* Turn on flow control */
	{
		.opt = XTE_OPTION_FLOW_CONTROL,
		.reg = XTE_FCC_OFFSET,
		.m_or =XTE_FCC_RXFLO_MASK,
	},
	/* Turn on flow control */
	{
		.opt = XTE_OPTION_FLOW_CONTROL,
		.reg = XTE_FCC_OFFSET,
		.m_or =XTE_FCC_TXFLO_MASK,
	},
	/* Turn on promiscuous frame filtering (all frames are received ) */
	{
		.opt = XTE_OPTION_PROMISC,
		.reg = XTE_AFM_OFFSET,
		.m_or =XTE_AFM_EPPRM_MASK,
	},
	/* Enable transmitter if not already enabled */
	{
		.opt = XTE_OPTION_TXEN,
		.reg = XTE_TXC_OFFSET,
		.m_or =XTE_TXC_TXEN_MASK,
	},
	/* Enable receiver? */
	{
		.opt = XTE_OPTION_RXEN,
		.reg = XTE_RXC1_OFFSET,
		.m_or =XTE_RXC1_RXEN_MASK,
	},
	{}
};

/**
 * temac_setoptions
 */
static u32 temac_setoptions(struct net_device *ndev, u32 options)
{
	struct temac_local *lp = netdev_priv(ndev);
	struct temac_option *tp = &temac_options[0];
	int reg;

	mutex_lock(&lp->indirect_mutex);
	while (tp->opt) {
		reg = temac_indirect_in32(lp, tp->reg) & ~tp->m_or;
		if (options & tp->opt)
			reg |= tp->m_or;
		temac_indirect_out32(lp, tp->reg, reg);
		tp++;
	}
	lp->options |= options;
	mutex_unlock(&lp->indirect_mutex);

	return (0);
}

/* Initilize temac */
static void temac_device_reset(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);
	u32 timeout;
	u32 val;

	/* Perform a software reset */

	/* 0x300 host enable bit ? */
	/* reset PHY through control register ?:1 */

	dev_dbg(&ndev->dev, "%s()\n", __func__);

	mutex_lock(&lp->indirect_mutex);
	/* Reset the receiver and wait for it to finish reset */
	temac_indirect_out32(lp, XTE_RXC1_OFFSET, XTE_RXC1_RXRST_MASK);
	timeout = 1000;
	while (temac_indirect_in32(lp, XTE_RXC1_OFFSET) & XTE_RXC1_RXRST_MASK) {
		udelay(1);
		if (--timeout == 0) {
			dev_err(&ndev->dev,
				"temac_device_reset RX reset timeout!!\n");
			break;
		}
	}

	/* Reset the transmitter and wait for it to finish reset */
	temac_indirect_out32(lp, XTE_TXC_OFFSET, XTE_TXC_TXRST_MASK);
	timeout = 1000;
	while (temac_indirect_in32(lp, XTE_TXC_OFFSET) & XTE_TXC_TXRST_MASK) {
		udelay(1);
		if (--timeout == 0) {
			dev_err(&ndev->dev,
				"temac_device_reset TX reset timeout!!\n");
			break;
		}
	}

	/* Disable the receiver */
	val = temac_indirect_in32(lp, XTE_RXC1_OFFSET);
	temac_indirect_out32(lp, XTE_RXC1_OFFSET, val & ~XTE_RXC1_RXEN_MASK);

	/* Reset Local Link (DMA) */
	temac_dma_out32(lp, DMA_CONTROL_REG, DMA_CONTROL_RST);
	timeout = 1000;
	while (temac_dma_in32(lp, DMA_CONTROL_REG) & DMA_CONTROL_RST) {
		udelay(1);
		if (--timeout == 0) {
			dev_err(&ndev->dev,
				"temac_device_reset DMA reset timeout!!\n");
			break;
		}
	}
	temac_dma_out32(lp, DMA_CONTROL_REG, DMA_TAIL_ENABLE);

	temac_dma_bd_init(ndev);

	temac_indirect_out32(lp, XTE_RXC0_OFFSET, 0);
	temac_indirect_out32(lp, XTE_RXC1_OFFSET, 0);
	temac_indirect_out32(lp, XTE_TXC_OFFSET, 0);
	temac_indirect_out32(lp, XTE_FCC_OFFSET, XTE_FCC_RXFLO_MASK);

	mutex_unlock(&lp->indirect_mutex);

	/* Sync default options with HW
	 * but leave receiver and transmitter disabled.  */
	temac_setoptions(ndev,
			 lp->options & ~(XTE_OPTION_TXEN | XTE_OPTION_RXEN));

	temac_set_mac_address(ndev, NULL);

	/* Set address filter table */
	temac_set_multicast_list(ndev);
	if (temac_setoptions(ndev, lp->options))
		dev_err(&ndev->dev, "Error setting TEMAC options\n");

	/* Init Driver variable */
	ndev->trans_start = 0;
}

void temac_adjust_link(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);
	struct phy_device *phy = lp->phy_dev;
	u32 mii_speed;
	int link_state;

	/* hash together the state values to decide if something has changed */
	link_state = phy->speed | (phy->duplex << 1) | phy->link;

	mutex_lock(&lp->indirect_mutex);
	if (lp->last_link != link_state) {
		mii_speed = temac_indirect_in32(lp, XTE_EMCFG_OFFSET);
		mii_speed &= ~XTE_EMCFG_LINKSPD_MASK;

		switch (phy->speed) {
		case SPEED_1000: mii_speed |= XTE_EMCFG_LINKSPD_1000; break;
		case SPEED_100: mii_speed |= XTE_EMCFG_LINKSPD_100; break;
		case SPEED_10: mii_speed |= XTE_EMCFG_LINKSPD_10; break;
		}

		/* Write new speed setting out to TEMAC */
		temac_indirect_out32(lp, XTE_EMCFG_OFFSET, mii_speed);
		lp->last_link = link_state;
		phy_print_status(phy);
	}
	mutex_unlock(&lp->indirect_mutex);
}

static void temac_start_xmit_done(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);
	struct cdmac_bd *cur_p;
	unsigned int stat = 0;

	cur_p = &lp->tx_bd_v[lp->tx_bd_ci];
	stat = cur_p->app0;

	while (stat & STS_CTRL_APP0_CMPLT) {
		dma_unmap_single(ndev->dev.parent, cur_p->phys, cur_p->len,
				 DMA_TO_DEVICE);
		if (cur_p->app4)
			dev_kfree_skb_irq((struct sk_buff *)cur_p->app4);
		cur_p->app0 = 0;

		ndev->stats.tx_packets++;
		ndev->stats.tx_bytes += cur_p->len;

		lp->tx_bd_ci++;
		if (lp->tx_bd_ci >= TX_BD_NUM)
			lp->tx_bd_ci = 0;

		cur_p = &lp->tx_bd_v[lp->tx_bd_ci];
		stat = cur_p->app0;
	}

	netif_wake_queue(ndev);
}

static int temac_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);
	struct cdmac_bd *cur_p;
	dma_addr_t start_p, tail_p;
	int ii;
	unsigned long num_frag;
	skb_frag_t *frag;

	num_frag = skb_shinfo(skb)->nr_frags;
	frag = &skb_shinfo(skb)->frags[0];
	start_p = lp->tx_bd_p + sizeof(*lp->tx_bd_v) * lp->tx_bd_tail;
	cur_p = &lp->tx_bd_v[lp->tx_bd_tail];

	if (cur_p->app0 & STS_CTRL_APP0_CMPLT) {
		if (!netif_queue_stopped(ndev)) {
			netif_stop_queue(ndev);
			return NETDEV_TX_BUSY;
		}
		return NETDEV_TX_BUSY;
	}

	cur_p->app0 = 0;
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const struct iphdr *ip = ip_hdr(skb);
		int length = 0, start = 0, insert = 0;

		switch (ip->protocol) {
		case IPPROTO_TCP:
			start = sizeof(struct iphdr) + ETH_HLEN;
			insert = sizeof(struct iphdr) + ETH_HLEN + 16;
			length = ip->tot_len - sizeof(struct iphdr);
			break;
		case IPPROTO_UDP:
			start = sizeof(struct iphdr) + ETH_HLEN;
			insert = sizeof(struct iphdr) + ETH_HLEN + 6;
			length = ip->tot_len - sizeof(struct iphdr);
			break;
		default:
			break;
		}
		cur_p->app1 = ((start << 16) | insert);
		cur_p->app2 = csum_tcpudp_magic(ip->saddr, ip->daddr,
						length, ip->protocol, 0);
		skb->data[insert] = 0;
		skb->data[insert + 1] = 0;
	}
	cur_p->app0 |= STS_CTRL_APP0_SOP;
	cur_p->len = skb_headlen(skb);
	cur_p->phys = dma_map_single(ndev->dev.parent, skb->data, skb->len,
				     DMA_TO_DEVICE);
	cur_p->app4 = (unsigned long)skb;

	for (ii = 0; ii < num_frag; ii++) {
		lp->tx_bd_tail++;
		if (lp->tx_bd_tail >= TX_BD_NUM)
			lp->tx_bd_tail = 0;

		cur_p = &lp->tx_bd_v[lp->tx_bd_tail];
		cur_p->phys = dma_map_single(ndev->dev.parent,
					     (void *)page_address(frag->page) +
					          frag->page_offset,
					     frag->size, DMA_TO_DEVICE);
		cur_p->len = frag->size;
		cur_p->app0 = 0;
		frag++;
	}
	cur_p->app0 |= STS_CTRL_APP0_EOP;

	tail_p = lp->tx_bd_p + sizeof(*lp->tx_bd_v) * lp->tx_bd_tail;
	lp->tx_bd_tail++;
	if (lp->tx_bd_tail >= TX_BD_NUM)
		lp->tx_bd_tail = 0;

	/* Kick off the transfer */
	temac_dma_out32(lp, TX_TAILDESC_PTR, tail_p); /* DMA start */

	return 0;
}


static void ll_temac_recv(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);
	struct sk_buff *skb, *new_skb;
	unsigned int bdstat;
	struct cdmac_bd *cur_p;
	dma_addr_t tail_p;
	int length;
	unsigned long skb_vaddr;
	unsigned long flags;

	spin_lock_irqsave(&lp->rx_lock, flags);

	tail_p = lp->rx_bd_p + sizeof(*lp->rx_bd_v) * lp->rx_bd_ci;
	cur_p = &lp->rx_bd_v[lp->rx_bd_ci];

	bdstat = cur_p->app0;
	while ((bdstat & STS_CTRL_APP0_CMPLT)) {

		skb = lp->rx_skb[lp->rx_bd_ci];
		length = cur_p->app4;

		skb_vaddr = virt_to_bus(skb->data);
		dma_unmap_single(ndev->dev.parent, skb_vaddr, length,
				 DMA_FROM_DEVICE);

		skb_put(skb, length);
		skb->dev = ndev;
		skb->protocol = eth_type_trans(skb, ndev);
		skb->ip_summed = CHECKSUM_NONE;

		netif_rx(skb);

		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += length;

		new_skb = alloc_skb(XTE_MAX_JUMBO_FRAME_SIZE + XTE_ALIGN,
				GFP_ATOMIC);
		if (new_skb == 0) {
			dev_err(&ndev->dev, "no memory for new sk_buff\n");
			spin_unlock_irqrestore(&lp->rx_lock, flags);
			return;
		}

		skb_reserve(new_skb, BUFFER_ALIGN(new_skb->data));

		cur_p->app0 = STS_CTRL_APP0_IRQONEND;
		cur_p->phys = dma_map_single(ndev->dev.parent, new_skb->data,
					     XTE_MAX_JUMBO_FRAME_SIZE,
					     DMA_FROM_DEVICE);
		cur_p->len = XTE_MAX_JUMBO_FRAME_SIZE;
		lp->rx_skb[lp->rx_bd_ci] = new_skb;

		lp->rx_bd_ci++;
		if (lp->rx_bd_ci >= RX_BD_NUM)
			lp->rx_bd_ci = 0;

		cur_p = &lp->rx_bd_v[lp->rx_bd_ci];
		bdstat = cur_p->app0;
	}
	temac_dma_out32(lp, RX_TAILDESC_PTR, tail_p);

	spin_unlock_irqrestore(&lp->rx_lock, flags);
}

static irqreturn_t ll_temac_tx_irq(int irq, void *_ndev)
{
	struct net_device *ndev = _ndev;
	struct temac_local *lp = netdev_priv(ndev);
	unsigned int status;

	status = temac_dma_in32(lp, TX_IRQ_REG);
	temac_dma_out32(lp, TX_IRQ_REG, status);

	if (status & (IRQ_COAL | IRQ_DLY))
		temac_start_xmit_done(lp->ndev);
	if (status & 0x080)
		dev_err(&ndev->dev, "DMA error 0x%x\n", status);

	return IRQ_HANDLED;
}

static irqreturn_t ll_temac_rx_irq(int irq, void *_ndev)
{
	struct net_device *ndev = _ndev;
	struct temac_local *lp = netdev_priv(ndev);
	unsigned int status;

	/* Read and clear the status registers */
	status = temac_dma_in32(lp, RX_IRQ_REG);
	temac_dma_out32(lp, RX_IRQ_REG, status);

	if (status & (IRQ_COAL | IRQ_DLY))
		ll_temac_recv(lp->ndev);

	return IRQ_HANDLED;
}

static int temac_open(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);
	int rc;

	dev_dbg(&ndev->dev, "temac_open()\n");

	if (lp->phy_node) {
		lp->phy_dev = of_phy_connect(lp->ndev, lp->phy_node,
					     temac_adjust_link, 0, 0);
		if (!lp->phy_dev) {
			dev_err(lp->dev, "of_phy_connect() failed\n");
			return -ENODEV;
		}

		phy_start(lp->phy_dev);
	}

	rc = request_irq(lp->tx_irq, ll_temac_tx_irq, 0, ndev->name, ndev);
	if (rc)
		goto err_tx_irq;
	rc = request_irq(lp->rx_irq, ll_temac_rx_irq, 0, ndev->name, ndev);
	if (rc)
		goto err_rx_irq;

	temac_device_reset(ndev);
	return 0;

 err_rx_irq:
	free_irq(lp->tx_irq, ndev);
 err_tx_irq:
	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);
	lp->phy_dev = NULL;
	dev_err(lp->dev, "request_irq() failed\n");
	return rc;
}

static int temac_stop(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);

	dev_dbg(&ndev->dev, "temac_close()\n");

	free_irq(lp->tx_irq, ndev);
	free_irq(lp->rx_irq, ndev);

	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);
	lp->phy_dev = NULL;

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void
temac_poll_controller(struct net_device *ndev)
{
	struct temac_local *lp = netdev_priv(ndev);

	disable_irq(lp->tx_irq);
	disable_irq(lp->rx_irq);

	ll_temac_rx_irq(lp->tx_irq, lp);
	ll_temac_tx_irq(lp->rx_irq, lp);

	enable_irq(lp->tx_irq);
	enable_irq(lp->rx_irq);
}
#endif

static const struct net_device_ops temac_netdev_ops = {
	.ndo_open = temac_open,
	.ndo_stop = temac_stop,
	.ndo_start_xmit = temac_start_xmit,
	.ndo_set_mac_address = temac_set_mac_address,
	//.ndo_set_multicast_list = temac_set_multicast_list,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = temac_poll_controller,
#endif
};

/* ---------------------------------------------------------------------
 * SYSFS device attributes
 */
static ssize_t temac_show_llink_regs(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct temac_local *lp = netdev_priv(ndev);
	int i, len = 0;

	for (i = 0; i < 0x11; i++)
		len += sprintf(buf + len, "%.8x%s", temac_dma_in32(lp, i),
			       (i % 8) == 7 ? "\n" : " ");
	len += sprintf(buf + len, "\n");

	return len;
}

static DEVICE_ATTR(llink_regs, 0440, temac_show_llink_regs, NULL);

static struct attribute *temac_device_attrs[] = {
	&dev_attr_llink_regs.attr,
	NULL,
};

static const struct attribute_group temac_attr_group = {
	.attrs = temac_device_attrs,
};

static int __init
temac_of_probe(struct of_device *op, const struct of_device_id *match)
{
	struct device_node *np;
	struct temac_local *lp;
	struct net_device *ndev;
	const void *addr;
	int size, rc = 0;
	unsigned int dcrs;

	/* Init network device structure */
	ndev = alloc_etherdev(sizeof(*lp));
	if (!ndev) {
		dev_err(&op->dev, "could not allocate device.\n");
		return -ENOMEM;
	}
	ether_setup(ndev);
	dev_set_drvdata(&op->dev, ndev);
	SET_NETDEV_DEV(ndev, &op->dev);
	ndev->flags &= ~IFF_MULTICAST;  /* clear multicast */
	ndev->features = NETIF_F_SG | NETIF_F_FRAGLIST;
	ndev->netdev_ops = &temac_netdev_ops;
#if 0
	ndev->features |= NETIF_F_IP_CSUM; /* Can checksum TCP/UDP over IPv4. */
	ndev->features |= NETIF_F_HW_CSUM; /* Can checksum all the packets. */
	ndev->features |= NETIF_F_IPV6_CSUM; /* Can checksum IPV6 TCP/UDP */
	ndev->features |= NETIF_F_HIGHDMA; /* Can DMA to high memory. */
	ndev->features |= NETIF_F_HW_VLAN_TX; /* Transmit VLAN hw accel */
	ndev->features |= NETIF_F_HW_VLAN_RX; /* Receive VLAN hw acceleration */
	ndev->features |= NETIF_F_HW_VLAN_FILTER; /* Receive VLAN filtering */
	ndev->features |= NETIF_F_VLAN_CHALLENGED; /* cannot handle VLAN pkts */
	ndev->features |= NETIF_F_GSO; /* Enable software GSO. */
	ndev->features |= NETIF_F_MULTI_QUEUE; /* Has multiple TX/RX queues */
	ndev->features |= NETIF_F_LRO; /* large receive offload */
#endif

	/* setup temac private info structure */
	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &op->dev;
	lp->options = XTE_OPTION_DEFAULTS;
	spin_lock_init(&lp->rx_lock);
	mutex_init(&lp->indirect_mutex);

	/* map device registers */
	lp->regs = of_iomap(op->node, 0);
	if (!lp->regs) {
		dev_err(&op->dev, "could not map temac regs.\n");
		goto nodev;
	}

	/* Find the DMA node, map the DMA registers, and decode the DMA IRQs */
	np = of_parse_phandle(op->node, "llink-connected", 0);
	if (!np) {
		dev_err(&op->dev, "could not find DMA node\n");
		goto nodev;
	}

	dcrs = dcr_resource_start(np, 0);
	if (dcrs == 0) {
		dev_err(&op->dev, "could not get DMA register address\n");
		goto nodev;;
	}
	lp->sdma_dcrs = dcr_map(np, dcrs, dcr_resource_len(np, 0));
	dev_dbg(&op->dev, "DCR base: %x\n", dcrs);

	lp->rx_irq = irq_of_parse_and_map(np, 0);
	lp->tx_irq = irq_of_parse_and_map(np, 1);
	if (!lp->rx_irq || !lp->tx_irq) {
		dev_err(&op->dev, "could not determine irqs\n");
		rc = -ENOMEM;
		goto nodev;
	}

	of_node_put(np); /* Finished with the DMA node; drop the reference */

	/* Retrieve the MAC address */
	addr = of_get_property(op->node, "local-mac-address", &size);
	if ((!addr) || (size != 6)) {
		dev_err(&op->dev, "could not find MAC address\n");
		rc = -ENODEV;
		goto nodev;
	}
	temac_set_mac_address(ndev, (void *)addr);

	rc = temac_mdio_setup(lp, op->node);
	if (rc)
		dev_warn(&op->dev, "error registering MDIO bus\n");

	lp->phy_node = of_parse_phandle(op->node, "phy-handle", 0);
	if (lp->phy_node)
		dev_dbg(lp->dev, "using PHY node %s (%p)\n", np->full_name, np);

	/* Add the device attributes */
	rc = sysfs_create_group(&lp->dev->kobj, &temac_attr_group);
	if (rc) {
		dev_err(lp->dev, "Error creating sysfs files\n");
		goto nodev;
	}

	rc = register_netdev(lp->ndev);
	if (rc) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", rc);
		goto err_register_ndev;
	}

	return 0;

 err_register_ndev:
	sysfs_remove_group(&lp->dev->kobj, &temac_attr_group);
 nodev:
	free_netdev(ndev);
	ndev = NULL;
	return rc;
}

static int __devexit temac_of_remove(struct of_device *op)
{
	struct net_device *ndev = dev_get_drvdata(&op->dev);
	struct temac_local *lp = netdev_priv(ndev);

	temac_mdio_teardown(lp);
	unregister_netdev(ndev);
	sysfs_remove_group(&lp->dev->kobj, &temac_attr_group);
	if (lp->phy_node)
		of_node_put(lp->phy_node);
	lp->phy_node = NULL;
	dev_set_drvdata(&op->dev, NULL);
	free_netdev(ndev);
	return 0;
}

static struct of_device_id temac_of_match[] __devinitdata = {
	{ .compatible = "xlnx,xps-ll-temac-1.01.b", },
	{},
};
MODULE_DEVICE_TABLE(of, temac_of_match);

static struct of_platform_driver temac_of_driver = {
	.match_table = temac_of_match,
	.probe = temac_of_probe,
	.remove = __devexit_p(temac_of_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = "xilinx_temac",
	},
};

static int __init temac_init(void)
{
	return of_register_platform_driver(&temac_of_driver);
}
module_init(temac_init);

static void __exit temac_exit(void)
{
	of_unregister_platform_driver(&temac_of_driver);
}
module_exit(temac_exit);

MODULE_DESCRIPTION("Xilinx LL_TEMAC Ethernet driver");
MODULE_AUTHOR("Yoshio Kashiwagi");
MODULE_LICENSE("GPL");
