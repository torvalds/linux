// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "rqt.h"
#include <linux/mlx5/transobj.h>

static bool verify_num_vhca_ids(struct mlx5_core_dev *mdev, u32 *vhca_ids,
				unsigned int size)
{
	unsigned int max_num_vhca_id = MLX5_CAP_GEN_2(mdev, max_rqt_vhca_id);
	int i;

	/* Verify that all vhca_ids are in range [0, max_num_vhca_ids - 1] */
	for (i = 0; i < size; i++)
		if (vhca_ids[i] >= max_num_vhca_id)
			return false;
	return true;
}

static bool rqt_verify_vhca_ids(struct mlx5_core_dev *mdev, u32 *vhca_ids,
				unsigned int size)
{
	if (!vhca_ids)
		return true;

	if (!MLX5_CAP_GEN(mdev, cross_vhca_rqt))
		return false;
	if (!verify_num_vhca_ids(mdev, vhca_ids, size))
		return false;

	return true;
}

void mlx5e_rss_params_indir_init_uniform(struct mlx5e_rss_params_indir *indir,
					 unsigned int num_channels)
{
	unsigned int i;

	for (i = 0; i < indir->actual_table_size; i++)
		indir->table[i] = i % num_channels;
}

static void fill_rqn_list(void *rqtc, u32 *rqns, u32 *vhca_ids, unsigned int size)
{
	unsigned int i;

	if (vhca_ids) {
		MLX5_SET(rqtc, rqtc, rq_vhca_id_format, 1);
		for (i = 0; i < size; i++) {
			MLX5_SET(rqtc, rqtc, rq_vhca[i].rq_num, rqns[i]);
			MLX5_SET(rqtc, rqtc, rq_vhca[i].rq_vhca_id, vhca_ids[i]);
		}
	} else {
		for (i = 0; i < size; i++)
			MLX5_SET(rqtc, rqtc, rq_num[i], rqns[i]);
	}
}
static int mlx5e_rqt_init(struct mlx5e_rqt *rqt, struct mlx5_core_dev *mdev,
			  u16 max_size, u32 *init_rqns, u32 *init_vhca_ids, u16 init_size)
{
	int entry_sz;
	void *rqtc;
	int inlen;
	int err;
	u32 *in;

	if (!rqt_verify_vhca_ids(mdev, init_vhca_ids, init_size))
		return -EOPNOTSUPP;

	rqt->mdev = mdev;
	rqt->size = max_size;

	entry_sz = init_vhca_ids ? MLX5_ST_SZ_BYTES(rq_vhca) : MLX5_ST_SZ_BYTES(rq_num);
	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + entry_sz * init_size;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, rqt_max_size, rqt->size);
	MLX5_SET(rqtc, rqtc, rqt_actual_size, init_size);

	fill_rqn_list(rqtc, init_rqns, init_vhca_ids, init_size);

	err = mlx5_core_create_rqt(rqt->mdev, in, inlen, &rqt->rqtn);

	kvfree(in);
	return err;
}

int mlx5e_rqt_init_direct(struct mlx5e_rqt *rqt, struct mlx5_core_dev *mdev,
			  bool indir_enabled, u32 init_rqn, u32 indir_table_size)
{
	u16 max_size = indir_enabled ? indir_table_size : 1;

	return mlx5e_rqt_init(rqt, mdev, max_size, &init_rqn, NULL, 1);
}

static int mlx5e_bits_invert(unsigned long a, int size)
{
	int inv = 0;
	int i;

	for (i = 0; i < size; i++)
		inv |= (test_bit(size - i - 1, &a) ? 1 : 0) << i;

	return inv;
}

static int mlx5e_calc_indir_rqns(u32 *rss_rqns, u32 *rqns, u32 *rss_vhca_ids, u32 *vhca_ids,
				 unsigned int num_rqns,
				 u8 hfunc, struct mlx5e_rss_params_indir *indir)
{
	unsigned int i;

	for (i = 0; i < indir->actual_table_size; i++) {
		unsigned int ix = i;

		if (hfunc == ETH_RSS_HASH_XOR)
			ix = mlx5e_bits_invert(ix, ilog2(indir->actual_table_size));

		ix = indir->table[ix];

		if (WARN_ON(ix >= num_rqns))
			/* Could be a bug in the driver or in the kernel part of
			 * ethtool: indir table refers to non-existent RQs.
			 */
			return -EINVAL;
		rss_rqns[i] = rqns[ix];
		if (vhca_ids)
			rss_vhca_ids[i] = vhca_ids[ix];
	}

	return 0;
}

int mlx5e_rqt_init_indir(struct mlx5e_rqt *rqt, struct mlx5_core_dev *mdev,
			 u32 *rqns, u32 *vhca_ids, unsigned int num_rqns,
			 u8 hfunc, struct mlx5e_rss_params_indir *indir)
{
	u32 *rss_rqns, *rss_vhca_ids = NULL;
	int err;

	rss_rqns = kvmalloc_array(indir->actual_table_size, sizeof(*rss_rqns), GFP_KERNEL);
	if (!rss_rqns)
		return -ENOMEM;

	if (vhca_ids) {
		rss_vhca_ids = kvmalloc_array(indir->actual_table_size, sizeof(*rss_vhca_ids),
					      GFP_KERNEL);
		if (!rss_vhca_ids) {
			kvfree(rss_rqns);
			return -ENOMEM;
		}
	}

	err = mlx5e_calc_indir_rqns(rss_rqns, rqns, rss_vhca_ids, vhca_ids, num_rqns, hfunc, indir);
	if (err)
		goto out;

	err = mlx5e_rqt_init(rqt, mdev, indir->max_table_size, rss_rqns, rss_vhca_ids,
			     indir->actual_table_size);

out:
	kvfree(rss_vhca_ids);
	kvfree(rss_rqns);
	return err;
}

#define MLX5E_UNIFORM_SPREAD_RQT_FACTOR 2

u32 mlx5e_rqt_size(struct mlx5_core_dev *mdev, unsigned int num_channels)
{
	u32 rqt_size = max_t(u32, MLX5E_INDIR_MIN_RQT_SIZE,
			     roundup_pow_of_two(num_channels * MLX5E_UNIFORM_SPREAD_RQT_FACTOR));
	u32 max_cap_rqt_size = 1 << MLX5_CAP_GEN(mdev, log_max_rqt_size);

	return min_t(u32, rqt_size, max_cap_rqt_size);
}

#define MLX5E_MAX_RQT_SIZE_ALLOWED_WITH_XOR8_HASH 256

unsigned int mlx5e_rqt_max_num_channels_allowed_for_xor8(void)
{
	return MLX5E_MAX_RQT_SIZE_ALLOWED_WITH_XOR8_HASH / MLX5E_UNIFORM_SPREAD_RQT_FACTOR;
}

void mlx5e_rqt_destroy(struct mlx5e_rqt *rqt)
{
	mlx5_core_destroy_rqt(rqt->mdev, rqt->rqtn);
}

static int mlx5e_rqt_redirect(struct mlx5e_rqt *rqt, u32 *rqns, u32 *vhca_ids,
			      unsigned int size)
{
	int entry_sz;
	void *rqtc;
	int inlen;
	u32 *in;
	int err;

	if (!rqt_verify_vhca_ids(rqt->mdev, vhca_ids, size))
		return -EINVAL;

	entry_sz = vhca_ids ? MLX5_ST_SZ_BYTES(rq_vhca) : MLX5_ST_SZ_BYTES(rq_num);
	inlen = MLX5_ST_SZ_BYTES(modify_rqt_in) + entry_sz * size;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(modify_rqt_in, in, ctx);

	MLX5_SET(modify_rqt_in, in, bitmask.rqn_list, 1);
	MLX5_SET(rqtc, rqtc, rqt_actual_size, size);

	fill_rqn_list(rqtc, rqns, vhca_ids, size);

	err = mlx5_core_modify_rqt(rqt->mdev, rqt->rqtn, in, inlen);

	kvfree(in);
	return err;
}

int mlx5e_rqt_redirect_direct(struct mlx5e_rqt *rqt, u32 rqn, u32 *vhca_id)
{
	return mlx5e_rqt_redirect(rqt, &rqn, vhca_id, 1);
}

int mlx5e_rqt_redirect_indir(struct mlx5e_rqt *rqt, u32 *rqns, u32 *vhca_ids,
			     unsigned int num_rqns,
			     u8 hfunc, struct mlx5e_rss_params_indir *indir)
{
	u32 *rss_rqns, *rss_vhca_ids = NULL;
	int err;

	if (!rqt_verify_vhca_ids(rqt->mdev, vhca_ids, num_rqns))
		return -EINVAL;

	if (WARN_ON(rqt->size != indir->max_table_size))
		return -EINVAL;

	rss_rqns = kvmalloc_array(indir->actual_table_size, sizeof(*rss_rqns), GFP_KERNEL);
	if (!rss_rqns)
		return -ENOMEM;

	if (vhca_ids) {
		rss_vhca_ids = kvmalloc_array(indir->actual_table_size, sizeof(*rss_vhca_ids),
					      GFP_KERNEL);
		if (!rss_vhca_ids) {
			kvfree(rss_rqns);
			return -ENOMEM;
		}
	}

	err = mlx5e_calc_indir_rqns(rss_rqns, rqns, rss_vhca_ids, vhca_ids, num_rqns, hfunc, indir);
	if (err)
		goto out;

	err = mlx5e_rqt_redirect(rqt, rss_rqns, rss_vhca_ids, indir->actual_table_size);

out:
	kvfree(rss_vhca_ids);
	kvfree(rss_rqns);
	return err;
}
