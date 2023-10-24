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

struct upspec {
	u16 dport;
	u16 dport_mask;
	u16 sport;
	u16 sport_mask;
	u8 proto;
};

struct mlx5_ipsec_lft {
	u64 hard_packet_limit;
	u64 soft_packet_limit;
	u64 numb_rounds_hard;
	u64 numb_rounds_soft;
};

struct mlx5_replay_esn {
	u32 replay_window;
	u32 esn;
	u32 esn_msb;
	u8 overlap : 1;
	u8 trigger : 1;
};

struct mlx5_accel_esp_xfrm_attrs {
	u32   spi;
	u32   mode;
	struct aes_gcm_keymat aes_gcm;

	union {
		__be32 a4;
		__be32 a6[4];
	} saddr;

	union {
		__be32 a4;
		__be32 a6[4];
	} daddr;

	struct upspec upspec;
	u8 dir : 2;
	u8 type : 2;
	u8 drop : 1;
	u8 encap : 1;
	u8 family;
	struct mlx5_replay_esn replay_esn;
	u32 authsize;
	u32 reqid;
	struct mlx5_ipsec_lft lft;
	union {
		u8 smac[ETH_ALEN];
		__be16 sport;
	};
	union {
		u8 dmac[ETH_ALEN];
		__be16 dport;
	};
};

enum mlx5_ipsec_cap {
	MLX5_IPSEC_CAP_CRYPTO		= 1 << 0,
	MLX5_IPSEC_CAP_ESN		= 1 << 1,
	MLX5_IPSEC_CAP_PACKET_OFFLOAD	= 1 << 2,
	MLX5_IPSEC_CAP_ROCE             = 1 << 3,
	MLX5_IPSEC_CAP_PRIO             = 1 << 4,
	MLX5_IPSEC_CAP_TUNNEL           = 1 << 5,
	MLX5_IPSEC_CAP_ESPINUDP         = 1 << 6,
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

struct mlx5e_ipsec_fc;
struct mlx5e_ipsec_tx;

struct mlx5e_ipsec_work {
	struct work_struct work;
	struct mlx5e_ipsec_sa_entry *sa_entry;
	void *data;
};

struct mlx5e_ipsec_netevent_data {
	u8 addr[ETH_ALEN];
};

struct mlx5e_ipsec_dwork {
	struct delayed_work dwork;
	struct mlx5e_ipsec_sa_entry *sa_entry;
};

struct mlx5e_ipsec_aso {
	u8 __aligned(64) ctx[MLX5_ST_SZ_BYTES(ipsec_aso)];
	dma_addr_t dma_addr;
	struct mlx5_aso *aso;
	/* Protect ASO WQ access, as it is global to whole IPsec */
	spinlock_t lock;
};

struct mlx5e_ipsec_rx_create_attr {
	struct mlx5_flow_namespace *ns;
	struct mlx5_ttc_table *ttc;
	u32 family;
	int prio;
	int pol_level;
	int sa_level;
	int status_level;
	enum mlx5_flow_namespace_type chains_ns;
};

struct mlx5e_ipsec_ft {
	struct mutex mutex; /* Protect changes to this struct */
	struct mlx5_flow_table *pol;
	struct mlx5_flow_table *sa;
	struct mlx5_flow_table *status;
	u32 refcnt;
};

struct mlx5e_ipsec_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5_modify_hdr *modify_hdr;
	struct mlx5_pkt_reformat *pkt_reformat;
	struct mlx5_fc *fc;
};

struct mlx5e_ipsec_miss {
	struct mlx5_flow_group *group;
	struct mlx5_flow_handle *rule;
};

struct mlx5e_ipsec_rx {
	struct mlx5e_ipsec_ft ft;
	struct mlx5e_ipsec_miss pol;
	struct mlx5e_ipsec_miss sa;
	struct mlx5e_ipsec_rule status;
	struct mlx5e_ipsec_miss status_drop;
	struct mlx5_fc *status_drop_cnt;
	struct mlx5e_ipsec_fc *fc;
	struct mlx5_fs_chains *chains;
	u8 allow_tunnel_mode : 1;
	struct xarray ipsec_obj_id_map;
};

struct mlx5e_ipsec_tx_create_attr {
	int prio;
	int pol_level;
	int sa_level;
	int cnt_level;
	enum mlx5_flow_namespace_type chains_ns;
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
	struct mlx5e_ipsec_rx *rx_esw;
	struct mlx5e_ipsec_tx *tx;
	struct mlx5e_ipsec_tx *tx_esw;
	struct mlx5e_ipsec_aso *aso;
	struct notifier_block nb;
	struct notifier_block netevent_nb;
	struct mlx5_ipsec_fs *roce;
	u8 is_uplink_rep: 1;
};

struct mlx5e_ipsec_esn_state {
	u32 esn;
	u32 esn_msb;
	u8 overlap: 1;
};

struct mlx5e_ipsec_limits {
	u64 round;
	u8 soft_limit_hit : 1;
	u8 fix_limit : 1;
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
	struct mlx5e_ipsec_work *work;
	struct mlx5e_ipsec_dwork *dwork;
	struct mlx5e_ipsec_limits limits;
	u32 rx_mapped_id;
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

	struct upspec upspec;
	u8 family;
	u8 action;
	u8 type : 2;
	u8 dir : 2;
	u32 reqid;
	u32 prio;
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
void mlx5e_accel_ipsec_fs_modify(struct mlx5e_ipsec_sa_entry *sa_entry);
bool mlx5e_ipsec_fs_tunnel_enabled(struct mlx5e_ipsec_sa_entry *sa_entry);

int mlx5_ipsec_create_sa_ctx(struct mlx5e_ipsec_sa_entry *sa_entry);
void mlx5_ipsec_free_sa_ctx(struct mlx5e_ipsec_sa_entry *sa_entry);

u32 mlx5_ipsec_device_caps(struct mlx5_core_dev *mdev);

void mlx5_accel_esp_modify_xfrm(struct mlx5e_ipsec_sa_entry *sa_entry,
				const struct mlx5_accel_esp_xfrm_attrs *attrs);

int mlx5e_ipsec_aso_init(struct mlx5e_ipsec *ipsec);
void mlx5e_ipsec_aso_cleanup(struct mlx5e_ipsec *ipsec);

int mlx5e_ipsec_aso_query(struct mlx5e_ipsec_sa_entry *sa_entry,
			  struct mlx5_wqe_aso_ctrl_seg *data);
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

static inline bool addr6_all_zero(__be32 *addr6)
{
	static const __be32 zaddr6[4] = {};

	return !memcmp(addr6, zaddr6, sizeof(zaddr6));
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
