/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#ifndef __gc_hal_kernel_hardware_h_
#define __gc_hal_kernel_hardware_h_

#if gcdENABLE_VG
#include "gc_hal_kernel_hardware_vg.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    gcvHARDWARE_FUNCTION_MMU,
    gcvHARDWARE_FUNCTION_FLUSH,

    /* BLT engine command sequence. */
    gcvHARDWARE_FUNCTION_BLT_EVENT,
    gcvHARDWARE_FUNCTION_NUM,
}
gceHARDWARE_FUNCTION;


typedef struct _gcsHARWARE_FUNCTION
{
    /* Entry of the function. */
    gctUINT32                   address;

    /* CPU address of the function. */
    gctUINT8_PTR                logical;

    /* Bytes of the function. */
    gctUINT32                   bytes;

    /* Hardware address of END in this function. */
    gctUINT32                   endAddress;

    /* Logical of END in this function. */
    gctUINT8_PTR                endLogical;
}
gcsHARDWARE_FUNCTION;

typedef struct _gcsSTATETIMER
{
    gctUINT64                   start;
    gctUINT64                   recent;

    /* Elapse of each power state. */
    gctUINT64                   elapse[4];
}
gcsSTATETIMER;

typedef struct _gcsHARDWARE_SIGNATURE
{
    /* Chip model. */
    gceCHIPMODEL                chipModel;

    /* Revision value.*/
    gctUINT32                   chipRevision;

    /* Supported feature fields. */
    gctUINT32                   chipFeatures;

    /* Supported minor feature fields. */
    gctUINT32                   chipMinorFeatures;

    /* Supported minor feature 1 fields. */
    gctUINT32                   chipMinorFeatures1;

    /* Supported minor feature 2 fields. */
    gctUINT32                   chipMinorFeatures2;
}
gcsHARDWARE_SIGNATURE;

typedef struct _gcsMMU_TABLE_ARRAY_ENTRY
{
    gctUINT32                   low;
    gctUINT32                   high;
}
gcsMMU_TABLE_ARRAY_ENTRY;

typedef struct _gcsHARDWARE_PAGETABLE_ARRAY
{
    /* Number of entries in page table array. */
    gctUINT                     num;

    /* Size in bytes of array. */
    gctSIZE_T                   size;

    /* Physical address of array. */
    gctPHYS_ADDR_T              address;

    /* Memory descriptor. */
    gctPHYS_ADDR                physical;

    /* Logical address of array. */
    gctPOINTER                  logical;
}
gcsHARDWARE_PAGETABLE_ARRAY;

/* gckHARDWARE object. */
struct _gckHARDWARE
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gctKERNEL object. */
    gckKERNEL                   kernel;

    /* Pointer to gctOS object. */
    gckOS                       os;

    /* Core */
    gceCORE                     core;

    /* Chip characteristics. */
    gcsHAL_QUERY_CHIP_IDENTITY  identity;
    gctBOOL                     allowFastClear;
    gctBOOL                     allowCompression;
    gctUINT32                   powerBaseAddress;
    gctBOOL                     extraEventStates;

    /* Big endian */
    gctBOOL                     bigEndian;

    /* Base address. */
    gctUINT32                   baseAddress;

    /* Chip status */
    gctPOINTER                  powerMutex;
    gctUINT32                   powerProcess;
    gctUINT32                   powerThread;
    gceCHIPPOWERSTATE           chipPowerState;
    gctUINT32                   lastWaitLink;
    gctUINT32                   lastEnd;
    gctBOOL                     clockState;
    gctBOOL                     powerState;
    gctPOINTER                  globalSemaphore;

    gctISRMANAGERFUNC           startIsr;
    gctISRMANAGERFUNC           stopIsr;
    gctPOINTER                  isrContext;

    gctUINT32                   mmuVersion;

    /* Whether use new MMU. It is meaningless
    ** for old MMU since old MMU is always enabled.
    */
    gctBOOL                     enableMMU;

    /* Type */
    gceHARDWARE_TYPE            type;

#if gcdPOWEROFF_TIMEOUT
    gctUINT32                   powerOffTime;
    gctUINT32                   powerOffTimeout;
    gctPOINTER                  powerOffTimer;
#endif

#if gcdENABLE_FSCALE_VAL_ADJUST
    gctUINT32                   powerOnFscaleVal;
#endif
    gctPOINTER                  pageTableDirty;

#if gcdLINK_QUEUE_SIZE
    struct _gckQUEUE            linkQueue;
#endif

    gctBOOL                     powerManagement;
    gctBOOL                     gpuProfiler;

    gctBOOL                     stallFEPrefetch;

    gctUINT32                   minFscaleValue;
    gctUINT                     waitCount;

    gctPOINTER                  pendingEvent;

    /* Function used by gckHARDWARE. */
    gctPHYS_ADDR                functionPhysical;
    gctPOINTER                  functionLogical;
    gctUINT32                   functionAddress;
    gctSIZE_T                   functionBytes;

    gcsHARDWARE_FUNCTION        functions[gcvHARDWARE_FUNCTION_NUM];

    gcsSTATETIMER               powerStateTimer;
    gctUINT32                   executeCount;
    gctUINT32                   lastExecuteAddress;

    /* Head for hardware list in gckMMU. */
    gcsLISTHEAD                 mmuHead;

    gctPOINTER                  featureDatabase;

    gcsHARDWARE_SIGNATURE       signature;

    gctUINT32                   maxOutstandingReads;

    gcsHARDWARE_PAGETABLE_ARRAY pagetableArray;

    gceSECURE_MODE              secureMode;

    gctUINT64                   contextID;
};

typedef struct _gcsFEDescriptor
{
    gctUINT32                   start;
    gctUINT32                   end;
}
gcsFEDescriptor;

typedef struct _gcsFE *         gckFE;
typedef struct _gcsFE
{
    gckOS                       os;

    /* Number of free descriptors. */
    gctPOINTER                  freeDscriptors;
}
gcsFE;

gceSTATUS
gckFE_Initialize(
    IN gckHARDWARE Hardware,
    OUT gckFE FE
    );

gceSTATUS
gckFE_ReserveSlot(
    IN gckHARDWARE Hardware,
    IN gckFE FE,
    OUT gctBOOL * Available
    );

void
gckFE_UpdateAvaiable(
    IN gckHARDWARE Hardware,
    OUT gckFE FE
    );

void
gckFE_Execute(
    IN gckHARDWARE Hardware,
    IN gckFE FE,
    IN gcsFEDescriptor * Desc
    );

gceSTATUS
gckHARDWARE_GetBaseAddress(
    IN gckHARDWARE Hardware,
    OUT gctUINT32_PTR BaseAddress
    );

gceSTATUS
gckHARDWARE_NeedBaseAddress(
    IN gckHARDWARE Hardware,
    IN gctUINT32 State,
    OUT gctBOOL_PTR NeedBase
    );

gceSTATUS
gckHARDWARE_GetFrameInfo(
    IN gckHARDWARE Hardware,
    OUT gcsHAL_FRAME_INFO * FrameInfo
    );

#define gcmkWRITE_MEMORY(logical, data) \
    do { \
    gcmkVERIFY_OK(gckOS_WriteMemory(os, logical, data)); \
    logical++; \
    }\
    while (0) ; \

gceSTATUS
gckHARDWARE_DummyDraw(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Address,
    IN gceDUMMY_DRAW_TYPE DummyDrawType,
    IN OUT gctUINT32 * Bytes
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_kernel_hardware_h_ */

