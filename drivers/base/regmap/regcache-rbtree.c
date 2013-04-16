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
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>

#include "internal.h"

static int regcache_rbtree_write(struct regmap *map, unsigned int reg,
				 unsigned int value);
static int regcache_rbtree_exit(struct regmap *map);

struct regcache_rbtree_node {
	/* the actual rbtree node holding this block */
	struct rb_node node;
	/* base register handled by this block */
	unsigned int base_reg;
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
	struct regmap *map,
	struct regcache_rbtree_node *rbnode,
	unsigned int *base, unsigned int *top)
{
	*base = rbnode->base_reg;
	*top = rbnode->base_reg + ((rbnode->blklen - 1) * map->reg_stride);
}

static unsigned int regcache_rbtree_get_register(struct regmap *map,
	struct regcache_rbtree_node *rbnode, unsigned int idx)
{
	return regcache_get_val(map, rbnode->block, idx);
}

static void regcache_rbtree_set_register(struct regmap *map,
					 struct regcache_rbtree_node *rbnode,
					 unsigned int idx, unsigned int val)
{
	regcache_set_val(map, rbnode->block, idx, val);
}

static struct regcache_rbtree_node *regcache_rbtree_lookup(struct regmap *map,
							   unsigned int reg)
{
	struct regcache_rbtree_ctx *rbtree_ctx = map->cache;
	struct rb_node *node;
	struct regcache_rbtree_node *rbnode;
	unsigned int base_reg, top_reg;

	rbnode = rbtree_ctx->cached_rbnode;
	if (rbnode) {
		regcache_rbtree_get_base_top_reg(map, rbnode, &base_reg,
						 &top_reg);
		if (reg >= base_reg && reg <= top_reg)
			return rbnode;
	}

	node = rbtree_ctx->root.rb_node;
	while (node) {
		rbnode = container_of(node, struct regcache_rbtree_node, node);
		regcache_rbtree_get_base_top_reg(map, rbnode, &base_reg,
						 &top_reg);
		if (reg >= base_reg && reg <= top_reg) {
			rbtree_ctx->cached_rbnode = rbnode;
			return rbnode;
		} else if (reg > top_reg) {
			node = node->rb_right;
		} else if (reg < base_reg) {
			node = node->rb_left;
		}
	}

	return NULL;
}

static int regcache_rbtree_insert(struct regmap *map, struct rb_root *root,
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
		regcache_rbtree_get_base_top_reg(map, rbnode_tmp, &base_reg_tmp,
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

#ifdef CONFIG_DEBUG_FS
static int rbtree_show(struct seq_file *s, void *ignored)
{
	struct regmap *map = s->private;
	struct regcache_rbtree_ctx *rbtree_ctx = map->cache;
	struct regcache_rbtree_node *n;
	struct rb_node *node;
	unsigned int base, top;
	size_t mem_size;
	int nodes = 0;
	int registers = 0;
	int this_registers, average;

	map->lock(map);

	mem_size = sizeof(*rbtree_ctx);
	mem_size += BITS_TO_LONGS(map->cache_present_nbits) * sizeof(long);

	for (node = rb_first(&rbtree_ctx->root); node != NULL;
	     node = rb_next(node)) {
		n = container_of(node, struct regcache_rbtree_node, node);
		mem_size += sizeof(*n);
		mem_size += (n->blklen * map->cache_word_size);

		regcache_rbtree_get_base_top_reg(map, n, &base, &top);
		this_registers = ((top - base) / map->reg_stride) + 1;
		seq_printf(s, "%x-%x (%d)\n", base, top, this_registers);

		nodes++;
		registers += this_registers;
	}

	if (nodes)
		average = registers / nodes;
	else
		average = 0;

	seq_printf(s, "%d nodes, %d registers, average %d registers, used %zu bytes\n",
		   nodes, registers, average, mem_size);

	map->unlock(map);

	return 0;
}

static int rbtree_open(struct inode *inode, struct file *file)
{
	return single_open(file, rbtree_show, inode->i_private);
}

static const struct file_operations rbtree_fops = {
	.open		= rbtree_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void rbtree_debugfs_init(struct regmap *map)
{
	debugfs_create_file("rbtree", 0400, map->debugfs, map, &rbtree_fops);
}
#else
static void rbtree_debugfs_init(struct regmap *map)
{
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
	rbtree_ctx->cached_rbnode = NULL;

	for (i = 0; i < map->num_reg_defaults; i++) {
		ret = regcache_rbtree_write(map,
					    map->reg_defaults[i].reg,
					    map->reg_defaults[i].def);
		if (ret)
			goto err;
	}

	rbtree_debugfs_init(map);

	return 0;

err:
	regcache_rbtree_exit(map);
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
	struct regcache_rbtree_node *rbnode;
	unsigned int reg_tmp;

	rbnode = regcache_rbtree_lookup(map, reg);
	if (rbnode) {
		reg_tmp = (reg - rbnode->base_reg) / map->reg_stride;
		if (!regcache_reg_present(map, reg))
			return -ENOENT;
		*value = regcache_rbtree_get_register(map, rbnode, reg_tmp);
	} else {
		return -ENOENT;
	}

	return 0;
}


static int regcache_rbtree_insert_to_block(struct regmap *map,
					   struct regcache_rbtree_node *rbnode,
					   unsigned int pos, unsigned int reg,
					   unsigned int value)
{
	u8 *blk;

	blk = krealloc(rbnode->block,
		       (rbnode->blklen + 1) * map->cache_word_size,
		       GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	/* insert the register value in the correct place in the rbnode block */
	memmove(blk + (pos + 1) * map->cache_word_size,
		blk + pos * map->cache_word_size,
		(rbnode->blklen - pos) * map->cache_word_size);

	/* update the rbnode block, its size and the base register */
	rbnode->block = blk;
	rbnode->blklen++;
	if (!pos)
		rbnode->base_reg = reg;

	regcache_rbtree_set_register(map, rbnode, pos, value);
	return 0;
}

static int regcache_rbtree_write(struct regmap *map, unsigned int reg,
				 unsigned int value)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct regcache_rbtree_node *rbnode, *rbnode_tmp;
	struct rb_node *node;
	unsigned int reg_tmp;
	unsigned int pos;
	int i;
	int ret;

	rbtree_ctx = map->cache;
	/* update the reg_present bitmap, make space if necessary */
	ret = regcache_set_reg_present(map, reg);
	if (ret < 0)
		return ret;

	/* if we can't locate it in the cached rbnode we'll have
	 * to traverse the rbtree looking for it.
	 */
	rbnode = regcache_rbtree_lookup(map, reg);
	if (rbnode) {
		reg_tmp = (reg - rbnode->base_reg) / map->reg_stride;
		regcache_rbtree_set_register(map, rbnode, reg_tmp, value);
	} else {
		/* look for an adjacent register to the one we are about to add */
		for (node = rb_first(&rbtree_ctx->root); node;
		     node = rb_next(node)) {
			rbnode_tmp = rb_entry(node, struct regcache_rbtree_node,
					      node);
			for (i = 0; i < rbnode_tmp->blklen; i++) {
				reg_tmp = rbnode_tmp->base_reg +
						(i * map->reg_stride);
				if (abs(reg_tmp - reg) != map->reg_stride)
					continue;
				/* decide where in the block to place our register */
				if (reg_tmp + map->reg_stride == reg)
					pos = i + 1;
				else
					pos = i;
				ret = regcache_rbtree_insert_to_block(map,
								      rbnode_tmp,
								      pos, reg,
								      value);
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
		rbnode->blklen = sizeof(*rbnode);
		rbnode->base_reg = reg;
		rbnode->block = kmalloc(rbnode->blklen * map->cache_word_size,
					GFP_KERNEL);
		if (!rbnode->block) {
			kfree(rbnode);
			return -ENOMEM;
		}
		regcache_rbtree_set_register(map, rbnode, 0, value);
		regcache_rbtree_insert(map, &rbtree_ctx->root, rbnode);
		rbtree_ctx->cached_rbnode = rbnode;
	}

	return 0;
}

static int regcache_rbtree_sync(struct regmap *map, unsigned int min,
				unsigned int max)
{
	struct regcache_rbtree_ctx *rbtree_ctx;
	struct rb_node *node;
	struct regcache_rbtree_node *rbnode;
	int ret;
	int base, end;

	rbtree_ctx = map->cache;
	for (node = rb_first(&rbtree_ctx->root); node; node = rb_next(node)) {
		rbnode = rb_entry(node, struct regcache_rbtree_node, node);

		if (rbnode->base_reg < min)
			continue;
		if (rbnode->base_reg > max)
			break;
		if (rbnode->base_reg + rbnode->blklen < min)
			continue;

		if (min > rbnode->base_reg)
			base = min - rbnode->base_reg;
		else
			base = 0;

		if (max < rbnode->base_reg + rbnode->blklen)
			end = max - rbnode->base_reg + 1;
		else
			end = rbnode->blklen;

		ret = regcache_sync_block(map, rbnode->block, rbnode->base_reg,
					  base, end);
		if (ret != 0)
			return ret;
	}

	return regmap_async_complete(map);
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
