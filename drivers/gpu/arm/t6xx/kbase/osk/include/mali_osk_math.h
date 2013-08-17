/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_osk_math.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_MATH_H_
#define _OSK_MATH_H_

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
 * @addtogroup oskmath Math
 *
 * Math related functions for which no commmon behavior exists on OS.
 *
 * @{
 */

/**
 * @brief Divide a 64-bit value with a 32-bit divider
 *
 * Performs an (unsigned) integer division of a 64-bit value
 * with a 32-bit divider and returns the 64-bit result and
 * 32-bit remainder.
 *
 * Provided as part of the OSK as not all OSs support 64-bit
 * division in an uniform way. Currently required to support
 * printing 64-bit numbers in the OSK debug message functions.
 *
 * @param[in,out] value   pointer to a 64-bit value to be divided by
 *                        \a divisor. The integer result of the division
 *                        is stored in \a value on output.
 * @param[in]     divisor 32-bit divisor
 * @return 32-bit remainder of the division
 */
OSK_STATIC_INLINE u32 osk_divmod6432(u64 *value, u32 divisor);

/** @} */  /* end group oskmath */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_math.h>

#ifdef __cplusplus
}
#endif

#endif /* _OSK_MATH_H_ */
