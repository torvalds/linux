/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_ENGINE_CLASS_SYSFS_H_
#define _XE_ENGINE_CLASS_SYSFS_H_

#include <linux/kobject.h>

struct xe_gt;
struct xe_hw_engine_class_intf;

int xe_hw_engine_class_sysfs_init(struct xe_gt *gt);
bool xe_hw_engine_timeout_in_range(u64 timeout, u64 min, u64 max);

/**
 * struct kobj_eclass - A eclass's kobject struct that connects the kobject and the
 * eclass.
 *
 * When dealing with multiple eclass, this struct helps to understand which eclass
 * needs to be addressed on a given sysfs call.
 */
struct kobj_eclass {
	/** @base: The actual kobject */
	struct kobject base;
	/** @eclass: A pointer to the hw engine class interface */
	struct xe_hw_engine_class_intf *eclass;
};

static inline struct xe_hw_engine_class_intf *kobj_to_eclass(struct kobject *kobj)
{
	return container_of(kobj, struct kobj_eclass, base)->eclass;
}

#endif
