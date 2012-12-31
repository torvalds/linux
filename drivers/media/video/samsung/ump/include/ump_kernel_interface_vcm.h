/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_kernel_interface_vcm.h
 */

#ifndef __UMP_KERNEL_INTERFACE_VCM_H__
#define __UMP_KERNEL_INTERFACE_VCM_H__

#include <linux/vcm-drv.h>
#include <plat/s5p-vcm.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Turn specified physical memory into UMP memory. */
struct ump_vcm {
	struct vcm *vcm;
	struct vcm_res  *vcm_res;
	unsigned int dev_id;
};


#ifdef __cplusplus
}
#endif

#endif  /* __UMP_KERNEL_INTERFACE_VCM_H__ */
