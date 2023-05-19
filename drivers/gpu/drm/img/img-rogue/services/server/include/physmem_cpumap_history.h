/*************************************************************************/ /*!
@File
@Title          Definitions of PMR Mapping History for OS managed memory
@Codingstyle    IMG
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
#ifndef PHYSMEM_CPUMAP_HISTORY_H_
#define PHYSMEM_CPUMAP_HISTORY_H_

#include "img_types.h"
#include "img_defs.h"
#include <powervr/mem_types.h>

/*!
*******************************************************************************
 @Function	  CPUMappingHistoryInit
 @Description CPU Mapping history initialisation
 @Return      PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR CPUMappingHistoryInit(void);

/*!
*******************************************************************************
 @Function	  CPUMappingHistoryDeInit
 @Description CPU Mapping history De-initialisation
 @Return      None
******************************************************************************/
void CPUMappingHistoryDeInit(void);

/*!
*******************************************************************************
 @Function	  InsertMappingRecord
 @Description CPU Mapping history Map Record insertion.
              This function given relevant mapping information, inserts a
              mapping record to the history buffer.
 @Input       pszAnnotation - The annotation related to the allocation to be
                              mapped.
 @Input       uiPID         - The PID of the process mapping the allocation.
 @Input       pvAddress     - The CPU virtual address of the newly mapped
                              allocation.
 @Input       sCPUPhyAddr       - The CPU Physical address of the newly mapped
                                  allocation.
 @Input       ui32CPUCacheFlags - The CPU Caching flags associated with the
                                  mapping.
 @Input       uiMapOffset       - The offset into the PMR at which the mapping
                                  resides.
 @input       ui32PageCount     - The number of pages mapped.
 @Return      None
******************************************************************************/
void InsertMappingRecord(const IMG_CHAR *pszAnnotation,
                         IMG_PID uiPID,
                         IMG_CPU_VIRTADDR pvAddress,
                         IMG_CPU_PHYADDR sCpuPhyAddr,
                         IMG_UINT32 ui32CPUCacheFlags,
                         size_t uiMapOffset,
                         IMG_UINT32 ui32PageCount);

/*!
*******************************************************************************
 @Function	  InsertUnMappingRecord
 @Description CPU Mapping history UnMap Record insertion.
              This function given relevant mapping information, inserts an
              un-mapping record to the history buffer.
 @Input       pvAddress     - The CPU virtual address of the un-mapped
                              allocation.
 @Input       sCPUPhyAddr       - The CPU Physical address of the un-mapped
                                  allocation.
 @Input       ui32CPUCacheFlags - The CPU Caching flags associated with the
                                  mapping.
 @input       ui32PageCount     - The number of pages un-mapped.
 @Return      None
******************************************************************************/
void InsertUnMappingRecord(IMG_CPU_VIRTADDR pvAddress,
                           IMG_CPU_PHYADDR sCpuPhyAddr,
                           IMG_UINT32 ui32CPUCacheFlags,
                           IMG_UINT32 ui32PageCount);


#endif /* PHYSMEM_CPUMAP_HISTORY_H_ */
