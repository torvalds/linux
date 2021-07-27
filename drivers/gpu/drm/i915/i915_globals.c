/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/slab.h>
#include <linux/workqueue.h>

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_object.h"
#include "i915_globals.h"
#include "i915_request.h"
#include "i915_scheduler.h"
#include "i915_vma.h"

static LIST_HEAD(globals);

void __init i915_global_register(struct i915_global *global)
{
	GEM_BUG_ON(!global->exit);

	list_add_tail(&global->link, &globals);
}

static void __i915_globals_cleanup(void)
{
	struct i915_global *global, *next;

	list_for_each_entry_safe_reverse(global, next, &globals, link)
		global->exit();
}

static __initconst int (* const initfn[])(void) = {
	i915_global_context_init,
	i915_global_gem_context_init,
	i915_global_objects_init,
	i915_global_request_init,
	i915_global_scheduler_init,
	i915_global_vma_init,
};

int __init i915_globals_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(initfn); i++) {
		int err;

		err = initfn[i]();
		if (err) {
			__i915_globals_cleanup();
			return err;
		}
	}

	return 0;
}

void i915_globals_exit(void)
{
	__i915_globals_cleanup();
}
