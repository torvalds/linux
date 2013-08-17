/*************************************************************************/ /*!
@Title          Linux mutex interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include <linux/version.h>
#include <linux/errno.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
#include <linux/mutex.h>
#else
#include <asm/semaphore.h>
#endif
#include <linux/module.h>

#include <img_defs.h>
#include <services.h>

#include "mutex.h"


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))

IMG_VOID LinuxInitMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    mutex_init(psPVRSRVMutex);
}

IMG_VOID LinuxLockMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    mutex_lock(psPVRSRVMutex);
}

PVRSRV_ERROR LinuxLockMutexInterruptible(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    if(mutex_lock_interruptible(psPVRSRVMutex) == -EINTR)
    {
        return PVRSRV_ERROR_MUTEX_INTERRUPTIBLE_ERROR;
    }
    else
    {
        return PVRSRV_OK;
    }
}

IMG_INT32 LinuxTryLockMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    return mutex_trylock(psPVRSRVMutex);
}

IMG_VOID LinuxUnLockMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    mutex_unlock(psPVRSRVMutex);
}

IMG_BOOL LinuxIsLockedMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    return (mutex_is_locked(psPVRSRVMutex)) ? IMG_TRUE : IMG_FALSE;
}


#else /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)) */


IMG_VOID LinuxInitMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    init_MUTEX(&psPVRSRVMutex->sSemaphore);
    atomic_set(&psPVRSRVMutex->Count, 0);
}

IMG_VOID LinuxLockMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    down(&psPVRSRVMutex->sSemaphore);
    atomic_dec(&psPVRSRVMutex->Count);
}

PVRSRV_ERROR LinuxLockMutexInterruptible(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    if(down_interruptible(&psPVRSRVMutex->sSemaphore) == -EINTR)
    {
        /* The process was sent a signal while waiting for the semaphore
         * (e.g. a kill signal from userspace)
         */
        return PVRSRV_ERROR_MUTEX_INTERRUPTIBLE_ERROR;
    }else{
        atomic_dec(&psPVRSRVMutex->Count);
        return PVRSRV_OK;
    }
}

IMG_INT32 LinuxTryLockMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    IMG_INT32 Status = down_trylock(&psPVRSRVMutex->sSemaphore);
    if(Status == 0)
    {
        atomic_dec(&psPVRSRVMutex->Count);
    }

    return Status == 0;
}

IMG_VOID LinuxUnLockMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    atomic_inc(&psPVRSRVMutex->Count);
    up(&psPVRSRVMutex->sSemaphore);
}

IMG_BOOL LinuxIsLockedMutex(PVRSRV_LINUX_MUTEX *psPVRSRVMutex)
{
    IMG_INT32 iCount;
    
    iCount = atomic_read(&psPVRSRVMutex->Count);

    return (IMG_BOOL)iCount;
}

#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)) */

