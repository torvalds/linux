// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#include <mlx5_core.h>
#include "en_accel/fs_tcp.h"
#include "fs_core.h"

enum accel_fs_tcp_type {
	ACCEL_FS_IPV4_TCP,
	ACCEL_FS_IPV6_TCP,
	ACCEL_FS_TCP_NUM_TYPES,
};

struct mlx5e_accel_fs_tcp {
	struct mlx5e_flow_table tables[ACCEL_FS_TCP_NUM_TYPES];
	struct mlx5_flow_handle *default_rules[ACCEL_FS_TCP_NUM_TYPES];
};

static enum mlx5_traffic_types fs_accel2tt(enum accel_fs_tcp_type i)
{
	switch (i) {
	case ACCEL_FS_IPV4_TCP:
		return MLX5_TT_IPV4_TCP;
	default: /* ACCEL_FS_IPV6_TCP */
		return MLX5_TT_IPV6_TCP;
	}
}

static void accel_fs_tcp_set_ipv4_flow(struct mlx5_flow_spec *spec, struct sock *sk)
{
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_TCP);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, 4);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4),
	       &inet_sk(sk)->inet_daddr, 4);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
	       &inet_sk(sk)->inet_rcv_saddr, 4);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
}

#if IS_ENABLED(CONFIG_IPV6)
static void accel_fs_tcp_set_ipv6_flow(struct mlx5_flow_spec *spec, struct sock *sk)
{
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_TCP);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, 6);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
	       &sk->sk_v6_daddr, 16);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
	       &inet6_sk(sk)->saddr, 16);
	memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
	       0xff, 16);
	memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
	       0xff, 16);
}
#endif

void mlx5e_accel_fs_del_sk(struct mlx5_flow_handle *rule)
{
	mlx5_del_flow_rules(rule);
}

struct mlx5_flow_handle *mlx5e_accel_fs_add_sk(struct mlx5e_flow_steering *fs,
					       struct sock *sk, u32 tirn,
					       uint32_t flow_tag)
{
	struct mlx5e_accel_fs_tcp *fs_tcp = mlx5e_fs_get_accel_tcp(fs);
	struct mlx5_flow_destination dest = {};
	struct mlx5e_flow_table *ft = NULL;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *flow;
	struct mlx5_flow_spec *spec;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	switch (sk->sk_family) {
	case AF_INET:
		accel_fs_tcp_set_ipv4_flow(spec, sk);
		ft = &fs_tcp->tables[ACCEL_FS_IPV4_TCP];
		mlx5_core_dbg(mlx5e_fs_get_mdev(fs), "%s flow is %pI4:%d -> %pI4:%d\n", __func__,
			      &inet_sk(sk)->inet_rcv_saddr,
			      inet_sk(sk)->inet_sport,
			      &inet_sk(sk)->inet_daddr,
			      inet_sk(sk)->inet_dport);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		if (!ipv6_only_sock(sk) &&
		    ipv6_addr_type(&sk->sk_v6_daddr) == IPV6_ADDR_MAPPED) {
			accel_fs_tcp_set_ipv4_flow(spec, sk);
			ft = &fs_tcp->tables[ACCEL_FS_IPV4_TCP];
		} else {
			accel_fs_tcp_set_ipv6_flow(spec, sk);
			ft = &fs_tcp->tables[ACCEL_FS_IPV6_TCP];
		}
		break;
#endif
	default:
		break;
	}

	if (!ft) {
		flow = ERR_PTR(-EINVAL);
		goto out;
	}

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.tcp_dport);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.tcp_sport);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.tcp_dport,
		 ntohs(inet_sk(sk)->inet_sport));
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.tcp_sport,
		 ntohs(inet_sk(sk)->inet_dport));

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tirn;
	if (flow_tag != MLX5_FS_DEFAULT_FLOW_TAG) {
		spec->flow_context.flow_tag = flow_tag;
		spec->flow_context.flags = FLOW_CONTEXT_HAS_TAG;
	}

	flow = mlx5_add_flow_rules(ft->t, spec, &flow_act, &dest, 1);

	if (IS_ERR(flow))
		mlx5_core_err(mlx5e_fs_get_mdev(fs), "mlx5_add_flow_rules() failed, flow is %ld\n",
			      PTR_ERR(flow));

out:
	kvfree(spec);
	return flow;
}

static int accel_fs_tcp_add_default_rule(struct mlx5e_flow_steering *fs,
					 enum accel_fs_tcp_type type)
{
	struct mlx5e_accel_fs_tcp *fs_tcp = mlx5e_fs_get_accel_tcp(fs);
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(fs, false);
	struct mlx5e_flow_table *accel_fs_t;
	struct mlx5_flow_destination dest;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	int err = 0;

	accel_fs_t = &fs_tcp->tables[type];

	dest = mlx5_ttc_get_default_dest(ttc, fs_accel2tt(type));
	rule = mlx5_add_flow_rules(accel_fs_t->t, NULL, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mlx5e_fs_get_mdev(fs),
			      "%s: add default rule failed, accel_fs type=%d, err %d\n",
			      __func__, type, err);
		return err;
	}

	fs_tcp->default_rules[type] = rule;
	return 0;
}

#define MLX5E_ACCEL_FS_TCP_NUM_GROUPS	(2)
#define MLX5E_ACCEL_FS_TCP_GROUP1_SIZE	(BIT(16) - 1)
#define MLX5E_ACCEL_FS_TCP_GROUP2_SIZE	(BIT(0))
#define MLX5E_ACCEL_FS_TCP_TABLE_SIZE	(MLX5E_ACCEL_FS_TCP_GROUP1_SIZE +\
					 MLX5E_ACCEL_FS_TCP_GROUP2_SIZE)
static int accel_fs_tcp_create_groups(struct mlx5e_flow_table *ft,
				      enum accel_fs_tcp_type type)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	void *outer_headers_c;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_ACCEL_FS_TCP_NUM_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	in = kvzalloc(inlen, GFP_KERNEL);
	if  (!in || !ft->g) {
		kfree(ft->g);
		kvfree(in);
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc, outer_headers);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_version);

	switch (type) {
	case ACCEL_FS_IPV4_TCP:
	case ACCEL_FS_IPV6_TCP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, tcp_dport);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, tcp_sport);
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	switch (type) {
	case ACCEL_FS_IPV4_TCP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c,
				 src_ipv4_src_ipv6.ipv4_layout.ipv4);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c,
				 dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
		break;
	case ACCEL_FS_IPV6_TCP:
		memset(MLX5_ADDR_OF(fte_match_set_lyr_2_4, outer_headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       0xff, 16);
		memset(MLX5_ADDR_OF(fte_match_set_lyr_2_4, outer_headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       0xff, 16);
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_ACCEL_FS_TCP_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Default Flow Group */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_ACCEL_FS_TCP_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	kvfree(in);
	return 0;

err:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
out:
	kvfree(in);

	return err;
}

static int accel_fs_tcp_create_table(struct mlx5e_flow_steering *fs, enum accel_fs_tcp_type type)
{
	struct mlx5e_accel_fs_tcp *accel_tcp = mlx5e_fs_get_accel_tcp(fs);
	struct mlx5_flow_namespace *ns = mlx5e_fs_get_ns(fs, false);
	struct mlx5e_flow_table *ft = &accel_tcp->tables[type];
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft->num_groups = 0;

	ft_attr.max_fte = MLX5E_ACCEL_FS_TCP_TABLE_SIZE;
	ft_attr.level = MLX5E_ACCEL_FS_TCP_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	mlx5_core_dbg(mlx5e_fs_get_mdev(fs), "Created fs accel table id %u level %u\n",
		      ft->t->id, ft->t->level);

	err = accel_fs_tcp_create_groups(ft, type);
	if (err)
		goto err;

	err = accel_fs_tcp_add_default_rule(fs, type);
	if (err)
		goto err;

	return 0;
err:
	mlx5e_destroy_flow_table(ft);
	return err;
}

static int accel_fs_tcp_disable(struct mlx5e_flow_steering *fs)
{
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(fs, false);
	int err, i;

	for (i = 0; i < ACCEL_FS_TCP_NUM_TYPES; i++) {
		/* Modify ttc rules destination to point back to the indir TIRs */
		err = mlx5_ttc_fwd_default_dest(ttc, fs_accel2tt(i));
		if (err) {
			mlx5_core_err(mlx5e_fs_get_mdev(fs),
				      "%s: modify ttc[%d] default destination failed, err(%d)\n",
				      __func__, fs_accel2tt(i), err);
			return err;
		}
	}

	return 0;
}

static int accel_fs_tcp_enable(struct mlx5e_flow_steering *fs)
{
	struct mlx5e_accel_fs_tcp *accel_tcp = mlx5e_fs_get_accel_tcp(fs);
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(fs, false);
	struct mlx5_flow_destination dest = {};
	int err, i;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	for (i = 0; i < ACCEL_FS_TCP_NUM_TYPES; i++) {
		dest.ft = accel_tcp->tables[i].t;

		/* Modify ttc rules destination to point on the accel_fs FTs */
		err = mlx5_ttc_fwd_dest(ttc, fs_accel2tt(i), &dest);
		if (err) {
			mlx5_core_err(mlx5e_fs_get_mdev(fs),
				      "%s: modify ttc[%d] destination to accel failed, err(%d)\n",
				      __func__, fs_accel2tt(i), err);
			return err;
		}
	}
	return 0;
}

static void accel_fs_tcp_destroy_table(struct mlx5e_flow_steering *fs, int i)
{
	struct mlx5e_accel_fs_tcp *fs_tcp = mlx5e_fs_get_accel_tcp(fs);

	if (IS_ERR_OR_NULL(fs_tcp->tables[i].t))
		return;

	mlx5_del_flow_rules(fs_tcp->default_rules[i]);
	mlx5e_destroy_flow_table(&fs_tcp->tables[i]);
	fs_tcp->tables[i].t = NULL;
}

void mlx5e_accel_fs_tcp_destroy(struct mlx5e_flow_steering *fs)
{
	struct mlx5e_accel_fs_tcp *accel_tcp = mlx5e_fs_get_accel_tcp(fs);
	int i;

	if (!accel_tcp)
		return;

	accel_fs_tcp_disable(fs);

	for (i = 0; i < ACCEL_FS_TCP_NUM_TYPES; i++)
		accel_fs_tcp_destroy_table(fs, i);

	kfree(accel_tcp);
	mlx5e_fs_set_accel_tcp(fs, NULL);
}

int mlx5e_accel_fs_tcp_create(struct mlx5e_flow_steering *fs)
{
	struct mlx5e_accel_fs_tcp *accel_tcp;
	int i, err;

	if (!MLX5_CAP_FLOWTABLE_NIC_RX(mlx5e_fs_get_mdev(fs), ft_field_support.outer_ip_version))
		return -EOPNOTSUPP;

	accel_tcp = kvzalloc(sizeof(*accel_tcp), GFP_KERNEL);
	if (!accel_tcp)
		return -ENOMEM;
	mlx5e_fs_set_accel_tcp(fs, accel_tcp);

	for (i = 0; i < ACCEL_FS_TCP_NUM_TYPES; i++) {
		err = accel_fs_tcp_create_table(fs, i);
		if (err)
			goto err_destroy_tables;
	}

	err = accel_fs_tcp_enable(fs);
	if (err)
		goto err_destroy_tables;

	return 0;

err_destroy_tables:
	while (--i >= 0)
		accel_fs_tcp_destroy_table(fs, i);
	kfree(accel_tcp);
	mlx5e_fs_set_accel_tcp(fs, NULL);
	return err;
}
