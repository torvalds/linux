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
 * @file mali_osk.h
 * Defines the OS abstraction layer for the kernel device driver (OSK)
 */

#ifndef __MALI_OSK_H__
#define __MALI_OSK_H__

#include <linux/seq_file.h>
#include "mali_osk_types.h"
#include "mali_osk_specific.h"           /* include any per-os specifics */
#include "mali_osk_locks.h"

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

/** @addtogroup _mali_osk_lock OSK Mutual Exclusion Locks
 * @{ */

#ifdef DEBUG
/** @brief Macro for asserting that the current thread holds a given lock
 */
#define MALI_DEBUG_ASSERT_LOCK_HELD(l) MALI_DEBUG_ASSERT(_mali_osk_lock_get_owner((_mali_osk_lock_debug_t *)l) == _mali_osk_get_tid());

/** @brief returns a lock's owner (thread id) if debugging is enabled
 */
#else
#define MALI_DEBUG_ASSERT_LOCK_HELD(l) do {} while(0)
#endif

#define _mali_osk_ctxprintf     seq_printf

/** @} */ /* end group _mali_osk_lock */

/** @addtogroup _mali_osk_miscellaneous
 * @{ */

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

/** @addtogroup _mali_osk_wq
 * @{ */

/** @brief Initialize work queues (for deferred work)
 *
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_wq_init(void);

/** @brief Terminate work queues (for deferred work)
 */
void _mali_osk_wq_term(void);

/** @brief Create work in the work queue
 *
 * Creates a work object which can be scheduled in the work queue. When
 * scheduled, \a handler will be called with \a data as the argument.
 *
 * Refer to \ref _mali_osk_wq_schedule_work() for details on how work
 * is scheduled in the queue.
 *
 * The returned pointer must be freed with \ref _mali_osk_wq_delete_work()
 * when no longer needed.
 */
_mali_osk_wq_work_t *_mali_osk_wq_create_work(_mali_osk_wq_work_handler_t handler, void *data);

/** @brief A high priority version of \a _mali_osk_wq_create_work()
 *
 * Creates a work object which can be scheduled in the high priority work queue.
 *
 * This is unfortunately needed to get low latency scheduling of the Mali cores.  Normally we would
 * schedule the next job in hw_irq or tasklet, but often we can't since we need to synchronously map
 * and unmap shared memory when a job is connected to external fences (timelines). And this requires
 * taking a mutex.
 *
 * We do signal a lot of other (low priority) work also as part of the job being finished, and if we
 * don't set this Mali scheduling thread as high priority, we see that the CPU scheduler often runs
 * random things instead of starting the next GPU job when the GPU is idle.  So setting the gpu
 * scheduler to high priority does give a visually more responsive system.
 *
 * Start the high priority work with: \a _mali_osk_wq_schedule_work_high_pri()
 */
_mali_osk_wq_work_t *_mali_osk_wq_create_work_high_pri(_mali_osk_wq_work_handler_t handler, void *data);

/** @brief Delete a work object
 *
 * This will flush the work queue to ensure that the work handler will not
 * be called after deletion.
 */
void _mali_osk_wq_delete_work(_mali_osk_wq_work_t *work);

/** @brief Delete a work object
 *
 * This will NOT flush the work queue, so only call this if you are sure that the work handler will
 * not be called after deletion.
 */
void _mali_osk_wq_delete_work_nonflush(_mali_osk_wq_work_t *work);

/** @brief Cause a queued, deferred call of the work handler
 *
 * _mali_osk_wq_schedule_work provides a mechanism for enqueuing deferred calls
 * to the work handler. After calling \ref _mali_osk_wq_schedule_work(), the
 * work handler will be scheduled to run at some point in the future.
 *
 * Typically this is called by the IRQ upper-half to defer further processing of
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
 * _mali_osk_wq_schedule_work() places deferred call requests on a queue, to
 * allow for more than one thread to make a deferred call. Therfore, if it is
 * called 'K' times, then the IRQ bottom-half will be scheduled 'K' times too.
 * 'K' is a number that is implementation-specific.
 *
 * _mali_osk_wq_schedule_work() is guaranteed to not block on:
 * - enqueuing a deferred call request.
 * - the completion of the work handler.
 *
 * This is to prevent deadlock. For example, if _mali_osk_wq_schedule_work()
 * blocked, then it would cause a deadlock when the following two conditions
 * hold:
 * - The work handler callback (of type _mali_osk_wq_work_handler_t) locks
 * a mutex
 * - And, at the same time, the caller of _mali_osk_wq_schedule_work() also
 * holds the same mutex
 *
 * @note care must be taken to not overflow the queue that
 * _mali_osk_wq_schedule_work() operates on. Code must be structured to
 * ensure that the number of requests made to the queue is bounded. Otherwise,
 * work will be lost.
 *
 * The queue that _mali_osk_wq_schedule_work implements is a FIFO of N-writer,
 * 1-reader type. The writers are the callers of _mali_osk_wq_schedule_work
 * (all OSK-registered IRQ upper-half handlers in the system, watchdog timers,
 * callers from a Kernel-process context). The reader is a single thread that
 * handles all OSK-registered work.
 *
 * @param work a pointer to the _mali_osk_wq_work_t object corresponding to the
 * work to begin processing.
 */
void _mali_osk_wq_schedule_work(_mali_osk_wq_work_t *work);

/** @brief Cause a queued, deferred call of the high priority work handler
 *
 * Function is the same as \a _mali_osk_wq_schedule_work() with the only
 * difference that it runs in a high (real time) priority on the system.
 *
 * Should only be used as a substitue for doing the same work in interrupts.
 *
 * This is allowed to sleep, but the work should be small since it will block
 * all other applications.
*/
void _mali_osk_wq_schedule_work_high_pri(_mali_osk_wq_work_t *work);

/** @brief Flush the work queue
 *
 * This will flush the OSK work queue, ensuring all work in the queue has
 * completed before returning.
 *
 * Since this blocks on the completion of work in the work-queue, the
 * caller of this function \b must \b not hold any mutexes that are taken by
 * any registered work handler. To do so may cause a deadlock.
 *
 */
void _mali_osk_wq_flush(void);

/** @brief Create work in the delayed work queue
 *
 * Creates a work object which can be scheduled in the work queue. When
 * scheduled, a timer will be start and the \a handler will be called with
 * \a data as the argument when timer out
 *
 * Refer to \ref _mali_osk_wq_delayed_schedule_work() for details on how work
 * is scheduled in the queue.
 *
 * The returned pointer must be freed with \ref _mali_osk_wq_delayed_delete_work_nonflush()
 * when no longer needed.
 */
_mali_osk_wq_delayed_work_t *_mali_osk_wq_delayed_create_work(_mali_osk_wq_work_handler_t handler, void *data);

/** @brief Delete a work object
 *
 * This will NOT flush the work queue, so only call this if you are sure that the work handler will
 * not be called after deletion.
 */
void _mali_osk_wq_delayed_delete_work_nonflush(_mali_osk_wq_delayed_work_t *work);

/** @brief Cancel a delayed work without waiting for it to finish
 *
 * Note that the \a work callback function may still be running on return from
 * _mali_osk_wq_delayed_cancel_work_async().
 *
 * @param work The delayed work to be cancelled
 */
void _mali_osk_wq_delayed_cancel_work_async(_mali_osk_wq_delayed_work_t *work);

/** @brief Cancel a delayed work and wait for it to finish
 *
 * When this function returns, the \a work was either cancelled or it finished running.
 *
 * @param work The delayed work to be cancelled
 */
void _mali_osk_wq_delayed_cancel_work_sync(_mali_osk_wq_delayed_work_t *work);

/** @brief Put \a work task in global workqueue after delay
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue.
 *
 * If \a work was already on a queue, this function will return without doing anything
 *
 * @param work job to be done
 * @param delay number of jiffies to wait or 0 for immediate execution
 */
void _mali_osk_wq_delayed_schedule_work(_mali_osk_wq_delayed_work_t *work, u32 delay);

/** @} */ /* end group _mali_osk_wq */


/** @addtogroup _mali_osk_irq
 * @{ */

/** @brief Initialize IRQ handling for a resource
 *
 * Registers an interrupt handler \a uhandler for the given IRQ number \a irqnum.
 * \a data will be passed as argument to the handler when an interrupt occurs.
 *
 * If \a irqnum is -1, _mali_osk_irq_init will probe for the IRQ number using
 * the supplied \a trigger_func and \a ack_func. These functions will also
 * receive \a data as their argument.
 *
 * @param irqnum The IRQ number that the resource uses, as seen by the CPU.
 * The value -1 has a special meaning which indicates the use of probing, and
 * trigger_func and ack_func must be non-NULL.
 * @param uhandler The interrupt handler, corresponding to a ISR handler for
 * the resource
 * @param int_data resource specific data, which will be passed to uhandler
 * @param trigger_func Optional: a function to trigger the resource's irq, to
 * probe for the interrupt. Use NULL if irqnum != -1.
 * @param ack_func Optional: a function to acknowledge the resource's irq, to
 * probe for the interrupt. Use NULL if irqnum != -1.
 * @param probe_data resource-specific data, which will be passed to
 * (if present) trigger_func and ack_func
 * @param description textual description of the IRQ resource.
 * @return on success, a pointer to a _mali_osk_irq_t object, which represents
 * the IRQ handling on this resource. NULL on failure.
 */
_mali_osk_irq_t *_mali_osk_irq_init(u32 irqnum, _mali_osk_irq_uhandler_t uhandler, void *int_data, _mali_osk_irq_trigger_t trigger_func, _mali_osk_irq_ack_t ack_func, void *probe_data, const char *description);

/** @brief Terminate IRQ handling on a resource.
 *
 * This will disable the interrupt from the device, and then waits for any
 * currently executing IRQ handlers to complete.
 *
 * @note If work is deferred to an IRQ bottom-half handler through
 * \ref _mali_osk_wq_schedule_work(), be sure to flush any remaining work
 * with \ref _mali_osk_wq_flush() or (implicitly) with \ref _mali_osk_wq_delete_work()
 *
 * @param irq a pointer to the _mali_osk_irq_t object corresponding to the
 * resource whose IRQ handling is to be terminated.
 */
void _mali_osk_irq_term(_mali_osk_irq_t *irq);

/** @} */ /* end group _mali_osk_irq */


/** @addtogroup _mali_osk_atomic
 * @{ */

/** @brief Decrement an atomic counter
 *
 * @note It is an error to decrement the counter beyond -(1<<23)
 *
 * @param atom pointer to an atomic counter */
void _mali_osk_atomic_dec(_mali_osk_atomic_t *atom);

/** @brief Decrement an atomic counter, return new value
 *
 * @param atom pointer to an atomic counter
 * @return The new value, after decrement */
u32 _mali_osk_atomic_dec_return(_mali_osk_atomic_t *atom);

/** @brief Increment an atomic counter
 *
 * @note It is an error to increment the counter beyond (1<<23)-1
 *
 * @param atom pointer to an atomic counter */
void _mali_osk_atomic_inc(_mali_osk_atomic_t *atom);

/** @brief Increment an atomic counter, return new value
 *
 * @param atom pointer to an atomic counter */
u32 _mali_osk_atomic_inc_return(_mali_osk_atomic_t *atom);

/** @brief Initialize an atomic counter
 *
 * @note the parameter required is a u32, and so signed integers should be
 * cast to u32.
 *
 * @param atom pointer to an atomic counter
 * @param val the value to initialize the atomic counter.
 */
void _mali_osk_atomic_init(_mali_osk_atomic_t *atom, u32 val);

/** @brief Read a value from an atomic counter
 *
 * This can only be safely used to determine the value of the counter when it
 * is guaranteed that other threads will not be modifying the counter. This
 * makes its usefulness limited.
 *
 * @param atom pointer to an atomic counter
 */
u32 _mali_osk_atomic_read(_mali_osk_atomic_t *atom);

/** @brief Terminate an atomic counter
 *
 * @param atom pointer to an atomic counter
 */
void _mali_osk_atomic_term(_mali_osk_atomic_t *atom);

/** @brief Assign a new val to atomic counter, and return the old atomic counter
 *
 * @param atom pointer to an atomic counter
 * @param val the new value assign to the atomic counter
 * @return the old value of the atomic counter
 */
u32 _mali_osk_atomic_xchg(_mali_osk_atomic_t *atom, u32 val);
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
void *_mali_osk_calloc(u32 n, u32 size);

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
void *_mali_osk_malloc(u32 size);

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
void _mali_osk_free(void *ptr);

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
void *_mali_osk_valloc(u32 size);

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
void _mali_osk_vfree(void *ptr);

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
void *_mali_osk_memcpy(void *dst, const void *src, u32 len);

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
void *_mali_osk_memset(void *s, u32 c, u32 n);
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
mali_bool _mali_osk_mem_check_allocated(u32 max_allocated);


/** @addtogroup _mali_osk_low_level_memory
 * @{ */

/** @brief Issue a memory barrier
 *
 * This defines an arbitrary memory barrier operation, which forces an ordering constraint
 * on memory read and write operations.
 */
void _mali_osk_mem_barrier(void);

/** @brief Issue a write memory barrier
 *
 * This defines an write memory barrier operation which forces an ordering constraint
 * on memory write operations.
 */
void _mali_osk_write_mem_barrier(void);

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
mali_io_address _mali_osk_mem_mapioregion(uintptr_t phys, u32 size, const char *description);

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
void _mali_osk_mem_unmapioregion(uintptr_t phys, u32 size, mali_io_address mapping);

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
mali_io_address _mali_osk_mem_allocioregion(u32 *phys, u32 size);

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
void _mali_osk_mem_freeioregion(u32 phys, u32 size, mali_io_address mapping);

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
_mali_osk_errcode_t _mali_osk_mem_reqregion(uintptr_t phys, u32 size, const char *description);

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
void _mali_osk_mem_unreqregion(uintptr_t phys, u32 size);

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
u32 _mali_osk_mem_ioread32(volatile mali_io_address mapping, u32 offset);

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
void _mali_osk_mem_iowrite32_relaxed(volatile mali_io_address addr, u32 offset, u32 val);

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
void _mali_osk_mem_iowrite32(volatile mali_io_address mapping, u32 offset, u32 val);

/** @brief Flush all CPU caches
 *
 * This should only be implemented if flushing of the cache is required for
 * memory mapped in through _mali_osk_mem_mapregion.
 */
void _mali_osk_cache_flushall(void);

/** @brief Flush any caches necessary for the CPU and MALI to have the same view of a range of uncached mapped memory
 *
 * This should only be implemented if your OS doesn't do a full cache flush (inner & outer)
 * after allocating uncached mapped memory.
 *
 * Some OS do not perform a full cache flush (including all outer caches) for uncached mapped memory.
 * They zero the memory through a cached mapping, then flush the inner caches but not the outer caches.
 * This is required for MALI to have the correct view of the memory.
 */
void _mali_osk_cache_ensure_uncached_range_flushed(void *uncached_mapping, u32 offset, u32 size);

/** @brief Safely copy as much data as possible from src to dest
 *
 * Do not crash if src or dest isn't available.
 *
 * @param dest Destination buffer (limited to user space mapped Mali memory)
 * @param src Source buffer
 * @param size Number of bytes to copy
 * @return Number of bytes actually copied
 */
u32 _mali_osk_mem_write_safe(void *dest, const void *src, u32 size);

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
_mali_osk_notification_t *_mali_osk_notification_create(u32 type, u32 size);

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
void _mali_osk_notification_delete(_mali_osk_notification_t *object);

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
_mali_osk_notification_queue_t *_mali_osk_notification_queue_init(void);

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
void _mali_osk_notification_queue_term(_mali_osk_notification_queue_t *queue);

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
void _mali_osk_notification_queue_send(_mali_osk_notification_queue_t *queue, _mali_osk_notification_t *object);

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
_mali_osk_errcode_t _mali_osk_notification_queue_receive(_mali_osk_notification_queue_t *queue, _mali_osk_notification_t **result);

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
_mali_osk_errcode_t _mali_osk_notification_queue_dequeue(_mali_osk_notification_queue_t *queue, _mali_osk_notification_t **result);

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
void _mali_osk_timer_add(_mali_osk_timer_t *tim, unsigned long ticks_to_expire);

/** @brief Modify a timer
 *
 * Set the relative time at which a timer will expire, and start it if it is
 * stopped. If \a ticks_to_expire 0 the timer fires immediately.
 *
 * It is an error to modify a timer without setting the callback via
 *  _mali_osk_timer_setcallback().
 *
 * The timer will expire at \a ticks_to_expire from the time of the call, at
 * which point, the callback function will be invoked with the
 * callback-specific data, as set by _mali_osk_timer_setcallback().
 *
 * @param tim the timer to modify, and start if necessary
 * @param ticks_to_expire the \em absolute time in ticks at which this timer
 * should trigger.
 *
 */
void _mali_osk_timer_mod(_mali_osk_timer_t *tim, unsigned long ticks_to_expire);

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
 * enqueued on the work-queue by the timer (with
 * \ref _mali_osk_wq_schedule_work()) may still run. The timer callback and
 * work handler must take this into account.
 *
 * It is legal to stop an already stopped timer.
 *
 * @param tim the timer to stop.
 *
 */
void _mali_osk_timer_del(_mali_osk_timer_t *tim);

/** @brief Stop a timer.
 *
 * Stop the timer. When the function returns, the timer's callback may still be
 * running on any CPU core.
 *
 * It is legal to stop an already stopped timer.
 *
 * @param tim the timer to stop.
 */
void _mali_osk_timer_del_async(_mali_osk_timer_t *tim);

/** @brief Check if timer is pending.
 *
 * Check if timer is active.
 *
 * @param tim the timer to check
 * @return MALI_TRUE if time is active, MALI_FALSE if it is not active
 */
mali_bool _mali_osk_timer_pending(_mali_osk_timer_t *tim);

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
void _mali_osk_timer_setcallback(_mali_osk_timer_t *tim, _mali_osk_timer_callback_t callback, void *data);

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
void _mali_osk_timer_term(_mali_osk_timer_t *tim);
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

/** @brief Return whether ticka occurs after or at the same time as  tickb
 *
 * Systems where ticks can wrap must handle that.
 *
 * @param ticka ticka
 * @param tickb tickb
 * @return MALI_TRUE if ticka represents a time that occurs at or after tickb.
 */
mali_bool _mali_osk_time_after_eq(unsigned long ticka, unsigned long tickb);

/** @brief Convert milliseconds to OS 'ticks'
 *
 * @param ms time interval in milliseconds
 * @return the corresponding time interval in OS ticks.
 */
unsigned long _mali_osk_time_mstoticks(u32 ms);

/** @brief Convert OS 'ticks' to milliseconds
 *
 * @param ticks time interval in OS ticks.
 * @return the corresponding time interval in milliseconds
 */
u32 _mali_osk_time_tickstoms(unsigned long ticks);


/** @brief Get the current time in OS 'ticks'.
 * @return the current time in OS 'ticks'.
 */
unsigned long _mali_osk_time_tickcount(void);

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
void _mali_osk_time_ubusydelay(u32 usecs);

/** @brief Return time in nano seconds, since any given reference.
 *
 * @return Time in nano seconds
 */
u64 _mali_osk_time_get_ns(void);

/** @brief Return time in nano seconds, since boot time.
 *
 * @return Time in nano seconds
 */
u64 _mali_osk_boot_time_get_ns(void);

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
u32 _mali_osk_clz(u32 val);

/** @brief find last (most-significant) bit set
 *
 * @param val 32-bit words to count last bit set on
 * @return last bit set.
 */
u32 _mali_osk_fls(u32 val);

/** @} */ /* end group _mali_osk_math */

/** @addtogroup _mali_osk_wait_queue OSK Wait Queue functionality
 * @{ */

/** @brief Initialize an empty Wait Queue */
_mali_osk_wait_queue_t *_mali_osk_wait_queue_init(void);

/** @brief Sleep if condition is false
 *
 * @param queue the queue to use
 * @param condition function pointer to a boolean function
 * @param data data parameter for condition function
 *
 * Put thread to sleep if the given \a condition function returns false. When
 * being asked to wake up again, the condition will be re-checked and the
 * thread only woken up if the condition is now true.
 */
void _mali_osk_wait_queue_wait_event(_mali_osk_wait_queue_t *queue, mali_bool(*condition)(void *), void *data);

/** @brief Sleep if condition is false
 *
 * @param queue the queue to use
 * @param condition function pointer to a boolean function
 * @param data data parameter for condition function
 * @param timeout timeout in ms
 *
 * Put thread to sleep if the given \a condition function returns false. When
 * being asked to wake up again, the condition will be re-checked and the
 * thread only woken up if the condition is now true.  Will return if time
 * exceeds timeout.
 */
void _mali_osk_wait_queue_wait_event_timeout(_mali_osk_wait_queue_t *queue, mali_bool(*condition)(void *), void *data, u32 timeout);

/** @brief Wake up all threads in wait queue if their respective conditions are
 * true
 *
 * @param queue the queue whose threads should be woken up
 *
 * Wake up all threads in wait queue \a queue whose condition is now true.
 */
void _mali_osk_wait_queue_wake_up(_mali_osk_wait_queue_t *queue);

/** @brief terminate a wait queue
 *
 * @param queue the queue to terminate.
 */
void _mali_osk_wait_queue_term(_mali_osk_wait_queue_t *queue);
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
void _mali_osk_dbgmsg(const char *fmt, ...);

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
u32 _mali_osk_snprintf(char *buf, u32 size, const char *fmt, ...);

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

/** @brief Return an name for calling process.
 *
 * @return name for calling process.
 */
char *_mali_osk_get_comm(void);

/** @brief Return an identificator for calling thread.
 *
 * @return Identificator for calling thread.
 */
u32 _mali_osk_get_tid(void);


/** @brief Take a reference to the power manager system for the Mali device (synchronously).
 *
 * When function returns successfully, Mali is ON.
 *
 * @note Call \a _mali_osk_pm_dev_ref_put() to release this reference.
 */
_mali_osk_errcode_t _mali_osk_pm_dev_ref_get_sync(void);

/** @brief Take a reference to the external power manager system for the Mali device (asynchronously).
 *
 * Mali might not yet be on after this function as returned.
 * Please use \a _mali_osk_pm_dev_barrier() or \a _mali_osk_pm_dev_ref_get_sync()
 * to wait for Mali to be powered on.
 *
 * @note Call \a _mali_osk_pm_dev_ref_dec() to release this reference.
 */
_mali_osk_errcode_t _mali_osk_pm_dev_ref_get_async(void);

/** @brief Release the reference to the external power manger system for the Mali device.
 *
 * When reference count reach zero, the cores can be off.
 *
 * @note This must be used to release references taken with
 * \a _mali_osk_pm_dev_ref_get_sync() or \a _mali_osk_pm_dev_ref_get_sync().
 */
void _mali_osk_pm_dev_ref_put(void);

/** @brief Block until pending PM operations are done
 */
void _mali_osk_pm_dev_barrier(void);

/** @} */ /* end group  _mali_osk_miscellaneous */

/** @defgroup _mali_osk_bitmap OSK Bitmap
 * @{ */

/** @brief Allocate a unique number from the bitmap object.
 *
 * @param bitmap Initialized bitmap object.
 * @return An unique existence in the bitmap object.
 */
u32 _mali_osk_bitmap_alloc(struct _mali_osk_bitmap *bitmap);

/** @brief Free a interger to the bitmap object.
 *
 * @param bitmap Initialized bitmap object.
 * @param obj An number allocated from bitmap object.
 */
void _mali_osk_bitmap_free(struct _mali_osk_bitmap *bitmap, u32 obj);

/** @brief Allocate continuous number from the bitmap object.
 *
 * @param bitmap Initialized bitmap object.
 * @return start number of the continuous number block.
 */
u32 _mali_osk_bitmap_alloc_range(struct _mali_osk_bitmap *bitmap, int cnt);

/** @brief Free a block of continuous number block to the bitmap object.
 *
 * @param bitmap Initialized bitmap object.
 * @param obj Start number.
 * @param cnt The size of the continuous number block.
 */
void _mali_osk_bitmap_free_range(struct _mali_osk_bitmap *bitmap, u32 obj, int cnt);

/** @brief Available count could be used to allocate in the given bitmap object.
 *
 */
u32 _mali_osk_bitmap_avail(struct _mali_osk_bitmap *bitmap);

/** @brief Initialize an bitmap object..
 *
 * @param bitmap An poiter of uninitialized bitmap object.
 * @param num Size of thei bitmap object and decide the memory size allocated.
 * @param reserve start number used to allocate.
 */
int _mali_osk_bitmap_init(struct _mali_osk_bitmap *bitmap, u32 num, u32 reserve);

/** @brief Free the given bitmap object.
 *
 * @param bitmap Initialized bitmap object.
 */
void _mali_osk_bitmap_term(struct _mali_osk_bitmap *bitmap);
/** @} */ /* end group  _mali_osk_bitmap */

/** @} */ /* end group osuapi */

/** @} */ /* end group uddapi */



#ifdef __cplusplus
}
#endif

/* Check standard inlines */
#ifndef MALI_STATIC_INLINE
#error MALI_STATIC_INLINE not defined on your OS
#endif

#ifndef MALI_NON_STATIC_INLINE
#error MALI_NON_STATIC_INLINE not defined on your OS
#endif

#endif /* __MALI_OSK_H__ */
