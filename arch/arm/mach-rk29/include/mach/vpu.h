/* arch/arm/mach-rk29/include/mach/vcodec.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

#ifndef __ARCH_ARM_MACH_RK29_VPU_H
#define __ARCH_ARM_MACH_RK29_VPU_H

#include <linux/ioctl.h>    /* needed for the _IOW etc stuff used later */

#define VPU_IRQ_EVENT_DEC_BIT		BIT(0)
#define VPU_IRQ_EVENT_DEC_IRQ_BIT	BIT(1)
#define VPU_IRQ_EVENT_PP_IRQ_BIT	BIT(2)
#define VPU_IRQ_EVENT_ENC_BIT		BIT(8)
#define VPU_IRQ_EVENT_ENC_IRQ_BIT	BIT(9)

/*
 * Ioctl definitions
 */

/* Use 'k' as magic number */
#define VPU_IOC_MAGIC  'k'

#define VPU_IOC_PP_INSTANCE     _IO(VPU_IOC_MAGIC, 1)   /* the client is pp instance */
#define VPU_IOC_HW_PERFORMANCE  _IO(VPU_IOC_MAGIC, 2)   /* get monotonic time (struct timespec) for HW performance */

#define VPU_IOC_GHWOFFSET       _IOR(VPU_IOC_MAGIC, 3, unsigned long *)
#define VPU_IOC_GHWIOSIZE       _IOR(VPU_IOC_MAGIC, 4, unsigned int *)

#define VPU_IOC_CLI             _IO(VPU_IOC_MAGIC, 5)
#define VPU_IOC_STI             _IO(VPU_IOC_MAGIC, 6)

#define VPU_IOC_DEC_INSTANCE    _IO(VPU_IOC_MAGIC, 7)
#define VPU_IOC_ENC_INSTANCE    _IO(VPU_IOC_MAGIC, 8)

#define VPU_IOC_MAXNR           8

#endif
