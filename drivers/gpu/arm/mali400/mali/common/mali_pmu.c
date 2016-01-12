/*
 * Copyright (C) 2010-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_pmu.c
 * Mali driver functions for Mali 400 PMU hardware
 */
#include "mali_hw_core.h"
#include "mali_pmu.h"
#include "mali_pp.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_pm.h"
#include "mali_osk_mali.h"

struct mali_pmu_core *mali_global_pmu_core = NULL;

static _mali_osk_errcode_t mali_pmu_wait_for_command_finish(
	struct mali_pmu_core *pmu);

struct mali_pmu_core *mali_pmu_create(_mali_osk_resource_t *resource)
{
	struct mali_pmu_core *pmu;

	MALI_DEBUG_ASSERT(NULL == mali_global_pmu_core);
	MALI_DEBUG_PRINT(2, ("Mali PMU: Creating Mali PMU core\n"));

	pmu = (struct mali_pmu_core *)_mali_osk_malloc(
		      sizeof(struct mali_pmu_core));
	if (NULL != pmu) {
		pmu->registered_cores_mask = 0; /* to be set later */

		if (_MALI_OSK_ERR_OK == mali_hw_core_create(&pmu->hw_core,
				resource, PMU_REGISTER_ADDRESS_SPACE_SIZE)) {

			pmu->switch_delay = _mali_osk_get_pmu_switch_delay();

			mali_global_pmu_core = pmu;

			return pmu;
		}
		_mali_osk_free(pmu);
	}

	return NULL;
}

void mali_pmu_delete(struct mali_pmu_core *pmu)
{
	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu == mali_global_pmu_core);

	MALI_DEBUG_PRINT(2, ("Mali PMU: Deleting Mali PMU core\n"));

	mali_global_pmu_core = NULL;

	mali_hw_core_delete(&pmu->hw_core);
	_mali_osk_free(pmu);
}

void mali_pmu_set_registered_cores_mask(struct mali_pmu_core *pmu, u32 mask)
{
	pmu->registered_cores_mask = mask;
}

void mali_pmu_reset(struct mali_pmu_core *pmu)
{
	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);

	/* Setup the desired defaults */
	mali_hw_core_register_write_relaxed(&pmu->hw_core,
					    PMU_REG_ADDR_MGMT_INT_MASK, 0);
	mali_hw_core_register_write_relaxed(&pmu->hw_core,
					    PMU_REG_ADDR_MGMT_SW_DELAY, pmu->switch_delay);
}

void mali_pmu_power_up_all(struct mali_pmu_core *pmu)
{
	u32 stat;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);

	mali_pm_exec_lock();

	mali_pmu_reset(pmu);

	/* Now simply power up the domains which are marked as powered down */
	stat = mali_hw_core_register_read(&pmu->hw_core,
					  PMU_REG_ADDR_MGMT_STATUS);
	mali_pmu_power_up(pmu, stat);

	mali_pm_exec_unlock();
}

void mali_pmu_power_down_all(struct mali_pmu_core *pmu)
{
	u32 stat;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);

	mali_pm_exec_lock();

	/* Now simply power down the domains which are marked as powered up */
	stat = mali_hw_core_register_read(&pmu->hw_core,
					  PMU_REG_ADDR_MGMT_STATUS);
	mali_pmu_power_down(pmu, (~stat) & pmu->registered_cores_mask);

	mali_pm_exec_unlock();
}

_mali_osk_errcode_t mali_pmu_power_down(struct mali_pmu_core *pmu, u32 mask)
{
	u32 stat;
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);
	MALI_DEBUG_ASSERT(mask <= pmu->registered_cores_mask);
	MALI_DEBUG_ASSERT(0 == (mali_hw_core_register_read(&pmu->hw_core,
				PMU_REG_ADDR_MGMT_INT_RAWSTAT) &
				PMU_REG_VAL_IRQ));

	MALI_DEBUG_PRINT(3,
			 ("PMU power down: ...................... [%s]\n",
			  mali_pm_mask_to_string(mask)));

	stat = mali_hw_core_register_read(&pmu->hw_core,
					  PMU_REG_ADDR_MGMT_STATUS);

	/*
	 * Assert that we are not powering down domains which are already
	 * powered down.
	 */
	MALI_DEBUG_ASSERT(0 == (stat & mask));

	mask  &= ~(0x1 << MALI_DOMAIN_INDEX_DUMMY);

	if (0 == mask || 0 == ((~stat) & mask)) return _MALI_OSK_ERR_OK;

	mali_hw_core_register_write(&pmu->hw_core,
				    PMU_REG_ADDR_MGMT_POWER_DOWN, mask);

	/*
	 * Do not wait for interrupt on Mali-300/400 if all domains are
	 * powered off by our power down command, because the HW will simply
	 * not generate an interrupt in this case.
	 */
	if (mali_is_mali450() || mali_is_mali470() || pmu->registered_cores_mask != (mask | stat)) {
		err = mali_pmu_wait_for_command_finish(pmu);
		if (_MALI_OSK_ERR_OK != err) {
			return err;
		}
	} else {
		mali_hw_core_register_write(&pmu->hw_core,
					    PMU_REG_ADDR_MGMT_INT_CLEAR, PMU_REG_VAL_IRQ);
	}

#if defined(DEBUG)
	/* Verify power status of domains after power down */
	stat = mali_hw_core_register_read(&pmu->hw_core,
					  PMU_REG_ADDR_MGMT_STATUS);
	MALI_DEBUG_ASSERT(mask == (stat & mask));
#endif

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_pmu_power_up(struct mali_pmu_core *pmu, u32 mask)
{
	u32 stat;
	_mali_osk_errcode_t err;
#if !defined(CONFIG_MALI_PMU_PARALLEL_POWER_UP)
	u32 current_domain;
#endif

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);
	MALI_DEBUG_ASSERT(mask <= pmu->registered_cores_mask);
	MALI_DEBUG_ASSERT(0 == (mali_hw_core_register_read(&pmu->hw_core,
				PMU_REG_ADDR_MGMT_INT_RAWSTAT) &
				PMU_REG_VAL_IRQ));

	MALI_DEBUG_PRINT(3,
			 ("PMU power up: ........................ [%s]\n",
			  mali_pm_mask_to_string(mask)));

	stat = mali_hw_core_register_read(&pmu->hw_core,
					  PMU_REG_ADDR_MGMT_STATUS);
	stat &= pmu->registered_cores_mask;

	mask  &= ~(0x1 << MALI_DOMAIN_INDEX_DUMMY);
	if (0 == mask || 0 == (stat & mask)) return _MALI_OSK_ERR_OK;

	/*
	 * Assert that we are only powering up domains which are currently
	 * powered down.
	 */
	MALI_DEBUG_ASSERT(mask == (stat & mask));

#if defined(CONFIG_MALI_PMU_PARALLEL_POWER_UP)
	mali_hw_core_register_write(&pmu->hw_core,
				    PMU_REG_ADDR_MGMT_POWER_UP, mask);

	err = mali_pmu_wait_for_command_finish(pmu);
	if (_MALI_OSK_ERR_OK != err) {
		return err;
	}
#else
	for (current_domain = 1;
	     current_domain <= pmu->registered_cores_mask;
	     current_domain <<= 1) {
		if (current_domain & mask & stat) {
			mali_hw_core_register_write(&pmu->hw_core,
						    PMU_REG_ADDR_MGMT_POWER_UP,
						    current_domain);

			err = mali_pmu_wait_for_command_finish(pmu);
			if (_MALI_OSK_ERR_OK != err) {
				return err;
			}
		}
	}
#endif

#if defined(DEBUG)
	/* Verify power status of domains after power up */
	stat = mali_hw_core_register_read(&pmu->hw_core,
					  PMU_REG_ADDR_MGMT_STATUS);
	MALI_DEBUG_ASSERT(0 == (stat & mask));
#endif /* defined(DEBUG) */

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_pmu_wait_for_command_finish(
	struct mali_pmu_core *pmu)
{
	u32 rawstat;
	u32 timeout = MALI_REG_POLL_COUNT_SLOW;

	MALI_DEBUG_ASSERT(pmu);

	/* Wait for the command to complete */
	do {
		rawstat = mali_hw_core_register_read(&pmu->hw_core,
						     PMU_REG_ADDR_MGMT_INT_RAWSTAT);
		--timeout;
	} while (0 == (rawstat & PMU_REG_VAL_IRQ) && 0 < timeout);

	MALI_DEBUG_ASSERT(0 < timeout);

	if (0 == timeout) {
		return _MALI_OSK_ERR_TIMEOUT;
	}

	mali_hw_core_register_write(&pmu->hw_core,
				    PMU_REG_ADDR_MGMT_INT_CLEAR, PMU_REG_VAL_IRQ);

	return _MALI_OSK_ERR_OK;
}
