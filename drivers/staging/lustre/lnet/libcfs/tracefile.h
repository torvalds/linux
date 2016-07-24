/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LIBCFS_TRACEFILE_H__
#define __LIBCFS_TRACEFILE_H__

#include "../../include/linux/libcfs/libcfs.h"

enum cfs_trace_buf_type {
	CFS_TCD_TYPE_PROC = 0,
	CFS_TCD_TYPE_SOFTIRQ,
	CFS_TCD_TYPE_IRQ,
	CFS_TCD_TYPE_MAX
};

/* trace file lock routines */

#define TRACEFILE_NAME_SIZE 1024
extern char      cfs_tracefile[TRACEFILE_NAME_SIZE];
extern long long cfs_tracefile_size;

void libcfs_run_debug_log_upcall(char *file);

int  cfs_tracefile_init_arch(void);
void cfs_tracefile_fini_arch(void);

void cfs_tracefile_read_lock(void);
void cfs_tracefile_read_unlock(void);
void cfs_tracefile_write_lock(void);
void cfs_tracefile_write_unlock(void);

int cfs_tracefile_dump_all_pages(char *filename);
void cfs_trace_debug_print(void);
void cfs_trace_flush_pages(void);
int cfs_trace_start_thread(void);
void cfs_trace_stop_thread(void);
int cfs_tracefile_init(int max_pages);
void cfs_tracefile_exit(void);

int cfs_trace_copyin_string(char *knl_buffer, int knl_buffer_nob,
			    const char __user *usr_buffer, int usr_buffer_nob);
int cfs_trace_copyout_string(char __user *usr_buffer, int usr_buffer_nob,
			     const char *knl_str, char *append);
int cfs_trace_allocate_string_buffer(char **str, int nob);
int cfs_trace_dump_debug_buffer_usrstr(void __user *usr_str, int usr_str_nob);
int cfs_trace_daemon_command(char *str);
int cfs_trace_daemon_command_usrstr(void __user *usr_str, int usr_str_nob);
int cfs_trace_set_debug_mb(int mb);
int cfs_trace_get_debug_mb(void);

void libcfs_debug_dumplog_internal(void *arg);
void libcfs_register_panic_notifier(void);
void libcfs_unregister_panic_notifier(void);
extern int  libcfs_panic_in_progress;
int cfs_trace_max_debug_mb(void);

#define TCD_MAX_PAGES (5 << (20 - PAGE_SHIFT))
#define TCD_STOCK_PAGES (TCD_MAX_PAGES)
#define CFS_TRACEFILE_SIZE (500 << 20)

#ifdef LUSTRE_TRACEFILE_PRIVATE

/*
 * Private declare for tracefile
 */
#define TCD_MAX_PAGES (5 << (20 - PAGE_SHIFT))
#define TCD_STOCK_PAGES (TCD_MAX_PAGES)

#define CFS_TRACEFILE_SIZE (500 << 20)

/*
 * Size of a buffer for sprinting console messages if we can't get a page
 * from system
 */
#define CFS_TRACE_CONSOLE_BUFFER_SIZE   1024

union cfs_trace_data_union {
	struct cfs_trace_cpu_data {
		/*
		 * Even though this structure is meant to be per-CPU, locking
		 * is needed because in some places the data may be accessed
		 * from other CPUs. This lock is directly used in trace_get_tcd
		 * and trace_put_tcd, which are called in libcfs_debug_vmsg2 and
		 * tcd_for_each_type_lock
		 */
		spinlock_t		tcd_lock;
		unsigned long	   tcd_lock_flags;

		/*
		 * pages with trace records not yet processed by tracefiled.
		 */
		struct list_head	      tcd_pages;
		/* number of pages on ->tcd_pages */
		unsigned long	   tcd_cur_pages;

		/*
		 * pages with trace records already processed by
		 * tracefiled. These pages are kept in memory, so that some
		 * portion of log can be written in the event of LBUG. This
		 * list is maintained in LRU order.
		 *
		 * Pages are moved to ->tcd_daemon_pages by tracefiled()
		 * (put_pages_on_daemon_list()). LRU pages from this list are
		 * discarded when list grows too large.
		 */
		struct list_head	      tcd_daemon_pages;
		/* number of pages on ->tcd_daemon_pages */
		unsigned long	   tcd_cur_daemon_pages;

		/*
		 * Maximal number of pages allowed on ->tcd_pages and
		 * ->tcd_daemon_pages each.
		 * Always TCD_MAX_PAGES * tcd_pages_factor / 100 in current
		 * implementation.
		 */
		unsigned long	   tcd_max_pages;

		/*
		 * preallocated pages to write trace records into. Pages from
		 * ->tcd_stock_pages are moved to ->tcd_pages by
		 * portals_debug_msg().
		 *
		 * This list is necessary, because on some platforms it's
		 * impossible to perform efficient atomic page allocation in a
		 * non-blockable context.
		 *
		 * Such platforms fill ->tcd_stock_pages "on occasion", when
		 * tracing code is entered in blockable context.
		 *
		 * trace_get_tage_try() tries to get a page from
		 * ->tcd_stock_pages first and resorts to atomic page
		 * allocation only if this queue is empty. ->tcd_stock_pages
		 * is replenished when tracing code is entered in blocking
		 * context (darwin-tracefile.c:trace_get_tcd()). We try to
		 * maintain TCD_STOCK_PAGES (40 by default) pages in this
		 * queue. Atomic allocation is only required if more than
		 * TCD_STOCK_PAGES pagesful are consumed by trace records all
		 * emitted in non-blocking contexts. Which is quite unlikely.
		 */
		struct list_head	      tcd_stock_pages;
		/* number of pages on ->tcd_stock_pages */
		unsigned long	   tcd_cur_stock_pages;

		unsigned short	  tcd_shutting_down;
		unsigned short	  tcd_cpu;
		unsigned short	  tcd_type;
		/* The factors to share debug memory. */
		unsigned short	  tcd_pages_factor;
	} tcd;
	char __pad[L1_CACHE_ALIGN(sizeof(struct cfs_trace_cpu_data))];
};

#define TCD_MAX_TYPES      8
extern union cfs_trace_data_union (*cfs_trace_data[TCD_MAX_TYPES])[NR_CPUS];

#define cfs_tcd_for_each(tcd, i, j)				       \
	for (i = 0; cfs_trace_data[i]; i++)				\
		for (j = 0, ((tcd) = &(*cfs_trace_data[i])[j].tcd);	\
		     j < num_possible_cpus();				 \
		     j++, (tcd) = &(*cfs_trace_data[i])[j].tcd)

#define cfs_tcd_for_each_type_lock(tcd, i, cpu)			   \
	for (i = 0; cfs_trace_data[i] &&				\
	     (tcd = &(*cfs_trace_data[i])[cpu].tcd) &&			\
	     cfs_trace_lock_tcd(tcd, 1); cfs_trace_unlock_tcd(tcd, 1), i++)

void cfs_set_ptldebug_header(struct ptldebug_header *header,
			     struct libcfs_debug_msg_data *m,
			     unsigned long stack);
void cfs_print_to_console(struct ptldebug_header *hdr, int mask,
			  const char *buf, int len, const char *file,
			  const char *fn);

int cfs_trace_lock_tcd(struct cfs_trace_cpu_data *tcd, int walking);
void cfs_trace_unlock_tcd(struct cfs_trace_cpu_data *tcd, int walking);

extern char *cfs_trace_console_buffers[NR_CPUS][CFS_TCD_TYPE_MAX];
enum cfs_trace_buf_type cfs_trace_buf_idx_get(void);

static inline char *
cfs_trace_get_console_buffer(void)
{
	unsigned int i = get_cpu();
	unsigned int j = cfs_trace_buf_idx_get();

	return cfs_trace_console_buffers[i][j];
}

static inline struct cfs_trace_cpu_data *
cfs_trace_get_tcd(void)
{
	struct cfs_trace_cpu_data *tcd =
		&(*cfs_trace_data[cfs_trace_buf_idx_get()])[get_cpu()].tcd;

	cfs_trace_lock_tcd(tcd, 0);

	return tcd;
}

static inline void cfs_trace_put_tcd(struct cfs_trace_cpu_data *tcd)
{
	cfs_trace_unlock_tcd(tcd, 0);

	put_cpu();
}

int cfs_trace_refill_stock(struct cfs_trace_cpu_data *tcd, gfp_t gfp,
			   struct list_head *stock);

void cfs_trace_assertion_failed(const char *str,
				struct libcfs_debug_msg_data *m);

/* ASSERTION that is safe to use within the debug system */
#define __LASSERT(cond)						 \
do {								    \
	if (unlikely(!(cond))) {					\
		LIBCFS_DEBUG_MSG_DATA_DECL(msgdata, D_EMERG, NULL);     \
		cfs_trace_assertion_failed("ASSERTION("#cond") failed", \
					   &msgdata);		   \
	}							       \
} while (0)

#define __LASSERT_TAGE_INVARIANT(tage)				  \
do {								    \
	__LASSERT(tage);					\
	__LASSERT(tage->page);				  \
	__LASSERT(tage->used <= PAGE_SIZE);			 \
	__LASSERT(page_count(tage->page) > 0);		      \
} while (0)

#endif	/* LUSTRE_TRACEFILE_PRIVATE */

#endif /* __LIBCFS_TRACEFILE_H__ */
