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
#include <linux/cpu.h>

/* The anchor node sits above the top of the usable address space */
#define IOVA_ANCHOR	~0UL

static bool iova_rcache_insert(struct iova_domain *iovad,
			       unsigned long pfn,
			       unsigned long size);
static unsigned long iova_rcache_get(struct iova_domain *iovad,
				     unsigned long size,
				     unsigned long limit_pfn);
static void init_iova_rcaches(struct iova_domain *iovad);
static void free_iova_rcaches(struct iova_domain *iovad);
static void fq_destroy_all_entries(struct iova_domain *iovad);
static void fq_flush_timeout(struct timer_list *t);

void
init_iova_domain(struct iova_domain *iovad, unsigned long granule,
	unsigned long start_pfn)
{
	/*
	 * IOVA granularity will normally be equal to the smallest
	 * supported IOMMU page size; both *must* be capable of
	 * representing individual CPU pages exactly.
	 */
	BUG_ON((granule > PAGE_SIZE) || !is_power_of_2(granule));

	spin_lock_init(&iovad->iova_rbtree_lock);
	iovad->rbroot = RB_ROOT;
	iovad->cached_node = &iovad->anchor.node;
	iovad->cached32_node = &iovad->anchor.node;
	iovad->granule = granule;
	iovad->start_pfn = start_pfn;
	iovad->dma_32bit_pfn = 1UL << (32 - iova_shift(iovad));
	iovad->flush_cb = NULL;
	iovad->fq = NULL;
	iovad->anchor.pfn_lo = iovad->anchor.pfn_hi = IOVA_ANCHOR;
	rb_link_node(&iovad->anchor.node, NULL, &iovad->rbroot.rb_node);
	rb_insert_color(&iovad->anchor.node, &iovad->rbroot);
	init_iova_rcaches(iovad);
}
EXPORT_SYMBOL_GPL(init_iova_domain);

bool has_iova_flush_queue(struct iova_domain *iovad)
{
	return !!iovad->fq;
}

static void free_iova_flush_queue(struct iova_domain *iovad)
{
	if (!has_iova_flush_queue(iovad))
		return;

	if (timer_pending(&iovad->fq_timer))
		del_timer(&iovad->fq_timer);

	fq_destroy_all_entries(iovad);

	free_percpu(iovad->fq);

	iovad->fq         = NULL;
	iovad->flush_cb   = NULL;
	iovad->entry_dtor = NULL;
}

int init_iova_flush_queue(struct iova_domain *iovad,
			  iova_flush_cb flush_cb, iova_entry_dtor entry_dtor)
{
	struct iova_fq __percpu *queue;
	int cpu;

	atomic64_set(&iovad->fq_flush_start_cnt,  0);
	atomic64_set(&iovad->fq_flush_finish_cnt, 0);

	queue = alloc_percpu(struct iova_fq);
	if (!queue)
		return -ENOMEM;

	iovad->flush_cb   = flush_cb;
	iovad->entry_dtor = entry_dtor;

	for_each_possible_cpu(cpu) {
		struct iova_fq *fq;

		fq = per_cpu_ptr(queue, cpu);
		fq->head = 0;
		fq->tail = 0;

		spin_lock_init(&fq->lock);
	}

	smp_wmb();

	iovad->fq = queue;

	timer_setup(&iovad->fq_timer, fq_flush_timeout, 0);
	atomic_set(&iovad->fq_timer_on, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(init_iova_flush_queue);

static struct rb_node *
__get_cached_rbnode(struct iova_domain *iovad, unsigned long limit_pfn)
{
	if (limit_pfn <= iovad->dma_32bit_pfn)
		return iovad->cached32_node;

	return iovad->cached_node;
}

static void
__cached_rbnode_insert_update(struct iova_domain *iovad, struct iova *new)
{
	if (new->pfn_hi < iovad->dma_32bit_pfn)
		iovad->cached32_node = &new->node;
	else
		iovad->cached_node = &new->node;
}

static void
__cached_rbnode_delete_update(struct iova_domain *iovad, struct iova *free)
{
	struct iova *cached_iova;

	cached_iova = rb_entry(iovad->cached32_node, struct iova, node);
	if (free == cached_iova ||
	    (free->pfn_hi < iovad->dma_32bit_pfn &&
	     free->pfn_lo >= cached_iova->pfn_lo))
		iovad->cached32_node = rb_next(&free->node);

	cached_iova = rb_entry(iovad->cached_node, struct iova, node);
	if (free->pfn_lo >= cached_iova->pfn_lo)
		iovad->cached_node = rb_next(&free->node);
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

static int __alloc_and_insert_iova_range(struct iova_domain *iovad,
		unsigned long size, unsigned long limit_pfn,
			struct iova *new, bool size_aligned)
{
	struct rb_node *curr, *prev;
	struct iova *curr_iova;
	unsigned long flags;
	unsigned long new_pfn;
	unsigned long align_mask = ~0UL;

	if (size_aligned)
		align_mask <<= fls_long(size - 1);

	/* Walk the tree backwards */
	spin_lock_irqsave(&iovad->iova_rbtree_lock, flags);
	curr = __get_cached_rbnode(iovad, limit_pfn);
	curr_iova = rb_entry(curr, struct iova, node);
	do {
		limit_pfn = min(limit_pfn, curr_iova->pfn_lo);
		new_pfn = (limit_pfn - size) & align_mask;
		prev = curr;
		curr = rb_prev(curr);
		curr_iova = rb_entry(curr, struct iova, node);
	} while (curr && new_pfn <= curr_iova->pfn_hi);

	if (limit_pfn < size || new_pfn < iovad->start_pfn) {
		spin_unlock_irqrestore(&iovad->iova_rbtree_lock, flags);
		return -ENOMEM;
	}

	/* pfn_lo will point to size aligned address if size_aligned is set */
	new->pfn_lo = new_pfn;
	new->pfn_hi = new->pfn_lo + size - 1;

	/* If we have 'prev', it's a valid place to start the insertion. */
	iova_insert_rbtree(&iovad->rbroot, new, prev);
	__cached_rbnode_insert_update(iovad, new);

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
	if (iova->pfn_lo != IOVA_ANCHOR)
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

	ret = __alloc_and_insert_iova_range(iovad, size, limit_pfn + 1,
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

		if (pfn < iova->pfn_lo)
			node = node->rb_left;
		else if (pfn > iova->pfn_hi)
			node = node->rb_right;
		else
			return iova;	/* pfn falls within iova's range */
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
 * @flush_rcache: - set to flush rcache on regular allocation failure
 * This function tries to satisfy an iova allocation from the rcache,
 * and falls back to regular allocation on failure. If regular allocation
 * fails too and the flush_rcache flag is set then the rcache will be flushed.
*/
unsigned long
alloc_iova_fast(struct iova_domain *iovad, unsigned long size,
		unsigned long limit_pfn, bool flush_rcache)
{
	unsigned long iova_pfn;
	struct iova *new_iova;

	iova_pfn = iova_rcache_get(iovad, size, limit_pfn + 1);
	if (iova_pfn)
		return iova_pfn;

retry:
	new_iova = alloc_iova(iovad, size, limit_pfn, true);
	if (!new_iova) {
		unsigned int cpu;

		if (!flush_rcache)
			return 0;

		/* Try replenishing IOVAs by flushing rcache. */
		flush_rcache = false;
		for_each_online_cpu(cpu)
			free_cpu_cached_iovas(cpu, iovad);
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

#define fq_ring_for_each(i, fq) \
	for ((i) = (fq)->head; (i) != (fq)->tail; (i) = ((i) + 1) % IOVA_FQ_SIZE)

static inline bool fq_full(struct iova_fq *fq)
{
	assert_spin_locked(&fq->lock);
	return (((fq->tail + 1) % IOVA_FQ_SIZE) == fq->head);
}

static inline unsigned fq_ring_add(struct iova_fq *fq)
{
	unsigned idx = fq->tail;

	assert_spin_locked(&fq->lock);

	fq->tail = (idx + 1) % IOVA_FQ_SIZE;

	return idx;
}

static void fq_ring_free(struct iova_domain *iovad, struct iova_fq *fq)
{
	u64 counter = atomic64_read(&iovad->fq_flush_finish_cnt);
	unsigned idx;

	assert_spin_locked(&fq->lock);

	fq_ring_for_each(idx, fq) {

		if (fq->entries[idx].counter >= counter)
			break;

		if (iovad->entry_dtor)
			iovad->entry_dtor(fq->entries[idx].data);

		free_iova_fast(iovad,
			       fq->entries[idx].iova_pfn,
			       fq->entries[idx].pages);

		fq->head = (fq->head + 1) % IOVA_FQ_SIZE;
	}
}

static void iova_domain_flush(struct iova_domain *iovad)
{
	atomic64_inc(&iovad->fq_flush_start_cnt);
	iovad->flush_cb(iovad);
	atomic64_inc(&iovad->fq_flush_finish_cnt);
}

static void fq_destroy_all_entries(struct iova_domain *iovad)
{
	int cpu;

	/*
	 * This code runs when the iova_domain is being detroyed, so don't
	 * bother to free iovas, just call the entry_dtor on all remaining
	 * entries.
	 */
	if (!iovad->entry_dtor)
		return;

	for_each_possible_cpu(cpu) {
		struct iova_fq *fq = per_cpu_ptr(iovad->fq, cpu);
		int idx;

		fq_ring_for_each(idx, fq)
			iovad->entry_dtor(fq->entries[idx].data);
	}
}

static void fq_flush_timeout(struct timer_list *t)
{
	struct iova_domain *iovad = from_timer(iovad, t, fq_timer);
	int cpu;

	atomic_set(&iovad->fq_timer_on, 0);
	iova_domain_flush(iovad);

	for_each_possible_cpu(cpu) {
		unsigned long flags;
		struct iova_fq *fq;

		fq = per_cpu_ptr(iovad->fq, cpu);
		spin_lock_irqsave(&fq->lock, flags);
		fq_ring_free(iovad, fq);
		spin_unlock_irqrestore(&fq->lock, flags);
	}
}

void queue_iova(struct iova_domain *iovad,
		unsigned long pfn, unsigned long pages,
		unsigned long data)
{
	struct iova_fq *fq = raw_cpu_ptr(iovad->fq);
	unsigned long flags;
	unsigned idx;

	spin_lock_irqsave(&fq->lock, flags);

	/*
	 * First remove all entries from the flush queue that have already been
	 * flushed out on another CPU. This makes the fq_full() check below less
	 * likely to be true.
	 */
	fq_ring_free(iovad, fq);

	if (fq_full(fq)) {
		iova_domain_flush(iovad);
		fq_ring_free(iovad, fq);
	}

	idx = fq_ring_add(fq);

	fq->entries[idx].iova_pfn = pfn;
	fq->entries[idx].pages    = pages;
	fq->entries[idx].data     = data;
	fq->entries[idx].counter  = atomic64_read(&iovad->fq_flush_start_cnt);

	spin_unlock_irqrestore(&fq->lock, flags);

	/* Avoid false sharing as much as possible. */
	if (!atomic_read(&iovad->fq_timer_on) &&
	    !atomic_cmpxchg(&iovad->fq_timer_on, 0, 1))
		mod_timer(&iovad->fq_timer,
			  jiffies + msecs_to_jiffies(IOVA_FQ_TIMEOUT));
}
EXPORT_SYMBOL_GPL(queue_iova);

/**
 * put_iova_domain - destroys the iova doamin
 * @iovad: - iova domain in question.
 * All the iova's in that domain are destroyed.
 */
void put_iova_domain(struct iova_domain *iovad)
{
	struct iova *iova, *tmp;

	free_iova_flush_queue(iovad);
	free_iova_rcaches(iovad);
	rbtree_postorder_for_each_entry_safe(iova, tmp, &iovad->rbroot, node)
		free_iova_mem(iova);
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

	/* Don't allow nonsensical pfns */
	if (WARN_ON((pfn_hi | pfn_lo) > (ULLONG_MAX >> iova_shift(iovad))))
		return NULL;

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

		if (iova->pfn_lo == IOVA_ANCHOR)
			continue;

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
	int i;
	unsigned long pfn;

	BUG_ON(iova_magazine_empty(mag));

	/* Only fall back to the rbtree if we have no suitable pfns at all */
	for (i = mag->size - 1; mag->pfns[i] > limit_pfn; i--)
		if (i == 0)
			return 0;

	/* Swap it to pop it */
	pfn = mag->pfns[i];
	mag->pfns[i] = mag->pfns[--mag->size];

	return pfn;
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

	cpu_rcache = raw_cpu_ptr(rcache->cpu_rcaches);
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

	cpu_rcache = raw_cpu_ptr(rcache->cpu_rcaches);
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

	return __iova_rcache_get(&iovad->rcaches[log_size], limit_pfn - size);
}

/*
 * free rcache data structures.
 */
static void free_iova_rcaches(struct iova_domain *iovad)
{
	struct iova_rcache *rcache;
	struct iova_cpu_rcache *cpu_rcache;
	unsigned int cpu;
	int i, j;

	for (i = 0; i < IOVA_RANGE_CACHE_MAX_SIZE; ++i) {
		rcache = &iovad->rcaches[i];
		for_each_possible_cpu(cpu) {
			cpu_rcache = per_cpu_ptr(rcache->cpu_rcaches, cpu);
			iova_magazine_free(cpu_rcache->loaded);
			iova_magazine_free(cpu_rcache->prev);
		}
		free_percpu(rcache->cpu_rcaches);
		for (j = 0; j < rcache->depot_size; ++j)
			iova_magazine_free(rcache->depot[j]);
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
