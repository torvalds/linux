/*
 * Copyright (C) 2010-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_types.h
 * Defines types of the OS abstraction layer for the kernel device driver (OSK)
 */

#ifndef __MALI_OSK_TYPES_H__
#define __MALI_OSK_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup uddapi Unified Device Driver (UDD) APIs
 *
 * @{
 */

/**
 * @addtogroup oskapi UDD OS Abstraction for Kernel-side (OSK) APIs
 *
 * @{
 */

/** @defgroup _mali_osk_miscellaneous OSK Miscellaneous functions, constants and types
 * @{ */

/* Define integer types used by OSK. Note: these currently clash with Linux so we only define them if not defined already */
#ifndef __KERNEL__
typedef unsigned char      u8;
typedef signed char        s8;
typedef unsigned short     u16;
typedef signed short       s16;
typedef unsigned int       u32;
typedef signed int         s32;
typedef unsigned long long u64;
#define BITS_PER_LONG (sizeof(long)*8)
#else
/* Ensure Linux types u32, etc. are defined */
#include <linux/types.h>
#endif

/** @brief Mali Boolean type which uses MALI_TRUE and MALI_FALSE
  */
typedef unsigned long mali_bool;

#ifndef MALI_TRUE
#define MALI_TRUE ((mali_bool)1)
#endif

#ifndef MALI_FALSE
#define MALI_FALSE ((mali_bool)0)
#endif

#define MALI_HW_CORE_NO_COUNTER     ((u32)-1)


#define MALI_S32_MAX 0x7fffffff

/**
 * @brief OSK Error codes
 *
 * Each OS may use its own set of error codes, and may require that the
 * User/Kernel interface take certain error code. This means that the common
 * error codes need to be sufficiently rich to pass the correct error code
 * thorugh from the OSK to U/K layer, across all OSs.
 *
 * The result is that some error codes will appear redundant on some OSs.
 * Under all OSs, the OSK layer must translate native OS error codes to
 * _mali_osk_errcode_t codes. Similarly, the U/K layer must translate from
 * _mali_osk_errcode_t codes to native OS error codes.
 */
typedef enum {
	_MALI_OSK_ERR_OK = 0, /**< Success. */
	_MALI_OSK_ERR_FAULT = -1, /**< General non-success */
	_MALI_OSK_ERR_INVALID_FUNC = -2, /**< Invalid function requested through User/Kernel interface (e.g. bad IOCTL number) */
	_MALI_OSK_ERR_INVALID_ARGS = -3, /**< Invalid arguments passed through User/Kernel interface */
	_MALI_OSK_ERR_NOMEM = -4, /**< Insufficient memory */
	_MALI_OSK_ERR_TIMEOUT = -5, /**< Timeout occurred */
	_MALI_OSK_ERR_RESTARTSYSCALL = -6, /**< Special: On certain OSs, must report when an interruptable mutex is interrupted. Ignore otherwise. */
	_MALI_OSK_ERR_ITEM_NOT_FOUND = -7, /**< Table Lookup failed */
	_MALI_OSK_ERR_BUSY = -8, /**< Device/operation is busy. Try again later */
	_MALI_OSK_ERR_UNSUPPORTED = -9, /**< Optional part of the interface used, and is unsupported */
} _mali_osk_errcode_t;

/** @} */ /* end group _mali_osk_miscellaneous */

/** @defgroup _mali_osk_wq OSK work queues
 * @{ */

/** @brief Private type for work objects */
typedef struct _mali_osk_wq_work_s _mali_osk_wq_work_t;
typedef struct _mali_osk_wq_delayed_work_s _mali_osk_wq_delayed_work_t;

/** @brief Work queue handler function
 *
 * This function type is called when the work is scheduled by the work queue,
 * e.g. as an IRQ bottom-half handler.
 *
 * Refer to \ref _mali_osk_wq_schedule_work() for more information on the
 * work-queue and work handlers.
 *
 * @param arg resource-specific data
 */
typedef void (*_mali_osk_wq_work_handler_t)(void *arg);

/* @} */ /* end group _mali_osk_wq */

/** @defgroup _mali_osk_irq OSK IRQ handling
 * @{ */

/** @brief Private type for IRQ handling objects */
typedef struct _mali_osk_irq_t_struct _mali_osk_irq_t;

/** @brief Optional function to trigger an irq from a resource
 *
 * This function is implemented by the common layer to allow probing of a resource's IRQ.
 * @param arg resource-specific data */
typedef void (*_mali_osk_irq_trigger_t)(void *arg);

/** @brief Optional function to acknowledge an irq from a resource
 *
 * This function is implemented by the common layer to allow probing of a resource's IRQ.
 * @param arg resource-specific data
 * @return _MALI_OSK_ERR_OK if the IRQ was successful, or a suitable _mali_osk_errcode_t on failure. */
typedef _mali_osk_errcode_t (*_mali_osk_irq_ack_t)(void *arg);

/** @brief IRQ 'upper-half' handler callback.
 *
 * This function is implemented by the common layer to do the initial handling of a
 * resource's IRQ. This maps on to the concept of an ISR that does the minimum
 * work necessary before handing off to an IST.
 *
 * The communication of the resource-specific data from the ISR to the IST is
 * handled by the OSK implementation.
 *
 * On most systems, the IRQ upper-half handler executes in IRQ context.
 * Therefore, the system may have restrictions about what can be done in this
 * context
 *
 * If an IRQ upper-half handler requires more work to be done than can be
 * acheived in an IRQ context, then it may defer the work with
 * _mali_osk_wq_schedule_work(). Refer to \ref _mali_osk_wq_create_work() for
 * more information.
 *
 * @param arg resource-specific data
 * @return _MALI_OSK_ERR_OK if the IRQ was correctly handled, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
typedef _mali_osk_errcode_t (*_mali_osk_irq_uhandler_t)(void *arg);


/** @} */ /* end group _mali_osk_irq */


/** @defgroup _mali_osk_atomic OSK Atomic counters
 * @{ */

/** @brief Public type of atomic counters
 *
 * This is public for allocation on stack. On systems that support it, this is just a single 32-bit value.
 * On others, it could be encapsulating an object stored elsewhere.
 *
 * Regardless of implementation, the \ref _mali_osk_atomic functions \b must be used
 * for all accesses to the variable's value, even if atomicity is not required.
 * Do not access u.val or u.obj directly.
 */
typedef struct {
	union {
		u32 val;
		void *obj;
	} u;
} _mali_osk_atomic_t;
/** @} */ /* end group _mali_osk_atomic */


/** @defgroup _mali_osk_lock OSK Mutual Exclusion Locks
 * @{ */


/** @brief OSK Mutual Exclusion Lock ordered list
 *
 * This lists the various types of locks in the system and is used to check
 * that locks are taken in the correct order.
 *
 * - Holding more than one lock of the same order at the same time is not
 *   allowed.
 * - Taking a lock of a lower order than the highest-order lock currently held
 *   is not allowed.
 *
 */
typedef enum {
	/*  ||    Locks    ||  */
	/*  ||   must be   ||  */
	/* _||_  taken in _||_ */
	/* \  /    this   \  / */
	/*  \/    order!   \/  */

	_MALI_OSK_LOCK_ORDER_FIRST = 0,

	_MALI_OSK_LOCK_ORDER_SESSIONS,
	_MALI_OSK_LOCK_ORDER_MEM_SESSION,
	_MALI_OSK_LOCK_ORDER_MEM_INFO,
	_MALI_OSK_LOCK_ORDER_MEM_PT_CACHE,
	_MALI_OSK_LOCK_ORDER_DESCRIPTOR_MAP,
	_MALI_OSK_LOCK_ORDER_PM_EXECUTION,
	_MALI_OSK_LOCK_ORDER_EXECUTOR,
	_MALI_OSK_LOCK_ORDER_TIMELINE_SYSTEM,
	_MALI_OSK_LOCK_ORDER_SCHEDULER,
	_MALI_OSK_LOCK_ORDER_SCHEDULER_DEFERRED,
	_MALI_OSK_LOCK_ORDER_PROFILING,
	_MALI_OSK_LOCK_ORDER_L2,
	_MALI_OSK_LOCK_ORDER_L2_COMMAND,
	_MALI_OSK_LOCK_ORDER_UTILIZATION,
	_MALI_OSK_LOCK_ORDER_SESSION_PENDING_JOBS,
	_MALI_OSK_LOCK_ORDER_PM_STATE,

	_MALI_OSK_LOCK_ORDER_LAST,
} _mali_osk_lock_order_t;


/** @brief OSK Mutual Exclusion Lock flags type
 *
 * - Any lock can use the order parameter.
 */
typedef enum {
	_MALI_OSK_LOCKFLAG_UNORDERED        = 0x1, /**< Indicate that the order of this lock should not be checked */
	_MALI_OSK_LOCKFLAG_ORDERED          = 0x2,
	/** @enum _mali_osk_lock_flags_t
	 *
	 * Flags from 0x10000--0x80000000 are RESERVED for User-mode */

} _mali_osk_lock_flags_t;

/** @brief Mutual Exclusion Lock Mode Optimization hint
 *
 * The lock mode is used to implement the read/write locking of locks when we call
 * functions _mali_osk_mutex_rw_init/wait/signal/term/. In this case, the RO mode can
 * be used to allow multiple concurrent readers, but no writers. The RW mode is used for
 * writers, and so will wait for all readers to release the lock (if any present).
 * Further readers and writers will wait until the writer releases the lock.
 *
 * The mode is purely an optimization hint: for example, it is permissible for
 * all locks to behave in RW mode, regardless of that supplied.
 *
 * It is an error to attempt to use locks in anything other that RW mode when
 * call functions _mali_osk_mutex_rw_wait/signal().
 *
 */
typedef enum {
	_MALI_OSK_LOCKMODE_UNDEF = -1,  /**< Undefined lock mode. For internal use only */
	_MALI_OSK_LOCKMODE_RW    = 0x0, /**< Read-write mode, default. All readers and writers are mutually-exclusive */
	_MALI_OSK_LOCKMODE_RO,          /**< Read-only mode, to support multiple concurrent readers, but mutual exclusion in the presence of writers. */
	/** @enum _mali_osk_lock_mode_t
	 *
	 * Lock modes 0x40--0x7F are RESERVED for User-mode */
} _mali_osk_lock_mode_t;

/** @brief Private types for Mutual Exclusion lock objects */
typedef struct _mali_osk_lock_debug_s _mali_osk_lock_debug_t;
typedef struct _mali_osk_spinlock_s _mali_osk_spinlock_t;
typedef struct _mali_osk_spinlock_irq_s _mali_osk_spinlock_irq_t;
typedef struct _mali_osk_mutex_s _mali_osk_mutex_t;
typedef struct _mali_osk_mutex_rw_s _mali_osk_mutex_rw_t;

/** @} */ /* end group _mali_osk_lock */

/** @defgroup _mali_osk_low_level_memory OSK Low-level Memory Operations
 * @{ */

/**
 * @brief Private data type for use in IO accesses to/from devices.
 *
 * This represents some range that is accessible from the device. Examples
 * include:
 * - Device Registers, which could be readable and/or writeable.
 * - Memory that the device has access to, for storing configuration structures.
 *
 * Access to this range must be made through the _mali_osk_mem_ioread32() and
 * _mali_osk_mem_iowrite32() functions.
 */
typedef struct _mali_io_address *mali_io_address;

/** @defgroup _MALI_OSK_CPU_PAGE CPU Physical page size macros.
 *
 * The order of the page size is supplied for
 * ease of use by algorithms that might require it, since it is easier to know
 * it ahead of time rather than calculating it.
 *
 * The Mali Page Mask macro masks off the lower bits of a physical address to
 * give the start address of the page for that physical address.
 *
 * @note The Mali device driver code is designed for systems with 4KB page size.
 * Changing these macros will not make the entire Mali device driver work with
 * page sizes other than 4KB.
 *
 * @note The CPU Physical Page Size has been assumed to be the same as the Mali
 * Physical Page Size.
 *
 * @{
 */

/** CPU Page Order, as log to base 2 of the Page size. @see _MALI_OSK_CPU_PAGE_SIZE */
#define _MALI_OSK_CPU_PAGE_ORDER ((u32)12)
/** CPU Page Size, in bytes.               */
#define _MALI_OSK_CPU_PAGE_SIZE (((u32)1) << (_MALI_OSK_CPU_PAGE_ORDER))
/** CPU Page Mask, which masks off the offset within a page */
#define _MALI_OSK_CPU_PAGE_MASK (~((((u32)1) << (_MALI_OSK_CPU_PAGE_ORDER)) - ((u32)1)))
/** @} */ /* end of group _MALI_OSK_CPU_PAGE */

/** @defgroup _MALI_OSK_MALI_PAGE Mali Physical Page size macros
 *
 * Mali Physical page size macros. The order of the page size is supplied for
 * ease of use by algorithms that might require it, since it is easier to know
 * it ahead of time rather than calculating it.
 *
 * The Mali Page Mask macro masks off the lower bits of a physical address to
 * give the start address of the page for that physical address.
 *
 * @note The Mali device driver code is designed for systems with 4KB page size.
 * Changing these macros will not make the entire Mali device driver work with
 * page sizes other than 4KB.
 *
 * @note The Mali Physical Page Size has been assumed to be the same as the CPU
 * Physical Page Size.
 *
 * @{
 */

/** Mali Page Order, as log to base 2 of the Page size. @see _MALI_OSK_MALI_PAGE_SIZE */
#define _MALI_OSK_MALI_PAGE_ORDER PAGE_SHIFT
/** Mali Page Size, in bytes.               */
#define _MALI_OSK_MALI_PAGE_SIZE PAGE_SIZE
/** Mali Page Mask, which masks off the offset within a page */
#define _MALI_OSK_MALI_PAGE_MASK PAGE_MASK
/** @} */ /* end of group _MALI_OSK_MALI_PAGE*/

/** @brief flags for mapping a user-accessible memory range
 *
 * Where a function with prefix '_mali_osk_mem_mapregion' accepts flags as one
 * of the function parameters, it will use one of these. These allow per-page
 * control over mappings. Compare with the mali_memory_allocation_flag type,
 * which acts over an entire range
 *
 * These may be OR'd together with bitwise OR (|), but must be cast back into
 * the type after OR'ing.
 */
typedef enum {
	_MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR = 0x1, /**< Physical address is OS Allocated */
} _mali_osk_mem_mapregion_flags_t;
/** @} */ /* end group _mali_osk_low_level_memory */

/** @defgroup _mali_osk_notification OSK Notification Queues
 * @{ */

/** @brief Private type for notification queue objects */
typedef struct _mali_osk_notification_queue_t_struct _mali_osk_notification_queue_t;

/** @brief Public notification data object type */
typedef struct _mali_osk_notification_t_struct {
	u32 notification_type;   /**< The notification type */
	u32 result_buffer_size; /**< Size of the result buffer to copy to user space */
	void *result_buffer;    /**< Buffer containing any type specific data */
} _mali_osk_notification_t;

/** @} */ /* end group _mali_osk_notification */


/** @defgroup _mali_osk_timer OSK Timer Callbacks
 * @{ */

/** @brief Function to call when a timer expires
 *
 * When a timer expires, this function is called. Note that on many systems,
 * a timer callback will be executed in IRQ context. Therefore, restrictions
 * may apply on what can be done inside the timer callback.
 *
 * If a timer requires more work to be done than can be acheived in an IRQ
 * context, then it may defer the work with a work-queue. For example, it may
 * use \ref _mali_osk_wq_schedule_work() to make use of a bottom-half handler
 * to carry out the remaining work.
 *
 * Stopping the timer with \ref _mali_osk_timer_del() blocks on compeletion of
 * the callback. Therefore, the callback may not obtain any mutexes also held
 * by any callers of _mali_osk_timer_del(). Otherwise, a deadlock may occur.
 *
 * @param arg Function-specific data */
typedef void (*_mali_osk_timer_callback_t)(void *arg);

/** @brief Private type for Timer Callback Objects */
typedef struct _mali_osk_timer_t_struct _mali_osk_timer_t;
/** @} */ /* end group _mali_osk_timer */


/** @addtogroup _mali_osk_list OSK Doubly-Linked Circular Lists
 * @{ */

/** @brief Public List objects.
 *
 * To use, add a _mali_osk_list_t member to the structure that may become part
 * of a list. When traversing the _mali_osk_list_t objects, use the
 * _MALI_OSK_CONTAINER_OF() macro to recover the structure from its
 *_mali_osk_list_t member
 *
 * Each structure may have multiple _mali_osk_list_t members, so that the
 * structure is part of multiple lists. When traversing lists, ensure that the
 * correct _mali_osk_list_t member is used, because type-checking will be
 * lost by the compiler.
 */
typedef struct _mali_osk_list_s {
	struct _mali_osk_list_s *next;
	struct _mali_osk_list_s *prev;
} _mali_osk_list_t;
/** @} */ /* end group _mali_osk_list */

/** @addtogroup _mali_osk_miscellaneous
 * @{ */

/** @brief resource description struct
 *
 * Platform independent representation of a Mali HW resource
 */
typedef struct _mali_osk_resource {
	const char *description;        /**< short description of the resource */
	uintptr_t base;                 /**< Physical base address of the resource, as seen by Mali resources. */
	const char *irq_name;           /**< Name of irq belong to this resource */
	u32 irq;                        /**< IRQ number delivered to the CPU, or -1 to tell the driver to probe for it (if possible) */
} _mali_osk_resource_t;
/** @} */ /* end group _mali_osk_miscellaneous */

/** @defgroup _mali_osk_wait_queue OSK Wait Queue functionality
 * @{ */
/** @brief Private type for wait queue objects */
typedef struct _mali_osk_wait_queue_t_struct _mali_osk_wait_queue_t;
/** @} */ /* end group _mali_osk_wait_queue */

/** @} */ /* end group osuapi */

/** @} */ /* end group uddapi */

/** @brief Mali print ctx type which uses seq_file
  */
typedef struct seq_file _mali_osk_print_ctx;

#define _MALI_OSK_BITMAP_INVALIDATE_INDEX -1

typedef struct _mali_osk_bitmap {
	u32         reserve;
	u32         last;
	u32         max;
	u32         avail;
	_mali_osk_spinlock_t   *lock;
	unsigned long          *table;
} _mali_osk_bitmap_t;


#ifdef __cplusplus
}
#endif

#endif /* __MALI_OSK_TYPES_H__ */
