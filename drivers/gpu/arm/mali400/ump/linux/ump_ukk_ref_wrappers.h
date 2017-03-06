/*
 * Copyright (C) 2010, 2013-2014, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_ukk_wrappers.h
 * Defines the wrapper functions which turn Linux IOCTL calls into _ukk_ calls for the reference implementation
 */

#ifndef __UMP_UKK_REF_WRAPPERS_H__
#define __UMP_UKK_REF_WRAPPERS_H__

#include <linux/kernel.h>
#include "ump_kernel_common.h"

#ifdef __cplusplus
extern "C" {
#endif


int ump_allocate_wrapper(u32 __user *argument, struct ump_session_data   *session_data);
#ifdef CONFIG_DMA_SHARED_BUFFER
int ump_dmabuf_import_wrapper(u32 __user *argument, struct ump_session_data  *session_data);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __UMP_UKK_REF_WRAPPERS_H__ */
