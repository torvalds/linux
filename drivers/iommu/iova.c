/*
 * Copyright © 2006-2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Author: Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 */

#include <linux/iova.h>
#include <linux/slab.h>

static struct kmem_cache *iommu_iova_cache;

int iommu_iova_cache_init(void)
{
	int ret = 0;

	iommu_iova_cache = kmem_cache_create("iommu_iova",
					 sizeof(struct iova),
					 0,
					 SLAB_HWCACHE_ALIGN,
					 NULL);
	if (!iommu_iova_cache) {
		pr_err("Couldn't create iova cache\n");
		ret = -ENOMEM;
	}

	return ret;
}

void iommu_iova_cache_destroy(void)
{
	kmem_cache_destroy(iommu_iova_cache);
}

struct iova *alloc_iova_mem(void)
{
	return kmem_cache_alloc(iommu_iova_cache, GFP_ATOMIC);
}

void free_iova_mem(struct iova *iova)
{
	kmem_cache_free(iommu_iova_cache, iova);
}

void
init_iova_domain(struct iova_domain *iovad, unsigned long granule,
	unsigned long start_pfn, unsigned long pfn_32bit)
{
	/*
	 * IOVA granularity will normally be equal to the smallest
	 * supported IOMMU page size; both *must* be capable of
	 * representing individual CPU pages exactly.
	 */
	BUG_ON((granule > PAGE_SIZE) || !is_power_of_2(granule));

	spin_lock_init(&iovad->iova_rbtree_lock);
	iovad->rbroot = RB_ROOT;
	iovad->cached32_node = NULL;
	iovad->granule = granule;
	iovad->start_pfn = start_pfn;
	iovad->dma_32bit_pfn = pfn_32bit;
}

static struct rb_node *
__get_cached_rbnode(struct iova_domain *iovad, unsigned long *limit_pfn)
{
	if ((*limit_pfn != iovad->dma_32bit_pfn) ||
		(iovad->cached32_node == NULL))
		return rb_last(&iovad->rbroot);
	else {
		struct rb_node *prev_node = rb_prev(iovad->cached32_node);
		struct iova *curr_iova =
			container_of(iovad->cached32_node, struct iova, node);
		*limit_pfn = curr_iova->pfn_lo - 1;
		return prev_node;
	}
}

static void
__cached_rbnode_insert_update(struct iova_domain *iovad,
	unsigned long limit_pfn, struct iova *new)
{
	if (limit_pfn != iovad->dma_32bit_pfn)
		return;
	iovad->cached32_node = &new->node;
}

static void
__cached_rbnode_delete_update(struct iova_domain *iovad, struct iova *free)
{
	struct iova *cached_iova;
	struct rb_node *curr;

	if (!iovad->cached32_node)
		return;
	curr = iovad->cached32_node;
	cached_iova = container_of(curr, struct iova, node);

	if (free->pfn_lo >= cached_iova->pfn_lo) {
		struct rb_node *node = rb_next(&free->node);
		struct iova *iova = container_of(node, struct iova, node);

		/* only cache if it's below 32bit pfn */
		if (node && iova->pfn_lo < iovad->dma_32bit_pfn)
			iovad->cached32_node = node;
		else
			iovad->cached32_node = NULL;
	}
}

/* Computes the padding size required, to make the
 * the start address naturally aligned on its size
 */
static int
iova_get_pad_size(int size, unsigned int limit_pfn)
{
	unsigned int pad_size = 0;
	unsigned int order = ilog2(size);

	if (order)
		pad_size = (limit_pfn + 1) % (1 << order);

	return pad_size;
}

static int __alloc_and_insert_iova_range(struct iova_domain *iovad,
		unsigned long size, unsigned long limit_pfn,
			struct iova *new, bool size_aligned)
{
	struct rb_node *prev, *curr = NULL;
	unsigned long flags;
	unsigned long saved_pfn;
	unsigned int pad_size = 0;

	/* Walk the tree backwards */
	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	saved_pfn = limit_pfn;
	curr = __get_cached_rbnode(iovad, &limit_pfn);
	prev = curr;
	while (curr) {
		struct iova *curr_iova = container_of(curr, struct iova, node);

		if (limit_pfn < curr_iova->pfn_lo)
			goto move_left;
		else if (limit_pfn < curr_iova->pfn_hi)
			goto adjust_limit_pfn;
		else {
			if (size_aligned)
				pad_size = iova_get_pad_size(size, limit_pfn);
			if ((curr_iova->pfn_hi + size + pad_size) <= limit_pfn)
				break;	/* found a free slot */
		}
adjust_limit_pfn:
		limit_pfn = curr_iova->pfn_lo - 1;
move_left:
		prev = curr;
		curr = rb_prev(curr);
	}

	if (!curr) {
		if (size_aligned)
			pad_size = iova_get_pad_size(size, limit_pfn);
		if ((iovad->start_pfn + size + pad_size) > limit_pfn) {
			spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
			return -ENOMEM;
		}
	}

	/* pfn_lo will point to size aligned address if size_aligned is set */
	new->pfn_lo = limit_pfn - (size + pad_size) + 1;
	new->pfn_hi = new->pfn_lo + size - 1;

	/* Insert the new_iova into domain rbtree by holding writer lock */
	/* Add new node and rebalance tree. */
	{
		struct rb_node **entry, *parent = NULL;

		/* If we have 'prev', it's a valid place to start the
		   insertion. Otherwise, start from the root. */
		if (prev)
			entry = &prev;
		else
			entry = &iovad->rbroot.rb_node;

		/* Figure out where to put new node */
		while (*entry) {
			struct iova *this = container_of(*entry,
							struct iova, node);
			parent = *entry;

			if (new->pfn_lo < this->pfn_lo)
				entry = &((*entry)->rb_left);
			else if (new->pfn_lo > this->pfn_lo)
				entry = &((*entry)->rb_right);
			else
				BUG(); /* this should not happen */
		}

		/* Add new node and rebalance tree. */
		rb_link_node(&new->node, parent, entry);
		rb_insert_color(&new->node, &iovad->rbroot);
	}
	__cached_rbnode_insert_update(iovad, saved_pfn, new);

	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);


	return 0;
}

static void
iova_insert_rbtree(struct rb_root *root, struct iova *iova)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	/* Figure out where to put new node */
	while (*new) {
		struct iova *this = container_of(*new, struct iova, node);
		parent = *new;

		if (iova->pfn_lo < this->pfn_lo)
			new = &((*new)->rb_left);
		else if (iova->pfn_lo > this->pfn_lo)
			new = &((*new)->rb_right);
		else
			BUG(); /* this should not happen */
	}
	/* Add new node and rebalance tree. */
	rb_link_node(&iova->node, parent, new);
	rb_insert_color(&iova->node, root);
}

/**
 * alloc_iova - allocates an iova
 * @iovad: - iova domain in question
 * @size: - size of page frames to allocate
 * @limit_pfn: - max limit address
 * @size_aligned: - set if size_aligned address range is required
 * This function allocates an iova in the range iovad->start_pfn to limit_pfn,
 * searching top-down from limit_pfn to iovad->start_pfn. If the size_aligned
 * flag is set then the allocated address iova->pfn_lo will be naturally
 * aligned on roundup_power_of_two(size).
 */
struct iova *
alloc_iova(struct iova_domain *iovad, unsigned long size,
	unsigned long limit_pfn,
	bool size_aligned)
{
	struct iova *new_iova;
	int ret;

	new_iova = alloc_iova_mem();
	if (!new_iova)
		return NULL;

	/* If size aligned is set then round the size to
	 * to next power of two.
	 */
	if (size_aligned)
		size = __roundup_pow_of_two(size);

	ret = __alloc_and_insert_iova_range(iovad, size, limit_pfn,
			new_iova, size_aligned);

	if (ret) {
		free_iova_mem(new_iova);
		return NULL;
	}

	return new_iova;
}

/**
 * find_iova - find's an iova for a given pfn
 * @iovad: - iova domain in question.
 * @pfn: - page frame number
 * This function finds and returns an iova belonging to the
 * given doamin which matches the given pfn.
 */
struct iova *find_iova(struct iova_domain *iovad, unsigned long pfn)
{
	unsigned long flags;
	struct rb_node *node;

	/* Take the lock so that no other thread is manipulating the rbtree */
	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	node = iovad->rbroot.rb_node;
	while (node) {
		struct iova *iova = container_of(node, struct iova, node);

		/* If pfn falls within iova's range, return iova */
		if ((pfn >= iova->pfn_lo) && (pfn <= iova->pfn_hi)) {
			spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
			/* We are not holding the lock while this iova
			 * is referenced by the caller as the same thread
			 * which called this function also calls __free_iova()
			 * and it is by design that only one thread can possibly
			 * reference a particular iova and hence no conflict.
			 */
			return iova;
		}

		if (pfn < iova->pfn_lo)
			node = node->rb_left;
		else if (pfn > iova->pfn_lo)
			node = node->rb_right;
	}

	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
	return NULL;
}

/**
 * __free_iova - frees the given iova
 * @iovad: iova domain in question.
 * @iova: iova in question.
 * Frees the given iova belonging to the giving domain
 */
void
__free_iova(struct iova_domain *iovad, struct iova *iova)
{
	unsigned long flags;

	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	__cached_rbnode_delete_update(iovad, iova);
	rb_erase(&iova->node, &iovad->rbroot);
	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
	free_iova_mem(iova);
}

/**
 * free_iova - finds and frees the iova for a given pfn
 * @iovad: - iova domain in question.
 * @pfn: - pfn that is allocated previously
 * This functions finds an iova for a given pfn and then
 * frees the iova from that domain.
 */
void
free_iova(struct iova_domain *iovad, unsigned long pfn)
{
	struct iova *iova = find_iova(iovad, pfn);
	if (iova)
		__free_iova(iovad, iova);

}

/**
 * put_iova_domain - destroys the iova doamin
 * @iovad: - iova domain in question.
 * All the iova's in that domain are destroyed.
 */
void put_iova_domain(struct iova_domain *iovad)
{
	struct rb_node *node;
	unsigned long flags;

	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	node = rb_first(&iovad->rbroot);
	while (node) {
		struct iova *iova = container_of(node, struct iova, node);
		rb_erase(node, &iovad->rbroot);
		free_iova_mem(iova);
		node = rb_first(&iovad->rbroot);
	}
	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
}

static int
__is_range_overlap(struct rb_node *node,
	unsigned long pfn_lo, unsigned long pfn_hi)
{
	struct iova *iova = container_of(node, struct iova, node);

	if ((pfn_lo <= iova->pfn_hi) && (pfn_hi >= iova->pfn_lo))
		return 1;
	return 0;
}

static inline struct iova *
alloc_and_init_iova(unsigned long pfn_lo, unsigned long pfn_hi)
{
	struct iova *iova;

	iova = alloc_iova_mem();
	if (iova) {
		iova->pfn_lo = pfn_lo;
		iova->pfn_hi = pfn_hi;
	}

	return iova;
}

static struct iova *
__insert_new_range(struct iova_domain *iovad,
	unsigned long pfn_lo, unsigned long pfn_hi)
{
	struct iova *iova;

	iova = alloc_and_init_iova(pfn_lo, pfn_hi);
	if (iova)
		iova_insert_rbtree(&iovad->rbroot, iova);

	return iova;
}

static void
__adjust_overlap_range(struct iova *iova,
	unsigned long *pfn_lo, unsigned long *pfn_hi)
{
	if (*pfn_lo < iova->pfn_lo)
		iova->pfn_lo = *pfn_lo;
	if (*pfn_hi > iova->pfn_hi)
		*pfn_lo = iova->pfn_hi + 1;
}

/**
 * reserve_iova - reserves an iova in the given range
 * @iovad: - iova domain pointer
 * @pfn_lo: - lower page frame address
 * @pfn_hi:- higher pfn adderss
 * This function allocates reserves the address range from pfn_lo to pfn_hi so
 * that this address is not dished out as part of alloc_iova.
 */
struct iova *
reserve_iova(struct iova_domain *iovad,
	unsigned long pfn_lo, unsigned long pfn_hi)
{
	struct rb_node *node;
	unsigned long flags;
	struct iova *iova;
	unsigned int overlap = 0;

	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	for (node = rb_first(&iovad->rbroot); node; node = rb_next(node)) {
		if (__is_range_overlap(node, pfn_lo, pfn_hi)) {
			iova = container_of(node, struct iova, node);
			__adjust_overlap_range(iova, &pfn_lo, &pfn_hi);
			if ((pfn_lo >= iova->pfn_lo) &&
				(pfn_hi <= iova->pfn_hi))
				goto finish;
			overlap = 1;

		} else if (overlap)
				break;
	}

	/* We are here either because this is the first reserver node
	 * or need to insert remaining non overlap addr range
	 */
	iova = __insert_new_range(iovad, pfn_lo, pfn_hi);
finish:

	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
	return iova;
}

/**
 * copy_reserved_iova - copies the reserved between domains
 * @from: - source doamin from where to copy
 * @to: - destination domin where to copy
 * This function copies reserved iova's from one doamin to
 * other.
 */
void
copy_reserved_iova(struct iova_domain *from, struct iova_domain *to)
{
	unsigned long flags;
	struct rb_node *node;

	spin_lock_irqsave(&from->iova_rbtree_lock, flags);
	for (node = rb_first(&from->rbroot); node; node = rb_next(node)) {
		struct iova *iova = container_of(node, struct iova, node);
		struct iova *new_iova;
		new_iova = reserve_iova(to, iova->pfn_lo, iova->pfn_hi);
		if (!new_iova)
			printk(KERN_ERR "Reserve iova range %lx@%lx failed\n",
				iova->pfn_lo, iova->pfn_lo);
	}
	spin_unlock_irqrestore(&from->iova_rbtree_lock, flags);
}

struct iova *
split_and_remove_iova(struct iova_domain *iovad, struct iova *iova,
		      unsigned long pfn_lo, unsigned long pfn_hi)
{
	unsigned long flags;
	struct iova *prev = NULL, *next = NULL;

	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	if (iova->pfn_lo < pfn_lo) {
		prev = alloc_and_init_iova(iova->pfn_lo, pfn_lo - 1);
		if (prev == NULL)
			goto error;
	}
	if (iova->pfn_hi > pfn_hi) {
		next = alloc_and_init_iova(pfn_hi + 1, iova->pfn_hi);
		if (next == NULL)
			goto error;
	}

	__cached_rbnode_delete_update(iovad, iova);
	rb_erase(&iova->node, &iovad->rbroot);

	if (prev) {
		iova_insert_rbtree(&iovad->rbroot, prev);
		iova->pfn_lo = pfn_lo;
	}
	if (next) {
		iova_insert_rbtree(&iovad->rbroot, next);
		iova->pfn_hi = pfn_hi;
	}
	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);

	return iova;

error:
	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
	if (prev)
		free_iova_mem(prev);
	return NULL;
}
