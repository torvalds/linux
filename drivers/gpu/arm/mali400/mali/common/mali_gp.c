/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_gp.h"
#include "mali_hw_core.h"
#include "mali_group.h"
#include "mali_osk.h"
#include "regs/mali_gp_regs.h"
#include "mali_kernel_common.h"
#include "mali_kernel_core.h"
#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif

static struct mali_gp_core *mali_global_gp_core = NULL;

/* Interrupt handlers */
static void mali_gp_irq_probe_trigger(void *data);
static _mali_osk_errcode_t mali_gp_irq_probe_ack(void *data);

struct mali_gp_core *mali_gp_create(const _mali_osk_resource_t * resource, struct mali_group *group)
{
	struct mali_gp_core* core = NULL;

	MALI_DEBUG_ASSERT(NULL == mali_global_gp_core);
	MALI_DEBUG_PRINT(2, ("Mali GP: Creating Mali GP core: %s\n", resource->description));

	core = _mali_osk_malloc(sizeof(struct mali_gp_core));
	if (NULL != core) {
		if (_MALI_OSK_ERR_OK == mali_hw_core_create(&core->hw_core, resource, MALIGP2_REGISTER_ADDRESS_SPACE_SIZE)) {
			_mali_osk_errcode_t ret;

			ret = mali_gp_reset(core);

			if (_MALI_OSK_ERR_OK == ret) {
				ret = mali_group_add_gp_core(group, core);
				if (_MALI_OSK_ERR_OK == ret) {
					/* Setup IRQ handlers (which will do IRQ probing if needed) */
					core->irq = _mali_osk_irq_init(resource->irq,
					                               mali_group_upper_half_gp,
					                               group,
					                               mali_gp_irq_probe_trigger,
					                               mali_gp_irq_probe_ack,
					                               core,
					                               resource->description);
					if (NULL != core->irq) {
						MALI_DEBUG_PRINT(4, ("Mali GP: set global gp core from 0x%08X to 0x%08X\n", mali_global_gp_core, core));
						mali_global_gp_core = core;

						return core;
					} else {
						MALI_PRINT_ERROR(("Mali GP: Failed to setup interrupt handlers for GP core %s\n", core->hw_core.description));
					}
					mali_group_remove_gp_core(group);
				} else {
					MALI_PRINT_ERROR(("Mali GP: Failed to add core %s to group\n", core->hw_core.description));
				}
			}
			mali_hw_core_delete(&core->hw_core);
		}

		_mali_osk_free(core);
	} else {
		MALI_PRINT_ERROR(("Failed to allocate memory for GP core\n"));
	}

	return NULL;
}

void mali_gp_delete(struct mali_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);

	_mali_osk_irq_term(core->irq);
	mali_hw_core_delete(&core->hw_core);
	mali_global_gp_core = NULL;
	_mali_osk_free(core);
}

void mali_gp_stop_bus(struct mali_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);

	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_STOP_BUS);
}

_mali_osk_errcode_t mali_gp_stop_bus_wait(struct mali_gp_core *core)
{
	int i;

	MALI_DEBUG_ASSERT_POINTER(core);

	/* Send the stop bus command. */
	mali_gp_stop_bus(core);

	/* Wait for bus to be stopped */
	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; i++) {
		if (mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_STATUS) & MALIGP2_REG_VAL_STATUS_BUS_STOPPED) {
			break;
		}
	}

	if (MALI_REG_POLL_COUNT_FAST == i) {
		MALI_PRINT_ERROR(("Mali GP: Failed to stop bus on %s\n", core->hw_core.description));
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

void mali_gp_hard_reset(struct mali_gp_core *core)
{
	const u32 reset_wait_target_register = MALIGP2_REG_ADDR_MGMT_WRITE_BOUND_LOW;
	const u32 reset_invalid_value = 0xC0FFE000;
	const u32 reset_check_value = 0xC01A0000;
	const u32 reset_default_value = 0;
	int i;

	MALI_DEBUG_ASSERT_POINTER(core);
	MALI_DEBUG_PRINT(4, ("Mali GP: Hard reset of core %s\n", core->hw_core.description));

	mali_hw_core_register_write(&core->hw_core, reset_wait_target_register, reset_invalid_value);

	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_RESET);

	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; i++) {
		mali_hw_core_register_write(&core->hw_core, reset_wait_target_register, reset_check_value);
		if (reset_check_value == mali_hw_core_register_read(&core->hw_core, reset_wait_target_register)) {
			break;
		}
	}

	if (MALI_REG_POLL_COUNT_FAST == i) {
		MALI_PRINT_ERROR(("Mali GP: The hard reset loop didn't work, unable to recover\n"));
	}

	mali_hw_core_register_write(&core->hw_core, reset_wait_target_register, reset_default_value); /* set it back to the default */
	/* Re-enable interrupts */
	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_MASK_ALL);
	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED);

}

void mali_gp_reset_async(struct mali_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);

	MALI_DEBUG_PRINT(4, ("Mali GP: Reset of core %s\n", core->hw_core.description));

	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, 0); /* disable the IRQs */
	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALI400GP_REG_VAL_IRQ_RESET_COMPLETED);
	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALI400GP_REG_VAL_CMD_SOFT_RESET);

}

_mali_osk_errcode_t mali_gp_reset_wait(struct mali_gp_core *core)
{
	int i;
	u32 rawstat = 0;

	MALI_DEBUG_ASSERT_POINTER(core);

	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; i++) {
		rawstat = mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT);
		if (rawstat & MALI400GP_REG_VAL_IRQ_RESET_COMPLETED) {
			break;
		}
	}

	if (i == MALI_REG_POLL_COUNT_FAST) {
		MALI_PRINT_ERROR(("Mali GP: Failed to reset core %s, rawstat: 0x%08x\n",
		                  core->hw_core.description, rawstat));
		return _MALI_OSK_ERR_FAULT;
	}

	/* Re-enable interrupts */
	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_MASK_ALL);
	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED);

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_gp_reset(struct mali_gp_core *core)
{
	mali_gp_reset_async(core);
	return mali_gp_reset_wait(core);
}

void mali_gp_job_start(struct mali_gp_core *core, struct mali_gp_job *job)
{
	u32 startcmd = 0;
	u32 *frame_registers = mali_gp_job_get_frame_registers(job);
	u32 counter_src0 = mali_gp_job_get_perf_counter_src0(job);
	u32 counter_src1 = mali_gp_job_get_perf_counter_src1(job);

	MALI_DEBUG_ASSERT_POINTER(core);

	if (mali_gp_job_has_vs_job(job)) {
		startcmd |= (u32) MALIGP2_REG_VAL_CMD_START_VS;
	}

	if (mali_gp_job_has_plbu_job(job)) {
		startcmd |= (u32) MALIGP2_REG_VAL_CMD_START_PLBU;
	}

	MALI_DEBUG_ASSERT(0 != startcmd);

	mali_hw_core_register_write_array_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_VSCL_START_ADDR, frame_registers, MALIGP2_NUM_REGS_FRAME);

	if (MALI_HW_CORE_NO_COUNTER != counter_src0) {
		mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_SRC, counter_src0);
		mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_ENABLE, MALIGP2_REG_VAL_PERF_CNT_ENABLE);
	}
	if (MALI_HW_CORE_NO_COUNTER != counter_src1) {
		mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_SRC, counter_src1);
		mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_ENABLE, MALIGP2_REG_VAL_PERF_CNT_ENABLE);
	}

	MALI_DEBUG_PRINT(3, ("Mali GP: Starting job (0x%08x) on core %s with command 0x%08X\n", job, core->hw_core.description, startcmd));

	mali_hw_core_register_write_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_UPDATE_PLBU_ALLOC);

	/* Barrier to make sure the previous register write is finished */
	_mali_osk_write_mem_barrier();

	/* This is the command that starts the core. */
	mali_hw_core_register_write_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, startcmd);

	/* Barrier to make sure the previous register write is finished */
	_mali_osk_write_mem_barrier();
}

void mali_gp_resume_with_new_heap(struct mali_gp_core *core, u32 start_addr, u32 end_addr)
{
	u32 irq_readout;

	MALI_DEBUG_ASSERT_POINTER(core);

	irq_readout = mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT);

	if (irq_readout & MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM) {
		mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, (MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM | MALIGP2_REG_VAL_IRQ_HANG));
		mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED); /* re-enable interrupts */
		mali_hw_core_register_write_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR, start_addr);
		mali_hw_core_register_write_relaxed(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_END_ADDR, end_addr);

		MALI_DEBUG_PRINT(3, ("Mali GP: Resuming job\n"));

		mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_CMD, MALIGP2_REG_VAL_CMD_UPDATE_PLBU_ALLOC);
		_mali_osk_write_mem_barrier();
	}
	/*
	 * else: core has been reset between PLBU_OUT_OF_MEM interrupt and this new heap response.
	 * A timeout or a page fault on Mali-200 PP core can cause this behaviour.
	 */
}

u32 mali_gp_core_get_version(struct mali_gp_core *core)
{
	MALI_DEBUG_ASSERT_POINTER(core);
	return mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_VERSION);
}

struct mali_gp_core *mali_gp_get_global_gp_core(void)
{
	return mali_global_gp_core;
}

/* ------------- interrupt handling below ------------------ */
static void mali_gp_irq_probe_trigger(void *data)
{
	struct mali_gp_core *core = (struct mali_gp_core *)data;

	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_VAL_IRQ_MASK_USED);
	mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT, MALIGP2_REG_VAL_CMD_FORCE_HANG);
	_mali_osk_mem_barrier();
}

static _mali_osk_errcode_t mali_gp_irq_probe_ack(void *data)
{
	struct mali_gp_core *core = (struct mali_gp_core *)data;
	u32 irq_readout;

	irq_readout = mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_STAT);
	if (MALIGP2_REG_VAL_IRQ_FORCE_HANG & irq_readout) {
		mali_hw_core_register_write(&core->hw_core, MALIGP2_REG_ADDR_MGMT_INT_CLEAR, MALIGP2_REG_VAL_IRQ_FORCE_HANG);
		_mali_osk_mem_barrier();
		return _MALI_OSK_ERR_OK;
	}

	return _MALI_OSK_ERR_FAULT;
}

/* ------ local helper functions below --------- */
#if MALI_STATE_TRACKING
u32 mali_gp_dump_state(struct mali_gp_core *core, char *buf, u32 size)
{
	int n = 0;

	n += _mali_osk_snprintf(buf + n, size - n, "\tGP: %s\n", core->hw_core.description);

	return n;
}
#endif

void mali_gp_update_performance_counters(struct mali_gp_core *core, struct mali_gp_job *job, mali_bool suspend)
{
	u32 val0 = 0;
	u32 val1 = 0;
	u32 counter_src0 = mali_gp_job_get_perf_counter_src0(job);
	u32 counter_src1 = mali_gp_job_get_perf_counter_src1(job);

	if (MALI_HW_CORE_NO_COUNTER != counter_src0) {
		val0 = mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_VALUE);
		mali_gp_job_set_perf_counter_value0(job, val0);

#if defined(CONFIG_MALI400_PROFILING)
		_mali_osk_profiling_report_hw_counter(COUNTER_VP_0_C0, val0);
#endif

	}

	if (MALI_HW_CORE_NO_COUNTER != counter_src1) {
		val1 = mali_hw_core_register_read(&core->hw_core, MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE);
		mali_gp_job_set_perf_counter_value1(job, val1);

#if defined(CONFIG_MALI400_PROFILING)
		_mali_osk_profiling_report_hw_counter(COUNTER_VP_0_C1, val1);
#endif
	}
}
