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
 *
 * libcfs/libcfs/linux/linux-debug.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 */

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/notifier.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>

# define DEBUG_SUBSYSTEM S_LNET

#include "../../../include/linux/libcfs/libcfs.h"

#include "../tracefile.h"

#include <linux/kallsyms.h>

char lnet_upcall[1024] = "/usr/lib/lustre/lnet_upcall";
char lnet_debug_log_upcall[1024] = "/usr/lib/lustre/lnet_debug_log_upcall";

/**
 * Upcall function once a Lustre log has been dumped.
 *
 * \param file  path of the dumped log
 */
void libcfs_run_debug_log_upcall(char *file)
{
	char *argv[3];
	int   rc;
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		NULL};

	argv[0] = lnet_debug_log_upcall;

	LASSERTF(file != NULL, "called on a null filename\n");
	argv[1] = file; /* only need to pass the path of the file */

	argv[2] = NULL;

	rc = call_usermodehelper(argv[0], argv, envp, 1);
	if (rc < 0 && rc != -ENOENT) {
		CERROR("Error %d invoking LNET debug log upcall %s %s; "
		       "check /proc/sys/lnet/debug_log_upcall\n",
		       rc, argv[0], argv[1]);
	} else {
		CDEBUG(D_HA, "Invoked LNET debug log upcall %s %s\n",
		       argv[0], argv[1]);
	}
}

void libcfs_run_upcall(char **argv)
{
	int   rc;
	int   argc;
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		NULL};

	argv[0] = lnet_upcall;
	argc = 1;
	while (argv[argc] != NULL)
		argc++;

	LASSERT(argc >= 2);

	rc = call_usermodehelper(argv[0], argv, envp, 1);
	if (rc < 0 && rc != -ENOENT) {
		CERROR("Error %d invoking LNET upcall %s %s%s%s%s%s%s%s%s; "
		       "check /proc/sys/lnet/upcall\n",
		       rc, argv[0], argv[1],
		       argc < 3 ? "" : ",", argc < 3 ? "" : argv[2],
		       argc < 4 ? "" : ",", argc < 4 ? "" : argv[3],
		       argc < 5 ? "" : ",", argc < 5 ? "" : argv[4],
		       argc < 6 ? "" : ",...");
	} else {
		CDEBUG(D_HA, "Invoked LNET upcall %s %s%s%s%s%s%s%s%s\n",
		       argv[0], argv[1],
		       argc < 3 ? "" : ",", argc < 3 ? "" : argv[2],
		       argc < 4 ? "" : ",", argc < 4 ? "" : argv[3],
		       argc < 5 ? "" : ",", argc < 5 ? "" : argv[4],
		       argc < 6 ? "" : ",...");
	}
}

void libcfs_run_lbug_upcall(struct libcfs_debug_msg_data *msgdata)
{
	char *argv[6];
	char buf[32];

	snprintf(buf, sizeof(buf), "%d", msgdata->msg_line);

	argv[1] = "LBUG";
	argv[2] = (char *)msgdata->msg_file;
	argv[3] = (char *)msgdata->msg_fn;
	argv[4] = buf;
	argv[5] = NULL;

	libcfs_run_upcall (argv);
}

/* coverity[+kill] */
void lbug_with_loc(struct libcfs_debug_msg_data *msgdata)
{
	libcfs_catastrophe = 1;
	libcfs_debug_msg(msgdata, "LBUG\n");

	if (in_interrupt()) {
		panic("LBUG in interrupt.\n");
		/* not reached */
	}

	dump_stack();
	if (!libcfs_panic_on_lbug)
		libcfs_debug_dumplog();
	libcfs_run_lbug_upcall(msgdata);
	if (libcfs_panic_on_lbug)
		panic("LBUG");
	set_task_state(current, TASK_UNINTERRUPTIBLE);
	while (1)
		schedule();
}

static int panic_notifier(struct notifier_block *self, unsigned long unused1,
			 void *unused2)
{
	if (libcfs_panic_in_progress)
		return 0;

	libcfs_panic_in_progress = 1;
	mb();

	return 0;
}

static struct notifier_block libcfs_panic_notifier = {
	.notifier_call	= panic_notifier,
	.next		= NULL,
	.priority	= 10000,
};

void libcfs_register_panic_notifier(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &libcfs_panic_notifier);
}

void libcfs_unregister_panic_notifier(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &libcfs_panic_notifier);
}

EXPORT_SYMBOL(libcfs_run_upcall);
EXPORT_SYMBOL(libcfs_run_lbug_upcall);
EXPORT_SYMBOL(lbug_with_loc);
