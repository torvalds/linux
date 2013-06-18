/*
 * arch/arm/include/asm/bL_switcher.h
 *
 * Created by:  Nicolas Pitre, April 2012
 * Copyright:   (C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_BL_SWITCHER_H
#define ASM_BL_SWITCHER_H

#include <linux/compiler.h>
#include <linux/types.h>

typedef void (*bL_switch_completion_handler)(void *cookie);

int bL_switch_request_cb(unsigned int cpu, unsigned int new_cluster_id,
			 bL_switch_completion_handler completer,
			 void *completer_cookie);
static inline int bL_switch_request(unsigned int cpu, unsigned int new_cluster_id)
{
	return bL_switch_request_cb(cpu, new_cluster_id, NULL, NULL);
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

#ifdef CONFIG_BL_SWITCHER

void bL_switch_request_detach(unsigned int cpu,
			      bL_switch_completion_handler completer);

int bL_switcher_register_notifier(struct notifier_block *nb);
int bL_switcher_unregister_notifier(struct notifier_block *nb);

/*
 * Use these functions to temporarily prevent enabling/disabling of
 * the switcher.
 * bL_switcher_get_enabled() returns true if the switcher is currently
 * enabled.  Each call to bL_switcher_get_enabled() must be followed
 * by a call to bL_switcher_put_enabled().  These functions are not
 * recursive.
 */
bool bL_switcher_get_enabled(void);
void bL_switcher_put_enabled(void);

#else
static void bL_switch_request_detach(unsigned int cpu,
				     bL_switch_completion_handler completer) { }

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
#endif /* CONFIG_BL_SWITCHER */

#endif
