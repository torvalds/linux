/**************************************************************************/ /*!
@File
@Title          Common MMU Management
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

#ifndef SRVKM_PDUMP_MMU_H
#define SRVKM_PDUMP_MMU_H

/* services/server/include/ */
#include "pdump_symbolicaddr.h"
/* include/ */
#include "img_types.h"
#include "pvrsrv_error.h"

#include "mmu_common.h"

/*
	PDUMP MMU attributes
*/
typedef struct _PDUMP_MMU_ATTRIB_DEVICE_
{
    /* Per-Device Pdump attribs */

	/*!< Pdump memory bank name */
	IMG_CHAR				*pszPDumpMemDevName;

	/*!< Pdump register bank name */
	IMG_CHAR				*pszPDumpRegDevName;

} PDUMP_MMU_ATTRIB_DEVICE;

typedef struct _PDUMP_MMU_ATTRIB_CONTEXT_
{
	IMG_UINT32 ui32Dummy;
} PDUMP_MMU_ATTRIB_CONTEXT;

typedef struct _PDUMP_MMU_ATTRIB_HEAP_
{
	/* data page info */
	IMG_UINT32 ui32DataPageMask;
} PDUMP_MMU_ATTRIB_HEAP;

typedef struct _PDUMP_MMU_ATTRIB_
{
    
    struct _PDUMP_MMU_ATTRIB_DEVICE_ sDevice;
    struct _PDUMP_MMU_ATTRIB_CONTEXT_ sContext;
    struct _PDUMP_MMU_ATTRIB_HEAP_ sHeap;
} PDUMP_MMU_ATTRIB;

#if defined(PDUMP)
    extern PVRSRV_ERROR PDumpMMUMalloc(const IMG_CHAR			*pszPDumpDevName,
                                       MMU_LEVEL				eMMULevel,
                                       IMG_DEV_PHYADDR			*psDevPAddr,
                                       IMG_UINT32				ui32Size,
                                       IMG_UINT32				ui32Align);

    extern PVRSRV_ERROR PDumpMMUFree(const IMG_CHAR				*pszPDumpDevName,
                                     MMU_LEVEL					eMMULevel,
                                     IMG_DEV_PHYADDR			*psDevPAddr);

    extern PVRSRV_ERROR PDumpMMUMalloc2(const IMG_CHAR			*pszPDumpDevName,
                                        const IMG_CHAR			*pszTableType,/* PAGE_CATALOGUE, PAGE_DIRECTORY, PAGE_TABLE */
                                        const IMG_CHAR 			*pszSymbolicAddr,
                                        IMG_UINT32				ui32Size,
                                        IMG_UINT32				ui32Align);

    extern PVRSRV_ERROR PDumpMMUFree2(const IMG_CHAR			*pszPDumpDevName,
                                      const IMG_CHAR			*pszTableType,/* PAGE_CATALOGUE, PAGE_DIRECTORY, PAGE_TABLE */
                                      const IMG_CHAR 			*pszSymbolicAddr);

    extern PVRSRV_ERROR PDumpMMUDumpPxEntries(MMU_LEVEL eMMULevel,
    								   const IMG_CHAR *pszPDumpDevName,
                                       IMG_VOID *pvPxMem,
                                       IMG_DEV_PHYADDR sPxDevPAddr,
                                       IMG_UINT32 uiFirstEntry,
                                       IMG_UINT32 uiNumEntries,
                                       const IMG_CHAR *pszMemspaceName,
                                       const IMG_CHAR *pszSymbolicAddr,
                                       IMG_UINT64 uiSymbolicAddrOffset,
                                       IMG_UINT32 uiBytesPerEntry,
                                       IMG_UINT32 uiLog2Align,
                                       IMG_UINT32 uiAddrShift,
                                       IMG_UINT64 uiAddrMask,
                                       IMG_UINT64 uiPxEProtMask,
                                       IMG_UINT32 ui32Flags);

    extern PVRSRV_ERROR PDumpMMUAllocMMUContext(const IMG_CHAR *pszPDumpMemSpaceName,
                                                IMG_DEV_PHYADDR sPCDevPAddr,
                                                PDUMP_MMU_TYPE eMMUType,
                                                IMG_UINT32 *pui32MMUContextID);

    extern PVRSRV_ERROR PDumpMMUFreeMMUContext(const IMG_CHAR *pszPDumpMemSpaceName,
                                               IMG_UINT32 ui32MMUContextID);

	extern PVRSRV_ERROR PDumpMMUActivateCatalog(const IMG_CHAR *pszPDumpRegSpaceName,
												const IMG_CHAR *pszPDumpRegName,
												IMG_UINT32 uiRegAddr,
												const IMG_CHAR *pszPDumpPCSymbolicName);

	
extern PVRSRV_ERROR
PDumpMMUSAB(const IMG_CHAR *pszPDumpMemNamespace,
               IMG_UINT32 uiPDumpMMUCtx,
               IMG_DEV_VIRTADDR sDevAddrStart,
               IMG_DEVMEM_SIZE_T uiSize,
               const IMG_CHAR *pszFilename,
               IMG_UINT32 uiFileOffset,
			   IMG_UINT32 ui32PDumpFlags);

	#define PDUMP_MMU_MALLOC_DP(pszPDumpMemDevName, aszSymbolicAddr, ui32Size, ui32Align) \
        PDumpMMUMalloc2(pszPDumpMemDevName, "DATA_PAGE", aszSymbolicAddr, ui32Size, ui32Align)
    #define PDUMP_MMU_FREE_DP(pszPDumpMemDevName, aszSymbolicAddr) \
        PDumpMMUFree2(pszPDumpMemDevName, "DATA_PAGE", aszSymbolicAddr)

    #define PDUMP_MMU_ALLOC_MMUCONTEXT(pszPDumpMemDevName, sPCDevPAddr, eMMUType, puiPDumpCtxID) \
        PDumpMMUAllocMMUContext(pszPDumpMemDevName,                     \
                                sPCDevPAddr,                            \
                                eMMUType,								\
                                puiPDumpCtxID)

    #define PDUMP_MMU_FREE_MMUCONTEXT(pszPDumpMemDevName, uiPDumpCtxID) \
        PDumpMMUFreeMMUContext(pszPDumpMemDevName, uiPDumpCtxID)
#else

	#define PDUMP_MMU_MALLOC_DP(pszPDumpMemDevName, pszDevPAddr, ui32Size, ui32Align) \
        ((IMG_VOID)0)
    #define PDUMP_MMU_FREE_DP(pszPDumpMemDevName, psDevPAddr) \
        ((IMG_VOID)0)
    #define PDUMP_MMU_ALLOC_MMUCONTEXT(pszPDumpMemDevName, sPCDevPAddr, puiPDumpCtxID) \
        ((IMG_VOID)0)
    #define PDUMP_MMU_FREE_MMUCONTEXT(pszPDumpMemDevName, uiPDumpCtxID) \
        ((IMG_VOID)0)

#endif // defined(PDUMP)

#endif
