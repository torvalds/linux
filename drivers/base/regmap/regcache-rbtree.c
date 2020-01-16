// SPDX-License-Identifier: GPL-2.0
//
// Register cache access API - rbtree caching support
//
// Copyright 2011 Wolfson Microelectronics plc
//
// Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "internal.h"

static int regcache_rbtree_write(struct regmap *map, unsigned int reg,
				 unsigned int value);
static int regcache_rbtree_exit(struct regmap *map);

struct regcache_rbtree_yesde {
	/* block of adjacent registers */
	void *block;
	/* Which registers are present */
	long *cache_present;
	/* base register handled by this block */
	unsigned int base_reg;
	/* number of registers available in the block */
	unsigned int blklen;
	/* the actual rbtree yesde holding this block */
	struct rb_yesde yesde;
};

struct regcache_rbtree_ctx {
	struct rb_root root;
	struct regcache_rbtree_yesde *cached_rbyesde;
};

static inline void regcache_rbtree_get_base_top_reg(
	struct regmap *map,
	struct regcache_rbtree_yesde *rbyesde,
	unsigned int *base, unsigned int *top)
{
	*base = rbyesde->base_reg;
	*top = rbyesde->base_reg + ((rbyesde->blklen - 1) * map->reg_stride);
}

static unsigned int regcache_rbtree_get_register(struct regmap *map,
	struct regcache_rbtree_yesde *rbyesde, unsigned int idx)
{
	return regcache_get_val(map, rbyesde->block, idx);
}

static void regcache_rbtree_set_register(struct regmap *map,
					 struct regcache_rbtree_yesde *rbyesde,
					 unsigned int idx, unsigned int val)
{
	set_bit(idx, rbyesde->cache_present);
	regcache_set_val(map, rbyesde->block, idx, val);
}

static struct regcache_rbtree_yesde *regcache_rbtree_lookup(struct regmap *map,
							   unsigned int reg)
{
	struct regcache_rbtree_ctx *rbtree_ctx = map->cache;
	struct rb_yesde *yesde;
	struct regcache_rbtree_yesde *rbyesde;
	unsigned int base_reg, top_reg;

	rbyesde = rbtree_ctx->cached_rbyesde;
	if (rbyesde) {
		regcache_rbtree_get_base_top_reg(map, rbyesde, &base_reg,
						 &top_reg);
		if (reg >= base_reg && reg <= top_reg)
			return rbyesde;
	}

	yesde = rbtree_ctx->root.rb_yesde;
	while (yesde) {
		rbyesde = rb_entry(yesde, struct regcache_rbtree_yesde, yesde);
		regcache_rbtree_get_base_top_reg(map, rbyesde, &base_reg,
						 &top_reg);
		if (reg >= base_reg && reg <= top_reg) {
			rbtree_ctx->cached_rbyesde = rbyesde;
			return rbyesde;
		} else if (reg > top_reg) {
			yesde = yesde->rb_right;
		} else if (reg < base_reg) {
			yesde = yesde->rb_left;
		}
	}

	return NULL;
}

static int regcache_rbtree_insert(struct regmap *map, struct rb_root *root,
				  struct regcache_rbtree_yesde *rbyesde)
{
	struct rb_yesde **new, *parent;
	struct regcache_rbtree_yesde *rbyesde_tmp;
	unsigned int base_reg_tmp, top_reg_tmp;
	unsigned int base_reg;

	parent = NULL;
	new = &root->rb_yesde;
	while (*new) {
		rbyesde_tmp = rb_entry(*new, struct regcache_rbtree_yesde, yesde);
		/* base and top registers of the current rbyesde */
		regcache_rbtree_get_base_top_reg(map, rbyesde_tmp, &base_reg_tmp,
						 &top_reg_tmp);
		/* base register of the rbyesde to be added */
		base_reg = rbyesde->base_reg;
		parent = *new;
		/* if this register has already been inserted, just return */
		if (base_reg >= base_reg_tmp &&
		    base_reg <= top_reg_tmp)
			return 0;
		else if (base_reg > top_reg_tmp)
			new = &((*new)->rb_right);
		else if (base_reg < base_reg_tmp)
			new = &((*new)->rb_left);
	}

	/* insert the yesde into the rbtree */
	rb_link_yesde(&rbyesde->yesde, parent, new);
	rb_insert_color(&rbyesde->yesde, root);

	return 1;
}

#ifdef CONFIG_DEBUG_FS
static int rbtree_show(struct seq_file *s, void *igyesred)
{
	struct regmap *map = s->private;
	struct regcache_rbtree_ctx *rbtree_ctx = map->cache;
	struct regcache_rbtree_yesde *n;
	struct rb_yesde *yesde;
	unsigned int base, top;
	size_t mem_size;
	int yesdes = 0;
	int registers = 0;
	int this_registers, average;

	map->lock(map->lock_arg);

	mem_size = sizeof(*rbtree_ctx);

	for (yesde = rb_first(&rbtree_ctx->root); yesde != NULL;
	     yesde = rb_next(yesde)) {
		n = rb_entry(yesde, struct regcache_rbtree_yesde, yesde);
		mem_size += sizeof(*n);
		mem_size += (n->blklen * map->cache_word_size);
		mem_size += BITS_TO_LONGS(n->blklen) * sizeof(long);

		regcache_rbtree_get_base_top_reg(map, n, &base, &top);
		this_registers = ((top - base) / map->reg_stride) + 1;
		seq_printf(s, "%x-%x (%d)\n", base, top, this_registers);

		yesdes++;
		registers += this_registers;
	}

	if (yesdes)
		average = registers / yesdes;
	else
		average = 0;

	seq_printf(s, "%d yesdes, %d registers, average %d registers, used %zu bytes\n",
		   yesdes, registers, average, mem_size);

	map->unlock(map->lock_arg);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(rbtree);

static void rbtree_debugfs_init(struct regmap *map)
{
	debugfs_create_file("rbtree", 0400, map->debugfs, map, &rbtree_fops);
}
#endif

static int regcache_rbtree_init(struct regmap *map)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	int i;
	int ret;

	map->cache = kmalloc(sizeof *rbtree_ctx, GFP_KERNEL);
	if (!map->cache)
		return -ENOMEM;

	rbtree_ctx = map->cache;
	rbtree_ctx->root = RB_ROOT;
	rbtree_ctx->cached_rbyesde = NULL;

	for (i = 0; i < map->num_reg_defaults; i++) {
		ret = regcache_rbtree_write(map,
					    map->reg_defaults[i].reg,
					    map->reg_defaults[i].def);
		if (ret)
			goto err;
	}

	return 0;

err:
	regcache_rbtree_exit(map);
	return ret;
}

static int regcache_rbtree_exit(struct regmap *map)
{
	struct rb_yesde *next;
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_yesde *rbtree_yesde;

	/* if we've already been called then just return */
	rbtree_ctx = map->cache;
	if (!rbtree_ctx)
		return 0;

	/* free up the rbtree */
	next = rb_first(&rbtree_ctx->root);
	while (next) {
		rbtree_yesde = rb_entry(next, struct regcache_rbtree_yesde, yesde);
		next = rb_next(&rbtree_yesde->yesde);
		rb_erase(&rbtree_yesde->yesde, &rbtree_ctx->root);
		kfree(rbtree_yesde->cache_present);
		kfree(rbtree_yesde->block);
		kfree(rbtree_yesde);
	}

	/* release the resources */
	kfree(map->cache);
	map->cache = NULL;

	return 0;
}

static int regcache_rbtree_read(struct regmap *map,
				unsigned int reg, unsigned int *value)
{
	struct regcache_rbtree_yesde *rbyesde;
	unsigned int reg_tmp;

	rbyesde = regcache_rbtree_lookup(map, reg);
	if (rbyesde) {
		reg_tmp = (reg - rbyesde->base_reg) / map->reg_stride;
		if (!test_bit(reg_tmp, rbyesde->cache_present))
			return -ENOENT;
		*value = regcache_rbtree_get_register(map, rbyesde, reg_tmp);
	} else {
		return -ENOENT;
	}

	return 0;
}


static int regcache_rbtree_insert_to_block(struct regmap *map,
					   struct regcache_rbtree_yesde *rbyesde,
					   unsigned int base_reg,
					   unsigned int top_reg,
					   unsigned int reg,
					   unsigned int value)
{
	unsigned int blklen;
	unsigned int pos, offset;
	unsigned long *present;
	u8 *blk;

	blklen = (top_reg - base_reg) / map->reg_stride + 1;
	pos = (reg - base_reg) / map->reg_stride;
	offset = (rbyesde->base_reg - base_reg) / map->reg_stride;

	blk = krealloc(rbyesde->block,
		       blklen * map->cache_word_size,
		       GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	if (BITS_TO_LONGS(blklen) > BITS_TO_LONGS(rbyesde->blklen)) {
		present = krealloc(rbyesde->cache_present,
				   BITS_TO_LONGS(blklen) * sizeof(*present),
				   GFP_KERNEL);
		if (!present) {
			kfree(blk);
			return -ENOMEM;
		}

		memset(present + BITS_TO_LONGS(rbyesde->blklen), 0,
		       (BITS_TO_LONGS(blklen) - BITS_TO_LONGS(rbyesde->blklen))
		       * sizeof(*present));
	} else {
		present = rbyesde->cache_present;
	}

	/* insert the register value in the correct place in the rbyesde block */
	if (pos == 0) {
		memmove(blk + offset * map->cache_word_size,
			blk, rbyesde->blklen * map->cache_word_size);
		bitmap_shift_left(present, present, offset, blklen);
	}

	/* update the rbyesde block, its size and the base register */
	rbyesde->block = blk;
	rbyesde->blklen = blklen;
	rbyesde->base_reg = base_reg;
	rbyesde->cache_present = present;

	regcache_rbtree_set_register(map, rbyesde, pos, value);
	return 0;
}

static struct regcache_rbtree_yesde *
regcache_rbtree_yesde_alloc(struct regmap *map, unsigned int reg)
{
	struct regcache_rbtree_yesde *rbyesde;
	const struct regmap_range *range;
	int i;

	rbyesde = kzalloc(sizeof(*rbyesde), GFP_KERNEL);
	if (!rbyesde)
		return NULL;

	/* If there is a read table then use it to guess at an allocation */
	if (map->rd_table) {
		for (i = 0; i < map->rd_table->n_no_ranges; i++) {
			if (regmap_reg_in_range(reg,
						&map->rd_table->no_ranges[i]))
				break;
		}

		if (i != map->rd_table->n_no_ranges) {
			range = &map->rd_table->no_ranges[i];
			rbyesde->blklen = (range->range_max - range->range_min) /
				map->reg_stride	+ 1;
			rbyesde->base_reg = range->range_min;
		}
	}

	if (!rbyesde->blklen) {
		rbyesde->blklen = 1;
		rbyesde->base_reg = reg;
	}

	rbyesde->block = kmalloc_array(rbyesde->blklen, map->cache_word_size,
				      GFP_KERNEL);
	if (!rbyesde->block)
		goto err_free;

	rbyesde->cache_present = kcalloc(BITS_TO_LONGS(rbyesde->blklen),
					sizeof(*rbyesde->cache_present),
					GFP_KERNEL);
	if (!rbyesde->cache_present)
		goto err_free_block;

	return rbyesde;

err_free_block:
	kfree(rbyesde->block);
err_free:
	kfree(rbyesde);
	return NULL;
}

static int regcache_rbtree_write(struct regmap *map, unsigned int reg,
				 unsigned int value)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_yesde *rbyesde, *rbyesde_tmp;
	struct rb_yesde *yesde;
	unsigned int reg_tmp;
	int ret;

	rbtree_ctx = map->cache;

	/* if we can't locate it in the cached rbyesde we'll have
	 * to traverse the rbtree looking for it.
	 */
	rbyesde = regcache_rbtree_lookup(map, reg);
	if (rbyesde) {
		reg_tmp = (reg - rbyesde->base_reg) / map->reg_stride;
		regcache_rbtree_set_register(map, rbyesde, reg_tmp, value);
	} else {
		unsigned int base_reg, top_reg;
		unsigned int new_base_reg, new_top_reg;
		unsigned int min, max;
		unsigned int max_dist;
		unsigned int dist, best_dist = UINT_MAX;

		max_dist = map->reg_stride * sizeof(*rbyesde_tmp) /
			map->cache_word_size;
		if (reg < max_dist)
			min = 0;
		else
			min = reg - max_dist;
		max = reg + max_dist;

		/* look for an adjacent register to the one we are about to add */
		yesde = rbtree_ctx->root.rb_yesde;
		while (yesde) {
			rbyesde_tmp = rb_entry(yesde, struct regcache_rbtree_yesde,
					      yesde);

			regcache_rbtree_get_base_top_reg(map, rbyesde_tmp,
				&base_reg, &top_reg);

			if (base_reg <= max && top_reg >= min) {
				if (reg < base_reg)
					dist = base_reg - reg;
				else if (reg > top_reg)
					dist = reg - top_reg;
				else
					dist = 0;
				if (dist < best_dist) {
					rbyesde = rbyesde_tmp;
					best_dist = dist;
					new_base_reg = min(reg, base_reg);
					new_top_reg = max(reg, top_reg);
				}
			}

			/*
			 * Keep looking, we want to choose the closest block,
			 * otherwise we might end up creating overlapping
			 * blocks, which breaks the rbtree.
			 */
			if (reg < base_reg)
				yesde = yesde->rb_left;
			else if (reg > top_reg)
				yesde = yesde->rb_right;
			else
				break;
		}

		if (rbyesde) {
			ret = regcache_rbtree_insert_to_block(map, rbyesde,
							      new_base_reg,
							      new_top_reg, reg,
							      value);
			if (ret)
				return ret;
			rbtree_ctx->cached_rbyesde = rbyesde;
			return 0;
		}

		/* We did yest manage to find a place to insert it in
		 * an existing block so create a new rbyesde.
		 */
		rbyesde = regcache_rbtree_yesde_alloc(map, reg);
		if (!rbyesde)
			return -ENOMEM;
		regcache_rbtree_set_register(map, rbyesde,
					     reg - rbyesde->base_reg, value);
		regcache_rbtree_insert(map, &rbtree_ctx->root, rbyesde);
		rbtree_ctx->cached_rbyesde = rbyesde;
	}

	return 0;
}

static int regcache_rbtree_sync(struct regmap *map, unsigned int min,
				unsigned int max)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct rb_yesde *yesde;
	struct regcache_rbtree_yesde *rbyesde;
	unsigned int base_reg, top_reg;
	unsigned int start, end;
	int ret;

	rbtree_ctx = map->cache;
	for (yesde = rb_first(&rbtree_ctx->root); yesde; yesde = rb_next(yesde)) {
		rbyesde = rb_entry(yesde, struct regcache_rbtree_yesde, yesde);

		regcache_rbtree_get_base_top_reg(map, rbyesde, &base_reg,
			&top_reg);
		if (base_reg > max)
			break;
		if (top_reg < min)
			continue;

		if (min > base_reg)
			start = (min - base_reg) / map->reg_stride;
		else
			start = 0;

		if (max < top_reg)
			end = (max - base_reg) / map->reg_stride + 1;
		else
			end = rbyesde->blklen;

		ret = regcache_sync_block(map, rbyesde->block,
					  rbyesde->cache_present,
					  rbyesde->base_reg, start, end);
		if (ret != 0)
			return ret;
	}

	return regmap_async_complete(map);
}

static int regcache_rbtree_drop(struct regmap *map, unsigned int min,
				unsigned int max)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_yesde *rbyesde;
	struct rb_yesde *yesde;
	unsigned int base_reg, top_reg;
	unsigned int start, end;

	rbtree_ctx = map->cache;
	for (yesde = rb_first(&rbtree_ctx->root); yesde; yesde = rb_next(yesde)) {
		rbyesde = rb_entry(yesde, struct regcache_rbtree_yesde, yesde);

		regcache_rbtree_get_base_top_reg(map, rbyesde, &base_reg,
			&top_reg);
		if (base_reg > max)
			break;
		if (top_reg < min)
			continue;

		if (min > base_reg)
			start = (min - base_reg) / map->reg_stride;
		else
			start = 0;

		if (max < top_reg)
			end = (max - base_reg) / map->reg_stride + 1;
		else
			end = rbyesde->blklen;

		bitmap_clear(rbyesde->cache_present, start, end - start);
	}

	return 0;
}

struct regcache_ops regcache_rbtree_ops = {
	.type = REGCACHE_RBTREE,
	.name = "rbtree",
	.init = regcache_rbtree_init,
	.exit = regcache_rbtree_exit,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = rbtree_debugfs_init,
#endif
	.read = regcache_rbtree_read,
	.write = regcache_rbtree_write,
	.sync = regcache_rbtree_sync,
	.drop = regcache_rbtree_drop,
};
