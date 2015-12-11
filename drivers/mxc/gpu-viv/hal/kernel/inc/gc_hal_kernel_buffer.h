/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2015 Vivante Corporation
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
*    Copyright (C) 2014 - 2015 Vivante Corporation
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


#ifndef __gc_hal_kernel_buffer_h_
#define __gc_hal_kernel_buffer_h_

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
************************ Command Buffer and Event Objects **********************
\******************************************************************************/

/* The number of context buffers per user. */
#define gcdCONTEXT_BUFFER_COUNT 2

/* State delta record. */
typedef struct _gcsSTATE_DELTA_RECORD * gcsSTATE_DELTA_RECORD_PTR;
typedef struct _gcsSTATE_DELTA_RECORD
{
    /* State address. */
    gctUINT                     address;

    /* State mask. */
    gctUINT32                   mask;

    /* State data. */
    gctUINT32                   data;
}
gcsSTATE_DELTA_RECORD;

/* State delta. */
typedef struct _gcsSTATE_DELTA
{
    /* For debugging: the number of delta in the order of creation. */
#if gcmIS_DEBUG(gcdDEBUG_CODE)
    gctUINT                     num;
#endif

    /* Main state delta ID. Every time state delta structure gets reinitialized,
       main ID is incremented. If main state ID overflows, all map entry IDs get
       reinitialized to make sure there is no potential erroneous match after
       the overflow.*/
    gctUINT                     id;

    /* The number of contexts pending modification by the delta. */
    gctINT                      refCount;

    /* Vertex element count for the delta buffer. */
    gctUINT                     elementCount;

    /* Number of states currently stored in the record array. */
    gctUINT                     recordCount;

    /* Record array; holds all modified states in gcsSTATE_DELTA_RECORD. */
    gctUINT64                   recordArray;

    /* Map entry ID is used for map entry validation. If map entry ID does not
       match the main state delta ID, the entry and the corresponding state are
       considered not in use. */
    gctUINT64                   mapEntryID;
    gctUINT                     mapEntryIDSize;

    /* If the map entry ID matches the main state delta ID, index points to
       the state record in the record array. */
    gctUINT64                   mapEntryIndex;

    /* Previous and next state deltas in gcsSTATE_DELTA. */
    gctUINT64                   prev;
    gctUINT64                   next;
}
gcsSTATE_DELTA;

/* Command buffer patch record. */
struct _gcsPATCH
{
    /* Pointer within the buffer. */
    gctUINT32_PTR               pointer;

    /* 32-bit data to write at the specified offset. */
    gctUINT32                   data;
};

/* List of patches for the command buffer. */
struct _gcsPATCH_LIST
{
    /* Array of patch records. */
    struct _gcsPATCH            patch[1024];

    /* Number of patches in the array. */
    gctUINT                     count;

    /* Next item in the list. */
    struct _gcsPATCH_LIST       *next;
};

/* Command buffer object. */
struct _gcoCMDBUF
{
    /* The object. */
    gcsOBJECT                   object;

    /* Commit count. */
    gctUINT64                   commitCount;

    /* Command buffer entry and exit pipes. */
    gcePIPE_SELECT              entryPipe;
    gcePIPE_SELECT              exitPipe;

    /* Feature usage flags. */
    gctBOOL                     using2D;
    gctBOOL                     using3D;
    gctBOOL                     usingFilterBlit;
    gctBOOL                     usingPalette;

    /* Physical address of command buffer. Just a name. */
    gctUINT32                   physical;

    /* Logical address of command buffer. */
    gctUINT64                   logical;

    /* Number of bytes in command buffer. */
    gctUINT32                   bytes;

    /* Start offset into the command buffer. */
    gctUINT32                   startOffset;

    /* Current offset into the command buffer. */
    gctUINT32                   offset;

    /* Number of free bytes in command buffer. */
    gctUINT32                   free;

    /* Location of the last reserved area. */
    gctUINT64                   lastReserve;
    gctUINT32                   lastOffset;

#if gcdSECURE_USER
    /* Hint array for the current command buffer. */
    gctUINT                     hintArraySize;
    gctUINT64                   hintArray;
    gctUINT64                   hintArrayTail;
#endif

#if gcmIS_DEBUG(gcdDEBUG_CODE)
    /* Last load state command location and hardware address. */
    gctUINT64                   lastLoadStatePtr;
    gctUINT32                   lastLoadStateAddress;
    gctUINT32                   lastLoadStateCount;
#endif

    /* Completion signal. */
    gctSIGNAL                   signal;

    /* List of patches. */
    struct _gcsPATCH_LIST       *patchHead;
    struct _gcsPATCH_LIST       *patchTail;

    /* Link to the siblings. */
    gcoCMDBUF                   prev;
    gcoCMDBUF                   next;
};

typedef struct _gcsQUEUE
{
    /* Pointer to next gcsQUEUE structure in gcsQUEUE. */
    gctUINT64                   next;

    /* Event information. */
    gcsHAL_INTERFACE            iface;
}
gcsQUEUE;

/* Event queue. */
struct _gcoQUEUE
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to current event queue. */
    gcsQUEUE_PTR                head;
    gcsQUEUE_PTR                tail;

    /* chunks of the records. */
    gctPOINTER                  chunks;

    /* List of free records. */
    gcsQUEUE_PTR                freeList;

    #define gcdIN_QUEUE_RECORD_LIMIT 16
    /* Number of records currently in queue */
    gctUINT32                   recordCount;
};

struct _gcsTEMPCMDBUF
{
    gctUINT32 currentByteSize;
    gctPOINTER buffer;
    gctBOOL  inUse;
};

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_kernel_buffer_h_ */
