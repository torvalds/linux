/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2008-2011 Code Aurora Forum. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
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
 *
 * The MSM peripherals are spread all over across 768MB of physical
 * space, which makes just having a simple IO_ADDRESS macro to slide
 * them into the right virtual location rough.  Instead, we will
 * provide a master phys->virt mapping for peripherals here.
 *
 */

#ifndef __ASM_ARCH_MSM_IOMAP_7X30_H
#define __ASM_ARCH_MSM_IOMAP_7X30_H

/* Physical base address and size of peripherals.
 * Ordered by the virtual base addresses they will be mapped at.
 *
 * MSM_VIC_BASE must be an value that can be loaded via a "mov"
 * instruction, otherwise entry-macro.S will not compile.
 *
 * If you add or remove entries here, you'll want to edit the
 * msm_io_desc array in arch/arm/mach-msm/io.c to reflect your
 * changes.
 *
 */

#define MSM_VIC_BASE          IOMEM(0xE0000000)
#define MSM_VIC_PHYS          0xC0080000
#define MSM_VIC_SIZE          SZ_4K

#define MSM7X30_CSR_PHYS      0xC0100000
#define MSM7X30_CSR_SIZE      SZ_4K

#define MSM_DMOV_BASE         IOMEM(0xE0002000)
#define MSM_DMOV_PHYS         0xAC400000
#define MSM_DMOV_SIZE         SZ_4K

#define MSM7X30_GPIO1_PHYS        0xAC001000
#define MSM7X30_GPIO1_SIZE        SZ_4K

#define MSM7X30_GPIO2_PHYS        0xAC101000
#define MSM7X30_GPIO2_SIZE        SZ_4K

#define MSM_CLK_CTL_BASE      IOMEM(0xE0005000)
#define MSM_CLK_CTL_PHYS      0xAB800000
#define MSM_CLK_CTL_SIZE      SZ_4K

#define MSM_CLK_CTL_SH2_BASE  IOMEM(0xE0006000)
#define MSM_CLK_CTL_SH2_PHYS  0xABA01000
#define MSM_CLK_CTL_SH2_SIZE  SZ_4K

#define MSM_ACC_BASE          IOMEM(0xE0007000)
#define MSM_ACC_PHYS          0xC0101000
#define MSM_ACC_SIZE          SZ_4K

#define MSM_SAW_BASE          IOMEM(0xE0008000)
#define MSM_SAW_PHYS          0xC0102000
#define MSM_SAW_SIZE          SZ_4K

#define MSM_GCC_BASE	      IOMEM(0xE0009000)
#define MSM_GCC_PHYS	      0xC0182000
#define MSM_GCC_SIZE	      SZ_4K

#define MSM_TCSR_BASE	      IOMEM(0xE000A000)
#define MSM_TCSR_PHYS	      0xAB600000
#define MSM_TCSR_SIZE	      SZ_4K

#define MSM_SHARED_RAM_BASE   IOMEM(0xE0100000)
#define MSM_SHARED_RAM_PHYS   0x00100000
#define MSM_SHARED_RAM_SIZE   SZ_1M

#define MSM_UART1_PHYS        0xACA00000
#define MSM_UART1_SIZE        SZ_4K

#define MSM_UART2_PHYS        0xACB00000
#define MSM_UART2_SIZE        SZ_4K

#define MSM_UART3_PHYS        0xACC00000
#define MSM_UART3_SIZE        SZ_4K

#define MSM_MDC_BASE	      IOMEM(0xE0200000)
#define MSM_MDC_PHYS	      0xAA500000
#define MSM_MDC_SIZE	      SZ_1M

#define MSM_AD5_BASE          IOMEM(0xE0300000)
#define MSM_AD5_PHYS          0xA7000000
#define MSM_AD5_SIZE          (SZ_1M*13)

#define MSM_HSUSB_PHYS        0xA3600000
#define MSM_HSUSB_SIZE        SZ_1K

#ifndef __ASSEMBLY__
extern void msm_map_msm7x30_io(void);
#endif

#endif
