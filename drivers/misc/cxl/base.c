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
#include <linux/of_platform.h>
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

/* AFU refcount management */
struct cxl_afu *cxl_afu_get(struct cxl_afu *afu)
{
	return (get_device(&afu->dev) == NULL) ? NULL : afu;
}
EXPORT_SYMBOL_GPL(cxl_afu_get);

void cxl_afu_put(struct cxl_afu *afu)
{
	put_device(&afu->dev);
}
EXPORT_SYMBOL_GPL(cxl_afu_put);

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

int cxl_update_properties(struct device_node *dn,
			  struct property *new_prop)
{
	return of_update_property(dn, new_prop);
}
EXPORT_SYMBOL_GPL(cxl_update_properties);

/*
 * API calls into the driver that may be called from the PHB code and must be
 * built in.
 */
bool cxl_pci_associate_default_context(struct pci_dev *dev, struct cxl_afu *afu)
{
	bool ret;
	struct cxl_calls *calls;

	calls = cxl_calls_get();
	if (!calls)
		return false;

	ret = calls->cxl_pci_associate_default_context(dev, afu);

	cxl_calls_put(calls);

	return ret;
}
EXPORT_SYMBOL_GPL(cxl_pci_associate_default_context);

void cxl_pci_disable_device(struct pci_dev *dev)
{
	struct cxl_calls *calls;

	calls = cxl_calls_get();
	if (!calls)
		return;

	calls->cxl_pci_disable_device(dev);

	cxl_calls_put(calls);
}
EXPORT_SYMBOL_GPL(cxl_pci_disable_device);

static int __init cxl_base_init(void)
{
	struct device_node *np;
	struct platform_device *dev;
	int count = 0;

	/*
	 * Scan for compatible devices in guest only
	 */
	if (cpu_has_feature(CPU_FTR_HVMODE))
		return 0;

	for_each_compatible_node(np, NULL, "ibm,coherent-platform-facility") {
		dev = of_platform_device_create(np, NULL, NULL);
		if (dev)
			count++;
	}
	pr_devel("Found %d cxl device(s)\n", count);
	return 0;
}
device_initcall(cxl_base_init);
