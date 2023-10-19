/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 */

#ifndef EFX_EFX_H
#define EFX_EFX_H

#include <linux/indirect_call_wrapper.h>
#include "net_driver.h"
#include "filter.h"

/* TX */
void efx_siena_init_tx_queue_core_txq(struct efx_tx_queue *tx_queue);
netdev_tx_t efx_siena_hard_start_xmit(struct sk_buff *skb,
				      struct net_device *net_dev);
netdev_tx_t __efx_siena_enqueue_skb(struct efx_tx_queue *tx_queue,
				    struct sk_buff *skb);
static inline netdev_tx_t efx_enqueue_skb(struct efx_tx_queue *tx_queue, struct sk_buff *skb)
{
	return INDIRECT_CALL_1(tx_queue->efx->type->tx_enqueue,
			       __efx_siena_enqueue_skb, tx_queue, skb);
}
int efx_siena_setup_tc(struct net_device *net_dev, enum tc_setup_type type,
		       void *type_data);

/* RX */
void __efx_siena_rx_packet(struct efx_channel *channel);
void efx_siena_rx_packet(struct efx_rx_queue *rx_queue, unsigned int index,
			 unsigned int n_frags, unsigned int len, u16 flags);
static inline void efx_rx_flush_packet(struct efx_channel *channel)
{
	if (channel->rx_pkt_n_frags)
		__efx_siena_rx_packet(channel);
}

/* Maximum number of TCP segments we support for soft-TSO */
#define EFX_TSO_MAX_SEGS	100

/* The smallest [rt]xq_entries that the driver supports.  RX minimum
 * is a bit arbitrary.  For TX, we must have space for at least 2
 * TSO skbs.
 */
#define EFX_RXQ_MIN_ENT		128U
#define EFX_TXQ_MIN_ENT(efx)	(2 * efx_siena_tx_max_skb_descs(efx))

/* All EF10 architecture NICs steal one bit of the DMAQ size for various
 * other purposes when counting TxQ entries, so we halve the queue size.
 */
#define EFX_TXQ_MAX_ENT(efx)	(EFX_WORKAROUND_EF10(efx) ? \
				 EFX_MAX_DMAQ_SIZE / 2 : EFX_MAX_DMAQ_SIZE)

static inline bool efx_rss_enabled(struct efx_nic *efx)
{
	return efx->rss_spread > 1;
}

/* Filters */

/**
 * efx_filter_insert_filter - add or replace a filter
 * @efx: NIC in which to insert the filter
 * @spec: Specification for the filter
 * @replace_equal: Flag for whether the specified filter may replace an
 *	existing filter with equal priority
 *
 * On success, return the filter ID.
 * On failure, return a negative error code.
 *
 * If existing filters have equal match values to the new filter spec,
 * then the new filter might replace them or the function might fail,
 * as follows.
 *
 * 1. If the existing filters have lower priority, or @replace_equal
 *    is set and they have equal priority, replace them.
 *
 * 2. If the existing filters have higher priority, return -%EPERM.
 *
 * 3. If !efx_siena_filter_is_mc_recipient(@spec), or the NIC does not
 *    support delivery to multiple recipients, return -%EEXIST.
 *
 * This implies that filters for multiple multicast recipients must
 * all be inserted with the same priority and @replace_equal = %false.
 */
static inline s32 efx_filter_insert_filter(struct efx_nic *efx,
					   struct efx_filter_spec *spec,
					   bool replace_equal)
{
	return efx->type->filter_insert(efx, spec, replace_equal);
}

/**
 * efx_filter_remove_id_safe - remove a filter by ID, carefully
 * @efx: NIC from which to remove the filter
 * @priority: Priority of filter, as passed to @efx_filter_insert_filter
 * @filter_id: ID of filter, as returned by @efx_filter_insert_filter
 *
 * This function will range-check @filter_id, so it is safe to call
 * with a value passed from userland.
 */
static inline int efx_filter_remove_id_safe(struct efx_nic *efx,
					    enum efx_filter_priority priority,
					    u32 filter_id)
{
	return efx->type->filter_remove_safe(efx, priority, filter_id);
}

/**
 * efx_filter_get_filter_safe - retrieve a filter by ID, carefully
 * @efx: NIC from which to remove the filter
 * @priority: Priority of filter, as passed to @efx_filter_insert_filter
 * @filter_id: ID of filter, as returned by @efx_filter_insert_filter
 * @spec: Buffer in which to store filter specification
 *
 * This function will range-check @filter_id, so it is safe to call
 * with a value passed from userland.
 */
static inline int
efx_filter_get_filter_safe(struct efx_nic *efx,
			   enum efx_filter_priority priority,
			   u32 filter_id, struct efx_filter_spec *spec)
{
	return efx->type->filter_get_safe(efx, priority, filter_id, spec);
}

static inline u32 efx_filter_count_rx_used(struct efx_nic *efx,
					   enum efx_filter_priority priority)
{
	return efx->type->filter_count_rx_used(efx, priority);
}
static inline u32 efx_filter_get_rx_id_limit(struct efx_nic *efx)
{
	return efx->type->filter_get_rx_id_limit(efx);
}
static inline s32 efx_filter_get_rx_ids(struct efx_nic *efx,
					enum efx_filter_priority priority,
					u32 *buf, u32 size)
{
	return efx->type->filter_get_rx_ids(efx, priority, buf, size);
}

/* RSS contexts */
static inline bool efx_rss_active(struct efx_rss_context *ctx)
{
	return ctx->context_id != EFX_MCDI_RSS_CONTEXT_INVALID;
}

/* Ethtool support */
extern const struct ethtool_ops efx_siena_ethtool_ops;

/* Global */
unsigned int efx_siena_usecs_to_ticks(struct efx_nic *efx, unsigned int usecs);
int efx_siena_init_irq_moderation(struct efx_nic *efx, unsigned int tx_usecs,
				  unsigned int rx_usecs, bool rx_adaptive,
				  bool rx_may_override_tx);
void efx_siena_get_irq_moderation(struct efx_nic *efx, unsigned int *tx_usecs,
				  unsigned int *rx_usecs, bool *rx_adaptive);

/* Update the generic software stats in the passed stats array */
void efx_siena_update_sw_stats(struct efx_nic *efx, u64 *stats);

/* MTD */
#ifdef CONFIG_SFC_SIENA_MTD
int efx_siena_mtd_add(struct efx_nic *efx, struct efx_mtd_partition *parts,
		      size_t n_parts, size_t sizeof_part);
static inline int efx_mtd_probe(struct efx_nic *efx)
{
	return efx->type->mtd_probe(efx);
}
void efx_siena_mtd_rename(struct efx_nic *efx);
void efx_siena_mtd_remove(struct efx_nic *efx);
#else
static inline int efx_mtd_probe(struct efx_nic *efx) { return 0; }
static inline void efx_siena_mtd_rename(struct efx_nic *efx) {}
static inline void efx_siena_mtd_remove(struct efx_nic *efx) {}
#endif

#ifdef CONFIG_SFC_SIENA_SRIOV
static inline unsigned int efx_vf_size(struct efx_nic *efx)
{
	return 1 << efx->vi_scale;
}
#endif

static inline void efx_device_detach_sync(struct efx_nic *efx)
{
	struct net_device *dev = efx->net_dev;

	/* Lock/freeze all TX queues so that we can be sure the
	 * TX scheduler is stopped when we're done and before
	 * netif_device_present() becomes false.
	 */
	netif_tx_lock_bh(dev);
	netif_device_detach(dev);
	netif_tx_unlock_bh(dev);
}

static inline void efx_device_attach_if_not_resetting(struct efx_nic *efx)
{
	if ((efx->state != STATE_DISABLED) && !efx->reset_pending)
		netif_device_attach(efx->net_dev);
}

static inline bool efx_rwsem_assert_write_locked(struct rw_semaphore *sem)
{
	if (WARN_ON(down_read_trylock(sem))) {
		up_read(sem);
		return false;
	}
	return true;
}

int efx_siena_xdp_tx_buffers(struct efx_nic *efx, int n,
			     struct xdp_frame **xdpfs, bool flush);

#endif /* EFX_EFX_H */
