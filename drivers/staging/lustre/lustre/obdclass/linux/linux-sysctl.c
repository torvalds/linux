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
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <linux/utsname.h>

#define DEBUG_SUBSYSTEM S_CLASS

#include <obd_support.h>
#include <lprocfs_status.h>

#ifdef CONFIG_SYSCTL
ctl_table_header_t *obd_table_header = NULL;
#endif


#define OBD_SYSCTL 300

enum {
	OBD_TIMEOUT = 3,	/* RPC timeout before recovery/intr */
	OBD_DUMP_ON_TIMEOUT,    /* dump kernel debug log upon eviction */
	OBD_MEMUSED,	    /* bytes currently OBD_ALLOCated */
	OBD_PAGESUSED,	  /* pages currently OBD_PAGE_ALLOCated */
	OBD_MAXMEMUSED,	 /* maximum bytes OBD_ALLOCated concurrently */
	OBD_MAXPAGESUSED,       /* maximum pages OBD_PAGE_ALLOCated concurrently */
	OBD_SYNCFILTER,	 /* XXX temporary, as we play with sync osts.. */
	OBD_LDLM_TIMEOUT,       /* LDLM timeout for ASTs before client eviction */
	OBD_DUMP_ON_EVICTION,   /* dump kernel debug log upon eviction */
	OBD_DEBUG_PEER_ON_TIMEOUT, /* dump peer debug when RPC times out */
	OBD_ALLOC_FAIL_RATE,    /* memory allocation random failure rate */
	OBD_MAX_DIRTY_PAGES,    /* maximum dirty pages */
	OBD_AT_MIN,	     /* Adaptive timeouts params */
	OBD_AT_MAX,
	OBD_AT_EXTRA,
	OBD_AT_EARLY_MARGIN,
	OBD_AT_HISTORY,
};


int LL_PROC_PROTO(proc_set_timeout)
{
	int rc;

	rc = ll_proc_dointvec(table, write, filp, buffer, lenp, ppos);
	if (ldlm_timeout >= obd_timeout)
		ldlm_timeout = max(obd_timeout / 3, 1U);
	return rc;
}

int LL_PROC_PROTO(proc_memory_alloc)
{
	char buf[22];
	int len;
	DECLARE_LL_PROC_PPOS_DECL;

	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	if (write)
		return -EINVAL;

	len = snprintf(buf, sizeof(buf), LPU64"\n", obd_memory_sum());
	if (len > *lenp)
		len = *lenp;
	buf[len] = '\0';
	if (copy_to_user(buffer, buf, len))
		return -EFAULT;
	*lenp = len;
	*ppos += *lenp;
	return 0;
}

int LL_PROC_PROTO(proc_pages_alloc)
{
	char buf[22];
	int len;
	DECLARE_LL_PROC_PPOS_DECL;

	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	if (write)
		return -EINVAL;

	len = snprintf(buf, sizeof(buf), LPU64"\n", obd_pages_sum());
	if (len > *lenp)
		len = *lenp;
	buf[len] = '\0';
	if (copy_to_user(buffer, buf, len))
		return -EFAULT;
	*lenp = len;
	*ppos += *lenp;
	return 0;
}

int LL_PROC_PROTO(proc_mem_max)
{
	char buf[22];
	int len;
	DECLARE_LL_PROC_PPOS_DECL;

	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	if (write)
		return -EINVAL;

	len = snprintf(buf, sizeof(buf), LPU64"\n", obd_memory_max());
	if (len > *lenp)
		len = *lenp;
	buf[len] = '\0';
	if (copy_to_user(buffer, buf, len))
		return -EFAULT;
	*lenp = len;
	*ppos += *lenp;
	return 0;
}

int LL_PROC_PROTO(proc_pages_max)
{
	char buf[22];
	int len;
	DECLARE_LL_PROC_PPOS_DECL;

	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	if (write)
		return -EINVAL;

	len = snprintf(buf, sizeof(buf), LPU64"\n", obd_pages_max());
	if (len > *lenp)
		len = *lenp;
	buf[len] = '\0';
	if (copy_to_user(buffer, buf, len))
		return -EFAULT;
	*lenp = len;
	*ppos += *lenp;
	return 0;
}

int LL_PROC_PROTO(proc_max_dirty_pages_in_mb)
{
	int rc = 0;
	DECLARE_LL_PROC_PPOS_DECL;

	if (!table->data || !table->maxlen || !*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	if (write) {
		rc = lprocfs_write_frac_helper(buffer, *lenp,
					       (unsigned int*)table->data,
					       1 << (20 - PAGE_CACHE_SHIFT));
		/* Don't allow them to let dirty pages exceed 90% of system
		 * memory and set a hard minimum of 4MB. */
		if (obd_max_dirty_pages > ((totalram_pages / 10) * 9)) {
			CERROR("Refusing to set max dirty pages to %u, which "
			       "is more than 90%% of available RAM; setting "
			       "to %lu\n", obd_max_dirty_pages,
			       ((totalram_pages / 10) * 9));
			obd_max_dirty_pages = ((totalram_pages / 10) * 9);
		} else if (obd_max_dirty_pages < 4 << (20 - PAGE_CACHE_SHIFT)) {
			obd_max_dirty_pages = 4 << (20 - PAGE_CACHE_SHIFT);
		}
	} else {
		char buf[21];
		int len;

		len = lprocfs_read_frac_helper(buf, sizeof(buf),
					       *(unsigned int*)table->data,
					       1 << (20 - PAGE_CACHE_SHIFT));
		if (len > *lenp)
			len = *lenp;
		buf[len] = '\0';
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
		*lenp = len;
	}
	*ppos += *lenp;
	return rc;
}

int LL_PROC_PROTO(proc_alloc_fail_rate)
{
	int rc	  = 0;
	DECLARE_LL_PROC_PPOS_DECL;

	if (!table->data || !table->maxlen || !*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	if (write) {
		rc = lprocfs_write_frac_helper(buffer, *lenp,
					       (unsigned int*)table->data,
					       OBD_ALLOC_FAIL_MULT);
	} else {
		char buf[21];
		int  len;

		len = lprocfs_read_frac_helper(buf, 21,
					       *(unsigned int*)table->data,
					       OBD_ALLOC_FAIL_MULT);
		if (len > *lenp)
			len = *lenp;
		buf[len] = '\0';
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
		*lenp = len;
	}
	*ppos += *lenp;
	return rc;
}

int LL_PROC_PROTO(proc_at_min)
{
	return ll_proc_dointvec(table, write, filp, buffer, lenp, ppos);
}
int LL_PROC_PROTO(proc_at_max)
{
	return ll_proc_dointvec(table, write, filp, buffer, lenp, ppos);
}
int LL_PROC_PROTO(proc_at_extra)
{
	return ll_proc_dointvec(table, write, filp, buffer, lenp, ppos);
}
int LL_PROC_PROTO(proc_at_early_margin)
{
	return ll_proc_dointvec(table, write, filp, buffer, lenp, ppos);
}
int LL_PROC_PROTO(proc_at_history)
{
	return ll_proc_dointvec(table, write, filp, buffer, lenp, ppos);
}

#ifdef CONFIG_SYSCTL
static ctl_table_t obd_table[] = {
	{
		INIT_CTL_NAME(OBD_TIMEOUT)
		.procname = "timeout",
		.data     = &obd_timeout,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_set_timeout
	},
	{
		INIT_CTL_NAME(OBD_DEBUG_PEER_ON_TIMEOUT)
		.procname = "debug_peer_on_timeout",
		.data     = &obd_debug_peer_on_timeout,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		INIT_CTL_NAME(OBD_DUMP_ON_TIMEOUT)
		.procname = "dump_on_timeout",
		.data     = &obd_dump_on_timeout,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		INIT_CTL_NAME(OBD_DUMP_ON_EVICTION)
		.procname = "dump_on_eviction",
		.data     = &obd_dump_on_eviction,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		INIT_CTL_NAME(OBD_MEMUSED)
		.procname = "memused",
		.data     = NULL,
		.maxlen   = 0,
		.mode     = 0444,
		.proc_handler = &proc_memory_alloc
	},
	{
		INIT_CTL_NAME(OBD_PAGESUSED)
		.procname = "pagesused",
		.data     = NULL,
		.maxlen   = 0,
		.mode     = 0444,
		.proc_handler = &proc_pages_alloc
	},
	{
		INIT_CTL_NAME(OBD_MAXMEMUSED)
		.procname = "memused_max",
		.data     = NULL,
		.maxlen   = 0,
		.mode     = 0444,
		.proc_handler = &proc_mem_max
	},
	{
		INIT_CTL_NAME(OBD_MAXPAGESUSED)
		.procname = "pagesused_max",
		.data     = NULL,
		.maxlen   = 0,
		.mode     = 0444,
		.proc_handler = &proc_pages_max
	},
	{
		INIT_CTL_NAME(OBD_LDLM_TIMEOUT)
		.procname = "ldlm_timeout",
		.data     = &ldlm_timeout,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_set_timeout
	},
	{
		INIT_CTL_NAME(OBD_ALLOC_FAIL_RATE)
		.procname = "alloc_fail_rate",
		.data     = &obd_alloc_fail_rate,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_alloc_fail_rate
	},
	{
		INIT_CTL_NAME(OBD_MAX_DIRTY_PAGES)
		.procname = "max_dirty_mb",
		.data     = &obd_max_dirty_pages,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_max_dirty_pages_in_mb
	},
	{
		INIT_CTL_NAME(OBD_AT_MIN)
		.procname = "at_min",
		.data     = &at_min,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_at_min
	},
	{
		INIT_CTL_NAME(OBD_AT_MAX)
		.procname = "at_max",
		.data     = &at_max,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_at_max
	},
	{
		INIT_CTL_NAME(OBD_AT_EXTRA)
		.procname = "at_extra",
		.data     = &at_extra,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_at_extra
	},
	{
		INIT_CTL_NAME(OBD_AT_EARLY_MARGIN)
		.procname = "at_early_margin",
		.data     = &at_early_margin,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_at_early_margin
	},
	{
		INIT_CTL_NAME(OBD_AT_HISTORY)
		.procname = "at_history",
		.data     = &at_history,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_at_history
	},
	{       INIT_CTL_NAME(0)    }
};

static ctl_table_t parent_table[] = {
	{
		INIT_CTL_NAME(OBD_SYSCTL)
		.procname = "lustre",
		.data     = NULL,
		.maxlen   = 0,
		.mode     = 0555,
		.child    = obd_table
	},
	{       INIT_CTL_NAME(0)   }
};
#endif

void obd_sysctl_init (void)
{
#ifdef CONFIG_SYSCTL
	if ( !obd_table_header )
		obd_table_header = cfs_register_sysctl_table(parent_table, 0);
#endif
}

void obd_sysctl_clean (void)
{
#ifdef CONFIG_SYSCTL
	if ( obd_table_header )
		unregister_sysctl_table(obd_table_header);
	obd_table_header = NULL;
#endif
}
