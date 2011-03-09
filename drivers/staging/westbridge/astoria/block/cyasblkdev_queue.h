/* cyanblkdev_queue.h - Antioch Linux Block Driver queue header file
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef _INCLUDED_CYANBLKDEV_QUEUE_H_
#define _INCLUDED_CYANBLKDEV_QUEUE_H_

/*
 * may contain various useful MACRO and debug printks
 */

/* moved to staging location, eventual implementation
 * considered is here
 * #include <linux/westbridge/cyashal.h>
 * #include <linux/westbridge/cyastoria.h>
 * */

#include "../include/linux/westbridge/cyashal.h"
#include "../include/linux/westbridge/cyastoria.h"

struct request;
struct task_struct;

struct cyasblkdev_queue {
	struct completion	thread_complete;
	wait_queue_head_t	thread_wq;
	struct semaphore	thread_sem;
	unsigned int	flags;
	struct request	*req;
	int	(*prep_fn)(struct cyasblkdev_queue *, struct request *);
	int	(*issue_fn)(struct cyasblkdev_queue *, struct request *);
	void		*data;
	struct request_queue *queue;
};

extern int cyasblkdev_init_queue(struct cyasblkdev_queue *, spinlock_t *);
extern void cyasblkdev_cleanup_queue(struct cyasblkdev_queue *);
extern void cyasblkdev_queue_suspend(struct cyasblkdev_queue *);
extern void cyasblkdev_queue_resume(struct cyasblkdev_queue *);

extern cy_as_device_handle cyasdevice_getdevhandle(void);
#define MOD_LOGS 1
void verbose_rq_flags(int flags);

#endif /* _INCLUDED_CYANBLKDEV_QUEUE_H_ */

/*[]*/
