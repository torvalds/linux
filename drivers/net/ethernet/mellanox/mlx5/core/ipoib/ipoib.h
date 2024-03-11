/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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
 */

#ifndef __MLX5E_IPOB_H__
#define __MLX5E_IPOB_H__

#ifdef CONFIG_MLX5_CORE_IPOIB

#include <linux/mlx5/fs.h>
#include "en.h"

#define MLX5I_MAX_NUM_TC 1

extern const struct ethtool_ops mlx5i_ethtool_ops;
extern const struct ethtool_ops mlx5i_pkey_ethtool_ops;
extern const struct mlx5e_rx_handlers mlx5i_rx_handlers;

#define MLX5_IB_GRH_BYTES       40
#define MLX5_IPOIB_ENCAP_LEN    4
#define MLX5_IPOIB_PSEUDO_LEN   20
#define MLX5_IPOIB_HARD_LEN     (MLX5_IPOIB_PSEUDO_LEN + MLX5_IPOIB_ENCAP_LEN)

/* ipoib rdma netdev's private data structure */
struct mlx5i_priv {
	struct rdma_netdev rn; /* keep this first */
	u32 qpn;
	u32 tisn;
	bool   sub_interface;
	u32    num_sub_interfaces;
	u32    qkey;
	u16    pkey_index;
	struct mlx5i_pkey_qpn_ht *qpn_htbl;
	struct net_device *parent_dev;
	char  *mlx5e_priv[];
};

int mlx5i_create_tis(struct mlx5_core_dev *mdev, u32 underlay_qpn, u32 *tisn);
u32 mlx5i_get_tisn(struct mlx5_core_dev *mdev, struct mlx5e_priv *priv, u8 lag_port, u8 tc);

/* Underlay QP create/destroy functions */
int mlx5i_create_underlay_qp(struct mlx5e_priv *priv);
void mlx5i_destroy_underlay_qp(struct mlx5_core_dev *mdev, u32 qpn);

/* Underlay QP state modification init/uninit functions */
int mlx5i_init_underlay_qp(struct mlx5e_priv *priv);
void mlx5i_uninit_underlay_qp(struct mlx5e_priv *priv);

/* Allocate/Free underlay QPN to net-device hash table */
int mlx5i_pkey_qpn_ht_init(struct net_device *netdev);
void mlx5i_pkey_qpn_ht_cleanup(struct net_device *netdev);

/* Add/Remove an underlay QPN to net-device mapping to/from the hash table */
int mlx5i_pkey_add_qpn(struct net_device *netdev, u32 qpn);
int mlx5i_pkey_del_qpn(struct net_device *netdev, u32 qpn);

/* Get the net-device corresponding to the given underlay QPN */
struct net_device *mlx5i_pkey_get_netdev(struct net_device *netdev, u32 qpn);

/* Shared ndo functions */
int mlx5i_dev_init(struct net_device *dev);
void mlx5i_dev_cleanup(struct net_device *dev);
int mlx5i_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

/* Parent profile functions */
int mlx5i_init(struct mlx5_core_dev *mdev, struct net_device *netdev);
void mlx5i_cleanup(struct mlx5e_priv *priv);

int mlx5i_update_nic_rx(struct mlx5e_priv *priv);

/* Get child interface nic profile */
const struct mlx5e_profile *mlx5i_pkey_get_profile(void);

/* Extract mlx5e_priv from IPoIB netdev */
#define mlx5i_epriv(netdev) ((void *)(((struct mlx5i_priv *)netdev_priv(netdev))->mlx5e_priv))

struct mlx5_wqe_eth_pad {
	u8 rsvd0[16];
};

struct mlx5i_tx_wqe {
	struct mlx5_wqe_ctrl_seg     ctrl;
	struct mlx5_wqe_datagram_seg datagram;
	struct mlx5_wqe_eth_pad      pad;
	struct mlx5_wqe_eth_seg      eth;
	struct mlx5_wqe_data_seg     data[];
};

#define MLX5I_SQ_FETCH_WQE(sq, pi) \
	((struct mlx5i_tx_wqe *)mlx5e_fetch_wqe(&(sq)->wq, pi, sizeof(struct mlx5i_tx_wqe)))

void mlx5i_sq_xmit(struct mlx5e_txqsq *sq, struct sk_buff *skb,
		   struct mlx5_av *av, u32 dqpn, u32 dqkey, bool xmit_more);
void mlx5i_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats);

/* Reference management for child to parent interfaces. */
struct net_device *mlx5i_parent_get(struct net_device *netdev);
void mlx5i_parent_put(struct net_device *netdev);

#endif /* CONFIG_MLX5_CORE_IPOIB */
#endif /* __MLX5E_IPOB_H__ */
