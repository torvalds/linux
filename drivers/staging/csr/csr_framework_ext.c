/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/module.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
#include <linux/slab.h>
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 19)
#include <linux/freezer.h>
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 27)
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif

#include <linux/bitops.h>

#include "csr_types.h"
#include "csr_framework_ext.h"
#include "csr_panic.h"

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMutexCreate
 *
 *  DESCRIPTION
 *      Create a mutex and return a handle to the created mutex.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS           in case of success
 *          CSR_FE_RESULT_NO_MORE_MUTEXES   in case of out of mutex resources
 *          CSR_FE_RESULT_INVALID_POINTER   in case the mutexHandle pointer is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrMutexCreate(CsrMutexHandle *mutexHandle)
{
    if (mutexHandle == NULL)
    {
        return CSR_FE_RESULT_INVALID_POINTER;
    }

    sema_init(mutexHandle, 1);

    return CSR_RESULT_SUCCESS;
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMutexDestroy
 *
 *  DESCRIPTION
 *      Destroy the previously created mutex.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrMutexDestroy(CsrMutexHandle *mutexHandle)
{
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMutexLock
 *
 *  DESCRIPTION
 *      Lock the mutex refered to by the provided handle.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS           in case of success
 *          CSR_FE_RESULT_INVALID_HANDLE    in case the mutexHandle is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrMutexLock(CsrMutexHandle *mutexHandle)
{
    if (mutexHandle == NULL)
    {
        return CSR_FE_RESULT_INVALID_POINTER;
    }

    if (down_interruptible(mutexHandle))
    {
        CsrPanic(CSR_TECH_FW, CSR_PANIC_FW_UNEXPECTED_VALUE, "CsrMutexLock Failed");
        return CSR_FE_RESULT_INVALID_POINTER;
    }

    return CSR_RESULT_SUCCESS;
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMutexUnlock
 *
 *  DESCRIPTION
 *      Unlock the mutex refered to by the provided handle.
 *
 *  RETURNS
 *      Possible values:
 *          CSR_RESULT_SUCCESS           in case of success
 *          CSR_FE_RESULT_INVALID_HANDLE    in case the mutexHandle is invalid
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrMutexUnlock(CsrMutexHandle *mutexHandle)
{
    if (mutexHandle == NULL)
    {
        return CSR_FE_RESULT_INVALID_POINTER;
    }

    up(mutexHandle);

    return CSR_RESULT_SUCCESS;
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrThreadSleep
 *
 *  DESCRIPTION
 *      Sleep for a given period.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrThreadSleep(u16 sleepTimeInMs)
{
    unsigned long t;

    /* Convert t in ms to jiffies and round up */
    t = ((sleepTimeInMs * HZ) + 999) / 1000;
    schedule_timeout_uninterruptible(t);
}
EXPORT_SYMBOL_GPL(CsrThreadSleep);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemCalloc
 *
 *  DESCRIPTION
 *      Allocate dynamic memory of a given size calculated as the
 *      numberOfElements times the elementSize.
 *
 *  RETURNS
 *      Pointer to allocated memory, or NULL in case of failure.
 *      Allocated memory is zero initialised.
 *
 *----------------------------------------------------------------------------*/
void *CsrMemCalloc(size_t numberOfElements, size_t elementSize)
{
    void *buf;
    size_t size;

    size = numberOfElements * elementSize;

    buf = kmalloc(size, GFP_KERNEL);
    if (buf != NULL)
    {
        memset(buf, 0, size);
    }

    return buf;
}

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemAlloc
 *
 *  DESCRIPTION
 *      Allocate dynamic memory of a given size.
 *
 *  RETURNS
 *      Pointer to allocated memory, or NULL in case of failure.
 *      Allocated memory is not initialised.
 *
 *----------------------------------------------------------------------------*/
void *CsrMemAlloc(size_t size)
{
    return kmalloc(size, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(CsrMemAlloc);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemFree
 *
 *  DESCRIPTION
 *      Free dynamic allocated memory.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrMemFree(void *pointer)
{
    kfree(pointer);
}
EXPORT_SYMBOL_GPL(CsrMemFree);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemAllocDma
 *
 *  DESCRIPTION
 *      Allocate DMA capable dynamic memory of a given size.
 *
 *  RETURNS
 *      Pointer to allocated memory, or NULL in case of failure.
 *      Allocated memory is not initialised.
 *
 *----------------------------------------------------------------------------*/
void *CsrMemAllocDma(size_t size)
{
    return kmalloc(size, GFP_KERNEL | GFP_DMA);
}
EXPORT_SYMBOL_GPL(CsrMemAllocDma);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrMemFreeDma
 *
 *  DESCRIPTION
 *      Free DMA capable dynamic allocated memory.
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
void CsrMemFreeDma(void *pointer)
{
    kfree(pointer);
}
EXPORT_SYMBOL_GPL(CsrMemFreeDma);
