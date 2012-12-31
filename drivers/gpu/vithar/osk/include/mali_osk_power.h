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



#ifndef _OSK_POWER_H_
#define _OSK_POWER_H_

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @addtogroup oskpower Power management
 *  
 * Some OS need to approve power state changes to a device, either to
 * control power to a bus the device resides on, or to give the OS
 * power manager the chance to see if the power state change is allowed
 * for the current OS power management policy.
 *
 * @{
 */

/**
 * @brief Request and perform a change in the power state of the device
 *
 * A function to request the OS to perform a change the power state of a device. This
 * function returns when the power state change has completed.
 *
 * This allows the OS to control the power for the bus on which the GPU device resides,
 * and the OS power manager can verify changing the power state is allowed according to
 * its own power management policy (the OS may have been informed that an application will 
 * make heavy use of the GPU soon). As a result of the request the OS is likely to 
 * request the GPU device driver to actually perform the power state change (in Windows 
 * CE for instance, the OS power manager will issue an IOCTL_POWER_SET to actually make 
 * the GPU device change the power state). 
 *
 * The result of the request is either success (the GPU device driver has successfully 
 * completed the power state change for the GPU device), refused (the OS didn't allow 
 * the power state change), or failure (the GPU device driver encountered an error 
 * changing the power state).
 *
 * @param[in,out] info  OS specific information necessary to control power to the device
 * @param[in] state     power state to switch to (off, idle, or active)
 * @return OSK_POWER_REQUEST_FINISHED when the driver successfully completed the power
 * state change for the device, OSK_POWER_REQUEST_FAILED when it failed, or
 * OSK_POWER_REQUEST_REFUSED when the OS didn't allow the power state change.
 */
OSK_STATIC_INLINE osk_power_request_result osk_power_request(osk_power_info *info, osk_power_state state) CHECK_RESULT;

/* @} */ /* end group oskpower */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_power.h>

#endif /* _OSK_POWER_H_ */
