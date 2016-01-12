/*
 * Copyright (C) 2010-2013, 2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 * Class Path Exception
 * Linking this library statically or dynamically with other modules is making a combined work based on this library. 
 * Thus, the terms and conditions of the GNU General Public License cover the whole combination.
 * As a special exception, the copyright holders of this library give you permission to link this library with independent modules 
 * to produce an executable, regardless of the license terms of these independent modules, and to copy and distribute the resulting 
 * executable under terms of your choice, provided that you also meet, for each linked independent module, the terms and conditions 
 * of the license of that module. An independent module is a module which is not derived from or based on this library. If you modify 
 * this library, you may extend this exception to your version of the library, but you are not obligated to do so. 
 * If you do not wish to do so, delete this exception statement from your version.
 */

#ifndef __UMP_IOCTL_H__
#define __UMP_IOCTL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/types.h>
#include <linux/ioctl.h>

#include <ump_uk_types.h>

#ifndef __user
#define __user
#endif


/**
 * @file UMP_ioctl.h
 * This file describes the interface needed to use the Linux device driver.
 * The interface is used by the userpace UMP driver.
 */

#define UMP_IOCTL_NR 0x90


#define UMP_IOC_QUERY_API_VERSION _IOR(UMP_IOCTL_NR, _UMP_IOC_QUERY_API_VERSION, _ump_uk_api_version_s)
#define UMP_IOC_ALLOCATE  _IOWR(UMP_IOCTL_NR,  _UMP_IOC_ALLOCATE,  _ump_uk_allocate_s)
#define UMP_IOC_RELEASE  _IOR(UMP_IOCTL_NR,  _UMP_IOC_RELEASE,  _ump_uk_release_s)
#define UMP_IOC_SIZE_GET  _IOWR(UMP_IOCTL_NR,  _UMP_IOC_SIZE_GET, _ump_uk_size_get_s)
#define UMP_IOC_MSYNC     _IOW(UMP_IOCTL_NR,  _UMP_IOC_MSYNC, _ump_uk_msync_s)

#define UMP_IOC_CACHE_OPERATIONS_CONTROL _IOW(UMP_IOCTL_NR,  _UMP_IOC_CACHE_OPERATIONS_CONTROL, _ump_uk_cache_operations_control_s)
#define UMP_IOC_SWITCH_HW_USAGE   _IOW(UMP_IOCTL_NR,  _UMP_IOC_SWITCH_HW_USAGE, _ump_uk_switch_hw_usage_s)
#define UMP_IOC_LOCK          _IOW(UMP_IOCTL_NR,  _UMP_IOC_LOCK, _ump_uk_lock_s)
#define UMP_IOC_UNLOCK        _IOW(UMP_IOCTL_NR,  _UMP_IOC_UNLOCK, _ump_uk_unlock_s)


#ifdef __cplusplus
}
#endif

#endif /* __UMP_IOCTL_H__ */
