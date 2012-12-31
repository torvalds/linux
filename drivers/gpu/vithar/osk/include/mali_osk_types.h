/*
 *
 * (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _OSK_TYPES_H_
#define _OSK_TYPES_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum osk_error
{
	OSK_ERR_NONE,          /**< Success */
	OSK_ERR_FAIL,          /**< Unclassified failure */
	OSK_ERR_MAP,           /**< Memory mapping operation failed */
	OSK_ERR_ALLOC,         /**< Memory allocation failed */
	OSK_ERR_ACCESS         /**< Permissions to access an object failed */
} osk_error;

#define OSK_STATIC_INLINE static __inline
#define OSK_BITS_PER_LONG (8 * sizeof(unsigned long))
#define OSK_ULONG_MAX (~0UL)

/**
 * OSK workqueue flags
 *
 * Flags specifying the kind of workqueue to create. Flags can be combined.
 */

/**
 * By default a work queue guarantees non-reentrace on the same CPU. 
 * When the OSK_WORKQ_NON_REENTRANT flag is set, this guarantee is 
 * extended to all CPUs.
 */
#define	OSK_WORKQ_NON_REENTRANT  (1 << 0)
/**
 * Work units submitted to a high priority queue start execution as soon
 * as resources are available.
 */
#define	OSK_WORKQ_HIGH_PRIORITY  (1 << 1)
/**
 * Ensures there is always a thread available to run tasks on this queue. This
 * flag should be set if the work queue is involved in reclaiming memory when
 * its work units run. 
 */
#define	OSK_WORKQ_RESCUER        (1 << 2)

/**
 * Prototype for a function called when a OSK timer expires. See osk_timer_callback_set()
 * that registers the callback function with a OSK timer.
 */
typedef void (*osk_timer_callback)(void *);

typedef enum osk_power_state
{
	OSK_POWER_STATE_OFF,            /**< Device is off */
	OSK_POWER_STATE_IDLE,           /**< Device is idle */
	OSK_POWER_STATE_ACTIVE          /**< Device is active */
} osk_power_state;

typedef enum osk_power_request_result
{
	OSK_POWER_REQUEST_FINISHED,     /**< The driver successfully completed the power state change for the device */
	OSK_POWER_REQUEST_FAILED,       /**< The driver for the device encountered an error changing the power state */
	OSK_POWER_REQUEST_REFUSED       /**< The OS didn't allow the power state change for the device */
} osk_power_request_result;


#include <osk/mali_osk_arch_types.h>

#ifdef __cplusplus
}
#endif

#endif /* _OSK_TYPES_H_ */

