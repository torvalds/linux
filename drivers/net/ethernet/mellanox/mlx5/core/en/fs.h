/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#ifndef __MLX5E_FLOW_STEER_H__
#define __MLX5E_FLOW_STEER_H__

#include "mod_hdr.h"

enum {
	MLX5E_TC_FT_LEVEL = 0,
	MLX5E_TC_TTC_FT_LEVEL,
};

struct mlx5e_tc_table {
	/* Protects the dynamic assignment of the t parameter
	 * which is the nic tc root table.
	 */
	struct mutex			t_lock;
	struct mlx5_flow_table		*t;
	struct mlx5_fs_chains           *chains;

	struct rhashtable               ht;

	struct mod_hdr_tbl mod_hdr;
	struct mutex hairpin_tbl_lock; /* protects hairpin_tbl */
	DECLARE_HASHTABLE(hairpin_tbl, 8);

	struct notifier_block     netdevice_nb;
	struct netdev_net_notifier	netdevice_nn;

	struct mlx5_tc_ct_priv         *ct;
	struct mapping_ctx             *mapping;
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

enum mlx5e_traffic_types {
	MLX5E_TT_IPV4_TCP,
	MLX5E_TT_IPV6_TCP,
	MLX5E_TT_IPV4_UDP,
	MLX5E_TT_IPV6_UDP,
	MLX5E_TT_IPV4_IPSEC_AH,
	MLX5E_TT_IPV6_IPSEC_AH,
	MLX5E_TT_IPV4_IPSEC_ESP,
	MLX5E_TT_IPV6_IPSEC_ESP,
	MLX5E_TT_IPV4,
	MLX5E_TT_IPV6,
	MLX5E_TT_ANY,
	MLX5E_NUM_TT,
	MLX5E_NUM_INDIR_TIRS = MLX5E_TT_ANY,
};

struct mlx5e_tirc_config {
	u8 l3_prot_type;
	u8 l4_prot_type;
	u32 rx_hash_fields;
};

#define MLX5_HASH_IP		(MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP)
#define MLX5_HASH_IP_L4PORTS	(MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP   |\
				 MLX5_HASH_FIELD_SEL_L4_SPORT |\
				 MLX5_HASH_FIELD_SEL_L4_DPORT)
#define MLX5_HASH_IP_IPSEC_SPI	(MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP   |\
				 MLX5_HASH_FIELD_SEL_IPSEC_SPI)

enum mlx5e_tunnel_types {
	MLX5E_TT_IPV4_GRE,
	MLX5E_TT_IPV6_GRE,
	MLX5E_TT_IPV4_IPIP,
	MLX5E_TT_IPV6_IPIP,
	MLX5E_TT_IPV4_IPV6,
	MLX5E_TT_IPV6_IPV6,
	MLX5E_NUM_TUNNEL_TT,
};

bool mlx5e_tunnel_inner_ft_supported(struct mlx5_core_dev *mdev);

struct mlx5e_ttc_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_destination default_dest;
};

/* L3/L4 traffic type classifier */
struct mlx5e_ttc_table {
	struct mlx5e_flow_table ft;
	struct mlx5e_ttc_rule rules[MLX5E_NUM_TT];
	struct mlx5_flow_handle *tunnel_rules[MLX5E_NUM_TUNNEL_TT];
};

/* NIC prio FTS */
enum {
	MLX5E_PROMISC_FT_LEVEL,
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
#endif
};

#define MLX5E_TTC_NUM_GROUPS	3
#define MLX5E_TTC_GROUP1_SIZE	(BIT(3) + MLX5E_NUM_TUNNEL_TT)
#define MLX5E_TTC_GROUP2_SIZE	 BIT(1)
#define MLX5E_TTC_GROUP3_SIZE	 BIT(0)
#define MLX5E_TTC_TABLE_SIZE	(MLX5E_TTC_GROUP1_SIZE +\
				 MLX5E_TTC_GROUP2_SIZE +\
				 MLX5E_TTC_GROUP3_SIZE)

#define MLX5E_INNER_TTC_NUM_GROUPS	3
#define MLX5E_INNER_TTC_GROUP1_SIZE	BIT(3)
#define MLX5E_INNER_TTC_GROUP2_SIZE	BIT(1)
#define MLX5E_INNER_TTC_GROUP3_SIZE	BIT(0)
#define MLX5E_INNER_TTC_TABLE_SIZE	(MLX5E_INNER_TTC_GROUP1_SIZE +\
					 MLX5E_INNER_TTC_GROUP2_SIZE +\
					 MLX5E_INNER_TTC_GROUP3_SIZE)

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
int mlx5e_ethtool_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd);
int mlx5e_ethtool_get_rxnfc(struct net_device *dev,
			    struct ethtool_rxnfc *info, u32 *rule_locs);
#else
static inline void mlx5e_ethtool_init_steering(struct mlx5e_priv *priv)    { }
static inline void mlx5e_ethtool_cleanup_steering(struct mlx5e_priv *priv) { }
static inline int mlx5e_ethtool_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{ return -EOPNOTSUPP; }
static inline int mlx5e_ethtool_get_rxnfc(struct net_device *dev,
					  struct ethtool_rxnfc *info, u32 *rule_locs)
{ return -EOPNOTSUPP; }
#endif /* CONFIG_MLX5_EN_RXNFC */

#ifdef CONFIG_MLX5_EN_ARFS
struct mlx5e_arfs_tables;

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

#ifdef CONFIG_MLX5_EN_TLS
struct mlx5e_accel_fs_tcp;
#endif

struct mlx5e_fs_udp;
struct mlx5e_fs_any;
struct mlx5e_ptp_fs;

struct mlx5e_flow_steering {
	struct mlx5_flow_namespace      *ns;
	struct mlx5_flow_namespace      *egress_ns;
#ifdef CONFIG_MLX5_EN_RXNFC
	struct mlx5e_ethtool_steering   ethtool;
#endif
	struct mlx5e_tc_table           tc;
	struct mlx5e_promisc_table      promisc;
	struct mlx5e_vlan_table         *vlan;
	struct mlx5e_l2_table           l2;
	struct mlx5e_ttc_table          ttc;
	struct mlx5e_ttc_table          inner_ttc;
#ifdef CONFIG_MLX5_EN_ARFS
	struct mlx5e_arfs_tables       *arfs;
#endif
#ifdef CONFIG_MLX5_EN_TLS
	struct mlx5e_accel_fs_tcp      *accel_tcp;
#endif
	struct mlx5e_fs_udp            *udp;
	struct mlx5e_fs_any            *any;
	struct mlx5e_ptp_fs            *ptp_fs;
};

struct ttc_params {
	struct mlx5_flow_table_attr ft_attr;
	u32 any_tt_tirn;
	u32 indir_tirn[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_ttc_table *inner_ttc;
};

void mlx5e_set_ttc_basic_params(struct mlx5e_priv *priv, struct ttc_params *ttc_params);
void mlx5e_set_ttc_ft_params(struct ttc_params *ttc_params);
void mlx5e_set_inner_ttc_ft_params(struct ttc_params *ttc_params);

int mlx5e_create_ttc_table(struct mlx5e_priv *priv, struct ttc_params *params,
			   struct mlx5e_ttc_table *ttc);
void mlx5e_destroy_ttc_table(struct mlx5e_priv *priv,
			     struct mlx5e_ttc_table *ttc);

int mlx5e_create_inner_ttc_table(struct mlx5e_priv *priv, struct ttc_params *params,
				 struct mlx5e_ttc_table *ttc);
void mlx5e_destroy_inner_ttc_table(struct mlx5e_priv *priv,
				   struct mlx5e_ttc_table *ttc);

void mlx5e_destroy_flow_table(struct mlx5e_flow_table *ft);
int mlx5e_ttc_fwd_dest(struct mlx5e_priv *priv, enum mlx5e_traffic_types type,
		       struct mlx5_flow_destination *new_dest);
struct mlx5_flow_destination
mlx5e_ttc_get_default_dest(struct mlx5e_priv *priv, enum mlx5e_traffic_types type);
int mlx5e_ttc_fwd_default_dest(struct mlx5e_priv *priv, enum mlx5e_traffic_types type);

void mlx5e_enable_cvlan_filter(struct mlx5e_priv *priv);
void mlx5e_disable_cvlan_filter(struct mlx5e_priv *priv);

int mlx5e_create_flow_steering(struct mlx5e_priv *priv);
void mlx5e_destroy_flow_steering(struct mlx5e_priv *priv);

u8 mlx5e_get_proto_by_tunnel_type(enum mlx5e_tunnel_types tt);
int mlx5e_add_vlan_trap(struct mlx5e_priv *priv, int  trap_id, int tir_num);
void mlx5e_remove_vlan_trap(struct mlx5e_priv *priv);
int mlx5e_add_mac_trap(struct mlx5e_priv *priv, int  trap_id, int tir_num);
void mlx5e_remove_mac_trap(struct mlx5e_priv *priv);

#endif /* __MLX5E_FLOW_STEER_H__ */

