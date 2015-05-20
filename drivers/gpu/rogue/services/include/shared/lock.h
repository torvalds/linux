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

#ifndef _LOCK_H_
#define _LOCK_H_

/* In Linux kernel mode we are using the kernel mutex implementation directly
 * with macros. This allows us to use the kernel lockdep feature for lock
 * debugging. */
#include "lock_types.h"

#if defined(LINUX) && defined(__KERNEL__)

#include <linux/mutex.h>
#include <linux/slab.h>

#define OSLockCreate(phLock, eLockType) ({ \
	PVRSRV_ERROR e = PVRSRV_ERROR_OUT_OF_MEMORY; \
	*(phLock) = kmalloc(sizeof(struct mutex), GFP_KERNEL); \
	if (*(phLock)) { mutex_init(*(phLock)); e = PVRSRV_OK; }; \
	e;})
#define OSLockDestroy(hLock) ({mutex_destroy((hLock)); kfree((hLock)); PVRSRV_OK;})

#define OSLockAcquire(hLock) ({mutex_lock((hLock)); PVRSRV_OK;})
#define OSLockAcquireNested(hLock, subclass) ({mutex_lock_nested((hLock), (subclass)); PVRSRV_OK;})
#define OSLockRelease(hLock) ({mutex_unlock((hLock)); PVRSRV_OK;})

#define OSLockIsLocked(hLock) ({IMG_BOOL b = ((mutex_is_locked((hLock)) == 1) ? IMG_TRUE : IMG_FALSE); b;})
#define OSTryLockAcquire(hLock) ({IMG_BOOL b = ((mutex_trylock(hLock) == 1) ? IMG_TRUE : IMG_FALSE); b;})


#else /* defined(LINUX) && defined(__KERNEL__) */

#include "img_types.h"
#include "pvrsrv_error.h"

IMG_INTERNAL
PVRSRV_ERROR OSLockCreate(POS_LOCK *phLock, LOCK_TYPE eLockType);

IMG_INTERNAL
IMG_VOID OSLockDestroy(POS_LOCK hLock);

IMG_INTERNAL
IMG_VOID OSLockAcquire(POS_LOCK hLock);

/* Nested notation isn't used in UM or other OS's */
#define OSLockAcquireNested(hLock, subclass) OSLockAcquire((hLock))

IMG_INTERNAL
IMG_VOID OSLockRelease(POS_LOCK hLock);

IMG_INTERNAL
IMG_BOOL OSLockIsLocked(POS_LOCK hLock);

#endif /* defined(LINUX) && defined(__KERNEL__) */

#endif	/* _LOCK_H_ */
