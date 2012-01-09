/*
 * zsmalloc memory allocator
 *
 * Copyright (C) 2011  Nitin Gupta
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the license that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 */

#ifdef CONFIG_ZSMALLOC_DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>

#include "zsmalloc.h"
#include "zsmalloc_int.h"

/*
 * A zspage's class index and fullness group
 * are encoded in its (first)page->mapping
 */
#define CLASS_IDX_BITS	28
#define FULLNESS_BITS	4
#define CLASS_IDX_MASK	((1 << CLASS_IDX_BITS) - 1)
#define FULLNESS_MASK	((1 << FULLNESS_BITS) - 1)

/*
 * Object location (<PFN>, <obj_idx>) is encoded as
 * as single (void *) handle value.
 *
 * Note that object index <obj_idx> is relative to system
 * page <PFN> it is stored in, so for each sub-page belonging
 * to a zspage, obj_idx starts with 0.
 */
#define _PFN_BITS		(MAX_PHYSMEM_BITS - PAGE_SHIFT)
#define OBJ_INDEX_BITS	(BITS_PER_LONG - _PFN_BITS)
#define OBJ_INDEX_MASK	((_AC(1, UL) << OBJ_INDEX_BITS) - 1)

/* per-cpu VM mapping areas for zspage accesses that cross page boundaries */
static DEFINE_PER_CPU(struct mapping_area, zs_map_area);

static int is_first_page(struct page *page)
{
	return test_bit(PG_private, &page->flags);
}

static int is_last_page(struct page *page)
{
	return test_bit(PG_private_2, &page->flags);
}

static void get_zspage_mapping(struct page *page, unsigned int *class_idx,
				enum fullness_group *fullness)
{
	unsigned long m;
	BUG_ON(!is_first_page(page));

	m = (unsigned long)page->mapping;
	*fullness = m & FULLNESS_MASK;
	*class_idx = (m >> FULLNESS_BITS) & CLASS_IDX_MASK;
}

static void set_zspage_mapping(struct page *page, unsigned int class_idx,
				enum fullness_group fullness)
{
	unsigned long m;
	BUG_ON(!is_first_page(page));

	m = ((class_idx & CLASS_IDX_MASK) << FULLNESS_BITS) |
			(fullness & FULLNESS_MASK);
	page->mapping = (struct address_space *)m;
}

static int get_size_class_index(int size)
{
	int idx = 0;

	if (likely(size > ZS_MIN_ALLOC_SIZE))
		idx = DIV_ROUND_UP(size - ZS_MIN_ALLOC_SIZE,
				ZS_SIZE_CLASS_DELTA);

	return idx;
}

static enum fullness_group get_fullness_group(struct page *page)
{
	int inuse, max_objects;
	enum fullness_group fg;
	BUG_ON(!is_first_page(page));

	inuse = page->inuse;
	max_objects = page->objects;

	if (inuse == 0)
		fg = ZS_EMPTY;
	else if (inuse == max_objects)
		fg = ZS_FULL;
	else if (inuse <= max_objects / fullness_threshold_frac)
		fg = ZS_ALMOST_EMPTY;
	else
		fg = ZS_ALMOST_FULL;

	return fg;
}

static void insert_zspage(struct page *page, struct size_class *class,
				enum fullness_group fullness)
{
	struct page **head;

	BUG_ON(!is_first_page(page));

	if (fullness >= _ZS_NR_FULLNESS_GROUPS)
		return;

	head = &class->fullness_list[fullness];
	if (*head)
		list_add_tail(&page->lru, &(*head)->lru);

	*head = page;
}

static void remove_zspage(struct page *page, struct size_class *class,
				enum fullness_group fullness)
{
	struct page **head;

	BUG_ON(!is_first_page(page));

	if (fullness >= _ZS_NR_FULLNESS_GROUPS)
		return;

	head = &class->fullness_list[fullness];
	BUG_ON(!*head);
	if (list_empty(&(*head)->lru))
		*head = NULL;
	else if (*head == page)
		*head = (struct page *)list_entry((*head)->lru.next,
					struct page, lru);

	list_del_init(&page->lru);
}

static enum fullness_group fix_fullness_group(struct zs_pool *pool,
						struct page *page)
{
	int class_idx;
	struct size_class *class;
	enum fullness_group currfg, newfg;

	BUG_ON(!is_first_page(page));

	get_zspage_mapping(page, &class_idx, &currfg);
	newfg = get_fullness_group(page);
	if (newfg == currfg)
		goto out;

	class = &pool->size_class[class_idx];
	remove_zspage(page, class, currfg);
	insert_zspage(page, class, newfg);
	set_zspage_mapping(page, class_idx, newfg);

out:
	return newfg;
}

/*
 * We have to decide on how many pages to link together
 * to form a zspage for each size class. This is important
 * to reduce wastage due to unusable space left at end of
 * each zspage which is given as:
 *	wastage = Zp - Zp % size_class
 * where Zp = zspage size = k * PAGE_SIZE where k = 1, 2, ...
 *
 * For example, for size class of 3/8 * PAGE_SIZE, we should
 * link together 3 PAGE_SIZE sized pages to form a zspage
 * since then we can perfectly fit in 8 such objects.
 */
static int get_zspage_order(int class_size)
{
	int i, max_usedpc = 0;
	/* zspage order which gives maximum used size per KB */
	int max_usedpc_order = 1;

	for (i = 1; i <= max_zspage_order; i++) {
		int zspage_size;
		int waste, usedpc;

		zspage_size = i * PAGE_SIZE;
		waste = zspage_size % class_size;
		usedpc = (zspage_size - waste) * 100 / zspage_size;

		if (usedpc > max_usedpc) {
			max_usedpc = usedpc;
			max_usedpc_order = i;
		}
	}

	return max_usedpc_order;
}

/*
 * A single 'zspage' is composed of many system pages which are
 * linked together using fields in struct page. This function finds
 * the first/head page, given any component page of a zspage.
 */
static struct page *get_first_page(struct page *page)
{
	if (is_first_page(page))
		return page;
	else
		return page->first_page;
}

static struct page *get_next_page(struct page *page)
{
	struct page *next;

	if (is_last_page(page))
		next = NULL;
	else if (is_first_page(page))
		next = (struct page *)page->private;
	else
		next = list_entry(page->lru.next, struct page, lru);

	return next;
}

/* Encode <page, obj_idx> as a single handle value */
static void *obj_location_to_handle(struct page *page, unsigned long obj_idx)
{
	unsigned long handle;

	if (!page) {
		BUG_ON(obj_idx);
		return NULL;
	}

	handle = page_to_pfn(page) << OBJ_INDEX_BITS;
	handle |= (obj_idx & OBJ_INDEX_MASK);

	return (void *)handle;
}

/* Decode <page, obj_idx> pair from the given object handle */
static void obj_handle_to_location(void *handle, struct page **page,
				unsigned long *obj_idx)
{
	unsigned long hval = (unsigned long)handle;

	*page = pfn_to_page(hval >> OBJ_INDEX_BITS);
	*obj_idx = hval & OBJ_INDEX_MASK;
}

static unsigned long obj_idx_to_offset(struct page *page,
				unsigned long obj_idx, int class_size)
{
	unsigned long off = 0;

	if (!is_first_page(page))
		off = page->index;

	return off + obj_idx * class_size;
}

static void free_zspage(struct page *first_page)
{
	struct page *nextp, *tmp;

	BUG_ON(!is_first_page(first_page));
	BUG_ON(first_page->inuse);

	nextp = (struct page *)page_private(first_page);

	clear_bit(PG_private, &first_page->flags);
	clear_bit(PG_private_2, &first_page->flags);
	set_page_private(first_page, 0);
	first_page->mapping = NULL;
	first_page->freelist = NULL;
	reset_page_mapcount(first_page);
	__free_page(first_page);

	/* zspage with only 1 system page */
	if (!nextp)
		return;

	list_for_each_entry_safe(nextp, tmp, &nextp->lru, lru) {
		list_del(&nextp->lru);
		clear_bit(PG_private_2, &nextp->flags);
		nextp->index = 0;
		__free_page(nextp);
	}
}

/* Initialize a newly allocated zspage */
static void init_zspage(struct page *first_page, struct size_class *class)
{
	unsigned long off = 0;
	struct page *page = first_page;

	BUG_ON(!is_first_page(first_page));
	while (page) {
		struct page *next_page;
		struct link_free *link;
		unsigned int i, objs_on_page;

		/*
		 * page->index stores offset of first object starting
		 * in the page. For the first page, this is always 0,
		 * so we use first_page->index (aka ->freelist) to store
		 * head of corresponding zspage's freelist.
		 */
		if (page != first_page)
			page->index = off;

		link = (struct link_free *)kmap_atomic(page) +
						off / sizeof(*link);
		objs_on_page = (PAGE_SIZE - off) / class->size;

		for (i = 1; i <= objs_on_page; i++) {
			off += class->size;
			if (off < PAGE_SIZE) {
				link->next = obj_location_to_handle(page, i);
				link += class->size / sizeof(*link);
			}
		}

		/*
		 * We now come to the last (full or partial) object on this
		 * page, which must point to the first object on the next
		 * page (if present)
		 */
		next_page = get_next_page(page);
		link->next = obj_location_to_handle(next_page, 0);
		kunmap_atomic(link);
		page = next_page;
		off = (off + class->size) % PAGE_SIZE;
	}
}

/*
 * Allocate a zspage for the given size class
 */
static struct page *alloc_zspage(struct size_class *class, gfp_t flags)
{
	int i, error;
	struct page *first_page = NULL;

	/*
	 * Allocate individual pages and link them together as:
	 * 1. first page->private = first sub-page
	 * 2. all sub-pages are linked together using page->lru
	 * 3. each sub-page is linked to the first page using page->first_page
	 *
	 * For each size class, First/Head pages are linked together using
	 * page->lru. Also, we set PG_private to identify the first page
	 * (i.e. no other sub-page has this flag set) and PG_private_2 to
	 * identify the last page.
	 */
	error = -ENOMEM;
	for (i = 0; i < class->zspage_order; i++) {
		struct page *page, *prev_page;

		page = alloc_page(flags);
		if (!page)
			goto cleanup;

		INIT_LIST_HEAD(&page->lru);
		if (i == 0) {	/* first page */
			set_bit(PG_private, &page->flags);
			set_page_private(page, 0);
			first_page = page;
			first_page->inuse = 0;
		}
		if (i == 1)
			first_page->private = (unsigned long)page;
		if (i >= 1)
			page->first_page = first_page;
		if (i >= 2)
			list_add(&page->lru, &prev_page->lru);
		if (i == class->zspage_order - 1)	/* last page */
			set_bit(PG_private_2, &page->flags);

		prev_page = page;
	}

	init_zspage(first_page, class);

	first_page->freelist = obj_location_to_handle(first_page, 0);
	/* Maximum number of objects we can store in this zspage */
	first_page->objects = class->zspage_order * PAGE_SIZE / class->size;

	error = 0; /* Success */

cleanup:
	if (unlikely(error) && first_page) {
		free_zspage(first_page);
		first_page = NULL;
	}

	return first_page;
}

static struct page *find_get_zspage(struct size_class *class)
{
	int i;
	struct page *page;

	for (i = 0; i < _ZS_NR_FULLNESS_GROUPS; i++) {
		page = class->fullness_list[i];
		if (page)
			break;
	}

	return page;
}


/*
 * If this becomes a separate module, register zs_init() with
 * module_init(), zs_exit with module_exit(), and remove zs_initialized
*/
static int zs_initialized;

static int zs_cpu_notifier(struct notifier_block *nb, unsigned long action,
				void *pcpu)
{
	int cpu = (long)pcpu;
	struct mapping_area *area;

	switch (action) {
	case CPU_UP_PREPARE:
		area = &per_cpu(zs_map_area, cpu);
		if (area->vm)
			break;
		area->vm = alloc_vm_area(2 * PAGE_SIZE, area->vm_ptes);
		if (!area->vm)
			return notifier_from_errno(-ENOMEM);
		break;
	case CPU_DEAD:
	case CPU_UP_CANCELED:
		area = &per_cpu(zs_map_area, cpu);
		if (area->vm)
			free_vm_area(area->vm);
		area->vm = NULL;
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block zs_cpu_nb = {
	.notifier_call = zs_cpu_notifier
};

static void zs_exit(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		zs_cpu_notifier(NULL, CPU_DEAD, (void *)(long)cpu);
	unregister_cpu_notifier(&zs_cpu_nb);
}

static int zs_init(void)
{
	int cpu, ret;

	register_cpu_notifier(&zs_cpu_nb);
	for_each_online_cpu(cpu) {
		ret = zs_cpu_notifier(NULL, CPU_UP_PREPARE, (void *)(long)cpu);
		if (notifier_to_errno(ret))
			goto fail;
	}
	return 0;
fail:
	zs_exit();
	return notifier_to_errno(ret);
}

struct zs_pool *zs_create_pool(const char *name, gfp_t flags)
{
	int i, error, ovhd_size;
	struct zs_pool *pool;

	if (!name)
		return NULL;

	ovhd_size = roundup(sizeof(*pool), PAGE_SIZE);
	pool = kzalloc(ovhd_size, GFP_KERNEL);
	if (!pool)
		return NULL;

	for (i = 0; i < ZS_SIZE_CLASSES; i++) {
		int size;
		struct size_class *class;

		size = ZS_MIN_ALLOC_SIZE + i * ZS_SIZE_CLASS_DELTA;
		if (size > ZS_MAX_ALLOC_SIZE)
			size = ZS_MAX_ALLOC_SIZE;

		class = &pool->size_class[i];
		class->size = size;
		class->index = i;
		spin_lock_init(&class->lock);
		class->zspage_order = get_zspage_order(size);

	}

	/*
	 * If this becomes a separate module, register zs_init with
	 * module_init, and remove this block
	*/
	if (!zs_initialized) {
		error = zs_init();
		if (error)
			goto cleanup;
		zs_initialized = 1;
	}

	pool->flags = flags;
	pool->name = name;

	error = 0; /* Success */

cleanup:
	if (error) {
		zs_destroy_pool(pool);
		pool = NULL;
	}

	return pool;
}
EXPORT_SYMBOL_GPL(zs_create_pool);

void zs_destroy_pool(struct zs_pool *pool)
{
	int i;

	for (i = 0; i < ZS_SIZE_CLASSES; i++) {
		int fg;
		struct size_class *class = &pool->size_class[i];

		for (fg = 0; fg < _ZS_NR_FULLNESS_GROUPS; fg++) {
			if (class->fullness_list[fg]) {
				pr_info("Freeing non-empty class with size "
					"%db, fullness group %d\n",
					class->size, fg);
			}
		}
	}
	kfree(pool);
}
EXPORT_SYMBOL_GPL(zs_destroy_pool);

/**
 * zs_malloc - Allocate block of given size from pool.
 * @pool: pool to allocate from
 * @size: size of block to allocate
 * @page: page no. that holds the object
 * @offset: location of object within page
 *
 * On success, <page, offset> identifies block allocated
 * and 0 is returned. On failure, <page, offset> is set to
 * 0 and -ENOMEM is returned.
 *
 * Allocation requests with size > ZS_MAX_ALLOC_SIZE will fail.
 */
void *zs_malloc(struct zs_pool *pool, size_t size)
{
	void *obj;
	struct link_free *link;
	int class_idx;
	struct size_class *class;

	struct page *first_page, *m_page;
	unsigned long m_objidx, m_offset;

	if (unlikely(!size || size > ZS_MAX_ALLOC_SIZE))
		return NULL;

	class_idx = get_size_class_index(size);
	class = &pool->size_class[class_idx];
	BUG_ON(class_idx != class->index);

	spin_lock(&class->lock);
	first_page = find_get_zspage(class);

	if (!first_page) {
		spin_unlock(&class->lock);
		first_page = alloc_zspage(class, pool->flags);
		if (unlikely(!first_page))
			return NULL;

		set_zspage_mapping(first_page, class->index, ZS_EMPTY);
		spin_lock(&class->lock);
		class->pages_allocated += class->zspage_order;
	}

	obj = first_page->freelist;
	obj_handle_to_location(obj, &m_page, &m_objidx);
	m_offset = obj_idx_to_offset(m_page, m_objidx, class->size);

	link = (struct link_free *)kmap_atomic(m_page) +
					m_offset / sizeof(*link);
	first_page->freelist = link->next;
	memset(link, POISON_INUSE, sizeof(*link));
	kunmap_atomic(link);

	first_page->inuse++;
	/* Now move the zspage to another fullness group, if required */
	fix_fullness_group(pool, first_page);
	spin_unlock(&class->lock);

	return obj;
}
EXPORT_SYMBOL_GPL(zs_malloc);

void zs_free(struct zs_pool *pool, void *obj)
{
	struct link_free *link;
	struct page *first_page, *f_page;
	unsigned long f_objidx, f_offset;

	int class_idx;
	struct size_class *class;
	enum fullness_group fullness;

	if (unlikely(!obj))
		return;

	obj_handle_to_location(obj, &f_page, &f_objidx);
	first_page = get_first_page(f_page);

	get_zspage_mapping(first_page, &class_idx, &fullness);
	class = &pool->size_class[class_idx];
	f_offset = obj_idx_to_offset(f_page, f_objidx, class->size);

	spin_lock(&class->lock);

	/* Insert this object in containing zspage's freelist */
	link = (struct link_free *)((unsigned char *)kmap_atomic(f_page)
							+ f_offset);
	link->next = first_page->freelist;
	kunmap_atomic(link);
	first_page->freelist = obj;

	first_page->inuse--;
	fullness = fix_fullness_group(pool, first_page);

	if (fullness == ZS_EMPTY)
		class->pages_allocated -= class->zspage_order;

	spin_unlock(&class->lock);

	if (fullness == ZS_EMPTY)
		free_zspage(first_page);
}
EXPORT_SYMBOL_GPL(zs_free);

void *zs_map_object(struct zs_pool *pool, void *handle)
{
	struct page *page;
	unsigned long obj_idx, off;

	unsigned int class_idx;
	enum fullness_group fg;
	struct size_class *class;
	struct mapping_area *area;

	BUG_ON(!handle);

	obj_handle_to_location(handle, &page, &obj_idx);
	get_zspage_mapping(get_first_page(page), &class_idx, &fg);
	class = &pool->size_class[class_idx];
	off = obj_idx_to_offset(page, obj_idx, class->size);

	area = &get_cpu_var(zs_map_area);
	if (off + class->size <= PAGE_SIZE) {
		/* this object is contained entirely within a page */
		area->vm_addr = kmap_atomic(page);
	} else {
		/* this object spans two pages */
		struct page *nextp;

		nextp = get_next_page(page);
		BUG_ON(!nextp);


		set_pte(area->vm_ptes[0], mk_pte(page, PAGE_KERNEL));
		set_pte(area->vm_ptes[1], mk_pte(nextp, PAGE_KERNEL));

		/* We pre-allocated VM area so mapping can never fail */
		area->vm_addr = area->vm->addr;
	}

	return area->vm_addr + off;
}
EXPORT_SYMBOL_GPL(zs_map_object);

void zs_unmap_object(struct zs_pool *pool, void *handle)
{
	struct page *page;
	unsigned long obj_idx, off;

	unsigned int class_idx;
	enum fullness_group fg;
	struct size_class *class;
	struct mapping_area *area;

	BUG_ON(!handle);

	obj_handle_to_location(handle, &page, &obj_idx);
	get_zspage_mapping(get_first_page(page), &class_idx, &fg);
	class = &pool->size_class[class_idx];
	off = obj_idx_to_offset(page, obj_idx, class->size);

	area = &__get_cpu_var(zs_map_area);
	if (off + class->size <= PAGE_SIZE) {
		kunmap_atomic(area->vm_addr);
	} else {
		set_pte(area->vm_ptes[0], __pte(0));
		set_pte(area->vm_ptes[1], __pte(0));
		__flush_tlb_one((unsigned long)area->vm_addr);
		__flush_tlb_one((unsigned long)area->vm_addr + PAGE_SIZE);
	}
	put_cpu_var(zs_map_area);
}
EXPORT_SYMBOL_GPL(zs_unmap_object);

u64 zs_get_total_size_bytes(struct zs_pool *pool)
{
	int i;
	u64 npages = 0;

	for (i = 0; i < ZS_SIZE_CLASSES; i++)
		npages += pool->size_class[i].pages_allocated;

	return npages << PAGE_SHIFT;
}
EXPORT_SYMBOL_GPL(zs_get_total_size_bytes);
