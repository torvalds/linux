// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Red Hat, Inc.
 * Author: Jason Wang <jasowang@redhat.com>
 *
 * IOTLB implementation for vhost.
 */
#include <linux/slab.h>
#include <linux/vhost_iotlb.h>
#include <linux/module.h>

#define MOD_VERSION  "0.1"
#define MOD_DESC     "VHOST IOTLB"
#define MOD_AUTHOR   "Jason Wang <jasowang@redhat.com>"
#define MOD_LICENSE  "GPL v2"

#define START(map) ((map)->start)
#define LAST(map) ((map)->last)

INTERVAL_TREE_DEFINE(struct vhost_iotlb_map,
		     rb, __u64, __subtree_last,
		     START, LAST, static inline, vhost_iotlb_itree);

/**
 * vhost_iotlb_map_free - remove a map node and free it
 * @iotlb: the IOTLB
 * @map: the map that want to be remove and freed
 */
void vhost_iotlb_map_free(struct vhost_iotlb *iotlb,
			  struct vhost_iotlb_map *map)
{
	vhost_iotlb_itree_remove(map, &iotlb->root);
	list_del(&map->link);
	kfree(map);
	iotlb->nmaps--;
}
EXPORT_SYMBOL_GPL(vhost_iotlb_map_free);

/**
 * vhost_iotlb_add_range_ctx - add a new range to vhost IOTLB
 * @iotlb: the IOTLB
 * @start: start of the IOVA range
 * @last: last of IOVA range
 * @addr: the address that is mapped to @start
 * @perm: access permission of this range
 * @opaque: the opaque pointer for the new mapping
 *
 * Returns an error last is smaller than start or memory allocation
 * fails
 */
int vhost_iotlb_add_range_ctx(struct vhost_iotlb *iotlb,
			      u64 start, u64 last,
			      u64 addr, unsigned int perm,
			      void *opaque)
{
	struct vhost_iotlb_map *map;

	if (last < start)
		return -EFAULT;

	/* If the range being mapped is [0, ULONG_MAX], split it into two entries
	 * otherwise its size would overflow u64.
	 */
	if (start == 0 && last == ULONG_MAX) {
		u64 mid = last / 2;
		int err = vhost_iotlb_add_range_ctx(iotlb, start, mid, addr,
				perm, opaque);

		if (err)
			return err;

		addr += mid + 1;
		start = mid + 1;
	}

	if (iotlb->limit &&
	    iotlb->nmaps == iotlb->limit &&
	    iotlb->flags & VHOST_IOTLB_FLAG_RETIRE) {
		map = list_first_entry(&iotlb->list, typeof(*map), link);
		vhost_iotlb_map_free(iotlb, map);
	}

	map = kmalloc(sizeof(*map), GFP_ATOMIC);
	if (!map)
		return -ENOMEM;

	map->start = start;
	map->size = last - start + 1;
	map->last = last;
	map->addr = addr;
	map->perm = perm;
	map->opaque = opaque;

	iotlb->nmaps++;
	vhost_iotlb_itree_insert(map, &iotlb->root);

	INIT_LIST_HEAD(&map->link);
	list_add_tail(&map->link, &iotlb->list);

	return 0;
}
EXPORT_SYMBOL_GPL(vhost_iotlb_add_range_ctx);

int vhost_iotlb_add_range(struct vhost_iotlb *iotlb,
			  u64 start, u64 last,
			  u64 addr, unsigned int perm)
{
	return vhost_iotlb_add_range_ctx(iotlb, start, last,
					 addr, perm, NULL);
}
EXPORT_SYMBOL_GPL(vhost_iotlb_add_range);

/**
 * vhost_iotlb_del_range - delete overlapped ranges from vhost IOTLB
 * @iotlb: the IOTLB
 * @start: start of the IOVA range
 * @last: last of IOVA range
 */
void vhost_iotlb_del_range(struct vhost_iotlb *iotlb, u64 start, u64 last)
{
	struct vhost_iotlb_map *map;

	while ((map = vhost_iotlb_itree_iter_first(&iotlb->root,
						   start, last)))
		vhost_iotlb_map_free(iotlb, map);
}
EXPORT_SYMBOL_GPL(vhost_iotlb_del_range);

/**
 * vhost_iotlb_alloc - add a new vhost IOTLB
 * @limit: maximum number of IOTLB entries
 * @flags: VHOST_IOTLB_FLAG_XXX
 *
 * Returns an error is memory allocation fails
 */
struct vhost_iotlb *vhost_iotlb_alloc(unsigned int limit, unsigned int flags)
{
	struct vhost_iotlb *iotlb = kzalloc(sizeof(*iotlb), GFP_KERNEL);

	if (!iotlb)
		return NULL;

	iotlb->root = RB_ROOT_CACHED;
	iotlb->limit = limit;
	iotlb->nmaps = 0;
	iotlb->flags = flags;
	INIT_LIST_HEAD(&iotlb->list);

	return iotlb;
}
EXPORT_SYMBOL_GPL(vhost_iotlb_alloc);

/**
 * vhost_iotlb_reset - reset vhost IOTLB (free all IOTLB entries)
 * @iotlb: the IOTLB to be reset
 */
void vhost_iotlb_reset(struct vhost_iotlb *iotlb)
{
	vhost_iotlb_del_range(iotlb, 0ULL, 0ULL - 1);
}
EXPORT_SYMBOL_GPL(vhost_iotlb_reset);

/**
 * vhost_iotlb_free - reset and free vhost IOTLB
 * @iotlb: the IOTLB to be freed
 */
void vhost_iotlb_free(struct vhost_iotlb *iotlb)
{
	if (iotlb) {
		vhost_iotlb_reset(iotlb);
		kfree(iotlb);
	}
}
EXPORT_SYMBOL_GPL(vhost_iotlb_free);

/**
 * vhost_iotlb_itree_first - return the first overlapped range
 * @iotlb: the IOTLB
 * @start: start of IOVA range
 * @last: last byte in IOVA range
 */
struct vhost_iotlb_map *
vhost_iotlb_itree_first(struct vhost_iotlb *iotlb, u64 start, u64 last)
{
	return vhost_iotlb_itree_iter_first(&iotlb->root, start, last);
}
EXPORT_SYMBOL_GPL(vhost_iotlb_itree_first);

/**
 * vhost_iotlb_itree_next - return the next overlapped range
 * @map: the starting map node
 * @start: start of IOVA range
 * @last: last byte IOVA range
 */
struct vhost_iotlb_map *
vhost_iotlb_itree_next(struct vhost_iotlb_map *map, u64 start, u64 last)
{
	return vhost_iotlb_itree_iter_next(map, start, last);
}
EXPORT_SYMBOL_GPL(vhost_iotlb_itree_next);

MODULE_VERSION(MOD_VERSION);
MODULE_DESCRIPTION(MOD_DESC);
MODULE_AUTHOR(MOD_AUTHOR);
MODULE_LICENSE(MOD_LICENSE);
