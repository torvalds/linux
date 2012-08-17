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
 * @file mali_osk.h
 * Defines the OS abstraction layer for the kernel device driver (OSK)
 */

#ifndef __MALI_OSK_H__
#define __MALI_OSK_H__

#ifdef __cplusplus
extern "C"
{
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
typedef enum
{
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


/** @defgroup _mali_osk_irq OSK IRQ handling
 * @{ */

/** @brief Private type for IRQ handling objects */
typedef struct _mali_osk_irq_t_struct _mali_osk_irq_t;

/** @brief Optional function to trigger an irq from a resource
 *
 * This function is implemented by the common layer to allow probing of a resource's IRQ.
 * @param arg resource-specific data */
typedef void  (*_mali_osk_irq_trigger_t)( void * arg );

/** @brief Optional function to acknowledge an irq from a resource
 *
 * This function is implemented by the common layer to allow probing of a resource's IRQ.
 * @param arg resource-specific data
 * @return _MALI_OSK_ERR_OK if the IRQ was successful, or a suitable _mali_osk_errcode_t on failure. */
typedef _mali_osk_errcode_t (*_mali_osk_irq_ack_t)( void * arg );

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
 * _mali_osk_irq_schedulework(). Refer to \ref _mali_osk_irq_schedulework() for
 * more information.
 *
 * @param arg resource-specific data
 * @return _MALI_OSK_ERR_OK if the IRQ was correctly handled, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
typedef _mali_osk_errcode_t  (*_mali_osk_irq_uhandler_t)( void * arg );

/** @brief IRQ 'bottom-half' handler callback.
 *
 * This function is implemented by the common layer to do the deferred handling
 * of a resource's IRQ. Usually, this work cannot be carried out in IRQ context
 * by the IRQ upper-half handler.
 *
 * The IRQ bottom-half handler maps on to the concept of an IST that may
 * execute some time after the actual IRQ has fired.
 *
 * All OSK-registered IRQ bottom-half handlers will be serialized, across all
 * CPU-cores in the system.
 *
 * Refer to \ref _mali_osk_irq_schedulework() for more information on the
 * IRQ work-queue, and the calling of the IRQ bottom-half handler.
 *
 * @param arg resource-specific data
 */
typedef void (*_mali_osk_irq_bhandler_t)( void * arg );
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
typedef struct
{
    union
    {
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
 * Holding more than one lock of the same order at the same time is not
 * allowed.
 *
 */
typedef enum
{
	_MALI_OSK_LOCK_ORDER_LAST = 0,

	_MALI_OSK_LOCK_ORDER_PM_EXECUTE,
	_MALI_OSK_LOCK_ORDER_UTILIZATION,
	_MALI_OSK_LOCK_ORDER_L2_COUNTER,
	_MALI_OSK_LOCK_ORDER_PROFILING,
	_MALI_OSK_LOCK_ORDER_L2_COMMAND,
	_MALI_OSK_LOCK_ORDER_PM_CORE_STATE,
	_MALI_OSK_LOCK_ORDER_GROUP,
	_MALI_OSK_LOCK_ORDER_SCHEDULER,

	_MALI_OSK_LOCK_ORDER_DESCRIPTOR_MAP,
	_MALI_OSK_LOCK_ORDER_MEM_PT_CACHE,
	_MALI_OSK_LOCK_ORDER_MEM_INFO,
	_MALI_OSK_LOCK_ORDER_MEM_SESSION,

	_MALI_OSK_LOCK_ORDER_SESSIONS,

	_MALI_OSK_LOCK_ORDER_FIRST
} _mali_osk_lock_order_t;


/** @brief OSK Mutual Exclusion Lock flags type
 *
 * Flags are supplied at the point where the Lock is initialized. Each flag can
 * be combined with others using bitwise OR, '|'.
 *
 * The flags must be sufficiently rich to cope with all our OSs. This means
 * that on some OSs, certain flags can be completely ignored. We define a
 * number of terms that are significant across all OSs:
 *
 * - Sleeping/non-sleeping mutexs. Sleeping mutexs can block on waiting, and so
 * schedule out the current thread. This is significant on OSs where there are
 * situations in which the current thread must not be put to sleep. On OSs
 * without this restriction, sleeping and non-sleeping mutexes can be treated
 * as the same (if that is required).
 * - Interruptable/non-interruptable mutexes. For sleeping mutexes, it may be
 * possible for the sleep to be interrupted for a reason other than the thread
 * being able to obtain the lock. OSs behaving in this way may provide a
 * mechanism to control whether sleeping mutexes can be interrupted. On OSs
 * that do not support the concept of interruption, \b or they do not support
 * control of mutex interruption, then interruptable mutexes may be treated
 * as non-interruptable.
 *
 * Some constrains apply to the lock type flags:
 *
 * - Spinlocks are by nature, non-interruptable. Hence, they must always be
 * combined with the NONINTERRUPTABLE flag, because it is meaningless to ask
 * for a spinlock that is interruptable (and this highlights its
 * non-interruptable-ness). For example, on certain OSs they should be used when
 * you must not sleep.
 * - Reader/writer is an optimization hint, and any type of lock can be
 * reader/writer. Since this is an optimization hint, the implementation need
 * not respect this for any/all types of lock. For example, on certain OSs,
 * there's no interruptable reader/writer mutex. If such a thing were requested
 * on that OS, the fact that interruptable was requested takes priority over the
 * reader/writer-ness, because reader/writer-ness is not necessary for correct
 * operation.
 * - Any lock can use the order parameter.
 * - A onelock is an optimization hint specific to certain OSs. It can be
 * specified when it is known that only one lock will be held by the thread,
 * and so can provide faster mutual exclusion. This can be safely ignored if
 * such optimization is not required/present.
 *
 * The absence of any flags (the value 0) results in a sleeping-mutex, which is interruptable.
 */
typedef enum
{
	_MALI_OSK_LOCKFLAG_SPINLOCK = 0x1,          /**< Specifically, don't sleep on those architectures that require it */
	_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE = 0x2,  /**< The mutex cannot be interrupted, e.g. delivery of signals on those architectures where this is required */
	_MALI_OSK_LOCKFLAG_READERWRITER = 0x4,      /**< Optimise for readers/writers */
	_MALI_OSK_LOCKFLAG_ORDERED = 0x8,           /**< Use the order parameter; otherwise use automatic ordering */
	_MALI_OSK_LOCKFLAG_ONELOCK = 0x10,          /**< Each thread can only hold one lock at a time */
	_MALI_OSK_LOCKFLAG_SPINLOCK_IRQ = 0x20,    /**<  IRQ version of spinlock */
	/** @enum _mali_osk_lock_flags_t
	 *
	 * Flags from 0x10000--0x80000000 are RESERVED for User-mode */

} _mali_osk_lock_flags_t;

/** @brief Mutual Exclusion Lock Mode Optimization hint
 *
 * The lock mode is used to implement the read/write locking of locks specified
 * as _MALI_OSK_LOCKFLAG_READERWRITER. In this case, the RO mode can be used
 * to allow multiple concurrent readers, but no writers. The RW mode is used for
 * writers, and so will wait for all readers to release the lock (if any present).
 * Further readers and writers will wait until the writer releases the lock.
 *
 * The mode is purely an optimization hint: for example, it is permissible for
 * all locks to behave in RW mode, regardless of that supplied.
 *
 * It is an error to attempt to use locks in anything other that RW mode when
 * _MALI_OSK_LOCKFLAG_READERWRITER is not supplied.
 *
 */
typedef enum
{
	_MALI_OSK_LOCKMODE_UNDEF = -1,  /**< Undefined lock mode. For internal use only */
	_MALI_OSK_LOCKMODE_RW    = 0x0, /**< Read-write mode, default. All readers and writers are mutually-exclusive */
	_MALI_OSK_LOCKMODE_RO,          /**< Read-only mode, to support multiple concurrent readers, but mutual exclusion in the presence of writers. */
	/** @enum _mali_osk_lock_mode_t
	 *
	 * Lock modes 0x40--0x7F are RESERVED for User-mode */
} _mali_osk_lock_mode_t;

/** @brief Private type for Mutual Exclusion lock objects */
typedef struct _mali_osk_lock_t_struct _mali_osk_lock_t;

#ifdef DEBUG
/** @brief Macro for asserting that the current thread holds a given lock
 */
#define MALI_DEBUG_ASSERT_LOCK_HELD(l) MALI_DEBUG_ASSERT(_mali_osk_lock_get_owner(l) == _mali_osk_get_tid());

/** @brief returns a lock's owner (thread id) if debugging is enabled
 */
u32 _mali_osk_lock_get_owner( _mali_osk_lock_t *lock );
#endif

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
typedef struct _mali_io_address * mali_io_address;

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
#define _MALI_OSK_MALI_PAGE_ORDER ((u32)12)
/** Mali Page Size, in bytes.               */
#define _MALI_OSK_MALI_PAGE_SIZE (((u32)1) << (_MALI_OSK_MALI_PAGE_ORDER))
/** Mali Page Mask, which masks off the offset within a page */
#define _MALI_OSK_MALI_PAGE_MASK (~((((u32)1) << (_MALI_OSK_MALI_PAGE_ORDER)) - ((u32)1)))
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
typedef enum
{
	_MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR = 0x1, /**< Physical address is OS Allocated */
} _mali_osk_mem_mapregion_flags_t;
/** @} */ /* end group _mali_osk_low_level_memory */

/** @defgroup _mali_osk_notification OSK Notification Queues
 * @{ */

/** @brief Private type for notification queue objects */
typedef struct _mali_osk_notification_queue_t_struct _mali_osk_notification_queue_t;

/** @brief Public notification data object type */
typedef struct _mali_osk_notification_t_struct
{
	u32 notification_type;   /**< The notification type */
	u32 result_buffer_size; /**< Size of the result buffer to copy to user space */
	void * result_buffer;   /**< Buffer containing any type specific data */
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
 * use \ref _mali_osk_irq_schedulework() to make use of the IRQ bottom-half handler
 * to carry out the remaining work.
 *
 * Stopping the timer with \ref _mali_osk_timer_del() blocks on compeletion of
 * the callback. Therefore, the callback may not obtain any mutexes also held
 * by any callers of _mali_osk_timer_del(). Otherwise, a deadlock may occur.
 *
 * @param arg Function-specific data */
typedef void (*_mali_osk_timer_callback_t)(void * arg );

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
typedef struct _mali_osk_list_s
{
	struct _mali_osk_list_s *next;
	struct _mali_osk_list_s *prev;
} _mali_osk_list_t;

/** @brief Initialize a list to be a head of an empty list
 * @param exp the list to initialize. */
#define _MALI_OSK_INIT_LIST_HEAD(exp) _mali_osk_list_init(exp)

/** @brief Define a list variable, which is uninitialized.
 * @param exp the name of the variable that the list will be defined as. */
#define _MALI_OSK_LIST_HEAD(exp)      _mali_osk_list_t exp

/** @brief Find the containing structure of another structure
 *
 * This is the reverse of the operation 'offsetof'. This means that the
 * following condition is satisfied:
 *
 *   ptr == _MALI_OSK_CONTAINER_OF( &ptr->member, type, member )
 *
 * When ptr is of type 'type'.
 *
 * Its purpose it to recover a larger structure that has wrapped a smaller one.
 *
 * @note no type or memory checking occurs to ensure that a wrapper structure
 * does in fact exist, and that it is being recovered with respect to the
 * correct member.
 *
 * @param ptr the pointer to the member that is contained within the larger
 * structure
 * @param type the type of the structure that contains the member
 * @param member the name of the member in the structure that ptr points to.
 * @return a pointer to a \a type object which contains \a member, as pointed
 * to by \a ptr.
 */
#define _MALI_OSK_CONTAINER_OF(ptr, type, member) \
             ((type *)( ((char *)ptr) - offsetof(type,member) ))

/** @brief Find the containing structure of a list
 *
 * When traversing a list, this is used to recover the containing structure,
 * given that is contains a _mali_osk_list_t member.
 *
 * Each list must be of structures of one type, and must link the same members
 * together, otherwise it will not be possible to correctly recover the
 * sturctures that the lists link.
 *
 * @note no type or memory checking occurs to ensure that a structure does in
 * fact exist for the list entry, and that it is being recovered with respect
 * to the correct list member.
 *
 * @param ptr the pointer to the _mali_osk_list_t member in this structure
 * @param type the type of the structure that contains the member
 * @param member the member of the structure that ptr points to.
 * @return a pointer to a \a type object which contains the _mali_osk_list_t
 * \a member, as pointed to by the _mali_osk_list_t \a *ptr.
 */
#define _MALI_OSK_LIST_ENTRY(ptr, type, member) \
            _MALI_OSK_CONTAINER_OF(ptr, type, member)

/** @brief Enumerate a list safely
 *
 * With this macro, lists can be enumerated in a 'safe' manner. That is,
 * entries can be deleted from the list without causing an error during
 * enumeration. To achieve this, a 'temporary' pointer is required, which must
 * be provided to the macro.
 *
 * Use it like a 'for()', 'while()' or 'do()' construct, and so it must be
 * followed by a statement or compound-statement which will be executed for
 * each list entry.
 *
 * Upon loop completion, providing that an early out was not taken in the
 * loop body, then it is guaranteed that ptr->member == list, even if the loop
 * body never executed.
 *
 * @param ptr a pointer to an object of type 'type', which points to the
 * structure that contains the currently enumerated list entry.
 * @param tmp a pointer to an object of type 'type', which must not be used
 * inside the list-execution statement.
 * @param list a pointer to a _mali_osk_list_t, from which enumeration will
 * begin
 * @param type the type of the structure that contains the _mali_osk_list_t
 * member that is part of the list to be enumerated.
 * @param member the _mali_osk_list_t member of the structure that is part of
 * the list to be enumerated.
 */
#define _MALI_OSK_LIST_FOREACHENTRY(ptr, tmp, list, type, member)         \
        for (ptr = _MALI_OSK_LIST_ENTRY((list)->next, type, member),      \
             tmp = _MALI_OSK_LIST_ENTRY(ptr->member.next, type, member); \
             &ptr->member != (list);                                    \
             ptr = tmp, tmp = _MALI_OSK_LIST_ENTRY(tmp->member.next, type, member))
/** @} */ /* end group _mali_osk_list */


/** @addtogroup _mali_osk_miscellaneous
 * @{ */

/** @brief The known resource types
 *
 * @note \b IMPORTANT: these must remain fixed, and only be extended. This is
 * because not all systems use a header file for reading in their resources.
 * The resources may instead come from a data file where these resources are
 * 'hard-coded' in, because there's no easy way of transferring the enum values
 * into such data files. E.g. the C-Pre-processor does \em not process enums.
 */
typedef enum _mali_osk_resource_type
{
	RESOURCE_TYPE_FIRST =0,  /**< Duplicate resource marker for the first resource*/

	MEMORY              =0,  /**< Physically contiguous memory block, not managed by the OS */
	OS_MEMORY           =1,  /**< Memory managed by and shared with the OS */

	MALI_PP             =2,  /**< Mali Pixel Processor core */
	MALI450PP           =2,  /**< Compatibility option */
	MALI400PP           =2,  /**< Compatibility option */
	MALI300PP           =2,  /**< Compatibility option */
	MALI200             =2,  /**< Compatibility option */
	
	MALI_GP             =3,  /**< Mali Geometry Processor core */
	MALI450GP           =3,  /**< Compatibility option */
	MALI400GP           =3,  /**< Compatibility option */
	MALI300GP           =3,  /**< Compatibility option */
	MALIGP2             =3,  /**< Compatibility option */

	MMU                 =4,  /**< Mali MMU (Memory Management Unit) */

	FPGA_FRAMEWORK      =5,  /**< Mali registers specific to FPGA implementations */

	MALI_L2             =6,  /**< Mali Level 2 cache core */
	MALI450L2           =6,  /**< Compatibility option */
	MALI400L2           =6,  /**< Compatibility option */
	MALI300L2           =6,  /**< Compatibility option */

	MEM_VALIDATION      =7, /**< External Memory Validator */

	PMU                 =8, /**< Power Manangement Unit */

	RESOURCE_TYPE_COUNT      /**< The total number of known resources */
} _mali_osk_resource_type_t;

/** @brief resource description struct
 *
 * _mali_osk_resources_init() will enumerate objects of this type. Not all
 * members have a valid meaning across all types.
 *
 * The mmu_id is used to group resources to a certain MMU, since there may be
 * more than one MMU in the system, and each resource may be using a different
 * MMU:
 * - For MMU resources, the setting of mmu_id is a uniquely identifying number.
 * - For Other resources, the setting of mmu_id determines which MMU the
 * resource uses.
 */
typedef struct _mali_osk_resource
{
	_mali_osk_resource_type_t type; /**< type of the resource */
	const char * description;       /**< short description of the resource */
	u32 base;                       /**< Physical base address of the resource, as seen by Mali resources. */
	s32 cpu_usage_adjust;           /**< Offset added to the base address of the resource to arrive at the CPU physical address of the resource (if different from the Mali physical address) */
	u32 size;                       /**< Size in bytes of the resource - either the size of its register range, or the size of the memory block. */
	u32 irq;                        /**< IRQ number delivered to the CPU, or -1 to tell the driver to probe for it (if possible) */
	u32 flags;                      /**< Resources-specific flags. */
	u32 mmu_id;                     /**< Identifier for Mali MMU resources. */
	u32 alloc_order;                /**< Order in which MEMORY/OS_MEMORY resources are used */
} _mali_osk_resource_t;
/** @} */ /* end group _mali_osk_miscellaneous */


#include "mali_kernel_memory_engine.h"   /* include for mali_memory_allocation and mali_physical_memory_allocation type */

/** @addtogroup _mali_osk_irq
 * @{ */

/** @brief Fake IRQ number for testing purposes
 */
#define _MALI_OSK_IRQ_NUMBER_FAKE ((u32)0xFFFFFFF1)

/** @addtogroup _mali_osk_irq
 * @{ */

/** @brief PMM Virtual IRQ number
 */
#define _MALI_OSK_IRQ_NUMBER_PMM ((u32)0xFFFFFFF2)


/** @brief Initialize IRQ handling for a resource
 *
 * The _mali_osk_irq_t returned must be written into the resource-specific data
 * pointed to by data. This is so that the upper and lower handlers can call
 * _mali_osk_irq_schedulework().
 *
 * @note The caller must ensure that the resource does not generate an
 * interrupt after _mali_osk_irq_init() finishes, and before the
 * _mali_osk_irq_t is written into the resource-specific data. Otherwise,
 * the upper-half handler will fail to call _mali_osk_irq_schedulework().
 *
 * @param irqnum The IRQ number that the resource uses, as seen by the CPU.
 * The value -1 has a special meaning which indicates the use of probing, and trigger_func and ack_func must be
 * non-NULL.
 * @param uhandler The upper-half handler, corresponding to a ISR handler for
 * the resource
 * @param bhandler The lower-half handler, corresponding to an IST handler for
 * the resource
 * @param trigger_func Optional: a function to trigger the resource's irq, to
 * probe for the interrupt. Use NULL if irqnum != -1.
 * @param ack_func Optional: a function to acknowledge the resource's irq, to
 * probe for the interrupt. Use NULL if irqnum != -1.
 * @param data resource-specific data, which will be passed to uhandler,
 * bhandler and (if present) trigger_func and ack_funnc
 * @param description textual description of the IRQ resource.
 * @return on success, a pointer to a _mali_osk_irq_t object, which represents
 * the IRQ handling on this resource. NULL on failure.
 */
_mali_osk_irq_t *_mali_osk_irq_init( u32 irqnum, _mali_osk_irq_uhandler_t uhandler,	_mali_osk_irq_bhandler_t bhandler, _mali_osk_irq_trigger_t trigger_func, _mali_osk_irq_ack_t ack_func, void *data, const char *description );

/** @brief Cause a queued, deferred call of the IRQ bottom-half.
 *
 * _mali_osk_irq_schedulework provides a mechanism for enqueuing deferred calls
 * to the IRQ bottom-half handler. The queue is known as the IRQ work-queue.
 * After calling _mali_osk_irq_schedulework(), the IRQ bottom-half handler will
 * be scheduled to run at some point in the future.
 *
 * This is called by the IRQ upper-half to defer further processing of
 * IRQ-related work to the IRQ bottom-half handler. This is necessary for work
 * that cannot be done in an IRQ context by the IRQ upper-half handler. Timer
 * callbacks also use this mechanism, because they are treated as though they
 * operate in an IRQ context. Refer to \ref _mali_osk_timer_t for more
 * information.
 *
 * Code that operates in a kernel-process context (with no IRQ context
 * restrictions) may also enqueue deferred calls to the IRQ bottom-half. The
 * advantage over direct calling is that deferred calling allows the caller and
 * IRQ bottom half to hold the same mutex, with a guarantee that they will not
 * deadlock just by using this mechanism.
 *
 * _mali_osk_irq_schedulework() places deferred call requests on a queue, to
 * allow for more than one thread to make a deferred call. Therfore, if it is
 * called 'K' times, then the IRQ bottom-half will be scheduled 'K' times too.
 * 'K' is a number that is implementation-specific.
 *
 * _mali_osk_irq_schedulework() is guaranteed to not block on:
 * - enqueuing a deferred call request.
 * - the completion of the IRQ bottom-half handler.
 *
 * This is to prevent deadlock. For example, if _mali_osk_irq_schedulework()
 * blocked, then it would cause a deadlock when the following two conditions
 * hold:
 * - The IRQ bottom-half callback (of type _mali_osk_irq_bhandler_t) locks
 * a mutex
 * - And, at the same time, the caller of _mali_osk_irq_schedulework() also
 * holds the same mutex
 *
 * @note care must be taken to not overflow the queue that
 * _mali_osk_irq_schedulework() operates on. Code must be structured to
 * ensure that the number of requests made to the queue is bounded. Otherwise,
 * IRQs will be lost.
 *
 * The queue that _mali_osk_irq_schedulework implements is a FIFO of N-writer,
 * 1-reader type. The writers are the callers of _mali_osk_irq_schedulework
 * (all OSK-registered IRQ upper-half handlers in the system, watchdog timers,
 * callers from a Kernel-process context). The reader is a single thread that
 * handles all OSK-registered IRQs.
 *
 * The consequence of the queue being a 1-reader type is that calling
 * _mali_osk_irq_schedulework() on different _mali_osk_irq_t objects causes
 * their IRQ bottom-halves to be serialized, across all CPU-cores in the
 * system.
 *
 * @param irq a pointer to the _mali_osk_irq_t object corresponding to the
 * resource whose IRQ bottom-half must begin processing.
 */
void _mali_osk_irq_schedulework( _mali_osk_irq_t *irq );

/** @brief Terminate IRQ handling on a resource.
 *
 * This will disable the interrupt from the device, and then waits for the
 * IRQ work-queue to finish the work that is currently in the queue. That is,
 * for every deferred call currently in the IRQ work-queue, it waits for each
 * of those to be processed by their respective IRQ bottom-half handler.
 *
 * This function is used to ensure that the bottom-half handler of the supplied
 * IRQ object will not be running at the completion of this function call.
 * However, the caller must ensure that no other sources could call the
 * _mali_osk_irq_schedulework() on the same IRQ object. For example, the
 * relevant timers must be stopped.
 *
 * @note While this function is being called, other OSK-registered IRQs in the
 * system may enqueue work for their respective bottom-half handlers. This
 * function will not wait for those entries in the work-queue to be flushed.
 *
 * Since this blocks on the completion of work in the IRQ work-queue, the
 * caller of this function \b must \b not hold any mutexes that are taken by
 * any OSK-registered IRQ bottom-half handler. To do so may cause a deadlock.
 *
 * @param irq a pointer to the _mali_osk_irq_t object corresponding to the
 * resource whose IRQ handling is to be terminated.
 */
void _mali_osk_irq_term( _mali_osk_irq_t *irq );

/** @brief flushing workqueue.
 *
 * This will flush the workqueue.
 *
 * @param irq a pointer to the _mali_osk_irq_t object corresponding to the
 * resource whose IRQ handling is to be terminated.
 */
void _mali_osk_flush_workqueue( _mali_osk_irq_t *irq );

/** @} */ /* end group _mali_osk_irq */


/** @addtogroup _mali_osk_atomic
 * @{ */

/** @brief Decrement an atomic counter
 *
 * @note It is an error to decrement the counter beyond -(1<<23)
 *
 * @param atom pointer to an atomic counter */
void _mali_osk_atomic_dec( _mali_osk_atomic_t *atom );

/** @brief Decrement an atomic counter, return new value
 *
 * @param atom pointer to an atomic counter
 * @return The new value, after decrement */
u32 _mali_osk_atomic_dec_return( _mali_osk_atomic_t *atom );

/** @brief Increment an atomic counter
 *
 * @note It is an error to increment the counter beyond (1<<23)-1
 *
 * @param atom pointer to an atomic counter */
void _mali_osk_atomic_inc( _mali_osk_atomic_t *atom );

/** @brief Increment an atomic counter, return new value
 *
 * @param atom pointer to an atomic counter */
u32 _mali_osk_atomic_inc_return( _mali_osk_atomic_t *atom );

/** @brief Initialize an atomic counter
 *
 * @note the parameter required is a u32, and so signed integers should be
 * cast to u32.
 *
 * @param atom pointer to an atomic counter
 * @param val the value to initialize the atomic counter.
 * @return _MALI_OSK_ERR_OK on success, otherwise, a suitable
 * _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_osk_atomic_init( _mali_osk_atomic_t *atom, u32 val );

/** @brief Read a value from an atomic counter
 *
 * This can only be safely used to determine the value of the counter when it
 * is guaranteed that other threads will not be modifying the counter. This
 * makes its usefulness limited.
 *
 * @param atom pointer to an atomic counter
 */
u32 _mali_osk_atomic_read( _mali_osk_atomic_t *atom );

/** @brief Terminate an atomic counter
 *
 * @param atom pointer to an atomic counter
 */
void _mali_osk_atomic_term( _mali_osk_atomic_t *atom );
/** @} */  /* end group _mali_osk_atomic */


/** @defgroup _mali_osk_memory OSK Memory Allocation
 * @{ */

/** @brief Allocate zero-initialized memory.
 *
 * Returns a buffer capable of containing at least \a n elements of \a size
 * bytes each. The buffer is initialized to zero.
 *
 * If there is a need for a bigger block of memory (16KB or bigger), then
 * consider to use _mali_osk_vmalloc() instead, as this function might
 * map down to a OS function with size limitations.
 *
 * The buffer is suitably aligned for storage and subsequent access of every
 * type that the compiler supports. Therefore, the pointer to the start of the
 * buffer may be cast into any pointer type, and be subsequently accessed from
 * such a pointer, without loss of information.
 *
 * When the buffer is no longer in use, it must be freed with _mali_osk_free().
 * Failure to do so will cause a memory leak.
 *
 * @note Most toolchains supply memory allocation functions that meet the
 * compiler's alignment requirements.
 *
 * @param n Number of elements to allocate
 * @param size Size of each element
 * @return On success, the zero-initialized buffer allocated. NULL on failure
 */
void *_mali_osk_calloc( u32 n, u32 size );

/** @brief Allocate memory.
 *
 * Returns a buffer capable of containing at least \a size bytes. The
 * contents of the buffer are undefined.
 *
 * If there is a need for a bigger block of memory (16KB or bigger), then
 * consider to use _mali_osk_vmalloc() instead, as this function might
 * map down to a OS function with size limitations.
 *
 * The buffer is suitably aligned for storage and subsequent access of every
 * type that the compiler supports. Therefore, the pointer to the start of the
 * buffer may be cast into any pointer type, and be subsequently accessed from
 * such a pointer, without loss of information.
 *
 * When the buffer is no longer in use, it must be freed with _mali_osk_free().
 * Failure to do so will cause a memory leak.
 *
 * @note Most toolchains supply memory allocation functions that meet the
 * compiler's alignment requirements.
 *
 * Remember to free memory using _mali_osk_free().
 * @param size Number of bytes to allocate
 * @return On success, the buffer allocated. NULL on failure.
 */
void *_mali_osk_malloc( u32 size );

/** @brief Free memory.
 *
 * Reclaims the buffer pointed to by the parameter \a ptr for the system.
 * All memory returned from _mali_osk_malloc() and _mali_osk_calloc()
 * must be freed before the application exits. Otherwise,
 * a memory leak will occur.
 *
 * Memory must be freed once. It is an error to free the same non-NULL pointer
 * more than once.
 *
 * It is legal to free the NULL pointer.
 *
 * @param ptr Pointer to buffer to free
 */
void _mali_osk_free( void *ptr );

/** @brief Allocate memory.
 *
 * Returns a buffer capable of containing at least \a size bytes. The
 * contents of the buffer are undefined.
 *
 * This function is potentially slower than _mali_osk_malloc() and _mali_osk_calloc(),
 * but do support bigger sizes.
 *
 * The buffer is suitably aligned for storage and subsequent access of every
 * type that the compiler supports. Therefore, the pointer to the start of the
 * buffer may be cast into any pointer type, and be subsequently accessed from
 * such a pointer, without loss of information.
 *
 * When the buffer is no longer in use, it must be freed with _mali_osk_free().
 * Failure to do so will cause a memory leak.
 *
 * @note Most toolchains supply memory allocation functions that meet the
 * compiler's alignment requirements.
 *
 * Remember to free memory using _mali_osk_free().
 * @param size Number of bytes to allocate
 * @return On success, the buffer allocated. NULL on failure.
 */
void *_mali_osk_valloc( u32 size );

/** @brief Free memory.
 *
 * Reclaims the buffer pointed to by the parameter \a ptr for the system.
 * All memory returned from _mali_osk_valloc() must be freed before the
 * application exits. Otherwise a memory leak will occur.
 *
 * Memory must be freed once. It is an error to free the same non-NULL pointer
 * more than once.
 *
 * It is legal to free the NULL pointer.
 *
 * @param ptr Pointer to buffer to free
 */
void _mali_osk_vfree( void *ptr );

/** @brief Copies memory.
 *
 * Copies the \a len bytes from the buffer pointed by the parameter \a src
 * directly to the buffer pointed by \a dst.
 *
 * It is an error for \a src to overlap \a dst anywhere in \a len bytes.
 *
 * @param dst Pointer to the destination array where the content is to be
 * copied.
 * @param src Pointer to the source of data to be copied.
 * @param len Number of bytes to copy.
 * @return \a dst is always passed through unmodified.
 */
void *_mali_osk_memcpy( void *dst, const void *src, u32 len );

/** @brief Fills memory.
 *
 * Sets the first \a n bytes of the block of memory pointed to by \a s to
 * the specified value
 * @param s Pointer to the block of memory to fill.
 * @param c Value to be set, passed as u32. Only the 8 Least Significant Bits (LSB)
 * are used.
 * @param n Number of bytes to be set to the value.
 * @return \a s is always passed through unmodified
 */
void *_mali_osk_memset( void *s, u32 c, u32 n );
/** @} */ /* end group _mali_osk_memory */


/** @brief Checks the amount of memory allocated
 *
 * Checks that not more than \a max_allocated bytes are allocated.
 *
 * Some OS bring up an interactive out of memory dialogue when the
 * system runs out of memory. This can stall non-interactive
 * apps (e.g. automated test runs). This function can be used to
 * not trigger the OOM dialogue by keeping allocations
 * within a certain limit.
 *
 * @return MALI_TRUE when \a max_allocated bytes are not in use yet. MALI_FALSE
 * when at least \a max_allocated bytes are in use.
 */
mali_bool _mali_osk_mem_check_allocated( u32 max_allocated );

/** @addtogroup _mali_osk_lock
 * @{ */

/** @brief Initialize a Mutual Exclusion Lock
 *
 * Locks are created in the signalled (unlocked) state.
 *
 * initial must be zero, since there is currently no means of expressing
 * whether a reader/writer lock should be initially locked as a reader or
 * writer. This would require some encoding to be used.
 *
 * 'Automatic' ordering means that locks must be obtained in the order that
 * they were created. For all locks that can be held at the same time, they must
 * either all provide the order parameter, or they all must use 'automatic'
 * ordering - because there is no way of mixing 'automatic' and 'manual'
 * ordering.
 *
 * @param flags flags combined with bitwise OR ('|'), or zero. There are
 * restrictions on which flags can be combined, @see _mali_osk_lock_flags_t.
 * @param initial For future expansion into semaphores. SBZ.
 * @param order The locking order of the mutex. That is, locks obtained by the
 * same thread must have been created with an increasing order parameter, for
 * deadlock prevention. Setting to zero causes 'automatic' ordering to be used.
 * @return On success, a pointer to a _mali_osk_lock_t object. NULL on failure.
 */
_mali_osk_lock_t *_mali_osk_lock_init( _mali_osk_lock_flags_t flags, u32 initial, u32 order );

/** @brief Wait for a lock to be signalled (obtained)

 * After a thread has successfully waited on the lock, the lock is obtained by
 * the thread, and is marked as unsignalled. The thread releases the lock by
 * signalling it.
 *
 * In the case of Reader/Writer locks, multiple readers can obtain a lock in
 * the absence of writers, which is a performance optimization (providing that
 * the readers never write to the protected resource).
 *
 * To prevent deadlock, locks must always be obtained in the same order.
 *
 * For locks marked as _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, it is a
 * programming error for the function to exit without obtaining the lock. This
 * means that the error code must only be checked for interruptible locks.
 *
 * @param lock the lock to wait upon (obtain).
 * @param mode the mode in which the lock should be obtained. Unless the lock
 * was created with _MALI_OSK_LOCKFLAG_READERWRITER, this must be
 * _MALI_OSK_LOCKMODE_RW.
 * @return On success, _MALI_OSK_ERR_OK. For interruptible locks, a suitable
 * _mali_osk_errcode_t will be returned on failure, and the lock will not be
 * obtained. In this case, the error code must be propagated up to the U/K
 * interface.
 */
_mali_osk_errcode_t _mali_osk_lock_wait( _mali_osk_lock_t *lock, _mali_osk_lock_mode_t mode);


/** @brief Signal (release) a lock
 *
 * Locks may only be signalled by the thread that originally waited upon the
 * lock.
 *
 * @note In the OSU, a flag exists to allow any thread to signal a
 * lock. Such functionality is not present in the OSK.
 *
 * @param lock the lock to signal (release).
 * @param mode the mode in which the lock should be obtained. This must match
 * the mode in which the lock was waited upon.
 */
void _mali_osk_lock_signal( _mali_osk_lock_t *lock, _mali_osk_lock_mode_t mode );

/** @brief Terminate a lock
 *
 * This terminates a lock and frees all associated resources.
 *
 * It is a programming error to terminate the lock when it is held (unsignalled)
 * by a thread.
 *
 * @param lock the lock to terminate.
 */
void _mali_osk_lock_term( _mali_osk_lock_t *lock );
/** @} */ /* end group _mali_osk_lock */


/** @addtogroup _mali_osk_low_level_memory
 * @{ */

/** @brief Issue a memory barrier
 *
 * This defines an arbitrary memory barrier operation, which forces an ordering constraint
 * on memory read and write operations.
 */
void _mali_osk_mem_barrier( void );

/** @brief Issue a write memory barrier
 *
 * This defines an write memory barrier operation which forces an ordering constraint
 * on memory write operations.
 */
void _mali_osk_write_mem_barrier( void );

/** @brief Map a physically contiguous region into kernel space
 *
 * This is primarily used for mapping in registers from resources, and Mali-MMU
 * page tables. The mapping is only visable from kernel-space.
 *
 * Access has to go through _mali_osk_mem_ioread32 and _mali_osk_mem_iowrite32
 *
 * @param phys CPU-physical base address of the memory to map in. This must
 * be aligned to the system's page size, which is assumed to be 4K.
 * @param size the number of bytes of physically contiguous address space to
 * map in
 * @param description A textual description of the memory being mapped in.
 * @return On success, a Mali IO address through which the mapped-in
 * memory/registers can be accessed. NULL on failure.
 */
mali_io_address _mali_osk_mem_mapioregion( u32 phys, u32 size, const char *description );

/** @brief Unmap a physically contiguous address range from kernel space.
 *
 * The address range should be one previously mapped in through
 * _mali_osk_mem_mapioregion.
 *
 * It is a programming error to do (but not limited to) the following:
 * - attempt an unmap twice
 * - unmap only part of a range obtained through _mali_osk_mem_mapioregion
 * - unmap more than the range obtained through  _mali_osk_mem_mapioregion
 * - unmap an address range that was not successfully mapped using
 * _mali_osk_mem_mapioregion
 * - provide a mapping that does not map to phys.
 *
 * @param phys CPU-physical base address of the memory that was originally
 * mapped in. This must be aligned to the system's page size, which is assumed
 * to be 4K
 * @param size The number of bytes that were originally mapped in.
 * @param mapping The Mali IO address through which the mapping is
 * accessed.
 */
void _mali_osk_mem_unmapioregion( u32 phys, u32 size, mali_io_address mapping );

/** @brief Allocate and Map a physically contiguous region into kernel space
 *
 * This is used for allocating physically contiguous regions (such as Mali-MMU
 * page tables) and mapping them into kernel space. The mapping is only
 * visible from kernel-space.
 *
 * The alignment of the returned memory is guaranteed to be at least
 * _MALI_OSK_CPU_PAGE_SIZE.
 *
 * Access must go through _mali_osk_mem_ioread32 and _mali_osk_mem_iowrite32
 *
 * @note This function is primarily to provide support for OSs that are
 * incapable of separating the tasks 'allocate physically contiguous memory'
 * and 'map it into kernel space'
 *
 * @param[out] phys CPU-physical base address of memory that was allocated.
 * (*phys) will be guaranteed to be aligned to at least
 * _MALI_OSK_CPU_PAGE_SIZE on success.
 *
 * @param[in] size the number of bytes of physically contiguous memory to
 * allocate. This must be a multiple of _MALI_OSK_CPU_PAGE_SIZE.
 *
 * @return On success, a Mali IO address through which the mapped-in
 * memory/registers can be accessed. NULL on failure, and (*phys) is unmodified.
 */
mali_io_address _mali_osk_mem_allocioregion( u32 *phys, u32 size );

/** @brief Free a physically contiguous address range from kernel space.
 *
 * The address range should be one previously mapped in through
 * _mali_osk_mem_allocioregion.
 *
 * It is a programming error to do (but not limited to) the following:
 * - attempt a free twice on the same ioregion
 * - free only part of a range obtained through _mali_osk_mem_allocioregion
 * - free more than the range obtained through  _mali_osk_mem_allocioregion
 * - free an address range that was not successfully mapped using
 * _mali_osk_mem_allocioregion
 * - provide a mapping that does not map to phys.
 *
 * @param phys CPU-physical base address of the memory that was originally
 * mapped in, which was aligned to _MALI_OSK_CPU_PAGE_SIZE.
 * @param size The number of bytes that were originally mapped in, which was
 * a multiple of _MALI_OSK_CPU_PAGE_SIZE.
 * @param mapping The Mali IO address through which the mapping is
 * accessed.
 */
void _mali_osk_mem_freeioregion( u32 phys, u32 size, mali_io_address mapping );

/** @brief Request a region of physically contiguous memory
 *
 * This is used to ensure exclusive access to a region of physically contigous
 * memory.
 *
 * It is acceptable to implement this as a stub. However, it is then the job
 * of the System Integrator to ensure that no other device driver will be using
 * the physical address ranges used by Mali, while the Mali device driver is
 * loaded.
 *
 * @param phys CPU-physical base address of the memory to request. This must
 * be aligned to the system's page size, which is assumed to be 4K.
 * @param size the number of bytes of physically contiguous address space to
 * request.
 * @param description A textual description of the memory being requested.
 * @return _MALI_OSK_ERR_OK on success. Otherwise, a suitable
 * _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_osk_mem_reqregion( u32 phys, u32 size, const char *description );

/** @brief Un-request a region of physically contiguous memory
 *
 * This is used to release a regious of physically contiguous memory previously
 * requested through _mali_osk_mem_reqregion, so that other device drivers may
 * use it. This will be called at time of Mali device driver termination.
 *
 * It is a programming error to attempt to:
 * - unrequest a region twice
 * - unrequest only part of a range obtained through _mali_osk_mem_reqregion
 * - unrequest more than the range obtained through  _mali_osk_mem_reqregion
 * - unrequest an address range that was not successfully requested using
 * _mali_osk_mem_reqregion
 *
 * @param phys CPU-physical base address of the memory to un-request. This must
 * be aligned to the system's page size, which is assumed to be 4K
 * @param size the number of bytes of physically contiguous address space to
 * un-request.
 */
void _mali_osk_mem_unreqregion( u32 phys, u32 size );

/** @brief Read from a location currently mapped in through
 * _mali_osk_mem_mapioregion
 *
 * This reads a 32-bit word from a 32-bit aligned location. It is a programming
 * error to provide unaligned locations, or to read from memory that is not
 * mapped in, or not mapped through either _mali_osk_mem_mapioregion() or
 * _mali_osk_mem_allocioregion().
 *
 * @param mapping Mali IO address to read from
 * @param offset Byte offset from the given IO address to operate on, must be a multiple of 4
 * @return the 32-bit word from the specified location.
 */
u32 _mali_osk_mem_ioread32( volatile mali_io_address mapping, u32 offset );

/** @brief Write to a location currently mapped in through
 * _mali_osk_mem_mapioregion without memory barriers
 *
 * This write a 32-bit word to a 32-bit aligned location without using memory barrier.
 * It is a programming error to provide unaligned locations, or to write to memory that is not
 * mapped in, or not mapped through either _mali_osk_mem_mapioregion() or
 * _mali_osk_mem_allocioregion().
 *
 * @param mapping Mali IO address to write to
 * @param offset Byte offset from the given IO address to operate on, must be a multiple of 4
 * @param val the 32-bit word to write.
 */
void _mali_osk_mem_iowrite32_relaxed( volatile mali_io_address addr, u32 offset, u32 val );

/** @brief Write to a location currently mapped in through
 * _mali_osk_mem_mapioregion with write memory barrier
 *
 * This write a 32-bit word to a 32-bit aligned location. It is a programming
 * error to provide unaligned locations, or to write to memory that is not
 * mapped in, or not mapped through either _mali_osk_mem_mapioregion() or
 * _mali_osk_mem_allocioregion().
 *
 * @param mapping Mali IO address to write to
 * @param offset Byte offset from the given IO address to operate on, must be a multiple of 4
 * @param val the 32-bit word to write.
 */
void _mali_osk_mem_iowrite32( volatile mali_io_address mapping, u32 offset, u32 val );

/** @brief Flush all CPU caches
 *
 * This should only be implemented if flushing of the cache is required for
 * memory mapped in through _mali_osk_mem_mapregion.
 */
void _mali_osk_cache_flushall( void );

/** @brief Flush any caches necessary for the CPU and MALI to have the same view of a range of uncached mapped memory
 *
 * This should only be implemented if your OS doesn't do a full cache flush (inner & outer)
 * after allocating uncached mapped memory.
 *
 * Some OS do not perform a full cache flush (including all outer caches) for uncached mapped memory.
 * They zero the memory through a cached mapping, then flush the inner caches but not the outer caches.
 * This is required for MALI to have the correct view of the memory.
 */
void _mali_osk_cache_ensure_uncached_range_flushed( void *uncached_mapping, u32 offset, u32 size );

/** @} */ /* end group _mali_osk_low_level_memory */


/** @addtogroup _mali_osk_notification
 *
 * User space notification framework
 *
 * Communication with user space of asynchronous events is performed through a
 * synchronous call to the \ref u_k_api.
 *
 * Since the events are asynchronous, the events have to be queued until a
 * synchronous U/K API call can be made by user-space. A U/K API call might also
 * be received before any event has happened. Therefore the notifications the
 * different subsystems wants to send to user space has to be queued for later
 * reception, or a U/K API call has to be blocked until an event has occured.
 *
 * Typical uses of notifications are after running of jobs on the hardware or
 * when changes to the system is detected that needs to be relayed to user
 * space.
 *
 * After an event has occured user space has to be notified using some kind of
 * message. The notification framework supports sending messages to waiting
 * threads or queueing of messages until a U/K API call is made.
 *
 * The notification queue is a FIFO. There are no restrictions on the numbers
 * of readers or writers in the queue.
 *
 * A message contains what user space needs to identifiy how to handle an
 * event. This includes a type field and a possible type specific payload.
 *
 * A notification to user space is represented by a
 * \ref _mali_osk_notification_t object. A sender gets hold of such an object
 * using _mali_osk_notification_create(). The buffer given by the
 * _mali_osk_notification_t::result_buffer field in the object is used to store
 * any type specific data. The other fields are internal to the queue system
 * and should not be touched.
 *
 * @{ */

/** @brief Create a notification object
 *
 * Returns a notification object which can be added to the queue of
 * notifications pending for user space transfer.
 *
 * The implementation will initialize all members of the
 * \ref _mali_osk_notification_t object. In particular, the
 * _mali_osk_notification_t::result_buffer member will be initialized to point
 * to \a size bytes of storage, and that storage will be suitably aligned for
 * storage of any structure. That is, the created buffer meets the same
 * requirements as _mali_osk_malloc().
 *
 * The notification object must be deleted when not in use. Use
 * _mali_osk_notification_delete() for deleting it.
 *
 * @note You \b must \b not call _mali_osk_free() on a \ref _mali_osk_notification_t,
 * object, or on a _mali_osk_notification_t::result_buffer. You must only use
 * _mali_osk_notification_delete() to free the resources assocaited with a
 * \ref _mali_osk_notification_t object.
 *
 * @param type The notification type
 * @param size The size of the type specific buffer to send
 * @return Pointer to a notification object with a suitable buffer, or NULL on error.
 */
_mali_osk_notification_t *_mali_osk_notification_create( u32 type, u32 size );

/** @brief Delete a notification object
 *
 * This must be called to reclaim the resources of a notification object. This
 * includes:
 * - The _mali_osk_notification_t::result_buffer
 * - The \ref _mali_osk_notification_t itself.
 *
 * A notification object \b must \b not be used after it has been deleted by
 * _mali_osk_notification_delete().
 *
 * In addition, the notification object may not be deleted while it is in a
 * queue. That is, if it has been placed on a queue with
 * _mali_osk_notification_queue_send(), then it must not be deleted until
 * it has been received by a call to _mali_osk_notification_queue_receive().
 * Otherwise, the queue may be corrupted.
 *
 * @param object the notification object to delete.
 */
void _mali_osk_notification_delete( _mali_osk_notification_t *object );

/** @brief Create a notification queue
 *
 * Creates a notification queue which can be used to queue messages for user
 * delivery and get queued messages from
 *
 * The queue is a FIFO, and has no restrictions on the numbers of readers or
 * writers.
 *
 * When the queue is no longer in use, it must be terminated with
 * \ref _mali_osk_notification_queue_term(). Failure to do so will result in a
 * memory leak.
 *
 * @return Pointer to a new notification queue or NULL on error.
 */
_mali_osk_notification_queue_t *_mali_osk_notification_queue_init( void );

/** @brief Destroy a notification queue
 *
 * Destroys a notification queue and frees associated resources from the queue.
 *
 * A notification queue \b must \b not be destroyed in the following cases:
 * - while there are \ref _mali_osk_notification_t objects in the queue.
 * - while there are writers currently acting upon the queue. That is, while
 * a thread is currently calling \ref _mali_osk_notification_queue_send() on
 * the queue, or while a thread may call
 * \ref _mali_osk_notification_queue_send() on the queue in the future.
 * - while there are readers currently waiting upon the queue. That is, while
 * a thread is currently calling \ref _mali_osk_notification_queue_receive() on
 * the queue, or while a thread may call
 * \ref _mali_osk_notification_queue_receive() on the queue in the future.
 *
 * Therefore, all \ref _mali_osk_notification_t objects must be flushed and
 * deleted by the code that makes use of the notification queues, since only
 * they know the structure of the _mali_osk_notification_t::result_buffer
 * (even if it may only be a flat sturcture).
 *
 * @note Since the queue is a FIFO, the code using notification queues may
 * create its own 'flush' type of notification, to assist in flushing the
 * queue.
 *
 * Once the queue has been destroyed, it must not be used again.
 *
 * @param queue The queue to destroy
 */
void _mali_osk_notification_queue_term( _mali_osk_notification_queue_t *queue );

/** @brief Schedule notification for delivery
 *
 * When a \ref _mali_osk_notification_t object has been created successfully
 * and set up, it may be added to the queue of objects waiting for user space
 * transfer.
 *
 * The sending will not block if the queue is full.
 *
 * A \ref _mali_osk_notification_t object \b must \b not be put on two different
 * queues at the same time, or enqueued twice onto a single queue before
 * reception. However, it is acceptable for it to be requeued \em after reception
 * from a call to _mali_osk_notification_queue_receive(), even onto the same queue.
 *
 * Again, requeuing must also not enqueue onto two different queues at the same
 * time, or enqueue onto the same queue twice before reception.
 *
 * @param queue The notification queue to add this notification to
 * @param object The entry to add
 */
void _mali_osk_notification_queue_send( _mali_osk_notification_queue_t *queue, _mali_osk_notification_t *object );

#if MALI_STATE_TRACKING
/** @brief Receive a notification from a queue
 *
 * Check if a notification queue is empty.
 *
 * @param queue The queue to check.
 * @return MALI_TRUE if queue is empty, otherwise MALI_FALSE.
 */
mali_bool _mali_osk_notification_queue_is_empty( _mali_osk_notification_queue_t *queue );
#endif

/** @brief Receive a notification from a queue
 *
 * Receives a single notification from the given queue.
 *
 * If no notifciations are ready the thread will sleep until one becomes ready.
 * Therefore, notifications may not be received into an
 * IRQ or 'atomic' context (that is, a context where sleeping is disallowed).
 *
 * @param queue The queue to receive from
 * @param result Pointer to storage of a pointer of type
 * \ref _mali_osk_notification_t*. \a result will be written to such that the
 * expression \a (*result) will evaluate to a pointer to a valid
 * \ref _mali_osk_notification_t object, or NULL if none were received.
 * @return _MALI_OSK_ERR_OK on success. _MALI_OSK_ERR_RESTARTSYSCALL if the sleep was interrupted.
 */
_mali_osk_errcode_t _mali_osk_notification_queue_receive( _mali_osk_notification_queue_t *queue, _mali_osk_notification_t **result );

/** @brief Dequeues a notification from a queue
 *
 * Receives a single notification from the given queue.
 *
 * If no notifciations are ready the function call will return an error code.
 *
 * @param queue The queue to receive from
 * @param result Pointer to storage of a pointer of type
 * \ref _mali_osk_notification_t*. \a result will be written to such that the
 * expression \a (*result) will evaluate to a pointer to a valid
 * \ref _mali_osk_notification_t object, or NULL if none were received.
 * @return _MALI_OSK_ERR_OK on success, _MALI_OSK_ERR_ITEM_NOT_FOUND if queue was empty.
 */
_mali_osk_errcode_t _mali_osk_notification_queue_dequeue( _mali_osk_notification_queue_t *queue, _mali_osk_notification_t **result );

/** @} */ /* end group _mali_osk_notification */


/** @addtogroup _mali_osk_timer
 *
 * Timers use the OS's representation of time, which are 'ticks'. This is to
 * prevent aliasing problems between the internal timer time, and the time
 * asked for.
 *
 * @{ */

/** @brief Initialize a timer
 *
 * Allocates resources for a new timer, and initializes them. This does not
 * start the timer.
 *
 * @return a pointer to the allocated timer object, or NULL on failure.
 */
_mali_osk_timer_t *_mali_osk_timer_init(void);

/** @brief Start a timer
 *
 * It is an error to start a timer without setting the callback via
 * _mali_osk_timer_setcallback().
 *
 * It is an error to use this to start an already started timer.
 *
 * The timer will expire in \a ticks_to_expire ticks, at which point, the
 * callback function will be invoked with the callback-specific data,
 * as registered by _mali_osk_timer_setcallback().
 *
 * @param tim the timer to start
 * @param ticks_to_expire the amount of time in ticks for the timer to run
 * before triggering.
 */
void _mali_osk_timer_add( _mali_osk_timer_t *tim, u32 ticks_to_expire );

/** @brief Modify a timer
 *
 * Set the absolute time at which a timer will expire, and start it if it is
 * stopped. If \a expiry_tick is in the past (determined by
 * _mali_osk_time_after() ), the timer fires immediately.
 *
 * It is an error to modify a timer without setting the callback via
 *  _mali_osk_timer_setcallback().
 *
 * The timer will expire at absolute time \a expiry_tick, at which point, the
 * callback function will be invoked with the callback-specific data, as set
 * by _mali_osk_timer_setcallback().
 *
 * @param tim the timer to modify, and start if necessary
 * @param expiry_tick the \em absolute time in ticks at which this timer should
 * trigger.
 *
 */
void _mali_osk_timer_mod( _mali_osk_timer_t *tim, u32 expiry_tick);

/** @brief Stop a timer, and block on its completion.
 *
 * Stop the timer. When the function returns, it is guaranteed that the timer's
 * callback will not be running on any CPU core.
 *
 * Since stoping the timer blocks on compeletion of the callback, the callback
 * may not obtain any mutexes that the caller holds. Otherwise, a deadlock will
 * occur.
 *
 * @note While the callback itself is guaranteed to not be running, work
 * enqueued on the IRQ work-queue by the timer (with
 * \ref _mali_osk_irq_schedulework()) may still run. The timer callback and IRQ
 * bottom-half handler must take this into account.
 *
 * It is legal to stop an already stopped timer.
 *
 * @param tim the timer to stop.
 *
 */
void _mali_osk_timer_del( _mali_osk_timer_t *tim );

/** @brief Set a timer's callback parameters.
 *
 * This must be called at least once before a timer is started/modified.
 *
 * After a timer has been stopped or expires, the callback remains set. This
 * means that restarting the timer will call the same function with the same
 * parameters on expiry.
 *
 * @param tim the timer to set callback on.
 * @param callback Function to call when timer expires
 * @param data Function-specific data to supply to the function on expiry.
 */
void _mali_osk_timer_setcallback( _mali_osk_timer_t *tim, _mali_osk_timer_callback_t callback, void *data );

/** @brief Terminate a timer, and deallocate resources.
 *
 * The timer must first be stopped by calling _mali_osk_timer_del().
 *
 * It is a programming error for _mali_osk_timer_term() to be called on:
 * - timer that is currently running
 * - a timer that is currently executing its callback.
 *
 * @param tim the timer to deallocate.
 */
void _mali_osk_timer_term( _mali_osk_timer_t *tim );
/** @} */ /* end group _mali_osk_timer */


/** @defgroup _mali_osk_time OSK Time functions
 *
 * \ref _mali_osk_time use the OS's representation of time, which are
 * 'ticks'. This is to prevent aliasing problems between the internal timer
 * time, and the time asked for.
 *
 * OS tick time is measured as a u32. The time stored in a u32 may either be
 * an absolute time, or a time delta between two events. Whilst it is valid to
 * use math opeartors to \em change the tick value represented as a u32, it
 * is often only meaningful to do such operations on time deltas, rather than
 * on absolute time. However, it is meaningful to add/subtract time deltas to
 * absolute times.
 *
 * Conversion between tick time and milliseconds (ms) may not be loss-less,
 * and are \em implementation \em depenedant.
 *
 * Code use OS time must take this into account, since:
 * - a small OS time may (or may not) be rounded
 * - a large time may (or may not) overflow
 *
 * @{ */

/** @brief Return whether ticka occurs after tickb
 *
 * Some OSs handle tick 'rollover' specially, and so can be more robust against
 * tick counters rolling-over. This function must therefore be called to
 * determine if a time (in ticks) really occurs after another time (in ticks).
 *
 * @param ticka ticka
 * @param tickb tickb
 * @return non-zero if ticka represents a time that occurs after tickb.
 * Zero otherwise.
 */
int	_mali_osk_time_after( u32 ticka, u32 tickb );

/** @brief Convert milliseconds to OS 'ticks'
 *
 * @param ms time interval in milliseconds
 * @return the corresponding time interval in OS ticks.
 */
u32	_mali_osk_time_mstoticks( u32 ms );

/** @brief Convert OS 'ticks' to milliseconds
 *
 * @param ticks time interval in OS ticks.
 * @return the corresponding time interval in milliseconds
 */
u32	_mali_osk_time_tickstoms( u32 ticks );


/** @brief Get the current time in OS 'ticks'.
 * @return the current time in OS 'ticks'.
 */
u32	_mali_osk_time_tickcount( void );

/** @brief Cause a microsecond delay
 *
 * The delay will have microsecond resolution, and is necessary for correct
 * operation of the driver. At worst, the delay will be \b at least \a usecs
 * microseconds, and so may be (significantly) more.
 *
 * This function may be implemented as a busy-wait, which is the most sensible
 * implementation. On OSs where there are situations in which a thread must not
 * sleep, this is definitely implemented as a busy-wait.
 *
 * @param usecs the number of microseconds to wait for.
 */
void _mali_osk_time_ubusydelay( u32 usecs );

/** @brief Return time in nano seconds, since any given reference.
 *
 * @return Time in nano seconds
 */
u64 _mali_osk_time_get_ns( void );


/** @} */ /* end group _mali_osk_time */

/** @defgroup _mali_osk_math OSK Math
 * @{ */

/** @brief Count Leading Zeros (Little-endian)
 *
 * @note This function must be implemented to support the reference
 * implementation of _mali_osk_find_first_zero_bit, as defined in
 * mali_osk_bitops.h.
 *
 * @param val 32-bit words to count leading zeros on
 * @return the number of leading zeros.
 */
u32 _mali_osk_clz( u32 val );
/** @} */ /* end group _mali_osk_math */

/** @defgroup _mali_osk_wait_queue OSK Wait Queue functionality
 * @{ */
/** @brief Private type for wait queue objects */
typedef struct _mali_osk_wait_queue_t_struct _mali_osk_wait_queue_t;

/** @brief Initialize an empty Wait Queue */
_mali_osk_wait_queue_t* _mali_osk_wait_queue_init( void );

/** @brief Sleep  if condition is false
 *
 * @param queue the queue to use
 * @param condition function pointer to a boolean function
 *
 * Put thread to sleep if the given \a codition function returns false. When
 * being asked to wake up again, the condition will be re-checked and the
 * thread only woken up if the condition is now true.
 */
void _mali_osk_wait_queue_wait_event( _mali_osk_wait_queue_t *queue, mali_bool (*condition)(void) );

/** @brief Wake up all threads in wait queue if their respective conditions are
 * true
 *
 * @param queue the queue whose threads should be woken up
 *
 * Wake up all threads in wait queue \a queue whose condition is now true.
 */
void _mali_osk_wait_queue_wake_up( _mali_osk_wait_queue_t *queue );

/** @brief terminate a wait queue
 *
 * @param queue the queue to terminate.
 */
void _mali_osk_wait_queue_term( _mali_osk_wait_queue_t *queue );
/** @} */ /* end group _mali_osk_wait_queue */


/** @addtogroup _mali_osk_miscellaneous
 * @{ */

/** @brief Output a device driver debug message.
 *
 * The interpretation of \a fmt is the same as the \c format parameter in
 * _mali_osu_vsnprintf().
 *
 * @param fmt a _mali_osu_vsnprintf() style format string
 * @param ... a variable-number of parameters suitable for \a fmt
 */
void _mali_osk_dbgmsg( const char *fmt, ... );

/** @brief Print fmt into buf.
 *
 * The interpretation of \a fmt is the same as the \c format parameter in
 * _mali_osu_vsnprintf().
 *
 * @param buf a pointer to the result buffer
 * @param size the total number of bytes allowed to write to \a buf
 * @param fmt a _mali_osu_vsnprintf() style format string
 * @param ... a variable-number of parameters suitable for \a fmt
 * @return The number of bytes written to \a buf
 */
u32 _mali_osk_snprintf( char *buf, u32 size, const char *fmt, ... );

/** @brief Abnormal process abort.
 *
 * Terminates the caller-process if this function is called.
 *
 * This function will be called from Debug assert-macros in mali_kernel_common.h.
 *
 * This function will never return - because to continue from a Debug assert
 * could cause even more problems, and hinder debugging of the initial problem.
 *
 * This function is only used in Debug builds, and is not used in Release builds.
 */
void _mali_osk_abort(void);

/** @brief Sets breakpoint at point where function is called.
 *
 * This function will be called from Debug assert-macros in mali_kernel_common.h,
 * to assist in debugging. If debugging at this level is not required, then this
 * function may be implemented as a stub.
 *
 * This function is only used in Debug builds, and is not used in Release builds.
 */
void _mali_osk_break(void);

/** @brief Return an identificator for calling process.
 *
 * @return Identificator for calling process.
 */
u32 _mali_osk_get_pid(void);

/** @brief Return an identificator for calling thread.
 *
 * @return Identificator for calling thread.
 */
u32 _mali_osk_get_tid(void);

/** @brief Enable OS controlled runtime power management
 */
void _mali_osk_pm_dev_enable(void);

/** @brief Tells the OS that device is now idle
 */
_mali_osk_errcode_t _mali_osk_pm_dev_idle(void);

/** @brief Tells the OS that the device is about to become active
 */
_mali_osk_errcode_t _mali_osk_pm_dev_activate(void);

/** @} */ /* end group  _mali_osk_miscellaneous */

/** @} */ /* end group osuapi */

/** @} */ /* end group uddapi */


#ifdef __cplusplus
}
#endif

#include "mali_osk_specific.h"           /* include any per-os specifics */

/* Check standard inlines */
#ifndef MALI_STATIC_INLINE
	#error MALI_STATIC_INLINE not defined on your OS
#endif

#ifndef MALI_NON_STATIC_INLINE
	#error MALI_NON_STATIC_INLINE not defined on your OS
#endif

#endif /* __MALI_OSK_H__ */
