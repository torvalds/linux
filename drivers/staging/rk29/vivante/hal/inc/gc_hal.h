/****************************************************************************
*
*    Copyright (C) 2005 - 2011 by Vivante Corp.
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




#ifndef __gc_hal_h_
#define __gc_hal_h_

// dkm: 方便调用系统的函数
#include <linux/kernel.h>
#include <linux/module.h>
#include "gc_hal_types.h"
#include "gc_hal_enum.h"
#include "gc_hal_base.h"
#include "gc_hal_profiler.h"

enum {
	D_ERROR	= 1U << 0,
	D_IOCTL	= 1U << 1,
	D_IRQ   = 1U << 2,
};
extern uint gpu_dmask;
#define dprintk(mask, fmt, ...) do { if (mask & gpu_dmask) printk("gpu: " fmt, ##__VA_ARGS__); } while (0)
//#define dprintk(mask, fmt, ...) do { if (mask & gpu_dmask) printk(fmt, ##__VA_ARGS__); } while (0)

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
******************************* Alignment Macros *******************************
\******************************************************************************/

#define gcmALIGN(n, align) \
( \
    ((n) + ((align) - 1)) & ~((align) - 1) \
)

/******************************************************************************\
***************************** Element Count Macro *****************************
\******************************************************************************/

#define gcmSIZEOF(a) \
( \
    (gctSIZE_T) (sizeof(a)) \
)

#define gcmCOUNTOF(a) \
( \
    sizeof(a) / sizeof(a[0]) \
)

/******************************************************************************\
******************************** gcsOBJECT Object *******************************
\******************************************************************************/

/* Type of objects. */
typedef enum _gceOBJECT_TYPE
{
    gcvOBJ_UNKNOWN              = 0,
    gcvOBJ_2D                   = gcmCC('2','D',' ',' '),
    gcvOBJ_3D                   = gcmCC('3','D',' ',' '),
    gcvOBJ_ATTRIBUTE            = gcmCC('A','T','T','R'),
    gcvOBJ_BRUSHCACHE           = gcmCC('B','R','U','$'),
    gcvOBJ_BRUSHNODE            = gcmCC('B','R','U','n'),
    gcvOBJ_BRUSH                = gcmCC('B','R','U','o'),
    gcvOBJ_BUFFER               = gcmCC('B','U','F','R'),
    gcvOBJ_COMMAND              = gcmCC('C','M','D',' '),
    gcvOBJ_COMMANDBUFFER        = gcmCC('C','M','D','B'),
    gcvOBJ_CONTEXT              = gcmCC('C','T','X','T'),
    gcvOBJ_DEVICE               = gcmCC('D','E','V',' '),
    gcvOBJ_DUMP                 = gcmCC('D','U','M','P'),
    gcvOBJ_EVENT                = gcmCC('E','V','N','T'),
    gcvOBJ_FUNCTION             = gcmCC('F','U','N','C'),
    gcvOBJ_HAL                  = gcmCC('H','A','L',' '),
    gcvOBJ_HARDWARE             = gcmCC('H','A','R','D'),
    gcvOBJ_HEAP                 = gcmCC('H','E','A','P'),
    gcvOBJ_INDEX                = gcmCC('I','N','D','X'),
    gcvOBJ_INTERRUPT            = gcmCC('I','N','T','R'),
    gcvOBJ_KERNEL               = gcmCC('K','E','R','N'),
    gcvOBJ_MEMORYBUFFER         = gcmCC('M','E','M','B'),
    gcvOBJ_MMU                  = gcmCC('M','M','U',' '),
    gcvOBJ_OS                   = gcmCC('O','S',' ',' '),
    gcvOBJ_OUTPUT               = gcmCC('O','U','T','P'),
    gcvOBJ_PAINT                = gcmCC('P','N','T',' '),
    gcvOBJ_PATH                 = gcmCC('P','A','T','H'),
    gcvOBJ_QUEUE                = gcmCC('Q','U','E',' '),
    gcvOBJ_SAMPLER              = gcmCC('S','A','M','P'),
    gcvOBJ_SHADER               = gcmCC('S','H','D','R'),
    gcvOBJ_STREAM               = gcmCC('S','T','R','M'),
    gcvOBJ_SURF                 = gcmCC('S','U','R','F'),
    gcvOBJ_TEXTURE              = gcmCC('T','X','T','R'),
    gcvOBJ_UNIFORM              = gcmCC('U','N','I','F'),
    gcvOBJ_VARIABLE             = gcmCC('V','A','R','I'),
    gcvOBJ_VERTEX               = gcmCC('V','R','T','X'),
    gcvOBJ_VIDMEM               = gcmCC('V','M','E','M'),
    gcvOBJ_VG                   = gcmCC('V','G',' ',' '),
}
gceOBJECT_TYPE;

/* gcsOBJECT object defintinon. */
typedef struct _gcsOBJECT
{
    /* Type of an object. */
    gceOBJECT_TYPE              type;
}
gcsOBJECT;

/* Kernel settings. */
typedef struct _gcsKERNEL_SETTINGS
{
    /* Used RealTime signal between kernel and user. */
    gctINT signal;
}
gcsKERNEL_SETTINGS;

typedef struct _gckHARDWARE *       gckHARDWARE;

/*******************************************************************************
**
**  gcmVERIFY_OBJECT
**
**      Assert if an object is invalid or is not of the specified type.  If the
**      object is invalid or not of the specified type, gcvSTATUS_INVALID_OBJECT
**      will be returned from the current function.  In retail mode this macro
**      does nothing.
**
**  ARGUMENTS:
**
**      obj     Object to test.
**      t       Expected type of the object.
*/
#ifndef EGL_API_ANDROID
#   define _gcmVERIFY_OBJECT(prefix, obj, t) \
        do \
        { \
            if ((obj) == gcvNULL) \
            { \
                prefix##TRACE(gcvLEVEL_ERROR, \
                              #prefix "VERIFY_OBJECT failed: NULL"); \
                prefix##TRACE(gcvLEVEL_ERROR, "  expected: %c%c%c%c", \
                              gcmCC_PRINT(t)); \
                prefix##ASSERT((obj) != gcvNULL); \
                prefix##FOOTER_ARG("status=%d", gcvSTATUS_INVALID_OBJECT); \
                return gcvSTATUS_INVALID_OBJECT; \
            } \
            else if (((gcsOBJECT*) (obj))->type != t) \
            { \
                prefix##TRACE(gcvLEVEL_ERROR, \
                              #prefix "VERIFY_OBJECT failed: %c%c%c%c", \
                              gcmCC_PRINT(((gcsOBJECT*) (obj))->type)); \
                prefix##TRACE(gcvLEVEL_ERROR, "  expected: %c%c%c%c", \
                              gcmCC_PRINT(t)); \
                prefix##ASSERT(((gcsOBJECT*)(obj))->type == t); \
                prefix##FOOTER_ARG("status=%d", gcvSTATUS_INVALID_OBJECT); \
                return gcvSTATUS_INVALID_OBJECT; \
            } \
        } \
        while (gcvFALSE)
#   define gcmVERIFY_OBJECT(obj, t)     _gcmVERIFY_OBJECT(gcm, obj, t)
#   define gcmkVERIFY_OBJECT(obj, t)    _gcmVERIFY_OBJECT(gcmk, obj, t)
#else
#   define gcmVERIFY_OBJECT(obj, t)     do {} while (gcvFALSE)
#   define gcmkVERIFY_OBJECT(obj, t)    do {} while (gcvFALSE)
#endif

/******************************************************************************\
********************************** gckOS Object *********************************
\******************************************************************************/

typedef struct _gckOS           * gckOS;

/* Construct a new gckOS object. */
gceSTATUS
gckOS_Construct(
    IN gctPOINTER Context,
    OUT gckOS * Os
    );

/* Destroy an gckOS object. */
gceSTATUS
gckOS_Destroy(
    IN gckOS Os
    );

/* Query the video memory. */
gceSTATUS
gckOS_QueryVideoMemory(
    IN gckOS Os,
    OUT gctPHYS_ADDR * InternalAddress,
    OUT gctSIZE_T * InternalSize,
    OUT gctPHYS_ADDR * ExternalAddress,
    OUT gctSIZE_T * ExternalSize,
    OUT gctPHYS_ADDR * ContiguousAddress,
    OUT gctSIZE_T * ContiguousSize
    );

/* Allocate memory from the heap. */
gceSTATUS
gckOS_Allocate(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    );

/* Free allocated memory. */
gceSTATUS
gckOS_Free(
    IN gckOS Os,
    IN gctPOINTER Memory
    );

/* Wrapper for allocation memory.. */
gceSTATUS
gckOS_AllocateMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    );

/* Wrapper for freeing memory. */
gceSTATUS
gckOS_FreeMemory(
    IN gckOS Os,
    IN gctPOINTER Memory
    );

/* Allocate paged memory. */
gceSTATUS
gckOS_AllocateVirtualMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    );

/* Wrapper for freeing virtual memory. */
gceSTATUS
gckOS_FreeVirtualMemory(
    IN gckOS Os,
    IN gctPOINTER Memory
    );

/* Allocate paged memory. */
gceSTATUS
gckOS_AllocatePagedMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPHYS_ADDR * Physical
    );

/* Allocate paged memory. */
gceSTATUS
gckOS_AllocatePagedMemoryEx(
    IN gckOS Os,
    IN gctBOOL Contiguous,
    IN gctSIZE_T Bytes,
    OUT gctPHYS_ADDR * Physical
    );

/* Lock pages. */
gceSTATUS
gckOS_LockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
#ifdef __QNXNTO__
    IN gctUINT32 Pid,
#endif
    OUT gctPOINTER * Logical,
    OUT gctSIZE_T * PageCount
    );

/* Map pages. */
gceSTATUS
gckOS_MapPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
#ifdef __QNXNTO__
    IN gctPOINTER Logical,
#endif
    IN gctSIZE_T PageCount,
    IN gctPOINTER PageTable
    );

/* Unlock pages. */
gceSTATUS
gckOS_UnlockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
#ifdef __QNXNTO__
    IN gctUINT32 Pid,
#endif
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    );

/* Free paged memory. */
gceSTATUS
gckOS_FreePagedMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes
    );

/* Allocate non-paged memory. */
gceSTATUS
gckOS_AllocateNonPagedMemory(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    );

/* Free non-paged memory. */
gceSTATUS
gckOS_FreeNonPagedMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical
    );

/* Allocate contiguous memory. */
gceSTATUS
gckOS_AllocateContiguous(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    );

/* Free contiguous memory. */
gceSTATUS
gckOS_FreeContiguous(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    );

/* Get the number fo bytes per page. */
gceSTATUS
gckOS_GetPageSize(
    IN gckOS Os,
    OUT gctSIZE_T * PageSize
    );

/* Get the physical address of a corresponding logical address. */
gceSTATUS
gckOS_GetPhysicalAddress(
    IN gckOS Os,
    IN gctPOINTER Logical,
    OUT gctUINT32 * Address
    );

/* Map physical memory. */
gceSTATUS
gckOS_MapPhysical(
    IN gckOS Os,
    IN gctUINT32 Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    );

/* Unmap previously mapped physical memory. */
gceSTATUS
gckOS_UnmapPhysical(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    );

/* Read data from a hardware register. */
gceSTATUS
gckOS_ReadRegister(
    IN gckOS Os,
    IN gctUINT32 Address,
    OUT gctUINT32 * Data
    );

/* Write data to a hardware register. */
gceSTATUS
gckOS_WriteRegister(
    IN gckOS Os,
    IN gctUINT32 Address,
    IN gctUINT32 Data
    );

/* Write data to a 32-bit memory location. */
gceSTATUS
gckOS_WriteMemory(
    IN gckOS Os,
    IN gctPOINTER Address,
    IN gctUINT32 Data
    );

/* Map physical memory into the process space. */
gceSTATUS
gckOS_MapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    );

/* Unmap physical memory from the process space. */
gceSTATUS
gckOS_UnmapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    );

/* Create a new mutex. */
gceSTATUS
gckOS_CreateMutex(
    IN gckOS Os,
    OUT gctPOINTER * Mutex
    );

/* Delete a mutex. */
gceSTATUS
gckOS_DeleteMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex
    );

/* Acquire a mutex. */
gceSTATUS
gckOS_AcquireMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex,
    IN gctUINT32 Timeout
    );

/* Release a mutex. */
gceSTATUS
gckOS_ReleaseMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex
    );

/* Atomically exchange a pair of 32-bit values. */
gceSTATUS
gckOS_AtomicExchange(
    IN gckOS Os,
    IN OUT gctUINT32_PTR Target,
    IN gctUINT32 NewValue,
    OUT gctUINT32_PTR OldValue
    );

/* Atomically exchange a pair of pointers. */
gceSTATUS
gckOS_AtomicExchangePtr(
    IN gckOS Os,
    IN OUT gctPOINTER * Target,
    IN gctPOINTER NewValue,
    OUT gctPOINTER * OldValue
    );

/*******************************************************************************
**
**  gckOS_AtomConstruct
**
**  Create an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**  OUTPUT:
**
**      gctPOINTER * Atom
**          Pointer to a variable receiving the constructed atom.
*/
gceSTATUS
gckOS_AtomConstruct(
    IN gckOS Os,
    OUT gctPOINTER * Atom
    );

/*******************************************************************************
**
**  gckOS_AtomDestroy
**
**  Destroy an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomDestroy(
    IN gckOS Os,
    OUT gctPOINTER Atom
    );

/*******************************************************************************
**
**  gckOS_AtomGet
**
**  Get the 32-bit value protected by an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable the receives the value of the atom.
*/
gceSTATUS
gckOS_AtomGet(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    );

/*******************************************************************************
**
**  gckOS_AtomIncrement
**
**  Atomically increment the 32-bit integer value inside an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable the receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomIncrement(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    );

/*******************************************************************************
**
**  gckOS_AtomDecrement
**
**  Atomically decrement the 32-bit integer value inside an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable the receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomDecrement(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    );

/* Delay a number of microseconds. */
gceSTATUS
gckOS_Delay(
    IN gckOS Os,
    IN gctUINT32 Delay
    );

/* Memory barrier. */
gceSTATUS
gckOS_MemoryBarrier(
    IN gckOS Os,
    IN gctPOINTER Address
    );

/* Map user pointer. */
gceSTATUS
gckOS_MapUserPointer(
    IN gckOS Os,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size,
    OUT gctPOINTER * KernelPointer
    );

/* Unmap user pointer. */
gceSTATUS
gckOS_UnmapUserPointer(
    IN gckOS Os,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size,
    IN gctPOINTER KernelPointer
    );

#ifdef __QNXNTO__
/* Map user physical address. */
gceSTATUS
gckOS_MapUserPhysical(
    IN gckOS Os,
    IN gctPHYS_ADDR Phys,
    OUT gctPOINTER * KernelPointer
    );

/* Allocate from user's shared pool. */
gceSTATUS
gckOS_AllocateNonPagedMemoryShmPool(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN gctUINT32 Pid,
    IN gctHANDLE Handle,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    );
#endif

gceSTATUS
gckOS_SuspendInterrupt(
    IN gckOS Os
    );

gceSTATUS
gckOS_ResumeInterrupt(
    IN gckOS Os
    );

/* Get the base address for the physical memory. */
gceSTATUS
gckOS_GetBaseAddress(
    IN gckOS Os,
    OUT gctUINT32_PTR BaseAddress
    );

/* Perform a memory copy. */
gceSTATUS
gckOS_MemCopy(
    IN gctPOINTER Destination,
    IN gctCONST_POINTER Source,
    IN gctSIZE_T Bytes
    );

/* Zero memory. */
gceSTATUS
gckOS_ZeroMemory(
    IN gctPOINTER Memory,
    IN gctSIZE_T Bytes
    );

/* Device I/O control to the kernel HAL layer. */
gceSTATUS
gckOS_DeviceControl(
    IN gckOS Os,
    IN gctBOOL FromUser,
    IN gctUINT32 IoControlCode,
    IN gctPOINTER InputBuffer,
    IN gctSIZE_T InputBufferSize,
    OUT gctPOINTER OutputBuffer,
    IN gctSIZE_T OutputBufferSize
    );

/*******************************************************************************
**
**  gckOS_GetProcessID
**
**  Get current process ID.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      gctUINT32_PTR ProcessID
**          Pointer to the variable that receives the process ID.
*/
gceSTATUS
gckOS_GetProcessID(
    OUT gctUINT32_PTR ProcessID
    );

/*******************************************************************************
**
**  gckOS_GetThreadID
**
**  Get current thread ID.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      gctUINT32_PTR ThreadID
**          Pointer to the variable that receives the thread ID.
*/
gceSTATUS
gckOS_GetThreadID(
    OUT gctUINT32_PTR ThreadID
    );

/******************************************************************************\
********************************** Signal Object *********************************
\******************************************************************************/

/* User signal command codes. */
typedef enum _gceUSER_SIGNAL_COMMAND_CODES
{
    gcvUSER_SIGNAL_CREATE,
    gcvUSER_SIGNAL_DESTROY,
    gcvUSER_SIGNAL_SIGNAL,
    gcvUSER_SIGNAL_WAIT,
}
gceUSER_SIGNAL_COMMAND_CODES;

/* Create a signal. */
gceSTATUS
gckOS_CreateSignal(
    IN gckOS Os,
    IN gctBOOL ManualReset,
    OUT gctSIGNAL * Signal
    );

/* Destroy a signal. */
gceSTATUS
gckOS_DestroySignal(
    IN gckOS Os,
    IN gctSIGNAL Signal
    );

/* Signal a signal. */
gceSTATUS
gckOS_Signal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctBOOL State
    );

/* Wait for a signal. */
gceSTATUS
gckOS_WaitSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctUINT32 Wait
    );

/* Wait for a signal uninterruptibly. */
gceSTATUS
gckOS_WaitSignalUninterruptible(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctUINT32 Wait
    );

/* Map a user signal to the kernel space. */
gceSTATUS
gckOS_MapSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctHANDLE Process,
    OUT gctSIGNAL * MappedSignal
    );

/* Unmap a user signal */
gceSTATUS
gckOS_UnmapSignal(
    IN gckOS Os,
    IN gctSIGNAL MappedSignal
    );

/* Map user memory. */
gceSTATUS
gckOS_MapUserMemory(
    IN gckOS Os,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    OUT gctPOINTER * Info,
    OUT gctUINT32_PTR Address
    );

/* Unmap user memory. */
gceSTATUS
gckOS_UnmapUserMemory(
    IN gckOS Os,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    IN gctPOINTER Info,
    IN gctUINT32 Address
    );

#if !USE_NEW_LINUX_SIGNAL
/* Create signal to be used in the user space. */
gceSTATUS
gckOS_CreateUserSignal(
    IN gckOS Os,
    IN gctBOOL ManualReset,
    OUT gctINT * SignalID
    );

/* Destroy signal used in the user space. */
gceSTATUS
gckOS_DestroyUserSignal(
    IN gckOS Os,
    IN gctINT SignalID
    );

/* Wait for signal used in the user space. */
gceSTATUS
gckOS_WaitUserSignal(
    IN gckOS Os,
    IN gctINT SignalID,
    IN gctUINT32 Wait
    );

/* Signal a signal used in the user space. */
gceSTATUS
gckOS_SignalUserSignal(
    IN gckOS Os,
    IN gctINT SignalID,
    IN gctBOOL State
    );
#endif /* USE_NEW_LINUX_SIGNAL */

/* Set a signal owned by a process. */
#if defined(__QNXNTO__)
gceSTATUS
gckOS_UserSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctINT Recvid,
    IN gctINT Coid
    );
#else
gceSTATUS
gckOS_UserSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctHANDLE Process
    );
#endif

/******************************************************************************\
** Cache Support
*/

gceSTATUS
gckOS_CacheFlush(
    gckOS Os,
    gctHANDLE ProcessId,
    gctPOINTER Logical,
    gctSIZE_T Bytes
    );

gceSTATUS
gckOS_CacheInvalidate(
    gckOS Os,
    gctHANDLE ProcessId,
    gctPOINTER Logical,
    gctSIZE_T Bytes
    );

/******************************************************************************\
** Debug Support
*/

void
gckOS_SetDebugLevel(
    IN gctUINT32 Level
    );

void
gckOS_SetDebugZone(
    IN gctUINT32 Zone
    );

void
gckOS_SetDebugLevelZone(
    IN gctUINT32 Level,
    IN gctUINT32 Zone
    );

void
gckOS_SetDebugZones(
    IN gctUINT32 Zones,
    IN gctBOOL Enable
    );

void
gckOS_SetDebugFile(
    IN gctCONST_STRING FileName
    );

/*******************************************************************************
** Broadcast interface.
*/

typedef enum _gceBROADCAST
{
    /* GPU might be idle. */
    gcvBROADCAST_GPU_IDLE,

    /* A commit is going to happen. */
    gcvBROADCAST_GPU_COMMIT,

    /* GPU seems to be stuck. */
    gcvBROADCAST_GPU_STUCK,

    /* First process gets attached. */
    gcvBROADCAST_FIRST_PROCESS,

    /* Last process gets detached. */
    gcvBROADCAST_LAST_PROCESS,

    /* AXI bus error. */
    gcvBROADCAST_AXI_BUS_ERROR,
}
gceBROADCAST;

gceSTATUS
gckOS_Broadcast(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gceBROADCAST Reason
    );

/*******************************************************************************
**
**  gckOS_SetGPUPower
**
**  Set the power of the GPU on or off.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.脽
**
**      gctBOOL Clock
**          gcvTRUE to turn on the clock, or gcvFALSE to turn off the clock.
**
**      gctBOOL Power
**          gcvTRUE to turn on the power, or gcvFALSE to turn off the power.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SetGPUPower(
    IN gckOS Os,
    IN gctBOOL Clock,
    IN gctBOOL Power
    );

/*******************************************************************************
** Semaphores.
*/

/* Create a new semaphore. */
gceSTATUS
gckOS_CreateSemaphore(
    IN gckOS Os,
    OUT gctPOINTER * Semaphore
    );

/* Delete a semahore. */
gceSTATUS
gckOS_DestroySemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    );

/* Acquire a semahore. */
gceSTATUS
gckOS_AcquireSemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    );

/* Release a semahore. */
gceSTATUS
gckOS_ReleaseSemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    );

/******************************************************************************\
********************************* gckHEAP Object ********************************
\******************************************************************************/

typedef struct _gckHEAP *       gckHEAP;

/* Construct a new gckHEAP object. */
gceSTATUS
gckHEAP_Construct(
    IN gckOS Os,
    IN gctSIZE_T AllocationSize,
    OUT gckHEAP * Heap
    );

/* Destroy an gckHEAP object. */
gceSTATUS
gckHEAP_Destroy(
    IN gckHEAP Heap
    );

/* Allocate memory. */
gceSTATUS
gckHEAP_Allocate(
    IN gckHEAP Heap,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Node
    );

/* Free memory. */
gceSTATUS
gckHEAP_Free(
    IN gckHEAP Heap,
    IN gctPOINTER Node
    );

/* Profile the heap. */
gceSTATUS
gckHEAP_ProfileStart(
    IN gckHEAP Heap
    );

gceSTATUS
gckHEAP_ProfileEnd(
    IN gckHEAP Heap,
    IN gctCONST_STRING Title
    );


/******************************************************************************\
******************************** gckVIDMEM Object ******************************
\******************************************************************************/

typedef struct _gckVIDMEM *         gckVIDMEM;
typedef union  _gcuVIDMEM_NODE *    gcuVIDMEM_NODE_PTR;
typedef struct _gckKERNEL *         gckKERNEL;

/* Construct a new gckVIDMEM object. */
gceSTATUS
gckVIDMEM_Construct(
    IN gckOS Os,
    IN gctUINT32 BaseAddress,
    IN gctSIZE_T Bytes,
    IN gctSIZE_T Threshold,
    IN gctSIZE_T Banking,
    OUT gckVIDMEM * Memory
    );

/* Destroy an gckVDIMEM object. */
gceSTATUS
gckVIDMEM_Destroy(
    IN gckVIDMEM Memory
    );

/* Allocate rectangular memory. */
gceSTATUS
gckVIDMEM_Allocate(
    IN gckVIDMEM Memory,
    IN gctUINT Width,
    IN gctUINT Height,
    IN gctUINT Depth,
    IN gctUINT BytesPerPixel,
    IN gctUINT32 Alignment,
    IN gceSURF_TYPE Type,
#ifdef __QNXNTO__
    IN gctHANDLE Handle,
#endif
    OUT gcuVIDMEM_NODE_PTR * Node
    );

/* Allocate linear memory. */
gceSTATUS
gckVIDMEM_AllocateLinear(
    IN gckVIDMEM Memory,
    IN gctSIZE_T Bytes,
    IN gctUINT32 Alignment,
    IN gceSURF_TYPE Type,
#ifdef __QNXNTO__
    IN gctHANDLE Handle,
#endif
    OUT gcuVIDMEM_NODE_PTR * Node
    );

/* Free memory. */
gceSTATUS
gckVIDMEM_Free(
    IN gcuVIDMEM_NODE_PTR Node
    );

/* Lock memory. */
gceSTATUS
gckVIDMEM_Lock(
    IN gcuVIDMEM_NODE_PTR Node,
    OUT gctUINT32 * Address
    );

/* Unlock memory. */
gceSTATUS
gckVIDMEM_Unlock(
    IN gcuVIDMEM_NODE_PTR Node,
    IN gceSURF_TYPE Type,
    IN OUT gctBOOL * Asynchroneous
    );

/* Construct a gcuVIDMEM_NODE union for virtual memory. */
gceSTATUS
gckVIDMEM_ConstructVirtual(
    IN gckKERNEL Kernel,
    IN gctBOOL Contiguous,
    IN gctSIZE_T Bytes,
#ifdef __QNXNTO__
    IN gctHANDLE Handle,
#endif
    OUT gcuVIDMEM_NODE_PTR * Node
    );

/* Destroy a gcuVIDMEM_NODE union for virtual memory. */
gceSTATUS
gckVIDMEM_DestroyVirtual(
    IN gcuVIDMEM_NODE_PTR Node
    );

#ifdef __QNXNTO__
/* Set the allocating process' PID for this node. */
gceSTATUS
gckVIDMEM_SetPID(
    IN gcuVIDMEM_NODE_PTR Node,
    IN gctUINT32 Pid);
#endif

/******************************************************************************\
******************************** gckKERNEL Object ******************************
\******************************************************************************/

struct _gcsHAL_INTERFACE;

/* Notifications. */
typedef enum _gceNOTIFY
{
    gcvNOTIFY_INTERRUPT,
    gcvNOTIFY_COMMAND_QUEUE,
}
gceNOTIFY;

/* Event locations. */
typedef enum _gceKERNEL_WHERE
{
    gcvKERNEL_COMMAND,
    gcvKERNEL_VERTEX,
    gcvKERNEL_TRIANGLE,
    gcvKERNEL_TEXTURE,
    gcvKERNEL_PIXEL,
}
gceKERNEL_WHERE;

/* Flush flags. */
typedef enum _gceKERNEL_FLUSH
{
    gcvFLUSH_COLOR              = 0x01,
    gcvFLUSH_DEPTH              = 0x02,
    gcvFLUSH_TEXTURE            = 0x04,
    gcvFLUSH_2D                 = 0x08,
    gcvFLUSH_ALL                = gcvFLUSH_COLOR
                                | gcvFLUSH_DEPTH
                                | gcvFLUSH_TEXTURE
                                | gcvFLUSH_2D,
}
gceKERNEL_FLUSH;

/* Construct a new gckKERNEL object. */
gceSTATUS
gckKERNEL_Construct(
    IN gckOS Os,
    IN gctPOINTER Context,
    OUT gckKERNEL * Kernel
    );

/* Destroy an gckKERNEL object. */
gceSTATUS
gckKERNEL_Destroy(
    IN gckKERNEL Kernel
    );

/* Dispatch a user-level command. */
gceSTATUS
gckKERNEL_Dispatch(
    IN gckKERNEL Kernel,
    IN gctBOOL FromUser,
    IN OUT struct _gcsHAL_INTERFACE * Interface
    );

/* Query the video memory. */
gceSTATUS
gckKERNEL_QueryVideoMemory(
    IN gckKERNEL Kernel,
    OUT struct _gcsHAL_INTERFACE * Interface
    );

/* Lookup the gckVIDMEM object for a pool. */
gceSTATUS
gckKERNEL_GetVideoMemoryPool(
    IN gckKERNEL Kernel,
    IN gcePOOL Pool,
    OUT gckVIDMEM * VideoMemory
    );

/* Map video memory. */
gceSTATUS
gckKERNEL_MapVideoMemory(
    IN gckKERNEL Kernel,
    IN gctBOOL InUserSpace,
    IN gctUINT32 Address,
#ifdef __QNXNTO__
    IN gctUINT32 Pid,
    IN gctUINT32 Bytes,
#endif
    OUT gctPOINTER * Logical
    );

#ifdef __QNXNTO__
/* Unmap video memory. */
gceSTATUS
gckKERNEL_UnmapVideoMemory(
    IN gckKERNEL Kernel,
    IN gctPOINTER Logical,
    IN gctUINT32 Pid,
    IN gctUINT32 Bytes
    );
#endif

/* Map memory. */
gceSTATUS
gckKERNEL_MapMemory(
    IN gckKERNEL Kernel,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    );

/* Unmap memory. */
gceSTATUS
gckKERNEL_UnmapMemory(
    IN gckKERNEL Kernel,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    );

/* Notification of events. */
gceSTATUS
gckKERNEL_Notify(
    IN gckKERNEL Kernel,
    IN gceNOTIFY Notifcation,
    IN gctBOOL Data
    );

gceSTATUS
gckKERNEL_QuerySettings(
    IN gckKERNEL Kernel,
    OUT gcsKERNEL_SETTINGS * Settings
    );

/*******************************************************************************
**
**  gckKERNEL_Recovery
**
**  Try to recover the GPU from a fatal error.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_Recovery(
    IN gckKERNEL Kernel
    );

/******************************************************************************\
******************************* gckHARDWARE Object *****************************
\******************************************************************************/

/* Construct a new gckHARDWARE object. */
gceSTATUS
gckHARDWARE_Construct(
    IN gckOS Os,
    OUT gckHARDWARE * Hardware
    );

/* Destroy an gckHARDWARE object. */
gceSTATUS
gckHARDWARE_Destroy(
    IN gckHARDWARE Hardware
    );

/* Query system memory requirements. */
gceSTATUS
gckHARDWARE_QuerySystemMemory(
    IN gckHARDWARE Hardware,
    OUT gctSIZE_T * SystemSize,
    OUT gctUINT32 * SystemBaseAddress
    );

/* Build virtual address. */
gceSTATUS
gckHARDWARE_BuildVirtualAddress(
    IN gckHARDWARE Hardware,
    IN gctUINT32 Index,
    IN gctUINT32 Offset,
    OUT gctUINT32 * Address
    );

/* Query command buffer requirements. */
gceSTATUS
gckHARDWARE_QueryCommandBuffer(
    IN gckHARDWARE Hardware,
    OUT gctSIZE_T * Alignment,
    OUT gctSIZE_T * ReservedHead,
    OUT gctSIZE_T * ReservedTail
    );

/* Add a WAIT/LINK pair in the command queue. */
gceSTATUS
gckHARDWARE_WaitLink(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Offset,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPOINTER * Wait,
    OUT gctSIZE_T * WaitBytes
    );

/* Kickstart the command processor. */
gceSTATUS
gckHARDWARE_Execute(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
#ifdef __QNXNTO__
    IN gctPOINTER Physical,
    IN gctBOOL PhysicalAddresses,
#endif
    IN gctSIZE_T Bytes
    );

/* Add an END command in the command queue. */
gceSTATUS
gckHARDWARE_End(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN OUT gctSIZE_T * Bytes
    );

/* Add a NOP command in the command queue. */
gceSTATUS
gckHARDWARE_Nop(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN OUT gctSIZE_T * Bytes
    );

/* Add a WAIT command in the command queue. */
gceSTATUS
gckHARDWARE_Wait(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Count,
    IN OUT gctSIZE_T * Bytes
    );

/* Add a PIPESELECT command in the command queue. */
gceSTATUS
gckHARDWARE_PipeSelect(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Pipe,
    IN OUT gctSIZE_T * Bytes
    );

/* Add a LINK command in the command queue. */
gceSTATUS
gckHARDWARE_Link(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctPOINTER FetchAddress,
    IN gctSIZE_T FetchSize,
    IN OUT gctSIZE_T * Bytes
    );

/* Add an EVENT command in the command queue. */
gceSTATUS
gckHARDWARE_Event(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT8 Event,
    IN gceKERNEL_WHERE FromWhere,
    IN OUT gctSIZE_T * Bytes
    );

/* Query the available memory. */
gceSTATUS
gckHARDWARE_QueryMemory(
    IN gckHARDWARE Hardware,
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
gckHARDWARE_QueryChipIdentity(
    IN gckHARDWARE Hardware,
    OUT gceCHIPMODEL* ChipModel,
    OUT gctUINT32* ChipRevision,
    OUT gctUINT32* ChipFeatures,
    OUT gctUINT32* ChipMinorFeatures,
    OUT gctUINT32* ChipMinorFeatures1,
    OUT gctUINT32* ChipMinorFeatures2
    );

/* Query the specifications sof the hardware. */
gceSTATUS
gckHARDWARE_QueryChipSpecs(
    IN gckHARDWARE Hardware,
    OUT gctUINT32_PTR StreamCount,
    OUT gctUINT32_PTR RegisterMax,
    OUT gctUINT32_PTR ThreadCount,
    OUT gctUINT32_PTR ShaderCoreCount,
    OUT gctUINT32_PTR VertexCacheSize,
    OUT gctUINT32_PTR VertexOutputBufferSize
    );

/* Convert an API format. */
gceSTATUS
gckHARDWARE_ConvertFormat(
    IN gckHARDWARE Hardware,
    IN gceSURF_FORMAT Format,
    OUT gctUINT32 * BitsPerPixel,
    OUT gctUINT32 * BytesPerTile
    );

/* Split a harwdare specific address into API stuff. */
gceSTATUS
gckHARDWARE_SplitMemory(
    IN gckHARDWARE Hardware,
    IN gctUINT32 Address,
    OUT gcePOOL * Pool,
    OUT gctUINT32 * Offset
    );

/* Align size to tile boundary. */
gceSTATUS
gckHARDWARE_AlignToTile(
    IN gckHARDWARE Hardware,
    IN gceSURF_TYPE Type,
    IN OUT gctUINT32_PTR Width,
    IN OUT gctUINT32_PTR Height,
    OUT gctBOOL_PTR SuperTiled
    );

/* Update command queue tail pointer. */
gceSTATUS
gckHARDWARE_UpdateQueueTail(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Offset
    );

/* Convert logical address to hardware specific address. */
gceSTATUS
gckHARDWARE_ConvertLogical(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    OUT gctUINT32 * Address
    );

#ifdef __QNXNTO__
/* Convert physical address to hardware specific address. */
gceSTATUS
gckHARDWARE_ConvertPhysical(
    IN gckHARDWARE Hardware,
    IN gctPHYS_ADDR Physical,
    OUT gctUINT32 * Address
    );
#endif

/* Interrupt manager. */
gceSTATUS
gckHARDWARE_Interrupt(
    IN gckHARDWARE Hardware,
    IN gctBOOL InterruptValid
    );

/* Program MMU. */
gceSTATUS
gckHARDWARE_SetMMU(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical
    );

/* Flush the MMU. */
gceSTATUS
gckHARDWARE_FlushMMU(
    IN gckHARDWARE Hardware
    );

/* Get idle register. */
gceSTATUS
gckHARDWARE_GetIdle(
    IN gckHARDWARE Hardware,
    IN gctBOOL Wait,
    OUT gctUINT32 * Data
    );

/* Flush the caches. */
gceSTATUS
gckHARDWARE_Flush(
    IN gckHARDWARE Hardware,
    IN gceKERNEL_FLUSH Flush,
    IN gctPOINTER Logical,
    IN OUT gctSIZE_T * Bytes
    );

/* Enable/disable fast clear. */
gceSTATUS
gckHARDWARE_SetFastClear(
    IN gckHARDWARE Hardware,
    IN gctINT Enable,
    IN gctINT Compression
    );

gceSTATUS
gckHARDWARE_ReadInterrupt(
    IN gckHARDWARE Hardware,
    OUT gctUINT32_PTR IDs
    );

/* Power management. */
gceSTATUS
gckHARDWARE_SetPowerManagementState(
    IN gckHARDWARE Hardware,
    IN gceCHIPPOWERSTATE State
    );

gceSTATUS
gckHARDWARE_QueryPowerManagementState(
    IN gckHARDWARE Hardware,
    OUT gceCHIPPOWERSTATE* State
    );

/* Profile 2D Engine. */
gceSTATUS
gckHARDWARE_ProfileEngine2D(
    IN gckHARDWARE Hardware,
    OUT gcs2D_PROFILE_PTR Profile
    );

gceSTATUS
gckHARDWARE_InitializeHardware(
    IN gckHARDWARE Hardware
    );

gceSTATUS
gckHARDWARE_Reset(
    IN gckHARDWARE Hardware
    );

typedef gceSTATUS (*gctISRMANAGERFUNC)(gctPOINTER Context);

gceSTATUS
gckHARDWARE_SetIsrManager(
    IN gckHARDWARE Hardware,
    IN gctISRMANAGERFUNC StartIsr,
    IN gctISRMANAGERFUNC StopIsr,
    IN gctPOINTER Context
    );

/******************************************************************************\
***************************** gckINTERRUPT Object ******************************
\******************************************************************************/

typedef struct _gckINTERRUPT *  gckINTERRUPT;

typedef gceSTATUS (* gctINTERRUPT_HANDLER)(
    IN gckKERNEL Kernel
    );

gceSTATUS
gckINTERRUPT_Construct(
    IN gckKERNEL Kernel,
    OUT gckINTERRUPT * Interrupt
    );

gceSTATUS
gckINTERRUPT_Destroy(
    IN gckINTERRUPT Interrupt
    );

gceSTATUS
gckINTERRUPT_SetHandler(
    IN gckINTERRUPT Interrupt,
    IN OUT gctINT32_PTR Id,
    IN gctINTERRUPT_HANDLER Handler
    );

gceSTATUS
gckINTERRUPT_Notify(
    IN gckINTERRUPT Interrupt,
    IN gctBOOL Valid
    );

/******************************************************************************\
******************************** gckEVENT Object *******************************
\******************************************************************************/

typedef struct _gckEVENT *      gckEVENT;

/* Construct a new gckEVENT object. */
gceSTATUS
gckEVENT_Construct(
    IN gckKERNEL Kernel,
    OUT gckEVENT * Event
    );

/* Destroy an gckEVENT object. */
gceSTATUS
gckEVENT_Destroy(
    IN gckEVENT Event
    );

/* Schedule a FreeNonPagedMemory event. */
gceSTATUS
gckEVENT_FreeNonPagedMemory(
    IN gckEVENT Event,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gceKERNEL_WHERE FromWhere
    );

/* Schedule a FreeContiguousMemory event. */
gceSTATUS
gckEVENT_FreeContiguousMemory(
    IN gckEVENT Event,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gceKERNEL_WHERE FromWhere
    );

/* Schedule a FreeVideoMemory event. */
gceSTATUS
gckEVENT_FreeVideoMemory(
    IN gckEVENT Event,
    IN gcuVIDMEM_NODE_PTR VideoMemory,
    IN gceKERNEL_WHERE FromWhere
    );

/* Schedule a signal event. */
gceSTATUS
gckEVENT_Signal(
    IN gckEVENT Event,
    IN gctSIGNAL Signal,
    IN gceKERNEL_WHERE FromWhere
    );

/* Schedule an Unlock event. */
gceSTATUS
gckEVENT_Unlock(
    IN gckEVENT Event,
    IN gceKERNEL_WHERE FromWhere,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gceSURF_TYPE Type
    );

gceSTATUS
gckEVENT_Submit(
    IN gckEVENT Event,
    IN gctBOOL Wait
    );

struct _gcsQUEUE;

/* Commit an event queue. */
gceSTATUS
gckEVENT_Commit(
    IN gckEVENT Event,
    IN struct _gcsQUEUE * Queue
    );

/* Event callback routine. */
gceSTATUS
gckEVENT_Notify(
    IN gckEVENT Event,
    IN gctUINT32 IDs
    );

/* Event callback routine. */
gceSTATUS
gckEVENT_Interrupt(
    IN gckEVENT Event,
    IN gctUINT32 IDs
    );

/******************************************************************************\
******************************* gckCOMMAND Object ******************************
\******************************************************************************/

typedef struct _gckCOMMAND *        gckCOMMAND;

/* Construct a new gckCOMMAND object. */
gceSTATUS
gckCOMMAND_Construct(
    IN gckKERNEL Kernel,
    OUT gckCOMMAND * Command
    );

/* Destroy an gckCOMMAND object. */
gceSTATUS
gckCOMMAND_Destroy(
    IN gckCOMMAND Command
    );

/* Start the command queue. */
gceSTATUS
gckCOMMAND_Start(
    IN gckCOMMAND Command
    );

/* Stop the command queue. */
gceSTATUS
gckCOMMAND_Stop(
    IN gckCOMMAND Command
    );

/* Commit a buffer to the command queue. */
gceSTATUS
gckCOMMAND_Commit(
    IN gckCOMMAND Command,
    IN gcoCMDBUF CommandBuffer,
    IN gcoCONTEXT Context,
    IN gctHANDLE Process
    );

/* Reserve space in the command buffer. */
gceSTATUS
gckCOMMAND_Reserve(
    IN gckCOMMAND Command,
    IN gctSIZE_T RequestedBytes,
    OUT gctPOINTER * Buffer,
    OUT gctSIZE_T * BufferSize
    );

/* Release reserved space in the command buffer. */
gceSTATUS
gckCOMMAND_Release(
    IN gckCOMMAND Command
    );

/* Execute reserved space in the command buffer. */
gceSTATUS
gckCOMMAND_Execute(
    IN gckCOMMAND Command,
    IN gctSIZE_T RequstedBytes
    );

/* Stall the command queue. */
gceSTATUS
gckCOMMAND_Stall(
    IN gckCOMMAND Command
    );

/******************************************************************************\
********************************* gckMMU Object ********************************
\******************************************************************************/

typedef struct _gckMMU *            gckMMU;

/* Construct a new gckMMU object. */
gceSTATUS
gckMMU_Construct(
    IN gckKERNEL Kernel,
    IN gctSIZE_T MmuSize,
    OUT gckMMU * Mmu
    );

/* Destroy an gckMMU object. */
gceSTATUS
gckMMU_Destroy(
    IN gckMMU Mmu
    );

/* Allocate pages inside the MMU. */
gceSTATUS
gckMMU_AllocatePages(
    IN gckMMU Mmu,
    IN gctSIZE_T PageCount,
    OUT gctPOINTER * PageTable,
    OUT gctUINT32 * Address
    );

/* Remove a page table from the MMU. */
gceSTATUS
gckMMU_FreePages(
    IN gckMMU Mmu,
    IN gctPOINTER PageTable,
    IN gctSIZE_T PageCount
    );

#ifdef __QNXNTO__
gceSTATUS
gckMMU_InsertNode(
    IN gckMMU Mmu,
    IN gcuVIDMEM_NODE_PTR Node);

gceSTATUS
gckMMU_RemoveNode(
    IN gckMMU Mmu,
    IN gcuVIDMEM_NODE_PTR Node);
#endif

#ifdef __QNXNTO__
gceSTATUS
gckMMU_FreeHandleMemory(
    IN gckMMU Mmu,
    IN gctHANDLE Handle
    );
#endif


gceSTATUS
gckHARDWARE_QueryProfileRegisters(
    IN gckHARDWARE Hardware,
    OUT gcsPROFILER_COUNTERS * Counters
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_h_ */

