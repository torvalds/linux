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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/bitops.h>

static bool iova_rcache_insert(struct iova_domain *iovad,
			       unsigned long pfn,
			       unsigned long size);
static unsigned long iova_rcache_get(struct iova_domain *iovad,
				     unsigned long size,
				     unsigned long limit_pfn);
static void init_iova_rcaches(struct iova_domain *iovad);
static void free_iova_rcaches(struct iova_domain *iovad);

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
	init_iova_rcaches(iovad);
}
EXPORT_SYMBOL_GPL(init_iova_domain);

static struct rb_node *
__get_cached_rbnode(struct iova_domain *iovad, unsigned long *limit_pfn)
{
	if ((*limit_pfn > iovad->dma_32bit_pfn) ||
		(iovad->cached32_node == NULL))
		return rb_last(&iovad->rbroot);
	else {
		struct rb_node *prev_node = rb_prev(iovad->cached32_node);
		struct iova *curr_iova =
			rb_entry(iovad->cached32_node, struct iova, node);
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
	cached_iova = rb_entry(curr, struct iova, node);

	if (free->pfn_lo >= cached_iova->pfn_lo) {
		struct rb_node *node = rb_next(&free->node);
		struct iova *iova = rb_entry(node, struct iova, node);

		/* only cache if it's below 32bit pfn */
		if (node && iova->pfn_lo < iovad->dma_32bit_pfn)
			iovad->cached32_node = node;
		else
			iovad->cached32_node = NULL;
	}
}

/* Insert the iova into domain rbtree by holding writer lock */
static void
iova_insert_rbtree(struct rb_root *root, struct iova *iova,
		   struct rb_node *start)
{
	struct rb_node **new, *parent = NULL;

	new = (start) ? &start : &(root->rb_node);
	/* Figure out where to put new node */
	while (*new) {
		struct iova *this = rb_entry(*new, struct iova, node);

		parent = *new;

		if (iova->pfn_lo < this->pfn_lo)
			new = &((*new)->rb_left);
		else if (iova->pfn_lo > this->pfn_lo)
			new = &((*new)->rb_right);
		else {
			WARN_ON(1); /* this should not happen */
			return;
		}
	}
	/* Add new node and rebalance tree. */
	rb_link_node(&iova->node, parent, new);
	rb_insert_color(&iova->node, root);
}

/*
 * Computes the padding size required, to make the start address
 * naturally aligned on the power-of-two order of its size
 */
static unsigned int
iova_get_pad_size(unsigned int size, unsigned int limit_pfn)
{
	return (limit_pfn + 1 - size) & (__roundup_pow_of_two(size) - 1);
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
		struct iova *curr_iova = rb_entry(curr, struct iova, node);

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
		limit_pfn = curr_iova->pfn_lo ? (curr_iova->pfn_lo - 1) : 0;
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

	/* If we have 'prev', it's a valid place to start the insertion. */
	iova_insert_rbtree(&iovad->rbroot, new, prev);
	__cached_rbnode_insert_update(iovad, saved_pfn, new);

	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);


	return 0;
}

static struct kmem_cache *iova_cache;
static unsigned int iova_cache_users;
static DEFINE_MUTEX(iova_cache_mutex);

struct iova *alloc_iova_mem(void)
{
	return kmem_cache_alloc(iova_cache, GFP_ATOMIC);
}
EXPORT_SYMBOL(alloc_iova_mem);

void free_iova_mem(struct iova *iova)
{
	kmem_cache_free(iova_cache, iova);
}
EXPORT_SYMBOL(free_iova_mem);

int iova_cache_get(void)
{
	mutex_lock(&iova_cache_mutex);
	if (!iova_cache_users) {
		iova_cache = kmem_cache_create(
			"iommu_iova", sizeof(struct iova), 0,
			SLAB_HWCACHE_ALIGN, NULL);
		if (!iova_cache) {
			mutex_unlock(&iova_cache_mutex);
			printk(KERN_ERR "Couldn't create iova cache\n");
			return -ENOMEM;
		}
	}

	iova_cache_users++;
	mutex_unlock(&iova_cache_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(iova_cache_get);

void iova_cache_put(void)
{
	mutex_lock(&iova_cache_mutex);
	if (WARN_ON(!iova_cache_users)) {
		mutex_unlock(&iova_cache_mutex);
		return;
	}
	iova_cache_users--;
	if (!iova_cache_users)
		kmem_cache_destroy(iova_cache);
	mutex_unlock(&iova_cache_mutex);
}
EXPORT_SYMBOL_GPL(iova_cache_put);

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

	ret = __alloc_and_insert_iova_range(iovad, size, limit_pfn,
			new_iova, size_aligned);

	if (ret) {
		free_iova_mem(new_iova);
		return NULL;
	}

	return new_iova;
}
EXPORT_SYMBOL_GPL(alloc_iova);

static struct iova *
private_find_iova(struct iova_domain *iovad, unsigned long pfn)
{
	struct rb_node *node = iovad->rbroot.rb_node;

	assert_spin_locked(&iovad->iova_rbtree_lock);

	while (node) {
		struct iova *iova = rb_entry(node, struct iova, node);

		/* If pfn falls within iova's range, return iova */
		if ((pfn >= iova->pfn_lo) && (pfn <= iova->pfn_hi)) {
			return iova;
		}

		if (pfn < iova->pfn_lo)
			node = node->rb_left;
		else if (pfn > iova->pfn_lo)
			node = node->rb_right;
	}

	return NULL;
}

static void private_free_iova(struct iova_domain *iovad, struct iova *iova)
{
	assert_spin_locked(&iovad->iova_rbtree_lock);
	__cached_rbnode_delete_update(iovad, iova);
	rb_erase(&iova->node, &iovad->rbroot);
	free_iova_mem(iova);
}

/**
 * find_iova - finds an iova for a given pfn
 * @iovad: - iova domain in question.
 * @pfn: - page frame number
 * This function finds and returns an iova belonging to the
 * given doamin which matches the given pfn.
 */
struct iova *find_iova(struct iova_domain *iovad, unsigned long pfn)
{
	unsigned long flags;
	struct iova *iova;

	/* Take the lock so that no other thread is manipulating the rbtree */
	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	iova = private_find_iova(iovad, pfn);
	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
	return iova;
}
EXPORT_SYMBOL_GPL(find_iova);

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
	private_free_iova(iovad, iova);
	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
}
EXPORT_SYMBOL_GPL(__free_iova);

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
EXPORT_SYMBOL_GPL(free_iova);

/**
 * alloc_iova_fast - allocates an iova from rcache
 * @iovad: - iova domain in question
 * @size: - size of page frames to allocate
 * @limit_pfn: - max limit address
 * This function tries to satisfy an iova allocation from the rcache,
 * and falls back to regular allocation on failure.
*/
unsigned long
alloc_iova_fast(struct iova_domain *iovad, unsigned long size,
		unsigned long limit_pfn)
{
	bool flushed_rcache = false;
	unsigned long iova_pfn;
	struct iova *new_iova;

	iova_pfn = iova_rcache_get(iovad, size, limit_pfn);
	if (iova_pfn)
		return iova_pfn;

retry:
	new_iova = alloc_iova(iovad, size, limit_pfn, true);
	if (!new_iova) {
		unsigned int cpu;

		if (flushed_rcache)
			return 0;

		/* Try replenishing IOVAs by flushing rcache. */
		flushed_rcache = true;
		preempt_disable();
		for_each_online_cpu(cpu)
			free_cpu_cached_iovas(cpu, iovad);
		preempt_enable();
		goto retry;
	}

	return new_iova->pfn_lo;
}
EXPORT_SYMBOL_GPL(alloc_iova_fast);

/**
 * free_iova_fast - free iova pfn range into rcache
 * @iovad: - iova domain in question.
 * @pfn: - pfn that is allocated previously
 * @size: - # of pages in range
 * This functions frees an iova range by trying to put it into the rcache,
 * falling back to regular iova deallocation via free_iova() if this fails.
 */
void
free_iova_fast(struct iova_domain *iovad, unsigned long pfn, unsigned long size)
{
	if (iova_rcache_insert(iovad, pfn, size))
		return;

	free_iova(iovad, pfn);
}
EXPORT_SYMBOL_GPL(free_iova_fast);

/**
 * put_iova_domain - destroys the iova doamin
 * @iovad: - iova domain in question.
 * All the iova's in that domain are destroyed.
 */
void put_iova_domain(struct iova_domain *iovad)
{
	struct rb_node *node;
	unsigned long flags;

	free_iova_rcaches(iovad);
	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	node = rb_first(&iovad->rbroot);
	while (node) {
		struct iova *iova = rb_entry(node, struct iova, node);

		rb_erase(node, &iovad->rbroot);
		free_iova_mem(iova);
		node = rb_first(&iovad->rbroot);
	}
	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
}
EXPORT_SYMBOL_GPL(put_iova_domain);

static int
__is_range_overlap(struct rb_node *node,
	unsigned long pfn_lo, unsigned long pfn_hi)
{
	struct iova *iova = rb_entry(node, struct iova, node);

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
		iova_insert_rbtree(&iovad->rbroot, iova, NULL);

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
			iova = rb_entry(node, struct iova, node);
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
EXPORT_SYMBOL_GPL(reserve_iova);

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
		struct iova *iova = rb_entry(node, struct iova, node);
		struct iova *new_iova;

		new_iova = reserve_iova(to, iova->pfn_lo, iova->pfn_hi);
		if (!new_iova)
			printk(KERN_ERR "Reserve iova range %lx@%lx failed\n",
				iova->pfn_lo, iova->pfn_lo);
	}
	spin_unlock_irqrestore(&from->iova_rbtree_lock, flags);
}
EXPORT_SYMBOL_GPL(copy_reserved_iova);

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
		iova_insert_rbtree(&iovad->rbroot, prev, NULL);
		iova->pfn_lo = pfn_lo;
	}
	if (next) {
		iova_insert_rbtree(&iovad->rbroot, next, NULL);
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

/*
 * Magazine caches for IOVA ranges.  For an introduction to magazines,
 * see the USENIX 2001 paper "Magazines and Vmem: Extending the Slab
 * Allocator to Many CPUs and Arbitrary Resources" by Bonwick and Adams.
 * For simplicity, we use a static magazine size and don't implement the
 * dynamic size tuning described in the paper.
 */

#define IOVA_MAG_SIZE 128

struct iova_magazine {
	unsigned long size;
	unsigned long pfns[IOVA_MAG_SIZE];
};

struct iova_cpu_rcache {
	spinlock_t lock;
	struct iova_magazine *loaded;
	struct iova_magazine *prev;
};

static struct iova_magazine *iova_magazine_alloc(gfp_t flags)
{
	return kzalloc(sizeof(struct iova_magazine), flags);
}

static void iova_magazine_free(struct iova_magazine *mag)
{
	kfree(mag);
}

static void
iova_magazine_free_pfns(struct iova_magazine *mag, struct iova_domain *iovad)
{
	unsigned long flags;
	int i;

	if (!mag)
		return;

	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);

	for (i = 0 ; i < mag->size; ++i) {
		struct iova *iova = private_find_iova(iovad, mag->pfns[i]);

		BUG_ON(!iova);
		private_free_iova(iovad, iova);
	}

	spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);

	mag->size = 0;
}

static bool iova_magazine_full(struct iova_magazine *mag)
{
	return (mag && mag->size == IOVA_MAG_SIZE);
}

static bool iova_magazine_empty(struct iova_magazine *mag)
{
	return (!mag || mag->size == 0);
}

static unsigned long iova_magazine_pop(struct iova_magazine *mag,
				       unsigned long limit_pfn)
{
	BUG_ON(iova_magazine_empty(mag));

	if (mag->pfns[mag->size - 1] >= limit_pfn)
		return 0;

	return mag->pfns[--mag->size];
}

static void iova_magazine_push(struct iova_magazine *mag, unsigned long pfn)
{
	BUG_ON(iova_magazine_full(mag));

	mag->pfns[mag->size++] = pfn;
}

static void init_iova_rcaches(struct iova_domain *iovad)
{
	struct iova_cpu_rcache *cpu_rcache;
	struct iova_rcache *rcache;
	unsigned int cpu;
	int i;

	for (i = 0; i < IOVA_RANGE_CACHE_MAX_SIZE; ++i) {
		rcache = &iovad->rcaches[i];
		spin_lock_init(&rcache->lock);
		rcache->depot_size = 0;
		rcache->cpu_rcaches = __alloc_percpu(sizeof(*cpu_rcache), cache_line_size());
		if (WARN_ON(!rcache->cpu_rcaches))
			continue;
		for_each_possible_cpu(cpu) {
			cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
			spin_lock_init(&cpu_rcache->lock);
			cpu_rcache->loaded = iova_magazine_alloc(GFP_KERNEL);
			cpu_rcache->prev = iova_magazine_alloc(GFP_KERNEL);
		}
	}
}

/*
 * Try inserting IOVA range starting with 'iova_pfn' into 'rcache', and
 * return true on success.  Can fail if rcache is full and we can't free
 * space, and free_iova() (our only caller) will then return the IOVA
 * range to the rbtree instead.
 */
static bool __iova_rcache_insert(struct iova_domain *iovad,
				 struct iova_rcache *rcache,
				 unsigned long iova_pfn)
{
	struct iova_magazine *mag_to_free = NULL;
	struct iova_cpu_rcache *cpu_rcache;
	bool can_insert = false;
	unsigned long flags;

	cpu_rcache = get_cpu_ptr(rcache->cpu_rcaches);
	spin_lock_irqsave(&cpu_rcache->lock, flags);

	if (!iova_magazine_full(cpu_rcache->loaded)) {
		can_insert = true;
	} else if (!iova_magazine_full(cpu_rcache->prev)) {
		swap(cpu_rcache->prev, cpu_rcache->loaded);
		can_insert = true;
	} else {
		struct iova_magazine *new_mag = iova_magazine_alloc(GFP_ATOMIC);

		if (new_mag) {
			spin_lock(&rcache->lock);
			if (rcache->depot_size < MAX_GLOBAL_MAGS) {
				rcache->depot[rcache->depot_size++] =
						cpu_rcache->loaded;
			} else {
				mag_to_free = cpu_rcache->loaded;
			}
			spin_unlock(&rcache->lock);

			cpu_rcache->loaded = new_mag;
			can_insert = true;
		}
	}

	if (can_insert)
		iova_magazine_push(cpu_rcache->loaded, iova_pfn);

	spin_unlock_irqrestore(&cpu_rcache->lock, flags);
	put_cpu_ptr(rcache->cpu_rcaches);

	if (mag_to_free) {
		iova_magazine_free_pfns(mag_to_free, iovad);
		iova_magazine_free(mag_to_free);
	}

	return can_insert;
}

static bool iova_rcache_insert(struct iova_domain *iovad, unsigned long pfn,
			       unsigned long size)
{
	unsigned int log_size = order_base_2(size);

	if (log_size >= IOVA_RANGE_CACHE_MAX_SIZE)
		return false;

	return __iova_rcache_insert(iovad, &iovad->rcaches[log_size], pfn);
}

/*
 * Caller wants to allocate a new IOVA range from 'rcache'.  If we can
 * satisfy the request, return a matching non-NULL range and remove
 * it from the 'rcache'.
 */
static unsigned long __iova_rcache_get(struct iova_rcache *rcache,
				       unsigned long limit_pfn)
{
	struct iova_cpu_rcache *cpu_rcache;
	unsigned long iova_pfn = 0;
	bool has_pfn = false;
	unsigned long flags;

	cpu_rcache = get_cpu_ptr(rcache->cpu_rcaches);
	spin_lock_irqsave(&cpu_rcache->lock, flags);

	if (!iova_magazine_empty(cpu_rcache->loaded)) {
		has_pfn = true;
	} else if (!iova_magazine_empty(cpu_rcache->prev)) {
		swap(cpu_rcache->prev, cpu_rcache->loaded);
		has_pfn = true;
	} else {
		spin_lock(&rcache->lock);
		if (rcache->depot_size > 0) {
			iova_magazine_free(cpu_rcache->loaded);
			cpu_rcache->loaded = rcache->depot[--rcache->depot_size];
			has_pfn = true;
		}
		spin_unlock(&rcache->lock);
	}

	if (has_pfn)
		iova_pfn = iova_magazine_pop(cpu_rcache->loaded, limit_pfn);

	spin_unlock_irqrestore(&cpu_rcache->lock, flags);
	put_cpu_ptr(rcache->cpu_rcaches);

	return iova_pfn;
}

/*
 * Try to satisfy IOVA allocation range from rcache.  Fail if requested
 * size is too big or the DMA limit we are given isn't satisfied by the
 * top element in the magazine.
 */
static unsigned long iova_rcache_get(struct iova_domain *iovad,
				     unsigned long size,
				     unsigned long limit_pfn)
{
	unsigned int log_size = order_base_2(size);

	if (log_size >= IOVA_RANGE_CACHE_MAX_SIZE)
		return 0;

	return __iova_rcache_get(&iovad->rcaches[log_size], limit_pfn);
}

/*
 * Free a cpu's rcache.
 */
static void free_cpu_iova_rcache(unsigned int cpu, struct iova_domain *iovad,
				 struct iova_rcache *rcache)
{
	struct iova_cpu_rcache *cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
	unsigned long flags;

	spin_lock_irqsave(&cpu_rcache->lock, flags);

	iova_magazine_free_pfns(cpu_rcache->loaded, iovad);
	iova_magazine_free(cpu_rcache->loaded);

	iova_magazine_free_pfns(cpu_rcache->prev, iovad);
	iova_magazine_free(cpu_rcache->prev);

	spin_unlock_irqrestore(&cpu_rcache->lock, flags);
}

/*
 * free rcache data structures.
 */
static void free_iova_rcaches(struct iova_domain *iovad)
{
	struct iova_rcache *rcache;
	unsigned long flags;
	unsigned int cpu;
	int i, j;

	for (i = 0; i < IOVA_RANGE_CACHE_MAX_SIZE; ++i) {
		rcache = &iovad->rcaches[i];
		for_each_possible_cpu(cpu)
			free_cpu_iova_rcache(cpu, iovad, rcache);
		spin_lock_irqsave(&rcache->lock, flags);
		free_percpu(rcache->cpu_rcaches);
		for (j = 0; j < rcache->depot_size; ++j) {
			iova_magazine_free_pfns(rcache->depot[j], iovad);
			iova_magazine_free(rcache->depot[j]);
		}
		spin_unlock_irqrestore(&rcache->lock, flags);
	}
}

/*
 * free all the IOVA ranges cached by a cpu (used when cpu is unplugged)
 */
void free_cpu_cached_iovas(unsigned int cpu, struct iova_domain *iovad)
{
	struct iova_cpu_rcache *cpu_rcache;
	struct iova_rcache *rcache;
	unsigned long flags;
	int i;

	for (i = 0; i < IOVA_RANGE_CACHE_MAX_SIZE; ++i) {
		rcache = &iovad->rcaches[i];
		cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
		spin_lock_irqsave(&cpu_rcache->lock, flags);
		iova_magazine_free_pfns(cpu_rcache->loaded, iovad);
		iova_magazine_free_pfns(cpu_rcache->prev, iovad);
		spin_unlock_irqrestore(&cpu_rcache->lock, flags);
	}
}

MODULE_AUTHOR("Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>");
MODULE_LICENSE("GPL");
