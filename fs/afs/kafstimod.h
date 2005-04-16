/* kafstimod.h: AFS timeout daemon
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_KAFSTIMOD_H
#define _LINUX_AFS_KAFSTIMOD_H

#include "types.h"

struct afs_timer;

struct afs_timer_ops {
	/* called when the front of the timer queue has timed out */
	void (*timed_out)(struct afs_timer *timer);
};

/*****************************************************************************/
/*
 * AFS timer/timeout record
 */
struct afs_timer
{
	struct list_head		link;		/* link in timer queue */
	unsigned long			timo_jif;	/* timeout time */
	const struct afs_timer_ops	*ops;		/* timeout expiry function */
};

static inline void afs_timer_init(struct afs_timer *timer,
				  const struct afs_timer_ops *ops)
{
	INIT_LIST_HEAD(&timer->link);
	timer->ops = ops;
}

extern int afs_kafstimod_start(void);
extern void afs_kafstimod_stop(void);

extern void afs_kafstimod_add_timer(struct afs_timer *timer,
				    unsigned long timeout);
extern int afs_kafstimod_del_timer(struct afs_timer *timer);

#endif /* _LINUX_AFS_KAFSTIMOD_H */
