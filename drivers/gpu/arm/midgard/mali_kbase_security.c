/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/**
 * @file mali_kbase_security.c
 * Base kernel security capability API
 */

#include <mali_kbase.h>

static inline bool kbasep_am_i_root(void)
{
#if KBASE_HWCNT_DUMP_BYPASS_ROOT
	return true;
#else
	/* Check if root */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
	if (uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return true;
#else
	if (current_euid() == 0)
		return true;
#endif /*LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)*/
	return false;
#endif /*KBASE_HWCNT_DUMP_BYPASS_ROOT*/
}

/**
 * kbase_security_has_capability - see mali_kbase_caps.h for description.
 */

bool kbase_security_has_capability(struct kbase_context *kctx, enum kbase_security_capability cap, u32 flags)
{
	/* Assume failure */
	bool access_allowed = false;
	bool audit = KBASE_SEC_FLAG_AUDIT & flags;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	CSTD_UNUSED(kctx);

	/* Detect unsupported flags */
	KBASE_DEBUG_ASSERT(((~KBASE_SEC_FLAG_MASK) & flags) == 0);

	/* Determine if access is allowed for the given cap */
	switch (cap) {
	case KBASE_SEC_MODIFY_PRIORITY:
	case KBASE_SEC_INSTR_HW_COUNTERS_COLLECT:
		/* Access is granted only if the caller is privileged */
		access_allowed = kbasep_am_i_root();
		break;
	}

	/* Report problem if requested */
	if (!access_allowed && audit)
		dev_warn(kctx->kbdev->dev, "Security capability failure: %d, %p", cap, (void *)kctx);

	return access_allowed;
}

KBASE_EXPORT_TEST_API(kbase_security_has_capability);
