// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "rx_res.h"
#include "channels.h"
#include "params.h"

static const struct mlx5e_rss_params_traffic_type rss_default_config[MLX5E_NUM_INDIR_TIRS] = {
	[MLX5_TT_IPV4_TCP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = MLX5_L4_PROT_TYPE_TCP,
		.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5_TT_IPV6_TCP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = MLX5_L4_PROT_TYPE_TCP,
		.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5_TT_IPV4_UDP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = MLX5_L4_PROT_TYPE_UDP,
		.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5_TT_IPV6_UDP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = MLX5_L4_PROT_TYPE_UDP,
		.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5_TT_IPV4_IPSEC_AH] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5_TT_IPV6_IPSEC_AH] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5_TT_IPV4_IPSEC_ESP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5_TT_IPV6_IPSEC_ESP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5_TT_IPV4] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP,
	},
	[MLX5_TT_IPV6] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP,
	},
};

struct mlx5e_rss_params_traffic_type
mlx5e_rss_get_default_tt_config(enum mlx5_traffic_types tt)
{
	return rss_default_config[tt];
}

struct mlx5e_rss {
	struct mlx5e_rss_params_hash hash;
	struct mlx5e_rss_params_indir indir;
	u32 rx_hash_fields[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_tir tir[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_tir inner_tir[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_rqt rqt;
};

struct mlx5e_rx_res {
	struct mlx5_core_dev *mdev;
	enum mlx5e_rx_res_features features;
	unsigned int max_nch;
	u32 drop_rqn;

	struct mlx5e_rss *rss;
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

struct mlx5e_rx_res *mlx5e_rx_res_alloc(void)
{
	return kvzalloc(sizeof(struct mlx5e_rx_res), GFP_KERNEL);
}

static void mlx5e_rx_res_rss_params_init(struct mlx5e_rx_res *res, unsigned int init_nch)
{
	struct mlx5e_rss *rss = res->rss;
	enum mlx5_traffic_types tt;

	rss->hash.hfunc = ETH_RSS_HASH_TOP;
	netdev_rss_key_fill(rss->hash.toeplitz_hash_key,
			    sizeof(rss->hash.toeplitz_hash_key));
	mlx5e_rss_params_indir_init_uniform(&rss->indir, init_nch);
	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		rss->rx_hash_fields[tt] =
			mlx5e_rss_get_default_tt_config(tt).rx_hash_fields;
}

static void mlx5e_rx_res_rss_destroy_tir(struct mlx5e_rx_res *res,
					 enum mlx5_traffic_types tt,
					 bool inner)
{
	struct mlx5e_rss *rss = res->rss;
	struct mlx5e_tir *tir;

	tir = inner ? &rss->inner_tir[tt] : &rss->tir[tt];
	mlx5e_tir_destroy(tir);
}

static int mlx5e_rx_res_rss_create_tir(struct mlx5e_rx_res *res,
				       struct mlx5e_tir_builder *builder,
				       enum mlx5_traffic_types tt,
				       const struct mlx5e_lro_param *init_lro_param,
				       bool inner)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_rss_params_traffic_type rss_tt;
	struct mlx5e_rss *rss = res->rss;
	struct mlx5e_tir *tir;
	u32 rqtn;
	int err;

	tir = inner ? &rss->inner_tir[tt] : &rss->tir[tt];

	rqtn = mlx5e_rqt_get_rqtn(&rss->rqt);
	mlx5e_tir_builder_build_rqt(builder, res->mdev->mlx5e_res.hw_objs.td.tdn,
				    rqtn, inner_ft_support);
	mlx5e_tir_builder_build_lro(builder, init_lro_param);
	rss_tt = mlx5e_rx_res_rss_get_current_tt_config(res, tt);
	mlx5e_tir_builder_build_rss(builder, &rss->hash, &rss_tt, inner);

	err = mlx5e_tir_init(tir, builder, res->mdev, true);
	if (err) {
		mlx5_core_warn(res->mdev, "Failed to create %sindirect TIR: err = %d, tt = %d\n",
			       inner ? "inner " : "", err, tt);
		return err;
	}

	return 0;
}

static int mlx5e_rx_res_rss_create_tirs(struct mlx5e_rx_res *res,
					const struct mlx5e_lro_param *init_lro_param,
					bool inner)
{
	enum mlx5_traffic_types tt, max_tt;
	struct mlx5e_tir_builder *builder;
	int err;

	builder = mlx5e_tir_builder_alloc(false);
	if (!builder)
		return -ENOMEM;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		err = mlx5e_rx_res_rss_create_tir(res, builder, tt, init_lro_param, inner);
		if (err)
			goto err_destroy_tirs;

		mlx5e_tir_builder_clear(builder);
	}

out:
	mlx5e_tir_builder_free(builder);
	return err;

err_destroy_tirs:
	max_tt = tt;
	for (tt = 0; tt < max_tt; tt++)
		mlx5e_rx_res_rss_destroy_tir(res, tt, inner);
	goto out;
}

static void mlx5e_rx_res_rss_destroy_tirs(struct mlx5e_rx_res *res, bool inner)
{
	enum mlx5_traffic_types tt;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		mlx5e_rx_res_rss_destroy_tir(res, tt, inner);
}

static int mlx5e_rx_res_rss_init(struct mlx5e_rx_res *res,
				 const struct mlx5e_lro_param *init_lro_param,
				 unsigned int init_nch)
{
	bool inner_ft_support = res->features & MLX5E_RX_RES_FEATURE_INNER_FT;
	struct mlx5e_rss *rss;
	int err;

	rss = kvzalloc(sizeof(*rss), GFP_KERNEL);
	if (!rss)
		return -ENOMEM;

	res->rss = rss;

	mlx5e_rx_res_rss_params_init(res, init_nch);

	err = mlx5e_rqt_init_direct(&rss->rqt, res->mdev, true, res->drop_rqn);
	if (err)
		goto err_free_rss;

	err = mlx5e_rx_res_rss_create_tirs(res, init_lro_param, false);
	if (err)
		goto err_destroy_rqt;

	if (inner_ft_support) {
		err = mlx5e_rx_res_rss_create_tirs(res, init_lro_param, true);
		if (err)
			goto err_destroy_tirs;
	}

	return 0;

err_destroy_tirs:
	mlx5e_rx_res_rss_destroy_tirs(res, false);

err_destroy_rqt:
	mlx5e_rqt_destroy(&rss->rqt);

err_free_rss:
	kvfree(rss);
	res->rss = NULL;
	return err;
}

static int mlx5e_rx_res_channels_init(struct mlx5e_rx_res *res,
				      const struct mlx5e_lro_param *init_lro_param)
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
		mlx5e_tir_builder_build_lro(builder, init_lro_param);
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
		mlx5e_tir_builder_build_lro(builder, init_lro_param);
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

static void mlx5e_rx_res_rss_destroy(struct mlx5e_rx_res *res)
{
	struct mlx5e_rss *rss = res->rss;

	mlx5e_rx_res_rss_destroy_tirs(res, false);

	if (res->features & MLX5E_RX_RES_FEATURE_INNER_FT)
		mlx5e_rx_res_rss_destroy_tirs(res, true);

	mlx5e_rqt_destroy(&rss->rqt);
	kvfree(rss);
	res->rss = NULL;
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
		      u32 drop_rqn, const struct mlx5e_lro_param *init_lro_param,
		      unsigned int init_nch)
{
	int err;

	res->mdev = mdev;
	res->features = features;
	res->max_nch = max_nch;
	res->drop_rqn = drop_rqn;

	err = mlx5e_rx_res_rss_init(res, init_lro_param, init_nch);
	if (err)
		return err;

	err = mlx5e_rx_res_channels_init(res, init_lro_param);
	if (err)
		goto err_rss_destroy;

	err = mlx5e_rx_res_ptp_init(res);
	if (err)
		goto err_channels_destroy;

	return 0;

err_channels_destroy:
	mlx5e_rx_res_channels_destroy(res);
err_rss_destroy:
	mlx5e_rx_res_rss_destroy(res);
	return err;
}

void mlx5e_rx_res_destroy(struct mlx5e_rx_res *res)
{
	mlx5e_rx_res_ptp_destroy(res);
	mlx5e_rx_res_channels_destroy(res);
	mlx5e_rx_res_rss_destroy(res);
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
	struct mlx5e_rss *rss = res->rss;

	return mlx5e_tir_get_tirn(&rss->tir[tt]);
}

u32 mlx5e_rx_res_get_tirn_rss_inner(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt)
{
	struct mlx5e_rss *rss = res->rss;

	WARN_ON(!(res->features & MLX5E_RX_RES_FEATURE_INNER_FT));
	return mlx5e_tir_get_tirn(&rss->inner_tir[tt]);
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

static void mlx5e_rx_res_rss_enable(struct mlx5e_rx_res *res)
{
	struct mlx5e_rss *rss = res->rss;
	int err;

	res->rss_active = true;

	err = mlx5e_rqt_redirect_indir(&rss->rqt, res->rss_rqns, res->rss_nch,
				       rss->hash.hfunc,
				       &rss->indir);
	if (err)
		mlx5_core_warn(res->mdev, "Failed to redirect RQT %#x to channels: err = %d\n",
			       mlx5e_rqt_get_rqtn(&rss->rqt), err);
}

static void mlx5e_rx_res_rss_disable(struct mlx5e_rx_res *res)
{
	struct mlx5e_rss *rss = res->rss;
	int err;

	res->rss_active = false;

	err = mlx5e_rqt_redirect_direct(&rss->rqt, res->drop_rqn);
	if (err)
		mlx5_core_warn(res->mdev, "Failed to redirect RQT %#x to drop RQ %#x: err = %d\n",
			       mlx5e_rqt_get_rqtn(&rss->rqt), res->drop_rqn, err);
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

		if (mlx5e_channels_get_ptp_rqn(chs, &rqn))
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

struct mlx5e_rss_params_traffic_type
mlx5e_rx_res_rss_get_current_tt_config(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt)
{
	struct mlx5e_rss_params_traffic_type rss_tt;
	struct mlx5e_rss *rss = res->rss;

	rss_tt = mlx5e_rss_get_default_tt_config(tt);
	rss_tt.rx_hash_fields = rss->rx_hash_fields[tt];
	return rss_tt;
}

/* Updates the indirection table SW shadow, does not update the HW resources yet */
void mlx5e_rx_res_rss_set_indir_uniform(struct mlx5e_rx_res *res, unsigned int nch)
{
	WARN_ON_ONCE(res->rss_active);
	mlx5e_rss_params_indir_init_uniform(&res->rss->indir, nch);
}

int mlx5e_rx_res_rss_get_rxfh(struct mlx5e_rx_res *res, u32 *indir, u8 *key, u8 *hfunc)
{
	struct mlx5e_rss *rss = res->rss;
	unsigned int i;

	if (indir)
		for (i = 0; i < MLX5E_INDIR_RQT_SIZE; i++)
			indir[i] = rss->indir.table[i];

	if (key)
		memcpy(key, rss->hash.toeplitz_hash_key,
		       sizeof(rss->hash.toeplitz_hash_key));

	if (hfunc)
		*hfunc = rss->hash.hfunc;

	return 0;
}

static int mlx5e_rx_res_rss_update_tir(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt,
				       bool inner)
{
	struct mlx5e_rss_params_traffic_type rss_tt;
	struct mlx5e_tir_builder *builder;
	struct mlx5e_rss *rss = res->rss;
	struct mlx5e_tir *tir;
	int err;

	builder = mlx5e_tir_builder_alloc(true);
	if (!builder)
		return -ENOMEM;

	rss_tt = mlx5e_rx_res_rss_get_current_tt_config(res, tt);

	mlx5e_tir_builder_build_rss(builder, &rss->hash, &rss_tt, inner);
	tir = inner ? &rss->inner_tir[tt] : &rss->tir[tt];
	err = mlx5e_tir_modify(tir, builder);

	mlx5e_tir_builder_free(builder);
	return err;
}

int mlx5e_rx_res_rss_set_rxfh(struct mlx5e_rx_res *res, const u32 *indir,
			      const u8 *key, const u8 *hfunc)
{
	struct mlx5e_rss *rss = res->rss;
	enum mlx5_traffic_types tt;
	bool changed_indir = false;
	bool changed_hash = false;
	int err;

	if (hfunc && *hfunc != rss->hash.hfunc) {
		switch (*hfunc) {
		case ETH_RSS_HASH_XOR:
		case ETH_RSS_HASH_TOP:
			break;
		default:
			return -EINVAL;
		}
		changed_hash = true;
		changed_indir = true;
		rss->hash.hfunc = *hfunc;
	}

	if (key) {
		if (rss->hash.hfunc == ETH_RSS_HASH_TOP)
			changed_hash = true;
		memcpy(rss->hash.toeplitz_hash_key, key,
		       sizeof(rss->hash.toeplitz_hash_key));
	}

	if (indir) {
		unsigned int i;

		changed_indir = true;

		for (i = 0; i < MLX5E_INDIR_RQT_SIZE; i++)
			rss->indir.table[i] = indir[i];
	}

	if (changed_indir && res->rss_active) {
		err = mlx5e_rqt_redirect_indir(&rss->rqt, res->rss_rqns, res->rss_nch,
					       rss->hash.hfunc, &rss->indir);
		if (err)
			mlx5_core_warn(res->mdev, "Failed to redirect indirect RQT %#x to channels: err = %d\n",
				       mlx5e_rqt_get_rqtn(&rss->rqt), err);
	}

	if (changed_hash)
		for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
			err = mlx5e_rx_res_rss_update_tir(res, tt, false);
			if (err)
				mlx5_core_warn(res->mdev, "Failed to update RSS hash of indirect TIR for traffic type %d: err = %d\n",
					       tt, err);

			if (!(res->features & MLX5E_RX_RES_FEATURE_INNER_FT))
				continue;

			err = mlx5e_rx_res_rss_update_tir(res, tt, true);
			if (err)
				mlx5_core_warn(res->mdev, "Failed to update RSS hash of inner indirect TIR for traffic type %d: err = %d\n",
					       tt, err);
		}

	return 0;
}

u8 mlx5e_rx_res_rss_get_hash_fields(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt)
{
	struct mlx5e_rss *rss = res->rss;

	return rss->rx_hash_fields[tt];
}

int mlx5e_rx_res_rss_set_hash_fields(struct mlx5e_rx_res *res, enum mlx5_traffic_types tt,
				     u8 rx_hash_fields)
{
	struct mlx5e_rss *rss = res->rss;
	u8 old_rx_hash_fields;
	int err;

	old_rx_hash_fields = rss->rx_hash_fields[tt];

	if (old_rx_hash_fields == rx_hash_fields)
		return 0;

	rss->rx_hash_fields[tt] = rx_hash_fields;

	err = mlx5e_rx_res_rss_update_tir(res, tt, false);
	if (err) {
		rss->rx_hash_fields[tt] = old_rx_hash_fields;
		mlx5_core_warn(res->mdev, "Failed to update RSS hash fields of indirect TIR for traffic type %d: err = %d\n",
			       tt, err);
		return err;
	}

	if (!(res->features & MLX5E_RX_RES_FEATURE_INNER_FT))
		return 0;

	err = mlx5e_rx_res_rss_update_tir(res, tt, true);
	if (err) {
		/* Partial update happened. Try to revert - it may fail too, but
		 * there is nothing more we can do.
		 */
		rss->rx_hash_fields[tt] = old_rx_hash_fields;
		mlx5_core_warn(res->mdev, "Failed to update RSS hash fields of inner indirect TIR for traffic type %d: err = %d\n",
			       tt, err);
		if (mlx5e_rx_res_rss_update_tir(res, tt, false))
			mlx5_core_warn(res->mdev,
				       "Partial update of RSS hash fields happened: failed to revert indirect TIR for traffic type %d to the old values\n",
				       tt);
	}

	return err;
}

int mlx5e_rx_res_lro_set_param(struct mlx5e_rx_res *res, struct mlx5e_lro_param *lro_param)
{
	struct mlx5e_rss *rss = res->rss;
	struct mlx5e_tir_builder *builder;
	enum mlx5_traffic_types tt;
	int err, final_err;
	unsigned int ix;

	builder = mlx5e_tir_builder_alloc(true);
	if (!builder)
		return -ENOMEM;

	mlx5e_tir_builder_build_lro(builder, lro_param);

	final_err = 0;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		err = mlx5e_tir_modify(&rss->tir[tt], builder);
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to update LRO state of indirect TIR %#x for traffic type %d: err = %d\n",
				       mlx5e_tir_get_tirn(&rss->tir[tt]), tt, err);
			if (!final_err)
				final_err = err;
		}

		if (!(res->features & MLX5E_RX_RES_FEATURE_INNER_FT))
			continue;

		err = mlx5e_tir_modify(&rss->inner_tir[tt], builder);
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to update LRO state of inner indirect TIR %#x for traffic type %d: err = %d\n",
				       mlx5e_tir_get_tirn(&rss->inner_tir[tt]), tt, err);
			if (!final_err)
				final_err = err;
		}
	}

	for (ix = 0; ix < res->max_nch; ix++) {
		err = mlx5e_tir_modify(&res->channels[ix].direct_tir, builder);
		if (err) {
			mlx5_core_warn(res->mdev, "Failed to update LRO state of direct TIR %#x for channel %u: err = %d\n",
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
	return res->rss->hash;
}
