/*
 * Copyright (C) 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_utgard.h
 * Defines types and interface exposed by the Mali Utgard device driver
 */

#ifndef __MALI_UTGARD_H__
#define __MALI_UTGARD_H__

#include "mali_osk_types.h"

#define MALI_GPU_NAME_UTGARD "mali-utgard"

/* Mali-200 */

#define MALI_GPU_RESOURCES_MALI200(base_addr, gp_irq, pp_irq, mmu_irq) \
	MALI_GPU_RESOURCE_PP(base_addr + 0x0000, pp_irq) \
	MALI_GPU_RESOURCE_GP(base_addr + 0x2000, gp_irq) \
	MALI_GPU_RESOURCE_MMU(base_addr + 0x3000, mmu_irq)

/* Mali-300 */

#define MALI_GPU_RESOURCES_MALI300(base_addr, gp_irq, gp_mmu_irq, pp_irq, pp_mmu_irq) \
	MALI_GPU_RESOURCES_MALI400_MP1(base_addr, gp_irq, gp_mmu_irq, pp_irq, pp_mmu_irq)

#define MALI_GPU_RESOURCES_MALI300_PMU(base_addr, gp_irq, gp_mmu_irq, pp_irq, pp_mmu_irq) \
	MALI_GPU_RESOURCES_MALI400_MP1_PMU(base_addr, gp_irq, gp_mmu_irq, pp_irq, pp_mmu_irq)

/* Mali-400 */

#define MALI_GPU_RESOURCES_MALI400_MP1(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x1000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x0000, gp_irq, base_addr + 0x3000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x8000, pp0_irq, base_addr + 0x4000, pp0_mmu_irq)

#define MALI_GPU_RESOURCES_MALI400_MP1_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq) \
	MALI_GPU_RESOURCES_MALI400_MP1(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000)

#define MALI_GPU_RESOURCES_MALI400_MP2(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x1000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x0000, gp_irq, base_addr + 0x3000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x8000, pp0_irq, base_addr + 0x4000, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(1, base_addr + 0xA000, pp1_irq, base_addr + 0x5000, pp1_mmu_irq)

#define MALI_GPU_RESOURCES_MALI400_MP2_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq) \
	MALI_GPU_RESOURCES_MALI400_MP2(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000)

#define MALI_GPU_RESOURCES_MALI400_MP3(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x1000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x0000, gp_irq, base_addr + 0x3000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x8000, pp0_irq, base_addr + 0x4000, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(1, base_addr + 0xA000, pp1_irq, base_addr + 0x5000, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(2, base_addr + 0xC000, pp2_irq, base_addr + 0x6000, pp2_mmu_irq)

#define MALI_GPU_RESOURCES_MALI400_MP3_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq) \
	MALI_GPU_RESOURCES_MALI400_MP3(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000)

#define MALI_GPU_RESOURCES_MALI400_MP4(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x1000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x0000, gp_irq, base_addr + 0x3000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x8000, pp0_irq, base_addr + 0x4000, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(1, base_addr + 0xA000, pp1_irq, base_addr + 0x5000, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(2, base_addr + 0xC000, pp2_irq, base_addr + 0x6000, pp2_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(3, base_addr + 0xE000, pp3_irq, base_addr + 0x7000, pp3_mmu_irq)

#define MALI_GPU_RESOURCES_MALI400_MP4_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq) \
	MALI_GPU_RESOURCES_MALI400_MP4(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000)

/* Mali-450 */
#define MALI_GPU_RESOURCES_MALI450_MP2(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x10000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x00000, gp_irq, base_addr + 0x03000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x01000) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x08000, pp0_irq, base_addr + 0x04000, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(1, base_addr + 0x0A000, pp1_irq, base_addr + 0x05000, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_BCAST(base_addr + 0x13000) \
	MALI_GPU_RESOURCE_DLBU(base_addr + 0x14000) \
	MALI_GPU_RESOURCE_PP_BCAST(base_addr + 0x16000, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PP_MMU_BCAST(base_addr + 0x15000) \
	MALI_GPU_RESOURCE_DMA(base_addr + 0x12000)

#define MALI_GPU_RESOURCES_MALI450_MP2_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCES_MALI450_MP2(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000) \
 
#define MALI_GPU_RESOURCES_MALI450_MP3(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x10000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x00000, gp_irq, base_addr + 0x03000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x01000) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x08000, pp0_irq, base_addr + 0x04000, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(1, base_addr + 0x0A000, pp1_irq, base_addr + 0x05000, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(2, base_addr + 0x0C000, pp2_irq, base_addr + 0x06000, pp2_mmu_irq) \
	MALI_GPU_RESOURCE_BCAST(base_addr + 0x13000) \
	MALI_GPU_RESOURCE_DLBU(base_addr + 0x14000) \
	MALI_GPU_RESOURCE_PP_BCAST(base_addr + 0x16000, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PP_MMU_BCAST(base_addr + 0x15000)

#define MALI_GPU_RESOURCES_MALI450_MP3_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCES_MALI450_MP3(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000) \
 
#define MALI_GPU_RESOURCES_MALI450_MP4(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x10000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x00000, gp_irq, base_addr + 0x03000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x01000) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x08000, pp0_irq, base_addr + 0x04000, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(1, base_addr + 0x0A000, pp1_irq, base_addr + 0x05000, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(2, base_addr + 0x0C000, pp2_irq, base_addr + 0x06000, pp2_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(3, base_addr + 0x0E000, pp3_irq, base_addr + 0x07000, pp3_mmu_irq) \
	MALI_GPU_RESOURCE_BCAST(base_addr + 0x13000) \
	MALI_GPU_RESOURCE_DLBU(base_addr + 0x14000) \
	MALI_GPU_RESOURCE_PP_BCAST(base_addr + 0x16000, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PP_MMU_BCAST(base_addr + 0x15000) \
	MALI_GPU_RESOURCE_DMA(base_addr + 0x12000)

#define MALI_GPU_RESOURCES_MALI450_MP4_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCES_MALI450_MP4(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000) \
 
#define MALI_GPU_RESOURCES_MALI450_MP6(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp4_irq, pp4_mmu_irq, pp5_irq, pp5_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x10000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x00000, gp_irq, base_addr + 0x03000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x01000) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x08000, pp0_irq, base_addr + 0x04000, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(1, base_addr + 0x0A000, pp1_irq, base_addr + 0x05000, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(2, base_addr + 0x0C000, pp2_irq, base_addr + 0x06000, pp2_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x11000) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(3, base_addr + 0x28000, pp3_irq, base_addr + 0x1C000, pp3_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(4, base_addr + 0x2A000, pp4_irq, base_addr + 0x1D000, pp4_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(5, base_addr + 0x2C000, pp5_irq, base_addr + 0x1E000, pp5_mmu_irq) \
	MALI_GPU_RESOURCE_BCAST(base_addr + 0x13000) \
	MALI_GPU_RESOURCE_DLBU(base_addr + 0x14000) \
	MALI_GPU_RESOURCE_PP_BCAST(base_addr + 0x16000, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PP_MMU_BCAST(base_addr + 0x15000) \
	MALI_GPU_RESOURCE_DMA(base_addr + 0x12000)

#define MALI_GPU_RESOURCES_MALI450_MP6_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp4_irq, pp4_mmu_irq, pp5_irq, pp5_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCES_MALI450_MP6(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp4_irq, pp4_mmu_irq, pp5_irq, pp5_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000) \
 
#define MALI_GPU_RESOURCES_MALI450_MP8(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp4_irq, pp4_mmu_irq, pp5_irq, pp5_mmu_irq, pp6_irq, pp6_mmu_irq, pp7_irq, pp7_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x10000) \
	MALI_GPU_RESOURCE_GP_WITH_MMU(base_addr + 0x00000, gp_irq, base_addr + 0x03000, gp_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x01000) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(0, base_addr + 0x08000, pp0_irq, base_addr + 0x04000, pp0_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(1, base_addr + 0x0A000, pp1_irq, base_addr + 0x05000, pp1_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(2, base_addr + 0x0C000, pp2_irq, base_addr + 0x06000, pp2_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(3, base_addr + 0x0E000, pp3_irq, base_addr + 0x07000, pp3_mmu_irq) \
	MALI_GPU_RESOURCE_L2(base_addr + 0x11000) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(4, base_addr + 0x28000, pp4_irq, base_addr + 0x1C000, pp4_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(5, base_addr + 0x2A000, pp5_irq, base_addr + 0x1D000, pp5_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(6, base_addr + 0x2C000, pp6_irq, base_addr + 0x1E000, pp6_mmu_irq) \
	MALI_GPU_RESOURCE_PP_WITH_MMU(7, base_addr + 0x2E000, pp7_irq, base_addr + 0x1F000, pp7_mmu_irq) \
	MALI_GPU_RESOURCE_BCAST(base_addr + 0x13000) \
	MALI_GPU_RESOURCE_DLBU(base_addr + 0x14000) \
	MALI_GPU_RESOURCE_PP_BCAST(base_addr + 0x16000, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PP_MMU_BCAST(base_addr + 0x15000) \
	MALI_GPU_RESOURCE_DMA(base_addr + 0x12000)

#define MALI_GPU_RESOURCES_MALI450_MP8_PMU(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp4_irq, pp4_mmu_irq, pp5_irq, pp5_mmu_irq, pp6_irq, pp6_mmu_irq, pp7_irq, pp7_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCES_MALI450_MP8(base_addr, gp_irq, gp_mmu_irq, pp0_irq, pp0_mmu_irq, pp1_irq, pp1_mmu_irq, pp2_irq, pp2_mmu_irq, pp3_irq, pp3_mmu_irq, pp4_irq, pp4_mmu_irq, pp5_irq, pp5_mmu_irq, pp6_irq, pp6_mmu_irq, pp7_irq, pp7_mmu_irq, pp_bcast_irq) \
	MALI_GPU_RESOURCE_PMU(base_addr + 0x2000) \
 
#define MALI_GPU_RESOURCE_L2(addr) \
	{ \
		.name = "Mali_L2", \
		.flags = IORESOURCE_MEM, \
		.start = addr, \
		.end   = addr + 0x200, \
	},

#define MALI_GPU_RESOURCE_GP(gp_addr, gp_irq) \
	{ \
		.name = "Mali_GP", \
		.flags = IORESOURCE_MEM, \
		.start = gp_addr, \
		.end =   gp_addr + 0x100, \
	}, \
	{ \
		.name = "Mali_GP_IRQ", \
		.flags = IORESOURCE_IRQ, \
		.start = gp_irq, \
		.end   = gp_irq, \
	}, \
 
#define MALI_GPU_RESOURCE_GP_WITH_MMU(gp_addr, gp_irq, gp_mmu_addr, gp_mmu_irq) \
	{ \
		.name = "Mali_GP", \
		.flags = IORESOURCE_MEM, \
		.start = gp_addr, \
		.end =   gp_addr + 0x100, \
	}, \
	{ \
		.name = "Mali_GP_IRQ", \
		.flags = IORESOURCE_IRQ, \
		.start = gp_irq, \
		.end   = gp_irq, \
	}, \
	{ \
		.name = "Mali_GP_MMU", \
		.flags = IORESOURCE_MEM, \
		.start = gp_mmu_addr, \
		.end =   gp_mmu_addr + 0x100, \
	}, \
	{ \
		.name = "Mali_GP_MMU_IRQ", \
		.flags = IORESOURCE_IRQ, \
		.start = gp_mmu_irq, \
		.end =   gp_mmu_irq, \
	},

#define MALI_GPU_RESOURCE_PP(pp_addr, pp_irq) \
	{ \
		.name = "Mali_PP", \
		.flags = IORESOURCE_MEM, \
		.start = pp_addr, \
		.end =   pp_addr + 0x1100, \
	}, \
	{ \
		.name = "Mali_PP_IRQ", \
		.flags = IORESOURCE_IRQ, \
		.start = pp_irq, \
		.end =   pp_irq, \
	}, \
 
#define MALI_GPU_RESOURCE_PP_WITH_MMU(id, pp_addr, pp_irq, pp_mmu_addr, pp_mmu_irq) \
	{ \
		.name = "Mali_PP" #id, \
		.flags = IORESOURCE_MEM, \
		.start = pp_addr, \
		.end =   pp_addr + 0x1100, \
	}, \
	{ \
		.name = "Mali_PP" #id "_IRQ", \
		.flags = IORESOURCE_IRQ, \
		.start = pp_irq, \
		.end =   pp_irq, \
	}, \
	{ \
		.name = "Mali_PP" #id "_MMU", \
		.flags = IORESOURCE_MEM, \
		.start = pp_mmu_addr, \
		.end =   pp_mmu_addr + 0x100, \
	}, \
	{ \
		.name = "Mali_PP" #id "_MMU_IRQ", \
		.flags = IORESOURCE_IRQ, \
		.start = pp_mmu_irq, \
		.end =   pp_mmu_irq, \
	},

#define MALI_GPU_RESOURCE_MMU(mmu_addr, mmu_irq) \
	{ \
		.name = "Mali_MMU", \
		.flags = IORESOURCE_MEM, \
		.start = mmu_addr, \
		.end =   mmu_addr + 0x100, \
	}, \
	{ \
		.name = "Mali_MMU_IRQ", \
		.flags = IORESOURCE_IRQ, \
		.start = mmu_irq, \
		.end =   mmu_irq, \
	},

#define MALI_GPU_RESOURCE_PMU(pmu_addr) \
	{ \
		.name = "Mali_PMU", \
		.flags = IORESOURCE_MEM, \
		.start = pmu_addr, \
		.end =   pmu_addr + 0x100, \
	},

#define MALI_GPU_RESOURCE_DMA(dma_addr) \
	{ \
		.name = "Mali_DMA", \
		.flags = IORESOURCE_MEM, \
		.start = dma_addr, \
		.end = dma_addr + 0x100, \
	},

#define MALI_GPU_RESOURCE_DLBU(dlbu_addr) \
	{ \
		.name = "Mali_DLBU", \
		.flags = IORESOURCE_MEM, \
		.start = dlbu_addr, \
		.end = dlbu_addr + 0x100, \
	},

#define MALI_GPU_RESOURCE_BCAST(bcast_addr) \
	{ \
		.name = "Mali_Broadcast", \
		.flags = IORESOURCE_MEM, \
		.start = bcast_addr, \
		.end = bcast_addr + 0x100, \
	},

#define MALI_GPU_RESOURCE_PP_BCAST(pp_addr, pp_irq) \
	{ \
		.name = "Mali_PP_Broadcast", \
		.flags = IORESOURCE_MEM, \
		.start = pp_addr, \
		.end =   pp_addr + 0x1100, \
	}, \
	{ \
		.name = "Mali_PP_Broadcast_IRQ", \
		.flags = IORESOURCE_IRQ, \
		.start = pp_irq, \
		.end =   pp_irq, \
	}, \
 
#define MALI_GPU_RESOURCE_PP_MMU_BCAST(pp_mmu_bcast_addr) \
	{ \
		.name = "Mali_PP_MMU_Broadcast", \
		.flags = IORESOURCE_MEM, \
		.start = pp_mmu_bcast_addr, \
		.end = pp_mmu_bcast_addr + 0x100, \
	},

struct mali_gpu_utilization_data {
	unsigned int utilization_gpu; /* Utilization for GP and all PP cores combined, 0 = no utilization, 256 = full utilization */
	unsigned int utilization_gp;  /* Utilization for GP core only, 0 = no utilization, 256 = full utilization */
	unsigned int utilization_pp;  /* Utilization for all PP cores combined, 0 = no utilization, 256 = full utilization */
#if defined(CONFIG_MALI400_POWER_PERFORMANCE_POLICY)
	unsigned int number_of_window_jobs;
	unsigned int number_of_window_jobs_under_pressure;
#endif
};

struct mali_gpu_device_data {
	/* Dedicated GPU memory range (physical). */
	unsigned long dedicated_mem_start;
	unsigned long dedicated_mem_size;

	/* Shared GPU memory */
	unsigned long shared_mem_size;

	/* Frame buffer memory to be accessible by Mali GPU (physical) */
	unsigned long fb_start;
	unsigned long fb_size;

	/* Max runtime [ms] for jobs */
	int max_job_runtime;

	/* Report GPU utilization in this interval (specified in ms) */
	unsigned long utilization_interval;

	/* Function that will receive periodic GPU utilization numbers */
	void (*utilization_callback)(struct mali_gpu_utilization_data *data);

	/*
	 * Mali PMU switch delay.
	 * Only needed if the power gates are connected to the PMU in a high fanout
	 * network. This value is the number of Mali clock cycles it takes to
	 * enable the power gates and turn on the power mesh.
	 * This value will have no effect if a daisy chain implementation is used.
	 */
	u32 pmu_switch_delay;


	/* Mali Dynamic power domain configuration in sequence from 0-11
	 *  GP  PP0 PP1  PP2  PP3  PP4  PP5  PP6  PP7, L2$0 L2$1 L2$2
	 */
	u16 pmu_domain_config[12];

	/* Fuction that platform callback for freq tunning, needed when POWER_PERFORMANCE_POLICY enabled*/
	int (*set_freq_callback)(unsigned int mhz);
};

/** @brief MALI GPU power down using MALI in-built PMU
 *
 * called to power down all cores
 */
int mali_pmu_powerdown(void);


/** @brief MALI GPU power up using MALI in-built PMU
 *
 * called to power up all cores
 */
int mali_pmu_powerup(void);

/**
 * Pause the scheduling and power state changes of Mali device driver.
 * mali_dev_resume() must always be called as soon as possible after this function
 * in order to resume normal operation of the Mali driver.
 */
void mali_dev_pause(void);

/**
 * Resume scheduling and allow power changes in Mali device driver.
 * This must always be called after mali_dev_pause().
 */
void mali_dev_resume(void);

/** @brief Set the desired number of PP cores to use.
 *
 * The internal Mali PMU will be used, if present, to physically power off the PP cores.
 *
 * @param num_cores The number of desired cores
 * @return 0 on success, otherwise error. -EINVAL means an invalid number of cores was specified.
 */
int mali_perf_set_num_pp_cores(unsigned int num_cores);

#endif
