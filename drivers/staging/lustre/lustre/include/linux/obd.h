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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LINUX_OBD_H
#define __LINUX_OBD_H

#ifndef __OBD_H
#error Do not #include this file directly. #include <obd.h> instead
#endif

#include <obd_support.h>

# include <linux/fs.h>
# include <linux/list.h>
# include <linux/sched.h>  /* for struct task_struct, for current.h */
# include <linux/proc_fs.h>
# include <linux/mount.h>
# include <linux/lustre_intent.h>

struct ll_iattr {
	struct iattr	iattr;
	unsigned int	ia_attr_flags;
};

#define CLIENT_OBD_LIST_LOCK_DEBUG 1

typedef struct {
	spinlock_t		lock;

	unsigned long       time;
	struct task_struct *task;
	const char	 *func;
	int		 line;
} client_obd_lock_t;

static inline void __client_obd_list_lock(client_obd_lock_t *lock,
					  const char *func, int line)
{
	unsigned long cur = jiffies;
	while (1) {
		if (spin_trylock(&lock->lock)) {
			LASSERT(lock->task == NULL);
			lock->task = current;
			lock->func = func;
			lock->line = line;
			lock->time = jiffies;
			break;
		}

		if (time_before(cur + 5 * HZ, jiffies) &&
		    time_before(lock->time + 5 * HZ, jiffies)) {
			struct task_struct *task = lock->task;

			if (task == NULL)
				continue;

			LCONSOLE_WARN("%s:%d: lock %p was acquired"
				      " by <%s:%d:%s:%d> for %lu seconds.\n",
				      current->comm, current->pid,
				      lock, task->comm, task->pid,
				      lock->func, lock->line,
				      (jiffies - lock->time) / HZ);
			LCONSOLE_WARN("====== for current process =====\n");
			dump_stack();
			LCONSOLE_WARN("====== end =======\n");
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1000 * HZ);
		}
		cpu_relax();
	}
}

#define client_obd_list_lock(lock) \
	__client_obd_list_lock(lock, __func__, __LINE__)

static inline void client_obd_list_unlock(client_obd_lock_t *lock)
{
	LASSERT(lock->task != NULL);
	lock->task = NULL;
	lock->time = jiffies;
	spin_unlock(&lock->lock);
}


static inline void client_obd_list_lock_init(client_obd_lock_t *lock)
{
	spin_lock_init(&lock->lock);
}

static inline void client_obd_list_lock_done(client_obd_lock_t *lock)
{}

#endif /* __LINUX_OBD_H */
