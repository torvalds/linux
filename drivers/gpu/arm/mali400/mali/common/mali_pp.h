/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_PP_H__
#define __MALI_PP_H__

#include "mali_osk.h"
#include "mali_pp_job.h"
#include "mali_hw_core.h"
#include "mali_dma.h"

struct mali_group;

#define MALI_MAX_NUMBER_OF_PP_CORES        9

/**
 * Definition of the PP core struct
 * Used to track a PP core in the system.
 */
struct mali_pp_core {
	struct mali_hw_core  hw_core;           /**< Common for all HW cores */
	_mali_osk_irq_t     *irq;               /**< IRQ handler */
	u32                  core_id;           /**< Unique core ID */
	u32                  bcast_id;          /**< The "flag" value used by the Mali-450 broadcast and DLBU unit */
};

_mali_osk_errcode_t mali_pp_initialize(void);
void mali_pp_terminate(void);

struct mali_pp_core *mali_pp_create(const _mali_osk_resource_t *resource, struct mali_group *group, mali_bool is_virtual, u32 bcast_id);
void mali_pp_delete(struct mali_pp_core *core);

void mali_pp_stop_bus(struct mali_pp_core *core);
_mali_osk_errcode_t mali_pp_stop_bus_wait(struct mali_pp_core *core);
void mali_pp_reset_async(struct mali_pp_core *core);
_mali_osk_errcode_t mali_pp_reset_wait(struct mali_pp_core *core);
_mali_osk_errcode_t mali_pp_reset(struct mali_pp_core *core);
_mali_osk_errcode_t mali_pp_hard_reset(struct mali_pp_core *core);

void mali_pp_job_start(struct mali_pp_core *core, struct mali_pp_job *job, u32 sub_job);

/**
 * @brief Add commands to DMA command buffer to start PP job on core.
 */
void mali_pp_job_dma_cmd_prepare(struct mali_pp_core *core, struct mali_pp_job *job, u32 sub_job,
				 mali_dma_cmd_buf *buf);

u32 mali_pp_core_get_version(struct mali_pp_core *core);

MALI_STATIC_INLINE u32 mali_pp_core_get_id(struct mali_pp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return core->core_id;
}

MALI_STATIC_INLINE u32 mali_pp_core_get_bcast_id(struct mali_pp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return core->bcast_id;
}

struct mali_pp_core *mali_pp_get_global_pp_core(u32 index);
u32 mali_pp_get_glob_num_pp_cores(void);

/* Debug */
u32 mali_pp_dump_state(struct mali_pp_core *core, char *buf, u32 size);

/**
 * Put instrumented HW counters from the core(s) to the job object (if enabled)
 *
 * parent and child is always the same, except for virtual jobs on Mali-450.
 * In this case, the counters will be enabled on the virtual core (parent),
 * but values need to be read from the child cores.
 *
 * @param parent The core used to see if the counters was enabled
 * @param child The core to actually read the values from
 * @job Job object to update with counter values (if enabled)
 * @subjob Which subjob the counters are applicable for (core ID for virtual jobs)
 */
void mali_pp_update_performance_counters(struct mali_pp_core *parent, struct mali_pp_core *child, struct mali_pp_job *job, u32 subjob);

MALI_STATIC_INLINE const char *mali_pp_get_hw_core_desc(struct mali_pp_core *core)
{
	return core->hw_core.description;
}

/*** Register reading/writing functions ***/
MALI_STATIC_INLINE u32 mali_pp_get_int_stat(struct mali_pp_core *core)
{
	return mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_STATUS);
}

MALI_STATIC_INLINE u32 mali_pp_read_rawstat(struct mali_pp_core *core)
{
	return mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_RAWSTAT) & MALI200_REG_VAL_IRQ_MASK_USED;
}

MALI_STATIC_INLINE u32 mali_pp_read_status(struct mali_pp_core *core)
{
	return mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_STATUS);
}

MALI_STATIC_INLINE void mali_pp_mask_all_interrupts(struct mali_pp_core *core)
{
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_NONE);
}

MALI_STATIC_INLINE void mali_pp_clear_hang_interrupt(struct mali_pp_core *core)
{
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI200_REG_VAL_IRQ_HANG);
}

MALI_STATIC_INLINE void mali_pp_enable_interrupts(struct mali_pp_core *core)
{
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_USED);
}

MALI_STATIC_INLINE void mali_pp_write_addr_renderer_list(struct mali_pp_core *core,
		struct mali_pp_job *job, u32 subjob)
{
	u32 addr = mali_pp_job_get_addr_frame(job, subjob);
	mali_hw_core_register_write_relaxed(&core->hw_core, MALI200_REG_ADDR_FRAME, addr);
}


MALI_STATIC_INLINE void mali_pp_write_addr_stack(struct mali_pp_core *core, struct mali_pp_job *job, u32 subjob)
{
	u32 addr = mali_pp_job_get_addr_stack(job, subjob);
	mali_hw_core_register_write_relaxed(&core->hw_core, MALI200_REG_ADDR_STACK, addr);
}

#endif /* __MALI_PP_H__ */
