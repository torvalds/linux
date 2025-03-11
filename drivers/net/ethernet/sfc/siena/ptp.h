/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
 */

#ifndef EFX_PTP_H
#define EFX_PTP_H

#include <linux/net_tstamp.h>
#include "net_driver.h"

struct kernel_ethtool_ts_info;
void efx_siena_ptp_defer_probe_with_channel(struct efx_nic *efx);
struct efx_channel *efx_siena_ptp_channel(struct efx_nic *efx);
int efx_siena_ptp_set_ts_config(struct efx_nic *efx,
				struct kernel_hwtstamp_config *config,
				struct netlink_ext_ack *extack);
int efx_siena_ptp_get_ts_config(struct efx_nic *efx,
				struct kernel_hwtstamp_config *config);
void efx_siena_ptp_get_ts_info(struct efx_nic *efx,
			       struct kernel_ethtool_ts_info *ts_info);
bool efx_siena_ptp_is_ptp_tx(struct efx_nic *efx, struct sk_buff *skb);
int efx_siena_ptp_get_mode(struct efx_nic *efx);
int efx_siena_ptp_change_mode(struct efx_nic *efx, bool enable_wanted,
			      unsigned int new_mode);
int efx_siena_ptp_tx(struct efx_nic *efx, struct sk_buff *skb);
void efx_siena_ptp_event(struct efx_nic *efx, efx_qword_t *ev);
size_t efx_siena_ptp_describe_stats(struct efx_nic *efx, u8 **strings);
size_t efx_siena_ptp_update_stats(struct efx_nic *efx, u64 *stats);
void efx_siena_time_sync_event(struct efx_channel *channel, efx_qword_t *ev);
void __efx_siena_rx_skb_attach_timestamp(struct efx_channel *channel,
					 struct sk_buff *skb);
static inline void efx_rx_skb_attach_timestamp(struct efx_channel *channel,
					       struct sk_buff *skb)
{
	if (channel->sync_events_state == SYNC_EVENTS_VALID)
		__efx_siena_rx_skb_attach_timestamp(channel, skb);
}

void efx_siena_ptp_start_datapath(struct efx_nic *efx);
void efx_siena_ptp_stop_datapath(struct efx_nic *efx);
bool efx_siena_ptp_use_mac_tx_timestamps(struct efx_nic *efx);
ktime_t efx_siena_ptp_nic_to_kernel_time(struct efx_tx_queue *tx_queue);

#endif /* EFX_PTP_H */
