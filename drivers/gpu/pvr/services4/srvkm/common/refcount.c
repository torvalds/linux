/*************************************************************************/ /*!
@Title          Services reference count debugging
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

#if defined(PVRSRV_REFCOUNT_DEBUG)

#include "services_headers.h"

#ifndef __linux__
#warning Reference count debugging is not thread-safe on this platform
#define PVRSRV_LOCK_CCB()
#define PVRSRV_UNLOCK_CCB()
#else /* __linux__ */
#include <linux/spinlock.h>
static DEFINE_SPINLOCK(gsCCBLock);
#define PVRSRV_LOCK_CCB() \
	{ \
		unsigned long flags; \
		spin_lock_irqsave(&gsCCBLock, flags);
#define PVRSRV_UNLOCK_CCB()	\
		spin_unlock_irqrestore(&gsCCBLock, flags); \
	}
#endif /* __linux__ */

#define PVRSRV_REFCOUNT_CCB_MAX			512
#define PVRSRV_REFCOUNT_CCB_MESG_MAX	80

#define PVRSRV_REFCOUNT_CCB_DEBUG_SYNCINFO	(1U << 0)
#define PVRSRV_REFCOUNT_CCB_DEBUG_MEMINFO	(1U << 1)
#define PVRSRV_REFCOUNT_CCB_DEBUG_BM_BUF	(1U << 2)
#define PVRSRV_REFCOUNT_CCB_DEBUG_BM_BUF2	(1U << 3)
#define PVRSRV_REFCOUNT_CCB_DEBUG_BM_XPROC	(1U << 4)

#if defined(__linux__)
#define PVRSRV_REFCOUNT_CCB_DEBUG_MMAP		(1U << 16)
#define PVRSRV_REFCOUNT_CCB_DEBUG_MMAP2		(1U << 17)
#define PVRSRV_REFCOUNT_CCB_DEBUG_ION_SYNC	(1U << 18)
#else
#define PVRSRV_REFCOUNT_CCB_DEBUG_MMAP		0
#define PVRSRV_REFCOUNT_CCB_DEBUG_MMAP2		0
#define PVRSRV_REFCOUNT_CCB_DEBUG_ION_SYNC	0
#endif

#define PVRSRV_REFCOUNT_CCB_DEBUG_ALL		~0U

/*static const IMG_UINT guiDebugMask = PVRSRV_REFCOUNT_CCB_DEBUG_ALL;*/
static const IMG_UINT guiDebugMask =
	PVRSRV_REFCOUNT_CCB_DEBUG_SYNCINFO |
#if defined(SUPPORT_ION)
	PVRSRV_REFCOUNT_CCB_DEBUG_ION_SYNC |
#endif
	PVRSRV_REFCOUNT_CCB_DEBUG_MMAP2;

typedef struct
{
	const IMG_CHAR *pszFile;
	IMG_INT iLine;
	IMG_UINT32 ui32PID;
	IMG_CHAR pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX];
}
PVRSRV_REFCOUNT_CCB;

static PVRSRV_REFCOUNT_CCB gsRefCountCCB[PVRSRV_REFCOUNT_CCB_MAX];
static IMG_UINT giOffset;

static const IMG_CHAR gszHeader[] =
	/*        10        20        30        40        50        60        70
	 * 345678901234567890123456789012345678901234567890123456789012345678901
	 */
	"TYPE     SYNCINFO MEMINFO  MEMHANDLE OTHER    REF  REF' SIZE     PID";
	/* NCINFO deadbeef deadbeef deadbeef  deadbeef 1234 1234 deadbeef */

#define PVRSRV_REFCOUNT_CCB_FMT_STRING "%8.8s %8p %8p %8p  %8p %.4d %.4d %.8x"

IMG_INTERNAL
void PVRSRVDumpRefCountCCB(void)
{
	int i;

	PVRSRV_LOCK_CCB();

	PVR_LOG(("%s", gszHeader));

	for(i = 0; i < PVRSRV_REFCOUNT_CCB_MAX; i++)
	{
		PVRSRV_REFCOUNT_CCB *psRefCountCCBEntry =
			&gsRefCountCCB[(giOffset + i) % PVRSRV_REFCOUNT_CCB_MAX];

		/* Early on, we won't have MAX_REFCOUNT_CCB_SIZE messages */
		if(!psRefCountCCBEntry->pszFile)
			continue;

		PVR_LOG(("%s %d %s:%d", psRefCountCCBEntry->pcMesg,
								psRefCountCCBEntry->ui32PID,
								psRefCountCCBEntry->pszFile,
								psRefCountCCBEntry->iLine));
	}

	PVRSRV_UNLOCK_CCB();
}

IMG_INTERNAL
void PVRSRVKernelSyncInfoIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
								 PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo,
								 PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	IMG_UINT32 ui32RefValue = OSAtomicRead(psKernelSyncInfo->pvRefCount);

	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_SYNCINFO))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "SYNCINFO",
			 psKernelSyncInfo,
			 psKernelMemInfo,
			 NULL,
			 (psKernelMemInfo) ? psKernelMemInfo->sMemBlk.hOSMemHandle : NULL,
			 ui32RefValue,
			 ui32RefValue + 1,
			 (psKernelMemInfo) ? psKernelMemInfo->uAllocSize : 0);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	PVRSRVAcquireSyncInfoKM(psKernelSyncInfo);
}

IMG_INTERNAL
void PVRSRVKernelSyncInfoDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
								 PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo,
								 PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	IMG_UINT32 ui32RefValue = OSAtomicRead(psKernelSyncInfo->pvRefCount);

	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_SYNCINFO))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "SYNCINFO",
			 psKernelSyncInfo,
			 psKernelMemInfo,
			 (psKernelMemInfo) ? psKernelMemInfo->sMemBlk.hOSMemHandle : NULL,
			 NULL,
			 ui32RefValue,
			 ui32RefValue - 1,
			 (psKernelMemInfo) ? psKernelMemInfo->uAllocSize : 0);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	PVRSRVReleaseSyncInfoKM(psKernelSyncInfo);
}

IMG_INTERNAL
void PVRSRVKernelMemInfoIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
								PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_MEMINFO))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "MEMINFO",
			 psKernelMemInfo->psKernelSyncInfo,
			 psKernelMemInfo,
			 psKernelMemInfo->sMemBlk.hOSMemHandle,
			 NULL,
			 psKernelMemInfo->ui32RefCount,
			 psKernelMemInfo->ui32RefCount + 1,
			 psKernelMemInfo->uAllocSize);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	psKernelMemInfo->ui32RefCount++;
}

IMG_INTERNAL
void PVRSRVKernelMemInfoDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
								PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_MEMINFO))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "MEMINFO",
			 psKernelMemInfo->psKernelSyncInfo,
			 psKernelMemInfo,
			 psKernelMemInfo->sMemBlk.hOSMemHandle,
			 NULL,
			 psKernelMemInfo->ui32RefCount,
			 psKernelMemInfo->ui32RefCount - 1,
			 psKernelMemInfo->uAllocSize);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	psKernelMemInfo->ui32RefCount--;
}

IMG_INTERNAL
void PVRSRVBMBufIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine, BM_BUF *pBuf)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_BM_BUF))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "BM_BUF",
			 NULL,
			 NULL,
			 BM_HandleToOSMemHandle(pBuf),
			 pBuf,
			 pBuf->ui32RefCount,
			 pBuf->ui32RefCount + 1,
			 (pBuf->pMapping) ? pBuf->pMapping->uSize : 0);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	pBuf->ui32RefCount++;
}

IMG_INTERNAL
void PVRSRVBMBufDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine, BM_BUF *pBuf)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_BM_BUF))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "BM_BUF",
			 NULL,
			 NULL,
			 BM_HandleToOSMemHandle(pBuf),
			 pBuf,
			 pBuf->ui32RefCount,
			 pBuf->ui32RefCount - 1,
			 (pBuf->pMapping) ? pBuf->pMapping->uSize : 0);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	pBuf->ui32RefCount--;
}

IMG_INTERNAL
void PVRSRVBMBufIncExport2(const IMG_CHAR *pszFile, IMG_INT iLine, BM_BUF *pBuf)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_BM_BUF2))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "BM_BUF2",
			 NULL,
			 NULL,
			 BM_HandleToOSMemHandle(pBuf),
			 pBuf,
			 pBuf->ui32ExportCount,
			 pBuf->ui32ExportCount + 1,
			 (pBuf->pMapping) ? pBuf->pMapping->uSize : 0);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	pBuf->ui32ExportCount++;
}

IMG_INTERNAL
void PVRSRVBMBufDecExport2(const IMG_CHAR *pszFile, IMG_INT iLine, BM_BUF *pBuf)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_BM_BUF2))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "BM_BUF2",
			 NULL,
			 NULL,
			 BM_HandleToOSMemHandle(pBuf),
			 pBuf,
			 pBuf->ui32ExportCount,
			 pBuf->ui32ExportCount - 1,
			 (pBuf->pMapping) ? pBuf->pMapping->uSize : 0);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	pBuf->ui32ExportCount--;
}

IMG_INTERNAL
void PVRSRVBMXProcIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine, IMG_UINT32 ui32Index)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_BM_XPROC))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "BM_XPROC",
			 NULL,
			 NULL,
			 gXProcWorkaroundShareData[ui32Index].hOSMemHandle,
			 (IMG_VOID *) ui32Index,
			 gXProcWorkaroundShareData[ui32Index].ui32RefCount,
			 gXProcWorkaroundShareData[ui32Index].ui32RefCount + 1,
			 gXProcWorkaroundShareData[ui32Index].ui32Size);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	gXProcWorkaroundShareData[ui32Index].ui32RefCount++;
}

IMG_INTERNAL
void PVRSRVBMXProcDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine, IMG_UINT32 ui32Index)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_BM_XPROC))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "BM_XPROC",
			 NULL,
			 NULL,
			 gXProcWorkaroundShareData[ui32Index].hOSMemHandle,
			 (IMG_VOID *) ui32Index,
			 gXProcWorkaroundShareData[ui32Index].ui32RefCount,
			 gXProcWorkaroundShareData[ui32Index].ui32RefCount - 1,
			 gXProcWorkaroundShareData[ui32Index].ui32Size);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	gXProcWorkaroundShareData[ui32Index].ui32RefCount--;
}

#if defined(__linux__)

/* mmap refcounting is Linux specific */

IMG_INTERNAL
void PVRSRVOffsetStructIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
							   PKV_OFFSET_STRUCT psOffsetStruct)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_MMAP))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "MMAP",
			 NULL,
			 NULL,
			 psOffsetStruct->psLinuxMemArea,
			 psOffsetStruct,
			 psOffsetStruct->ui32RefCount,
			 psOffsetStruct->ui32RefCount + 1,
			 psOffsetStruct->uiRealByteSize);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	psOffsetStruct->ui32RefCount++;
}

IMG_INTERNAL
void PVRSRVOffsetStructDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
							   PKV_OFFSET_STRUCT psOffsetStruct)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_MMAP))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "MMAP",
			 NULL,
			 NULL,
			 psOffsetStruct->psLinuxMemArea,
			 psOffsetStruct,
			 psOffsetStruct->ui32RefCount,
			 psOffsetStruct->ui32RefCount - 1,
			 psOffsetStruct->uiRealByteSize);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	psOffsetStruct->ui32RefCount--;
}

IMG_INTERNAL
void PVRSRVOffsetStructIncMapped2(const IMG_CHAR *pszFile, IMG_INT iLine,
								  PKV_OFFSET_STRUCT psOffsetStruct)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_MMAP2))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "MMAP2",
			 NULL,
			 NULL,
			 psOffsetStruct->psLinuxMemArea,
			 psOffsetStruct,
			 psOffsetStruct->ui32Mapped,
			 psOffsetStruct->ui32Mapped + 1,
			 psOffsetStruct->uiRealByteSize);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	psOffsetStruct->ui32Mapped++;
}

IMG_INTERNAL
void PVRSRVOffsetStructDecMapped2(const IMG_CHAR *pszFile, IMG_INT iLine,
								  PKV_OFFSET_STRUCT psOffsetStruct)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_MMAP2))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "MMAP2",
			 NULL,
			 NULL,
			 psOffsetStruct->psLinuxMemArea,
			 psOffsetStruct,
			 psOffsetStruct->ui32Mapped,
			 psOffsetStruct->ui32Mapped - 1,
			 psOffsetStruct->uiRealByteSize);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();

skip:
	psOffsetStruct->ui32Mapped--;
}

#if defined(SUPPORT_ION)
PVRSRV_ERROR PVRSRVIonBufferSyncInfoIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
											IMG_HANDLE hUnique,
											IMG_HANDLE hDevCookie,
											IMG_HANDLE hDevMemContext,
											PVRSRV_ION_SYNC_INFO **ppsIonSyncInfo,
											PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	PVRSRV_ERROR eError;

	/*
		We have to do the call 1st as we need to Ion syninfo which it returns
	*/
	eError = PVRSRVIonBufferSyncAcquire(hUnique,
										hDevCookie,
										hDevMemContext,
										ppsIonSyncInfo);

	if (eError == PVRSRV_OK)
	{
		if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_ION_SYNC))
			goto skip;

		PVRSRV_LOCK_CCB();

		gsRefCountCCB[giOffset].pszFile = pszFile;
		gsRefCountCCB[giOffset].iLine = iLine;
		gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
		snprintf(gsRefCountCCB[giOffset].pcMesg,
				 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
				 PVRSRV_REFCOUNT_CCB_FMT_STRING,
				 "ION_SYNC",
				 (*ppsIonSyncInfo)->psSyncInfo,
				 psKernelMemInfo,
				 NULL,
				 *ppsIonSyncInfo,
				 (*ppsIonSyncInfo)->ui32RefCount - 1,
				 (*ppsIonSyncInfo)->ui32RefCount,
				 0);
		gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
		giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

		PVRSRV_UNLOCK_CCB();
	}

skip:
	return eError;
}

void PVRSRVIonBufferSyncInfoDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
									PVRSRV_ION_SYNC_INFO *psIonSyncInfo,
									PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	if(!(guiDebugMask & PVRSRV_REFCOUNT_CCB_DEBUG_ION_SYNC))
		goto skip;

	PVRSRV_LOCK_CCB();

	gsRefCountCCB[giOffset].pszFile = pszFile;
	gsRefCountCCB[giOffset].iLine = iLine;
	gsRefCountCCB[giOffset].ui32PID = OSGetCurrentProcessIDKM();
	snprintf(gsRefCountCCB[giOffset].pcMesg,
			 PVRSRV_REFCOUNT_CCB_MESG_MAX - 1,
			 PVRSRV_REFCOUNT_CCB_FMT_STRING,
			 "ION_SYNC",
			 psIonSyncInfo->psSyncInfo,
			 psKernelMemInfo,
			 NULL,
			 psIonSyncInfo,
			 psIonSyncInfo->ui32RefCount,
			 psIonSyncInfo->ui32RefCount - 1,
			 0);
	gsRefCountCCB[giOffset].pcMesg[PVRSRV_REFCOUNT_CCB_MESG_MAX - 1] = 0;
	giOffset = (giOffset + 1) % PVRSRV_REFCOUNT_CCB_MAX;

	PVRSRV_UNLOCK_CCB();
skip:
	PVRSRVIonBufferSyncRelease(psIonSyncInfo);
}

#endif /* defined (SUPPORT_ION) */

#endif /* defined(__linux__) */

#endif /* defined(PVRSRV_REFCOUNT_DEBUG) */
