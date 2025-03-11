/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 */

#ifndef EF4_EFX_H
#define EF4_EFX_H

#include "net_driver.h"
#include "filter.h"

/* All controllers use BAR 0 for I/O space and BAR 2(&3) for memory */
/* All VFs use BAR 0/1 for memory */
#define EF4_MEM_BAR 2
#define EF4_MEM_VF_BAR 0

int ef4_net_open(struct net_device *net_dev);
int ef4_net_stop(struct net_device *net_dev);

/* TX */
int ef4_probe_tx_queue(struct ef4_tx_queue *tx_queue);
void ef4_remove_tx_queue(struct ef4_tx_queue *tx_queue);
void ef4_init_tx_queue(struct ef4_tx_queue *tx_queue);
void ef4_init_tx_queue_core_txq(struct ef4_tx_queue *tx_queue);
void ef4_fini_tx_queue(struct ef4_tx_queue *tx_queue);
netdev_tx_t ef4_hard_start_xmit(struct sk_buff *skb,
				struct net_device *net_dev);
netdev_tx_t ef4_enqueue_skb(struct ef4_tx_queue *tx_queue, struct sk_buff *skb);
void ef4_xmit_done(struct ef4_tx_queue *tx_queue, unsigned int index);
int ef4_setup_tc(struct net_device *net_dev, enum tc_setup_type type,
		 void *type_data);
unsigned int ef4_tx_max_skb_descs(struct ef4_nic *efx);
extern bool ef4_separate_tx_channels;

/* RX */
void ef4_set_default_rx_indir_table(struct ef4_nic *efx);
void ef4_rx_config_page_split(struct ef4_nic *efx);
int ef4_probe_rx_queue(struct ef4_rx_queue *rx_queue);
void ef4_remove_rx_queue(struct ef4_rx_queue *rx_queue);
void ef4_init_rx_queue(struct ef4_rx_queue *rx_queue);
void ef4_fini_rx_queue(struct ef4_rx_queue *rx_queue);
void ef4_fast_push_rx_descriptors(struct ef4_rx_queue *rx_queue, bool atomic);
void ef4_rx_slow_fill(struct timer_list *t);
void __ef4_rx_packet(struct ef4_channel *channel);
void ef4_rx_packet(struct ef4_rx_queue *rx_queue, unsigned int index,
		   unsigned int n_frags, unsigned int len, u16 flags);
static inline void ef4_rx_flush_packet(struct ef4_channel *channel)
{
	if (channel->rx_pkt_n_frags)
		__ef4_rx_packet(channel);
}
void ef4_schedule_slow_fill(struct ef4_rx_queue *rx_queue);

#define EF4_MAX_DMAQ_SIZE 4096UL
#define EF4_DEFAULT_DMAQ_SIZE 1024UL
#define EF4_MIN_DMAQ_SIZE 512UL

#define EF4_MAX_EVQ_SIZE 16384UL
#define EF4_MIN_EVQ_SIZE 512UL

/* Maximum number of TCP segments we support for soft-TSO */
#define EF4_TSO_MAX_SEGS	100

/* The smallest [rt]xq_entries that the driver supports.  RX minimum
 * is a bit arbitrary.  For TX, we must have space for at least 2
 * TSO skbs.
 */
#define EF4_RXQ_MIN_ENT		128U
#define EF4_TXQ_MIN_ENT(efx)	(2 * ef4_tx_max_skb_descs(efx))

static inline bool ef4_rss_enabled(struct ef4_nic *efx)
{
	return efx->rss_spread > 1;
}

/* Filters */

void ef4_mac_reconfigure(struct ef4_nic *efx);

/**
 * ef4_filter_insert_filter - add or replace a filter
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
 * 3. If !ef4_filter_is_mc_recipient(@spec), or the NIC does not
 *    support delivery to multiple recipients, return -%EEXIST.
 *
 * This implies that filters for multiple multicast recipients must
 * all be inserted with the same priority and @replace_equal = %false.
 */
static inline s32 ef4_filter_insert_filter(struct ef4_nic *efx,
					   struct ef4_filter_spec *spec,
					   bool replace_equal)
{
	return efx->type->filter_insert(efx, spec, replace_equal);
}

/**
 * ef4_filter_remove_id_safe - remove a filter by ID, carefully
 * @efx: NIC from which to remove the filter
 * @priority: Priority of filter, as passed to @ef4_filter_insert_filter
 * @filter_id: ID of filter, as returned by @ef4_filter_insert_filter
 *
 * This function will range-check @filter_id, so it is safe to call
 * with a value passed from userland.
 */
static inline int ef4_filter_remove_id_safe(struct ef4_nic *efx,
					    enum ef4_filter_priority priority,
					    u32 filter_id)
{
	return efx->type->filter_remove_safe(efx, priority, filter_id);
}

/**
 * ef4_filter_get_filter_safe - retrieve a filter by ID, carefully
 * @efx: NIC from which to remove the filter
 * @priority: Priority of filter, as passed to @ef4_filter_insert_filter
 * @filter_id: ID of filter, as returned by @ef4_filter_insert_filter
 * @spec: Buffer in which to store filter specification
 *
 * This function will range-check @filter_id, so it is safe to call
 * with a value passed from userland.
 */
static inline int
ef4_filter_get_filter_safe(struct ef4_nic *efx,
			   enum ef4_filter_priority priority,
			   u32 filter_id, struct ef4_filter_spec *spec)
{
	return efx->type->filter_get_safe(efx, priority, filter_id, spec);
}

static inline u32 ef4_filter_count_rx_used(struct ef4_nic *efx,
					   enum ef4_filter_priority priority)
{
	return efx->type->filter_count_rx_used(efx, priority);
}
static inline u32 ef4_filter_get_rx_id_limit(struct ef4_nic *efx)
{
	return efx->type->filter_get_rx_id_limit(efx);
}
static inline s32 ef4_filter_get_rx_ids(struct ef4_nic *efx,
					enum ef4_filter_priority priority,
					u32 *buf, u32 size)
{
	return efx->type->filter_get_rx_ids(efx, priority, buf, size);
}
#ifdef CONFIG_RFS_ACCEL
int ef4_filter_rfs(struct net_device *net_dev, const struct sk_buff *skb,
		   u16 rxq_index, u32 flow_id);
bool __ef4_filter_rfs_expire(struct ef4_nic *efx, unsigned quota);
static inline void ef4_filter_rfs_expire(struct ef4_channel *channel)
{
	if (channel->rfs_filters_added >= 60 &&
	    __ef4_filter_rfs_expire(channel->efx, 100))
		channel->rfs_filters_added -= 60;
}
#define ef4_filter_rfs_enabled() 1
#else
static inline void ef4_filter_rfs_expire(struct ef4_channel *channel) {}
#define ef4_filter_rfs_enabled() 0
#endif
bool ef4_filter_is_mc_recipient(const struct ef4_filter_spec *spec);

/* Channels */
int ef4_channel_dummy_op_int(struct ef4_channel *channel);
void ef4_channel_dummy_op_void(struct ef4_channel *channel);
int ef4_realloc_channels(struct ef4_nic *efx, u32 rxq_entries, u32 txq_entries);

/* Ports */
int ef4_reconfigure_port(struct ef4_nic *efx);
int __ef4_reconfigure_port(struct ef4_nic *efx);

/* Ethtool support */
extern const struct ethtool_ops ef4_ethtool_ops;

/* Reset handling */
int ef4_reset(struct ef4_nic *efx, enum reset_type method);
void ef4_reset_down(struct ef4_nic *efx, enum reset_type method);
int ef4_reset_up(struct ef4_nic *efx, enum reset_type method, bool ok);
int ef4_try_recovery(struct ef4_nic *efx);

/* Global */
void ef4_schedule_reset(struct ef4_nic *efx, enum reset_type type);
unsigned int ef4_usecs_to_ticks(struct ef4_nic *efx, unsigned int usecs);
int ef4_init_irq_moderation(struct ef4_nic *efx, unsigned int tx_usecs,
			    unsigned int rx_usecs, bool rx_adaptive,
			    bool rx_may_override_tx);
void ef4_get_irq_moderation(struct ef4_nic *efx, unsigned int *tx_usecs,
			    unsigned int *rx_usecs, bool *rx_adaptive);
void ef4_stop_eventq(struct ef4_channel *channel);
void ef4_start_eventq(struct ef4_channel *channel);

/* Dummy PHY ops for PHY drivers */
int ef4_port_dummy_op_int(struct ef4_nic *efx);
void ef4_port_dummy_op_void(struct ef4_nic *efx);

/* Update the generic software stats in the passed stats array */
void ef4_update_sw_stats(struct ef4_nic *efx, u64 *stats);

/* MTD */
#ifdef CONFIG_SFC_FALCON_MTD
int ef4_mtd_add(struct ef4_nic *efx, struct ef4_mtd_partition *parts,
		size_t n_parts, size_t sizeof_part);
static inline int ef4_mtd_probe(struct ef4_nic *efx)
{
	return efx->type->mtd_probe(efx);
}
void ef4_mtd_rename(struct ef4_nic *efx);
void ef4_mtd_remove(struct ef4_nic *efx);
#else
static inline int ef4_mtd_probe(struct ef4_nic *efx) { return 0; }
static inline void ef4_mtd_rename(struct ef4_nic *efx) {}
static inline void ef4_mtd_remove(struct ef4_nic *efx) {}
#endif

static inline void ef4_schedule_channel(struct ef4_channel *channel)
{
	netif_vdbg(channel->efx, intr, channel->efx->net_dev,
		   "channel %d scheduling NAPI poll on CPU%d\n",
		   channel->channel, raw_smp_processor_id());

	napi_schedule(&channel->napi_str);
}

static inline void ef4_schedule_channel_irq(struct ef4_channel *channel)
{
	channel->event_test_cpu = raw_smp_processor_id();
	ef4_schedule_channel(channel);
}

void ef4_link_status_changed(struct ef4_nic *efx);
void ef4_link_set_advertising(struct ef4_nic *efx, u32);
void ef4_link_set_wanted_fc(struct ef4_nic *efx, u8);

static inline void ef4_device_detach_sync(struct ef4_nic *efx)
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

static inline bool ef4_rwsem_assert_write_locked(struct rw_semaphore *sem)
{
	if (WARN_ON(down_read_trylock(sem))) {
		up_read(sem);
		return false;
	}
	return true;
}

#endif /* EF4_EFX_H */
