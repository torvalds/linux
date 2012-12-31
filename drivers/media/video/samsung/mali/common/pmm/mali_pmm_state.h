/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PMM_STATE_H__
#define __MALI_PMM_STATE_H__

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup pmmapi Power Management Module APIs
 *
 * @{
 *
 * @defgroup pmmapi_state Power Management Module State
 *
 * @{
 */

/* Check that the subset is really a subset of cores */
#define MALI_PMM_DEBUG_ASSERT_CORES_SUBSET( cores, subset ) \
	MALI_DEBUG_ASSERT( ((~(cores)) & (subset)) == 0 )


/* Locking macros */
#define MALI_PMM_LOCK(pmm) \
	_mali_osk_lock_wait( pmm->lock, _MALI_OSK_LOCKMODE_RW )
#define MALI_PMM_UNLOCK(pmm) \
	_mali_osk_lock_signal( pmm->lock, _MALI_OSK_LOCKMODE_RW )
#define MALI_PMM_LOCK_TERM(pmm) \
        _mali_osk_lock_term( pmm->lock )

/* Notification type for messages */
#define MALI_PMM_NOTIFICATION_TYPE 0

/** @brief Status of the PMM state machine
 */
typedef enum mali_pmm_status_tag
{
	MALI_PMM_STATUS_IDLE,                       /**< PMM is waiting next event */
	MALI_PMM_STATUS_POLICY_POWER_DOWN,          /**< Policy initiated power down */
	MALI_PMM_STATUS_POLICY_POWER_UP,            /**< Policy initiated power down */
        MALI_PMM_STATUS_OS_WAITING,                 /**< PMM is waiting for OS power up */
	MALI_PMM_STATUS_OS_POWER_DOWN,              /**< OS initiated power down */
	MALI_PMM_STATUS_DVFS_PAUSE,                 /**< PMM DVFS Status Pause */
	MALI_PMM_STATUS_OS_POWER_UP,                /**< OS initiated power up */
	MALI_PMM_STATUS_OFF,                        /**< PMM is not active */
} mali_pmm_status;


/** @brief Internal state of the PMM
 */
typedef struct _mali_pmm_internal_state
{
	mali_pmm_status status;                 /**< PMM state machine */
	mali_pmm_policy policy;                 /**< PMM policy */
	mali_bool check_policy;                 /**< PMM policy needs checking */
	mali_pmm_state state;                   /**< PMM state */
	mali_pmm_core_mask cores_registered;    /**< Bitmask of cores registered */
	mali_pmm_core_mask cores_powered;       /**< Bitmask of cores powered up */
	mali_pmm_core_mask cores_idle;          /**< Bitmask of cores idle */
	mali_pmm_core_mask cores_pend_down;     /**< Bitmask of cores pending power down */
	mali_pmm_core_mask cores_pend_up;       /**< Bitmask of cores pending power up */
	mali_pmm_core_mask cores_ack_down;      /**< Bitmask of cores acknowledged power down */
	mali_pmm_core_mask cores_ack_up;        /**< Bitmask of cores acknowledged power up */

	_mali_osk_notification_queue_t *queue;  /**< PMM event queue */
	_mali_osk_notification_queue_t *iqueue; /**< PMM internal event queue */
	_mali_osk_irq_t *irq;                   /**< PMM irq handler */
	_mali_osk_lock_t *lock;                 /**< PMM lock */

	mali_pmm_message_data os_data;          /**< OS data sent via the OS events */

	mali_bool pmu_initialized;              /**< PMU initialized */

	_mali_osk_atomic_t messages_queued;     /**< PMM event messages queued */
	u32 waiting;                            /**< PMM waiting events - due to busy */
	u32 no_events;                          /**< PMM called to process when no events */

	u32 missed;                             /**< PMM missed events due to OOM */
	mali_bool fatal_power_err;				/**< PMM has had a fatal power error? */
	u32 is_dvfs_active;			/**< PMM DVFS activity */

#if MALI_STATE_TRACKING
	mali_pmm_status mali_last_pmm_status;  /**< The previous PMM status */
	mali_pmm_event_id mali_new_event_status;/**< The type of the last PMM event */
	mali_bool mali_pmm_lock_acquired;      /**< Is the PMM lock held somewhere or not */
#endif

#if (MALI_PMM_TRACE || MALI_STATE_TRACKING)
	u32 messages_sent;                      /**< Total event messages sent */
	u32 messages_received;                  /**< Total event messages received */
	u32 imessages_sent;                     /**< Total event internal messages sent */
	u32 imessages_received;                 /**< Total event internal messages received */
#endif
} _mali_pmm_internal_state_t;

/** @brief Sets that a policy needs a check before processing events
 *
 * A timer or something has expired that needs dealing with
 */
void malipmm_set_policy_check(void);

/** @brief Update the PMM externally viewable state depending on the current PMM internal state
 *
 * @param pmm internal PMM state
 * @return MALI_TRUE if the timeout is valid, else MALI_FALSE
 */
void pmm_update_system_state( _mali_pmm_internal_state_t *pmm );

/** @brief Returns the core mask from the event data - if applicable
 *
 * @param pmm internal PMM state
 * @param event event message to get the core mask from
 * @return mask of cores that is relevant to this event message
 */
mali_pmm_core_mask pmm_cores_from_event_data( _mali_pmm_internal_state_t *pmm, mali_pmm_message_t *event );

/** @brief Sort out which cores need to be powered up from the given core mask
 *
 * All cores that can be powered up will be put into a pending state
 *
 * @param pmm internal PMM state
 * @param cores mask of cores to check if they need to be powered up
 * @return mask of cores that need to be powered up, this can be 0 if all cores
 * are powered up already
 */
mali_pmm_core_mask pmm_cores_to_power_up( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores );

/** @brief Sort out which cores need to be powered down from the given core mask
 *
 * All cores that can be powered down will be put into a pending state. If they
 * can be powered down immediately they will also be acknowledged that they can be
 * powered down. If the immediate_only flag is set, then only those cores that
 * can be acknowledged for power down will be put into a pending state.
 *
 * @param pmm internal PMM state
 * @param cores mask of cores to check if they need to be powered down
 * @param immediate_only MALI_TRUE means that only cores that can power down now will
 * be put into a pending state
 * @return mask of cores that need to be powered down, this can be 0 if all cores
 * are powered down already
 */
mali_pmm_core_mask pmm_cores_to_power_down( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores, mali_bool immediate_only );

/** @brief Cancel an invokation to power down (pmm_invoke_power_down)
 *
 * @param pmm internal PMM state
 */
void pmm_power_down_cancel( _mali_pmm_internal_state_t *pmm );

/** @brief Check if a call to invoke power down should succeed, or fail
 *
 * This will report MALI_FALSE if some of the cores are still active and need
 * to acknowledge that they are ready to power down
 *
 * @param pmm internal PMM state
 * @return MALI_TRUE if the pending cores to power down have acknowledged they
 * can power down, else MALI_FALSE
 */
mali_bool pmm_power_down_okay( _mali_pmm_internal_state_t *pmm );

/** @brief Try to make all the pending cores power down
 *
 * If all the pending cores have acknowledged they can power down, this will call the
 * PMU power down function to turn them off
 *
 * @param pmm internal PMM state
 * @return MALI_TRUE if the pending cores have been powered down, else MALI_FALSE
 */
mali_bool pmm_invoke_power_down( _mali_pmm_internal_state_t *pmm,  mali_power_mode power_mode );

/** @brief Check if all the pending cores to power up have done so
 *
 * This will report MALI_FALSE if some of the cores are still powered off
 * and have not acknowledged that they have powered up
 *
 * @param pmm internal PMM state
 * @return MALI_TRUE if the pending cores to power up have acknowledged they
 * are now powered up, else MALI_FALSE
 */
mali_bool pmm_power_up_okay( _mali_pmm_internal_state_t *pmm );

/** @brief Try to make all the pending cores power up
 *
 * If all the pending cores have acknowledged they have powered up, this will
 * make the cores start processing jobs again, else this will call the PMU
 * power up function to turn them on, and the PMM is then expected to wait for an
 * interrupt to acknowledge the power up
 *
 * @param pmm internal PMM state
 * @return MALI_TRUE if the pending cores have been powered up, else MALI_FALSE
 */
mali_bool pmm_invoke_power_up( _mali_pmm_internal_state_t *pmm );

/** @brief Set the cores that are now active in the system
 *
 * Updates which cores are active and returns which cores are still idle
 *
 * @param pmm internal PMM state
 * @param cores mask of cores to set to active
 * @return mask of all the cores that are idle
 */
mali_pmm_core_mask pmm_cores_set_active( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores );

/** @brief Set the cores that are now idle in the system
 *
 * Updates which cores are idle and returns which cores are still idle
 *
 * @param pmm internal PMM state
 * @param cores mask of cores to set to idle
 * @return mask of all the cores that are idle
 */
mali_pmm_core_mask pmm_cores_set_idle( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores );

/** @brief Set the cores that have acknowledged a pending power down
 *
 * Updates which cores have acknowledged the pending power down and are now ready
 * to be turned off
 *
 * @param pmm internal PMM state
 * @param cores mask of cores that have acknowledged the pending power down
 * @return mask of all the cores that have acknowledged the power down
 */
mali_pmm_core_mask pmm_cores_set_down_ack( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores );

/** @brief Set the cores that have acknowledged a pending power up
 *
 * Updates which cores have acknowledged the pending power up and are now
 * fully powered and ready to run jobs
 *
 * @param pmm internal PMM state
 * @param cores mask of cores that have acknowledged the pending power up
 * @return mask of all the cores that have acknowledged the power up
 */
mali_pmm_core_mask pmm_cores_set_up_ack( _mali_pmm_internal_state_t *pmm, mali_pmm_core_mask cores );


/** @brief Tries to reset the PMM and PMU hardware to a known state after any fatal issues
 *
 * This will try and make all the cores powered up and reset the PMM state
 * to its initial state after core registration - all cores powered but not
 * pending or active.
 * All events in the event queues will be thrown away.
 *
 * @note: Any pending power down will be cancelled including the OS calling for power down
 */
void pmm_fatal_reset( _mali_pmm_internal_state_t *pmm );

/** @brief Save the OS specific data for an OS power up/down event
 *
 * @param pmm internal PMM state
 * @param data OS specific event data
 */
void pmm_save_os_event_data(_mali_pmm_internal_state_t *pmm, mali_pmm_message_data data);

/** @brief Retrieve the OS specific data for an OS power up/down event
 *
 * This will clear the stored OS data, as well as return it.
 *
 * @param pmm internal PMM state
 * @return OS specific event data that was saved previously
 */
mali_pmm_message_data pmm_retrieve_os_event_data(_mali_pmm_internal_state_t *pmm);


/** @brief Get a human readable name for the cores in a core mask
 *
 * @param core the core mask
 * @return string containing a name relating to the given core mask
 */
const char *pmm_trace_get_core_name( mali_pmm_core_mask core );

/** @} */ /* End group pmmapi_state */
/** @} */ /* End group pmmapi */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_PMM_STATE_H__ */
