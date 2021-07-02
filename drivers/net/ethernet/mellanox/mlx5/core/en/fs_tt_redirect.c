// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include <linux/netdevice.h>
#include "en/fs_tt_redirect.h"
#include "fs_core.h"

enum fs_udp_type {
	FS_IPV4_UDP,
	FS_IPV6_UDP,
	FS_UDP_NUM_TYPES,
};

struct mlx5e_fs_udp {
	struct mlx5e_flow_table tables[FS_UDP_NUM_TYPES];
	struct mlx5_flow_handle *default_rules[FS_UDP_NUM_TYPES];
	int ref_cnt;
};

struct mlx5e_fs_any {
	struct mlx5e_flow_table table;
	struct mlx5_flow_handle *default_rule;
	int ref_cnt;
};

static char *fs_udp_type2str(enum fs_udp_type i)
{
	switch (i) {
	case FS_IPV4_UDP:
		return "UDP v4";
	default: /* FS_IPV6_UDP */
		return "UDP v6";
	}
}

static enum mlx5_traffic_types fs_udp2tt(enum fs_udp_type i)
{
	switch (i) {
	case FS_IPV4_UDP:
		return MLX5_TT_IPV4_UDP;
	default: /* FS_IPV6_UDP */
		return MLX5_TT_IPV6_UDP;
	}
}

static enum fs_udp_type tt2fs_udp(enum mlx5_traffic_types i)
{
	switch (i) {
	case MLX5_TT_IPV4_UDP:
		return FS_IPV4_UDP;
	case MLX5_TT_IPV6_UDP:
		return FS_IPV6_UDP;
	default:
		return FS_UDP_NUM_TYPES;
	}
}

void mlx5e_fs_tt_redirect_del_rule(struct mlx5_flow_handle *rule)
{
	mlx5_del_flow_rules(rule);
}

static void fs_udp_set_dport_flow(struct mlx5_flow_spec *spec, enum fs_udp_type type,
				  u16 udp_dport)
{
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_UDP);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version,
		 type == FS_IPV4_UDP ? 4 : 6);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.udp_dport);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.udp_dport, udp_dport);
}

struct mlx5_flow_handle *
mlx5e_fs_tt_redirect_udp_add_rule(struct mlx5e_priv *priv,
				  enum mlx5_traffic_types ttc_type,
				  u32 tir_num, u16 d_port)
{
	enum fs_udp_type type = tt2fs_udp(ttc_type);
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_table *ft = NULL;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_fs_udp *fs_udp;
	int err;

	if (type == FS_UDP_NUM_TYPES)
		return ERR_PTR(-EINVAL);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	fs_udp = priv->fs.udp;
	ft = fs_udp->tables[type].t;

	fs_udp_set_dport_flow(spec, type, d_port);
	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tir_num;

	rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	kvfree(spec);

	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev, "%s: add %s rule failed, err %d\n",
			   __func__, fs_udp_type2str(type), err);
	}
	return rule;
}

static int fs_udp_add_default_rule(struct mlx5e_priv *priv, enum fs_udp_type type)
{
	struct mlx5e_flow_table *fs_udp_t;
	struct mlx5_flow_destination dest;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5e_fs_udp *fs_udp;
	int err;

	fs_udp = priv->fs.udp;
	fs_udp_t = &fs_udp->tables[type];

	dest = mlx5e_ttc_get_default_dest(priv, fs_udp2tt(type));
	rule = mlx5_add_flow_rules(fs_udp_t->t, NULL, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev,
			   "%s: add default rule failed, fs type=%d, err %d\n",
			   __func__, type, err);
		return err;
	}

	fs_udp->default_rules[type] = rule;
	return 0;
}

#define MLX5E_FS_UDP_NUM_GROUPS	(2)
#define MLX5E_FS_UDP_GROUP1_SIZE	(BIT(16))
#define MLX5E_FS_UDP_GROUP2_SIZE	(BIT(0))
#define MLX5E_FS_UDP_TABLE_SIZE		(MLX5E_FS_UDP_GROUP1_SIZE +\
					 MLX5E_FS_UDP_GROUP2_SIZE)
static int fs_udp_create_groups(struct mlx5e_flow_table *ft, enum fs_udp_type type)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	void *outer_headers_c;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_FS_UDP_NUM_GROUPS, sizeof(*ft->g), GFP_KERNEL);
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
	case FS_IPV4_UDP:
	case FS_IPV6_UDP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, udp_dport);
		break;
	default:
		err = -EINVAL;
		goto out;
	}
	/* Match on udp protocol, Ipv4/6 and dport */
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_FS_UDP_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Default Flow Group */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_FS_UDP_GROUP2_SIZE;
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

static int fs_udp_create_table(struct mlx5e_priv *priv, enum fs_udp_type type)
{
	struct mlx5e_flow_table *ft = &priv->fs.udp->tables[type];
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft->num_groups = 0;

	ft_attr.max_fte = MLX5E_FS_UDP_TABLE_SIZE;
	ft_attr.level = MLX5E_FS_TT_UDP_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	netdev_dbg(priv->netdev, "Created fs %s table id %u level %u\n",
		   fs_udp_type2str(type), ft->t->id, ft->t->level);

	err = fs_udp_create_groups(ft, type);
	if (err)
		goto err;

	err = fs_udp_add_default_rule(priv, type);
	if (err)
		goto err;

	return 0;

err:
	mlx5e_destroy_flow_table(ft);
	return err;
}

static void fs_udp_destroy_table(struct mlx5e_fs_udp *fs_udp, int i)
{
	if (IS_ERR_OR_NULL(fs_udp->tables[i].t))
		return;

	mlx5_del_flow_rules(fs_udp->default_rules[i]);
	mlx5e_destroy_flow_table(&fs_udp->tables[i]);
	fs_udp->tables[i].t = NULL;
}

static int fs_udp_disable(struct mlx5e_priv *priv)
{
	int err, i;

	for (i = 0; i < FS_UDP_NUM_TYPES; i++) {
		/* Modify ttc rules destination to point back to the indir TIRs */
		err = mlx5e_ttc_fwd_default_dest(priv, fs_udp2tt(i));
		if (err) {
			netdev_err(priv->netdev,
				   "%s: modify ttc[%d] default destination failed, err(%d)\n",
				   __func__, fs_udp2tt(i), err);
			return err;
		}
	}

	return 0;
}

static int fs_udp_enable(struct mlx5e_priv *priv)
{
	struct mlx5_flow_destination dest = {};
	int err, i;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	for (i = 0; i < FS_UDP_NUM_TYPES; i++) {
		dest.ft = priv->fs.udp->tables[i].t;

		/* Modify ttc rules destination to point on the accel_fs FTs */
		err = mlx5e_ttc_fwd_dest(priv, fs_udp2tt(i), &dest);
		if (err) {
			netdev_err(priv->netdev,
				   "%s: modify ttc[%d] destination to accel failed, err(%d)\n",
				   __func__, fs_udp2tt(i), err);
			return err;
		}
	}
	return 0;
}

void mlx5e_fs_tt_redirect_udp_destroy(struct mlx5e_priv *priv)
{
	struct mlx5e_fs_udp *fs_udp = priv->fs.udp;
	int i;

	if (!fs_udp)
		return;

	if (--fs_udp->ref_cnt)
		return;

	fs_udp_disable(priv);

	for (i = 0; i < FS_UDP_NUM_TYPES; i++)
		fs_udp_destroy_table(fs_udp, i);

	kfree(fs_udp);
	priv->fs.udp = NULL;
}

int mlx5e_fs_tt_redirect_udp_create(struct mlx5e_priv *priv)
{
	int i, err;

	if (priv->fs.udp) {
		priv->fs.udp->ref_cnt++;
		return 0;
	}

	priv->fs.udp = kzalloc(sizeof(*priv->fs.udp), GFP_KERNEL);
	if (!priv->fs.udp)
		return -ENOMEM;

	for (i = 0; i < FS_UDP_NUM_TYPES; i++) {
		err = fs_udp_create_table(priv, i);
		if (err)
			goto err_destroy_tables;
	}

	err = fs_udp_enable(priv);
	if (err)
		goto err_destroy_tables;

	priv->fs.udp->ref_cnt = 1;

	return 0;

err_destroy_tables:
	while (--i >= 0)
		fs_udp_destroy_table(priv->fs.udp, i);

	kfree(priv->fs.udp);
	priv->fs.udp = NULL;
	return err;
}

static void fs_any_set_ethertype_flow(struct mlx5_flow_spec *spec, u16 ether_type)
{
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ethertype);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ethertype, ether_type);
}

struct mlx5_flow_handle *
mlx5e_fs_tt_redirect_any_add_rule(struct mlx5e_priv *priv,
				  u32 tir_num, u16 ether_type)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_table *ft = NULL;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_fs_any *fs_any;
	int err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	fs_any = priv->fs.any;
	ft = fs_any->table.t;

	fs_any_set_ethertype_flow(spec, ether_type);
	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tir_num;

	rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	kvfree(spec);

	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev, "%s: add ANY rule failed, err %d\n",
			   __func__, err);
	}
	return rule;
}

static int fs_any_add_default_rule(struct mlx5e_priv *priv)
{
	struct mlx5e_flow_table *fs_any_t;
	struct mlx5_flow_destination dest;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5e_fs_any *fs_any;
	int err;

	fs_any = priv->fs.any;
	fs_any_t = &fs_any->table;

	dest = mlx5e_ttc_get_default_dest(priv, MLX5_TT_ANY);
	rule = mlx5_add_flow_rules(fs_any_t->t, NULL, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev,
			   "%s: add default rule failed, fs type=ANY, err %d\n",
			   __func__, err);
		return err;
	}

	fs_any->default_rule = rule;
	return 0;
}

#define MLX5E_FS_ANY_NUM_GROUPS	(2)
#define MLX5E_FS_ANY_GROUP1_SIZE	(BIT(16))
#define MLX5E_FS_ANY_GROUP2_SIZE	(BIT(0))
#define MLX5E_FS_ANY_TABLE_SIZE		(MLX5E_FS_ANY_GROUP1_SIZE +\
					 MLX5E_FS_ANY_GROUP2_SIZE)

static int fs_any_create_groups(struct mlx5e_flow_table *ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	void *outer_headers_c;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_FS_UDP_NUM_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	in = kvzalloc(inlen, GFP_KERNEL);
	if  (!in || !ft->g) {
		kfree(ft->g);
		kvfree(in);
		return -ENOMEM;
	}

	/* Match on ethertype */
	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc, outer_headers);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ethertype);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_FS_ANY_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Default Flow Group */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_FS_ANY_GROUP2_SIZE;
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
	kvfree(in);

	return err;
}

static int fs_any_create_table(struct mlx5e_priv *priv)
{
	struct mlx5e_flow_table *ft = &priv->fs.any->table;
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft->num_groups = 0;

	ft_attr.max_fte = MLX5E_FS_UDP_TABLE_SIZE;
	ft_attr.level = MLX5E_FS_TT_ANY_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	netdev_dbg(priv->netdev, "Created fs ANY table id %u level %u\n",
		   ft->t->id, ft->t->level);

	err = fs_any_create_groups(ft);
	if (err)
		goto err;

	err = fs_any_add_default_rule(priv);
	if (err)
		goto err;

	return 0;

err:
	mlx5e_destroy_flow_table(ft);
	return err;
}

static int fs_any_disable(struct mlx5e_priv *priv)
{
	int err;

	/* Modify ttc rules destination to point back to the indir TIRs */
	err = mlx5e_ttc_fwd_default_dest(priv, MLX5_TT_ANY);
	if (err) {
		netdev_err(priv->netdev,
			   "%s: modify ttc[%d] default destination failed, err(%d)\n",
			   __func__, MLX5_TT_ANY, err);
		return err;
	}
	return 0;
}

static int fs_any_enable(struct mlx5e_priv *priv)
{
	struct mlx5_flow_destination dest = {};
	int err;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fs.any->table.t;

	/* Modify ttc rules destination to point on the accel_fs FTs */
	err = mlx5e_ttc_fwd_dest(priv, MLX5_TT_ANY, &dest);
	if (err) {
		netdev_err(priv->netdev,
			   "%s: modify ttc[%d] destination to accel failed, err(%d)\n",
			   __func__, MLX5_TT_ANY, err);
		return err;
	}
	return 0;
}

static void fs_any_destroy_table(struct mlx5e_fs_any *fs_any)
{
	if (IS_ERR_OR_NULL(fs_any->table.t))
		return;

	mlx5_del_flow_rules(fs_any->default_rule);
	mlx5e_destroy_flow_table(&fs_any->table);
	fs_any->table.t = NULL;
}

void mlx5e_fs_tt_redirect_any_destroy(struct mlx5e_priv *priv)
{
	struct mlx5e_fs_any *fs_any = priv->fs.any;

	if (!fs_any)
		return;

	if (--fs_any->ref_cnt)
		return;

	fs_any_disable(priv);

	fs_any_destroy_table(fs_any);

	kfree(fs_any);
	priv->fs.any = NULL;
}

int mlx5e_fs_tt_redirect_any_create(struct mlx5e_priv *priv)
{
	int err;

	if (priv->fs.any) {
		priv->fs.any->ref_cnt++;
		return 0;
	}

	priv->fs.any = kzalloc(sizeof(*priv->fs.any), GFP_KERNEL);
	if (!priv->fs.any)
		return -ENOMEM;

	err = fs_any_create_table(priv);
	if (err)
		return err;

	err = fs_any_enable(priv);
	if (err)
		goto err_destroy_table;

	priv->fs.any->ref_cnt = 1;

	return 0;

err_destroy_table:
	fs_any_destroy_table(priv->fs.any);

	kfree(priv->fs.any);
	priv->fs.any = NULL;
	return err;
}
