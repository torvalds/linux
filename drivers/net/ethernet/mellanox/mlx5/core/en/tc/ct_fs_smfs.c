// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#include "en_tc.h"
#include "en/tc_priv.h"
#include "en/tc_ct.h"
#include "en/tc/ct_fs.h"

#include "lib/smfs.h"

#define INIT_ERR_PREFIX "ct_fs_smfs init failed"
#define ct_dbg(fmt, args...)\
	netdev_dbg(fs->netdev, "ct_fs_smfs debug: " fmt "\n", ##args)
#define MLX5_CT_TCP_FLAGS_MASK cpu_to_be16(be32_to_cpu(TCP_FLAG_RST | TCP_FLAG_FIN) >> 16)

struct mlx5_ct_fs_smfs_matchers {
	struct mlx5dr_matcher *ipv4_tcp;
	struct mlx5dr_matcher *ipv4_udp;
	struct mlx5dr_matcher *ipv6_tcp;
	struct mlx5dr_matcher *ipv6_udp;
};

struct mlx5_ct_fs_smfs {
	struct mlx5_ct_fs_smfs_matchers ct_matchers;
	struct mlx5_ct_fs_smfs_matchers ct_matchers_nat;
	struct mlx5dr_action *fwd_action;
	struct mlx5_flow_table *ct_nat;
};

struct mlx5_ct_fs_smfs_rule {
	struct mlx5_ct_fs_rule fs_rule;
	struct mlx5dr_rule *rule;
	struct mlx5dr_action *count_action;
};

static inline void
mlx5_ct_fs_smfs_fill_mask(struct mlx5_ct_fs *fs, struct mlx5_flow_spec *spec, bool ipv4, bool tcp)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, outer_headers);

	if (likely(MLX5_CAP_FLOWTABLE_NIC_RX(fs->dev, ft_field_support.outer_ip_version)))
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_version);
	else
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ethertype);

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
	if (likely(ipv4)) {
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c,
				 src_ipv4_src_ipv6.ipv4_layout.ipv4);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c,
				 dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
	} else {
		memset(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       0xFF,
		       MLX5_FLD_SZ_BYTES(fte_match_set_lyr_2_4,
					 dst_ipv4_dst_ipv6.ipv6_layout.ipv6));
		memset(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       0xFF,
		       MLX5_FLD_SZ_BYTES(fte_match_set_lyr_2_4,
					 src_ipv4_src_ipv6.ipv6_layout.ipv6));
	}

	if (likely(tcp)) {
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, tcp_sport);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, tcp_dport);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, tcp_flags,
			 ntohs(MLX5_CT_TCP_FLAGS_MASK));
	} else {
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, udp_sport);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, udp_dport);
	}

	mlx5e_tc_match_to_reg_match(spec, ZONE_TO_REG, 0, MLX5_CT_ZONE_MASK);
}

static struct mlx5dr_matcher *
mlx5_ct_fs_smfs_matcher_create(struct mlx5_ct_fs *fs, struct mlx5dr_table *tbl, bool ipv4,
			       bool tcp, u32 priority)
{
	struct mlx5dr_matcher *dr_matcher;
	struct mlx5_flow_spec *spec;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	mlx5_ct_fs_smfs_fill_mask(fs, spec, ipv4, tcp);
	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2 | MLX5_MATCH_OUTER_HEADERS;

	dr_matcher = mlx5_smfs_matcher_create(tbl, priority, spec);
	kfree(spec);
	if (!dr_matcher)
		return ERR_PTR(-EINVAL);

	return dr_matcher;
}

static int
mlx5_ct_fs_smfs_matchers_create(struct mlx5_ct_fs *fs, struct mlx5dr_table *tbl,
				struct mlx5_ct_fs_smfs_matchers *ct_matchers)
{
	const struct net_device *netdev = fs->netdev;
	u32 prio = 0;
	int err;

	ct_matchers->ipv4_tcp = mlx5_ct_fs_smfs_matcher_create(fs, tbl, true, true, prio);
	if (IS_ERR(ct_matchers->ipv4_tcp)) {
		err = PTR_ERR(ct_matchers->ipv4_tcp);
		netdev_warn(netdev,
			    "%s, failed to create ipv4 tcp matcher, err: %d\n",
			    INIT_ERR_PREFIX, err);
		return err;
	}

	++prio;
	ct_matchers->ipv4_udp = mlx5_ct_fs_smfs_matcher_create(fs, tbl, true, false, prio);
	if (IS_ERR(ct_matchers->ipv4_udp)) {
		err = PTR_ERR(ct_matchers->ipv4_udp);
		netdev_warn(netdev,
			    "%s, failed to create ipv4 udp matcher, err: %d\n",
			    INIT_ERR_PREFIX, err);
		goto err_matcher_ipv4_udp;
	}

	++prio;
	ct_matchers->ipv6_tcp = mlx5_ct_fs_smfs_matcher_create(fs, tbl, false, true, prio);
	if (IS_ERR(ct_matchers->ipv6_tcp)) {
		err = PTR_ERR(ct_matchers->ipv6_tcp);
		netdev_warn(netdev,
			    "%s, failed to create ipv6 tcp matcher, err: %d\n",
			    INIT_ERR_PREFIX, err);
		goto err_matcher_ipv6_tcp;
	}

	++prio;
	ct_matchers->ipv6_udp = mlx5_ct_fs_smfs_matcher_create(fs, tbl, false, false, prio);
	if (IS_ERR(ct_matchers->ipv6_udp)) {
		err = PTR_ERR(ct_matchers->ipv6_udp);
		netdev_warn(netdev,
			    "%s, failed to create ipv6 tcp matcher, err: %d\n",
			     INIT_ERR_PREFIX, err);
		goto err_matcher_ipv6_udp;
	}

	return 0;

err_matcher_ipv6_udp:
	mlx5_smfs_matcher_destroy(ct_matchers->ipv6_tcp);
err_matcher_ipv6_tcp:
	mlx5_smfs_matcher_destroy(ct_matchers->ipv4_udp);
err_matcher_ipv4_udp:
	mlx5_smfs_matcher_destroy(ct_matchers->ipv4_tcp);
	return 0;
}

static void
mlx5_ct_fs_smfs_matchers_destroy(struct mlx5_ct_fs_smfs_matchers *ct_matchers)
{
	mlx5_smfs_matcher_destroy(ct_matchers->ipv6_udp);
	mlx5_smfs_matcher_destroy(ct_matchers->ipv6_tcp);
	mlx5_smfs_matcher_destroy(ct_matchers->ipv4_udp);
	mlx5_smfs_matcher_destroy(ct_matchers->ipv4_tcp);
}

static int
mlx5_ct_fs_smfs_init(struct mlx5_ct_fs *fs, struct mlx5_flow_table *ct,
		     struct mlx5_flow_table *ct_nat, struct mlx5_flow_table *post_ct)
{
	struct mlx5dr_table *ct_tbl, *ct_nat_tbl, *post_ct_tbl;
	struct mlx5_ct_fs_smfs *fs_smfs = mlx5_ct_fs_priv(fs);
	int err;

	post_ct_tbl = mlx5_smfs_table_get_from_fs_ft(post_ct);
	ct_nat_tbl = mlx5_smfs_table_get_from_fs_ft(ct_nat);
	ct_tbl = mlx5_smfs_table_get_from_fs_ft(ct);
	fs_smfs->ct_nat = ct_nat;

	if (!ct_tbl || !ct_nat_tbl || !post_ct_tbl) {
		netdev_warn(fs->netdev, "ct_fs_smfs: failed to init, missing backing dr tables");
		return -EOPNOTSUPP;
	}

	ct_dbg("using smfs steering");

	err = mlx5_ct_fs_smfs_matchers_create(fs, ct_tbl, &fs_smfs->ct_matchers);
	if (err)
		goto err_init;

	err = mlx5_ct_fs_smfs_matchers_create(fs, ct_nat_tbl, &fs_smfs->ct_matchers_nat);
	if (err)
		goto err_matchers_nat;

	fs_smfs->fwd_action = mlx5_smfs_action_create_dest_table(post_ct_tbl);
	if (!fs_smfs->fwd_action) {
		err = -EINVAL;
		goto err_action_create;
	}

	return 0;

err_action_create:
	mlx5_ct_fs_smfs_matchers_destroy(&fs_smfs->ct_matchers_nat);
err_matchers_nat:
	mlx5_ct_fs_smfs_matchers_destroy(&fs_smfs->ct_matchers);
err_init:
	return err;
}

static void
mlx5_ct_fs_smfs_destroy(struct mlx5_ct_fs *fs)
{
	struct mlx5_ct_fs_smfs *fs_smfs = mlx5_ct_fs_priv(fs);

	mlx5_smfs_action_destroy(fs_smfs->fwd_action);
	mlx5_ct_fs_smfs_matchers_destroy(&fs_smfs->ct_matchers_nat);
	mlx5_ct_fs_smfs_matchers_destroy(&fs_smfs->ct_matchers);
}

static inline bool
mlx5_tc_ct_valid_used_dissector_keys(const u32 used_keys)
{
#define DISSECTOR_BIT(name) BIT(FLOW_DISSECTOR_KEY_ ## name)
	const u32 basic_keys = DISSECTOR_BIT(BASIC) | DISSECTOR_BIT(CONTROL) |
			       DISSECTOR_BIT(PORTS) | DISSECTOR_BIT(META);
	const u32 ipv4_tcp = basic_keys | DISSECTOR_BIT(IPV4_ADDRS) | DISSECTOR_BIT(TCP);
	const u32 ipv4_udp = basic_keys | DISSECTOR_BIT(IPV4_ADDRS);
	const u32 ipv6_tcp = basic_keys | DISSECTOR_BIT(IPV6_ADDRS) | DISSECTOR_BIT(TCP);
	const u32 ipv6_udp = basic_keys | DISSECTOR_BIT(IPV6_ADDRS);

	return (used_keys == ipv4_tcp || used_keys == ipv4_udp || used_keys == ipv6_tcp ||
		used_keys == ipv6_udp);
}

static bool
mlx5_ct_fs_smfs_ct_validate_flow_rule(struct mlx5_ct_fs *fs, struct flow_rule *flow_rule)
{
	struct flow_match_ipv4_addrs ipv4_addrs;
	struct flow_match_ipv6_addrs ipv6_addrs;
	struct flow_match_control control;
	struct flow_match_basic basic;
	struct flow_match_ports ports;
	struct flow_match_tcp tcp;

	if (!mlx5_tc_ct_valid_used_dissector_keys(flow_rule->match.dissector->used_keys)) {
		ct_dbg("rule uses unexpected dissectors (0x%08x)",
		       flow_rule->match.dissector->used_keys);
		return false;
	}

	flow_rule_match_basic(flow_rule, &basic);
	flow_rule_match_control(flow_rule, &control);
	flow_rule_match_ipv4_addrs(flow_rule, &ipv4_addrs);
	flow_rule_match_ipv6_addrs(flow_rule, &ipv6_addrs);
	flow_rule_match_ports(flow_rule, &ports);
	flow_rule_match_tcp(flow_rule, &tcp);

	if (basic.mask->n_proto != htons(0xFFFF) ||
	    (basic.key->n_proto != htons(ETH_P_IP) && basic.key->n_proto != htons(ETH_P_IPV6)) ||
	    basic.mask->ip_proto != 0xFF ||
	    (basic.key->ip_proto != IPPROTO_UDP && basic.key->ip_proto != IPPROTO_TCP)) {
		ct_dbg("rule uses unexpected basic match (n_proto 0x%04x/0x%04x, ip_proto 0x%02x/0x%02x)",
		       ntohs(basic.key->n_proto), ntohs(basic.mask->n_proto),
		       basic.key->ip_proto, basic.mask->ip_proto);
		return false;
	}

	if (ports.mask->src != htons(0xFFFF) || ports.mask->dst != htons(0xFFFF)) {
		ct_dbg("rule uses ports match (src 0x%04x, dst 0x%04x)",
		       ports.mask->src, ports.mask->dst);
		return false;
	}

	if (basic.key->ip_proto == IPPROTO_TCP && tcp.mask->flags != MLX5_CT_TCP_FLAGS_MASK) {
		ct_dbg("rule uses unexpected tcp match (flags 0x%02x)", tcp.mask->flags);
		return false;
	}

	return true;
}

static struct mlx5_ct_fs_rule *
mlx5_ct_fs_smfs_ct_rule_add(struct mlx5_ct_fs *fs, struct mlx5_flow_spec *spec,
			    struct mlx5_flow_attr *attr, struct flow_rule *flow_rule)
{
	struct mlx5_ct_fs_smfs *fs_smfs = mlx5_ct_fs_priv(fs);
	struct mlx5_ct_fs_smfs_matchers *matchers;
	struct mlx5_ct_fs_smfs_rule *smfs_rule;
	struct mlx5dr_action *actions[5];
	struct mlx5dr_matcher *matcher;
	struct mlx5dr_rule *rule;
	int num_actions = 0, err;
	bool nat, tcp, ipv4;

	if (!mlx5_ct_fs_smfs_ct_validate_flow_rule(fs, flow_rule))
		return ERR_PTR(-EOPNOTSUPP);

	smfs_rule = kzalloc(sizeof(*smfs_rule), GFP_KERNEL);
	if (!smfs_rule)
		return ERR_PTR(-ENOMEM);

	smfs_rule->count_action = mlx5_smfs_action_create_flow_counter(mlx5_fc_id(attr->counter));
	if (!smfs_rule->count_action) {
		err = -EINVAL;
		goto err_count;
	}

	actions[num_actions++] = smfs_rule->count_action;
	actions[num_actions++] = attr->modify_hdr->action.dr_action;
	actions[num_actions++] = fs_smfs->fwd_action;

	nat = (attr->ft == fs_smfs->ct_nat);
	ipv4 = mlx5e_tc_get_ip_version(spec, true) == 4;
	tcp = MLX5_GET(fte_match_param, spec->match_value,
		       outer_headers.ip_protocol) == IPPROTO_TCP;

	matchers = nat ? &fs_smfs->ct_matchers_nat : &fs_smfs->ct_matchers;
	matcher = ipv4 ? (tcp ? matchers->ipv4_tcp : matchers->ipv4_udp) :
			 (tcp ? matchers->ipv6_tcp : matchers->ipv6_udp);

	rule = mlx5_smfs_rule_create(matcher, spec, num_actions, actions,
				     MLX5_FLOW_CONTEXT_FLOW_SOURCE_ANY_VPORT);
	if (!rule) {
		err = -EINVAL;
		goto err_rule;
	}

	smfs_rule->rule = rule;

	return &smfs_rule->fs_rule;

err_rule:
	mlx5_smfs_action_destroy(smfs_rule->count_action);
err_count:
	kfree(smfs_rule);
	return ERR_PTR(err);
}

static void
mlx5_ct_fs_smfs_ct_rule_del(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_rule *fs_rule)
{
	struct mlx5_ct_fs_smfs_rule *smfs_rule = container_of(fs_rule,
							      struct mlx5_ct_fs_smfs_rule,
							      fs_rule);

	mlx5_smfs_rule_destroy(smfs_rule->rule);
	mlx5_smfs_action_destroy(smfs_rule->count_action);
	kfree(smfs_rule);
}

static struct mlx5_ct_fs_ops fs_smfs_ops = {
	.ct_rule_add = mlx5_ct_fs_smfs_ct_rule_add,
	.ct_rule_del = mlx5_ct_fs_smfs_ct_rule_del,

	.init = mlx5_ct_fs_smfs_init,
	.destroy = mlx5_ct_fs_smfs_destroy,

	.priv_size = sizeof(struct mlx5_ct_fs_smfs),
};

struct mlx5_ct_fs_ops *
mlx5_ct_fs_smfs_ops_get(void)
{
	return &fs_smfs_ops;
}
