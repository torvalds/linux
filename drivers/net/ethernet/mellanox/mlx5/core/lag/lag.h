/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_LAG_H__
#define __MLX5_LAG_H__

#include <linux/debugfs.h>

#define MLX5_LAG_MAX_HASH_BUCKETS 16
#include "mlx5_core.h"
#include "mp.h"
#include "port_sel.h"
#include "mpesw.h"

enum {
	MLX5_LAG_P1,
	MLX5_LAG_P2,
};

enum {
	MLX5_LAG_FLAG_NDEVS_READY,
};

enum {
	MLX5_LAG_MODE_FLAG_HASH_BASED,
	MLX5_LAG_MODE_FLAG_SHARED_FDB,
};

enum mlx5_lag_mode {
	MLX5_LAG_MODE_NONE,
	MLX5_LAG_MODE_ROCE,
	MLX5_LAG_MODE_SRIOV,
	MLX5_LAG_MODE_MULTIPATH,
	MLX5_LAG_MODE_MPESW,
};

struct lag_func {
	struct mlx5_core_dev *dev;
	struct net_device    *netdev;
	bool has_drop;
};

/* Used for collection of netdev event info. */
struct lag_tracker {
	enum   netdev_lag_tx_type           tx_type;
	struct netdev_lag_lower_state_info  netdev_state[MLX5_MAX_PORTS];
	unsigned int is_bonded:1;
	unsigned int has_inactive:1;
	enum netdev_lag_hash hash_type;
};

/* LAG data of a ConnectX card.
 * It serves both its phys functions.
 */
struct mlx5_lag {
	enum mlx5_lag_mode        mode;
	unsigned long		  mode_flags;
	unsigned long		  state_flags;
	u8			  ports;
	u8			  buckets;
	int			  mode_changes_in_progress;
	u8			  v2p_map[MLX5_MAX_PORTS * MLX5_LAG_MAX_HASH_BUCKETS];
	struct kref               ref;
	struct lag_func           pf[MLX5_MAX_PORTS];
	struct lag_tracker        tracker;
	struct workqueue_struct   *wq;
	struct delayed_work       bond_work;
	struct work_struct	  mpesw_work;
	struct notifier_block     nb;
	struct lag_mp             lag_mp;
	struct mlx5_lag_port_sel  port_sel;
	/* Protect lag fields/state changes */
	struct mutex		  lock;
	struct lag_mpesw	  lag_mpesw;
};

static inline struct mlx5_lag *
mlx5_lag_dev(struct mlx5_core_dev *dev)
{
	return dev->priv.lag;
}

static inline bool
__mlx5_lag_is_active(struct mlx5_lag *ldev)
{
	return ldev->mode != MLX5_LAG_MODE_NONE;
}

static inline bool
mlx5_lag_is_ready(struct mlx5_lag *ldev)
{
	return test_bit(MLX5_LAG_FLAG_NDEVS_READY, &ldev->state_flags);
}

void mlx5_modify_lag(struct mlx5_lag *ldev,
		     struct lag_tracker *tracker);
int mlx5_activate_lag(struct mlx5_lag *ldev,
		      struct lag_tracker *tracker,
		      enum mlx5_lag_mode mode,
		      bool shared_fdb);
int mlx5_lag_dev_get_netdev_idx(struct mlx5_lag *ldev,
				struct net_device *ndev);
bool mlx5_shared_fdb_supported(struct mlx5_lag *ldev);
void mlx5_lag_del_mpesw_rule(struct mlx5_core_dev *dev);
int mlx5_lag_add_mpesw_rule(struct mlx5_core_dev *dev);

char *mlx5_get_str_port_sel_mode(struct mlx5_lag *ldev);
void mlx5_infer_tx_enabled(struct lag_tracker *tracker, u8 num_ports,
			   u8 *ports, int *num_enabled);

void mlx5_ldev_add_debugfs(struct mlx5_core_dev *dev);
void mlx5_ldev_remove_debugfs(struct dentry *dbg);
void mlx5_disable_lag(struct mlx5_lag *ldev);

#endif /* __MLX5_LAG_H__ */
