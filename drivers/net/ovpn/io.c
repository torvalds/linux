// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2019-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#include <crypto/aead.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/gro_cells.h>
#include <net/gso.h>
#include <net/ip.h>

#include "ovpnpriv.h"
#include "peer.h"
#include "io.h"
#include "bind.h"
#include "crypto.h"
#include "crypto_aead.h"
#include "netlink.h"
#include "proto.h"
#include "tcp.h"
#include "udp.h"
#include "skb.h"
#include "socket.h"

const unsigned char ovpn_keepalive_message[OVPN_KEEPALIVE_SIZE] = {
	0x2a, 0x18, 0x7b, 0xf3, 0x64, 0x1e, 0xb4, 0xcb,
	0x07, 0xed, 0x2d, 0x0a, 0x98, 0x1f, 0xc7, 0x48
};

/**
 * ovpn_is_keepalive - check if skb contains a keepalive message
 * @skb: packet to check
 *
 * Assumes that the first byte of skb->data is defined.
 *
 * Return: true if skb contains a keepalive or false otherwise
 */
static bool ovpn_is_keepalive(struct sk_buff *skb)
{
	if (*skb->data != ovpn_keepalive_message[0])
		return false;

	if (skb->len != OVPN_KEEPALIVE_SIZE)
		return false;

	if (!pskb_may_pull(skb, OVPN_KEEPALIVE_SIZE))
		return false;

	return !memcmp(skb->data, ovpn_keepalive_message, OVPN_KEEPALIVE_SIZE);
}

/* Called after decrypt to write the IP packet to the device.
 * This method is expected to manage/free the skb.
 */
static void ovpn_netdev_write(struct ovpn_peer *peer, struct sk_buff *skb)
{
	unsigned int pkt_len;
	int ret;

	/*
	 * GSO state from the transport layer is not valid for the tunnel/data
	 * path. Reset all GSO fields to prevent any further GSO processing
	 * from entering an inconsistent state.
	 */
	skb_gso_reset(skb);

	/* we can't guarantee the packet wasn't corrupted before entering the
	 * VPN, therefore we give other layers a chance to check that
	 */
	skb->ip_summed = CHECKSUM_NONE;

	/* skb hash for transport packet no longer valid after decapsulation */
	skb_clear_hash(skb);

	/* post-decrypt scrub -- prepare to inject encapsulated packet onto the
	 * interface, based on __skb_tunnel_rx() in dst.h
	 */
	skb->dev = peer->ovpn->dev;
	skb_set_queue_mapping(skb, 0);
	skb_scrub_packet(skb, true);

	/* network header reset in ovpn_decrypt_post() */
	skb_reset_transport_header(skb);
	skb_reset_inner_headers(skb);

	/* cause packet to be "received" by the interface */
	pkt_len = skb->len;
	ret = gro_cells_receive(&peer->ovpn->gro_cells, skb);
	if (likely(ret == NET_RX_SUCCESS)) {
		/* update RX stats with the size of decrypted packet */
		ovpn_peer_stats_increment_rx(&peer->vpn_stats, pkt_len);
		dev_dstats_rx_add(peer->ovpn->dev, pkt_len);
	}
}

void ovpn_decrypt_post(void *data, int ret)
{
	struct ovpn_crypto_key_slot *ks;
	unsigned int payload_offset = 0;
	struct sk_buff *skb = data;
	struct ovpn_socket *sock;
	struct ovpn_peer *peer;
	__be16 proto;
	__be32 *pid;

	/* crypto is happening asynchronously. this function will be called
	 * again later by the crypto callback with a proper return code
	 */
	if (unlikely(ret == -EINPROGRESS))
		return;

	payload_offset = ovpn_skb_cb(skb)->payload_offset;
	ks = ovpn_skb_cb(skb)->ks;
	peer = ovpn_skb_cb(skb)->peer;

	/* crypto is done, cleanup skb CB and its members */
	kfree(ovpn_skb_cb(skb)->iv);
	kfree(ovpn_skb_cb(skb)->sg);
	aead_request_free(ovpn_skb_cb(skb)->req);

	if (unlikely(ret < 0))
		goto drop;

	/* PID sits after the op */
	pid = (__force __be32 *)(skb->data + OVPN_OPCODE_SIZE);
	ret = ovpn_pktid_recv(&ks->pid_recv, ntohl(*pid), 0);
	if (unlikely(ret < 0)) {
		net_err_ratelimited("%s: PKT ID RX error for peer %u: %d\n",
				    netdev_name(peer->ovpn->dev), peer->id,
				    ret);
		goto drop;
	}

	/* keep track of last received authenticated packet for keepalive */
	WRITE_ONCE(peer->last_recv, ktime_get_real_seconds());

	rcu_read_lock();
	sock = rcu_dereference(peer->sock);
	if (sock && sock->sk->sk_protocol == IPPROTO_UDP)
		/* check if this peer changed local or remote endpoint */
		ovpn_peer_endpoints_update(peer, skb);
	rcu_read_unlock();

	/* point to encapsulated IP packet */
	__skb_pull(skb, payload_offset);

	/* check if this is a valid datapacket that has to be delivered to the
	 * ovpn interface
	 */
	skb_reset_network_header(skb);
	proto = ovpn_ip_check_protocol(skb);
	if (unlikely(!proto)) {
		/* check if null packet */
		if (unlikely(!pskb_may_pull(skb, 1))) {
			net_info_ratelimited("%s: NULL packet received from peer %u\n",
					     netdev_name(peer->ovpn->dev),
					     peer->id);
			goto drop;
		}

		if (ovpn_is_keepalive(skb)) {
			net_dbg_ratelimited("%s: ping received from peer %u\n",
					    netdev_name(peer->ovpn->dev),
					    peer->id);
			/* we drop the packet, but this is not a failure */
			consume_skb(skb);
			goto drop_nocount;
		}

		net_info_ratelimited("%s: unsupported protocol received from peer %u\n",
				     netdev_name(peer->ovpn->dev), peer->id);
		goto drop;
	}
	skb->protocol = proto;

	/* perform Reverse Path Filtering (RPF) */
	if (unlikely(!ovpn_peer_check_by_src(peer->ovpn, skb, peer))) {
		if (skb->protocol == htons(ETH_P_IPV6))
			net_dbg_ratelimited("%s: RPF dropped packet from peer %u, src: %pI6c\n",
					    netdev_name(peer->ovpn->dev),
					    peer->id, &ipv6_hdr(skb)->saddr);
		else
			net_dbg_ratelimited("%s: RPF dropped packet from peer %u, src: %pI4\n",
					    netdev_name(peer->ovpn->dev),
					    peer->id, &ip_hdr(skb)->saddr);
		goto drop;
	}

	ovpn_netdev_write(peer, skb);
	/* skb is passed to upper layer - don't free it */
	skb = NULL;
drop:
	if (unlikely(skb))
		dev_dstats_rx_dropped(peer->ovpn->dev);
	kfree_skb(skb);
drop_nocount:
	if (likely(peer))
		ovpn_peer_put(peer);
	if (likely(ks))
		ovpn_crypto_key_slot_put(ks);
}

/* RX path entry point: decrypt packet and forward it to the device */
void ovpn_recv(struct ovpn_peer *peer, struct sk_buff *skb)
{
	struct ovpn_crypto_key_slot *ks;
	u8 key_id;

	ovpn_peer_stats_increment_rx(&peer->link_stats, skb->len);

	/* get the key slot matching the key ID in the received packet */
	key_id = ovpn_key_id_from_skb(skb);
	ks = ovpn_crypto_key_id_to_slot(&peer->crypto, key_id);
	if (unlikely(!ks)) {
		net_info_ratelimited("%s: no available key for peer %u, key-id: %u\n",
				     netdev_name(peer->ovpn->dev), peer->id,
				     key_id);
		dev_dstats_rx_dropped(peer->ovpn->dev);
		kfree_skb(skb);
		ovpn_peer_put(peer);
		return;
	}

	memset(ovpn_skb_cb(skb), 0, sizeof(struct ovpn_cb));
	ovpn_decrypt_post(skb, ovpn_aead_decrypt(peer, ks, skb));
}

void ovpn_encrypt_post(void *data, int ret)
{
	struct ovpn_crypto_key_slot *ks;
	struct sk_buff *skb = data;
	struct ovpn_socket *sock;
	struct ovpn_peer *peer;
	unsigned int orig_len;

	/* encryption is happening asynchronously. This function will be
	 * called later by the crypto callback with a proper return value
	 */
	if (unlikely(ret == -EINPROGRESS))
		return;

	ks = ovpn_skb_cb(skb)->ks;
	peer = ovpn_skb_cb(skb)->peer;

	/* crypto is done, cleanup skb CB and its members */
	kfree(ovpn_skb_cb(skb)->iv);
	kfree(ovpn_skb_cb(skb)->sg);
	aead_request_free(ovpn_skb_cb(skb)->req);

	if (unlikely(ret == -ERANGE)) {
		/* we ran out of IVs and we must kill the key as it can't be
		 * use anymore
		 */
		netdev_warn(peer->ovpn->dev,
			    "killing key %u for peer %u\n", ks->key_id,
			    peer->id);
		if (ovpn_crypto_kill_key(&peer->crypto, ks->key_id))
			/* let userspace know so that a new key must be negotiated */
			ovpn_nl_key_swap_notify(peer, ks->key_id);

		goto err;
	}

	if (unlikely(ret < 0))
		goto err;

	skb_mark_not_on_list(skb);
	orig_len = skb->len;

	rcu_read_lock();
	sock = rcu_dereference(peer->sock);
	if (unlikely(!sock))
		goto err_unlock;

	switch (sock->sk->sk_protocol) {
	case IPPROTO_UDP:
		ovpn_udp_send_skb(peer, sock->sk, skb);
		break;
	case IPPROTO_TCP:
		ovpn_tcp_send_skb(peer, sock->sk, skb);
		break;
	default:
		/* no transport configured yet */
		goto err_unlock;
	}

	ovpn_peer_stats_increment_tx(&peer->link_stats, orig_len);
	/* keep track of last sent packet for keepalive */
	WRITE_ONCE(peer->last_sent, ktime_get_real_seconds());
	/* skb passed down the stack - don't free it */
	skb = NULL;
err_unlock:
	rcu_read_unlock();
err:
	if (unlikely(skb))
		dev_dstats_tx_dropped(peer->ovpn->dev);
	if (likely(peer))
		ovpn_peer_put(peer);
	if (likely(ks))
		ovpn_crypto_key_slot_put(ks);
	kfree_skb(skb);
}

static bool ovpn_encrypt_one(struct ovpn_peer *peer, struct sk_buff *skb)
{
	struct ovpn_crypto_key_slot *ks;

	/* get primary key to be used for encrypting data */
	ks = ovpn_crypto_key_slot_primary(&peer->crypto);
	if (unlikely(!ks))
		return false;

	/* take a reference to the peer because the crypto code may run async.
	 * ovpn_encrypt_post() will release it upon completion
	 */
	if (unlikely(!ovpn_peer_hold(peer))) {
		DEBUG_NET_WARN_ON_ONCE(1);
		ovpn_crypto_key_slot_put(ks);
		return false;
	}

	memset(ovpn_skb_cb(skb), 0, sizeof(struct ovpn_cb));
	ovpn_encrypt_post(skb, ovpn_aead_encrypt(peer, ks, skb));
	return true;
}

/* send skb to connected peer, if any */
static void ovpn_send(struct ovpn_priv *ovpn, struct sk_buff *skb,
		      struct ovpn_peer *peer)
{
	struct sk_buff *curr, *next;

	/* this might be a GSO-segmented skb list: process each skb
	 * independently
	 */
	skb_list_walk_safe(skb, curr, next) {
		if (unlikely(!ovpn_encrypt_one(peer, curr))) {
			dev_dstats_tx_dropped(ovpn->dev);
			kfree_skb(curr);
		}
	}

	ovpn_peer_put(peer);
}

/* Send user data to the network
 */
netdev_tx_t ovpn_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ovpn_priv *ovpn = netdev_priv(dev);
	struct sk_buff *segments, *curr, *next;
	struct sk_buff_head skb_list;
	struct ovpn_peer *peer;
	__be16 proto;
	int ret;

	/* reset netfilter state */
	nf_reset_ct(skb);

	/* verify IP header size in network packet */
	proto = ovpn_ip_check_protocol(skb);
	if (unlikely(!proto || skb->protocol != proto))
		goto drop;

	if (skb_is_gso(skb)) {
		segments = skb_gso_segment(skb, 0);
		if (IS_ERR(segments)) {
			ret = PTR_ERR(segments);
			net_err_ratelimited("%s: cannot segment payload packet: %d\n",
					    netdev_name(dev), ret);
			goto drop;
		}

		consume_skb(skb);
		skb = segments;
	}

	/* from this moment on, "skb" might be a list */

	__skb_queue_head_init(&skb_list);
	skb_list_walk_safe(skb, curr, next) {
		skb_mark_not_on_list(curr);

		curr = skb_share_check(curr, GFP_ATOMIC);
		if (unlikely(!curr)) {
			net_err_ratelimited("%s: skb_share_check failed for payload packet\n",
					    netdev_name(dev));
			dev_dstats_tx_dropped(ovpn->dev);
			continue;
		}

		__skb_queue_tail(&skb_list, curr);
	}
	skb_list.prev->next = NULL;

	/* retrieve peer serving the destination IP of this packet */
	peer = ovpn_peer_get_by_dst(ovpn, skb);
	if (unlikely(!peer)) {
		switch (skb->protocol) {
		case htons(ETH_P_IP):
			net_dbg_ratelimited("%s: no peer to send data to dst=%pI4\n",
					    netdev_name(ovpn->dev),
					    &ip_hdr(skb)->daddr);
			break;
		case htons(ETH_P_IPV6):
			net_dbg_ratelimited("%s: no peer to send data to dst=%pI6c\n",
					    netdev_name(ovpn->dev),
					    &ipv6_hdr(skb)->daddr);
			break;
		}
		goto drop;
	}
	/* dst was needed for peer selection - it can now be dropped */
	skb_dst_drop(skb);

	ovpn_peer_stats_increment_tx(&peer->vpn_stats, skb->len);
	ovpn_send(ovpn, skb_list.next, peer);

	return NETDEV_TX_OK;

drop:
	dev_dstats_tx_dropped(ovpn->dev);
	skb_tx_error(skb);
	kfree_skb_list(skb);
	return NETDEV_TX_OK;
}

/**
 * ovpn_xmit_special - encrypt and transmit an out-of-band message to peer
 * @peer: peer to send the message to
 * @data: message content
 * @len: message length
 *
 * Assumes that caller holds a reference to peer, which will be
 * passed to ovpn_send()
 */
void ovpn_xmit_special(struct ovpn_peer *peer, const void *data,
		       const unsigned int len)
{
	struct ovpn_priv *ovpn;
	struct sk_buff *skb;

	ovpn = peer->ovpn;
	if (unlikely(!ovpn)) {
		ovpn_peer_put(peer);
		return;
	}

	skb = alloc_skb(256 + len, GFP_ATOMIC);
	if (unlikely(!skb)) {
		ovpn_peer_put(peer);
		return;
	}

	skb_reserve(skb, 128);
	skb->priority = TC_PRIO_BESTEFFORT;
	__skb_put_data(skb, data, len);

	ovpn_send(ovpn, skb, peer);
}
