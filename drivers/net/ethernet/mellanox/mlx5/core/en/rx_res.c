// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "rx_res.h"
#include "channels.h"
#include "params.h"

struct mlx5e_rx_res {
	struct mlx5_core_dev *mdev; /* primary */
	enum mlx5e_rx_res_features features;
	unsigned int max_nch;
	u32 drop_rqn;

	struct mlx5e_packet_merge_param pkt_merge_param;
	struct rw_semaphore pkt_merge_param_sem;

	struct mlx5e_rss *rss[MLX5E_MAX_NUM_RSS];
	bool rss_active;
	u32 *rss_rqns;
	u32 *rss_vhca_ids;
	unsigned int rss_nch;

	struct {
		struct mlx5e_rqt direct_rqt;
		struct mlx5e_tir direct_tir;
	} *channels;

	struct {
		struct mlx5e_rqt rqt;
		struct mlx5e_tir tir;
	} ptp;
};

/* API for rx_res_rss_* */

static u32 *get_vhca_ids(struct mlx5e_rx_res *res, int offset)
{
	bool multi_vhca = res->features & MLX5E_RX_RES_FEATURE_MULTI_VHCA;

	return multi_vhca ? res->rss_vhca_ids + offset : NULL;
}

void mlx5e_rx_res_rss_update_num_channels(struct mlx5e_rx_res *res, u32 nch)
{
	int i;

	for (i = 0; i < MLX5E_MAX_NUM_RSS; i++) {
		if (res->rss[i])
			mlx5e_rss_params_indir_modify_actual_size(res->rss[i], nch);
	}
}

static int mlx5e_rx_res_rss_init_def(struct mlx5e_rx_res *res,
				     unsigned int init_nch)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_rss *rss;

	if (WARN_ON(res->rss[0]))
		return -EINVAL;

	rss = mlx5e_rss_init(res->mdev, inner_ft_support, res->drop_rqn,
			     &res->pkt_merge_param, MLX5E_RSS_INIT_TIRS, init_nch, res->max_nch);
	if (IS_ERR(rss))
		return PTR_ERR(rss);

	mlx5e_rss_set_indir_uniform(rss, init_nch);

	res->rss[0] = rss;

	return 0;
}

int mlx5e_rx_res_rss_init(struct mlx5e_rx_res *res, u32 *rss_idx, unsigned int init_nch)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_rss *rss;
	int i;

	for (i = 1; i < MLX5E_MAX_NUM_RSS; i++)
		if (!res->rss[i])
			break;

	if (i == MLX5E_MAX_NUM_RSS)
		return -ENOSPC;

	rss = mlx5e_rss_init(res->mdev, inner_ft_support, res->drop_rqn,
			     &res->pkt_merge_param, MLX5E_RSS_INIT_NO_TIRS, init_nch,
			     res->max_nch);
	if (IS_ERR(rss))
		return PTR_ERR(rss);

	mlx5e_rss_set_indir_uniform(rss, init_nch);
	if (res->rss_active) {
		u32 *vhca_ids = get_vhca_ids(res, 0);

		mlx5e_rss_enable(rss, res->rss_rqns, vhca_ids, res->rss_nch);
	}

	res->rss[i] = rss;
	*rss_idx = i;

	return 0;
}

static int __mlx5e_rx_res_rss_destroy(struct mlx5e_rx_res *res, u32 rss_idx)
{
	struct mlx5e_rss *rss = res->rss[rss_idx];
	int err;

	err = mlx5e_rss_cleanup(rss);
	if (err)
		return err;

	res->rss[rss_idx] = NULL;

	return 0;
}

int mlx5e_rx_res_rss_destroy(struct mlx5e_rx_res *res, u32 rss_idx)
{
	struct mlx5e_rss *rss;

	if (rss_idx >= MLX5E_MAX_NUM_RSS)
		return -EINVAL;

	rss = res->rss[rss_idx];
	if (!rss)
		return -EINVAL;

	return __mlx5e_rx_res_rss_destroy(res, rss_idx);
}

static void mlx5e_rx_res_rss_destroy_all(struct mlx5e_rx_res *res)
{
	int i;

	for (i = 0; i < MLX5E_MAX_NUM_RSS; i++) {
		struct mlx5e_rss *rss = res->rss[i];
		int err;

		if (!rss)
			continue;

		err = __mlx5e_rx_res_rss_destroy(res, i);
		if (err) {
			unsigned int refcount;

			refcount = mlx5e_rss_refcnt_read(rss);
			mlx5_core_warn(res->mdev,
				       "Failed to destroy RSS context %d, refcount = %u, err = %d\n",
				       i, refcount, err);
		}
	}
}

static void mlx5e_rx_res_rss_enable(struct mlx5e_rx_res *res)
{
	int i;

	res->rss_active = true;

	for (i = 0; i < MLX5E_MAX_NUM_RSS; i++) {
		struct mlx5e_rss *rss = res->rss[i];
		u32 *vhca_ids;

		if (!rss)
			continue;
		vhca_ids = get_vhca_ids(res, 0);
		mlx5e_rss_enable(rss, res->rss_rqns, vhca_ids, res->rss_nch);
	}
}

static void mlx5e_rx_res_rss_disable(struct mlx5e_rx_res *res)
{
	int i;

	res->rss_active = false;

	for (i = 0; i < MLX5E_MAX_NUM_RSS; i++) {
		struct mlx5e_rss *rss = res->rss[i];

		if (!rss)
			continue;
		mlx5e_rss_disable(rss);
	}
}

/* Updates the indirection table SW shadow, does not update the HW resources yet */
void mlx5e_rx_res_rss_set_indir_uniform(struct mlx5e_rx_res *res, unsigned int nch)
{
	WARN_ON_ONCE(res->rss_active);
	mlx5e_rss_set_indir_uniform(res->rss[0], nch);
}

int mlx5e_rx_res_rss_get_rxfh(struct mlx5e_rx_res *res, u32 rss_idx,
			      u32 *indir, u8 *key, u8 *hfunc, bool *symmetric)
{
	struct mlx5e_rss *rss;

	if (rss_idx >= MLX5E_MAX_NUM_RSS)
		return -EINVAL;

	rss = res->rss[rss_idx];
	if (!rss)
		return -ENOENT;

	return mlx5e_rss_get_rxfh(rss, indir, key, hfunc, symmetric);
}

int mlx5e_rx_res_rss_set_rxfh(struct mlx5e_rx_res *res, u32 rss_idx,
			      const u32 *indir, const u8 *key, const u8 *hfunc,
			      const bool *symmetric)
{
	u32 *vhca_ids = get_vhca_ids(res, 0);
	struct mlx5e_rss *rss;

	if (rss_idx >= MLX5E_MAX_NUM_RSS)
		return -EINVAL;

	rss = res->rss[rss_idx];
	if (!rss)
		return -ENOENT;

	return mlx5e_rss_set_rxfh(rss, indir, key, hfunc, symmetric,
				  res->rss_rqns, vhca_ids, res->rss_nch);
}

int mlx5e_rx_res_rss_get_hash_fields(struct mlx5e_rx_res *res, u32 rss_idx,
				     enum mlx5_traffic_types tt)
{
	struct mlx5e_rss *rss;

	if (rss_idx >= MLX5E_MAX_NUM_RSS)
		return -EINVAL;

	rss = res->rss[rss_idx];
	if (!rss)
		return -ENOENT;

	return mlx5e_rss_get_hash_fields(rss, tt);
}

int mlx5e_rx_res_rss_set_hash_fields(struct mlx5e_rx_res *res, u32 rss_idx,
				     enum mlx5_traffic_types tt, u8 rx_hash_fields)
{
	struct mlx5e_rss *rss;

	if (rss_idx >= MLX5E_MAX_NUM_RSS)
		return -EINVAL;

	rss = res->rss[rss_idx];
	if (!rss)
		return -ENOENT;

	return mlx5e_rss_set_hash_fields(rss, tt, rx_hash_fields);
}

int mlx5e_rx_res_rss_cnt(struct mlx5e_rx_res *res)
{
	int i, cnt;

	cnt = 0;
	for (i = 0; i < MLX5E_MAX_NUM_RSS; i++)
		if (res->rss[i])
			cnt++;

	return cnt;
}

int mlx5e_rx_res_rss_index(struct mlx5e_rx_res *res, struct mlx5e_rss *rss)
{
	int i;

	if (!rss)
		return -EINVAL;

	for (i = 0; i < MLX5E_MAX_NUM_RSS; i++)
		if (rss == res->rss[i])
			return i;

	return -ENOENT;
}

struct mlx5e_rss *mlx5e_rx_res_rss_get(struct mlx5e_rx_res *res, u32 rss_idx)
{
	if (rss_idx >= MLX5E_MAX_NUM_RSS)
		return NULL;

	return res->rss[rss_idx];
}

/* End of API rx_res_rss_* */

static void mlx5e_rx_res_free(struct mlx5e_rx_res *res)
{
	kvfree(res->rss_vhca_ids);
	kvfree(res->rss_rqns);
	kvfree(res);
}

static struct mlx5e_rx_res *mlx5e_rx_res_alloc(struct mlx5_core_dev *mdev, unsigned int max_nch,
					       bool multi_vhca)
{
	struct mlx5e_rx_res *rx_res;

	rx_res = kvzalloc(sizeof(*rx_res), GFP_KERNEL);
	if (!rx_res)
		return NULL;

	rx_res->rss_rqns = kvcalloc(max_nch, sizeof(*rx_res->rss_rqns), GFP_KERNEL);
	if (!rx_res->rss_rqns) {
		kvfree(rx_res);
		return NULL;
	}

	if (multi_vhca) {
		rx_res->rss_vhca_ids = kvcalloc(max_nch, sizeof(*rx_res->rss_vhca_ids), GFP_KERNEL);
		if (!rx_res->rss_vhca_ids) {
			kvfree(rx_res->rss_rqns);
			kvfree(rx_res);
			return NULL;
		}
	}

	return rx_res;
}

static int mlx5e_rx_res_channels_init(struct mlx5e_rx_res *res)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_tir_builder *builder;
	int err = 0;
	int ix;

	builder = mlx5e_tir_builder_alloc(false);
	if (!builder)
		return -ENOMEM;

	res->channels = kvcalloc(res->max_nch, sizeof(*res->channels), GFP_KERNEL);
	if (!res->channels) {
		err = -ENOMEM;
		goto out;
	}

	for (ix = 0; ix < res->max_nch; ix++) {
		err = mlx5e_rqt_init_direct(&res->channels[ix].direct_rqt,
					    res->mdev, false, res->drop_rqn,
					    mlx5e_rqt_size(res->mdev, res->max_nch));
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to create a direct RQT: err = %d, ix = %u\n",
				       err, ix);
			goto err_destroy_direct_rqts;
		}
	}

	for (ix = 0; ix < res->max_nch; ix++) {
		mlx5e_tir_builder_build_rqt(builder, res->mdev->mlx5e_res.hw_objs.td.tdn,
					    mlx5e_rqt_get_rqtn(&res->channels[ix].direct_rqt),
					    inner_ft_support);
		mlx5e_tir_builder_build_packet_merge(builder, &res->pkt_merge_param);
		mlx5e_tir_builder_build_direct(builder);

		err = mlx5e_tir_init(&res->channels[ix].direct_tir, builder, res->mdev, true);
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to create a direct TIR: err = %d, ix = %u\n",
				       err, ix);
			goto err_destroy_direct_tirs;
		}

		mlx5e_tir_builder_clear(builder);
	}

	goto out;

err_destroy_direct_tirs:
	while (--ix >= 0)
		mlx5e_tir_destroy(&res->channels[ix].direct_tir);

	ix = res->max_nch;
err_destroy_direct_rqts:
	while (--ix >= 0)
		mlx5e_rqt_destroy(&res->channels[ix].direct_rqt);

	kvfree(res->channels);

out:
	mlx5e_tir_builder_free(builder);

	return err;
}

static int mlx5e_rx_res_ptp_init(struct mlx5e_rx_res *res)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_tir_builder *builder;
	int err;

	builder = mlx5e_tir_builder_alloc(false);
	if (!builder)
		return -ENOMEM;

	err = mlx5e_rqt_init_direct(&res->ptp.rqt, res->mdev, false, res->drop_rqn,
				    mlx5e_rqt_size(res->mdev, res->max_nch));
	if (err)
		goto out;

	/* Separated from the channels RQs, does not share pkt_merge state with them */
	mlx5e_tir_builder_build_rqt(builder, res->mdev->mlx5e_res.hw_objs.td.tdn,
				    mlx5e_rqt_get_rqtn(&res->ptp.rqt),
				    inner_ft_support);
	mlx5e_tir_builder_build_direct(builder);

	err = mlx5e_tir_init(&res->ptp.tir, builder, res->mdev, true);
	if (err)
		goto err_destroy_ptp_rqt;

	goto out;

err_destroy_ptp_rqt:
	mlx5e_rqt_destroy(&res->ptp.rqt);

out:
	mlx5e_tir_builder_free(builder);
	return err;
}

static void mlx5e_rx_res_channels_destroy(struct mlx5e_rx_res *res)
{
	unsigned int ix;

	for (ix = 0; ix < res->max_nch; ix++) {
		mlx5e_tir_destroy(&res->channels[ix].direct_tir);
		mlx5e_rqt_destroy(&res->channels[ix].direct_rqt);
	}

	kvfree(res->channels);
}

static void mlx5e_rx_res_ptp_destroy(struct mlx5e_rx_res *res)
{
	mlx5e_tir_destroy(&res->ptp.tir);
	mlx5e_rqt_destroy(&res->ptp.rqt);
}

struct mlx5e_rx_res *
mlx5e_rx_res_create(struct mlx5_core_dev *mdev, enum mlx5e_rx_res_features features,
		    unsigned int max_nch, u32 drop_rqn,
		    const struct mlx5e_packet_merge_param *init_pkt_merge_param,
		    unsigned int init_nch)
{
	bool multi_vhca = features & MLX5E_RX_RES_FEATURE_MULTI_VHCA;
	struct mlx5e_rx_res *res;
	int err;

	res = mlx5e_rx_res_alloc(mdev, max_nch, multi_vhca);
	if (!res)
		return ERR_PTR(-ENOMEM);

	res->mdev = mdev;
	res->features = features;
	res->max_nch = max_nch;
	res->drop_rqn = drop_rqn;

	res->pkt_merge_param = *init_pkt_merge_param;
	init_rwsem(&res->pkt_merge_param_sem);

	err = mlx5e_rx_res_rss_init_def(res, init_nch);
	if (err)
		goto err_rx_res_free;

	err = mlx5e_rx_res_channels_init(res);
	if (err)
		goto err_rss_destroy;

	err = mlx5e_rx_res_ptp_init(res);
	if (err)
		goto err_channels_destroy;

	return res;

err_channels_destroy:
	mlx5e_rx_res_channels_destroy(res);
err_rss_destroy:
	__mlx5e_rx_res_rss_destroy(res, 0);
err_rx_res_free:
	mlx5e_rx_res_free(res);
	return ERR_PTR(err);
}

void mlx5e_rx_res_destroy(struct mlx5e_rx_res *res)
{
	mlx5e_rx_res_ptp_destroy(res);
	mlx5e_rx_res_channels_destroy(res);
	mlx5e_rx_res_rss_destroy_all(res);
	mlx5e_rx_res_free(res);
}

unsigned int mlx5e_rx_res_get_max_nch(struct mlx5e_rx_res *res)
{
	return res->max_nch;
}

u32 mlx5e_rx_res_get_tirn_direct(struct mlx5e_rx_res *res, unsigned int ix)
{
	return mlx5e_tir_get_tirn(&res->channels[ix].direct_tir);
}

u32 mlx5e_rx_res_get_tirn_rss(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt)
{
	struct mlx5e_rss *rss = res->rss[0];

	return mlx5e_rss_get_tirn(rss, tt, false);
}

u32 mlx5e_rx_res_get_tirn_rss_inner(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt)
{
	struct mlx5e_rss *rss = res->rss[0];

	return mlx5e_rss_get_tirn(rss, tt, true);
}

u32 mlx5e_rx_res_get_tirn_ptp(struct mlx5e_rx_res *res)
{
	WARN_ON(!(res->features & MLX5E_RX_RES_FEATURE_PTP));
	return mlx5e_tir_get_tirn(&res->ptp.tir);
}

u32 mlx5e_rx_res_get_rqtn_direct(struct mlx5e_rx_res *res, unsigned int ix)
{
	return mlx5e_rqt_get_rqtn(&res->channels[ix].direct_rqt);
}

static void mlx5e_rx_res_channel_activate_direct(struct mlx5e_rx_res *res,
						 struct mlx5e_channels *chs,
						 unsigned int ix)
{
	u32 *vhca_id = get_vhca_ids(res, ix);
	u32 rqn = res->rss_rqns[ix];
	int err;

	err = mlx5e_rqt_redirect_direct(&res->channels[ix].direct_rqt, rqn, vhca_id);
	if (err)
		mlx5_core_warn(res->mdev, "Failed to redirect direct RQT %#x to RQ %#x (channel %u): err = %d\n",
			       mlx5e_rqt_get_rqtn(&res->channels[ix].direct_rqt),
			       rqn, ix, err);
}

static void mlx5e_rx_res_channel_deactivate_direct(struct mlx5e_rx_res *res,
						   unsigned int ix)
{
	int err;

	err = mlx5e_rqt_redirect_direct(&res->channels[ix].direct_rqt, res->drop_rqn, NULL);
	if (err)
		mlx5_core_warn(res->mdev, "Failed to redirect direct RQT %#x to drop RQ %#x (channel %u): err = %d\n",
			       mlx5e_rqt_get_rqtn(&res->channels[ix].direct_rqt),
			       res->drop_rqn, ix, err);
}

void mlx5e_rx_res_channels_activate(struct mlx5e_rx_res *res, struct mlx5e_channels *chs)
{
	unsigned int nch, ix;
	int err;

	nch = mlx5e_channels_get_num(chs);

	for (ix = 0; ix < chs->num; ix++) {
		u32 *vhca_id = get_vhca_ids(res, ix);

		if (mlx5e_channels_is_xsk(chs, ix))
			mlx5e_channels_get_xsk_rqn(chs, ix, &res->rss_rqns[ix], vhca_id);
		else
			mlx5e_channels_get_regular_rqn(chs, ix, &res->rss_rqns[ix], vhca_id);
	}
	res->rss_nch = chs->num;

	mlx5e_rx_res_rss_enable(res);

	for (ix = 0; ix < nch; ix++)
		mlx5e_rx_res_channel_activate_direct(res, chs, ix);
	for (ix = nch; ix < res->max_nch; ix++)
		mlx5e_rx_res_channel_deactivate_direct(res, ix);

	if (res->features & MLX5E_RX_RES_FEATURE_PTP) {
		u32 rqn;

		if (!mlx5e_channels_get_ptp_rqn(chs, &rqn))
			rqn = res->drop_rqn;

		err = mlx5e_rqt_redirect_direct(&res->ptp.rqt, rqn, NULL);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect direct RQT %#x to RQ %#x (PTP): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->ptp.rqt),
				       rqn, err);
	}
}

void mlx5e_rx_res_channels_deactivate(struct mlx5e_rx_res *res)
{
	unsigned int ix;
	int err;

	mlx5e_rx_res_rss_disable(res);

	for (ix = 0; ix < res->max_nch; ix++)
		mlx5e_rx_res_channel_deactivate_direct(res, ix);

	if (res->features & MLX5E_RX_RES_FEATURE_PTP) {
		err = mlx5e_rqt_redirect_direct(&res->ptp.rqt, res->drop_rqn, NULL);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect direct RQT %#x to drop RQ %#x (PTP): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->ptp.rqt),
				       res->drop_rqn, err);
	}
}

void mlx5e_rx_res_xsk_update(struct mlx5e_rx_res *res, struct mlx5e_channels *chs,
			     unsigned int ix, bool xsk)
{
	u32 *vhca_id = get_vhca_ids(res, ix);

	if (xsk)
		mlx5e_channels_get_xsk_rqn(chs, ix, &res->rss_rqns[ix], vhca_id);
	else
		mlx5e_channels_get_regular_rqn(chs, ix, &res->rss_rqns[ix], vhca_id);

	mlx5e_rx_res_rss_enable(res);

	mlx5e_rx_res_channel_activate_direct(res, chs, ix);
}

int mlx5e_rx_res_packet_merge_set_param(struct mlx5e_rx_res *res,
					struct mlx5e_packet_merge_param *pkt_merge_param)
{
	struct mlx5e_tir_builder *builder;
	int err, final_err;
	unsigned int ix;

	builder = mlx5e_tir_builder_alloc(true);
	if (!builder)
		return -ENOMEM;

	down_write(&res->pkt_merge_param_sem);
	res->pkt_merge_param = *pkt_merge_param;

	mlx5e_tir_builder_build_packet_merge(builder, pkt_merge_param);

	final_err = 0;

	for (ix = 0; ix < MLX5E_MAX_NUM_RSS; ix++) {
		struct mlx5e_rss *rss = res->rss[ix];

		if (!rss)
			continue;

		err = mlx5e_rss_packet_merge_set_param(rss, pkt_merge_param);
		if (err)
			final_err = final_err ? : err;
	}

	for (ix = 0; ix < res->max_nch; ix++) {
		err = mlx5e_tir_modify(&res->channels[ix].direct_tir, builder);
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to update packet merge state of direct TIR %#x for channel %u: err = %d\n",
				       mlx5e_tir_get_tirn(&res->channels[ix].direct_tir), ix, err);
			if (!final_err)
				final_err = err;
		}
	}

	up_write(&res->pkt_merge_param_sem);
	mlx5e_tir_builder_free(builder);
	return final_err;
}

struct mlx5e_rss_params_hash mlx5e_rx_res_get_current_hash(struct mlx5e_rx_res *res)
{
	return mlx5e_rss_get_hash(res->rss[0]);
}

int mlx5e_rx_res_tls_tir_create(struct mlx5e_rx_res *res, unsigned int rxq,
				struct mlx5e_tir *tir)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_tir_builder *builder;
	u32 rqtn;
	int err;

	builder = mlx5e_tir_builder_alloc(false);
	if (!builder)
		return -ENOMEM;

	rqtn = mlx5e_rx_res_get_rqtn_direct(res, rxq);

	mlx5e_tir_builder_build_rqt(builder, res->mdev->mlx5e_res.hw_objs.td.tdn, rqtn,
				    inner_ft_support);
	mlx5e_tir_builder_build_direct(builder);
	mlx5e_tir_builder_build_tls(builder);
	down_read(&res->pkt_merge_param_sem);
	mlx5e_tir_builder_build_packet_merge(builder, &res->pkt_merge_param);
	err = mlx5e_tir_init(tir, builder, res->mdev, false);
	up_read(&res->pkt_merge_param_sem);

	mlx5e_tir_builder_free(builder);

	return err;
}
