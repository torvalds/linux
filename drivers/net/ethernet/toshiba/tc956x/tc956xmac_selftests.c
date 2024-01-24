/*
 * TC956X ethernet driver.
 *
 * tc956xmac_selftests.c
 *
 * Copyright (C) 2019 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 */

#include <linux/bitrev.h>
#include <linux/completion.h>
#include <linux/crc32.h>
#include <linux/ethtool.h>
#include <linux/ip.h>
#include <linux/phy.h>
#include <linux/udp.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/tc_act/tc_gact.h>
#include "tc956xmac.h"

struct tc956xmachdr {
	__be32 version;
	__be64 magic;
	u8 id;
} __packed;

#define TC956XMAC_TEST_PKT_SIZE (sizeof(struct ethhdr) + sizeof(struct iphdr) + \
			      sizeof(struct tc956xmachdr))
#define TC956XMAC_TEST_PKT_MAGIC	0xdeadcafecafedeadULL
#define TC956XMAC_LB_TIMEOUT	msecs_to_jiffies(200)
#define TC956XMAC_PTP_TIMEOUT	msecs_to_jiffies(2000)
#define MMC_COUNTER_INITIAL_VALUE 0
/*#define SELFTEST_NOT_SUPPORTED*/

struct tc956xmac_packet_attrs {
	int vlan;
	int vlan_id_in;
	int vlan_id_out;
	unsigned char *src;
	unsigned char *dst;
	u32 ip_src;
	u32 ip_dst;
	int tcp;
	int sport;
	int dport;
	u32 exp_hash;
	int dont_wait;
	int timeout;
	int size;
	int max_size;
	int remove_sa;
	u8 id;
	int sarc;
	u16 queue_mapping;
	u64 timestamp;
};

static u8 tc956xmac_test_next_id;

static struct sk_buff *tc956xmac_test_get_udp_skb(struct tc956xmac_priv *priv,
					       struct tc956xmac_packet_attrs *attr)
{
	struct sk_buff *skb = NULL;
	struct udphdr *uhdr = NULL;
	struct tcphdr *thdr = NULL;
	struct tc956xmachdr *shdr;
	struct ethhdr *ehdr;
	struct iphdr *ihdr;
	int iplen, size;

	size = attr->size + TC956XMAC_TEST_PKT_SIZE;
	if (attr->vlan) {
		size += 4;
		if (attr->vlan > 1)
			size += 4;
	}

	if (attr->tcp)
		size += sizeof(struct tcphdr);
	else
		size += sizeof(struct udphdr);

	if (attr->max_size && (attr->max_size > size))
		size = attr->max_size;

	skb = netdev_alloc_skb(priv->dev, size);
	if (!skb)
		return NULL;

	prefetchw(skb->data);

	if (attr->vlan > 1)
		ehdr = skb_push(skb, ETH_HLEN + 8);
	else if (attr->vlan)
		ehdr = skb_push(skb, ETH_HLEN + 4);
	else if (attr->remove_sa)
		ehdr = skb_push(skb, ETH_HLEN - 6);
	else
		ehdr = skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);

	skb_set_network_header(skb, skb->len);
	ihdr = skb_put(skb, sizeof(*ihdr));

	skb_set_transport_header(skb, skb->len);
	if (attr->tcp)
		thdr = skb_put(skb, sizeof(*thdr));
	else
		uhdr = skb_put(skb, sizeof(*uhdr));

	if (!attr->remove_sa)
		eth_zero_addr(ehdr->h_source);
	eth_zero_addr(ehdr->h_dest);
	if (attr->src && !attr->remove_sa)
		ether_addr_copy(ehdr->h_source, attr->src);
	if (attr->dst)
		ether_addr_copy(ehdr->h_dest, attr->dst);

	if (!attr->remove_sa) {
		ehdr->h_proto = htons(ETH_P_IP);
	} else {
		__be16 *ptr = (__be16 *)ehdr;

		/* HACK */
		ptr[3] = htons(ETH_P_IP);
	}

	if (attr->vlan) {
		__be16 *tag, *proto;

		if (!attr->remove_sa) {
			tag = (void *)ehdr + ETH_HLEN;
			proto = (void *)ehdr + (2 * ETH_ALEN);
		} else {
			tag = (void *)ehdr + ETH_HLEN - 6;
			proto = (void *)ehdr + ETH_ALEN;
		}

		proto[0] = htons(ETH_P_8021Q);
		tag[0] = htons(attr->vlan_id_out);
		tag[1] = htons(ETH_P_IP);
		if (attr->vlan > 1) {
			proto[0] = htons(ETH_P_8021AD);
			tag[1] = htons(ETH_P_8021Q);
			tag[2] = htons(attr->vlan_id_in);
			tag[3] = htons(ETH_P_IP);
		}
	}

	if (attr->tcp) {
		thdr->source = htons(attr->sport);
		thdr->dest = htons(attr->dport);
		thdr->doff = sizeof(struct tcphdr) / 4;
		thdr->check = 0;
	} else {
		uhdr->source = htons(attr->sport);
		uhdr->dest = htons(attr->dport);
		uhdr->len = htons(sizeof(*shdr) + sizeof(*uhdr) + attr->size);
		if (attr->max_size)
			uhdr->len = htons(attr->max_size -
					  (sizeof(*ihdr) + sizeof(*ehdr)));
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

	if (attr->max_size)
		iplen = attr->max_size - sizeof(*ehdr);

	ihdr->tot_len = htons(iplen);
	ihdr->frag_off = 0;
	ihdr->saddr = htonl(attr->ip_src);
	ihdr->daddr = htonl(attr->ip_dst);
	ihdr->tos = 0;
	ihdr->id = 0;
	ip_send_check(ihdr);

	shdr = skb_put(skb, sizeof(*shdr));
	shdr->version = 0;
	shdr->magic = cpu_to_be64(TC956XMAC_TEST_PKT_MAGIC);
	attr->id = tc956xmac_test_next_id;
	shdr->id = tc956xmac_test_next_id++;

	if (attr->size)
		skb_put(skb, attr->size);
	if (attr->max_size && (attr->max_size > skb->len))
		skb_put(skb, attr->max_size - skb->len);

	skb->csum = 0;
	skb->ip_summed = CHECKSUM_PARTIAL;
	if (attr->tcp) {
		thdr->check = ~tcp_v4_check(skb->len, ihdr->saddr, ihdr->daddr, 0);
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct tcphdr, check);
	} else {
		udp4_hwcsum(skb, ihdr->saddr, ihdr->daddr);
	}

	skb->protocol = htons(ETH_P_IP);
	skb->pkt_type = PACKET_HOST;
	skb->dev = priv->dev;

	if (attr->timestamp)
		skb->tstamp = ns_to_ktime(attr->timestamp);

	return skb;
}

struct tc956xmac_test_priv {
	struct tc956xmac_packet_attrs *packet;
	struct packet_type pt;
	struct completion comp;
	int double_vlan;
	int vlan_id;
	int ok;
};

static int tc956xmac_test_loopback_validate(struct sk_buff *skb,
					 struct net_device *ndev,
					 struct packet_type *pt,
					 struct net_device *orig_ndev)
{
	struct tc956xmac_test_priv *tpriv = pt->af_packet_priv;
	unsigned char *src = tpriv->packet->src;
	unsigned char *dst = tpriv->packet->dst;
	struct tc956xmachdr *shdr;
	struct ethhdr *ehdr;
	struct udphdr *uhdr;
	struct tcphdr *thdr;
	struct iphdr *ihdr;

	skb = skb_unshare(skb, GFP_ATOMIC);
	if (!skb)
		goto out;

	if (skb_linearize(skb))
		goto out;
	if (skb_headlen(skb) < (TC956XMAC_TEST_PKT_SIZE - ETH_HLEN))
		goto out;

	ehdr = (struct ethhdr *)skb_mac_header(skb);
	if (dst) {
		if (!ether_addr_equal_unaligned(ehdr->h_dest, dst))
			goto out;
	}
	if (tpriv->packet->sarc) {
		if (!ether_addr_equal_unaligned(ehdr->h_source, ehdr->h_dest))
			goto out;
	} else if (src) {
		if (!ether_addr_equal_unaligned(ehdr->h_source, src))
			goto out;
	}

	ihdr = ip_hdr(skb);
	if (tpriv->double_vlan)
		ihdr = (struct iphdr *)(skb_network_header(skb) + 4);

	if (tpriv->packet->tcp) {
		if (ihdr->protocol != IPPROTO_TCP)
			goto out;

		thdr = (struct tcphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
		if (thdr->dest != htons(tpriv->packet->dport))
			goto out;

		shdr = (struct tc956xmachdr *)((u8 *)thdr + sizeof(*thdr));
	} else {
		if (ihdr->protocol != IPPROTO_UDP)
			goto out;

		uhdr = (struct udphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
		if (uhdr->dest != htons(tpriv->packet->dport))
			goto out;

		shdr = (struct tc956xmachdr *)((u8 *)uhdr + sizeof(*uhdr));
	}

	if (shdr->magic != cpu_to_be64(TC956XMAC_TEST_PKT_MAGIC))
		goto out;
	if (tpriv->packet->exp_hash && !skb->hash)
		goto out;
	if (tpriv->packet->id != shdr->id)
		goto out;

	tpriv->ok = true;
	complete(&tpriv->comp);
out:
	kfree_skb(skb);
	return 0;
}

static int __tc956xmac_test_loopback(struct tc956xmac_priv *priv,
				  struct tc956xmac_packet_attrs *attr)
{
	struct tc956xmac_test_priv *tpriv;
	struct sk_buff *skb = NULL;
	int ret = 0;

	tpriv = kzalloc(sizeof(*tpriv), GFP_KERNEL);
	if (!tpriv)
		return -ENOMEM;

	tpriv->ok = false;
	init_completion(&tpriv->comp);

	tpriv->pt.type = htons(ETH_P_IP);
	tpriv->pt.func = tc956xmac_test_loopback_validate;
	tpriv->pt.dev = priv->dev;
	tpriv->pt.af_packet_priv = tpriv;
	tpriv->packet = attr;

	if (!attr->dont_wait)
		dev_add_pack(&tpriv->pt);

	skb = tc956xmac_test_get_udp_skb(priv, attr);
	if (!skb) {
		ret = -ENOMEM;
		goto cleanup;
	}

	attr->queue_mapping = HOST_BEST_EFF_CH;
	ret = dev_direct_xmit(skb, attr->queue_mapping);
	if (ret)
		goto cleanup;

	if (attr->dont_wait)
		goto cleanup;

	if (!attr->timeout)
		attr->timeout = TC956XMAC_LB_TIMEOUT;

	wait_for_completion_timeout(&tpriv->comp, attr->timeout);
	ret = tpriv->ok ? 0 : -ETIMEDOUT;

cleanup:
	if (!attr->dont_wait)
		dev_remove_pack(&tpriv->pt);
	kfree(tpriv);
	return ret;
}

static int tc956xmac_test_mac_loopback(struct tc956xmac_priv *priv)
{
	struct tc956xmac_packet_attrs attr = { };

	attr.dst = priv->dev->dev_addr;
	return __tc956xmac_test_loopback(priv, &attr);
}

static int tc956xmac_test_phy_loopback(struct tc956xmac_priv *priv)
{
	struct tc956xmac_packet_attrs attr = { };
	int ret;

	if (!priv->dev->phydev)
		return -EBUSY;
	ret = phy_loopback(priv->dev->phydev, true);
	if (ret)
		return ret;

	msleep(7000);
	attr.dst = priv->dev->dev_addr;
	ret = __tc956xmac_test_loopback(priv, &attr);

	phy_loopback(priv->dev->phydev, false);
	msleep(7000);

	return ret;
}

static int tc956xmac_test_mmc(struct tc956xmac_priv *priv)
{
	struct tc956xmac_counters final;
	int ret;

	memset(&final, 0, sizeof(final));

	if (!priv->dma_cap.rmon)
		return -EOPNOTSUPP;

	/* Save previous results into internal struct */
	tc956xmac_mmc_read(priv, priv->mmcaddr, &priv->mmc);

	ret = tc956xmac_test_mac_loopback(priv);
	if (ret)
		return ret;

	/* These will be loopback results so no need to save them */
	tc956xmac_mmc_read(priv, priv->mmcaddr, &final);

	/*
	 * The number of MMC counters available depends on HW configuration
	 * so we just use this one to validate the feature. I hope there is
	 * not a version without this counter.
	 */
	if (final.mmc_tx_framecount_g <= MMC_COUNTER_INITIAL_VALUE)
		return -EINVAL;

	return 0;
}

#ifdef SELFTEST_NOT_SUPPORTED
static int tc956xmac_test_eee(struct tc956xmac_priv *priv)
{
	struct tc956xmac_extra_stats *initial, *final;
	int retries = 10;
	int ret;

	if (!priv->dma_cap.eee || !priv->eee_active)
		return -EOPNOTSUPP;

	initial = kzalloc(sizeof(*initial), GFP_KERNEL);
	if (!initial)
		return -ENOMEM;

	final = kzalloc(sizeof(*final), GFP_KERNEL);
	if (!final) {
		ret = -ENOMEM;
		goto out_free_initial;
	}

	memcpy(initial, &priv->xstats, sizeof(*initial));

	ret = tc956xmac_test_mac_loopback(priv);
	if (ret)
		goto out_free_final;

	/* We have no traffic in the line so, sooner or later it will go LPI */
	while (--retries) {
		memcpy(final, &priv->xstats, sizeof(*final));

		if (final->irq_tx_path_in_lpi_mode_n >
		    initial->irq_tx_path_in_lpi_mode_n)
			break;
		msleep(100);
	}

	if (!retries) {
		ret = -ETIMEDOUT;
		goto out_free_final;
	}

	if (final->irq_tx_path_in_lpi_mode_n <=
	    initial->irq_tx_path_in_lpi_mode_n) {
		ret = -EINVAL;
		goto out_free_final;
	}

	if (final->irq_tx_path_exit_lpi_mode_n <=
	    initial->irq_tx_path_exit_lpi_mode_n) {
		ret = -EINVAL;
		goto out_free_final;
	}

out_free_final:
	kfree(final);
out_free_initial:
	kfree(initial);
	return ret;
}
#endif

static int tc956xmac_filter_check(struct tc956xmac_priv *priv)
{
	if (!(priv->dev->flags & IFF_PROMISC))
		return 0;

	netdev_warn(priv->dev, "Test can't be run in promiscuous mode!\n");
	return -EOPNOTSUPP;
}

static bool tc956xmac_hash_check(struct tc956xmac_priv *priv, unsigned char *addr)
{
	int mc_offset = 32 - priv->hw->mcast_bits_log2;
	struct netdev_hw_addr *ha;
	u32 hash, hash_nr;

	/* First compute the hash for desired addr */
	hash = bitrev32(~crc32_le(~0, addr, 6)) >> mc_offset;
	hash_nr = hash >> 5;
	hash = 1 << (hash & 0x1f);

	/* Now, check if it collides with any existing one */
	netdev_for_each_mc_addr(ha, priv->dev) {
		u32 nr = bitrev32(~crc32_le(~0, ha->addr, ETH_ALEN)) >> mc_offset;

		if (((nr >> 5) == hash_nr) && ((1 << (nr & 0x1f)) == hash))
			return false;
	}

	/* No collisions, address is good to go */
	return true;
}

static bool tc956xmac_perfect_check(struct tc956xmac_priv *priv, unsigned char *addr)
{
	struct netdev_hw_addr *ha;

	/* Check if it collides with any existing one */
	netdev_for_each_uc_addr(ha, priv->dev) {
		if (!memcmp(ha->addr, addr, ETH_ALEN))
			return false;
	}

	/* No collisions, address is good to go */
	return true;
}

static int tc956xmac_test_hfilt(struct tc956xmac_priv *priv)
{
	unsigned char gd_addr[ETH_ALEN] = {0xf1, 0xee, 0xdd, 0xcc, 0xbb, 0xaa};
	unsigned char bd_addr[ETH_ALEN] = {0xf1, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct tc956xmac_packet_attrs attr = { };
	int ret, tries = 256;

	ret = tc956xmac_filter_check(priv);
	if (ret)
		return ret;

	if (netdev_mc_count(priv->dev) >= priv->hw->multicast_filter_bins)
		return -EOPNOTSUPP;

	while (--tries) {
		/* We only need to check the bd_addr for collisions */
		bd_addr[ETH_ALEN - 1] = tries;
		if (tc956xmac_hash_check(priv, bd_addr))
			break;
	}

	if (!tries)
		return -EOPNOTSUPP;

	ret = dev_mc_add(priv->dev, gd_addr);
	if (ret)
		return ret;

	attr.dst = gd_addr;

	/* Shall receive packet */
	ret = __tc956xmac_test_loopback(priv, &attr);
	if (ret)
		goto cleanup;

	attr.dst = bd_addr;

	/* Shall NOT receive packet */
	ret = __tc956xmac_test_loopback(priv, &attr);
	ret = ret ? 0 : -EINVAL;

cleanup:
	dev_mc_del(priv->dev, gd_addr);
	return ret;
}

static int tc956xmac_test_pfilt(struct tc956xmac_priv *priv)
{
	unsigned char gd_addr[ETH_ALEN] = {0xf0, 0x01, 0x44, 0x55, 0x66, 0x77};
	unsigned char bd_addr[ETH_ALEN] = {0xf0, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct tc956xmac_packet_attrs attr = { };
	int ret, tries = 256;

	if (tc956xmac_filter_check(priv))
		return -EOPNOTSUPP;
	if (netdev_uc_count(priv->dev) >= priv->hw->unicast_filter_entries)
		return -EOPNOTSUPP;

	while (--tries) {
		/* We only need to check the bd_addr for collisions */
		bd_addr[ETH_ALEN - 1] = tries;
		if (tc956xmac_perfect_check(priv, bd_addr))
			break;
	}

	if (!tries)
		return -EOPNOTSUPP;

	ret = dev_uc_add(priv->dev, gd_addr);
	if (ret)
		return ret;

	attr.dst = gd_addr;

	/* Shall receive packet */
	ret = __tc956xmac_test_loopback(priv, &attr);
	if (ret)
		goto cleanup;

	attr.dst = bd_addr;

	/* Shall NOT receive packet */
	ret = __tc956xmac_test_loopback(priv, &attr);
	ret = ret ? 0 : -EINVAL;

cleanup:
	dev_uc_del(priv->dev, gd_addr);
	return ret;
}

static int tc956xmac_test_mcfilt(struct tc956xmac_priv *priv)
{
	unsigned char uc_addr[ETH_ALEN] = {0xf0, 0xff, 0xff, 0xff, 0xff, 0xff};
	unsigned char mc_addr[ETH_ALEN] = {0xf1, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct tc956xmac_packet_attrs attr = { };
	int ret, tries = 256;

	if (tc956xmac_filter_check(priv))
		return -EOPNOTSUPP;
	if (netdev_uc_count(priv->dev) >= priv->hw->unicast_filter_entries)
		return -EOPNOTSUPP;
	if (netdev_mc_count(priv->dev) >= priv->hw->multicast_filter_bins)
		return -EOPNOTSUPP;

	while (--tries) {
		/* We only need to check the mc_addr for collisions */
		mc_addr[ETH_ALEN - 1] = tries;
		if (tc956xmac_hash_check(priv, mc_addr))
			break;
	}

	if (!tries)
		return -EOPNOTSUPP;

	ret = dev_uc_add(priv->dev, uc_addr);
	if (ret)
		return ret;

	attr.dst = uc_addr;

	/* Shall receive packet */
	ret = __tc956xmac_test_loopback(priv, &attr);
	if (ret)
		goto cleanup;

	attr.dst = mc_addr;

	/* Shall NOT receive packet */
	ret = __tc956xmac_test_loopback(priv, &attr);
	ret = ret ? 0 : -EINVAL;

cleanup:
	dev_uc_del(priv->dev, uc_addr);
	return ret;
}

static int tc956xmac_test_ucfilt(struct tc956xmac_priv *priv)
{
	unsigned char uc_addr[ETH_ALEN] = {0xf0, 0xff, 0xff, 0xff, 0xff, 0xff};
	unsigned char mc_addr[ETH_ALEN] = {0xf1, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct tc956xmac_packet_attrs attr = { };
	int ret, tries = 256;

	if (tc956xmac_filter_check(priv))
		return -EOPNOTSUPP;
	if (netdev_uc_count(priv->dev) >= priv->hw->unicast_filter_entries)
		return -EOPNOTSUPP;
	if (netdev_mc_count(priv->dev) >= priv->hw->multicast_filter_bins)
		return -EOPNOTSUPP;

	while (--tries) {
		/* We only need to check the uc_addr for collisions */
		uc_addr[ETH_ALEN - 1] = tries;
		if (tc956xmac_perfect_check(priv, uc_addr))
			break;
	}

	if (!tries)
		return -EOPNOTSUPP;

	ret = dev_mc_add(priv->dev, mc_addr);
	if (ret)
		return ret;

	attr.dst = mc_addr;

	/* Shall receive packet */
	ret = __tc956xmac_test_loopback(priv, &attr);
	if (ret)
		goto cleanup;

	attr.dst = uc_addr;

	/* Shall NOT receive packet */
	ret = __tc956xmac_test_loopback(priv, &attr);
	ret = ret ? 0 : -EINVAL;

cleanup:
	dev_mc_del(priv->dev, mc_addr);
	return ret;
}

#ifdef SELFTEST_NOT_SUPPORTED
static int tc956xmac_test_flowctrl_validate(struct sk_buff *skb,
					 struct net_device *ndev,
					 struct packet_type *pt,
					 struct net_device *orig_ndev)
{
	struct tc956xmac_test_priv *tpriv = pt->af_packet_priv;
	struct ethhdr *ehdr;

	ehdr = (struct ethhdr *)skb_mac_header(skb);
	if (!ether_addr_equal_unaligned(ehdr->h_source, orig_ndev->dev_addr))
		goto out;
	if (ehdr->h_proto != htons(ETH_P_PAUSE))
		goto out;

	tpriv->ok = true;
	complete(&tpriv->comp);
out:
	kfree_skb(skb);
	return 0;
}

static int tc956xmac_test_flowctrl(struct tc956xmac_priv *priv)
{
	unsigned char paddr[ETH_ALEN] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x01};
	struct phy_device *phydev = priv->dev->phydev;
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	struct tc956xmac_test_priv *tpriv;
	unsigned int pkt_count;
	int i, ret = 0;

	if (!phydev || (!phydev->pause && !phydev->asym_pause))
		return -EOPNOTSUPP;

	tpriv = kzalloc(sizeof(*tpriv), GFP_KERNEL);
	if (!tpriv)
		return -ENOMEM;

	tpriv->ok = false;
	init_completion(&tpriv->comp);
	tpriv->pt.type = htons(ETH_P_PAUSE);
	tpriv->pt.func = tc956xmac_test_flowctrl_validate;
	tpriv->pt.dev = priv->dev;
	tpriv->pt.af_packet_priv = tpriv;
	dev_add_pack(&tpriv->pt);

	/* Compute minimum number of packets to make FIFO full */
	pkt_count = priv->plat->rx_fifo_size;
	if (!pkt_count)
		pkt_count = priv->dma_cap.rx_fifo_size;
	pkt_count /= 1400;
	pkt_count *= 2;

	for (i = 0; i < rx_cnt; i++)
		tc956xmac_stop_rx(priv, priv->ioaddr, i);

	ret = dev_set_promiscuity(priv->dev, 1);
	if (ret)
		goto cleanup;

	ret = dev_mc_add(priv->dev, paddr);
	if (ret)
		goto cleanup;

	for (i = 0; i < pkt_count; i++) {
		struct tc956xmac_packet_attrs attr = { };

		attr.dst = priv->dev->dev_addr;
		attr.dont_wait = true;
		attr.size = 1400;

		ret = __tc956xmac_test_loopback(priv, &attr);
		if (ret)
			goto cleanup;
		if (tpriv->ok)
			break;
	}

	/* Wait for some time in case RX Watchdog is enabled */
	msleep(200);

	for (i = 0; i < rx_cnt; i++) {
		struct tc956xmac_channel *ch = &priv->channel[i];
		u32 tail;

		tail = priv->rx_queue[i].dma_rx_phy +
			(DMA_RX_SIZE * sizeof(struct dma_desc));

		tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr, tail, i);
		tc956xmac_start_rx(priv, priv->ioaddr, i);

		local_bh_disable();
		napi_reschedule(&ch->rx_napi);
		local_bh_enable();
	}

	wait_for_completion_timeout(&tpriv->comp, TC956XMAC_LB_TIMEOUT);
	ret = tpriv->ok ? 0 : -ETIMEDOUT;

cleanup:
	dev_mc_del(priv->dev, paddr);
	dev_set_promiscuity(priv->dev, -1);
	dev_remove_pack(&tpriv->pt);
	kfree(tpriv);
	return ret;
}

#endif

static int tc956xmac_test_vlan_validate(struct sk_buff *skb,
				     struct net_device *ndev,
				     struct packet_type *pt,
				     struct net_device *orig_ndev)
{
	struct tc956xmac_test_priv *tpriv = pt->af_packet_priv;
	struct tc956xmachdr *shdr;
	struct ethhdr *ehdr;
	struct udphdr *uhdr;
	struct iphdr *ihdr;
	u16 proto;

	proto = tpriv->double_vlan ? ETH_P_8021AD : ETH_P_8021Q;

	skb = skb_unshare(skb, GFP_ATOMIC);
	if (!skb)
		goto out;

	if (skb_linearize(skb))
		goto out;
	if (skb_headlen(skb) < (TC956XMAC_TEST_PKT_SIZE - ETH_HLEN))
		goto out;
	if (tpriv->vlan_id) {
		if (skb->vlan_proto != htons(proto))
			goto out;
		if (skb->vlan_tci != tpriv->vlan_id) {
			/* Means filter did not work. */
			tpriv->ok = false;
			complete(&tpriv->comp);
			goto out;
		}
	}

	ehdr = (struct ethhdr *)skb_mac_header(skb);
	if (!ether_addr_equal_unaligned(ehdr->h_dest, tpriv->packet->dst))
		goto out;

	ihdr = ip_hdr(skb);
	if (tpriv->double_vlan)
		ihdr = (struct iphdr *)(skb_network_header(skb) + 4);
	if (ihdr->protocol != IPPROTO_UDP)
		goto out;

	uhdr = (struct udphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
	if (uhdr->dest != htons(tpriv->packet->dport))
		goto out;

	shdr = (struct tc956xmachdr *)((u8 *)uhdr + sizeof(*uhdr));
	if (shdr->magic != cpu_to_be64(TC956XMAC_TEST_PKT_MAGIC))
		goto out;

	tpriv->ok = true;
	complete(&tpriv->comp);

out:
	kfree_skb(skb);
	return 0;
}

static int __tc956xmac_test_vlanfilt(struct tc956xmac_priv *priv)
{
	struct tc956xmac_packet_attrs attr = { };
	struct tc956xmac_test_priv *tpriv;
	struct sk_buff *skb = NULL;
	int ret = 0, i;

	tpriv = kzalloc(sizeof(*tpriv), GFP_KERNEL);
	if (!tpriv)
		return -ENOMEM;

	tpriv->ok = false;
	init_completion(&tpriv->comp);

	tpriv->pt.type = htons(ETH_P_IP);
	tpriv->pt.func = tc956xmac_test_vlan_validate;
	tpriv->pt.dev = priv->dev;
	tpriv->pt.af_packet_priv = tpriv;
	tpriv->packet = &attr;

	/*
	 * As we use HASH filtering, false positives may appear. This is a
	 * specially chosen ID so that adjacent IDs (+4) have different
	 * HASH values.
	 */
	tpriv->vlan_id = 0x123;
	dev_add_pack(&tpriv->pt);

	ret = vlan_vid_add(priv->dev, htons(ETH_P_8021Q), tpriv->vlan_id);
	if (ret)
		goto cleanup;

	for (i = 0; i < 4; i++) {
		attr.vlan = 1;
		attr.vlan_id_out = tpriv->vlan_id + i;
		attr.dst = priv->dev->dev_addr;
		attr.sport = 9;
		attr.dport = 9;

		skb = tc956xmac_test_get_udp_skb(priv, &attr);
		if (!skb) {
			ret = -ENOMEM;
			goto vlan_del;
		}

		ret = dev_direct_xmit(skb, HOST_BEST_EFF_CH);
		if (ret)
			goto vlan_del;

		wait_for_completion_timeout(&tpriv->comp, TC956XMAC_LB_TIMEOUT);
		ret = tpriv->ok ? 0 : -ETIMEDOUT;
		if (ret && !i) {
			goto vlan_del;
		} else if (!ret && i) {
			ret = -EINVAL;
			goto vlan_del;
		} else {
			ret = 0;
		}

		tpriv->ok = false;
	}

vlan_del:
	vlan_vid_del(priv->dev, htons(ETH_P_8021Q), tpriv->vlan_id);
cleanup:
	dev_remove_pack(&tpriv->pt);
	kfree(tpriv);
	return ret;
}

static int tc956xmac_test_vlanfilt(struct tc956xmac_priv *priv)
{
	if (!priv->dma_cap.vlhash)
		return -EOPNOTSUPP;

	return __tc956xmac_test_vlanfilt(priv);
}

static int tc956xmac_test_vlanfilt_perfect(struct tc956xmac_priv *priv)
{
	int ret, prev_cap = priv->dma_cap.vlhash;

	if (!(priv->dev->features & NETIF_F_HW_VLAN_CTAG_FILTER))
		return -EOPNOTSUPP;

	priv->dma_cap.vlhash = 0;
	ret = __tc956xmac_test_vlanfilt(priv);
	priv->dma_cap.vlhash = prev_cap;

	return ret;
}

static int __tc956xmac_test_dvlanfilt(struct tc956xmac_priv *priv)
{
	struct tc956xmac_packet_attrs attr = { };
	struct tc956xmac_test_priv *tpriv;
	struct sk_buff *skb = NULL;
	int ret = 0, i;

	tpriv = kzalloc(sizeof(*tpriv), GFP_KERNEL);
	if (!tpriv)
		return -ENOMEM;

	tpriv->ok = false;
	tpriv->double_vlan = true;
	init_completion(&tpriv->comp);

	tpriv->pt.type = htons(ETH_P_8021Q);
	tpriv->pt.func = tc956xmac_test_vlan_validate;
	tpriv->pt.dev = priv->dev;
	tpriv->pt.af_packet_priv = tpriv;
	tpriv->packet = &attr;

	/*
	 * As we use HASH filtering, false positives may appear. This is a
	 * specially chosen ID so that adjacent IDs (+4) have different
	 * HASH values.
	 */
	tpriv->vlan_id = 0x123;
	dev_add_pack(&tpriv->pt);

	ret = vlan_vid_add(priv->dev, htons(ETH_P_8021AD), tpriv->vlan_id);
	if (ret)
		goto cleanup;

	for (i = 0; i < 4; i++) {
		attr.vlan = 2;
		attr.vlan_id_out = tpriv->vlan_id + i;
		attr.dst = priv->dev->dev_addr;
		attr.sport = 9;
		attr.dport = 9;

		skb = tc956xmac_test_get_udp_skb(priv, &attr);
		if (!skb) {
			ret = -ENOMEM;
			goto vlan_del;
		}

		ret = dev_direct_xmit(skb, HOST_BEST_EFF_CH);
		if (ret)
			goto vlan_del;

		wait_for_completion_timeout(&tpriv->comp, TC956XMAC_LB_TIMEOUT);
		ret = tpriv->ok ? 0 : -ETIMEDOUT;
		if (ret && !i) {
			goto vlan_del;
		} else if (!ret && i) {
			ret = -EINVAL;
			goto vlan_del;
		} else {
			ret = 0;
		}

		tpriv->ok = false;
	}

vlan_del:
	vlan_vid_del(priv->dev, htons(ETH_P_8021AD), tpriv->vlan_id);
cleanup:
	dev_remove_pack(&tpriv->pt);
	kfree(tpriv);
	return ret;
}

static int tc956xmac_test_dvlanfilt(struct tc956xmac_priv *priv)
{
	if (!priv->dma_cap.vlhash)
		return -EOPNOTSUPP;

	return __tc956xmac_test_dvlanfilt(priv);
}

static int tc956xmac_test_dvlanfilt_perfect(struct tc956xmac_priv *priv)
{
	int ret, prev_cap = priv->dma_cap.vlhash;

	if (!(priv->dev->features & NETIF_F_HW_VLAN_STAG_FILTER))
		return -EOPNOTSUPP;

	priv->dma_cap.vlhash = 0;
	ret = __tc956xmac_test_dvlanfilt(priv);
	priv->dma_cap.vlhash = prev_cap;

	return ret;
}

#ifdef TC956X_FRP_ENABLE
static int tc956xmac_test_rxp(struct tc956xmac_priv *priv)
{
	unsigned char addr[ETH_ALEN] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x00};
	struct tc_cls_u32_offload cls_u32 = { };
	struct tc956xmac_packet_attrs attr = { };
	struct tc_action **actions, *act;
	struct tc_u32_sel *sel;
	struct tcf_exts *exts;
	int ret, i, nk = 1;

	if (!tc_can_offload(priv->dev))
		return -EOPNOTSUPP;
	if (!priv->dma_cap.frpsel)
		return -EOPNOTSUPP;

	sel = kzalloc(sizeof(*sel) + nk * sizeof(struct tc_u32_key), GFP_KERNEL);
	if (!sel)
		return -ENOMEM;

	exts = kzalloc(sizeof(*exts), GFP_KERNEL);
	if (!exts) {
		ret = -ENOMEM;
		goto cleanup_sel;
	}

	actions = kzalloc(nk * sizeof(*actions), GFP_KERNEL);
	if (!actions) {
		ret = -ENOMEM;
		goto cleanup_exts;
	}

	act = kzalloc(nk * sizeof(*act), GFP_KERNEL);
	if (!act) {
		ret = -ENOMEM;
		goto cleanup_actions;
	}

	cls_u32.command = TC_CLSU32_NEW_KNODE;
	cls_u32.common.chain_index = 0;
	cls_u32.common.protocol = htons(ETH_P_ALL);
	cls_u32.knode.exts = exts;
	cls_u32.knode.sel = sel;
	cls_u32.knode.handle = 0x123;

	exts->nr_actions = nk;
	exts->actions = actions;
	for (i = 0; i < nk; i++) {
		struct tcf_gact *gact = to_gact(&act[i]);

		actions[i] = &act[i];
		gact->tcf_action = TC_ACT_SHOT;
	}

	sel->nkeys = nk;
	sel->offshift = 0;
	sel->keys[0].off = 6;
	sel->keys[0].val = htonl(0xdeadbeef);
	sel->keys[0].mask = ~0x0;

	ret = tc956xmac_tc_setup_cls_u32(priv, &cls_u32);
	if (ret)
		goto cleanup_act;

	attr.dst = priv->dev->dev_addr;
	attr.src = addr;

	ret = __tc956xmac_test_loopback(priv, &attr);
	ret = ret ? 0 : -EINVAL; /* Shall NOT receive packet */

	cls_u32.command = TC_CLSU32_DELETE_KNODE;
	tc956xmac_tc_setup_cls_u32(priv, &cls_u32);

cleanup_act:
	kfree(act);
cleanup_actions:
	kfree(actions);
cleanup_exts:
	kfree(exts);
cleanup_sel:
	kfree(sel);
	return ret;
}
#endif

static int tc956xmac_test_reg_sai(struct tc956xmac_priv *priv)
{
	unsigned char src[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct tc956xmac_packet_attrs attr = { };
	int ret;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	if (!priv->dma_cap.vlins)
		return -EOPNOTSUPP;

	attr.remove_sa = true;
	attr.sarc = true;
	attr.src = src;
	attr.dst = priv->dev->dev_addr;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif

	tc956xmac_set_umac_addr(priv, priv->hw, priv->dev->dev_addr, 0, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

	if (tc956xmac_sarc_configure(priv, priv->ioaddr, 0x2))
		return -EOPNOTSUPP;

	ret = __tc956xmac_test_loopback(priv, &attr);

	tc956xmac_sarc_configure(priv, priv->ioaddr, 0x0);
	return ret;
}

static int tc956xmac_test_reg_sar(struct tc956xmac_priv *priv)
{
	unsigned char src[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct tc956xmac_packet_attrs attr = { };
	int ret;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	if (!priv->dma_cap.vlins)
		return -EOPNOTSUPP;

	attr.sarc = true;
	attr.src = src;
	attr.dst = priv->dev->dev_addr;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif

	tc956xmac_set_umac_addr(priv, priv->hw, priv->dev->dev_addr, 0, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

	if (tc956xmac_sarc_configure(priv, priv->ioaddr, 0x3))
		return -EOPNOTSUPP;

	ret = __tc956xmac_test_loopback(priv, &attr);

	tc956xmac_sarc_configure(priv, priv->ioaddr, 0x0);
	return ret;
}


static int tc956xmac_test_vlanoff_common(struct tc956xmac_priv *priv, bool svlan)
{
	struct tc956xmac_packet_attrs attr = { };
	struct tc956xmac_test_priv *tpriv;
	struct sk_buff *skb = NULL;
	int ret = 0;
	u16 proto;

	if (!priv->dma_cap.vlins)
		return -EOPNOTSUPP;

	tpriv = kzalloc(sizeof(*tpriv), GFP_KERNEL);
	if (!tpriv)
		return -ENOMEM;

	proto = svlan ? ETH_P_8021AD : ETH_P_8021Q;

	tpriv->ok = false;
	tpriv->double_vlan = svlan;
	init_completion(&tpriv->comp);

	tpriv->pt.type = svlan ? htons(ETH_P_8021Q) : htons(ETH_P_IP);
	tpriv->pt.func = tc956xmac_test_vlan_validate;
	tpriv->pt.dev = priv->dev;
	tpriv->pt.af_packet_priv = tpriv;
	tpriv->packet = &attr;
	tpriv->vlan_id = 0x123;
	dev_add_pack(&tpriv->pt);

	ret = vlan_vid_add(priv->dev, htons(proto), tpriv->vlan_id);
	if (ret)
		goto cleanup;

	attr.dst = priv->dev->dev_addr;

	skb = tc956xmac_test_get_udp_skb(priv, &attr);
	if (!skb) {
		ret = -ENOMEM;
		goto vlan_del;
	}

	__vlan_hwaccel_put_tag(skb, htons(proto), tpriv->vlan_id);
	skb->protocol = htons(proto);

	ret = dev_direct_xmit(skb, HOST_BEST_EFF_CH);
	if (ret)
		goto vlan_del;

	wait_for_completion_timeout(&tpriv->comp, TC956XMAC_LB_TIMEOUT);
	ret = tpriv->ok ? 0 : -ETIMEDOUT;

vlan_del:
	vlan_vid_del(priv->dev, htons(proto), tpriv->vlan_id);
cleanup:
	dev_remove_pack(&tpriv->pt);
	kfree(tpriv);
	return ret;
}

static int tc956xmac_test_vlanoff(struct tc956xmac_priv *priv)
{
	return tc956xmac_test_vlanoff_common(priv, false);
}


static int __tc956xmac_test_jumbo(struct tc956xmac_priv *priv, u16 queue)
{
	struct tc956xmac_packet_attrs attr = { };
	int size = priv->dma_buf_sz;

	attr.dst = priv->dev->dev_addr;
	attr.max_size = size - ETH_FCS_LEN;
	attr.queue_mapping = queue;

	return __tc956xmac_test_loopback(priv, &attr);
}

static int tc956xmac_test_jumbo(struct tc956xmac_priv *priv)
{
	return __tc956xmac_test_jumbo(priv, 0);
}

#ifdef SELFTEST_NOT_SUPPORTED

static int tc956xmac_test_tbs(struct tc956xmac_priv *priv)
{
#define TC956XMAC_TBS_LT_OFFSET		(500 * 1000 * 1000) /* 500 ms*/
	struct tc956xmac_packet_attrs attr = { };
	struct tc_etf_qopt_offload qopt;
	u64 start_time, curr_time = 0;
	unsigned long flags;
	int ret, i;

	if (!priv->hwts_tx_en)
		return -EOPNOTSUPP;

	/* Find first TBS enabled Queue, if any */
	for (i = 0; i < priv->plat->tx_queues_to_use; i++)
		if (priv->tx_queue[i].tbs & TC956XMAC_TBS_AVAIL)
			break;

	if (i >= priv->plat->tx_queues_to_use)
		return -EOPNOTSUPP;

	qopt.enable = true;
	qopt.queue = i;

	ret = tc956xmac_tc_setup_etf(priv, &qopt);
	if (ret)
		return ret;

	spin_lock_irqsave(&priv->ptp_lock, flags);
	tc956xmac_get_systime(priv, priv->ptpaddr, &curr_time);
	spin_unlock_irqrestore(&priv->ptp_lock, flags);

	if (!curr_time) {
		ret = -EOPNOTSUPP;
		goto fail_disable;
	}

	start_time = curr_time;
	curr_time += TC956XMAC_TBS_LT_OFFSET;

	attr.dst = priv->dev->dev_addr;
	attr.timestamp = curr_time;
	attr.timeout = nsecs_to_jiffies(2 * TC956XMAC_TBS_LT_OFFSET);
	attr.queue_mapping = i;

	ret = __tc956xmac_test_loopback(priv, &attr);
	if (ret)
		goto fail_disable;

	/* Check if expected time has elapsed */
	spin_lock_irqsave(&priv->ptp_lock, flags);
	tc956xmac_get_systime(priv, priv->ptpaddr, &curr_time);
	spin_unlock_irqrestore(&priv->ptp_lock, flags);

	if ((curr_time - start_time) < TC956XMAC_TBS_LT_OFFSET)
		ret = -EINVAL;

fail_disable:
	qopt.enable = false;
	tc956xmac_tc_setup_etf(priv, &qopt);
	return ret;
}

static int tc956xmac_test_ptp_validate(struct sk_buff *skb,
				    struct net_device *ndev,
				    struct packet_type *pt,
				    struct net_device *orig_ndev)
{
	unsigned char dst[ETH_ALEN] = { 0x01, 0x1b, 0x19, 0x00, 0x00, 0x00 };
	struct tc956xmac_test_priv *tpriv = pt->af_packet_priv;
	struct ethhdr *ehdr;

	ehdr = (struct ethhdr *)skb_mac_header(skb);
	if (!ether_addr_equal(ehdr->h_source, orig_ndev->dev_addr))
		goto out;
	if (!ether_addr_equal(ehdr->h_dest, dst))
		goto out;

	tpriv->ok = true;
	complete(&tpriv->comp);
out:
	kfree_skb(skb);
	return 0;
}

static int tc956xmac_test_ptp_offload(struct tc956xmac_priv *priv)
{
#define TC956XMAC_PTP_CNT			10
	struct tc956xmac_test_priv *tpriv;
	int ret, i;

	if (!priv->dma_cap.ptoen)
		return -EOPNOTSUPP;
	if (!priv->hwts_tx_en)
		return -EOPNOTSUPP;

	tpriv = kzalloc(sizeof(*tpriv), GFP_KERNEL);
	if (!tpriv)
		return -ENOMEM;

	tpriv->ok = false;
	init_completion(&tpriv->comp);

	tpriv->pt.type = htons(ETH_P_1588);
	tpriv->pt.func = tc956xmac_test_ptp_validate;
	tpriv->pt.dev = priv->dev;
	tpriv->pt.af_packet_priv = tpriv;
	dev_add_pack(&tpriv->pt);

	ret = dev_set_promiscuity(priv->dev, 1);
	if (ret)
		goto cleanup;

	ret = tc956xmac_set_ptp_offload(priv, priv->ioaddr, true);
	if (ret)
		goto cleanup;

	for (i = 0; i < TC956XMAC_PTP_CNT; i++) {
		wait_for_completion_timeout(&tpriv->comp, TC956XMAC_PTP_TIMEOUT);
		ret = tpriv->ok ? 0 : -ETIMEDOUT;
		if (ret)
			goto cleanup;
	}

cleanup:
	tc956xmac_set_ptp_offload(priv, priv->ioaddr, false);
	dev_set_promiscuity(priv->dev, -1);
	dev_remove_pack(&tpriv->pt);
	kfree(tpriv);
	return ret;
}
#endif

#define TC956XMAC_LOOPBACK_NONE	0
#define TC956XMAC_LOOPBACK_MAC	1
#define TC956XMAC_LOOPBACK_PHY	2

static const struct tc956xmac_test {
	char name[ETH_GSTRING_LEN];
	int lb;
	int (*fn)(struct tc956xmac_priv *priv);
} tc956xmac_selftests[] = {
	{
		.name = "MAC Loopback               ",
		.lb = TC956XMAC_LOOPBACK_MAC,
		.fn = tc956xmac_test_mac_loopback,
	}, {
		.name = "PHY Loopback               ",
		.lb = TC956XMAC_LOOPBACK_NONE, /* Test will handle it */
		.fn = tc956xmac_test_phy_loopback,
	}, {
		.name = "MMC Counters               ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_mmc,
	},
#ifdef SELFTEST_NOT_SUPPORTED
	{
		.name = "EEE                        ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_eee,
	},
#endif
	{
		.name = "Hash Filter MC             ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_hfilt,
	}, {
		.name = "Perfect Filter UC          ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_pfilt,
	}, {
		.name = "MC Filter                  ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_mcfilt,
	}, {
		.name = "UC Filter                  ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_ucfilt,
	},
#ifdef SELFTEST_NOT_SUPPORTED
	{
		.name = "Flow Control               ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_flowctrl,
	},
#endif
	{
		.name = "VLAN Filtering             ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_vlanfilt,
	}, {
		.name = "VLAN Filtering (perf)      ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_vlanfilt_perfect,
	}, {
		.name = "Double VLAN Filter         ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_dvlanfilt,
	}, {
		.name = "Double VLAN Filter (perf)  ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_dvlanfilt_perfect,
	},
#ifdef TC956X_FRP_ENABLE
	{
		.name = "Flexible RX Parser         ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_rxp,
	},
#endif
	{
		.name = "SA Insertion (reg)         ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_reg_sai,
	}, {
		.name = "SA Replacement (reg)       ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_reg_sar,
	}, {
		.name = "VLAN TX Insertion          ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_vlanoff,
	}, {
		.name = "Jumbo Frame                ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_jumbo,
	},
#ifdef SELFTEST_NOT_SUPPORTED
	{
		.name = "TBS (ETF Scheduler)        ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_tbs,
	}, {
		.name = "PTP Offload                ",
		.lb = TC956XMAC_LOOPBACK_PHY,
		.fn = tc956xmac_test_ptp_offload,
	},
#endif
};

void tc956xmac_selftest_run(struct net_device *dev,
			 struct ethtool_test *etest, u64 *buf)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int count = tc956xmac_selftest_get_count(priv);
	int i, ret, phy_loopback_enabled = 0;

	memset(buf, 0, sizeof(*buf) * count);
	tc956xmac_test_next_id = 0;

	if (etest->flags != ETH_TEST_FL_OFFLINE) {
		netdev_err(priv->dev, "Only offline tests are supported\n");
		etest->flags |= ETH_TEST_FL_FAILED;
		return;
	} else if (!netif_carrier_ok(dev)) {
		netdev_err(priv->dev, "You need valid Link to execute tests\n");
		etest->flags |= ETH_TEST_FL_FAILED;
		return;
	}

	/* Wait for queues drain */
	msleep(200);

	for (i = 0; i < count; i++) {
		ret = 0;

		switch (tc956xmac_selftests[i].lb) {
		case TC956XMAC_LOOPBACK_PHY:
			ret = -EOPNOTSUPP;
			if ((dev->phydev) && !(phy_loopback_enabled)) {
				ret = phy_loopback(dev->phydev, true);
				msleep(7000);
				phy_loopback_enabled = 1;
			} else {
				ret = 0;
			}

			if (!ret)
				break;
			/* Fallthrough */
		case TC956XMAC_LOOPBACK_MAC:
			ret = tc956xmac_set_mac_loopback(priv, priv->ioaddr, true);
			break;
		case TC956XMAC_LOOPBACK_NONE:
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
		}

		/*
		 * First tests will always be MAC / PHY loobpack. If any of
		 * them is not supported we abort earlier.
		 */
		if (ret) {
			netdev_err(priv->dev, "Loopback is not supported\n");
			etest->flags |= ETH_TEST_FL_FAILED;
			break;
		}

		ret = tc956xmac_selftests[i].fn(priv);
		if (ret && (ret != -EOPNOTSUPP))
			etest->flags |= ETH_TEST_FL_FAILED;
		buf[i] = ret;

		switch (tc956xmac_selftests[i].lb) {
			/* Fallthrough */
		case TC956XMAC_LOOPBACK_MAC:
			tc956xmac_set_mac_loopback(priv, priv->ioaddr, false);
			break;
		default:
			break;
		}
	}

	if (dev->phydev) {
		ret = phy_loopback(dev->phydev, false);
		msleep(7000);
	}
}

void tc956xmac_selftest_get_strings(struct tc956xmac_priv *priv, u8 *data)
{
	u8 *p = data;
	int i;

	for (i = 0; i < tc956xmac_selftest_get_count(priv); i++) {
		snprintf(p, ETH_GSTRING_LEN, "%2d. %s", i + 1,
			 tc956xmac_selftests[i].name);
		p += ETH_GSTRING_LEN;
	}
}

int tc956xmac_selftest_get_count(struct tc956xmac_priv *priv)
{
	return ARRAY_SIZE(tc956xmac_selftests);
}
