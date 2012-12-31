/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARCH_CONFIG_H__
#define __ARCH_CONFIG_H__

/* Configuration for the EB platform with ZBT memory enabled */
/*zepplin added 2010.08.17 for orion configuration*/
#define MALI_BASE_ADDR 		   0x13000000
#define GP_ADDR 	MALI_BASE_ADDR
#define L2_ADDR 	MALI_BASE_ADDR+0x1000
#define PMU_ADDR 	MALI_BASE_ADDR+0x2000
#define GP_MMU_ADDR 	MALI_BASE_ADDR+0x3000
#define PP0_MMU_ADDR 	MALI_BASE_ADDR+0x4000
#define PP1_MMU_ADDR 	MALI_BASE_ADDR+0x5000
#define PP2_MMU_ADDR 	MALI_BASE_ADDR+0x6000
#define PP3_MMU_ADDR 	MALI_BASE_ADDR+0x7000
#define PP0_ADDR 	MALI_BASE_ADDR+0x8000
#define PP1_ADDR 	MALI_BASE_ADDR+0xA000
#define PP2_ADDR 	MALI_BASE_ADDR+0xC000
#define PP3_ADDR 	MALI_BASE_ADDR+0xE000

/*for mmu and os memory*/
#define MEM_BASE_ADDR    0x40000000
#define MEM_TOTAL_SIZE   0x40000000
#define MEM_MALI_OS_SIZE 0x40000000

/*for dedicated memory*/
//#define MEM_MALI_BASE    0x58000000
//#define MEM_MALI_SIZE 	 0x08000000
#define MEM_MALI_SIZE 	 CONFIG_MALI_MEM_SIZE*1024*1024
#define MEM_MALI_BASE    0x80000000 - MEM_MALI_SIZE 

static _mali_osk_resource_t arch_configuration [] =
{
	{
		.type = MALI400GP,
		.description = "Mali-400 GP",
		.base = GP_ADDR,
		.irq = IRQ_GP_3D,
		.mmu_id = 1
	},
	{
		.type = MALI400PP,
		.base = PP0_ADDR,
		.irq = IRQ_PP0_3D,
		.description = "Mali-400 PP 0",
		.mmu_id = 2
	},
	{
		.type = MALI400PP,
		.base = PP1_ADDR,
		.irq = IRQ_PP1_3D,
		.description = "Mali-400 PP 1",
		.mmu_id = 3
	},
	{
		.type = MALI400PP,
		.base = PP2_ADDR,
		.irq = IRQ_PP2_3D,
		.description = "Mali-400 PP 2",
		.mmu_id = 4
	},
	{
		.type = MALI400PP,
		.base = PP3_ADDR,
		.irq = IRQ_PP3_3D,
		.description = "Mali-400 PP 3",
		.mmu_id = 5
	},
#if USING_MMU
	{
		.type = MMU,
		.base = GP_MMU_ADDR,
		.irq = IRQ_GPMMU_3D,
		.description = "Mali-400 MMU for GP",
		.mmu_id = 1
	},
	{
		.type = MMU,
		.base = PP0_MMU_ADDR,
		.irq = IRQ_PPMMU0_3D,
		.description = "Mali-400 MMU for PP 0",
		.mmu_id = 2
	},
	{
		.type = MMU,
		.base = PP1_MMU_ADDR,
		.irq = IRQ_PPMMU1_3D,
		.description = "Mali-400 MMU for PP 1",
		.mmu_id = 3
	},
	{
		.type = MMU,
		.base = PP2_MMU_ADDR,
		.irq = IRQ_PPMMU2_3D,
		.description = "Mali-400 MMU for PP 2",
		.mmu_id = 4
	},
	{
		.type = MMU,
		.base = PP3_MMU_ADDR,
		.irq = IRQ_PPMMU3_3D,
		.description = "Mali-400 MMU for PP 3",
		.mmu_id = 5
	},
#if USING_OS_MEMORY 
	{
		.type = OS_MEMORY,
		.description = "System Memory",
		.size = MEM_MALI_OS_SIZE,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE | _MALI_GP_READABLE | _MALI_GP_WRITEABLE
	},
#endif
#if USING_DED /* Dedicated Memory */
	{
		.type = MEMORY,
		.description = "Dedicated Memory",
		.base = MEM_MALI_BASE,
		.size = MEM_MALI_SIZE,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE | _MALI_GP_READABLE | _MALI_GP_WRITEABLE | _MALI_MMU_READABLE | _MALI_MMU_WRITEABLE
	},
#endif/* if USING_OS_MEMORY*/
	{
		.type = MEM_VALIDATION,
		.description = "memory validation",
		.base = MEM_BASE_ADDR,
		.size = MEM_TOTAL_SIZE,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE | _MALI_GP_READABLE | _MALI_GP_WRITEABLE | _MALI_MMU_READABLE | _MALI_MMU_WRITEABLE
	},
#else /* Not using MMU */
	{
		.type = MEMORY,
		.description = "Dedicated Memory",
		.base = MEM_MALI_BASE,
		.size = MEM_MALI_SIZE,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE | _MALI_GP_READABLE | _MALI_GP_WRITEABLE | _MALI_MMU_READABLE | _MALI_MMU_WRITEABLE
	},
#endif
	{
		.type = MALI400L2,
		.base = L2_ADDR,
		.description = "Mali-400 L2 cache"
	},
};

#endif /* __ARCH_CONFIG_H__ */
