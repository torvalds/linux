/*
 *
 * (C) COPYRIGHT 2008-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_osk_credentials.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_CREDENTIALS_H_
#define _OSK_CREDENTIALS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @defgroup oskcredentials Credentials  Access
 */
/** @{ */

/** @brief Check if the caller is privileged.
 *
 * @return MALI_TRUE if the caller is privileged.
 */
OSK_STATIC_INLINE mali_bool osk_is_privileged(void);

#define OSK_PROCESS_PRIORITY_MIN ( -20 )
#define OSK_PROCESS_PRIORITY_MAX ( 19 )

typedef struct osk_process_priority
{
	/* MALI_TRUE if process is using a realtime scheduling policy */
	mali_bool is_realtime;
	/* The process priority in the range of OSK_PROCESS_PRIORITY_MIN
	   and OSK_PROCESS_PRIORITY_MAX. */
	int priority;
} osk_process_priority;

/** @brief Check if the caller is using a realtime scheduling policy
 *
 * @return MALI_TRUE if process is running a realtime policy.
 */
OSK_STATIC_INLINE mali_bool osk_is_policy_realtime(void);

/** @brief Retrieve the calling process priority and policy
 *
 *  @param[out] prio    structure to contain the process policy type
 *                      and priority number
 */
OSK_STATIC_INLINE void osk_get_process_priority(osk_process_priority *prio);

/** @} */ /* end group oskcredentials */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_credentials.h>

#ifdef __cplusplus
}
#endif

#endif /* _OSK_CREDENTIALS_H_ */
