/*
 * MobiCore driver module.(interface to the secure world SWD)
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_MEM_H_
#define _MC_MEM_H_

#define FREE_FROM_SWD	1
#define FREE_FROM_NWD	0

#define LOCKED_BY_APP	(1U << 0)
#define LOCKED_BY_MC	(1U << 1)

/*
 * MobiCore specific page tables for world shared memory.
 * Linux uses shadow page tables, see arch/arm/include/asm/pgtable-2level.
 * MobiCore uses the default ARM format.
 *
 * Number of page table entries in one L2 table. This is ARM specific, an
 * L2 table covers 1 MiB by using 256 entry referring to 4KiB pages each.
 */
#define MC_ARM_L2_TABLE_ENTRIES		256

/* ARM level 2 (L2) table with 256 entries. Size: 1k */
struct l2table {
	pte_t	table_entries[MC_ARM_L2_TABLE_ENTRIES];
};

/* Number of pages for L2 tables. There are 4 table in each page. */
#define L2_TABLES_PER_PAGE		4

/* Store for four L2 tables in one 4kb page*/
struct mc_l2_table_store {
	struct l2table table[L2_TABLES_PER_PAGE];
};

/* Usage and maintenance information about mc_l2_table_store */
struct mc_l2_tables_set {
	struct list_head		list;
	/* kernel virtual address */
	struct mc_l2_table_store	*kernel_virt;
	/* physical address */
	struct mc_l2_table_store	*phys;
	/* pointer to page struct */
	struct page			*page;
	/* How many pages from this set are used */
	atomic_t			used_tables;
};

/*
 * L2 table allocated to the Daemon or a TLC describing a world shared buffer.
 * When users map a malloc()ed area into SWd, a L2 table is allocated.
 * In addition, the area of maximum 1MB virtual address space is mapped into
 * the L2 table and a handle for this table is returned to the user.
 */
struct mc_l2_table {
	struct list_head	list;
	/* Table lock */
	struct mutex		lock;
	/* handle as communicated to user mode */
	unsigned int		handle;
	/* Number of references kept to this l2 table */
	atomic_t		usage;
	/* owner of this L2 table */
	struct mc_instance	*owner;
	/* set describing where our L2 table is stored */
	struct mc_l2_tables_set	*set;
	/* index into L2 table set */
	unsigned int		idx;
	/* size of buffer */
	unsigned int		pages;
	/* virtual address*/
	void			*virt;
	unsigned long		phys;
};

/* MobiCore Driver Memory context data. */
struct mc_mem_context {
	struct mc_instance	*daemon_inst;
	/* Backing store for L2 tables */
	struct list_head	l2_tables_sets;
	/* Bookkeeping for used L2 tables */
	struct list_head	l2_tables;
	/* Bookkeeping for free L2 tables */
	struct list_head	free_l2_tables;
	/* semaphore to synchronize access to above lists */
	struct mutex		table_lock;
};

/*
 * Allocate L2 table and map buffer into it.
 * That is, create respective table entries.
 */
struct mc_l2_table *mc_alloc_l2_table(struct mc_instance *instance,
	struct task_struct *task, void *wsm_buffer, unsigned int wsm_len);

/* Delete all the l2 tables associated with an instance */
void mc_clear_l2_tables(struct mc_instance *instance);

/* Release all orphaned L2 tables */
void mc_clean_l2_tables(void);

/* Delete a used l2 table. */
int mc_free_l2_table(struct mc_instance *instance, uint32_t handle);

/*
 * Lock a l2 table - the daemon adds +1 to refcount of the L2 table
 * marking it in use by SWD so it doesn't get released when the TLC dies.
 */
int mc_lock_l2_table(struct mc_instance *instance, uint32_t handle);
/* Unlock l2 table. */
int mc_unlock_l2_table(struct mc_instance *instance, uint32_t handle);
/* Return the phys address of l2 table. */
uint32_t mc_find_l2_table(uint32_t handle, int32_t fd);
/* Release all used l2 tables to Linux memory space */
void mc_release_l2_tables(void);

/* Initialize all l2 tables structure */
int mc_init_l2_tables(void);

#endif /* _MC_MEM_H_ */
