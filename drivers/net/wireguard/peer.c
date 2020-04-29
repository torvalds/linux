// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "peer.h"
#include "device.h"
#include "queueing.h"
#include "timers.h"
#include "peerlookup.h"
#include "noise.h"

#include <linux/kref.h>
#include <linux/lockdep.h>
#include <linux/rcupdate.h>
#include <linux/list.h>

static atomic64_t peer_counter = ATOMIC64_INIT(0);

struct wg_peer *wg_peer_create(struct wg_device *wg,
			       const u8 public_key[NOISE_PUBLIC_KEY_LEN],
			       const u8 preshared_key[NOISE_SYMMETRIC_KEY_LEN])
{
	struct wg_peer *peer;
	int ret = -ENOMEM;

	lockdep_assert_held(&wg->device_update_lock);

	if (wg->num_peers >= MAX_PEERS_PER_DEVICE)
		return ERR_PTR(ret);

	peer = kzalloc(sizeof(*peer), GFP_KERNEL);
	if (unlikely(!peer))
		return ERR_PTR(ret);
	peer->device = wg;

	wg_noise_handshake_init(&peer->handshake, &wg->static_identity,
				public_key, preshared_key, peer);
	if (dst_cache_init(&peer->endpoint_cache, GFP_KERNEL))
		goto err_1;
	if (wg_packet_queue_init(&peer->tx_queue, wg_packet_tx_worker, false,
				 MAX_QUEUED_PACKETS))
		goto err_2;
	if (wg_packet_queue_init(&peer->rx_queue, NULL, false,
				 MAX_QUEUED_PACKETS))
		goto err_3;

	peer->internal_id = atomic64_inc_return(&peer_counter);
	peer->serial_work_cpu = nr_cpumask_bits;
	wg_cookie_init(&peer->latest_cookie);
	wg_timers_init(peer);
	wg_cookie_checker_precompute_peer_keys(peer);
	spin_lock_init(&peer->keypairs.keypair_update_lock);
	INIT_WORK(&peer->transmit_handshake_work,
		  wg_packet_handshake_send_worker);
	rwlock_init(&peer->endpoint_lock);
	kref_init(&peer->refcount);
	skb_queue_head_init(&peer->staged_packet_queue);
	wg_noise_reset_last_sent_handshake(&peer->last_sent_handshake);
	set_bit(NAPI_STATE_NO_BUSY_POLL, &peer->napi.state);
	netif_napi_add(wg->dev, &peer->napi, wg_packet_rx_poll,
		       NAPI_POLL_WEIGHT);
	napi_enable(&peer->napi);
	list_add_tail(&peer->peer_list, &wg->peer_list);
	INIT_LIST_HEAD(&peer->allowedips_list);
	wg_pubkey_hashtable_add(wg->peer_hashtable, peer);
	++wg->num_peers;
	pr_debug("%s: Peer %llu created\n", wg->dev->name, peer->internal_id);
	return peer;

err_3:
	wg_packet_queue_free(&peer->tx_queue, false);
err_2:
	dst_cache_destroy(&peer->endpoint_cache);
err_1:
	kfree(peer);
	return ERR_PTR(ret);
}

struct wg_peer *wg_peer_get_maybe_zero(struct wg_peer *peer)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_bh_held(),
			 "Taking peer reference without holding the RCU read lock");
	if (unlikely(!peer || !kref_get_unless_zero(&peer->refcount)))
		return NULL;
	return peer;
}

static void peer_make_dead(struct wg_peer *peer)
{
	/* Remove from configuration-time lookup structures. */
	list_del_init(&peer->peer_list);
	wg_allowedips_remove_by_peer(&peer->device->peer_allowedips, peer,
				     &peer->device->device_update_lock);
	wg_pubkey_hashtable_remove(peer->device->peer_hashtable, peer);

	/* Mark as dead, so that we don't allow jumping contexts after. */
	WRITE_ONCE(peer->is_dead, true);

	/* The caller must now synchronize_rcu() for this to take effect. */
}

static void peer_remove_after_dead(struct wg_peer *peer)
{
	WARN_ON(!peer->is_dead);

	/* No more keypairs can be created for this peer, since is_dead protects
	 * add_new_keypair, so we can now destroy existing ones.
	 */
	wg_noise_keypairs_clear(&peer->keypairs);

	/* Destroy all ongoing timers that were in-flight at the beginning of
	 * this function.
	 */
	wg_timers_stop(peer);

	/* The transition between packet encryption/decryption queues isn't
	 * guarded by is_dead, but each reference's life is strictly bounded by
	 * two generations: once for parallel crypto and once for serial
	 * ingestion, so we can simply flush twice, and be sure that we no
	 * longer have references inside these queues.
	 */

	/* a) For encrypt/decrypt. */
	flush_workqueue(peer->device->packet_crypt_wq);
	/* b.1) For send (but not receive, since that's napi). */
	flush_workqueue(peer->device->packet_crypt_wq);
	/* b.2.1) For receive (but not send, since that's wq). */
	napi_disable(&peer->napi);
	/* b.2.1) It's now safe to remove the napi struct, which must be done
	 * here from process context.
	 */
	netif_napi_del(&peer->napi);

	/* Ensure any workstructs we own (like transmit_handshake_work or
	 * clear_peer_work) no longer are in use.
	 */
	flush_workqueue(peer->device->handshake_send_wq);

	/* After the above flushes, a peer might still be active in a few
	 * different contexts: 1) from xmit(), before hitting is_dead and
	 * returning, 2) from wg_packet_consume_data(), before hitting is_dead
	 * and returning, 3) from wg_receive_handshake_packet() after a point
	 * where it has processed an incoming handshake packet, but where
	 * all calls to pass it off to timers fails because of is_dead. We won't
	 * have new references in (1) eventually, because we're removed from
	 * allowedips; we won't have new references in (2) eventually, because
	 * wg_index_hashtable_lookup will always return NULL, since we removed
	 * all existing keypairs and no more can be created; we won't have new
	 * references in (3) eventually, because we're removed from the pubkey
	 * hash table, which allows for a maximum of one handshake response,
	 * via the still-uncleared index hashtable entry, but not more than one,
	 * and in wg_cookie_message_consume, the lookup eventually gets a peer
	 * with a refcount of zero, so no new reference is taken.
	 */

	--peer->device->num_peers;
	wg_peer_put(peer);
}

/* We have a separate "remove" function make sure that all active places where
 * a peer is currently operating will eventually come to an end and not pass
 * their reference onto another context.
 */
void wg_peer_remove(struct wg_peer *peer)
{
	if (unlikely(!peer))
		return;
	lockdep_assert_held(&peer->device->device_update_lock);

	peer_make_dead(peer);
	synchronize_rcu();
	peer_remove_after_dead(peer);
}

void wg_peer_remove_all(struct wg_device *wg)
{
	struct wg_peer *peer, *temp;
	LIST_HEAD(dead_peers);

	lockdep_assert_held(&wg->device_update_lock);

	/* Avoid having to traverse individually for each one. */
	wg_allowedips_free(&wg->peer_allowedips, &wg->device_update_lock);

	list_for_each_entry_safe(peer, temp, &wg->peer_list, peer_list) {
		peer_make_dead(peer);
		list_add_tail(&peer->peer_list, &dead_peers);
	}
	synchronize_rcu();
	list_for_each_entry_safe(peer, temp, &dead_peers, peer_list)
		peer_remove_after_dead(peer);
}

static void rcu_release(struct rcu_head *rcu)
{
	struct wg_peer *peer = container_of(rcu, struct wg_peer, rcu);

	dst_cache_destroy(&peer->endpoint_cache);
	wg_packet_queue_free(&peer->rx_queue, false);
	wg_packet_queue_free(&peer->tx_queue, false);

	/* The final zeroing takes care of clearing any remaining handshake key
	 * material and other potentially sensitive information.
	 */
	kzfree(peer);
}

static void kref_release(struct kref *refcount)
{
	struct wg_peer *peer = container_of(refcount, struct wg_peer, refcount);

	pr_debug("%s: Peer %llu (%pISpfsc) destroyed\n",
		 peer->device->dev->name, peer->internal_id,
		 &peer->endpoint.addr);

	/* Remove ourself from dynamic runtime lookup structures, now that the
	 * last reference is gone.
	 */
	wg_index_hashtable_remove(peer->device->index_hashtable,
				  &peer->handshake.entry);

	/* Remove any lingering packets that didn't have a chance to be
	 * transmitted.
	 */
	wg_packet_purge_staged_packets(peer);

	/* Free the memory used. */
	call_rcu(&peer->rcu, rcu_release);
}

void wg_peer_put(struct wg_peer *peer)
{
	if (unlikely(!peer))
		return;
	kref_put(&peer->refcount, kref_release);
}
