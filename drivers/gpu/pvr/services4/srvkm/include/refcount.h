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

#ifndef __REFCOUNT_H__
#define __REFCOUNT_H__

#include "pvr_bridge_km.h"
#if defined(SUPPORT_ION)
#include "ion_sync.h"
#endif /* defined(SUPPORT_ION) */

#if defined(PVRSRV_REFCOUNT_DEBUG)

void PVRSRVDumpRefCountCCB(void);

#define PVRSRVKernelSyncInfoIncRef(x...) \
	PVRSRVKernelSyncInfoIncRef2(__FILE__, __LINE__, x)
#define PVRSRVKernelSyncInfoDecRef(x...) \
	PVRSRVKernelSyncInfoDecRef2(__FILE__, __LINE__, x)
#define PVRSRVKernelMemInfoIncRef(x...) \
	PVRSRVKernelMemInfoIncRef2(__FILE__, __LINE__, x)
#define PVRSRVKernelMemInfoDecRef(x...) \
	PVRSRVKernelMemInfoDecRef2(__FILE__, __LINE__, x)
#define PVRSRVBMBufIncRef(x...) \
	PVRSRVBMBufIncRef2(__FILE__, __LINE__, x)
#define PVRSRVBMBufDecRef(x...) \
	PVRSRVBMBufDecRef2(__FILE__, __LINE__, x)
#define PVRSRVBMBufIncExport(x...) \
	PVRSRVBMBufIncExport2(__FILE__, __LINE__, x)
#define PVRSRVBMBufDecExport(x...) \
	PVRSRVBMBufDecExport2(__FILE__, __LINE__, x)

void PVRSRVKernelSyncInfoIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
								 PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo,
								 PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo);
void PVRSRVKernelSyncInfoDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
								 PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo,
								 PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo);
void PVRSRVKernelMemInfoIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
								PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo);
void PVRSRVKernelMemInfoDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
								PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo);
void PVRSRVBMBufIncRef2(const IMG_CHAR *pszFile,
						IMG_INT iLine, BM_BUF *pBuf);
void PVRSRVBMBufDecRef2(const IMG_CHAR *pszFile,
						IMG_INT iLine, BM_BUF *pBuf);
void PVRSRVBMBufIncExport2(const IMG_CHAR *pszFile,
						   IMG_INT iLine, BM_BUF *pBuf);
void PVRSRVBMBufDecExport2(const IMG_CHAR *pszFile,
						   IMG_INT iLine, BM_BUF *pBuf);
void PVRSRVBMXProcIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
						  IMG_UINT32 ui32Index);
void PVRSRVBMXProcDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
						  IMG_UINT32 ui32Index);

#if defined(__linux__)

/* mmap refcounting is Linux specific */
#include "mmap.h"

#define PVRSRVOffsetStructIncRef(x...) \
	PVRSRVOffsetStructIncRef2(__FILE__, __LINE__, x)
#define PVRSRVOffsetStructDecRef(x...) \
	PVRSRVOffsetStructDecRef2(__FILE__, __LINE__, x)
#define PVRSRVOffsetStructIncMapped(x...) \
	PVRSRVOffsetStructIncMapped2(__FILE__, __LINE__, x)
#define PVRSRVOffsetStructDecMapped(x...) \
	PVRSRVOffsetStructDecMapped2(__FILE__, __LINE__, x)

void PVRSRVOffsetStructIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
							   PKV_OFFSET_STRUCT psOffsetStruct);
void PVRSRVOffsetStructDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
							   PKV_OFFSET_STRUCT psOffsetStruct);
void PVRSRVOffsetStructIncMapped2(const IMG_CHAR *pszFile, IMG_INT iLine,
								  PKV_OFFSET_STRUCT psOffsetStruct);
void PVRSRVOffsetStructDecMapped2(const IMG_CHAR *pszFile, IMG_INT iLine,
								  PKV_OFFSET_STRUCT psOffsetStruct);

#if defined(SUPPORT_ION)
#define PVRSRVIonBufferSyncInfoIncRef(x...) \
	PVRSRVIonBufferSyncInfoIncRef2(__FILE__, __LINE__, x)
#define PVRSRVIonBufferSyncInfoDecRef(x...) \
	PVRSRVIonBufferSyncInfoDecRef2(__FILE__, __LINE__, x)

PVRSRV_ERROR PVRSRVIonBufferSyncInfoIncRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
											IMG_HANDLE hUnique,
											IMG_HANDLE hDevCookie,
											IMG_HANDLE hDevMemContext,
											PVRSRV_ION_SYNC_INFO **ppsIonSyncInfo,
											PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo);
void PVRSRVIonBufferSyncInfoDecRef2(const IMG_CHAR *pszFile, IMG_INT iLine,
									PVRSRV_ION_SYNC_INFO *psIonSyncInfo,
									PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo);
#endif /* defined (SUPPORT_ION) */

#endif /* defined(__linux__) */

#else /* defined(PVRSRV_REFCOUNT_DEBUG) */

static INLINE void PVRSRVDumpRefCountCCB(void) { }

static INLINE void PVRSRVKernelSyncInfoIncRef(PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo,
								PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	PVR_UNREFERENCED_PARAMETER(psKernelMemInfo);
	PVRSRVAcquireSyncInfoKM(psKernelSyncInfo);
}

static INLINE void PVRSRVKernelSyncInfoDecRef(PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo,
								PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	PVR_UNREFERENCED_PARAMETER(psKernelMemInfo);
	PVRSRVReleaseSyncInfoKM(psKernelSyncInfo);
}

static INLINE void PVRSRVKernelMemInfoIncRef(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	psKernelMemInfo->ui32RefCount++;
}

static INLINE void PVRSRVKernelMemInfoDecRef(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	psKernelMemInfo->ui32RefCount--;
}

static INLINE void PVRSRVBMBufIncRef(BM_BUF *pBuf)
{
	pBuf->ui32RefCount++;
}

static INLINE void PVRSRVBMBufDecRef(BM_BUF *pBuf)
{
	pBuf->ui32RefCount--;
}

static INLINE void PVRSRVBMBufIncExport(BM_BUF *pBuf)
{
	pBuf->ui32ExportCount++;
}

static INLINE void PVRSRVBMBufDecExport(BM_BUF *pBuf)
{
	pBuf->ui32ExportCount--;
}

static INLINE void PVRSRVBMXProcIncRef(IMG_UINT32 ui32Index)
{
	gXProcWorkaroundShareData[ui32Index].ui32RefCount++;
}

static INLINE void PVRSRVBMXProcDecRef(IMG_UINT32 ui32Index)
{
	gXProcWorkaroundShareData[ui32Index].ui32RefCount--;
}

#if defined(__linux__)

/* mmap refcounting is Linux specific */
#include "mmap.h"

static INLINE void PVRSRVOffsetStructIncRef(PKV_OFFSET_STRUCT psOffsetStruct)
{
	psOffsetStruct->ui32RefCount++;
}

static INLINE void PVRSRVOffsetStructDecRef(PKV_OFFSET_STRUCT psOffsetStruct)
{
	psOffsetStruct->ui32RefCount--;
}

static INLINE void PVRSRVOffsetStructIncMapped(PKV_OFFSET_STRUCT psOffsetStruct)
{
	psOffsetStruct->ui32Mapped++;
}

static INLINE void PVRSRVOffsetStructDecMapped(PKV_OFFSET_STRUCT psOffsetStruct)
{
	psOffsetStruct->ui32Mapped--;
}

#if defined(SUPPORT_ION)
static INLINE PVRSRV_ERROR PVRSRVIonBufferSyncInfoIncRef(IMG_HANDLE hUnique,
														 IMG_HANDLE hDevCookie,
														 IMG_HANDLE hDevMemContext,
														 PVRSRV_ION_SYNC_INFO **ppsIonSyncInfo,
														 PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	PVR_UNREFERENCED_PARAMETER(psKernelMemInfo);

	return PVRSRVIonBufferSyncAcquire(hUnique,
									  hDevCookie,
									  hDevMemContext,
									  ppsIonSyncInfo);
}

static INLINE void PVRSRVIonBufferSyncInfoDecRef(PVRSRV_ION_SYNC_INFO *psIonSyncInfo,
										   PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	PVR_UNREFERENCED_PARAMETER(psKernelMemInfo);
	PVRSRVIonBufferSyncRelease(psIonSyncInfo);
}
#endif	/* defined (SUPPORT_ION) */

#endif /* defined(__linux__) */

#endif /* defined(PVRSRV_REFCOUNT_DEBUG) */

#endif /* __REFCOUNT_H__ */
