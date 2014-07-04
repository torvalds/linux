/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_osu.h
 * Defines the OS abstraction layer for the base driver
 */

#ifndef __UMP_OSU_H__
#define __UMP_OSU_H__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef unsigned int u32;
#ifdef _MSC_VER
typedef unsigned __int64        u64;
typedef signed   __int64        s64;
#else
typedef unsigned long long      u64;
typedef signed long long        s64;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned long ump_bool;

#ifndef UMP_TRUE
#define UMP_TRUE ((ump_bool)1)
#endif

#ifndef UMP_FALSE
#define UMP_FALSE ((ump_bool)0)
#endif

#define UMP_STATIC          static

/**
 * @addtogroup ump_user_space_api Unified Device Driver (UDD) APIs used by UMP
 *
 * @{
 */

/**
 * @defgroup ump_osuapi UDD OS Abstraction for User-side (OSU) APIs for UMP
 *
 * @{
 */

/* The following is necessary to prevent the _ump_osk_errcode_t doxygen from
 * becoming unreadable: */
/** @cond OSU_COPY_OF__UMP_OSU_ERRCODE_T */

/**
 * @brief OSU/OSK Error codes.
 *
 * Each OS may use its own set of error codes, and may require that the
 * User/Kernel interface take certain error code. This means that the common
 * error codes need to be sufficiently rich to pass the correct error code
 * through from the OSK/OSU to U/K layer, across all OSs.
 *
 * The result is that some error codes will appear redundant on some OSs.
 * Under all OSs, the OSK/OSU layer must translate native OS error codes to
 * _ump_osk/u_errcode_t codes. Similarly, the U/K layer must translate from
 * _ump_osk/u_errcode_t codes to native OS error codes.
 *
 */
typedef enum
{
	_UMP_OSK_ERR_OK = 0,              /**< Success. */
	_UMP_OSK_ERR_FAULT = -1,          /**< General non-success */
	_UMP_OSK_ERR_INVALID_FUNC = -2,   /**< Invalid function requested through User/Kernel interface (e.g. bad IOCTL number) */
	_UMP_OSK_ERR_INVALID_ARGS = -3,   /**< Invalid arguments passed through User/Kernel interface */
	_UMP_OSK_ERR_NOMEM = -4,          /**< Insufficient memory */
	_UMP_OSK_ERR_TIMEOUT = -5,        /**< Timeout occured */
	_UMP_OSK_ERR_RESTARTSYSCALL = -6, /**< Special: On certain OSs, must report when an interruptable mutex is interrupted. Ignore otherwise. */
	_UMP_OSK_ERR_ITEM_NOT_FOUND = -7, /**< Table Lookup failed */
	_UMP_OSK_ERR_BUSY = -8,           /**< Device/operation is busy. Try again later */
	_UMP_OSK_ERR_UNSUPPORTED = -9,  /**< Optional part of the interface used, and is unsupported */
} _ump_osk_errcode_t;

/** @endcond */ /* end cond OSU_COPY_OF__UMP_OSU_ERRCODE_T */

/**
 * @brief OSU Error codes.
 *
 * OSU error codes - enum values intentionally same as OSK
 */
typedef enum
{
	_UMP_OSU_ERR_OK = 0,           /**< Success. */
	_UMP_OSU_ERR_FAULT = -1,       /**< General non-success */
	_UMP_OSU_ERR_TIMEOUT = -2,     /**< Timeout occured */
} _ump_osu_errcode_t;

/** @brief Translate OSU error code to base driver error code.
 *
 * The _UMP_OSU_TRANSLATE_ERROR macro translates an OSU error code to the
 * error codes in use by the base driver.
 */
#define _UMP_OSU_TRANSLATE_ERROR(_ump_osu_errcode) ( ( _UMP_OSU_ERR_OK == (_ump_osu_errcode) ) ? UMP_ERR_NO_ERROR : UMP_ERR_FUNCTION_FAILED)

/** @defgroup _ump_osu_lock OSU Mutual Exclusion Locks
  * @{ */

/** @brief OSU Mutual Exclusion Lock flags type.
 *
 * This is made to look like and function identically to the OSK locks (refer
 * to \ref _ump_osk_lock). However, please note the following \b important
 * differences:
 * - the OSU default lock is a Sleeping, non-interruptible mutex.
 * - the OSU adds the ANYUNLOCK type of lock which allows a thread which doesn't
 * own the lock to release the lock.
 * - the order parameter when creating a lock is currently unused
 *
 * @note Pay careful attention to the difference in default locks for OSU and
 * OSK locks; OSU locks are always non-interruptible, but OSK locks are by
 * default, interruptible. This has implications for systems that do not
 * distinguish between user and kernel mode.
 */
typedef enum
{
	_UMP_OSU_LOCKFLAG_DEFAULT = 0, /**< Default lock type. */
	/** @enum _ump_osu_lock_flags_t
	 *
	 * Flags from 0x0--0x8000 are RESERVED for Kernel-mode
	 */
	_UMP_OSU_LOCKFLAG_ANYUNLOCK = 0x10000, /**< Mutex that guarantees that any thread can unlock it when locked. Otherwise, this will not be possible. */
	/** @enum _ump_osu_lock_flags_t
	 *
	 * Flags from 0x10000 are RESERVED for User-mode
	 */
	_UMP_OSU_LOCKFLAG_STATIC = 0x20000, /* Flag in OSU reserved range to identify lock as a statically initialized lock */

} _ump_osu_lock_flags_t;

typedef enum
{
	_UMP_OSU_LOCKMODE_UNDEF = -1,  /**< Undefined lock mode. For internal use only */
	_UMP_OSU_LOCKMODE_RW    = 0x0, /**< Default. Lock is used to protect data that is read from and written to */
	/** @enum _ump_osu_lock_mode_t
	 *
	 * Lock modes 0x1--0x3F are RESERVED for Kernel-mode */
} _ump_osu_lock_mode_t;

/** @brief Private type for Mutual Exclusion lock objects. */
typedef struct _ump_osu_lock_t_struct _ump_osu_lock_t;

/** @brief The number of static locks supported in _ump_osu_lock_static(). */
#define UMP_OSU_STATIC_LOCK_COUNT (sizeof(_ump_osu_static_locks) / sizeof(_ump_osu_lock_t))

/** @} */ /* end group _ump_osu_lock */

/** @defgroup _ump_osu_memory OSU Memory Allocation
 * @{ */

/** @brief Allocate zero-initialized memory.
 *
 * Returns a buffer capable of containing at least \a n elements of \a size
 * bytes each. The buffer is initialized to zero.
 *
 * The buffer is suitably aligned for storage and subsequent access of every
 * type that the compiler supports. Therefore, the pointer to the start of the
 * buffer may be cast into any pointer type, and be subsequently accessed from
 * such a pointer, without loss of information.
 *
 * When the buffer is no longer in use, it must be freed with _ump_osu_free().
 * Failure to do so will cause a memory leak.
 *
 * @note Most toolchains supply memory allocation functions that meet the
 * compiler's alignment requirements.
 *
 * @param n Number of elements to allocate
 * @param size Size of each element
 * @return On success, the zero-initialized buffer allocated. NULL on failure
 */
void *_ump_osu_calloc(u32 n, u32 size);

/** @brief Allocate memory.
 *
 * Returns a buffer capable of containing at least \a size bytes. The
 * contents of the buffer are undefined.
 *
 * The buffer is suitably aligned for storage and subsequent access of every
 * type that the compiler supports. Therefore, the pointer to the start of the
 * buffer may be cast into any pointer type, and be subsequently accessed from
 * such a pointer, without loss of information.
 *
 * When the buffer is no longer in use, it must be freed with _ump_osu_free().
 * Failure to do so will cause a memory leak.
 *
 * @note Most toolchains supply memory allocation functions that meet the
 * compiler's alignment requirements.
 *
 * Remember to free memory using _ump_osu_free().
 * @param size Number of bytes to allocate
 * @return On success, the buffer allocated. NULL on failure.
 */
void *_ump_osu_malloc(u32 size);

/** @brief Free memory.
 *
 * Reclaims the buffer pointed to by the parameter \a ptr for the system.
 * All memory returned from _ump_osu_malloc(), _ump_osu_calloc() and
 * _ump_osu_realloc() must be freed before the application exits. Otherwise,
 * a memory leak will occur.
 *
 * Memory must be freed once. It is an error to free the same non-NULL pointer
 * more than once.
 *
 * It is legal to free the NULL pointer.
 *
 * @param ptr Pointer to buffer to free
 */
void _ump_osu_free(void *ptr);

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
void *_ump_osu_memcpy(void *dst, const void *src, u32   len);

/** @brief Fills memory.
 *
 * Sets the first \a size bytes of the block of memory pointed to by \a ptr to
 * the specified value
 * @param ptr Pointer to the block of memory to fill.
 * @param chr Value to be set, passed as u32. Only the 8 Least Significant Bits (LSB)
 * are used.
 * @param size Number of bytes to be set to the value.
 * @return \a ptr is always passed through unmodified
 */
void *_ump_osu_memset(void *ptr, u32 chr, u32 size);

/** @} */ /* end group _ump_osu_memory */


/** @addtogroup _ump_osu_lock
 * @{ */

/** @brief Initialize a Mutual Exclusion Lock.
 *
 * Locks are created in the signalled (unlocked) state.
 *
 * The parameter \a initial must be zero.
 *
 * At present, the parameter \a order must be zero. It remains for future
 * expansion for mutex order checking.
 *
 * @param flags flags combined with bitwise OR ('|'), or zero. There are
 * restrictions on which flags can be combined, see \ref _ump_osu_lock_flags_t.
 * @param initial For future expansion into semaphores. SBZ.
 * @param order The locking order of the mutex. SBZ.
 * @return On success, a pointer to a \ref _ump_osu_lock_t object. NULL on failure.
 */
_ump_osu_lock_t *_ump_osu_lock_init(_ump_osu_lock_flags_t flags, u32 initial, u32 order);

/** @brief Obtain a statically initialized Mutual Exclusion Lock.
 *
 * Retrieves a reference to a statically initialized lock. Up to
 * _UMP_OSU_STATIC_LOCK_COUNT statically initialized locks are
 * available. Only _ump_osu_lock_wait(), _ump_osu_lock_trywait(),
 * _ump_osu_lock_signal() can be used with statically initialized locks.
 * _UMP_OSU_LOCKMODE_RW mode should be used when waiting and signalling
 * statically initialized locks.
 *
 * For the same \a nr a pointer to the same statically initialized lock is
 * returned. That is, given the following code:
 * @code
 *  extern u32 n;
 *
 *  _ump_osu_lock_t *locka = _ump_osu_lock_static(n);
 *  _ump_osu_lock_t *lockb = _ump_osu_lock_static(n);
 * @endcode
 * Then (locka == lockb), for all 0 <= n < UMP_OSU_STATIC_LOCK_COUNT.
 *
 * @param nr index of a statically initialized lock [0..UMP_OSU_STATIC_LOCK_COUNT-1]
 * @return On success, a pointer to a _ump_osu_lock_t object. NULL on failure.
 */
_ump_osu_lock_t *_ump_osu_lock_static(u32 nr);

/** @brief Initialize a Mutual Exclusion Lock safely across multiple threads.
 *
 * The _ump_osu_lock_auto_init() function guarantees that the given lock will
 * be initialized once and precisely once, even in a situation involving
 * multiple threads.
 *
 * This is necessary because the first call to certain Public API functions must
 * initialize the API. However, there can be a race involved to call the first
 * library function in multi-threaded applications. To resolve this race, a
 * mutex can be used. This mutex must be initialized, but initialized only once
 * by any thread that might compete for its initialization. This function
 * guarantees the initialization to happen correctly, even when there is an
 * initialization race between multiple threads.
 *
 * Otherwise, the operation is identical to the _ump_osu_lock_init() function.
 * For more details, refer to _ump_osu_lock_init().
 *
 * @param pplock pointer to storage for a _ump_osu_lock_t pointer. This
 * _ump_osu_lock_t pointer may point to a _ump_osu_lock_t that has been
 * initialized already
 * @param flags flags combined with bitwise OR ('|'), or zero. There are
 * restrictions on which flags can be combined. Refer to
 * \ref _ump_osu_lock_flags_t for more information.
 * The absence of any flags (the value 0) results in a sleeping-mutex,
 * which is non-interruptible.
 * @param initial For future expansion into semaphores. SBZ.
 * @param order The locking order of the mutex. SBZ.
 * @return On success, _UMP_OSU_ERR_OK is returned and a pointer to an
 * initialized \ref _ump_osu_lock_t object is written into \a *pplock.
 * _UMP_OSU_ERR_FAULT is returned on failure.
 */
_ump_osu_errcode_t _ump_osu_lock_auto_init(_ump_osu_lock_t **pplock, _ump_osu_lock_flags_t flags, u32 initial, u32 order);

/** @brief Wait for a lock to be signalled (obtained).
 *
 * After a thread has successfully waited on the lock, the lock is obtained by
 * the thread, and is marked as unsignalled. The thread releases the lock by
 * signalling it.
 *
 * To prevent deadlock, locks must always be obtained in the same order.
 *
 * @param lock the lock to wait upon (obtain).
 * @param mode the mode in which the lock should be obtained. Currently this
 * must be _UMP_OSU_LOCKMODE_RW.
 * @return On success, _UMP_OSU_ERR_OK, _UMP_OSU_ERR_FAULT on error.
 */
_ump_osu_errcode_t _ump_osu_lock_wait(_ump_osu_lock_t *lock, _ump_osu_lock_mode_t mode);

/** @brief Wait for a lock to be signalled (obtained) with timeout
 *
 * After a thread has successfully waited on the lock, the lock is obtained by
 * the thread, and is marked as unsignalled. The thread releases the lock by
 * signalling it.
 *
 * To prevent deadlock, locks must always be obtained in the same order.
 *
 * This version can return early if it cannot obtain the lock within the given timeout.
 *
 * @param lock the lock to wait upon (obtain).
 * @param mode the mode in which the lock should be obtained. Currently this
 * must be _UMP_OSU_LOCKMODE_RW.
 * @param timeout Relative time in microseconds for the timeout
 * @return _UMP_OSU_ERR_OK if the lock was obtained, _UMP_OSU_ERR_TIMEOUT if the timeout expired or  _UMP_OSU_ERR_FAULT on error.
 */
_ump_osu_errcode_t _ump_osu_lock_timed_wait(_ump_osu_lock_t *lock, _ump_osu_lock_mode_t mode, u64 timeout);

/** @brief Test for a lock to be signalled and obtains the lock when so.
 *
 * Obtains the lock only when it is in signalled state. The lock is then
 * marked as unsignalled. The lock is released again by signalling
 * it by _ump_osu_lock_signal().
 *
 * If the lock could not be obtained immediately (that is, another thread
 * currently holds the lock), then this function \b does \b not wait for the
 * lock to be in a signalled state. Instead, an error code is immediately
 * returned to indicate that the thread could not obtain the lock.
 *
 * To prevent deadlock, locks must always be obtained in the same order.
 *
 * @param lock the lock to wait upon (obtain).
 * @param mode the mode in which the lock should be obtained. Currently this
 * must be _UMP_OSU_LOCKMODE_RW.
 * @return When the lock was obtained, _UMP_OSU_ERR_OK. If the lock could not
 * be obtained, _UMP_OSU_ERR_FAULT.
 */
_ump_osu_errcode_t _ump_osu_lock_trywait(_ump_osu_lock_t *lock, _ump_osu_lock_mode_t mode);

/** @brief Signal (release) a lock.
 *
 * Locks may only be signalled by the thread that originally waited upon the
 * lock, unless the lock was created using the _UMP_OSU_LOCKFLAG_ANYUNLOCK flag.
 *
 * @param lock the lock to signal (release).
 * @param mode the mode in which the lock should be obtained. This must match
 * the mode in which the lock was waited upon.
 */
void _ump_osu_lock_signal(_ump_osu_lock_t *lock, _ump_osu_lock_mode_t mode);

/** @brief Terminate a lock.
 *
 * This terminates a lock and frees all associated resources.
 *
 * It is a programming error to terminate the lock when it is held (unsignalled)
 * by a thread.
 *
 * @param lock the lock to terminate.
 */
void _ump_osu_lock_term(_ump_osu_lock_t *lock);
/** @} */ /* end group _ump_osu_lock */

/** @} */ /* end group osuapi */

/** @} */ /* end group uddapi */


#ifdef __cplusplus
}
#endif

#endif /* __UMP_OSU_H__ */
