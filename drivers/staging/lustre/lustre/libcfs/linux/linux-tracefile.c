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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LNET
#define LUSTRE_TRACEFILE_PRIVATE

#include "../../../include/linux/libcfs/libcfs.h"
#include "tracefile.h"

/* percents to share the total debug memory for each type */
static unsigned int pages_factor[CFS_TCD_TYPE_MAX] = {
	80,  /* 80% pages for CFS_TCD_TYPE_PROC */
	10,  /* 10% pages for CFS_TCD_TYPE_SOFTIRQ */
	10   /* 10% pages for CFS_TCD_TYPE_IRQ */
};

char *cfs_trace_console_buffers[NR_CPUS][CFS_TCD_TYPE_MAX];

struct rw_semaphore cfs_tracefile_sem;

int cfs_tracefile_init_arch(void)
{
	int    i;
	int    j;
	struct cfs_trace_cpu_data *tcd;

	init_rwsem(&cfs_tracefile_sem);

	/* initialize trace_data */
	memset(cfs_trace_data, 0, sizeof(cfs_trace_data));
	for (i = 0; i < CFS_TCD_TYPE_MAX; i++) {
		cfs_trace_data[i] =
			kmalloc(sizeof(union cfs_trace_data_union) *
				num_possible_cpus(), GFP_KERNEL);
		if (cfs_trace_data[i] == NULL)
			goto out;

	}

	/* arch related info initialized */
	cfs_tcd_for_each(tcd, i, j) {
		spin_lock_init(&tcd->tcd_lock);
		tcd->tcd_pages_factor = pages_factor[i];
		tcd->tcd_type = i;
		tcd->tcd_cpu = j;
	}

	for (i = 0; i < num_possible_cpus(); i++)
		for (j = 0; j < 3; j++) {
			cfs_trace_console_buffers[i][j] =
				kmalloc(CFS_TRACE_CONSOLE_BUFFER_SIZE,
					GFP_KERNEL);

			if (cfs_trace_console_buffers[i][j] == NULL)
				goto out;
		}

	return 0;

out:
	cfs_tracefile_fini_arch();
	printk(KERN_ERR "lnet: Not enough memory\n");
	return -ENOMEM;
}

void cfs_tracefile_fini_arch(void)
{
	int    i;
	int    j;

	for (i = 0; i < num_possible_cpus(); i++)
		for (j = 0; j < 3; j++)
			if (cfs_trace_console_buffers[i][j] != NULL) {
				kfree(cfs_trace_console_buffers[i][j]);
				cfs_trace_console_buffers[i][j] = NULL;
			}

	for (i = 0; cfs_trace_data[i] != NULL; i++) {
		kfree(cfs_trace_data[i]);
		cfs_trace_data[i] = NULL;
	}

	fini_rwsem(&cfs_tracefile_sem);
}

void cfs_tracefile_read_lock(void)
{
	down_read(&cfs_tracefile_sem);
}

void cfs_tracefile_read_unlock(void)
{
	up_read(&cfs_tracefile_sem);
}

void cfs_tracefile_write_lock(void)
{
	down_write(&cfs_tracefile_sem);
}

void cfs_tracefile_write_unlock(void)
{
	up_write(&cfs_tracefile_sem);
}

cfs_trace_buf_type_t cfs_trace_buf_idx_get(void)
{
	if (in_irq())
		return CFS_TCD_TYPE_IRQ;
	else if (in_softirq())
		return CFS_TCD_TYPE_SOFTIRQ;
	else
		return CFS_TCD_TYPE_PROC;
}

/*
 * The walking argument indicates the locking comes from all tcd types
 * iterator and we must lock it and dissable local irqs to avoid deadlocks
 * with other interrupt locks that might be happening. See LU-1311
 * for details.
 */
int cfs_trace_lock_tcd(struct cfs_trace_cpu_data *tcd, int walking)
{
	__LASSERT(tcd->tcd_type < CFS_TCD_TYPE_MAX);
	if (tcd->tcd_type == CFS_TCD_TYPE_IRQ)
		spin_lock_irqsave(&tcd->tcd_lock, tcd->tcd_lock_flags);
	else if (tcd->tcd_type == CFS_TCD_TYPE_SOFTIRQ)
		spin_lock_bh(&tcd->tcd_lock);
	else if (unlikely(walking))
		spin_lock_irq(&tcd->tcd_lock);
	else
		spin_lock(&tcd->tcd_lock);
	return 1;
}

void cfs_trace_unlock_tcd(struct cfs_trace_cpu_data *tcd, int walking)
{
	__LASSERT(tcd->tcd_type < CFS_TCD_TYPE_MAX);
	if (tcd->tcd_type == CFS_TCD_TYPE_IRQ)
		spin_unlock_irqrestore(&tcd->tcd_lock, tcd->tcd_lock_flags);
	else if (tcd->tcd_type == CFS_TCD_TYPE_SOFTIRQ)
		spin_unlock_bh(&tcd->tcd_lock);
	else if (unlikely(walking))
		spin_unlock_irq(&tcd->tcd_lock);
	else
		spin_unlock(&tcd->tcd_lock);
}

int cfs_tcd_owns_tage(struct cfs_trace_cpu_data *tcd,
		      struct cfs_trace_page *tage)
{
	/*
	 * XXX nikita: do NOT call portals_debug_msg() (CDEBUG/ENTRY/EXIT)
	 * from here: this will lead to infinite recursion.
	 */
	return tcd->tcd_cpu == tage->cpu;
}

void
cfs_set_ptldebug_header(struct ptldebug_header *header,
			struct libcfs_debug_msg_data *msgdata,
			unsigned long stack)
{
	struct timeval tv;

	do_gettimeofday(&tv);

	header->ph_subsys = msgdata->msg_subsys;
	header->ph_mask = msgdata->msg_mask;
	header->ph_cpu_id = smp_processor_id();
	header->ph_type = cfs_trace_buf_idx_get();
	header->ph_sec = (__u32)tv.tv_sec;
	header->ph_usec = tv.tv_usec;
	header->ph_stack = stack;
	header->ph_pid = current->pid;
	header->ph_line_num = msgdata->msg_line;
	header->ph_extern_pid = 0;
	return;
}

static char *
dbghdr_to_err_string(struct ptldebug_header *hdr)
{
	switch (hdr->ph_subsys) {

		case S_LND:
		case S_LNET:
			return "LNetError";
		default:
			return "LustreError";
	}
}

static char *
dbghdr_to_info_string(struct ptldebug_header *hdr)
{
	switch (hdr->ph_subsys) {

		case S_LND:
		case S_LNET:
			return "LNet";
		default:
			return "Lustre";
	}
}

void cfs_print_to_console(struct ptldebug_header *hdr, int mask,
			  const char *buf, int len, const char *file,
			  const char *fn)
{
	char *prefix = "Lustre", *ptype = NULL;

	if ((mask & D_EMERG) != 0) {
		prefix = dbghdr_to_err_string(hdr);
		ptype = KERN_EMERG;
	} else if ((mask & D_ERROR) != 0) {
		prefix = dbghdr_to_err_string(hdr);
		ptype = KERN_ERR;
	} else if ((mask & D_WARNING) != 0) {
		prefix = dbghdr_to_info_string(hdr);
		ptype = KERN_WARNING;
	} else if ((mask & (D_CONSOLE | libcfs_printk)) != 0) {
		prefix = dbghdr_to_info_string(hdr);
		ptype = KERN_INFO;
	}

	if ((mask & D_CONSOLE) != 0) {
		printk("%s%s: %.*s", ptype, prefix, len, buf);
	} else {
		printk("%s%s: %d:%d:(%s:%d:%s()) %.*s", ptype, prefix,
		       hdr->ph_pid, hdr->ph_extern_pid, file, hdr->ph_line_num,
		       fn, len, buf);
	}
	return;
}

int cfs_trace_max_debug_mb(void)
{
	int  total_mb = (totalram_pages >> (20 - PAGE_SHIFT));

	return MAX(512, (total_mb * 80)/100);
}
