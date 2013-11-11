/*
 * include/linux/sw_sync.h
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_SW_SYNC_H
#define _LINUX_SW_SYNC_H

#include <linux/types.h>
#include <linux/kconfig.h>
#include "sync.h"
#include "uapi/sw_sync.h"

struct sw_sync_timeline {
	struct	sync_timeline	obj;

	u32			value;
};

struct sw_sync_pt {
	struct sync_pt		pt;

	u32			value;
};

#if IS_ENABLED(CONFIG_SW_SYNC)
struct sw_sync_timeline *sw_sync_timeline_create(const char *name);
void sw_sync_timeline_inc(struct sw_sync_timeline *obj, u32 inc);

struct sync_pt *sw_sync_pt_create(struct sw_sync_timeline *obj, u32 value);
#else
static inline struct sw_sync_timeline *sw_sync_timeline_create(const char *name)
{
	return NULL;
}

static inline void sw_sync_timeline_inc(struct sw_sync_timeline *obj, u32 inc)
{
}

static inline struct sync_pt *sw_sync_pt_create(struct sw_sync_timeline *obj,
		u32 value)
{
	return NULL;
}
#endif /* IS_ENABLED(CONFIG_SW_SYNC) */

#endif /* _LINUX_SW_SYNC_H */
