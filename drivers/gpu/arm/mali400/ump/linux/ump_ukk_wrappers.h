/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2010, 2012-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_ukk_wrappers.h
 * Defines the wrapper functions which turn Linux IOCTL calls into _ukk_ calls
 */

#ifndef __UMP_UKK_WRAPPERS_H__
#define __UMP_UKK_WRAPPERS_H__

#include <linux/kernel.h>
#include "ump_kernel_common.h"

#ifdef __cplusplus
extern "C" {
#endif



int ump_get_api_version_wrapper(u32 __user * argument, struct ump_session_data * session_data);
int ump_release_wrapper(u32 __user * argument, struct ump_session_data  * session_data);
int ump_size_get_wrapper(u32 __user * argument, struct ump_session_data  * session_data);
int ump_msync_wrapper(u32 __user * argument, struct ump_session_data  * session_data);
int ump_cache_operations_control_wrapper(u32 __user * argument, struct ump_session_data  * session_data);
int ump_switch_hw_usage_wrapper(u32 __user * argument, struct ump_session_data  * session_data);
int ump_lock_wrapper(u32 __user * argument, struct ump_session_data  * session_data);
int ump_unlock_wrapper(u32 __user * argument, struct ump_session_data  * session_data);




#ifdef __cplusplus
}
#endif



#endif /* __UMP_UKK_WRAPPERS_H__ */
