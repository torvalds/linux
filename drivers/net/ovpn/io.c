// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2019-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/gro_cells.h>
#include <net/gso.h>

#include "ovpnpriv.h"
#include "peer.h"
#include "io.h"
#include "netlink.h"
#include "proto.h"
#include "udp.h"
#include "skb.h"
#include "socket.h"

/* Called after decrypt to write the IP packet to the device.
 * This method is expected to manage/free the skb.
 */
static void ovpn_netdev_write(struct ovpn_peer *peer, struct sk_buff *skb)
{
	unsigned int pkt_len;
	int ret;

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

	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_reset_inner_headers(skb);

	/* cause packet to be "received" by the interface */
	pkt_len = skb->len;
	ret = gro_cells_receive(&peer->ovpn->gro_cells, skb);
	if (likely(ret == NET_RX_SUCCESS))
		/* update RX stats with the size of decrypted packet */
		dev_dstats_rx_add(peer->ovpn->dev, pkt_len);
}

static void ovpn_decrypt_post(struct sk_buff *skb, int ret)
{
	struct ovpn_peer *peer = ovpn_skb_cb(skb)->peer;

	if (unlikely(ret < 0))
		goto drop;

	ovpn_netdev_write(peer, skb);
	/* skb is passed to upper layer - don't free it */
	skb = NULL;
drop:
	if (unlikely(skb))
		dev_dstats_rx_dropped(peer->ovpn->dev);
	ovpn_peer_put(peer);
	kfree_skb(skb);
}

/* RX path entry point: decrypt packet and forward it to the device */
void ovpn_recv(struct ovpn_peer *peer, struct sk_buff *skb)
{
	ovpn_skb_cb(skb)->peer = peer;
	ovpn_decrypt_post(skb, 0);
}

static void ovpn_encrypt_post(struct sk_buff *skb, int ret)
{
	struct ovpn_peer *peer = ovpn_skb_cb(skb)->peer;
	struct ovpn_socket *sock;

	if (unlikely(ret < 0))
		goto err;

	skb_mark_not_on_list(skb);

	rcu_read_lock();
	sock = rcu_dereference(peer->sock);
	if (unlikely(!sock))
		goto err_unlock;

	switch (sock->sock->sk->sk_protocol) {
	case IPPROTO_UDP:
		ovpn_udp_send_skb(peer, sock->sock, skb);
		break;
	default:
		/* no transport configured yet */
		goto err_unlock;
	}
	/* skb passed down the stack - don't free it */
	skb = NULL;
err_unlock:
	rcu_read_unlock();
err:
	if (unlikely(skb))
		dev_dstats_tx_dropped(peer->ovpn->dev);
	ovpn_peer_put(peer);
	kfree_skb(skb);
}

static bool ovpn_encrypt_one(struct ovpn_peer *peer, struct sk_buff *skb)
{
	ovpn_skb_cb(skb)->peer = peer;

	/* take a reference to the peer because the crypto code may run async.
	 * ovpn_encrypt_post() will release it upon completion
	 */
	if (unlikely(!ovpn_peer_hold(peer))) {
		DEBUG_NET_WARN_ON_ONCE(1);
		return false;
	}

	ovpn_encrypt_post(skb, 0);
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
		net_dbg_ratelimited("%s: no peer to send data to\n",
				    netdev_name(ovpn->dev));
		goto drop;
	}

	ovpn_send(ovpn, skb_list.next, peer);

	return NETDEV_TX_OK;

drop:
	dev_dstats_tx_dropped(ovpn->dev);
	skb_tx_error(skb);
	kfree_skb_list(skb);
	return NET_XMIT_DROP;
}
