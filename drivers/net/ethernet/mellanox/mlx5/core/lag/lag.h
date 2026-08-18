/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_LAG_H__
#define __MLX5_LAG_H__

#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/xarray.h>
#include <linux/mlx5/fs.h>

#define MLX5_LAG_MAX_HASH_BUCKETS 16
/* XArray mark for the LAG master device
 * (device with lowest mlx5_get_dev_index).
 * Note: XA_MARK_0 is reserved by XA_FLAGS_ALLOC for free-slot tracking.
 */
#define MLX5_LAG_XA_MARK_MASTER XA_MARK_1
/* XArray mark for port-level entries (excludes SD secondaries) */
#define MLX5_LAG_XA_MARK_PORT   XA_MARK_2

/* Like xa_for_each_marked but starting from a given index */
#define xa_for_each_marked_start(xa, index, entry, filter, start)	\
	for (index = start, entry = xa_find(xa, &index, ULONG_MAX, filter); \
	     entry; entry = xa_find_after(xa, &index, ULONG_MAX, filter))

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
	MLX5_LAG_MODE_FLAG_FDB_SEL_MODE_NATIVE,
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
	unsigned int idx; /* xarray index assigned by LAG */
	struct mlx5_nb port_change_nb;
	u32 group_id;        /* SD group ID, 0 = not SD */
	bool sd_fdb_active;  /* set on all SD group members */
	/* Lag demux resources - only populated on master devices */
	struct mlx5_flow_table   *lag_demux_ft;
	struct mlx5_flow_group   *lag_demux_fg;
	struct xarray		  lag_demux_rules;
};

/* Used for collection of netdev event info. */
struct lag_tracker {
	enum   netdev_lag_tx_type           tx_type;
	struct netdev_lag_lower_state_info  netdev_state[MLX5_MAX_PORTS];
	unsigned int is_bonded:1;
	unsigned int has_inactive:1;
	enum netdev_lag_hash hash_type;
	u32 bond_speed_mbps;
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
	struct xarray             pfs;
	struct lag_tracker        tracker;
	struct workqueue_struct   *wq;
	struct delayed_work       bond_work;
	struct work_struct        speed_update_work;
	struct notifier_block     nb;
	possible_net_t net;
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

static inline struct lag_func *
mlx5_lag_pf(struct mlx5_lag *ldev, unsigned int idx)
{
	return xa_load(&ldev->pfs, idx);
}

/* Get device index (mlx5_get_dev_index) from xarray index */
static inline int mlx5_lag_xa_to_dev_idx(struct mlx5_lag *ldev, int xa_idx)
{
	struct lag_func *pf = mlx5_lag_pf(ldev, xa_idx);

	return pf ? mlx5_get_dev_index(pf->dev) : -ENOENT;
}

/* Find lag_func by device index (reverse lookup from mlx5_get_dev_index) */
static inline struct lag_func *
mlx5_lag_pf_by_dev_idx(struct mlx5_lag *ldev, int dev_idx)
{
	struct lag_func *pf;
	unsigned long idx;

	xa_for_each(&ldev->pfs, idx, pf) {
		if (mlx5_get_dev_index(pf->dev) == dev_idx)
			return pf;
	}
	return NULL;
}

/* Find lag_func by mlx5_core_dev pointer */
static inline struct lag_func *
mlx5_lag_pf_by_dev(struct mlx5_lag *ldev, struct mlx5_core_dev *dev)
{
	struct lag_func *pf;
	unsigned long idx;

	xa_for_each(&ldev->pfs, idx, pf) {
		if (pf->dev == dev)
			return pf;
	}
	return NULL;
}

static inline bool
__mlx5_lag_is_sd(struct mlx5_lag *ldev, struct mlx5_core_dev *dev)
{
	struct lag_func *pf = mlx5_lag_pf_by_dev(ldev, dev);

	return pf && pf->group_id != 0;
}

static inline bool
__mlx5_lag_dev_is_port(struct mlx5_lag *ldev, struct mlx5_core_dev *dev)
{
	struct lag_func *pf = mlx5_lag_pf_by_dev(ldev, dev);

	return pf && xa_get_mark(&ldev->pfs, pf->idx, MLX5_LAG_XA_MARK_PORT);
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

#ifdef CONFIG_MLX5_ESWITCH
int mlx5_lag_shared_fdb_create(struct mlx5_lag *ldev,
			       struct lag_tracker *tracker,
			       enum mlx5_lag_mode mode,
			       u32 group_id);
void mlx5_lag_shared_fdb_destroy(struct mlx5_lag *ldev, u32 group_id);
int mlx5_lag_create_vport_lag(struct mlx5_lag *ldev, u32 group_id);
int mlx5_lag_destroy_vport_lag(struct mlx5_lag *ldev, u32 group_id);
int mlx5_lag_create_single_fdb(struct mlx5_lag *ldev);
void mlx5_lag_destroy_single_fdb(struct mlx5_lag *ldev);
bool mlx5_lag_shared_fdb_supported(struct mlx5_lag *ldev);
bool mlx5_lag_shared_fdb_supported_filter(struct mlx5_lag *ldev, u32 filter);
#else
static inline int mlx5_lag_shared_fdb_create(struct mlx5_lag *ldev,
					     struct lag_tracker *tracker,
					     enum mlx5_lag_mode mode,
					     u32 group_id)
{
	return -EOPNOTSUPP;
}

static inline void mlx5_lag_shared_fdb_destroy(struct mlx5_lag *ldev,
					       u32 group_id) {}

static inline int mlx5_lag_create_vport_lag(struct mlx5_lag *ldev,
					    u32 group_id)
{
	return -EOPNOTSUPP;
}

static inline int mlx5_lag_destroy_vport_lag(struct mlx5_lag *ldev,
					     u32 group_id)
{
	return -EOPNOTSUPP;
}

static inline int mlx5_lag_create_single_fdb(struct mlx5_lag *ldev)
{
	return -EOPNOTSUPP;
}

static inline void mlx5_lag_destroy_single_fdb(struct mlx5_lag *ldev) {}
static inline bool mlx5_lag_shared_fdb_supported(struct mlx5_lag *ldev)
{
	return false;
}
#endif
bool mlx5_lag_check_prereq(struct mlx5_lag *ldev);
bool mlx5_lag_is_sd(struct mlx5_core_dev *dev);
int mlx5_lag_demux_init(struct mlx5_core_dev *dev,
			struct mlx5_flow_table_attr *ft_attr);
void mlx5_lag_demux_cleanup(struct mlx5_core_dev *dev);
int mlx5_lag_demux_rule_add(struct mlx5_core_dev *dev, u16 vport_num,
			    int vport_index);
void mlx5_lag_demux_rule_del(struct mlx5_core_dev *dev, int vport_index);
void mlx5_modify_lag(struct mlx5_lag *ldev,
		     struct lag_tracker *tracker);
int mlx5_activate_lag(struct mlx5_lag *ldev,
		      struct lag_tracker *tracker,
		      enum mlx5_lag_mode mode,
		      bool shared_fdb);
int mlx5_lag_dev_get_netdev_idx(struct mlx5_lag *ldev,
				struct net_device *ndev);

char *mlx5_get_str_port_sel_mode(enum mlx5_lag_mode mode, unsigned long flags);
void mlx5_infer_tx_enabled(struct lag_tracker *tracker, struct mlx5_lag *ldev,
			   u8 *ports, int *num_enabled);

void mlx5_ldev_add_debugfs(struct mlx5_core_dev *dev);
void mlx5_ldev_remove_debugfs(struct dentry *dbg);
void mlx5_disable_lag(struct mlx5_lag *ldev);
void mlx5_lag_remove_devices(struct mlx5_lag *ldev);
void mlx5_lag_remove_devices_filter(struct mlx5_lag *ldev, u32 filter);
int mlx5_deactivate_lag(struct mlx5_lag *ldev);
void mlx5_lag_add_devices(struct mlx5_lag *ldev);
void mlx5_lag_rescan_dev_locked(struct mlx5_lag *ldev,
				struct mlx5_core_dev *dev,
				bool enable);
void mlx5_lag_add_devices_filter(struct mlx5_lag *ldev, u32 filter);
struct mlx5_devcom_comp_dev *mlx5_lag_get_devcom_comp(struct mlx5_lag *ldev);

#ifdef CONFIG_MLX5_ESWITCH
void mlx5_lag_set_vports_agg_speed(struct mlx5_lag *ldev);
void mlx5_lag_reset_vports_speed(struct mlx5_lag *ldev);
#else
static inline void mlx5_lag_set_vports_agg_speed(struct mlx5_lag *ldev) {}
static inline void mlx5_lag_reset_vports_speed(struct mlx5_lag *ldev) {}
#endif

static inline bool mlx5_lag_is_supported(struct mlx5_core_dev *dev)
{
	if (!MLX5_CAP_GEN(dev, vport_group_manager) ||
	    !MLX5_CAP_GEN(dev, lag_master) ||
	    MLX5_CAP_GEN(dev, num_lag_ports) < 2 ||
	    mlx5_get_dev_index(dev) >= MLX5_MAX_PORTS ||
	    MLX5_CAP_GEN(dev, num_lag_ports) > MLX5_MAX_PORTS)
		return false;
	return true;
}

/* Iterator filter constants for mlx5_lag_for_each() */
#define MLX5_LAG_FILTER_PORTS 0        /* iterate ports only (XA_MARK_PORT) */
#define MLX5_LAG_FILTER_ALL   U32_MAX  /* iterate ALL devices */
/* any other value = iterate devices with that specific group_id */

#define mlx5_lag_for_each(i, start_index, ldev, filter) \
	for (int tmp = start_index; \
	     tmp = mlx5_get_next_lag_func(ldev, tmp, filter), \
	     i = tmp, tmp < MLX5_MAX_PORTS; tmp++)

#define mlx5_lag_for_each_reverse(i, start_index, end_index, ldev, filter) \
	for (int tmp = start_index, tmp1 = end_index; \
	     tmp = mlx5_get_pre_lag_func(ldev, tmp, tmp1, filter), \
	     i = tmp, tmp >= tmp1; tmp--)

/* Convenience wrappers - keeps existing behavior */
#define mlx5_ldev_for_each(i, start_index, ldev) \
	mlx5_lag_for_each(i, start_index, ldev, MLX5_LAG_FILTER_PORTS)

#define mlx5_ldev_for_each_reverse(i, start_index, end_index, ldev) \
	mlx5_lag_for_each_reverse(i, start_index, end_index, ldev, \
				  MLX5_LAG_FILTER_PORTS)

int mlx5_get_pre_lag_func(struct mlx5_lag *ldev, int start_idx, int end_idx,
			  u32 filter);
int mlx5_get_next_lag_func(struct mlx5_lag *ldev, int start_idx, u32 filter);
int mlx5_lag_get_dev_index_by_seq(struct mlx5_lag *ldev, int seq);
int mlx5_lag_get_dev_index_by_seq_filter(struct mlx5_lag *ldev, int seq,
					 u32 filter);
int mlx5_lag_num_devs(struct mlx5_lag *ldev);
int mlx5_lag_num_netdevs(struct mlx5_lag *ldev);
int mlx5_lag_reload_ib_reps_from_locked(struct mlx5_lag *ldev, u32 flags,
					u32 filter, bool cont_on_fail);
void mlx5_lag_unload_reps_from_locked(struct mlx5_lag *ldev, u32 filter);
int mlx5_ldev_add_mdev(struct mlx5_lag *ldev, struct mlx5_core_dev *dev,
		       u32 group_id);
void mlx5_ldev_remove_mdev(struct mlx5_lag *ldev, struct mlx5_core_dev *dev);
#endif /* __MLX5_LAG_H__ */
