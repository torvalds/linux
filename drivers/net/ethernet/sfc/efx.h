/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_EFX_H
#define EFX_EFX_H

#include "net_driver.h"
#include "filter.h"

int efx_net_open(struct net_device *net_dev);
int efx_net_stop(struct net_device *net_dev);

/* TX */
int efx_probe_tx_queue(struct efx_tx_queue *tx_queue);
void efx_remove_tx_queue(struct efx_tx_queue *tx_queue);
void efx_init_tx_queue(struct efx_tx_queue *tx_queue);
void efx_init_tx_queue_core_txq(struct efx_tx_queue *tx_queue);
void efx_fini_tx_queue(struct efx_tx_queue *tx_queue);
netdev_tx_t efx_hard_start_xmit(struct sk_buff *skb,
				struct net_device *net_dev);
netdev_tx_t efx_enqueue_skb(struct efx_tx_queue *tx_queue, struct sk_buff *skb);
void efx_xmit_done(struct efx_tx_queue *tx_queue, unsigned int index);
int efx_setup_tc(struct net_device *net_dev, enum tc_setup_type type,
		 void *type_data);
unsigned int efx_tx_max_skb_descs(struct efx_nic *efx);
extern unsigned int efx_piobuf_size;
extern bool efx_separate_tx_channels;

/* RX */
void efx_set_default_rx_indir_table(struct efx_nic *efx);
void efx_rx_config_page_split(struct efx_nic *efx);
int efx_probe_rx_queue(struct efx_rx_queue *rx_queue);
void efx_remove_rx_queue(struct efx_rx_queue *rx_queue);
void efx_init_rx_queue(struct efx_rx_queue *rx_queue);
void efx_fini_rx_queue(struct efx_rx_queue *rx_queue);
void efx_fast_push_rx_descriptors(struct efx_rx_queue *rx_queue, bool atomic);
void efx_rx_slow_fill(struct timer_list *t);
void __efx_rx_packet(struct efx_channel *channel);
void efx_rx_packet(struct efx_rx_queue *rx_queue, unsigned int index,
		   unsigned int n_frags, unsigned int len, u16 flags);
static inline void efx_rx_flush_packet(struct efx_channel *channel)
{
	if (channel->rx_pkt_n_frags)
		__efx_rx_packet(channel);
}
void efx_schedule_slow_fill(struct efx_rx_queue *rx_queue);

#define EFX_MAX_DMAQ_SIZE 4096UL
#define EFX_DEFAULT_DMAQ_SIZE 1024UL
#define EFX_MIN_DMAQ_SIZE 512UL

#define EFX_MAX_EVQ_SIZE 16384UL
#define EFX_MIN_EVQ_SIZE 512UL

/* Maximum number of TCP segments we support for soft-TSO */
#define EFX_TSO_MAX_SEGS	100

/* The smallest [rt]xq_entries that the driver supports.  RX minimum
 * is a bit arbitrary.  For TX, we must have space for at least 2
 * TSO skbs.
 */
#define EFX_RXQ_MIN_ENT		128U
#define EFX_TXQ_MIN_ENT(efx)	(2 * efx_tx_max_skb_descs(efx))

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

void efx_mac_reconfigure(struct efx_nic *efx);

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
 * 3. If !efx_filter_is_mc_recipient(@spec), or the NIC does not
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
#ifdef CONFIG_RFS_ACCEL
int efx_filter_rfs(struct net_device *net_dev, const struct sk_buff *skb,
		   u16 rxq_index, u32 flow_id);
bool __efx_filter_rfs_expire(struct efx_nic *efx, unsigned quota);
static inline void efx_filter_rfs_expire(struct efx_channel *channel)
{
	if (channel->rfs_filters_added >= 60 &&
	    __efx_filter_rfs_expire(channel->efx, 100))
		channel->rfs_filters_added -= 60;
}
#define efx_filter_rfs_enabled() 1
#else
static inline void efx_filter_rfs_expire(struct efx_channel *channel) {}
#define efx_filter_rfs_enabled() 0
#endif
bool efx_filter_is_mc_recipient(const struct efx_filter_spec *spec);

/* Channels */
int efx_channel_dummy_op_int(struct efx_channel *channel);
void efx_channel_dummy_op_void(struct efx_channel *channel);
int efx_realloc_channels(struct efx_nic *efx, u32 rxq_entries, u32 txq_entries);

/* Ports */
int efx_reconfigure_port(struct efx_nic *efx);
int __efx_reconfigure_port(struct efx_nic *efx);

/* Ethtool support */
extern const struct ethtool_ops efx_ethtool_ops;

/* Reset handling */
int efx_reset(struct efx_nic *efx, enum reset_type method);
void efx_reset_down(struct efx_nic *efx, enum reset_type method);
int efx_reset_up(struct efx_nic *efx, enum reset_type method, bool ok);
int efx_try_recovery(struct efx_nic *efx);

/* Global */
void efx_schedule_reset(struct efx_nic *efx, enum reset_type type);
unsigned int efx_usecs_to_ticks(struct efx_nic *efx, unsigned int usecs);
unsigned int efx_ticks_to_usecs(struct efx_nic *efx, unsigned int ticks);
int efx_init_irq_moderation(struct efx_nic *efx, unsigned int tx_usecs,
			    unsigned int rx_usecs, bool rx_adaptive,
			    bool rx_may_override_tx);
void efx_get_irq_moderation(struct efx_nic *efx, unsigned int *tx_usecs,
			    unsigned int *rx_usecs, bool *rx_adaptive);
void efx_stop_eventq(struct efx_channel *channel);
void efx_start_eventq(struct efx_channel *channel);

/* Dummy PHY ops for PHY drivers */
int efx_port_dummy_op_int(struct efx_nic *efx);
void efx_port_dummy_op_void(struct efx_nic *efx);

/* Update the generic software stats in the passed stats array */
void efx_update_sw_stats(struct efx_nic *efx, u64 *stats);

/* MTD */
#ifdef CONFIG_SFC_MTD
int efx_mtd_add(struct efx_nic *efx, struct efx_mtd_partition *parts,
		size_t n_parts, size_t sizeof_part);
static inline int efx_mtd_probe(struct efx_nic *efx)
{
	return efx->type->mtd_probe(efx);
}
void efx_mtd_rename(struct efx_nic *efx);
void efx_mtd_remove(struct efx_nic *efx);
#else
static inline int efx_mtd_probe(struct efx_nic *efx) { return 0; }
static inline void efx_mtd_rename(struct efx_nic *efx) {}
static inline void efx_mtd_remove(struct efx_nic *efx) {}
#endif

#ifdef CONFIG_SFC_SRIOV
static inline unsigned int efx_vf_size(struct efx_nic *efx)
{
	return 1 << efx->vi_scale;
}
#endif

static inline void efx_schedule_channel(struct efx_channel *channel)
{
	netif_vdbg(channel->efx, intr, channel->efx->net_dev,
		   "channel %d scheduling NAPI poll on CPU%d\n",
		   channel->channel, raw_smp_processor_id());

	napi_schedule(&channel->napi_str);
}

static inline void efx_schedule_channel_irq(struct efx_channel *channel)
{
	channel->event_test_cpu = raw_smp_processor_id();
	efx_schedule_channel(channel);
}

void efx_link_status_changed(struct efx_nic *efx);
void efx_link_set_advertising(struct efx_nic *efx, u32);
void efx_link_set_wanted_fc(struct efx_nic *efx, u8);

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

#endif /* EFX_EFX_H */
