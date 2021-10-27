// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "rx_res.h"
#include "channels.h"
#include "params.h"

#define MLX5E_MAX_NUM_RSS 16

struct mlx5e_rx_res {
	struct mlx5_core_dev *mdev;
	enum mlx5e_rx_res_features features;
	unsigned int max_nch;
	u32 drop_rqn;

	struct mlx5e_rss *rss[MLX5E_MAX_NUM_RSS];
	bool rss_active;
	u32 rss_rqns[MLX5E_INDIR_RQT_SIZE];
	unsigned int rss_nch;

	struct {
		struct mlx5e_rqt direct_rqt;
		struct mlx5e_tir direct_tir;
		struct mlx5e_rqt xsk_rqt;
		struct mlx5e_tir xsk_tir;
	} *channels;

	struct {
		struct mlx5e_rqt rqt;
		struct mlx5e_tir tir;
	} ptp;
};

/* API for rx_res_rss_* */

static int mlx5e_rx_res_rss_init_def(struct mlx5e_rx_res *res,
				     const struct mlx5e_packet_merge_param *init_pkt_merge_param,
				     unsigned int init_nch)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_rss *rss;
	int err;

	if (WARN_ON(res->rss[0]))
		return -EINVAL;

	rss = mlx5e_rss_alloc();
	if (!rss)
		return -ENOMEM;

	err = mlx5e_rss_init(rss, res->mdev, inner_ft_support, res->drop_rqn,
			     init_pkt_merge_param);
	if (err)
		goto err_rss_free;

	mlx5e_rss_set_indir_uniform(rss, init_nch);

	res->rss[0] = rss;

	return 0;

err_rss_free:
	mlx5e_rss_free(rss);
	return err;
}

int mlx5e_rx_res_rss_init(struct mlx5e_rx_res *res, u32 *rss_idx, unsigned int init_nch)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_rss *rss;
	int err, i;

	for (i = 1; i < MLX5E_MAX_NUM_RSS; i++)
		if (!res->rss[i])
			break;

	if (i == MLX5E_MAX_NUM_RSS)
		return -ENOSPC;

	rss = mlx5e_rss_alloc();
	if (!rss)
		return -ENOMEM;

	err = mlx5e_rss_init_no_tirs(rss, res->mdev, inner_ft_support, res->drop_rqn);
	if (err)
		goto err_rss_free;

	mlx5e_rss_set_indir_uniform(rss, init_nch);
	if (res->rss_active)
		mlx5e_rss_enable(rss, res->rss_rqns, res->rss_nch);

	res->rss[i] = rss;
	*rss_idx = i;

	return 0;

err_rss_free:
	mlx5e_rss_free(rss);
	return err;
}

static int __mlx5e_rx_res_rss_destroy(struct mlx5e_rx_res *res, u32 rss_idx)
{
	struct mlx5e_rss *rss = res->rss[rss_idx];
	int err;

	err = mlx5e_rss_cleanup(rss);
	if (err)
		return err;

	mlx5e_rss_free(rss);
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

		if (!rss)
			continue;
		mlx5e_rss_enable(rss, res->rss_rqns, res->rss_nch);
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
			      u32 *indir, u8 *key, u8 *hfunc)
{
	struct mlx5e_rss *rss;

	if (rss_idx >= MLX5E_MAX_NUM_RSS)
		return -EINVAL;

	rss = res->rss[rss_idx];
	if (!rss)
		return -ENOENT;

	return mlx5e_rss_get_rxfh(rss, indir, key, hfunc);
}

int mlx5e_rx_res_rss_set_rxfh(struct mlx5e_rx_res *res, u32 rss_idx,
			      const u32 *indir, const u8 *key, const u8 *hfunc)
{
	struct mlx5e_rss *rss;

	if (rss_idx >= MLX5E_MAX_NUM_RSS)
		return -EINVAL;

	rss = res->rss[rss_idx];
	if (!rss)
		return -ENOENT;

	return mlx5e_rss_set_rxfh(rss, indir, key, hfunc, res->rss_rqns, res->rss_nch);
}

u8 mlx5e_rx_res_rss_get_hash_fields(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt)
{
	struct mlx5e_rss *rss = res->rss[0];

	return mlx5e_rss_get_hash_fields(rss, tt);
}

int mlx5e_rx_res_rss_set_hash_fields(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt,
				     u8 rx_hash_fields)
{
	struct mlx5e_rss *rss = res->rss[0];

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

struct mlx5e_rx_res *mlx5e_rx_res_alloc(void)
{
	return kvzalloc(sizeof(struct mlx5e_rx_res), GFP_KERNEL);
}

static int mlx5e_rx_res_channels_init(struct mlx5e_rx_res *res,
				      const struct mlx5e_packet_merge_param *init_pkt_merge_param)
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
					    res->mdev, false, res->drop_rqn);
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
		mlx5e_tir_builder_build_packet_merge(builder, init_pkt_merge_param);
		mlx5e_tir_builder_build_direct(builder);

		err = mlx5e_tir_init(&res->channels[ix].direct_tir, builder, res->mdev, true);
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to create a direct TIR: err = %d, ix = %u\n",
				       err, ix);
			goto err_destroy_direct_tirs;
		}

		mlx5e_tir_builder_clear(builder);
	}

	if (!(res->features & MLX5E_RX_RES_FEATURE_XSK))
		goto out;

	for (ix = 0; ix < res->max_nch; ix++) {
		err = mlx5e_rqt_init_direct(&res->channels[ix].xsk_rqt,
					    res->mdev, false, res->drop_rqn);
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to create an XSK RQT: err = %d, ix = %u\n",
				       err, ix);
			goto err_destroy_xsk_rqts;
		}
	}

	for (ix = 0; ix < res->max_nch; ix++) {
		mlx5e_tir_builder_build_rqt(builder, res->mdev->mlx5e_res.hw_objs.td.tdn,
					    mlx5e_rqt_get_rqtn(&res->channels[ix].xsk_rqt),
					    inner_ft_support);
		mlx5e_tir_builder_build_packet_merge(builder, init_pkt_merge_param);
		mlx5e_tir_builder_build_direct(builder);

		err = mlx5e_tir_init(&res->channels[ix].xsk_tir, builder, res->mdev, true);
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to create an XSK TIR: err = %d, ix = %u\n",
				       err, ix);
			goto err_destroy_xsk_tirs;
		}

		mlx5e_tir_builder_clear(builder);
	}

	goto out;

err_destroy_xsk_tirs:
	while (--ix >= 0)
		mlx5e_tir_destroy(&res->channels[ix].xsk_tir);

	ix = res->max_nch;
err_destroy_xsk_rqts:
	while (--ix >= 0)
		mlx5e_rqt_destroy(&res->channels[ix].xsk_rqt);

	ix = res->max_nch;
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

	err = mlx5e_rqt_init_direct(&res->ptp.rqt, res->mdev, false, res->drop_rqn);
	if (err)
		goto out;

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

		if (!(res->features & MLX5E_RX_RES_FEATURE_XSK))
			continue;

		mlx5e_tir_destroy(&res->channels[ix].xsk_tir);
		mlx5e_rqt_destroy(&res->channels[ix].xsk_rqt);
	}

	kvfree(res->channels);
}

static void mlx5e_rx_res_ptp_destroy(struct mlx5e_rx_res *res)
{
	mlx5e_tir_destroy(&res->ptp.tir);
	mlx5e_rqt_destroy(&res->ptp.rqt);
}

int mlx5e_rx_res_init(struct mlx5e_rx_res *res, struct mlx5_core_dev *mdev,
		      enum mlx5e_rx_res_features features, unsigned int max_nch,
		      u32 drop_rqn, const struct mlx5e_packet_merge_param *init_pkt_merge_param,
		      unsigned int init_nch)
{
	int err;

	res->mdev = mdev;
	res->features = features;
	res->max_nch = max_nch;
	res->drop_rqn = drop_rqn;

	err = mlx5e_rx_res_rss_init_def(res, init_pkt_merge_param, init_nch);
	if (err)
		goto err_out;

	err = mlx5e_rx_res_channels_init(res, init_pkt_merge_param);
	if (err)
		goto err_rss_destroy;

	err = mlx5e_rx_res_ptp_init(res);
	if (err)
		goto err_channels_destroy;

	return 0;

err_channels_destroy:
	mlx5e_rx_res_channels_destroy(res);
err_rss_destroy:
	__mlx5e_rx_res_rss_destroy(res, 0);
err_out:
	return err;
}

void mlx5e_rx_res_destroy(struct mlx5e_rx_res *res)
{
	mlx5e_rx_res_ptp_destroy(res);
	mlx5e_rx_res_channels_destroy(res);
	mlx5e_rx_res_rss_destroy_all(res);
}

void mlx5e_rx_res_free(struct mlx5e_rx_res *res)
{
	kvfree(res);
}

u32 mlx5e_rx_res_get_tirn_direct(struct mlx5e_rx_res *res, unsigned int ix)
{
	return mlx5e_tir_get_tirn(&res->channels[ix].direct_tir);
}

u32 mlx5e_rx_res_get_tirn_xsk(struct mlx5e_rx_res *res, unsigned int ix)
{
	WARN_ON(!(res->features & MLX5E_RX_RES_FEATURE_XSK));

	return mlx5e_tir_get_tirn(&res->channels[ix].xsk_tir);
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

void mlx5e_rx_res_channels_activate(struct mlx5e_rx_res *res, struct mlx5e_channels *chs)
{
	unsigned int nch, ix;
	int err;

	nch = mlx5e_channels_get_num(chs);

	for (ix = 0; ix < chs->num; ix++)
		mlx5e_channels_get_regular_rqn(chs, ix, &res->rss_rqns[ix]);
	res->rss_nch = chs->num;

	mlx5e_rx_res_rss_enable(res);

	for (ix = 0; ix < nch; ix++) {
		u32 rqn;

		mlx5e_channels_get_regular_rqn(chs, ix, &rqn);
		err = mlx5e_rqt_redirect_direct(&res->channels[ix].direct_rqt, rqn);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect direct RQT %#x to RQ %#x (channel %u): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->channels[ix].direct_rqt),
				       rqn, ix, err);

		if (!(res->features & MLX5E_RX_RES_FEATURE_XSK))
			continue;

		if (!mlx5e_channels_get_xsk_rqn(chs, ix, &rqn))
			rqn = res->drop_rqn;
		err = mlx5e_rqt_redirect_direct(&res->channels[ix].xsk_rqt, rqn);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect XSK RQT %#x to RQ %#x (channel %u): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->channels[ix].xsk_rqt),
				       rqn, ix, err);
	}
	for (ix = nch; ix < res->max_nch; ix++) {
		err = mlx5e_rqt_redirect_direct(&res->channels[ix].direct_rqt, res->drop_rqn);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect direct RQT %#x to drop RQ %#x (channel %u): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->channels[ix].direct_rqt),
				       res->drop_rqn, ix, err);

		if (!(res->features & MLX5E_RX_RES_FEATURE_XSK))
			continue;

		err = mlx5e_rqt_redirect_direct(&res->channels[ix].xsk_rqt, res->drop_rqn);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect XSK RQT %#x to drop RQ %#x (channel %u): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->channels[ix].xsk_rqt),
				       res->drop_rqn, ix, err);
	}

	if (res->features & MLX5E_RX_RES_FEATURE_PTP) {
		u32 rqn;

		if (!mlx5e_channels_get_ptp_rqn(chs, &rqn))
			rqn = res->drop_rqn;

		err = mlx5e_rqt_redirect_direct(&res->ptp.rqt, rqn);
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

	for (ix = 0; ix < res->max_nch; ix++) {
		err = mlx5e_rqt_redirect_direct(&res->channels[ix].direct_rqt, res->drop_rqn);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect direct RQT %#x to drop RQ %#x (channel %u): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->channels[ix].direct_rqt),
				       res->drop_rqn, ix, err);

		if (!(res->features & MLX5E_RX_RES_FEATURE_XSK))
			continue;

		err = mlx5e_rqt_redirect_direct(&res->channels[ix].xsk_rqt, res->drop_rqn);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect XSK RQT %#x to drop RQ %#x (channel %u): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->channels[ix].xsk_rqt),
				       res->drop_rqn, ix, err);
	}

	if (res->features & MLX5E_RX_RES_FEATURE_PTP) {
		err = mlx5e_rqt_redirect_direct(&res->ptp.rqt, res->drop_rqn);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect direct RQT %#x to drop RQ %#x (PTP): err = %d\n",
				       mlx5e_rqt_get_rqtn(&res->ptp.rqt),
				       res->drop_rqn, err);
	}
}

int mlx5e_rx_res_xsk_activate(struct mlx5e_rx_res *res, struct mlx5e_channels *chs,
			      unsigned int ix)
{
	u32 rqn;
	int err;

	if (!mlx5e_channels_get_xsk_rqn(chs, ix, &rqn))
		return -EINVAL;

	err = mlx5e_rqt_redirect_direct(&res->channels[ix].xsk_rqt, rqn);
	if (err)
		mlx5_core_warn(res->mdev, "Failed to redirect XSK RQT %#x to XSK RQ %#x (channel %u): err = %d\n",
			       mlx5e_rqt_get_rqtn(&res->channels[ix].xsk_rqt),
			       rqn, ix, err);
	return err;
}

int mlx5e_rx_res_xsk_deactivate(struct mlx5e_rx_res *res, unsigned int ix)
{
	int err;

	err = mlx5e_rqt_redirect_direct(&res->channels[ix].xsk_rqt, res->drop_rqn);
	if (err)
		mlx5_core_warn(res->mdev, "Failed to redirect XSK RQT %#x to drop RQ %#x (channel %u): err = %d\n",
			       mlx5e_rqt_get_rqtn(&res->channels[ix].xsk_rqt),
			       res->drop_rqn, ix, err);
	return err;
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

	mlx5e_tir_builder_free(builder);
	return final_err;
}

struct mlx5e_rss_params_hash mlx5e_rx_res_get_current_hash(struct mlx5e_rx_res *res)
{
	return mlx5e_rss_get_hash(res->rss[0]);
}
