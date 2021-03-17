// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "queueing.h"
#include "timers.h"
#include "device.h"
#include "peer.h"
#include "socket.h"
#include "messages.h"
#include "cookie.h"

#include <linux/uio.h>
#include <linux/inetdevice.h>
#include <linux/socket.h>
#include <net/ip_tunnels.h>
#include <net/udp.h>
#include <net/sock.h>

static void wg_packet_send_handshake_initiation(struct wg_peer *peer)
{
	struct message_handshake_initiation packet;

	if (!wg_birthdate_has_expired(atomic64_read(&peer->last_sent_handshake),
				      REKEY_TIMEOUT))
		return; /* This function is rate limited. */

	atomic64_set(&peer->last_sent_handshake, ktime_get_coarse_boottime_ns());
	net_dbg_ratelimited("%s: Sending handshake initiation to peer %llu (%pISpfsc)\n",
			    peer->device->dev->name, peer->internal_id,
			    &peer->endpoint.addr);

	if (wg_noise_handshake_create_initiation(&packet, &peer->handshake)) {
		wg_cookie_add_mac_to_packet(&packet, sizeof(packet), peer);
		wg_timers_any_authenticated_packet_traversal(peer);
		wg_timers_any_authenticated_packet_sent(peer);
		atomic64_set(&peer->last_sent_handshake,
			     ktime_get_coarse_boottime_ns());
		wg_socket_send_buffer_to_peer(peer, &packet, sizeof(packet),
					      HANDSHAKE_DSCP);
		wg_timers_handshake_initiated(peer);
	}
}

void wg_packet_handshake_send_worker(struct work_struct *work)
{
	struct wg_peer *peer = container_of(work, struct wg_peer,
					    transmit_handshake_work);

	wg_packet_send_handshake_initiation(peer);
	wg_peer_put(peer);
}

void wg_packet_send_queued_handshake_initiation(struct wg_peer *peer,
						bool is_retry)
{
	if (!is_retry)
		peer->timer_handshake_attempts = 0;

	rcu_read_lock_bh();
	/* We check last_sent_handshake here in addition to the actual function
	 * we're queueing up, so that we don't queue things if not strictly
	 * necessary:
	 */
	if (!wg_birthdate_has_expired(atomic64_read(&peer->last_sent_handshake),
				      REKEY_TIMEOUT) ||
			unlikely(READ_ONCE(peer->is_dead)))
		goto out;

	wg_peer_get(peer);
	/* Queues up calling packet_send_queued_handshakes(peer), where we do a
	 * peer_put(peer) after:
	 */
	if (!queue_work(peer->device->handshake_send_wq,
			&peer->transmit_handshake_work))
		/* If the work was already queued, we want to drop the
		 * extra reference:
		 */
		wg_peer_put(peer);
out:
	rcu_read_unlock_bh();
}

void wg_packet_send_handshake_response(struct wg_peer *peer)
{
	struct message_handshake_response packet;

	atomic64_set(&peer->last_sent_handshake, ktime_get_coarse_boottime_ns());
	net_dbg_ratelimited("%s: Sending handshake response to peer %llu (%pISpfsc)\n",
			    peer->device->dev->name, peer->internal_id,
			    &peer->endpoint.addr);

	if (wg_noise_handshake_create_response(&packet, &peer->handshake)) {
		wg_cookie_add_mac_to_packet(&packet, sizeof(packet), peer);
		if (wg_noise_handshake_begin_session(&peer->handshake,
						     &peer->keypairs)) {
			wg_timers_session_derived(peer);
			wg_timers_any_authenticated_packet_traversal(peer);
			wg_timers_any_authenticated_packet_sent(peer);
			atomic64_set(&peer->last_sent_handshake,
				     ktime_get_coarse_boottime_ns());
			wg_socket_send_buffer_to_peer(peer, &packet,
						      sizeof(packet),
						      HANDSHAKE_DSCP);
		}
	}
}

void wg_packet_send_handshake_cookie(struct wg_device *wg,
				     struct sk_buff *initiating_skb,
				     __le32 sender_index)
{
	struct message_handshake_cookie packet;

	net_dbg_skb_ratelimited("%s: Sending cookie response for denied handshake message for %pISpfsc\n",
				wg->dev->name, initiating_skb);
	wg_cookie_message_create(&packet, initiating_skb, sender_index,
				 &wg->cookie_checker);
	wg_socket_send_buffer_as_reply_to_skb(wg, initiating_skb, &packet,
					      sizeof(packet));
}

static void keep_key_fresh(struct wg_peer *peer)
{
	struct noise_keypair *keypair;
	bool send;

	rcu_read_lock_bh();
	keypair = rcu_dereference_bh(peer->keypairs.current_keypair);
	send = keypair && READ_ONCE(keypair->sending.is_valid) &&
	       (atomic64_read(&keypair->sending_counter) > REKEY_AFTER_MESSAGES ||
		(keypair->i_am_the_initiator &&
		 wg_birthdate_has_expired(keypair->sending.birthdate, REKEY_AFTER_TIME)));
	rcu_read_unlock_bh();

	if (unlikely(send))
		wg_packet_send_queued_handshake_initiation(peer, false);
}

static unsigned int calculate_skb_padding(struct sk_buff *skb)
{
	unsigned int padded_size, last_unit = skb->len;

	if (unlikely(!PACKET_CB(skb)->mtu))
		return ALIGN(last_unit, MESSAGE_PADDING_MULTIPLE) - last_unit;

	/* We do this modulo business with the MTU, just in case the networking
	 * layer gives us a packet that's bigger than the MTU. In that case, we
	 * wouldn't want the final subtraction to overflow in the case of the
	 * padded_size being clamped. Fortunately, that's very rarely the case,
	 * so we optimize for that not happening.
	 */
	if (unlikely(last_unit > PACKET_CB(skb)->mtu))
		last_unit %= PACKET_CB(skb)->mtu;

	padded_size = min(PACKET_CB(skb)->mtu,
			  ALIGN(last_unit, MESSAGE_PADDING_MULTIPLE));
	return padded_size - last_unit;
}

static bool encrypt_packet(struct sk_buff *skb, struct noise_keypair *keypair)
{
	unsigned int padding_len, plaintext_len, trailer_len;
	struct scatterlist sg[MAX_SKB_FRAGS + 8];
	struct message_data *header;
	struct sk_buff *trailer;
	int num_frags;

	/* Force hash calculation before encryption so that flow analysis is
	 * consistent over the inner packet.
	 */
	skb_get_hash(skb);

	/* Calculate lengths. */
	padding_len = calculate_skb_padding(skb);
	trailer_len = padding_len + noise_encrypted_len(0);
	plaintext_len = skb->len + padding_len;

	/* Expand data section to have room for padding and auth tag. */
	num_frags = skb_cow_data(skb, trailer_len, &trailer);
	if (unlikely(num_frags < 0 || num_frags > ARRAY_SIZE(sg)))
		return false;

	/* Set the padding to zeros, and make sure it and the auth tag are part
	 * of the skb.
	 */
	memset(skb_tail_pointer(trailer), 0, padding_len);

	/* Expand head section to have room for our header and the network
	 * stack's headers.
	 */
	if (unlikely(skb_cow_head(skb, DATA_PACKET_HEAD_ROOM) < 0))
		return false;

	/* Finalize checksum calculation for the inner packet, if required. */
	if (unlikely(skb->ip_summed == CHECKSUM_PARTIAL &&
		     skb_checksum_help(skb)))
		return false;

	/* Only after checksumming can we safely add on the padding at the end
	 * and the header.
	 */
	skb_set_inner_network_header(skb, 0);
	header = (struct message_data *)skb_push(skb, sizeof(*header));
	header->header.type = cpu_to_le32(MESSAGE_DATA);
	header->key_idx = keypair->remote_index;
	header->counter = cpu_to_le64(PACKET_CB(skb)->nonce);
	pskb_put(skb, trailer, trailer_len);

	/* Now we can encrypt the scattergather segments */
	sg_init_table(sg, num_frags);
	if (skb_to_sgvec(skb, sg, sizeof(struct message_data),
			 noise_encrypted_len(plaintext_len)) <= 0)
		return false;
	return chacha20poly1305_encrypt_sg_inplace(sg, plaintext_len, NULL, 0,
						   PACKET_CB(skb)->nonce,
						   keypair->sending.key);
}

void wg_packet_send_keepalive(struct wg_peer *peer)
{
	struct sk_buff *skb;

	if (skb_queue_empty(&peer->staged_packet_queue)) {
		skb = alloc_skb(DATA_PACKET_HEAD_ROOM + MESSAGE_MINIMUM_LENGTH,
				GFP_ATOMIC);
		if (unlikely(!skb))
			return;
		skb_reserve(skb, DATA_PACKET_HEAD_ROOM);
		skb->dev = peer->device->dev;
		PACKET_CB(skb)->mtu = skb->dev->mtu;
		skb_queue_tail(&peer->staged_packet_queue, skb);
		net_dbg_ratelimited("%s: Sending keepalive packet to peer %llu (%pISpfsc)\n",
				    peer->device->dev->name, peer->internal_id,
				    &peer->endpoint.addr);
	}

	wg_packet_send_staged_packets(peer);
}

static void wg_packet_create_data_done(struct sk_buff *first,
				       struct wg_peer *peer)
{
	struct sk_buff *skb, *next;
	bool is_keepalive, data_sent = false;

	wg_timers_any_authenticated_packet_traversal(peer);
	wg_timers_any_authenticated_packet_sent(peer);
	skb_list_walk_safe(first, skb, next) {
		is_keepalive = skb->len == message_data_len(0);
		if (likely(!wg_socket_send_skb_to_peer(peer, skb,
				PACKET_CB(skb)->ds) && !is_keepalive))
			data_sent = true;
	}

	if (likely(data_sent))
		wg_timers_data_sent(peer);

	keep_key_fresh(peer);
}

void wg_packet_tx_worker(struct work_struct *work)
{
	struct crypt_queue *queue = container_of(work, struct crypt_queue,
						 work);
	struct noise_keypair *keypair;
	enum packet_state state;
	struct sk_buff *first;
	struct wg_peer *peer;

	while ((first = __ptr_ring_peek(&queue->ring)) != NULL &&
	       (state = atomic_read_acquire(&PACKET_CB(first)->state)) !=
		       PACKET_STATE_UNCRYPTED) {
		__ptr_ring_discard_one(&queue->ring);
		peer = PACKET_PEER(first);
		keypair = PACKET_CB(first)->keypair;

		if (likely(state == PACKET_STATE_CRYPTED))
			wg_packet_create_data_done(first, peer);
		else
			kfree_skb_list(first);

		wg_noise_keypair_put(keypair, false);
		wg_peer_put(peer);
		if (need_resched())
			cond_resched();
	}
}

void wg_packet_encrypt_worker(struct work_struct *work)
{
	struct crypt_queue *queue = container_of(work, struct multicore_worker,
						 work)->ptr;
	struct sk_buff *first, *skb, *next;

	while ((first = ptr_ring_consume_bh(&queue->ring)) != NULL) {
		enum packet_state state = PACKET_STATE_CRYPTED;

		skb_list_walk_safe(first, skb, next) {
			if (likely(encrypt_packet(skb,
					PACKET_CB(first)->keypair))) {
				wg_reset_packet(skb, true);
			} else {
				state = PACKET_STATE_DEAD;
				break;
			}
		}
		wg_queue_enqueue_per_peer(&PACKET_PEER(first)->tx_queue, first,
					  state);
		if (need_resched())
			cond_resched();
	}
}

static void wg_packet_create_data(struct sk_buff *first)
{
	struct wg_peer *peer = PACKET_PEER(first);
	struct wg_device *wg = peer->device;
	int ret = -EINVAL;

	rcu_read_lock_bh();
	if (unlikely(READ_ONCE(peer->is_dead)))
		goto err;

	ret = wg_queue_enqueue_per_device_and_peer(&wg->encrypt_queue,
						   &peer->tx_queue, first,
						   wg->packet_crypt_wq,
						   &wg->encrypt_queue.last_cpu);
	if (unlikely(ret == -EPIPE))
		wg_queue_enqueue_per_peer(&peer->tx_queue, first,
					  PACKET_STATE_DEAD);
err:
	rcu_read_unlock_bh();
	if (likely(!ret || ret == -EPIPE))
		return;
	wg_noise_keypair_put(PACKET_CB(first)->keypair, false);
	wg_peer_put(peer);
	kfree_skb_list(first);
}

void wg_packet_purge_staged_packets(struct wg_peer *peer)
{
	spin_lock_bh(&peer->staged_packet_queue.lock);
	peer->device->dev->stats.tx_dropped += peer->staged_packet_queue.qlen;
	__skb_queue_purge(&peer->staged_packet_queue);
	spin_unlock_bh(&peer->staged_packet_queue.lock);
}

void wg_packet_send_staged_packets(struct wg_peer *peer)
{
	struct noise_keypair *keypair;
	struct sk_buff_head packets;
	struct sk_buff *skb;

	/* Steal the current queue into our local one. */
	__skb_queue_head_init(&packets);
	spin_lock_bh(&peer->staged_packet_queue.lock);
	skb_queue_splice_init(&peer->staged_packet_queue, &packets);
	spin_unlock_bh(&peer->staged_packet_queue.lock);
	if (unlikely(skb_queue_empty(&packets)))
		return;

	/* First we make sure we have a valid reference to a valid key. */
	rcu_read_lock_bh();
	keypair = wg_noise_keypair_get(
		rcu_dereference_bh(peer->keypairs.current_keypair));
	rcu_read_unlock_bh();
	if (unlikely(!keypair))
		goto out_nokey;
	if (unlikely(!READ_ONCE(keypair->sending.is_valid)))
		goto out_nokey;
	if (unlikely(wg_birthdate_has_expired(keypair->sending.birthdate,
					      REJECT_AFTER_TIME)))
		goto out_invalid;

	/* After we know we have a somewhat valid key, we now try to assign
	 * nonces to all of the packets in the queue. If we can't assign nonces
	 * for all of them, we just consider it a failure and wait for the next
	 * handshake.
	 */
	skb_queue_walk(&packets, skb) {
		/* 0 for no outer TOS: no leak. TODO: at some later point, we
		 * might consider using flowi->tos as outer instead.
		 */
		PACKET_CB(skb)->ds = ip_tunnel_ecn_encap(0, ip_hdr(skb), skb);
		PACKET_CB(skb)->nonce =
				atomic64_inc_return(&keypair->sending_counter) - 1;
		if (unlikely(PACKET_CB(skb)->nonce >= REJECT_AFTER_MESSAGES))
			goto out_invalid;
	}

	packets.prev->next = NULL;
	wg_peer_get(keypair->entry.peer);
	PACKET_CB(packets.next)->keypair = keypair;
	wg_packet_create_data(packets.next);
	return;

out_invalid:
	WRITE_ONCE(keypair->sending.is_valid, false);
out_nokey:
	wg_noise_keypair_put(keypair, false);

	/* We orphan the packets if we're waiting on a handshake, so that they
	 * don't block a socket's pool.
	 */
	skb_queue_walk(&packets, skb)
		skb_orphan(skb);
	/* Then we put them back on the top of the queue. We're not too
	 * concerned about accidentally getting things a little out of order if
	 * packets are being added really fast, because this queue is for before
	 * packets can even be sent and it's small anyway.
	 */
	spin_lock_bh(&peer->staged_packet_queue.lock);
	skb_queue_splice(&packets, &peer->staged_packet_queue);
	spin_unlock_bh(&peer->staged_packet_queue.lock);

	/* If we're exiting because there's something wrong with the key, it
	 * means we should initiate a new handshake.
	 */
	wg_packet_send_queued_handshake_initiation(peer, false);
}
