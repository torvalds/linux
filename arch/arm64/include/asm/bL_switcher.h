/*
 * Based on the stubs for the ARM implementation which is:
 *
 * Created by:  Nicolas Pitre, April 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_BL_SWITCHER_H
#define ASM_BL_SWITCHER_H

#include <linux/notifier.h>
#include <linux/types.h>

typedef void (*bL_switch_completion_handler)(void *cookie);

static inline int bL_switch_request(unsigned int cpu,
				    unsigned int new_cluster_id)
{
	return -ENOTSUPP;
}

/*
 * Register here to be notified about runtime enabling/disabling of
 * the switcher.
 *
 * The notifier chain is called with the switcher activation lock held:
 * the switcher will not be enabled or disabled during callbacks.
 * Callbacks must not call bL_switcher_{get,put}_enabled().
 */
#define BL_NOTIFY_PRE_ENABLE	0
#define BL_NOTIFY_POST_ENABLE	1
#define BL_NOTIFY_PRE_DISABLE	2
#define BL_NOTIFY_POST_DISABLE	3

static inline int bL_switcher_register_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int bL_switcher_unregister_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline bool bL_switcher_get_enabled(void) { return false; }
static inline void bL_switcher_put_enabled(void) { }
static inline int bL_switcher_trace_trigger(void) { return 0; }
static inline int bL_switcher_get_logical_index(u32 mpidr) { return -EUNATCH; }

#endif
