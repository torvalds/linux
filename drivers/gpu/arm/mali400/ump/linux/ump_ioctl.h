/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
