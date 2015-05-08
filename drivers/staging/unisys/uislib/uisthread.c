/* uisthread.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* @ALL_INSPECTED */
#include <asm/processor.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include "uisutils.h"
#include "uisthread.h"

/* this is shorter than using __FILE__ (full path name) in
 * debug/info/error messages
 */
#define CURRENT_FILE_PC UISLIB_PC_uisthread_c
#define __MYFILE__ "uisthread.c"

/*****************************************************/
/* Exported functions                                */
/*****************************************************/

/* returns 0 for failure, 1 for success */
int
uisthread_start(struct uisthread_info *thrinfo,
		int (*threadfn)(void *), void *thrcontext, char *name)
{
	/* used to stop the thread */
	init_completion(&thrinfo->has_stopped);
	thrinfo->task = kthread_run(threadfn, thrcontext, name);
	if (IS_ERR(thrinfo->task)) {
		thrinfo->id = 0;
		return 0;	/* failure */
	}
	thrinfo->id = thrinfo->task->pid;
	return 1;
}
EXPORT_SYMBOL_GPL(uisthread_start);

void
uisthread_stop(struct uisthread_info *thrinfo)
{
	int stopped = 0;

	if (thrinfo->id == 0)
		return;		/* thread not running */

	kthread_stop(thrinfo->task);
	/* give up if the thread has NOT died in 1 minute */
	if (wait_for_completion_timeout(&thrinfo->has_stopped, 60 * HZ))
		stopped = 1;

	if (stopped)
		thrinfo->id = 0;
}
EXPORT_SYMBOL_GPL(uisthread_stop);
