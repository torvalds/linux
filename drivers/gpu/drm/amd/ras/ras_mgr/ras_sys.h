/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __RAS_SYS_H__
#define __RAS_SYS_H__
#include <linux/stdarg.h>
#include <linux/printk.h>
#include <linux/dev_printk.h>
#include <linux/mempool.h>
#include "amdgpu.h"

#define RAS_DEV_ERR(device, fmt, ...)                                               \
	do {                                                                      \
		if (device)                                                             \
			dev_err(((struct amdgpu_device *)device)->dev, fmt, ##__VA_ARGS__); \
		else                                                                  \
			printk(KERN_ERR fmt, ##__VA_ARGS__);                              \
	} while (0)

#define RAS_DEV_WARN(device, fmt, ...)                                               \
	do {                                                                       \
		if (device)                                                              \
			dev_warn(((struct amdgpu_device *)device)->dev, fmt, ##__VA_ARGS__); \
		else                                                                   \
			printk(KERN_WARNING fmt, ##__VA_ARGS__);                           \
	} while (0)

#define RAS_DEV_INFO(device, fmt, ...)                                                 \
	do {                                                                         \
		if (device)                                                                \
			dev_info(((struct amdgpu_device *)device)->dev, fmt, ##__VA_ARGS__);   \
		else                                                                     \
			printk(KERN_INFO fmt, ##__VA_ARGS__);                                \
	} while (0)

#define RAS_DEV_DBG(device, fmt, ...)                                                  \
	do {                                                                         \
		if (device)                                                                \
			dev_dbg(((struct amdgpu_device *)device)->dev, fmt, ##__VA_ARGS__);    \
		else                                                                     \
			printk(KERN_DEBUG fmt, ##__VA_ARGS__);                               \
	} while (0)

#define RAS_INFO(fmt, ...)  printk(KERN_INFO fmt, ##__VA_ARGS__)

#define RAS_DEV_RREG32_SOC15(dev, ip, inst, reg) \
({ \
	struct amdgpu_device *adev = (struct amdgpu_device *)dev; \
	__RREG32_SOC15_RLC__(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg, \
			 0, ip##_HWIP, inst); \
})

#define RAS_DEV_WREG32_SOC15(dev, ip, inst, reg, value) \
({ \
	struct amdgpu_device *adev = (struct amdgpu_device *)dev; \
	__WREG32_SOC15_RLC__((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg), \
			  value, 0, ip##_HWIP, inst); \
})

/* GET_INST returns the physical instance corresponding to a logical instance */
#define RAS_GET_INST(dev, ip, inst) \
({ \
	struct amdgpu_device *adev = (struct amdgpu_device *)dev; \
	adev->ip_map.logical_to_dev_inst ? \
		adev->ip_map.logical_to_dev_inst(adev, ip##_HWIP, inst) : inst; \
})

#define RAS_GET_MASK(dev, ip, mask) \
({ \
	struct amdgpu_device *adev = (struct amdgpu_device *)dev; \
	(adev->ip_map.logical_to_dev_mask ? \
		adev->ip_map.logical_to_dev_mask(adev, ip##_HWIP, mask) : mask); \
})

static inline void *ras_radix_tree_delete_iter(struct radix_tree_root *root, void *iter)
{
	return radix_tree_delete(root, ((struct radix_tree_iter *)iter)->index);
}

static inline long ras_wait_event_interruptible_timeout(void *wq_head,
			int (*condition)(void *param), void *param, unsigned int timeout)
{
	return wait_event_interruptible_timeout(*(wait_queue_head_t *)wq_head,
				condition(param), timeout);
}

extern const struct ras_sys_func amdgpu_ras_sys_fn;

#endif
