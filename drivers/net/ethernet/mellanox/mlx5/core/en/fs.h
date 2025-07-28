/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#ifndef __MLX5E_FLOW_STEER_H__
#define __MLX5E_FLOW_STEER_H__

#include "mod_hdr.h"
#include "lib/fs_ttc.h"

struct mlx5e_post_act;
struct mlx5e_tc_table;

enum {
	MLX5E_TC_FT_LEVEL = 0,
	MLX5E_TC_TTC_FT_LEVEL,
	MLX5E_TC_MISS_LEVEL,
};

enum {
	MLX5E_TC_PRIO = 0,
	MLX5E_PROMISC_PRIO,
	MLX5E_NIC_PRIO,
};

struct mlx5e_flow_table {
	int num_groups;
	struct mlx5_flow_table *t;
	struct mlx5_flow_group **g;
};

struct mlx5e_l2_rule {
	u8  addr[ETH_ALEN + 2];
	struct mlx5_flow_handle *rule;
};

#define MLX5E_L2_ADDR_HASH_SIZE BIT(BITS_PER_BYTE)

struct mlx5e_promisc_table {
	struct mlx5e_flow_table	ft;
	struct mlx5_flow_handle	*rule;
};

/* Forward declaration and APIs to get private fields of vlan_table */
struct mlx5e_vlan_table;
unsigned long *mlx5e_vlan_get_active_svlans(struct mlx5e_vlan_table *vlan);
struct mlx5_flow_table *mlx5e_vlan_get_flowtable(struct mlx5e_vlan_table *vlan);

struct mlx5e_l2_table {
	struct mlx5e_flow_table    ft;
	struct hlist_head          netdev_uc[MLX5E_L2_ADDR_HASH_SIZE];
	struct hlist_head          netdev_mc[MLX5E_L2_ADDR_HASH_SIZE];
	struct mlx5e_l2_rule	   broadcast;
	struct mlx5e_l2_rule	   allmulti;
	struct mlx5_flow_handle    *trap_rule;
	bool                       broadcast_enabled;
	bool                       allmulti_enabled;
	bool                       promisc_enabled;
};

#define MLX5E_NUM_INDIR_TIRS (MLX5_NUM_TT - 1)

#define MLX5_HASH_IP		(MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP)
#define MLX5_HASH_IP_L4PORTS	(MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP   |\
				 MLX5_HASH_FIELD_SEL_L4_SPORT |\
				 MLX5_HASH_FIELD_SEL_L4_DPORT)
#define MLX5_HASH_IP_IPSEC_SPI	(MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP   |\
				 MLX5_HASH_FIELD_SEL_IPSEC_SPI)

/* NIC promisc FT level */
enum {
	MLX5E_PROMISC_FT_LEVEL,
};

/* NIC prio FTS */
enum {
	MLX5E_VLAN_FT_LEVEL,
	MLX5E_L2_FT_LEVEL,
	MLX5E_TTC_FT_LEVEL,
	MLX5E_INNER_TTC_FT_LEVEL,
	MLX5E_FS_TT_UDP_FT_LEVEL = MLX5E_INNER_TTC_FT_LEVEL + 1,
	MLX5E_FS_TT_ANY_FT_LEVEL = MLX5E_INNER_TTC_FT_LEVEL + 1,
#ifdef CONFIG_MLX5_EN_TLS
	MLX5E_ACCEL_FS_TCP_FT_LEVEL = MLX5E_INNER_TTC_FT_LEVEL + 1,
#endif
#ifdef CONFIG_MLX5_EN_ARFS
	MLX5E_ARFS_FT_LEVEL = MLX5E_INNER_TTC_FT_LEVEL + 1,
#endif
#ifdef CONFIG_MLX5_EN_IPSEC
	MLX5E_ACCEL_FS_ESP_FT_LEVEL = MLX5E_INNER_TTC_FT_LEVEL + 1,
	MLX5E_ACCEL_FS_ESP_FT_ERR_LEVEL,
	MLX5E_ACCEL_FS_POL_FT_LEVEL,
	MLX5E_ACCEL_FS_ESP_FT_ROCE_LEVEL,
#endif
};

struct mlx5e_flow_steering;
struct mlx5e_rx_res;

#ifdef CONFIG_MLX5_EN_ARFS
struct mlx5e_arfs_tables;

int mlx5e_arfs_create_tables(struct mlx5e_flow_steering *fs,
			     struct mlx5e_rx_res *rx_res, bool ntuple);
void mlx5e_arfs_destroy_tables(struct mlx5e_flow_steering *fs, bool ntuple);
int mlx5e_arfs_enable(struct mlx5e_flow_steering *fs);
int mlx5e_arfs_disable(struct mlx5e_flow_steering *fs);
int mlx5e_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			u16 rxq_index, u32 flow_id);
#else
static inline int mlx5e_arfs_create_tables(struct mlx5e_flow_steering *fs,
					   struct mlx5e_rx_res *rx_res, bool ntuple)
{ return 0; }
static inline void mlx5e_arfs_destroy_tables(struct mlx5e_flow_steering *fs, bool ntuple) {}
static inline int mlx5e_arfs_enable(struct mlx5e_flow_steering *fs)
{ return -EOPNOTSUPP; }
static inline int mlx5e_arfs_disable(struct mlx5e_flow_steering *fs)
{ return -EOPNOTSUPP; }
#endif

#ifdef CONFIG_MLX5_EN_TLS
struct mlx5e_accel_fs_tcp;
#endif

struct mlx5e_profile;
struct mlx5e_fs_udp;
struct mlx5e_fs_any;
struct mlx5e_ptp_fs;

void mlx5e_set_ttc_params(struct mlx5e_flow_steering *fs,
			  struct mlx5e_rx_res *rx_res,
			  struct ttc_params *ttc_params, bool tunnel);

void mlx5e_destroy_ttc_table(struct mlx5e_flow_steering *fs);
int mlx5e_create_ttc_table(struct mlx5e_flow_steering  *fs,
			   struct mlx5e_rx_res *rx_res);

void mlx5e_destroy_flow_table(struct mlx5e_flow_table *ft);

void mlx5e_enable_cvlan_filter(struct mlx5e_flow_steering *fs, bool promisc);
void mlx5e_disable_cvlan_filter(struct mlx5e_flow_steering *fs, bool promisc);

int mlx5e_create_flow_steering(struct mlx5e_flow_steering *fs,
			       struct mlx5e_rx_res *rx_res,
			       const struct mlx5e_profile *profile,
			       struct net_device *netdev);
void mlx5e_destroy_flow_steering(struct mlx5e_flow_steering *fs, bool ntuple,
				 const struct mlx5e_profile *profile);

struct mlx5e_flow_steering *mlx5e_fs_init(const struct mlx5e_profile *profile,
					  struct mlx5_core_dev *mdev,
					  bool state_destroy,
					  struct dentry *dfs_root);
void mlx5e_fs_cleanup(struct mlx5e_flow_steering *fs);
struct mlx5e_vlan_table *mlx5e_fs_get_vlan(struct mlx5e_flow_steering *fs);
struct mlx5e_tc_table *mlx5e_fs_get_tc(struct mlx5e_flow_steering *fs);
struct mlx5e_l2_table *mlx5e_fs_get_l2(struct mlx5e_flow_steering *fs);
struct mlx5_flow_namespace *mlx5e_fs_get_ns(struct mlx5e_flow_steering *fs, bool egress);
void mlx5e_fs_set_ns(struct mlx5e_flow_steering *fs, struct mlx5_flow_namespace *ns, bool egress);

static inline bool mlx5e_fs_has_arfs(struct net_device *netdev)
{
	return IS_ENABLED(CONFIG_MLX5_EN_ARFS) &&
		netdev->hw_features & NETIF_F_NTUPLE;
}

static inline bool mlx5e_fs_want_arfs(struct net_device *netdev)
{
	return IS_ENABLED(CONFIG_MLX5_EN_ARFS) &&
		netdev->features & NETIF_F_NTUPLE;
}

#ifdef CONFIG_MLX5_EN_RXNFC
struct mlx5e_ethtool_steering *mlx5e_fs_get_ethtool(struct mlx5e_flow_steering *fs);
#endif
struct mlx5_ttc_table *mlx5e_fs_get_ttc(struct mlx5e_flow_steering *fs, bool inner);
void mlx5e_fs_set_ttc(struct mlx5e_flow_steering *fs, struct mlx5_ttc_table *ttc, bool inner);
#ifdef CONFIG_MLX5_EN_ARFS
struct mlx5e_arfs_tables *mlx5e_fs_get_arfs(struct mlx5e_flow_steering *fs);
void mlx5e_fs_set_arfs(struct mlx5e_flow_steering *fs, struct mlx5e_arfs_tables *arfs);
#endif
struct mlx5e_ptp_fs *mlx5e_fs_get_ptp(struct mlx5e_flow_steering *fs);
void mlx5e_fs_set_ptp(struct mlx5e_flow_steering *fs, struct mlx5e_ptp_fs *ptp_fs);
struct mlx5e_fs_any *mlx5e_fs_get_any(struct mlx5e_flow_steering *fs);
void mlx5e_fs_set_any(struct mlx5e_flow_steering *fs, struct mlx5e_fs_any *any);
struct mlx5e_fs_udp *mlx5e_fs_get_udp(struct mlx5e_flow_steering *fs);
void mlx5e_fs_set_udp(struct mlx5e_flow_steering *fs, struct mlx5e_fs_udp *udp);
#ifdef CONFIG_MLX5_EN_TLS
struct mlx5e_accel_fs_tcp *mlx5e_fs_get_accel_tcp(struct mlx5e_flow_steering *fs);
void mlx5e_fs_set_accel_tcp(struct mlx5e_flow_steering *fs, struct mlx5e_accel_fs_tcp *accel_tcp);
#endif
void mlx5e_fs_set_state_destroy(struct mlx5e_flow_steering *fs, bool state_destroy);
void mlx5e_fs_set_vlan_strip_disable(struct mlx5e_flow_steering *fs, bool vlan_strip_disable);

struct mlx5_core_dev *mlx5e_fs_get_mdev(struct mlx5e_flow_steering *fs);
int mlx5e_add_vlan_trap(struct mlx5e_flow_steering *fs, int  trap_id, int tir_num);
void mlx5e_remove_vlan_trap(struct mlx5e_flow_steering *fs);
int mlx5e_add_mac_trap(struct mlx5e_flow_steering *fs, int  trap_id, int tir_num);
void mlx5e_remove_mac_trap(struct mlx5e_flow_steering *fs);
void mlx5e_fs_set_rx_mode_work(struct mlx5e_flow_steering *fs, struct net_device *netdev);
int mlx5e_fs_vlan_rx_add_vid(struct mlx5e_flow_steering *fs,
			     struct net_device *netdev,
			     __be16 proto, u16 vid);
int mlx5e_fs_vlan_rx_kill_vid(struct mlx5e_flow_steering *fs,
			      struct net_device *netdev,
			      __be16 proto, u16 vid);
void mlx5e_fs_init_l2_addr(struct mlx5e_flow_steering *fs, struct net_device *netdev);

struct dentry *mlx5e_fs_get_debugfs_root(struct mlx5e_flow_steering *fs);

#define fs_err(fs, fmt, ...) \
	mlx5_core_err(mlx5e_fs_get_mdev(fs), fmt, ##__VA_ARGS__)

#define fs_dbg(fs, fmt, ...) \
	mlx5_core_dbg(mlx5e_fs_get_mdev(fs), fmt, ##__VA_ARGS__)

#define fs_warn(fs, fmt, ...) \
	mlx5_core_warn(mlx5e_fs_get_mdev(fs), fmt, ##__VA_ARGS__)

#define fs_warn_once(fs, fmt, ...) \
	mlx5_core_warn_once(mlx5e_fs_get_mdev(fs), fmt, ##__VA_ARGS__)

#endif /* __MLX5E_FLOW_STEER_H__ */

