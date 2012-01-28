/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
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

#ifndef __ASM_ARCH_MSM_IOMAP_8X60_H
#define __ASM_ARCH_MSM_IOMAP_8X60_H

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

#define MSM8X60_QGIC_DIST_PHYS	0x02080000
#define MSM8X60_QGIC_DIST_SIZE	SZ_4K

#define MSM8X60_QGIC_CPU_PHYS	0x02081000
#define MSM8X60_QGIC_CPU_SIZE	SZ_4K

#define MSM_ACC_BASE		IOMEM(0xF0002000)
#define MSM_ACC_PHYS		0x02001000
#define MSM_ACC_SIZE		SZ_4K

#define MSM_GCC_BASE		IOMEM(0xF0003000)
#define MSM_GCC_PHYS		0x02082000
#define MSM_GCC_SIZE		SZ_4K

#define MSM_TLMM_BASE		IOMEM(0xF0004000)
#define MSM_TLMM_PHYS		0x00800000
#define MSM_TLMM_SIZE		SZ_16K

#define MSM_SHARED_RAM_BASE	IOMEM(0xF0100000)
#define MSM_SHARED_RAM_SIZE	SZ_1M

#define MSM8X60_TMR_PHYS	0x02000000
#define MSM8X60_TMR_SIZE	SZ_4K

#define MSM8X60_TMR0_PHYS	0x02040000
#define MSM8X60_TMR0_SIZE	SZ_4K

#ifdef CONFIG_DEBUG_MSM8660_UART
#define MSM_DEBUG_UART_BASE	0xE1040000
#define MSM_DEBUG_UART_PHYS	0x19C40000
#endif

#endif
