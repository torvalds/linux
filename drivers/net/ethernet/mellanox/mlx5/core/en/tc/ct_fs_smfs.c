// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#include <linux/refcount.h>

#include "en_tc.h"
#include "en/tc_priv.h"
#include "en/tc_ct.h"
#include "en/tc/ct_fs.h"

#include "lib/smfs.h"

#define INIT_ERR_PREFIX "ct_fs_smfs init failed"
#define ct_dbg(fmt, args...)\
	netdev_dbg(fs->netdev, "ct_fs_smfs debug: " fmt "\n", ##args)
#define MLX5_CT_TCP_FLAGS_MASK cpu_to_be16(be32_to_cpu(TCP_FLAG_RST | TCP_FLAG_FIN) >> 16)

struct mlx5_ct_fs_smfs_matcher {
	struct mlx5dr_matcher *dr_matcher;
	struct list_head list;
	int prio;
	refcount_t ref;
};

struct mlx5_ct_fs_smfs_matchers {
	struct mlx5_ct_fs_smfs_matcher smfs_matchers[6];
	struct list_head used;
};

struct mlx5_ct_fs_smfs {
	struct mlx5dr_table *ct_tbl, *ct_nat_tbl;
	struct mlx5_ct_fs_smfs_matchers matchers;
	struct mlx5_ct_fs_smfs_matchers matchers_nat;
	struct mlx5dr_action *fwd_action;
	struct mlx5_flow_table *ct_nat;
	struct mutex lock; /* Guards matchers */
};

struct mlx5_ct_fs_smfs_rule {
	struct mlx5_ct_fs_rule fs_rule;
	struct mlx5dr_rule *rule;
	struct mlx5dr_action *count_action;
	struct mlx5_ct_fs_smfs_matcher *smfs_matcher;
};

static inline void
mlx5_ct_fs_smfs_fill_mask(struct mlx5_ct_fs *fs, struct mlx5_flow_spec *spec, bool ipv4, bool tcp,
			  bool gre)
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
	} else if (!gre) {
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, udp_sport);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, udp_dport);
	}

	mlx5e_tc_match_to_reg_match(spec, ZONE_TO_REG, 0, MLX5_CT_ZONE_MASK);
}

static struct mlx5dr_matcher *
mlx5_ct_fs_smfs_matcher_create(struct mlx5_ct_fs *fs, struct mlx5dr_table *tbl, bool ipv4,
			       bool tcp, bool gre, u32 priority)
{
	struct mlx5dr_matcher *dr_matcher;
	struct mlx5_flow_spec *spec;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	mlx5_ct_fs_smfs_fill_mask(fs, spec, ipv4, tcp, gre);
	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2 | MLX5_MATCH_OUTER_HEADERS;

	dr_matcher = mlx5_smfs_matcher_create(tbl, priority, spec);
	kvfree(spec);
	if (!dr_matcher)
		return ERR_PTR(-EINVAL);

	return dr_matcher;
}

static struct mlx5_ct_fs_smfs_matcher *
mlx5_ct_fs_smfs_matcher_get(struct mlx5_ct_fs *fs, bool nat, bool ipv4, bool tcp, bool gre)
{
	struct mlx5_ct_fs_smfs *fs_smfs = mlx5_ct_fs_priv(fs);
	struct mlx5_ct_fs_smfs_matcher *m, *smfs_matcher;
	struct mlx5_ct_fs_smfs_matchers *matchers;
	struct mlx5dr_matcher *dr_matcher;
	struct mlx5dr_table *tbl;
	struct list_head *prev;
	int prio;

	matchers = nat ? &fs_smfs->matchers_nat : &fs_smfs->matchers;
	smfs_matcher = &matchers->smfs_matchers[ipv4 * 3 + tcp * 2 + gre];

	if (refcount_inc_not_zero(&smfs_matcher->ref))
		return smfs_matcher;

	mutex_lock(&fs_smfs->lock);

	/* Retry with lock, as another thread might have already created the relevant matcher
	 * till we acquired the lock
	 */
	if (refcount_inc_not_zero(&smfs_matcher->ref))
		goto out_unlock;

	// Find next available priority in sorted used list
	prio = 0;
	prev = &matchers->used;
	list_for_each_entry(m, &matchers->used, list) {
		prev = &m->list;

		if (m->prio == prio)
			prio = m->prio + 1;
		else
			break;
	}

	tbl = nat ? fs_smfs->ct_nat_tbl : fs_smfs->ct_tbl;
	dr_matcher = mlx5_ct_fs_smfs_matcher_create(fs, tbl, ipv4, tcp, gre, prio);
	if (IS_ERR(dr_matcher)) {
		netdev_warn(fs->netdev,
			    "ct_fs_smfs: failed to create matcher (nat %d, ipv4 %d, tcp %d, gre %d), err: %ld\n",
			    nat, ipv4, tcp, gre, PTR_ERR(dr_matcher));

		smfs_matcher = ERR_CAST(dr_matcher);
		goto out_unlock;
	}

	smfs_matcher->dr_matcher = dr_matcher;
	smfs_matcher->prio = prio;
	list_add(&smfs_matcher->list, prev);
	refcount_set(&smfs_matcher->ref, 1);

out_unlock:
	mutex_unlock(&fs_smfs->lock);
	return smfs_matcher;
}

static void
mlx5_ct_fs_smfs_matcher_put(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_smfs_matcher *smfs_matcher)
{
	struct mlx5_ct_fs_smfs *fs_smfs = mlx5_ct_fs_priv(fs);

	if (!refcount_dec_and_mutex_lock(&smfs_matcher->ref, &fs_smfs->lock))
		return;

	mlx5_smfs_matcher_destroy(smfs_matcher->dr_matcher);
	list_del(&smfs_matcher->list);
	mutex_unlock(&fs_smfs->lock);
}

static int
mlx5_ct_fs_smfs_init(struct mlx5_ct_fs *fs, struct mlx5_flow_table *ct,
		     struct mlx5_flow_table *ct_nat, struct mlx5_flow_table *post_ct)
{
	struct mlx5dr_table *ct_tbl, *ct_nat_tbl, *post_ct_tbl;
	struct mlx5_ct_fs_smfs *fs_smfs = mlx5_ct_fs_priv(fs);

	post_ct_tbl = mlx5_smfs_table_get_from_fs_ft(post_ct);
	ct_nat_tbl = mlx5_smfs_table_get_from_fs_ft(ct_nat);
	ct_tbl = mlx5_smfs_table_get_from_fs_ft(ct);
	fs_smfs->ct_nat = ct_nat;

	if (!ct_tbl || !ct_nat_tbl || !post_ct_tbl) {
		netdev_warn(fs->netdev, "ct_fs_smfs: failed to init, missing backing dr tables");
		return -EOPNOTSUPP;
	}

	ct_dbg("using smfs steering");

	fs_smfs->fwd_action = mlx5_smfs_action_create_dest_table(post_ct_tbl);
	if (!fs_smfs->fwd_action) {
		return -EINVAL;
	}

	fs_smfs->ct_tbl = ct_tbl;
	fs_smfs->ct_nat_tbl = ct_nat_tbl;
	mutex_init(&fs_smfs->lock);
	INIT_LIST_HEAD(&fs_smfs->matchers.used);
	INIT_LIST_HEAD(&fs_smfs->matchers_nat.used);

	return 0;
}

static void
mlx5_ct_fs_smfs_destroy(struct mlx5_ct_fs *fs)
{
	struct mlx5_ct_fs_smfs *fs_smfs = mlx5_ct_fs_priv(fs);

	mlx5_smfs_action_destroy(fs_smfs->fwd_action);
}

static inline bool
mlx5_tc_ct_valid_used_dissector_keys(const u64 used_keys)
{
#define DISS_BIT(name) BIT_ULL(FLOW_DISSECTOR_KEY_ ## name)
	const u64 basic_keys = DISS_BIT(BASIC) | DISS_BIT(CONTROL) |
				DISS_BIT(META);
	const u64 ipv4_tcp = basic_keys | DISS_BIT(IPV4_ADDRS) |
				DISS_BIT(PORTS) | DISS_BIT(TCP);
	const u64 ipv6_tcp = basic_keys | DISS_BIT(IPV6_ADDRS) |
				DISS_BIT(PORTS) | DISS_BIT(TCP);
	const u64 ipv4_udp = basic_keys | DISS_BIT(IPV4_ADDRS) |
				DISS_BIT(PORTS);
	const u64 ipv6_udp = basic_keys | DISS_BIT(IPV6_ADDRS) |
				 DISS_BIT(PORTS);
	const u64 ipv4_gre = basic_keys | DISS_BIT(IPV4_ADDRS);
	const u64 ipv6_gre = basic_keys | DISS_BIT(IPV6_ADDRS);

	return (used_keys == ipv4_tcp || used_keys == ipv4_udp || used_keys == ipv6_tcp ||
		used_keys == ipv6_udp || used_keys == ipv4_gre || used_keys == ipv6_gre);
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
		ct_dbg("rule uses unexpected dissectors (0x%016llx)",
		       flow_rule->match.dissector->used_keys);
		return false;
	}

	flow_rule_match_basic(flow_rule, &basic);
	flow_rule_match_control(flow_rule, &control);
	flow_rule_match_ipv4_addrs(flow_rule, &ipv4_addrs);
	flow_rule_match_ipv6_addrs(flow_rule, &ipv6_addrs);
	if (basic.key->ip_proto != IPPROTO_GRE)
		flow_rule_match_ports(flow_rule, &ports);
	if (basic.key->ip_proto == IPPROTO_TCP)
		flow_rule_match_tcp(flow_rule, &tcp);

	if (basic.mask->n_proto != htons(0xFFFF) ||
	    (basic.key->n_proto != htons(ETH_P_IP) && basic.key->n_proto != htons(ETH_P_IPV6)) ||
	    basic.mask->ip_proto != 0xFF ||
	    (basic.key->ip_proto != IPPROTO_UDP && basic.key->ip_proto != IPPROTO_TCP &&
	     basic.key->ip_proto != IPPROTO_GRE)) {
		ct_dbg("rule uses unexpected basic match (n_proto 0x%04x/0x%04x, ip_proto 0x%02x/0x%02x)",
		       ntohs(basic.key->n_proto), ntohs(basic.mask->n_proto),
		       basic.key->ip_proto, basic.mask->ip_proto);
		return false;
	}

	if (basic.key->ip_proto != IPPROTO_GRE &&
	    (ports.mask->src != htons(0xFFFF) || ports.mask->dst != htons(0xFFFF))) {
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
	struct mlx5_ct_fs_smfs_matcher *smfs_matcher;
	struct mlx5_ct_fs_smfs_rule *smfs_rule;
	struct mlx5dr_action *actions[5];
	struct mlx5dr_rule *rule;
	int num_actions = 0, err;
	bool nat, tcp, ipv4, gre;

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
	gre = MLX5_GET(fte_match_param, spec->match_value,
		       outer_headers.ip_protocol) == IPPROTO_GRE;

	smfs_matcher = mlx5_ct_fs_smfs_matcher_get(fs, nat, ipv4, tcp, gre);
	if (IS_ERR(smfs_matcher)) {
		err = PTR_ERR(smfs_matcher);
		goto err_matcher;
	}

	rule = mlx5_smfs_rule_create(smfs_matcher->dr_matcher, spec, num_actions, actions,
				     spec->flow_context.flow_source);
	if (!rule) {
		err = -EINVAL;
		goto err_create;
	}

	smfs_rule->rule = rule;
	smfs_rule->smfs_matcher = smfs_matcher;

	return &smfs_rule->fs_rule;

err_create:
	mlx5_ct_fs_smfs_matcher_put(fs, smfs_matcher);
err_matcher:
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
	mlx5_ct_fs_smfs_matcher_put(fs, smfs_rule->smfs_matcher);
	mlx5_smfs_action_destroy(smfs_rule->count_action);
	kfree(smfs_rule);
}

static int mlx5_ct_fs_smfs_ct_rule_update(struct mlx5_ct_fs *fs, struct mlx5_ct_fs_rule *fs_rule,
					  struct mlx5_flow_spec *spec, struct mlx5_flow_attr *attr)
{
	struct mlx5_ct_fs_smfs_rule *smfs_rule = container_of(fs_rule,
							      struct mlx5_ct_fs_smfs_rule,
							      fs_rule);
	struct mlx5_ct_fs_smfs *fs_smfs = mlx5_ct_fs_priv(fs);
	struct mlx5dr_action *actions[3];  /* We only need to create 3 actions, see below. */
	struct mlx5dr_rule *rule;

	actions[0] = smfs_rule->count_action;
	actions[1] = attr->modify_hdr->action.dr_action;
	actions[2] = fs_smfs->fwd_action;

	rule = mlx5_smfs_rule_create(smfs_rule->smfs_matcher->dr_matcher, spec,
				     ARRAY_SIZE(actions), actions, spec->flow_context.flow_source);
	if (!rule)
		return -EINVAL;

	mlx5_smfs_rule_destroy(smfs_rule->rule);
	smfs_rule->rule = rule;

	return 0;
}

static struct mlx5_ct_fs_ops fs_smfs_ops = {
	.ct_rule_add = mlx5_ct_fs_smfs_ct_rule_add,
	.ct_rule_del = mlx5_ct_fs_smfs_ct_rule_del,
	.ct_rule_update = mlx5_ct_fs_smfs_ct_rule_update,

	.init = mlx5_ct_fs_smfs_init,
	.destroy = mlx5_ct_fs_smfs_destroy,

	.priv_size = sizeof(struct mlx5_ct_fs_smfs),
};

struct mlx5_ct_fs_ops *
mlx5_ct_fs_smfs_ops_get(void)
{
	return &fs_smfs_ops;
}
