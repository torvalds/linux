/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2010 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __ARCH_CONFIG_H__
#define __ARCH_CONFIG_H__

/* Configuration for the EB platform with ZBT memory enabled */

/* FIXME: mali don't use kernel header, so that we are unable to known which platform to use.
 hardcode the irq to make it work on allwinner a20 platform
*/
#define SW_INT_START 32
#define SW_INT_IRQNO_GPU_GP             (69 + SW_INT_START)
#define SW_INT_IRQNO_GPU_GPMMU          (70 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PP0            (71 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PPMMU0         (72 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PMU            (73 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PP1            (74 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PPMMU1         (75 + SW_INT_START)

static _mali_osk_resource_t arch_configuration [] =
{
	{
		.type = PMU,
		.description = "Mali-400 PMU",
		.base = 0x01C42000,
		.irq = SW_INT_IRQNO_GPU_PMU,
		.mmu_id = 0
	},
	{
		.type = MALI400GP,
		.description = "Mali-400 GP",
		.base = 0x01C40000,
		.irq = SW_INT_IRQNO_GPU_GP,
		.mmu_id = 1
	},
	{
		.type = MALI400PP,
		.base = 0x01C48000,
		.irq = SW_INT_IRQNO_GPU_PP0,
		.description = "Mali-400 PP0",
		.mmu_id = 2
	},
	{
		.type = MALI400PP,
		.base = 0x01C4A000,
		.irq = SW_INT_IRQNO_GPU_PP1,
		.description = "Mali-400 PP1",
		.mmu_id = 3
	},
	{
		.type = MMU,
		.base = 0x01C43000,
		.irq = SW_INT_IRQNO_GPU_GPMMU,
		.description = "Mali-400 MMU for GP",
		.mmu_id = 1
	},
	{
		.type = MMU,
		.base = 0x01C44000,
		.irq = SW_INT_IRQNO_GPU_PPMMU0,
		.description = "Mali-400 MMU for PP0",
		.mmu_id = 2
	},
	{
		.type = MMU,
		.base = 0x01C45000,
		.irq = SW_INT_IRQNO_GPU_PPMMU1,
		.description = "Mali-400 MMU for PP1",
		.mmu_id = 3
	},
	{
		.type = OS_MEMORY,
		.description = "OS Memory",
		.cpu_usage_adjust = 0x40000000,
		.alloc_order = 1, /* Lowest preference for this memory */
		.size = 192 * 1024 * 1024, /* 64 MB */
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_MMU_READABLE | _MALI_MMU_WRITEABLE
	},
	{
		.type = MALI400L2,
		.base = 0x01C41000,
		.description = "Mali-400 L2 cache"
	},
};

#endif /* __ARCH_CONFIG_H__ */
