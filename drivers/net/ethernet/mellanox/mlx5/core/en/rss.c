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
	struct mlx5e_tir *tir[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_tir *inner_tir[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_rqt rqt;
	struct mlx5_core_dev *mdev;
	u32 drop_rqn;
	bool inner_ft_support;
	bool enabled;
	refcount_t refcnt;
};

void mlx5e_rss_params_indir_modify_actual_size(struct mlx5e_rss *rss, u32 num_channels)
{
	rss->indir.actual_table_size = mlx5e_rqt_size(rss->mdev, num_channels);
}

int mlx5e_rss_params_indir_init(struct mlx5e_rss_params_indir *indir, struct mlx5_core_dev *mdev,
				u32 actual_table_size, u32 max_table_size)
{
	indir->table = kvmalloc_array(max_table_size, sizeof(*indir->table), GFP_KERNEL);
	if (!indir->table)
		return -ENOMEM;

	indir->max_table_size = max_table_size;
	indir->actual_table_size = actual_table_size;

	return 0;
}

void mlx5e_rss_params_indir_cleanup(struct mlx5e_rss_params_indir *indir)
{
	kvfree(indir->table);
}

static int mlx5e_rss_copy(struct mlx5e_rss *to, const struct mlx5e_rss *from)
{
	u32 *dst_indir_table;

	if (to->indir.actual_table_size != from->indir.actual_table_size ||
	    to->indir.max_table_size != from->indir.max_table_size) {
		mlx5e_rss_warn(to->mdev,
			       "Failed to copy RSS due to size mismatch, src (actual %u, max %u) != dst (actual %u, max %u)\n",
			       from->indir.actual_table_size, from->indir.max_table_size,
			       to->indir.actual_table_size, to->indir.max_table_size);
		return -EINVAL;
	}

	dst_indir_table = to->indir.table;
	*to = *from;
	to->indir.table = dst_indir_table;
	memcpy(to->indir.table, from->indir.table,
	       from->indir.actual_table_size * sizeof(*from->indir.table));
	return 0;
}

static struct mlx5e_rss *mlx5e_rss_init_copy(const struct mlx5e_rss *from)
{
	struct mlx5e_rss *rss;
	int err;

	rss = kvzalloc(sizeof(*rss), GFP_KERNEL);
	if (!rss)
		return ERR_PTR(-ENOMEM);

	err = mlx5e_rss_params_indir_init(&rss->indir, from->mdev, from->indir.actual_table_size,
					  from->indir.max_table_size);
	if (err)
		goto err_free_rss;

	err = mlx5e_rss_copy(rss, from);
	if (err)
		goto err_free_indir;

	return rss;

err_free_indir:
	mlx5e_rss_params_indir_cleanup(&rss->indir);
err_free_rss:
	kvfree(rss);
	return ERR_PTR(err);
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

static struct mlx5e_tir **rss_get_tirp(struct mlx5e_rss *rss, enum mlx5_traffic_types tt,
				       bool inner)
{
	return inner ? &rss->inner_tir[tt] : &rss->tir[tt];
}

static struct mlx5e_tir *rss_get_tir(struct mlx5e_rss *rss, enum mlx5_traffic_types tt,
				     bool inner)
{
	return *rss_get_tirp(rss, tt, inner);
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
				const struct mlx5e_packet_merge_param *init_pkt_merge_param,
				bool inner)
{
	struct mlx5e_rss_params_traffic_type rss_tt;
	struct mlx5e_tir_builder *builder;
	struct mlx5e_tir **tir_p;
	struct mlx5e_tir *tir;
	u32 rqtn;
	int err;

	if (inner && !rss->inner_ft_support) {
		mlx5e_rss_warn(rss->mdev,
			       "Cannot create inner indirect TIR[%d], RSS inner FT is not supported.\n",
			       tt);
		return -EINVAL;
	}

	tir_p = rss_get_tirp(rss, tt, inner);
	if (*tir_p)
		return -EINVAL;

	tir = kvzalloc(sizeof(*tir), GFP_KERNEL);
	if (!tir)
		return -ENOMEM;

	builder = mlx5e_tir_builder_alloc(false);
	if (!builder) {
		err = -ENOMEM;
		goto free_tir;
	}

	rqtn = mlx5e_rqt_get_rqtn(&rss->rqt);
	mlx5e_tir_builder_build_rqt(builder, rss->mdev->mlx5e_res.hw_objs.td.tdn,
				    rqtn, rss->inner_ft_support);
	mlx5e_tir_builder_build_packet_merge(builder, init_pkt_merge_param);
	rss_tt = mlx5e_rss_get_tt_config(rss, tt);
	mlx5e_tir_builder_build_rss(builder, &rss->hash, &rss_tt, inner);

	err = mlx5e_tir_init(tir, builder, rss->mdev, true);
	mlx5e_tir_builder_free(builder);
	if (err) {
		mlx5e_rss_warn(rss->mdev, "Failed to create %sindirect TIR: err = %d, tt = %d\n",
			       inner ? "inner " : "", err, tt);
		goto free_tir;
	}

	*tir_p = tir;
	return 0;

free_tir:
	kvfree(tir);
	return err;
}

static void mlx5e_rss_destroy_tir(struct mlx5e_rss *rss, enum mlx5_traffic_types tt,
				  bool inner)
{
	struct mlx5e_tir **tir_p;
	struct mlx5e_tir *tir;

	tir_p = rss_get_tirp(rss, tt, inner);
	if (!*tir_p)
		return;

	tir = *tir_p;
	mlx5e_tir_destroy(tir);
	kvfree(tir);
	*tir_p = NULL;
}

static int mlx5e_rss_create_tirs(struct mlx5e_rss *rss,
				 const struct mlx5e_packet_merge_param *init_pkt_merge_param,
				 bool inner)
{
	enum mlx5_traffic_types tt, max_tt;
	int err;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		err = mlx5e_rss_create_tir(rss, tt, init_pkt_merge_param, inner);
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

	tir = rss_get_tir(rss, tt, inner);
	if (!tir)
		return 0;

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

static int mlx5e_rss_init_no_tirs(struct mlx5e_rss *rss)
{
	mlx5e_rss_params_init(rss);
	refcount_set(&rss->refcnt, 1);

	return mlx5e_rqt_init_direct(&rss->rqt, rss->mdev, true,
				     rss->drop_rqn, rss->indir.max_table_size);
}

struct mlx5e_rss *mlx5e_rss_init(struct mlx5_core_dev *mdev, bool inner_ft_support, u32 drop_rqn,
				 const struct mlx5e_packet_merge_param *init_pkt_merge_param,
				 enum mlx5e_rss_init_type type, unsigned int nch,
				 unsigned int max_nch)
{
	struct mlx5e_rss *rss;
	int err;

	rss = kvzalloc(sizeof(*rss), GFP_KERNEL);
	if (!rss)
		return ERR_PTR(-ENOMEM);

	err = mlx5e_rss_params_indir_init(&rss->indir, mdev,
					  mlx5e_rqt_size(mdev, nch),
					  mlx5e_rqt_size(mdev, max_nch));
	if (err)
		goto err_free_rss;

	rss->mdev = mdev;
	rss->inner_ft_support = inner_ft_support;
	rss->drop_rqn = drop_rqn;

	err = mlx5e_rss_init_no_tirs(rss);
	if (err)
		goto err_free_indir;

	if (type == MLX5E_RSS_INIT_NO_TIRS)
		goto out;

	err = mlx5e_rss_create_tirs(rss, init_pkt_merge_param, false);
	if (err)
		goto err_destroy_rqt;

	if (inner_ft_support) {
		err = mlx5e_rss_create_tirs(rss, init_pkt_merge_param, true);
		if (err)
			goto err_destroy_tirs;
	}

out:
	return rss;

err_destroy_tirs:
	mlx5e_rss_destroy_tirs(rss, false);
err_destroy_rqt:
	mlx5e_rqt_destroy(&rss->rqt);
err_free_indir:
	mlx5e_rss_params_indir_cleanup(&rss->indir);
err_free_rss:
	kvfree(rss);
	return ERR_PTR(err);
}

int mlx5e_rss_cleanup(struct mlx5e_rss *rss)
{
	if (!refcount_dec_if_one(&rss->refcnt))
		return -EBUSY;

	mlx5e_rss_destroy_tirs(rss, false);

	if (rss->inner_ft_support)
		mlx5e_rss_destroy_tirs(rss, true);

	mlx5e_rqt_destroy(&rss->rqt);
	mlx5e_rss_params_indir_cleanup(&rss->indir);
	kvfree(rss);

	return 0;
}

void mlx5e_rss_refcnt_inc(struct mlx5e_rss *rss)
{
	refcount_inc(&rss->refcnt);
}

void mlx5e_rss_refcnt_dec(struct mlx5e_rss *rss)
{
	refcount_dec(&rss->refcnt);
}

unsigned int mlx5e_rss_refcnt_read(struct mlx5e_rss *rss)
{
	return refcount_read(&rss->refcnt);
}

u32 mlx5e_rss_get_tirn(struct mlx5e_rss *rss, enum mlx5_traffic_types tt,
		       bool inner)
{
	struct mlx5e_tir *tir;

	WARN_ON(inner && !rss->inner_ft_support);
	tir = rss_get_tir(rss, tt, inner);
	WARN_ON(!tir);

	return mlx5e_tir_get_tirn(tir);
}

/* Fill the "tirn" output parameter.
 * Create the requested TIR if it's its first usage.
 */
int mlx5e_rss_obtain_tirn(struct mlx5e_rss *rss,
			  enum mlx5_traffic_types tt,
			  const struct mlx5e_packet_merge_param *init_pkt_merge_param,
			  bool inner, u32 *tirn)
{
	struct mlx5e_tir *tir;

	tir = rss_get_tir(rss, tt, inner);
	if (!tir) { /* TIR doesn't exist, create one */
		int err;

		err = mlx5e_rss_create_tir(rss, tt, init_pkt_merge_param, inner);
		if (err)
			return err;
		tir = rss_get_tir(rss, tt, inner);
	}

	*tirn = mlx5e_tir_get_tirn(tir);
	return 0;
}

static int mlx5e_rss_apply(struct mlx5e_rss *rss, u32 *rqns, unsigned int num_rqns)
{
	int err;

	err = mlx5e_rqt_redirect_indir(&rss->rqt, rqns, num_rqns, rss->hash.hfunc, &rss->indir);
	if (err)
		mlx5e_rss_warn(rss->mdev, "Failed to redirect RQT %#x to channels: err = %d\n",
			       mlx5e_rqt_get_rqtn(&rss->rqt), err);
	return err;
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

int mlx5e_rss_packet_merge_set_param(struct mlx5e_rss *rss,
				     struct mlx5e_packet_merge_param *pkt_merge_param)
{
	struct mlx5e_tir_builder *builder;
	enum mlx5_traffic_types tt;
	int err, final_err;

	builder = mlx5e_tir_builder_alloc(true);
	if (!builder)
		return -ENOMEM;

	mlx5e_tir_builder_build_packet_merge(builder, pkt_merge_param);

	final_err = 0;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		struct mlx5e_tir *tir;

		tir = rss_get_tir(rss, tt, false);
		if (!tir)
			goto inner_tir;
		err = mlx5e_tir_modify(tir, builder);
		if (err) {
			mlx5e_rss_warn(rss->mdev, "Failed to update packet merge state of indirect TIR %#x for traffic type %d: err = %d\n",
				       mlx5e_tir_get_tirn(tir), tt, err);
			if (!final_err)
				final_err = err;
		}

inner_tir:
		if (!rss->inner_ft_support)
			continue;

		tir = rss_get_tir(rss, tt, true);
		if (!tir)
			continue;
		err = mlx5e_tir_modify(tir, builder);
		if (err) {
			mlx5e_rss_warn(rss->mdev, "Failed to update packet merge state of inner indirect TIR %#x for traffic type %d: err = %d\n",
				       mlx5e_tir_get_tirn(tir), tt, err);
			if (!final_err)
				final_err = err;
		}
	}

	mlx5e_tir_builder_free(builder);
	return final_err;
}

int mlx5e_rss_get_rxfh(struct mlx5e_rss *rss, u32 *indir, u8 *key, u8 *hfunc)
{
	if (indir)
		memcpy(indir, rss->indir.table,
		       rss->indir.actual_table_size * sizeof(*rss->indir.table));

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
	struct mlx5e_rss *old_rss;
	int err = 0;

	old_rss = mlx5e_rss_init_copy(rss);
	if (IS_ERR(old_rss))
		return PTR_ERR(old_rss);

	if (hfunc && *hfunc != rss->hash.hfunc) {
		switch (*hfunc) {
		case ETH_RSS_HASH_XOR:
		case ETH_RSS_HASH_TOP:
			break;
		default:
			err = -EINVAL;
			goto out;
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
		changed_indir = true;

		memcpy(rss->indir.table, indir,
		       rss->indir.actual_table_size * sizeof(*rss->indir.table));
	}

	if (changed_indir && rss->enabled) {
		err = mlx5e_rss_apply(rss, rqns, num_rqns);
		if (err) {
			mlx5e_rss_copy(rss, old_rss);
			goto out;
		}
	}

	if (changed_hash)
		mlx5e_rss_update_tirs(rss);

out:
	mlx5e_rss_params_indir_cleanup(&old_rss->indir);
	kvfree(old_rss);

	return err;
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
