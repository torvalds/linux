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


#ifndef _gc_hal_kernel_platform_h_
#define _gc_hal_kernel_platform_h_
#include <linux/mm.h>

typedef struct _gcsMODULE_PARAMETERS
{
    gctINT  irqLine;
    gctUINT registerMemBase;
    gctUINT registerMemSize;
    gctINT  irqLine2D;
    gctUINT registerMemBase2D;
    gctUINT registerMemSize2D;
    gctINT  irqLineVG;
    gctUINT registerMemBaseVG;
    gctUINT registerMemSizeVG;
    gctUINT contiguousSize;
    gctUINT contiguousBase;
    gctUINT contiguousRequested;
    gctUINT bankSize;
    gctINT  fastClear;
    gctINT  compression;
    gctINT  powerManagement;
    gctINT  gpuProfiler;
    gctINT  signal;
    gctUINT baseAddress;
    gctUINT physSize;
    gctUINT logFileSize;
    gctUINT recovery;
    gctUINT stuckDump;
    gctUINT showArgs;
    gctUINT gpu3DMinClock;
    gctBOOL registerMemMapped;
    gctPOINTER registerMemAddress;
    gctINT  irqs[gcvCORE_COUNT];
    gctUINT registerBases[gcvCORE_COUNT];
    gctUINT registerSizes[gcvCORE_COUNT];
    gctUINT chipIDs[gcvCORE_COUNT];
}
gcsMODULE_PARAMETERS;

typedef struct _gcsPLATFORM * gckPLATFORM;

typedef struct _gcsPLATFORM_OPERATIONS
{
    /*******************************************************************************
    **
    **  needAddDevice
    **
    **  Determine whether platform_device is created by initialization code.
    **  If platform_device is created by BSP, return gcvFLASE here.
    */
    gctBOOL
    (*needAddDevice)(
        IN gckPLATFORM Platform
        );

    /*******************************************************************************
    **
    **  adjustParam
    **
    **  Override content of arguments, if a argument is not changed here, it will
    **  keep as default value or value set by insmod command line.
    */
    gceSTATUS
    (*adjustParam)(
        IN gckPLATFORM Platform,
        OUT gcsMODULE_PARAMETERS *Args
        );

    /*******************************************************************************
    **
    **  adjustDriver
    **
    **  Override content of platform_driver which will be registered.
    */
    gceSTATUS
    (*adjustDriver)(
        IN gckPLATFORM Platform
        );

    /*******************************************************************************
    **
    **  getPower
    **
    **  Prepare power and clock operation.
    */
    gceSTATUS
    (*getPower)(
        IN gckPLATFORM Platform
        );

    /*******************************************************************************
    **
    **  putPower
    **
    **  Finish power and clock operation.
    */
    gceSTATUS
    (*putPower)(
        IN gckPLATFORM Platform
        );

    /*******************************************************************************
    **
    **  allocPriv
    **
    **  Construct platform private data.
    */
    gceSTATUS
    (*allocPriv)(
        IN gckPLATFORM Platform
        );

    /*******************************************************************************
    **
    **  freePriv
    **
    **  free platform private data.
    */
    gceSTATUS
    (*freePriv)(
        IN gckPLATFORM Platform
        );

    /*******************************************************************************
    **
    **  setPower
    **
    **  Set power state of specified GPU.
    **
    **  INPUT:
    **
    **      gceCORE GPU
    **          GPU neeed to config.
    **
    **      gceBOOL Enable
    **          Enable or disable power.
    */
    gceSTATUS
    (*setPower)(
        IN gckPLATFORM Platform,
        IN gceCORE GPU,
        IN gctBOOL Enable
        );

    /*******************************************************************************
    **
    **  setClock
    **
    **  Set clock state of specified GPU.
    **
    **  INPUT:
    **
    **      gceCORE GPU
    **          GPU neeed to config.
    **
    **      gceBOOL Enable
    **          Enable or disable clock.
    */
    gceSTATUS
    (*setClock)(
        IN gckPLATFORM Platform,
        IN gceCORE GPU,
        IN gctBOOL Enable
        );

    /*******************************************************************************
    **
    **  reset
    **
    **  Reset GPU outside.
    **
    **  INPUT:
    **
    **      gceCORE GPU
    **          GPU neeed to reset.
    */
    gceSTATUS
    (*reset)(
        IN gckPLATFORM Platform,
        IN gceCORE GPU
        );

    /*******************************************************************************
    **
    **  getGPUPhysical
    **
    **  Convert CPU physical address to GPU physical address if they are
    **  different.
    */
    gceSTATUS
    (*getGPUPhysical)(
        IN gckPLATFORM Platform,
        IN gctPHYS_ADDR_T CPUPhysical,
        OUT gctPHYS_ADDR_T * GPUPhysical
        );

    /*******************************************************************************
    **
    **  getGPUPhysical
    **
    **  Convert GPU physical address to CPU physical address if they are
    **  different.
    */
    gceSTATUS
    (*getCPUPhysical)(
        IN gckPLATFORM Platform,
        IN gctUINT32 GPUPhysical,
        OUT gctPHYS_ADDR_T * CPUPhysical
        );

    /*******************************************************************************
    **
    **  adjustProt
    **
    **  Override Prot flag when mapping paged memory to userspace.
    */
    gceSTATUS
    (*adjustProt)(
        IN struct vm_area_struct * vma
        );

    /*******************************************************************************
    **
    **  shrinkMemory
    **
    **  Do something to collect memory, eg, act as oom killer.
    */
    gceSTATUS
    (*shrinkMemory)(
        IN gckPLATFORM Platform
        );

    /*******************************************************************************
    **
    **  cache
    **
    **  Cache operation.
    */
    gceSTATUS
    (*cache)(
        IN gckPLATFORM Platform,
        IN gctUINT32 ProcessID,
        IN gctPHYS_ADDR Handle,
        IN gctUINT32 Physical,
        IN gctPOINTER Logical,
        IN gctSIZE_T Bytes,
        IN gceCACHEOPERATION Operation
        );

    /*******************************************************************************
    **
    **  name
    **
    **  Get name of platform code.
    **
    **  There is a helper macro gcmkPLATFORM_Name which defines a default callback
    **  function _Name() which uses code path as name.
    */
    gceSTATUS
    (*name)(
        IN gckPLATFORM Platform,
        IN gctCONST_STRING * Name
        );

    /*******************************************************************************
    **
    ** getPolicyID
    **
    ** Get policyID for a specified surface type.
    */
    gceSTATUS
    (*getPolicyID)(
        IN gckPLATFORM Platform,
        IN gceSURF_TYPE Type,
        OUT gctUINT32_PTR PolicyID,
        OUT gctUINT32_PTR AXIConfig
        );
}
gcsPLATFORM_OPERATIONS;

typedef struct _gcsPLATFORM
{
    struct platform_device* device;
    struct platform_driver* driver;

    gcsPLATFORM_OPERATIONS* ops;

    void*                   priv;
}
gcsPLATFORM;

void
gckPLATFORM_QueryOperations(
    IN gcsPLATFORM_OPERATIONS ** Operations
    );

#define gcmkPLATFROM_Name               \
static gceSTATUS                        \
_Name(                                  \
    IN gckPLATFORM Platform,            \
    IN gctCONST_STRING * Name           \
    )                                   \
{                                       \
    * Name = __FILE__;                  \
    return gcvSTATUS_OK;                \
}                                       \

#endif
