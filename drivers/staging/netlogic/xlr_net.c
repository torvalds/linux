/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/smp.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/mipsregs.h>
/*
 * fmn.h - For FMN credit configuration and registering fmn_handler.
 * FMN is communication mechanism that allows processing agents within
 * XLR/XLS to communicate each other.
 */
#include <asm/netlogic/xlr/fmn.h>

#include "platform_net.h"
#include "xlr_net.h"

/*
 * The readl/writel implementation byteswaps on XLR/XLS, so
 * we need to use __raw_ IO to read the NAE registers
 * because they are in the big-endian MMIO area on the SoC.
 */
static inline void xlr_nae_wreg(u32 __iomem *base, unsigned int reg, u32 val)
{
	__raw_writel(val, base + reg);
}

static inline u32 xlr_nae_rdreg(u32 __iomem *base, unsigned int reg)
{
	return __raw_readl(base + reg);
}

static inline void xlr_reg_update(u32 *base_addr, u32 off, u32 val, u32 mask)
{
	u32 tmp;

	tmp = xlr_nae_rdreg(base_addr, off);
	xlr_nae_wreg(base_addr, off, (tmp & ~mask) | (val & mask));
}

#define MAC_SKB_BACK_PTR_SIZE SMP_CACHE_BYTES

static int send_to_rfr_fifo(struct xlr_net_priv *priv, void *addr)
{
	struct nlm_fmn_msg msg;
	int ret = 0, num_try = 0, stnid;
	unsigned long paddr, mflags;

	paddr = virt_to_bus(addr);
	msg.msg0 = (u64)paddr & 0xffffffffe0ULL;
	msg.msg1 = 0;
	msg.msg2 = 0;
	msg.msg3 = 0;
	stnid = priv->nd->rfr_station;
	do {
		mflags = nlm_cop2_enable_irqsave();
		ret = nlm_fmn_send(1, 0, stnid, &msg);
		nlm_cop2_disable_irqrestore(mflags);
		if (ret == 0)
			return 0;
	} while (++num_try < 10000);

	netdev_err(priv->ndev, "Send to RFR failed in RX path\n");
	return ret;
}

static inline unsigned char *xlr_alloc_skb(void)
{
	struct sk_buff *skb;
	int buf_len = sizeof(struct sk_buff *);
	unsigned char *skb_data;

	/* skb->data is cache aligned */
	skb = alloc_skb(XLR_RX_BUF_SIZE, GFP_ATOMIC);
	if (!skb)
		return NULL;
	skb_data = skb->data;
	skb_put(skb, MAC_SKB_BACK_PTR_SIZE);
	skb_pull(skb, MAC_SKB_BACK_PTR_SIZE);
	memcpy(skb_data, &skb, buf_len);

	return skb->data;
}

static void xlr_net_fmn_handler(int bkt, int src_stnid, int size, int code,
				struct nlm_fmn_msg *msg, void *arg)
{
	struct sk_buff *skb;
	void *skb_data = NULL;
	struct net_device *ndev;
	struct xlr_net_priv *priv;
	u32 port, length;
	unsigned char *addr;
	struct xlr_adapter *adapter = arg;

	length = (msg->msg0 >> 40) & 0x3fff;
	if (length == 0) {
		addr = bus_to_virt(msg->msg0 & 0xffffffffffULL);
		addr = addr - MAC_SKB_BACK_PTR_SIZE;
		skb = (struct sk_buff *)(*(unsigned long *)addr);
		dev_kfree_skb_any((struct sk_buff *)addr);
	} else {
		addr = (unsigned char *)
			bus_to_virt(msg->msg0 & 0xffffffffe0ULL);
		length = length - BYTE_OFFSET - MAC_CRC_LEN;
		port = ((int)msg->msg0) & 0x0f;
		addr = addr - MAC_SKB_BACK_PTR_SIZE;
		skb = (struct sk_buff *)(*(unsigned long *)addr);
		skb->dev = adapter->netdev[port];
		if (!skb->dev)
			return;
		ndev = skb->dev;
		priv = netdev_priv(ndev);

		/* 16 byte IP header align */
		skb_reserve(skb, BYTE_OFFSET);
		skb_put(skb, length);
		skb->protocol = eth_type_trans(skb, skb->dev);
		skb->dev->last_rx = jiffies;
		netif_rx(skb);
		/* Fill rx ring */
		skb_data = xlr_alloc_skb();
		if (skb_data)
			send_to_rfr_fifo(priv, skb_data);
	}
}

static struct phy_device *xlr_get_phydev(struct xlr_net_priv *priv)
{
	return mdiobus_get_phy(priv->mii_bus, priv->phy_addr);
}

/*
 * Ethtool operation
 */
static int xlr_get_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct xlr_net_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = xlr_get_phydev(priv);

	if (!phydev)
		return -ENODEV;
	return phy_ethtool_gset(phydev, ecmd);
}

static int xlr_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct xlr_net_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = xlr_get_phydev(priv);

	if (!phydev)
		return -ENODEV;
	return phy_ethtool_sset(phydev, ecmd);
}

static const struct ethtool_ops xlr_ethtool_ops = {
	.get_settings = xlr_get_settings,
	.set_settings = xlr_set_settings,
};

/*
 * Net operations
 */
static int xlr_net_fill_rx_ring(struct net_device *ndev)
{
	void *skb_data;
	struct xlr_net_priv *priv = netdev_priv(ndev);
	int i;

	for (i = 0; i < MAX_FRIN_SPILL / 4; i++) {
		skb_data = xlr_alloc_skb();
		if (!skb_data) {
			netdev_err(ndev, "SKB allocation failed\n");
			return -ENOMEM;
		}
		send_to_rfr_fifo(priv, skb_data);
	}
	netdev_info(ndev, "Rx ring setup done\n");
	return 0;
}

static int xlr_net_open(struct net_device *ndev)
{
	u32 err;
	struct xlr_net_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = xlr_get_phydev(priv);

	/* schedule a link state check */
	phy_start(phydev);

	err = phy_start_aneg(phydev);
	if (err) {
		pr_err("Autoneg failed\n");
		return err;
	}
	/* Setup the speed from PHY to internal reg*/
	xlr_set_gmac_speed(priv);

	netif_tx_start_all_queues(ndev);

	return 0;
}

static int xlr_net_stop(struct net_device *ndev)
{
	struct xlr_net_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = xlr_get_phydev(priv);

	phy_stop(phydev);
	netif_tx_stop_all_queues(ndev);
	return 0;
}

static void xlr_make_tx_desc(struct nlm_fmn_msg *msg, unsigned long addr,
			     struct sk_buff *skb)
{
	unsigned long physkb = virt_to_phys(skb);
	int cpu_core = nlm_core_id();
	int fr_stn_id = cpu_core * 8 + XLR_FB_STN;	/* FB to 6th bucket */

	msg->msg0 = (((u64)1 << 63)	|	/* End of packet descriptor */
		((u64)127 << 54)	|	/* No Free back */
		(u64)skb->len << 40	|	/* Length of data */
		((u64)addr));
	msg->msg1 = (((u64)1 << 63)	|
		((u64)fr_stn_id << 54)	|	/* Free back id */
		(u64)0 << 40		|	/* Set len to 0 */
		((u64)physkb  & 0xffffffff));	/* 32bit address */
	msg->msg2 = 0;
	msg->msg3 = 0;
}

static netdev_tx_t xlr_net_start_xmit(struct sk_buff *skb,
				      struct net_device *ndev)
{
	struct nlm_fmn_msg msg;
	struct xlr_net_priv *priv = netdev_priv(ndev);
	int ret;
	u32 flags;

	xlr_make_tx_desc(&msg, virt_to_phys(skb->data), skb);
	flags = nlm_cop2_enable_irqsave();
	ret = nlm_fmn_send(2, 0, priv->tx_stnid, &msg);
	nlm_cop2_disable_irqrestore(flags);
	if (ret)
		dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static u16 xlr_net_select_queue(struct net_device *ndev, struct sk_buff *skb,
				void *accel_priv,
				select_queue_fallback_t fallback)
{
	return (u16)smp_processor_id();
}

static void xlr_hw_set_mac_addr(struct net_device *ndev)
{
	struct xlr_net_priv *priv = netdev_priv(ndev);

	/* set mac station address */
	xlr_nae_wreg(priv->base_addr, R_MAC_ADDR0,
		     ((ndev->dev_addr[5] << 24) | (ndev->dev_addr[4] << 16) |
		     (ndev->dev_addr[3] << 8) | (ndev->dev_addr[2])));
	xlr_nae_wreg(priv->base_addr, R_MAC_ADDR0 + 1,
		     ((ndev->dev_addr[1] << 24) | (ndev->dev_addr[0] << 16)));

	xlr_nae_wreg(priv->base_addr, R_MAC_ADDR_MASK2, 0xffffffff);
	xlr_nae_wreg(priv->base_addr, R_MAC_ADDR_MASK2 + 1, 0xffffffff);
	xlr_nae_wreg(priv->base_addr, R_MAC_ADDR_MASK3, 0xffffffff);
	xlr_nae_wreg(priv->base_addr, R_MAC_ADDR_MASK3 + 1, 0xffffffff);

	xlr_nae_wreg(priv->base_addr, R_MAC_FILTER_CONFIG,
		     (1 << O_MAC_FILTER_CONFIG__BROADCAST_EN) |
		     (1 << O_MAC_FILTER_CONFIG__ALL_MCAST_EN) |
		     (1 << O_MAC_FILTER_CONFIG__MAC_ADDR0_VALID));

	if (priv->nd->phy_interface == PHY_INTERFACE_MODE_RGMII ||
	    priv->nd->phy_interface == PHY_INTERFACE_MODE_SGMII)
		xlr_reg_update(priv->base_addr, R_IPG_IFG, MAC_B2B_IPG, 0x7f);
}

static int xlr_net_set_mac_addr(struct net_device *ndev, void *data)
{
	int err;

	err = eth_mac_addr(ndev, data);
	if (err)
		return err;
	xlr_hw_set_mac_addr(ndev);
	return 0;
}

static void xlr_set_rx_mode(struct net_device *ndev)
{
	struct xlr_net_priv *priv = netdev_priv(ndev);
	u32 regval;

	regval = xlr_nae_rdreg(priv->base_addr, R_MAC_FILTER_CONFIG);

	if (ndev->flags & IFF_PROMISC) {
		regval |= (1 << O_MAC_FILTER_CONFIG__BROADCAST_EN) |
		(1 << O_MAC_FILTER_CONFIG__PAUSE_FRAME_EN) |
		(1 << O_MAC_FILTER_CONFIG__ALL_MCAST_EN) |
		(1 << O_MAC_FILTER_CONFIG__ALL_UCAST_EN);
	} else {
		regval &= ~((1 << O_MAC_FILTER_CONFIG__PAUSE_FRAME_EN) |
		(1 << O_MAC_FILTER_CONFIG__ALL_UCAST_EN));
	}

	xlr_nae_wreg(priv->base_addr, R_MAC_FILTER_CONFIG, regval);
}

static void xlr_stats(struct net_device *ndev, struct rtnl_link_stats64 *stats)
{
	struct xlr_net_priv *priv = netdev_priv(ndev);

	stats->rx_packets = xlr_nae_rdreg(priv->base_addr, RX_PACKET_COUNTER);
	stats->tx_packets = xlr_nae_rdreg(priv->base_addr, TX_PACKET_COUNTER);
	stats->rx_bytes = xlr_nae_rdreg(priv->base_addr, RX_BYTE_COUNTER);
	stats->tx_bytes = xlr_nae_rdreg(priv->base_addr, TX_BYTE_COUNTER);
	stats->tx_errors = xlr_nae_rdreg(priv->base_addr, TX_FCS_ERROR_COUNTER);
	stats->rx_dropped = xlr_nae_rdreg(priv->base_addr,
			RX_DROP_PACKET_COUNTER);
	stats->tx_dropped = xlr_nae_rdreg(priv->base_addr,
			TX_DROP_FRAME_COUNTER);

	stats->multicast = xlr_nae_rdreg(priv->base_addr,
			RX_MULTICAST_PACKET_COUNTER);
	stats->collisions = xlr_nae_rdreg(priv->base_addr,
			TX_TOTAL_COLLISION_COUNTER);

	stats->rx_length_errors = xlr_nae_rdreg(priv->base_addr,
			RX_FRAME_LENGTH_ERROR_COUNTER);
	stats->rx_over_errors = xlr_nae_rdreg(priv->base_addr,
			RX_DROP_PACKET_COUNTER);
	stats->rx_crc_errors = xlr_nae_rdreg(priv->base_addr,
			RX_FCS_ERROR_COUNTER);
	stats->rx_frame_errors = xlr_nae_rdreg(priv->base_addr,
			RX_ALIGNMENT_ERROR_COUNTER);

	stats->rx_fifo_errors = xlr_nae_rdreg(priv->base_addr,
			RX_DROP_PACKET_COUNTER);
	stats->rx_missed_errors = xlr_nae_rdreg(priv->base_addr,
			RX_CARRIER_SENSE_ERROR_COUNTER);

	stats->rx_errors = (stats->rx_over_errors + stats->rx_crc_errors +
			stats->rx_frame_errors + stats->rx_fifo_errors +
			stats->rx_missed_errors);

	stats->tx_aborted_errors = xlr_nae_rdreg(priv->base_addr,
			TX_EXCESSIVE_COLLISION_PACKET_COUNTER);
	stats->tx_carrier_errors = xlr_nae_rdreg(priv->base_addr,
			TX_DROP_FRAME_COUNTER);
	stats->tx_fifo_errors = xlr_nae_rdreg(priv->base_addr,
			TX_DROP_FRAME_COUNTER);
}

static struct rtnl_link_stats64 *xlr_get_stats64(struct net_device *ndev,
						 struct rtnl_link_stats64 *stats
						 )
{
	xlr_stats(ndev, stats);
	return stats;
}

static const struct net_device_ops xlr_netdev_ops = {
	.ndo_open = xlr_net_open,
	.ndo_stop = xlr_net_stop,
	.ndo_start_xmit = xlr_net_start_xmit,
	.ndo_select_queue = xlr_net_select_queue,
	.ndo_set_mac_address = xlr_net_set_mac_addr,
	.ndo_set_rx_mode = xlr_set_rx_mode,
	.ndo_get_stats64 = xlr_get_stats64,
};

/*
 * Gmac init
 */
static void *xlr_config_spill(struct xlr_net_priv *priv, int reg_start_0,
			      int reg_start_1, int reg_size, int size)
{
	void *spill;
	u32 *base;
	unsigned long phys_addr;
	u32 spill_size;

	base = priv->base_addr;
	spill_size = size;
	spill = kmalloc(spill_size + SMP_CACHE_BYTES, GFP_ATOMIC);
	if (!spill) {
		pr_err("Unable to allocate memory for spill area!\n");
		return ZERO_SIZE_PTR;
	}

	spill = PTR_ALIGN(spill, SMP_CACHE_BYTES);
	phys_addr = virt_to_phys(spill);
	dev_dbg(&priv->ndev->dev, "Allocated spill %d bytes at %lx\n",
		size, phys_addr);
	xlr_nae_wreg(base, reg_start_0, (phys_addr >> 5) & 0xffffffff);
	xlr_nae_wreg(base, reg_start_1, ((u64)phys_addr >> 37) & 0x07);
	xlr_nae_wreg(base, reg_size, spill_size);

	return spill;
}

/*
 * Configure the 6 FIFO's that are used by the network accelarator to
 * communicate with the rest of the XLx device. 4 of the FIFO's are for
 * packets from NA --> cpu (called Class FIFO's) and 2 are for feeding
 * the NA with free descriptors.
 */
static void xlr_config_fifo_spill_area(struct xlr_net_priv *priv)
{
	priv->frin_spill = xlr_config_spill(priv,
			R_REG_FRIN_SPILL_MEM_START_0,
			R_REG_FRIN_SPILL_MEM_START_1,
			R_REG_FRIN_SPILL_MEM_SIZE,
			MAX_FRIN_SPILL *
			sizeof(u64));
	priv->frout_spill = xlr_config_spill(priv,
			R_FROUT_SPILL_MEM_START_0,
			R_FROUT_SPILL_MEM_START_1,
			R_FROUT_SPILL_MEM_SIZE,
			MAX_FROUT_SPILL *
			sizeof(u64));
	priv->class_0_spill = xlr_config_spill(priv,
			R_CLASS0_SPILL_MEM_START_0,
			R_CLASS0_SPILL_MEM_START_1,
			R_CLASS0_SPILL_MEM_SIZE,
			MAX_CLASS_0_SPILL *
			sizeof(u64));
	priv->class_1_spill = xlr_config_spill(priv,
			R_CLASS1_SPILL_MEM_START_0,
			R_CLASS1_SPILL_MEM_START_1,
			R_CLASS1_SPILL_MEM_SIZE,
			MAX_CLASS_1_SPILL *
			sizeof(u64));
	priv->class_2_spill = xlr_config_spill(priv,
			R_CLASS2_SPILL_MEM_START_0,
			R_CLASS2_SPILL_MEM_START_1,
			R_CLASS2_SPILL_MEM_SIZE,
			MAX_CLASS_2_SPILL *
			sizeof(u64));
	priv->class_3_spill = xlr_config_spill(priv,
			R_CLASS3_SPILL_MEM_START_0,
			R_CLASS3_SPILL_MEM_START_1,
			R_CLASS3_SPILL_MEM_SIZE,
			MAX_CLASS_3_SPILL *
			sizeof(u64));
}

/*
 * Configure PDE to Round-Robin distribution of packets to the
 * available cpu
 */
static void xlr_config_pde(struct xlr_net_priv *priv)
{
	int i = 0;
	u64 bkt_map = 0;

	/* Each core has 8 buckets(station) */
	for (i = 0; i < hweight32(priv->nd->cpu_mask); i++)
		bkt_map |= (0xff << (i * 8));

	xlr_nae_wreg(priv->base_addr, R_PDE_CLASS_0, (bkt_map & 0xffffffff));
	xlr_nae_wreg(priv->base_addr, R_PDE_CLASS_0 + 1,
		     ((bkt_map >> 32) & 0xffffffff));

	xlr_nae_wreg(priv->base_addr, R_PDE_CLASS_1, (bkt_map & 0xffffffff));
	xlr_nae_wreg(priv->base_addr, R_PDE_CLASS_1 + 1,
		     ((bkt_map >> 32) & 0xffffffff));

	xlr_nae_wreg(priv->base_addr, R_PDE_CLASS_2, (bkt_map & 0xffffffff));
	xlr_nae_wreg(priv->base_addr, R_PDE_CLASS_2 + 1,
		     ((bkt_map >> 32) & 0xffffffff));

	xlr_nae_wreg(priv->base_addr, R_PDE_CLASS_3, (bkt_map & 0xffffffff));
	xlr_nae_wreg(priv->base_addr, R_PDE_CLASS_3 + 1,
		     ((bkt_map >> 32) & 0xffffffff));
}

/*
 * Setup the Message ring credits, bucket size and other
 * common configuration
 */
static int xlr_config_common(struct xlr_net_priv *priv)
{
	struct xlr_fmn_info *gmac = priv->nd->gmac_fmn_info;
	int start_stn_id = gmac->start_stn_id;
	int end_stn_id = gmac->end_stn_id;
	int *bucket_size = priv->nd->bucket_size;
	int i, j, err;

	/* Setting non-core MsgBktSize(0x321 - 0x325) */
	for (i = start_stn_id; i <= end_stn_id; i++) {
		xlr_nae_wreg(priv->base_addr,
			     R_GMAC_RFR0_BUCKET_SIZE + i - start_stn_id,
			     bucket_size[i]);
	}

	/*
	 * Setting non-core Credit counter register
	 * Distributing Gmac's credit to CPU's
	 */
	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++)
			xlr_nae_wreg(priv->base_addr,
				     (R_CC_CPU0_0 + (i * 8)) + j,
				     gmac->credit_config[(i * 8) + j]);
	}

	xlr_nae_wreg(priv->base_addr, R_MSG_TX_THRESHOLD, 3);
	xlr_nae_wreg(priv->base_addr, R_DMACR0, 0xffffffff);
	xlr_nae_wreg(priv->base_addr, R_DMACR1, 0xffffffff);
	xlr_nae_wreg(priv->base_addr, R_DMACR2, 0xffffffff);
	xlr_nae_wreg(priv->base_addr, R_DMACR3, 0xffffffff);
	xlr_nae_wreg(priv->base_addr, R_FREEQCARVE, 0);

	err = xlr_net_fill_rx_ring(priv->ndev);
	if (err)
		return err;
	nlm_register_fmn_handler(start_stn_id, end_stn_id, xlr_net_fmn_handler,
				 priv->adapter);
	return 0;
}

static void xlr_config_translate_table(struct xlr_net_priv *priv)
{
	u32 cpu_mask;
	u32 val;
	int bkts[32]; /* one bucket is assumed for each cpu */
	int b1, b2, c1, c2, i, j, k;
	int use_bkt;

	use_bkt = 0;
	cpu_mask = priv->nd->cpu_mask;

	pr_info("Using %s-based distribution\n",
		(use_bkt) ? "bucket" : "class");
	j = 0;
	for (i = 0; i < 32; i++) {
		if ((1 << i) & cpu_mask) {
			/* for each cpu, mark the 4+threadid bucket */
			bkts[j] = ((i / 4) * 8) + (i % 4);
			j++;
		}
	}

	/*configure the 128 * 9 Translation table to send to available buckets*/
	k = 0;
	c1 = 3;
	c2 = 0;
	for (i = 0; i < 64; i++) {
		/*
		 * On use_bkt set the b0, b1 are used, else
		 * the 4 classes are used, here implemented
		 * a logic to distribute the packets to the
		 * buckets equally or based on the class
		 */
		c1 = (c1 + 1) & 3;
		c2 = (c1 + 1) & 3;
		b1 = bkts[k];
		k = (k + 1) % j;
		b2 = bkts[k];
		k = (k + 1) % j;

		val = ((c1 << 23) | (b1 << 17) | (use_bkt << 16) |
				(c2 << 7) | (b2 << 1) | (use_bkt << 0));
		dev_dbg(&priv->ndev->dev, "Table[%d] b1=%d b2=%d c1=%d c2=%d\n",
			i, b1, b2, c1, c2);
		xlr_nae_wreg(priv->base_addr, R_TRANSLATETABLE + i, val);
		c1 = c2;
	}
}

static void xlr_config_parser(struct xlr_net_priv *priv)
{
	u32 val;

	/* Mark it as ETHERNET type */
	xlr_nae_wreg(priv->base_addr, R_L2TYPE_0, 0x01);

	/* Use 7bit CRChash for flow classification with 127 as CRC polynomial*/
	xlr_nae_wreg(priv->base_addr, R_PARSERCONFIGREG,
		     ((0x7f << 8) | (1 << 1)));

	/* configure the parser : L2 Type is configured in the bootloader */
	/* extract IP: src, dest protocol */
	xlr_nae_wreg(priv->base_addr, R_L3CTABLE,
		     (9 << 20) | (1 << 19) | (1 << 18) | (0x01 << 16) |
		     (0x0800 << 0));
	xlr_nae_wreg(priv->base_addr, R_L3CTABLE + 1,
		     (9 << 25) | (1 << 21) | (12 << 14) | (4 << 10) |
		     (16 << 4) | 4);

	/* Configure to extract SRC port and Dest port for TCP and UDP pkts */
	xlr_nae_wreg(priv->base_addr, R_L4CTABLE, 6);
	xlr_nae_wreg(priv->base_addr, R_L4CTABLE + 2, 17);
	val = ((0 << 21) | (2 << 17) | (2 << 11) | (2 << 7));
	xlr_nae_wreg(priv->base_addr, R_L4CTABLE + 1, val);
	xlr_nae_wreg(priv->base_addr, R_L4CTABLE + 3, val);

	xlr_config_translate_table(priv);
}

static int xlr_phy_write(u32 *base_addr, int phy_addr, int regnum, u16 val)
{
	unsigned long timeout, stoptime, checktime;
	int timedout;

	/* 100ms timeout*/
	timeout = msecs_to_jiffies(100);
	stoptime = jiffies + timeout;
	timedout = 0;

	xlr_nae_wreg(base_addr, R_MII_MGMT_ADDRESS, (phy_addr << 8) | regnum);

	/* Write the data which starts the write cycle */
	xlr_nae_wreg(base_addr, R_MII_MGMT_WRITE_DATA, (u32)val);

	/* poll for the read cycle to complete */
	while (!timedout) {
		checktime = jiffies;
		if (xlr_nae_rdreg(base_addr, R_MII_MGMT_INDICATORS) == 0)
			break;
		timedout = time_after(checktime, stoptime);
	}
	if (timedout) {
		pr_info("Phy device write err: device busy");
		return -EBUSY;
	}

	return 0;
}

static int xlr_phy_read(u32 *base_addr, int phy_addr, int regnum)
{
	unsigned long timeout, stoptime, checktime;
	int timedout;

	/* 100ms timeout*/
	timeout = msecs_to_jiffies(100);
	stoptime = jiffies + timeout;
	timedout = 0;

	/* setup the phy reg to be used */
	xlr_nae_wreg(base_addr, R_MII_MGMT_ADDRESS,
		     (phy_addr << 8) | (regnum << 0));

	/* Issue the read command */
	xlr_nae_wreg(base_addr, R_MII_MGMT_COMMAND,
		     (1 << O_MII_MGMT_COMMAND__rstat));

	/* poll for the read cycle to complete */
	while (!timedout) {
		checktime = jiffies;
		if (xlr_nae_rdreg(base_addr, R_MII_MGMT_INDICATORS) == 0)
			break;
		timedout = time_after(checktime, stoptime);
	}
	if (timedout) {
		pr_info("Phy device read err: device busy");
		return -EBUSY;
	}

	/* clear the read cycle */
	xlr_nae_wreg(base_addr, R_MII_MGMT_COMMAND, 0);

	/* Read the data */
	return xlr_nae_rdreg(base_addr, R_MII_MGMT_STATUS);
}

static int xlr_mii_write(struct mii_bus *bus, int phy_addr, int regnum, u16 val)
{
	struct xlr_net_priv *priv = bus->priv;
	int ret;

	ret = xlr_phy_write(priv->mii_addr, phy_addr, regnum, val);
	dev_dbg(&priv->ndev->dev, "mii_write phy %d : %d <- %x [%x]\n",
		phy_addr, regnum, val, ret);
	return ret;
}

static int xlr_mii_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	struct xlr_net_priv *priv = bus->priv;
	int ret;

	ret =  xlr_phy_read(priv->mii_addr, phy_addr, regnum);
	dev_dbg(&priv->ndev->dev, "mii_read phy %d : %d [%x]\n",
		phy_addr, regnum, ret);
	return ret;
}

/*
 * XLR ports are RGMII. XLS ports are SGMII mostly except the port0,
 * which can be configured either SGMII or RGMII, considered SGMII
 * by default, if board setup to RGMII the port_type need to set
 * accordingly.Serdes and PCS layer need to configured for SGMII
 */
static void xlr_sgmii_init(struct xlr_net_priv *priv)
{
	int phy;

	xlr_phy_write(priv->serdes_addr, 26, 0, 0x6DB0);
	xlr_phy_write(priv->serdes_addr, 26, 1, 0xFFFF);
	xlr_phy_write(priv->serdes_addr, 26, 2, 0xB6D0);
	xlr_phy_write(priv->serdes_addr, 26, 3, 0x00FF);
	xlr_phy_write(priv->serdes_addr, 26, 4, 0x0000);
	xlr_phy_write(priv->serdes_addr, 26, 5, 0x0000);
	xlr_phy_write(priv->serdes_addr, 26, 6, 0x0005);
	xlr_phy_write(priv->serdes_addr, 26, 7, 0x0001);
	xlr_phy_write(priv->serdes_addr, 26, 8, 0x0000);
	xlr_phy_write(priv->serdes_addr, 26, 9, 0x0000);
	xlr_phy_write(priv->serdes_addr, 26, 10, 0x0000);

	/* program  GPIO values for serdes init parameters */
	xlr_nae_wreg(priv->gpio_addr, 0x20, 0x7e6802);
	xlr_nae_wreg(priv->gpio_addr, 0x10, 0x7104);

	xlr_nae_wreg(priv->gpio_addr, 0x22, 0x7e6802);
	xlr_nae_wreg(priv->gpio_addr, 0x21, 0x7104);

	/* enable autoneg - more magic */
	phy = priv->phy_addr % 4 + 27;
	xlr_phy_write(priv->pcs_addr, phy, 0, 0x1000);
	xlr_phy_write(priv->pcs_addr, phy, 0, 0x0200);
}

void xlr_set_gmac_speed(struct xlr_net_priv *priv)
{
	struct phy_device *phydev = xlr_get_phydev(priv);
	int speed;

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII)
		xlr_sgmii_init(priv);

	if (phydev->speed != priv->phy_speed) {
		speed = phydev->speed;
		if (speed == SPEED_1000) {
			/* Set interface to Byte mode */
			xlr_nae_wreg(priv->base_addr, R_MAC_CONFIG_2, 0x7217);
			priv->phy_speed = speed;
		} else if (speed == SPEED_100 || speed == SPEED_10) {
			/* Set interface to Nibble mode */
			xlr_nae_wreg(priv->base_addr, R_MAC_CONFIG_2, 0x7117);
			priv->phy_speed = speed;
		}
		/* Set SGMII speed in Interface control reg */
		if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
			if (speed == SPEED_10)
				xlr_nae_wreg(priv->base_addr,
					     R_INTERFACE_CONTROL,
					     SGMII_SPEED_10);
			if (speed == SPEED_100)
				xlr_nae_wreg(priv->base_addr,
					     R_INTERFACE_CONTROL,
					     SGMII_SPEED_100);
			if (speed == SPEED_1000)
				xlr_nae_wreg(priv->base_addr,
					     R_INTERFACE_CONTROL,
					     SGMII_SPEED_1000);
		}
		if (speed == SPEED_10)
			xlr_nae_wreg(priv->base_addr, R_CORECONTROL, 0x2);
		if (speed == SPEED_100)
			xlr_nae_wreg(priv->base_addr, R_CORECONTROL, 0x1);
		if (speed == SPEED_1000)
			xlr_nae_wreg(priv->base_addr, R_CORECONTROL, 0x0);
	}
	pr_info("gmac%d : %dMbps\n", priv->port_id, priv->phy_speed);
}

static void xlr_gmac_link_adjust(struct net_device *ndev)
{
	struct xlr_net_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = xlr_get_phydev(priv);
	u32 intreg;

	intreg = xlr_nae_rdreg(priv->base_addr, R_INTREG);
	if (phydev->link) {
		if (phydev->speed != priv->phy_speed) {
			xlr_set_gmac_speed(priv);
			pr_info("gmac%d : Link up\n", priv->port_id);
		}
	} else {
		xlr_set_gmac_speed(priv);
		pr_info("gmac%d : Link down\n", priv->port_id);
	}
}

static int xlr_mii_probe(struct xlr_net_priv *priv)
{
	struct phy_device *phydev = xlr_get_phydev(priv);

	if (!phydev) {
		pr_err("no PHY found on phy_addr %d\n", priv->phy_addr);
		return -ENODEV;
	}

	/* Attach MAC to PHY */
	phydev = phy_connect(priv->ndev, phydev_name(phydev),
			     xlr_gmac_link_adjust, priv->nd->phy_interface);

	if (IS_ERR(phydev)) {
		pr_err("could not attach PHY\n");
		return PTR_ERR(phydev);
	}
	phydev->supported &= (ADVERTISED_10baseT_Full
				| ADVERTISED_10baseT_Half
				| ADVERTISED_100baseT_Full
				| ADVERTISED_100baseT_Half
				| ADVERTISED_1000baseT_Full
				| ADVERTISED_Autoneg
				| ADVERTISED_MII);

	phydev->advertising = phydev->supported;
	phy_attached_info(phydev);
	return 0;
}

static int xlr_setup_mdio(struct xlr_net_priv *priv,
			  struct platform_device *pdev)
{
	int err;

	priv->mii_bus = mdiobus_alloc();
	if (!priv->mii_bus) {
		pr_err("mdiobus alloc failed\n");
		return -ENOMEM;
	}

	priv->mii_bus->priv = priv;
	priv->mii_bus->name = "xlr-mdio";
	snprintf(priv->mii_bus->id, MII_BUS_ID_SIZE, "%s-%d",
		 priv->mii_bus->name, priv->port_id);
	priv->mii_bus->read = xlr_mii_read;
	priv->mii_bus->write = xlr_mii_write;
	priv->mii_bus->parent = &pdev->dev;

	/* Scan only the enabled address */
	priv->mii_bus->phy_mask = ~(1 << priv->phy_addr);

	/* setting clock divisor to 54 */
	xlr_nae_wreg(priv->base_addr, R_MII_MGMT_CONFIG, 0x7);

	err = mdiobus_register(priv->mii_bus);
	if (err) {
		mdiobus_free(priv->mii_bus);
		pr_err("mdio bus registration failed\n");
		return err;
	}

	pr_info("Registered mdio bus id : %s\n", priv->mii_bus->id);
	err = xlr_mii_probe(priv);
	if (err) {
		mdiobus_free(priv->mii_bus);
		return err;
	}
	return 0;
}

static void xlr_port_enable(struct xlr_net_priv *priv)
{
	u32 prid = (read_c0_prid() & 0xf000);

	/* Setup MAC_CONFIG reg if (xls & rgmii) */
	if ((prid == 0x8000 || prid == 0x4000 || prid == 0xc000) &&
	    priv->nd->phy_interface == PHY_INTERFACE_MODE_RGMII)
		xlr_reg_update(priv->base_addr, R_RX_CONTROL,
			       (1 << O_RX_CONTROL__RGMII),
			       (1 << O_RX_CONTROL__RGMII));

	/* Rx Tx enable */
	xlr_reg_update(priv->base_addr, R_MAC_CONFIG_1,
		       ((1 << O_MAC_CONFIG_1__rxen) |
			(1 << O_MAC_CONFIG_1__txen) |
			(1 << O_MAC_CONFIG_1__rxfc) |
			(1 << O_MAC_CONFIG_1__txfc)),
		       ((1 << O_MAC_CONFIG_1__rxen) |
			(1 << O_MAC_CONFIG_1__txen) |
			(1 << O_MAC_CONFIG_1__rxfc) |
			(1 << O_MAC_CONFIG_1__txfc)));

	/* Setup tx control reg */
	xlr_reg_update(priv->base_addr, R_TX_CONTROL,
		       ((1 << O_TX_CONTROL__TXENABLE) |
		       (512 << O_TX_CONTROL__TXTHRESHOLD)), 0x3fff);

	/* Setup rx control reg */
	xlr_reg_update(priv->base_addr, R_RX_CONTROL,
		       1 << O_RX_CONTROL__RXENABLE,
		       1 << O_RX_CONTROL__RXENABLE);
}

static void xlr_port_disable(struct xlr_net_priv *priv)
{
	/* Setup MAC_CONFIG reg */
	/* Rx Tx disable*/
	xlr_reg_update(priv->base_addr, R_MAC_CONFIG_1,
		       ((1 << O_MAC_CONFIG_1__rxen) |
			(1 << O_MAC_CONFIG_1__txen) |
			(1 << O_MAC_CONFIG_1__rxfc) |
			(1 << O_MAC_CONFIG_1__txfc)), 0x0);

	/* Setup tx control reg */
	xlr_reg_update(priv->base_addr, R_TX_CONTROL,
		       ((1 << O_TX_CONTROL__TXENABLE) |
		       (512 << O_TX_CONTROL__TXTHRESHOLD)), 0);

	/* Setup rx control reg */
	xlr_reg_update(priv->base_addr, R_RX_CONTROL,
		       1 << O_RX_CONTROL__RXENABLE, 0);
}

/*
 * Initialization of gmac
 */
static int xlr_gmac_init(struct xlr_net_priv *priv,
			 struct platform_device *pdev)
{
	int ret;

	pr_info("Initializing the gmac%d\n", priv->port_id);

	xlr_port_disable(priv);

	xlr_nae_wreg(priv->base_addr, R_DESC_PACK_CTRL,
		     (1 << O_DESC_PACK_CTRL__MAXENTRY) |
		     (BYTE_OFFSET << O_DESC_PACK_CTRL__BYTEOFFSET) |
		     (1600 << O_DESC_PACK_CTRL__REGULARSIZE));

	ret = xlr_setup_mdio(priv, pdev);
	if (ret)
		return ret;
	xlr_port_enable(priv);

	/* Enable Full-duplex/1000Mbps/CRC */
	xlr_nae_wreg(priv->base_addr, R_MAC_CONFIG_2, 0x7217);
	/* speed 2.5Mhz */
	xlr_nae_wreg(priv->base_addr, R_CORECONTROL, 0x02);
	/* Setup Interrupt mask reg */
	xlr_nae_wreg(priv->base_addr, R_INTMASK, (1 << O_INTMASK__TXILLEGAL) |
		     (1 << O_INTMASK__MDINT) | (1 << O_INTMASK__TXFETCHERROR) |
		     (1 << O_INTMASK__P2PSPILLECC) | (1 << O_INTMASK__TAGFULL) |
		     (1 << O_INTMASK__UNDERRUN) | (1 << O_INTMASK__ABORT));

	/* Clear all stats */
	xlr_reg_update(priv->base_addr, R_STATCTRL, 0, 1 << O_STATCTRL__CLRCNT);
	xlr_reg_update(priv->base_addr, R_STATCTRL, 1 << 2, 1 << 2);
	return 0;
}

static int xlr_net_probe(struct platform_device *pdev)
{
	struct xlr_net_priv *priv = NULL;
	struct net_device *ndev;
	struct resource *res;
	struct xlr_adapter *adapter;
	int err, port;

	pr_info("XLR/XLS Ethernet Driver controller %d\n", pdev->id);
	/*
	 * Allocate our adapter data structure and attach it to the device.
	 */
	adapter = (struct xlr_adapter *)
		devm_kzalloc(&pdev->dev, sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		err = -ENOMEM;
		return err;
	}

	/*
	 * XLR and XLS have 1 and 2 NAE controller respectively
	 * Each controller has 4 gmac ports, mapping each controller
	 * under one parent device, 4 gmac ports under one device.
	 */
	for (port = 0; port < pdev->num_resources / 2; port++) {
		ndev = alloc_etherdev_mq(sizeof(struct xlr_net_priv), 32);
		if (!ndev) {
			dev_err(&pdev->dev,
				"Allocation of Ethernet device failed\n");
			return -ENOMEM;
		}

		priv = netdev_priv(ndev);
		priv->pdev = pdev;
		priv->ndev = ndev;
		priv->port_id = (pdev->id * 4) + port;
		priv->nd = (struct xlr_net_data *)pdev->dev.platform_data;
		res = platform_get_resource(pdev, IORESOURCE_MEM, port);
		priv->base_addr = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(priv->base_addr)) {
			err = PTR_ERR(priv->base_addr);
			goto err_gmac;
		}
		priv->adapter = adapter;
		adapter->netdev[port] = ndev;

		res = platform_get_resource(pdev, IORESOURCE_IRQ, port);
		if (!res) {
			dev_err(&pdev->dev, "No irq resource for MAC %d\n",
				priv->port_id);
			err = -ENODEV;
			goto err_gmac;
		}

		ndev->irq = res->start;

		priv->phy_addr = priv->nd->phy_addr[port];
		priv->tx_stnid = priv->nd->tx_stnid[port];
		priv->mii_addr = priv->nd->mii_addr;
		priv->serdes_addr = priv->nd->serdes_addr;
		priv->pcs_addr = priv->nd->pcs_addr;
		priv->gpio_addr = priv->nd->gpio_addr;

		ndev->netdev_ops = &xlr_netdev_ops;
		ndev->watchdog_timeo = HZ;

		/* Setup Mac address and Rx mode */
		eth_hw_addr_random(ndev);
		xlr_hw_set_mac_addr(ndev);
		xlr_set_rx_mode(ndev);

		priv->num_rx_desc += MAX_NUM_DESC_SPILL;
		ndev->ethtool_ops = &xlr_ethtool_ops;
		SET_NETDEV_DEV(ndev, &pdev->dev);

		xlr_config_fifo_spill_area(priv);
		/* Configure PDE to Round-Robin pkt distribution */
		xlr_config_pde(priv);
		xlr_config_parser(priv);

		/* Call init with respect to port */
		if (strcmp(res->name, "gmac") == 0) {
			err = xlr_gmac_init(priv, pdev);
			if (err) {
				dev_err(&pdev->dev, "gmac%d init failed\n",
					priv->port_id);
				goto err_gmac;
			}
		}

		if (priv->port_id == 0 || priv->port_id == 4) {
			err = xlr_config_common(priv);
			if (err)
				goto err_netdev;
		}

		err = register_netdev(ndev);
		if (err) {
			dev_err(&pdev->dev,
				"Registering netdev failed for gmac%d\n",
				priv->port_id);
			goto err_netdev;
		}
		platform_set_drvdata(pdev, priv);
	}

	return 0;

err_netdev:
	mdiobus_free(priv->mii_bus);
err_gmac:
	free_netdev(ndev);
	return err;
}

static int xlr_net_remove(struct platform_device *pdev)
{
	struct xlr_net_priv *priv = platform_get_drvdata(pdev);

	unregister_netdev(priv->ndev);
	mdiobus_unregister(priv->mii_bus);
	mdiobus_free(priv->mii_bus);
	free_netdev(priv->ndev);
	return 0;
}

static struct platform_driver xlr_net_driver = {
	.probe		= xlr_net_probe,
	.remove		= xlr_net_remove,
	.driver		= {
		.name	= "xlr-net",
	},
};

module_platform_driver(xlr_net_driver);

MODULE_AUTHOR("Ganesan Ramalingam <ganesanr@broadcom.com>");
MODULE_DESCRIPTION("Ethernet driver for Netlogic XLR/XLS");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:xlr-net");
