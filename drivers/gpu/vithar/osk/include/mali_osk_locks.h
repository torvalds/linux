/*
 *
 * (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _OSK_LOCKS_H_
#define _OSK_LOCKS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

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
 * @defgroup osklocks Mutual Exclusion
 *
 * A read/write lock (rwlock) is used to control access to a shared resource,
 * where multiple threads are allowed to read from the shared resource, but
 * only one thread is allowed to write to the shared resources at any one time.
 * A thread must specify the type of access (read/write) when locking the
 * rwlock. If a rwlock is locked for write access, other threads that attempt
 * to lock the same rwlock will block. If a rwlock is locked for read access,
 * threads that attempts to lock the rwlock for write access, will block until
 * until all threads with read access have unlocked the rwlock.
 
 * @note If an OS does not provide a synchronisation object to implement a 
 * rwlock, a OSK mutex can be used instead for its implementation. This would
 * only allow one reader or writer to access the shared resources at any one
 * time.
 *
 * A mutex is used to control access to a shared resource, where only one
 * thread is allowed access at any one time. A thread must lock the mutex
 * to gain access; other threads that attempt to lock the same mutex will
 * block. Mutexes can only be unlocked by the thread that holds the lock.
 *
 * @note OSK mutexes are intended for use in a situation where access to the
 * shared resource is likely to be contended. OSK mutexes make use of the
 * mutual exclusion primitives provided by the target OS, which often
 * are considered "heavyweight". 
 *
 * Spinlocks are also used to control access to a shared resource and
 * enforce that only one thread has access at any one time. They differ from
 * OSK mutexes in that they poll the mutex to obtain the lock. This makes a
 * spinlock especially suited for contexts where you are not allowed to block
 * while waiting for access to the shared resource. A OSK mutex could not be
 * used in such a context as it can block while trying to obtain the mutex.
 *
 * A spinlock should be held for the minimum time possible, as in the contended
 * case threads will not sleep but poll and therefore use CPU-cycles.
 *
 * While holding a spinlock, you must not sleep. You must not obtain a rwlock,
 * mutex or do anything else that might block your thread. This is to prevent another
 * thread trying to lock the same spinlock while your thread holds the spinlock, 
 * which could take a very long time (as it requires your thread to get scheduled
 * in again and unlock the spinlock) or could even deadlock your system.
 *
 * Spinlocks are considered 'lightweight': for the uncontended cases, the mutex
 * can be obtained quickly. For the lightly-contended cases on Multiprocessor
 * systems, the mutex can be obtained quickly without resorting to
 * "heavyweight" OS primitives. 
 *
 * Two types of spinlocks are provided. A type that is safe to use when sharing
 * a resource with an interrupt service routine, and one that should only be
 * used to share the resource between threads. The former should be used to
 * prevent deadlock between a thread that holds a spinlock while an
 * interrupt occurs and the interrupt service routine trying to obtain the same
 * spinlock too.
 *
 * @anchor oskmutex_spinlockdetails
 * @par Important details of OSK Spinlocks.
 *
 * OSK spinlocks are not intended for high-contention cases. If high-contention 
 * usecases occurs frequently for a particular spinlock, then it is wise to 
 * consider using an OSK Mutex instead.
 *
 * @note An especially important reason for not using OSK Spinlocks in highly
 * contended cases is that they defeat the OS's Priority Inheritance mechanisms
 * that would normally alleviate Priority Inversion problems. This is because
 * once the spinlock is obtained, the OS usually does not know which thread has
 * obtained the lock, and so cannot know which thread must have its priority
 * boosted to alleviate the Priority Inversion.
 *
 * As a guide, use a spinlock when CPU-bound for a short period of time
 * (thousands of cycles). CPU-bound operations include reading/writing of
 * memory or registers. Do not use a spinlock when IO bound (e.g. user input,
 * buffered IO reads/writes, calls involving significant device driver IO
 * calls).
 */
/** @{ */

/**
 * @brief Initialize a mutex
 * 
 * Initialize a mutex structure. If the function returns successfully, the
 * mutex is in the unlocked state.
 *
 * The caller must allocate the memory for the @see osk_mutex
 * structure, which is then populated within this function. If the OS-specific
 * mutex referenced from the structure cannot be initialized, an error is
 * returned.
 *
 * The mutex must be terminated when no longer required, by using
 * osk_mutex_term(). Otherwise, a resource leak may result in the OS.
 *
 * The mutex is initialized with a lock order parameter, \a order. Refer to
 * @see oskmutex_lockorder for more information on Rwlock/Mutex/Spinlock lock
 * ordering.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to attempt to initialize a mutex that is
 * currently initialized.
 *
 * @param[out] lock  pointer to an uninitialized mutex structure
 * @param[in] order  the locking order of the mutex
 * @return OSK_ERR_NONE on success, any other value indicates a failure.
 */
OSK_STATIC_INLINE osk_error osk_mutex_init(osk_mutex * const lock, osk_lock_order order) CHECK_RESULT;

/**
 * @brief Terminate a mutex
 * 
 * Terminate the mutex pointed to by \a lock, which must be
 * a pointer to a valid unlocked mutex. When the mutex is terminated, the
 * OS-specific mutex is freed.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to attempt to terminate a mutex that is currently
 * terminated.
 *
 * @illegal It is illegal to call osk_mutex_term() on a locked mutex.
 *
 * @param[in] lock  pointer to a valid mutex structure
 */
OSK_STATIC_INLINE void osk_mutex_term(osk_mutex * lock);

/**
 * @brief Lock a mutex
 * 
 * Lock the mutex pointed to by \a lock. If the mutex is currently unlocked,
 * the calling thread returns with the mutex locked. If a second thread
 * attempts to lock the same mutex, it blocks until the first thread
 * unlocks the mutex. If two or more threads are blocked waiting on the first
 * thread to unlock the mutex, it is undefined as to which thread is unblocked
 * when the first thread unlocks the mutex. 
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to lock a mutex or spinlock with an order that is
 * higher than any mutex or spinlock held by the current thread. Mutexes and
 * spinlocks must be locked in the order of highest to lowest, to prevent
 * deadlocks. Refer to @see oskmutex_lockorder for more information.
 *
 * It is a programming error to exit a thread while it has a locked mutex.
 *
 * It is a programming error to lock a mutex from an ISR context. In an ISR
 * context you are not allowed to block what osk_mutex_lock() potentially does.
 *
 * @illegal It is illegal to call osk_mutex_lock() on a mutex that is currently
 * locked by the caller thread. That is, it is illegal for the same thread to
 * lock a mutex twice, without unlocking it in between. 
 *
 * @param[in] lock  pointer to a valid mutex structure
 */
OSK_STATIC_INLINE void osk_mutex_lock(osk_mutex * lock);

/**
 * @brief Unlock a mutex
 * 
 * Unlock the mutex pointed to by \a lock. The calling thread must be the
 * same thread that locked the mutex. If no other threads are waiting on the
 * mutex to be unlocked, the function returns immediately, with the mutex
 * unlocked. If one or more threads are waiting on the mutex to be unlocked,
 * then this function returns, and a thread waiting on the mutex can be
 * unblocked. It is undefined as to which thread is unblocked.
 *
 * @note It is not defined \em when a waiting thread is unblocked. For example,
 * a thread calling osk_mutex_unlock() followed by osk_mutex_lock() may (or may
 * not) obtain the lock again, preventing other threads from being
 * released. Neither the 'immediately releasing', nor the 'delayed releasing'
 * behavior of osk_mutex_unlock() can be relied upon. If such behavior is
 * required, then you must implement it yourself, such as by using a second
 * synchronization primitive. 
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * @illegal It is illegal for a thread to call osk_mutex_unlock() on a mutex
 * that it has not locked, even if that mutex is currently locked by another
 * thread. That is, it is illegal for any thread other than the 'owner' of the
 * mutex to unlock it. And, you must not unlock an already unlocked mutex.
 *
 * @param[in] lock  pointer to a valid mutex structure
 */
OSK_STATIC_INLINE void osk_mutex_unlock(osk_mutex * lock);

/**
 * @brief Initialize a spinlock
 * 
 * Initialize a spinlock. If the function returns successfully, the
 * spinlock is in the unlocked state.
 *
 * @note If the spinlock is used for sharing a resource with an interrupt service
 * routine, use the IRQ safe variant of the spinlock, see osk_spinlock_irq.
 * The IRQ safe variant should be used in that situation to prevent
 * deadlock between a thread/ISR that holds a spinlock while an interrupt occurs
 * and the interrupt service routine trying to obtain the same spinlock too.
 
 * The caller must allocate the memory for the @see osk_spinlock
 * structure, which is then populated within this function. If the OS-specific
 * spinlock referenced from the structure cannot be initialized, an error is
 * returned.
 *
 * The spinlock must be terminated when no longer required, by using
 * osk_spinlock_term(). Otherwise, a resource leak may result in the OS.
 *
 * The spinlock is initialized with a lock order parameter, \a order. Refer to
 * @see oskmutex_lockorder for more information on Rwlock/Mutex/Spinlock lock
 * ordering.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to attempt to initialize a spinlock that is
 * currently initialized.
 *
 * @param[out] lock  pointer to a spinlock structure
 * @param[in] order  the locking order of the spinlock
 * @return OSK_ERR_NONE on success, any other value indicates a failure.
 */
OSK_STATIC_INLINE osk_error osk_spinlock_init(osk_spinlock * const lock, osk_lock_order order) CHECK_RESULT;

/**
 * @brief Terminate a spinlock
 * 
 * Terminates the spinlock and releases any associated resources.
 * The spinlock must be in an unlocked state.
 *
 * Terminate the spinlock pointed to by \a lock, which must be
 * a pointer to a valid unlocked spinlock. When the spinlock is terminated, the
 * OS-specific spinlock is freed.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to attempt to terminate a spinlock that is currently
 * terminated.
 *
 * @illegal It is illegal to call osk_spinlock_term() on a locked spinlock.
 * @param[in] lock  pointer to a valid spinlock structure
 */
OSK_STATIC_INLINE void osk_spinlock_term(osk_spinlock * lock);

/**
 * @brief Lock a spinlock
 *
 * Lock the spinlock pointed to by \a lock. If the spinlock is currently unlocked,
 * the calling thread returns with the spinlock locked. If a second thread
 * attempts to lock the same spinlock, it polls the spinlock until the first thread
 * unlocks the spinlock. If two or more threads are polling the spinlock waiting 
 * on the first thread to unlock the spinlock, it is undefined as to which thread
 * will lock the spinlock when the first thread unlocks the spinlock. 
 *
 * While the spinlock is locked by the calling thread, the spinlock implementation
 * should prevent any possible deadlock issues arising from another thread on the
 * same CPU trying to lock the same spinlock.
 *
 * While holding a spinlock, you must not sleep. You must not obtain a rwlock,
 * mutex or do anything else that might block your thread. This is to prevent another
 * thread trying to lock the same spinlock while your thread holds the spinlock, 
 * which could take a very long time (as it requires your thread to get scheduled
 * in again and unlock the spinlock) or could even deadlock your system.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to lock a spinlock, rwlock or mutex with an order that
 * is higher than any spinlock, rwlock, or mutex held by the current thread. Spinlocks,
 * Rwlocks, and Mutexes must be locked in the order of highest to lowest, to prevent
 * deadlocks. Refer to @see oskmutex_lockorder for more information.
 *
 * It is a programming error to exit a thread while it has a locked spinlock.
 *
 * It is a programming error to lock a spinlock from an ISR context. Use the IRQ
 * safe spinlock type instead.
 *
 * @illegal It is illegal to call osk_spinlock_lock() on a spinlock that is currently
 * locked by the caller thread. That is, it is illegal for the same thread to
 * lock a spinlock twice, without unlocking it in between. 
 *
 * @param[in] lock  pointer to a valid spinlock structure
 */
OSK_STATIC_INLINE void osk_spinlock_lock(osk_spinlock * lock);

/**
 * @brief Unlock a spinlock
 *
 * Unlock the spinlock pointed to by \a lock. The calling thread must be the
 * same thread that locked the spinlock. If no other threads are polling the
 * spinlock waiting on the spinlock to be unlocked, the function returns
 * immediately, with the spinlock unlocked. If one or more threads are polling
 * the spinlock waiting on the spinlock to be unlocked, then this function 
 * returns, and a thread waiting on the spinlock can stop polling and continue
 * with the spinlock locked. It is undefined as to which thread this is.
 *
 * @note It is not defined \em when a waiting thread continues. For example,
 * a thread calling osk_spinlock_unlock() followed by osk_spinlock_lock() may (or may
 * not) obtain the spinlock again, preventing other threads from continueing.
 * Neither the 'immediately releasing', nor the 'delayed releasing'
 * behavior of osk_spinlock_unlock() can be relied upon. If such behavior is
 * required, then you must implement it yourself, such as by using a second
 * synchronization primitive. 
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * @illegal It is illegal for a thread to call osk_spinlock_unlock() on a spinlock
 * that it has not locked, even if that spinlock is currently locked by another
 * thread. That is, it is illegal for any thread other than the 'owner' of the
 * spinlock to unlock it. And, you must not unlock an already unlocked spinlock.
 *
 * @param[in] lock  pointer to a valid spinlock structure
 */
OSK_STATIC_INLINE void osk_spinlock_unlock(osk_spinlock * lock);

/**
 * @brief Initialize an IRQ safe spinlock
 * 
 * Initialize an IRQ safe spinlock. If the function returns successfully, the
 * spinlock is in the unlocked state.
 *
 * This variant of spinlock is used for sharing a resource with an interrupt
 * service routine. The IRQ safe variant should be used in this siutation to
 * prevent deadlock between a thread/ISR that holds a spinlock while an interrupt
 * occurs and the interrupt service routine trying to obtain the same spinlock
 * too. If the spinlock is not used to share a resource with an interrupt service
 * routine, one should use the osk_spinlock instead of the osk_spinlock_irq
 * variant, see osk_spinlock_init().
 
 * The caller must allocate the memory for the @see osk_spinlock_irq
 * structure, which is then populated within this function. If the OS-specific
 * spinlock referenced from the structure cannot be initialized, an error is
 * returned.
 *
 * The spinlock must be terminated when no longer required, by using
 * osk_spinlock_irq_term(). Otherwise, a resource leak may result in the OS.
 *
 * The spinlock is initialized with a lock order parameter, \a order. Refer to
 * @see oskmutex_lockorder for more information on Rwlock/Mutex/Spinlock lock
 * ordering.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to attempt to initialize a spinlock that is
 * currently initialized.
 *
 * @param[out] lock  pointer to a IRQ safe spinlock structure
 * @param[in] order  the locking order of the IRQ safe spinlock
 * @return OSK_ERR_NONE on success, any other value indicates a failure.
 */
OSK_STATIC_INLINE osk_error osk_spinlock_irq_init(osk_spinlock_irq * const lock, osk_lock_order order) CHECK_RESULT;

/**
 * @brief Terminate an IRQ safe spinlock
 * 
 * Terminate the IRQ safe spinlock pointed to by \a lock, which must be
 * a pointer to a valid unlocked IRQ safe spinlock. When the IRQ safe spinlock 
 * is terminated, the OS-specific spinlock is freed.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to attempt to terminate a IRQ safe pinlock that is
 * currently terminated.
 *
 * @param[in] lock  pointer to a valid IRQ safe spinlock structure
 */
OSK_STATIC_INLINE void osk_spinlock_irq_term(osk_spinlock_irq * lock);

/**
 * @brief Lock an IRQ safe spinlock
 *
 * Lock the IRQ safe spinlock (from here on refered to as 'spinlock') pointed to 
 * by \a lock. If the spinlock is currently unlocked, the calling thread returns 
 * with the spinlock locked. If a second thread attempts to lock the same spinlock,
 * it polls the spinlock until the first thread unlocks the spinlock. If two or
 * more threads are polling the spinlock waiting on the first thread to unlock the
 * spinlock, it is undefined as to which thread will lock the spinlock when the 
 * first thread unlocks the spinlock. 
 *
 * While the spinlock is locked by the calling thread, the spinlock implementation
 * should prevent any possible deadlock issues arising from another thread on the
 * same CPU trying to lock the same spinlock.
 *
 * While holding a spinlock, you must not sleep. You must not obtain a rwlock,
 * mutex or do anything else that might block your thread. This is to prevent another
 * thread trying to lock the same spinlock while your thread holds the spinlock, 
 * which could take a very long time (as it requires your thread to get scheduled
 * in again and unlock the spinlock) or could even deadlock your system.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to lock a spinlock, rwlock or mutex with an order that
 * is higher than any spinlock, rwlock, or mutex held by the current thread. Spinlocks,
 * Rwlocks, and Mutexes must be locked in the order of highest to lowest, to prevent
 * deadlocks. Refer to @see oskmutex_lockorder for more information.
 *
 * It is a programming error to exit a thread while it has a locked spinlock.
 *
 * @illegal It is illegal to call osk_spinlock_irq_lock() on a spinlock that is
 * currently locked by the caller thread. That is, it is illegal for the same thread
 * to lock a spinlock twice, without unlocking it in between. 
 * 
 * @param[in] lock  pointer to a valid IRQ safe spinlock structure
 */
OSK_STATIC_INLINE void osk_spinlock_irq_lock(osk_spinlock_irq * lock);

/**
 * @brief Unlock an IRQ safe spinlock 
 *
 * Unlock the IRQ safe spinlock (from hereon refered to as 'spinlock') pointed to 
 * by \a lock. The calling thread/ISR must be the same thread/ISR that locked the
 * spinlock. If no other threads/ISRs are polling the spinlock waiting on the spinlock
 * to be unlocked, the function returns* immediately, with the spinlock unlocked. If
 * one or more threads/ISRs are polling the spinlock waiting on the spinlock to be unlocked,
 * then this function returns, and a thread/ISR waiting on the spinlock can stop polling 
 * and continue with the spinlock locked. It is undefined as to which thread/ISR this is.
 *
 * @note It is not defined \em when a waiting thread/ISR continues. For example,
 * a thread/ISR calling osk_spinlock_irq_unlock() followed by osk_spinlock_irq_lock() may
 * (or may not) obtain the spinlock again, preventing other threads from continueing.
 * Neither the 'immediately releasing', nor the 'delayed releasing'
 * behavior of osk_spinlock_irq_unlock() can be relied upon. If such behavior is
 * required, then you must implement it yourself, such as by using a second
 * synchronization primitive. 
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * @illegal It is illegal for a thread to call osk_spinlock_irq_unlock() on a spinlock
 * that it has not locked, even if that spinlock is currently locked by another
 * thread. That is, it is illegal for any thread other than the 'owner' of the
 * spinlock to unlock it. And, you must not unlock an already unlocked spinlock.
 *
 * @param[in] lock  pointer to a valid IRQ safe spinlock structure
 */
OSK_STATIC_INLINE void osk_spinlock_irq_unlock(osk_spinlock_irq * lock);

/**
 * @brief Initialize a rwlock
 * 
 * Read/write locks allow multiple readers to obtain the lock (shared access),
 * or one writer to obtain the lock (exclusive access).
 * Read/write locks are created in an unlocked state. 
 *
 * Initialize a rwlock structure. If the function returns successfully, the
 * rwlock is in the unlocked state.
 *
 * The caller must allocate the memory for the @see osk_rwlock
 * structure, which is then populated within this function. If the OS-specific
 * rwlock referenced from the structure cannot be initialized, an error is
 * returned.
 *
 * The rwlock must be terminated when no longer required, by using
 * osk_rwlock_term(). Otherwise, a resource leak may result in the OS.
 *
 * The rwlock is initialized with a lock order parameter, \a order. Refer to
 * @see oskmutex_lockorder for more information on Rwlock/Mutex/Spinlock lock
 * ordering.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to attempt to initialize a rwlock that is
 * currently initialized.
 *
 * @param[out] lock  pointer to a rwlock structure
 * @param[in] order  the locking order of the rwlock
 * @return OSK_ERR_NONE on success, any other value indicates a failure.
 */
OSK_STATIC_INLINE osk_error osk_rwlock_init(osk_rwlock * const lock, osk_lock_order order) CHECK_RESULT;

/**
 * @brief Terminate a rwlock
 * 
 * Terminate the rwlock pointed to by \a lock, which must be
 * a pointer to a valid unlocked rwlock. When the rwlock is terminated, the
 * OS-specific rwlock is freed.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to attempt to terminate a rwlock that is currently
 * terminated.
 *
 * @illegal It is illegal to call osk_rwlock_term() on a locked rwlock.
 *
 * @param[in] lock  pointer to a valid rwlock structure
 */
OSK_STATIC_INLINE void osk_rwlock_term(osk_rwlock * lock);

/**
 * @brief Lock a rwlock for read access
 * 
 * Lock the rwlock pointed to by \a lock for read access. A rwlock may
 * be locked for read access by multiple threads. If the mutex
 * mutex is not locked for exclusive write access, the calling thread 
 * returns with the rwlock locked for read access. If the mutex is 
 * currently locked for exclusive write access, the calling thread blocks 
 * until the thread with exclusive write access unlocks the rwlock.
 * If multiple threads are blocked waiting for read access or exclusive 
 * write access to the rwlock, it is undefined as to which thread is
 * unblocked when the rwlock is unlocked (by the thread with exclusive
 * write access).
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to lock a rwlock, mutex or spinlock with an order that is
 * higher than any rwlock, mutex or spinlock held by the current thread. Rwlocks, mutexes and
 * spinlocks must be locked in the order of highest to lowest, to prevent
 * deadlocks. Refer to @see oskmutex_lockorder for more information.
 *
 * It is a programming error to exit a thread while it has a locked rwlock.
 *
 * It is a programming error to lock a rwlock from an ISR context. In an ISR
 * context you are not allowed to block what osk_rwlock_read_lock() potentially does.
 *
 * @illegal It is illegal to call osk_rwlock_read_lock() on a rwlock that is currently
 * locked by the caller thread. That is, it is illegal for the same thread to
 * lock a rwlock twice, without unlocking it in between. 
 * @param[in] lock  pointer to a valid rwlock structure
 */
OSK_STATIC_INLINE void osk_rwlock_read_lock(osk_rwlock * lock);

/**
 * @brief Unlock a rwlock for read access
 * 
 * Unlock the rwlock pointed to by \a lock. The calling thread must be the
 * same thread that locked the rwlock for read access. If no other threads
 * are waiting on the rwlock to be unlocked, the function returns
 * immediately, with the rwlock unlocked. If one or more threads are waiting
 * on the rwlock to be unlocked for write access, and the calling thread
 * is the last thread holding the rwlock for read access, then this function
 * returns, and a thread waiting on the rwlock for write access can be
 * unblocked. It is undefined as to which thread is unblocked.
 *
 * @note It is not defined \em when a waiting thread is unblocked. For example,
 * a thread calling osk_rwlock_read_unlock() followed by osk_rwlock_read_lock()
 * may (or may not) obtain the lock again, preventing other threads from being
 * released. Neither the 'immediately releasing', nor the 'delayed releasing'
 * behavior of osk_rwlock_read_unlock() can be relied upon. If such behavior is
 * required, then you must implement it yourself, such as by using a second
 * synchronization primitve. 
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * @illegal It is illegal for a thread to call osk_rwlock_read_unlock() on a
 * rwlock that it has not locked, even if that rwlock is currently locked by another
 * thread. That is, it is illegal for any thread other than the 'owner' of the
 * rwlock to unlock it. And, you must not unlock an already unlocked rwlock.

 * @param[in] lock  pointer to a valid rwlock structure
 */
OSK_STATIC_INLINE void osk_rwlock_read_unlock(osk_rwlock * lock);

/**
 * @brief Lock a rwlock for exclusive write access
 * 
 * Lock the rwlock pointed to by \a lock for exclusive write access. If the 
 * rwlock is currently unlocked, the calling thread returns with the rwlock 
 * locked. If the rwlock is currently locked, the calling thread blocks
 * until the last thread with read access or the thread with exclusive write
 * access unlocks the rwlock. If multiple threads are blocked waiting
 * for exclusive write access to the rwlock, it is undefined as to which
 * thread is unblocked when the rwlock is unlocked (by either the last thread
 * thread with read access or the thread with exclusive write access).
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * It is a programming error to lock a rwlock, mutex or spinlock with an order that is
 * higher than any rwlock, mutex or spinlock held by the current thread. Rwlocks, mutexes and
 * spinlocks must be locked in the order of highest to lowest, to prevent
 * deadlocks. Refer to @see oskmutex_lockorder for more information.
 *
 * It is a programming error to exit a thread while it has a locked rwlock.
 *
 * It is a programming error to lock a rwlock from an ISR context. In an ISR
 * context you are not allowed to block what osk_rwlock_write_lock() potentially does.
 *
 * @illegal It is illegal to call osk_rwlock_write_lock() on a rwlock that is currently
 * locked by the caller thread. That is, it is illegal for the same thread to
 * lock a rwlock twice, without unlocking it in between. 
 *
 * @param[in] lock  pointer to a valid rwlock structure
 */
OSK_STATIC_INLINE void osk_rwlock_write_lock(osk_rwlock * lock);

/**
 * @brief Unlock a rwlock for exclusive write access
 *
 * Unlock the rwlock pointed to by \a lock. The calling thread must be the
 * same thread that locked the rwlock for exclusive write access. If no
 * other threads are waiting on the rwlock to be unlocked, the function returns
 * immediately, with the rwlock unlocked. If one or more threads are waiting
 * on the rwlock to be unlocked, then this function returns, and a thread
 * waiting on the rwlock can be unblocked. It is undefined as to which
 * thread is unblocked.
 *
 * @note It is not defined \em when a waiting thread is unblocked. For example,
 * a thread calling osk_rwlock_write_unlock() followed by osk_rwlock_write_lock()
 * may (or may not) obtain the lock again, preventing other threads from being
 * released. Neither the 'immediately releasing', nor the 'delayed releasing'
 * behavior of osk_rwlock_write_unlock() can be relied upon. If such behavior is
 * required, then you must implement it yourself, such as by using a second
 * synchronization primitve. 
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a lock parameter.
 *
 * @illegal It is illegal for a thread to call osk_rwlock_write_unlock() on a
 * rwlock that it has not locked, even if that rwlock is currently locked by another
 * thread. That is, it is illegal for any thread other than the 'owner' of the
 * rwlock to unlock it. And, you must not unlock an already unlocked rwlock.
 *
 * @param[in] lock  pointer to a valid read/write lock structure
 */
OSK_STATIC_INLINE void osk_rwlock_write_unlock(osk_rwlock * lock);

/* @} */ /* end group osklocks */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_locks.h>


#ifdef __cplusplus
}
#endif

#endif /* _OSK_LOCKS_H_ */
