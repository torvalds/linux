/*************************************************************************/ /*!
@File
@Title          OS Interface for mapping PMRs into CPU space.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    OS abstraction for the mmap2 interface for mapping PMRs into
                User Mode memory
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

#ifndef OSMMAP_H
#define OSMMAP_H

#include <powervr/mem_types.h>

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

/*************************************************************************/ /*!
@Function       OSMMapPMR
@Description    Maps the specified PMR into CPU memory so that it may be
                accessed by the user process.
                Whether the memory is mapped read only, read/write, or not at
                all, is dependent on the PMR itself.
                The PMR handle is opaque to the user, and lower levels of this
                stack ensure that the handle is private to this process, such
                that this API cannot be abused to gain access to other people's
                PMRs. The OS implementation of this function should return the
                virtual address and length for the User to use. The "PrivData"
                is to be stored opaquely by the caller (N.B. he should make no
                assumptions, in particular, NULL is a valid handle) and given
                back to the call to OSMUnmapPMR.
                The OS implementation is free to use the PrivData handle for
                any purpose it sees fit.
@Input          hBridge              The bridge handle.
@Input          hPMR                 The handle of the PMR to be mapped.
@Input          uiPMRLength          The size of the PMR.
@Input          uiFlags              Flags indicating how the mapping should
                                     be done (read-only, etc). These may not
                                     be honoured if the PMR does not permit
                                     them.
@Output         phOSMMapPrivDataOut  Returned private data.
@Output         ppvMappingAddressOut The returned mapping.
@Output         puiMappingLengthOut  The size of the returned mapping.
@Return         PVRSRV_OK on success, failure code otherwise.
 */ /*************************************************************************/
PVRSRV_ERROR
OSMMapPMR(IMG_HANDLE hBridge,
          IMG_HANDLE hPMR,
          IMG_DEVMEM_SIZE_T uiPMRLength,
          PVRSRV_MEMALLOCFLAGS_T uiFlags,
          IMG_HANDLE *phOSMMapPrivDataOut,
          void **ppvMappingAddressOut,
          size_t *puiMappingLengthOut);

/*************************************************************************/ /*!
@Function       OSMUnmapPMR
@Description    Unmaps the specified PMR from CPU memory.
                This function is the counterpart to OSMMapPMR.
                The caller is required to pass the PMR handle back in along
                with the same 3-tuple of information that was returned by the
                call to OSMMapPMR in phOSMMapPrivDataOut.
                It is possible to unmap only part of the original mapping
                with this call, by specifying only the address range to be
                unmapped in pvMappingAddress and uiMappingLength.
@Input          hBridge              The bridge handle.
@Input          hPMR                 The handle of the PMR to be unmapped.
@Input          hOSMMapPrivData      The OS private data of the mapping.
@Input          pvMappingAddress     The address to be unmapped.
@Input          uiMappingLength      The size to be unmapped.
@Return         PVRSRV_OK on success, failure code otherwise.
 */ /*************************************************************************/
void
OSMUnmapPMR(IMG_HANDLE hBridge,
            IMG_HANDLE hPMR,
            IMG_HANDLE hOSMMapPrivData,
            void *pvMappingAddress,
            size_t uiMappingLength);

#endif /* OSMMAP_H */
