/*
 * Copyright (c) 2017, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __MLX5_MPFS_H__
#define __MLX5_MPFS_H__

#include <linux/if_ether.h>
#include <linux/mlx5/device.h>

/* L2 -mac address based- hash helpers */
#define MLX5_L2_ADDR_HASH_SIZE (BIT(BITS_PER_BYTE))
#define MLX5_L2_ADDR_HASH(addr) (addr[5])

struct l2addr_node {
	struct hlist_node hlist;
	u8                addr[ETH_ALEN];
};

#define for_each_l2hash_node(hn, tmp, hash, i) \
	for (i = 0; i < MLX5_L2_ADDR_HASH_SIZE; i++) \
		hlist_for_each_entry_safe(hn, tmp, &(hash)[i], hlist)

#define l2addr_hash_find(hash, mac, type) ({                \
	int ix = MLX5_L2_ADDR_HASH(mac);                    \
	bool found = false;                                 \
	type *ptr = NULL;                                   \
							    \
	hlist_for_each_entry(ptr, &(hash)[ix], node.hlist)  \
		if (ether_addr_equal(ptr->node.addr, mac)) {\
			found = true;                       \
			break;                              \
		}                                           \
	if (!found)                                         \
		ptr = NULL;                                 \
	ptr;                                                \
})

#define l2addr_hash_add(hash, mac, type, gfp) ({            \
	int ix = MLX5_L2_ADDR_HASH(mac);                    \
	type *ptr = NULL;                                   \
							    \
	ptr = kzalloc(sizeof(type), gfp);                   \
	if (ptr) {                                          \
		ether_addr_copy(ptr->node.addr, mac);       \
		hlist_add_head(&ptr->node.hlist, &(hash)[ix]);\
	}                                                   \
	ptr;                                                \
})

#define l2addr_hash_del(ptr) ({                             \
	hlist_del(&(ptr)->node.hlist);                      \
	kfree(ptr);                                         \
})

#ifdef CONFIG_MLX5_MPFS
int  mlx5_mpfs_init(struct mlx5_core_dev *dev);
void mlx5_mpfs_cleanup(struct mlx5_core_dev *dev);
#else /* #ifndef CONFIG_MLX5_MPFS */
static inline int  mlx5_mpfs_init(struct mlx5_core_dev *dev) { return 0; }
static inline void mlx5_mpfs_cleanup(struct mlx5_core_dev *dev) {}
#endif

#endif
