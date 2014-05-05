/* uisthread.c
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
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
#include "uniklog.h"
#include "uisutils.h"
#include "uisthread.h"

#define KILL(a, b, c) kill_pid(find_vpid(a), b, c)

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
	thrinfo->should_stop = 0;
	/* used to stop the thread */
	init_completion(&thrinfo->has_stopped);
	thrinfo->task = kthread_create(threadfn, thrcontext, name, NULL);
	if (IS_ERR(thrinfo->task)) {
		thrinfo->id = 0;
		return 0;	/* failure */
	}
	thrinfo->id = thrinfo->task->pid;
	wake_up_process(thrinfo->task);
	LOGINF("started thread pid:%d\n", thrinfo->id);
	return 1;

}
EXPORT_SYMBOL_GPL(uisthread_start);

void
uisthread_stop(struct uisthread_info *thrinfo)
{
	int ret;
	int stopped = 0;
	if (thrinfo->id == 0)
		return;		/* thread not running */

	LOGINF("uisthread_stop stopping id:%d\n", thrinfo->id);
	thrinfo->should_stop = 1;
	ret = KILL(thrinfo->id, SIGHUP, 1);
	if (ret) {
		LOGERR("unable to signal thread %d\n", ret);
	} else {
		/* give up if the thread has NOT died in 1 minute */
		if (wait_for_completion_timeout(&thrinfo->has_stopped, 60 * HZ))
			stopped = 1;
		else
			LOGERR("timed out trying to signal thread\n");
	}
	if (stopped) {
		LOGINF("uisthread_stop stopped id:%d\n", thrinfo->id);
		thrinfo->id = 0;
	}
}
EXPORT_SYMBOL_GPL(uisthread_stop);
