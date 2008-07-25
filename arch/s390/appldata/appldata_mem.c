/*
 * arch/s390/appldata/appldata_mem.c
 *
 * Data gathering module for Linux-VM Monitor Stream, Stage 1.
 * Collects data related to memory management.
 *
 * Copyright (C) 2003,2006 IBM Corporation, IBM Deutschland Entwicklung GmbH.
 *
 * Author: Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <asm/io.h>

#include "appldata.h"


#define P2K(x) ((x) << (PAGE_SHIFT - 10))	/* Converts #Pages to KB */

/*
 * Memory data
 *
 * This is accessed as binary data by z/VM. If changes to it can't be avoided,
 * the structure version (product ID, see appldata_base.c) needs to be changed
 * as well and all documentation and z/VM applications using it must be
 * updated.
 *
 * The record layout is documented in the Linux for zSeries Device Drivers
 * book:
 * http://oss.software.ibm.com/developerworks/opensource/linux390/index.shtml
 */
static struct appldata_mem_data {
	u64 timestamp;
	u32 sync_count_1;       /* after VM collected the record data, */
	u32 sync_count_2;	/* sync_count_1 and sync_count_2 should be the
				   same. If not, the record has been updated on
				   the Linux side while VM was collecting the
				   (possibly corrupt) data */

	u64 pgpgin;		/* data read from disk  */
	u64 pgpgout;		/* data written to disk */
	u64 pswpin;		/* pages swapped in  */
	u64 pswpout;		/* pages swapped out */

	u64 sharedram;		/* sharedram is currently set to 0 */

	u64 totalram;		/* total main memory size */
	u64 freeram;		/* free main memory size  */
	u64 totalhigh;		/* total high memory size */
	u64 freehigh;		/* free high memory size  */

	u64 bufferram;		/* memory reserved for buffers, free cache */
	u64 cached;		/* size of (used) cache, w/o buffers */
	u64 totalswap;		/* total swap space size */
	u64 freeswap;		/* free swap space */

// New in 2.6 -->
	u64 pgalloc;		/* page allocations */
	u64 pgfault;		/* page faults (major+minor) */
	u64 pgmajfault;		/* page faults (major only) */
// <-- New in 2.6

} __attribute__((packed)) appldata_mem_data;


/*
 * appldata_get_mem_data()
 *
 * gather memory data
 */
static void appldata_get_mem_data(void *data)
{
	/*
	 * don't put large structures on the stack, we are
	 * serialized through the appldata_ops_lock and can use static
	 */
	static struct sysinfo val;
	unsigned long ev[NR_VM_EVENT_ITEMS];
	struct appldata_mem_data *mem_data;

	mem_data = data;
	mem_data->sync_count_1++;

	all_vm_events(ev);
	mem_data->pgpgin     = ev[PGPGIN] >> 1;
	mem_data->pgpgout    = ev[PGPGOUT] >> 1;
	mem_data->pswpin     = ev[PSWPIN];
	mem_data->pswpout    = ev[PSWPOUT];
	mem_data->pgalloc    = ev[PGALLOC_NORMAL];
#ifdef CONFIG_ZONE_DMA
	mem_data->pgalloc    += ev[PGALLOC_DMA];
#endif
	mem_data->pgfault    = ev[PGFAULT];
	mem_data->pgmajfault = ev[PGMAJFAULT];

	si_meminfo(&val);
	mem_data->sharedram = val.sharedram;
	mem_data->totalram  = P2K(val.totalram);
	mem_data->freeram   = P2K(val.freeram);
	mem_data->totalhigh = P2K(val.totalhigh);
	mem_data->freehigh  = P2K(val.freehigh);
	mem_data->bufferram = P2K(val.bufferram);
	mem_data->cached    = P2K(global_page_state(NR_FILE_PAGES)
				- val.bufferram);

	si_swapinfo(&val);
	mem_data->totalswap = P2K(val.totalswap);
	mem_data->freeswap  = P2K(val.freeswap);

	mem_data->timestamp = get_clock();
	mem_data->sync_count_2++;
}


static struct appldata_ops ops = {
	.name      = "mem",
	.record_nr = APPLDATA_RECORD_MEM_ID,
	.size	   = sizeof(struct appldata_mem_data),
	.callback  = &appldata_get_mem_data,
	.data      = &appldata_mem_data,
	.owner     = THIS_MODULE,
	.mod_lvl   = {0xF0, 0xF0},		/* EBCDIC "00" */
};


/*
 * appldata_mem_init()
 *
 * init_data, register ops
 */
static int __init appldata_mem_init(void)
{
	return appldata_register_ops(&ops);
}

/*
 * appldata_mem_exit()
 *
 * unregister ops
 */
static void __exit appldata_mem_exit(void)
{
	appldata_unregister_ops(&ops);
}


module_init(appldata_mem_init);
module_exit(appldata_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gerald Schaefer");
MODULE_DESCRIPTION("Linux-VM Monitor Stream, MEMORY statistics");
