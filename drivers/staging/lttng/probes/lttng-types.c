/*
 * probes/lttng-types.c
 *
 * LTTng types.
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/types.h>
#include "../wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "../lttng-events.h"
#include "lttng-types.h"
#include <linux/hrtimer.h>

#define STAGE_EXPORT_ENUMS
#include "lttng-types.h"
#include "lttng-type-list.h"
#undef STAGE_EXPORT_ENUMS

struct lttng_enum lttng_enums[] = {
#define STAGE_EXPORT_TYPES
#include "lttng-types.h"
#include "lttng-type-list.h"
#undef STAGE_EXPORT_TYPES
};

static int lttng_types_init(void)
{
	int ret = 0;

	wrapper_vmalloc_sync_all();
	/* TODO */
	return ret;
}

module_init(lttng_types_init);

static void lttng_types_exit(void)
{
}

module_exit(lttng_types_exit);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers <mathieu.desnoyers@efficios.com>");
MODULE_DESCRIPTION("LTTng types");
