/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies */

#ifndef __MLX5_MAPPING_H__
#define __MLX5_MAPPING_H__

struct mapping_ctx;

int mapping_add(struct mapping_ctx *ctx, void *data, u32 *id);
int mapping_remove(struct mapping_ctx *ctx, u32 id);
int mapping_find(struct mapping_ctx *ctx, u32 id, void *data);

/* mapping uses an xarray to map data to ids in add(), and for find().
 * For locking, it uses a internal xarray spin lock for add()/remove(),
 * find() uses rcu_read_lock().
 * Choosing delayed_removal postpones the removal of a previously mapped
 * id by MAPPING_GRACE_PERIOD milliseconds.
 * This is to avoid races against hardware, where we mark the packet in
 * hardware with a previous id, and quick remove() and add() reusing the same
 * previous id. Then find() will get the new mapping instead of the old
 * which was used to mark the packet.
 */
struct mapping_ctx *mapping_create(size_t data_size, u32 max_id,
				   bool delayed_removal);
void mapping_destroy(struct mapping_ctx *ctx);

#endif /* __MLX5_MAPPING_H__ */
