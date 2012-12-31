/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_pmm.h
 * Defines the power management module for the kernel device driver
 */

#ifndef __MALI_PMM_H__
#define __MALI_PMM_H__

/* For mali_pmm_message_data and MALI_PMM_EVENT_UK_* defines */
#include "mali_uk_types.h"
#include "mali_platform.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @defgroup pmmapi Power Management Module APIs
 *
 * @{
 */

/** OS event tester */
#define PMM_OS_TEST 0

/** @brief Compile option to turn on/off tracing */
#define MALI_PMM_TRACE 0
#define MALI_PMM_TRACE_SENT_EVENTS 0

/** @brief Compile option to switch between always on or job control PMM policy */
#define MALI_PMM_ALWAYS_ON 0

/** @brief Overrides hardware PMU and uses software simulation instead
 *  @note This even stops intialization of PMU and cores being powered on at start up
 */
#define MALI_PMM_NO_PMU 0

/** @brief PMM debug print to control debug message level */
#define MALIPMM_DEBUG_PRINT(args) \
	MALI_DEBUG_PRINT(3, args)


/** @brief power management event message identifiers.
 */
/* These must match up with the pmm_trace_events & pmm_trace_events_internal
 * arrays
 */
typedef enum mali_pmm_event_id
{
	MALI_PMM_EVENT_OS_POWER_UP             =    0,   /**< OS power up event */
	MALI_PMM_EVENT_OS_POWER_DOWN           =    1,   /**< OS power down event */
	MALI_PMM_EVENT_JOB_SCHEDULED           =    2,   /**< Job scheduled to run event */
	MALI_PMM_EVENT_JOB_QUEUED              =    3,   /**< Job queued (but not run) event */
	MALI_PMM_EVENT_JOB_FINISHED            =    4,   /**< Job finished event */
	MALI_PMM_EVENT_TIMEOUT                 =    5,   /**< Time out timer has expired */
	MALI_PMM_EVENT_DVFS_PAUSE              =    6,   /**< Mali device pause event */
	MALI_PMM_EVENT_DVFS_RESUME             =    7,   /**< Mali device resume event */

	MALI_PMM_EVENT_UKS                     =  200,   /**< Events from the user-side start here */
	MALI_PMM_EVENT_UK_EXAMPLE              =  _MALI_PMM_EVENT_UK_EXAMPLE,

	MALI_PMM_EVENT_INTERNALS               = 1000,
	MALI_PMM_EVENT_INTERNAL_POWER_UP_ACK   = 1001,   /**< Internal power up acknowledgement */
	MALI_PMM_EVENT_INTERNAL_POWER_DOWN_ACK = 1002,   /**< Internal power down acknowledgment */
} mali_pmm_event_id;


/** @brief Use this when the power up/down callbacks do not need any OS data. */
#define MALI_PMM_NO_OS_DATA 1


/* @brief Geometry and pixel processor identifiers for the PMM
 *
 * @note these match the ARM Mali 400 PMU hardware definitions, apart from the "SYSTEM"
 */
typedef enum mali_pmm_core_id_tag
{
	MALI_PMM_CORE_SYSTEM = 0x00000000,          /**< All of the Mali hardware */
	MALI_PMM_CORE_GP     = 0x00000001,          /**< Mali GP2 */
	MALI_PMM_CORE_L2     = 0x00000002,          /**< Level 2 cache */
	MALI_PMM_CORE_PP0    = 0x00000004,          /**< Mali 200 pixel processor 0 */
	MALI_PMM_CORE_PP1    = 0x00000008,          /**< Mali 200 pixel processor 1 */
	MALI_PMM_CORE_PP2    = 0x00000010,          /**< Mali 200 pixel processor 2 */
	MALI_PMM_CORE_PP3    = 0x00000020,          /**< Mali 200 pixel processor 3 */
	MALI_PMM_CORE_PP_ALL = 0x0000003C           /**< Mali 200 pixel processors 0-3 */
} mali_pmm_core_id;


/* @brief PMM bitmask of mali_pmm_core_ids
 */
typedef u32 mali_pmm_core_mask;

/* @brief PMM event timestamp type
 */
typedef u32 mali_pmm_timestamp;

/** @brief power management event message struct
 */
typedef struct _mali_pmm_message
{
	mali_pmm_event_id id;               /**< event id */
	mali_pmm_message_data data;         /**< specific data associated with the event */
	mali_pmm_timestamp ts;              /**< timestamp the event was placed in the event queue */
} mali_pmm_message_t;



/** @brief the state of the power management module.
 */
/* These must match up with the pmm_trace_state array */
typedef enum mali_pmm_state_tag
{
	MALI_PMM_STATE_UNAVAILABLE       = 0,       /**< PMM is not available */
	MALI_PMM_STATE_SYSTEM_ON         = 1,       /**< All of the Mali hardware is on */
	MALI_PMM_STATE_SYSTEM_OFF        = 2,       /**< All of the Mali hardware is off */
	MALI_PMM_STATE_SYSTEM_TRANSITION = 3        /**< System is changing state */
} mali_pmm_state;


/** @brief a power management policy.
 */
/* These must match up with the pmm_trace_policy array */
typedef enum mali_pmm_policy_tag
{
	MALI_PMM_POLICY_NONE        = 0,            /**< No policy */
	MALI_PMM_POLICY_ALWAYS_ON   = 1,            /**< Always on policy */
	MALI_PMM_POLICY_JOB_CONTROL = 2,            /**< Job control policy */
	MALI_PMM_POLICY_RUNTIME_JOB_CONTROL = 3     /**< Run time power management control policy */
} mali_pmm_policy;

/** @brief Function to power up MALI
 *
 * @param cores core mask to power up the cores
 *
 * @return error code if MALI fails to power up
 */
_mali_osk_errcode_t malipmm_powerup( u32 cores );

/** @brief Function to power down MALI
 *
 * @param cores core mask to power down the cores
 * @param The power mode to which MALI transitions
 *
 * @return error code if MALI fails to power down
 */
_mali_osk_errcode_t malipmm_powerdown( u32 cores, mali_power_mode power_mode );

/** @brief Function to report to the OS when the power down has finished
 *
 * @param data The event message data that initiated the power down
 */
void _mali_osk_pmm_power_down_done(mali_pmm_message_data data);

/** @brief Function to report to the OS when the power up has finished
 *
 * @param data The event message data that initiated the power up
 */
void _mali_osk_pmm_power_up_done(mali_pmm_message_data data);

/** @brief Function to report that DVFS operation done
 *
 * @param data The event message data
 */
void _mali_osk_pmm_dvfs_operation_done(mali_pmm_message_data data);

#if MALI_POWER_MGMT_TEST_SUITE
/** @brief Function to notify power management events
 *
 * @param data The event message data
 */
void _mali_osk_pmm_policy_events_notifications(mali_pmm_event_id event_id);

#endif

/** @brief Function to power up MALI
 *
 *  @note powers up the MALI during MALI device driver is unloaded
 */
void malipmm_force_powerup( void );

/** @brief Function to report the OS that device is idle
 *
 *  @note inform the OS that device is idle
 */
_mali_osk_errcode_t _mali_osk_pmm_dev_idle( void );

/** @brief Function to report the OS to activate device
 *
 * @note inform the os that device needs to be activated
 */
int _mali_osk_pmm_dev_activate( void );

/** @brief Function to report OS PMM for cleanup
 *
 * @note Function to report OS PMM for cleanup
 */
void _mali_osk_pmm_ospmm_cleanup( void );

/** @brief Queries the current state of the PMM software
 *
 * @note the state of the PMM can change after this call has returned
 *
 * @return the current PMM state value
 */
mali_pmm_state _mali_pmm_state( void );

/** @brief List of cores that are registered with the PMM
 *
 * This will return the cores that have been currently registered with the PMM,
 * which is a bitwise OR of the mali_pmm_core_id_tags. A value of 0x0 means that
 * there are no cores registered.
 *
 * @note the list of cores can change after this call has returned
 *
 * @return a bit mask representing all the cores that have been registered with the PMM
 */
mali_pmm_core_mask _mali_pmm_cores_list( void );

/** @brief List of cores that are powered up in the PMM
 *
 * This will return the subset of the cores that can be listed using mali_pmm_cores_
 * list, that have power. It is a bitwise OR of the mali_pmm_core_id_tags. A value of
 * 0x0 means that none of the cores registered are powered.
 *
 * @note the list of cores can change after this call has returned
 *
 * @return a bit mask representing all the cores that are powered up
 */
mali_pmm_core_mask _mali_pmm_cores_powered( void );


/** @brief List of power management policies that are supported by the PMM
 *
 * Given an empty array of policies - policy_list - which contains the number
 * of entries as specified by - policy_list_size, this function will populate
 * the list with the available policies. If the policy_list is too small for
 * all the policies then only policy_list_size entries will be returned. If the
 * policy_list is bigger than the number of available policies then, the extra
 * entries will be set to MALI_PMM_POLICY_NONE.
 * The function will also update available_policies with the number of policies
 * that are available, even if it exceeds the policy_list_size.
 * The function will succeed if all policies could be returned, else it will
 * fail if none or only a subset of policies could be returned.
 * The function will also fail if no policy_list is supplied, though
 * available_policies is optional.
 *
 * @note this is a STUB function and is not yet implemented
 *
 * @param policy_list_size is the number of policies that can be returned in
 * the policy_list argument
 * @param policy_list is an array of policies that should be populated with
 * the list of policies that are supported by the PMM
 * @param policies_available optional argument, if non-NULL will be set to the
 * number of policies available
 * @return _MALI_OSK_ERR_OK if the policies could be listed, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t _mali_pmm_list_policies(
		u32 policy_list_size,
		mali_pmm_policy *policy_list,
		u32 *policies_available );

/** @brief Set the power management policy in the PMM
 *
 * Given a valid supported policy, this function will change the PMM to use
 * this new policy
 * The function will fail if the policy given is invalid or unsupported.
 *
 * @note this is a STUB function and is not yet implemented
 *
 * @param policy the new policy to be set
 * @return _MALI_OSK_ERR_OK if the policy could be set, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t _mali_pmm_set_policy( mali_pmm_policy policy );

/** @brief Get the current power management policy in the PMM
 *
 * Given a pointer to a policy data type, this function will return the current
 * policy that is in effect for the PMM. This maybe out of date if there is a
 * pending set policy call that has not been serviced.
 * The function will fail if the policy given is NULL.
 *
 * @note the policy of the PMM can change after this call has returned
 *
 * @param policy a pointer to a policy that can be updated to the current
 * policy
 * @return _MALI_OSK_ERR_OK if the policy could be returned, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t _mali_pmm_get_policy( mali_pmm_policy *policy );

#if MALI_PMM_TRACE

/** @brief Indicates when a hardware state change occurs in the PMM
 *
 * @param old a mask of the cores indicating the previous state of the cores
 * @param newstate a mask of the cores indicating the new current state of the cores
 */
void _mali_pmm_trace_hardware_change( mali_pmm_core_mask old, mali_pmm_core_mask newstate );

/** @brief Indicates when a state change occurs in the PMM
 *
 * @param old the previous state for the PMM
 * @param newstate the new current state of the PMM
 */
void _mali_pmm_trace_state_change( mali_pmm_state old, mali_pmm_state newstate );

/** @brief Indicates when a policy change occurs in the PMM
 *
 * @param old the previous policy for the PMM
 * @param newpolicy the new current policy of the PMM
 */
void _mali_pmm_trace_policy_change( mali_pmm_policy old, mali_pmm_policy newpolicy );

/** @brief Records when an event message is read by the event system
 *
 * @param event the message details
 * @param received MALI_TRUE when the message is received by the PMM, else it is being sent
 */
void _mali_pmm_trace_event_message( mali_pmm_message_t *event, mali_bool received );

#endif /* MALI_PMM_TRACE */

/** @brief Dumps the current state of OS PMM thread
 */
#if MALI_STATE_TRACKING
u32 mali_pmm_dump_os_thread_state( char *buf, u32 size );
#endif /* MALI_STATE_TRACKING */

/** @} */ /* end group pmmapi */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_PMM_H__ */
