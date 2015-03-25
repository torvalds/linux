/*************************************************************************/ /*!
@File
@Title          Device Memory Management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Client side part of device memory management -- this file
                is forked from new_devmem_allocation.h as this one has to
                reside in the top level include so that client code is able
                to make use of the typedefs.
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

#ifndef DEVICEMEM_TYPEDEFS_H
#define DEVICEMEM_TYPEDEFS_H

#include "img_types.h"
#include "pvrsrv_memallocflags.h"

typedef struct _DEVMEM_CONTEXT_ DEVMEM_CONTEXT;     /*!< Convenience typedef for struct _DEVMEM_CONTEXT_ */
typedef struct _DEVMEM_HEAP_ DEVMEM_HEAP;           /*!< Convenience typedef for struct _DEVMEM_HEAP_ */
typedef struct _DEVMEM_MEMDESC_ DEVMEM_MEMDESC;     /*!< Convenience typedef for struct _DEVMEM_MEMDESC_ */
typedef struct _DEVMEM_PAGELIST_ DEVMEM_PAGELIST;	/*!< Convenience typedef for struct _DEVMEM_PAGELIST_ */
typedef PVRSRV_MEMALLOCFLAGS_T DEVMEM_FLAGS_T;      /*!< Conveneince typedef for PVRSRV_MEMALLOCFLAGS_T */

typedef IMG_HANDLE /* */ DEVMEM_EXPORTHANDLE; /*!< Typedef for DeviceMem Export Handle */
typedef IMG_UINT64 DEVMEM_EXPORTKEY;                                /*!< Typedef for DeviceMem Export Key */
typedef IMG_DEVMEM_SIZE_T DEVMEM_SIZE_T;                            /*!< Typedef for DeviceMem SIZE_T */
typedef IMG_DEVMEM_LOG2ALIGN_T DEVMEM_LOG2ALIGN_T;                  /*!< Typdef for DeviceMem LOG2 Alignment */

/*! calling code needs all the info in this struct, to be able to pass it around */
typedef struct
{
    /*! A handle to the PMR.  Should be a SID.  
*/
    IMG_HANDLE hPMRExportHandle;
    /*! The "key" to prove we have authorization to use this PMR */
    IMG_UINT64 uiPMRExportPassword;
    /*! Size and alignment properties for this PMR.  Note, these
       numbers are not trusted in kernel, but we need to cache them
       client-side in order to allocate from the VM arena.  The kernel
       will know the actual alignment and size of the PMR and thus
       would prevent client code from breaching security here.  Ditto
       for physmem granularity (aka page size) if this is different
       from alignment */
    IMG_DEVMEM_SIZE_T uiSize;
    /*! We call this "contiguity guarantee" to be more precise than
       calling it "alignment" or "page size", terms which may seem
       similar but have different emphasis.  The number reported here
       is the minimum contiguity guarantee from the creator of the
       PMR.  Now, there is no requirement to allocate that coarsely
       from the RA.  The alignment given to the RA simply needs to be
       at least as coarse as the device page size for the heap we
       ultimately intend to map into.  What is important is that the
       device MMU data page size is not greater than the minimum
       contiguity guarantee from the PMR.  This value is reported to
       the client in order that it can choose to make early checks and
       perhaps decide which heap (in a variable page size scenario) it
       would be safe to map this PMR into.  For convenience, the
       client may choose to use this argument as the alignment of the
       virtual range he chooses to allocate, but this is _not_
       necessary and in many cases would be able to get away with a
       finer alignment, should the heap into which this PMR will be
       mapped support it. */
    IMG_DEVMEM_LOG2ALIGN_T uiLog2ContiguityGuarantee;
} DEVMEM_EXPORTCOOKIE;

typedef IMG_HANDLE DEVMEM_SERVER_EXPORTCOOKIE; /*!< typedef for DeviceMem Server Export Cookie */

#endif /* #ifndef SRVCLIENT_NEW_DEVMEM_ALLOCATION_TYPEDEFS_H */

