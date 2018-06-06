/*
 * Copyright (c) 2016~2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/sctp.h>
#include <linux/vermagic.h>
#include <net/gre.h>
#include <net/pkt_cls.h>
#include <net/vxlan.h>

#include "hnae3.h"
#include "hns3_enet.h"

static const char hns3_driver_name[] = "hns3";
const char hns3_driver_version[] = VERMAGIC_STRING;
static const char hns3_driver_string[] =
			"Hisilicon Ethernet Network Driver for Hip08 Family";
static const char hns3_copyright[] = "Copyright (c) 2017 Huawei Corporation.";
static struct hnae3_client client;

/* hns3_pci_tbl - PCI Device ID Table
 *
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id hns3_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_GE), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_VF), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_DCB_PFC_VF), 0},
	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, hns3_pci_tbl);

static irqreturn_t hns3_irq_handle(int irq, void *dev)
{
	struct hns3_enet_tqp_vector *tqp_vector = dev;

	napi_schedule(&tqp_vector->napi);

	return IRQ_HANDLED;
}

static void hns3_nic_uninit_irq(struct hns3_nic_priv *priv)
{
	struct hns3_enet_tqp_vector *tqp_vectors;
	unsigned int i;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vectors = &priv->tqp_vector[i];

		if (tqp_vectors->irq_init_flag != HNS3_VECTOR_INITED)
			continue;

		/* release the irq resource */
		free_irq(tqp_vectors->vector_irq, tqp_vectors);
		tqp_vectors->irq_init_flag = HNS3_VECTOR_NOT_INITED;
	}
}

static int hns3_nic_init_irq(struct hns3_nic_priv *priv)
{
	struct hns3_enet_tqp_vector *tqp_vectors;
	int txrx_int_idx = 0;
	int rx_int_idx = 0;
	int tx_int_idx = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vectors = &priv->tqp_vector[i];

		if (tqp_vectors->irq_init_flag == HNS3_VECTOR_INITED)
			continue;

		if (tqp_vectors->tx_group.ring && tqp_vectors->rx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN - 1,
				 "%s-%s-%d", priv->netdev->name, "TxRx",
				 txrx_int_idx++);
			txrx_int_idx++;
		} else if (tqp_vectors->rx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN - 1,
				 "%s-%s-%d", priv->netdev->name, "Rx",
				 rx_int_idx++);
		} else if (tqp_vectors->tx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN - 1,
				 "%s-%s-%d", priv->netdev->name, "Tx",
				 tx_int_idx++);
		} else {
			/* Skip this unused q_vector */
			continue;
		}

		tqp_vectors->name[HNAE3_INT_NAME_LEN - 1] = '\0';

		ret = request_irq(tqp_vectors->vector_irq, hns3_irq_handle, 0,
				  tqp_vectors->name,
				       tqp_vectors);
		if (ret) {
			netdev_err(priv->netdev, "request irq(%d) fail\n",
				   tqp_vectors->vector_irq);
			return ret;
		}

		tqp_vectors->irq_init_flag = HNS3_VECTOR_INITED;
	}

	return 0;
}

static void hns3_mask_vector_irq(struct hns3_enet_tqp_vector *tqp_vector,
				 u32 mask_en)
{
	writel(mask_en, tqp_vector->mask_addr);
}

static void hns3_vector_enable(struct hns3_enet_tqp_vector *tqp_vector)
{
	napi_enable(&tqp_vector->napi);

	/* enable vector */
	hns3_mask_vector_irq(tqp_vector, 1);
}

static void hns3_vector_disable(struct hns3_enet_tqp_vector *tqp_vector)
{
	/* disable vector */
	hns3_mask_vector_irq(tqp_vector, 0);

	disable_irq(tqp_vector->vector_irq);
	napi_disable(&tqp_vector->napi);
}

void hns3_set_vector_coalesce_rl(struct hns3_enet_tqp_vector *tqp_vector,
				 u32 rl_value)
{
	u32 rl_reg = hns3_rl_usec_to_reg(rl_value);

	/* this defines the configuration for RL (Interrupt Rate Limiter).
	 * Rl defines rate of interrupts i.e. number of interrupts-per-second
	 * GL and RL(Rate Limiter) are 2 ways to acheive interrupt coalescing
	 */

	if (rl_reg > 0 && !tqp_vector->tx_group.coal.gl_adapt_enable &&
	    !tqp_vector->rx_group.coal.gl_adapt_enable)
		/* According to the hardware, the range of rl_reg is
		 * 0-59 and the unit is 4.
		 */
		rl_reg |=  HNS3_INT_RL_ENABLE_MASK;

	writel(rl_reg, tqp_vector->mask_addr + HNS3_VECTOR_RL_OFFSET);
}

void hns3_set_vector_coalesce_rx_gl(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 gl_value)
{
	u32 rx_gl_reg = hns3_gl_usec_to_reg(gl_value);

	writel(rx_gl_reg, tqp_vector->mask_addr + HNS3_VECTOR_GL0_OFFSET);
}

void hns3_set_vector_coalesce_tx_gl(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 gl_value)
{
	u32 tx_gl_reg = hns3_gl_usec_to_reg(gl_value);

	writel(tx_gl_reg, tqp_vector->mask_addr + HNS3_VECTOR_GL1_OFFSET);
}

static void hns3_vector_gl_rl_init(struct hns3_enet_tqp_vector *tqp_vector,
				   struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;

	/* initialize the configuration for interrupt coalescing.
	 * 1. GL (Interrupt Gap Limiter)
	 * 2. RL (Interrupt Rate Limiter)
	 */

	/* Default: enable interrupt coalescing self-adaptive and GL */
	tqp_vector->tx_group.coal.gl_adapt_enable = 1;
	tqp_vector->rx_group.coal.gl_adapt_enable = 1;

	tqp_vector->tx_group.coal.int_gl = HNS3_INT_GL_50K;
	tqp_vector->rx_group.coal.int_gl = HNS3_INT_GL_50K;

	/* Default: disable RL */
	h->kinfo.int_rl_setting = 0;

	tqp_vector->int_adapt_down = HNS3_INT_ADAPT_DOWN_START;
	tqp_vector->rx_group.coal.flow_level = HNS3_FLOW_LOW;
	tqp_vector->tx_group.coal.flow_level = HNS3_FLOW_LOW;
}

static void hns3_vector_gl_rl_init_hw(struct hns3_enet_tqp_vector *tqp_vector,
				      struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;

	hns3_set_vector_coalesce_tx_gl(tqp_vector,
				       tqp_vector->tx_group.coal.int_gl);
	hns3_set_vector_coalesce_rx_gl(tqp_vector,
				       tqp_vector->rx_group.coal.int_gl);
	hns3_set_vector_coalesce_rl(tqp_vector, h->kinfo.int_rl_setting);
}

static int hns3_nic_set_real_num_queue(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	unsigned int queue_size = kinfo->rss_size * kinfo->num_tc;
	int ret;

	ret = netif_set_real_num_tx_queues(netdev, queue_size);
	if (ret) {
		netdev_err(netdev,
			   "netif_set_real_num_tx_queues fail, ret=%d!\n",
			   ret);
		return ret;
	}

	ret = netif_set_real_num_rx_queues(netdev, queue_size);
	if (ret) {
		netdev_err(netdev,
			   "netif_set_real_num_rx_queues fail, ret=%d!\n", ret);
		return ret;
	}

	return 0;
}

static u16 hns3_get_max_available_channels(struct hnae3_handle *h)
{
	u16 free_tqps, max_rss_size, max_tqps;

	h->ae_algo->ops->get_tqps_and_rss_info(h, &free_tqps, &max_rss_size);
	max_tqps = h->kinfo.num_tc * max_rss_size;

	return min_t(u16, max_tqps, (free_tqps + h->kinfo.num_tqps));
}

static int hns3_nic_net_up(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	int i, j;
	int ret;

	/* get irq resource for all vectors */
	ret = hns3_nic_init_irq(priv);
	if (ret) {
		netdev_err(netdev, "hns init irq failed! ret=%d\n", ret);
		return ret;
	}

	/* enable the vectors */
	for (i = 0; i < priv->vector_num; i++)
		hns3_vector_enable(&priv->tqp_vector[i]);

	/* start the ae_dev */
	ret = h->ae_algo->ops->start ? h->ae_algo->ops->start(h) : 0;
	if (ret)
		goto out_start_err;

	clear_bit(HNS3_NIC_STATE_DOWN, &priv->state);

	return 0;

out_start_err:
	for (j = i - 1; j >= 0; j--)
		hns3_vector_disable(&priv->tqp_vector[j]);

	hns3_nic_uninit_irq(priv);

	return ret;
}

static int hns3_nic_net_open(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret;

	netif_carrier_off(netdev);

	ret = hns3_nic_set_real_num_queue(netdev);
	if (ret)
		return ret;

	ret = hns3_nic_net_up(netdev);
	if (ret) {
		netdev_err(netdev,
			   "hns net up fail, ret=%d!\n", ret);
		return ret;
	}

	priv->ae_handle->last_reset_time = jiffies;
	return 0;
}

static void hns3_nic_net_down(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	const struct hnae3_ae_ops *ops;
	int i;

	if (test_and_set_bit(HNS3_NIC_STATE_DOWN, &priv->state))
		return;

	/* stop ae_dev */
	ops = priv->ae_handle->ae_algo->ops;
	if (ops->stop)
		ops->stop(priv->ae_handle);

	/* disable vectors */
	for (i = 0; i < priv->vector_num; i++)
		hns3_vector_disable(&priv->tqp_vector[i]);

	/* free irq resources */
	hns3_nic_uninit_irq(priv);
}

static int hns3_nic_net_stop(struct net_device *netdev)
{
	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);

	hns3_nic_net_down(netdev);

	return 0;
}

static int hns3_nic_uc_sync(struct net_device *netdev,
			    const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->add_uc_addr)
		return h->ae_algo->ops->add_uc_addr(h, addr);

	return 0;
}

static int hns3_nic_uc_unsync(struct net_device *netdev,
			      const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->rm_uc_addr)
		return h->ae_algo->ops->rm_uc_addr(h, addr);

	return 0;
}

static int hns3_nic_mc_sync(struct net_device *netdev,
			    const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->add_mc_addr)
		return h->ae_algo->ops->add_mc_addr(h, addr);

	return 0;
}

static int hns3_nic_mc_unsync(struct net_device *netdev,
			      const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->rm_mc_addr)
		return h->ae_algo->ops->rm_mc_addr(h, addr);

	return 0;
}

static void hns3_nic_set_rx_mode(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->set_promisc_mode) {
		if (netdev->flags & IFF_PROMISC)
			h->ae_algo->ops->set_promisc_mode(h, 1);
		else
			h->ae_algo->ops->set_promisc_mode(h, 0);
	}
	if (__dev_uc_sync(netdev, hns3_nic_uc_sync, hns3_nic_uc_unsync))
		netdev_err(netdev, "sync uc address fail\n");
	if (netdev->flags & IFF_MULTICAST)
		if (__dev_mc_sync(netdev, hns3_nic_mc_sync, hns3_nic_mc_unsync))
			netdev_err(netdev, "sync mc address fail\n");
}

static int hns3_set_tso(struct sk_buff *skb, u32 *paylen,
			u16 *mss, u32 *type_cs_vlan_tso)
{
	u32 l4_offset, hdr_len;
	union l3_hdr_info l3;
	union l4_hdr_info l4;
	u32 l4_paylen;
	int ret;

	if (!skb_is_gso(skb))
		return 0;

	ret = skb_cow_head(skb, 0);
	if (ret)
		return ret;

	l3.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* Software should clear the IPv4's checksum field when tso is
	 * needed.
	 */
	if (l3.v4->version == 4)
		l3.v4->check = 0;

	/* tunnel packet.*/
	if (skb_shinfo(skb)->gso_type & (SKB_GSO_GRE |
					 SKB_GSO_GRE_CSUM |
					 SKB_GSO_UDP_TUNNEL |
					 SKB_GSO_UDP_TUNNEL_CSUM)) {
		if ((!(skb_shinfo(skb)->gso_type &
		    SKB_GSO_PARTIAL)) &&
		    (skb_shinfo(skb)->gso_type &
		    SKB_GSO_UDP_TUNNEL_CSUM)) {
			/* Software should clear the udp's checksum
			 * field when tso is needed.
			 */
			l4.udp->check = 0;
		}
		/* reset l3&l4 pointers from outer to inner headers */
		l3.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);

		/* Software should clear the IPv4's checksum field when
		 * tso is needed.
		 */
		if (l3.v4->version == 4)
			l3.v4->check = 0;
	}

	/* normal or tunnel packet*/
	l4_offset = l4.hdr - skb->data;
	hdr_len = (l4.tcp->doff * 4) + l4_offset;

	/* remove payload length from inner pseudo checksum when tso*/
	l4_paylen = skb->len - l4_offset;
	csum_replace_by_diff(&l4.tcp->check,
			     (__force __wsum)htonl(l4_paylen));

	/* find the txbd field values */
	*paylen = skb->len - hdr_len;
	hnae_set_bit(*type_cs_vlan_tso,
		     HNS3_TXD_TSO_B, 1);

	/* get MSS for TSO */
	*mss = skb_shinfo(skb)->gso_size;

	return 0;
}

static int hns3_get_l4_protocol(struct sk_buff *skb, u8 *ol4_proto,
				u8 *il4_proto)
{
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} l3;
	unsigned char *l4_hdr;
	unsigned char *exthdr;
	u8 l4_proto_tmp;
	__be16 frag_off;

	/* find outer header point */
	l3.hdr = skb_network_header(skb);
	l4_hdr = skb_inner_transport_header(skb);

	if (skb->protocol == htons(ETH_P_IPV6)) {
		exthdr = l3.hdr + sizeof(*l3.v6);
		l4_proto_tmp = l3.v6->nexthdr;
		if (l4_hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data,
					 &l4_proto_tmp, &frag_off);
	} else if (skb->protocol == htons(ETH_P_IP)) {
		l4_proto_tmp = l3.v4->protocol;
	} else {
		return -EINVAL;
	}

	*ol4_proto = l4_proto_tmp;

	/* tunnel packet */
	if (!skb->encapsulation) {
		*il4_proto = 0;
		return 0;
	}

	/* find inner header point */
	l3.hdr = skb_inner_network_header(skb);
	l4_hdr = skb_inner_transport_header(skb);

	if (l3.v6->version == 6) {
		exthdr = l3.hdr + sizeof(*l3.v6);
		l4_proto_tmp = l3.v6->nexthdr;
		if (l4_hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data,
					 &l4_proto_tmp, &frag_off);
	} else if (l3.v4->version == 4) {
		l4_proto_tmp = l3.v4->protocol;
	}

	*il4_proto = l4_proto_tmp;

	return 0;
}

static void hns3_set_l2l3l4_len(struct sk_buff *skb, u8 ol4_proto,
				u8 il4_proto, u32 *type_cs_vlan_tso,
				u32 *ol_type_vlan_len_msec)
{
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} l3;
	union {
		struct tcphdr *tcp;
		struct udphdr *udp;
		struct gre_base_hdr *gre;
		unsigned char *hdr;
	} l4;
	unsigned char *l2_hdr;
	u8 l4_proto = ol4_proto;
	u32 ol2_len;
	u32 ol3_len;
	u32 ol4_len;
	u32 l2_len;
	u32 l3_len;

	l3.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* compute L2 header size for normal packet, defined in 2 Bytes */
	l2_len = l3.hdr - skb->data;
	hnae_set_field(*type_cs_vlan_tso, HNS3_TXD_L2LEN_M,
		       HNS3_TXD_L2LEN_S, l2_len >> 1);

	/* tunnel packet*/
	if (skb->encapsulation) {
		/* compute OL2 header size, defined in 2 Bytes */
		ol2_len = l2_len;
		hnae_set_field(*ol_type_vlan_len_msec,
			       HNS3_TXD_L2LEN_M,
			       HNS3_TXD_L2LEN_S, ol2_len >> 1);

		/* compute OL3 header size, defined in 4 Bytes */
		ol3_len = l4.hdr - l3.hdr;
		hnae_set_field(*ol_type_vlan_len_msec, HNS3_TXD_L3LEN_M,
			       HNS3_TXD_L3LEN_S, ol3_len >> 2);

		/* MAC in UDP, MAC in GRE (0x6558)*/
		if ((ol4_proto == IPPROTO_UDP) || (ol4_proto == IPPROTO_GRE)) {
			/* switch MAC header ptr from outer to inner header.*/
			l2_hdr = skb_inner_mac_header(skb);

			/* compute OL4 header size, defined in 4 Bytes. */
			ol4_len = l2_hdr - l4.hdr;
			hnae_set_field(*ol_type_vlan_len_msec, HNS3_TXD_L4LEN_M,
				       HNS3_TXD_L4LEN_S, ol4_len >> 2);

			/* switch IP header ptr from outer to inner header */
			l3.hdr = skb_inner_network_header(skb);

			/* compute inner l2 header size, defined in 2 Bytes. */
			l2_len = l3.hdr - l2_hdr;
			hnae_set_field(*type_cs_vlan_tso, HNS3_TXD_L2LEN_M,
				       HNS3_TXD_L2LEN_S, l2_len >> 1);
		} else {
			/* skb packet types not supported by hardware,
			 * txbd len fild doesn't be filled.
			 */
			return;
		}

		/* switch L4 header pointer from outer to inner */
		l4.hdr = skb_inner_transport_header(skb);

		l4_proto = il4_proto;
	}

	/* compute inner(/normal) L3 header size, defined in 4 Bytes */
	l3_len = l4.hdr - l3.hdr;
	hnae_set_field(*type_cs_vlan_tso, HNS3_TXD_L3LEN_M,
		       HNS3_TXD_L3LEN_S, l3_len >> 2);

	/* compute inner(/normal) L4 header size, defined in 4 Bytes */
	switch (l4_proto) {
	case IPPROTO_TCP:
		hnae_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_M,
			       HNS3_TXD_L4LEN_S, l4.tcp->doff);
		break;
	case IPPROTO_SCTP:
		hnae_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_M,
			       HNS3_TXD_L4LEN_S, (sizeof(struct sctphdr) >> 2));
		break;
	case IPPROTO_UDP:
		hnae_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_M,
			       HNS3_TXD_L4LEN_S, (sizeof(struct udphdr) >> 2));
		break;
	default:
		/* skb packet types not supported by hardware,
		 * txbd len fild doesn't be filled.
		 */
		return;
	}
}

static int hns3_set_l3l4_type_csum(struct sk_buff *skb, u8 ol4_proto,
				   u8 il4_proto, u32 *type_cs_vlan_tso,
				   u32 *ol_type_vlan_len_msec)
{
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} l3;
	u32 l4_proto = ol4_proto;

	l3.hdr = skb_network_header(skb);

	/* define OL3 type and tunnel type(OL4).*/
	if (skb->encapsulation) {
		/* define outer network header type.*/
		if (skb->protocol == htons(ETH_P_IP)) {
			if (skb_is_gso(skb))
				hnae_set_field(*ol_type_vlan_len_msec,
					       HNS3_TXD_OL3T_M, HNS3_TXD_OL3T_S,
					       HNS3_OL3T_IPV4_CSUM);
			else
				hnae_set_field(*ol_type_vlan_len_msec,
					       HNS3_TXD_OL3T_M, HNS3_TXD_OL3T_S,
					       HNS3_OL3T_IPV4_NO_CSUM);

		} else if (skb->protocol == htons(ETH_P_IPV6)) {
			hnae_set_field(*ol_type_vlan_len_msec, HNS3_TXD_OL3T_M,
				       HNS3_TXD_OL3T_S, HNS3_OL3T_IPV6);
		}

		/* define tunnel type(OL4).*/
		switch (l4_proto) {
		case IPPROTO_UDP:
			hnae_set_field(*ol_type_vlan_len_msec,
				       HNS3_TXD_TUNTYPE_M,
				       HNS3_TXD_TUNTYPE_S,
				       HNS3_TUN_MAC_IN_UDP);
			break;
		case IPPROTO_GRE:
			hnae_set_field(*ol_type_vlan_len_msec,
				       HNS3_TXD_TUNTYPE_M,
				       HNS3_TXD_TUNTYPE_S,
				       HNS3_TUN_NVGRE);
			break;
		default:
			/* drop the skb tunnel packet if hardware don't support,
			 * because hardware can't calculate csum when TSO.
			 */
			if (skb_is_gso(skb))
				return -EDOM;

			/* the stack computes the IP header already,
			 * driver calculate l4 checksum when not TSO.
			 */
			skb_checksum_help(skb);
			return 0;
		}

		l3.hdr = skb_inner_network_header(skb);
		l4_proto = il4_proto;
	}

	if (l3.v4->version == 4) {
		hnae_set_field(*type_cs_vlan_tso, HNS3_TXD_L3T_M,
			       HNS3_TXD_L3T_S, HNS3_L3T_IPV4);

		/* the stack computes the IP header already, the only time we
		 * need the hardware to recompute it is in the case of TSO.
		 */
		if (skb_is_gso(skb))
			hnae_set_bit(*type_cs_vlan_tso, HNS3_TXD_L3CS_B, 1);

		hnae_set_bit(*type_cs_vlan_tso, HNS3_TXD_L4CS_B, 1);
	} else if (l3.v6->version == 6) {
		hnae_set_field(*type_cs_vlan_tso, HNS3_TXD_L3T_M,
			       HNS3_TXD_L3T_S, HNS3_L3T_IPV6);
		hnae_set_bit(*type_cs_vlan_tso, HNS3_TXD_L4CS_B, 1);
	}

	switch (l4_proto) {
	case IPPROTO_TCP:
		hnae_set_field(*type_cs_vlan_tso,
			       HNS3_TXD_L4T_M,
			       HNS3_TXD_L4T_S,
			       HNS3_L4T_TCP);
		break;
	case IPPROTO_UDP:
		hnae_set_field(*type_cs_vlan_tso,
			       HNS3_TXD_L4T_M,
			       HNS3_TXD_L4T_S,
			       HNS3_L4T_UDP);
		break;
	case IPPROTO_SCTP:
		hnae_set_field(*type_cs_vlan_tso,
			       HNS3_TXD_L4T_M,
			       HNS3_TXD_L4T_S,
			       HNS3_L4T_SCTP);
		break;
	default:
		/* drop the skb tunnel packet if hardware don't support,
		 * because hardware can't calculate csum when TSO.
		 */
		if (skb_is_gso(skb))
			return -EDOM;

		/* the stack computes the IP header already,
		 * driver calculate l4 checksum when not TSO.
		 */
		skb_checksum_help(skb);
		return 0;
	}

	return 0;
}

static void hns3_set_txbd_baseinfo(u16 *bdtp_fe_sc_vld_ra_ri, int frag_end)
{
	/* Config bd buffer end */
	hnae_set_field(*bdtp_fe_sc_vld_ra_ri, HNS3_TXD_BDTYPE_M,
		       HNS3_TXD_BDTYPE_S, 0);
	hnae_set_bit(*bdtp_fe_sc_vld_ra_ri, HNS3_TXD_FE_B, !!frag_end);
	hnae_set_bit(*bdtp_fe_sc_vld_ra_ri, HNS3_TXD_VLD_B, 1);
	hnae_set_field(*bdtp_fe_sc_vld_ra_ri, HNS3_TXD_SC_M, HNS3_TXD_SC_S, 0);
}

static int hns3_fill_desc_vtags(struct sk_buff *skb,
				struct hns3_enet_ring *tx_ring,
				u32 *inner_vlan_flag,
				u32 *out_vlan_flag,
				u16 *inner_vtag,
				u16 *out_vtag)
{
#define HNS3_TX_VLAN_PRIO_SHIFT 13

	if (skb->protocol == htons(ETH_P_8021Q) &&
	    !(tx_ring->tqp->handle->kinfo.netdev->features &
	    NETIF_F_HW_VLAN_CTAG_TX)) {
		/* When HW VLAN acceleration is turned off, and the stack
		 * sets the protocol to 802.1q, the driver just need to
		 * set the protocol to the encapsulated ethertype.
		 */
		skb->protocol = vlan_get_protocol(skb);
		return 0;
	}

	if (skb_vlan_tag_present(skb)) {
		u16 vlan_tag;

		vlan_tag = skb_vlan_tag_get(skb);
		vlan_tag |= (skb->priority & 0x7) << HNS3_TX_VLAN_PRIO_SHIFT;

		/* Based on hw strategy, use out_vtag in two layer tag case,
		 * and use inner_vtag in one tag case.
		 */
		if (skb->protocol == htons(ETH_P_8021Q)) {
			hnae_set_bit(*out_vlan_flag, HNS3_TXD_OVLAN_B, 1);
			*out_vtag = vlan_tag;
		} else {
			hnae_set_bit(*inner_vlan_flag, HNS3_TXD_VLAN_B, 1);
			*inner_vtag = vlan_tag;
		}
	} else if (skb->protocol == htons(ETH_P_8021Q)) {
		struct vlan_ethhdr *vhdr;
		int rc;

		rc = skb_cow_head(skb, 0);
		if (rc < 0)
			return rc;
		vhdr = (struct vlan_ethhdr *)skb->data;
		vhdr->h_vlan_TCI |= cpu_to_be16((skb->priority & 0x7)
					<< HNS3_TX_VLAN_PRIO_SHIFT);
	}

	skb->protocol = vlan_get_protocol(skb);
	return 0;
}

static int hns3_fill_desc(struct hns3_enet_ring *ring, void *priv,
			  int size, dma_addr_t dma, int frag_end,
			  enum hns_desc_type type)
{
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	struct hns3_desc *desc = &ring->desc[ring->next_to_use];
	u32 ol_type_vlan_len_msec = 0;
	u16 bdtp_fe_sc_vld_ra_ri = 0;
	u32 type_cs_vlan_tso = 0;
	struct sk_buff *skb;
	u16 inner_vtag = 0;
	u16 out_vtag = 0;
	u32 paylen = 0;
	u16 mss = 0;
	__be16 protocol;
	u8 ol4_proto;
	u8 il4_proto;
	int ret;

	/* The txbd's baseinfo of DESC_TYPE_PAGE & DESC_TYPE_SKB */
	desc_cb->priv = priv;
	desc_cb->length = size;
	desc_cb->dma = dma;
	desc_cb->type = type;

	/* now, fill the descriptor */
	desc->addr = cpu_to_le64(dma);
	desc->tx.send_size = cpu_to_le16((u16)size);
	hns3_set_txbd_baseinfo(&bdtp_fe_sc_vld_ra_ri, frag_end);
	desc->tx.bdtp_fe_sc_vld_ra_ri = cpu_to_le16(bdtp_fe_sc_vld_ra_ri);

	if (type == DESC_TYPE_SKB) {
		skb = (struct sk_buff *)priv;
		paylen = skb->len;

		ret = hns3_fill_desc_vtags(skb, ring, &type_cs_vlan_tso,
					   &ol_type_vlan_len_msec,
					   &inner_vtag, &out_vtag);
		if (unlikely(ret))
			return ret;

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			skb_reset_mac_len(skb);
			protocol = skb->protocol;

			ret = hns3_get_l4_protocol(skb, &ol4_proto, &il4_proto);
			if (ret)
				return ret;
			hns3_set_l2l3l4_len(skb, ol4_proto, il4_proto,
					    &type_cs_vlan_tso,
					    &ol_type_vlan_len_msec);
			ret = hns3_set_l3l4_type_csum(skb, ol4_proto, il4_proto,
						      &type_cs_vlan_tso,
						      &ol_type_vlan_len_msec);
			if (ret)
				return ret;

			ret = hns3_set_tso(skb, &paylen, &mss,
					   &type_cs_vlan_tso);
			if (ret)
				return ret;
		}

		/* Set txbd */
		desc->tx.ol_type_vlan_len_msec =
			cpu_to_le32(ol_type_vlan_len_msec);
		desc->tx.type_cs_vlan_tso_len =
			cpu_to_le32(type_cs_vlan_tso);
		desc->tx.paylen = cpu_to_le32(paylen);
		desc->tx.mss = cpu_to_le16(mss);
		desc->tx.vlan_tag = cpu_to_le16(inner_vtag);
		desc->tx.outer_vlan_tag = cpu_to_le16(out_vtag);
	}

	/* move ring pointer to next.*/
	ring_ptr_move_fw(ring, next_to_use);

	return 0;
}

static int hns3_fill_desc_tso(struct hns3_enet_ring *ring, void *priv,
			      int size, dma_addr_t dma, int frag_end,
			      enum hns_desc_type type)
{
	unsigned int frag_buf_num;
	unsigned int k;
	int sizeoflast;
	int ret;

	frag_buf_num = (size + HNS3_MAX_BD_SIZE - 1) / HNS3_MAX_BD_SIZE;
	sizeoflast = size % HNS3_MAX_BD_SIZE;
	sizeoflast = sizeoflast ? sizeoflast : HNS3_MAX_BD_SIZE;

	/* When the frag size is bigger than hardware, split this frag */
	for (k = 0; k < frag_buf_num; k++) {
		ret = hns3_fill_desc(ring, priv,
				     (k == frag_buf_num - 1) ?
				sizeoflast : HNS3_MAX_BD_SIZE,
				dma + HNS3_MAX_BD_SIZE * k,
				frag_end && (k == frag_buf_num - 1) ? 1 : 0,
				(type == DESC_TYPE_SKB && !k) ?
					DESC_TYPE_SKB : DESC_TYPE_PAGE);
		if (ret)
			return ret;
	}

	return 0;
}

static int hns3_nic_maybe_stop_tso(struct sk_buff **out_skb, int *bnum,
				   struct hns3_enet_ring *ring)
{
	struct sk_buff *skb = *out_skb;
	struct skb_frag_struct *frag;
	int bdnum_for_frag;
	int frag_num;
	int buf_num;
	int size;
	int i;

	size = skb_headlen(skb);
	buf_num = (size + HNS3_MAX_BD_SIZE - 1) / HNS3_MAX_BD_SIZE;

	frag_num = skb_shinfo(skb)->nr_frags;
	for (i = 0; i < frag_num; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		size = skb_frag_size(frag);
		bdnum_for_frag =
			(size + HNS3_MAX_BD_SIZE - 1) / HNS3_MAX_BD_SIZE;
		if (bdnum_for_frag > HNS3_MAX_BD_PER_FRAG)
			return -ENOMEM;

		buf_num += bdnum_for_frag;
	}

	if (buf_num > ring_space(ring))
		return -EBUSY;

	*bnum = buf_num;
	return 0;
}

static int hns3_nic_maybe_stop_tx(struct sk_buff **out_skb, int *bnum,
				  struct hns3_enet_ring *ring)
{
	struct sk_buff *skb = *out_skb;
	int buf_num;

	/* No. of segments (plus a header) */
	buf_num = skb_shinfo(skb)->nr_frags + 1;

	if (buf_num > ring_space(ring))
		return -EBUSY;

	*bnum = buf_num;

	return 0;
}

static void hns_nic_dma_unmap(struct hns3_enet_ring *ring, int next_to_use_orig)
{
	struct device *dev = ring_to_dev(ring);
	unsigned int i;

	for (i = 0; i < ring->desc_num; i++) {
		/* check if this is where we started */
		if (ring->next_to_use == next_to_use_orig)
			break;

		/* unmap the descriptor dma address */
		if (ring->desc_cb[ring->next_to_use].type == DESC_TYPE_SKB)
			dma_unmap_single(dev,
					 ring->desc_cb[ring->next_to_use].dma,
					ring->desc_cb[ring->next_to_use].length,
					DMA_TO_DEVICE);
		else
			dma_unmap_page(dev,
				       ring->desc_cb[ring->next_to_use].dma,
				       ring->desc_cb[ring->next_to_use].length,
				       DMA_TO_DEVICE);

		/* rollback one */
		ring_ptr_move_bw(ring, next_to_use);
	}
}

netdev_tx_t hns3_nic_net_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hns3_nic_ring_data *ring_data =
		&tx_ring_data(priv, skb->queue_mapping);
	struct hns3_enet_ring *ring = ring_data->ring;
	struct device *dev = priv->dev;
	struct netdev_queue *dev_queue;
	struct skb_frag_struct *frag;
	int next_to_use_head;
	int next_to_use_frag;
	dma_addr_t dma;
	int buf_num;
	int seg_num;
	int size;
	int ret;
	int i;

	/* Prefetch the data used later */
	prefetch(skb->data);

	switch (priv->ops.maybe_stop_tx(&skb, &buf_num, ring)) {
	case -EBUSY:
		u64_stats_update_begin(&ring->syncp);
		ring->stats.tx_busy++;
		u64_stats_update_end(&ring->syncp);

		goto out_net_tx_busy;
	case -ENOMEM:
		u64_stats_update_begin(&ring->syncp);
		ring->stats.sw_err_cnt++;
		u64_stats_update_end(&ring->syncp);
		netdev_err(netdev, "no memory to xmit!\n");

		goto out_err_tx_ok;
	default:
		break;
	}

	/* No. of segments (plus a header) */
	seg_num = skb_shinfo(skb)->nr_frags + 1;
	/* Fill the first part */
	size = skb_headlen(skb);

	next_to_use_head = ring->next_to_use;

	dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma)) {
		netdev_err(netdev, "TX head DMA map failed\n");
		ring->stats.sw_err_cnt++;
		goto out_err_tx_ok;
	}

	ret = priv->ops.fill_desc(ring, skb, size, dma, seg_num == 1 ? 1 : 0,
			   DESC_TYPE_SKB);
	if (ret)
		goto head_dma_map_err;

	next_to_use_frag = ring->next_to_use;
	/* Fill the fragments */
	for (i = 1; i < seg_num; i++) {
		frag = &skb_shinfo(skb)->frags[i - 1];
		size = skb_frag_size(frag);
		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma)) {
			netdev_err(netdev, "TX frag(%d) DMA map failed\n", i);
			ring->stats.sw_err_cnt++;
			goto frag_dma_map_err;
		}
		ret = priv->ops.fill_desc(ring, skb_frag_page(frag), size, dma,
				    seg_num - 1 == i ? 1 : 0,
				    DESC_TYPE_PAGE);

		if (ret)
			goto frag_dma_map_err;
	}

	/* Complete translate all packets */
	dev_queue = netdev_get_tx_queue(netdev, ring_data->queue_index);
	netdev_tx_sent_queue(dev_queue, skb->len);

	wmb(); /* Commit all data before submit */

	hnae_queue_xmit(ring->tqp, buf_num);

	return NETDEV_TX_OK;

frag_dma_map_err:
	hns_nic_dma_unmap(ring, next_to_use_frag);

head_dma_map_err:
	hns_nic_dma_unmap(ring, next_to_use_head);

out_err_tx_ok:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;

out_net_tx_busy:
	netif_stop_subqueue(netdev, ring_data->queue_index);
	smp_mb(); /* Commit all data before submit */

	return NETDEV_TX_BUSY;
}

static int hns3_nic_net_set_mac_address(struct net_device *netdev, void *p)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct sockaddr *mac_addr = p;
	int ret;

	if (!mac_addr || !is_valid_ether_addr((const u8 *)mac_addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = h->ae_algo->ops->set_mac_addr(h, mac_addr->sa_data, false);
	if (ret) {
		netdev_err(netdev, "set_mac_address fail, ret=%d!\n", ret);
		return ret;
	}

	ether_addr_copy(netdev->dev_addr, mac_addr->sa_data);

	return 0;
}

static int hns3_nic_set_features(struct net_device *netdev,
				 netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	int ret;

	if (changed & (NETIF_F_TSO | NETIF_F_TSO6)) {
		if (features & (NETIF_F_TSO | NETIF_F_TSO6)) {
			priv->ops.fill_desc = hns3_fill_desc_tso;
			priv->ops.maybe_stop_tx = hns3_nic_maybe_stop_tso;
		} else {
			priv->ops.fill_desc = hns3_fill_desc;
			priv->ops.maybe_stop_tx = hns3_nic_maybe_stop_tx;
		}
	}

	if ((changed & NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    h->ae_algo->ops->enable_vlan_filter) {
		if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
			h->ae_algo->ops->enable_vlan_filter(h, true);
		else
			h->ae_algo->ops->enable_vlan_filter(h, false);
	}

	if ((changed & NETIF_F_HW_VLAN_CTAG_RX) &&
	    h->ae_algo->ops->enable_hw_strip_rxvtag) {
		if (features & NETIF_F_HW_VLAN_CTAG_RX)
			ret = h->ae_algo->ops->enable_hw_strip_rxvtag(h, true);
		else
			ret = h->ae_algo->ops->enable_hw_strip_rxvtag(h, false);

		if (ret)
			return ret;
	}

	netdev->features = features;
	return 0;
}

static void hns3_nic_get_stats64(struct net_device *netdev,
				 struct rtnl_link_stats64 *stats)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int queue_num = priv->ae_handle->kinfo.num_tqps;
	struct hnae3_handle *handle = priv->ae_handle;
	struct hns3_enet_ring *ring;
	unsigned int start;
	unsigned int idx;
	u64 tx_bytes = 0;
	u64 rx_bytes = 0;
	u64 tx_pkts = 0;
	u64 rx_pkts = 0;
	u64 tx_drop = 0;
	u64 rx_drop = 0;

	if (test_bit(HNS3_NIC_STATE_DOWN, &priv->state))
		return;

	handle->ae_algo->ops->update_stats(handle, &netdev->stats);

	for (idx = 0; idx < queue_num; idx++) {
		/* fetch the tx stats */
		ring = priv->ring_data[idx].ring;
		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			tx_bytes += ring->stats.tx_bytes;
			tx_pkts += ring->stats.tx_pkts;
			tx_drop += ring->stats.tx_busy;
			tx_drop += ring->stats.sw_err_cnt;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));

		/* fetch the rx stats */
		ring = priv->ring_data[idx + queue_num].ring;
		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			rx_bytes += ring->stats.rx_bytes;
			rx_pkts += ring->stats.rx_pkts;
			rx_drop += ring->stats.non_vld_descs;
			rx_drop += ring->stats.err_pkt_len;
			rx_drop += ring->stats.l2_err;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));
	}

	stats->tx_bytes = tx_bytes;
	stats->tx_packets = tx_pkts;
	stats->rx_bytes = rx_bytes;
	stats->rx_packets = rx_pkts;

	stats->rx_errors = netdev->stats.rx_errors;
	stats->multicast = netdev->stats.multicast;
	stats->rx_length_errors = netdev->stats.rx_length_errors;
	stats->rx_crc_errors = netdev->stats.rx_crc_errors;
	stats->rx_missed_errors = netdev->stats.rx_missed_errors;

	stats->tx_errors = netdev->stats.tx_errors;
	stats->rx_dropped = rx_drop + netdev->stats.rx_dropped;
	stats->tx_dropped = tx_drop + netdev->stats.tx_dropped;
	stats->collisions = netdev->stats.collisions;
	stats->rx_over_errors = netdev->stats.rx_over_errors;
	stats->rx_frame_errors = netdev->stats.rx_frame_errors;
	stats->rx_fifo_errors = netdev->stats.rx_fifo_errors;
	stats->tx_aborted_errors = netdev->stats.tx_aborted_errors;
	stats->tx_carrier_errors = netdev->stats.tx_carrier_errors;
	stats->tx_fifo_errors = netdev->stats.tx_fifo_errors;
	stats->tx_heartbeat_errors = netdev->stats.tx_heartbeat_errors;
	stats->tx_window_errors = netdev->stats.tx_window_errors;
	stats->rx_compressed = netdev->stats.rx_compressed;
	stats->tx_compressed = netdev->stats.tx_compressed;
}

static void hns3_add_tunnel_port(struct net_device *netdev, u16 port,
				 enum hns3_udp_tnl_type type)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hns3_udp_tunnel *udp_tnl = &priv->udp_tnl[type];
	struct hnae3_handle *h = priv->ae_handle;

	if (udp_tnl->used && udp_tnl->dst_port == port) {
		udp_tnl->used++;
		return;
	}

	if (udp_tnl->used) {
		netdev_warn(netdev,
			    "UDP tunnel [%d], port [%d] offload\n", type, port);
		return;
	}

	udp_tnl->dst_port = port;
	udp_tnl->used = 1;
	/* TBD send command to hardware to add port */
	if (h->ae_algo->ops->add_tunnel_udp)
		h->ae_algo->ops->add_tunnel_udp(h, port);
}

static void hns3_del_tunnel_port(struct net_device *netdev, u16 port,
				 enum hns3_udp_tnl_type type)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hns3_udp_tunnel *udp_tnl = &priv->udp_tnl[type];
	struct hnae3_handle *h = priv->ae_handle;

	if (!udp_tnl->used || udp_tnl->dst_port != port) {
		netdev_warn(netdev,
			    "Invalid UDP tunnel port %d\n", port);
		return;
	}

	udp_tnl->used--;
	if (udp_tnl->used)
		return;

	udp_tnl->dst_port = 0;
	/* TBD send command to hardware to del port  */
	if (h->ae_algo->ops->del_tunnel_udp)
		h->ae_algo->ops->del_tunnel_udp(h, port);
}

/* hns3_nic_udp_tunnel_add - Get notifiacetion about UDP tunnel ports
 * @netdev: This physical ports's netdev
 * @ti: Tunnel information
 */
static void hns3_nic_udp_tunnel_add(struct net_device *netdev,
				    struct udp_tunnel_info *ti)
{
	u16 port_n = ntohs(ti->port);

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		hns3_add_tunnel_port(netdev, port_n, HNS3_UDP_TNL_VXLAN);
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		hns3_add_tunnel_port(netdev, port_n, HNS3_UDP_TNL_GENEVE);
		break;
	default:
		netdev_err(netdev, "unsupported tunnel type %d\n", ti->type);
		break;
	}
}

static void hns3_nic_udp_tunnel_del(struct net_device *netdev,
				    struct udp_tunnel_info *ti)
{
	u16 port_n = ntohs(ti->port);

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		hns3_del_tunnel_port(netdev, port_n, HNS3_UDP_TNL_VXLAN);
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		hns3_del_tunnel_port(netdev, port_n, HNS3_UDP_TNL_GENEVE);
		break;
	default:
		break;
	}
}

static int hns3_setup_tc(struct net_device *netdev, void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio_qopt = type_data;
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	u8 *prio_tc = mqprio_qopt->qopt.prio_tc_map;
	u8 tc = mqprio_qopt->qopt.num_tc;
	u16 mode = mqprio_qopt->mode;
	u8 hw = mqprio_qopt->qopt.hw;
	bool if_running;
	unsigned int i;
	int ret;

	if (!((hw == TC_MQPRIO_HW_OFFLOAD_TCS &&
	       mode == TC_MQPRIO_MODE_CHANNEL) || (!hw && tc == 0)))
		return -EOPNOTSUPP;

	if (tc > HNAE3_MAX_TC)
		return -EINVAL;

	if (!netdev)
		return -EINVAL;

	if_running = netif_running(netdev);
	if (if_running) {
		hns3_nic_net_stop(netdev);
		msleep(100);
	}

	ret = (kinfo->dcb_ops && kinfo->dcb_ops->setup_tc) ?
		kinfo->dcb_ops->setup_tc(h, tc, prio_tc) : -EOPNOTSUPP;
	if (ret)
		goto out;

	if (tc <= 1) {
		netdev_reset_tc(netdev);
	} else {
		ret = netdev_set_num_tc(netdev, tc);
		if (ret)
			goto out;

		for (i = 0; i < HNAE3_MAX_TC; i++) {
			if (!kinfo->tc_info[i].enable)
				continue;

			netdev_set_tc_queue(netdev,
					    kinfo->tc_info[i].tc,
					    kinfo->tc_info[i].tqp_count,
					    kinfo->tc_info[i].tqp_offset);
		}
	}

	ret = hns3_nic_set_real_num_queue(netdev);

out:
	if (if_running)
		hns3_nic_net_open(netdev);

	return ret;
}

static int hns3_nic_setup_tc(struct net_device *dev, enum tc_setup_type type,
			     void *type_data)
{
	if (type != TC_SETUP_QDISC_MQPRIO)
		return -EOPNOTSUPP;

	return hns3_setup_tc(dev, type_data);
}

static int hns3_vlan_rx_add_vid(struct net_device *netdev,
				__be16 proto, u16 vid)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret = -EIO;

	if (h->ae_algo->ops->set_vlan_filter)
		ret = h->ae_algo->ops->set_vlan_filter(h, proto, vid, false);

	if (!ret)
		set_bit(vid, priv->active_vlans);

	return ret;
}

static int hns3_vlan_rx_kill_vid(struct net_device *netdev,
				 __be16 proto, u16 vid)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret = -EIO;

	if (h->ae_algo->ops->set_vlan_filter)
		ret = h->ae_algo->ops->set_vlan_filter(h, proto, vid, true);

	if (!ret)
		clear_bit(vid, priv->active_vlans);

	return ret;
}

static void hns3_restore_vlan(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	u16 vid;
	int ret;

	for_each_set_bit(vid, priv->active_vlans, VLAN_N_VID) {
		ret = hns3_vlan_rx_add_vid(netdev, htons(ETH_P_8021Q), vid);
		if (ret)
			netdev_warn(netdev, "Restore vlan: %d filter, ret:%d\n",
				    vid, ret);
	}
}

static int hns3_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan,
				u8 qos, __be16 vlan_proto)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = -EIO;

	if (h->ae_algo->ops->set_vf_vlan_filter)
		ret = h->ae_algo->ops->set_vf_vlan_filter(h, vf, vlan,
						   qos, vlan_proto);

	return ret;
}

static int hns3_nic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	bool if_running = netif_running(netdev);
	int ret;

	if (!h->ae_algo->ops->set_mtu)
		return -EOPNOTSUPP;

	/* if this was called with netdev up then bring netdevice down */
	if (if_running) {
		(void)hns3_nic_net_stop(netdev);
		msleep(100);
	}

	ret = h->ae_algo->ops->set_mtu(h, new_mtu);
	if (ret) {
		netdev_err(netdev, "failed to change MTU in hardware %d\n",
			   ret);
		return ret;
	}

	netdev->mtu = new_mtu;

	/* if the netdev was running earlier, bring it up again */
	if (if_running && hns3_nic_net_open(netdev))
		ret = -EINVAL;

	return ret;
}

static bool hns3_get_tx_timeo_queue_info(struct net_device *ndev)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hns3_enet_ring *tx_ring = NULL;
	int timeout_queue = 0;
	int hw_head, hw_tail;
	int i;

	/* Find the stopped queue the same way the stack does */
	for (i = 0; i < ndev->real_num_tx_queues; i++) {
		struct netdev_queue *q;
		unsigned long trans_start;

		q = netdev_get_tx_queue(ndev, i);
		trans_start = q->trans_start;
		if (netif_xmit_stopped(q) &&
		    time_after(jiffies,
			       (trans_start + ndev->watchdog_timeo))) {
			timeout_queue = i;
			break;
		}
	}

	if (i == ndev->num_tx_queues) {
		netdev_info(ndev,
			    "no netdev TX timeout queue found, timeout count: %llu\n",
			    priv->tx_timeout_count);
		return false;
	}

	tx_ring = priv->ring_data[timeout_queue].ring;

	hw_head = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_HEAD_REG);
	hw_tail = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_TAIL_REG);
	netdev_info(ndev,
		    "tx_timeout count: %llu, queue id: %d, SW_NTU: 0x%x, SW_NTC: 0x%x, HW_HEAD: 0x%x, HW_TAIL: 0x%x, INT: 0x%x\n",
		    priv->tx_timeout_count,
		    timeout_queue,
		    tx_ring->next_to_use,
		    tx_ring->next_to_clean,
		    hw_head,
		    hw_tail,
		    readl(tx_ring->tqp_vector->mask_addr));

	return true;
}

static void hns3_nic_net_timeout(struct net_device *ndev)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = priv->ae_handle;

	if (!hns3_get_tx_timeo_queue_info(ndev))
		return;

	priv->tx_timeout_count++;

	if (time_before(jiffies, (h->last_reset_time + ndev->watchdog_timeo)))
		return;

	/* request the reset */
	if (h->ae_algo->ops->reset_event)
		h->ae_algo->ops->reset_event(h);
}

static const struct net_device_ops hns3_nic_netdev_ops = {
	.ndo_open		= hns3_nic_net_open,
	.ndo_stop		= hns3_nic_net_stop,
	.ndo_start_xmit		= hns3_nic_net_xmit,
	.ndo_tx_timeout		= hns3_nic_net_timeout,
	.ndo_set_mac_address	= hns3_nic_net_set_mac_address,
	.ndo_change_mtu		= hns3_nic_change_mtu,
	.ndo_set_features	= hns3_nic_set_features,
	.ndo_get_stats64	= hns3_nic_get_stats64,
	.ndo_setup_tc		= hns3_nic_setup_tc,
	.ndo_set_rx_mode	= hns3_nic_set_rx_mode,
	.ndo_udp_tunnel_add	= hns3_nic_udp_tunnel_add,
	.ndo_udp_tunnel_del	= hns3_nic_udp_tunnel_del,
	.ndo_vlan_rx_add_vid	= hns3_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= hns3_vlan_rx_kill_vid,
	.ndo_set_vf_vlan	= hns3_ndo_set_vf_vlan,
};

/* hns3_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in hns3_pci_tbl
 *
 * hns3_probe initializes a PF identified by a pci_dev structure.
 * The OS initialization, configuring of the PF private structure,
 * and a hardware reset occur.
 *
 * Returns 0 on success, negative on failure
 */
static int hns3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct hnae3_ae_dev *ae_dev;
	int ret;

	ae_dev = devm_kzalloc(&pdev->dev, sizeof(*ae_dev),
			      GFP_KERNEL);
	if (!ae_dev) {
		ret = -ENOMEM;
		return ret;
	}

	ae_dev->pdev = pdev;
	ae_dev->flag = ent->driver_data;
	ae_dev->dev_type = HNAE3_DEV_KNIC;
	pci_set_drvdata(pdev, ae_dev);

	return hnae3_register_ae_dev(ae_dev);
}

/* hns3_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void hns3_remove(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	hnae3_unregister_ae_dev(ae_dev);
}

static struct pci_driver hns3_driver = {
	.name     = hns3_driver_name,
	.id_table = hns3_pci_tbl,
	.probe    = hns3_probe,
	.remove   = hns3_remove,
};

/* set default feature to hns3 */
static void hns3_set_default_feature(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	netdev->priv_flags |= IFF_UNICAST_FLT;

	netdev->hw_enc_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM;

	netdev->hw_enc_features |= NETIF_F_TSO_MANGLEID;

	netdev->gso_partial_features |= NETIF_F_GSO_GRE_CSUM;

	netdev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_HW_VLAN_CTAG_FILTER |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM;

	netdev->vlan_features |=
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM |
		NETIF_F_SG | NETIF_F_GSO | NETIF_F_GRO |
		NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM;

	netdev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_HW_VLAN_CTAG_TX |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM;

	if (!(h->flags & HNAE3_SUPPORT_VF))
		netdev->hw_features |=
			NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_CTAG_RX;
}

static int hns3_alloc_buffer(struct hns3_enet_ring *ring,
			     struct hns3_desc_cb *cb)
{
	unsigned int order = hnae_page_order(ring);
	struct page *p;

	p = dev_alloc_pages(order);
	if (!p)
		return -ENOMEM;

	cb->priv = p;
	cb->page_offset = 0;
	cb->reuse_flag = 0;
	cb->buf  = page_address(p);
	cb->length = hnae_page_size(ring);
	cb->type = DESC_TYPE_PAGE;

	return 0;
}

static void hns3_free_buffer(struct hns3_enet_ring *ring,
			     struct hns3_desc_cb *cb)
{
	if (cb->type == DESC_TYPE_SKB)
		dev_kfree_skb_any((struct sk_buff *)cb->priv);
	else if (!HNAE3_IS_TX_RING(ring))
		put_page((struct page *)cb->priv);
	memset(cb, 0, sizeof(*cb));
}

static int hns3_map_buffer(struct hns3_enet_ring *ring, struct hns3_desc_cb *cb)
{
	cb->dma = dma_map_page(ring_to_dev(ring), cb->priv, 0,
			       cb->length, ring_to_dma_dir(ring));

	if (dma_mapping_error(ring_to_dev(ring), cb->dma))
		return -EIO;

	return 0;
}

static void hns3_unmap_buffer(struct hns3_enet_ring *ring,
			      struct hns3_desc_cb *cb)
{
	if (cb->type == DESC_TYPE_SKB)
		dma_unmap_single(ring_to_dev(ring), cb->dma, cb->length,
				 ring_to_dma_dir(ring));
	else
		dma_unmap_page(ring_to_dev(ring), cb->dma, cb->length,
			       ring_to_dma_dir(ring));
}

static void hns3_buffer_detach(struct hns3_enet_ring *ring, int i)
{
	hns3_unmap_buffer(ring, &ring->desc_cb[i]);
	ring->desc[i].addr = 0;
}

static void hns3_free_buffer_detach(struct hns3_enet_ring *ring, int i)
{
	struct hns3_desc_cb *cb = &ring->desc_cb[i];

	if (!ring->desc_cb[i].dma)
		return;

	hns3_buffer_detach(ring, i);
	hns3_free_buffer(ring, cb);
}

static void hns3_free_buffers(struct hns3_enet_ring *ring)
{
	int i;

	for (i = 0; i < ring->desc_num; i++)
		hns3_free_buffer_detach(ring, i);
}

/* free desc along with its attached buffer */
static void hns3_free_desc(struct hns3_enet_ring *ring)
{
	hns3_free_buffers(ring);

	dma_unmap_single(ring_to_dev(ring), ring->desc_dma_addr,
			 ring->desc_num * sizeof(ring->desc[0]),
			 DMA_BIDIRECTIONAL);
	ring->desc_dma_addr = 0;
	kfree(ring->desc);
	ring->desc = NULL;
}

static int hns3_alloc_desc(struct hns3_enet_ring *ring)
{
	int size = ring->desc_num * sizeof(ring->desc[0]);

	ring->desc = kzalloc(size, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	ring->desc_dma_addr = dma_map_single(ring_to_dev(ring), ring->desc,
					     size, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(ring_to_dev(ring), ring->desc_dma_addr)) {
		ring->desc_dma_addr = 0;
		kfree(ring->desc);
		ring->desc = NULL;
		return -ENOMEM;
	}

	return 0;
}

static int hns3_reserve_buffer_map(struct hns3_enet_ring *ring,
				   struct hns3_desc_cb *cb)
{
	int ret;

	ret = hns3_alloc_buffer(ring, cb);
	if (ret)
		goto out;

	ret = hns3_map_buffer(ring, cb);
	if (ret)
		goto out_with_buf;

	return 0;

out_with_buf:
	hns3_free_buffer(ring, cb);
out:
	return ret;
}

static int hns3_alloc_buffer_attach(struct hns3_enet_ring *ring, int i)
{
	int ret = hns3_reserve_buffer_map(ring, &ring->desc_cb[i]);

	if (ret)
		return ret;

	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma);

	return 0;
}

/* Allocate memory for raw pkg, and map with dma */
static int hns3_alloc_ring_buffers(struct hns3_enet_ring *ring)
{
	int i, j, ret;

	for (i = 0; i < ring->desc_num; i++) {
		ret = hns3_alloc_buffer_attach(ring, i);
		if (ret)
			goto out_buffer_fail;
	}

	return 0;

out_buffer_fail:
	for (j = i - 1; j >= 0; j--)
		hns3_free_buffer_detach(ring, j);
	return ret;
}

/* detach a in-used buffer and replace with a reserved one  */
static void hns3_replace_buffer(struct hns3_enet_ring *ring, int i,
				struct hns3_desc_cb *res_cb)
{
	hns3_unmap_buffer(ring, &ring->desc_cb[i]);
	ring->desc_cb[i] = *res_cb;
	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma);
}

static void hns3_reuse_buffer(struct hns3_enet_ring *ring, int i)
{
	ring->desc_cb[i].reuse_flag = 0;
	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma
		+ ring->desc_cb[i].page_offset);
}

static void hns3_nic_reclaim_one_desc(struct hns3_enet_ring *ring, int *bytes,
				      int *pkts)
{
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_clean];

	(*pkts) += (desc_cb->type == DESC_TYPE_SKB);
	(*bytes) += desc_cb->length;
	/* desc_cb will be cleaned, after hnae_free_buffer_detach*/
	hns3_free_buffer_detach(ring, ring->next_to_clean);

	ring_ptr_move_fw(ring, next_to_clean);
}

static int is_valid_clean_head(struct hns3_enet_ring *ring, int h)
{
	int u = ring->next_to_use;
	int c = ring->next_to_clean;

	if (unlikely(h > ring->desc_num))
		return 0;

	return u > c ? (h > c && h <= u) : (h > c || h <= u);
}

bool hns3_clean_tx_ring(struct hns3_enet_ring *ring, int budget)
{
	struct net_device *netdev = ring->tqp->handle->kinfo.netdev;
	struct netdev_queue *dev_queue;
	int bytes, pkts;
	int head;

	head = readl_relaxed(ring->tqp->io_base + HNS3_RING_TX_RING_HEAD_REG);
	rmb(); /* Make sure head is ready before touch any data */

	if (is_ring_empty(ring) || head == ring->next_to_clean)
		return true; /* no data to poll */

	if (!is_valid_clean_head(ring, head)) {
		netdev_err(netdev, "wrong head (%d, %d-%d)\n", head,
			   ring->next_to_use, ring->next_to_clean);

		u64_stats_update_begin(&ring->syncp);
		ring->stats.io_err_cnt++;
		u64_stats_update_end(&ring->syncp);
		return true;
	}

	bytes = 0;
	pkts = 0;
	while (head != ring->next_to_clean && budget) {
		hns3_nic_reclaim_one_desc(ring, &bytes, &pkts);
		/* Issue prefetch for next Tx descriptor */
		prefetch(&ring->desc_cb[ring->next_to_clean]);
		budget--;
	}

	ring->tqp_vector->tx_group.total_bytes += bytes;
	ring->tqp_vector->tx_group.total_packets += pkts;

	u64_stats_update_begin(&ring->syncp);
	ring->stats.tx_bytes += bytes;
	ring->stats.tx_pkts += pkts;
	u64_stats_update_end(&ring->syncp);

	dev_queue = netdev_get_tx_queue(netdev, ring->tqp->tqp_index);
	netdev_tx_completed_queue(dev_queue, pkts, bytes);

	if (unlikely(pkts && netif_carrier_ok(netdev) &&
		     (ring_space(ring) > HNS3_MAX_BD_PER_PKT))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (netif_tx_queue_stopped(dev_queue)) {
			netif_tx_wake_queue(dev_queue);
			ring->stats.restart_queue++;
		}
	}

	return !!budget;
}

static int hns3_desc_unused(struct hns3_enet_ring *ring)
{
	int ntc = ring->next_to_clean;
	int ntu = ring->next_to_use;

	return ((ntc >= ntu) ? 0 : ring->desc_num) + ntc - ntu;
}

static void
hns3_nic_alloc_rx_buffers(struct hns3_enet_ring *ring, int cleand_count)
{
	struct hns3_desc_cb *desc_cb;
	struct hns3_desc_cb res_cbs;
	int i, ret;

	for (i = 0; i < cleand_count; i++) {
		desc_cb = &ring->desc_cb[ring->next_to_use];
		if (desc_cb->reuse_flag) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.reuse_pg_cnt++;
			u64_stats_update_end(&ring->syncp);

			hns3_reuse_buffer(ring, ring->next_to_use);
		} else {
			ret = hns3_reserve_buffer_map(ring, &res_cbs);
			if (ret) {
				u64_stats_update_begin(&ring->syncp);
				ring->stats.sw_err_cnt++;
				u64_stats_update_end(&ring->syncp);

				netdev_err(ring->tqp->handle->kinfo.netdev,
					   "hnae reserve buffer map failed.\n");
				break;
			}
			hns3_replace_buffer(ring, ring->next_to_use, &res_cbs);
		}

		ring_ptr_move_fw(ring, next_to_use);
	}

	wmb(); /* Make all data has been write before submit */
	writel_relaxed(i, ring->tqp->io_base + HNS3_RING_RX_RING_HEAD_REG);
}

/* hns3_nic_get_headlen - determine size of header for LRO/GRO
 * @data: pointer to the start of the headers
 * @max: total length of section to find headers in
 *
 * This function is meant to determine the length of headers that will
 * be recognized by hardware for LRO, GRO, and RSC offloads.  The main
 * motivation of doing this is to only perform one pull for IPv4 TCP
 * packets so that we can do basic things like calculating the gso_size
 * based on the average data per packet.
 */
static unsigned int hns3_nic_get_headlen(unsigned char *data, u32 flag,
					 unsigned int max_size)
{
	unsigned char *network;
	u8 hlen;

	/* This should never happen, but better safe than sorry */
	if (max_size < ETH_HLEN)
		return max_size;

	/* Initialize network frame pointer */
	network = data;

	/* Set first protocol and move network header forward */
	network += ETH_HLEN;

	/* Handle any vlan tag if present */
	if (hnae_get_field(flag, HNS3_RXD_VLAN_M, HNS3_RXD_VLAN_S)
		== HNS3_RX_FLAG_VLAN_PRESENT) {
		if ((typeof(max_size))(network - data) > (max_size - VLAN_HLEN))
			return max_size;

		network += VLAN_HLEN;
	}

	/* Handle L3 protocols */
	if (hnae_get_field(flag, HNS3_RXD_L3ID_M, HNS3_RXD_L3ID_S)
		== HNS3_RX_FLAG_L3ID_IPV4) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct iphdr)))
			return max_size;

		/* Access ihl as a u8 to avoid unaligned access on ia64 */
		hlen = (network[0] & 0x0F) << 2;

		/* Verify hlen meets minimum size requirements */
		if (hlen < sizeof(struct iphdr))
			return network - data;

		/* Record next protocol if header is present */
	} else if (hnae_get_field(flag, HNS3_RXD_L3ID_M, HNS3_RXD_L3ID_S)
		== HNS3_RX_FLAG_L3ID_IPV6) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct ipv6hdr)))
			return max_size;

		/* Record next protocol */
		hlen = sizeof(struct ipv6hdr);
	} else {
		return network - data;
	}

	/* Relocate pointer to start of L4 header */
	network += hlen;

	/* Finally sort out TCP/UDP */
	if (hnae_get_field(flag, HNS3_RXD_L4ID_M, HNS3_RXD_L4ID_S)
		== HNS3_RX_FLAG_L4ID_TCP) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct tcphdr)))
			return max_size;

		/* Access doff as a u8 to avoid unaligned access on ia64 */
		hlen = (network[12] & 0xF0) >> 2;

		/* Verify hlen meets minimum size requirements */
		if (hlen < sizeof(struct tcphdr))
			return network - data;

		network += hlen;
	} else if (hnae_get_field(flag, HNS3_RXD_L4ID_M, HNS3_RXD_L4ID_S)
		== HNS3_RX_FLAG_L4ID_UDP) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct udphdr)))
			return max_size;

		network += sizeof(struct udphdr);
	}

	/* If everything has gone correctly network should be the
	 * data section of the packet and will be the end of the header.
	 * If not then it probably represents the end of the last recognized
	 * header.
	 */
	if ((typeof(max_size))(network - data) < max_size)
		return network - data;
	else
		return max_size;
}

static void hns3_nic_reuse_page(struct sk_buff *skb, int i,
				struct hns3_enet_ring *ring, int pull_len,
				struct hns3_desc_cb *desc_cb)
{
	struct hns3_desc *desc;
	int truesize, size;
	int last_offset;
	bool twobufs;

	twobufs = ((PAGE_SIZE < 8192) &&
		hnae_buf_size(ring) == HNS3_BUFFER_SIZE_2048);

	desc = &ring->desc[ring->next_to_clean];
	size = le16_to_cpu(desc->rx.size);

	truesize = hnae_buf_size(ring);

	if (!twobufs)
		last_offset = hnae_page_size(ring) - hnae_buf_size(ring);

	skb_add_rx_frag(skb, i, desc_cb->priv, desc_cb->page_offset + pull_len,
			size - pull_len, truesize);

	 /* Avoid re-using remote pages,flag default unreuse */
	if (unlikely(page_to_nid(desc_cb->priv) != numa_node_id()))
		return;

	if (twobufs) {
		/* If we are only owner of page we can reuse it */
		if (likely(page_count(desc_cb->priv) == 1)) {
			/* Flip page offset to other buffer */
			desc_cb->page_offset ^= truesize;

			desc_cb->reuse_flag = 1;
			/* bump ref count on page before it is given*/
			get_page(desc_cb->priv);
		}
		return;
	}

	/* Move offset up to the next cache line */
	desc_cb->page_offset += truesize;

	if (desc_cb->page_offset <= last_offset) {
		desc_cb->reuse_flag = 1;
		/* Bump ref count on page before it is given*/
		get_page(desc_cb->priv);
	}
}

static void hns3_rx_checksum(struct hns3_enet_ring *ring, struct sk_buff *skb,
			     struct hns3_desc *desc)
{
	struct net_device *netdev = ring->tqp->handle->kinfo.netdev;
	int l3_type, l4_type;
	u32 bd_base_info;
	int ol4_type;
	u32 l234info;

	bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
	l234info = le32_to_cpu(desc->rx.l234_info);

	skb->ip_summed = CHECKSUM_NONE;

	skb_checksum_none_assert(skb);

	if (!(netdev->features & NETIF_F_RXCSUM))
		return;

	/* check if hardware has done checksum */
	if (!hnae_get_bit(bd_base_info, HNS3_RXD_L3L4P_B))
		return;

	if (unlikely(hnae_get_bit(l234info, HNS3_RXD_L3E_B) ||
		     hnae_get_bit(l234info, HNS3_RXD_L4E_B) ||
		     hnae_get_bit(l234info, HNS3_RXD_OL3E_B) ||
		     hnae_get_bit(l234info, HNS3_RXD_OL4E_B))) {
		netdev_err(netdev, "L3/L4 error pkt\n");
		u64_stats_update_begin(&ring->syncp);
		ring->stats.l3l4_csum_err++;
		u64_stats_update_end(&ring->syncp);

		return;
	}

	l3_type = hnae_get_field(l234info, HNS3_RXD_L3ID_M,
				 HNS3_RXD_L3ID_S);
	l4_type = hnae_get_field(l234info, HNS3_RXD_L4ID_M,
				 HNS3_RXD_L4ID_S);

	ol4_type = hnae_get_field(l234info, HNS3_RXD_OL4ID_M, HNS3_RXD_OL4ID_S);
	switch (ol4_type) {
	case HNS3_OL4_TYPE_MAC_IN_UDP:
	case HNS3_OL4_TYPE_NVGRE:
		skb->csum_level = 1;
	case HNS3_OL4_TYPE_NO_TUN:
		/* Can checksum ipv4 or ipv6 + UDP/TCP/SCTP packets */
		if (l3_type == HNS3_L3_TYPE_IPV4 ||
		    (l3_type == HNS3_L3_TYPE_IPV6 &&
		     (l4_type == HNS3_L4_TYPE_UDP ||
		      l4_type == HNS3_L4_TYPE_TCP ||
		      l4_type == HNS3_L4_TYPE_SCTP)))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		break;
	}
}

static void hns3_rx_skb(struct hns3_enet_ring *ring, struct sk_buff *skb)
{
	napi_gro_receive(&ring->tqp_vector->napi, skb);
}

static int hns3_handle_rx_bd(struct hns3_enet_ring *ring,
			     struct sk_buff **out_skb, int *out_bnum)
{
	struct net_device *netdev = ring->tqp->handle->kinfo.netdev;
	struct hns3_desc_cb *desc_cb;
	struct hns3_desc *desc;
	struct sk_buff *skb;
	unsigned char *va;
	u32 bd_base_info;
	int pull_len;
	u32 l234info;
	int length;
	int bnum;

	desc = &ring->desc[ring->next_to_clean];
	desc_cb = &ring->desc_cb[ring->next_to_clean];

	prefetch(desc);

	length = le16_to_cpu(desc->rx.pkt_len);
	bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
	l234info = le32_to_cpu(desc->rx.l234_info);

	/* Check valid BD */
	if (!hnae_get_bit(bd_base_info, HNS3_RXD_VLD_B))
		return -EFAULT;

	va = (unsigned char *)desc_cb->buf + desc_cb->page_offset;

	/* Prefetch first cache line of first page
	 * Idea is to cache few bytes of the header of the packet. Our L1 Cache
	 * line size is 64B so need to prefetch twice to make it 128B. But in
	 * actual we can have greater size of caches with 128B Level 1 cache
	 * lines. In such a case, single fetch would suffice to cache in the
	 * relevant part of the header.
	 */
	prefetch(va);
#if L1_CACHE_BYTES < 128
	prefetch(va + L1_CACHE_BYTES);
#endif

	skb = *out_skb = napi_alloc_skb(&ring->tqp_vector->napi,
					HNS3_RX_HEAD_SIZE);
	if (unlikely(!skb)) {
		netdev_err(netdev, "alloc rx skb fail\n");

		u64_stats_update_begin(&ring->syncp);
		ring->stats.sw_err_cnt++;
		u64_stats_update_end(&ring->syncp);

		return -ENOMEM;
	}

	prefetchw(skb->data);

	/* Based on hw strategy, the tag offloaded will be stored at
	 * ot_vlan_tag in two layer tag case, and stored at vlan_tag
	 * in one layer tag case.
	 */
	if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX) {
		u16 vlan_tag;

		vlan_tag = le16_to_cpu(desc->rx.ot_vlan_tag);
		if (!(vlan_tag & VLAN_VID_MASK))
			vlan_tag = le16_to_cpu(desc->rx.vlan_tag);
		if (vlan_tag & VLAN_VID_MASK)
			__vlan_hwaccel_put_tag(skb,
					       htons(ETH_P_8021Q),
					       vlan_tag);
	}

	bnum = 1;
	if (length <= HNS3_RX_HEAD_SIZE) {
		memcpy(__skb_put(skb, length), va, ALIGN(length, sizeof(long)));

		/* We can reuse buffer as-is, just make sure it is local */
		if (likely(page_to_nid(desc_cb->priv) == numa_node_id()))
			desc_cb->reuse_flag = 1;
		else /* This page cannot be reused so discard it */
			put_page(desc_cb->priv);

		ring_ptr_move_fw(ring, next_to_clean);
	} else {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.seg_pkt_cnt++;
		u64_stats_update_end(&ring->syncp);

		pull_len = hns3_nic_get_headlen(va, l234info,
						HNS3_RX_HEAD_SIZE);
		memcpy(__skb_put(skb, pull_len), va,
		       ALIGN(pull_len, sizeof(long)));

		hns3_nic_reuse_page(skb, 0, ring, pull_len, desc_cb);
		ring_ptr_move_fw(ring, next_to_clean);

		while (!hnae_get_bit(bd_base_info, HNS3_RXD_FE_B)) {
			desc = &ring->desc[ring->next_to_clean];
			desc_cb = &ring->desc_cb[ring->next_to_clean];
			bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
			hns3_nic_reuse_page(skb, bnum, ring, 0, desc_cb);
			ring_ptr_move_fw(ring, next_to_clean);
			bnum++;
		}
	}

	*out_bnum = bnum;

	if (unlikely(!hnae_get_bit(bd_base_info, HNS3_RXD_VLD_B))) {
		netdev_err(netdev, "no valid bd,%016llx,%016llx\n",
			   ((u64 *)desc)[0], ((u64 *)desc)[1]);
		u64_stats_update_begin(&ring->syncp);
		ring->stats.non_vld_descs++;
		u64_stats_update_end(&ring->syncp);

		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	if (unlikely((!desc->rx.pkt_len) ||
		     hnae_get_bit(l234info, HNS3_RXD_TRUNCAT_B))) {
		netdev_err(netdev, "truncated pkt\n");
		u64_stats_update_begin(&ring->syncp);
		ring->stats.err_pkt_len++;
		u64_stats_update_end(&ring->syncp);

		dev_kfree_skb_any(skb);
		return -EFAULT;
	}

	if (unlikely(hnae_get_bit(l234info, HNS3_RXD_L2E_B))) {
		netdev_err(netdev, "L2 error pkt\n");
		u64_stats_update_begin(&ring->syncp);
		ring->stats.l2_err++;
		u64_stats_update_end(&ring->syncp);

		dev_kfree_skb_any(skb);
		return -EFAULT;
	}

	u64_stats_update_begin(&ring->syncp);
	ring->stats.rx_pkts++;
	ring->stats.rx_bytes += skb->len;
	u64_stats_update_end(&ring->syncp);

	ring->tqp_vector->rx_group.total_bytes += skb->len;

	hns3_rx_checksum(ring, skb, desc);
	return 0;
}

int hns3_clean_rx_ring(
		struct hns3_enet_ring *ring, int budget,
		void (*rx_fn)(struct hns3_enet_ring *, struct sk_buff *))
{
#define RCB_NOF_ALLOC_RX_BUFF_ONCE 16
	struct net_device *netdev = ring->tqp->handle->kinfo.netdev;
	int recv_pkts, recv_bds, clean_count, err;
	int unused_count = hns3_desc_unused(ring);
	struct sk_buff *skb = NULL;
	int num, bnum = 0;

	num = readl_relaxed(ring->tqp->io_base + HNS3_RING_RX_RING_FBDNUM_REG);
	rmb(); /* Make sure num taken effect before the other data is touched */

	recv_pkts = 0, recv_bds = 0, clean_count = 0;
	num -= unused_count;

	while (recv_pkts < budget && recv_bds < num) {
		/* Reuse or realloc buffers */
		if (clean_count + unused_count >= RCB_NOF_ALLOC_RX_BUFF_ONCE) {
			hns3_nic_alloc_rx_buffers(ring,
						  clean_count + unused_count);
			clean_count = 0;
			unused_count = hns3_desc_unused(ring);
		}

		/* Poll one pkt */
		err = hns3_handle_rx_bd(ring, &skb, &bnum);
		if (unlikely(!skb)) /* This fault cannot be repaired */
			goto out;

		recv_bds += bnum;
		clean_count += bnum;
		if (unlikely(err)) {  /* Do jump the err */
			recv_pkts++;
			continue;
		}

		/* Do update ip stack process */
		skb->protocol = eth_type_trans(skb, netdev);
		rx_fn(ring, skb);

		recv_pkts++;
	}

out:
	/* Make all data has been write before submit */
	if (clean_count + unused_count > 0)
		hns3_nic_alloc_rx_buffers(ring,
					  clean_count + unused_count);

	return recv_pkts;
}

static bool hns3_get_new_int_gl(struct hns3_enet_ring_group *ring_group)
{
	struct hns3_enet_tqp_vector *tqp_vector =
					ring_group->ring->tqp_vector;
	enum hns3_flow_level_range new_flow_level;
	int packets_per_msecs;
	int bytes_per_msecs;
	u32 time_passed_ms;
	u16 new_int_gl;

	if (!ring_group->coal.int_gl || !tqp_vector->last_jiffies)
		return false;

	if (ring_group->total_packets == 0) {
		ring_group->coal.int_gl = HNS3_INT_GL_50K;
		ring_group->coal.flow_level = HNS3_FLOW_LOW;
		return true;
	}

	/* Simple throttlerate management
	 * 0-10MB/s   lower     (50000 ints/s)
	 * 10-20MB/s   middle    (20000 ints/s)
	 * 20-1249MB/s high      (18000 ints/s)
	 * > 40000pps  ultra     (8000 ints/s)
	 */
	new_flow_level = ring_group->coal.flow_level;
	new_int_gl = ring_group->coal.int_gl;
	time_passed_ms =
		jiffies_to_msecs(jiffies - tqp_vector->last_jiffies);

	if (!time_passed_ms)
		return false;

	do_div(ring_group->total_packets, time_passed_ms);
	packets_per_msecs = ring_group->total_packets;

	do_div(ring_group->total_bytes, time_passed_ms);
	bytes_per_msecs = ring_group->total_bytes;

#define HNS3_RX_LOW_BYTE_RATE 10000
#define HNS3_RX_MID_BYTE_RATE 20000

	switch (new_flow_level) {
	case HNS3_FLOW_LOW:
		if (bytes_per_msecs > HNS3_RX_LOW_BYTE_RATE)
			new_flow_level = HNS3_FLOW_MID;
		break;
	case HNS3_FLOW_MID:
		if (bytes_per_msecs > HNS3_RX_MID_BYTE_RATE)
			new_flow_level = HNS3_FLOW_HIGH;
		else if (bytes_per_msecs <= HNS3_RX_LOW_BYTE_RATE)
			new_flow_level = HNS3_FLOW_LOW;
		break;
	case HNS3_FLOW_HIGH:
	case HNS3_FLOW_ULTRA:
	default:
		if (bytes_per_msecs <= HNS3_RX_MID_BYTE_RATE)
			new_flow_level = HNS3_FLOW_MID;
		break;
	}

#define HNS3_RX_ULTRA_PACKET_RATE 40

	if (packets_per_msecs > HNS3_RX_ULTRA_PACKET_RATE &&
	    &tqp_vector->rx_group == ring_group)
		new_flow_level = HNS3_FLOW_ULTRA;

	switch (new_flow_level) {
	case HNS3_FLOW_LOW:
		new_int_gl = HNS3_INT_GL_50K;
		break;
	case HNS3_FLOW_MID:
		new_int_gl = HNS3_INT_GL_20K;
		break;
	case HNS3_FLOW_HIGH:
		new_int_gl = HNS3_INT_GL_18K;
		break;
	case HNS3_FLOW_ULTRA:
		new_int_gl = HNS3_INT_GL_8K;
		break;
	default:
		break;
	}

	ring_group->total_bytes = 0;
	ring_group->total_packets = 0;
	ring_group->coal.flow_level = new_flow_level;
	if (new_int_gl != ring_group->coal.int_gl) {
		ring_group->coal.int_gl = new_int_gl;
		return true;
	}
	return false;
}

static void hns3_update_new_int_gl(struct hns3_enet_tqp_vector *tqp_vector)
{
	struct hns3_enet_ring_group *rx_group = &tqp_vector->rx_group;
	struct hns3_enet_ring_group *tx_group = &tqp_vector->tx_group;
	bool rx_update, tx_update;

	if (tqp_vector->int_adapt_down > 0) {
		tqp_vector->int_adapt_down--;
		return;
	}

	if (rx_group->coal.gl_adapt_enable) {
		rx_update = hns3_get_new_int_gl(rx_group);
		if (rx_update)
			hns3_set_vector_coalesce_rx_gl(tqp_vector,
						       rx_group->coal.int_gl);
	}

	if (tx_group->coal.gl_adapt_enable) {
		tx_update = hns3_get_new_int_gl(&tqp_vector->tx_group);
		if (tx_update)
			hns3_set_vector_coalesce_tx_gl(tqp_vector,
						       tx_group->coal.int_gl);
	}

	tqp_vector->last_jiffies = jiffies;
	tqp_vector->int_adapt_down = HNS3_INT_ADAPT_DOWN_START;
}

static int hns3_nic_common_poll(struct napi_struct *napi, int budget)
{
	struct hns3_enet_ring *ring;
	int rx_pkt_total = 0;

	struct hns3_enet_tqp_vector *tqp_vector =
		container_of(napi, struct hns3_enet_tqp_vector, napi);
	bool clean_complete = true;
	int rx_budget;

	/* Since the actual Tx work is minimal, we can give the Tx a larger
	 * budget and be more aggressive about cleaning up the Tx descriptors.
	 */
	hns3_for_each_ring(ring, tqp_vector->tx_group) {
		if (!hns3_clean_tx_ring(ring, budget))
			clean_complete = false;
	}

	/* make sure rx ring budget not smaller than 1 */
	rx_budget = max(budget / tqp_vector->num_tqps, 1);

	hns3_for_each_ring(ring, tqp_vector->rx_group) {
		int rx_cleaned = hns3_clean_rx_ring(ring, rx_budget,
						    hns3_rx_skb);

		if (rx_cleaned >= rx_budget)
			clean_complete = false;

		rx_pkt_total += rx_cleaned;
	}

	tqp_vector->rx_group.total_packets += rx_pkt_total;

	if (!clean_complete)
		return budget;

	napi_complete(napi);
	hns3_update_new_int_gl(tqp_vector);
	hns3_mask_vector_irq(tqp_vector, 1);

	return rx_pkt_total;
}

static int hns3_get_vector_ring_chain(struct hns3_enet_tqp_vector *tqp_vector,
				      struct hnae3_ring_chain_node *head)
{
	struct pci_dev *pdev = tqp_vector->handle->pdev;
	struct hnae3_ring_chain_node *cur_chain = head;
	struct hnae3_ring_chain_node *chain;
	struct hns3_enet_ring *tx_ring;
	struct hns3_enet_ring *rx_ring;

	tx_ring = tqp_vector->tx_group.ring;
	if (tx_ring) {
		cur_chain->tqp_index = tx_ring->tqp->tqp_index;
		hnae_set_bit(cur_chain->flag, HNAE3_RING_TYPE_B,
			     HNAE3_RING_TYPE_TX);
		hnae_set_field(cur_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
			       HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_TX);

		cur_chain->next = NULL;

		while (tx_ring->next) {
			tx_ring = tx_ring->next;

			chain = devm_kzalloc(&pdev->dev, sizeof(*chain),
					     GFP_KERNEL);
			if (!chain)
				return -ENOMEM;

			cur_chain->next = chain;
			chain->tqp_index = tx_ring->tqp->tqp_index;
			hnae_set_bit(chain->flag, HNAE3_RING_TYPE_B,
				     HNAE3_RING_TYPE_TX);
			hnae_set_field(chain->int_gl_idx,
				       HNAE3_RING_GL_IDX_M,
				       HNAE3_RING_GL_IDX_S,
				       HNAE3_RING_GL_TX);

			cur_chain = chain;
		}
	}

	rx_ring = tqp_vector->rx_group.ring;
	if (!tx_ring && rx_ring) {
		cur_chain->next = NULL;
		cur_chain->tqp_index = rx_ring->tqp->tqp_index;
		hnae_set_bit(cur_chain->flag, HNAE3_RING_TYPE_B,
			     HNAE3_RING_TYPE_RX);
		hnae_set_field(cur_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
			       HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_RX);

		rx_ring = rx_ring->next;
	}

	while (rx_ring) {
		chain = devm_kzalloc(&pdev->dev, sizeof(*chain), GFP_KERNEL);
		if (!chain)
			return -ENOMEM;

		cur_chain->next = chain;
		chain->tqp_index = rx_ring->tqp->tqp_index;
		hnae_set_bit(chain->flag, HNAE3_RING_TYPE_B,
			     HNAE3_RING_TYPE_RX);
		hnae_set_field(chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
			       HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_RX);

		cur_chain = chain;

		rx_ring = rx_ring->next;
	}

	return 0;
}

static void hns3_free_vector_ring_chain(struct hns3_enet_tqp_vector *tqp_vector,
					struct hnae3_ring_chain_node *head)
{
	struct pci_dev *pdev = tqp_vector->handle->pdev;
	struct hnae3_ring_chain_node *chain_tmp, *chain;

	chain = head->next;

	while (chain) {
		chain_tmp = chain->next;
		devm_kfree(&pdev->dev, chain);
		chain = chain_tmp;
	}
}

static void hns3_add_ring_to_group(struct hns3_enet_ring_group *group,
				   struct hns3_enet_ring *ring)
{
	ring->next = group->ring;
	group->ring = ring;

	group->count++;
}

static int hns3_nic_init_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_ring_chain_node vector_ring_chain;
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	int ret = 0;
	u16 i;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		hns3_vector_gl_rl_init_hw(tqp_vector, priv);
		tqp_vector->num_tqps = 0;
	}

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		u16 vector_i = i % priv->vector_num;
		u16 tqp_num = h->kinfo.num_tqps;

		tqp_vector = &priv->tqp_vector[vector_i];

		hns3_add_ring_to_group(&tqp_vector->tx_group,
				       priv->ring_data[i].ring);

		hns3_add_ring_to_group(&tqp_vector->rx_group,
				       priv->ring_data[i + tqp_num].ring);

		priv->ring_data[i].ring->tqp_vector = tqp_vector;
		priv->ring_data[i + tqp_num].ring->tqp_vector = tqp_vector;
		tqp_vector->num_tqps++;
	}

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];

		tqp_vector->rx_group.total_bytes = 0;
		tqp_vector->rx_group.total_packets = 0;
		tqp_vector->tx_group.total_bytes = 0;
		tqp_vector->tx_group.total_packets = 0;
		tqp_vector->handle = h;

		ret = hns3_get_vector_ring_chain(tqp_vector,
						 &vector_ring_chain);
		if (ret)
			return ret;

		ret = h->ae_algo->ops->map_ring_to_vector(h,
			tqp_vector->vector_irq, &vector_ring_chain);

		hns3_free_vector_ring_chain(tqp_vector, &vector_ring_chain);

		if (ret)
			return ret;

		netif_napi_add(priv->netdev, &tqp_vector->napi,
			       hns3_nic_common_poll, NAPI_POLL_WEIGHT);
	}

	return 0;
}

static int hns3_nic_alloc_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	struct hnae3_vector_info *vector;
	struct pci_dev *pdev = h->pdev;
	u16 tqp_num = h->kinfo.num_tqps;
	u16 vector_num;
	int ret = 0;
	u16 i;

	/* RSS size, cpu online and vector_num should be the same */
	/* Should consider 2p/4p later */
	vector_num = min_t(u16, num_online_cpus(), tqp_num);
	vector = devm_kcalloc(&pdev->dev, vector_num, sizeof(*vector),
			      GFP_KERNEL);
	if (!vector)
		return -ENOMEM;

	vector_num = h->ae_algo->ops->get_vector(h, vector_num, vector);

	priv->vector_num = vector_num;
	priv->tqp_vector = (struct hns3_enet_tqp_vector *)
		devm_kcalloc(&pdev->dev, vector_num, sizeof(*priv->tqp_vector),
			     GFP_KERNEL);
	if (!priv->tqp_vector) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		tqp_vector->idx = i;
		tqp_vector->mask_addr = vector[i].io_addr;
		tqp_vector->vector_irq = vector[i].vector;
		hns3_vector_gl_rl_init(tqp_vector, priv);
	}

out:
	devm_kfree(&pdev->dev, vector);
	return ret;
}

static void hns3_clear_ring_group(struct hns3_enet_ring_group *group)
{
	group->ring = NULL;
	group->count = 0;
}

static int hns3_nic_uninit_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_ring_chain_node vector_ring_chain;
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	int i, ret;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];

		ret = hns3_get_vector_ring_chain(tqp_vector,
						 &vector_ring_chain);
		if (ret)
			return ret;

		ret = h->ae_algo->ops->unmap_ring_from_vector(h,
			tqp_vector->vector_irq, &vector_ring_chain);
		if (ret)
			return ret;

		ret = h->ae_algo->ops->put_vector(h, tqp_vector->vector_irq);
		if (ret)
			return ret;

		hns3_free_vector_ring_chain(tqp_vector, &vector_ring_chain);

		if (priv->tqp_vector[i].irq_init_flag == HNS3_VECTOR_INITED) {
			(void)irq_set_affinity_hint(
				priv->tqp_vector[i].vector_irq,
						    NULL);
			free_irq(priv->tqp_vector[i].vector_irq,
				 &priv->tqp_vector[i]);
		}

		priv->ring_data[i].ring->irq_init_flag = HNS3_VECTOR_NOT_INITED;
		hns3_clear_ring_group(&tqp_vector->rx_group);
		hns3_clear_ring_group(&tqp_vector->tx_group);
		netif_napi_del(&priv->tqp_vector[i].napi);
	}

	return 0;
}

static int hns3_nic_dealloc_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct pci_dev *pdev = h->pdev;
	int i, ret;

	for (i = 0; i < priv->vector_num; i++) {
		struct hns3_enet_tqp_vector *tqp_vector;

		tqp_vector = &priv->tqp_vector[i];
		ret = h->ae_algo->ops->put_vector(h, tqp_vector->vector_irq);
		if (ret)
			return ret;
	}

	devm_kfree(&pdev->dev, priv->tqp_vector);
	return 0;
}

static int hns3_ring_get_cfg(struct hnae3_queue *q, struct hns3_nic_priv *priv,
			     int ring_type)
{
	struct hns3_nic_ring_data *ring_data = priv->ring_data;
	int queue_num = priv->ae_handle->kinfo.num_tqps;
	struct pci_dev *pdev = priv->ae_handle->pdev;
	struct hns3_enet_ring *ring;

	ring = devm_kzalloc(&pdev->dev, sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return -ENOMEM;

	if (ring_type == HNAE3_RING_TYPE_TX) {
		ring_data[q->tqp_index].ring = ring;
		ring_data[q->tqp_index].queue_index = q->tqp_index;
		ring->io_base = (u8 __iomem *)q->io_base + HNS3_TX_REG_OFFSET;
	} else {
		ring_data[q->tqp_index + queue_num].ring = ring;
		ring_data[q->tqp_index + queue_num].queue_index = q->tqp_index;
		ring->io_base = q->io_base;
	}

	hnae_set_bit(ring->flag, HNAE3_RING_TYPE_B, ring_type);

	ring->tqp = q;
	ring->desc = NULL;
	ring->desc_cb = NULL;
	ring->dev = priv->dev;
	ring->desc_dma_addr = 0;
	ring->buf_size = q->buf_size;
	ring->desc_num = q->desc_num;
	ring->next_to_use = 0;
	ring->next_to_clean = 0;

	return 0;
}

static int hns3_queue_to_ring(struct hnae3_queue *tqp,
			      struct hns3_nic_priv *priv)
{
	int ret;

	ret = hns3_ring_get_cfg(tqp, priv, HNAE3_RING_TYPE_TX);
	if (ret)
		return ret;

	ret = hns3_ring_get_cfg(tqp, priv, HNAE3_RING_TYPE_RX);
	if (ret)
		return ret;

	return 0;
}

static int hns3_get_ring_config(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct pci_dev *pdev = h->pdev;
	int i, ret;

	priv->ring_data =  devm_kzalloc(&pdev->dev, h->kinfo.num_tqps *
					sizeof(*priv->ring_data) * 2,
					GFP_KERNEL);
	if (!priv->ring_data)
		return -ENOMEM;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		ret = hns3_queue_to_ring(h->kinfo.tqp[i], priv);
		if (ret)
			goto err;
	}

	return 0;
err:
	devm_kfree(&pdev->dev, priv->ring_data);
	return ret;
}

static void hns3_put_ring_config(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	int i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		devm_kfree(priv->dev, priv->ring_data[i].ring);
		devm_kfree(priv->dev,
			   priv->ring_data[i + h->kinfo.num_tqps].ring);
	}
	devm_kfree(priv->dev, priv->ring_data);
}

static int hns3_alloc_ring_memory(struct hns3_enet_ring *ring)
{
	int ret;

	if (ring->desc_num <= 0 || ring->buf_size <= 0)
		return -EINVAL;

	ring->desc_cb = kcalloc(ring->desc_num, sizeof(ring->desc_cb[0]),
				GFP_KERNEL);
	if (!ring->desc_cb) {
		ret = -ENOMEM;
		goto out;
	}

	ret = hns3_alloc_desc(ring);
	if (ret)
		goto out_with_desc_cb;

	if (!HNAE3_IS_TX_RING(ring)) {
		ret = hns3_alloc_ring_buffers(ring);
		if (ret)
			goto out_with_desc;
	}

	return 0;

out_with_desc:
	hns3_free_desc(ring);
out_with_desc_cb:
	kfree(ring->desc_cb);
	ring->desc_cb = NULL;
out:
	return ret;
}

static void hns3_fini_ring(struct hns3_enet_ring *ring)
{
	hns3_free_desc(ring);
	kfree(ring->desc_cb);
	ring->desc_cb = NULL;
	ring->next_to_clean = 0;
	ring->next_to_use = 0;
}

static int hns3_buf_size2type(u32 buf_size)
{
	int bd_size_type;

	switch (buf_size) {
	case 512:
		bd_size_type = HNS3_BD_SIZE_512_TYPE;
		break;
	case 1024:
		bd_size_type = HNS3_BD_SIZE_1024_TYPE;
		break;
	case 2048:
		bd_size_type = HNS3_BD_SIZE_2048_TYPE;
		break;
	case 4096:
		bd_size_type = HNS3_BD_SIZE_4096_TYPE;
		break;
	default:
		bd_size_type = HNS3_BD_SIZE_2048_TYPE;
	}

	return bd_size_type;
}

static void hns3_init_ring_hw(struct hns3_enet_ring *ring)
{
	dma_addr_t dma = ring->desc_dma_addr;
	struct hnae3_queue *q = ring->tqp;

	if (!HNAE3_IS_TX_RING(ring)) {
		hns3_write_dev(q, HNS3_RING_RX_RING_BASEADDR_L_REG,
			       (u32)dma);
		hns3_write_dev(q, HNS3_RING_RX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));

		hns3_write_dev(q, HNS3_RING_RX_RING_BD_LEN_REG,
			       hns3_buf_size2type(ring->buf_size));
		hns3_write_dev(q, HNS3_RING_RX_RING_BD_NUM_REG,
			       ring->desc_num / 8 - 1);

	} else {
		hns3_write_dev(q, HNS3_RING_TX_RING_BASEADDR_L_REG,
			       (u32)dma);
		hns3_write_dev(q, HNS3_RING_TX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));

		hns3_write_dev(q, HNS3_RING_TX_RING_BD_LEN_REG,
			       hns3_buf_size2type(ring->buf_size));
		hns3_write_dev(q, HNS3_RING_TX_RING_BD_NUM_REG,
			       ring->desc_num / 8 - 1);
	}
}

int hns3_init_all_ring(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	int ring_num = h->kinfo.num_tqps * 2;
	int i, j;
	int ret;

	for (i = 0; i < ring_num; i++) {
		ret = hns3_alloc_ring_memory(priv->ring_data[i].ring);
		if (ret) {
			dev_err(priv->dev,
				"Alloc ring memory fail! ret=%d\n", ret);
			goto out_when_alloc_ring_memory;
		}

		hns3_init_ring_hw(priv->ring_data[i].ring);

		u64_stats_init(&priv->ring_data[i].ring->syncp);
	}

	return 0;

out_when_alloc_ring_memory:
	for (j = i - 1; j >= 0; j--)
		hns3_fini_ring(priv->ring_data[j].ring);

	return -ENOMEM;
}

int hns3_uninit_all_ring(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	int i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		if (h->ae_algo->ops->reset_queue)
			h->ae_algo->ops->reset_queue(h, i);

		hns3_fini_ring(priv->ring_data[i].ring);
		hns3_fini_ring(priv->ring_data[i + h->kinfo.num_tqps].ring);
	}
	return 0;
}

/* Set mac addr if it is configured. or leave it to the AE driver */
static void hns3_init_mac_addr(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	u8 mac_addr_temp[ETH_ALEN];

	if (h->ae_algo->ops->get_mac_addr) {
		h->ae_algo->ops->get_mac_addr(h, mac_addr_temp);
		ether_addr_copy(netdev->dev_addr, mac_addr_temp);
	}

	/* Check if the MAC address is valid, if not get a random one */
	if (!is_valid_ether_addr(netdev->dev_addr)) {
		eth_hw_addr_random(netdev);
		dev_warn(priv->dev, "using random MAC address %pM\n",
			 netdev->dev_addr);
	}

	if (h->ae_algo->ops->set_mac_addr)
		h->ae_algo->ops->set_mac_addr(h, netdev->dev_addr, true);

}

static void hns3_nic_set_priv_ops(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);

	if ((netdev->features & NETIF_F_TSO) ||
	    (netdev->features & NETIF_F_TSO6)) {
		priv->ops.fill_desc = hns3_fill_desc_tso;
		priv->ops.maybe_stop_tx = hns3_nic_maybe_stop_tso;
	} else {
		priv->ops.fill_desc = hns3_fill_desc;
		priv->ops.maybe_stop_tx = hns3_nic_maybe_stop_tx;
	}
}

static int hns3_client_init(struct hnae3_handle *handle)
{
	struct pci_dev *pdev = handle->pdev;
	struct hns3_nic_priv *priv;
	struct net_device *netdev;
	int ret;

	netdev = alloc_etherdev_mq(sizeof(struct hns3_nic_priv),
				   hns3_get_max_available_channels(handle));
	if (!netdev)
		return -ENOMEM;

	priv = netdev_priv(netdev);
	priv->dev = &pdev->dev;
	priv->netdev = netdev;
	priv->ae_handle = handle;
	priv->ae_handle->reset_level = HNAE3_NONE_RESET;
	priv->ae_handle->last_reset_time = jiffies;
	priv->tx_timeout_count = 0;

	handle->kinfo.netdev = netdev;
	handle->priv = (void *)priv;

	hns3_init_mac_addr(netdev);

	hns3_set_default_feature(netdev);

	netdev->watchdog_timeo = HNS3_TX_TIMEOUT;
	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->netdev_ops = &hns3_nic_netdev_ops;
	SET_NETDEV_DEV(netdev, &pdev->dev);
	hns3_ethtool_set_ops(netdev);
	hns3_nic_set_priv_ops(netdev);

	/* Carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	ret = hns3_get_ring_config(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_get_ring_cfg;
	}

	ret = hns3_nic_alloc_vector_data(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_alloc_vector_data;
	}

	ret = hns3_nic_init_vector_data(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_init_vector_data;
	}

	ret = hns3_init_all_ring(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_init_ring_data;
	}

	ret = register_netdev(netdev);
	if (ret) {
		dev_err(priv->dev, "probe register netdev fail!\n");
		goto out_reg_netdev_fail;
	}

	hns3_dcbnl_setup(handle);

	/* MTU range: (ETH_MIN_MTU(kernel default) - 9706) */
	netdev->max_mtu = HNS3_MAX_MTU - (ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN);

	return ret;

out_reg_netdev_fail:
out_init_ring_data:
	(void)hns3_nic_uninit_vector_data(priv);
out_init_vector_data:
	hns3_nic_dealloc_vector_data(priv);
out_alloc_vector_data:
	priv->ring_data = NULL;
out_get_ring_cfg:
	priv->ae_handle = NULL;
	free_netdev(netdev);
	return ret;
}

static void hns3_client_uninit(struct hnae3_handle *handle, bool reset)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret;

	if (netdev->reg_state != NETREG_UNINITIALIZED)
		unregister_netdev(netdev);

	ret = hns3_nic_uninit_vector_data(priv);
	if (ret)
		netdev_err(netdev, "uninit vector error\n");

	ret = hns3_nic_dealloc_vector_data(priv);
	if (ret)
		netdev_err(netdev, "dealloc vector error\n");

	ret = hns3_uninit_all_ring(priv);
	if (ret)
		netdev_err(netdev, "uninit ring error\n");

	hns3_put_ring_config(priv);

	priv->ring_data = NULL;

	free_netdev(netdev);
}

static void hns3_link_status_change(struct hnae3_handle *handle, bool linkup)
{
	struct net_device *netdev = handle->kinfo.netdev;

	if (!netdev)
		return;

	if (linkup) {
		netif_carrier_on(netdev);
		netif_tx_wake_all_queues(netdev);
		netdev_info(netdev, "link up\n");
	} else {
		netif_carrier_off(netdev);
		netif_tx_stop_all_queues(netdev);
		netdev_info(netdev, "link down\n");
	}
}

static int hns3_client_setup_tc(struct hnae3_handle *handle, u8 tc)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct net_device *ndev = kinfo->netdev;
	bool if_running;
	int ret;
	u8 i;

	if (tc > HNAE3_MAX_TC)
		return -EINVAL;

	if (!ndev)
		return -ENODEV;

	if_running = netif_running(ndev);

	ret = netdev_set_num_tc(ndev, tc);
	if (ret)
		return ret;

	if (if_running) {
		(void)hns3_nic_net_stop(ndev);
		msleep(100);
	}

	ret = (kinfo->dcb_ops && kinfo->dcb_ops->map_update) ?
		kinfo->dcb_ops->map_update(handle) : -EOPNOTSUPP;
	if (ret)
		goto err_out;

	if (tc <= 1) {
		netdev_reset_tc(ndev);
		goto out;
	}

	for (i = 0; i < HNAE3_MAX_TC; i++) {
		struct hnae3_tc_info *tc_info = &kinfo->tc_info[i];

		if (tc_info->enable)
			netdev_set_tc_queue(ndev,
					    tc_info->tc,
					    tc_info->tqp_count,
					    tc_info->tqp_offset);
	}

	for (i = 0; i < HNAE3_MAX_USER_PRIO; i++) {
		netdev_set_prio_tc_map(ndev, i,
				       kinfo->prio_tc[i]);
	}

out:
	ret = hns3_nic_set_real_num_queue(ndev);

err_out:
	if (if_running)
		(void)hns3_nic_net_open(ndev);

	return ret;
}

static void hns3_recover_hw_addr(struct net_device *ndev)
{
	struct netdev_hw_addr_list *list;
	struct netdev_hw_addr *ha, *tmp;

	/* go through and sync uc_addr entries to the device */
	list = &ndev->uc;
	list_for_each_entry_safe(ha, tmp, &list->list, list)
		hns3_nic_uc_sync(ndev, ha->addr);

	/* go through and sync mc_addr entries to the device */
	list = &ndev->mc;
	list_for_each_entry_safe(ha, tmp, &list->list, list)
		hns3_nic_mc_sync(ndev, ha->addr);
}

static void hns3_drop_skb_data(struct hns3_enet_ring *ring, struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
}

static void hns3_clear_all_ring(struct hnae3_handle *h)
{
	struct net_device *ndev = h->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	u32 i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		struct netdev_queue *dev_queue;
		struct hns3_enet_ring *ring;

		ring = priv->ring_data[i].ring;
		hns3_clean_tx_ring(ring, ring->desc_num);
		dev_queue = netdev_get_tx_queue(ndev,
						priv->ring_data[i].queue_index);
		netdev_tx_reset_queue(dev_queue);

		ring = priv->ring_data[i + h->kinfo.num_tqps].ring;
		hns3_clean_rx_ring(ring, ring->desc_num, hns3_drop_skb_data);
	}
}

static int hns3_reset_notify_down_enet(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct net_device *ndev = kinfo->netdev;

	if (!netif_running(ndev))
		return -EIO;

	return hns3_nic_net_stop(ndev);
}

static int hns3_reset_notify_up_enet(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	int ret = 0;

	if (netif_running(kinfo->netdev)) {
		ret = hns3_nic_net_up(kinfo->netdev);
		if (ret) {
			netdev_err(kinfo->netdev,
				   "hns net up fail, ret=%d!\n", ret);
			return ret;
		}
		handle->last_reset_time = jiffies;
	}

	return ret;
}

static int hns3_reset_notify_init_enet(struct hnae3_handle *handle)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret;

	hns3_init_mac_addr(netdev);
	hns3_nic_set_rx_mode(netdev);
	hns3_recover_hw_addr(netdev);

	/* Hardware table is only clear when pf resets */
	if (!(handle->flags & HNAE3_SUPPORT_VF))
		hns3_restore_vlan(netdev);

	/* Carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	ret = hns3_get_ring_config(priv);
	if (ret)
		return ret;

	ret = hns3_nic_init_vector_data(priv);
	if (ret)
		return ret;

	ret = hns3_init_all_ring(priv);
	if (ret) {
		hns3_nic_uninit_vector_data(priv);
		priv->ring_data = NULL;
	}

	return ret;
}

static int hns3_reset_notify_uninit_enet(struct hnae3_handle *handle)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret;

	hns3_clear_all_ring(handle);

	ret = hns3_nic_uninit_vector_data(priv);
	if (ret) {
		netdev_err(netdev, "uninit vector error\n");
		return ret;
	}

	ret = hns3_uninit_all_ring(priv);
	if (ret)
		netdev_err(netdev, "uninit ring error\n");

	hns3_put_ring_config(priv);

	priv->ring_data = NULL;

	return ret;
}

static int hns3_reset_notify(struct hnae3_handle *handle,
			     enum hnae3_reset_notify_type type)
{
	int ret = 0;

	switch (type) {
	case HNAE3_UP_CLIENT:
		ret = hns3_reset_notify_up_enet(handle);
		break;
	case HNAE3_DOWN_CLIENT:
		ret = hns3_reset_notify_down_enet(handle);
		break;
	case HNAE3_INIT_CLIENT:
		ret = hns3_reset_notify_init_enet(handle);
		break;
	case HNAE3_UNINIT_CLIENT:
		ret = hns3_reset_notify_uninit_enet(handle);
		break;
	default:
		break;
	}

	return ret;
}

static void hns3_restore_coal(struct hns3_nic_priv *priv,
			      struct hns3_enet_coalesce *tx,
			      struct hns3_enet_coalesce *rx)
{
	u16 vector_num = priv->vector_num;
	int i;

	for (i = 0; i < vector_num; i++) {
		memcpy(&priv->tqp_vector[i].tx_group.coal, tx,
		       sizeof(struct hns3_enet_coalesce));
		memcpy(&priv->tqp_vector[i].rx_group.coal, rx,
		       sizeof(struct hns3_enet_coalesce));
	}
}

static int hns3_modify_tqp_num(struct net_device *netdev, u16 new_tqp_num,
			       struct hns3_enet_coalesce *tx,
			       struct hns3_enet_coalesce *rx)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret;

	ret = h->ae_algo->ops->set_channels(h, new_tqp_num);
	if (ret)
		return ret;

	ret = hns3_get_ring_config(priv);
	if (ret)
		return ret;

	ret = hns3_nic_alloc_vector_data(priv);
	if (ret)
		goto err_alloc_vector;

	hns3_restore_coal(priv, tx, rx);

	ret = hns3_nic_init_vector_data(priv);
	if (ret)
		goto err_uninit_vector;

	ret = hns3_init_all_ring(priv);
	if (ret)
		goto err_put_ring;

	return 0;

err_put_ring:
	hns3_put_ring_config(priv);
err_uninit_vector:
	hns3_nic_uninit_vector_data(priv);
err_alloc_vector:
	hns3_nic_dealloc_vector_data(priv);
	return ret;
}

static int hns3_adjust_tqps_num(u8 num_tc, u32 new_tqp_num)
{
	return (new_tqp_num / num_tc) * num_tc;
}

int hns3_set_channels(struct net_device *netdev,
		      struct ethtool_channels *ch)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	struct hns3_enet_coalesce tx_coal, rx_coal;
	bool if_running = netif_running(netdev);
	u32 new_tqp_num = ch->combined_count;
	u16 org_tqp_num;
	int ret;

	if (ch->rx_count || ch->tx_count)
		return -EINVAL;

	if (new_tqp_num > hns3_get_max_available_channels(h) ||
	    new_tqp_num < kinfo->num_tc) {
		dev_err(&netdev->dev,
			"Change tqps fail, the tqp range is from %d to %d",
			kinfo->num_tc,
			hns3_get_max_available_channels(h));
		return -EINVAL;
	}

	new_tqp_num = hns3_adjust_tqps_num(kinfo->num_tc, new_tqp_num);
	if (kinfo->num_tqps == new_tqp_num)
		return 0;

	if (if_running)
		hns3_nic_net_stop(netdev);

	hns3_clear_all_ring(h);

	ret = hns3_nic_uninit_vector_data(priv);
	if (ret) {
		dev_err(&netdev->dev,
			"Unbind vector with tqp fail, nothing is changed");
		goto open_netdev;
	}

	/* Changing the tqp num may also change the vector num,
	 * ethtool only support setting and querying one coal
	 * configuation for now, so save the vector 0' coal
	 * configuation here in order to restore it.
	 */
	memcpy(&tx_coal, &priv->tqp_vector[0].tx_group.coal,
	       sizeof(struct hns3_enet_coalesce));
	memcpy(&rx_coal, &priv->tqp_vector[0].rx_group.coal,
	       sizeof(struct hns3_enet_coalesce));

	hns3_nic_dealloc_vector_data(priv);

	hns3_uninit_all_ring(priv);
	hns3_put_ring_config(priv);

	org_tqp_num = h->kinfo.num_tqps;
	ret = hns3_modify_tqp_num(netdev, new_tqp_num, &tx_coal, &rx_coal);
	if (ret) {
		ret = hns3_modify_tqp_num(netdev, org_tqp_num,
					  &tx_coal, &rx_coal);
		if (ret) {
			/* If revert to old tqp failed, fatal error occurred */
			dev_err(&netdev->dev,
				"Revert to old tqp num fail, ret=%d", ret);
			return ret;
		}
		dev_info(&netdev->dev,
			 "Change tqp num fail, Revert to old tqp num");
	}

open_netdev:
	if (if_running)
		hns3_nic_net_open(netdev);

	return ret;
}

static const struct hnae3_client_ops client_ops = {
	.init_instance = hns3_client_init,
	.uninit_instance = hns3_client_uninit,
	.link_status_change = hns3_link_status_change,
	.setup_tc = hns3_client_setup_tc,
	.reset_notify = hns3_reset_notify,
};

/* hns3_init_module - Driver registration routine
 * hns3_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init hns3_init_module(void)
{
	int ret;

	pr_info("%s: %s - version\n", hns3_driver_name, hns3_driver_string);
	pr_info("%s: %s\n", hns3_driver_name, hns3_copyright);

	client.type = HNAE3_CLIENT_KNIC;
	snprintf(client.name, HNAE3_CLIENT_NAME_LENGTH - 1, "%s",
		 hns3_driver_name);

	client.ops = &client_ops;

	ret = hnae3_register_client(&client);
	if (ret)
		return ret;

	ret = pci_register_driver(&hns3_driver);
	if (ret)
		hnae3_unregister_client(&client);

	return ret;
}
module_init(hns3_init_module);

/* hns3_exit_module - Driver exit cleanup routine
 * hns3_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit hns3_exit_module(void)
{
	pci_unregister_driver(&hns3_driver);
	hnae3_unregister_client(&client);
}
module_exit(hns3_exit_module);

MODULE_DESCRIPTION("HNS3: Hisilicon Ethernet Driver");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("pci:hns-nic");
