// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "queueing.h"
#include "device.h"
#include "peer.h"
#include "timers.h"
#include "messages.h"
#include "cookie.h"
#include "socket.h"

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <net/ip_tunnels.h>

/* Must be called with bh disabled. */
static void update_rx_stats(struct wg_peer *peer, size_t len)
{
	struct pcpu_sw_netstats *tstats =
		get_cpu_ptr(peer->device->dev->tstats);

	u64_stats_update_begin(&tstats->syncp);
	++tstats->rx_packets;
	tstats->rx_bytes += len;
	peer->rx_bytes += len;
	u64_stats_update_end(&tstats->syncp);
	put_cpu_ptr(tstats);
}

#define SKB_TYPE_LE32(skb) (((struct message_header *)(skb)->data)->type)

static size_t validate_header_len(struct sk_buff *skb)
{
	if (unlikely(skb->len < sizeof(struct message_header)))
		return 0;
	if (SKB_TYPE_LE32(skb) == cpu_to_le32(MESSAGE_DATA) &&
	    skb->len >= MESSAGE_MINIMUM_LENGTH)
		return sizeof(struct message_data);
	if (SKB_TYPE_LE32(skb) == cpu_to_le32(MESSAGE_HANDSHAKE_INITIATION) &&
	    skb->len == sizeof(struct message_handshake_initiation))
		return sizeof(struct message_handshake_initiation);
	if (SKB_TYPE_LE32(skb) == cpu_to_le32(MESSAGE_HANDSHAKE_RESPONSE) &&
	    skb->len == sizeof(struct message_handshake_response))
		return sizeof(struct message_handshake_response);
	if (SKB_TYPE_LE32(skb) == cpu_to_le32(MESSAGE_HANDSHAKE_COOKIE) &&
	    skb->len == sizeof(struct message_handshake_cookie))
		return sizeof(struct message_handshake_cookie);
	return 0;
}

static int prepare_skb_header(struct sk_buff *skb, struct wg_device *wg)
{
	size_t data_offset, data_len, header_len;
	struct udphdr *udp;

	if (unlikely(!wg_check_packet_protocol(skb) ||
		     skb_transport_header(skb) < skb->head ||
		     (skb_transport_header(skb) + sizeof(struct udphdr)) >
			     skb_tail_pointer(skb)))
		return -EINVAL; /* Bogus IP header */
	udp = udp_hdr(skb);
	data_offset = (u8 *)udp - skb->data;
	if (unlikely(data_offset > U16_MAX ||
		     data_offset + sizeof(struct udphdr) > skb->len))
		/* Packet has offset at impossible location or isn't big enough
		 * to have UDP fields.
		 */
		return -EINVAL;
	data_len = ntohs(udp->len);
	if (unlikely(data_len < sizeof(struct udphdr) ||
		     data_len > skb->len - data_offset))
		/* UDP packet is reporting too small of a size or lying about
		 * its size.
		 */
		return -EINVAL;
	data_len -= sizeof(struct udphdr);
	data_offset = (u8 *)udp + sizeof(struct udphdr) - skb->data;
	if (unlikely(!pskb_may_pull(skb,
				data_offset + sizeof(struct message_header)) ||
		     pskb_trim(skb, data_len + data_offset) < 0))
		return -EINVAL;
	skb_pull(skb, data_offset);
	if (unlikely(skb->len != data_len))
		/* Final len does not agree with calculated len */
		return -EINVAL;
	header_len = validate_header_len(skb);
	if (unlikely(!header_len))
		return -EINVAL;
	__skb_push(skb, data_offset);
	if (unlikely(!pskb_may_pull(skb, data_offset + header_len)))
		return -EINVAL;
	__skb_pull(skb, data_offset);
	return 0;
}

static void wg_receive_handshake_packet(struct wg_device *wg,
					struct sk_buff *skb)
{
	enum cookie_mac_state mac_state;
	struct wg_peer *peer = NULL;
	/* This is global, so that our load calculation applies to the whole
	 * system. We don't care about races with it at all.
	 */
	static u64 last_under_load;
	bool packet_needs_cookie;
	bool under_load;

	if (SKB_TYPE_LE32(skb) == cpu_to_le32(MESSAGE_HANDSHAKE_COOKIE)) {
		net_dbg_skb_ratelimited("%s: Receiving cookie response from %pISpfsc\n",
					wg->dev->name, skb);
		wg_cookie_message_consume(
			(struct message_handshake_cookie *)skb->data, wg);
		return;
	}

	under_load = skb_queue_len(&wg->incoming_handshakes) >=
		     MAX_QUEUED_INCOMING_HANDSHAKES / 8;
	if (under_load) {
		last_under_load = ktime_get_coarse_boottime_ns();
	} else if (last_under_load) {
		under_load = !wg_birthdate_has_expired(last_under_load, 1);
		if (!under_load)
			last_under_load = 0;
	}
	mac_state = wg_cookie_validate_packet(&wg->cookie_checker, skb,
					      under_load);
	if ((under_load && mac_state == VALID_MAC_WITH_COOKIE) ||
	    (!under_load && mac_state == VALID_MAC_BUT_NO_COOKIE)) {
		packet_needs_cookie = false;
	} else if (under_load && mac_state == VALID_MAC_BUT_NO_COOKIE) {
		packet_needs_cookie = true;
	} else {
		net_dbg_skb_ratelimited("%s: Invalid MAC of handshake, dropping packet from %pISpfsc\n",
					wg->dev->name, skb);
		return;
	}

	switch (SKB_TYPE_LE32(skb)) {
	case cpu_to_le32(MESSAGE_HANDSHAKE_INITIATION): {
		struct message_handshake_initiation *message =
			(struct message_handshake_initiation *)skb->data;

		if (packet_needs_cookie) {
			wg_packet_send_handshake_cookie(wg, skb,
							message->sender_index);
			return;
		}
		peer = wg_noise_handshake_consume_initiation(message, wg);
		if (unlikely(!peer)) {
			net_dbg_skb_ratelimited("%s: Invalid handshake initiation from %pISpfsc\n",
						wg->dev->name, skb);
			return;
		}
		wg_socket_set_peer_endpoint_from_skb(peer, skb);
		net_dbg_ratelimited("%s: Receiving handshake initiation from peer %llu (%pISpfsc)\n",
				    wg->dev->name, peer->internal_id,
				    &peer->endpoint.addr);
		wg_packet_send_handshake_response(peer);
		break;
	}
	case cpu_to_le32(MESSAGE_HANDSHAKE_RESPONSE): {
		struct message_handshake_response *message =
			(struct message_handshake_response *)skb->data;

		if (packet_needs_cookie) {
			wg_packet_send_handshake_cookie(wg, skb,
							message->sender_index);
			return;
		}
		peer = wg_noise_handshake_consume_response(message, wg);
		if (unlikely(!peer)) {
			net_dbg_skb_ratelimited("%s: Invalid handshake response from %pISpfsc\n",
						wg->dev->name, skb);
			return;
		}
		wg_socket_set_peer_endpoint_from_skb(peer, skb);
		net_dbg_ratelimited("%s: Receiving handshake response from peer %llu (%pISpfsc)\n",
				    wg->dev->name, peer->internal_id,
				    &peer->endpoint.addr);
		if (wg_noise_handshake_begin_session(&peer->handshake,
						     &peer->keypairs)) {
			wg_timers_session_derived(peer);
			wg_timers_handshake_complete(peer);
			/* Calling this function will either send any existing
			 * packets in the queue and not send a keepalive, which
			 * is the best case, Or, if there's nothing in the
			 * queue, it will send a keepalive, in order to give
			 * immediate confirmation of the session.
			 */
			wg_packet_send_keepalive(peer);
		}
		break;
	}
	}

	if (unlikely(!peer)) {
		WARN(1, "Somehow a wrong type of packet wound up in the handshake queue!\n");
		return;
	}

	local_bh_disable();
	update_rx_stats(peer, skb->len);
	local_bh_enable();

	wg_timers_any_authenticated_packet_received(peer);
	wg_timers_any_authenticated_packet_traversal(peer);
	wg_peer_put(peer);
}

void wg_packet_handshake_receive_worker(struct work_struct *work)
{
	struct wg_device *wg = container_of(work, struct multicore_worker,
					    work)->ptr;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&wg->incoming_handshakes)) != NULL) {
		wg_receive_handshake_packet(wg, skb);
		dev_kfree_skb(skb);
		cond_resched();
	}
}

static void keep_key_fresh(struct wg_peer *peer)
{
	struct noise_keypair *keypair;
	bool send;

	if (peer->sent_lastminute_handshake)
		return;

	rcu_read_lock_bh();
	keypair = rcu_dereference_bh(peer->keypairs.current_keypair);
	send = keypair && READ_ONCE(keypair->sending.is_valid) &&
	       keypair->i_am_the_initiator &&
	       wg_birthdate_has_expired(keypair->sending.birthdate,
			REJECT_AFTER_TIME - KEEPALIVE_TIMEOUT - REKEY_TIMEOUT);
	rcu_read_unlock_bh();

	if (unlikely(send)) {
		peer->sent_lastminute_handshake = true;
		wg_packet_send_queued_handshake_initiation(peer, false);
	}
}

static bool decrypt_packet(struct sk_buff *skb, struct noise_keypair *keypair)
{
	struct scatterlist sg[MAX_SKB_FRAGS + 8];
	struct sk_buff *trailer;
	unsigned int offset;
	int num_frags;

	if (unlikely(!keypair))
		return false;

	if (unlikely(!READ_ONCE(keypair->receiving.is_valid) ||
		  wg_birthdate_has_expired(keypair->receiving.birthdate, REJECT_AFTER_TIME) ||
		  keypair->receiving_counter.counter >= REJECT_AFTER_MESSAGES)) {
		WRITE_ONCE(keypair->receiving.is_valid, false);
		return false;
	}

	PACKET_CB(skb)->nonce =
		le64_to_cpu(((struct message_data *)skb->data)->counter);

	/* We ensure that the network header is part of the packet before we
	 * call skb_cow_data, so that there's no chance that data is removed
	 * from the skb, so that later we can extract the original endpoint.
	 */
	offset = skb->data - skb_network_header(skb);
	skb_push(skb, offset);
	num_frags = skb_cow_data(skb, 0, &trailer);
	offset += sizeof(struct message_data);
	skb_pull(skb, offset);
	if (unlikely(num_frags < 0 || num_frags > ARRAY_SIZE(sg)))
		return false;

	sg_init_table(sg, num_frags);
	if (skb_to_sgvec(skb, sg, 0, skb->len) <= 0)
		return false;

	if (!chacha20poly1305_decrypt_sg_inplace(sg, skb->len, NULL, 0,
					         PACKET_CB(skb)->nonce,
						 keypair->receiving.key))
		return false;

	/* Another ugly situation of pushing and pulling the header so as to
	 * keep endpoint information intact.
	 */
	skb_push(skb, offset);
	if (pskb_trim(skb, skb->len - noise_encrypted_len(0)))
		return false;
	skb_pull(skb, offset);

	return true;
}

/* This is RFC6479, a replay detection bitmap algorithm that avoids bitshifts */
static bool counter_validate(struct noise_replay_counter *counter, u64 their_counter)
{
	unsigned long index, index_current, top, i;
	bool ret = false;

	spin_lock_bh(&counter->lock);

	if (unlikely(counter->counter >= REJECT_AFTER_MESSAGES + 1 ||
		     their_counter >= REJECT_AFTER_MESSAGES))
		goto out;

	++their_counter;

	if (unlikely((COUNTER_WINDOW_SIZE + their_counter) <
		     counter->counter))
		goto out;

	index = their_counter >> ilog2(BITS_PER_LONG);

	if (likely(their_counter > counter->counter)) {
		index_current = counter->counter >> ilog2(BITS_PER_LONG);
		top = min_t(unsigned long, index - index_current,
			    COUNTER_BITS_TOTAL / BITS_PER_LONG);
		for (i = 1; i <= top; ++i)
			counter->backtrack[(i + index_current) &
				((COUNTER_BITS_TOTAL / BITS_PER_LONG) - 1)] = 0;
		counter->counter = their_counter;
	}

	index &= (COUNTER_BITS_TOTAL / BITS_PER_LONG) - 1;
	ret = !test_and_set_bit(their_counter & (BITS_PER_LONG - 1),
				&counter->backtrack[index]);

out:
	spin_unlock_bh(&counter->lock);
	return ret;
}

#include "selftest/counter.c"

static void wg_packet_consume_data_done(struct wg_peer *peer,
					struct sk_buff *skb,
					struct endpoint *endpoint)
{
	struct net_device *dev = peer->device->dev;
	unsigned int len, len_before_trim;
	struct wg_peer *routed_peer;

	wg_socket_set_peer_endpoint(peer, endpoint);

	if (unlikely(wg_noise_received_with_keypair(&peer->keypairs,
						    PACKET_CB(skb)->keypair))) {
		wg_timers_handshake_complete(peer);
		wg_packet_send_staged_packets(peer);
	}

	keep_key_fresh(peer);

	wg_timers_any_authenticated_packet_received(peer);
	wg_timers_any_authenticated_packet_traversal(peer);

	/* A packet with length 0 is a keepalive packet */
	if (unlikely(!skb->len)) {
		update_rx_stats(peer, message_data_len(0));
		net_dbg_ratelimited("%s: Receiving keepalive packet from peer %llu (%pISpfsc)\n",
				    dev->name, peer->internal_id,
				    &peer->endpoint.addr);
		goto packet_processed;
	}

	wg_timers_data_received(peer);

	if (unlikely(skb_network_header(skb) < skb->head))
		goto dishonest_packet_size;
	if (unlikely(!(pskb_network_may_pull(skb, sizeof(struct iphdr)) &&
		       (ip_hdr(skb)->version == 4 ||
			(ip_hdr(skb)->version == 6 &&
			 pskb_network_may_pull(skb, sizeof(struct ipv6hdr)))))))
		goto dishonest_packet_type;

	skb->dev = dev;
	/* We've already verified the Poly1305 auth tag, which means this packet
	 * was not modified in transit. We can therefore tell the networking
	 * stack that all checksums of every layer of encapsulation have already
	 * been checked "by the hardware" and therefore is unnecessary to check
	 * again in software.
	 */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->csum_level = ~0; /* All levels */
	skb->protocol = wg_examine_packet_protocol(skb);
	if (skb->protocol == htons(ETH_P_IP)) {
		len = ntohs(ip_hdr(skb)->tot_len);
		if (unlikely(len < sizeof(struct iphdr)))
			goto dishonest_packet_size;
		INET_ECN_decapsulate(skb, PACKET_CB(skb)->ds, ip_hdr(skb)->tos);
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		len = ntohs(ipv6_hdr(skb)->payload_len) +
		      sizeof(struct ipv6hdr);
		INET_ECN_decapsulate(skb, PACKET_CB(skb)->ds, ipv6_get_dsfield(ipv6_hdr(skb)));
	} else {
		goto dishonest_packet_type;
	}

	if (unlikely(len > skb->len))
		goto dishonest_packet_size;
	len_before_trim = skb->len;
	if (unlikely(pskb_trim(skb, len)))
		goto packet_processed;

	routed_peer = wg_allowedips_lookup_src(&peer->device->peer_allowedips,
					       skb);
	wg_peer_put(routed_peer); /* We don't need the extra reference. */

	if (unlikely(routed_peer != peer))
		goto dishonest_packet_peer;

	if (unlikely(napi_gro_receive(&peer->napi, skb) == GRO_DROP)) {
		++dev->stats.rx_dropped;
		net_dbg_ratelimited("%s: Failed to give packet to userspace from peer %llu (%pISpfsc)\n",
				    dev->name, peer->internal_id,
				    &peer->endpoint.addr);
	} else {
		update_rx_stats(peer, message_data_len(len_before_trim));
	}
	return;

dishonest_packet_peer:
	net_dbg_skb_ratelimited("%s: Packet has unallowed src IP (%pISc) from peer %llu (%pISpfsc)\n",
				dev->name, skb, peer->internal_id,
				&peer->endpoint.addr);
	++dev->stats.rx_errors;
	++dev->stats.rx_frame_errors;
	goto packet_processed;
dishonest_packet_type:
	net_dbg_ratelimited("%s: Packet is neither ipv4 nor ipv6 from peer %llu (%pISpfsc)\n",
			    dev->name, peer->internal_id, &peer->endpoint.addr);
	++dev->stats.rx_errors;
	++dev->stats.rx_frame_errors;
	goto packet_processed;
dishonest_packet_size:
	net_dbg_ratelimited("%s: Packet has incorrect size from peer %llu (%pISpfsc)\n",
			    dev->name, peer->internal_id, &peer->endpoint.addr);
	++dev->stats.rx_errors;
	++dev->stats.rx_length_errors;
	goto packet_processed;
packet_processed:
	dev_kfree_skb(skb);
}

int wg_packet_rx_poll(struct napi_struct *napi, int budget)
{
	struct wg_peer *peer = container_of(napi, struct wg_peer, napi);
	struct crypt_queue *queue = &peer->rx_queue;
	struct noise_keypair *keypair;
	struct endpoint endpoint;
	enum packet_state state;
	struct sk_buff *skb;
	int work_done = 0;
	bool free;

	if (unlikely(budget <= 0))
		return 0;

	while ((skb = __ptr_ring_peek(&queue->ring)) != NULL &&
	       (state = atomic_read_acquire(&PACKET_CB(skb)->state)) !=
		       PACKET_STATE_UNCRYPTED) {
		__ptr_ring_discard_one(&queue->ring);
		peer = PACKET_PEER(skb);
		keypair = PACKET_CB(skb)->keypair;
		free = true;

		if (unlikely(state != PACKET_STATE_CRYPTED))
			goto next;

		if (unlikely(!counter_validate(&keypair->receiving_counter,
					       PACKET_CB(skb)->nonce))) {
			net_dbg_ratelimited("%s: Packet has invalid nonce %llu (max %llu)\n",
					    peer->device->dev->name,
					    PACKET_CB(skb)->nonce,
					    keypair->receiving_counter.counter);
			goto next;
		}

		if (unlikely(wg_socket_endpoint_from_skb(&endpoint, skb)))
			goto next;

		wg_reset_packet(skb, false);
		wg_packet_consume_data_done(peer, skb, &endpoint);
		free = false;

next:
		wg_noise_keypair_put(keypair, false);
		wg_peer_put(peer);
		if (unlikely(free))
			dev_kfree_skb(skb);

		if (++work_done >= budget)
			break;
	}

	if (work_done < budget)
		napi_complete_done(napi, work_done);

	return work_done;
}

void wg_packet_decrypt_worker(struct work_struct *work)
{
	struct crypt_queue *queue = container_of(work, struct multicore_worker,
						 work)->ptr;
	struct sk_buff *skb;

	while ((skb = ptr_ring_consume_bh(&queue->ring)) != NULL) {
		enum packet_state state =
			likely(decrypt_packet(skb, PACKET_CB(skb)->keypair)) ?
				PACKET_STATE_CRYPTED : PACKET_STATE_DEAD;
		wg_queue_enqueue_per_peer_napi(skb, state);
		if (need_resched())
			cond_resched();
	}
}

static void wg_packet_consume_data(struct wg_device *wg, struct sk_buff *skb)
{
	__le32 idx = ((struct message_data *)skb->data)->key_idx;
	struct wg_peer *peer = NULL;
	int ret;

	rcu_read_lock_bh();
	PACKET_CB(skb)->keypair =
		(struct noise_keypair *)wg_index_hashtable_lookup(
			wg->index_hashtable, INDEX_HASHTABLE_KEYPAIR, idx,
			&peer);
	if (unlikely(!wg_noise_keypair_get(PACKET_CB(skb)->keypair)))
		goto err_keypair;

	if (unlikely(READ_ONCE(peer->is_dead)))
		goto err;

	ret = wg_queue_enqueue_per_device_and_peer(&wg->decrypt_queue,
						   &peer->rx_queue, skb,
						   wg->packet_crypt_wq,
						   &wg->decrypt_queue.last_cpu);
	if (unlikely(ret == -EPIPE))
		wg_queue_enqueue_per_peer_napi(skb, PACKET_STATE_DEAD);
	if (likely(!ret || ret == -EPIPE)) {
		rcu_read_unlock_bh();
		return;
	}
err:
	wg_noise_keypair_put(PACKET_CB(skb)->keypair, false);
err_keypair:
	rcu_read_unlock_bh();
	wg_peer_put(peer);
	dev_kfree_skb(skb);
}

void wg_packet_receive(struct wg_device *wg, struct sk_buff *skb)
{
	if (unlikely(prepare_skb_header(skb, wg) < 0))
		goto err;
	switch (SKB_TYPE_LE32(skb)) {
	case cpu_to_le32(MESSAGE_HANDSHAKE_INITIATION):
	case cpu_to_le32(MESSAGE_HANDSHAKE_RESPONSE):
	case cpu_to_le32(MESSAGE_HANDSHAKE_COOKIE): {
		int cpu;

		if (skb_queue_len(&wg->incoming_handshakes) >
			    MAX_QUEUED_INCOMING_HANDSHAKES ||
		    unlikely(!rng_is_initialized())) {
			net_dbg_skb_ratelimited("%s: Dropping handshake packet from %pISpfsc\n",
						wg->dev->name, skb);
			goto err;
		}
		skb_queue_tail(&wg->incoming_handshakes, skb);
		/* Queues up a call to packet_process_queued_handshake_
		 * packets(skb):
		 */
		cpu = wg_cpumask_next_online(&wg->incoming_handshake_cpu);
		queue_work_on(cpu, wg->handshake_receive_wq,
			&per_cpu_ptr(wg->incoming_handshakes_worker, cpu)->work);
		break;
	}
	case cpu_to_le32(MESSAGE_DATA):
		PACKET_CB(skb)->ds = ip_tunnel_get_dsfield(ip_hdr(skb), skb);
		wg_packet_consume_data(wg, skb);
		break;
	default:
		WARN(1, "Non-exhaustive parsing of packet header lead to unknown packet type!\n");
		goto err;
	}
	return;

err:
	dev_kfree_skb(skb);
}
