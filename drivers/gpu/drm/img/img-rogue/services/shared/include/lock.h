/*************************************************************************/ /*!
@File           lock.h
@Title          Locking interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Services internal locking interface
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef LOCK_H
#define LOCK_H

/* In Linux kernel mode we are using the kernel mutex implementation directly
 * with macros. This allows us to use the kernel lockdep feature for lock
 * debugging. */
#include "lock_types.h"

#if defined(__linux__) && defined(__KERNEL__)

#include "allocmem.h"
#include <linux/atomic.h>

#define OSLockCreateNoStats(phLock) ({ \
	PVRSRV_ERROR e = PVRSRV_ERROR_OUT_OF_MEMORY; \
	*(phLock) = OSAllocMemNoStats(sizeof(struct mutex)); \
	if (*(phLock)) { mutex_init(*(phLock)); e = PVRSRV_OK; }; \
	e;})
#define OSLockCreate(phLock) ({ \
	PVRSRV_ERROR e = PVRSRV_ERROR_OUT_OF_MEMORY; \
	*(phLock) = OSAllocMem(sizeof(struct mutex)); \
	if (*(phLock)) { mutex_init(*(phLock)); e = PVRSRV_OK; }; \
	e;})
#define OSLockDestroy(hLock) ({mutex_destroy((hLock)); OSFreeMem((hLock)); PVRSRV_OK;})
#define OSLockDestroyNoStats(hLock) ({mutex_destroy((hLock)); OSFreeMemNoStats((hLock)); PVRSRV_OK;})

#define OSLockAcquire(hLock) ({mutex_lock((hLock)); PVRSRV_OK;})
#define OSLockAcquireNested(hLock, subclass) ({mutex_lock_nested((hLock), (subclass)); PVRSRV_OK;})
#define OSLockRelease(hLock) ({mutex_unlock((hLock)); PVRSRV_OK;})

#define OSLockIsLocked(hLock) ((mutex_is_locked((hLock)) == 1) ? IMG_TRUE : IMG_FALSE)
#define OSTryLockAcquire(hLock) ((mutex_trylock(hLock) == 1) ? IMG_TRUE : IMG_FALSE)

#define OSSpinLockCreate(_ppsLock) ({ \
	PVRSRV_ERROR e = PVRSRV_ERROR_OUT_OF_MEMORY; \
	*(_ppsLock) = OSAllocMem(sizeof(spinlock_t)); \
	if (*(_ppsLock)) {spin_lock_init(*(_ppsLock)); e = PVRSRV_OK;} \
	e;})
#define OSSpinLockDestroy(_psLock) ({OSFreeMem(_psLock);})

typedef unsigned long OS_SPINLOCK_FLAGS;
#define OSSpinLockAcquire(_pLock, _flags) spin_lock_irqsave(_pLock, _flags)
#define OSSpinLockRelease(_pLock, _flags) spin_unlock_irqrestore(_pLock, _flags)

/* These _may_ be reordered or optimized away entirely by the compiler/hw */
#define OSAtomicRead(pCounter)	atomic_read(pCounter)
#define OSAtomicWrite(pCounter, i)	atomic_set(pCounter, i)

/* The following atomic operations, in addition to being SMP-safe, also
   imply a memory barrier around the operation  */
#define OSAtomicIncrement(pCounter) atomic_inc_return(pCounter)
#define OSAtomicDecrement(pCounter) atomic_dec_return(pCounter)
#define OSAtomicCompareExchange(pCounter, oldv, newv) atomic_cmpxchg(pCounter,oldv,newv)
#define OSAtomicExchange(pCounter, iNewVal) atomic_xchg(pCounter, iNewVal)

static inline IMG_INT OSAtomicOr(ATOMIC_T *pCounter, IMG_INT iVal)
{
	IMG_INT iOldVal, iLastVal, iNewVal;

	iLastVal = OSAtomicRead(pCounter);
	do
	{
		iOldVal = iLastVal;
		iNewVal = iOldVal | iVal;

		iLastVal = OSAtomicCompareExchange(pCounter, iOldVal, iNewVal);
	}
	while (iOldVal != iLastVal);

	return iNewVal;
}

#define OSAtomicAdd(pCounter, incr) atomic_add_return(incr,pCounter)
#define OSAtomicAddUnless(pCounter, incr, test) __atomic_add_unless(pCounter,incr,test)

#define OSAtomicSubtract(pCounter, incr) atomic_add_return(-(incr),pCounter)
#define OSAtomicSubtractUnless(pCounter, incr, test) OSAtomicAddUnless(pCounter, -(incr), test)

#else /* defined(__linux__) && defined(__KERNEL__) */

#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"

/**************************************************************************/ /*!
@Function       OSLockCreate
@Description    Creates an operating system lock object.
@Output         phLock           The created lock.
@Return         PVRSRV_OK on success. PVRSRV_ERROR_OUT_OF_MEMORY if the driver
                cannot allocate CPU memory needed for the lock.
                PVRSRV_ERROR_INIT_FAILURE if the Operating System fails to
                allocate the lock.
 */ /**************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR OSLockCreate(POS_LOCK *phLock);
#if defined(INTEGRITY_OS)
#define OSLockCreateNoStats OSLockCreate
#endif

/**************************************************************************/ /*!
@Function       OSLockDestroy
@Description    Destroys an operating system lock object.
@Input          hLock            The lock to be destroyed.
@Return         None.
 */ /**************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR OSLockDestroy(POS_LOCK hLock);

#if defined(INTEGRITY_OS)
#define OSLockDestroyNoStats OSLockDestroy
#endif
/**************************************************************************/ /*!
@Function       OSLockAcquire
@Description    Acquires an operating system lock.
                NB. This function must not return until the lock is acquired
                (meaning the implementation should not timeout or return with
                an error, as the caller will assume they have the lock).
@Input          hLock            The lock to be acquired.
@Return         None.
 */ /**************************************************************************/
IMG_INTERNAL
void OSLockAcquire(POS_LOCK hLock);

/**************************************************************************/ /*!
@Function       OSTryLockAcquire
@Description    Try to acquire an operating system lock.
                NB. If lock is acquired successfully in the first attempt,
                then the function returns true and else it will return false.
@Input          hLock            The lock to be acquired.
@Return         IMG_TRUE if lock acquired successfully,
                IMG_FALSE otherwise.
 */ /**************************************************************************/
IMG_INTERNAL
IMG_BOOL OSTryLockAcquire(POS_LOCK hLock);

/* Nested notation isn't used in UM or other OS's */
/**************************************************************************/ /*!
@Function       OSLockAcquireNested
@Description    For operating systems other than Linux, this equates to an
                OSLockAcquire() call. On Linux, this function wraps a call
                to mutex_lock_nested(). This recognises the scenario where
                there may be multiple subclasses within a particular class
                of lock. In such cases, the order in which the locks belonging
                these various subclasses are acquired is important and must be
                validated.
@Input          hLock            The lock to be acquired.
@Input          subclass         The subclass of the lock.
@Return         None.
 */ /**************************************************************************/
#define OSLockAcquireNested(hLock, subclass) OSLockAcquire((hLock))

/**************************************************************************/ /*!
@Function       OSLockRelease
@Description    Releases an operating system lock.
@Input          hLock            The lock to be released.
@Return         None.
 */ /**************************************************************************/
IMG_INTERNAL
void OSLockRelease(POS_LOCK hLock);

/**************************************************************************/ /*!
@Function       OSLockIsLocked
@Description    Tests whether or not an operating system lock is currently
                locked.
@Input          hLock            The lock to be tested.
@Return         IMG_TRUE if locked, IMG_FALSE if not locked.
 */ /**************************************************************************/
IMG_INTERNAL
IMG_BOOL OSLockIsLocked(POS_LOCK hLock);

#if defined(__linux__)

/* Use GCC intrinsics (read/write semantics consistent with kernel-side implementation) */
#define OSAtomicRead(pCounter) (*(volatile IMG_INT32 *)&(pCounter)->counter)
#define OSAtomicWrite(pCounter, i) ((pCounter)->counter = (IMG_INT32) i)
#define OSAtomicIncrement(pCounter) __sync_add_and_fetch((&(pCounter)->counter), 1)
#define OSAtomicDecrement(pCounter) __sync_sub_and_fetch((&(pCounter)->counter), 1)
#define OSAtomicCompareExchange(pCounter, oldv, newv) \
	__sync_val_compare_and_swap((&(pCounter)->counter), oldv, newv)
#define OSAtomicOr(pCounter, iVal) __sync_or_and_fetch((&(pCounter)->counter), iVal)

static inline IMG_UINT32 OSAtomicExchange(ATOMIC_T *pCounter, IMG_UINT32 iNewVal)
{
	IMG_UINT32 iOldVal;
	IMG_UINT32 iLastVal;

	iLastVal = OSAtomicRead(pCounter);
	do
	{
		iOldVal = iLastVal;
		iLastVal = OSAtomicCompareExchange(pCounter, iOldVal, iNewVal);
	}
	while (iOldVal != iLastVal);

	return iOldVal;
}

#define OSAtomicAdd(pCounter, incr) __sync_add_and_fetch((&(pCounter)->counter), incr)
#define OSAtomicAddUnless(pCounter, incr, test) ({ \
	IMG_INT32 c; IMG_INT32 old; \
	c = OSAtomicRead(pCounter); \
	while (1) { \
		if (c == (test)) break; \
		old = OSAtomicCompareExchange(pCounter, c, c+(incr)); \
		if (old == c) break; \
		c = old; \
	} c; })

#define OSAtomicSubtract(pCounter, incr) OSAtomicAdd(pCounter, -(incr))
#define OSAtomicSubtractUnless(pCounter, incr, test) OSAtomicAddUnless(pCounter, -(incr), test)

#else

/*************************************************************************/ /*!
@Function       OSAtomicRead
@Description    Read the value of a variable atomically.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to read
@Return         The value of the atomic variable
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicRead(const ATOMIC_T *pCounter);

/*************************************************************************/ /*!
@Function       OSAtomicWrite
@Description    Write the value of a variable atomically.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to be written to
@Input          v               The value to write
@Return         None
*/ /**************************************************************************/
IMG_INTERNAL
void OSAtomicWrite(ATOMIC_T *pCounter, IMG_INT32 v);

/* For the following atomic operations, in addition to being SMP-safe,
   should also  have a memory barrier around each operation  */
/*************************************************************************/ /*!
@Function       OSAtomicIncrement
@Description    Increment the value of a variable atomically.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to be incremented
@Return         The new value of *pCounter.
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicIncrement(ATOMIC_T *pCounter);

/*************************************************************************/ /*!
@Function       OSAtomicDecrement
@Description    Decrement the value of a variable atomically.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to be decremented
@Return         The new value of *pCounter.
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicDecrement(ATOMIC_T *pCounter);

/*************************************************************************/ /*!
@Function       OSAtomicAdd
@Description    Add a specified value to a variable atomically.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to add the value to
@Input          v               The value to be added
@Return         The new value of *pCounter.
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicAdd(ATOMIC_T *pCounter, IMG_INT32 v);

/*************************************************************************/ /*!
@Function       OSAtomicAddUnless
@Description    Add a specified value to a variable atomically unless it
                already equals a particular value.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to add the value to
@Input          v               The value to be added to 'pCounter'
@Input          t               The test value. If 'pCounter' equals this,
                                its value will not be adjusted
@Return         The old value of *pCounter.
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicAddUnless(ATOMIC_T *pCounter, IMG_INT32 v, IMG_INT32 t);

/*************************************************************************/ /*!
@Function       OSAtomicSubtract
@Description    Subtract a specified value to a variable atomically.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to subtract the value from
@Input          v               The value to be subtracted
@Return         The new value of *pCounter.
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicSubtract(ATOMIC_T *pCounter, IMG_INT32 v);

/*************************************************************************/ /*!
@Function       OSAtomicSubtractUnless
@Description    Subtract a specified value from a variable atomically unless
                it already equals a particular value.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to subtract the value from
@Input          v               The value to be subtracted from 'pCounter'
@Input          t               The test value. If 'pCounter' equals this,
                                its value will not be adjusted
@Return         The old value of *pCounter.
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicSubtractUnless(ATOMIC_T *pCounter, IMG_INT32 v, IMG_INT32 t);

/*************************************************************************/ /*!
@Function       OSAtomicCompareExchange
@Description    Set a variable to a given value only if it is currently
                equal to a specified value. The whole operation must be atomic.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to be checked and
                                possibly updated
@Input          oldv            The value the atomic variable must have in
                                order to be modified
@Input          newv            The value to write to the atomic variable if
                                it equals 'oldv'
@Return         The old value of *pCounter
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicCompareExchange(ATOMIC_T *pCounter, IMG_INT32 oldv, IMG_INT32 newv);

/*************************************************************************/ /*!
@Function       OSAtomicExchange
@Description    Set a variable to a given value and retrieve previous value.
                The whole operation must be atomic.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to be updated
@Input          iNewVal         The value to write to the atomic variable
@Return         The previous value of *pCounter.
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicExchange(ATOMIC_T *pCounter, IMG_INT32 iNewVal);

/*************************************************************************/ /*!
@Function       OSAtomicOr
@Description    Set a variable to the bitwise or of its current value and the
                specified value. Equivalent to *pCounter |= iVal.
                The whole operation must be atomic.
                Atomic functions must be implemented in a manner that
                is both symmetric multiprocessor (SMP) safe and has a memory
                barrier around each operation.
@Input          pCounter        The atomic variable to be updated
@Input          iVal            The value to bitwise or against
@Return         The new value of *pCounter.
*/ /**************************************************************************/
IMG_INTERNAL
IMG_INT32 OSAtomicOr(ATOMIC_T *pCounter, IMG_INT32 iVal);

/* For now, spin-locks are required on Linux only, so other platforms fake
 * spinlocks with normal mutex locks */
typedef unsigned long OS_SPINLOCK_FLAGS;
#define POS_SPINLOCK POS_LOCK
#define OSSpinLockCreate(ppLock) OSLockCreate(ppLock)
#define OSSpinLockDestroy(pLock) OSLockDestroy(pLock)
#define OSSpinLockAcquire(pLock, flags) {flags = 0; OSLockAcquire(pLock);}
#define OSSpinLockRelease(pLock, flags) {flags = 0; OSLockRelease(pLock);}

#endif /* defined(__linux__) */
#endif /* defined(__linux__) && defined(__KERNEL__) */

#endif	/* LOCK_H */
