/*
 * Copyright (C) 2011-2014, 2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_GP_H__
#define __MALI_GP_H__

#include "mali_osk.h"
#include "mali_gp_job.h"
#include "mali_hw_core.h"
#include "regs/mali_gp_regs.h"

struct mali_group;

/**
 * Definition of the GP core struct
 * Used to track a GP core in the system.
 */
struct mali_gp_core {
	struct mali_hw_core  hw_core;           /**< Common for all HW cores */
	_mali_osk_irq_t     *irq;               /**< IRQ handler */
};

_mali_osk_errcode_t mali_gp_initialize(void);
void mali_gp_terminate(void);

struct mali_gp_core *mali_gp_create(const _mali_osk_resource_t *resource, struct mali_group *group);
void mali_gp_delete(struct mali_gp_core *core);

void mali_gp_stop_bus(struct mali_gp_core *core);
_mali_osk_errcode_t mali_gp_stop_bus_wait(struct mali_gp_core *core);
void mali_gp_reset_async(struct mali_gp_core *core);
_mali_osk_errcode_t mali_gp_reset_wait(struct mali_gp_core *core);
void mali_gp_hard_reset(struct mali_gp_core *core);
_mali_osk_errcode_t mali_gp_reset(struct mali_gp_core *core);

void mali_gp_job_start(struct mali_gp_core *core, struct mali_gp_job *job);
void mali_gp_resume_with_new_heap(struct mali_gp_core *core, u32 start_addr, u32 end_addr);

u32 mali_gp_core_get_version(struct mali_gp_core *core);

struct mali_gp_core *mali_gp_get_global_gp_core(void);

#if MALI_STATE_TRACKING
u32 mali_gp_dump_state(struct mali_gp_core *core, char *buf, u32 size);
#endif

void mali_gp_update_performance_counters(struct mali_gp_core *core, struct mali_gp_job *job);

MALI_STATIC_INLINE const char *mali_gp_core_description(struct mali_gp_core *core)
{
	return core->hw_core.description;
}

MALI_STATIC_INLINE enum mali_interrupt_result mali_gp_get_interrupt_result(struct mali_gp_core *core)
{
	u32 stat_used = mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_STAT) &
			MALIGP2_REG_VAL_IRQ_MASK_USED;

	if (0 == stat_used) {
		return MALI_INTERRUPT_RESULT_NONE;
	} else if ((MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST |
		    MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST) == stat_used) {
		return MALI_INTERRUPT_RESULT_SUCCESS;
	} else if (MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST == stat_used) {
		return MALI_INTERRUPT_RESULT_SUCCESS_VS;
	} else if (MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST == stat_used) {
		return MALI_INTERRUPT_RESULT_SUCCESS_PLBU;
	} else if (MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM & stat_used) {
		return MALI_INTERRUPT_RESULT_OOM;
	}

	return MALI_INTERRUPT_RESULT_ERROR;
}

MALI_STATIC_INLINE u32 mali_gp_get_rawstat(struct mali_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return mali_hw_core_register_read(&core->hw_core,
					  MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT);
}

MALI_STATIC_INLINE u32 mali_gp_is_active(struct mali_gp_core *core)
{
	u32 status = mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_STATUS);
	return (status & MALIGP2_REG_VAL_STATUS_MASK_ACTIVE) ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE void mali_gp_mask_all_interrupts(struct mali_gp_core *core)
{
	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_NONE);
}

MALI_STATIC_INLINE void mali_gp_enable_interrupts(struct mali_gp_core *core, enum mali_interrupt_result exceptions)
{
	/* Enable all interrupts, except those specified in exceptions */
	u32 value;

	if (MALI_INTERRUPT_RESULT_SUCCESS_VS == exceptions) {
		/* Enable all used except VS complete */
		value = MALIGP2_REG_VAL_IRQ_MASK_USED &
			~MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST;
	} else {
		MALI_DEBUG_ASSERT(MALI_INTERRUPT_RESULT_SUCCESS_PLBU ==
				  exceptions);
		/* Enable all used except PLBU complete */
		value = MALIGP2_REG_VAL_IRQ_MASK_USED &
			~MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST;
	}

	mali_hw_core_register_write(&core->hw_core,
				    MALIGP2_REG_ADDR_MGMT_INT_MASK,
				    value);
}

MALI_STATIC_INLINE u32 mali_gp_read_plbu_alloc_start_addr(struct mali_gp_core *core)
{
	return mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR);
}

#endif /* __MALI_GP_H__ */
