/*
 * Hardware spinlocks internal header
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com
 *
 * Contact: Ohad Ben-Cohen <ohad@wizery.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __HWSPINLOCK_HWSPINLOCK_H
#define __HWSPINLOCK_HWSPINLOCK_H

#include <linux/spinlock.h>
#include <linux/device.h>

/**
 * struct hwspinlock_ops - platform-specific hwspinlock handlers
 *
 * @trylock: make a single attempt to take the lock. returns 0 on
 *	     failure and true on success. may _not_ sleep.
 * @unlock:  release the lock. always succeed. may _not_ sleep.
 * @relax:   optional, platform-specific relax handler, called by hwspinlock
 *	     core while spinning on a lock, between two successive
 *	     invocations of @trylock. may _not_ sleep.
 */
struct hwspinlock_ops {
	int (*trylock)(struct hwspinlock *lock);
	void (*unlock)(struct hwspinlock *lock);
	void (*relax)(struct hwspinlock *lock);
};

/**
 * struct hwspinlock - this struct represents a single hwspinlock instance
 *
 * @dev: underlying device, will be used to invoke runtime PM api
 * @ops: platform-specific hwspinlock handlers
 * @id: a global, unique, system-wide, index of the lock.
 * @lock: initialized and used by hwspinlock core
 * @owner: underlying implementation module, used to maintain module ref count
 *
 * Note: currently simplicity was opted for, but later we can squeeze some
 * memory bytes by grouping the dev, ops and owner members in a single
 * per-platform struct, and have all hwspinlocks point at it.
 */
struct hwspinlock {
	struct device *dev;
	const struct hwspinlock_ops *ops;
	int id;
	spinlock_t lock;
	struct module *owner;
};

#endif /* __HWSPINLOCK_HWSPINLOCK_H */
