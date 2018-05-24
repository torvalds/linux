// SPDX-License-Identifier: GPL-2.0
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
 *
 * libcfs/libcfs/tracefile.c
 *
 * Author: Zach Brown <zab@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LNET
#define LUSTRE_TRACEFILE_PRIVATE
#define pr_fmt(fmt) "Lustre: " fmt
#include "tracefile.h"

#include <linux/libcfs/libcfs.h>

/* XXX move things up to the top, comment */
union cfs_trace_data_union (*cfs_trace_data[TCD_MAX_TYPES])[NR_CPUS] __cacheline_aligned;

char cfs_tracefile[TRACEFILE_NAME_SIZE];
long long cfs_tracefile_size = CFS_TRACEFILE_SIZE;
static struct tracefiled_ctl trace_tctl;
static DEFINE_MUTEX(cfs_trace_thread_mutex);
static int thread_running;

static atomic_t cfs_tage_allocated = ATOMIC_INIT(0);

struct page_collection {
	struct list_head	pc_pages;
	/*
	 * if this flag is set, collect_pages() will spill both
	 * ->tcd_daemon_pages and ->tcd_pages to the ->pc_pages. Otherwise,
	 * only ->tcd_pages are spilled.
	 */
	int			pc_want_daemon_pages;
};

struct tracefiled_ctl {
	struct completion	tctl_start;
	struct completion	tctl_stop;
	wait_queue_head_t	tctl_waitq;
	pid_t			tctl_pid;
	atomic_t		tctl_shutdown;
};

/*
 * small data-structure for each page owned by tracefiled.
 */
struct cfs_trace_page {
	/*
	 * page itself
	 */
	struct page		*page;
	/*
	 * linkage into one of the lists in trace_data_union or
	 * page_collection
	 */
	struct list_head	linkage;
	/*
	 * number of bytes used within this page
	 */
	unsigned int		used;
	/*
	 * cpu that owns this page
	 */
	unsigned short		cpu;
	/*
	 * type(context) of this page
	 */
	unsigned short		type;
};

static void put_pages_on_tcd_daemon_list(struct page_collection *pc,
					 struct cfs_trace_cpu_data *tcd);

static inline struct cfs_trace_page *
cfs_tage_from_list(struct list_head *list)
{
	return list_entry(list, struct cfs_trace_page, linkage);
}

static struct cfs_trace_page *cfs_tage_alloc(gfp_t gfp)
{
	struct page *page;
	struct cfs_trace_page *tage;

	/* My caller is trying to free memory */
	if (!in_interrupt() && memory_pressure_get())
		return NULL;

	/*
	 * Don't spam console with allocation failures: they will be reported
	 * by upper layer anyway.
	 */
	gfp |= __GFP_NOWARN;
	page = alloc_page(gfp);
	if (!page)
		return NULL;

	tage = kmalloc(sizeof(*tage), gfp);
	if (!tage) {
		__free_page(page);
		return NULL;
	}

	tage->page = page;
	atomic_inc(&cfs_tage_allocated);
	return tage;
}

static void cfs_tage_free(struct cfs_trace_page *tage)
{
	__free_page(tage->page);
	kfree(tage);
	atomic_dec(&cfs_tage_allocated);
}

static void cfs_tage_to_tail(struct cfs_trace_page *tage,
			     struct list_head *queue)
{
	list_move_tail(&tage->linkage, queue);
}

int cfs_trace_refill_stock(struct cfs_trace_cpu_data *tcd, gfp_t gfp,
			   struct list_head *stock)
{
	int i;

	/*
	 * XXX nikita: do NOT call portals_debug_msg() (CDEBUG/ENTRY/EXIT)
	 * from here: this will lead to infinite recursion.
	 */

	for (i = 0; i + tcd->tcd_cur_stock_pages < TCD_STOCK_PAGES ; ++i) {
		struct cfs_trace_page *tage;

		tage = cfs_tage_alloc(gfp);
		if (!tage)
			break;
		list_add_tail(&tage->linkage, stock);
	}
	return i;
}

/* return a page that has 'len' bytes left at the end */
static struct cfs_trace_page *
cfs_trace_get_tage_try(struct cfs_trace_cpu_data *tcd, unsigned long len)
{
	struct cfs_trace_page *tage;

	if (tcd->tcd_cur_pages > 0) {
		__LASSERT(!list_empty(&tcd->tcd_pages));
		tage = cfs_tage_from_list(tcd->tcd_pages.prev);
		if (tage->used + len <= PAGE_SIZE)
			return tage;
	}

	if (tcd->tcd_cur_pages < tcd->tcd_max_pages) {
		if (tcd->tcd_cur_stock_pages > 0) {
			tage = cfs_tage_from_list(tcd->tcd_stock_pages.prev);
			--tcd->tcd_cur_stock_pages;
			list_del_init(&tage->linkage);
		} else {
			tage = cfs_tage_alloc(GFP_ATOMIC);
			if (unlikely(!tage)) {
				if (!memory_pressure_get() || in_interrupt())
					pr_warn_ratelimited("cannot allocate a tage (%ld)\n",
							    tcd->tcd_cur_pages);
				return NULL;
			}
		}

		tage->used = 0;
		tage->cpu = smp_processor_id();
		tage->type = tcd->tcd_type;
		list_add_tail(&tage->linkage, &tcd->tcd_pages);
		tcd->tcd_cur_pages++;

		if (tcd->tcd_cur_pages > 8 && thread_running) {
			struct tracefiled_ctl *tctl = &trace_tctl;
			/*
			 * wake up tracefiled to process some pages.
			 */
			wake_up(&tctl->tctl_waitq);
		}
		return tage;
	}
	return NULL;
}

static void cfs_tcd_shrink(struct cfs_trace_cpu_data *tcd)
{
	int pgcount = tcd->tcd_cur_pages / 10;
	struct page_collection pc;
	struct cfs_trace_page *tage;
	struct cfs_trace_page *tmp;

	/*
	 * XXX nikita: do NOT call portals_debug_msg() (CDEBUG/ENTRY/EXIT)
	 * from here: this will lead to infinite recursion.
	 */

	pr_warn_ratelimited("debug daemon buffer overflowed; discarding 10%% of pages (%d of %ld)\n",
			    pgcount + 1, tcd->tcd_cur_pages);

	INIT_LIST_HEAD(&pc.pc_pages);

	list_for_each_entry_safe(tage, tmp, &tcd->tcd_pages, linkage) {
		if (!pgcount--)
			break;

		list_move_tail(&tage->linkage, &pc.pc_pages);
		tcd->tcd_cur_pages--;
	}
	put_pages_on_tcd_daemon_list(&pc, tcd);
}

/* return a page that has 'len' bytes left at the end */
static struct cfs_trace_page *cfs_trace_get_tage(struct cfs_trace_cpu_data *tcd,
						 unsigned long len)
{
	struct cfs_trace_page *tage;

	/*
	 * XXX nikita: do NOT call portals_debug_msg() (CDEBUG/ENTRY/EXIT)
	 * from here: this will lead to infinite recursion.
	 */

	if (len > PAGE_SIZE) {
		pr_err("cowardly refusing to write %lu bytes in a page\n", len);
		return NULL;
	}

	tage = cfs_trace_get_tage_try(tcd, len);
	if (tage)
		return tage;
	if (thread_running)
		cfs_tcd_shrink(tcd);
	if (tcd->tcd_cur_pages > 0) {
		tage = cfs_tage_from_list(tcd->tcd_pages.next);
		tage->used = 0;
		cfs_tage_to_tail(tage, &tcd->tcd_pages);
	}
	return tage;
}

int libcfs_debug_msg(struct libcfs_debug_msg_data *msgdata,
		     const char *format, ...)
{
	va_list args;
	int rc;

	va_start(args, format);
	rc = libcfs_debug_vmsg2(msgdata, format, args, NULL);
	va_end(args);

	return rc;
}
EXPORT_SYMBOL(libcfs_debug_msg);

int libcfs_debug_vmsg2(struct libcfs_debug_msg_data *msgdata,
		       const char *format1, va_list args,
		       const char *format2, ...)
{
	struct cfs_trace_cpu_data *tcd = NULL;
	struct ptldebug_header header = { 0 };
	struct cfs_trace_page *tage;
	/* string_buf is used only if tcd != NULL, and is always set then */
	char *string_buf = NULL;
	char *debug_buf;
	int known_size;
	int needed = 85; /* average message length */
	int max_nob;
	va_list ap;
	int depth;
	int i;
	int remain;
	int mask = msgdata->msg_mask;
	const char *file = kbasename(msgdata->msg_file);
	struct cfs_debug_limit_state *cdls = msgdata->msg_cdls;

	tcd = cfs_trace_get_tcd();

	/* cfs_trace_get_tcd() grabs a lock, which disables preemption and
	 * pins us to a particular CPU.  This avoids an smp_processor_id()
	 * warning on Linux when debugging is enabled.
	 */
	cfs_set_ptldebug_header(&header, msgdata, CDEBUG_STACK());

	if (!tcd)		/* arch may not log in IRQ context */
		goto console;

	if (!tcd->tcd_cur_pages)
		header.ph_flags |= PH_FLAG_FIRST_RECORD;

	if (tcd->tcd_shutting_down) {
		cfs_trace_put_tcd(tcd);
		tcd = NULL;
		goto console;
	}

	depth = __current_nesting_level();
	known_size = strlen(file) + 1 + depth;
	if (msgdata->msg_fn)
		known_size += strlen(msgdata->msg_fn) + 1;

	if (libcfs_debug_binary)
		known_size += sizeof(header);

	/*
	 * '2' used because vsnprintf return real size required for output
	 * _without_ terminating NULL.
	 * if needed is to small for this format.
	 */
	for (i = 0; i < 2; i++) {
		tage = cfs_trace_get_tage(tcd, needed + known_size + 1);
		if (!tage) {
			if (needed + known_size > PAGE_SIZE)
				mask |= D_ERROR;

			cfs_trace_put_tcd(tcd);
			tcd = NULL;
			goto console;
		}

		string_buf = (char *)page_address(tage->page) +
					tage->used + known_size;

		max_nob = PAGE_SIZE - tage->used - known_size;
		if (max_nob <= 0) {
			pr_emerg("negative max_nob: %d\n", max_nob);
			mask |= D_ERROR;
			cfs_trace_put_tcd(tcd);
			tcd = NULL;
			goto console;
		}

		needed = 0;
		if (format1) {
			va_copy(ap, args);
			needed = vsnprintf(string_buf, max_nob, format1, ap);
			va_end(ap);
		}

		if (format2) {
			remain = max_nob - needed;
			if (remain < 0)
				remain = 0;

			va_start(ap, format2);
			needed += vsnprintf(string_buf + needed, remain,
					    format2, ap);
			va_end(ap);
		}

		if (needed < max_nob) /* well. printing ok.. */
			break;
	}

	if (*(string_buf + needed - 1) != '\n')
		pr_info("format at %s:%d:%s doesn't end in newline\n", file,
			msgdata->msg_line, msgdata->msg_fn);

	header.ph_len = known_size + needed;
	debug_buf = (char *)page_address(tage->page) + tage->used;

	if (libcfs_debug_binary) {
		memcpy(debug_buf, &header, sizeof(header));
		tage->used += sizeof(header);
		debug_buf += sizeof(header);
	}

	/* indent message according to the nesting level */
	while (depth-- > 0) {
		*(debug_buf++) = '.';
		++tage->used;
	}

	strcpy(debug_buf, file);
	tage->used += strlen(file) + 1;
	debug_buf += strlen(file) + 1;

	if (msgdata->msg_fn) {
		strcpy(debug_buf, msgdata->msg_fn);
		tage->used += strlen(msgdata->msg_fn) + 1;
		debug_buf += strlen(msgdata->msg_fn) + 1;
	}

	__LASSERT(debug_buf == string_buf);

	tage->used += needed;
	__LASSERT(tage->used <= PAGE_SIZE);

console:
	if (!(mask & libcfs_printk)) {
		/* no console output requested */
		if (tcd)
			cfs_trace_put_tcd(tcd);
		return 1;
	}

	if (cdls) {
		if (libcfs_console_ratelimit &&
		    cdls->cdls_next &&		/* not first time ever */
		    !cfs_time_after(cfs_time_current(), cdls->cdls_next)) {
			/* skipping a console message */
			cdls->cdls_count++;
			if (tcd)
				cfs_trace_put_tcd(tcd);
			return 1;
		}

		if (cfs_time_after(cfs_time_current(),
				   cdls->cdls_next + libcfs_console_max_delay +
				   10 * HZ)) {
			/* last timeout was a long time ago */
			cdls->cdls_delay /= libcfs_console_backoff * 4;
		} else {
			cdls->cdls_delay *= libcfs_console_backoff;
		}

		if (cdls->cdls_delay < libcfs_console_min_delay)
			cdls->cdls_delay = libcfs_console_min_delay;
		else if (cdls->cdls_delay > libcfs_console_max_delay)
			cdls->cdls_delay = libcfs_console_max_delay;

		/* ensure cdls_next is never zero after it's been seen */
		cdls->cdls_next = (cfs_time_current() + cdls->cdls_delay) | 1;
	}

	if (tcd) {
		cfs_print_to_console(&header, mask, string_buf, needed, file,
				     msgdata->msg_fn);
		cfs_trace_put_tcd(tcd);
	} else {
		string_buf = cfs_trace_get_console_buffer();

		needed = 0;
		if (format1) {
			va_copy(ap, args);
			needed = vsnprintf(string_buf,
					   CFS_TRACE_CONSOLE_BUFFER_SIZE,
					   format1, ap);
			va_end(ap);
		}
		if (format2) {
			remain = CFS_TRACE_CONSOLE_BUFFER_SIZE - needed;
			if (remain > 0) {
				va_start(ap, format2);
				needed += vsnprintf(string_buf + needed, remain,
						    format2, ap);
				va_end(ap);
			}
		}
		cfs_print_to_console(&header, mask,
				     string_buf, needed, file, msgdata->msg_fn);

		put_cpu();
	}

	if (cdls && cdls->cdls_count) {
		string_buf = cfs_trace_get_console_buffer();

		needed = snprintf(string_buf, CFS_TRACE_CONSOLE_BUFFER_SIZE,
				  "Skipped %d previous similar message%s\n",
				  cdls->cdls_count,
				  (cdls->cdls_count > 1) ? "s" : "");

		cfs_print_to_console(&header, mask,
				     string_buf, needed, file, msgdata->msg_fn);

		put_cpu();
		cdls->cdls_count = 0;
	}

	return 0;
}
EXPORT_SYMBOL(libcfs_debug_vmsg2);

void
cfs_trace_assertion_failed(const char *str,
			   struct libcfs_debug_msg_data *msgdata)
{
	struct ptldebug_header hdr;

	libcfs_panic_in_progress = 1;
	libcfs_catastrophe = 1;
	mb();

	cfs_set_ptldebug_header(&hdr, msgdata, CDEBUG_STACK());

	cfs_print_to_console(&hdr, D_EMERG, str, strlen(str),
			     msgdata->msg_file, msgdata->msg_fn);

	panic("Lustre debug assertion failure\n");

	/* not reached */
}

static void
panic_collect_pages(struct page_collection *pc)
{
	/* Do the collect_pages job on a single CPU: assumes that all other
	 * CPUs have been stopped during a panic.  If this isn't true for some
	 * arch, this will have to be implemented separately in each arch.
	 */
	struct cfs_trace_cpu_data *tcd;
	int i;
	int j;

	INIT_LIST_HEAD(&pc->pc_pages);

	cfs_tcd_for_each(tcd, i, j) {
		list_splice_init(&tcd->tcd_pages, &pc->pc_pages);
		tcd->tcd_cur_pages = 0;

		if (pc->pc_want_daemon_pages) {
			list_splice_init(&tcd->tcd_daemon_pages, &pc->pc_pages);
			tcd->tcd_cur_daemon_pages = 0;
		}
	}
}

static void collect_pages_on_all_cpus(struct page_collection *pc)
{
	struct cfs_trace_cpu_data *tcd;
	int i, cpu;

	for_each_possible_cpu(cpu) {
		cfs_tcd_for_each_type_lock(tcd, i, cpu) {
			list_splice_init(&tcd->tcd_pages, &pc->pc_pages);
			tcd->tcd_cur_pages = 0;
			if (pc->pc_want_daemon_pages) {
				list_splice_init(&tcd->tcd_daemon_pages,
						 &pc->pc_pages);
				tcd->tcd_cur_daemon_pages = 0;
			}
		}
	}
}

static void collect_pages(struct page_collection *pc)
{
	INIT_LIST_HEAD(&pc->pc_pages);

	if (libcfs_panic_in_progress)
		panic_collect_pages(pc);
	else
		collect_pages_on_all_cpus(pc);
}

static void put_pages_back_on_all_cpus(struct page_collection *pc)
{
	struct cfs_trace_cpu_data *tcd;
	struct list_head *cur_head;
	struct cfs_trace_page *tage;
	struct cfs_trace_page *tmp;
	int i, cpu;

	for_each_possible_cpu(cpu) {
		cfs_tcd_for_each_type_lock(tcd, i, cpu) {
			cur_head = tcd->tcd_pages.next;

			list_for_each_entry_safe(tage, tmp, &pc->pc_pages,
						 linkage) {
				__LASSERT_TAGE_INVARIANT(tage);

				if (tage->cpu != cpu || tage->type != i)
					continue;

				cfs_tage_to_tail(tage, cur_head);
				tcd->tcd_cur_pages++;
			}
		}
	}
}

static void put_pages_back(struct page_collection *pc)
{
	if (!libcfs_panic_in_progress)
		put_pages_back_on_all_cpus(pc);
}

/* Add pages to a per-cpu debug daemon ringbuffer.  This buffer makes sure that
 * we have a good amount of data at all times for dumping during an LBUG, even
 * if we have been steadily writing (and otherwise discarding) pages via the
 * debug daemon.
 */
static void put_pages_on_tcd_daemon_list(struct page_collection *pc,
					 struct cfs_trace_cpu_data *tcd)
{
	struct cfs_trace_page *tage;
	struct cfs_trace_page *tmp;

	list_for_each_entry_safe(tage, tmp, &pc->pc_pages, linkage) {
		__LASSERT_TAGE_INVARIANT(tage);

		if (tage->cpu != tcd->tcd_cpu || tage->type != tcd->tcd_type)
			continue;

		cfs_tage_to_tail(tage, &tcd->tcd_daemon_pages);
		tcd->tcd_cur_daemon_pages++;

		if (tcd->tcd_cur_daemon_pages > tcd->tcd_max_pages) {
			struct cfs_trace_page *victim;

			__LASSERT(!list_empty(&tcd->tcd_daemon_pages));
			victim = cfs_tage_from_list(tcd->tcd_daemon_pages.next);

			__LASSERT_TAGE_INVARIANT(victim);

			list_del(&victim->linkage);
			cfs_tage_free(victim);
			tcd->tcd_cur_daemon_pages--;
		}
	}
}

static void put_pages_on_daemon_list(struct page_collection *pc)
{
	struct cfs_trace_cpu_data *tcd;
	int i, cpu;

	for_each_possible_cpu(cpu) {
		cfs_tcd_for_each_type_lock(tcd, i, cpu)
			put_pages_on_tcd_daemon_list(pc, tcd);
	}
}

void cfs_trace_debug_print(void)
{
	struct page_collection pc;
	struct cfs_trace_page *tage;
	struct cfs_trace_page *tmp;

	pc.pc_want_daemon_pages = 1;
	collect_pages(&pc);
	list_for_each_entry_safe(tage, tmp, &pc.pc_pages, linkage) {
		char *p, *file, *fn;
		struct page *page;

		__LASSERT_TAGE_INVARIANT(tage);

		page = tage->page;
		p = page_address(page);
		while (p < ((char *)page_address(page) + tage->used)) {
			struct ptldebug_header *hdr;
			int len;

			hdr = (void *)p;
			p += sizeof(*hdr);
			file = p;
			p += strlen(file) + 1;
			fn = p;
			p += strlen(fn) + 1;
			len = hdr->ph_len - (int)(p - (char *)hdr);

			cfs_print_to_console(hdr, D_EMERG, p, len, file, fn);

			p += len;
		}

		list_del(&tage->linkage);
		cfs_tage_free(tage);
	}
}

int cfs_tracefile_dump_all_pages(char *filename)
{
	struct page_collection pc;
	struct file *filp;
	struct cfs_trace_page *tage;
	struct cfs_trace_page *tmp;
	char *buf;
	mm_segment_t __oldfs;
	int rc;

	cfs_tracefile_write_lock();

	filp = filp_open(filename, O_CREAT | O_EXCL | O_WRONLY | O_LARGEFILE,
			 0600);
	if (IS_ERR(filp)) {
		rc = PTR_ERR(filp);
		filp = NULL;
		pr_err("LustreError: can't open %s for dump: rc %d\n",
		       filename, rc);
		goto out;
	}

	pc.pc_want_daemon_pages = 1;
	collect_pages(&pc);
	if (list_empty(&pc.pc_pages)) {
		rc = 0;
		goto close;
	}
	__oldfs = get_fs();
	set_fs(get_ds());

	/* ok, for now, just write the pages.  in the future we'll be building
	 * iobufs with the pages and calling generic_direct_IO
	 */
	list_for_each_entry_safe(tage, tmp, &pc.pc_pages, linkage) {
		__LASSERT_TAGE_INVARIANT(tage);

		buf = kmap(tage->page);
		rc = kernel_write(filp, buf, tage->used, &filp->f_pos);
		kunmap(tage->page);

		if (rc != (int)tage->used) {
			pr_warn("wanted to write %u but wrote %d\n", tage->used,
				rc);
			put_pages_back(&pc);
			__LASSERT(list_empty(&pc.pc_pages));
			break;
		}
		list_del(&tage->linkage);
		cfs_tage_free(tage);
	}
	set_fs(__oldfs);
	rc = vfs_fsync(filp, 1);
	if (rc)
		pr_err("sync returns %d\n", rc);
close:
	filp_close(filp, NULL);
out:
	cfs_tracefile_write_unlock();
	return rc;
}

void cfs_trace_flush_pages(void)
{
	struct page_collection pc;
	struct cfs_trace_page *tage;
	struct cfs_trace_page *tmp;

	pc.pc_want_daemon_pages = 1;
	collect_pages(&pc);
	list_for_each_entry_safe(tage, tmp, &pc.pc_pages, linkage) {
		__LASSERT_TAGE_INVARIANT(tage);

		list_del(&tage->linkage);
		cfs_tage_free(tage);
	}
}

int cfs_trace_copyin_string(char *knl_buffer, int knl_buffer_nob,
			    const char __user *usr_buffer, int usr_buffer_nob)
{
	int nob;

	if (usr_buffer_nob > knl_buffer_nob)
		return -EOVERFLOW;

	if (copy_from_user((void *)knl_buffer,
			   usr_buffer, usr_buffer_nob))
		return -EFAULT;

	nob = strnlen(knl_buffer, usr_buffer_nob);
	while (--nob >= 0)		      /* strip trailing whitespace */
		if (!isspace(knl_buffer[nob]))
			break;

	if (nob < 0)			    /* empty string */
		return -EINVAL;

	if (nob == knl_buffer_nob)	      /* no space to terminate */
		return -EOVERFLOW;

	knl_buffer[nob + 1] = 0;		/* terminate */
	return 0;
}
EXPORT_SYMBOL(cfs_trace_copyin_string);

int cfs_trace_copyout_string(char __user *usr_buffer, int usr_buffer_nob,
			     const char *knl_buffer, char *append)
{
	/*
	 * NB if 'append' != NULL, it's a single character to append to the
	 * copied out string - usually "\n" or "" (i.e. a terminating zero byte)
	 */
	int nob = strlen(knl_buffer);

	if (nob > usr_buffer_nob)
		nob = usr_buffer_nob;

	if (copy_to_user(usr_buffer, knl_buffer, nob))
		return -EFAULT;

	if (append && nob < usr_buffer_nob) {
		if (copy_to_user(usr_buffer + nob, append, 1))
			return -EFAULT;

		nob++;
	}

	return nob;
}
EXPORT_SYMBOL(cfs_trace_copyout_string);

int cfs_trace_allocate_string_buffer(char **str, int nob)
{
	if (nob > 2 * PAGE_SIZE)	    /* string must be "sensible" */
		return -EINVAL;

	*str = kmalloc(nob, GFP_KERNEL | __GFP_ZERO);
	if (!*str)
		return -ENOMEM;

	return 0;
}

int cfs_trace_dump_debug_buffer_usrstr(void __user *usr_str, int usr_str_nob)
{
	char *str;
	int rc;

	rc = cfs_trace_allocate_string_buffer(&str, usr_str_nob + 1);
	if (rc)
		return rc;

	rc = cfs_trace_copyin_string(str, usr_str_nob + 1,
				     usr_str, usr_str_nob);
	if (rc)
		goto out;

	if (str[0] != '/') {
		rc = -EINVAL;
		goto out;
	}
	rc = cfs_tracefile_dump_all_pages(str);
out:
	kfree(str);
	return rc;
}

int cfs_trace_daemon_command(char *str)
{
	int rc = 0;

	cfs_tracefile_write_lock();

	if (!strcmp(str, "stop")) {
		cfs_tracefile_write_unlock();
		cfs_trace_stop_thread();
		cfs_tracefile_write_lock();
		memset(cfs_tracefile, 0, sizeof(cfs_tracefile));

	} else if (!strncmp(str, "size=", 5)) {
		unsigned long tmp;

		rc = kstrtoul(str + 5, 10, &tmp);
		if (!rc) {
			if (tmp < 10 || tmp > 20480)
				cfs_tracefile_size = CFS_TRACEFILE_SIZE;
			else
				cfs_tracefile_size = tmp << 20;
		}
	} else if (strlen(str) >= sizeof(cfs_tracefile)) {
		rc = -ENAMETOOLONG;
	} else if (str[0] != '/') {
		rc = -EINVAL;
	} else {
		strcpy(cfs_tracefile, str);

		pr_info("debug daemon will attempt to start writing to %s (%lukB max)\n",
			cfs_tracefile,
			(long)(cfs_tracefile_size >> 10));

		cfs_trace_start_thread();
	}

	cfs_tracefile_write_unlock();
	return rc;
}

int cfs_trace_daemon_command_usrstr(void __user *usr_str, int usr_str_nob)
{
	char *str;
	int rc;

	rc = cfs_trace_allocate_string_buffer(&str, usr_str_nob + 1);
	if (rc)
		return rc;

	rc = cfs_trace_copyin_string(str, usr_str_nob + 1,
				     usr_str, usr_str_nob);
	if (!rc)
		rc = cfs_trace_daemon_command(str);

	kfree(str);
	return rc;
}

int cfs_trace_set_debug_mb(int mb)
{
	int i;
	int j;
	int pages;
	int limit = cfs_trace_max_debug_mb();
	struct cfs_trace_cpu_data *tcd;

	if (mb < num_possible_cpus()) {
		pr_warn("%d MB is too small for debug buffer size, setting it to %d MB.\n",
			mb, num_possible_cpus());
		mb = num_possible_cpus();
	}

	if (mb > limit) {
		pr_warn("%d MB is too large for debug buffer size, setting it to %d MB.\n",
			mb, limit);
		mb = limit;
	}

	mb /= num_possible_cpus();
	pages = mb << (20 - PAGE_SHIFT);

	cfs_tracefile_write_lock();

	cfs_tcd_for_each(tcd, i, j)
		tcd->tcd_max_pages = (pages * tcd->tcd_pages_factor) / 100;

	cfs_tracefile_write_unlock();

	return 0;
}

int cfs_trace_get_debug_mb(void)
{
	int i;
	int j;
	struct cfs_trace_cpu_data *tcd;
	int total_pages = 0;

	cfs_tracefile_read_lock();

	cfs_tcd_for_each(tcd, i, j)
		total_pages += tcd->tcd_max_pages;

	cfs_tracefile_read_unlock();

	return (total_pages >> (20 - PAGE_SHIFT)) + 1;
}

static int tracefiled(void *arg)
{
	struct page_collection pc;
	struct tracefiled_ctl *tctl = arg;
	struct cfs_trace_page *tage;
	struct cfs_trace_page *tmp;
	struct file *filp;
	char *buf;
	int last_loop = 0;
	int rc;

	/* we're started late enough that we pick up init's fs context */
	/* this is so broken in uml?  what on earth is going on? */

	complete(&tctl->tctl_start);

	while (1) {
		wait_queue_entry_t __wait;

		pc.pc_want_daemon_pages = 0;
		collect_pages(&pc);
		if (list_empty(&pc.pc_pages))
			goto end_loop;

		filp = NULL;
		cfs_tracefile_read_lock();
		if (cfs_tracefile[0]) {
			filp = filp_open(cfs_tracefile,
					 O_CREAT | O_RDWR | O_LARGEFILE,
					 0600);
			if (IS_ERR(filp)) {
				rc = PTR_ERR(filp);
				filp = NULL;
				pr_warn("couldn't open %s: %d\n", cfs_tracefile,
					rc);
			}
		}
		cfs_tracefile_read_unlock();
		if (!filp) {
			put_pages_on_daemon_list(&pc);
			__LASSERT(list_empty(&pc.pc_pages));
			goto end_loop;
		}

		list_for_each_entry_safe(tage, tmp, &pc.pc_pages, linkage) {
			static loff_t f_pos;

			__LASSERT_TAGE_INVARIANT(tage);

			if (f_pos >= (off_t)cfs_tracefile_size)
				f_pos = 0;
			else if (f_pos > i_size_read(file_inode(filp)))
				f_pos = i_size_read(file_inode(filp));

			buf = kmap(tage->page);
			rc = kernel_write(filp, buf, tage->used, &f_pos);
			kunmap(tage->page);

			if (rc != (int)tage->used) {
				pr_warn("wanted to write %u but wrote %d\n",
					tage->used, rc);
				put_pages_back(&pc);
				__LASSERT(list_empty(&pc.pc_pages));
				break;
			}
		}

		filp_close(filp, NULL);
		put_pages_on_daemon_list(&pc);
		if (!list_empty(&pc.pc_pages)) {
			int i;

			pr_alert("trace pages aren't empty\n");
			pr_err("total cpus(%d): ", num_possible_cpus());
			for (i = 0; i < num_possible_cpus(); i++)
				if (cpu_online(i))
					pr_cont("%d(on) ", i);
				else
					pr_cont("%d(off) ", i);
			pr_cont("\n");

			i = 0;
			list_for_each_entry_safe(tage, tmp, &pc.pc_pages,
						 linkage)
				pr_err("page %d belongs to cpu %d\n",
				       ++i, tage->cpu);
			pr_err("There are %d pages unwritten\n", i);
		}
		__LASSERT(list_empty(&pc.pc_pages));
end_loop:
		if (atomic_read(&tctl->tctl_shutdown)) {
			if (!last_loop) {
				last_loop = 1;
				continue;
			} else {
				break;
			}
		}
		init_waitqueue_entry(&__wait, current);
		add_wait_queue(&tctl->tctl_waitq, &__wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
		remove_wait_queue(&tctl->tctl_waitq, &__wait);
	}
	complete(&tctl->tctl_stop);
	return 0;
}

int cfs_trace_start_thread(void)
{
	struct tracefiled_ctl *tctl = &trace_tctl;
	struct task_struct *task;
	int rc = 0;

	mutex_lock(&cfs_trace_thread_mutex);
	if (thread_running)
		goto out;

	init_completion(&tctl->tctl_start);
	init_completion(&tctl->tctl_stop);
	init_waitqueue_head(&tctl->tctl_waitq);
	atomic_set(&tctl->tctl_shutdown, 0);

	task = kthread_run(tracefiled, tctl, "ktracefiled");
	if (IS_ERR(task)) {
		rc = PTR_ERR(task);
		goto out;
	}

	wait_for_completion(&tctl->tctl_start);
	thread_running = 1;
out:
	mutex_unlock(&cfs_trace_thread_mutex);
	return rc;
}

void cfs_trace_stop_thread(void)
{
	struct tracefiled_ctl *tctl = &trace_tctl;

	mutex_lock(&cfs_trace_thread_mutex);
	if (thread_running) {
		pr_info("shutting down debug daemon thread...\n");
		atomic_set(&tctl->tctl_shutdown, 1);
		wait_for_completion(&tctl->tctl_stop);
		thread_running = 0;
	}
	mutex_unlock(&cfs_trace_thread_mutex);
}

int cfs_tracefile_init(int max_pages)
{
	struct cfs_trace_cpu_data *tcd;
	int i;
	int j;
	int rc;
	int factor;

	rc = cfs_tracefile_init_arch();
	if (rc)
		return rc;

	cfs_tcd_for_each(tcd, i, j) {
		/* tcd_pages_factor is initialized int tracefile_init_arch. */
		factor = tcd->tcd_pages_factor;
		INIT_LIST_HEAD(&tcd->tcd_pages);
		INIT_LIST_HEAD(&tcd->tcd_stock_pages);
		INIT_LIST_HEAD(&tcd->tcd_daemon_pages);
		tcd->tcd_cur_pages = 0;
		tcd->tcd_cur_stock_pages = 0;
		tcd->tcd_cur_daemon_pages = 0;
		tcd->tcd_max_pages = (max_pages * factor) / 100;
		LASSERT(tcd->tcd_max_pages > 0);
		tcd->tcd_shutting_down = 0;
	}

	return 0;
}

static void trace_cleanup_on_all_cpus(void)
{
	struct cfs_trace_cpu_data *tcd;
	struct cfs_trace_page *tage;
	struct cfs_trace_page *tmp;
	int i, cpu;

	for_each_possible_cpu(cpu) {
		cfs_tcd_for_each_type_lock(tcd, i, cpu) {
			tcd->tcd_shutting_down = 1;

			list_for_each_entry_safe(tage, tmp, &tcd->tcd_pages,
						 linkage) {
				__LASSERT_TAGE_INVARIANT(tage);

				list_del(&tage->linkage);
				cfs_tage_free(tage);
			}

			tcd->tcd_cur_pages = 0;
		}
	}
}

static void cfs_trace_cleanup(void)
{
	struct page_collection pc;

	INIT_LIST_HEAD(&pc.pc_pages);

	trace_cleanup_on_all_cpus();

	cfs_tracefile_fini_arch();
}

void cfs_tracefile_exit(void)
{
	cfs_trace_stop_thread();
	cfs_trace_cleanup();
}
