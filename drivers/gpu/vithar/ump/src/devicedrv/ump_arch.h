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



#ifndef _UMP_ARCH_H_
#define _UMP_ARCH_H_

#include <common/ump_kernel_core.h>

/**
 * Device specific setup.
 * Called by the UMP core code to to host OS/device specific setup.
 * Typical use case is device node creation for talking to user space.
 * @return UMP_OK on success, any other value on failure
 */
extern ump_result umpp_device_initialize(void);

/**
 * Device specific teardown.
 * Undo any things done by ump_device_initialize.
 */
extern void umpp_device_terminate(void);

extern int umpp_phys_commit(umpp_allocation * alloc);
extern void umpp_phys_free(umpp_allocation * alloc);

#endif /* _UMP_ARCH_H_ */
