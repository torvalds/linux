/*
 * governor.h - internal header for devfreq governors.
 *
 * Copyright (C) 2011 Samsung Electronics
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This header is for devfreq governors in drivers/devfreq/
 */

#ifndef _GOVERNOR_H
#define _GOVERNOR_H

#include <linux/devfreq.h>

#define to_devfreq(DEV)	container_of((DEV), struct devfreq, dev)

/* Caution: devfreq->lock must be locked before calling update_devfreq */
extern int update_devfreq(struct devfreq *devfreq);

#endif /* _GOVERNOR_H */
