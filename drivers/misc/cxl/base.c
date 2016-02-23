/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/rcupdate.h>
#include <asm/errno.h>
#include <misc/cxl-base.h>
#include "cxl.h"

/* protected by rcu */
static struct cxl_calls *cxl_calls;

atomic_t cxl_use_count = ATOMIC_INIT(0);
EXPORT_SYMBOL(cxl_use_count);

#ifdef CONFIG_CXL_MODULE

static inline struct cxl_calls *cxl_calls_get(void)
{
	struct cxl_calls *calls = NULL;

	rcu_read_lock();
	calls = rcu_dereference(cxl_calls);
	if (calls && !try_module_get(calls->owner))
		calls = NULL;
	rcu_read_unlock();

	return calls;
}

static inline void cxl_calls_put(struct cxl_calls *calls)
{
	BUG_ON(calls != cxl_calls);

	/* we don't need to rcu this, as we hold a reference to the module */
	module_put(cxl_calls->owner);
}

#else /* !defined CONFIG_CXL_MODULE */

static inline struct cxl_calls *cxl_calls_get(void)
{
	return cxl_calls;
}

static inline void cxl_calls_put(struct cxl_calls *calls) { }

#endif /* CONFIG_CXL_MODULE */

void cxl_slbia(struct mm_struct *mm)
{
	struct cxl_calls *calls;

	calls = cxl_calls_get();
	if (!calls)
		return;

	if (cxl_ctx_in_use())
	    calls->cxl_slbia(mm);

	cxl_calls_put(calls);
}

int register_cxl_calls(struct cxl_calls *calls)
{
	if (cxl_calls)
		return -EBUSY;

	rcu_assign_pointer(cxl_calls, calls);
	return 0;
}
EXPORT_SYMBOL_GPL(register_cxl_calls);

void unregister_cxl_calls(struct cxl_calls *calls)
{
	BUG_ON(cxl_calls->owner != calls->owner);
	RCU_INIT_POINTER(cxl_calls, NULL);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(unregister_cxl_calls);
