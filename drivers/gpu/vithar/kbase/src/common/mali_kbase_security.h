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
 * @file mali_kbase_security.h
 * Base kernel security capability APIs
 */

#ifndef _KBASE_SECURITY_H_
#define _KBASE_SECURITY_H_

/* Security flags */
#define KBASE_SEC_FLAG_NOAUDIT (0u << 0)              /* Silently handle privilege failure */
#define KBASE_SEC_FLAG_AUDIT   (1u << 0)              /* Write audit message on privilege failure */
#define KBASE_SEC_FLAG_MASK    (KBASE_SEC_FLAG_AUDIT) /* Mask of all valid flag bits */

/* List of unique capabilities that have security access privileges */
typedef enum {
		/* Instrumentation Counters access privilege */
        KBASE_SEC_INSTR_HW_COUNTERS_COLLECT = 1,
        KBASE_SEC_MODIFY_PRIORITY
		/* Add additional access privileges here */
} kbase_security_capability;


/**
 * kbase_security_has_capability - determine whether a task has a particular effective capability
 * @param[in]   kctx    The task context.
 * @param[in]   cap     The capability to check for.
 * @param[in]   flags   Additional configuration information
 *                      Such as whether to write an audit message or not.
 * @return MALI_TRUE if success (capability is allowed), MALI_FALSE otherwise.
 */

mali_bool kbase_security_has_capability(kbase_context *kctx, kbase_security_capability cap, u32 flags);

#endif /* _KBASE_SECURITY_H_ */

