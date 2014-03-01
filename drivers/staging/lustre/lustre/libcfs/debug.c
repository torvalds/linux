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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/debug.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 *
 */

# define DEBUG_SUBSYSTEM S_LNET

#include <linux/libcfs/libcfs.h>
#include "tracefile.h"

static char debug_file_name[1024];

unsigned int libcfs_subsystem_debug = ~0;
module_param(libcfs_subsystem_debug, int, 0644);
MODULE_PARM_DESC(libcfs_subsystem_debug, "Lustre kernel debug subsystem mask");
EXPORT_SYMBOL(libcfs_subsystem_debug);

unsigned int libcfs_debug = (D_CANTMASK |
			     D_NETERROR | D_HA | D_CONFIG | D_IOCTL);
module_param(libcfs_debug, int, 0644);
MODULE_PARM_DESC(libcfs_debug, "Lustre kernel debug mask");
EXPORT_SYMBOL(libcfs_debug);

unsigned int libcfs_debug_mb = 0;
module_param(libcfs_debug_mb, uint, 0644);
MODULE_PARM_DESC(libcfs_debug_mb, "Total debug buffer size.");
EXPORT_SYMBOL(libcfs_debug_mb);

unsigned int libcfs_printk = D_CANTMASK;
module_param(libcfs_printk, uint, 0644);
MODULE_PARM_DESC(libcfs_printk, "Lustre kernel debug console mask");
EXPORT_SYMBOL(libcfs_printk);

unsigned int libcfs_console_ratelimit = 1;
module_param(libcfs_console_ratelimit, uint, 0644);
MODULE_PARM_DESC(libcfs_console_ratelimit, "Lustre kernel debug console ratelimit (0 to disable)");
EXPORT_SYMBOL(libcfs_console_ratelimit);

unsigned int libcfs_console_max_delay;
module_param(libcfs_console_max_delay, uint, 0644);
MODULE_PARM_DESC(libcfs_console_max_delay, "Lustre kernel debug console max delay (jiffies)");
EXPORT_SYMBOL(libcfs_console_max_delay);

unsigned int libcfs_console_min_delay;
module_param(libcfs_console_min_delay, uint, 0644);
MODULE_PARM_DESC(libcfs_console_min_delay, "Lustre kernel debug console min delay (jiffies)");
EXPORT_SYMBOL(libcfs_console_min_delay);

unsigned int libcfs_console_backoff = CDEBUG_DEFAULT_BACKOFF;
module_param(libcfs_console_backoff, uint, 0644);
MODULE_PARM_DESC(libcfs_console_backoff, "Lustre kernel debug console backoff factor");
EXPORT_SYMBOL(libcfs_console_backoff);

unsigned int libcfs_debug_binary = 1;
EXPORT_SYMBOL(libcfs_debug_binary);

unsigned int libcfs_stack = 3 * THREAD_SIZE / 4;
EXPORT_SYMBOL(libcfs_stack);

unsigned int portal_enter_debugger;
EXPORT_SYMBOL(portal_enter_debugger);

unsigned int libcfs_catastrophe;
EXPORT_SYMBOL(libcfs_catastrophe);

unsigned int libcfs_watchdog_ratelimit = 300;
EXPORT_SYMBOL(libcfs_watchdog_ratelimit);

unsigned int libcfs_panic_on_lbug = 1;
module_param(libcfs_panic_on_lbug, uint, 0644);
MODULE_PARM_DESC(libcfs_panic_on_lbug, "Lustre kernel panic on LBUG");
EXPORT_SYMBOL(libcfs_panic_on_lbug);

atomic_t libcfs_kmemory = ATOMIC_INIT(0);
EXPORT_SYMBOL(libcfs_kmemory);

static wait_queue_head_t debug_ctlwq;

char libcfs_debug_file_path_arr[PATH_MAX] = LIBCFS_DEBUG_FILE_PATH_DEFAULT;

/* We need to pass a pointer here, but elsewhere this must be a const */
char *libcfs_debug_file_path;
module_param(libcfs_debug_file_path, charp, 0644);
MODULE_PARM_DESC(libcfs_debug_file_path,
		 "Path for dumping debug logs, set 'NONE' to prevent log dumping");

int libcfs_panic_in_progress;

/* libcfs_debug_token2mask() expects the returned
 * string in lower-case */
const char *
libcfs_debug_subsys2str(int subsys)
{
	switch (1 << subsys) {
	default:
		return NULL;
	case S_UNDEFINED:
		return "undefined";
	case S_MDC:
		return "mdc";
	case S_MDS:
		return "mds";
	case S_OSC:
		return "osc";
	case S_OST:
		return "ost";
	case S_CLASS:
		return "class";
	case S_LOG:
		return "log";
	case S_LLITE:
		return "llite";
	case S_RPC:
		return "rpc";
	case S_LNET:
		return "lnet";
	case S_LND:
		return "lnd";
	case S_PINGER:
		return "pinger";
	case S_FILTER:
		return "filter";
	case S_ECHO:
		return "echo";
	case S_LDLM:
		return "ldlm";
	case S_LOV:
		return "lov";
	case S_LQUOTA:
		return "lquota";
	case S_OSD:
		return "osd";
	case S_LMV:
		return "lmv";
	case S_SEC:
		return "sec";
	case S_GSS:
		return "gss";
	case S_MGC:
		return "mgc";
	case S_MGS:
		return "mgs";
	case S_FID:
		return "fid";
	case S_FLD:
		return "fld";
	}
}

/* libcfs_debug_token2mask() expects the returned
 * string in lower-case */
const char *
libcfs_debug_dbg2str(int debug)
{
	switch (1 << debug) {
	default:
		return NULL;
	case D_TRACE:
		return "trace";
	case D_INODE:
		return "inode";
	case D_SUPER:
		return "super";
	case D_EXT2:
		return "ext2";
	case D_MALLOC:
		return "malloc";
	case D_CACHE:
		return "cache";
	case D_INFO:
		return "info";
	case D_IOCTL:
		return "ioctl";
	case D_NETERROR:
		return "neterror";
	case D_NET:
		return "net";
	case D_WARNING:
		return "warning";
	case D_BUFFS:
		return "buffs";
	case D_OTHER:
		return "other";
	case D_DENTRY:
		return "dentry";
	case D_NETTRACE:
		return "nettrace";
	case D_PAGE:
		return "page";
	case D_DLMTRACE:
		return "dlmtrace";
	case D_ERROR:
		return "error";
	case D_EMERG:
		return "emerg";
	case D_HA:
		return "ha";
	case D_RPCTRACE:
		return "rpctrace";
	case D_VFSTRACE:
		return "vfstrace";
	case D_READA:
		return "reada";
	case D_MMAP:
		return "mmap";
	case D_CONFIG:
		return "config";
	case D_CONSOLE:
		return "console";
	case D_QUOTA:
		return "quota";
	case D_SEC:
		return "sec";
	case D_LFSCK:
		return "lfsck";
	}
}

int
libcfs_debug_mask2str(char *str, int size, int mask, int is_subsys)
{
	const char *(*fn)(int bit) = is_subsys ? libcfs_debug_subsys2str :
						 libcfs_debug_dbg2str;
	int	   len = 0;
	const char   *token;
	int	   i;

	if (mask == 0) {			/* "0" */
		if (size > 0)
			str[0] = '0';
		len = 1;
	} else {				/* space-separated tokens */
		for (i = 0; i < 32; i++) {
			if ((mask & (1 << i)) == 0)
				continue;

			token = fn(i);
			if (token == NULL)	      /* unused bit */
				continue;

			if (len > 0) {		  /* separator? */
				if (len < size)
					str[len] = ' ';
				len++;
			}

			while (*token != 0) {
				if (len < size)
					str[len] = *token;
				token++;
				len++;
			}
		}
	}

	/* terminate 'str' */
	if (len < size)
		str[len] = 0;
	else
		str[size - 1] = 0;

	return len;
}

int
libcfs_debug_str2mask(int *mask, const char *str, int is_subsys)
{
	const char *(*fn)(int bit) = is_subsys ? libcfs_debug_subsys2str :
						 libcfs_debug_dbg2str;
	int	 m = 0;
	int	 matched;
	int	 n;
	int	 t;

	/* Allow a number for backwards compatibility */

	for (n = strlen(str); n > 0; n--)
		if (!isspace(str[n-1]))
			break;
	matched = n;

	if ((t = sscanf(str, "%i%n", &m, &matched)) >= 1 &&
	    matched == n) {
		/* don't print warning for lctl set_param debug=0 or -1 */
		if (m != 0 && m != -1)
			CWARN("You are trying to use a numerical value for the "
			      "mask - this will be deprecated in a future "
			      "release.\n");
		*mask = m;
		return 0;
	}

	return cfs_str2mask(str, fn, mask, is_subsys ? 0 : D_CANTMASK,
			    0xffffffff);
}

/**
 * Dump Lustre log to ::debug_file_path by calling tracefile_dump_all_pages()
 */
void libcfs_debug_dumplog_internal(void *arg)
{
	void *journal_info;

	journal_info = current->journal_info;
	current->journal_info = NULL;

	if (strncmp(libcfs_debug_file_path_arr, "NONE", 4) != 0) {
		snprintf(debug_file_name, sizeof(debug_file_name) - 1,
			 "%s.%ld." LPLD, libcfs_debug_file_path_arr,
			 cfs_time_current_sec(), (long_ptr_t)arg);
		printk(KERN_ALERT "LustreError: dumping log to %s\n",
		       debug_file_name);
		cfs_tracefile_dump_all_pages(debug_file_name);
		libcfs_run_debug_log_upcall(debug_file_name);
	}

	current->journal_info = journal_info;
}

int libcfs_debug_dumplog_thread(void *arg)
{
	libcfs_debug_dumplog_internal(arg);
	wake_up(&debug_ctlwq);
	return 0;
}

void libcfs_debug_dumplog(void)
{
	wait_queue_t wait;
	struct task_struct *dumper;

	/* we're being careful to ensure that the kernel thread is
	 * able to set our state to running as it exits before we
	 * get to schedule() */
	init_waitqueue_entry_current(&wait);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&debug_ctlwq, &wait);

	dumper = kthread_run(libcfs_debug_dumplog_thread,
			     (void *)(long)current_pid(),
			     "libcfs_debug_dumper");
	if (IS_ERR(dumper))
		printk(KERN_ERR "LustreError: cannot start log dump thread:"
		       " %ld\n", PTR_ERR(dumper));
	else
		waitq_wait(&wait, TASK_INTERRUPTIBLE);

	/* be sure to teardown if cfs_create_thread() failed */
	remove_wait_queue(&debug_ctlwq, &wait);
	set_current_state(TASK_RUNNING);
}
EXPORT_SYMBOL(libcfs_debug_dumplog);

int libcfs_debug_init(unsigned long bufsize)
{
	int    rc = 0;
	unsigned int max = libcfs_debug_mb;

	init_waitqueue_head(&debug_ctlwq);

	if (libcfs_console_max_delay <= 0 || /* not set by user or */
	    libcfs_console_min_delay <= 0 || /* set to invalid values */
	    libcfs_console_min_delay >= libcfs_console_max_delay) {
		libcfs_console_max_delay = CDEBUG_DEFAULT_MAX_DELAY;
		libcfs_console_min_delay = CDEBUG_DEFAULT_MIN_DELAY;
	}

	if (libcfs_debug_file_path != NULL) {
		memset(libcfs_debug_file_path_arr, 0, PATH_MAX);
		strncpy(libcfs_debug_file_path_arr,
			libcfs_debug_file_path, PATH_MAX-1);
	}

	/* If libcfs_debug_mb is set to an invalid value or uninitialized
	 * then just make the total buffers smp_num_cpus * TCD_MAX_PAGES */
	if (max > cfs_trace_max_debug_mb() || max < num_possible_cpus()) {
		max = TCD_MAX_PAGES;
	} else {
		max = (max / num_possible_cpus());
		max = (max << (20 - PAGE_CACHE_SHIFT));
	}
	rc = cfs_tracefile_init(max);

	if (rc == 0)
		libcfs_register_panic_notifier();

	return rc;
}

int libcfs_debug_cleanup(void)
{
	libcfs_unregister_panic_notifier();
	cfs_tracefile_exit();
	return 0;
}

int libcfs_debug_clear_buffer(void)
{
	cfs_trace_flush_pages();
	return 0;
}

/* Debug markers, although printed by S_LNET
 * should not be be marked as such. */
#undef DEBUG_SUBSYSTEM
#define DEBUG_SUBSYSTEM S_UNDEFINED
int libcfs_debug_mark_buffer(const char *text)
{
	CDEBUG(D_TRACE,"***************************************************\n");
	LCONSOLE(D_WARNING, "DEBUG MARKER: %s\n", text);
	CDEBUG(D_TRACE,"***************************************************\n");

	return 0;
}
#undef DEBUG_SUBSYSTEM
#define DEBUG_SUBSYSTEM S_LNET

void libcfs_debug_set_level(unsigned int debug_level)
{
	printk(KERN_WARNING "Lustre: Setting portals debug level to %08x\n",
	       debug_level);
	libcfs_debug = debug_level;
}

EXPORT_SYMBOL(libcfs_debug_set_level);

void libcfs_log_goto(struct libcfs_debug_msg_data *msgdata, const char *label,
		     long_ptr_t rc)
{
	libcfs_debug_msg(msgdata, "Process leaving via %s (rc=" LPLU " : " LPLD
			 " : " LPLX ")\n", label, (ulong_ptr_t)rc, rc, rc);
}
EXPORT_SYMBOL(libcfs_log_goto);
