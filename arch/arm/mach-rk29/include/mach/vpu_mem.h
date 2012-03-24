/* arch/arm/mach-rk29/include/mach/vpu_mem.h
 *
 * Copyright (C) 2007 Google, Inc.
 * author: chenhengming chm@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_RK29_VPU_MEM_H
#define __ARCH_ARM_MACH_RK29_VPU_MEM_H


#define VPU_MEM_IOCTL_MAGIC 'p'
#define VPU_MEM_GET_PHYS        _IOW(VPU_MEM_IOCTL_MAGIC, 1, unsigned int)
#define VPU_MEM_GET_TOTAL_SIZE  _IOW(VPU_MEM_IOCTL_MAGIC, 2, unsigned int)
#define VPU_MEM_ALLOCATE        _IOW(VPU_MEM_IOCTL_MAGIC, 3, unsigned int)
#define VPU_MEM_FREE            _IOW(VPU_MEM_IOCTL_MAGIC, 4, unsigned int)
#define VPU_MEM_CACHE_FLUSH     _IOW(VPU_MEM_IOCTL_MAGIC, 5, unsigned int)
#define VPU_MEM_DUPLICATE       _IOW(VPU_MEM_IOCTL_MAGIC, 6, unsigned int)
#define VPU_MEM_LINK            _IOW(VPU_MEM_IOCTL_MAGIC, 7, unsigned int)
#define VPU_MEM_CACHE_CLEAN     _IOW(VPU_MEM_IOCTL_MAGIC, 8, unsigned int)
#define VPU_MEM_CACHE_INVALID   _IOW(VPU_MEM_IOCTL_MAGIC, 9, unsigned int)
#define VPU_MEM_POOL_SET        _IOW(VPU_MEM_IOCTL_MAGIC, 10, unsigned int)
#define VPU_MEM_POOL_UNSET      _IOW(VPU_MEM_IOCTL_MAGIC, 11, unsigned int)
#define VPU_MEM_POOL_CHECK      _IOW(VPU_MEM_IOCTL_MAGIC, 12, unsigned int)

struct vpu_mem_platform_data
{
	const char* name;
	/* starting physical address of memory region */
	unsigned long start;
	/* size of memory region */
	unsigned long size;
	/* set to indicate maps of this region should be cached, if a mix of
	 * cached and uncached is desired, set this and open the device with
	 * O_SYNC to get an uncached region */
	unsigned cached;
	/* The MSM7k has bits to enable a write buffer in the bus controller*/
	unsigned buffered;
};

#endif //__ARCH_ARM_MACH_RK29_VPU_MEM_H

