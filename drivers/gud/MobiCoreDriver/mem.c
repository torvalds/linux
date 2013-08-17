/*
 * MobiCore Driver Kernel Module.
 *
 * This module is written as a Linux device driver.
 * This driver represents the command proxy on the lowest layer, from the
 * secure world to the non secure world, and vice versa.
 * This driver is located in the non secure world (Linux).
 * This driver offers IOCTL commands, for access to the secure world, and has
 * the interface from the secure world to the normal world.
 * The access to the driver is possible with a file descriptor,
 * which has to be created by the fd = open(/dev/mobicore) command.
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "main.h"
#include "debug.h"
#include "mem.h"

#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/device.h>


/* MobiCore memory context data */
struct mc_mem_context mem_ctx;

/* convert L2 PTE to page pointer */
static inline struct page *l2_pte_to_page(pte_t pte)
{
	unsigned long phys_page_addr = ((unsigned long)pte & PAGE_MASK);
	unsigned int pfn = phys_page_addr >> PAGE_SHIFT;
	struct page *page = pfn_to_page(pfn);
	return page;
}

/* convert page pointer to L2 PTE */
static inline pte_t page_to_l2_pte(struct page *page)
{
	unsigned long pfn = page_to_pfn(page);
	unsigned long phys_addr = (pfn << PAGE_SHIFT);
	pte_t pte = (pte_t)(phys_addr & PAGE_MASK);
	return pte;
}

static inline void release_page(struct page *page)
{
	SetPageDirty(page);

	page_cache_release(page);
}

static int lock_pages(struct task_struct *task, void *virt_start_page_addr,
	int pages_no, struct page **pages)
{
	int locked_pages;

	/* lock user pages, must hold the mmap_sem to do this. */
	down_read(&(task->mm->mmap_sem));
	locked_pages = get_user_pages(
				task,
				task->mm,
				(unsigned long)virt_start_page_addr,
				pages_no,
				1, /* write access */
				0,
				pages,
				NULL);
	up_read(&(task->mm->mmap_sem));

	/* check if we could lock all pages. */
	if (locked_pages != pages_no) {
		MCDRV_DBG_ERROR(mcd, "get_user_pages() failed, locked_pages=%d",
				locked_pages);
		if (locked_pages > 0) {
			/* release all locked pages. */
			release_pages(pages, locked_pages, 0);
		}
		return -ENOMEM;
	}

	return 0;
}

/* Get kernel pointer to shared L2 table given a per-process reference */
struct l2table *get_l2_table_kernel_virt(struct mc_l2_table *table)
{
	if (WARN(!table, "Invalid L2 table"))
		return NULL;

	if (WARN(!table->set, "Invalid L2 table set"))
		return NULL;

	if (WARN(!table->set->kernel_virt, "Invalid L2 pointer"))
		return NULL;

	return &(table->set->kernel_virt->table[table->idx]);
}

/* Get physical address of a shared L2 table given a per-process reference */
struct l2table *get_l2_table_phys(struct mc_l2_table *table)
{
	if (WARN(!table, "Invalid L2 table"))
		return NULL;
	if (WARN(!table->set, "Invalid L2 table set"))
		return NULL;
	if (WARN(!table->set->kernel_virt, "Invalid L2 phys pointer"))
		return NULL;

	return &(table->set->phys->table[table->idx]);
}

static inline int in_use(struct mc_l2_table *table)
{
	return atomic_read(&table->usage) > 0;
}

/*
 * Search the list of used l2 tables and return the one with the handle.
 * Assumes the table_lock is taken.
 */
struct mc_l2_table *find_l2_table(unsigned int handle)
{
	struct mc_l2_table *table;

	list_for_each_entry(table, &mem_ctx.l2_tables, list) {
		if (table->handle == handle)
			return table;
	}
	return NULL;
}

/*
 * Allocate a new l2 table store plus L2_TABLES_PER_PAGE in the l2 free tables
 * list. Assumes the table_lock is already taken by the caller above.
 */
static int alloc_table_store(void)
{
	unsigned long store;
	struct mc_l2_tables_set *l2table_set;
	struct mc_l2_table *l2table, *l2table2;
	struct page *page;
	int ret = 0, i;
	/* temp list for holding the l2 tables */
	LIST_HEAD(temp);

	store = get_zeroed_page(GFP_KERNEL);
	if (!store)
		return -ENOMEM;

	/*
	 * Actually, locking is not necessary, because kernel
	 * memory is not supposed to get swapped out. But we
	 * play safe....
	 */
	page = virt_to_page(store);
	SetPageReserved(page);

	/* add all the descriptors to the free descriptors list */
	l2table_set = kmalloc(sizeof(*l2table_set), GFP_KERNEL | __GFP_ZERO);
	if (l2table_set == NULL) {
		ret = -ENOMEM;
		goto free_store;
	}
	/* initialize */
	l2table_set->kernel_virt = (void *)store;
	l2table_set->page = page;
	l2table_set->phys = (void *)virt_to_phys((void *)store);
	/* the set is not yet used */
	atomic_set(&l2table_set->used_tables, 0);

	/* init add to list. */
	INIT_LIST_HEAD(&(l2table_set->list));
	list_add(&l2table_set->list, &mem_ctx.l2_tables_sets);

	for (i = 0; i < L2_TABLES_PER_PAGE; i++) {
		/* allocate a WSM L2 descriptor */
		l2table  = kmalloc(sizeof(*l2table), GFP_KERNEL | __GFP_ZERO);
		if (l2table == NULL) {
			ret = -ENOMEM;
			MCDRV_DBG_ERROR(mcd, "out of memory\n");
			/* Free the full temp list and the store in this case */
			goto free_temp_list;
		}

		/* set set reference */
		l2table->set = l2table_set;
		l2table->idx = i;
		l2table->virt = get_l2_table_kernel_virt(l2table);
		l2table->phys = (unsigned long)get_l2_table_phys(l2table);
		atomic_set(&l2table->usage, 0);

		/* add to temp list. */
		INIT_LIST_HEAD(&l2table->list);
		list_add_tail(&l2table->list, &temp);
	}

	/*
	 * If everything went ok then merge the temp list with the global
	 * free list
	 */
	list_splice_tail(&temp, &mem_ctx.free_l2_tables);
	return 0;
free_temp_list:
	list_for_each_entry_safe(l2table, l2table2, &temp, list) {
		kfree(l2table);
	}

	list_del(&l2table_set->list);

free_store:
	free_page(store);
	return ret;

}
/*
 * Get a l2 table from the free tables list or allocate a new one and
 * initialize it. Assumes the table_lock is already taken.
 */
static struct mc_l2_table *alloc_l2_table(struct mc_instance *instance)
{
	int ret = 0;
	struct mc_l2_table *table = NULL;

	if (list_empty(&mem_ctx.free_l2_tables)) {
		ret = alloc_table_store();
		if (ret) {
			MCDRV_DBG_ERROR(mcd, "Failed to allocate new store!");
			return ERR_PTR(-ENOMEM);
		}
		/* if it's still empty something wrong has happened */
		if (list_empty(&mem_ctx.free_l2_tables)) {
			MCDRV_DBG_ERROR(mcd,
					"Free list not updated correctly!");
			return ERR_PTR(-EFAULT);
		}
	}

	/* get a WSM L2 descriptor */
	table  = list_first_entry(&mem_ctx.free_l2_tables,
		struct mc_l2_table, list);
	if (table == NULL) {
		MCDRV_DBG_ERROR(mcd, "out of memory\n");
		return ERR_PTR(-ENOMEM);
	}
	/* Move it to the used l2 tables list */
	list_move_tail(&table->list, &mem_ctx.l2_tables);

	table->handle = get_unique_id();
	table->owner = instance;

	atomic_inc(&table->set->used_tables);
	atomic_inc(&table->usage);

	MCDRV_DBG_VERBOSE(mcd,
			  "chunkPhys=%p,idx=%d", table->set->phys, table->idx);

	return table;
}

/*
 * Frees the object associated with a l2 table. Initially the object is moved
 * to the free tables list, but if all the 4 lists of the store are free
 * then the store is also released.
 * Assumes the table_lock is already taken.
 */
static void free_l2_table(struct mc_l2_table *table)
{
	struct mc_l2_tables_set *l2table_set;

	if (WARN(!table, "Invalid table"))
		return;

	l2table_set = table->set;
	if (WARN(!l2table_set, "Invalid table set"))
		return;

	list_move_tail(&table->list, &mem_ctx.free_l2_tables);

	/* if nobody uses this set, we can release it. */
	if (atomic_dec_and_test(&l2table_set->used_tables)) {
		struct mc_l2_table *tmp;

		/* remove from list */
		list_del(&l2table_set->list);
		/*
		 * All the l2 tables are in the free list for this set
		 * so we can just remove them from there
		 */
		list_for_each_entry_safe(table, tmp, &mem_ctx.free_l2_tables,
					 list) {
			if (table->set == l2table_set) {
				list_del(&table->list);
				kfree(table);
			}
		} /* end while */

		/*
		 * We shouldn't recover from this since it was some data
		 * corruption before
		 */
		BUG_ON(!l2table_set->page);
		ClearPageReserved(l2table_set->page);

		BUG_ON(!l2table_set->kernel_virt);
		free_page((unsigned long)l2table_set->kernel_virt);

		kfree(l2table_set);
	}
}

/*
 * Create a L2 table in a WSM container that has been allocates previously.
 * Assumes the table lock is already taken or there is no need to take like
 * when first creating the l2 table the full list is locked.
 *
 * @task	pointer to task owning WSM
 * @wsm_buffer	user space WSM start
 * @wsm_len	WSM length
 * @table	Pointer to L2 table details
 */
static int map_buffer(struct task_struct *task, void *wsm_buffer,
		      unsigned int wsm_len, struct mc_l2_table *table)
{
	int		ret = 0;
	unsigned int	i, nr_of_pages;
	/* start address of the 4 KiB page of wsm_buffer */
	void		*virt_addr_page;
	struct page	*page;
	struct l2table	*l2table;
	struct page	**l2table_as_array_of_pointers_to_page;
	/* page offset in wsm buffer */
	unsigned int offset;

	if (WARN(!wsm_buffer, "Invalid WSM buffer pointer"))
		return -EINVAL;

	if (WARN(wsm_len == 0, "Invalid WSM buffer length"))
		return -EINVAL;

	if (WARN(!table, "Invalid mapping table for WSM"))
		return -EINVAL;

	/* no size > 1Mib supported */
	if (wsm_len > SZ_1M) {
		MCDRV_DBG_ERROR(mcd, "size > 1 MiB\n");
		return -EINVAL;
	}

	MCDRV_DBG_VERBOSE(mcd, "WSM addr=0x%p, len=0x%08x\n", wsm_buffer,
			  wsm_len);


	/* calculate page usage */
	virt_addr_page = (void *)(((unsigned long)(wsm_buffer)) & PAGE_MASK);
	offset = (unsigned int)	(((unsigned long)(wsm_buffer)) & (~PAGE_MASK));
	nr_of_pages  = PAGE_ALIGN(offset + wsm_len) / PAGE_SIZE;

	MCDRV_DBG_VERBOSE(mcd, "virt addr page start=0x%p, pages=%d\n",
			  virt_addr_page, nr_of_pages);

	/* L2 table can hold max 1MiB in 256 pages. */
	if ((nr_of_pages * PAGE_SIZE) > SZ_1M) {
		MCDRV_DBG_ERROR(mcd, "WSM paged exceed 1 MiB\n");
		return -EINVAL;
	}

	l2table = table->virt;
	/*
	 * We use the memory for the L2 table to hold the pointer
	 * and convert them later. This works, as everything comes
	 * down to a 32 bit value.
	 */
	l2table_as_array_of_pointers_to_page = (struct page **)l2table;

	/* Request comes from user space */
	if (task != NULL && !is_vmalloc_addr(wsm_buffer)) {
		/*
		 * lock user page in memory, so they do not get swapped
		 * out.
		 * REV axh: Kernel 2.6.27 added a new get_user_pages_fast()
		 * function, maybe it is called fast_gup() in some versions.
		 * handle user process doing a fork().
		 * Child should not get things.
		 * http://osdir.com/ml/linux-media/2009-07/msg00813.html
		 * http://lwn.net/Articles/275808/
		 */
		ret = lock_pages(task, virt_addr_page, nr_of_pages,
				 l2table_as_array_of_pointers_to_page);
		if (ret != 0) {
			MCDRV_DBG_ERROR(mcd, "lock_user_pages() failed\n");
			return ret;
		}
	}
	/* Request comes from kernel space(cont buffer) */
	else if (task == NULL && !is_vmalloc_addr(wsm_buffer)) {
		void *uaddr = wsm_buffer;
		for (i = 0; i < nr_of_pages; i++) {
			page = virt_to_page(uaddr);
			if (!page) {
				MCDRV_DBG_ERROR(mcd, "failed to map address");
				return -EINVAL;
			}
			get_page(page);
			l2table_as_array_of_pointers_to_page[i] = page;
			uaddr += PAGE_SIZE;
		}
	}
	/* Request comes from kernel space(vmalloc buffer) */
	else {
		void *uaddr = wsm_buffer;
		for (i = 0; i < nr_of_pages; i++) {
			page = vmalloc_to_page(uaddr);
			if (!page) {
				MCDRV_DBG_ERROR(mcd, "failed to map address");
				return -EINVAL;
			}
			get_page(page);
			l2table_as_array_of_pointers_to_page[i] = page;
			uaddr += PAGE_SIZE;
		}
	}

	table->pages = nr_of_pages;

	/*
	 * create L2 Table entries.
	 * used_l2table->table contains a list of page pointers here.
	 * For a proper cleanup we have to ensure that the following
	 * code either works and used_l2table contains a valid L2 table
	 * - or fails and used_l2table->table contains the list of page
	 * pointers.
	 * Any mixed contents will make cleanup difficult.
	 */
	for (i = 0; i < nr_of_pages; i++) {
		pte_t pte;
		page = l2table_as_array_of_pointers_to_page[i];

		/*
		 * create L2 table entry, see ARM MMU docu for details
		 * about flags stored in the lowest 12 bits.
		 * As a side reference, the Article
		 * "ARM's multiply-mapped memory mess"
		 * found in the collection at
		 * http://lwn.net/Articles/409032/
		 * is also worth reading.
		 */
		pte = page_to_l2_pte(page)
				| PTE_EXT_AP1 | PTE_EXT_AP0
				| PTE_CACHEABLE | PTE_BUFFERABLE
				| PTE_TYPE_SMALL | PTE_TYPE_EXT | PTE_EXT_NG;
		/*
		 * Linux uses different mappings for SMP systems(the
		 * sharing flag is set for the pte. In order not to
		 * confuse things too much in Mobicore make sure the
		 * shared buffers have the same flags.
		 * This should also be done in SWD side
		 */
#ifdef CONFIG_SMP
		pte |= PTE_EXT_SHARED | PTE_EXT_TEX(1);
#endif

		l2table->table_entries[i] = pte;
		MCDRV_DBG_VERBOSE(mcd, "L2 entry %d:  0x%08x\n", i,
				  (unsigned int)(pte));
	}

	/* ensure rest of table is empty */
	while (i < 255)
		l2table->table_entries[i++] = (pte_t)0;


	return ret;
}

/*
 * Remove a L2 table in a WSM container. Afterwards the container may be
 * released. Assumes the table_lock and the lock is taken.
 */
static void unmap_buffers(struct mc_l2_table *table)
{
	struct l2table *l2table;
	int i;

	if (WARN_ON(!table))
		return;

	/* found the table, now release the resources. */
	MCDRV_DBG_VERBOSE(mcd, "clear L2 table, phys_base=%p, nr_of_pages=%d\n",
			  (void *)table->phys, table->pages);

	l2table = table->virt;

	/* release all locked user space pages */
	for (i = 0; i < table->pages; i++) {
		/* convert physical entries from L2 table to page pointers */
		pte_t pte = l2table->table_entries[i];
		struct page *page = l2_pte_to_page(pte);
		release_page(page);
	}

	/* remember that all pages have been freed */
	table->pages = 0;
}

/* Delete a used l2 table. Assumes the table_lock and the lock is taken */
static void unmap_l2_table(struct mc_l2_table *table)
{
	/* Check if it's not locked by other processes too! */
	if (!atomic_dec_and_test(&table->usage))
		return;

	/* release if Nwd and Swd/MC do no longer use it. */
	unmap_buffers(table);
	free_l2_table(table);
}

int mc_free_l2_table(struct mc_instance *instance, uint32_t handle)
{
	struct mc_l2_table *table;
	int ret = 0;

	if (WARN(!instance, "No instance data available"))
		return -EFAULT;

	mutex_lock(&mem_ctx.table_lock);
	table = find_l2_table(handle);

	if (table == NULL) {
		MCDRV_DBG_VERBOSE(mcd, "entry not found");
		ret = -EINVAL;
		goto err_unlock;
	}
	if (instance != table->owner && !is_daemon(instance)) {
		MCDRV_DBG_ERROR(mcd, "instance does no own it");
		ret = -EPERM;
		goto err_unlock;
	}
	/* free table (if no further locks exist) */
	unmap_l2_table(table);
err_unlock:
	mutex_unlock(&mem_ctx.table_lock);

	return ret;
}

int mc_lock_l2_table(struct mc_instance *instance, uint32_t handle)
{
	int ret = 0;
	struct mc_l2_table *table = NULL;

	if (WARN(!instance, "No instance data available"))
		return -EFAULT;

	mutex_lock(&mem_ctx.table_lock);
	table = find_l2_table(handle);

	if (table == NULL) {
		MCDRV_DBG_VERBOSE(mcd, "entry not found %u\n", handle);
		ret = -EINVAL;
		goto table_err;
	}
	if (instance != table->owner && !is_daemon(instance)) {
		MCDRV_DBG_ERROR(mcd, "instance does no own it\n");
		ret = -EPERM;
		goto table_err;
	}

	/* lock entry */
	atomic_inc(&table->usage);
table_err:
	mutex_unlock(&mem_ctx.table_lock);
	return ret;
}
/*
 * Allocate L2 table and map buffer into it.
 * That is, create respective table entries.
 * Must hold Semaphore mem_ctx.wsm_l2_sem
 */
struct mc_l2_table *mc_alloc_l2_table(struct mc_instance *instance,
	struct task_struct *task, void *wsm_buffer, unsigned int wsm_len)
{
	int ret = 0;
	struct mc_l2_table *table;

	if (WARN(!instance, "No instance data available"))
		return ERR_PTR(-EFAULT);

	mutex_lock(&mem_ctx.table_lock);
	table = alloc_l2_table(instance);
	if (IS_ERR(table)) {
		MCDRV_DBG_ERROR(mcd, "allocate_used_l2_table() failed\n");
		ret = -ENOMEM;
		goto err_no_mem;
	}

	/* create the L2 page for the WSM */
	ret = map_buffer(task, wsm_buffer, wsm_len, table);

	if (ret != 0) {
		MCDRV_DBG_ERROR(mcd, "map_buffer() failed\n");
		unmap_l2_table(table);
		goto err_no_mem;
	}
	MCDRV_DBG(mcd, "mapped buffer %p to table with handle %d @ %lx",
		  wsm_buffer, table->handle, table->phys);

	mutex_unlock(&mem_ctx.table_lock);
	return table;
err_no_mem:
	mutex_unlock(&mem_ctx.table_lock);
	return ERR_PTR(ret);
}

uint32_t mc_find_l2_table(uint32_t handle, int32_t fd)
{
	uint32_t ret = 0;
	struct mc_l2_table *table = NULL;

	mutex_lock(&mem_ctx.table_lock);
	table = find_l2_table(handle);

	if (table == NULL) {
		MCDRV_DBG_ERROR(mcd, "entry not found %u\n", handle);
		ret = 0;
		goto table_err;
	}

	/*
	 * It's safe here not to lock the instance since the owner of
	 * the table will be cleared only with the table lock taken
	 */
	if (!mc_check_owner_fd(table->owner, fd)) {
		MCDRV_DBG_ERROR(mcd, "not valid owner%u\n", handle);
		ret = 0;
		goto table_err;
	}

	ret = table->phys;
table_err:
	mutex_unlock(&mem_ctx.table_lock);
	return ret;
}

void mc_clean_l2_tables(void)
{
	struct mc_l2_table *table, *tmp;

	mutex_lock(&mem_ctx.table_lock);
	/* Check if some WSM is orphaned. */
	list_for_each_entry_safe(table, tmp, &mem_ctx.l2_tables, list) {
		if (table->owner == NULL) {
			MCDRV_DBG(mcd,
				  "clearing orphaned WSM L2: p=%lx pages=%d\n",
				  table->phys, table->pages);
			unmap_l2_table(table);
		}
	}
	mutex_unlock(&mem_ctx.table_lock);
}

void mc_clear_l2_tables(struct mc_instance *instance)
{
	struct mc_l2_table *table, *tmp;

	mutex_lock(&mem_ctx.table_lock);
	/* Check if some WSM is still in use. */
	list_for_each_entry_safe(table, tmp, &mem_ctx.l2_tables, list) {
		if (table->owner == instance) {
			MCDRV_DBG(mcd, "release WSM L2: p=%lx pages=%d\n",
				  table->phys, table->pages);
			/* unlock app usage and free or mark it as orphan */
			table->owner = NULL;
			unmap_l2_table(table);
		}
	}
	mutex_unlock(&mem_ctx.table_lock);
}

int mc_init_l2_tables(void)
{
	/* init list for WSM L2 chunks. */
	INIT_LIST_HEAD(&mem_ctx.l2_tables_sets);

	/* L2 table descriptor list. */
	INIT_LIST_HEAD(&mem_ctx.l2_tables);

	/* L2 table descriptor list. */
	INIT_LIST_HEAD(&mem_ctx.free_l2_tables);

	mutex_init(&mem_ctx.table_lock);

	return 0;
}

void mc_release_l2_tables()
{
	struct mc_l2_table *table;
	/* Check if some WSM is still in use. */
	list_for_each_entry(table, &mem_ctx.l2_tables, list) {
		WARN(1, "WSM L2 still in use: phys=%lx ,nr_of_pages=%d\n",
		     table->phys, table->pages);
	}
}
