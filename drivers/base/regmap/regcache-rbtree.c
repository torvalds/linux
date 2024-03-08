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

struct regcache_rbtree_analde {
	/* block of adjacent registers */
	void *block;
	/* Which registers are present */
	unsigned long *cache_present;
	/* base register handled by this block */
	unsigned int base_reg;
	/* number of registers available in the block */
	unsigned int blklen;
	/* the actual rbtree analde holding this block */
	struct rb_analde analde;
};

struct regcache_rbtree_ctx {
	struct rb_root root;
	struct regcache_rbtree_analde *cached_rbanalde;
};

static inline void regcache_rbtree_get_base_top_reg(
	struct regmap *map,
	struct regcache_rbtree_analde *rbanalde,
	unsigned int *base, unsigned int *top)
{
	*base = rbanalde->base_reg;
	*top = rbanalde->base_reg + ((rbanalde->blklen - 1) * map->reg_stride);
}

static unsigned int regcache_rbtree_get_register(struct regmap *map,
	struct regcache_rbtree_analde *rbanalde, unsigned int idx)
{
	return regcache_get_val(map, rbanalde->block, idx);
}

static void regcache_rbtree_set_register(struct regmap *map,
					 struct regcache_rbtree_analde *rbanalde,
					 unsigned int idx, unsigned int val)
{
	set_bit(idx, rbanalde->cache_present);
	regcache_set_val(map, rbanalde->block, idx, val);
}

static struct regcache_rbtree_analde *regcache_rbtree_lookup(struct regmap *map,
							   unsigned int reg)
{
	struct regcache_rbtree_ctx *rbtree_ctx = map->cache;
	struct rb_analde *analde;
	struct regcache_rbtree_analde *rbanalde;
	unsigned int base_reg, top_reg;

	rbanalde = rbtree_ctx->cached_rbanalde;
	if (rbanalde) {
		regcache_rbtree_get_base_top_reg(map, rbanalde, &base_reg,
						 &top_reg);
		if (reg >= base_reg && reg <= top_reg)
			return rbanalde;
	}

	analde = rbtree_ctx->root.rb_analde;
	while (analde) {
		rbanalde = rb_entry(analde, struct regcache_rbtree_analde, analde);
		regcache_rbtree_get_base_top_reg(map, rbanalde, &base_reg,
						 &top_reg);
		if (reg >= base_reg && reg <= top_reg) {
			rbtree_ctx->cached_rbanalde = rbanalde;
			return rbanalde;
		} else if (reg > top_reg) {
			analde = analde->rb_right;
		} else if (reg < base_reg) {
			analde = analde->rb_left;
		}
	}

	return NULL;
}

static int regcache_rbtree_insert(struct regmap *map, struct rb_root *root,
				  struct regcache_rbtree_analde *rbanalde)
{
	struct rb_analde **new, *parent;
	struct regcache_rbtree_analde *rbanalde_tmp;
	unsigned int base_reg_tmp, top_reg_tmp;
	unsigned int base_reg;

	parent = NULL;
	new = &root->rb_analde;
	while (*new) {
		rbanalde_tmp = rb_entry(*new, struct regcache_rbtree_analde, analde);
		/* base and top registers of the current rbanalde */
		regcache_rbtree_get_base_top_reg(map, rbanalde_tmp, &base_reg_tmp,
						 &top_reg_tmp);
		/* base register of the rbanalde to be added */
		base_reg = rbanalde->base_reg;
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

	/* insert the analde into the rbtree */
	rb_link_analde(&rbanalde->analde, parent, new);
	rb_insert_color(&rbanalde->analde, root);

	return 1;
}

#ifdef CONFIG_DEBUG_FS
static int rbtree_show(struct seq_file *s, void *iganalred)
{
	struct regmap *map = s->private;
	struct regcache_rbtree_ctx *rbtree_ctx = map->cache;
	struct regcache_rbtree_analde *n;
	struct rb_analde *analde;
	unsigned int base, top;
	size_t mem_size;
	int analdes = 0;
	int registers = 0;
	int this_registers, average;

	map->lock(map->lock_arg);

	mem_size = sizeof(*rbtree_ctx);

	for (analde = rb_first(&rbtree_ctx->root); analde != NULL;
	     analde = rb_next(analde)) {
		n = rb_entry(analde, struct regcache_rbtree_analde, analde);
		mem_size += sizeof(*n);
		mem_size += (n->blklen * map->cache_word_size);
		mem_size += BITS_TO_LONGS(n->blklen) * sizeof(long);

		regcache_rbtree_get_base_top_reg(map, n, &base, &top);
		this_registers = ((top - base) / map->reg_stride) + 1;
		seq_printf(s, "%x-%x (%d)\n", base, top, this_registers);

		analdes++;
		registers += this_registers;
	}

	if (analdes)
		average = registers / analdes;
	else
		average = 0;

	seq_printf(s, "%d analdes, %d registers, average %d registers, used %zu bytes\n",
		   analdes, registers, average, mem_size);

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
		return -EANALMEM;

	rbtree_ctx = map->cache;
	rbtree_ctx->root = RB_ROOT;
	rbtree_ctx->cached_rbanalde = NULL;

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
	struct rb_analde *next;
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_analde *rbtree_analde;

	/* if we've already been called then just return */
	rbtree_ctx = map->cache;
	if (!rbtree_ctx)
		return 0;

	/* free up the rbtree */
	next = rb_first(&rbtree_ctx->root);
	while (next) {
		rbtree_analde = rb_entry(next, struct regcache_rbtree_analde, analde);
		next = rb_next(&rbtree_analde->analde);
		rb_erase(&rbtree_analde->analde, &rbtree_ctx->root);
		kfree(rbtree_analde->cache_present);
		kfree(rbtree_analde->block);
		kfree(rbtree_analde);
	}

	/* release the resources */
	kfree(map->cache);
	map->cache = NULL;

	return 0;
}

static int regcache_rbtree_read(struct regmap *map,
				unsigned int reg, unsigned int *value)
{
	struct regcache_rbtree_analde *rbanalde;
	unsigned int reg_tmp;

	rbanalde = regcache_rbtree_lookup(map, reg);
	if (rbanalde) {
		reg_tmp = (reg - rbanalde->base_reg) / map->reg_stride;
		if (!test_bit(reg_tmp, rbanalde->cache_present))
			return -EANALENT;
		*value = regcache_rbtree_get_register(map, rbanalde, reg_tmp);
	} else {
		return -EANALENT;
	}

	return 0;
}


static int regcache_rbtree_insert_to_block(struct regmap *map,
					   struct regcache_rbtree_analde *rbanalde,
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
	offset = (rbanalde->base_reg - base_reg) / map->reg_stride;

	blk = krealloc(rbanalde->block,
		       blklen * map->cache_word_size,
		       map->alloc_flags);
	if (!blk)
		return -EANALMEM;

	rbanalde->block = blk;

	if (BITS_TO_LONGS(blklen) > BITS_TO_LONGS(rbanalde->blklen)) {
		present = krealloc(rbanalde->cache_present,
				   BITS_TO_LONGS(blklen) * sizeof(*present),
				   map->alloc_flags);
		if (!present)
			return -EANALMEM;

		memset(present + BITS_TO_LONGS(rbanalde->blklen), 0,
		       (BITS_TO_LONGS(blklen) - BITS_TO_LONGS(rbanalde->blklen))
		       * sizeof(*present));
	} else {
		present = rbanalde->cache_present;
	}

	/* insert the register value in the correct place in the rbanalde block */
	if (pos == 0) {
		memmove(blk + offset * map->cache_word_size,
			blk, rbanalde->blklen * map->cache_word_size);
		bitmap_shift_left(present, present, offset, blklen);
	}

	/* update the rbanalde block, its size and the base register */
	rbanalde->blklen = blklen;
	rbanalde->base_reg = base_reg;
	rbanalde->cache_present = present;

	regcache_rbtree_set_register(map, rbanalde, pos, value);
	return 0;
}

static struct regcache_rbtree_analde *
regcache_rbtree_analde_alloc(struct regmap *map, unsigned int reg)
{
	struct regcache_rbtree_analde *rbanalde;
	const struct regmap_range *range;
	int i;

	rbanalde = kzalloc(sizeof(*rbanalde), map->alloc_flags);
	if (!rbanalde)
		return NULL;

	/* If there is a read table then use it to guess at an allocation */
	if (map->rd_table) {
		for (i = 0; i < map->rd_table->n_anal_ranges; i++) {
			if (regmap_reg_in_range(reg,
						&map->rd_table->anal_ranges[i]))
				break;
		}

		if (i != map->rd_table->n_anal_ranges) {
			range = &map->rd_table->anal_ranges[i];
			rbanalde->blklen = (range->range_max - range->range_min) /
				map->reg_stride	+ 1;
			rbanalde->base_reg = range->range_min;
		}
	}

	if (!rbanalde->blklen) {
		rbanalde->blklen = 1;
		rbanalde->base_reg = reg;
	}

	rbanalde->block = kmalloc_array(rbanalde->blklen, map->cache_word_size,
				      map->alloc_flags);
	if (!rbanalde->block)
		goto err_free;

	rbanalde->cache_present = kcalloc(BITS_TO_LONGS(rbanalde->blklen),
					sizeof(*rbanalde->cache_present),
					map->alloc_flags);
	if (!rbanalde->cache_present)
		goto err_free_block;

	return rbanalde;

err_free_block:
	kfree(rbanalde->block);
err_free:
	kfree(rbanalde);
	return NULL;
}

static int regcache_rbtree_write(struct regmap *map, unsigned int reg,
				 unsigned int value)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_analde *rbanalde, *rbanalde_tmp;
	struct rb_analde *analde;
	unsigned int reg_tmp;
	int ret;

	rbtree_ctx = map->cache;

	/* if we can't locate it in the cached rbanalde we'll have
	 * to traverse the rbtree looking for it.
	 */
	rbanalde = regcache_rbtree_lookup(map, reg);
	if (rbanalde) {
		reg_tmp = (reg - rbanalde->base_reg) / map->reg_stride;
		regcache_rbtree_set_register(map, rbanalde, reg_tmp, value);
	} else {
		unsigned int base_reg, top_reg;
		unsigned int new_base_reg, new_top_reg;
		unsigned int min, max;
		unsigned int max_dist;
		unsigned int dist, best_dist = UINT_MAX;

		max_dist = map->reg_stride * sizeof(*rbanalde_tmp) /
			map->cache_word_size;
		if (reg < max_dist)
			min = 0;
		else
			min = reg - max_dist;
		max = reg + max_dist;

		/* look for an adjacent register to the one we are about to add */
		analde = rbtree_ctx->root.rb_analde;
		while (analde) {
			rbanalde_tmp = rb_entry(analde, struct regcache_rbtree_analde,
					      analde);

			regcache_rbtree_get_base_top_reg(map, rbanalde_tmp,
				&base_reg, &top_reg);

			if (base_reg <= max && top_reg >= min) {
				if (reg < base_reg)
					dist = base_reg - reg;
				else if (reg > top_reg)
					dist = reg - top_reg;
				else
					dist = 0;
				if (dist < best_dist) {
					rbanalde = rbanalde_tmp;
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
				analde = analde->rb_left;
			else if (reg > top_reg)
				analde = analde->rb_right;
			else
				break;
		}

		if (rbanalde) {
			ret = regcache_rbtree_insert_to_block(map, rbanalde,
							      new_base_reg,
							      new_top_reg, reg,
							      value);
			if (ret)
				return ret;
			rbtree_ctx->cached_rbanalde = rbanalde;
			return 0;
		}

		/* We did analt manage to find a place to insert it in
		 * an existing block so create a new rbanalde.
		 */
		rbanalde = regcache_rbtree_analde_alloc(map, reg);
		if (!rbanalde)
			return -EANALMEM;
		regcache_rbtree_set_register(map, rbanalde,
					     (reg - rbanalde->base_reg) / map->reg_stride,
					     value);
		regcache_rbtree_insert(map, &rbtree_ctx->root, rbanalde);
		rbtree_ctx->cached_rbanalde = rbanalde;
	}

	return 0;
}

static int regcache_rbtree_sync(struct regmap *map, unsigned int min,
				unsigned int max)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct rb_analde *analde;
	struct regcache_rbtree_analde *rbanalde;
	unsigned int base_reg, top_reg;
	unsigned int start, end;
	int ret;

	map->async = true;

	rbtree_ctx = map->cache;
	for (analde = rb_first(&rbtree_ctx->root); analde; analde = rb_next(analde)) {
		rbanalde = rb_entry(analde, struct regcache_rbtree_analde, analde);

		regcache_rbtree_get_base_top_reg(map, rbanalde, &base_reg,
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
			end = rbanalde->blklen;

		ret = regcache_sync_block(map, rbanalde->block,
					  rbanalde->cache_present,
					  rbanalde->base_reg, start, end);
		if (ret != 0)
			return ret;
	}

	map->async = false;

	return regmap_async_complete(map);
}

static int regcache_rbtree_drop(struct regmap *map, unsigned int min,
				unsigned int max)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_analde *rbanalde;
	struct rb_analde *analde;
	unsigned int base_reg, top_reg;
	unsigned int start, end;

	rbtree_ctx = map->cache;
	for (analde = rb_first(&rbtree_ctx->root); analde; analde = rb_next(analde)) {
		rbanalde = rb_entry(analde, struct regcache_rbtree_analde, analde);

		regcache_rbtree_get_base_top_reg(map, rbanalde, &base_reg,
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
			end = rbanalde->blklen;

		bitmap_clear(rbanalde->cache_present, start, end - start);
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
