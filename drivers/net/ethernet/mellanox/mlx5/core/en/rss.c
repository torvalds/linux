// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES.

#include "rss.h"

#define mlx5e_rss_warn(__dev, format, ...)			\
	dev_warn((__dev)->device, "%s:%d:(pid %d): " format,	\
		 __func__, __LINE__, current->pid,		\
		 ##__VA_ARGS__)

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
	struct mlx5_core_dev *mdev;
	u32 drop_rqn;
	bool inner_ft_support;
	bool enabled;
};

struct mlx5e_rss *mlx5e_rss_alloc(void)
{
	return kvzalloc(sizeof(struct mlx5e_rss), GFP_KERNEL);
}

void mlx5e_rss_free(struct mlx5e_rss *rss)
{
	kvfree(rss);
}

static void mlx5e_rss_params_init(struct mlx5e_rss *rss)
{
	enum mlx5_traffic_types tt;

	rss->hash.hfunc = ETH_RSS_HASH_TOP;
	netdev_rss_key_fill(rss->hash.toeplitz_hash_key,
			    sizeof(rss->hash.toeplitz_hash_key));
	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		rss->rx_hash_fields[tt] =
			mlx5e_rss_get_default_tt_config(tt).rx_hash_fields;
}

static struct mlx5e_rss_params_traffic_type
mlx5e_rss_get_tt_config(struct mlx5e_rss *rss, enum mlx5_traffic_types tt)
{
	struct mlx5e_rss_params_traffic_type rss_tt;

	rss_tt = mlx5e_rss_get_default_tt_config(tt);
	rss_tt.rx_hash_fields = rss->rx_hash_fields[tt];
	return rss_tt;
}

static int mlx5e_rss_create_tir(struct mlx5e_rss *rss,
				enum mlx5_traffic_types tt,
				const struct mlx5e_lro_param *init_lro_param,
				bool inner)
{
	struct mlx5e_rss_params_traffic_type rss_tt;
	struct mlx5e_tir_builder *builder;
	struct mlx5e_tir *tir;
	u32 rqtn;
	int err;

	if (inner && !rss->inner_ft_support) {
		mlx5e_rss_warn(rss->mdev,
			       "Cannot create inner indirect TIR[%d], RSS inner FT is not supported.\n",
			       tt);
		return -EINVAL;
	}

	tir = inner ? &rss->inner_tir[tt] : &rss->tir[tt];

	builder = mlx5e_tir_builder_alloc(false);
	if (!builder)
		return -ENOMEM;

	rqtn = mlx5e_rqt_get_rqtn(&rss->rqt);
	mlx5e_tir_builder_build_rqt(builder, rss->mdev->mlx5e_res.hw_objs.td.tdn,
				    rqtn, rss->inner_ft_support);
	mlx5e_tir_builder_build_lro(builder, init_lro_param);
	rss_tt = mlx5e_rss_get_tt_config(rss, tt);
	mlx5e_tir_builder_build_rss(builder, &rss->hash, &rss_tt, inner);

	err = mlx5e_tir_init(tir, builder, rss->mdev, true);
	mlx5e_tir_builder_free(builder);
	if (err)
		mlx5e_rss_warn(rss->mdev, "Failed to create %sindirect TIR: err = %d, tt = %d\n",
			       inner ? "inner " : "", err, tt);
	return err;
}

static void mlx5e_rss_destroy_tir(struct mlx5e_rss *rss, enum mlx5_traffic_types tt,
				  bool inner)
{
	struct mlx5e_tir *tir;

	tir = inner ? &rss->inner_tir[tt] : &rss->tir[tt];
	mlx5e_tir_destroy(tir);
}

static int mlx5e_rss_create_tirs(struct mlx5e_rss *rss,
				 const struct mlx5e_lro_param *init_lro_param,
				 bool inner)
{
	enum mlx5_traffic_types tt, max_tt;
	int err;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		err = mlx5e_rss_create_tir(rss, tt, init_lro_param, inner);
		if (err)
			goto err_destroy_tirs;
	}

	return 0;

err_destroy_tirs:
	max_tt = tt;
	for (tt = 0; tt < max_tt; tt++)
		mlx5e_rss_destroy_tir(rss, tt, inner);
	return err;
}

static void mlx5e_rss_destroy_tirs(struct mlx5e_rss *rss, bool inner)
{
	enum mlx5_traffic_types tt;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		mlx5e_rss_destroy_tir(rss, tt, inner);
}

static int mlx5e_rss_update_tir(struct mlx5e_rss *rss, enum mlx5_traffic_types tt,
				bool inner)
{
	struct mlx5e_rss_params_traffic_type rss_tt;
	struct mlx5e_tir_builder *builder;
	struct mlx5e_tir *tir;
	int err;

	tir = inner ? &rss->inner_tir[tt] : &rss->tir[tt];

	builder = mlx5e_tir_builder_alloc(true);
	if (!builder)
		return -ENOMEM;

	rss_tt = mlx5e_rss_get_tt_config(rss, tt);

	mlx5e_tir_builder_build_rss(builder, &rss->hash, &rss_tt, inner);
	err = mlx5e_tir_modify(tir, builder);

	mlx5e_tir_builder_free(builder);
	return err;
}

static int mlx5e_rss_update_tirs(struct mlx5e_rss *rss)
{
	enum mlx5_traffic_types tt;
	int err, retval;

	retval = 0;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		err = mlx5e_rss_update_tir(rss, tt, false);
		if (err) {
			retval = retval ? : err;
			mlx5e_rss_warn(rss->mdev,
				       "Failed to update RSS hash of indirect TIR for traffic type %d: err = %d\n",
				       tt, err);
		}

		if (!rss->inner_ft_support)
			continue;

		err = mlx5e_rss_update_tir(rss, tt, true);
		if (err) {
			retval = retval ? : err;
			mlx5e_rss_warn(rss->mdev,
				       "Failed to update RSS hash of inner indirect TIR for traffic type %d: err = %d\n",
				       tt, err);
		}
	}
	return retval;
}

int mlx5e_rss_init(struct mlx5e_rss *rss, struct mlx5_core_dev *mdev,
		   bool inner_ft_support, u32 drop_rqn,
		   const struct mlx5e_lro_param *init_lro_param)
{
	int err;

	rss->mdev = mdev;
	rss->inner_ft_support = inner_ft_support;
	rss->drop_rqn = drop_rqn;

	mlx5e_rss_params_init(rss);

	err = mlx5e_rqt_init_direct(&rss->rqt, mdev, true, drop_rqn);
	if (err)
		goto err_out;

	err = mlx5e_rss_create_tirs(rss, init_lro_param, false);
	if (err)
		goto err_destroy_rqt;

	if (inner_ft_support) {
		err = mlx5e_rss_create_tirs(rss, init_lro_param, true);
		if (err)
			goto err_destroy_tirs;
	}

	return 0;

err_destroy_tirs:
	mlx5e_rss_destroy_tirs(rss, false);
err_destroy_rqt:
	mlx5e_rqt_destroy(&rss->rqt);
err_out:
	return err;
}

void mlx5e_rss_cleanup(struct mlx5e_rss *rss)
{
	mlx5e_rss_destroy_tirs(rss, false);

	if (rss->inner_ft_support)
		mlx5e_rss_destroy_tirs(rss, true);

	mlx5e_rqt_destroy(&rss->rqt);
}

u32 mlx5e_rss_get_tirn(struct mlx5e_rss *rss, enum mlx5_traffic_types tt,
		       bool inner)
{
	struct mlx5e_tir *tir;

	WARN_ON(inner && !rss->inner_ft_support);
	tir = inner ? &rss->inner_tir[tt] : &rss->tir[tt];

	return mlx5e_tir_get_tirn(tir);
}

static void mlx5e_rss_apply(struct mlx5e_rss *rss, u32 *rqns, unsigned int num_rqns)
{
	int err;

	err = mlx5e_rqt_redirect_indir(&rss->rqt, rqns, num_rqns, rss->hash.hfunc, &rss->indir);
	if (err)
		mlx5e_rss_warn(rss->mdev, "Failed to redirect RQT %#x to channels: err = %d\n",
			       mlx5e_rqt_get_rqtn(&rss->rqt), err);
}

void mlx5e_rss_enable(struct mlx5e_rss *rss, u32 *rqns, unsigned int num_rqns)
{
	rss->enabled = true;
	mlx5e_rss_apply(rss, rqns, num_rqns);
}

void mlx5e_rss_disable(struct mlx5e_rss *rss)
{
	int err;

	rss->enabled = false;
	err = mlx5e_rqt_redirect_direct(&rss->rqt, rss->drop_rqn);
	if (err)
		mlx5e_rss_warn(rss->mdev, "Failed to redirect RQT %#x to drop RQ %#x: err = %d\n",
			       mlx5e_rqt_get_rqtn(&rss->rqt), rss->drop_rqn, err);
}

int mlx5e_rss_lro_set_param(struct mlx5e_rss *rss, struct mlx5e_lro_param *lro_param)
{
	struct mlx5e_tir_builder *builder;
	enum mlx5_traffic_types tt;
	int err, final_err;

	builder = mlx5e_tir_builder_alloc(true);
	if (!builder)
		return -ENOMEM;

	mlx5e_tir_builder_build_lro(builder, lro_param);

	final_err = 0;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		err = mlx5e_tir_modify(&rss->tir[tt], builder);
		if (err) {
			mlx5e_rss_warn(rss->mdev, "Failed to update LRO state of indirect TIR %#x for traffic type %d: err = %d\n",
				       mlx5e_tir_get_tirn(&rss->tir[tt]), tt, err);
			if (!final_err)
				final_err = err;
		}

		if (!rss->inner_ft_support)
			continue;

		err = mlx5e_tir_modify(&rss->inner_tir[tt], builder);
		if (err) {
			mlx5e_rss_warn(rss->mdev, "Failed to update LRO state of inner indirect TIR %#x for traffic type %d: err = %d\n",
				       mlx5e_tir_get_tirn(&rss->inner_tir[tt]), tt, err);
			if (!final_err)
				final_err = err;
		}
	}

	mlx5e_tir_builder_free(builder);
	return final_err;
}

int mlx5e_rss_get_rxfh(struct mlx5e_rss *rss, u32 *indir, u8 *key, u8 *hfunc)
{
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

int mlx5e_rss_set_rxfh(struct mlx5e_rss *rss, const u32 *indir,
		       const u8 *key, const u8 *hfunc,
		       u32 *rqns, unsigned int num_rqns)
{
	bool changed_indir = false;
	bool changed_hash = false;

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

	if (changed_indir && rss->enabled)
		mlx5e_rss_apply(rss, rqns, num_rqns);

	if (changed_hash)
		mlx5e_rss_update_tirs(rss);

	return 0;
}

struct mlx5e_rss_params_hash mlx5e_rss_get_hash(struct mlx5e_rss *rss)
{
	return rss->hash;
}

u8 mlx5e_rss_get_hash_fields(struct mlx5e_rss *rss, enum mlx5_traffic_types tt)
{
	return rss->rx_hash_fields[tt];
}

int mlx5e_rss_set_hash_fields(struct mlx5e_rss *rss, enum mlx5_traffic_types tt,
			      u8 rx_hash_fields)
{
	u8 old_rx_hash_fields;
	int err;

	old_rx_hash_fields = rss->rx_hash_fields[tt];

	if (old_rx_hash_fields == rx_hash_fields)
		return 0;

	rss->rx_hash_fields[tt] = rx_hash_fields;

	err = mlx5e_rss_update_tir(rss, tt, false);
	if (err) {
		rss->rx_hash_fields[tt] = old_rx_hash_fields;
		mlx5e_rss_warn(rss->mdev,
			       "Failed to update RSS hash fields of indirect TIR for traffic type %d: err = %d\n",
			       tt, err);
		return err;
	}

	if (!(rss->inner_ft_support))
		return 0;

	err = mlx5e_rss_update_tir(rss, tt, true);
	if (err) {
		/* Partial update happened. Try to revert - it may fail too, but
		 * there is nothing more we can do.
		 */
		rss->rx_hash_fields[tt] = old_rx_hash_fields;
		mlx5e_rss_warn(rss->mdev,
			       "Failed to update RSS hash fields of inner indirect TIR for traffic type %d: err = %d\n",
			       tt, err);
		if (mlx5e_rss_update_tir(rss, tt, false))
			mlx5e_rss_warn(rss->mdev,
				       "Partial update of RSS hash fields happened: failed to revert indirect TIR for traffic type %d to the old values\n",
				       tt);
	}

	return err;
}

void mlx5e_rss_set_indir_uniform(struct mlx5e_rss *rss, unsigned int nch)
{
	mlx5e_rss_params_indir_init_uniform(&rss->indir, nch);
}
