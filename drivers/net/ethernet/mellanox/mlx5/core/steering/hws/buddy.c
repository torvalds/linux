// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"
#include "buddy.h"

static int hws_buddy_init(struct mlx5hws_buddy_mem *buddy, u32 max_order)
{
	int i, s, ret = 0;

	buddy->max_order = max_order;

	buddy->bitmap = kcalloc(buddy->max_order + 1,
				sizeof(*buddy->bitmap),
				GFP_KERNEL);
	if (!buddy->bitmap)
		return -ENOMEM;

	buddy->num_free = kcalloc(buddy->max_order + 1,
				  sizeof(*buddy->num_free),
				  GFP_KERNEL);
	if (!buddy->num_free) {
		ret = -ENOMEM;
		goto err_out_free_bits;
	}

	for (i = 0; i <= (int)buddy->max_order; ++i) {
		s = 1 << (buddy->max_order - i);

		buddy->bitmap[i] = bitmap_zalloc(s, GFP_KERNEL);
		if (!buddy->bitmap[i]) {
			ret = -ENOMEM;
			goto err_out_free_num_free;
		}
	}

	bitmap_set(buddy->bitmap[buddy->max_order], 0, 1);
	buddy->num_free[buddy->max_order] = 1;

	return 0;

err_out_free_num_free:
	for (i = 0; i <= (int)buddy->max_order; ++i)
		bitmap_free(buddy->bitmap[i]);

	kfree(buddy->num_free);

err_out_free_bits:
	kfree(buddy->bitmap);
	return ret;
}

struct mlx5hws_buddy_mem *mlx5hws_buddy_create(u32 max_order)
{
	struct mlx5hws_buddy_mem *buddy;

	buddy = kzalloc(sizeof(*buddy), GFP_KERNEL);
	if (!buddy)
		return NULL;

	if (hws_buddy_init(buddy, max_order))
		goto free_buddy;

	return buddy;

free_buddy:
	kfree(buddy);
	return NULL;
}

void mlx5hws_buddy_cleanup(struct mlx5hws_buddy_mem *buddy)
{
	int i;

	for (i = 0; i <= (int)buddy->max_order; ++i)
		bitmap_free(buddy->bitmap[i]);

	kfree(buddy->num_free);
	kfree(buddy->bitmap);
}

static int hws_buddy_find_free_seg(struct mlx5hws_buddy_mem *buddy,
				   u32 start_order,
				   u32 *segment,
				   u32 *order)
{
	unsigned int seg, order_iter, m;

	for (order_iter = start_order;
	     order_iter <= buddy->max_order; ++order_iter) {
		if (!buddy->num_free[order_iter])
			continue;

		m = 1 << (buddy->max_order - order_iter);
		seg = find_first_bit(buddy->bitmap[order_iter], m);

		if (WARN(seg >= m,
			 "ICM Buddy: failed finding free mem for order %d\n",
			 order_iter))
			return -ENOMEM;

		break;
	}

	if (order_iter > buddy->max_order)
		return -ENOMEM;

	*segment = seg;
	*order = order_iter;
	return 0;
}

int mlx5hws_buddy_alloc_mem(struct mlx5hws_buddy_mem *buddy, u32 order)
{
	u32 seg, order_iter, err;

	err = hws_buddy_find_free_seg(buddy, order, &seg, &order_iter);
	if (err)
		return err;

	bitmap_clear(buddy->bitmap[order_iter], seg, 1);
	--buddy->num_free[order_iter];

	while (order_iter > order) {
		--order_iter;
		seg <<= 1;
		bitmap_set(buddy->bitmap[order_iter], seg ^ 1, 1);
		++buddy->num_free[order_iter];
	}

	seg <<= order;

	return seg;
}

void mlx5hws_buddy_free_mem(struct mlx5hws_buddy_mem *buddy, u32 seg, u32 order)
{
	seg >>= order;

	while (test_bit(seg ^ 1, buddy->bitmap[order])) {
		bitmap_clear(buddy->bitmap[order], seg ^ 1, 1);
		--buddy->num_free[order];
		seg >>= 1;
		++order;
	}

	bitmap_set(buddy->bitmap[order], seg, 1);
	++buddy->num_free[order];
}
