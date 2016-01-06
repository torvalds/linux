/**************************************************************************/ /*!
@File
@Title          pdump functions to assist with physmem allocations
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements basic low level control of MMU.
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
*/ /***************************************************************************/

#ifndef SRVSRV_PDUMP_PHYSMEM_H
#define SRVSRV_PDUMP_PHYSMEM_H

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pmr.h"


typedef struct _PDUMP_PHYSMEM_INFO_T_ PDUMP_PHYSMEM_INFO_T;

#if defined(PDUMP)
extern PVRSRV_ERROR
PDumpPMRMalloc(const IMG_CHAR *pszDevSpace,
               const IMG_CHAR *pszSymbolicAddress,
               IMG_UINT64 ui64Size,
               /* alignment is alignment of start of buffer _and_
                  minimum contiguity - i.e. smallest allowable
                  page-size. */
               IMG_DEVMEM_ALIGN_T uiAlign,
               IMG_BOOL bForcePersistent,
               IMG_HANDLE *phHandlePtr);

IMG_INTERNAL IMG_VOID
PDumpPMRMallocPMR(const PMR *psPMR,
                  IMG_DEVMEM_SIZE_T uiSize,
                  IMG_DEVMEM_ALIGN_T uiBlockSize,
                  IMG_BOOL bForcePersistent,
                  IMG_HANDLE *phPDumpAllocInfoPtr);

extern
PVRSRV_ERROR PDumpPMRFree(IMG_HANDLE hPDumpAllocationInfoHandle);
#else	/* PDUMP */

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimPDumpPolKM)
#endif
static INLINE PVRSRV_ERROR
PDumpPMRMalloc(const IMG_CHAR *pszDevSpace,
               const IMG_CHAR *pszSymbolicAddress,
               IMG_UINT64 ui64Size,
               IMG_DEVMEM_ALIGN_T uiAlign,
               IMG_BOOL bForcePersistent,
               IMG_HANDLE *phHandlePtr)
{
	PVR_UNREFERENCED_PARAMETER(pszDevSpace);
	PVR_UNREFERENCED_PARAMETER(pszSymbolicAddress);
	PVR_UNREFERENCED_PARAMETER(ui64Size);
	PVR_UNREFERENCED_PARAMETER(uiAlign);
	PVR_UNREFERENCED_PARAMETER(bForcePersistent);
	PVR_UNREFERENCED_PARAMETER(phHandlePtr);
	PVR_UNREFERENCED_PARAMETER(bForcePersistent);
	return PVRSRV_OK;
}

static INLINE IMG_VOID
PDumpPMRMallocPMR(const PMR *psPMR,
                  IMG_DEVMEM_SIZE_T uiSize,
                  IMG_DEVMEM_ALIGN_T uiBlockSize,
                  IMG_BOOL bForcePersistent,
                  IMG_HANDLE *phPDumpAllocInfoPtr)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiBlockSize);
	PVR_UNREFERENCED_PARAMETER(bForcePersistent);
	PVR_UNREFERENCED_PARAMETER(phPDumpAllocInfoPtr);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimPDumpPolKM)
#endif
static INLINE PVRSRV_ERROR
PDumpPMRFree(IMG_HANDLE hPDumpAllocationInfoHandle)
{
	PVR_UNREFERENCED_PARAMETER(hPDumpAllocationInfoHandle);
	return PVRSRV_OK;
}
#endif	/* PDUMP */

#define PMR_DEFAULT_PREFIX "PMR"
#define PMR_SYMBOLICADDR_FMTSPEC "%s%llu"

#if defined(PDUMP)
#define PDUMP_PHYSMEM_MALLOC_OSPAGES(pszPDumpMemDevName, ui32SerialNum, ui32Size, ui32Align, phHandlePtr) \
    PDumpPMRMalloc(pszPDumpMemDevName, PMR_OSALLOCPAGES_PREFIX, ui32SerialNum, ui32Size, ui32Align, phHandlePtr)
#define PDUMP_PHYSMEM_FREE_OSPAGES(hHandle) \
    PDumpPMRFree(hHandle)
#else
#define PDUMP_PHYSMEM_MALLOC_OSPAGES(pszPDumpMemDevName, ui32SerialNum, ui32Size, ui32Align, phHandlePtr) \
    ((IMG_VOID)(*phHandlePtr=IMG_NULL))
#define PDUMP_PHYSMEM_FREE_OSPAGES(hHandle) \
    ((IMG_VOID)(0))
#endif // defined(PDUMP)

extern PVRSRV_ERROR
PDumpPMRWRW32(const IMG_CHAR *pszDevSpace,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_UINT32 ui32Value,
            PDUMP_FLAGS_T uiPDumpFlags);

extern PVRSRV_ERROR
PDumpPMRWRW64(const IMG_CHAR *pszDevSpace,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_UINT64 ui64Value,
            PDUMP_FLAGS_T uiPDumpFlags);

extern PVRSRV_ERROR
PDumpPMRLDB(const IMG_CHAR *pszDevSpace,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_DEVMEM_SIZE_T uiSize,
            const IMG_CHAR *pszFilename,
            IMG_UINT32 uiFileOffset,
            PDUMP_FLAGS_T uiPDumpFlags);

extern PVRSRV_ERROR
PDumpPMRSAB(const IMG_CHAR *pszDevSpace,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_DEVMEM_SIZE_T uiSize,
            const IMG_CHAR *pszFileName,
            IMG_UINT32 uiFileOffset);

/*
  PDumpPMRPOL()

  emits a POL to the PDUMP.
*/
extern PVRSRV_ERROR
PDumpPMRPOL(const IMG_CHAR *pszMempaceName,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiOffset,
            IMG_UINT32 ui32Value,
            IMG_UINT32 ui32Mask,
            PDUMP_POLL_OPERATOR eOperator,
            IMG_UINT32 uiCount,
            IMG_UINT32 uiDelay,
            PDUMP_FLAGS_T uiPDumpFlags);

extern PVRSRV_ERROR
PDumpPMRCBP(const IMG_CHAR *pszMemspaceName,
            const IMG_CHAR *pszSymbolicName,
            IMG_DEVMEM_OFFSET_T uiReadOffset,
            IMG_DEVMEM_OFFSET_T uiWriteOffset,
            IMG_DEVMEM_SIZE_T uiPacketSize,
            IMG_DEVMEM_SIZE_T uiBufferSize);

/*
 * PDumpWriteBuffer()
 *
 * writes a binary blob to the pdump param stream containing the
 * current contents of the memory, and returns the filename and offset
 * of where that blob is located (for use in a subsequent LDB, for
 * example)
 *
 * Caller to provide buffer to receive filename, and declare the size
 * of that buffer
 */
extern PVRSRV_ERROR
PDumpWriteBuffer(IMG_UINT8 *pcBuffer,
                 IMG_SIZE_T uiNumBytes,
                 PDUMP_FLAGS_T uiPDumpFlags,
                 IMG_CHAR *pszFilenameOut,
                 IMG_SIZE_T uiFilenameBufSz,
                 PDUMP_FILEOFFSET_T *puiOffsetOut);

#endif /* #ifndef SRVSRV_PDUMP_PHYSMEM_H */
