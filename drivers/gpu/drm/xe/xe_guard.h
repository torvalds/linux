/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_GUARD_H_
#define _XE_GUARD_H_

#include <linux/spinlock.h>

/**
 * struct xe_guard - Simple logic to protect a feature.
 *
 * Implements simple semaphore-like logic that can be used to lockdown the
 * feature unless it is already in use.  Allows enabling of the otherwise
 * incompatible features, where we can't follow the strict owner semantics
 * required by the &rw_semaphore.
 *
 * NOTE! It shouldn't be used to protect a data, use &rw_semaphore instead.
 */
struct xe_guard {
	/**
	 * @counter: implements simple exclusive/lockdown logic:
	 *           if == 0 then guard/feature is idle/not in use,
	 *           if < 0 then feature is active and can't be locked-down,
	 *           if > 0 then feature is lockded-down and can't be activated.
	 */
	int counter;

	/** @name: the name of the guard (useful for debug) */
	const char *name;

	/** @owner: the info about the last owner of the guard (for debug) */
	void *owner;

	/** @lock: protects guard's data */
	spinlock_t lock;
};

/**
 * xe_guard_init() - Initialize the guard.
 * @guard: the &xe_guard to init
 * @name: name of the guard
 */
static inline void xe_guard_init(struct xe_guard *guard, const char *name)
{
	spin_lock_init(&guard->lock);
	guard->counter = 0;
	guard->name = name;
}

/**
 * xe_guard_arm() - Arm the guard for the exclusive/lockdown mode.
 * @guard: the &xe_guard to arm
 * @lockdown: arm for lockdown(true) or exclusive(false) mode
 * @who: optional owner info (for debug only)
 *
 * Multiple lockdown requests are allowed.
 * Only single exclusive access can be granted.
 * Will fail if the guard is already in exclusive mode.
 * On success, must call the xe_guard_disarm() to release.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static inline int xe_guard_arm(struct xe_guard *guard, bool lockdown, void *who)
{
	guard(spinlock)(&guard->lock);

	if (lockdown) {
		if (guard->counter < 0)
			return -EBUSY;
		guard->counter++;
	} else {
		if (guard->counter > 0)
			return -EPERM;
		if (guard->counter < 0)
			return -EUSERS;
		guard->counter--;
	}

	guard->owner = who;
	return 0;
}

/**
 * xe_guard_disarm() - Disarm the guard from exclusive/lockdown mode.
 * @guard: the &xe_guard to disarm
 * @lockdown: disarm from lockdown(true) or exclusive(false) mode
 *
 * Return: true if successfully disarmed or false in case of mismatch.
 */
static inline bool xe_guard_disarm(struct xe_guard *guard, bool lockdown)
{
	guard(spinlock)(&guard->lock);

	if (lockdown) {
		if (guard->counter <= 0)
			return false;
		guard->counter--;
	} else {
		if (guard->counter != -1)
			return false;
		guard->counter++;
	}
	return true;
}

/**
 * xe_guard_mode_str() - Convert guard mode into a string.
 * @lockdown: flag used to select lockdown or exclusive mode
 *
 * Return: "lockdown" or "exclusive" string.
 */
static inline const char *xe_guard_mode_str(bool lockdown)
{
	return lockdown ? "lockdown" : "exclusive";
}

#endif
