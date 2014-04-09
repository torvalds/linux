/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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
 * @file mali_ukk_os.h
 * Types and definitions that are common for Linux OSs for the kernel side of the
 * User-Kernel interface.
 */

#ifndef _UKK_OS_H_ /* Linux version */
#define _UKK_OS_H_

#include <linux/fs.h>

/**
 * @addtogroup uk_api User-Kernel Interface API
 * @{
 */

/**
 * @addtogroup uk_api_kernel UKK (Kernel side)
 * @{
 */

/**
 * Internal OS specific data structure associated with each UKK session. Part
 * of a ukk_session object.
 */
typedef struct ukkp_session
{
	int dummy;     /**< No internal OS specific data at this time */
} ukkp_session;

/** @} end group uk_api_kernel */

/** @} end group uk_api */

#endif /* _UKK_OS_H__ */
