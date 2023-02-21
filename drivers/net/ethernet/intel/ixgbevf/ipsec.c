// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Oracle and/or its affiliates. All rights reserved. */

#include "ixgbevf.h"
#include <net/xfrm.h>
#include <crypto/aead.h>

#define IXGBE_IPSEC_KEY_BITS  160
static const char aes_gcm_name[] = "rfc4106(gcm(aes))";

/**
 * ixgbevf_ipsec_set_pf_sa - ask the PF to set up an SA
 * @adapter: board private structure
 * @xs: xfrm info to be sent to the PF
 *
 * Returns: positive offload handle from the PF, or negative error code
 **/
static int ixgbevf_ipsec_set_pf_sa(struct ixgbevf_adapter *adapter,
				   struct xfrm_state *xs)
{
	u32 msgbuf[IXGBE_VFMAILBOX_SIZE] = { 0 };
	struct ixgbe_hw *hw = &adapter->hw;
	struct sa_mbx_msg *sam;
	int ret;

	/* send the important bits to the PF */
	sam = (struct sa_mbx_msg *)(&msgbuf[1]);
	sam->dir = xs->xso.dir;
	sam->spi = xs->id.spi;
	sam->proto = xs->id.proto;
	sam->family = xs->props.family;

	if (xs->props.family == AF_INET6)
		memcpy(sam->addr, &xs->id.daddr.a6, sizeof(xs->id.daddr.a6));
	else
		memcpy(sam->addr, &xs->id.daddr.a4, sizeof(xs->id.daddr.a4));
	memcpy(sam->key, xs->aead->alg_key, sizeof(sam->key));

	msgbuf[0] = IXGBE_VF_IPSEC_ADD;

	spin_lock_bh(&adapter->mbx_lock);

	ret = ixgbevf_write_mbx(hw, msgbuf, IXGBE_VFMAILBOX_SIZE);
	if (ret)
		goto out;

	ret = ixgbevf_poll_mbx(hw, msgbuf, 2);
	if (ret)
		goto out;

	ret = (int)msgbuf[1];
	if (msgbuf[0] & IXGBE_VT_MSGTYPE_FAILURE && ret >= 0)
		ret = -1;

out:
	spin_unlock_bh(&adapter->mbx_lock);

	return ret;
}

/**
 * ixgbevf_ipsec_del_pf_sa - ask the PF to delete an SA
 * @adapter: board private structure
 * @pfsa: sa index returned from PF when created, -1 for all
 *
 * Returns: 0 on success, or negative error code
 **/
static int ixgbevf_ipsec_del_pf_sa(struct ixgbevf_adapter *adapter, int pfsa)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 msgbuf[2];
	int err;

	memset(msgbuf, 0, sizeof(msgbuf));
	msgbuf[0] = IXGBE_VF_IPSEC_DEL;
	msgbuf[1] = (u32)pfsa;

	spin_lock_bh(&adapter->mbx_lock);

	err = ixgbevf_write_mbx(hw, msgbuf, 2);
	if (err)
		goto out;

	err = ixgbevf_poll_mbx(hw, msgbuf, 2);
	if (err)
		goto out;

out:
	spin_unlock_bh(&adapter->mbx_lock);
	return err;
}

/**
 * ixgbevf_ipsec_restore - restore the IPsec HW settings after a reset
 * @adapter: board private structure
 *
 * Reload the HW tables from the SW tables after they've been bashed
 * by a chip reset.  While we're here, make sure any stale VF data is
 * removed, since we go through reset when num_vfs changes.
 **/
void ixgbevf_ipsec_restore(struct ixgbevf_adapter *adapter)
{
	struct ixgbevf_ipsec *ipsec = adapter->ipsec;
	struct net_device *netdev = adapter->netdev;
	int i;

	if (!(adapter->netdev->features & NETIF_F_HW_ESP))
		return;

	/* reload the Rx and Tx keys */
	for (i = 0; i < IXGBE_IPSEC_MAX_SA_COUNT; i++) {
		struct rx_sa *r = &ipsec->rx_tbl[i];
		struct tx_sa *t = &ipsec->tx_tbl[i];
		int ret;

		if (r->used) {
			ret = ixgbevf_ipsec_set_pf_sa(adapter, r->xs);
			if (ret < 0)
				netdev_err(netdev, "reload rx_tbl[%d] failed = %d\n",
					   i, ret);
		}

		if (t->used) {
			ret = ixgbevf_ipsec_set_pf_sa(adapter, t->xs);
			if (ret < 0)
				netdev_err(netdev, "reload tx_tbl[%d] failed = %d\n",
					   i, ret);
		}
	}
}

/**
 * ixgbevf_ipsec_find_empty_idx - find the first unused security parameter index
 * @ipsec: pointer to IPsec struct
 * @rxtable: true if we need to look in the Rx table
 *
 * Returns the first unused index in either the Rx or Tx SA table
 **/
static
int ixgbevf_ipsec_find_empty_idx(struct ixgbevf_ipsec *ipsec, bool rxtable)
{
	u32 i;

	if (rxtable) {
		if (ipsec->num_rx_sa == IXGBE_IPSEC_MAX_SA_COUNT)
			return -ENOSPC;

		/* search rx sa table */
		for (i = 0; i < IXGBE_IPSEC_MAX_SA_COUNT; i++) {
			if (!ipsec->rx_tbl[i].used)
				return i;
		}
	} else {
		if (ipsec->num_tx_sa == IXGBE_IPSEC_MAX_SA_COUNT)
			return -ENOSPC;

		/* search tx sa table */
		for (i = 0; i < IXGBE_IPSEC_MAX_SA_COUNT; i++) {
			if (!ipsec->tx_tbl[i].used)
				return i;
		}
	}

	return -ENOSPC;
}

/**
 * ixgbevf_ipsec_find_rx_state - find the state that matches
 * @ipsec: pointer to IPsec struct
 * @daddr: inbound address to match
 * @proto: protocol to match
 * @spi: SPI to match
 * @ip4: true if using an IPv4 address
 *
 * Returns a pointer to the matching SA state information
 **/
static
struct xfrm_state *ixgbevf_ipsec_find_rx_state(struct ixgbevf_ipsec *ipsec,
					       __be32 *daddr, u8 proto,
					       __be32 spi, bool ip4)
{
	struct xfrm_state *ret = NULL;
	struct rx_sa *rsa;

	rcu_read_lock();
	hash_for_each_possible_rcu(ipsec->rx_sa_list, rsa, hlist,
				   (__force u32)spi) {
		if (spi == rsa->xs->id.spi &&
		    ((ip4 && *daddr == rsa->xs->id.daddr.a4) ||
		      (!ip4 && !memcmp(daddr, &rsa->xs->id.daddr.a6,
				       sizeof(rsa->xs->id.daddr.a6)))) &&
		    proto == rsa->xs->id.proto) {
			ret = rsa->xs;
			xfrm_state_hold(ret);
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

/**
 * ixgbevf_ipsec_parse_proto_keys - find the key and salt based on the protocol
 * @xs: pointer to xfrm_state struct
 * @mykey: pointer to key array to populate
 * @mysalt: pointer to salt value to populate
 *
 * This copies the protocol keys and salt to our own data tables.  The
 * 82599 family only supports the one algorithm.
 **/
static int ixgbevf_ipsec_parse_proto_keys(struct xfrm_state *xs,
					  u32 *mykey, u32 *mysalt)
{
	struct net_device *dev = xs->xso.real_dev;
	unsigned char *key_data;
	char *alg_name = NULL;
	int key_len;

	if (!xs->aead) {
		netdev_err(dev, "Unsupported IPsec algorithm\n");
		return -EINVAL;
	}

	if (xs->aead->alg_icv_len != IXGBE_IPSEC_AUTH_BITS) {
		netdev_err(dev, "IPsec offload requires %d bit authentication\n",
			   IXGBE_IPSEC_AUTH_BITS);
		return -EINVAL;
	}

	key_data = &xs->aead->alg_key[0];
	key_len = xs->aead->alg_key_len;
	alg_name = xs->aead->alg_name;

	if (strcmp(alg_name, aes_gcm_name)) {
		netdev_err(dev, "Unsupported IPsec algorithm - please use %s\n",
			   aes_gcm_name);
		return -EINVAL;
	}

	/* The key bytes come down in a big endian array of bytes, so
	 * we don't need to do any byte swapping.
	 * 160 accounts for 16 byte key and 4 byte salt
	 */
	if (key_len > IXGBE_IPSEC_KEY_BITS) {
		*mysalt = ((u32 *)key_data)[4];
	} else if (key_len == IXGBE_IPSEC_KEY_BITS) {
		*mysalt = 0;
	} else {
		netdev_err(dev, "IPsec hw offload only supports keys up to 128 bits with a 32 bit salt\n");
		return -EINVAL;
	}
	memcpy(mykey, key_data, 16);

	return 0;
}

/**
 * ixgbevf_ipsec_add_sa - program device with a security association
 * @xs: pointer to transformer state struct
 **/
static int ixgbevf_ipsec_add_sa(struct xfrm_state *xs)
{
	struct net_device *dev = xs->xso.real_dev;
	struct ixgbevf_adapter *adapter;
	struct ixgbevf_ipsec *ipsec;
	u16 sa_idx;
	int ret;

	adapter = netdev_priv(dev);
	ipsec = adapter->ipsec;

	if (xs->id.proto != IPPROTO_ESP && xs->id.proto != IPPROTO_AH) {
		netdev_err(dev, "Unsupported protocol 0x%04x for IPsec offload\n",
			   xs->id.proto);
		return -EINVAL;
	}

	if (xs->props.mode != XFRM_MODE_TRANSPORT) {
		netdev_err(dev, "Unsupported mode for ipsec offload\n");
		return -EINVAL;
	}

	if (xs->xso.type != XFRM_DEV_OFFLOAD_CRYPTO) {
		netdev_err(dev, "Unsupported ipsec offload type\n");
		return -EINVAL;
	}

	if (xs->xso.dir == XFRM_DEV_OFFLOAD_IN) {
		struct rx_sa rsa;

		if (xs->calg) {
			netdev_err(dev, "Compression offload not supported\n");
			return -EINVAL;
		}

		/* find the first unused index */
		ret = ixgbevf_ipsec_find_empty_idx(ipsec, true);
		if (ret < 0) {
			netdev_err(dev, "No space for SA in Rx table!\n");
			return ret;
		}
		sa_idx = (u16)ret;

		memset(&rsa, 0, sizeof(rsa));
		rsa.used = true;
		rsa.xs = xs;

		if (rsa.xs->id.proto & IPPROTO_ESP)
			rsa.decrypt = xs->ealg || xs->aead;

		/* get the key and salt */
		ret = ixgbevf_ipsec_parse_proto_keys(xs, rsa.key, &rsa.salt);
		if (ret) {
			netdev_err(dev, "Failed to get key data for Rx SA table\n");
			return ret;
		}

		/* get ip for rx sa table */
		if (xs->props.family == AF_INET6)
			memcpy(rsa.ipaddr, &xs->id.daddr.a6, 16);
		else
			memcpy(&rsa.ipaddr[3], &xs->id.daddr.a4, 4);

		rsa.mode = IXGBE_RXMOD_VALID;
		if (rsa.xs->id.proto & IPPROTO_ESP)
			rsa.mode |= IXGBE_RXMOD_PROTO_ESP;
		if (rsa.decrypt)
			rsa.mode |= IXGBE_RXMOD_DECRYPT;
		if (rsa.xs->props.family == AF_INET6)
			rsa.mode |= IXGBE_RXMOD_IPV6;

		ret = ixgbevf_ipsec_set_pf_sa(adapter, xs);
		if (ret < 0)
			return ret;
		rsa.pfsa = ret;

		/* the preparations worked, so save the info */
		memcpy(&ipsec->rx_tbl[sa_idx], &rsa, sizeof(rsa));

		xs->xso.offload_handle = sa_idx + IXGBE_IPSEC_BASE_RX_INDEX;

		ipsec->num_rx_sa++;

		/* hash the new entry for faster search in Rx path */
		hash_add_rcu(ipsec->rx_sa_list, &ipsec->rx_tbl[sa_idx].hlist,
			     (__force u32)rsa.xs->id.spi);
	} else {
		struct tx_sa tsa;

		/* find the first unused index */
		ret = ixgbevf_ipsec_find_empty_idx(ipsec, false);
		if (ret < 0) {
			netdev_err(dev, "No space for SA in Tx table\n");
			return ret;
		}
		sa_idx = (u16)ret;

		memset(&tsa, 0, sizeof(tsa));
		tsa.used = true;
		tsa.xs = xs;

		if (xs->id.proto & IPPROTO_ESP)
			tsa.encrypt = xs->ealg || xs->aead;

		ret = ixgbevf_ipsec_parse_proto_keys(xs, tsa.key, &tsa.salt);
		if (ret) {
			netdev_err(dev, "Failed to get key data for Tx SA table\n");
			memset(&tsa, 0, sizeof(tsa));
			return ret;
		}

		ret = ixgbevf_ipsec_set_pf_sa(adapter, xs);
		if (ret < 0)
			return ret;
		tsa.pfsa = ret;

		/* the preparations worked, so save the info */
		memcpy(&ipsec->tx_tbl[sa_idx], &tsa, sizeof(tsa));

		xs->xso.offload_handle = sa_idx + IXGBE_IPSEC_BASE_TX_INDEX;

		ipsec->num_tx_sa++;
	}

	return 0;
}

/**
 * ixgbevf_ipsec_del_sa - clear out this specific SA
 * @xs: pointer to transformer state struct
 **/
static void ixgbevf_ipsec_del_sa(struct xfrm_state *xs)
{
	struct net_device *dev = xs->xso.real_dev;
	struct ixgbevf_adapter *adapter;
	struct ixgbevf_ipsec *ipsec;
	u16 sa_idx;

	adapter = netdev_priv(dev);
	ipsec = adapter->ipsec;

	if (xs->xso.dir == XFRM_DEV_OFFLOAD_IN) {
		sa_idx = xs->xso.offload_handle - IXGBE_IPSEC_BASE_RX_INDEX;

		if (!ipsec->rx_tbl[sa_idx].used) {
			netdev_err(dev, "Invalid Rx SA selected sa_idx=%d offload_handle=%lu\n",
				   sa_idx, xs->xso.offload_handle);
			return;
		}

		ixgbevf_ipsec_del_pf_sa(adapter, ipsec->rx_tbl[sa_idx].pfsa);
		hash_del_rcu(&ipsec->rx_tbl[sa_idx].hlist);
		memset(&ipsec->rx_tbl[sa_idx], 0, sizeof(struct rx_sa));
		ipsec->num_rx_sa--;
	} else {
		sa_idx = xs->xso.offload_handle - IXGBE_IPSEC_BASE_TX_INDEX;

		if (!ipsec->tx_tbl[sa_idx].used) {
			netdev_err(dev, "Invalid Tx SA selected sa_idx=%d offload_handle=%lu\n",
				   sa_idx, xs->xso.offload_handle);
			return;
		}

		ixgbevf_ipsec_del_pf_sa(adapter, ipsec->tx_tbl[sa_idx].pfsa);
		memset(&ipsec->tx_tbl[sa_idx], 0, sizeof(struct tx_sa));
		ipsec->num_tx_sa--;
	}
}

/**
 * ixgbevf_ipsec_offload_ok - can this packet use the xfrm hw offload
 * @skb: current data packet
 * @xs: pointer to transformer state struct
 **/
static bool ixgbevf_ipsec_offload_ok(struct sk_buff *skb, struct xfrm_state *xs)
{
	if (xs->props.family == AF_INET) {
		/* Offload with IPv4 options is not supported yet */
		if (ip_hdr(skb)->ihl != 5)
			return false;
	} else {
		/* Offload with IPv6 extension headers is not support yet */
		if (ipv6_ext_hdr(ipv6_hdr(skb)->nexthdr))
			return false;
	}

	return true;
}

static const struct xfrmdev_ops ixgbevf_xfrmdev_ops = {
	.xdo_dev_state_add = ixgbevf_ipsec_add_sa,
	.xdo_dev_state_delete = ixgbevf_ipsec_del_sa,
	.xdo_dev_offload_ok = ixgbevf_ipsec_offload_ok,
};

/**
 * ixgbevf_ipsec_tx - setup Tx flags for IPsec offload
 * @tx_ring: outgoing context
 * @first: current data packet
 * @itd: ipsec Tx data for later use in building context descriptor
 **/
int ixgbevf_ipsec_tx(struct ixgbevf_ring *tx_ring,
		     struct ixgbevf_tx_buffer *first,
		     struct ixgbevf_ipsec_tx_data *itd)
{
	struct ixgbevf_adapter *adapter = netdev_priv(tx_ring->netdev);
	struct ixgbevf_ipsec *ipsec = adapter->ipsec;
	struct xfrm_state *xs;
	struct sec_path *sp;
	struct tx_sa *tsa;
	u16 sa_idx;

	sp = skb_sec_path(first->skb);
	if (unlikely(!sp->len)) {
		netdev_err(tx_ring->netdev, "%s: no xfrm state len = %d\n",
			   __func__, sp->len);
		return 0;
	}

	xs = xfrm_input_state(first->skb);
	if (unlikely(!xs)) {
		netdev_err(tx_ring->netdev, "%s: no xfrm_input_state() xs = %p\n",
			   __func__, xs);
		return 0;
	}

	sa_idx = xs->xso.offload_handle - IXGBE_IPSEC_BASE_TX_INDEX;
	if (unlikely(sa_idx >= IXGBE_IPSEC_MAX_SA_COUNT)) {
		netdev_err(tx_ring->netdev, "%s: bad sa_idx=%d handle=%lu\n",
			   __func__, sa_idx, xs->xso.offload_handle);
		return 0;
	}

	tsa = &ipsec->tx_tbl[sa_idx];
	if (unlikely(!tsa->used)) {
		netdev_err(tx_ring->netdev, "%s: unused sa_idx=%d\n",
			   __func__, sa_idx);
		return 0;
	}

	itd->pfsa = tsa->pfsa - IXGBE_IPSEC_BASE_TX_INDEX;

	first->tx_flags |= IXGBE_TX_FLAGS_IPSEC | IXGBE_TX_FLAGS_CSUM;

	if (xs->id.proto == IPPROTO_ESP) {
		itd->flags |= IXGBE_ADVTXD_TUCMD_IPSEC_TYPE_ESP |
			      IXGBE_ADVTXD_TUCMD_L4T_TCP;
		if (first->protocol == htons(ETH_P_IP))
			itd->flags |= IXGBE_ADVTXD_TUCMD_IPV4;

		/* The actual trailer length is authlen (16 bytes) plus
		 * 2 bytes for the proto and the padlen values, plus
		 * padlen bytes of padding.  This ends up not the same
		 * as the static value found in xs->props.trailer_len (21).
		 *
		 * ... but if we're doing GSO, don't bother as the stack
		 * doesn't add a trailer for those.
		 */
		if (!skb_is_gso(first->skb)) {
			/* The "correct" way to get the auth length would be
			 * to use
			 *    authlen = crypto_aead_authsize(xs->data);
			 * but since we know we only have one size to worry
			 * about * we can let the compiler use the constant
			 * and save us a few CPU cycles.
			 */
			const int authlen = IXGBE_IPSEC_AUTH_BITS / 8;
			struct sk_buff *skb = first->skb;
			u8 padlen;
			int ret;

			ret = skb_copy_bits(skb, skb->len - (authlen + 2),
					    &padlen, 1);
			if (unlikely(ret))
				return 0;
			itd->trailer_len = authlen + 2 + padlen;
		}
	}
	if (tsa->encrypt)
		itd->flags |= IXGBE_ADVTXD_TUCMD_IPSEC_ENCRYPT_EN;

	return 1;
}

/**
 * ixgbevf_ipsec_rx - decode IPsec bits from Rx descriptor
 * @rx_ring: receiving ring
 * @rx_desc: receive data descriptor
 * @skb: current data packet
 *
 * Determine if there was an IPsec encapsulation noticed, and if so set up
 * the resulting status for later in the receive stack.
 **/
void ixgbevf_ipsec_rx(struct ixgbevf_ring *rx_ring,
		      union ixgbe_adv_rx_desc *rx_desc,
		      struct sk_buff *skb)
{
	struct ixgbevf_adapter *adapter = netdev_priv(rx_ring->netdev);
	__le16 pkt_info = rx_desc->wb.lower.lo_dword.hs_rss.pkt_info;
	__le16 ipsec_pkt_types = cpu_to_le16(IXGBE_RXDADV_PKTTYPE_IPSEC_AH |
					     IXGBE_RXDADV_PKTTYPE_IPSEC_ESP);
	struct ixgbevf_ipsec *ipsec = adapter->ipsec;
	struct xfrm_offload *xo = NULL;
	struct xfrm_state *xs = NULL;
	struct ipv6hdr *ip6 = NULL;
	struct iphdr *ip4 = NULL;
	struct sec_path *sp;
	void *daddr;
	__be32 spi;
	u8 *c_hdr;
	u8 proto;

	/* Find the IP and crypto headers in the data.
	 * We can assume no VLAN header in the way, b/c the
	 * hw won't recognize the IPsec packet and anyway the
	 * currently VLAN device doesn't support xfrm offload.
	 */
	if (pkt_info & cpu_to_le16(IXGBE_RXDADV_PKTTYPE_IPV4)) {
		ip4 = (struct iphdr *)(skb->data + ETH_HLEN);
		daddr = &ip4->daddr;
		c_hdr = (u8 *)ip4 + ip4->ihl * 4;
	} else if (pkt_info & cpu_to_le16(IXGBE_RXDADV_PKTTYPE_IPV6)) {
		ip6 = (struct ipv6hdr *)(skb->data + ETH_HLEN);
		daddr = &ip6->daddr;
		c_hdr = (u8 *)ip6 + sizeof(struct ipv6hdr);
	} else {
		return;
	}

	switch (pkt_info & ipsec_pkt_types) {
	case cpu_to_le16(IXGBE_RXDADV_PKTTYPE_IPSEC_AH):
		spi = ((struct ip_auth_hdr *)c_hdr)->spi;
		proto = IPPROTO_AH;
		break;
	case cpu_to_le16(IXGBE_RXDADV_PKTTYPE_IPSEC_ESP):
		spi = ((struct ip_esp_hdr *)c_hdr)->spi;
		proto = IPPROTO_ESP;
		break;
	default:
		return;
	}

	xs = ixgbevf_ipsec_find_rx_state(ipsec, daddr, proto, spi, !!ip4);
	if (unlikely(!xs))
		return;

	sp = secpath_set(skb);
	if (unlikely(!sp))
		return;

	sp->xvec[sp->len++] = xs;
	sp->olen++;
	xo = xfrm_offload(skb);
	xo->flags = CRYPTO_DONE;
	xo->status = CRYPTO_SUCCESS;

	adapter->rx_ipsec++;
}

/**
 * ixgbevf_init_ipsec_offload - initialize registers for IPsec operation
 * @adapter: board private structure
 **/
void ixgbevf_init_ipsec_offload(struct ixgbevf_adapter *adapter)
{
	struct ixgbevf_ipsec *ipsec;
	size_t size;

	switch (adapter->hw.api_version) {
	case ixgbe_mbox_api_14:
	case ixgbe_mbox_api_15:
		break;
	default:
		return;
	}

	ipsec = kzalloc(sizeof(*ipsec), GFP_KERNEL);
	if (!ipsec)
		goto err1;
	hash_init(ipsec->rx_sa_list);

	size = sizeof(struct rx_sa) * IXGBE_IPSEC_MAX_SA_COUNT;
	ipsec->rx_tbl = kzalloc(size, GFP_KERNEL);
	if (!ipsec->rx_tbl)
		goto err2;

	size = sizeof(struct tx_sa) * IXGBE_IPSEC_MAX_SA_COUNT;
	ipsec->tx_tbl = kzalloc(size, GFP_KERNEL);
	if (!ipsec->tx_tbl)
		goto err2;

	ipsec->num_rx_sa = 0;
	ipsec->num_tx_sa = 0;

	adapter->ipsec = ipsec;

	adapter->netdev->xfrmdev_ops = &ixgbevf_xfrmdev_ops;

#define IXGBEVF_ESP_FEATURES	(NETIF_F_HW_ESP | \
				 NETIF_F_HW_ESP_TX_CSUM | \
				 NETIF_F_GSO_ESP)

	adapter->netdev->features |= IXGBEVF_ESP_FEATURES;
	adapter->netdev->hw_enc_features |= IXGBEVF_ESP_FEATURES;

	return;

err2:
	kfree(ipsec->rx_tbl);
	kfree(ipsec->tx_tbl);
	kfree(ipsec);
err1:
	netdev_err(adapter->netdev, "Unable to allocate memory for SA tables");
}

/**
 * ixgbevf_stop_ipsec_offload - tear down the IPsec offload
 * @adapter: board private structure
 **/
void ixgbevf_stop_ipsec_offload(struct ixgbevf_adapter *adapter)
{
	struct ixgbevf_ipsec *ipsec = adapter->ipsec;

	adapter->ipsec = NULL;
	if (ipsec) {
		kfree(ipsec->rx_tbl);
		kfree(ipsec->tx_tbl);
		kfree(ipsec);
	}
}
