/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_MEMORY_H__
#define __ASM_ARCH_MXC_MEMORY_H__

#define MX1_PHYS_OFFSET		UL(0x08000000)
#define MX21_PHYS_OFFSET	UL(0xc0000000)
#define MX25_PHYS_OFFSET	UL(0x80000000)
#define MX27_PHYS_OFFSET	UL(0xa0000000)
#define MX3x_PHYS_OFFSET	UL(0x80000000)
#define MX50_PHYS_OFFSET	UL(0x70000000)
#define MX51_PHYS_OFFSET	UL(0x90000000)
#define MX53_PHYS_OFFSET	UL(0x70000000)

#if !defined(CONFIG_RUNTIME_PHYS_OFFSET)
# if defined CONFIG_ARCH_MX1
#  define PLAT_PHYS_OFFSET		MX1_PHYS_OFFSET
# elif defined CONFIG_MACH_MX21
#  define PLAT_PHYS_OFFSET		MX21_PHYS_OFFSET
# elif defined CONFIG_ARCH_MX25
#  define PLAT_PHYS_OFFSET		MX25_PHYS_OFFSET
# elif defined CONFIG_MACH_MX27
#  define PLAT_PHYS_OFFSET		MX27_PHYS_OFFSET
# elif defined CONFIG_ARCH_MX3
#  define PLAT_PHYS_OFFSET		MX3x_PHYS_OFFSET
# elif defined CONFIG_ARCH_MX50
#  define PLAT_PHYS_OFFSET		MX50_PHYS_OFFSET
# elif defined CONFIG_ARCH_MX51
#  define PLAT_PHYS_OFFSET		MX51_PHYS_OFFSET
# elif defined CONFIG_ARCH_MX53
#  define PLAT_PHYS_OFFSET		MX53_PHYS_OFFSET
# endif
#endif

#if defined(CONFIG_MX3_VIDEO)
/*
 * Increase size of DMA-consistent memory region.
 * This is required for mx3 camera driver to capture at least two QXGA frames.
 */
#define CONSISTENT_DMA_SIZE SZ_8M

#elif defined(CONFIG_MX1_VIDEO) || defined(CONFIG_VIDEO_MX2_HOSTSUPPORT)
/*
 * Increase size of DMA-consistent memory region.
 * This is required for i.MX camera driver to capture at least four VGA frames.
 */
#define CONSISTENT_DMA_SIZE SZ_4M
#endif /* CONFIG_MX1_VIDEO || CONFIG_VIDEO_MX2_HOSTSUPPORT */

#endif /* __ASM_ARCH_MXC_MEMORY_H__ */
