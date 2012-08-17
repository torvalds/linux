/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_pp.h"
#include "mali_hw_core.h"
#include "mali_group.h"
#include "mali_osk.h"
#include "regs/mali_200_regs.h"
#include "mali_kernel_common.h"
#include "mali_kernel_core.h"
#if MALI_TIMELINE_PROFILING_ENABLED
#include "mali_osk_profiling.h"
#endif

/* See mali_gp.c file for description on how to handle the interrupt mask.
 * This is how to do it on PP: mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_USED);
 */

#define MALI_MAX_NUMBER_OF_PP_CORES        8

/**
 * Definition of the PP core struct
 * Used to track a PP core in the system.
 */
struct mali_pp_core
{
	struct mali_hw_core  hw_core;           /**< Common for all HW cores */
	struct mali_group   *group;             /**< Parent group for this core */
	_mali_osk_irq_t     *irq;               /**< IRQ handler */
	u32                  core_id;           /**< Unique core ID */
	struct mali_pp_job  *running_job;       /**< Current running (super) job */
	u32                  running_sub_job;   /**< Current running sub job */
	_mali_osk_timer_t   *timeout_timer;     /**< timeout timer for this core */
	u32                  timeout_job_id;    /**< job id for the timed out job - relevant only if pp_core_timed_out == MALI_TRUE */
	mali_bool            core_timed_out;    /**< if MALI_TRUE, this pp core has timed out; if MALI_FALSE, no timeout on this pp core */
	u32                  counter_src0;      /**< Performance counter 0, MALI_HW_CORE_NO_COUNTER for disabled */
	u32                  counter_src1;      /**< Performance counter 1, MALI_HW_CORE_NO_COUNTER for disabled */
	u32                  counter_src0_used; /**< The selected performance counter 0 when a job is running */
	u32                  counter_src1_used; /**< The selected performance counter 1 when a job is running */
};

static struct mali_pp_core* mali_global_pp_cores[MALI_MAX_NUMBER_OF_PP_CORES];
static u32 mali_global_num_pp_cores = 0;

/* Interrupt handlers */
static _mali_osk_errcode_t mali_pp_upper_half(void *data);
static void mali_pp_bottom_half(void *data);
static void mali_pp_irq_probe_trigger(void *data);
static _mali_osk_errcode_t mali_pp_irq_probe_ack(void *data);
static void mali_pp_post_process_job(struct mali_pp_core *core);
static void mali_pp_timeout(void *data);

struct mali_pp_core *mali_pp_create(const _mali_osk_resource_t *resource, struct mali_group *group)
{
	struct mali_pp_core* core = NULL;

	MALI_DEBUG_PRINT(2, ("Mali PP: Creating Mali PP core: %s\n", resource->description));
	MALI_DEBUG_PRINT(2, ("Mali PP: Base address of PP core: 0x%x\n", resource->base));

	if (mali_global_num_pp_cores >= MALI_MAX_NUMBER_OF_PP_CORES)
	{
		MALI_PRINT_ERROR(("Mali PP: Too many PP core objects created\n"));
		return NULL;
	}

	core = _mali_osk_malloc(sizeof(struct mali_pp_core));
	if (NULL != core)
	{
		core->group = group;
		core->core_id = mali_global_num_pp_cores;
		core->running_job = NULL;
		core->counter_src0 = MALI_HW_CORE_NO_COUNTER;
		core->counter_src1 = MALI_HW_CORE_NO_COUNTER;
		core->counter_src0_used = MALI_HW_CORE_NO_COUNTER;
		core->counter_src1_used = MALI_HW_CORE_NO_COUNTER;
		if (_MALI_OSK_ERR_OK == mali_hw_core_create(&core->hw_core, resource, MALI200_REG_SIZEOF_REGISTER_BANK))
		{
			_mali_osk_errcode_t ret;

			mali_group_lock(group);
			ret = mali_pp_reset(core);
			mali_group_unlock(group);

			if (_MALI_OSK_ERR_OK == ret)
			{
				/* Setup IRQ handlers (which will do IRQ probing if needed) */
				core->irq = _mali_osk_irq_init(resource->irq,
				                               mali_pp_upper_half,
				                               mali_pp_bottom_half,
				                               mali_pp_irq_probe_trigger,
				                               mali_pp_irq_probe_ack,
				                               core,
				                               "mali_pp_irq_handlers");
				if (NULL != core->irq)
				{
					/* Initialise the timeout timer */
					core->timeout_timer = _mali_osk_timer_init();
					if(NULL != core->timeout_timer)
					{
						_mali_osk_timer_setcallback(core->timeout_timer, mali_pp_timeout, (void *)core);

						mali_global_pp_cores[mali_global_num_pp_cores] = core;
						mali_global_num_pp_cores++;

						return core;
					}
					else
					{
						MALI_PRINT_ERROR(("Failed to setup timeout timer for PP core %s\n", core->hw_core.description));
						/* Release IRQ handlers */
						_mali_osk_irq_term(core->irq);
					}
				}
				else
				{
					MALI_PRINT_ERROR(("Mali PP: Failed to setup interrupt handlers for PP core %s\n", core->hw_core.description));
				}
			}
			mali_hw_core_delete(&core->hw_core);
		}

		_mali_osk_free(core);
	}
	else
	{
		MALI_PRINT_ERROR(("Mali PP: Failed to allocate memory for PP core\n"));
	}

	return NULL;
}

void mali_pp_delete(struct mali_pp_core *core)
{
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(core);

	_mali_osk_timer_term(core->timeout_timer);
	_mali_osk_irq_term(core->irq);
	mali_hw_core_delete(&core->hw_core);

	/* Remove core from global list */
	for (i = 0; i < mali_global_num_pp_cores; i++)
	{
		if (mali_global_pp_cores[i] == core)
		{
			mali_global_pp_cores[i] = NULL;
			mali_global_num_pp_cores--;
			break;
		}
	}

	_mali_osk_free(core);
}

void mali_pp_stop_bus(struct mali_pp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	/* Will only send the stop bus command, and not wait for it to complete */
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI200_REG_VAL_CTRL_MGMT_STOP_BUS);
}

_mali_osk_errcode_t mali_pp_stop_bus_wait(struct mali_pp_core *core)
{
	int i;
	const int request_loop_count = 20;

	MALI_DEBUG_ASSERT_POINTER(core);
	MALI_ASSERT_GROUP_LOCKED(core->group);

	/* Send the stop bus command. */
	mali_pp_stop_bus(core);

	/* Wait for bus to be stopped */
	for (i = 0; i < request_loop_count; i++)
	{
		if (mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_STATUS) & MALI200_REG_VAL_STATUS_BUS_STOPPED)
			break;
		_mali_osk_time_ubusydelay(10);
	}

	if (request_loop_count == i)
	{
		MALI_PRINT_ERROR(("Mali PP: Failed to stop bus on %s. Status: 0x%08x\n", core->hw_core.description, mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_STATUS)));
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_pp_hard_reset(struct mali_pp_core *core)
{
	/* Bus must be stopped before calling this function */
	const int reset_finished_loop_count = 15;
	const u32 reset_invalid_value = 0xC0FFE000;
	const u32 reset_check_value = 0xC01A0000;
	int i;

	MALI_DEBUG_ASSERT_POINTER(core);
	MALI_DEBUG_PRINT(2, ("Mali PP: Hard reset of core %s\n", core->hw_core.description));
	MALI_ASSERT_GROUP_LOCKED(core->group);

	mali_pp_post_process_job(core); /* @@@@ is there some cases where it is unsafe to post process the job here? */

	/* Set register to a bogus value. The register will be used to detect when reset is complete */
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_WRITE_BOUNDARY_LOW, reset_invalid_value);

	/* Force core to reset */
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI200_REG_VAL_CTRL_MGMT_FORCE_RESET);

	/* Wait for reset to be complete */
	for (i = 0; i < reset_finished_loop_count; i++)
	{
		mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_WRITE_BOUNDARY_LOW, reset_check_value);
		if (reset_check_value == mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_WRITE_BOUNDARY_LOW))
		{
			break;
		}
		_mali_osk_time_ubusydelay(10);
	}

	if (i == reset_finished_loop_count)
	{
		MALI_PRINT_ERROR(("Mali PP: The hard reset loop didn't work, unable to recover\n"));
	}

	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_WRITE_BOUNDARY_LOW, 0x00000000); /* set it back to the default */
	/* Re-enable interrupts */
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI200_REG_VAL_IRQ_MASK_ALL);
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_USED);

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_pp_reset(struct mali_pp_core *core)
{
	int i;
	const int request_loop_count = 20;

	MALI_DEBUG_ASSERT_POINTER(core);
	MALI_DEBUG_PRINT(4, ("Mali PP: Reset of core %s\n", core->hw_core.description));
	MALI_ASSERT_GROUP_LOCKED(core->group);

	mali_pp_post_process_job(core); /* @@@@ is there some cases where it is unsafe to post process the job here? */

	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, 0); /* disable the IRQs */

#if defined(USING_MALI200)

	/* On Mali-200, stop the  bus, then do a hard reset of the core */

	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI200_REG_VAL_CTRL_MGMT_STOP_BUS);

	for (i = 0; i < request_loop_count; i++)
	{
		if (mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_STATUS) & MALI200_REG_VAL_STATUS_BUS_STOPPED)
		{
			break;
		}
		_mali_osk_time_ubusydelay(10);
	}

	if (request_loop_count == i)
	{
		MALI_PRINT_ERROR(("Mali PP: Failed to stop bus for core %s, unable to recover\n", core->hw_core.description));
		return _MALI_OSK_ERR_FAULT ;
	}

	/* the bus was stopped OK, do the hard reset */
	mali_pp_hard_reset(core);

#elif defined(USING_MALI400)

	/* Mali-300 and Mali-400 have a safe reset command which we use */

	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI400PP_REG_VAL_IRQ_RESET_COMPLETED);
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI400PP_REG_VAL_CTRL_MGMT_SOFT_RESET);

	for (i = 0; i < request_loop_count; i++)
	{
		if (mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_RAWSTAT) & MALI400PP_REG_VAL_IRQ_RESET_COMPLETED)
		{
			break;
		}
		_mali_osk_time_ubusydelay(10);
	}

	if (request_loop_count == i)
	{
		MALI_DEBUG_PRINT(2, ("Mali PP: Failed to reset core %s, Status: 0x%08x\n", core->hw_core.description, mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_STATUS)));
		return _MALI_OSK_ERR_FAULT;
	}
#else
#error "no supported mali core defined"
#endif

	/* Re-enable interrupts */
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI200_REG_VAL_IRQ_MASK_ALL);
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_USED);

	return _MALI_OSK_ERR_OK;
}

void mali_pp_job_start(struct mali_pp_core *core, struct mali_pp_job *job, u32 sub_job)
{
	u32 *frame_registers = mali_pp_job_get_frame_registers(job);
	u32 *wb0_registers = mali_pp_job_get_wb0_registers(job);
	u32 *wb1_registers = mali_pp_job_get_wb1_registers(job);
	u32 *wb2_registers = mali_pp_job_get_wb2_registers(job);
	core->counter_src0_used = core->counter_src0;
	core->counter_src1_used = core->counter_src1;

	MALI_DEBUG_ASSERT_POINTER(core);
	MALI_ASSERT_GROUP_LOCKED(core->group);

	mali_hw_core_register_write_array_relaxed(&core->hw_core, MALI200_REG_ADDR_FRAME, frame_registers, MALI200_NUM_REGS_FRAME);
	if (0 != sub_job)
	{
		/*
		 * There are two frame registers which are different for each sub job.
		 * For the first sub job, these are correctly represented in the frame register array,
		 * but we need to patch these for all other sub jobs
		 */
		mali_hw_core_register_write_relaxed(&core->hw_core, MALI200_REG_ADDR_FRAME, mali_pp_job_get_addr_frame(job, sub_job));
		mali_hw_core_register_write_relaxed(&core->hw_core, MALI200_REG_ADDR_STACK, mali_pp_job_get_addr_stack(job, sub_job));
	}

	if (wb0_registers[0]) /* M200_WB0_REG_SOURCE_SELECT register */
	{
		mali_hw_core_register_write_array_relaxed(&core->hw_core, MALI200_REG_ADDR_WB0, wb0_registers, MALI200_NUM_REGS_WBx);
	}

	if (wb1_registers[0]) /* M200_WB1_REG_SOURCE_SELECT register */
	{
		mali_hw_core_register_write_array_relaxed(&core->hw_core, MALI200_REG_ADDR_WB1, wb1_registers, MALI200_NUM_REGS_WBx);
	}

	if (wb2_registers[0]) /* M200_WB2_REG_SOURCE_SELECT register */
	{
		mali_hw_core_register_write_array_relaxed(&core->hw_core, MALI200_REG_ADDR_WB2, wb2_registers, MALI200_NUM_REGS_WBx);
	}

	/* This selects which performance counters we are reading */
	if (MALI_HW_CORE_NO_COUNTER != core->counter_src0_used || MALI_HW_CORE_NO_COUNTER != core->counter_src1_used)
	{
		/* global_config has enabled HW counters, this will override anything specified by user space */
		if (MALI_HW_CORE_NO_COUNTER != core->counter_src0_used)
		{
			mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_SRC, core->counter_src0_used);
			mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_ENABLE, MALI200_REG_VAL_PERF_CNT_ENABLE);
		}
		if (MALI_HW_CORE_NO_COUNTER != core->counter_src1_used)
		{
			mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_SRC, core->counter_src1_used);
			mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_ENABLE, MALI200_REG_VAL_PERF_CNT_ENABLE);
		}
	}
	else
	{
		/* Use HW counters from job object, if any */
		u32 perf_counter_flag = mali_pp_job_get_perf_counter_flag(job);
		if (0 != perf_counter_flag)
		{
			if (perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE)
			{
				core->counter_src0_used = mali_pp_job_get_perf_counter_src0(job);
				mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_SRC, core->counter_src0_used);
				mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_ENABLE, MALI200_REG_VAL_PERF_CNT_ENABLE);
			}

			if (perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE)
			{
				core->counter_src1_used = mali_pp_job_get_perf_counter_src1(job);
				mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_SRC, core->counter_src1_used);
				mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_ENABLE, MALI200_REG_VAL_PERF_CNT_ENABLE);
			}
		}
	}

	MALI_DEBUG_PRINT(3, ("Mali PP: Starting job 0x%08X part %u/%u on PP core %s\n", job, sub_job + 1, mali_pp_job_get_sub_job_count(job), core->hw_core.description));

	/* Adding barrier to make sure all rester writes are finished */
	_mali_osk_write_mem_barrier();

	/* This is the command that starts the core. */
	mali_hw_core_register_write_relaxed(&core->hw_core, MALI200_REG_ADDR_MGMT_CTRL_MGMT, MALI200_REG_VAL_CTRL_MGMT_START_RENDERING);

	/* Adding barrier to make sure previous rester writes is finished */
	_mali_osk_write_mem_barrier();

	/* Setup the timeout timer value and save the job id for the job running on the pp core */
	_mali_osk_timer_add(core->timeout_timer, _mali_osk_time_mstoticks(mali_max_job_runtime));
	core->timeout_job_id = mali_pp_job_get_id(job);

#if MALI_TIMELINE_PROFILING_ENABLED
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE | MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_id) | MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH, job->frame_builder_id, job->flush_id, 0, 0, 0);
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START|MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_id), job->pid, job->tid, 0, 0, 0);
#endif

	core->running_job = job;
	core->running_sub_job = sub_job;
}

u32 mali_pp_core_get_version(struct mali_pp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_VERSION);
}

u32 mali_pp_core_get_id(struct mali_pp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return core->core_id;
}

mali_bool mali_pp_core_set_counter_src0(struct mali_pp_core *core, u32 counter)
{
	MALI_DEBUG_ASSERT_POINTER(core);

	core->counter_src0 = counter;
	return MALI_TRUE;
}

mali_bool mali_pp_core_set_counter_src1(struct mali_pp_core *core, u32 counter)
{
	MALI_DEBUG_ASSERT_POINTER(core);

	core->counter_src1 = counter;
	return MALI_TRUE;
}

u32 mali_pp_core_get_counter_src0(struct mali_pp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return core->counter_src0;
}

u32 mali_pp_core_get_counter_src1(struct mali_pp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return core->counter_src1;
}

struct mali_pp_core* mali_pp_get_global_pp_core(u32 index)
{
	if (MALI_MAX_NUMBER_OF_PP_CORES > index)
	{
		return mali_global_pp_cores[index];
	}

	return NULL;
}

u32 mali_pp_get_glob_num_pp_cores(void)
{
	return mali_global_num_pp_cores;
}

u32 mali_pp_get_max_num_pp_cores(void)
{
	return MALI_MAX_NUMBER_OF_PP_CORES;
}

/* ------------- interrupt handling below ------------------ */
static _mali_osk_errcode_t mali_pp_upper_half(void *data)
{
	struct mali_pp_core *core = (struct mali_pp_core *)data;
	u32 irq_readout;

	irq_readout = mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_STATUS);
	if (MALI200_REG_VAL_IRQ_MASK_NONE != irq_readout)
	{
		/* Mask out all IRQs from this core until IRQ is handled */
		mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_NONE);

#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_id)|MALI_PROFILING_EVENT_REASON_SINGLE_HW_INTERRUPT, irq_readout, 0, 0, 0, 0);
#endif

		/* We do need to handle this in a bottom half */
		_mali_osk_irq_schedulework(core->irq);
		return _MALI_OSK_ERR_OK;
	}

	return _MALI_OSK_ERR_FAULT;
}

static void mali_pp_bottom_half(void *data)
{
	struct mali_pp_core *core = (struct mali_pp_core *)data;
	u32 irq_readout;
	u32 irq_errors;

#if MALI_TIMELINE_PROFILING_ENABLED
#if 0  /* Bottom half TLP logging is currently not supported */
	_mali_osk_profiling_add_event( MALI_PROFILING_EVENT_TYPE_START| MALI_PROFILING_EVENT_CHANNEL_SOFTWARE ,  _mali_osk_get_pid(), _mali_osk_get_tid(), 0, 0, 0);
#endif
#endif

	mali_group_lock(core->group); /* Group lock grabbed in core handlers, but released in common group handler */

	if ( MALI_FALSE == mali_group_power_is_on(core->group) )
	{
		MALI_PRINT_ERROR(("Interrupt bottom half of %s when core is OFF.", core->hw_core.description));
		mali_group_unlock(core->group);
		return;
	}

	irq_readout = mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_RAWSTAT) & MALI200_REG_VAL_IRQ_MASK_USED;

	MALI_DEBUG_PRINT(4, ("Mali PP: Bottom half IRQ 0x%08X from core %s\n", irq_readout, core->hw_core.description));

	if (irq_readout & MALI200_REG_VAL_IRQ_END_OF_FRAME)
	{
		mali_pp_post_process_job(core);
		MALI_DEBUG_PRINT(3, ("Mali PP: Job completed, calling group handler\n"));
		mali_group_bottom_half(core->group, GROUP_EVENT_PP_JOB_COMPLETED); /* Will release group lock */
		return;
	}

	/*
	 * Now lets look at the possible error cases (IRQ indicating error or timeout)
	 * END_OF_FRAME and HANG interrupts are not considered error.
	 */
	irq_errors = irq_readout & ~(MALI200_REG_VAL_IRQ_END_OF_FRAME|MALI200_REG_VAL_IRQ_HANG);
	if (0 != irq_errors)
	{
		mali_pp_post_process_job(core);
		MALI_PRINT_ERROR(("Mali PP: Unknown interrupt 0x%08X from core %s, aborting job\n",
		                  irq_readout, core->hw_core.description));
		mali_group_bottom_half(core->group, GROUP_EVENT_PP_JOB_FAILED); /* Will release group lock */
		return;
	}
	else if (MALI_TRUE == core->core_timed_out) /* SW timeout */
	{
		if (core->timeout_job_id == mali_pp_job_get_id(core->running_job))
		{
			mali_pp_post_process_job(core);
			MALI_DEBUG_PRINT(2, ("Mali PP: Job %d timed out on core %s\n",
			                 mali_pp_job_get_id(core->running_job), core->hw_core.description));
			mali_group_bottom_half(core->group, GROUP_EVENT_PP_JOB_TIMED_OUT); /* Will release group lock */
		}
		else
		{
			mali_group_unlock(core->group);
		}
		core->core_timed_out = MALI_FALSE;
		return;
	}
	else if (irq_readout & MALI200_REG_VAL_IRQ_HANG)
	{
		/* Just ignore hang interrupts, the job timer will detect hanging jobs anyways */
		mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI200_REG_VAL_IRQ_HANG);
	}

	/*
	 * The only way to get here is if we got a HANG interrupt, which we ignore.
	 * Re-enable interrupts and let core continue to run
	 */
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_USED);
	mali_group_unlock(core->group);

#if MALI_TIMELINE_PROFILING_ENABLED
#if 0   /* Bottom half TLP logging is currently not supported */
	_mali_osk_profiling_add_event( MALI_PROFILING_EVENT_TYPE_STOP| MALI_PROFILING_EVENT_CHANNEL_SOFTWARE ,  _mali_osk_get_pid(), _mali_osk_get_tid(), 0, 0, 0);
#endif
#endif
}

static void mali_pp_irq_probe_trigger(void *data)
{
	struct mali_pp_core *core = (struct mali_pp_core *)data;
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK, MALI200_REG_VAL_IRQ_MASK_USED);     /* @@@@ This should not be needed */
	mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_RAWSTAT, MALI200_REG_VAL_IRQ_FORCE_HANG);
	_mali_osk_mem_barrier();
}

static _mali_osk_errcode_t mali_pp_irq_probe_ack(void *data)
{
	struct mali_pp_core *core = (struct mali_pp_core *)data;
	u32 irq_readout;

	irq_readout = mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_STATUS);
	if (MALI200_REG_VAL_IRQ_FORCE_HANG & irq_readout)
	{
		mali_hw_core_register_write(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_CLEAR, MALI200_REG_VAL_IRQ_FORCE_HANG);
		_mali_osk_mem_barrier();
		return _MALI_OSK_ERR_OK;
	}

	return _MALI_OSK_ERR_FAULT;
}


/* ------ local helper functions below --------- */
static void mali_pp_post_process_job(struct mali_pp_core *core)
{
	MALI_ASSERT_GROUP_LOCKED(core->group);

	if (NULL != core->running_job)
	{
		u32 val0 = 0;
		u32 val1 = 0;
#if MALI_TIMELINE_PROFILING_ENABLED
		int counter_index = COUNTER_FP0_C0 + (2 * core->core_id);
#endif

		if (MALI_HW_CORE_NO_COUNTER != core->counter_src0_used)
		{
			val0 = mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_VALUE);
			if (mali_pp_job_get_perf_counter_flag(core->running_job) &&
			    _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE && mali_pp_job_get_perf_counter_src0(core->running_job) == core->counter_src0_used)
			{
				/* We retrieved the counter that user space asked for, so return the value through the job object */
				mali_pp_job_set_perf_counter_value0(core->running_job, core->running_sub_job, val0);
			}
			else
			{
				/* User space asked for a counter, but this is not what we retrived (overridden by counter src set on core) */
				mali_pp_job_set_perf_counter_value0(core->running_job, core->running_sub_job, MALI_HW_CORE_INVALID_VALUE);
			}

#if MALI_TIMELINE_PROFILING_ENABLED
			_mali_osk_profiling_report_hw_counter(counter_index, val0);
#endif
		}

		if (MALI_HW_CORE_NO_COUNTER != core->counter_src1_used)
		{
			val1 = mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_VALUE);
			if (mali_pp_job_get_perf_counter_flag(core->running_job) &&
			    _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE && mali_pp_job_get_perf_counter_src1(core->running_job) == core->counter_src1_used)
			{
				/* We retrieved the counter that user space asked for, so return the value through the job object */
				mali_pp_job_set_perf_counter_value1(core->running_job, core->running_sub_job, val1);
			}
			else
			{
				/* User space asked for a counter, but this is not what we retrived (overridden by counter src set on core) */
				mali_pp_job_set_perf_counter_value1(core->running_job, core->running_sub_job, MALI_HW_CORE_INVALID_VALUE);
			}

#if MALI_TIMELINE_PROFILING_ENABLED
			_mali_osk_profiling_report_hw_counter(counter_index + 1, val1);
#endif
		}

#if MALI_TIMELINE_PROFILING_ENABLED
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_id),
		                          val0, val1, core->counter_src0_used | (core->counter_src1_used << 8), 0, 0);
#endif

		/* We are no longer running a job... */
		core->running_job = NULL;
		_mali_osk_timer_del(core->timeout_timer);
	}
}

/* callback function for pp core timeout */
static void mali_pp_timeout(void *data)
{
	struct mali_pp_core * core = ((struct mali_pp_core *)data);

	MALI_DEBUG_PRINT(3, ("Mali PP: TIMEOUT callback \n"));
	core->core_timed_out = MALI_TRUE;
	_mali_osk_irq_schedulework(core->irq);
}

#if 0
static void mali_pp_print_registers(struct mali_pp_core *core)
{
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_VERSION = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_VERSION)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_CURRENT_REND_LIST_ADDR = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_CURRENT_REND_LIST_ADDR)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_STATUS = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_STATUS)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_INT_RAWSTAT = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_RAWSTAT)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_INT_MASK = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_MASK)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_INT_STATUS = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_INT_STATUS)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_BUS_ERROR_STATUS = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_BUS_ERROR_STATUS)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_PERF_CNT_0_ENABLE = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_ENABLE)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_PERF_CNT_0_SRC = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_SRC)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_PERF_CNT_0_VALUE = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_0_VALUE)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_PERF_CNT_1_ENABLE = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_ENABLE)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_PERF_CNT_1_SRC = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_SRC)));
	MALI_DEBUG_PRINT(2, ("Mali PP: Register MALI200_REG_ADDR_MGMT_PERF_CNT_1_VALUE = 0x%08X\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_PERF_CNT_1_VALUE)));
}
#endif

#if 0
void mali_pp_print_state(struct mali_pp_core *core)
{
	MALI_DEBUG_PRINT(2, ("Mali PP: State: 0x%08x\n", mali_hw_core_register_read(&core->hw_core, MALI200_REG_ADDR_MGMT_STATUS) ));
}
#endif

#if MALI_STATE_TRACKING
u32 mali_pp_dump_state(struct mali_pp_core *core, char *buf, u32 size)
{
	int n = 0;

	n += _mali_osk_snprintf(buf + n, size - n, "\tPP #%d: %s\n", core->core_id, core->hw_core.description);

	return n;
}
#endif
