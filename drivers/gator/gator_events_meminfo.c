/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"

#include <linux/hardirq.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <trace/events/kmem.h>

enum {
	MEMINFO_MEMFREE,
	MEMINFO_MEMUSED,
	MEMINFO_BUFFERRAM,
	MEMINFO_TOTAL,
};

enum {
	PROC_SIZE,
	PROC_SHARE,
	PROC_TEXT,
	PROC_DATA,
	PROC_COUNT,
};

static const char * const meminfo_names[] = {
	"Linux_meminfo_memfree",
	"Linux_meminfo_memused",
	"Linux_meminfo_bufferram",
};

static const char * const proc_names[] = {
	"Linux_proc_statm_size",
	"Linux_proc_statm_share",
	"Linux_proc_statm_text",
	"Linux_proc_statm_data",
};

static bool meminfo_global_enabled;
static ulong meminfo_enabled[MEMINFO_TOTAL];
static ulong meminfo_keys[MEMINFO_TOTAL];
static long long meminfo_buffer[2 * (MEMINFO_TOTAL + 2)];
static int meminfo_length = 0;
static bool new_data_avail;

static bool proc_global_enabled;
static ulong proc_enabled[PROC_COUNT];
static ulong proc_keys[PROC_COUNT];
static DEFINE_PER_CPU(long long, proc_buffer[2 * (PROC_COUNT + 3)]);

static int gator_meminfo_func(void *data);
static bool gator_meminfo_run;
// Initialize semaphore unlocked to initialize memory values
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
static DECLARE_MUTEX(gator_meminfo_sem);
#else
static DEFINE_SEMAPHORE(gator_meminfo_sem);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
GATOR_DEFINE_PROBE(mm_page_free_direct, TP_PROTO(struct page *page, unsigned int order))
#else
GATOR_DEFINE_PROBE(mm_page_free, TP_PROTO(struct page *page, unsigned int order))
#endif
{
	up(&gator_meminfo_sem);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
GATOR_DEFINE_PROBE(mm_pagevec_free, TP_PROTO(struct page *page, int cold))
#else
GATOR_DEFINE_PROBE(mm_page_free_batched, TP_PROTO(struct page *page, int cold))
#endif
{
	up(&gator_meminfo_sem);
}

GATOR_DEFINE_PROBE(mm_page_alloc, TP_PROTO(struct page *page, unsigned int order, gfp_t gfp_flags, int migratetype))
{
	up(&gator_meminfo_sem);
}

static int gator_events_meminfo_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int i;

	for (i = 0; i < MEMINFO_TOTAL; i++) {
		dir = gatorfs_mkdir(sb, root, meminfo_names[i]);
		if (!dir) {
			return -1;
		}
		gatorfs_create_ulong(sb, dir, "enabled", &meminfo_enabled[i]);
		gatorfs_create_ro_ulong(sb, dir, "key", &meminfo_keys[i]);
	}

	for (i = 0; i < PROC_COUNT; ++i) {
		dir = gatorfs_mkdir(sb, root, proc_names[i]);
		if (!dir) {
			return -1;
		}
		gatorfs_create_ulong(sb, dir, "enabled", &proc_enabled[i]);
		gatorfs_create_ro_ulong(sb, dir, "key", &proc_keys[i]);
	}

	return 0;
}

static int gator_events_meminfo_start(void)
{
	int i;

	new_data_avail = false;
	meminfo_global_enabled = 0;
	for (i = 0; i < MEMINFO_TOTAL; i++) {
		if (meminfo_enabled[i]) {
			meminfo_global_enabled = 1;
			break;
		}
	}

	proc_global_enabled = 0;
	for (i = 0; i < PROC_COUNT; ++i) {
		if (proc_enabled[i]) {
			proc_global_enabled = 1;
			break;
		}
	}
	if (meminfo_enabled[MEMINFO_MEMUSED]) {
		proc_global_enabled = 1;
	}

	if (meminfo_global_enabled == 0)
		return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	if (GATOR_REGISTER_TRACE(mm_page_free_direct))
#else
	if (GATOR_REGISTER_TRACE(mm_page_free))
#endif
		goto mm_page_free_exit;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	if (GATOR_REGISTER_TRACE(mm_pagevec_free))
#else
	if (GATOR_REGISTER_TRACE(mm_page_free_batched))
#endif
		goto mm_page_free_batched_exit;
	if (GATOR_REGISTER_TRACE(mm_page_alloc))
		goto mm_page_alloc_exit;

	// Start worker thread
	gator_meminfo_run = true;
	// Since the mutex starts unlocked, memory values will be initialized
	if (IS_ERR(kthread_run(gator_meminfo_func, NULL, "gator_meminfo")))
		goto kthread_run_exit;

	return 0;

kthread_run_exit:
	GATOR_UNREGISTER_TRACE(mm_page_alloc);
mm_page_alloc_exit:
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	GATOR_UNREGISTER_TRACE(mm_pagevec_free);
#else
	GATOR_UNREGISTER_TRACE(mm_page_free_batched);
#endif
mm_page_free_batched_exit:
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	GATOR_UNREGISTER_TRACE(mm_page_free_direct);
#else
	GATOR_UNREGISTER_TRACE(mm_page_free);
#endif
mm_page_free_exit:
	return -1;
}

static void gator_events_meminfo_stop(void)
{
	if (meminfo_global_enabled) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
		GATOR_UNREGISTER_TRACE(mm_page_free_direct);
		GATOR_UNREGISTER_TRACE(mm_pagevec_free);
#else
		GATOR_UNREGISTER_TRACE(mm_page_free);
		GATOR_UNREGISTER_TRACE(mm_page_free_batched);
#endif
		GATOR_UNREGISTER_TRACE(mm_page_alloc);

		// Stop worker thread
		gator_meminfo_run = false;
		up(&gator_meminfo_sem);
	}
}

// Must be run in process context as the kernel function si_meminfo() can sleep
static int gator_meminfo_func(void *data)
{
	struct sysinfo info;
	int i, len;
	unsigned long long value;

	for (;;) {
		if (down_killable(&gator_meminfo_sem)) {
			break;
		}

		// Eat up any pending events
		while (!down_trylock(&gator_meminfo_sem));

		if (!gator_meminfo_run) {
			break;
		}

		meminfo_length = len = 0;

		si_meminfo(&info);
		for (i = 0; i < MEMINFO_TOTAL; i++) {
			if (meminfo_enabled[i]) {
				switch (i) {
				case MEMINFO_MEMFREE:
					value = info.freeram * PAGE_SIZE;
					break;
				case MEMINFO_MEMUSED:
					// pid -1 means system wide
					meminfo_buffer[len++] = 1;
					meminfo_buffer[len++] = -1;
					// Emit value
					meminfo_buffer[len++] = meminfo_keys[MEMINFO_MEMUSED];
					meminfo_buffer[len++] = (info.totalram - info.freeram) * PAGE_SIZE;
					// Clear pid
					meminfo_buffer[len++] = 1;
					meminfo_buffer[len++] = 0;
					continue;
				case MEMINFO_BUFFERRAM:
					value = info.bufferram * PAGE_SIZE;
					break;
				default:
					value = 0;
					break;
				}
				meminfo_buffer[len++] = meminfo_keys[i];
				meminfo_buffer[len++] = value;
			}
		}

		meminfo_length = len;
		new_data_avail = true;
	}

	return 0;
}

static int gator_events_meminfo_read(long long **buffer)
{
	if (!on_primary_core() || !meminfo_global_enabled)
		return 0;

	if (!new_data_avail)
		return 0;

	new_data_avail = false;

	if (buffer)
		*buffer = meminfo_buffer;

	return meminfo_length;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34) && LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)

static inline unsigned long gator_get_mm_counter(struct mm_struct *mm, int member)
{
#ifdef SPLIT_RSS_COUNTING
	long val = atomic_long_read(&mm->rss_stat.count[member]);
	if (val < 0)
		val = 0;
	return (unsigned long)val;
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
	return mm->rss_stat.count[member];
#else
	return atomic_long_read(&mm->rss_stat.count[member]);
#endif
#endif
}

#define get_mm_counter(mm, member) gator_get_mm_counter(mm, member)

#endif

static int gator_events_meminfo_read_proc(long long **buffer, struct task_struct *task)
{
	struct mm_struct *mm;
	u64 share = 0;
	int i;
	long long value;
	int len = 0;
	int cpu = get_physical_cpu();
	long long *buf = per_cpu(proc_buffer, cpu);

	if (!proc_global_enabled) {
		return 0;
	}

	// Collect the memory stats of the process instead of the thread
	if (task->group_leader != NULL) {
		task = task->group_leader;
	}

	// get_task_mm/mmput is not needed in this context because the task and it's mm are required as part of the sched_switch
	mm = task->mm;
	if (mm == NULL) {
		return 0;
	}

	// Derived from task_statm in fs/proc/task_mmu.c
	if (meminfo_enabled[MEMINFO_MEMUSED] || proc_enabled[PROC_SHARE]) {
		share = get_mm_counter(mm,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
							   file_rss
#else
							   MM_FILEPAGES
#endif
							   );
	}

	// key of 1 indicates a pid
	buf[len++] = 1;
	buf[len++] = task->pid;

	for (i = 0; i < PROC_COUNT; ++i) {
		if (proc_enabled[i]) {
			switch (i) {
			case PROC_SIZE:
				value = mm->total_vm;
				break;
			case PROC_SHARE:
				value = share;
				break;
			case PROC_TEXT:
				value = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK)) >> PAGE_SHIFT;
				break;
			case PROC_DATA:
				value = mm->total_vm - mm->shared_vm;
				break;
			}

			buf[len++] = proc_keys[i];
			buf[len++] = value * PAGE_SIZE;
		}
	}

	if (meminfo_enabled[MEMINFO_MEMUSED]) {
		value = share + get_mm_counter(mm,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
									   anon_rss
#else
									   MM_ANONPAGES
#endif
									   );
		// Send resident for this pid
		buf[len++] = meminfo_keys[MEMINFO_MEMUSED];
		buf[len++] = value * PAGE_SIZE;
	}

	// Clear pid
	buf[len++] = 1;
	buf[len++] = 0;

	if (buffer)
		*buffer = buf;

	return len;
}

static struct gator_interface gator_events_meminfo_interface = {
	.create_files = gator_events_meminfo_create_files,
	.start = gator_events_meminfo_start,
	.stop = gator_events_meminfo_stop,
	.read64 = gator_events_meminfo_read,
	.read_proc = gator_events_meminfo_read_proc,
};

int gator_events_meminfo_init(void)
{
	int i;

	meminfo_global_enabled = 0;
	for (i = 0; i < MEMINFO_TOTAL; i++) {
		meminfo_enabled[i] = 0;
		meminfo_keys[i] = gator_events_get_key();
	}

	proc_global_enabled = 0;
	for (i = 0; i < PROC_COUNT; ++i) {
		proc_enabled[i] = 0;
		proc_keys[i] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_meminfo_interface);
}
