/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_IOSF_MBI_H__
#define __I915_IOSF_MBI_H__

#if IS_ENABLED(CONFIG_IOSF_MBI)
#include <asm/iosf_mbi.h>
#else

/* Stubs to compile for all non-x86 archs */
#define MBI_PMIC_BUS_ACCESS_BEGIN       1
#define MBI_PMIC_BUS_ACCESS_END         2

struct notifier_block;

static inline void iosf_mbi_punit_acquire(void) {}
static inline void iosf_mbi_punit_release(void) {}
static inline void iosf_mbi_assert_punit_acquired(void) {}

static inline
int iosf_mbi_register_pmic_bus_access_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int
iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(struct notifier_block *nb)
{
	return 0;
}
#endif

#endif /* __I915_IOSF_MBI_H__ */
