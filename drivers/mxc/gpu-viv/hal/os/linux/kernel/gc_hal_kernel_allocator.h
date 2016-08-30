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


#ifndef __gc_hal_kernel_allocator_h_
#define __gc_hal_kernel_allocator_h_

#include "gc_hal_kernel_linux.h"

typedef struct _gcsALLOCATOR * gckALLOCATOR;
typedef struct _gcsATTACH_DESC * gcsATTACH_DESC_PTR;

typedef struct _gcsALLOCATOR_OPERATIONS
{
    /**************************************************************************
    **
    ** Alloc
    **
    ** Allocte memory, request size is page aligned.
    **
    ** INPUT:
    **
    **    gckALLOCATOR Allocator
    **        Pointer to an gckALLOCATOER object.
    **
    **    PLINUX_Mdl
    **        Pointer to Mdl whichs stores information
    **        about allocated memory.
    **
    **    gctSIZE_T NumPages
    **        Number of pages need to allocate.
    **
    **    gctUINT32 Flag
    **        Allocation option.
    **
    ** OUTPUT:
    **
    **      Nothing.
    **
    */
    gceSTATUS
    (*Alloc)(
        IN gckALLOCATOR Allocator,
        IN PLINUX_MDL Mdl,
        IN gctSIZE_T NumPages,
        IN gctUINT32 Flag
        );

    /**************************************************************************
    **
    ** Free
    **
    ** Free memory.
    **
    ** INPUT:
    **
    **     gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **     PLINUX_MDL Mdl
    **          Mdl which stores information.
    **
    ** OUTPUT:
    **
    **      Nothing.
    **
    */
    void
    (*Free)(
        IN gckALLOCATOR Allocator,
        IN PLINUX_MDL Mdl
        );

    /**************************************************************************
    **
    ** MapUser
    **
    ** Map memory to user space.
    **
    ** INPUT:
    **      gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **      PLINUX_MDL Mdl
    **          Pointer to a Mdl.
    **
    **      PLINUX_MDL_MAP MdlMap
    **          Pointer to a MdlMap, mapped address is stored
    **          in MdlMap->vmaAddr
    **
    **      gctBOOL Cacheable
    **          Whether this mapping is cacheable.
    **
    ** OUTPUT:
    **
    **      Nothing.
    **
    */
    gctINT
    (*MapUser)(
        IN gckALLOCATOR Allocator,
        IN PLINUX_MDL Mdl,
        IN PLINUX_MDL_MAP MdlMap,
        IN gctBOOL Cacheable
        );

    /**************************************************************************
    **
    ** UnmapUser
    **
    ** Unmap address from user address space.
    **
    ** INPUT:
    **      gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **      gctPOINTER Logical
    **          Address to be unmap
    **
    **      gctUINT32 Size
    **          Size of address space
    **
    ** OUTPUT:
    **
    **      Nothing.
    **
    */
    void
    (*UnmapUser)(
        IN gckALLOCATOR Allocator,
        IN gctPOINTER Logical,
        IN gctUINT32 Size
        );

    /**************************************************************************
    **
    ** MapKernel
    **
    ** Map memory to kernel space.
    **
    ** INPUT:
    **      gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **      PLINUX_MDL Mdl
    **          Pointer to a Mdl object.
    **
    ** OUTPUT:
    **      gctPOINTER * Logical
    **          Mapped kernel address.
    */
    gceSTATUS
    (*MapKernel)(
        IN gckALLOCATOR Allocator,
        IN PLINUX_MDL Mdl,
        OUT gctPOINTER *Logical
        );

    /**************************************************************************
    **
    ** UnmapKernel
    **
    ** Unmap memory from kernel space.
    **
    ** INPUT:
    **      gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **      PLINUX_MDL Mdl
    **          Pointer to a Mdl object.
    **
    **      gctPOINTER Logical
    **          Mapped kernel address.
    **
    ** OUTPUT:
    **
    **      Nothing.
    **
    */
    gceSTATUS
    (*UnmapKernel)(
        IN gckALLOCATOR Allocator,
        IN PLINUX_MDL Mdl,
        IN gctPOINTER Logical
        );

    /**************************************************************************
    **
    ** LogicalToPhysical
    **
    ** Get physical address from logical address, logical
    ** address could be user virtual address or kernel
    ** virtual address.
    **
    ** INPUT:
    **      gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **      PLINUX_MDL Mdl
    **          Pointer to a Mdl object.
    **
    **      gctPOINTER Logical
    **          Mapped kernel address.
    **
    **      gctUINT32 ProcessID
    **          pid of current process.
    ** OUTPUT:
    **
    **      gctUINT32_PTR Physical
    **          Physical address.
    **
    */
    gceSTATUS
    (*LogicalToPhysical)(
        IN gckALLOCATOR Allocator,
        IN PLINUX_MDL Mdl,
        IN gctPOINTER Logical,
        IN gctUINT32 ProcessID,
        OUT gctPHYS_ADDR_T * Physical
        );

    /**************************************************************************
    **
    ** Cache
    **
    ** Maintain cache coherency.
    **
    ** INPUT:
    **      gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **      PLINUX_MDL Mdl
    **          Pointer to a Mdl object.
    **
    **      gctPOINTER Logical
    **          Logical address, could be user address or kernel address
    **
    **      gctUINT32_PTR Physical
    **          Physical address.
    **
    **      gctUINT32 Bytes
    **          Size of memory region.
    **
    **      gceCACHEOPERATION Opertaion
    **          Cache operation.
    **
    ** OUTPUT:
    **
    **      Nothing.
    **
    */
    gceSTATUS (*Cache)(
        IN gckALLOCATOR Allocator,
        IN PLINUX_MDL Mdl,
        IN gctPOINTER Logical,
        IN gctUINT32 Physical,
        IN gctUINT32 Bytes,
        IN gceCACHEOPERATION Operation
        );

    /**************************************************************************
    **
    ** Physical
    **
    ** Get physical address from a offset in memory region.
    **
    ** INPUT:
    **      gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **      PLINUX_MDL Mdl
    **          Pointer to a Mdl object.
    **
    **      gctUINT32 Offset
    **          Offset in this memory region.
    **
    ** OUTPUT:
    **      gctUINT32_PTR Physical
    **          Physical address.
    **
    */
    gceSTATUS (*Physical)(
        IN gckALLOCATOR Allocator,
        IN PLINUX_MDL Mdl,
        IN gctUINT32 Offset,
        OUT gctPHYS_ADDR_T * Physical
        );

    /**************************************************************************
    **
    ** Attach
    **
    ** Import memory allocated by an external allocator.
    **
    ** INPUT:
    **      gckALLOCATOR Allocator
    **          Pointer to an gckALLOCATOER object.
    **
    **      gctUINT32 Handle
    **          Handle of the memory.
    **
    ** OUTPUT:
    **      None.
    **
    */
    gceSTATUS (*Attach)(
        IN gckALLOCATOR Allocator,
        IN gcsATTACH_DESC_PTR Desc,
        OUT PLINUX_MDL Mdl
        );
}
gcsALLOCATOR_OPERATIONS;

typedef struct _gcsALLOCATOR
{
    /* Pointer to gckOS Object. */
    gckOS                     os;

    /* Name. */
    gctSTRING                 name;

    /* Operations. */
    gcsALLOCATOR_OPERATIONS*  ops;

    /* Capability of this allocator. */
    gctUINT32                 capability;

    struct list_head          head;

    /* Debugfs entry of this allocator. */
    gcsDEBUGFS_DIR            debugfsDir;

    /* Init allocator debugfs. */
    void                      (*debugfsInit)(gckALLOCATOR, gckDEBUGFS_DIR);

    /* Cleanup allocator debugfs. */
    void                      (*debugfsCleanup)(gckALLOCATOR);

    /* Private data used by customer allocator. */
    void *                    privateData;

    /* Private data destructor. */
    void                      (*privateDataDestructor)(void *);
}
gcsALLOCATOR;

typedef struct _gcsALLOCATOR_DESC
{
    /* Name of a allocator. */
    char *                    name;

    /* Entry function to construct a allocator. */
    gceSTATUS                 (*construct)(gckOS, gckALLOCATOR *);
}
gcsALLOCATOR_DESC;

typedef struct _gcsATTACH_DESC
{
    /* gcvALLOC_FLAG_DMABUF */
    gctUINT32                  handle;

    /* gcvALLOC_FLAG_USERMEMORY */
    gctPOINTER                 memory;
    gctUINT32                  physical;
    gctSIZE_T                  size;
}
gcsATTACH_DESC;

/*
* Helpers
*/

/* Fill a gcsALLOCATOR_DESC structure. */
#define gcmkDEFINE_ALLOCATOR_DESC(Name, Construct) \
    { \
        .name      = Name, \
        .construct = Construct, \
    }

/* Construct a allocator. */
gceSTATUS
gckALLOCATOR_Construct(
    IN gckOS Os,
    IN gcsALLOCATOR_OPERATIONS * Operations,
    OUT gckALLOCATOR * Allocator
    );

/*
    How to implement customer allocator

    Build in customer alloctor

        It is recommanded that customer allocator is implmented in independent
        source file(s) which is specified by CUSOMTER_ALLOCATOR_OBJS in Kbuld.

    Register gcsALLOCATOR

        For each customer specified allocator, a desciption entry must be added
        to allocatorArray defined in gc_hal_kernel_allocator_array.h.

        An entry in allocatorArray is a gcsALLOCATOR_DESC structure which describes
        name and constructor of a gckALLOCATOR object.


    Implement gcsALLOCATOR_DESC.init()

        In gcsALLOCATOR_DESC.init(), gckALLOCATOR_Construct should be called
        to create a gckALLOCATOR object, customer specified private data can
        be put in gcsALLOCATOR.privateData.


    Implement gcsALLOCATOR_OPERATIONS

        When call gckALLOCATOR_Construct to create a gckALLOCATOR object, a
        gcsALLOCATOR_OPERATIONS structure must be provided whose all members
        implemented.

*/
#endif
