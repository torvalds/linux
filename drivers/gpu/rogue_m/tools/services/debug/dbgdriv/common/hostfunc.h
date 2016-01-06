/*************************************************************************/ /*!
@File
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

#ifndef _HOSTFUNC_
#define _HOSTFUNC_

/*****************************************************************************
 Defines
*****************************************************************************/
#define HOST_PAGESIZE			(4096)
#define DBG_MEMORY_INITIALIZER	(0xe2)

/*****************************************************************************
 Function prototypes
*****************************************************************************/
IMG_UINT32 HostReadRegistryDWORDFromString(IMG_CHAR *pcKey, IMG_CHAR *pcValueName, IMG_UINT32 *pui32Data);

IMG_VOID * HostPageablePageAlloc(IMG_UINT32 ui32Pages);
IMG_VOID HostPageablePageFree(IMG_VOID * pvBase);
IMG_VOID * HostNonPageablePageAlloc(IMG_UINT32 ui32Pages);
IMG_VOID HostNonPageablePageFree(IMG_VOID * pvBase);

IMG_VOID * HostMapKrnBufIntoUser(IMG_VOID * pvKrnAddr, IMG_UINT32 ui32Size, IMG_VOID * *ppvMdl);
IMG_VOID HostUnMapKrnBufFromUser(IMG_VOID * pvUserAddr, IMG_VOID * pvMdl, IMG_VOID * pvProcess);

IMG_VOID HostCreateRegDeclStreams(IMG_VOID);

/* Direct macros for Linux to avoid LockDep false-positives from occurring */
#if defined(LINUX) && defined(__KERNEL__)

#include <linux/mutex.h>
#include <linux/slab.h>

#define HostCreateMutex(IMG_VOID) ({ \
	struct mutex* pMutex = NULL; \
	pMutex = kmalloc(sizeof(struct mutex), GFP_KERNEL); \
	if (pMutex) { mutex_init(pMutex); }; \
	pMutex;})
#define HostDestroyMutex(hLock) ({mutex_destroy((hLock)); kfree((hLock)); PVRSRV_OK;})

#define HostAquireMutex(hLock) ({mutex_lock((hLock)); PVRSRV_OK;})
#define HostReleaseMutex(hLock) ({mutex_unlock((hLock)); PVRSRV_OK;})

#else /* defined(LINUX) && defined(__KERNEL__) */

IMG_VOID * HostCreateMutex(IMG_VOID);
IMG_VOID HostAquireMutex(IMG_VOID * pvMutex);
IMG_VOID HostReleaseMutex(IMG_VOID * pvMutex);
IMG_VOID HostDestroyMutex(IMG_VOID * pvMutex);

#endif

#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
IMG_INT32 HostCreateEventObjects(IMG_VOID);
IMG_VOID HostWaitForEvent(DBG_EVENT eEvent);
IMG_VOID HostSignalEvent(DBG_EVENT eEvent);
IMG_VOID HostDestroyEventObjects(IMG_VOID);
#endif	/*defined(SUPPORT_DBGDRV_EVENT_OBJECTS) */

#endif

/*****************************************************************************
 End of file (HOSTFUNC.H)
*****************************************************************************/
