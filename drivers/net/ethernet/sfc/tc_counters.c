// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "tc_counters.h"
#include "tc_encap_actions.h"
#include "mae_counter_format.h"
#include "mae.h"
#include "rx_common.h"

/* Counter-management hashtables */

static const struct rhashtable_params efx_tc_counter_id_ht_params = {
	.key_len	= offsetof(struct efx_tc_counter_index, linkage),
	.key_offset	= 0,
	.head_offset	= offsetof(struct efx_tc_counter_index, linkage),
};

static const struct rhashtable_params efx_tc_counter_ht_params = {
	.key_len	= offsetof(struct efx_tc_counter, linkage),
	.key_offset	= 0,
	.head_offset	= offsetof(struct efx_tc_counter, linkage),
};

static void efx_tc_counter_free(void *ptr, void *__unused)
{
	struct efx_tc_counter *cnt = ptr;

	WARN_ON(!list_empty(&cnt->users));
	/* We'd like to synchronize_rcu() here, but unfortunately we aren't
	 * removing the element from the hashtable (it's not clear that's a
	 * safe thing to do in an rhashtable_free_and_destroy free_fn), so
	 * threads could still be obtaining new pointers to *cnt if they can
	 * race against this function at all.
	 */
	flush_work(&cnt->work);
	EFX_WARN_ON_PARANOID(spin_is_locked(&cnt->lock));
	kfree(cnt);
}

static void efx_tc_counter_id_free(void *ptr, void *__unused)
{
	struct efx_tc_counter_index *ctr = ptr;

	WARN_ON(refcount_read(&ctr->ref));
	kfree(ctr);
}

int efx_tc_init_counters(struct efx_nic *efx)
{
	int rc;

	rc = rhashtable_init(&efx->tc->counter_id_ht, &efx_tc_counter_id_ht_params);
	if (rc < 0)
		goto fail_counter_id_ht;
	rc = rhashtable_init(&efx->tc->counter_ht, &efx_tc_counter_ht_params);
	if (rc < 0)
		goto fail_counter_ht;
	return 0;
fail_counter_ht:
	rhashtable_destroy(&efx->tc->counter_id_ht);
fail_counter_id_ht:
	return rc;
}

/* Only call this in init failure teardown.
 * Normal exit should fini instead as there may be entries in the table.
 */
void efx_tc_destroy_counters(struct efx_nic *efx)
{
	rhashtable_destroy(&efx->tc->counter_ht);
	rhashtable_destroy(&efx->tc->counter_id_ht);
}

void efx_tc_fini_counters(struct efx_nic *efx)
{
	rhashtable_free_and_destroy(&efx->tc->counter_id_ht, efx_tc_counter_id_free, NULL);
	rhashtable_free_and_destroy(&efx->tc->counter_ht, efx_tc_counter_free, NULL);
}

static void efx_tc_counter_work(struct work_struct *work)
{
	struct efx_tc_counter *cnt = container_of(work, struct efx_tc_counter, work);
	struct efx_tc_encap_action *encap;
	struct efx_tc_action_set *act;
	unsigned long touched;
	struct neighbour *n;

	spin_lock_bh(&cnt->lock);
	touched = READ_ONCE(cnt->touched);

	list_for_each_entry(act, &cnt->users, count_user) {
		encap = act->encap_md;
		if (!encap)
			continue;
		if (!encap->neigh) /* can't happen */
			continue;
		if (time_after_eq(encap->neigh->used, touched))
			continue;
		encap->neigh->used = touched;
		/* We have passed traffic using this ARP entry, so
		 * indicate to the ARP cache that it's still active
		 */
		if (encap->neigh->dst_ip)
			n = neigh_lookup(&arp_tbl, &encap->neigh->dst_ip,
					 encap->neigh->egdev);
		else
#if IS_ENABLED(CONFIG_IPV6)
			n = neigh_lookup(ipv6_stub->nd_tbl,
					 &encap->neigh->dst_ip6,
					 encap->neigh->egdev);
#else
			n = NULL;
#endif
		if (!n)
			continue;

		neigh_event_send(n, NULL);
		neigh_release(n);
	}
	spin_unlock_bh(&cnt->lock);
}

/* Counter allocation */

struct efx_tc_counter *efx_tc_flower_allocate_counter(struct efx_nic *efx,
						      int type)
{
	struct efx_tc_counter *cnt;
	int rc, rc2;

	cnt = kzalloc(sizeof(*cnt), GFP_USER);
	if (!cnt)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&cnt->lock);
	INIT_WORK(&cnt->work, efx_tc_counter_work);
	cnt->touched = jiffies;
	cnt->type = type;

	rc = efx_mae_allocate_counter(efx, cnt);
	if (rc)
		goto fail1;
	INIT_LIST_HEAD(&cnt->users);
	rc = rhashtable_insert_fast(&efx->tc->counter_ht, &cnt->linkage,
				    efx_tc_counter_ht_params);
	if (rc)
		goto fail2;
	return cnt;
fail2:
	/* If we get here, it implies that we couldn't insert into the table,
	 * which in turn probably means that the fw_id was already taken.
	 * In that case, it's unclear whether we really 'own' the fw_id; but
	 * the firmware seemed to think we did, so it's proper to free it.
	 */
	rc2 = efx_mae_free_counter(efx, cnt);
	if (rc2)
		netif_warn(efx, hw, efx->net_dev,
			   "Failed to free MAE counter %u, rc %d\n",
			   cnt->fw_id, rc2);
fail1:
	kfree(cnt);
	return ERR_PTR(rc > 0 ? -EIO : rc);
}

void efx_tc_flower_release_counter(struct efx_nic *efx,
				   struct efx_tc_counter *cnt)
{
	int rc;

	rhashtable_remove_fast(&efx->tc->counter_ht, &cnt->linkage,
			       efx_tc_counter_ht_params);
	rc = efx_mae_free_counter(efx, cnt);
	if (rc)
		netif_warn(efx, hw, efx->net_dev,
			   "Failed to free MAE counter %u, rc %d\n",
			   cnt->fw_id, rc);
	WARN_ON(!list_empty(&cnt->users));
	/* This doesn't protect counter updates coming in arbitrarily long
	 * after we deleted the counter.  The RCU just ensures that we won't
	 * free the counter while another thread has a pointer to it.
	 * Ensuring we don't update the wrong counter if the ID gets re-used
	 * is handled by the generation count.
	 */
	synchronize_rcu();
	flush_work(&cnt->work);
	EFX_WARN_ON_PARANOID(spin_is_locked(&cnt->lock));
	kfree(cnt);
}

static struct efx_tc_counter *efx_tc_flower_find_counter_by_fw_id(
				struct efx_nic *efx, int type, u32 fw_id)
{
	struct efx_tc_counter key = {};

	key.fw_id = fw_id;
	key.type = type;

	return rhashtable_lookup_fast(&efx->tc->counter_ht, &key,
				      efx_tc_counter_ht_params);
}

/* TC cookie to counter mapping */

void efx_tc_flower_put_counter_index(struct efx_nic *efx,
				     struct efx_tc_counter_index *ctr)
{
	if (!refcount_dec_and_test(&ctr->ref))
		return; /* still in use */
	rhashtable_remove_fast(&efx->tc->counter_id_ht, &ctr->linkage,
			       efx_tc_counter_id_ht_params);
	efx_tc_flower_release_counter(efx, ctr->cnt);
	kfree(ctr);
}

struct efx_tc_counter_index *efx_tc_flower_get_counter_index(
				struct efx_nic *efx, unsigned long cookie,
				enum efx_tc_counter_type type)
{
	struct efx_tc_counter_index *ctr, *old;
	struct efx_tc_counter *cnt;

	ctr = kzalloc(sizeof(*ctr), GFP_USER);
	if (!ctr)
		return ERR_PTR(-ENOMEM);
	ctr->cookie = cookie;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->counter_id_ht,
						&ctr->linkage,
						efx_tc_counter_id_ht_params);
	if (old) {
		/* don't need our new entry */
		kfree(ctr);
		if (IS_ERR(old)) /* oh dear, it's actually an error */
			return ERR_CAST(old);
		if (!refcount_inc_not_zero(&old->ref))
			return ERR_PTR(-EAGAIN);
		/* existing entry found */
		ctr = old;
	} else {
		cnt = efx_tc_flower_allocate_counter(efx, type);
		if (IS_ERR(cnt)) {
			rhashtable_remove_fast(&efx->tc->counter_id_ht,
					       &ctr->linkage,
					       efx_tc_counter_id_ht_params);
			kfree(ctr);
			return (void *)cnt; /* it's an ERR_PTR */
		}
		ctr->cnt = cnt;
		refcount_set(&ctr->ref, 1);
	}
	return ctr;
}

struct efx_tc_counter_index *efx_tc_flower_find_counter_index(
				struct efx_nic *efx, unsigned long cookie)
{
	struct efx_tc_counter_index key = {};

	key.cookie = cookie;
	return rhashtable_lookup_fast(&efx->tc->counter_id_ht, &key,
				      efx_tc_counter_id_ht_params);
}

/* TC Channel.  Counter updates are delivered on this channel's RXQ. */

static void efx_tc_handle_no_channel(struct efx_nic *efx)
{
	netif_warn(efx, drv, efx->net_dev,
		   "MAE counters require MSI-X and 1 additional interrupt vector.\n");
}

static int efx_tc_probe_channel(struct efx_channel *channel)
{
	struct efx_rx_queue *rx_queue = &channel->rx_queue;

	channel->irq_moderation_us = 0;
	rx_queue->core_index = 0;

	INIT_WORK(&rx_queue->grant_work, efx_mae_counters_grant_credits);

	return 0;
}

static int efx_tc_start_channel(struct efx_channel *channel)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);
	struct efx_nic *efx = channel->efx;

	return efx_mae_start_counters(efx, rx_queue);
}

static void efx_tc_stop_channel(struct efx_channel *channel)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);
	struct efx_nic *efx = channel->efx;
	int rc;

	rc = efx_mae_stop_counters(efx, rx_queue);
	if (rc)
		netif_warn(efx, drv, efx->net_dev,
			   "Failed to stop MAE counters streaming, rc=%d.\n",
			   rc);
	rx_queue->grant_credits = false;
	flush_work(&rx_queue->grant_work);
}

static void efx_tc_remove_channel(struct efx_channel *channel)
{
}

static void efx_tc_get_channel_name(struct efx_channel *channel,
				    char *buf, size_t len)
{
	snprintf(buf, len, "%s-mae", channel->efx->name);
}

static void efx_tc_counter_update(struct efx_nic *efx,
				  enum efx_tc_counter_type counter_type,
				  u32 counter_idx, u64 packets, u64 bytes,
				  u32 mark)
{
	struct efx_tc_counter *cnt;

	rcu_read_lock(); /* Protect against deletion of 'cnt' */
	cnt = efx_tc_flower_find_counter_by_fw_id(efx, counter_type, counter_idx);
	if (!cnt) {
		/* This can legitimately happen when a counter is removed,
		 * with updates for the counter still in-flight; however this
		 * should be an infrequent occurrence.
		 */
		if (net_ratelimit())
			netif_dbg(efx, drv, efx->net_dev,
				  "Got update for unwanted MAE counter %u type %u\n",
				  counter_idx, counter_type);
		goto out;
	}

	spin_lock_bh(&cnt->lock);
	if ((s32)mark - (s32)cnt->gen < 0) {
		/* This counter update packet is from before the counter was
		 * allocated; thus it must be for a previous counter with
		 * the same ID that has since been freed, and it should be
		 * ignored.
		 */
	} else {
		/* Update latest seen generation count.  This ensures that
		 * even a long-lived counter won't start getting ignored if
		 * the generation count wraps around, unless it somehow
		 * manages to go 1<<31 generations without an update.
		 */
		cnt->gen = mark;
		/* update counter values */
		cnt->packets += packets;
		cnt->bytes += bytes;
		cnt->touched = jiffies;
	}
	spin_unlock_bh(&cnt->lock);
	schedule_work(&cnt->work);
out:
	rcu_read_unlock();
}

static void efx_tc_rx_version_1(struct efx_nic *efx, const u8 *data, u32 mark)
{
	u16 n_counters, i;

	/* Header format:
	 * + |   0    |   1    |   2    |   3    |
	 * 0 |version |         reserved         |
	 * 4 |    seq_index    |   n_counters    |
	 */

	n_counters = le16_to_cpu(*(const __le16 *)(data + 6));

	/* Counter update entry format:
	 * | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | a | b | c | d | e | f |
	 * |  counter_idx  |     packet_count      |      byte_count       |
	 */
	for (i = 0; i < n_counters; i++) {
		const void *entry = data + 8 + 16 * i;
		u64 packet_count, byte_count;
		u32 counter_idx;

		counter_idx = le32_to_cpu(*(const __le32 *)entry);
		packet_count = le32_to_cpu(*(const __le32 *)(entry + 4)) |
			       ((u64)le16_to_cpu(*(const __le16 *)(entry + 8)) << 32);
		byte_count = le16_to_cpu(*(const __le16 *)(entry + 10)) |
			     ((u64)le32_to_cpu(*(const __le32 *)(entry + 12)) << 16);
		efx_tc_counter_update(efx, EFX_TC_COUNTER_TYPE_AR, counter_idx,
				      packet_count, byte_count, mark);
	}
}

#define TCV2_HDR_PTR(pkt, field)						\
	((void)BUILD_BUG_ON_ZERO(ERF_SC_PACKETISER_HEADER_##field##_LBN & 7),	\
	 (pkt) + ERF_SC_PACKETISER_HEADER_##field##_LBN / 8)
#define TCV2_HDR_BYTE(pkt, field)						\
	((void)BUILD_BUG_ON_ZERO(ERF_SC_PACKETISER_HEADER_##field##_WIDTH != 8),\
	 *TCV2_HDR_PTR(pkt, field))
#define TCV2_HDR_WORD(pkt, field)						\
	((void)BUILD_BUG_ON_ZERO(ERF_SC_PACKETISER_HEADER_##field##_WIDTH != 16),\
	 (void)BUILD_BUG_ON_ZERO(ERF_SC_PACKETISER_HEADER_##field##_LBN & 15),	\
	 *(__force const __le16 *)TCV2_HDR_PTR(pkt, field))
#define TCV2_PKT_PTR(pkt, poff, i, field)					\
	((void)BUILD_BUG_ON_ZERO(ERF_SC_PACKETISER_PAYLOAD_##field##_LBN & 7),	\
	 (pkt) + ERF_SC_PACKETISER_PAYLOAD_##field##_LBN/8 + poff +		\
	 i * ER_RX_SL_PACKETISER_PAYLOAD_WORD_SIZE)

/* Read a little-endian 48-bit field with 16-bit alignment */
static u64 efx_tc_read48(const __le16 *field)
{
	u64 out = 0;
	int i;

	for (i = 0; i < 3; i++)
		out |= (u64)le16_to_cpu(field[i]) << (i * 16);
	return out;
}

static enum efx_tc_counter_type efx_tc_rx_version_2(struct efx_nic *efx,
						    const u8 *data, u32 mark)
{
	u8 payload_offset, header_offset, ident;
	enum efx_tc_counter_type type;
	u16 n_counters, i;

	ident = TCV2_HDR_BYTE(data, IDENTIFIER);
	switch (ident) {
	case ERF_SC_PACKETISER_HEADER_IDENTIFIER_AR:
		type = EFX_TC_COUNTER_TYPE_AR;
		break;
	case ERF_SC_PACKETISER_HEADER_IDENTIFIER_CT:
		type = EFX_TC_COUNTER_TYPE_CT;
		break;
	case ERF_SC_PACKETISER_HEADER_IDENTIFIER_OR:
		type = EFX_TC_COUNTER_TYPE_OR;
		break;
	default:
		if (net_ratelimit())
			netif_err(efx, drv, efx->net_dev,
				  "ignored v2 MAE counter packet (bad identifier %u"
				  "), counters may be inaccurate\n", ident);
		return EFX_TC_COUNTER_TYPE_MAX;
	}
	header_offset = TCV2_HDR_BYTE(data, HEADER_OFFSET);
	/* mae_counter_format.h implies that this offset is fixed, since it
	 * carries on with SOP-based LBNs for the fields in this header
	 */
	if (header_offset != ERF_SC_PACKETISER_HEADER_HEADER_OFFSET_DEFAULT) {
		if (net_ratelimit())
			netif_err(efx, drv, efx->net_dev,
				  "choked on v2 MAE counter packet (bad header_offset %u"
				  "), counters may be inaccurate\n", header_offset);
		return EFX_TC_COUNTER_TYPE_MAX;
	}
	payload_offset = TCV2_HDR_BYTE(data, PAYLOAD_OFFSET);
	n_counters = le16_to_cpu(TCV2_HDR_WORD(data, COUNT));

	for (i = 0; i < n_counters; i++) {
		const void *counter_idx_p, *packet_count_p, *byte_count_p;
		u64 packet_count, byte_count;
		u32 counter_idx;

		/* 24-bit field with 32-bit alignment */
		counter_idx_p = TCV2_PKT_PTR(data, payload_offset, i, COUNTER_INDEX);
		BUILD_BUG_ON(ERF_SC_PACKETISER_PAYLOAD_COUNTER_INDEX_WIDTH != 24);
		BUILD_BUG_ON(ERF_SC_PACKETISER_PAYLOAD_COUNTER_INDEX_LBN & 31);
		counter_idx = le32_to_cpu(*(const __le32 *)counter_idx_p) & 0xffffff;
		/* 48-bit field with 16-bit alignment */
		packet_count_p = TCV2_PKT_PTR(data, payload_offset, i, PACKET_COUNT);
		BUILD_BUG_ON(ERF_SC_PACKETISER_PAYLOAD_PACKET_COUNT_WIDTH != 48);
		BUILD_BUG_ON(ERF_SC_PACKETISER_PAYLOAD_PACKET_COUNT_LBN & 15);
		packet_count = efx_tc_read48((const __le16 *)packet_count_p);
		/* 48-bit field with 16-bit alignment */
		byte_count_p = TCV2_PKT_PTR(data, payload_offset, i, BYTE_COUNT);
		BUILD_BUG_ON(ERF_SC_PACKETISER_PAYLOAD_BYTE_COUNT_WIDTH != 48);
		BUILD_BUG_ON(ERF_SC_PACKETISER_PAYLOAD_BYTE_COUNT_LBN & 15);
		byte_count = efx_tc_read48((const __le16 *)byte_count_p);

		if (type == EFX_TC_COUNTER_TYPE_CT) {
			/* CT counters are 1-bit saturating counters to update
			 * the lastuse time in CT stats. A received CT counter
			 * should have packet counter to 0 and only LSB bit on
			 * in byte counter.
			 */
			if (packet_count || byte_count != 1)
				netdev_warn_once(efx->net_dev,
						 "CT counter with inconsistent state (%llu, %llu)\n",
						 packet_count, byte_count);
			/* Do not increment the driver's byte counter */
			byte_count = 0;
		}

		efx_tc_counter_update(efx, type, counter_idx, packet_count,
				      byte_count, mark);
	}
	return type;
}

/* We always swallow the packet, whether successful or not, since it's not
 * a network packet and shouldn't ever be forwarded to the stack.
 * @mark is the generation count for counter allocations.
 */
static bool efx_tc_rx(struct efx_rx_queue *rx_queue, u32 mark)
{
	struct efx_channel *channel = efx_rx_queue_channel(rx_queue);
	struct efx_rx_buffer *rx_buf = efx_rx_buffer(rx_queue,
						     channel->rx_pkt_index);
	const u8 *data = efx_rx_buf_va(rx_buf);
	struct efx_nic *efx = rx_queue->efx;
	enum efx_tc_counter_type type;
	u8 version;

	/* version is always first byte of packet */
	version = *data;
	switch (version) {
	case 1:
		type = EFX_TC_COUNTER_TYPE_AR;
		efx_tc_rx_version_1(efx, data, mark);
		break;
	case ERF_SC_PACKETISER_HEADER_VERSION_VALUE: // 2
		type = efx_tc_rx_version_2(efx, data, mark);
		break;
	default:
		if (net_ratelimit())
			netif_err(efx, drv, efx->net_dev,
				  "choked on MAE counter packet (bad version %u"
				  "); counters may be inaccurate\n",
				  version);
		goto out;
	}

	if (type < EFX_TC_COUNTER_TYPE_MAX) {
		/* Update seen_gen unconditionally, to avoid a missed wakeup if
		 * we race with efx_mae_stop_counters().
		 */
		efx->tc->seen_gen[type] = mark;
		if (efx->tc->flush_counters &&
		    (s32)(efx->tc->flush_gen[type] - mark) <= 0)
			wake_up(&efx->tc->flush_wq);
	}
out:
	efx_free_rx_buffers(rx_queue, rx_buf, 1);
	channel->rx_pkt_n_frags = 0;
	return true;
}

const struct efx_channel_type efx_tc_channel_type = {
	.handle_no_channel	= efx_tc_handle_no_channel,
	.pre_probe		= efx_tc_probe_channel,
	.start			= efx_tc_start_channel,
	.stop			= efx_tc_stop_channel,
	.post_remove		= efx_tc_remove_channel,
	.get_name		= efx_tc_get_channel_name,
	.receive_raw		= efx_tc_rx,
	.keep_eventq		= true,
};
