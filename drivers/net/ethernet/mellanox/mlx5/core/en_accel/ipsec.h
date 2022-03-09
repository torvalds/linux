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

#ifdef CONFIG_MLX5_EN_IPSEC

#include <linux/mlx5/device.h>
#include <net/xfrm.h>
#include <linux/idr.h>

#define MLX5E_IPSEC_SADB_RX_BITS 10
#define MLX5E_IPSEC_ESN_SCOPE_MID 0x80000000L

enum mlx5_accel_esp_flags {
	MLX5_ACCEL_ESP_FLAGS_TUNNEL            = 0,    /* Default */
	MLX5_ACCEL_ESP_FLAGS_TRANSPORT         = 1UL << 0,
	MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED     = 1UL << 1,
	MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP = 1UL << 2,
};

enum mlx5_accel_esp_action {
	MLX5_ACCEL_ESP_ACTION_DECRYPT,
	MLX5_ACCEL_ESP_ACTION_ENCRYPT,
};

enum mlx5_accel_esp_keymats {
	MLX5_ACCEL_ESP_KEYMAT_AES_NONE,
	MLX5_ACCEL_ESP_KEYMAT_AES_GCM,
};

struct aes_gcm_keymat {
	u64   seq_iv;

	u32   salt;
	u32   icv_len;

	u32   key_len;
	u32   aes_key[256 / 32];
};

struct mlx5_accel_esp_xfrm_attrs {
	enum mlx5_accel_esp_action action;
	u32   esn;
	__be32 spi;
	u32   seq;
	u32   tfc_pad;
	u32   flags;
	u32   sa_handle;
	union {
		struct {
			u32 size;

		} bmp;
	} replay;
	enum mlx5_accel_esp_keymats keymat_type;
	union {
		struct aes_gcm_keymat aes_gcm;
	} keymat;

	union {
		__be32 a4;
		__be32 a6[4];
	} saddr;

	union {
		__be32 a4;
		__be32 a6[4];
	} daddr;

	u8 is_ipv6;
};

struct mlx5_accel_esp_xfrm {
	struct mlx5_core_dev  *mdev;
	struct mlx5_accel_esp_xfrm_attrs attrs;
};

enum mlx5_accel_ipsec_cap {
	MLX5_ACCEL_IPSEC_CAP_DEVICE		= 1 << 0,
	MLX5_ACCEL_IPSEC_CAP_ESP		= 1 << 1,
	MLX5_ACCEL_IPSEC_CAP_IPV6		= 1 << 2,
	MLX5_ACCEL_IPSEC_CAP_LSO		= 1 << 3,
	MLX5_ACCEL_IPSEC_CAP_ESN		= 1 << 4,
};

struct mlx5e_priv;

struct mlx5e_ipsec_sw_stats {
	atomic64_t ipsec_rx_drop_sp_alloc;
	atomic64_t ipsec_rx_drop_sadb_miss;
	atomic64_t ipsec_rx_drop_syndrome;
	atomic64_t ipsec_tx_drop_bundle;
	atomic64_t ipsec_tx_drop_no_state;
	atomic64_t ipsec_tx_drop_not_ip;
	atomic64_t ipsec_tx_drop_trailer;
};

struct mlx5e_accel_fs_esp;
struct mlx5e_ipsec_tx;

struct mlx5e_ipsec {
	struct mlx5_core_dev *mdev;
	DECLARE_HASHTABLE(sadb_rx, MLX5E_IPSEC_SADB_RX_BITS);
	spinlock_t sadb_rx_lock; /* Protects sadb_rx */
	struct mlx5e_ipsec_sw_stats sw_stats;
	struct workqueue_struct *wq;
	struct mlx5e_accel_fs_esp *rx_fs;
	struct mlx5e_ipsec_tx *tx_fs;
};

struct mlx5e_ipsec_esn_state {
	u32 esn;
	u8 trigger: 1;
	u8 overlap: 1;
};

struct mlx5e_ipsec_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5_modify_hdr *set_modify_hdr;
};

struct mlx5e_ipsec_modify_state_work {
	struct work_struct		work;
	struct mlx5_accel_esp_xfrm_attrs attrs;
};

struct mlx5e_ipsec_sa_entry {
	struct hlist_node hlist; /* Item in SADB_RX hashtable */
	struct mlx5e_ipsec_esn_state esn_state;
	unsigned int handle; /* Handle in SADB_RX */
	struct xfrm_state *x;
	struct mlx5e_ipsec *ipsec;
	struct mlx5_accel_esp_xfrm *xfrm;
	void *hw_context;
	void (*set_iv_op)(struct sk_buff *skb, struct xfrm_state *x,
			  struct xfrm_offload *xo);
	u32 ipsec_obj_id;
	struct mlx5e_ipsec_rule ipsec_rule;
	struct mlx5e_ipsec_modify_state_work modify_work;
};

int mlx5e_ipsec_init(struct mlx5e_priv *priv);
void mlx5e_ipsec_cleanup(struct mlx5e_priv *priv);
void mlx5e_ipsec_build_netdev(struct mlx5e_priv *priv);

struct xfrm_state *mlx5e_ipsec_sadb_rx_lookup(struct mlx5e_ipsec *dev,
					      unsigned int handle);

void mlx5e_accel_ipsec_fs_cleanup(struct mlx5e_ipsec *ipsec);
int mlx5e_accel_ipsec_fs_init(struct mlx5e_ipsec *ipsec);
int mlx5e_accel_ipsec_fs_add_rule(struct mlx5e_priv *priv,
				  struct mlx5_accel_esp_xfrm_attrs *attrs,
				  u32 ipsec_obj_id,
				  struct mlx5e_ipsec_rule *ipsec_rule);
void mlx5e_accel_ipsec_fs_del_rule(struct mlx5e_priv *priv,
				   struct mlx5_accel_esp_xfrm_attrs *attrs,
				   struct mlx5e_ipsec_rule *ipsec_rule);

void *mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				       struct mlx5_accel_esp_xfrm *xfrm,
				       u32 *sa_handle);
void mlx5_accel_esp_free_hw_context(struct mlx5_core_dev *mdev, void *context);

u32 mlx5_ipsec_device_caps(struct mlx5_core_dev *mdev);

struct mlx5_accel_esp_xfrm *
mlx5_accel_esp_create_xfrm(struct mlx5_core_dev *mdev,
			   const struct mlx5_accel_esp_xfrm_attrs *attrs);
void mlx5_accel_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm);
void mlx5_accel_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
				const struct mlx5_accel_esp_xfrm_attrs *attrs);
#else
static inline int mlx5e_ipsec_init(struct mlx5e_priv *priv)
{
	return 0;
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
