/* arch/arm/mach-msm/include/mach/msm_iomap.h
 *
 * Copyright (C) 2007 Google, Inc.
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

#ifndef __ASM_ARCH_MSM_IOMAP_H
#define __ASM_ARCH_MSM_IOMAP_H

#include <asm/sizes.h>

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

#define MSM_VIC_BASE          0xE0000000
#define MSM_VIC_PHYS          0xC0000000
#define MSM_VIC_SIZE          SZ_4K

#define MSM_CSR_BASE          0xE0001000
#define MSM_CSR_PHYS          0xC0100000
#define MSM_CSR_SIZE          SZ_4K

#define MSM_GPT_PHYS          MSM_CSR_PHYS
#define MSM_GPT_BASE          MSM_CSR_BASE
#define MSM_GPT_SIZE          SZ_4K

#define MSM_DMOV_BASE         0xE0002000
#define MSM_DMOV_PHYS         0xA9700000
#define MSM_DMOV_SIZE         SZ_4K

#define MSM_UART1_BASE        0xE0003000
#define MSM_UART1_PHYS        0xA9A00000
#define MSM_UART1_SIZE        SZ_4K

#define MSM_UART2_BASE        0xE0004000
#define MSM_UART2_PHYS        0xA9B00000
#define MSM_UART2_SIZE        SZ_4K

#define MSM_UART3_BASE        0xE0005000
#define MSM_UART3_PHYS        0xA9C00000
#define MSM_UART3_SIZE        SZ_4K

#define MSM_I2C_BASE          0xE0006000
#define MSM_I2C_PHYS          0xA9900000
#define MSM_I2C_SIZE          SZ_4K

#define MSM_GPIO1_BASE        0xE0007000
#define MSM_GPIO1_PHYS        0xA9200000
#define MSM_GPIO1_SIZE        SZ_4K

#define MSM_GPIO2_BASE        0xE0008000
#define MSM_GPIO2_PHYS        0xA9300000
#define MSM_GPIO2_SIZE        SZ_4K

#define MSM_HSUSB_BASE        0xE0009000
#define MSM_HSUSB_PHYS        0xA0800000
#define MSM_HSUSB_SIZE        SZ_4K

#define MSM_CLK_CTL_BASE      0xE000A000
#define MSM_CLK_CTL_PHYS      0xA8600000
#define MSM_CLK_CTL_SIZE      SZ_4K

#define MSM_PMDH_BASE         0xE000B000
#define MSM_PMDH_PHYS         0xAA600000
#define MSM_PMDH_SIZE         SZ_4K

#define MSM_EMDH_BASE         0xE000C000
#define MSM_EMDH_PHYS         0xAA700000
#define MSM_EMDH_SIZE         SZ_4K

#define MSM_MDP_BASE          0xE0010000
#define MSM_MDP_PHYS          0xAA200000
#define MSM_MDP_SIZE          0x000F0000

#define MSM_SHARED_RAM_BASE   0xE0100000
#define MSM_SHARED_RAM_PHYS   0x01F00000
#define MSM_SHARED_RAM_SIZE   SZ_1M

#endif
