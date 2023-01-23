/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __MLX5E_IPSEC_H__
#define __MLX5E_IPSEC_H__

#include <linux/mlx5/device.h>
#include <net/xfrm.h>
#include <linux/idr.h>
#include "lib/aso.h"

#define MLX5E_IPSEC_SADB_RX_BITS 10
#define MLX5E_IPSEC_ESN_SCOPE_MID 0x80000000L

struct aes_gcm_keymat {
	u64   seq_iv;

	u32   salt;
	u32   icv_len;

	u32   key_len;
	u32   aes_key[256 / 32];
};

struct mlx5_accel_esp_xfrm_attrs {
	u32   esn;
	u32   spi;
	u32   flags;
	struct aes_gcm_keymat aes_gcm;

	union {
		__be32 a4;
		__be32 a6[4];
	} saddr;

	union {
		__be32 a4;
		__be32 a6[4];
	} daddr;

	u8 dir : 2;
	u8 esn_overlap : 1;
	u8 esn_trigger : 1;
	u8 type : 2;
	u8 family;
	u32 replay_window;
	u32 authsize;
	u32 reqid;
	u64 hard_packet_limit;
	u64 soft_packet_limit;
};

enum mlx5_ipsec_cap {
	MLX5_IPSEC_CAP_CRYPTO		= 1 << 0,
	MLX5_IPSEC_CAP_ESN		= 1 << 1,
	MLX5_IPSEC_CAP_PACKET_OFFLOAD	= 1 << 2,
};

struct mlx5e_priv;

struct mlx5e_ipsec_hw_stats {
	u64 ipsec_rx_pkts;
	u64 ipsec_rx_bytes;
	u64 ipsec_rx_drop_pkts;
	u64 ipsec_rx_drop_bytes;
	u64 ipsec_tx_pkts;
	u64 ipsec_tx_bytes;
	u64 ipsec_tx_drop_pkts;
	u64 ipsec_tx_drop_bytes;
};

struct mlx5e_ipsec_sw_stats {
	atomic64_t ipsec_rx_drop_sp_alloc;
	atomic64_t ipsec_rx_drop_sadb_miss;
	atomic64_t ipsec_rx_drop_syndrome;
	atomic64_t ipsec_tx_drop_bundle;
	atomic64_t ipsec_tx_drop_no_state;
	atomic64_t ipsec_tx_drop_not_ip;
	atomic64_t ipsec_tx_drop_trailer;
};

struct mlx5e_ipsec_rx;
struct mlx5e_ipsec_tx;

struct mlx5e_ipsec_work {
	struct work_struct work;
	struct mlx5e_ipsec *ipsec;
	u32 id;
};

struct mlx5e_ipsec_aso {
	u8 ctx[MLX5_ST_SZ_BYTES(ipsec_aso)];
	dma_addr_t dma_addr;
	struct mlx5_aso *aso;
	/* Protect ASO WQ access, as it is global to whole IPsec */
	spinlock_t lock;
};

struct mlx5e_ipsec {
	struct mlx5_core_dev *mdev;
	struct xarray sadb;
	struct mlx5e_ipsec_sw_stats sw_stats;
	struct mlx5e_ipsec_hw_stats hw_stats;
	struct workqueue_struct *wq;
	struct mlx5e_flow_steering *fs;
	struct mlx5e_ipsec_rx *rx_ipv4;
	struct mlx5e_ipsec_rx *rx_ipv6;
	struct mlx5e_ipsec_tx *tx;
	struct mlx5e_ipsec_aso *aso;
	struct notifier_block nb;
};

struct mlx5e_ipsec_esn_state {
	u32 esn;
	u8 trigger: 1;
	u8 overlap: 1;
};

struct mlx5e_ipsec_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5_modify_hdr *modify_hdr;
	struct mlx5_pkt_reformat *pkt_reformat;
};

struct mlx5e_ipsec_modify_state_work {
	struct work_struct		work;
	struct mlx5_accel_esp_xfrm_attrs attrs;
};

struct mlx5e_ipsec_sa_entry {
	struct mlx5e_ipsec_esn_state esn_state;
	struct xfrm_state *x;
	struct mlx5e_ipsec *ipsec;
	struct mlx5_accel_esp_xfrm_attrs attrs;
	void (*set_iv_op)(struct sk_buff *skb, struct xfrm_state *x,
			  struct xfrm_offload *xo);
	u32 ipsec_obj_id;
	u32 enc_key_id;
	struct mlx5e_ipsec_rule ipsec_rule;
	struct mlx5e_ipsec_modify_state_work modify_work;
};

struct mlx5_accel_pol_xfrm_attrs {
	union {
		__be32 a4;
		__be32 a6[4];
	} saddr;

	union {
		__be32 a4;
		__be32 a6[4];
	} daddr;

	u8 family;
	u8 action;
	u8 type : 2;
	u8 dir : 2;
	u32 reqid;
};

struct mlx5e_ipsec_pol_entry {
	struct xfrm_policy *x;
	struct mlx5e_ipsec *ipsec;
	struct mlx5e_ipsec_rule ipsec_rule;
	struct mlx5_accel_pol_xfrm_attrs attrs;
};

#ifdef CONFIG_MLX5_EN_IPSEC

void mlx5e_ipsec_init(struct mlx5e_priv *priv);
void mlx5e_ipsec_cleanup(struct mlx5e_priv *priv);
void mlx5e_ipsec_build_netdev(struct mlx5e_priv *priv);

void mlx5e_accel_ipsec_fs_cleanup(struct mlx5e_ipsec *ipsec);
int mlx5e_accel_ipsec_fs_init(struct mlx5e_ipsec *ipsec);
int mlx5e_accel_ipsec_fs_add_rule(struct mlx5e_ipsec_sa_entry *sa_entry);
void mlx5e_accel_ipsec_fs_del_rule(struct mlx5e_ipsec_sa_entry *sa_entry);
int mlx5e_accel_ipsec_fs_add_pol(struct mlx5e_ipsec_pol_entry *pol_entry);
void mlx5e_accel_ipsec_fs_del_pol(struct mlx5e_ipsec_pol_entry *pol_entry);

int mlx5_ipsec_create_sa_ctx(struct mlx5e_ipsec_sa_entry *sa_entry);
void mlx5_ipsec_free_sa_ctx(struct mlx5e_ipsec_sa_entry *sa_entry);

u32 mlx5_ipsec_device_caps(struct mlx5_core_dev *mdev);

void mlx5_accel_esp_modify_xfrm(struct mlx5e_ipsec_sa_entry *sa_entry,
				const struct mlx5_accel_esp_xfrm_attrs *attrs);

int mlx5e_ipsec_aso_init(struct mlx5e_ipsec *ipsec);
void mlx5e_ipsec_aso_cleanup(struct mlx5e_ipsec *ipsec);

int mlx5e_ipsec_aso_query(struct mlx5e_ipsec_sa_entry *sa_entry,
			  struct mlx5_wqe_aso_ctrl_seg *data);
void mlx5e_ipsec_aso_update_curlft(struct mlx5e_ipsec_sa_entry *sa_entry,
				   u64 *packets);

void mlx5e_accel_ipsec_fs_read_stats(struct mlx5e_priv *priv,
				     void *ipsec_stats);

void mlx5e_ipsec_build_accel_xfrm_attrs(struct mlx5e_ipsec_sa_entry *sa_entry,
					struct mlx5_accel_esp_xfrm_attrs *attrs);
static inline struct mlx5_core_dev *
mlx5e_ipsec_sa2dev(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	return sa_entry->ipsec->mdev;
}

static inline struct mlx5_core_dev *
mlx5e_ipsec_pol2dev(struct mlx5e_ipsec_pol_entry *pol_entry)
{
	return pol_entry->ipsec->mdev;
}
#else
static inline void mlx5e_ipsec_init(struct mlx5e_priv *priv)
{
}

static inline void mlx5e_ipsec_cleanup(struct mlx5e_priv *priv)
{
}

static inline void mlx5e_ipsec_build_netdev(struct mlx5e_priv *priv)
{
}

static inline u32 mlx5_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	return 0;
}
#endif

#endif	/* __MLX5E_IPSEC_H__ */
