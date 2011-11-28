/*
 * probes/lttng-types.c
 *
 * Copyright 2010 (c) - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng types.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include <linux/types.h>
#include "../wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "../ltt-events.h"
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
