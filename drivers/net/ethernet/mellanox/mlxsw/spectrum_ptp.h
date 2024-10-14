/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_PTP_H
#define _MLXSW_SPECTRUM_PTP_H

#include <linux/device.h>
#include <linux/rhashtable.h>

struct mlxsw_sp;
struct mlxsw_sp_port;
struct mlxsw_sp_ptp_clock;

#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)

struct mlxsw_sp_ptp_clock *
mlxsw_sp1_ptp_clock_init(struct mlxsw_sp *mlxsw_sp, struct device *dev);

void mlxsw_sp1_ptp_clock_fini(struct mlxsw_sp_ptp_clock *clock);

struct mlxsw_sp_ptp_state *mlxsw_sp1_ptp_init(struct mlxsw_sp *mlxsw_sp);

void mlxsw_sp1_ptp_fini(struct mlxsw_sp_ptp_state *ptp_state);

void mlxsw_sp1_ptp_receive(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			   u16 local_port);

void mlxsw_sp1_ptp_transmitted(struct mlxsw_sp *mlxsw_sp,
			       struct sk_buff *skb, u16 local_port);

void mlxsw_sp1_ptp_got_timestamp(struct mlxsw_sp *mlxsw_sp, bool ingress,
				 u16 local_port, u8 message_type,
				 u8 domain_number, u16 sequence_id,
				 u64 timestamp);

int mlxsw_sp1_ptp_hwtstamp_get(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct hwtstamp_config *config);

int mlxsw_sp1_ptp_hwtstamp_set(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct hwtstamp_config *config);

void mlxsw_sp1_ptp_shaper_work(struct work_struct *work);

int mlxsw_sp1_ptp_get_ts_info(struct mlxsw_sp *mlxsw_sp,
			      struct kernel_ethtool_ts_info *info);

int mlxsw_sp1_get_stats_count(void);
void mlxsw_sp1_get_stats_strings(u8 **p);
void mlxsw_sp1_get_stats(struct mlxsw_sp_port *mlxsw_sp_port,
			 u64 *data, int data_index);

int mlxsw_sp_ptp_txhdr_construct(struct mlxsw_core *mlxsw_core,
				 struct mlxsw_sp_port *mlxsw_sp_port,
				 struct sk_buff *skb,
				 const struct mlxsw_tx_info *tx_info);

struct mlxsw_sp_ptp_clock *
mlxsw_sp2_ptp_clock_init(struct mlxsw_sp *mlxsw_sp, struct device *dev);

void mlxsw_sp2_ptp_clock_fini(struct mlxsw_sp_ptp_clock *clock);

struct mlxsw_sp_ptp_state *mlxsw_sp2_ptp_init(struct mlxsw_sp *mlxsw_sp);

void mlxsw_sp2_ptp_fini(struct mlxsw_sp_ptp_state *ptp_state);

void mlxsw_sp2_ptp_receive(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			   u16 local_port);

void mlxsw_sp2_ptp_transmitted(struct mlxsw_sp *mlxsw_sp,
			       struct sk_buff *skb, u16 local_port);

int mlxsw_sp2_ptp_hwtstamp_get(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct hwtstamp_config *config);

int mlxsw_sp2_ptp_hwtstamp_set(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct hwtstamp_config *config);

int mlxsw_sp2_ptp_get_ts_info(struct mlxsw_sp *mlxsw_sp,
			      struct kernel_ethtool_ts_info *info);

int mlxsw_sp2_ptp_txhdr_construct(struct mlxsw_core *mlxsw_core,
				  struct mlxsw_sp_port *mlxsw_sp_port,
				  struct sk_buff *skb,
				  const struct mlxsw_tx_info *tx_info);

#else

static inline struct mlxsw_sp_ptp_clock *
mlxsw_sp1_ptp_clock_init(struct mlxsw_sp *mlxsw_sp, struct device *dev)
{
	return NULL;
}

static inline void mlxsw_sp1_ptp_clock_fini(struct mlxsw_sp_ptp_clock *clock)
{
}

static inline struct mlxsw_sp_ptp_state *
mlxsw_sp1_ptp_init(struct mlxsw_sp *mlxsw_sp)
{
	return NULL;
}

static inline void mlxsw_sp1_ptp_fini(struct mlxsw_sp_ptp_state *ptp_state)
{
}

static inline void mlxsw_sp1_ptp_receive(struct mlxsw_sp *mlxsw_sp,
					 struct sk_buff *skb, u16 local_port)
{
	mlxsw_sp_rx_listener_no_mark_func(skb, local_port, mlxsw_sp);
}

static inline void mlxsw_sp1_ptp_transmitted(struct mlxsw_sp *mlxsw_sp,
					     struct sk_buff *skb, u16 local_port)
{
	dev_kfree_skb_any(skb);
}

static inline void
mlxsw_sp1_ptp_got_timestamp(struct mlxsw_sp *mlxsw_sp, bool ingress,
			    u16 local_port, u8 message_type,
			    u8 domain_number,
			    u16 sequence_id, u64 timestamp)
{
}

static inline int
mlxsw_sp1_ptp_hwtstamp_get(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct hwtstamp_config *config)
{
	return -EOPNOTSUPP;
}

static inline int
mlxsw_sp1_ptp_hwtstamp_set(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct hwtstamp_config *config)
{
	return -EOPNOTSUPP;
}

static inline void mlxsw_sp1_ptp_shaper_work(struct work_struct *work)
{
}

static inline int mlxsw_sp1_get_stats_count(void)
{
	return 0;
}

static inline void mlxsw_sp1_get_stats_strings(u8 **p)
{
}

static inline void mlxsw_sp1_get_stats(struct mlxsw_sp_port *mlxsw_sp_port,
				       u64 *data, int data_index)
{
}

static inline int
mlxsw_sp_ptp_txhdr_construct(struct mlxsw_core *mlxsw_core,
			     struct mlxsw_sp_port *mlxsw_sp_port,
			     struct sk_buff *skb,
			     const struct mlxsw_tx_info *tx_info)
{
	return -EOPNOTSUPP;
}

static inline struct mlxsw_sp_ptp_clock *
mlxsw_sp2_ptp_clock_init(struct mlxsw_sp *mlxsw_sp, struct device *dev)
{
	return NULL;
}

static inline void mlxsw_sp2_ptp_clock_fini(struct mlxsw_sp_ptp_clock *clock)
{
}

static inline struct mlxsw_sp_ptp_state *
mlxsw_sp2_ptp_init(struct mlxsw_sp *mlxsw_sp)
{
	return NULL;
}

static inline void mlxsw_sp2_ptp_fini(struct mlxsw_sp_ptp_state *ptp_state)
{
}

static inline void mlxsw_sp2_ptp_receive(struct mlxsw_sp *mlxsw_sp,
					 struct sk_buff *skb, u16 local_port)
{
	mlxsw_sp_rx_listener_no_mark_func(skb, local_port, mlxsw_sp);
}

static inline void mlxsw_sp2_ptp_transmitted(struct mlxsw_sp *mlxsw_sp,
					     struct sk_buff *skb, u16 local_port)
{
	dev_kfree_skb_any(skb);
}

static inline int
mlxsw_sp2_ptp_hwtstamp_get(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct hwtstamp_config *config)
{
	return -EOPNOTSUPP;
}

static inline int
mlxsw_sp2_ptp_hwtstamp_set(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct hwtstamp_config *config)
{
	return -EOPNOTSUPP;
}

static inline int
mlxsw_sp2_ptp_txhdr_construct(struct mlxsw_core *mlxsw_core,
			      struct mlxsw_sp_port *mlxsw_sp_port,
			      struct sk_buff *skb,
			      const struct mlxsw_tx_info *tx_info)
{
	return -EOPNOTSUPP;
}
#endif

static inline void mlxsw_sp2_ptp_shaper_work(struct work_struct *work)
{
}

static inline int mlxsw_sp2_get_stats_count(void)
{
	return 0;
}

static inline void mlxsw_sp2_get_stats_strings(u8 **p)
{
}

static inline void mlxsw_sp2_get_stats(struct mlxsw_sp_port *mlxsw_sp_port,
				       u64 *data, int data_index)
{
}

#endif
