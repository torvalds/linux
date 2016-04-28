/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "en.h"
#include <linux/mlx5/fs.h>

static void arfs_destroy_table(struct arfs_table *arfs_t)
{
	mlx5_del_flow_rule(arfs_t->default_rule);
	mlx5e_destroy_flow_table(&arfs_t->ft);
}

void mlx5e_arfs_destroy_tables(struct mlx5e_priv *priv)
{
	int i;

	if (!(priv->netdev->hw_features & NETIF_F_NTUPLE))
		return;
	for (i = 0; i < ARFS_NUM_TYPES; i++) {
		if (!IS_ERR_OR_NULL(priv->fs.arfs.arfs_tables[i].ft.t))
			arfs_destroy_table(&priv->fs.arfs.arfs_tables[i]);
	}
}

static int arfs_add_default_rule(struct mlx5e_priv *priv,
				 enum arfs_type type)
{
	struct arfs_table *arfs_t = &priv->fs.arfs.arfs_tables[type];
	struct mlx5_flow_destination dest;
	u8 match_criteria_enable = 0;
	u32 *tirn = priv->indir_tirn;
	u32 *match_criteria;
	u32 *match_value;
	int err = 0;

	match_value	= mlx5_vzalloc(MLX5_ST_SZ_BYTES(fte_match_param));
	match_criteria	= mlx5_vzalloc(MLX5_ST_SZ_BYTES(fte_match_param));
	if (!match_value || !match_criteria) {
		netdev_err(priv->netdev, "%s: alloc failed\n", __func__);
		err = -ENOMEM;
		goto out;
	}

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	switch (type) {
	case ARFS_IPV4_TCP:
		dest.tir_num = tirn[MLX5E_TT_IPV4_TCP];
		break;
	case ARFS_IPV4_UDP:
		dest.tir_num = tirn[MLX5E_TT_IPV4_UDP];
		break;
	case ARFS_IPV6_TCP:
		dest.tir_num = tirn[MLX5E_TT_IPV6_TCP];
		break;
	case ARFS_IPV6_UDP:
		dest.tir_num = tirn[MLX5E_TT_IPV6_UDP];
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	arfs_t->default_rule = mlx5_add_flow_rule(arfs_t->ft.t, match_criteria_enable,
						  match_criteria, match_value,
						  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
						  MLX5_FS_DEFAULT_FLOW_TAG,
						  &dest);
	if (IS_ERR(arfs_t->default_rule)) {
		err = PTR_ERR(arfs_t->default_rule);
		arfs_t->default_rule = NULL;
		netdev_err(priv->netdev, "%s: add rule failed, arfs type=%d\n",
			   __func__, type);
	}
out:
	kvfree(match_criteria);
	kvfree(match_value);
	return err;
}

#define MLX5E_ARFS_NUM_GROUPS	2
#define MLX5E_ARFS_GROUP1_SIZE	BIT(12)
#define MLX5E_ARFS_GROUP2_SIZE	BIT(0)
#define MLX5E_ARFS_TABLE_SIZE	(MLX5E_ARFS_GROUP1_SIZE +\
				 MLX5E_ARFS_GROUP2_SIZE)
static int arfs_create_groups(struct mlx5e_flow_table *ft,
			      enum  arfs_type type)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	void *outer_headers_c;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_ARFS_NUM_GROUPS,
			sizeof(*ft->g), GFP_KERNEL);
	in = mlx5_vzalloc(inlen);
	if  (!in || !ft->g) {
		kvfree(ft->g);
		kvfree(in);
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc,
				       outer_headers);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ethertype);
	switch (type) {
	case ARFS_IPV4_TCP:
	case ARFS_IPV6_TCP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, tcp_dport);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, tcp_sport);
		break;
	case ARFS_IPV4_UDP:
	case ARFS_IPV6_UDP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, udp_dport);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, udp_sport);
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	switch (type) {
	case ARFS_IPV4_TCP:
	case ARFS_IPV4_UDP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c,
				 src_ipv4_src_ipv6.ipv4_layout.ipv4);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c,
				 dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
		break;
	case ARFS_IPV6_TCP:
	case ARFS_IPV6_UDP:
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
	ix += MLX5E_ARFS_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_ARFS_GROUP2_SIZE;
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

static int arfs_create_table(struct mlx5e_priv *priv,
			     enum arfs_type type)
{
	struct mlx5e_arfs_tables *arfs = &priv->fs.arfs;
	struct mlx5e_flow_table *ft = &arfs->arfs_tables[type].ft;
	int err;

	ft->t = mlx5_create_flow_table(priv->fs.ns, MLX5E_NIC_PRIO,
				       MLX5E_ARFS_TABLE_SIZE, MLX5E_ARFS_FT_LEVEL);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	err = arfs_create_groups(ft, type);
	if (err)
		goto err;

	err = arfs_add_default_rule(priv, type);
	if (err)
		goto err;

	return 0;
err:
	mlx5e_destroy_flow_table(ft);
	return err;
}

int mlx5e_arfs_create_tables(struct mlx5e_priv *priv)
{
	int err = 0;
	int i;

	if (!(priv->netdev->hw_features & NETIF_F_NTUPLE))
		return 0;

	for (i = 0; i < ARFS_NUM_TYPES; i++) {
		err = arfs_create_table(priv, i);
		if (err)
			goto err;
	}
	return 0;
err:
	mlx5e_arfs_destroy_tables(priv);
	return err;
}
