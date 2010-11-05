/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#ifndef __gc_hal_user_context_h_
#define __gc_hal_user_context_h_

#ifdef __cplusplus
extern "C" {
#endif

/* gcoCONTEXT structure that hold the current context. */
struct _gcoCONTEXT
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gcoOS object. */
    gcoOS                       os;

    /* Pointer to gcoHARDWARE object. */
    gcoHARDWARE                 hardware;

    /* Context ID. */
    gctUINT64                   id;

    /* State mapping. */
    gctUINT32_PTR               map;
    gctSIZE_T                   stateCount;

    /* State hinting. */
    gctUINT8_PTR                hint;
    gctUINT8                    hintValue;
    gctSIZE_T                   hintCount;

    /* Context buffer. */
    gctUINT32_PTR               buffer;
    gctUINT32                   pipe3DIndex;
    gctUINT32                   pipe2DIndex;
    gctUINT32                   linkIndex;
    gctUINT32                   inUseIndex;
    gctSIZE_T                   bufferSize;

    /* Context buffer used for commitment. */
    gctSIZE_T                   bytes;
    gctPHYS_ADDR                physical;
    gctPOINTER                  logical;

    /* Pointer to final LINK command. */
    gctPOINTER                  link;

    /* Requested pipe select for context. */
    gctUINT32                   initialPipe;
    gctUINT32                   entryPipe;
    gctUINT32                   currentPipe;

    /* Flag to specify whether PostCommit needs to be called. */
    gctBOOL                     postCommit;

    /* Busy flag. */
    volatile gctBOOL *          inUse;

    /* Variables used for building state buffer. */
    gctUINT32                   lastAddress;
    gctSIZE_T                   lastSize;
    gctUINT32                   lastIndex;
    gctBOOL                     lastFixed;

    /* Hint array. */
    gctUINT32_PTR               hintArray;
    gctUINT32_PTR               hintIndex;
};

struct _gcoCMDBUF
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to gcoOS object. */
    gcoOS                       os;

    /* Pointer to gcoHARDWARE object. */
    gcoHARDWARE                 hardware;

    /* Physical address of command buffer. */
    gctPHYS_ADDR                physical;

    /* Logical address of command buffer. */
    gctPOINTER                  logical;

    /* Number of bytes in command buffer. */
    gctSIZE_T                   bytes;

    /* Start offset into the command buffer. */
    gctUINT32                   startOffset;

    /* Current offset into the command buffer. */
    gctUINT32                   offset;

    /* Number of free bytes in command buffer. */
    gctSIZE_T                   free;

#if gcdSECURE_USER
    /* Table of offsets that define the physical addresses to be mapped. */
    gctUINT32_PTR               hintTable;

    /* Current index into map table. */
    gctUINT32_PTR               hintIndex;

    /* Commit index for map table. */
    gctUINT32_PTR               hintCommit;
#endif
};

typedef struct _gcsQUEUE * gcsQUEUE_PTR;

typedef struct _gcsQUEUE
{
    /* Pointer to next gcsQUEUE structure. */
    gcsQUEUE_PTR                next;

#ifdef __QNXNTO__
    /* Size of this object. */
    gctSIZE_T                   bytes;
#endif

    /* Event information. */
    gcsHAL_INTERFACE            iface;
}
gcsQUEUE;

/* Event queue. */
struct _gcoQUEUE
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to gcoOS object. */
    gcoOS                       os;

    /* Pointer to current event queue. */
    gcsQUEUE_PTR                head;
    gcsQUEUE_PTR                tail;
};

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_user_context_h_ */

