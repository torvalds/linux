/* periodic_work.h
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

#ifndef __PERIODIC_WORK_H__
#define __PERIODIC_WORK_H__

#include "timskmod.h"



/* PERIODIC_WORK an opaque structure to users.
 * Fields are declared only in the implementation .c files.
 */
typedef struct PERIODIC_WORK_Tag PERIODIC_WORK;

PERIODIC_WORK *visor_periodic_work_create(ulong jiffy_interval,
					  struct workqueue_struct *workqueue,
					  void (*workfunc)(void *),
					  void *workfuncarg,
					  const char *devnam);
void            visor_periodic_work_destroy(PERIODIC_WORK *periodic_work);
BOOL            visor_periodic_work_nextperiod(PERIODIC_WORK *periodic_work);
BOOL            visor_periodic_work_start(PERIODIC_WORK *periodic_work);
BOOL            visor_periodic_work_stop(PERIODIC_WORK *periodic_work);

#endif
