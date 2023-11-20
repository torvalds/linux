// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "rqt.h"
#include <linux/mlx5/transobj.h>

void mlx5e_rss_params_indir_init_uniform(struct mlx5e_rss_params_indir *indir,
					 unsigned int num_channels)
{
	unsigned int i;

	for (i = 0; i < indir->actual_table_size; i++)
		indir->table[i] = i % num_channels;
}

static int mlx5e_rqt_init(struct mlx5e_rqt *rqt, struct mlx5_core_dev *mdev,
			  u16 max_size, u32 *init_rqns, u16 init_size)
{
	void *rqtc;
	int inlen;
	int err;
	u32 *in;
	int i;

	rqt->mdev = mdev;
	rqt->size = max_size;

	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + sizeof(u32) * init_size;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, rqt_max_size, rqt->size);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, init_size);
	for (i = 0; i < init_size; i++)
		MLX5_SET(rqtc, rqtc, rq_num[i], init_rqns[i]);

	err = mlx5_core_create_rqt(rqt->mdev, in, inlen, &rqt->rqtn);

	kvfree(in);
	return err;
}

int mlx5e_rqt_init_direct(struct mlx5e_rqt *rqt, struct mlx5_core_dev *mdev,
			  bool indir_enabled, u32 init_rqn, u32 indir_table_size)
{
	u16 max_size = indir_enabled ? indir_table_size : 1;

	return mlx5e_rqt_init(rqt, mdev, max_size, &init_rqn, 1);
}

static int mlx5e_bits_invert(unsigned long a, int size)
{
	int inv = 0;
	int i;

	for (i = 0; i < size; i++)
		inv |= (test_bit(size - i - 1, &a) ? 1 : 0) << i;

	return inv;
}

static int mlx5e_calc_indir_rqns(u32 *rss_rqns, u32 *rqns, unsigned int num_rqns,
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
	}

	return 0;
}

int mlx5e_rqt_init_indir(struct mlx5e_rqt *rqt, struct mlx5_core_dev *mdev,
			 u32 *rqns, unsigned int num_rqns,
			 u8 hfunc, struct mlx5e_rss_params_indir *indir)
{
	u32 *rss_rqns;
	int err;

	rss_rqns = kvmalloc_array(indir->actual_table_size, sizeof(*rss_rqns), GFP_KERNEL);
	if (!rss_rqns)
		return -ENOMEM;

	err = mlx5e_calc_indir_rqns(rss_rqns, rqns, num_rqns, hfunc, indir);
	if (err)
		goto out;

	err = mlx5e_rqt_init(rqt, mdev, indir->max_table_size, rss_rqns,
			     indir->actual_table_size);

out:
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

void mlx5e_rqt_destroy(struct mlx5e_rqt *rqt)
{
	mlx5_core_destroy_rqt(rqt->mdev, rqt->rqtn);
}

static int mlx5e_rqt_redirect(struct mlx5e_rqt *rqt, u32 *rqns, unsigned int size)
{
	unsigned int i;
	void *rqtc;
	int inlen;
	u32 *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rqt_in) + sizeof(u32) * size;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(modify_rqt_in, in, ctx);

	MLX5_SET(modify_rqt_in, in, bitmask.rqn_list, 1);
	MLX5_SET(rqtc, rqtc, rqt_actual_size, size);
	for (i = 0; i < size; i++)
		MLX5_SET(rqtc, rqtc, rq_num[i], rqns[i]);

	err = mlx5_core_modify_rqt(rqt->mdev, rqt->rqtn, in, inlen);

	kvfree(in);
	return err;
}

int mlx5e_rqt_redirect_direct(struct mlx5e_rqt *rqt, u32 rqn)
{
	return mlx5e_rqt_redirect(rqt, &rqn, 1);
}

int mlx5e_rqt_redirect_indir(struct mlx5e_rqt *rqt, u32 *rqns, unsigned int num_rqns,
			     u8 hfunc, struct mlx5e_rss_params_indir *indir)
{
	u32 *rss_rqns;
	int err;

	if (WARN_ON(rqt->size != indir->max_table_size))
		return -EINVAL;

	rss_rqns = kvmalloc_array(indir->actual_table_size, sizeof(*rss_rqns), GFP_KERNEL);
	if (!rss_rqns)
		return -ENOMEM;

	err = mlx5e_calc_indir_rqns(rss_rqns, rqns, num_rqns, hfunc, indir);
	if (err)
		goto out;

	err = mlx5e_rqt_redirect(rqt, rss_rqns, indir->actual_table_size);

out:
	kvfree(rss_rqns);
	return err;
}
