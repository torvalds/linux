/*
 * Register cache access API - rbtree caching support
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/rbtree.h>

#include "internal.h"

static int regcache_rbtree_write(struct regmap *map, unsigned int reg,
				 unsigned int value);

struct regcache_rbtree_node {
	/* the actual rbtree node holding this block */
	struct rb_node node;
	/* base register handled by this block */
	unsigned int base_reg;
	/* number of bytes needed to represent the register index */
	unsigned int word_size;
	/* block of adjacent registers */
	void *block;
	/* number of registers available in the block */
	unsigned int blklen;
} __attribute__ ((packed));

struct regcache_rbtree_ctx {
	struct rb_root root;
	struct regcache_rbtree_node *cached_rbnode;
};

static inline void regcache_rbtree_get_base_top_reg(
	struct regcache_rbtree_node *rbnode,
	unsigned int *base, unsigned int *top)
{
	*base = rbnode->base_reg;
	*top = rbnode->base_reg + rbnode->blklen - 1;
}

static unsigned int regcache_rbtree_get_register(
	struct regcache_rbtree_node *rbnode, unsigned int idx)
{
	unsigned int val;

	switch (rbnode->word_size) {
	case 1: {
		u8 *p = rbnode->block;
		val = p[idx];
		return val;
	}
	case 2: {
		u16 *p = rbnode->block;
		val = p[idx];
		return val;
	}
	default:
		BUG();
		break;
	}
	return -1;
}

static void regcache_rbtree_set_register(struct regcache_rbtree_node *rbnode,
					 unsigned int idx, unsigned int val)
{
	switch (rbnode->word_size) {
	case 1: {
		u8 *p = rbnode->block;
		p[idx] = val;
		break;
	}
	case 2: {
		u16 *p = rbnode->block;
		p[idx] = val;
		break;
	}
	default:
		BUG();
		break;
	}
}

static struct regcache_rbtree_node *regcache_rbtree_lookup(
	struct rb_root *root, unsigned int reg)
{
	struct rb_node *node;
	struct regcache_rbtree_node *rbnode;
	unsigned int base_reg, top_reg;

	node = root->rb_node;
	while (node) {
		rbnode = container_of(node, struct regcache_rbtree_node, node);
		regcache_rbtree_get_base_top_reg(rbnode, &base_reg, &top_reg);
		if (reg >= base_reg && reg <= top_reg)
			return rbnode;
		else if (reg > top_reg)
			node = node->rb_right;
		else if (reg < base_reg)
			node = node->rb_left;
	}

	return NULL;
}

static int regcache_rbtree_insert(struct rb_root *root,
				  struct regcache_rbtree_node *rbnode)
{
	struct rb_node **new, *parent;
	struct regcache_rbtree_node *rbnode_tmp;
	unsigned int base_reg_tmp, top_reg_tmp;
	unsigned int base_reg;

	parent = NULL;
	new = &root->rb_node;
	while (*new) {
		rbnode_tmp = container_of(*new, struct regcache_rbtree_node,
					  node);
		/* base and top registers of the current rbnode */
		regcache_rbtree_get_base_top_reg(rbnode_tmp, &base_reg_tmp,
						 &top_reg_tmp);
		/* base register of the rbnode to be added */
		base_reg = rbnode->base_reg;
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

	/* insert the node into the rbtree */
	rb_link_node(&rbnode->node, parent, new);
	rb_insert_color(&rbnode->node, root);

	return 1;
}

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
	rbtree_ctx->cached_rbnode = NULL;

	for (i = 0; i < map->num_reg_defaults; i++) {
		ret = regcache_rbtree_write(map,
					    map->reg_defaults[i].reg,
					    map->reg_defaults[i].def);
		if (ret)
			goto err;
	}

	return 0;

err:
	regcache_exit(map);
	return ret;
}

static int regcache_rbtree_exit(struct regmap *map)
{
	struct rb_node *next;
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_node *rbtree_node;

	/* if we've already been called then just return */
	rbtree_ctx = map->cache;
	if (!rbtree_ctx)
		return 0;

	/* free up the rbtree */
	next = rb_first(&rbtree_ctx->root);
	while (next) {
		rbtree_node = rb_entry(next, struct regcache_rbtree_node, node);
		next = rb_next(&rbtree_node->node);
		rb_erase(&rbtree_node->node, &rbtree_ctx->root);
		kfree(rbtree_node->block);
		kfree(rbtree_node);
	}

	/* release the resources */
	kfree(map->cache);
	map->cache = NULL;

	return 0;
}

static int regcache_rbtree_read(struct regmap *map,
				unsigned int reg, unsigned int *value)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_node *rbnode;
	unsigned int base_reg, top_reg;
	unsigned int reg_tmp;

	rbtree_ctx = map->cache;
	/* look up the required register in the cached rbnode */
	rbnode = rbtree_ctx->cached_rbnode;
	if (rbnode) {
		regcache_rbtree_get_base_top_reg(rbnode, &base_reg, &top_reg);
		if (reg >= base_reg && reg <= top_reg) {
			reg_tmp = reg - base_reg;
			*value = regcache_rbtree_get_register(rbnode, reg_tmp);
			return 0;
		}
	}
	/* if we can't locate it in the cached rbnode we'll have
	 * to traverse the rbtree looking for it.
	 */
	rbnode = regcache_rbtree_lookup(&rbtree_ctx->root, reg);
	if (rbnode) {
		reg_tmp = reg - rbnode->base_reg;
		*value = regcache_rbtree_get_register(rbnode, reg_tmp);
		rbtree_ctx->cached_rbnode = rbnode;
	} else {
		/* uninitialized registers default to 0 */
		*value = 0;
	}

	return 0;
}


static int regcache_rbtree_insert_to_block(struct regcache_rbtree_node *rbnode,
					   unsigned int pos, unsigned int reg,
					   unsigned int value)
{
	u8 *blk;

	blk = krealloc(rbnode->block,
		       (rbnode->blklen + 1) * rbnode->word_size, GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	/* insert the register value in the correct place in the rbnode block */
	memmove(blk + (pos + 1) * rbnode->word_size,
		blk + pos * rbnode->word_size,
		(rbnode->blklen - pos) * rbnode->word_size);

	/* update the rbnode block, its size and the base register */
	rbnode->block = blk;
	rbnode->blklen++;
	if (!pos)
		rbnode->base_reg = reg;

	regcache_rbtree_set_register(rbnode, pos, value);
	return 0;
}

static int regcache_rbtree_write(struct regmap *map, unsigned int reg,
				 unsigned int value)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_node *rbnode, *rbnode_tmp;
	struct rb_node *node;
	unsigned int val;
	unsigned int reg_tmp;
	unsigned int base_reg, top_reg;
	unsigned int pos;
	int i;
	int ret;

	rbtree_ctx = map->cache;
	/* look up the required register in the cached rbnode */
	rbnode = rbtree_ctx->cached_rbnode;
	if (rbnode) {
		regcache_rbtree_get_base_top_reg(rbnode, &base_reg, &top_reg);
		if (reg >= base_reg && reg <= top_reg) {
			reg_tmp = reg - base_reg;
			val = regcache_rbtree_get_register(rbnode, reg_tmp);
			if (val == value)
				return 0;
			regcache_rbtree_set_register(rbnode, reg_tmp, value);
			return 0;
		}
	}
	/* if we can't locate it in the cached rbnode we'll have
	 * to traverse the rbtree looking for it.
	 */
	rbnode = regcache_rbtree_lookup(&rbtree_ctx->root, reg);
	if (rbnode) {
		reg_tmp = reg - rbnode->base_reg;
		val = regcache_rbtree_get_register(rbnode, reg_tmp);
		if (val == value)
			return 0;
		regcache_rbtree_set_register(rbnode, reg_tmp, value);
		rbtree_ctx->cached_rbnode = rbnode;
	} else {
		/* bail out early, no need to create the rbnode yet */
		if (!value)
			return 0;
		/* look for an adjacent register to the one we are about to add */
		for (node = rb_first(&rbtree_ctx->root); node;
		     node = rb_next(node)) {
			rbnode_tmp = rb_entry(node, struct regcache_rbtree_node, node);
			for (i = 0; i < rbnode_tmp->blklen; i++) {
				reg_tmp = rbnode_tmp->base_reg + i;
				if (abs(reg_tmp - reg) != 1)
					continue;
				/* decide where in the block to place our register */
				if (reg_tmp + 1 == reg)
					pos = i + 1;
				else
					pos = i;
				ret = regcache_rbtree_insert_to_block(rbnode_tmp, pos,
								      reg, value);
				if (ret)
					return ret;
				rbtree_ctx->cached_rbnode = rbnode_tmp;
				return 0;
			}
		}
		/* we did not manage to find a place to insert it in an existing
		 * block so create a new rbnode with a single register in its block.
		 * This block will get populated further if any other adjacent
		 * registers get modified in the future.
		 */
		rbnode = kzalloc(sizeof *rbnode, GFP_KERNEL);
		if (!rbnode)
			return -ENOMEM;
		rbnode->blklen = 1;
		rbnode->base_reg = reg;
		rbnode->word_size = map->cache_word_size;
		rbnode->block = kmalloc(rbnode->blklen * rbnode->word_size,
					GFP_KERNEL);
		if (!rbnode->block) {
			kfree(rbnode);
			return -ENOMEM;
		}
		regcache_rbtree_set_register(rbnode, 0, value);
		regcache_rbtree_insert(&rbtree_ctx->root, rbnode);
		rbtree_ctx->cached_rbnode = rbnode;
	}

	return 0;
}

static int regcache_rbtree_sync(struct regmap *map)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct rb_node *node;
	struct regcache_rbtree_node *rbnode;
	unsigned int regtmp;
	unsigned int val, def;
	int ret;
	int i;

	rbtree_ctx = map->cache;
	for (node = rb_first(&rbtree_ctx->root); node; node = rb_next(node)) {
		rbnode = rb_entry(node, struct regcache_rbtree_node, node);
		for (i = 0; i < rbnode->blklen; i++) {
			regtmp = rbnode->base_reg + i;
			val = regcache_rbtree_get_register(rbnode, i);
			ret = regcache_lookup_reg(map, i);
			if (ret < 0)
				def = 0;
			else
				def = map->reg_defaults[ret].def;
			if (val == def)
				continue;
			map->cache_bypass = 1;
			ret = regmap_write(map, regtmp, val);
			map->cache_bypass = 0;
			if (ret)
				return ret;
			dev_dbg(map->dev, "Synced register %#x, value %#x\n",
				regtmp, val);
		}
	}

	return 0;
}

struct regcache_ops regcache_rbtree_ops = {
	.type = REGCACHE_RBTREE,
	.name = "rbtree",
	.init = regcache_rbtree_init,
	.exit = regcache_rbtree_exit,
	.read = regcache_rbtree_read,
	.write = regcache_rbtree_write,
	.sync = regcache_rbtree_sync
};
