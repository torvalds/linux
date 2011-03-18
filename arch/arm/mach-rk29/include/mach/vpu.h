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

#define VPU_aclk_vepu           0
#define VPU_hclk_vepu           1
#define VPU_aclk_ddr_vepu       2
#define VPU_hclk_cpu_vcodec     3

/* Use 'k' as magic number */
#define VPU_IOC_MAGIC  'k'

#define VPU_IOC_CLOCK_ON        _IOW(VPU_IOC_MAGIC, 1, unsigned long)
#define VPU_IOC_CLOCK_OFF       _IOW(VPU_IOC_MAGIC, 2, unsigned long)

#define VPU_IOC_CLOCK_RESET     _IOW(VPU_IOC_MAGIC, 3, unsigned long)
#define VPU_IOC_CLOCK_UNRESET   _IOW(VPU_IOC_MAGIC, 4, unsigned long)

#define VPU_IOC_DOMAIN_ON       _IO(VPU_IOC_MAGIC, 5)
#define VPU_IOC_DOMAIN_OFF      _IO(VPU_IOC_MAGIC, 6)

#define VPU_IOC_TEST            _IO(VPU_IOC_MAGIC, 7)

#define VPU_IOC_WR_DEC          _IOW(VPU_IOC_MAGIC, 8,  unsigned long)
#define VPU_IOC_WR_DEC_PP       _IOW(VPU_IOC_MAGIC, 9,  unsigned long)
#define VPU_IOC_WR_ENC          _IOW(VPU_IOC_MAGIC, 10, unsigned long)
#define VPU_IOC_WR_PP           _IOW(VPU_IOC_MAGIC, 11, unsigned long)

#define VPU_IOC_RD_DEC          _IOW(VPU_IOC_MAGIC, 12, unsigned long)
#define VPU_IOC_RD_DEC_PP       _IOW(VPU_IOC_MAGIC, 13, unsigned long)
#define VPU_IOC_RD_ENC          _IOW(VPU_IOC_MAGIC, 14, unsigned long)
#define VPU_IOC_RD_PP           _IOW(VPU_IOC_MAGIC, 15, unsigned long)

#define VPU_IOC_CLS_IRQ         _IOW(VPU_IOC_MAGIC, 16, unsigned long)

#endif
