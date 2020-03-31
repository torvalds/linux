/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _WG_QUEUEING_H
#define _WG_QUEUEING_H

#include "peer.h"
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

struct wg_device;
struct wg_peer;
struct multicore_worker;
struct crypt_queue;
struct sk_buff;

/* queueing.c APIs: */
int wg_packet_queue_init(struct crypt_queue *queue, work_func_t function,
			 bool multicore, unsigned int len);
void wg_packet_queue_free(struct crypt_queue *queue, bool multicore);
struct multicore_worker __percpu *
wg_packet_percpu_multicore_worker_alloc(work_func_t function, void *ptr);

/* receive.c APIs: */
void wg_packet_receive(struct wg_device *wg, struct sk_buff *skb);
void wg_packet_handshake_receive_worker(struct work_struct *work);
/* NAPI poll function: */
int wg_packet_rx_poll(struct napi_struct *napi, int budget);
/* Workqueue worker: */
void wg_packet_decrypt_worker(struct work_struct *work);

/* send.c APIs: */
void wg_packet_send_queued_handshake_initiation(struct wg_peer *peer,
						bool is_retry);
void wg_packet_send_handshake_response(struct wg_peer *peer);
void wg_packet_send_handshake_cookie(struct wg_device *wg,
				     struct sk_buff *initiating_skb,
				     __le32 sender_index);
void wg_packet_send_keepalive(struct wg_peer *peer);
void wg_packet_purge_staged_packets(struct wg_peer *peer);
void wg_packet_send_staged_packets(struct wg_peer *peer);
/* Workqueue workers: */
void wg_packet_handshake_send_worker(struct work_struct *work);
void wg_packet_tx_worker(struct work_struct *work);
void wg_packet_encrypt_worker(struct work_struct *work);

enum packet_state {
	PACKET_STATE_UNCRYPTED,
	PACKET_STATE_CRYPTED,
	PACKET_STATE_DEAD
};

struct packet_cb {
	u64 nonce;
	struct noise_keypair *keypair;
	atomic_t state;
	u32 mtu;
	u8 ds;
};

#define PACKET_CB(skb) ((struct packet_cb *)((skb)->cb))
#define PACKET_PEER(skb) (PACKET_CB(skb)->keypair->entry.peer)

/* Returns either the correct skb->protocol value, or 0 if invalid. */
static inline __be16 wg_examine_packet_protocol(struct sk_buff *skb)
{
	if (skb_network_header(skb) >= skb->head &&
	    (skb_network_header(skb) + sizeof(struct iphdr)) <=
		    skb_tail_pointer(skb) &&
	    ip_hdr(skb)->version == 4)
		return htons(ETH_P_IP);
	if (skb_network_header(skb) >= skb->head &&
	    (skb_network_header(skb) + sizeof(struct ipv6hdr)) <=
		    skb_tail_pointer(skb) &&
	    ipv6_hdr(skb)->version == 6)
		return htons(ETH_P_IPV6);
	return 0;
}

static inline bool wg_check_packet_protocol(struct sk_buff *skb)
{
	__be16 real_protocol = wg_examine_packet_protocol(skb);
	return real_protocol && skb->protocol == real_protocol;
}

static inline void wg_reset_packet(struct sk_buff *skb)
{
	skb_scrub_packet(skb, true);
	memset(&skb->headers_start, 0,
	       offsetof(struct sk_buff, headers_end) -
		       offsetof(struct sk_buff, headers_start));
	skb->queue_mapping = 0;
	skb->nohdr = 0;
	skb->peeked = 0;
	skb->mac_len = 0;
	skb->dev = NULL;
#ifdef CONFIG_NET_SCHED
	skb->tc_index = 0;
#endif
	skb_reset_redirect(skb);
	skb->hdr_len = skb_headroom(skb);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_probe_transport_header(skb);
	skb_reset_inner_headers(skb);
}

static inline int wg_cpumask_choose_online(int *stored_cpu, unsigned int id)
{
	unsigned int cpu = *stored_cpu, cpu_index, i;

	if (unlikely(cpu == nr_cpumask_bits ||
		     !cpumask_test_cpu(cpu, cpu_online_mask))) {
		cpu_index = id % cpumask_weight(cpu_online_mask);
		cpu = cpumask_first(cpu_online_mask);
		for (i = 0; i < cpu_index; ++i)
			cpu = cpumask_next(cpu, cpu_online_mask);
		*stored_cpu = cpu;
	}
	return cpu;
}

/* This function is racy, in the sense that next is unlocked, so it could return
 * the same CPU twice. A race-free version of this would be to instead store an
 * atomic sequence number, do an increment-and-return, and then iterate through
 * every possible CPU until we get to that index -- choose_cpu. However that's
 * a bit slower, and it doesn't seem like this potential race actually
 * introduces any performance loss, so we live with it.
 */
static inline int wg_cpumask_next_online(int *next)
{
	int cpu = *next;

	while (unlikely(!cpumask_test_cpu(cpu, cpu_online_mask)))
		cpu = cpumask_next(cpu, cpu_online_mask) % nr_cpumask_bits;
	*next = cpumask_next(cpu, cpu_online_mask) % nr_cpumask_bits;
	return cpu;
}

static inline int wg_queue_enqueue_per_device_and_peer(
	struct crypt_queue *device_queue, struct crypt_queue *peer_queue,
	struct sk_buff *skb, struct workqueue_struct *wq, int *next_cpu)
{
	int cpu;

	atomic_set_release(&PACKET_CB(skb)->state, PACKET_STATE_UNCRYPTED);
	/* We first queue this up for the peer ingestion, but the consumer
	 * will wait for the state to change to CRYPTED or DEAD before.
	 */
	if (unlikely(ptr_ring_produce_bh(&peer_queue->ring, skb)))
		return -ENOSPC;
	/* Then we queue it up in the device queue, which consumes the
	 * packet as soon as it can.
	 */
	cpu = wg_cpumask_next_online(next_cpu);
	if (unlikely(ptr_ring_produce_bh(&device_queue->ring, skb)))
		return -EPIPE;
	queue_work_on(cpu, wq, &per_cpu_ptr(device_queue->worker, cpu)->work);
	return 0;
}

static inline void wg_queue_enqueue_per_peer(struct crypt_queue *queue,
					     struct sk_buff *skb,
					     enum packet_state state)
{
	/* We take a reference, because as soon as we call atomic_set, the
	 * peer can be freed from below us.
	 */
	struct wg_peer *peer = wg_peer_get(PACKET_PEER(skb));

	atomic_set_release(&PACKET_CB(skb)->state, state);
	queue_work_on(wg_cpumask_choose_online(&peer->serial_work_cpu,
					       peer->internal_id),
		      peer->device->packet_crypt_wq, &queue->work);
	wg_peer_put(peer);
}

static inline void wg_queue_enqueue_per_peer_napi(struct sk_buff *skb,
						  enum packet_state state)
{
	/* We take a reference, because as soon as we call atomic_set, the
	 * peer can be freed from below us.
	 */
	struct wg_peer *peer = wg_peer_get(PACKET_PEER(skb));

	atomic_set_release(&PACKET_CB(skb)->state, state);
	napi_schedule(&peer->napi);
	wg_peer_put(peer);
}

#ifdef DEBUG
bool wg_packet_counter_selftest(void);
#endif

#endif /* _WG_QUEUEING_H */
