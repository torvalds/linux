/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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

u16 mali_pmu_global_domain_config[MALI_MAX_NUMBER_OF_DOMAINS]= {0};

static u32 mali_pmu_detect_mask(void);

/** @brief MALI inbuilt PMU hardware info and PMU hardware has knowledge of cores power mask
 */
struct mali_pmu_core {
	struct mali_hw_core hw_core;
	_mali_osk_spinlock_t *lock;
	u32 registered_cores_mask;
	u32 active_cores_mask;
	u32 switch_delay;
};

static struct mali_pmu_core *mali_global_pmu_core = NULL;

/** @brief Register layout for hardware PMU
 */
typedef enum {
	PMU_REG_ADDR_MGMT_POWER_UP                  = 0x00,     /*< Power up register */
	PMU_REG_ADDR_MGMT_POWER_DOWN                = 0x04,     /*< Power down register */
	PMU_REG_ADDR_MGMT_STATUS                    = 0x08,     /*< Core sleep status register */
	PMU_REG_ADDR_MGMT_INT_MASK                  = 0x0C,     /*< Interrupt mask register */
	PMU_REG_ADDR_MGMT_INT_RAWSTAT               = 0x10,     /*< Interrupt raw status register */
	PMU_REG_ADDR_MGMT_INT_CLEAR                 = 0x18,     /*< Interrupt clear register */
	PMU_REG_ADDR_MGMT_SW_DELAY                  = 0x1C,     /*< Switch delay register */
	PMU_REGISTER_ADDRESS_SPACE_SIZE             = 0x28,     /*< Size of register space */
} pmu_reg_addr_mgmt_addr;

#define PMU_REG_VAL_IRQ 1

struct mali_pmu_core *mali_pmu_create(_mali_osk_resource_t *resource)
{
	struct mali_pmu_core* pmu;

	MALI_DEBUG_ASSERT(NULL == mali_global_pmu_core);
	MALI_DEBUG_PRINT(2, ("Mali PMU: Creating Mali PMU core\n"));

	pmu = (struct mali_pmu_core *)_mali_osk_malloc(sizeof(struct mali_pmu_core));
	if (NULL != pmu) {
		pmu->lock = _mali_osk_spinlock_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_PMU);
		if (NULL != pmu->lock) {
			pmu->registered_cores_mask = mali_pmu_detect_mask();
			pmu->active_cores_mask = pmu->registered_cores_mask;

			if (_MALI_OSK_ERR_OK == mali_hw_core_create(&pmu->hw_core, resource, PMU_REGISTER_ADDRESS_SPACE_SIZE)) {
				_mali_osk_errcode_t err;
				struct _mali_osk_device_data data = { 0, };

				err = _mali_osk_device_data_get(&data);
				if (_MALI_OSK_ERR_OK == err) {
					pmu->switch_delay = data.pmu_switch_delay;
					mali_global_pmu_core = pmu;
					return pmu;
				}
				mali_hw_core_delete(&pmu->hw_core);
			}
			_mali_osk_spinlock_term(pmu->lock);
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

	_mali_osk_spinlock_term(pmu->lock);
	mali_hw_core_delete(&pmu->hw_core);
	_mali_osk_free(pmu);
	mali_global_pmu_core = NULL;
}

static void mali_pmu_lock(struct mali_pmu_core *pmu)
{
	_mali_osk_spinlock_lock(pmu->lock);
}
static void mali_pmu_unlock(struct mali_pmu_core *pmu)
{
	_mali_osk_spinlock_unlock(pmu->lock);
}

static _mali_osk_errcode_t mali_pmu_wait_for_command_finish(struct mali_pmu_core *pmu)
{
	u32 rawstat;
	u32 timeout = MALI_REG_POLL_COUNT_SLOW;

	MALI_DEBUG_ASSERT(pmu);

	/* Wait for the command to complete */
	do {
		rawstat = mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_RAWSTAT);
		--timeout;
	} while (0 == (rawstat & PMU_REG_VAL_IRQ) && 0 < timeout);

	MALI_DEBUG_ASSERT(0 < timeout);
	if (0 == timeout) {
		return _MALI_OSK_ERR_TIMEOUT;
	}

	mali_hw_core_register_write(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_CLEAR, PMU_REG_VAL_IRQ);

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_pmu_power_up_internal(struct mali_pmu_core *pmu, const u32 mask)
{
	u32 stat;
	_mali_osk_errcode_t err;
#if !defined(CONFIG_MALI_PMU_PARALLEL_POWER_UP)
	u32 current_domain;
#endif

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(0 == (mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_RAWSTAT)
	                        & PMU_REG_VAL_IRQ));

	stat = mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
	stat &= pmu->registered_cores_mask;
	if (0 == mask || 0 == (stat & mask)) return _MALI_OSK_ERR_OK;

#if defined(CONFIG_MALI_PMU_PARALLEL_POWER_UP)
	mali_hw_core_register_write(&pmu->hw_core, PMU_REG_ADDR_MGMT_POWER_UP, mask);

	err = mali_pmu_wait_for_command_finish(pmu);
	if (_MALI_OSK_ERR_OK != err) {
		return err;
	}
#else
	for (current_domain = 1; current_domain <= pmu->registered_cores_mask; current_domain <<= 1) {
		if (current_domain & mask & stat) {
			mali_hw_core_register_write(&pmu->hw_core, PMU_REG_ADDR_MGMT_POWER_UP, current_domain);

			err = mali_pmu_wait_for_command_finish(pmu);
			if (_MALI_OSK_ERR_OK != err) {
				return err;
			}
		}
	}
#endif

#if defined(DEBUG)
	/* Get power status of cores */
	stat = mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
	stat &= pmu->registered_cores_mask;

	MALI_DEBUG_ASSERT(0 == (stat & mask));
	MALI_DEBUG_ASSERT(0 == (stat & pmu->active_cores_mask));
#endif /* defined(DEBUG) */

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_pmu_power_down_internal(struct mali_pmu_core *pmu, const u32 mask)
{
	u32 stat;
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(0 == (mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_RAWSTAT)
	                        & PMU_REG_VAL_IRQ));

	stat = mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
	stat &= pmu->registered_cores_mask;

	if (0 == mask || 0 == ((~stat) & mask)) return _MALI_OSK_ERR_OK;

	mali_hw_core_register_write(&pmu->hw_core, PMU_REG_ADDR_MGMT_POWER_DOWN, mask);

	/* Do not wait for interrupt on Mali-300/400 if all domains are powered off
	 * by our power down command, because the HW will simply not generate an
	 * interrupt in this case.*/
	if (mali_is_mali450() || pmu->registered_cores_mask != (mask | stat)) {
		err = mali_pmu_wait_for_command_finish(pmu);
		if (_MALI_OSK_ERR_OK != err) {
			return err;
		}
	} else {
		mali_hw_core_register_write(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_CLEAR, PMU_REG_VAL_IRQ);
	}
#if defined(DEBUG)
	/* Get power status of cores */
	stat = mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
	stat &= pmu->registered_cores_mask;

	MALI_DEBUG_ASSERT(mask == (stat & mask));
#endif

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_pmu_reset(struct mali_pmu_core *pmu)
{
	_mali_osk_errcode_t err;
	u32 cores_off_mask, cores_on_mask, stat;

	mali_pmu_lock(pmu);

	/* Setup the desired defaults */
	mali_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_MASK, 0);
	mali_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_SW_DELAY, pmu->switch_delay);

	/* Get power status of cores */
	stat = mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);

	cores_off_mask = pmu->registered_cores_mask & ~(stat | pmu->active_cores_mask);
	cores_on_mask  = pmu->registered_cores_mask &  (stat & pmu->active_cores_mask);

	if (0 != cores_off_mask) {
		err = mali_pmu_power_down_internal(pmu, cores_off_mask);
		if (_MALI_OSK_ERR_OK != err) return err;
	}

	if (0 != cores_on_mask) {
		err = mali_pmu_power_up_internal(pmu, cores_on_mask);
		if (_MALI_OSK_ERR_OK != err) return err;
	}

#if defined(DEBUG)
	{
		stat = mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
		stat &= pmu->registered_cores_mask;

		MALI_DEBUG_ASSERT(stat == (pmu->registered_cores_mask & ~pmu->active_cores_mask));
	}
#endif /* defined(DEBUG) */

	mali_pmu_unlock(pmu);

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_pmu_power_down(struct mali_pmu_core *pmu, u32 mask)
{
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0 );

	/* Make sure we have a valid power domain mask */
	if (mask > pmu->registered_cores_mask) {
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	mali_pmu_lock(pmu);

	MALI_DEBUG_PRINT(4, ("Mali PMU: Power down (0x%08X)\n", mask));

	pmu->active_cores_mask &= ~mask;

	_mali_osk_pm_dev_ref_add_no_power_on();
	if (!mali_pm_is_power_on()) {
		/* Don't touch hardware if all of Mali is powered off. */
		_mali_osk_pm_dev_ref_dec_no_power_on();
		mali_pmu_unlock(pmu);

		MALI_DEBUG_PRINT(4, ("Mali PMU: Skipping power down (0x%08X) since Mali is off\n", mask));

		return _MALI_OSK_ERR_BUSY;
	}

	err = mali_pmu_power_down_internal(pmu, mask);

	_mali_osk_pm_dev_ref_dec_no_power_on();
	mali_pmu_unlock(pmu);

	return err;
}

_mali_osk_errcode_t mali_pmu_power_up(struct mali_pmu_core *pmu, u32 mask)
{
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0 );

	/* Make sure we have a valid power domain mask */
	if (mask & ~pmu->registered_cores_mask) {
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	mali_pmu_lock(pmu);

	MALI_DEBUG_PRINT(4, ("Mali PMU: Power up (0x%08X)\n", mask));

	pmu->active_cores_mask |= mask;

	_mali_osk_pm_dev_ref_add_no_power_on();
	if (!mali_pm_is_power_on()) {
		/* Don't touch hardware if all of Mali is powered off. */
		_mali_osk_pm_dev_ref_dec_no_power_on();
		mali_pmu_unlock(pmu);

		MALI_DEBUG_PRINT(4, ("Mali PMU: Skipping power up (0x%08X) since Mali is off\n", mask));

		return _MALI_OSK_ERR_BUSY;
	}

	err = mali_pmu_power_up_internal(pmu, mask);

	_mali_osk_pm_dev_ref_dec_no_power_on();
	mali_pmu_unlock(pmu);

	return err;
}

_mali_osk_errcode_t mali_pmu_power_down_all(struct mali_pmu_core *pmu)
{
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);

	mali_pmu_lock(pmu);

	/* Setup the desired defaults in case we were called before mali_pmu_reset() */
	mali_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_MASK, 0);
	mali_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_SW_DELAY, pmu->switch_delay);

	err = mali_pmu_power_down_internal(pmu, pmu->registered_cores_mask);

	mali_pmu_unlock(pmu);

	return err;
}

_mali_osk_errcode_t mali_pmu_power_up_all(struct mali_pmu_core *pmu)
{
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT(pmu->registered_cores_mask != 0);

	mali_pmu_lock(pmu);

	/* Setup the desired defaults in case we were called before mali_pmu_reset() */
	mali_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_INT_MASK, 0);
	mali_hw_core_register_write_relaxed(&pmu->hw_core, PMU_REG_ADDR_MGMT_SW_DELAY, pmu->switch_delay);

	err = mali_pmu_power_up_internal(pmu, pmu->active_cores_mask);

	mali_pmu_unlock(pmu);
	return err;
}

struct mali_pmu_core *mali_pmu_get_global_pmu_core(void)
{
	return mali_global_pmu_core;
}

static u32 mali_pmu_detect_mask(void)
{
	int dynamic_config_pp = 0;
	int dynamic_config_l2 = 0;
	int i = 0;
	u32 mask = 0;

	/* Check if PM domain compatible with actually pp core and l2 cache and collection info about domain */
	mask = mali_pmu_get_domain_mask(MALI_GP_DOMAIN_INDEX);

	for (i = MALI_PP0_DOMAIN_INDEX; i <= MALI_PP7_DOMAIN_INDEX; i++) {
		mask |= mali_pmu_get_domain_mask(i);

		if (0x0 != mali_pmu_get_domain_mask(i)) {
			dynamic_config_pp++;
		}
	}

	for (i = MALI_L20_DOMAIN_INDEX; i <= MALI_L22_DOMAIN_INDEX; i++) {
		mask |= mali_pmu_get_domain_mask(i);

		if (0x0 != mali_pmu_get_domain_mask(i)) {
			dynamic_config_l2++;
		}
	}

	MALI_DEBUG_PRINT(2, ("Mali PMU: mask 0x%x, pp_core %d, l2_core %d \n", mask, dynamic_config_pp, dynamic_config_l2));

	return mask;
}
