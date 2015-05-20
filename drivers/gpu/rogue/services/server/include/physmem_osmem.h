/**************************************************************************/ /*!
@File
@Title		PMR implementation of OS derived physical memory
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Part of the memory management.  This module is
                responsible for the an implementation of the "PMR"
                abstraction.  This interface is for the
                PhysmemNewOSRamBackedPMR() "PMR Factory" which is
                responsible for claiming chunks of memory (in
                particular physically contiguous quanta) from the
                Operating System.

                As such, this interface will be implemented on a
                Per-OS basis, in the "env" directory for that system.
                A dummy implementation is available in
                physmem_osmem_dummy.c for operating systems that
                cannot, or do not wish to, offer this functionality.
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
#ifndef _SRVSRV_PHYSMEM_OSMEM_H_
#define _SRVSRV_PHYSMEM_OSMEM_H_

/* include/ */
#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

/* services/server/include/ */
#include "pmr.h"
#include "pmr_impl.h"

/*
 * PhysmemNewOSRamBackedPMR
 *
 * To be overridden on a per-OS basis.
 *
 * This function will create a PMR using the default "OS supplied" physical pages
 * method, assuming such is available on a particular operating system.  (If not,
 * PVRSRV_ERROR_NOT_SUPPORTED should be returned)
 */
extern PVRSRV_ERROR
PhysmemNewOSRamBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
                         IMG_DEVMEM_SIZE_T uiSize,
						 IMG_DEVMEM_SIZE_T uiChunkSize,
						 IMG_UINT32 ui32NumPhysChunks,
						 IMG_UINT32 ui32NumVirtChunks,
						 IMG_BOOL *pabMappingTable,
                         IMG_UINT32 uiLog2PageSize,
                         PVRSRV_MEMALLOCFLAGS_T uiFlags,
                         PMR **ppsPMROut);

/*
 * PhysmemNewTDMetaCodePMR
 *
 * This function is used as part of the facility to provide secure META firmware
 * memory. A default implementation is provided which must be replaced by the SoC
 * implementor.
 *
 * Calling this function will return a PMR for a memory allocation made in "secure
 * META code memory". It will only be writable by a hypervisor, and when the feature
 * is enabled on the SoC, the META will only be able to perform instruction reads from
 * memory that is secured that way.
 */
PVRSRV_ERROR
PhysmemNewTDMetaCodePMR(PVRSRV_DEVICE_NODE *psDevNode,
                        IMG_DEVMEM_SIZE_T uiSize,
                        IMG_UINT32 uiLog2PageSize,
                        PVRSRV_MEMALLOCFLAGS_T uiFlags,
                        PMR **ppsPMRPtr);

PVRSRV_ERROR
PhysmemNewTDSecureBufPMR(PVRSRV_DEVICE_NODE *psDevNode,
                         IMG_DEVMEM_SIZE_T uiSize,
                         IMG_UINT32 uiLog2PageSize,
                         PVRSRV_MEMALLOCFLAGS_T uiFlags,
                         PMR **ppsPMRPtr);


#endif /* #ifndef _SRVSRV_PHYSMEM_OSMEM_H_ */
