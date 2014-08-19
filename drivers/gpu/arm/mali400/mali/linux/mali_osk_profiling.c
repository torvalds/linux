/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include <linux/module.h>

#include <mali_profiling_gator_api.h>
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_ukk.h"
#include "mali_uk_types.h"
#include "mali_osk_profiling.h"
#include "mali_linux_trace.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_pp_scheduler.h"
#include "mali_l2_cache.h"
#include "mali_user_settings_db.h"

_mali_osk_errcode_t _mali_osk_profiling_init(mali_bool auto_start)
{
	if (MALI_TRUE == auto_start) {
		mali_set_user_setting(_MALI_UK_USER_SETTING_SW_EVENTS_ENABLE, MALI_TRUE);
	}

	return _MALI_OSK_ERR_OK;
}

void _mali_osk_profiling_term(void)
{
	/* Nothing to do */
}

void _mali_osk_profiling_report_sw_counters(u32 *counters)
{
	trace_mali_sw_counters(_mali_osk_get_pid(), _mali_osk_get_tid(), NULL, counters);
}

void _mali_osk_profiling_memory_usage_get(u32 *memory_usage)
{
	*memory_usage = _mali_ukk_report_memory_usage();
}

_mali_osk_errcode_t _mali_ukk_profiling_add_event(_mali_uk_profiling_add_event_s *args)
{
	/* Always add process and thread identificator in the first two data elements for events from user space */
	_mali_osk_profiling_add_event(args->event_id, _mali_osk_get_pid(), _mali_osk_get_tid(), args->data[2], args->data[3], args->data[4]);

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_sw_counters_report(_mali_uk_sw_counters_report_s *args)
{
	u32 *counters = (u32 *)(uintptr_t)args->counters;

	_mali_osk_profiling_report_sw_counters(counters);

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_profiling_memory_usage_get(_mali_uk_profiling_memory_usage_get_s *args)
{
	_mali_osk_profiling_memory_usage_get(&args->memory_usage);
	return _MALI_OSK_ERR_OK;
}

/**
 * Called by gator.ko to set HW counters
 *
 * @param counter_id The counter ID.
 * @param event_id Event ID that the counter should count (HW counter value from TRM).
 *
 * @return 1 on success, 0 on failure.
 */
int _mali_profiling_set_event(u32 counter_id, s32 event_id)
{
	if (COUNTER_VP_0_C0 == counter_id) {
		mali_gp_job_set_gp_counter_src0(event_id);
	} else if (COUNTER_VP_0_C1 == counter_id) {
		mali_gp_job_set_gp_counter_src1(event_id);
	} else if (COUNTER_FP_0_C0 <= counter_id && COUNTER_FP_7_C1 >= counter_id) {
		/*
		 * Two compatibility notes for this function:
		 *
		 * 1) Previously the DDK allowed per core counters.
		 *
		 *    This did not make much sense on Mali-450 with the "virtual PP core" concept,
		 *    so this option was removed, and only the same pair of HW counters was allowed on all cores,
		 *    beginning with r3p2 release.
		 *
		 *    Starting with r4p0, it is now possible to set different HW counters for the different sub jobs.
		 *    This should be almost the same, since sub job 0 is designed to run on core 0,
		 *    sub job 1 on core 1, and so on.
		 *
		 *    The scheduling of PP sub jobs is not predictable, and this often led to situations where core 0 ran 2
		 *    sub jobs, while for instance core 1 ran zero. Having the counters set per sub job would thus increase
		 *    the predictability of the returned data (as you would be guaranteed data for all the selected HW counters).
		 *
		 *    PS: Core scaling needs to be disabled in order to use this reliably (goes for both solutions).
		 *
		 *    The framework/#defines with Gator still indicates that the counter is for a particular core,
		 *    but this is internally used as a sub job ID instead (no translation needed).
		 *
		 *  2) Global/default vs per sub job counters
		 *
		 *     Releases before r3p2 had only per PP core counters.
		 *     r3p2 releases had only one set of default/global counters which applied to all PP cores
		 *     Starting with r4p0, we have both a set of default/global counters,
		 *     and individual counters per sub job (equal to per core).
		 *
		 *     To keep compatibility with Gator/DS-5/streamline, the following scheme is used:
		 *
		 *     r3p2 release; only counters set for core 0 is handled,
		 *     this is applied as the default/global set of counters, and will thus affect all cores.
		 *
		 *     r4p0 release; counters set for core 0 is applied as both the global/default set of counters,
		 *     and counters for sub job 0.
		 *     Counters set for core 1-7 is only applied for the corresponding sub job.
		 *
		 *     This should allow the DS-5/Streamline GUI to have a simple mode where it only allows setting the
		 *     values for core 0, and thus this will be applied to all PP sub jobs/cores.
		 *     Advanced mode will also be supported, where individual pairs of HW counters can be selected.
		 *
		 *     The GUI will (until it is updated) still refer to cores instead of sub jobs, but this is probably
		 *     something we can live with!
		 *
		 *     Mali-450 note: Each job is not divided into a deterministic number of sub jobs, as the HW DLBU
		 *     automatically distributes the load between whatever number of cores is available at this particular time.
		 *     A normal PP job on Mali-450 is thus considered a single (virtual) job, and it will thus only be possible
		 *     to use a single pair of HW counters (even if the job ran on multiple PP cores).
		 *     In other words, only the global/default pair of PP HW counters will be used for normal Mali-450 jobs.
		 */
		u32 sub_job = (counter_id - COUNTER_FP_0_C0) >> 1;
		u32 counter_src = (counter_id - COUNTER_FP_0_C0) & 1;
		if (0 == counter_src) {
			mali_pp_job_set_pp_counter_sub_job_src0(sub_job, event_id);
			if (0 == sub_job) {
				mali_pp_job_set_pp_counter_global_src0(event_id);
			}
		} else {
			mali_pp_job_set_pp_counter_sub_job_src1(sub_job, event_id);
			if (0 == sub_job) {
				mali_pp_job_set_pp_counter_global_src1(event_id);
			}
		}
	} else if (COUNTER_L2_0_C0 <= counter_id && COUNTER_L2_2_C1 >= counter_id) {
		u32 core_id = (counter_id - COUNTER_L2_0_C0) >> 1;
		struct mali_l2_cache_core *l2_cache_core = mali_l2_cache_core_get_glob_l2_core(core_id);

		if (NULL != l2_cache_core) {
			u32 counter_src = (counter_id - COUNTER_L2_0_C0) & 1;
			if (0 == counter_src) {
				mali_l2_cache_core_set_counter_src0(l2_cache_core, event_id);
			} else {
				mali_l2_cache_core_set_counter_src1(l2_cache_core, event_id);
			}
		}
	} else {
		return 0; /* Failure, unknown event */
	}

	return 1; /* success */
}

/**
 * Called by gator.ko to retrieve the L2 cache counter values for all L2 cache cores.
 * The L2 cache counters are unique in that they are polled by gator, rather than being
 * transmitted via the tracepoint mechanism.
 *
 * @param values Pointer to a _mali_profiling_l2_counter_values structure where
 *               the counter sources and values will be output
 * @return 0 if all went well; otherwise, return the mask with the bits set for the powered off cores
 */
u32 _mali_profiling_get_l2_counters(_mali_profiling_l2_counter_values *values)
{
	struct mali_l2_cache_core *l2_cache;
	u32 l2_cores_num = mali_l2_cache_core_get_glob_num_l2_cores();
	u32 i;
	u32 ret = 0;

	MALI_DEBUG_ASSERT(l2_cores_num <= 3);

	for (i = 0; i < l2_cores_num; i++) {
		l2_cache = mali_l2_cache_core_get_glob_l2_core(i);

		if (NULL == l2_cache) {
			continue;
		}

		if (MALI_TRUE == mali_l2_cache_lock_power_state(l2_cache)) {
			/* It is now safe to access the L2 cache core in order to retrieve the counters */
			mali_l2_cache_core_get_counter_values(l2_cache,
							      &values->cores[i].source0,
							      &values->cores[i].value0,
							      &values->cores[i].source1,
							      &values->cores[i].value1);
		} else {
			/* The core was not available, set the right bit in the mask. */
			ret |= (1 << i);
		}
		mali_l2_cache_unlock_power_state(l2_cache);
	}

	return ret;
}

/**
 * Called by gator to control the production of profiling information at runtime.
 */
void _mali_profiling_control(u32 action, u32 value)
{
	switch (action) {
	case FBDUMP_CONTROL_ENABLE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_COLORBUFFER_CAPTURE_ENABLED, (value == 0 ? MALI_FALSE : MALI_TRUE));
		break;
	case FBDUMP_CONTROL_RATE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_BUFFER_CAPTURE_N_FRAMES, value);
		break;
	case SW_COUNTER_ENABLE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_SW_COUNTER_ENABLED, value);
		break;
	case FBDUMP_CONTROL_RESIZE_FACTOR:
		mali_set_user_setting(_MALI_UK_USER_SETTING_BUFFER_CAPTURE_RESIZE_FACTOR, value);
		break;
	default:
		break;  /* Ignore unimplemented actions */
	}
}

/**
 * Called by gator to get mali api version.
 */
u32 _mali_profiling_get_api_version(void)
{
	return MALI_PROFILING_API_VERSION;
}

/**
* Called by gator to get the data about Mali instance in use:
* product id, version, number of cores
*/
void _mali_profiling_get_mali_version(struct _mali_profiling_mali_version *values)
{
	values->mali_product_id = (u32)mali_kernel_core_get_product_id();
	values->mali_version_major = mali_kernel_core_get_gpu_major_version();
	values->mali_version_minor = mali_kernel_core_get_gpu_minor_version();
	values->num_of_l2_cores = mali_l2_cache_core_get_glob_num_l2_cores();
	values->num_of_fp_cores = mali_pp_scheduler_get_num_cores_total();
	values->num_of_vp_cores = 1;
}


EXPORT_SYMBOL(_mali_profiling_set_event);
EXPORT_SYMBOL(_mali_profiling_get_l2_counters);
EXPORT_SYMBOL(_mali_profiling_control);
EXPORT_SYMBOL(_mali_profiling_get_api_version);
EXPORT_SYMBOL(_mali_profiling_get_mali_version);
