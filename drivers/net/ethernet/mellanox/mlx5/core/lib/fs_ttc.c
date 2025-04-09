// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES.

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "lib/fs_ttc.h"

#define MLX5_TTC_MAX_NUM_GROUPS		4
#define MLX5_TTC_GROUP_TCPUDP_SIZE	(MLX5_TT_IPV6_UDP + 1)

struct mlx5_fs_ttc_groups {
	bool use_l4_type;
	int num_groups;
	int group_size[MLX5_TTC_MAX_NUM_GROUPS];
};

static int mlx5_fs_ttc_table_size(const struct mlx5_fs_ttc_groups *groups)
{
	int i, sz = 0;

	for (i = 0; i < groups->num_groups; i++)
		sz += groups->group_size[i];

	return sz;
}

/* L3/L4 traffic type classifier */
struct mlx5_ttc_table {
	int num_groups;
	struct mlx5_flow_table *t;
	struct mlx5_flow_group **g;
	struct mlx5_ttc_rule rules[MLX5_NUM_TT];
	struct mlx5_flow_handle *tunnel_rules[MLX5_NUM_TUNNEL_TT];
};

struct mlx5_flow_table *mlx5_get_ttc_flow_table(struct mlx5_ttc_table *ttc)
{
	return ttc->t;
}

static void mlx5_cleanup_ttc_rules(struct mlx5_ttc_table *ttc)
{
	int i;

	for (i = 0; i < MLX5_NUM_TT; i++) {
		if (!IS_ERR_OR_NULL(ttc->rules[i].rule)) {
			mlx5_del_flow_rules(ttc->rules[i].rule);
			ttc->rules[i].rule = NULL;
		}
	}

	for (i = 0; i < MLX5_NUM_TUNNEL_TT; i++) {
		if (!IS_ERR_OR_NULL(ttc->tunnel_rules[i])) {
			mlx5_del_flow_rules(ttc->tunnel_rules[i]);
			ttc->tunnel_rules[i] = NULL;
		}
	}
}

static const char *mlx5_traffic_types_names[MLX5_NUM_TT] = {
	[MLX5_TT_IPV4_TCP] =  "TT_IPV4_TCP",
	[MLX5_TT_IPV6_TCP] =  "TT_IPV6_TCP",
	[MLX5_TT_IPV4_UDP] =  "TT_IPV4_UDP",
	[MLX5_TT_IPV6_UDP] =  "TT_IPV6_UDP",
	[MLX5_TT_IPV4_IPSEC_AH] = "TT_IPV4_IPSEC_AH",
	[MLX5_TT_IPV6_IPSEC_AH] = "TT_IPV6_IPSEC_AH",
	[MLX5_TT_IPV4_IPSEC_ESP] = "TT_IPV4_IPSEC_ESP",
	[MLX5_TT_IPV6_IPSEC_ESP] = "TT_IPV6_IPSEC_ESP",
	[MLX5_TT_IPV4] = "TT_IPV4",
	[MLX5_TT_IPV6] = "TT_IPV6",
	[MLX5_TT_ANY] = "TT_ANY"
};

const char *mlx5_ttc_get_name(enum mlx5_traffic_types tt)
{
	return mlx5_traffic_types_names[tt];
}

struct mlx5_etype_proto {
	u16 etype;
	u8 proto;
};

static struct mlx5_etype_proto ttc_rules[] = {
	[MLX5_TT_IPV4_TCP] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_TCP,
	},
	[MLX5_TT_IPV6_TCP] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_TCP,
	},
	[MLX5_TT_IPV4_UDP] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_UDP,
	},
	[MLX5_TT_IPV6_UDP] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_UDP,
	},
	[MLX5_TT_IPV4_IPSEC_AH] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_AH,
	},
	[MLX5_TT_IPV6_IPSEC_AH] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_AH,
	},
	[MLX5_TT_IPV4_IPSEC_ESP] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_ESP,
	},
	[MLX5_TT_IPV6_IPSEC_ESP] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_ESP,
	},
	[MLX5_TT_IPV4] = {
		.etype = ETH_P_IP,
		.proto = 0,
	},
	[MLX5_TT_IPV6] = {
		.etype = ETH_P_IPV6,
		.proto = 0,
	},
	[MLX5_TT_ANY] = {
		.etype = 0,
		.proto = 0,
	},
};

static struct mlx5_etype_proto ttc_tunnel_rules[] = {
	[MLX5_TT_IPV4_GRE] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_GRE,
	},
	[MLX5_TT_IPV6_GRE] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_GRE,
	},
	[MLX5_TT_IPV4_IPIP] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_IPIP,
	},
	[MLX5_TT_IPV6_IPIP] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_IPIP,
	},
	[MLX5_TT_IPV4_IPV6] = {
		.etype = ETH_P_IP,
		.proto = IPPROTO_IPV6,
	},
	[MLX5_TT_IPV6_IPV6] = {
		.etype = ETH_P_IPV6,
		.proto = IPPROTO_IPV6,
	},

};

enum TTC_GROUP_TYPE {
	TTC_GROUPS_DEFAULT = 0,
	TTC_GROUPS_USE_L4_TYPE = 1,
};

static const struct mlx5_fs_ttc_groups ttc_groups[] = {
	[TTC_GROUPS_DEFAULT] = {
		.num_groups = 3,
		.group_size = {
			BIT(3) + MLX5_NUM_TUNNEL_TT,
			BIT(1),
			BIT(0),
		},
	},
	[TTC_GROUPS_USE_L4_TYPE] = {
		.use_l4_type = true,
		.num_groups = 4,
		.group_size = {
			MLX5_TTC_GROUP_TCPUDP_SIZE,
			BIT(3) + MLX5_NUM_TUNNEL_TT - MLX5_TTC_GROUP_TCPUDP_SIZE,
			BIT(1),
			BIT(0),
		},
	},
};

static const struct mlx5_fs_ttc_groups inner_ttc_groups[] = {
	[TTC_GROUPS_DEFAULT] = {
		.num_groups = 3,
		.group_size = {
			BIT(3),
			BIT(1),
			BIT(0),
		},
	},
	[TTC_GROUPS_USE_L4_TYPE] = {
		.use_l4_type = true,
		.num_groups = 4,
		.group_size = {
			MLX5_TTC_GROUP_TCPUDP_SIZE,
			BIT(3) - MLX5_TTC_GROUP_TCPUDP_SIZE,
			BIT(1),
			BIT(0),
		},
	},
};

u8 mlx5_get_proto_by_tunnel_type(enum mlx5_tunnel_types tt)
{
	return ttc_tunnel_rules[tt].proto;
}

static bool mlx5_tunnel_proto_supported_rx(struct mlx5_core_dev *mdev,
					   u8 proto_type)
{
	switch (proto_type) {
	case IPPROTO_GRE:
		return MLX5_CAP_ETH(mdev, tunnel_stateless_gre);
	case IPPROTO_IPIP:
	case IPPROTO_IPV6:
		return (MLX5_CAP_ETH(mdev, tunnel_stateless_ip_over_ip) ||
			MLX5_CAP_ETH(mdev, tunnel_stateless_ip_over_ip_rx));
	default:
		return false;
	}
}

static bool mlx5_tunnel_any_rx_proto_supported(struct mlx5_core_dev *mdev)
{
	int tt;

	for (tt = 0; tt < MLX5_NUM_TUNNEL_TT; tt++) {
		if (mlx5_tunnel_proto_supported_rx(mdev,
						   ttc_tunnel_rules[tt].proto))
			return true;
	}
	return false;
}

bool mlx5_tunnel_inner_ft_supported(struct mlx5_core_dev *mdev)
{
	return (mlx5_tunnel_any_rx_proto_supported(mdev) &&
		MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					  ft_field_support.inner_ip_version));
}

static u8 mlx5_etype_to_ipv(u16 ethertype)
{
	if (ethertype == ETH_P_IP)
		return 4;

	if (ethertype == ETH_P_IPV6)
		return 6;

	return 0;
}

static void mlx5_fs_ttc_set_match_proto(void *headers_c, void *headers_v,
					u8 proto, bool use_l4_type)
{
	int l4_type;

	if (use_l4_type && (proto == IPPROTO_TCP || proto == IPPROTO_UDP)) {
		if (proto == IPPROTO_TCP)
			l4_type = MLX5_PACKET_L4_TYPE_TCP;
		else
			l4_type = MLX5_PACKET_L4_TYPE_UDP;

		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, l4_type);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, l4_type, l4_type);
	} else {
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol, proto);
	}
}

static struct mlx5_flow_handle *
mlx5_generate_ttc_rule(struct mlx5_core_dev *dev, struct mlx5_flow_table *ft,
		       struct mlx5_flow_destination *dest, u16 etype, u8 proto,
		       bool use_l4_type)
{
	int match_ipv_outer =
		MLX5_CAP_FLOWTABLE_NIC_RX(dev,
					  ft_field_support.outer_ip_version);
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;
	u8 ipv;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	if (proto) {
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		mlx5_fs_ttc_set_match_proto(MLX5_ADDR_OF(fte_match_param,
							 spec->match_criteria,
							 outer_headers),
					    MLX5_ADDR_OF(fte_match_param,
							 spec->match_value,
							 outer_headers),
					    proto, use_l4_type);
	}

	ipv = mlx5_etype_to_ipv(etype);
	if (match_ipv_outer && ipv) {
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, ipv);
	} else if (etype) {
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ethertype);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.ethertype, etype);
	}

	rule = mlx5_add_flow_rules(ft, spec, &flow_act, dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(dev, "%s: add rule failed\n", __func__);
	}

	kvfree(spec);
	return err ? ERR_PTR(err) : rule;
}

static int mlx5_generate_ttc_table_rules(struct mlx5_core_dev *dev,
					 struct ttc_params *params,
					 struct mlx5_ttc_table *ttc,
					 bool use_l4_type)
{
	struct mlx5_flow_handle **trules;
	struct mlx5_ttc_rule *rules;
	struct mlx5_flow_table *ft;
	int tt;
	int err;

	ft = ttc->t;
	rules = ttc->rules;
	for (tt = 0; tt < MLX5_NUM_TT; tt++) {
		struct mlx5_ttc_rule *rule = &rules[tt];

		if (test_bit(tt, params->ignore_dests))
			continue;
		rule->rule = mlx5_generate_ttc_rule(dev, ft, &params->dests[tt],
						    ttc_rules[tt].etype,
						    ttc_rules[tt].proto,
						    use_l4_type);
		if (IS_ERR(rule->rule)) {
			err = PTR_ERR(rule->rule);
			rule->rule = NULL;
			goto del_rules;
		}
		rule->default_dest = params->dests[tt];
	}

	if (!params->inner_ttc || !mlx5_tunnel_inner_ft_supported(dev))
		return 0;

	trules    = ttc->tunnel_rules;
	for (tt = 0; tt < MLX5_NUM_TUNNEL_TT; tt++) {
		if (!mlx5_tunnel_proto_supported_rx(dev,
						    ttc_tunnel_rules[tt].proto))
			continue;
		if (test_bit(tt, params->ignore_tunnel_dests))
			continue;
		trules[tt] = mlx5_generate_ttc_rule(dev, ft,
						    &params->tunnel_dests[tt],
						    ttc_tunnel_rules[tt].etype,
						    ttc_tunnel_rules[tt].proto,
						    use_l4_type);
		if (IS_ERR(trules[tt])) {
			err = PTR_ERR(trules[tt]);
			trules[tt] = NULL;
			goto del_rules;
		}
	}

	return 0;

del_rules:
	mlx5_cleanup_ttc_rules(ttc);
	return err;
}

static int mlx5_create_ttc_table_groups(struct mlx5_ttc_table *ttc,
					bool use_ipv,
					const struct mlx5_fs_ttc_groups *groups)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ttc->g = kcalloc(groups->num_groups, sizeof(*ttc->g), GFP_KERNEL);
	if (!ttc->g)
		return -ENOMEM;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		kfree(ttc->g);
		ttc->g = NULL;
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	if (use_ipv)
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_version);
	else
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);

	/* TCP UDP group */
	if (groups->use_l4_type) {
		MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.l4_type);
		MLX5_SET_CFG(in, start_flow_index, ix);
		ix += groups->group_size[ttc->num_groups];
		MLX5_SET_CFG(in, end_flow_index, ix - 1);
		ttc->g[ttc->num_groups] = mlx5_create_flow_group(ttc->t, in);
		if (IS_ERR(ttc->g[ttc->num_groups]))
			goto err;
		ttc->num_groups++;

		MLX5_SET(fte_match_param, mc, outer_headers.l4_type, 0);
	}

	/* L4 Group */
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ip_protocol);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += groups->group_size[ttc->num_groups];
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ttc->g[ttc->num_groups] = mlx5_create_flow_group(ttc->t, in);
	if (IS_ERR(ttc->g[ttc->num_groups]))
		goto err;
	ttc->num_groups++;

	/* L3 Group */
	MLX5_SET(fte_match_param, mc, outer_headers.ip_protocol, 0);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += groups->group_size[ttc->num_groups];
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ttc->g[ttc->num_groups] = mlx5_create_flow_group(ttc->t, in);
	if (IS_ERR(ttc->g[ttc->num_groups]))
		goto err;
	ttc->num_groups++;

	/* Any Group */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += groups->group_size[ttc->num_groups];
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ttc->g[ttc->num_groups] = mlx5_create_flow_group(ttc->t, in);
	if (IS_ERR(ttc->g[ttc->num_groups]))
		goto err;
	ttc->num_groups++;

	kvfree(in);
	return 0;

err:
	err = PTR_ERR(ttc->g[ttc->num_groups]);
	ttc->g[ttc->num_groups] = NULL;
	kvfree(in);

	return err;
}

static struct mlx5_flow_handle *
mlx5_generate_inner_ttc_rule(struct mlx5_core_dev *dev,
			     struct mlx5_flow_table *ft,
			     struct mlx5_flow_destination *dest,
			     u16 etype, u8 proto, bool use_l4_type)
{
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;
	u8 ipv;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	ipv = mlx5_etype_to_ipv(etype);
	if (etype && ipv) {
		spec->match_criteria_enable = MLX5_MATCH_INNER_HEADERS;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, inner_headers.ip_version);
		MLX5_SET(fte_match_param, spec->match_value, inner_headers.ip_version, ipv);
	}

	if (proto) {
		spec->match_criteria_enable = MLX5_MATCH_INNER_HEADERS;
		mlx5_fs_ttc_set_match_proto(MLX5_ADDR_OF(fte_match_param,
							 spec->match_criteria,
							 inner_headers),
					    MLX5_ADDR_OF(fte_match_param,
							 spec->match_value,
							 inner_headers),
					    proto, use_l4_type);
	}

	rule = mlx5_add_flow_rules(ft, spec, &flow_act, dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(dev, "%s: add inner TTC rule failed\n", __func__);
	}

	kvfree(spec);
	return err ? ERR_PTR(err) : rule;
}

static int mlx5_generate_inner_ttc_table_rules(struct mlx5_core_dev *dev,
					       struct ttc_params *params,
					       struct mlx5_ttc_table *ttc,
					       bool use_l4_type)
{
	struct mlx5_ttc_rule *rules;
	struct mlx5_flow_table *ft;
	int err;
	int tt;

	ft = ttc->t;
	rules = ttc->rules;

	for (tt = 0; tt < MLX5_NUM_TT; tt++) {
		struct mlx5_ttc_rule *rule = &rules[tt];

		if (test_bit(tt, params->ignore_dests))
			continue;
		rule->rule = mlx5_generate_inner_ttc_rule(dev, ft,
							  &params->dests[tt],
							  ttc_rules[tt].etype,
							  ttc_rules[tt].proto,
							  use_l4_type);
		if (IS_ERR(rule->rule)) {
			err = PTR_ERR(rule->rule);
			rule->rule = NULL;
			goto del_rules;
		}
		rule->default_dest = params->dests[tt];
	}

	return 0;

del_rules:

	mlx5_cleanup_ttc_rules(ttc);
	return err;
}

static int mlx5_create_inner_ttc_table_groups(struct mlx5_ttc_table *ttc,
					      const struct mlx5_fs_ttc_groups *groups)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ttc->g = kcalloc(groups->num_groups, sizeof(*ttc->g), GFP_KERNEL);
	if (!ttc->g)
		return -ENOMEM;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		kfree(ttc->g);
		ttc->g = NULL;
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	MLX5_SET_TO_ONES(fte_match_param, mc, inner_headers.ip_version);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_INNER_HEADERS);

	/* TCP UDP group */
	if (groups->use_l4_type) {
		MLX5_SET_TO_ONES(fte_match_param, mc, inner_headers.l4_type);
		MLX5_SET_CFG(in, start_flow_index, ix);
		ix += groups->group_size[ttc->num_groups];
		MLX5_SET_CFG(in, end_flow_index, ix - 1);
		ttc->g[ttc->num_groups] = mlx5_create_flow_group(ttc->t, in);
		if (IS_ERR(ttc->g[ttc->num_groups]))
			goto err;
		ttc->num_groups++;

		MLX5_SET(fte_match_param, mc, inner_headers.l4_type, 0);
	}

	/* L4 Group */
	MLX5_SET_TO_ONES(fte_match_param, mc, inner_headers.ip_protocol);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += groups->group_size[ttc->num_groups];
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ttc->g[ttc->num_groups] = mlx5_create_flow_group(ttc->t, in);
	if (IS_ERR(ttc->g[ttc->num_groups]))
		goto err;
	ttc->num_groups++;

	/* L3 Group */
	MLX5_SET(fte_match_param, mc, inner_headers.ip_protocol, 0);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += groups->group_size[ttc->num_groups];
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ttc->g[ttc->num_groups] = mlx5_create_flow_group(ttc->t, in);
	if (IS_ERR(ttc->g[ttc->num_groups]))
		goto err;
	ttc->num_groups++;

	/* Any Group */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += groups->group_size[ttc->num_groups];
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ttc->g[ttc->num_groups] = mlx5_create_flow_group(ttc->t, in);
	if (IS_ERR(ttc->g[ttc->num_groups]))
		goto err;
	ttc->num_groups++;

	kvfree(in);
	return 0;

err:
	err = PTR_ERR(ttc->g[ttc->num_groups]);
	ttc->g[ttc->num_groups] = NULL;
	kvfree(in);

	return err;
}

struct mlx5_ttc_table *mlx5_create_inner_ttc_table(struct mlx5_core_dev *dev,
						   struct ttc_params *params)
{
	const struct mlx5_fs_ttc_groups *groups;
	struct mlx5_flow_namespace *ns;
	struct mlx5_ttc_table *ttc;
	bool use_l4_type;
	int err;

	ttc = kvzalloc(sizeof(*ttc), GFP_KERNEL);
	if (!ttc)
		return ERR_PTR(-ENOMEM);

	switch (params->ns_type) {
	case MLX5_FLOW_NAMESPACE_PORT_SEL:
		use_l4_type = MLX5_CAP_GEN_2(dev, pcc_ifa2) &&
			MLX5_CAP_PORT_SELECTION_FT_FIELD_SUPPORT_2(dev, inner_l4_type);
		break;
	case MLX5_FLOW_NAMESPACE_KERNEL:
		use_l4_type = MLX5_CAP_GEN_2(dev, pcc_ifa2) &&
			MLX5_CAP_NIC_RX_FT_FIELD_SUPPORT_2(dev, inner_l4_type);
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ns = mlx5_get_flow_namespace(dev, params->ns_type);
	groups = use_l4_type ? &inner_ttc_groups[TTC_GROUPS_USE_L4_TYPE] :
			       &inner_ttc_groups[TTC_GROUPS_DEFAULT];

	WARN_ON_ONCE(params->ft_attr.max_fte);
	params->ft_attr.max_fte = mlx5_fs_ttc_table_size(groups);
	ttc->t = mlx5_create_flow_table(ns, &params->ft_attr);
	if (IS_ERR(ttc->t)) {
		err = PTR_ERR(ttc->t);
		kvfree(ttc);
		return ERR_PTR(err);
	}

	err = mlx5_create_inner_ttc_table_groups(ttc, groups);
	if (err)
		goto destroy_ft;

	err = mlx5_generate_inner_ttc_table_rules(dev, params, ttc, use_l4_type);
	if (err)
		goto destroy_ft;

	return ttc;

destroy_ft:
	mlx5_destroy_ttc_table(ttc);
	return ERR_PTR(err);
}

void mlx5_destroy_ttc_table(struct mlx5_ttc_table *ttc)
{
	int i;

	mlx5_cleanup_ttc_rules(ttc);
	for (i = ttc->num_groups - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(ttc->g[i]))
			mlx5_destroy_flow_group(ttc->g[i]);
		ttc->g[i] = NULL;
	}

	kfree(ttc->g);
	mlx5_destroy_flow_table(ttc->t);
	kvfree(ttc);
}

struct mlx5_ttc_table *mlx5_create_ttc_table(struct mlx5_core_dev *dev,
					     struct ttc_params *params)
{
	bool match_ipv_outer =
		MLX5_CAP_FLOWTABLE_NIC_RX(dev,
					  ft_field_support.outer_ip_version);
	const struct mlx5_fs_ttc_groups *groups;
	struct mlx5_flow_namespace *ns;
	struct mlx5_ttc_table *ttc;
	bool use_l4_type;
	int err;

	ttc = kvzalloc(sizeof(*ttc), GFP_KERNEL);
	if (!ttc)
		return ERR_PTR(-ENOMEM);

	switch (params->ns_type) {
	case MLX5_FLOW_NAMESPACE_PORT_SEL:
		use_l4_type = MLX5_CAP_GEN_2(dev, pcc_ifa2) &&
			MLX5_CAP_PORT_SELECTION_FT_FIELD_SUPPORT_2(dev, outer_l4_type);
		break;
	case MLX5_FLOW_NAMESPACE_KERNEL:
		use_l4_type = MLX5_CAP_GEN_2(dev, pcc_ifa2) &&
			MLX5_CAP_NIC_RX_FT_FIELD_SUPPORT_2(dev, outer_l4_type);
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ns = mlx5_get_flow_namespace(dev, params->ns_type);
	groups = use_l4_type ? &ttc_groups[TTC_GROUPS_USE_L4_TYPE] :
			       &ttc_groups[TTC_GROUPS_DEFAULT];

	WARN_ON_ONCE(params->ft_attr.max_fte);
	params->ft_attr.max_fte = mlx5_fs_ttc_table_size(groups);
	ttc->t = mlx5_create_flow_table(ns, &params->ft_attr);
	if (IS_ERR(ttc->t)) {
		err = PTR_ERR(ttc->t);
		kvfree(ttc);
		return ERR_PTR(err);
	}

	err = mlx5_create_ttc_table_groups(ttc, match_ipv_outer, groups);
	if (err)
		goto destroy_ft;

	err = mlx5_generate_ttc_table_rules(dev, params, ttc, use_l4_type);
	if (err)
		goto destroy_ft;

	return ttc;

destroy_ft:
	mlx5_destroy_ttc_table(ttc);
	return ERR_PTR(err);
}

int mlx5_ttc_fwd_dest(struct mlx5_ttc_table *ttc, enum mlx5_traffic_types type,
		      struct mlx5_flow_destination *new_dest)
{
	return mlx5_modify_rule_destination(ttc->rules[type].rule, new_dest,
					    NULL);
}

struct mlx5_flow_destination
mlx5_ttc_get_default_dest(struct mlx5_ttc_table *ttc,
			  enum mlx5_traffic_types type)
{
	struct mlx5_flow_destination *dest = &ttc->rules[type].default_dest;

	WARN_ONCE(dest->type != MLX5_FLOW_DESTINATION_TYPE_TIR,
		  "TTC[%d] default dest is not setup yet", type);

	return *dest;
}

int mlx5_ttc_fwd_default_dest(struct mlx5_ttc_table *ttc,
			      enum mlx5_traffic_types type)
{
	struct mlx5_flow_destination dest = mlx5_ttc_get_default_dest(ttc, type);

	return mlx5_ttc_fwd_dest(ttc, type, &dest);
}
