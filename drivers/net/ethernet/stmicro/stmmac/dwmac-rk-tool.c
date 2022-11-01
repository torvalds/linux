// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2020 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/if.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/regmap.h>
#include <linux/phy.h>
#include <linux/udp.h>
#include <linux/skbuff.h>
#include <net/pkt_cls.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include "stmmac.h"
#include "dwmac1000.h"
#include "dwmac_dma.h"
#include "dwmac-rk-tool.h"

enum {
	LOOPBACK_TYPE_GMAC = 1,
	LOOPBACK_TYPE_PHY
};

enum {
	LOOPBACK_SPEED10 = 10,
	LOOPBACK_SPEED100 = 100,
	LOOPBACK_SPEED1000 = 1000
};

struct dwmac_rk_packet_attrs {
	unsigned char src[6];
	unsigned char dst[6];
	u32 ip_src;
	u32 ip_dst;
	int tcp;
	int sport;
	int dport;
	int size;
};

struct dwmac_rk_hdr {
	__be32 version;
	__be64 magic;
	u32 id;
	int tx;
	int rx;
} __packed;

struct dwmac_rk_lb_priv {
	/* desc && buffer */
	struct dma_desc *dma_tx;
	dma_addr_t dma_tx_phy;
	struct sk_buff *tx_skbuff;
	dma_addr_t tx_skbuff_dma;
	unsigned int tx_skbuff_dma_len;

	struct dma_desc *dma_rx ____cacheline_aligned_in_smp;
	dma_addr_t dma_rx_phy;
	struct sk_buff *rx_skbuff;
	dma_addr_t rx_skbuff_dma;
	u32 rx_tail_addr;
	u32 tx_tail_addr;

	/* rx buffer size */
	unsigned int dma_buf_sz;
	unsigned int buf_sz;

	int type;
	int speed;
	struct dwmac_rk_packet_attrs *packet;

	unsigned int actual_size;
	int scan;
	int sysfs;
	u32 id;
	int tx;
	int rx;
	int final_tx;
	int final_rx;
	int max_delay;
};

#define DMA_CONTROL_OSP		BIT(4)
#define DMA_CHAN_BASE_ADDR	0x00001100
#define DMA_CHAN_BASE_OFFSET	0x80
#define DMA_CHANX_BASE_ADDR(x)	(DMA_CHAN_BASE_ADDR + \
				((x) * DMA_CHAN_BASE_OFFSET))
#define DMA_CHAN_TX_CONTROL(x)	(DMA_CHANX_BASE_ADDR(x) + 0x4)
#define DMA_CHAN_STATUS(x)	(DMA_CHANX_BASE_ADDR(x) + 0x60)
#define DMA_CHAN_STATUS_ERI	BIT(11)
#define DMA_CHAN_STATUS_ETI	BIT(10)

#define	STMMAC_ALIGN(x) __ALIGN_KERNEL(x, SMP_CACHE_BYTES)
#define MAX_DELAYLINE 0x7f
#define RK3588_MAX_DELAYLINE 0xc7
#define SCAN_STEP 0x5
#define SCAN_VALID_RANGE 0xA

#define DWMAC_RK_TEST_PKT_SIZE (sizeof(struct ethhdr) + sizeof(struct iphdr) + \
				sizeof(struct dwmac_rk_hdr))
#define DWMAC_RK_TEST_PKT_MAGIC 0xdeadcafecafedeadULL
#define DWMAC_RK_TEST_PKT_MAX_SIZE 1500

static __maybe_unused struct dwmac_rk_packet_attrs dwmac_rk_udp_attr = {
	.dst = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	.tcp = 0,
	.size = 1024,
};

static __maybe_unused struct dwmac_rk_packet_attrs dwmac_rk_tcp_attr = {
	.dst = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	.tcp = 1,
	.size = 1024,
};

static int dwmac_rk_enable_mac_loopback(struct stmmac_priv *priv, int speed,
					int addr, bool phy)
{
	u32 ctrl;
	int phy_val;

	ctrl = readl(priv->ioaddr + GMAC_CONTROL);
	ctrl &= ~priv->hw->link.speed_mask;
	ctrl |= GMAC_CONTROL_LM;

	if (phy)
		phy_val = mdiobus_read(priv->mii, addr, MII_BMCR);

	switch (speed) {
	case LOOPBACK_SPEED1000:
		ctrl |= priv->hw->link.speed1000;
		if (phy) {
			phy_val &= ~BMCR_SPEED100;
			phy_val |= BMCR_SPEED1000;
		}
		break;
	case LOOPBACK_SPEED100:
		ctrl |= priv->hw->link.speed100;
		if (phy) {
			phy_val &= ~BMCR_SPEED1000;
			phy_val |= BMCR_SPEED100;
		}
		break;
	case LOOPBACK_SPEED10:
		ctrl |= priv->hw->link.speed10;
		if (phy) {
			phy_val &= ~BMCR_SPEED1000;
			phy_val &= ~BMCR_SPEED100;
		}
		break;
	default:
		return -EPERM;
	}

	ctrl |= priv->hw->link.duplex;
	writel(ctrl, priv->ioaddr + GMAC_CONTROL);

	if (phy) {
		phy_val &= ~BMCR_PDOWN;
		phy_val &= ~BMCR_ANENABLE;
		phy_val &= ~BMCR_PDOWN;
		phy_val |= BMCR_FULLDPLX;
		mdiobus_write(priv->mii, addr, MII_BMCR, phy_val);
		phy_val = mdiobus_read(priv->mii, addr, MII_BMCR);
	}

	if (likely(priv->plat->fix_mac_speed))
		priv->plat->fix_mac_speed(priv->plat->bsp_priv, speed);

	return 0;
}

static int dwmac_rk_disable_mac_loopback(struct stmmac_priv *priv, int addr)
{
	u32 ctrl;
	int phy_val;

	ctrl = readl(priv->ioaddr + GMAC_CONTROL);
	ctrl &= ~GMAC_CONTROL_LM;
	writel(ctrl, priv->ioaddr + GMAC_CONTROL);

	phy_val = mdiobus_read(priv->mii, addr, MII_BMCR);
	phy_val |= BMCR_ANENABLE;

	mdiobus_write(priv->mii, addr, MII_BMCR, phy_val);
	phy_val = mdiobus_read(priv->mii, addr, MII_BMCR);

	return 0;
}

static int dwmac_rk_set_mac_loopback(struct stmmac_priv *priv,
				     int speed, bool enable,
				     int addr, bool phy)
{
	if (enable)
		return dwmac_rk_enable_mac_loopback(priv, speed, addr, phy);
	else
		return dwmac_rk_disable_mac_loopback(priv, addr);
}

static int dwmac_rk_enable_phy_loopback(struct stmmac_priv *priv, int speed,
					int addr, bool phy)
{
	u32 ctrl;
	int val;

	ctrl = readl(priv->ioaddr + MAC_CTRL_REG);
	ctrl &= ~priv->hw->link.speed_mask;

	if (phy)
		val = mdiobus_read(priv->mii, addr, MII_BMCR);

	switch (speed) {
	case LOOPBACK_SPEED1000:
		ctrl |= priv->hw->link.speed1000;
		if (phy) {
			val &= ~BMCR_SPEED100;
			val |= BMCR_SPEED1000;
		}
		break;
	case LOOPBACK_SPEED100:
		ctrl |= priv->hw->link.speed100;
		if (phy) {
			val &= ~BMCR_SPEED1000;
			val |= BMCR_SPEED100;
		}
		break;
	case LOOPBACK_SPEED10:
		ctrl |= priv->hw->link.speed10;
		if (phy) {
			val &= ~BMCR_SPEED1000;
			val &= ~BMCR_SPEED100;
		}
		break;
	default:
		return -EPERM;
	}

	ctrl |= priv->hw->link.duplex;
	writel(ctrl, priv->ioaddr + MAC_CTRL_REG);

	if (phy) {
		val |= BMCR_FULLDPLX;
		val &= ~BMCR_PDOWN;
		val &= ~BMCR_ANENABLE;
		val |= BMCR_LOOPBACK;
		mdiobus_write(priv->mii, addr, MII_BMCR, val);
		val = mdiobus_read(priv->mii, addr, MII_BMCR);
	}

	if (likely(priv->plat->fix_mac_speed))
		priv->plat->fix_mac_speed(priv->plat->bsp_priv, speed);

	return 0;
}

static int dwmac_rk_disable_phy_loopback(struct stmmac_priv *priv, int addr)
{
	int val;

	val = mdiobus_read(priv->mii, addr, MII_BMCR);
	val |= BMCR_ANENABLE;
	val &= ~BMCR_LOOPBACK;

	mdiobus_write(priv->mii, addr, MII_BMCR, val);
	val = mdiobus_read(priv->mii, addr, MII_BMCR);

	return 0;
}

static int dwmac_rk_set_phy_loopback(struct stmmac_priv *priv,
				     int speed, bool enable,
				     int addr, bool phy)
{
	if (enable)
		return dwmac_rk_enable_phy_loopback(priv, speed,
						     addr, phy);
	else
		return dwmac_rk_disable_phy_loopback(priv, addr);
}

static int dwmac_rk_set_loopback(struct stmmac_priv *priv,
				 int type, int speed, bool enable,
				 int addr, bool phy)
{
	int ret;

	switch (type) {
	case LOOPBACK_TYPE_PHY:
		ret = dwmac_rk_set_phy_loopback(priv, speed, enable, addr, phy);
		break;
	case LOOPBACK_TYPE_GMAC:
		ret = dwmac_rk_set_mac_loopback(priv, speed, enable, addr, phy);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	usleep_range(100000, 200000);
	return ret;
}

static inline void dwmac_rk_ether_addr_copy(u8 *dst, const u8 *src)
{
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
}

static void dwmac_rk_udp4_hwcsum(struct sk_buff *skb, __be32 src, __be32 dst)
{
	struct udphdr *uh = udp_hdr(skb);
	int offset = skb_transport_offset(skb);
	int len = skb->len - offset;

	skb->csum_start = skb_transport_header(skb) - skb->head;
	skb->csum_offset = offsetof(struct udphdr, check);
	uh->check = ~csum_tcpudp_magic(src, dst, len,
				       IPPROTO_UDP, 0);
}

static struct sk_buff *dwmac_rk_get_skb(struct stmmac_priv *priv,
					struct dwmac_rk_lb_priv *lb_priv)
{
	struct sk_buff *skb = NULL;
	struct udphdr *uhdr = NULL;
	struct tcphdr *thdr = NULL;
	struct dwmac_rk_hdr *shdr;
	struct ethhdr *ehdr;
	struct iphdr *ihdr;
	struct dwmac_rk_packet_attrs *attr;
	int iplen, size, nfrags;

	attr = lb_priv->packet;
	size = attr->size + DWMAC_RK_TEST_PKT_SIZE;
	if (attr->tcp)
		size += sizeof(struct tcphdr);
	else
		size += sizeof(struct udphdr);

	if (size >= DWMAC_RK_TEST_PKT_MAX_SIZE)
		return NULL;

	lb_priv->actual_size = size;

	skb = netdev_alloc_skb_ip_align(priv->dev, size);
	if (!skb)
		return NULL;

	skb_linearize(skb);
	nfrags = skb_shinfo(skb)->nr_frags;
	if (nfrags > 0) {
		pr_err("%s: TX nfrags is not zero\n", __func__);
		dev_kfree_skb(skb);
		return NULL;
	}

	ehdr = (struct ethhdr *)skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);

	skb_set_network_header(skb, skb->len);
	ihdr = (struct iphdr *)skb_put(skb, sizeof(*ihdr));

	skb_set_transport_header(skb, skb->len);
	if (attr->tcp)
		thdr = (struct tcphdr *)skb_put(skb, sizeof(*thdr));
	else
		uhdr = (struct udphdr *)skb_put(skb, sizeof(*uhdr));

	eth_zero_addr(ehdr->h_source);
	eth_zero_addr(ehdr->h_dest);

	dwmac_rk_ether_addr_copy(ehdr->h_source, priv->dev->dev_addr);
	dwmac_rk_ether_addr_copy(ehdr->h_dest, attr->dst);

	ehdr->h_proto = htons(ETH_P_IP);

	if (attr->tcp) {
		if (!thdr) {
			dev_kfree_skb(skb);
			return NULL;
		}

		thdr->source = htons(attr->sport);
		thdr->dest = htons(attr->dport);
		thdr->doff = sizeof(struct tcphdr) / 4;
		thdr->check = 0;
	} else {
		if (!uhdr) {
			dev_kfree_skb(skb);
			return NULL;
		}

		uhdr->source = htons(attr->sport);
		uhdr->dest = htons(attr->dport);
		uhdr->len = htons(sizeof(*shdr) + sizeof(*uhdr) + attr->size);
		uhdr->check = 0;
	}

	ihdr->ihl = 5;
	ihdr->ttl = 32;
	ihdr->version = 4;
	if (attr->tcp)
		ihdr->protocol = IPPROTO_TCP;
	else
		ihdr->protocol = IPPROTO_UDP;

	iplen = sizeof(*ihdr) + sizeof(*shdr) + attr->size;
	if (attr->tcp)
		iplen += sizeof(*thdr);
	else
		iplen += sizeof(*uhdr);

	ihdr->tot_len = htons(iplen);
	ihdr->frag_off = 0;
	ihdr->saddr = htonl(attr->ip_src);
	ihdr->daddr = htonl(attr->ip_dst);
	ihdr->tos = 0;
	ihdr->id = 0;
	ip_send_check(ihdr);

	shdr = (struct dwmac_rk_hdr *)skb_put(skb, sizeof(*shdr));
	shdr->version = 0;
	shdr->magic = cpu_to_be64(DWMAC_RK_TEST_PKT_MAGIC);
	shdr->id = lb_priv->id;
	shdr->tx = lb_priv->tx;
	shdr->rx = lb_priv->rx;

	if (attr->size) {
		skb_put(skb, attr->size);
		get_random_bytes((u8 *)shdr + sizeof(*shdr), attr->size);
	}

	skb->csum = 0;
	skb->ip_summed = CHECKSUM_PARTIAL;
	if (attr->tcp) {
		if (!thdr) {
			dev_kfree_skb(skb);
			return NULL;
		}

		thdr->check = ~tcp_v4_check(skb->len, ihdr->saddr,
					    ihdr->daddr, 0);
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct tcphdr, check);
	} else {
		dwmac_rk_udp4_hwcsum(skb, ihdr->saddr, ihdr->daddr);
	}

	skb->protocol = htons(ETH_P_IP);
	skb->pkt_type = PACKET_HOST;

	return skb;
}

static int dwmac_rk_loopback_validate(struct stmmac_priv *priv,
				      struct dwmac_rk_lb_priv *lb_priv,
				      struct sk_buff *skb)
{
	struct dwmac_rk_hdr *shdr;
	struct ethhdr *ehdr;
	struct udphdr *uhdr;
	struct tcphdr *thdr;
	struct iphdr *ihdr;
	int ret = -EAGAIN;

	if (skb->len >= DWMAC_RK_TEST_PKT_MAX_SIZE)
		goto out;

	if (lb_priv->actual_size != skb->len)
		goto out;

	ehdr = (struct ethhdr *)(skb->data);
	if (!ether_addr_equal(ehdr->h_dest, lb_priv->packet->dst))
		goto out;

	if (!ether_addr_equal(ehdr->h_source, priv->dev->dev_addr))
		goto out;

	ihdr = (struct iphdr *)(skb->data + ETH_HLEN);

	if (lb_priv->packet->tcp) {
		if (ihdr->protocol != IPPROTO_TCP)
			goto out;

		thdr = (struct tcphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
		if (thdr->dest != htons(lb_priv->packet->dport))
			goto out;

		shdr = (struct dwmac_rk_hdr *)((u8 *)thdr + sizeof(*thdr));
	} else {
		if (ihdr->protocol != IPPROTO_UDP)
			goto out;

		uhdr = (struct udphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
		if (uhdr->dest != htons(lb_priv->packet->dport))
			goto out;

		shdr = (struct dwmac_rk_hdr *)((u8 *)uhdr + sizeof(*uhdr));
	}

	if (shdr->magic != cpu_to_be64(DWMAC_RK_TEST_PKT_MAGIC))
		goto out;

	if (lb_priv->id != shdr->id)
		goto out;

	if (lb_priv->tx != shdr->tx || lb_priv->rx != shdr->rx)
		goto out;

	ret = 0;
out:
	return ret;
}

static inline int dwmac_rk_rx_fill(struct stmmac_priv *priv,
				   struct dwmac_rk_lb_priv *lb_priv)
{
	struct dma_desc *p;
	struct sk_buff *skb;

	p = lb_priv->dma_rx;
	if (likely(!lb_priv->rx_skbuff)) {
		skb = netdev_alloc_skb_ip_align(priv->dev, lb_priv->buf_sz);
		if (unlikely(!skb))
			return -ENOMEM;

		if (skb_linearize(skb)) {
			pr_err("%s: Rx skb linearize failed\n", __func__);
			lb_priv->rx_skbuff = NULL;
			dev_kfree_skb(skb);
			return -EPERM;
		}

		lb_priv->rx_skbuff = skb;
		lb_priv->rx_skbuff_dma =
		    dma_map_single(priv->device, skb->data, lb_priv->dma_buf_sz,
				   DMA_FROM_DEVICE);
		if (dma_mapping_error(priv->device,
				      lb_priv->rx_skbuff_dma)) {
			pr_err("%s: Rx dma map failed\n", __func__);
			lb_priv->rx_skbuff = NULL;
			dev_kfree_skb(skb);
			return -EFAULT;
		}

		stmmac_set_desc_addr(priv, p, lb_priv->rx_skbuff_dma);
		/* Fill DES3 in case of RING mode */
		if (lb_priv->dma_buf_sz == BUF_SIZE_16KiB)
			p->des3 = cpu_to_le32(le32_to_cpu(p->des2) + BUF_SIZE_8KiB);
	}

	wmb();
	stmmac_set_rx_owner(priv, p, priv->use_riwt);
	wmb();

	stmmac_set_rx_tail_ptr(priv, priv->ioaddr, lb_priv->rx_tail_addr, 0);

	return 0;
}

static void dwmac_rk_rx_clean(struct stmmac_priv *priv,
			      struct dwmac_rk_lb_priv *lb_priv)
{
	if (likely(lb_priv->rx_skbuff_dma)) {
		dma_unmap_single(priv->device,
				 lb_priv->rx_skbuff_dma,
				 lb_priv->dma_buf_sz, DMA_FROM_DEVICE);
		lb_priv->rx_skbuff_dma = 0;
	}

	if (likely(lb_priv->rx_skbuff)) {
		dev_consume_skb_any(lb_priv->rx_skbuff);
		lb_priv->rx_skbuff = NULL;
	}
}

static int dwmac_rk_rx_validate(struct stmmac_priv *priv,
				struct dwmac_rk_lb_priv *lb_priv)
{
	struct dma_desc *p;
	struct sk_buff *skb;
	int coe = priv->hw->rx_csum;
	unsigned int frame_len;

	p = lb_priv->dma_rx;
	skb = lb_priv->rx_skbuff;
	if (unlikely(!skb)) {
		pr_err("%s: Inconsistent Rx descriptor chain\n",
		       __func__);
		return -EINVAL;
	}

	frame_len = priv->hw->desc->get_rx_frame_len(p, coe);
	/*  check if frame_len fits the preallocated memory */
	if (frame_len > lb_priv->dma_buf_sz) {
		pr_err("%s: frame_len long: %d\n", __func__, frame_len);
		return -ENOMEM;
	}

	frame_len -= ETH_FCS_LEN;
	prefetch(skb->data - NET_IP_ALIGN);
	skb_put(skb, frame_len);
	dma_unmap_single(priv->device,
			 lb_priv->rx_skbuff_dma,
			 lb_priv->dma_buf_sz,
			 DMA_FROM_DEVICE);

	return dwmac_rk_loopback_validate(priv, lb_priv, skb);
}

static int dwmac_rk_get_desc_status(struct stmmac_priv *priv,
				    struct dwmac_rk_lb_priv *lb_priv)
{
	struct dma_desc *txp, *rxp;
	int tx_status, rx_status;

	txp = lb_priv->dma_tx;
	tx_status = priv->hw->desc->tx_status(&priv->dev->stats,
					      &priv->xstats, txp,
					      priv->ioaddr);
	/* Check if the descriptor is owned by the DMA */
	if (unlikely(tx_status & tx_dma_own))
		return -EBUSY;

	rxp = lb_priv->dma_rx;
	/* read the status of the incoming frame */
	rx_status = priv->hw->desc->rx_status(&priv->dev->stats,
					      &priv->xstats, rxp);
	if (unlikely(rx_status & dma_own))
		return -EBUSY;

	usleep_range(100, 150);

	return 0;
}

static void dwmac_rk_tx_clean(struct stmmac_priv *priv,
			      struct dwmac_rk_lb_priv *lb_priv)
{
	struct sk_buff *skb = lb_priv->tx_skbuff;
	struct dma_desc *p;

	p = lb_priv->dma_tx;

	if (likely(lb_priv->tx_skbuff_dma)) {
		dma_unmap_single(priv->device,
				 lb_priv->tx_skbuff_dma,
				 lb_priv->tx_skbuff_dma_len,
				 DMA_TO_DEVICE);
		lb_priv->tx_skbuff_dma = 0;
	}

	if (likely(skb)) {
		dev_consume_skb_any(skb);
		lb_priv->tx_skbuff = NULL;
	}

	priv->hw->desc->release_tx_desc(p, priv->mode);
}

static int dwmac_rk_xmit(struct sk_buff *skb, struct net_device *dev,
			 struct dwmac_rk_lb_priv *lb_priv)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	unsigned int nopaged_len = skb_headlen(skb);
	int csum_insertion = 0;
	struct dma_desc *desc;
	unsigned int des;

	priv->hw->mac->reset_eee_mode(priv->hw);

	csum_insertion = (skb->ip_summed == CHECKSUM_PARTIAL);

	desc = lb_priv->dma_tx;
	lb_priv->tx_skbuff = skb;

	des = dma_map_single(priv->device, skb->data,
			     nopaged_len, DMA_TO_DEVICE);
	if (dma_mapping_error(priv->device, des))
		goto dma_map_err;
	lb_priv->tx_skbuff_dma = des;

	stmmac_set_desc_addr(priv, desc, des);
	lb_priv->tx_skbuff_dma_len = nopaged_len;

	/* Prepare the first descriptor setting the OWN bit too */
	stmmac_prepare_tx_desc(priv, desc, 1, nopaged_len,
			       csum_insertion, priv->mode, 1, 1,
			       skb->len);
	stmmac_enable_dma_transmission(priv, priv->ioaddr);

	lb_priv->tx_tail_addr = lb_priv->dma_tx_phy + sizeof(*desc);
	stmmac_set_tx_tail_ptr(priv, priv->ioaddr, lb_priv->tx_tail_addr, 0);

	return 0;

dma_map_err:
	pr_err("%s: Tx dma map failed\n", __func__);
	dev_kfree_skb(skb);
	return -EFAULT;
}

static int __dwmac_rk_loopback_run(struct stmmac_priv *priv,
				   struct dwmac_rk_lb_priv *lb_priv)
{
	u32 rx_channels_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	u32 tx_channels_count = min_t(u32, priv->plat->tx_queues_to_use, 1);
	struct sk_buff *tx_skb;
	u32 chan = 0;
	int ret = -EIO, delay;
	u32 status;
	bool finish = false;

	if (lb_priv->speed == LOOPBACK_SPEED1000)
		delay = 10;
	else if (lb_priv->speed == LOOPBACK_SPEED100)
		delay = 20;
	else if (lb_priv->speed == LOOPBACK_SPEED10)
		delay = 50;
	else
		return -EPERM;

	if (dwmac_rk_rx_fill(priv, lb_priv))
		return -ENOMEM;

	/* Enable the MAC Rx/Tx */
	stmmac_mac_set(priv, priv->ioaddr, true);

	for (chan = 0; chan < rx_channels_count; chan++)
		stmmac_start_rx(priv, priv->ioaddr, chan);
	for (chan = 0; chan < tx_channels_count; chan++)
		stmmac_start_tx(priv, priv->ioaddr, chan);

	tx_skb = dwmac_rk_get_skb(priv, lb_priv);
	if (!tx_skb) {
		ret = -ENOMEM;
		goto stop;
	}

	if (dwmac_rk_xmit(tx_skb, priv->dev, lb_priv)) {
		ret = -EFAULT;
		goto stop;
	}

	do {
		usleep_range(100, 150);
		delay--;
		if (priv->plat->has_gmac4) {
			status = readl(priv->ioaddr + DMA_CHAN_STATUS(0));
			finish = (status & DMA_CHAN_STATUS_ERI) && (status & DMA_CHAN_STATUS_ETI);
		} else {
			status = readl(priv->ioaddr + DMA_STATUS);
			finish = (status & DMA_STATUS_ERI) && (status & DMA_STATUS_ETI);
		}

		if (finish) {
			if (!dwmac_rk_get_desc_status(priv, lb_priv)) {
				ret = dwmac_rk_rx_validate(priv, lb_priv);
				break;
			}
		}
	} while (delay <= 0);
	writel((status & 0x1ffff), priv->ioaddr + DMA_STATUS);

stop:
	for (chan = 0; chan < rx_channels_count; chan++)
		stmmac_stop_rx(priv, priv->ioaddr, chan);
	for (chan = 0; chan < tx_channels_count; chan++)
		stmmac_stop_tx(priv, priv->ioaddr, chan);

	stmmac_mac_set(priv, priv->ioaddr, false);
	/* wait for state machine is disabled */
	usleep_range(100, 150);

	dwmac_rk_tx_clean(priv, lb_priv);
	dwmac_rk_rx_clean(priv, lb_priv);

	return ret;
}

static int dwmac_rk_loopback_with_identify(struct stmmac_priv *priv,
					   struct dwmac_rk_lb_priv *lb_priv,
					   int tx, int rx)
{
	lb_priv->id++;
	lb_priv->tx = tx;
	lb_priv->rx = rx;

	lb_priv->packet = &dwmac_rk_tcp_attr;
	dwmac_rk_set_rgmii_delayline(priv, tx, rx);

	return __dwmac_rk_loopback_run(priv, lb_priv);
}

static inline bool dwmac_rk_delayline_is_txvalid(struct dwmac_rk_lb_priv *lb_priv,
						 int tx)
{
	if (tx > 0 && tx < lb_priv->max_delay)
		return true;
	else
		return false;
}

static inline bool dwmac_rk_delayline_is_valid(struct dwmac_rk_lb_priv *lb_priv,
					       int tx, int rx)
{
	if ((tx > 0 && tx < lb_priv->max_delay) &&
	    (rx > 0 && rx < lb_priv->max_delay))
		return true;
	else
		return false;
}

static int dwmac_rk_delayline_scan_cross(struct stmmac_priv *priv,
					 struct dwmac_rk_lb_priv *lb_priv)
{
	int tx_left, tx_right, rx_up, rx_down;
	int i, j, tx_index, rx_index;
	int tx_mid = 0, rx_mid = 0;

	/* initiation */
	tx_index = SCAN_STEP;
	rx_index = SCAN_STEP;

re_scan:
	/* start from rx based on the experience */
	for (i = rx_index; i <= (lb_priv->max_delay - SCAN_STEP); i += SCAN_STEP) {
		tx_left = 0;
		tx_right = 0;
		tx_mid = 0;

		for (j = tx_index; j <= (lb_priv->max_delay - SCAN_STEP);
		     j += SCAN_STEP) {
			if (!dwmac_rk_loopback_with_identify(priv,
			    lb_priv, j, i)) {
				if (!tx_left)
					tx_left = j;
				tx_right = j;
			}
		}

		/* look for tx_mid */
		if ((tx_right - tx_left) > SCAN_VALID_RANGE) {
			tx_mid = (tx_right + tx_left) / 2;
			break;
		}
	}

	/* Worst case: reach the end */
	if (i >= (lb_priv->max_delay - SCAN_STEP))
		goto end;

	rx_up = 0;
	rx_down = 0;

	/* look for rx_mid base on the tx_mid */
	for (i = SCAN_STEP; i <= (lb_priv->max_delay - SCAN_STEP);
	     i += SCAN_STEP) {
		if (!dwmac_rk_loopback_with_identify(priv, lb_priv,
		    tx_mid, i)) {
			if (!rx_up)
				rx_up = i;
			rx_down = i;
		}
	}

	if ((rx_down - rx_up) > SCAN_VALID_RANGE) {
		/* Now get the rx_mid */
		rx_mid = (rx_up + rx_down) / 2;
	} else {
		rx_index += SCAN_STEP;
		rx_mid = 0;
		goto re_scan;
	}

	if (dwmac_rk_delayline_is_valid(lb_priv, tx_mid, rx_mid)) {
		lb_priv->final_tx = tx_mid;
		lb_priv->final_rx = rx_mid;

		pr_info("Find available tx_delay = 0x%02x, rx_delay = 0x%02x\n",
			lb_priv->final_tx, lb_priv->final_rx);

		return 0;
	}
end:
	pr_err("Can't find available delayline\n");
	return -ENXIO;
}

static int dwmac_rk_delayline_scan(struct stmmac_priv *priv,
				   struct dwmac_rk_lb_priv *lb_priv)
{
	int phy_iface = dwmac_rk_get_phy_interface(priv);
	int tx, rx, tx_sum, rx_sum, count;
	int tx_mid, rx_mid;
	int ret = -ENXIO;

	tx_sum = 0;
	rx_sum = 0;
	count = 0;

	for (rx = 0x0; rx <= lb_priv->max_delay; rx++) {
		if (phy_iface == PHY_INTERFACE_MODE_RGMII_RXID)
			rx = -1;
		printk(KERN_CONT "RX(%03d):", rx);
		for (tx = 0x0; tx <= lb_priv->max_delay; tx++) {
			if (!dwmac_rk_loopback_with_identify(priv,
			    lb_priv, tx, rx)) {
				tx_sum += tx;
				rx_sum += rx;
				count++;
				printk(KERN_CONT "O");
			} else {
				printk(KERN_CONT " ");
			}
		}
		printk(KERN_CONT "\n");

		if (phy_iface == PHY_INTERFACE_MODE_RGMII_RXID)
			break;
	}

	if (tx_sum && rx_sum && count) {
		tx_mid = tx_sum / count;
		rx_mid = rx_sum / count;

		if (phy_iface == PHY_INTERFACE_MODE_RGMII_RXID) {
			if (dwmac_rk_delayline_is_txvalid(lb_priv, tx_mid)) {
				lb_priv->final_tx = tx_mid;
				lb_priv->final_rx = -1;
				ret = 0;
			}
		} else {
			if (dwmac_rk_delayline_is_valid(lb_priv, tx_mid, rx_mid)) {
				lb_priv->final_tx = tx_mid;
				lb_priv->final_rx = rx_mid;
				ret = 0;
			}
		}
	}

	if (ret) {
		pr_err("\nCan't find suitable delayline\n");
	} else {
		if (phy_iface == PHY_INTERFACE_MODE_RGMII_RXID)
			pr_info("Find available tx_delay = 0x%02x, rx_delay = disable\n",
				lb_priv->final_tx);
		else
			pr_info("\nFind suitable tx_delay = 0x%02x, rx_delay = 0x%02x\n",
				lb_priv->final_tx, lb_priv->final_rx);
	}

	return ret;
}

static int dwmac_rk_loopback_delayline_scan(struct stmmac_priv *priv,
					    struct dwmac_rk_lb_priv *lb_priv)
{
	if (lb_priv->sysfs)
		return dwmac_rk_delayline_scan(priv, lb_priv);
	else
		return dwmac_rk_delayline_scan_cross(priv, lb_priv);
}

static void dwmac_rk_dma_free_rx_skbufs(struct stmmac_priv *priv,
					struct dwmac_rk_lb_priv *lb_priv)
{
	if (lb_priv->rx_skbuff) {
		dma_unmap_single(priv->device, lb_priv->rx_skbuff_dma,
				 lb_priv->dma_buf_sz, DMA_FROM_DEVICE);
		dev_kfree_skb_any(lb_priv->rx_skbuff);
	}
	lb_priv->rx_skbuff = NULL;
}

static void dwmac_rk_dma_free_tx_skbufs(struct stmmac_priv *priv,
					struct dwmac_rk_lb_priv *lb_priv)
{
	if (lb_priv->tx_skbuff_dma) {
		dma_unmap_single(priv->device,
				 lb_priv->tx_skbuff_dma,
				 lb_priv->tx_skbuff_dma_len,
				 DMA_TO_DEVICE);
	}

	if (lb_priv->tx_skbuff) {
		dev_kfree_skb_any(lb_priv->tx_skbuff);
		lb_priv->tx_skbuff = NULL;
		lb_priv->tx_skbuff_dma = 0;
	}
}

static int dwmac_rk_init_dma_desc_rings(struct net_device *dev, gfp_t flags,
					struct dwmac_rk_lb_priv *lb_priv)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct dma_desc *p;

	p = lb_priv->dma_tx;
	p->des2 = 0;
	lb_priv->tx_skbuff_dma = 0;
	lb_priv->tx_skbuff_dma_len = 0;
	lb_priv->tx_skbuff = NULL;

	lb_priv->rx_skbuff = NULL;
	stmmac_init_rx_desc(priv, lb_priv->dma_rx,
				     priv->use_riwt, priv->mode,
				     true, lb_priv->dma_buf_sz);

	stmmac_init_tx_desc(priv, lb_priv->dma_tx,
				     priv->mode,
				     true);

	return 0;
}

static int dwmac_rk_alloc_dma_desc_resources(struct stmmac_priv *priv,
					     struct dwmac_rk_lb_priv *lb_priv)
{
	int ret = -ENOMEM;

	/* desc dma map */
	lb_priv->dma_rx = dma_alloc_coherent(priv->device,
					     sizeof(struct dma_desc),
					     &lb_priv->dma_rx_phy,
					     GFP_KERNEL);
	if (!lb_priv->dma_rx)
		return ret;

	lb_priv->dma_tx = dma_alloc_coherent(priv->device,
					     sizeof(struct dma_desc),
					     &lb_priv->dma_tx_phy,
					     GFP_KERNEL);
	if (!lb_priv->dma_tx) {
		dma_free_coherent(priv->device,
				  sizeof(struct dma_desc),
				  lb_priv->dma_rx, lb_priv->dma_rx_phy);
		return ret;
	}

	return 0;
}

static void dwmac_rk_free_dma_desc_resources(struct stmmac_priv *priv,
					     struct dwmac_rk_lb_priv *lb_priv)
{
	/* Release the DMA TX/RX socket buffers */
	dwmac_rk_dma_free_rx_skbufs(priv, lb_priv);
	dwmac_rk_dma_free_tx_skbufs(priv, lb_priv);

	dma_free_coherent(priv->device, sizeof(struct dma_desc),
			  lb_priv->dma_tx, lb_priv->dma_tx_phy);
	dma_free_coherent(priv->device, sizeof(struct dma_desc),
			  lb_priv->dma_rx, lb_priv->dma_rx_phy);
}

static int dwmac_rk_init_dma_engine(struct stmmac_priv *priv,
				    struct dwmac_rk_lb_priv *lb_priv)
{
	u32 rx_channels_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	u32 tx_channels_count = min_t(u32, priv->plat->tx_queues_to_use, 1);
	u32 dma_csr_ch = max(rx_channels_count, tx_channels_count);
	u32 chan = 0;
	int ret = 0;

	ret = stmmac_reset(priv, priv->ioaddr);
	if (ret) {
		dev_err(priv->device, "Failed to reset the dma\n");
		return ret;
	}

	/* DMA Configuration */
	stmmac_dma_init(priv, priv->ioaddr, priv->plat->dma_cfg, 0);

	if (priv->plat->axi)
		stmmac_axi(priv, priv->ioaddr, priv->plat->axi);

	for (chan = 0; chan < dma_csr_ch; chan++)
		stmmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, 0);

	/* DMA RX Channel Configuration */
	for (chan = 0; chan < rx_channels_count; chan++) {
		stmmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    lb_priv->dma_rx_phy, 0);

		lb_priv->rx_tail_addr = lb_priv->dma_rx_phy +
			    (1 * sizeof(struct dma_desc));
		stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
				       lb_priv->rx_tail_addr, 0);
	}

	/* DMA TX Channel Configuration */
	for (chan = 0; chan < tx_channels_count; chan++) {
		stmmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    lb_priv->dma_tx_phy, chan);

		lb_priv->tx_tail_addr = lb_priv->dma_tx_phy;
		stmmac_set_tx_tail_ptr(priv, priv->ioaddr,
				       lb_priv->tx_tail_addr, chan);
	}

	return ret;
}

static void dwmac_rk_dma_operation_mode(struct stmmac_priv *priv,
					struct dwmac_rk_lb_priv *lb_priv)
{
	u32 rx_channels_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	u32 tx_channels_count = min_t(u32, priv->plat->tx_queues_to_use, 1);
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;
	u32 txmode = SF_DMA_MODE;
	u32 rxmode = SF_DMA_MODE;
	u32 chan = 0;
	u8 qmode = 0;

	if (rxfifosz == 0)
		rxfifosz = priv->dma_cap.rx_fifo_size;
	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	/* Adjust for real per queue fifo size */
	rxfifosz /= rx_channels_count;
	txfifosz /= tx_channels_count;

	/* configure all channels */
	for (chan = 0; chan < rx_channels_count; chan++) {
		qmode = priv->plat->rx_queues_cfg[chan].mode_to_use;

		stmmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan,
				   rxfifosz, qmode);
		stmmac_set_dma_bfsize(priv, priv->ioaddr, lb_priv->dma_buf_sz,
				      chan);
	}

	for (chan = 0; chan < tx_channels_count; chan++) {
		qmode = priv->plat->tx_queues_cfg[chan].mode_to_use;

		stmmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan,
				   txfifosz, qmode);
	}
}

static void dwmac_rk_rx_queue_dma_chan_map(struct stmmac_priv *priv)
{
	u32 rx_queues_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	u32 queue;
	u32 chan;

	for (queue = 0; queue < rx_queues_count; queue++) {
		chan = priv->plat->rx_queues_cfg[queue].chan;
		stmmac_map_mtl_to_dma(priv, priv->hw, queue, chan);
	}
}

static void dwmac_rk_mac_enable_rx_queues(struct stmmac_priv *priv)
{
	u32 rx_queues_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	int queue;
	u8 mode;

	for (queue = 0; queue < rx_queues_count; queue++) {
		mode = priv->plat->rx_queues_cfg[queue].mode_to_use;
		stmmac_rx_queue_enable(priv, priv->hw, mode, queue);
	}
}

static void dwmac_rk_mtl_configuration(struct stmmac_priv *priv)
{
	/* Map RX MTL to DMA channels */
	dwmac_rk_rx_queue_dma_chan_map(priv);

	/* Enable MAC RX Queues */
	dwmac_rk_mac_enable_rx_queues(priv);
}

static void dwmac_rk_mmc_setup(struct stmmac_priv *priv)
{
	unsigned int mode = MMC_CNTRL_RESET_ON_READ | MMC_CNTRL_COUNTER_RESET |
			    MMC_CNTRL_PRESET | MMC_CNTRL_FULL_HALF_PRESET;

	stmmac_mmc_intr_all_mask(priv, priv->mmcaddr);

	if (priv->dma_cap.rmon) {
		stmmac_mmc_ctrl(priv, priv->mmcaddr, mode);
		memset(&priv->mmc, 0, sizeof(struct stmmac_counters));
	} else {
		netdev_info(priv->dev, "No MAC Management Counters available\n");
	}
}

static int dwmac_rk_init(struct net_device *dev,
			 struct dwmac_rk_lb_priv *lb_priv)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int ret;
	u32 mode;

	lb_priv->dma_buf_sz = 1536; /* mtu 1500 size */

	if (priv->plat->has_gmac4)
		lb_priv->buf_sz = priv->dma_cap.rx_fifo_size; /* rx fifo size */
	else
		lb_priv->buf_sz = 4096; /* rx fifo size */

	ret = dwmac_rk_alloc_dma_desc_resources(priv, lb_priv);
	if (ret < 0) {
		pr_err("%s: DMA descriptors allocation failed\n", __func__);
		return ret;
	}

	ret = dwmac_rk_init_dma_desc_rings(dev, GFP_KERNEL, lb_priv);
	if (ret < 0) {
		pr_err("%s: DMA descriptors initialization failed\n", __func__);
		goto init_error;
	}

	/* DMA initialization and SW reset */
	ret = dwmac_rk_init_dma_engine(priv, lb_priv);
	if (ret < 0) {
		pr_err("%s: DMA engine initialization failed\n", __func__);
		goto init_error;
	}

	/* Copy the MAC addr into the HW  */
	priv->hw->mac->set_umac_addr(priv->hw, dev->dev_addr, 0);

	/* Initialize the MAC Core */
	stmmac_core_init(priv, priv->hw, dev);

	dwmac_rk_mtl_configuration(priv);

	dwmac_rk_mmc_setup(priv);

	ret = priv->hw->mac->rx_ipc(priv->hw);
	if (!ret) {
		pr_warn(" RX IPC Checksum Offload disabled\n");
		priv->plat->rx_coe = STMMAC_RX_COE_NONE;
		priv->hw->rx_csum = 0;
	}

	/* Set the HW DMA mode and the COE */
	dwmac_rk_dma_operation_mode(priv, lb_priv);

	if (priv->plat->has_gmac4) {
		mode = readl(priv->ioaddr + DMA_CHAN_TX_CONTROL(0));
		/* Disable OSP to get best performance */
		mode &= ~DMA_CONTROL_OSP;
		writel(mode, priv->ioaddr + DMA_CHAN_TX_CONTROL(0));
	} else {
		/* Disable OSF */
		mode = readl(priv->ioaddr + DMA_CONTROL);
		writel((mode & ~DMA_CONTROL_OSF), priv->ioaddr + DMA_CONTROL);
	}

	stmmac_enable_dma_irq(priv, priv->ioaddr, 0, 1, 1);

	if (priv->hw->pcs)
		stmmac_pcs_ctrl_ane(priv, priv->hw, 1, priv->hw->ps, 0);

	return 0;
init_error:
	dwmac_rk_free_dma_desc_resources(priv, lb_priv);

	return ret;
}

static void dwmac_rk_release(struct net_device *dev,
			     struct dwmac_rk_lb_priv *lb_priv)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	stmmac_disable_dma_irq(priv, priv->ioaddr, 0, 0, 0);

	/* Release and free the Rx/Tx resources */
	dwmac_rk_free_dma_desc_resources(priv, lb_priv);
}

static int dwmac_rk_get_max_delayline(struct stmmac_priv *priv)
{
	if (of_device_is_compatible(priv->device->of_node,
				    "rockchip,rk3588-gmac"))
		return RK3588_MAX_DELAYLINE;
	else
		return MAX_DELAYLINE;
}

static int dwmac_rk_phy_poll_reset(struct stmmac_priv *priv, int addr)
{
	/* Poll until the reset bit clears (50ms per retry == 0.6 sec) */
	unsigned int val, retries = 12;
	int ret;

	val = mdiobus_read(priv->mii, addr, MII_BMCR);
	mdiobus_write(priv->mii, addr, MII_BMCR, val | BMCR_RESET);

	do {
		msleep(50);
		ret = mdiobus_read(priv->mii, addr, MII_BMCR);
		if (ret < 0)
			return ret;
	} while (ret & BMCR_RESET && --retries);
	if (ret & BMCR_RESET)
		return -ETIMEDOUT;

	msleep(1);
	return 0;
}

static int dwmac_rk_loopback_run(struct stmmac_priv *priv,
				 struct dwmac_rk_lb_priv *lb_priv)
{
	struct net_device *ndev = priv->dev;
	int phy_iface = dwmac_rk_get_phy_interface(priv);
	int ndev_up, phy_addr;
	int ret = -EINVAL;

	if (!ndev || !priv->mii)
		return -EINVAL;

	phy_addr = priv->dev->phydev->mdio.addr;
	lb_priv->max_delay = dwmac_rk_get_max_delayline(priv);

	rtnl_lock();
	/* check the netdevice up or not */
	ndev_up = ndev->flags & IFF_UP;

	if (ndev_up) {
		if (!netif_running(ndev) || !ndev->phydev) {
			rtnl_unlock();
			return -EINVAL;
		}

		/* check if the negotiation status */
		if (ndev->phydev->state != PHY_NOLINK &&
		    ndev->phydev->state != PHY_RUNNING) {
			rtnl_unlock();
			pr_warn("Try again later, after negotiation done\n");
			return -EAGAIN;
		}

		ndev->netdev_ops->ndo_stop(ndev);

		if (priv->plat->stmmac_rst)
			reset_control_assert(priv->plat->stmmac_rst);
		dwmac_rk_phy_poll_reset(priv, phy_addr);
		if (priv->plat->stmmac_rst)
			reset_control_deassert(priv->plat->stmmac_rst);
	}
	/* wait for phy and controller ready */
	usleep_range(100000, 200000);

	dwmac_rk_set_loopback(priv, lb_priv->type, lb_priv->speed,
			      true, phy_addr, true);

	ret = dwmac_rk_init(ndev, lb_priv);
	if (ret)
		goto exit_init;

	dwmac_rk_set_loopback(priv, lb_priv->type, lb_priv->speed,
			      true, phy_addr, false);

	if (lb_priv->scan) {
		/* scan only support for rgmii mode */
		if (phy_iface != PHY_INTERFACE_MODE_RGMII &&
		    phy_iface != PHY_INTERFACE_MODE_RGMII_ID &&
		    phy_iface != PHY_INTERFACE_MODE_RGMII_RXID &&
		    phy_iface != PHY_INTERFACE_MODE_RGMII_TXID) {
			ret = -EINVAL;
			goto out;
		}
		ret = dwmac_rk_loopback_delayline_scan(priv, lb_priv);
	} else {
		lb_priv->id++;
		lb_priv->tx = 0;
		lb_priv->rx = 0;

		lb_priv->packet = &dwmac_rk_tcp_attr;
		ret = __dwmac_rk_loopback_run(priv, lb_priv);
	}

out:
	dwmac_rk_release(ndev, lb_priv);
	dwmac_rk_set_loopback(priv, lb_priv->type, lb_priv->speed,
			      false, phy_addr, false);
exit_init:
	if (ndev_up)
		ndev->netdev_ops->ndo_open(ndev);

	rtnl_unlock();

	return ret;
}

static ssize_t rgmii_delayline_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int tx, rx;

	dwmac_rk_get_rgmii_delayline(priv, &tx, &rx);

	return sprintf(buf, "tx delayline: 0x%x, rx delayline: 0x%x\n",
		       tx, rx);
}

static ssize_t rgmii_delayline_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int tx = 0, rx = 0;
	char tmp[32];
	size_t buf_size = min(count, (sizeof(tmp) - 1));
	char *data;

	memset(tmp, 0, sizeof(tmp));
	strncpy(tmp, buf, buf_size);

	data = tmp;
	data = strstr(data, " ");
	if (!data)
		goto out;
	*data = 0;
	data++;

	if (kstrtoint(tmp, 0, &tx) || tx > dwmac_rk_get_max_delayline(priv))
		goto out;

	if (kstrtoint(data, 0, &rx) || rx > dwmac_rk_get_max_delayline(priv))
		goto out;

	dwmac_rk_set_rgmii_delayline(priv, tx, rx);
	pr_info("Set rgmii delayline tx: 0x%x, rx: 0x%x\n", tx, rx);

	return count;
out:
	pr_err("wrong delayline value input, range is <0x0, 0x7f>\n");
	pr_err("usage: <tx_delayline> <rx_delayline>\n");

	return count;
}
static DEVICE_ATTR_RW(rgmii_delayline);

static ssize_t mac_lb_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct dwmac_rk_lb_priv *lb_priv;
	int ret, speed;

	lb_priv = kzalloc(sizeof(*lb_priv), GFP_KERNEL);
	if (!lb_priv)
		return -ENOMEM;

	ret = kstrtoint(buf, 0, &speed);
	if (ret) {
		kfree(lb_priv);
		return count;
	}
	pr_info("MAC loopback speed set to %d\n", speed);

	lb_priv->sysfs = 1;
	lb_priv->type = LOOPBACK_TYPE_GMAC;
	lb_priv->speed = speed;
	lb_priv->scan = 0;

	ret = dwmac_rk_loopback_run(priv, lb_priv);
	kfree(lb_priv);

	if (!ret)
		pr_info("MAC loopback: PASS\n");
	else
		pr_info("MAC loopback: FAIL\n");

	return count;
}
static DEVICE_ATTR_WO(mac_lb);

static ssize_t phy_lb_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct dwmac_rk_lb_priv *lb_priv;
	int ret, speed;

	lb_priv = kzalloc(sizeof(*lb_priv), GFP_KERNEL);
	if (!lb_priv)
		return  -ENOMEM;

	ret = kstrtoint(buf, 0, &speed);
	if (ret) {
		kfree(lb_priv);
		return count;
	}
	pr_info("PHY loopback speed set to %d\n", speed);

	lb_priv->sysfs = 1;
	lb_priv->type = LOOPBACK_TYPE_PHY;
	lb_priv->speed = speed;
	lb_priv->scan = 0;

	ret = dwmac_rk_loopback_run(priv, lb_priv);
	if (!ret)
		pr_info("PHY loopback: PASS\n");
	else
		pr_info("PHY loopback: FAIL\n");

	kfree(lb_priv);
	return count;
}
static DEVICE_ATTR_WO(phy_lb);

static ssize_t phy_lb_scan_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct dwmac_rk_lb_priv *lb_priv;
	int ret, speed;

	lb_priv = kzalloc(sizeof(*lb_priv), GFP_KERNEL);
	if (!lb_priv)
		return -ENOMEM;

	ret = kstrtoint(buf, 0, &speed);
	if (ret) {
		kfree(lb_priv);
		return count;
	}
	pr_info("Delayline scan speed set to %d\n", speed);

	lb_priv->sysfs = 1;
	lb_priv->type = LOOPBACK_TYPE_PHY;
	lb_priv->speed = speed;
	lb_priv->scan = 1;

	dwmac_rk_loopback_run(priv, lb_priv);

	kfree(lb_priv);
	return count;
}
static DEVICE_ATTR_WO(phy_lb_scan);

int dwmac_rk_create_loopback_sysfs(struct device *device)
{
	int ret;

	ret = device_create_file(device, &dev_attr_rgmii_delayline);
	if (ret)
		return ret;

	ret = device_create_file(device, &dev_attr_mac_lb);
	if (ret)
		goto remove_rgmii_delayline;

	ret = device_create_file(device, &dev_attr_phy_lb);
	if (ret)
		goto remove_mac_lb;

	ret = device_create_file(device, &dev_attr_phy_lb_scan);
	if (ret)
		goto remove_phy_lb;

	return 0;

remove_rgmii_delayline:
	device_remove_file(device, &dev_attr_rgmii_delayline);

remove_mac_lb:
	device_remove_file(device, &dev_attr_mac_lb);

remove_phy_lb:
	device_remove_file(device, &dev_attr_phy_lb);

	return ret;
}

int dwmac_rk_remove_loopback_sysfs(struct device *device)
{
	device_remove_file(device, &dev_attr_rgmii_delayline);
	device_remove_file(device, &dev_attr_mac_lb);
	device_remove_file(device, &dev_attr_phy_lb);
	device_remove_file(device, &dev_attr_phy_lb_scan);

	return 0;
}
