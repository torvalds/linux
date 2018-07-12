/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#ifndef __MLX5E_FLOW_STEER_H__
#define __MLX5E_FLOW_STEER_H__

struct mlx5e_flow_table {
	int num_groups;
	struct mlx5_flow_table *t;
	struct mlx5_flow_group **g;
};

#ifdef CONFIG_MLX5_EN_RXNFC

struct mlx5e_ethtool_table {
	struct mlx5_flow_table *ft;
	int                    num_rules;
};

#define ETHTOOL_NUM_L3_L4_FTS 7
#define ETHTOOL_NUM_L2_FTS 4

struct mlx5e_ethtool_steering {
	struct mlx5e_ethtool_table      l3_l4_ft[ETHTOOL_NUM_L3_L4_FTS];
	struct mlx5e_ethtool_table      l2_ft[ETHTOOL_NUM_L2_FTS];
	struct list_head                rules;
	int                             tot_num_rules;
};

void mlx5e_ethtool_init_steering(struct mlx5e_priv *priv);
void mlx5e_ethtool_cleanup_steering(struct mlx5e_priv *priv);
int mlx5e_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd);
int mlx5e_get_rxnfc(struct net_device *dev,
		    struct ethtool_rxnfc *info, u32 *rule_locs);
#else
static inline void mlx5e_ethtool_init_steering(struct mlx5e_priv *priv)    { }
static inline void mlx5e_ethtool_cleanup_steering(struct mlx5e_priv *priv) { }
#endif /* CONFIG_MLX5_EN_RXNFC */

#ifdef CONFIG_MLX5_EN_ARFS
#define ARFS_HASH_SHIFT BITS_PER_BYTE
#define ARFS_HASH_SIZE BIT(BITS_PER_BYTE)

struct arfs_table {
	struct mlx5e_flow_table  ft;
	struct mlx5_flow_handle	 *default_rule;
	struct hlist_head	 rules_hash[ARFS_HASH_SIZE];
};

enum  arfs_type {
	ARFS_IPV4_TCP,
	ARFS_IPV6_TCP,
	ARFS_IPV4_UDP,
	ARFS_IPV6_UDP,
	ARFS_NUM_TYPES,
};

struct mlx5e_arfs_tables {
	struct arfs_table arfs_tables[ARFS_NUM_TYPES];
	/* Protect aRFS rules list */
	spinlock_t                     arfs_lock;
	struct list_head               rules;
	int                            last_filter_id;
	struct workqueue_struct        *wq;
};

int mlx5e_arfs_create_tables(struct mlx5e_priv *priv);
void mlx5e_arfs_destroy_tables(struct mlx5e_priv *priv);
int mlx5e_arfs_enable(struct mlx5e_priv *priv);
int mlx5e_arfs_disable(struct mlx5e_priv *priv);
int mlx5e_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			u16 rxq_index, u32 flow_id);
#else
static inline int mlx5e_arfs_create_tables(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_arfs_destroy_tables(struct mlx5e_priv *priv) {}
static inline int mlx5e_arfs_enable(struct mlx5e_priv *priv) { return -EOPNOTSUPP; }
static inline int mlx5e_arfs_disable(struct mlx5e_priv *priv) {	return -EOPNOTSUPP; }
#endif

#endif /* __MLX5E_FLOW_STEER_H__ */

