/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 Vivante Corporation
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
*    Copyright (C) 2014  Vivante Corporation
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


#ifndef __gc_hal_vg_h_
#define __gc_hal_vg_h_

#ifdef __cplusplus
extern "C" {
#endif


#include "gc_hal_rename.h"
#include "gc_hal_types.h"
#include "gc_hal_enum.h"
#include "gc_hal_base.h"

#if gcdENABLE_VG

/* Thread routine type. */
#if defined(LINUX)
    typedef gctINT              gctTHREADFUNCRESULT;
    typedef gctPOINTER          gctTHREADFUNCPARAMETER;
#   define  gctTHREADFUNCTYPE
#elif defined(WIN32)
    typedef gctUINT             gctTHREADFUNCRESULT;
    typedef gctPOINTER          gctTHREADFUNCPARAMETER;
#   define  gctTHREADFUNCTYPE   __stdcall
#elif defined(__QNXNTO__)
    typedef void *              gctTHREADFUNCRESULT;
    typedef gctPOINTER          gctTHREADFUNCPARAMETER;
#   define  gctTHREADFUNCTYPE
#endif

typedef gctTHREADFUNCRESULT (gctTHREADFUNCTYPE * gctTHREADFUNC) (
    gctTHREADFUNCPARAMETER ThreadParameter
    );


#if defined(gcvDEBUG)
#   undef gcvDEBUG
#endif

#define gcdFORCE_DEBUG 0
#define gcdFORCE_MESSAGES 0


#if DBG || defined(DEBUG) || defined(_DEBUG) || gcdFORCE_DEBUG
#   define gcvDEBUG 1
#else
#   define gcvDEBUG 0
#endif

#define _gcmERROR_RETURN(prefix, func) \
    status = func; \
    if (gcmIS_ERROR(status)) \
    { \
        prefix##PRINT_VERSION(); \
        prefix##TRACE(gcvLEVEL_ERROR, \
            #prefix "ERR_RETURN: status=%d(%s) @ %s(%d)", \
            status, gcoOS_DebugStatus2Name(status), __FUNCTION__, __LINE__); \
        return status; \
    } \
    do { } while (gcvFALSE)

#define gcmERROR_RETURN(func)         _gcmERROR_RETURN(gcm, func)

#define gcmLOG_LOCATION()

#define gcmkIS_ERROR(status)        (status < 0)

#define gcmALIGNDOWN(n, align) \
( \
    (n) & ~((align) - 1) \
)

#define gcmIS_VALID_INDEX(Index, Array) \
    (((gctUINT) (Index)) < gcmCOUNTOF(Array))


#define gcmIS_NAN(x) \
( \
    ((* (gctUINT32_PTR) &(x)) & 0x7FFFFFFF) == 0x7FFFFFFF \
)

#define gcmLERP(v1, v2, w) \
    ((v1) * (w) + (v2) * (1.0f - (w)))

#define gcmINTERSECT(Start1, Start2, Length) \
    (gcmABS((Start1) - (Start2)) < (Length))

/*******************************************************************************
**
**  gcmERR_GOTO
**
**      Prints a message and terminates the current loop on error.
**
**  ASSUMPTIONS:
**
**      'status' variable of gceSTATUS type must be defined.
**
**  ARGUMENTS:
**
**      Function
**          Function to evaluate.
*/

#define gcmERR_GOTO(Function) \
    status = Function; \
    if (gcmIS_ERROR(status)) \
    { \
        gcmTRACE( \
            gcvLEVEL_ERROR, \
            "gcmERR_GOTO: status=%d @ line=%d in function %s.\n", \
            status, __LINE__, __FUNCTION__ \
            ); \
        goto ErrorHandler; \
    }

#if gcvDEBUG || gcdFORCE_MESSAGES
#   define gcmVERIFY_BOOLEAN(Expression) \
        gcmASSERT( \
            ( (Expression) == gcvFALSE ) || \
            ( (Expression) == gcvTRUE  )    \
            )
#else
#   define gcmVERIFY_BOOLEAN(Expression)
#endif

/*******************************************************************************
**
**  gcmVERIFYFIELDFIT
**
**      Verify whether the value fits in the field.
**
**  ARGUMENTS:
**
**      data    Data value.
**      reg     Name of register.
**      field   Name of field within register.
**      value   Value for field.
*/
#define gcmVERIFYFIELDFIT(reg, field, value) \
    gcmASSERT( \
        (value) <= gcmFIELDMAX(reg, field) \
        )
/*******************************************************************************
**
**  gcmFIELDMAX
**
**      Get field maximum value.
**
**  ARGUMENTS:
**
**      reg     Name of register.
**      field   Name of field within register.
*/
#define gcmFIELDMAX(reg, field) \
( \
    (gctUINT32) \
        ( \
        (__gcmGETSIZE(reg##_##field) == 32) \
                ?  ~0 \
                : (~(~0 << __gcmGETSIZE(reg##_##field))) \
        ) \
)


/* ANSI C does not have the 'f' functions, define replacements here. */
#define gcmSINF(x)                      ((gctFLOAT) sin(x))
#define gcmCOSF(x)                      ((gctFLOAT) cos(x))
#define gcmASINF(x)                     ((gctFLOAT) asin(x))
#define gcmACOSF(x)                     ((gctFLOAT) acos(x))
#define gcmSQRTF(x)                     ((gctFLOAT) sqrt(x))
#define gcmFABSF(x)                     ((gctFLOAT) fabs(x))
#define gcmFMODF(x, y)                  ((gctFLOAT) fmod((x), (y)))
#define gcmCEILF(x)                     ((gctFLOAT) ceil(x))
#define gcmFLOORF(x)                    ((gctFLOAT) floor(x))



/* Fixed point constants. */
#define gcvZERO_X               ((gctFIXED_POINT) 0x00000000)
#define gcvHALF_X               ((gctFIXED_POINT) 0x00008000)
#define gcvONE_X                ((gctFIXED_POINT) 0x00010000)
#define gcvNEGONE_X             ((gctFIXED_POINT) 0xFFFF0000)
#define gcvTWO_X                ((gctFIXED_POINT) 0x00020000)

/* Integer constants. */
#define gcvMAX_POS_INT          ((gctINT) 0x7FFFFFFF)
#define gcvMAX_NEG_INT          ((gctINT) 0x80000000)

/* Float constants. */
#define gcvMAX_POS_FLOAT        ((gctFLOAT)  3.4028235e+038)
#define gcvMAX_NEG_FLOAT        ((gctFLOAT) -3.4028235e+038)

/******************************************************************************\
***************************** Miscellaneous Macro ******************************
\******************************************************************************/

#define gcmKB2BYTES(Kilobyte) \
( \
    (Kilobyte) << 10 \
)

#define gcmMB2BYTES(Megabyte) \
( \
    (Megabyte) << 20 \
)

#define gcmMAT(Matrix, Row, Column) \
( \
    (Matrix) [(Row) * 3 + (Column)] \
)

#define gcmMAKE2CHAR(Char1, Char2) \
( \
    ((gctUINT16) (gctUINT8) (Char1) << 0) | \
    ((gctUINT16) (gctUINT8) (Char2) << 8) \
)

#define gcmMAKE4CHAR(Char1, Char2, Char3, Char4) \
( \
    ((gctUINT32)(gctUINT8) (Char1) <<  0) | \
    ((gctUINT32)(gctUINT8) (Char2) <<  8) | \
    ((gctUINT32)(gctUINT8) (Char3) << 16) | \
    ((gctUINT32)(gctUINT8) (Char4) << 24) \
)

/* some platforms need to fix the physical address for HW to access*/
#define gcmFIXADDRESS(address) \
(\
    (address)\
)

#define gcmkFIXADDRESS(address) \
(\
    (address)\
)

/******************************************************************************\
****************************** Kernel Debug Macro ******************************
\******************************************************************************/

/* Set signal to signaled state for specified process. */
gceSTATUS
gckOS_SetSignal(
    IN gckOS Os,
    IN gctHANDLE Process,
    IN gctSIGNAL Signal
    );

/* Return the kernel logical pointer for the given physical one. */
gceSTATUS
gckOS_GetKernelLogical(
    IN gckOS Os,
    IN gctUINT32 Address,
    OUT gctPOINTER * KernelPointer
    );

/* Return the kernel logical pointer for the given physical one. */
gceSTATUS
gckOS_GetKernelLogicalEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT32 Address,
    OUT gctPOINTER * KernelPointer
    );

/*----------------------------------------------------------------------------*/
/*----------------------------- Semaphore Object -----------------------------*/

/* Increment the value of a semaphore. */
gceSTATUS
gckOS_IncrementSemaphore(
    IN gckOS Os,
    IN gctSEMAPHORE Semaphore
    );

/* Decrement the value of a semaphore (waiting might occur). */
gceSTATUS
gckOS_DecrementSemaphore(
    IN gckOS Os,
    IN gctSEMAPHORE Semaphore
    );


/*----------------------------------------------------------------------------*/
/*------------------------------- Thread Object ------------------------------*/

/* Start a thread. */
gceSTATUS
gckOS_StartThread(
    IN gckOS Os,
    IN gctTHREADFUNC ThreadFunction,
    IN gctPOINTER ThreadParameter,
    OUT gctTHREAD * Thread
    );

/* Stop a thread. */
gceSTATUS
gckOS_StopThread(
    IN gckOS Os,
    IN gctTHREAD Thread
    );

/* Verify whether the thread is still running. */
gceSTATUS
gckOS_VerifyThread(
    IN gckOS Os,
    IN gctTHREAD Thread
    );


/* Construct a new gckVGKERNEL object. */
gceSTATUS
gckVGKERNEL_Construct(
    IN gckOS Os,
    IN gctPOINTER Context,
    IN gckKERNEL  inKernel,
    OUT gckVGKERNEL * Kernel
    );

/* Destroy an gckVGKERNEL object. */
gceSTATUS
gckVGKERNEL_Destroy(
    IN gckVGKERNEL Kernel
    );

/* Allocate linear video memory. */
gceSTATUS
gckVGKERNEL_AllocateLinearMemory(
    IN gckKERNEL Kernel,
    IN OUT gcePOOL * Pool,
    IN gctSIZE_T Bytes,
    IN gctUINT32 Alignment,
    IN gceSURF_TYPE Type,
    OUT gcuVIDMEM_NODE_PTR * Node
    );

/* Unmap memory. */
gceSTATUS
gckKERNEL_UnmapMemory(
    IN gckKERNEL Kernel,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    );

/* Dispatch a user-level command. */
gceSTATUS
gckVGKERNEL_Dispatch(
    IN gckKERNEL Kernel,
    IN gctBOOL FromUser,
    IN OUT struct _gcsHAL_INTERFACE * Interface
    );

/* Query command buffer requirements. */
gceSTATUS
gckKERNEL_QueryCommandBuffer(
    IN gckKERNEL Kernel,
    OUT gcsCOMMAND_BUFFER_INFO_PTR Information
    );

/******************************************************************************\
******************************* gckVGHARDWARE Object ******************************
\******************************************************************************/

/* Construct a new gckVGHARDWARE object. */
gceSTATUS
gckVGHARDWARE_Construct(
    IN gckOS Os,
    OUT gckVGHARDWARE * Hardware
    );

/* Destroy an gckVGHARDWARE object. */
gceSTATUS
gckVGHARDWARE_Destroy(
    IN gckVGHARDWARE Hardware
    );

/* Query system memory requirements. */
gceSTATUS
gckVGHARDWARE_QuerySystemMemory(
    IN gckVGHARDWARE Hardware,
    OUT gctSIZE_T * SystemSize,
    OUT gctUINT32 * SystemBaseAddress
    );

/* Build virtual address. */
gceSTATUS
gckVGHARDWARE_BuildVirtualAddress(
    IN gckVGHARDWARE Hardware,
    IN gctUINT32 Index,
    IN gctUINT32 Offset,
    OUT gctUINT32 * Address
    );

/* Kickstart the command processor. */
gceSTATUS
gckVGHARDWARE_Execute(
    IN gckVGHARDWARE Hardware,
    IN gctUINT32 Address,
    IN gctUINT32 Count
    );

/* Query the available memory. */
gceSTATUS
gckVGHARDWARE_QueryMemory(
    IN gckVGHARDWARE Hardware,
    OUT gctSIZE_T * InternalSize,
    OUT gctUINT32 * InternalBaseAddress,
    OUT gctUINT32 * InternalAlignment,
    OUT gctSIZE_T * ExternalSize,
    OUT gctUINT32 * ExternalBaseAddress,
    OUT gctUINT32 * ExternalAlignment,
    OUT gctUINT32 * HorizontalTileSize,
    OUT gctUINT32 * VerticalTileSize
    );

/* Query the identity of the hardware. */
gceSTATUS
gckVGHARDWARE_QueryChipIdentity(
    IN gckVGHARDWARE Hardware,
    OUT gceCHIPMODEL* ChipModel,
    OUT gctUINT32* ChipRevision,
    OUT gctUINT32* ChipFeatures,
    OUT gctUINT32* ChipMinorFeatures,
    OUT gctUINT32* ChipMinorFeatures1
    );

/* Convert an API format. */
gceSTATUS
gckVGHARDWARE_ConvertFormat(
    IN gckVGHARDWARE Hardware,
    IN gceSURF_FORMAT Format,
    OUT gctUINT32 * BitsPerPixel,
    OUT gctUINT32 * BytesPerTile
    );

/* Split a harwdare specific address into API stuff. */
gceSTATUS
gckVGHARDWARE_SplitMemory(
    IN gckVGHARDWARE Hardware,
    IN gctUINT32 Address,
    OUT gcePOOL * Pool,
    OUT gctUINT32 * Offset
    );

/* Align size to tile boundary. */
gceSTATUS
gckVGHARDWARE_AlignToTile(
    IN gckVGHARDWARE Hardware,
    IN gceSURF_TYPE Type,
    IN OUT gctUINT32_PTR Width,
    IN OUT gctUINT32_PTR Height
    );

/* Convert logical address to hardware specific address. */
gceSTATUS
gckVGHARDWARE_ConvertLogical(
    IN gckVGHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctBOOL InUserSpace,
    OUT gctUINT32 * Address
    );

/* Program MMU. */
gceSTATUS
gckVGHARDWARE_SetMMU(
    IN gckVGHARDWARE Hardware,
    IN gctPOINTER Logical
    );

/* Flush the MMU. */
gceSTATUS
gckVGHARDWARE_FlushMMU(
    IN gckVGHARDWARE Hardware
    );

/* Get idle register. */
gceSTATUS
gckVGHARDWARE_GetIdle(
    IN gckVGHARDWARE Hardware,
    OUT gctUINT32 * Data
    );

/* Flush the caches. */
gceSTATUS
gckVGHARDWARE_Flush(
    IN gckVGHARDWARE Hardware,
    IN gceKERNEL_FLUSH Flush,
    IN gctPOINTER Logical,
    IN OUT gctSIZE_T * Bytes
    );

/* Enable/disable fast clear. */
gceSTATUS
gckVGHARDWARE_SetFastClear(
    IN gckVGHARDWARE Hardware,
    IN gctINT Enable
    );

gceSTATUS
gckVGHARDWARE_ReadInterrupt(
    IN gckVGHARDWARE Hardware,
    OUT gctUINT32_PTR IDs
    );

/* Power management. */
gceSTATUS
gckVGHARDWARE_SetPowerManagementState(
    IN gckVGHARDWARE Hardware,
    IN gceCHIPPOWERSTATE State
    );

gceSTATUS
gckVGHARDWARE_QueryPowerManagementState(
    IN gckVGHARDWARE Hardware,
    OUT gceCHIPPOWERSTATE* State
    );

gceSTATUS
gckVGHARDWARE_SetPowerManagement(
    IN gckVGHARDWARE Hardware,
    IN gctBOOL PowerManagement
    );

gceSTATUS
gckVGHARDWARE_SetPowerOffTimeout(
    IN gckVGHARDWARE  Hardware,
    IN gctUINT32    Timeout
    );

gceSTATUS
gckVGHARDWARE_QueryPowerOffTimeout(
    IN gckVGHARDWARE  Hardware,
    OUT gctUINT32*  Timeout
    );

gceSTATUS
gckVGHARDWARE_QueryIdle(
    IN gckVGHARDWARE Hardware,
    OUT gctBOOL_PTR IsIdle
    );
/******************************************************************************\
*************************** Command Buffer Structures **************************
\******************************************************************************/

/* Vacant command buffer marker. */
#define gcvVACANT_BUFFER        ((gcsCOMPLETION_SIGNAL_PTR) ((gctSIZE_T)1))

/* Command buffer header. */
typedef struct _gcsCMDBUFFER * gcsCMDBUFFER_PTR;
typedef struct _gcsCMDBUFFER
{
    /* Pointer to the completion signal. */
    gcsCOMPLETION_SIGNAL_PTR    completion;

    /* The user sets this to the node of the container buffer whitin which
       this particular command buffer resides. The kernel sets this to the
       node of the internally allocated buffer. */
    gcuVIDMEM_NODE_PTR          node;

    /* Command buffer hardware address. */
    gctUINT32                   address;

    /* The offset of the buffer from the beginning of the header. */
    gctUINT32                   bufferOffset;

    /* Size of the area allocated for the data portion of this particular
       command buffer (headers and tail reserves are excluded). */
    gctUINT32                   size;

    /* Offset into the buffer [0..size]; reflects exactly how much data has
       been put into the command buffer. */
    gctUINT                     offset;

    /* The number of command units in the buffer for the hardware to
       execute. */
    gctUINT32                   dataCount;

    /* MANAGED BY : user HAL (gcoBUFFER object).
       USED BY    : user HAL (gcoBUFFER object).
       Points to the immediate next allocated command buffer. */
    gcsCMDBUFFER_PTR            nextAllocated;

    /* MANAGED BY : user layers (HAL and drivers).
       USED BY    : kernel HAL (gcoBUFFER object).
       Points to the next subbuffer if any. A family of subbuffers are chained
       together and are meant to be executed inseparably as a unit. Meaning
       that context switching cannot occur while a chain of subbuffers is being
       executed. */
    gcsCMDBUFFER_PTR            nextSubBuffer;
}
gcsCMDBUFFER;

/* Command queue element. */
typedef struct _gcsVGCMDQUEUE
{
    /* Pointer to the command buffer header. */
    gcsCMDBUFFER_PTR            commandBuffer;

    /* Dynamic vs. static command buffer state. */
    gctBOOL                     dynamic;
}
gcsVGCMDQUEUE;

/* Context map entry. */
typedef struct _gcsVGCONTEXT_MAP
{
    /* State index. */
    gctUINT32                   index;

    /* New state value. */
    gctUINT32                   data;

    /* Points to the next entry in the mod list. */
    gcsVGCONTEXT_MAP_PTR            next;
}
gcsVGCONTEXT_MAP;

/* gcsVGCONTEXT structure that holds the current context. */
typedef struct _gcsVGCONTEXT
{
    /* Context ID. */
    gctUINT64                   id;

    /* State caching ebable flag. */
    gctBOOL                     stateCachingEnabled;

    /* Current pipe. */
    gctUINT32                   currentPipe;

    /* State map/mod buffer. */
    gctUINT32                   mapFirst;
    gctUINT32                   mapLast;
    gcsVGCONTEXT_MAP_PTR        mapContainer;
    gcsVGCONTEXT_MAP_PTR        mapPrev;
    gcsVGCONTEXT_MAP_PTR        mapCurr;
    gcsVGCONTEXT_MAP_PTR        firstPrevMap;
    gcsVGCONTEXT_MAP_PTR        firstCurrMap;

    /* Main context buffer. */
    gcsCMDBUFFER_PTR            header;
    gctUINT32_PTR               buffer;

    /* Completion signal. */
    gctHANDLE                   process;
    gctSIGNAL                   signal;

#if defined(__QNXNTO__)
    gctSIGNAL                   userSignal;
    gctINT32                    coid;
    gctINT32                    rcvid;
#endif
}
gcsVGCONTEXT;

/* User space task header. */
typedef struct _gcsTASK * gcsTASK_PTR;
typedef struct _gcsTASK
{
    /* Pointer to the next task for the same interrupt in user space. */
    gcsTASK_PTR                 next;

    /* Size of the task data that immediately follows the structure. */
    gctUINT                     size;

    /* Task data starts here. */
    /* ... */
}
gcsTASK;

/* User space task master table entry. */
typedef struct _gcsTASK_MASTER_ENTRY * gcsTASK_MASTER_ENTRY_PTR;
typedef struct _gcsTASK_MASTER_ENTRY
{
    /* Pointers to the head and to the tail of the task chain. */
    gcsTASK_PTR                 head;
    gcsTASK_PTR                 tail;
}
gcsTASK_MASTER_ENTRY;

/* User space task master table entry. */
typedef struct _gcsTASK_MASTER_TABLE
{
    /* Table with one entry per block. */
    gcsTASK_MASTER_ENTRY        table[gcvBLOCK_COUNT];

    /* The total number of tasks sckeduled. */
    gctUINT                     count;

    /* The total size of event data in bytes. */
    gctUINT                     size;

#if defined(__QNXNTO__)
    gctINT32                    coid;
    gctINT32                    rcvid;
#endif
}
gcsTASK_MASTER_TABLE;

/******************************************************************************\
***************************** gckVGINTERRUPT Object ******************************
\******************************************************************************/

typedef struct _gckVGINTERRUPT * gckVGINTERRUPT;

typedef gceSTATUS (* gctINTERRUPT_HANDLER)(
    IN gckVGKERNEL Kernel
    );

gceSTATUS
gckVGINTERRUPT_Construct(
    IN gckVGKERNEL Kernel,
    OUT gckVGINTERRUPT * Interrupt
    );

gceSTATUS
gckVGINTERRUPT_Destroy(
    IN gckVGINTERRUPT Interrupt
    );

gceSTATUS
gckVGINTERRUPT_Enable(
    IN gckVGINTERRUPT Interrupt,
    IN OUT gctINT32_PTR Id,
    IN gctINTERRUPT_HANDLER Handler
    );

gceSTATUS
gckVGINTERRUPT_Disable(
    IN gckVGINTERRUPT Interrupt,
    IN gctINT32 Id
    );

#ifndef __QNXNTO__

gceSTATUS
gckVGINTERRUPT_Enque(
    IN gckVGINTERRUPT Interrupt
    );

#else

gceSTATUS
gckVGINTERRUPT_Enque(
    IN gckVGINTERRUPT Interrupt,
    OUT gckOS *Os,
    OUT gctSEMAPHORE *Semaphore
    );

#endif

gceSTATUS
gckVGINTERRUPT_DumpState(
    IN gckVGINTERRUPT Interrupt
    );


/******************************************************************************\
******************************* gckVGCOMMAND Object *******************************
\******************************************************************************/

typedef struct _gckVGCOMMAND *      gckVGCOMMAND;

/* Construct a new gckVGCOMMAND object. */
gceSTATUS
gckVGCOMMAND_Construct(
    IN gckVGKERNEL Kernel,
    IN gctUINT TaskGranularity,
    IN gctUINT QueueSize,
    OUT gckVGCOMMAND * Command
    );

/* Destroy an gckVGCOMMAND object. */
gceSTATUS
gckVGCOMMAND_Destroy(
    IN gckVGCOMMAND Command
    );

/* Query command buffer attributes. */
gceSTATUS
gckVGCOMMAND_QueryCommandBuffer(
    IN gckVGCOMMAND Command,
    OUT gcsCOMMAND_BUFFER_INFO_PTR Information
    );

/* Allocate a command queue. */
gceSTATUS
gckVGCOMMAND_Allocate(
    IN gckVGCOMMAND Command,
    IN gctSIZE_T Size,
    OUT gcsCMDBUFFER_PTR * CommandBuffer,
    OUT gctPOINTER * Data
    );

/* Release memory held by the command queue. */
gceSTATUS
gckVGCOMMAND_Free(
    IN gckVGCOMMAND Command,
    IN gcsCMDBUFFER_PTR CommandBuffer
    );

/* Schedule the command queue for execution. */
gceSTATUS
gckVGCOMMAND_Execute(
    IN gckVGCOMMAND Command,
    IN gcsCMDBUFFER_PTR CommandBuffer
    );

/* Commit a buffer to the command queue. */
gceSTATUS
gckVGCOMMAND_Commit(
    IN gckVGCOMMAND Command,
    IN gcsVGCONTEXT_PTR Context,
    IN gcsVGCMDQUEUE_PTR Queue,
    IN gctUINT EntryCount,
    IN gcsTASK_MASTER_TABLE_PTR TaskTable
    );

/******************************************************************************\
********************************* gckVGMMU Object ********************************
\******************************************************************************/

typedef struct _gckVGMMU *          gckVGMMU;

/* Construct a new gckVGMMU object. */
gceSTATUS
gckVGMMU_Construct(
    IN gckVGKERNEL Kernel,
    IN gctUINT32 MmuSize,
    OUT gckVGMMU * Mmu
    );

/* Destroy an gckVGMMU object. */
gceSTATUS
gckVGMMU_Destroy(
    IN gckVGMMU Mmu
    );

/* Allocate pages inside the MMU. */
gceSTATUS
gckVGMMU_AllocatePages(
    IN gckVGMMU Mmu,
    IN gctSIZE_T PageCount,
    OUT gctPOINTER * PageTable,
    OUT gctUINT32 * Address
    );

/* Remove a page table from the MMU. */
gceSTATUS
gckVGMMU_FreePages(
    IN gckVGMMU Mmu,
    IN gctPOINTER PageTable,
    IN gctSIZE_T PageCount
    );

/* Set the MMU page with info. */
gceSTATUS
gckVGMMU_SetPage(
   IN gckVGMMU Mmu,
   IN gctUINT32 PageAddress,
   IN gctUINT32 *PageEntry
   );

/* Flush MMU */
gceSTATUS
gckVGMMU_Flush(
   IN gckVGMMU Mmu
   );

#endif /* gcdENABLE_VG */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __gc_hal_h_ */
