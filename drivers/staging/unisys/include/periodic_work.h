/* periodic_work.h
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

#ifndef __PERIODIC_WORK_H__
#define __PERIODIC_WORK_H__

#include <linux/seq_file.h>
#include <linux/slab.h>


/* PERIODIC_WORK an opaque structure to users.
 * Fields are declared only in the implementation .c files.
 */
struct periodic_work;

struct periodic_work *visor_periodic_work_create(ulong jiffy_interval,
					struct workqueue_struct *workqueue,
					void (*workfunc)(void *),
					void *workfuncarg,
					const char *devnam);
void visor_periodic_work_destroy(struct periodic_work *pw);
bool visor_periodic_work_nextperiod(struct periodic_work *pw);
bool visor_periodic_work_start(struct periodic_work *pw);
bool visor_periodic_work_stop(struct periodic_work *pw);

#endif
