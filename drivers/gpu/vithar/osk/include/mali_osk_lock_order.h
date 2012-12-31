/*
 *
 * (C) COPYRIGHT 2010 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _OSK_LOCK_ORDER_H_
#define _OSK_LOCK_ORDER_H_

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @addtogroup oskmutex_lockorder
 * @{
 */

/**
 * @anchor oskmutex_lockorder
 * @par Lock ordering for Mutexes and Spinlocks
 *
 * When an OSK Rwlock, Mutex or Spinlock is initialized, it is given a locking order.
 * This is a number that is checked in QA builds to detect possible deadlock
 * conditions. The order is checked when a thread calls
 * osk_rwlock_read_lock() / osk_rwlock_write_lock() / osk_mutex_lock() /
 * osk_spinlock_lock() / osk_spinlock_irq_lock(). If the calling
 * thread already holds a lock with an order less than that of the object being
 * locked, an assertion failure will occur.
 *
 * Lock ordering must be respected between OSK Rwlocks, Mutexes, and Spinlocks.
 * That is, when obtaining an OSK Rwlock, Mutex or Spinlock, its lock order
 * must be lower than any other OSK Rwlock, Mutex or Spinlock held by the current thread.
 *
 */
/** @{ */

typedef enum
{
	/**
	 * Reserved mutex order, indicating that the mutex will be the last to be
	 * locked, and all other OSK mutexes are obtained before this one.
	 *
	 * All other lock orders must be after this one, because we use this to
	 * ASSERT that lock orders are >= OSK_LOCK_ORDER_LAST
	 */
	OSK_LOCK_ORDER_LAST = 0,

	/**
	 * Lock order for umpp_descriptor_mapping.
	 *
	 * This lock is always obtained last: no other locks are obtained whilst
	 * operating on a descriptor mapping, and so this should be as high as
	 * possible in this enum (lower in number) than any other lock held by UMP.
	 *
	 * It can have the same order as any other lock in UMP that is always
	 * obtained last.
	 */
	OSK_LOCK_ORDER_UMP_DESCRIPTOR_MAPPING,

	/**
	 * Lock order for mutex protecting umpp_device::secure_id_map (this is in
	 * the 'single global UMP device').
	 *
	 * This must be obtained after (lower in number than) the
	 * OSK_LOCK_ORDER_UMP_SESSION_LOCK, since the allocation is often looked up
	 * in secure_id_map while manipulating the umpp_session::memory_usage list.
	 */
	OSK_LOCK_ORDER_UMP_IDMAP_LOCK,

	/**
	 * Lock order for mutex protecting the umpp_session::memory_usage list
	 */
	OSK_LOCK_ORDER_UMP_SESSION_LOCK,


	/**
	 *
	 */
	OSK_LOCK_ORDER_OSK_FAILURE,
	
	/**
	 * For the power management metrics system
	 */
	OSK_LOCK_ORDER_PM_METRICS,

	/**
	 * For fast queue management, with very little processing and
	 * no other lock held within the critical section.
	 */
	OSK_LOCK_ORDER_QUEUE = OSK_LOCK_ORDER_PM_METRICS,

	/**
	 * For register trace buffer access in kernel space
	 */

	OSK_LOCK_ORDER_TB,

	/**
	 * For modification of the MMU mask register, which is done as a read-modify-write
	 */
	OSK_LOCK_ORDER_MMU_MASK,
	/**
	 * For access and modification to the power state of a device
	 */
	OSK_LOCK_ORDER_POWER_MGMT = OSK_LOCK_ORDER_MMU_MASK,
	
	/**
	 * For access to active_count in kbase_pm_device_data
	 */
	OSK_LOCK_ORDER_POWER_MGMT_ACTIVE = OSK_LOCK_ORDER_POWER_MGMT,


	OSK_LOCK_ORDER_TRACE,
	/**
	 * For the resources used during MMU pf or low-level job handling
	 */
	OSK_LOCK_ORDER_JS_RUNPOOL_IRQ,

	/**
	 * For hardware counters collection setup
	 */
	OSK_LOCK_ORDER_HWCNT,

	/**
	 * For job slot management
	 *
	 * This is an IRQ lock, and so must be held after all sleeping locks
	 */
	OSK_LOCK_ORDER_JSLOT,
	
	/**
	 * For use when zapping a context (see kbase_jd_zap_context)
	 */
	OSK_LOCK_ORDER_JD_ZAP_CONTEXT,

	/**
	 * AS lock, used to access kbase_as structure.
	 *
	 * This must be held after:
	 * - Job Scheduler Run Pool lock (OSK_LOCK_ORDER_RUNPOOL)
	 *
	 * This is an IRQ lock, and so must be held after all sleeping locks
	 *
	 * @since OSU 1.9
	 */
	OSK_LOCK_ORDER_AS,

	/**
	 * Job Scheduling Run Pool lock
	 *
	 * This must be held after:
	 * - Job Scheduling Context Lock (OSK_LOCK_ORDER_JS_CTX)
	 * - Job Slot management lock (OSK_LOCK_ORDER_JSLOT)
	 *
	 * This is an IRQ lock, and so must be held after all sleeping locks
	 *
	 */
	OSK_LOCK_ORDER_JS_RUNPOOL,

	
	/**
	 * Job Scheduling Policy Queue lock
	 *
	 * This must be held after Job Scheduling Context Lock (OSK_LOCK_ORDER_JS_CTX).
	 *
	 * Currently, there's no restriction on holding this at the same time as the  JSLOT/JS_RUNPOOL locks - but, this doesn't happen anyway.
	 *
	 */
	OSK_LOCK_ORDER_JS_QUEUE,

	/**
	 * Job Scheduling Context Lock
	 *
	 * This must be held after Job Dispatch lock (OSK_LOCK_ORDER_JCTX), but before:
	 * - The Job Slot lock (OSK_LOCK_ORDER_JSLOT)
	 * - The Run Pool lock (OSK_LOCK_ORDER_JS_RUNPOOL)
	 * - The Policy Queue lock (OSK_LOCK_ORDER_JS_QUEUE)
	 *
	 * In addition, it must be held before the VM Region Lock (OSK_LOCK_ORDER_MEM_REG),
	 * because at some point need to modify the MMU registers to update the address
	 * space on scheduling in the context.
	 *
	 */
	OSK_LOCK_ORDER_JS_CTX,

	/**
	 * For memory mapping management
	 */
	OSK_LOCK_ORDER_MEM_REG,
	
	/**
	 * For job dispatch management
	 */
	OSK_LOCK_ORDER_JCTX,

	/**
	 * Register queue lock for model
	 */
	OSK_LOCK_ORDER_BASE_REG_QUEUE,

#ifdef CONFIG_VITHAR_RT_PM
    /**
     * System power for mali-t604
     */
    OSK_LOCK_ORDER_CMU_PMU,
#endif

	/**
	 * Reserved mutex order, indicating that the mutex will be the first to be
	 * locked, and all other OSK mutexes are obtained after this one.
	 *
	 * All other lock orders must be before this one, because we use this to
	 * ASSERT that lock orders are <= OSK_LOCK_ORDER_FIRST
	 */
	OSK_LOCK_ORDER_FIRST
} osk_lock_order;

/** @} */

/** @} */ /* end group oskmutex_lockorder */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

#ifdef __cplusplus
}
#endif

#endif /* _OSK_LOCK_ORDER_H_ */
