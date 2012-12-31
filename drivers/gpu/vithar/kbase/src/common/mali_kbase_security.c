/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_security.c
 * Base kernel security capability API
 */

#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>

/**
 * kbase_security_has_capability - see mali_kbase_caps.h for description.
 */

mali_bool kbase_security_has_capability(kbase_context *kctx, kbase_security_capability cap, u32 flags)
{
	/* Assume failure */
	mali_bool access_allowed = MALI_FALSE;
	mali_bool audit = (KBASE_SEC_FLAG_AUDIT & flags)? MALI_TRUE : MALI_FALSE;

	OSK_ASSERT(NULL != kctx);
	CSTD_UNUSED(kctx);

	/* Detect unsupported flags */
	OSK_ASSERT(((~KBASE_SEC_FLAG_MASK) & flags) == 0);

	/* Determine if access is allowed for the given cap */
	switch(cap)
	{
		case KBASE_SEC_MODIFY_PRIORITY:
#if KBASE_HWCNT_DUMP_BYPASS_ROOT
				access_allowed = TRUE;
#else
				if (osk_is_privileged() == MALI_TRUE)
				{
					access_allowed = TRUE;
				}
#endif
				break;
		case KBASE_SEC_INSTR_HW_COUNTERS_COLLECT:
				/* Access is granted only if the caller is privileged */
#if KBASE_HWCNT_DUMP_BYPASS_ROOT
				access_allowed = TRUE;
#else
				if (osk_is_privileged() == MALI_TRUE)
				{
					access_allowed = TRUE;
				}
#endif
				break;
	}

	/* Report problem if requested */
	if(MALI_FALSE == access_allowed)
	{
		if(MALI_FALSE != audit)
		{
			OSK_PRINT_WARN(OSK_BASE_CORE, "Security capability failure: %d, %p", cap, (void *)kctx);
		}
	}

	return access_allowed;
}

