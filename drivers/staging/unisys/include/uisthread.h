/* uisthread.h
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

/*****************************************************************************/
/* Unisys thread utilities header                                            */
/*****************************************************************************/


#ifndef __UISTHREAD_H__
#define __UISTHREAD_H__


#include "linux/completion.h"

struct uisthread_info {
	struct task_struct *task;
	int id;
	int should_stop;
	struct completion has_stopped;
};


/* returns 0 for failure, 1 for success */
int uisthread_start(
	struct uisthread_info *thrinfo,
	int (*threadfn)(void *),
	void *thrcontext,
	char *name);

void uisthread_stop(struct uisthread_info *thrinfo);

#endif /* __UISTHREAD_H__ */
