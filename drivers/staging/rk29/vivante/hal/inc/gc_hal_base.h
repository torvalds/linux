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




#ifndef __gc_hal_base_h_
#define __gc_hal_base_h_

#include "gc_hal_enum.h"
#include "gc_hal_types.h"
#include "gc_hal_dump.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef struct _gcoHAL *                gcoHAL;
typedef struct _gcoOS *                 gcoOS;
typedef struct _gco2D *                 gco2D;
typedef struct _gcoVG *                 gcoVG;
typedef struct _gco3D *                 gco3D;
typedef struct _gcoSURF *               gcoSURF;
typedef struct _gcsSURF_INFO *          gcsSURF_INFO_PTR;
typedef struct _gcsSURF_NODE *          gcsSURF_NODE_PTR;
typedef struct _gcsSURF_FORMAT_INFO *   gcsSURF_FORMAT_INFO_PTR;
typedef struct _gcsPOINT *              gcsPOINT_PTR;
typedef struct _gcsSIZE *               gcsSIZE_PTR;
typedef struct _gcsRECT *               gcsRECT_PTR;
typedef struct _gcsBOUNDARY *           gcsBOUNDARY_PTR;
typedef struct _gcoDUMP *               gcoDUMP;
typedef struct _gcoHARDWARE *           gcoHARDWARE;

/******************************************************************************\
********************************* Enumerations *********************************
\******************************************************************************/

/* Video memory pool type. */
typedef enum _gcePOOL
{
    gcvPOOL_UNKNOWN,
    gcvPOOL_DEFAULT,
    gcvPOOL_LOCAL,
    gcvPOOL_LOCAL_INTERNAL,
    gcvPOOL_LOCAL_EXTERNAL,
    gcvPOOL_UNIFIED,
    gcvPOOL_SYSTEM,
    gcvPOOL_VIRTUAL,
    gcvPOOL_USER,
    gcvPOOL_CONTIGUOUS
}
gcePOOL;

/* Blending functions. */
typedef enum _gceBLEND_FUNCTION
{
    gcvBLEND_ZERO,
    gcvBLEND_ONE,
    gcvBLEND_SOURCE_COLOR,
    gcvBLEND_INV_SOURCE_COLOR,
    gcvBLEND_SOURCE_ALPHA,
    gcvBLEND_INV_SOURCE_ALPHA,
    gcvBLEND_TARGET_COLOR,
    gcvBLEND_INV_TARGET_COLOR,
    gcvBLEND_TARGET_ALPHA,
    gcvBLEND_INV_TARGET_ALPHA,
    gcvBLEND_SOURCE_ALPHA_SATURATE,
    gcvBLEND_CONST_COLOR,
    gcvBLEND_INV_CONST_COLOR,
    gcvBLEND_CONST_ALPHA,
    gcvBLEND_INV_CONST_ALPHA,
}
gceBLEND_FUNCTION;

/* Blending modes. */
typedef enum _gceBLEND_MODE
{
    gcvBLEND_ADD,
    gcvBLEND_SUBTRACT,
    gcvBLEND_REVERSE_SUBTRACT,
    gcvBLEND_MIN,
    gcvBLEND_MAX,
}
gceBLEND_MODE;

/* API flags. */
typedef enum _gceAPI
{
    gcvAPI_D3D                  = 0x1,
    gcvAPI_OPENGL               = 0x2,
}
gceAPI;

/* Depth modes. */
typedef enum _gceDEPTH_MODE
{
    gcvDEPTH_NONE,
    gcvDEPTH_Z,
    gcvDEPTH_W,
}
gceDEPTH_MODE;

typedef enum _gceWHERE
{
    gcvWHERE_COMMAND,
    gcvWHERE_RASTER,
    gcvWHERE_PIXEL,
}
gceWHERE;

typedef enum _gceHOW
{
    gcvHOW_SEMAPHORE            = 0x1,
    gcvHOW_STALL                = 0x2,
    gcvHOW_SEMAPHORE_STALL      = 0x3,
}
gceHOW;

/******************************************************************************\
********************************* gcoHAL Object *********************************
\******************************************************************************/

/* Construct a new gcoHAL object. */
gceSTATUS
gcoHAL_Construct(
    IN gctPOINTER Context,
    IN gcoOS Os,
    OUT gcoHAL * Hal
    );

/* Destroy an gcoHAL object. */
gceSTATUS
gcoHAL_Destroy(
    IN gcoHAL Hal
    );

/* Get pointer to gco2D object. */
gceSTATUS
gcoHAL_Get2DEngine(
    IN gcoHAL Hal,
    OUT gco2D * Engine
    );

/* Get pointer to gcoVG object. */
gceSTATUS
gcoHAL_GetVGEngine(
    IN gcoHAL Hal,
    OUT gcoVG * Engine
    );

/* Get pointer to gco3D object. */
gceSTATUS
gcoHAL_Get3DEngine(
    IN gcoHAL Hal,
    OUT gco3D * Engine
    );

/* Verify whether the specified feature is available in hardware. */
gceSTATUS
gcoHAL_IsFeatureAvailable(
    IN gcoHAL Hal,
    IN gceFEATURE Feature
    );

/* Query the identity of the hardware. */
gceSTATUS
gcoHAL_QueryChipIdentity(
    IN gcoHAL Hal,
    OUT gceCHIPMODEL* ChipModel,
    OUT gctUINT32* ChipRevision,
    OUT gctUINT32* ChipFeatures,
    OUT gctUINT32* ChipMinorFeatures
    );

/* Query the amount of video memory. */
gceSTATUS
gcoHAL_QueryVideoMemory(
    IN gcoHAL Hal,
    OUT gctPHYS_ADDR * InternalAddress,
    OUT gctSIZE_T * InternalSize,
    OUT gctPHYS_ADDR * ExternalAddress,
    OUT gctSIZE_T * ExternalSize,
    OUT gctPHYS_ADDR * ContiguousAddress,
    OUT gctSIZE_T * ContiguousSize
    );

/* Map video memory. */
gceSTATUS
gcoHAL_MapMemory(
    IN gcoHAL Hal,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T NumberOfBytes,
    OUT gctPOINTER * Logical
    );

/* Unmap video memory. */
gceSTATUS
gcoHAL_UnmapMemory(
    IN gcoHAL Hal,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T NumberOfBytes,
    IN gctPOINTER Logical
    );

/* Schedule an unmap of a buffer mapped through its physical address. */
gceSTATUS
gcoHAL_ScheduleUnmapMemory(
    IN gcoHAL Hal,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T NumberOfBytes,
    IN gctPOINTER Logical
    );

/* Schedule an unmap of a user buffer using event mechanism. */
gceSTATUS
gcoHAL_ScheduleUnmapUserMemory(
    IN gcoHAL Hal,
    IN gctPOINTER Info,
    IN gctSIZE_T Size,
    IN gctUINT32 Address,
    IN gctPOINTER Memory
    );

/* Commit the current command buffer. */
gceSTATUS
gcoHAL_Commit(
    IN gcoHAL Hal,
    IN gctBOOL Stall
    );

/* Query the tile capabilities. */
gceSTATUS
gcoHAL_QueryTiled(
    IN gcoHAL Hal,
    OUT gctINT32 * TileWidth2D,
    OUT gctINT32 * TileHeight2D,
    OUT gctINT32 * TileWidth3D,
    OUT gctINT32 * TileHeight3D
    );

gceSTATUS
gcoHAL_Compact(
    IN gcoHAL Hal
    );

gceSTATUS
gcoHAL_ProfileStart(
    IN gcoHAL Hal
    );

gceSTATUS
gcoHAL_ProfileEnd(
    IN gcoHAL Hal,
    IN gctCONST_STRING Title
    );

/* Power Management */
gceSTATUS
gcoHAL_SetPowerManagementState(
    IN gcoHAL Hal,
    IN gceCHIPPOWERSTATE State
    );

gceSTATUS
gcoHAL_QueryPowerManagementState(
    IN gcoHAL Hal,
    OUT gceCHIPPOWERSTATE *State
    );

/* Set the filter type for filter blit. */
gceSTATUS
gcoHAL_SetFilterType(
    IN gcoHAL Hal,
    IN gceFILTER_TYPE FilterType
    );

gceSTATUS
gcoHAL_GetDump(
    IN gcoHAL Hal,
    OUT gcoDUMP * Dump
    );

/* Call the kernel HAL layer. */
gceSTATUS
gcoHAL_Call(
    IN gcoHAL Hal,
    IN OUT gcsHAL_INTERFACE_PTR Interface
    );

/* Schedule an event. */
gceSTATUS
gcoHAL_ScheduleEvent(
    IN gcoHAL Hal,
    IN OUT gcsHAL_INTERFACE_PTR Interface
    );

/* Destroy a surface. */
gceSTATUS
gcoHAL_DestroySurface(
    IN gcoHAL Hal,
    IN gcoSURF Surface
    );

/******************************************************************************\
********************************** gcoOS Object *********************************
\******************************************************************************/

/* Construct a new gcoOS object. */
gceSTATUS
gcoOS_Construct(
    IN gctPOINTER Context,
    OUT gcoOS * Os
    );

/* Destroy an gcoOS object. */
gceSTATUS
gcoOS_Destroy(
    IN gcoOS Os
    );

/* Get the base address for the physical memory. */
gceSTATUS
gcoOS_GetBaseAddress(
    IN gcoOS Os,
    OUT gctUINT32_PTR BaseAddress
    );

/* Allocate memory from the heap. */
gceSTATUS
gcoOS_Allocate(
    IN gcoOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    );

/* Free allocated memory. */
gceSTATUS
gcoOS_Free(
    IN gcoOS Os,
    IN gctPOINTER Memory
    );

/* Allocate memory. */
gceSTATUS
gcoOS_AllocateMemory(
    IN gcoOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    );

/* Free memory. */
gceSTATUS
gcoOS_FreeMemory(
    IN gcoOS Os,
    IN gctPOINTER Memory
    );

/* Allocate contiguous memory. */
gceSTATUS
gcoOS_AllocateContiguous(
    IN gcoOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    );

/* Free contiguous memory. */
gceSTATUS
gcoOS_FreeContiguous(
    IN gcoOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    );

/* Map user memory. */
gceSTATUS
gcoOS_MapUserMemory(
    IN gcoOS Os,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    OUT gctPOINTER * Info,
    OUT gctUINT32_PTR Address
    );

/* Unmap user memory. */
gceSTATUS
gcoOS_UnmapUserMemory(
    IN gcoOS Os,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    IN gctPOINTER Info,
    IN gctUINT32 Address
    );

/* Device I/O Control call to the kernel HAL layer. */
gceSTATUS
gcoOS_DeviceControl(
    IN gcoOS Os,
    IN gctUINT32 IoControlCode,
    IN gctPOINTER InputBuffer,
    IN gctSIZE_T InputBufferSize,
    IN gctPOINTER OutputBuffer,
    IN gctSIZE_T OutputBufferSize
    );

/* Allocate non paged memory. */
gceSTATUS gcoOS_AllocateNonPagedMemory(
        IN gcoOS Os,
        IN gctBOOL InUserSpace,
        IN OUT gctSIZE_T * Bytes,
        OUT gctPHYS_ADDR * Physical,
        OUT gctPOINTER * Logical
        );

/* Free non paged memory. */
gceSTATUS gcoOS_FreeNonPagedMemory(
        IN gcoOS Os,
        IN gctSIZE_T Bytes,
        IN gctPHYS_ADDR Physical,
        IN gctPOINTER Logical
        );

typedef enum _gceFILE_MODE
{
    gcvFILE_CREATE          = 0,
    gcvFILE_APPEND,
    gcvFILE_READ,
    gcvFILE_CREATETEXT,
    gcvFILE_APPENDTEXT,
    gcvFILE_READTEXT,
}
gceFILE_MODE;

/* Open a file. */
gceSTATUS
gcoOS_Open(
    IN gcoOS Os,
    IN gctCONST_STRING FileName,
    IN gceFILE_MODE Mode,
    OUT gctFILE * File
    );

/* Close a file. */
gceSTATUS
gcoOS_Close(
    IN gcoOS Os,
    IN gctFILE File
    );

/* Read data from a file. */
gceSTATUS
gcoOS_Read(
    IN gcoOS Os,
    IN gctFILE File,
    IN gctSIZE_T ByteCount,
    IN gctPOINTER Data,
    OUT gctSIZE_T * ByteRead
    );

/* Write data to a file. */
gceSTATUS
gcoOS_Write(
    IN gcoOS Os,
    IN gctFILE File,
    IN gctSIZE_T ByteCount,
    IN gctCONST_POINTER Data
    );

typedef enum _gceFILE_WHENCE
{
    gcvFILE_SEEK_SET,
    gcvFILE_SEEK_CUR,
    gcvFILE_SEEK_END
}
gceFILE_WHENCE;

/* Set the current position of a file. */
gceSTATUS
gcoOS_Seek(
    IN gcoOS Os,
    IN gctFILE File,
    IN gctUINT32 Offset,
    IN gceFILE_WHENCE Whence
    );

/* Set the current position of a file. */
gceSTATUS
gcoOS_SetPos(
    IN gcoOS Os,
    IN gctFILE File,
    IN gctUINT32 Position
    );

/* Get the current position of a file. */
gceSTATUS
gcoOS_GetPos(
    IN gcoOS Os,
    IN gctFILE File,
    OUT gctUINT32 * Position
    );

/* Perform a memory copy. */
gceSTATUS
gcoOS_MemCopy(
    IN gctPOINTER Destination,
    IN gctCONST_POINTER Source,
    IN gctSIZE_T Bytes
    );

/* Perform a memory fill. */
gceSTATUS
gcoOS_MemFill(
    IN gctPOINTER Destination,
    IN gctUINT8 Filler,
    IN gctSIZE_T Bytes
    );

/* Zero memory. */
gceSTATUS
gcoOS_ZeroMemory(
    IN gctPOINTER Memory,
    IN gctSIZE_T Bytes
    );

/* Find the last occurance of a character inside a string. */
gceSTATUS
gcoOS_StrFindReverse(
    IN gctCONST_STRING String,
    IN gctINT8 Character,
    OUT gctSTRING * Output
    );

gceSTATUS
gcoOS_StrLen(
    IN gctCONST_STRING String,
    OUT gctSIZE_T * Length
    );

gceSTATUS
gcoOS_StrDup(
    IN gcoOS Os,
    IN gctCONST_STRING String,
    OUT gctSTRING * Target
    );

/* Copy a string. */
gceSTATUS
gcoOS_StrCopySafe(
    IN gctSTRING Destination,
    IN gctSIZE_T DestinationSize,
    IN gctCONST_STRING Source
    );

/* Append a string. */
gceSTATUS
gcoOS_StrCatSafe(
    IN gctSTRING Destination,
    IN gctSIZE_T DestinationSize,
    IN gctCONST_STRING Source
    );

/* Compare two strings. */
gceSTATUS
gcoOS_StrCmp(
    IN gctCONST_STRING String1,
    IN gctCONST_STRING String2
    );

/* Compare characters of two strings. */
gceSTATUS
gcoOS_StrNCmp(
    IN gctCONST_STRING String1,
    IN gctCONST_STRING String2,
    IN gctSIZE_T Count
    );

/* Convert string to float. */
gceSTATUS
gcoOS_StrToFloat(
    IN gctCONST_STRING String,
    OUT gctFLOAT * Float
    );

/* Convert string to integer. */
gceSTATUS
gcoOS_StrToInt(
    IN gctCONST_STRING String,
    OUT gctINT * Int
    );

gceSTATUS
gcoOS_MemCmp(
    IN gctCONST_POINTER Memory1,
    IN gctCONST_POINTER Memory2,
    IN gctSIZE_T Bytes
    );

gceSTATUS
gcoOS_PrintStrSafe(
    OUT gctSTRING String,
    IN gctSIZE_T StringSize,
    IN OUT gctUINT * Offset,
    IN gctCONST_STRING Format,
    ...
    );

gceSTATUS
gcoOS_PrintStrVSafe(
    OUT gctSTRING String,
    IN gctSIZE_T StringSize,
    IN OUT gctUINT * Offset,
    IN gctCONST_STRING Format,
    IN gctPOINTER Arguments
    );

gceSTATUS
gcoOS_LoadLibrary(
    IN gcoOS Os,
    IN gctCONST_STRING Library,
    OUT gctHANDLE * Handle
    );

gceSTATUS
gcoOS_FreeLibrary(
    IN gcoOS Os,
    IN gctHANDLE Handle
    );

gceSTATUS
gcoOS_GetProcAddress(
    IN gcoOS Os,
    IN gctHANDLE Handle,
    IN gctCONST_STRING Name,
    OUT gctPOINTER * Function
    );

gceSTATUS
gcoOS_Compact(
    IN gcoOS Os
    );

gceSTATUS
gcoOS_ProfileStart(
    IN gcoOS Os
    );

gceSTATUS
gcoOS_ProfileEnd(
    IN gcoOS Os,
    IN gctCONST_STRING Title
    );

gceSTATUS
gcoOS_SetProfileSetting(
        IN gcoOS Os,
        IN gctBOOL Enable,
        IN gctCONST_STRING FileName
        );

/* Query the video memory. */
gceSTATUS
gcoOS_QueryVideoMemory(
    IN gcoOS Os,
    OUT gctPHYS_ADDR * InternalAddress,
    OUT gctSIZE_T * InternalSize,
    OUT gctPHYS_ADDR * ExternalAddress,
    OUT gctSIZE_T * ExternalSize,
    OUT gctPHYS_ADDR * ContiguousAddress,
    OUT gctSIZE_T * ContiguousSize
    );

/*----------------------------------------------------------------------------*/
/*----- Atoms ----------------------------------------------------------------*/

typedef struct gcsATOM * gcsATOM_PTR;

/* Construct an atom. */
gceSTATUS
gcoOS_AtomConstruct(
    IN gcoOS Os,
    OUT gcsATOM_PTR * Atom
    );

/* Destroy an atom. */
gceSTATUS
gcoOS_AtomDestroy(
    IN gcoOS Os,
    IN gcsATOM_PTR Atom
    );

/* Increment an atom. */
gceSTATUS
gcoOS_AtomIncrement(
    IN gcoOS Os,
    IN gcsATOM_PTR Atom,
    OUT gctINT32_PTR OldValue
    );

/* Decrement an atom. */
gceSTATUS
gcoOS_AtomDecrement(
    IN gcoOS Os,
    IN gcsATOM_PTR Atom,
    OUT gctINT32_PTR OldValue
    );

gctHANDLE
gcoOS_GetCurrentProcessID(
    void
    );

/*----------------------------------------------------------------------------*/
/*----- Time -----------------------------------------------------------------*/

/* Get the number of milliseconds since the system started. */
gctUINT32
gcoOS_GetTicks(
    void
    );

/* Get time in microseconds. */
gceSTATUS
gcoOS_GetTime(
    gctUINT64_PTR Time
    );

/* Get CPU usage in microseconds. */
gceSTATUS
gcoOS_GetCPUTime(
    gctUINT64_PTR CPUTime
    );

/* Get memory usage. */
gceSTATUS
gcoOS_GetMemoryUsage(
    gctUINT32_PTR MaxRSS,
    gctUINT32_PTR IxRSS,
    gctUINT32_PTR IdRSS,
    gctUINT32_PTR IsRSS
    );

/* Delay a number of microseconds. */
gceSTATUS
gcoOS_Delay(
    IN gcoOS Os,
    IN gctUINT32 Delay
    );

/*----------------------------------------------------------------------------*/
/*----- Mutexes --------------------------------------------------------------*/

/* Create a new mutex. */
gceSTATUS
gcoOS_CreateMutex(
    IN gcoOS Os,
    OUT gctPOINTER * Mutex
    );

/* Delete a mutex. */
gceSTATUS
gcoOS_DeleteMutex(
    IN gcoOS Os,
    IN gctPOINTER Mutex
    );

/* Acquire a mutex. */
gceSTATUS
gcoOS_AcquireMutex(
    IN gcoOS Os,
    IN gctPOINTER Mutex,
    IN gctUINT32 Timeout
    );

/* Release a mutex. */
gceSTATUS
gcoOS_ReleaseMutex(
    IN gcoOS Os,
    IN gctPOINTER Mutex
    );

/*----------------------------------------------------------------------------*/
/*----- Signals --------------------------------------------------------------*/

/* Create a signal. */
gceSTATUS
gcoOS_CreateSignal(
    IN gcoOS Os,
    IN gctBOOL ManualReset,
    OUT gctSIGNAL * Signal
    );

/* Destroy a signal. */
gceSTATUS
gcoOS_DestroySignal(
    IN gcoOS Os,
    IN gctSIGNAL Signal
    );

/* Signal a signal. */
gceSTATUS
gcoOS_Signal(
    IN gcoOS Os,
    IN gctSIGNAL Signal,
    IN gctBOOL State
    );

/* Wait for a signal. */
gceSTATUS
gcoOS_WaitSignal(
    IN gcoOS Os,
    IN gctSIGNAL Signal,
    IN gctUINT32 Wait
    );

/* Write a register. */
gceSTATUS
gcoOS_WriteRegister(
    IN gcoOS Os,
    IN gctUINT32 Address,
    IN gctUINT32 Data
    );

/* Read a register. */
gceSTATUS
gcoOS_ReadRegister(
    IN gcoOS Os,
    IN gctUINT32 Address,
    OUT gctUINT32 * Data
    );

gceSTATUS
gcoOS_CacheFlush(
    IN gcoOS Os,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    );

gceSTATUS
gcoOS_CacheInvalidate(
    IN gcoOS Os,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    );

/*******************************************************************************
**  gcoMATH object
*/

#define gcdPI                   3.14159265358979323846f

gctUINT32
gcoMATH_Log2in5dot5(
    IN gctINT X
    );

gctFLOAT
gcoMATH_Sine(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_Cosine(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_Floor(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_Ceiling(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_SquareRoot(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_Log2(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_Power(
    IN gctFLOAT X,
    IN gctFLOAT Y
    );

gctFLOAT
gcoMATH_Modulo(
    IN gctFLOAT X,
    IN gctFLOAT Y
    );

gctFLOAT
gcoMATH_Exp(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_Absolute(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_ArcCosine(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_Tangent(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_UInt2Float(
    IN gctUINT X
    );

gctUINT
gcoMATH_Float2UInt(
    IN gctFLOAT X
    );

gctFLOAT
gcoMATH_Multiply(
    IN gctFLOAT X,
    IN gctFLOAT Y
    );

/******************************************************************************\
**************************** Coordinate Structures *****************************
\******************************************************************************/

typedef struct _gcsPOINT
{
    gctINT32                    x;
    gctINT32                    y;
}
gcsPOINT;

typedef struct _gcsSIZE
{
    gctINT32                    width;
    gctINT32                    height;
}
gcsSIZE;

typedef struct _gcsRECT
{
    gctINT32                    left;
    gctINT32                    top;
    gctINT32                    right;
    gctINT32                    bottom;
}
gcsRECT;


/******************************************************************************\
********************************* gcoSURF Object ********************************
\******************************************************************************/

/*----------------------------------------------------------------------------*/
/*------------------------------- gcoSURF Common ------------------------------*/

/* Color format classes. */
typedef enum _gceFORMAT_CLASS
{
    gcvFORMAT_CLASS_RGBA        = 4500,
    gcvFORMAT_CLASS_YUV,
    gcvFORMAT_CLASS_INDEX,
    gcvFORMAT_CLASS_LUMINANCE,
    gcvFORMAT_CLASS_BUMP,
    gcvFORMAT_CLASS_DEPTH,
}
gceFORMAT_CLASS;

/* Special enums for width field in gcsFORMAT_COMPONENT. */
typedef enum _gceCOMPONENT_CONTROL
{
    gcvCOMPONENT_NOTPRESENT     = 0x00,
    gcvCOMPONENT_DONTCARE       = 0x80,
    gcvCOMPONENT_WIDTHMASK      = 0x7F,
    gcvCOMPONENT_ODD            = 0x80
}
gceCOMPONENT_CONTROL;

/* Color format component parameters. */
typedef struct _gcsFORMAT_COMPONENT
{
    gctUINT8                    start;
    gctUINT8                    width;
}
gcsFORMAT_COMPONENT;

/* RGBA color format class. */
typedef struct _gcsFORMAT_CLASS_TYPE_RGBA
{
    gcsFORMAT_COMPONENT         alpha;
    gcsFORMAT_COMPONENT         red;
    gcsFORMAT_COMPONENT         green;
    gcsFORMAT_COMPONENT         blue;
}
gcsFORMAT_CLASS_TYPE_RGBA;

/* YUV color format class. */
typedef struct _gcsFORMAT_CLASS_TYPE_YUV
{
    gcsFORMAT_COMPONENT         y;
    gcsFORMAT_COMPONENT         u;
    gcsFORMAT_COMPONENT         v;
}
gcsFORMAT_CLASS_TYPE_YUV;

/* Index color format class. */
typedef struct _gcsFORMAT_CLASS_TYPE_INDEX
{
    gcsFORMAT_COMPONENT         value;
}
gcsFORMAT_CLASS_TYPE_INDEX;

/* Luminance color format class. */
typedef struct _gcsFORMAT_CLASS_TYPE_LUMINANCE
{
    gcsFORMAT_COMPONENT         alpha;
    gcsFORMAT_COMPONENT         value;
}
gcsFORMAT_CLASS_TYPE_LUMINANCE;

/* Bump map color format class. */
typedef struct _gcsFORMAT_CLASS_TYPE_BUMP
{
    gcsFORMAT_COMPONENT         alpha;
    gcsFORMAT_COMPONENT         l;
    gcsFORMAT_COMPONENT         v;
    gcsFORMAT_COMPONENT         u;
    gcsFORMAT_COMPONENT         q;
    gcsFORMAT_COMPONENT         w;
}
gcsFORMAT_CLASS_TYPE_BUMP;

/* Depth and stencil format class. */
typedef struct _gcsFORMAT_CLASS_TYPE_DEPTH
{
    gcsFORMAT_COMPONENT         depth;
    gcsFORMAT_COMPONENT         stencil;
}
gcsFORMAT_CLASS_TYPE_DEPTH;

/* Format parameters. */
typedef struct _gcsSURF_FORMAT_INFO
{
    /* Format code and class. */
    gceSURF_FORMAT              format;
    gceFORMAT_CLASS             fmtClass;

    /* The size of one pixel in bits. */
    gctUINT8                    bitsPerPixel;

    /* Component swizzle. */
    gceSURF_SWIZZLE             swizzle;

    /* Some formats have two neighbour pixels interleaved together. */
    /* To describe such format, set the flag to 1 and add another   */
    /* like this one describing the odd pixel format.               */
    gctUINT8                    interleaved;

    /* Format components. */
    union
    {
        gcsFORMAT_CLASS_TYPE_BUMP       bump;
        gcsFORMAT_CLASS_TYPE_RGBA       rgba;
        gcsFORMAT_CLASS_TYPE_YUV        yuv;
        gcsFORMAT_CLASS_TYPE_LUMINANCE  lum;
        gcsFORMAT_CLASS_TYPE_INDEX      index;
        gcsFORMAT_CLASS_TYPE_DEPTH      depth;
    } u;
}
gcsSURF_FORMAT_INFO;

/* Frame buffer information. */
typedef struct _gcsSURF_FRAMEBUFFER
{
    gctPOINTER                  logical;
    gctUINT                     width, height;
    gctINT                      stride;
    gceSURF_FORMAT              format;
}
gcsSURF_FRAMEBUFFER;

/* Generic pixel component descriptors. */
extern gcsFORMAT_COMPONENT gcvPIXEL_COMP_XXX8;
extern gcsFORMAT_COMPONENT gcvPIXEL_COMP_XX8X;
extern gcsFORMAT_COMPONENT gcvPIXEL_COMP_X8XX;
extern gcsFORMAT_COMPONENT gcvPIXEL_COMP_8XXX;

typedef enum _gceORIENTATION
{
    gcvORIENTATION_TOP_BOTTOM,
    gcvORIENTATION_BOTTOM_TOP,
}
gceORIENTATION;


/* Construct a new gcoSURF object. */
gceSTATUS
gcoSURF_Construct(
    IN gcoHAL Hal,
    IN gctUINT Width,
    IN gctUINT Height,
    IN gctUINT Depth,
    IN gceSURF_TYPE Type,
    IN gceSURF_FORMAT Format,
    IN gcePOOL Pool,
    OUT gcoSURF * Surface
    );

/* Destroy an gcoSURF object. */
gceSTATUS
gcoSURF_Destroy(
    IN gcoSURF Surface
    );

/* Map user-allocated surface. */
gceSTATUS
gcoSURF_MapUserSurface(
    IN gcoSURF Surface,
    IN gctUINT Alignment,
    IN gctPOINTER Logical,
    IN gctUINT32 Physical
    );

/* Set the color type of the surface. */
gceSTATUS
gcoSURF_SetColorType(
    IN gcoSURF Surface,
    IN gceSURF_COLOR_TYPE ColorType
    );

/* Get the color type of the surface. */
gceSTATUS
gcoSURF_GetColorType(
    IN gcoSURF Surface,
    OUT gceSURF_COLOR_TYPE *ColorType
    );

/* Set the surface ration angle. */
gceSTATUS
gcoSURF_SetRotation(
    IN gcoSURF Surface,
    IN gceSURF_ROTATION Rotation
    );

/* Verify and return the state of the tile status mechanism. */
gceSTATUS
gcoSURF_IsTileStatusSupported(
    IN gcoSURF Surface
    );

/* Enable tile status for the specified surface. */
gceSTATUS
gcoSURF_EnableTileStatus(
    IN gcoSURF Surface
    );

/* Disable tile status for the specified surface. */
gceSTATUS
gcoSURF_DisableTileStatus(
    IN gcoSURF Surface,
    IN gctBOOL Decompress
    );

/* Get surface size. */
gceSTATUS
gcoSURF_GetSize(
    IN gcoSURF Surface,
    OUT gctUINT * Width,
    OUT gctUINT * Height,
    OUT gctUINT * Depth
    );

/* Get surface aligned sizes. */
gceSTATUS
gcoSURF_GetAlignedSize(
    IN gcoSURF Surface,
    OUT gctUINT * Width,
    OUT gctUINT * Height,
    OUT gctINT * Stride
    );

/* Get surface type and format. */
gceSTATUS
gcoSURF_GetFormat(
    IN gcoSURF Surface,
    OUT gceSURF_TYPE * Type,
    OUT gceSURF_FORMAT * Format
    );

/* Lock the surface. */
gceSTATUS
gcoSURF_Lock(
    IN gcoSURF Surface,
    IN OUT gctUINT32 * Address,
    IN OUT gctPOINTER * Memory
    );

/* Unlock the surface. */
gceSTATUS
gcoSURF_Unlock(
    IN gcoSURF Surface,
    IN gctPOINTER Memory
    );

/* Return pixel format parameters. */
gceSTATUS
gcoSURF_QueryFormat(
    IN gceSURF_FORMAT Format,
    OUT gcsSURF_FORMAT_INFO_PTR * Info
    );

/* Compute the color pixel mask. */
gceSTATUS
gcoSURF_ComputeColorMask(
    IN gcsSURF_FORMAT_INFO_PTR Format,
    OUT gctUINT32_PTR ColorMask
    );

/* Flush the surface. */
gceSTATUS
gcoSURF_Flush(
    IN gcoSURF Surface
    );

/* Fill surface with a value. */
gceSTATUS
gcoSURF_Fill(
    IN gcoSURF Surface,
    IN gcsPOINT_PTR Origin,
    IN gcsSIZE_PTR Size,
    IN gctUINT32 Value,
    IN gctUINT32 Mask
    );

/* Alpha blend two surfaces together. */
gceSTATUS
gcoSURF_Blend(
    IN gcoSURF SrcSurface,
    IN gcoSURF DestSurface,
    IN gcsPOINT_PTR SrcOrig,
    IN gcsPOINT_PTR DestOrigin,
    IN gcsSIZE_PTR Size,
    IN gceSURF_BLEND_MODE Mode
    );

/* Create a new gcoSURF wrapper object. */
gceSTATUS
gcoSURF_ConstructWrapper(
    IN gcoHAL Hal,
    OUT gcoSURF * Surface
    );

/* Set the underlying buffer for the surface wrapper. */
gceSTATUS
gcoSURF_SetBuffer(
    IN gcoSURF Surface,
    IN gceSURF_TYPE Type,
    IN gceSURF_FORMAT Format,
    IN gctUINT Stride,
    IN gctPOINTER Logical,
    IN gctUINT32 Physical
    );

/* Set the size of the surface in pixels and map the underlying buffer. */
gceSTATUS
gcoSURF_SetWindow(
    IN gcoSURF Surface,
    IN gctUINT X,
    IN gctUINT Y,
    IN gctUINT Width,
    IN gctUINT Height
    );

/* Increase reference count of the surface. */
gceSTATUS
gcoSURF_ReferenceSurface(
    IN gcoSURF Surface
    );

/* Get surface reference count. */
gceSTATUS
gcoSURF_QueryReferenceCount(
    IN gcoSURF Surface,
    OUT gctINT32 * ReferenceCount
    );

/* Set surface orientation. */
gceSTATUS
gcoSURF_SetOrientation(
    IN gcoSURF Surface,
    IN gceORIENTATION Orientation
    );

/* Query surface orientation. */
gceSTATUS
gcoSURF_QueryOrientation(
    IN gcoSURF Surface,
    OUT gceORIENTATION * Orientation
    );

/******************************************************************************\
********************************* gcoDUMP Object ********************************
\******************************************************************************/

/* Construct a new gcoDUMP object. */
gceSTATUS
gcoDUMP_Construct(
    IN gcoOS Os,
    IN gcoHAL Hal,
    OUT gcoDUMP * Dump
    );

/* Destroy a gcoDUMP object. */
gceSTATUS
gcoDUMP_Destroy(
    IN gcoDUMP Dump
    );

/* Enable/disable dumping. */
gceSTATUS
gcoDUMP_Control(
    IN gcoDUMP Dump,
    IN gctSTRING FileName
    );

gceSTATUS
gcoDUMP_IsEnabled(
    IN gcoDUMP Dump,
    OUT gctBOOL * Enabled
    );

/* Add surface. */
gceSTATUS
gcoDUMP_AddSurface(
    IN gcoDUMP Dump,
    IN gctINT32 Width,
    IN gctINT32 Height,
    IN gceSURF_FORMAT PixelFormat,
    IN gctUINT32 Address,
    IN gctSIZE_T ByteCount
    );

/* Mark the beginning of a frame. */
gceSTATUS
gcoDUMP_FrameBegin(
    IN gcoDUMP Dump
    );

/* Mark the end of a frame. */
gceSTATUS
gcoDUMP_FrameEnd(
    IN gcoDUMP Dump
    );

/* Dump data. */
gceSTATUS
gcoDUMP_DumpData(
    IN gcoDUMP Dump,
    IN gceDUMP_TAG Type,
    IN gctUINT32 Address,
    IN gctSIZE_T ByteCount,
    IN gctCONST_POINTER Data
    );

/* Delete an address. */
gceSTATUS
gcoDUMP_Delete(
    IN gcoDUMP Dump,
    IN gctUINT32 Address
    );

/******************************************************************************\
******************************* gcsRECT Structure ******************************
\******************************************************************************/

/* Initialize rectangle structure. */
gceSTATUS
gcsRECT_Set(
    OUT gcsRECT_PTR Rect,
    IN gctINT32 Left,
    IN gctINT32 Top,
    IN gctINT32 Right,
    IN gctINT32 Bottom
    );

/* Return the width of the rectangle. */
gceSTATUS
gcsRECT_Width(
    IN gcsRECT_PTR Rect,
    OUT gctINT32 * Width
    );

/* Return the height of the rectangle. */
gceSTATUS
gcsRECT_Height(
    IN gcsRECT_PTR Rect,
    OUT gctINT32 * Height
    );

/* Ensure that top left corner is to the left and above the right bottom. */
gceSTATUS
gcsRECT_Normalize(
    IN OUT gcsRECT_PTR Rect
    );

/* Compare two rectangles. */
gceSTATUS
gcsRECT_IsEqual(
    IN gcsRECT_PTR Rect1,
    IN gcsRECT_PTR Rect2,
    OUT gctBOOL * Equal
    );

/* Compare the sizes of two rectangles. */
gceSTATUS
gcsRECT_IsOfEqualSize(
    IN gcsRECT_PTR Rect1,
    IN gcsRECT_PTR Rect2,
    OUT gctBOOL * EqualSize
    );


/******************************************************************************\
**************************** gcsBOUNDARY Structure *****************************
\******************************************************************************/

typedef struct _gcsBOUNDARY
{
    gctINT                      x;
    gctINT                      y;
    gctINT                      width;
    gctINT                      height;
}
gcsBOUNDARY;

/******************************************************************************\
********************************* gcoHEAP Object ********************************
\******************************************************************************/

typedef struct _gcoHEAP *       gcoHEAP;

/* Construct a new gcoHEAP object. */
gceSTATUS
gcoHEAP_Construct(
    IN gcoOS Os,
    IN gctSIZE_T AllocationSize,
    OUT gcoHEAP * Heap
    );

/* Destroy an gcoHEAP object. */
gceSTATUS
gcoHEAP_Destroy(
    IN gcoHEAP Heap
    );

/* Allocate memory. */
gceSTATUS
gcoHEAP_Allocate(
    IN gcoHEAP Heap,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Node
    );

/* Free memory. */
gceSTATUS
gcoHEAP_Free(
    IN gcoHEAP Heap,
    IN gctPOINTER Node
    );

/* Profile the heap. */
gceSTATUS
gcoHEAP_ProfileStart(
    IN gcoHEAP Heap
    );

gceSTATUS
gcoHEAP_ProfileEnd(
    IN gcoHEAP Heap,
    IN gctCONST_STRING Title
    );

#if defined gcdHAL_TEST
gceSTATUS
gcoHEAP_Test(
    IN gcoHEAP Heap,
    IN gctSIZE_T Vectors,
    IN gctSIZE_T MaxSize
    );
#endif

/******************************************************************************\
******************************* Debugging Macros *******************************
\******************************************************************************/

void
gcoOS_SetDebugLevel(
    IN gctUINT32 Level
    );

void
gcoOS_SetDebugZone(
    IN gctUINT32 Zone
    );

void
gcoOS_SetDebugLevelZone(
    IN gctUINT32 Level,
    IN gctUINT32 Zone
    );

void
gcoOS_SetDebugZones(
    IN gctUINT32 Zones,
    IN gctBOOL Enable
    );

void
gcoOS_SetDebugFile(
    IN gctCONST_STRING FileName
    );

/*******************************************************************************
**
**  gcmFATAL
**
**      Print a message to the debugger and execute a break point.
**
**  ARGUMENTS:
**
**      message Message.
**      ...     Optional arguments.
*/

void
gckOS_DebugFatal(
    IN gctCONST_STRING Message,
    ...
    );

void
gcoOS_DebugFatal(
    IN gctCONST_STRING Message,
    ...
    );

#if gcdDEBUG
#   define gcmFATAL             gcoOS_DebugFatal
#   define gcmkFATAL            gckOS_DebugFatal
#elif gcdHAS_ELLIPSES
#   define gcmFATAL(...)
#   define gcmkFATAL(...)
#else
    gcmINLINE static void
    __dummy_fatal(
        IN gctCONST_STRING Message,
        ...
        )
    {
    }
#   define gcmFATAL             __dummy_fatal
#   define gcmkFATAL            __dummy_fatal
#endif

#define gcmENUM2TEXT(e)         case e: return #e

/*******************************************************************************
**
**  gcmTRACE
**
**      Print a message to the debugfer if the correct level has been set.  In
**      retail mode this macro does nothing.
**
**  ARGUMENTS:
**
**      level   Level of message.
**      message Message.
**      ...     Optional arguments.
*/
#define gcvLEVEL_NONE           -1
#define gcvLEVEL_ERROR          0
#define gcvLEVEL_WARNING        1
#define gcvLEVEL_INFO           2
#define gcvLEVEL_VERBOSE        3

void
gckOS_DebugTrace(
    IN gctUINT32 Level,
    IN gctCONST_STRING Message,
    ...
    );
void

gcoOS_DebugTrace(
    IN gctUINT32 Level,
    IN gctCONST_STRING Message,
    ...
    );

#if gcdDEBUG
#   define gcmTRACE             gcoOS_DebugTrace
#   define gcmkTRACE            gckOS_DebugTrace
#elif gcdHAS_ELLIPSES
#   define gcmTRACE(...)
#   define gcmkTRACE(...)
#else
    gcmINLINE static void
    __dummy_trace(
        IN gctUINT32 Level,
        IN gctCONST_STRING Message,
        ...
        )
    {
    }
#   define gcmTRACE             __dummy_trace
#   define gcmkTRACE            __dummy_trace
#endif

/* Debug zones. */
#define gcvZONE_OS              (1 << 0)
#define gcvZONE_HARDWARE        (1 << 1)
#define gcvZONE_HEAP            (1 << 2)

/* Kernel zones. */
#define gcvZONE_KERNEL          (1 << 3)
#define gcvZONE_VIDMEM          (1 << 4)
#define gcvZONE_COMMAND         (1 << 5)
#define gcvZONE_DRIVER          (1 << 6)
#define gcvZONE_CMODEL          (1 << 7)
#define gcvZONE_MMU             (1 << 8)
#define gcvZONE_EVENT           (1 << 9)
#define gcvZONE_DEVICE          (1 << 10)

/* User zones. */
#define gcvZONE_HAL             (1 << 3)
#define gcvZONE_BUFFER          (1 << 4)
#define gcvZONE_CONTEXT         (1 << 5)
#define gcvZONE_SURFACE         (1 << 6)
#define gcvZONE_INDEX           (1 << 7)
#define gcvZONE_STREAM          (1 << 8)
#define gcvZONE_TEXTURE         (1 << 9)
#define gcvZONE_2D              (1 << 10)
#define gcvZONE_3D              (1 << 11)
#define gcvZONE_COMPILER        (1 << 12)
#define gcvZONE_MEMORY          (1 << 13)
#define gcvZONE_STATE           (1 << 14)
#define gcvZONE_AUX             (1 << 15)

/* API definitions. */
#define gcvZONE_API_HAL         (0 << 28)
#define gcvZONE_API_EGL         (1 << 28)
#define gcvZONE_API_ES11        (2 << 28)
#define gcvZONE_API_ES20        (3 << 28)
#define gcvZONE_API_VG11        (4 << 28)
#define gcvZONE_API_GL          (5 << 28)
#define gcvZONE_API_DFB         (6 << 28)
#define gcvZONE_API_GDI         (7 << 28)
#define gcvZONE_API_D3D         (8 << 28)

#define gcmZONE_GET_API(zone)   ((zone) >> 28)
#define gcdZONE_MASK            0x0FFFFFFF

/* Handy zones. */
#define gcvZONE_NONE            0
#define gcvZONE_ALL             gcdZONE_MASK

/*******************************************************************************
**
**  gcmTRACE_ZONE
**
**      Print a message to the debugger if the correct level and zone has been
**      set.  In retail mode this macro does nothing.
**
**  ARGUMENTS:
**
**      Level   Level of message.
**      Zone    Zone of message.
**      Message Message.
**      ...     Optional arguments.
*/

void
gckOS_DebugTraceZone(
    IN gctUINT32 Level,
    IN gctUINT32 Zone,
    IN gctCONST_STRING Message,
    ...
    );

void
gcoOS_DebugTraceZone(
    IN gctUINT32 Level,
    IN gctUINT32 Zone,
    IN gctCONST_STRING Message,
    ...
    );

#if gcdDEBUG
#   define gcmTRACE_ZONE            gcoOS_DebugTraceZone
#   define gcmkTRACE_ZONE           gckOS_DebugTraceZone
#elif gcdHAS_ELLIPSES
#   define gcmTRACE_ZONE(...)
#   define gcmkTRACE_ZONE(...)
#else
    gcmINLINE static void
    __dummy_trace_zone(
        IN gctUINT32 Level,
        IN gctUINT32 Zone,
        IN gctCONST_STRING Message,
        ...
        )
    {
    }
#   define gcmTRACE_ZONE            __dummy_trace_zone
#   define gcmkTRACE_ZONE           __dummy_trace_zone
#endif

/******************************************************************************\
******************************** Logging Macros ********************************
\******************************************************************************/

#define gcdHEADER_LEVEL             gcvLEVEL_VERBOSE

#define gcmHEADER() \
    gcmTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                  "++%s(%d)", __FUNCTION__, __LINE__)

#if gcdHAS_ELLIPSES
#   define gcmHEADER_ARG(Text, ...) \
        gcmTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                      "++%s(%d): " Text, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
    gcmINLINE static void
    __dummy_header_arg(
        IN gctCONST_STRING Text,
        ...
        )
    {
    }
#   define gcmHEADER_ARG                __dummy_header_arg
#endif

#define gcmFOOTER() \
    gcmTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                  "--%s(%d): status=%d", \
                  __FUNCTION__, __LINE__, status)

#define gcmFOOTER_NO() \
    gcmTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                  "--%s(%d)", __FUNCTION__, __LINE__)

#if gcdHAS_ELLIPSES
#   define gcmFOOTER_ARG(Text, ...) \
        gcmTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                      "--%s(%d): " Text, \
                      __FUNCTION__, __LINE__, __VA_ARGS__)
#else
    gcmINLINE static void
    __dummy_footer_arg(
        IN gctCONST_STRING Text,
        ...
        )
    {
    }
#   define gcmFOOTER_ARG                __dummy_footer_arg
#endif

#define gcmkHEADER() \
    gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                   "++%s(%d)", __FUNCTION__, __LINE__)

#if gcdHAS_ELLIPSES
#   define gcmkHEADER_ARG(Text, ...) \
        gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                       "++%s(%d): " Text, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
    gcmINLINE static void
    __dummy_kheader_arg(
        IN gctCONST_STRING Text,
        ...
        )
    {
    }
#   define gcmkHEADER_ARG               __dummy_kheader_arg
#endif

#define gcmkFOOTER() \
    gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                   "--%s(%d): status=%d", \
                   __FUNCTION__, __LINE__, status)

#define gcmkFOOTER_NO() \
    gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                   "--%s(%d)", __FUNCTION__, __LINE__)

#if gcdHAS_ELLIPSES
#   define gcmkFOOTER_ARG(Text, ...) \
        gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                       "--%s(%d): " Text, \
                       __FUNCTION__, __LINE__, __VA_ARGS__)
#else
    gcmINLINE static void
    __dummy_kfooter_arg(
        IN gctCONST_STRING Text,
        ...
        )
    {
    }
#   define gcmkFOOTER_ARG               __dummy_kfooter_arg
#endif

#define gcmOPT_VALUE(ptr)           (((ptr) == gcvNULL) ? 0 : *(ptr))
#define gcmOPT_POINTER(ptr)         (((ptr) == gcvNULL) ? gcvNULL : *(ptr))

void
gcoOS_Print(
    IN gctCONST_STRING Message,
    ...
    );
void
gckOS_Print(
    IN gctCONST_STRING Message,
    ...
    );
#define gcmPRINT                gcoOS_Print
#define gcmkPRINT               gckOS_Print

/*******************************************************************************
**
**  gcmDUMP
**
**      Print a dump message.
**
**  ARGUMENTS:
**
**      gctSTRING   Message.
**
**      ...         Optional arguments.
*/
#if gcdDUMP
    gceSTATUS
    gcfDump(
        IN gcoOS Os,
        IN gctCONST_STRING String,
        ...
        );
#  define gcmDUMP               gcfDump
#elif gcdHAS_ELLIPSES
#  define gcmDUMP(...)
#else
    gcmINLINE static void
    __dummy_dump(
        IN gcoOS Os,
        IN gctCONST_STRING Message,
        ...
        )
    {
    }
#  define gcmDUMP               __dummy_dump
#endif

/*******************************************************************************
**
**  gcmDUMP_DATA
**
**      Add data to the dump.
**
**  ARGUMENTS:
**
**      gctSTRING Tag
**          Tag for dump.
**
**      gctPOINTER Logical
**          Logical address of buffer.
**
**      gctSIZE_T Bytes
**          Number of bytes.
*/

#if gcdDUMP
    gceSTATUS
    gcfDumpData(
        IN gcoOS Os,
        IN gctSTRING Tag,
        IN gctPOINTER Logical,
        IN gctSIZE_T Bytes
        );
#  define gcmDUMP_DATA          gcfDumpData
#elif gcdHAS_ELLIPSES
#  define gcmDUMP_DATA(...)
#else
    gcmINLINE static void
    __dummy_dump_data(
        IN gcoOS Os,
        IN gctSTRING Tag,
        IN gctPOINTER Logical,
        IN gctSIZE_T Bytes
        )
    {
    }
#  define gcmDUMP_DATA          __dummy_dump_data
#endif

/*******************************************************************************
**
**  gcmDUMP_BUFFER
**
**      Print a buffer to the dump.
**
**  ARGUMENTS:
**
**      gctSTRING Tag
**          Tag for dump.
**
**      gctUINT32 Physical
**          Physical address of buffer.
**
**      gctPOINTER Logical
**          Logical address of buffer.
**
**      gctUINT32 Offset
**          Offset into buffer.
**
**      gctSIZE_T Bytes
**          Number of bytes.
*/

#if gcdDUMP
gceSTATUS
gcfDumpBuffer(
    IN gcoOS Os,
    IN gctSTRING Tag,
    IN gctUINT32 Physical,
    IN gctPOINTER Logical,
    IN gctUINT32 Offset,
    IN gctSIZE_T Bytes
    );
#   define gcmDUMP_BUFFER       gcfDumpBuffer
#elif gcdHAS_ELLIPSES
#   define gcmDUMP_BUFFER(...)
#else
    gcmINLINE static void
    __dummy_dump_buffer(
        IN gcoOS Os,
        IN gctSTRING Tag,
        IN gctUINT32 Physical,
        IN gctPOINTER Logical,
        IN gctUINT32 Offset,
        IN gctSIZE_T Bytes
        )
    {
    }
#   define gcmDUMP_BUFFER       __dummy_dump_buffer
#endif

/*******************************************************************************
**
**  gcmDUMP_API
**
**      Print a dump message for a high level API prefixed by the function name.
**
**  ARGUMENTS:
**
**      gctSTRING   Message.
**
**      ...         Optional arguments.
*/
#if gcdDUMP_API
    gceSTATUS
    gcfDumpApi(
        IN gctCONST_STRING String,
        ...
        );
#   define gcmDUMP_API           gcfDumpApi
#elif gcdHAS_ELLIPSES
#   define gcmDUMP_API(...)
#else
    gcmINLINE static void
    __dummy_dump_api(
        IN gctCONST_STRING Message,
        ...
        )
    {
    }
#  define gcmDUMP_API           __dummy_dump_api
#endif

/*******************************************************************************
**
**  gcmDUMP_API_ARRAY
**
**      Print an array of data.
**
**  ARGUMENTS:
**
**      gctUINT32_PTR   Pointer to array.
**      gctUINT32       Size.
*/
#if gcdDUMP_API
    gceSTATUS
    gcfDumpArray(
        IN gctCONST_POINTER Data,
        IN gctUINT32 Size
    );
#   define gcmDUMP_API_ARRAY        gcfDumpArray
#elif gcdHAS_ELLIPSES
#   define gcmDUMP_API_ARRAY(...)
#else
    gcmINLINE static void
    __dummy_dump_api_array(
        IN gctCONST_POINTER Data,
        IN gctUINT32 Size
        )
    {
    }
#   define gcmDUMP_API_ARRAY        __dummy_dump_api_array
#endif

/*******************************************************************************
**
**  gcmDUMP_API_ARRAY_TOKEN
**
**      Print an array of data terminated by a token.
**
**  ARGUMENTS:
**
**      gctUINT32_PTR   Pointer to array.
**      gctUINT32       Termination.
*/
#if gcdDUMP_API
    gceSTATUS
    gcfDumpArrayToken(
        IN gctCONST_POINTER Data,
        IN gctUINT32 Termination
    );
#   define gcmDUMP_API_ARRAY_TOKEN  gcfDumpArrayToken
#elif gcdHAS_ELLIPSES
#   define gcmDUMP_API_ARRAY_TOKEN(...)
#else
    gcmINLINE static void
    __dummy_dump_api_array_token(
        IN gctCONST_POINTER Data,
        IN gctUINT32 Termination
        )
    {
    }
#   define gcmDUMP_API_ARRAY_TOKEN  __dummy_dump_api_array_token
#endif

/*******************************************************************************
**
**  gcmTRACE_RELEASE
**
**      Print a message to the shader debugger.
**
**  ARGUMENTS:
**
**      message Message.
**      ...     Optional arguments.
*/

#define gcmTRACE_RELEASE                gcoOS_DebugShaderTrace

void
gcoOS_DebugShaderTrace(
    IN gctCONST_STRING Message,
    ...
    );

void
gcoOS_SetDebugShaderFiles(
    IN gctCONST_STRING VSFileName,
    IN gctCONST_STRING FSFileName
    );

void
gcoOS_SetDebugShaderFileType(
    IN gctUINT32 ShaderType
    );

/*******************************************************************************
**
**  gcmBREAK
**
**      Break into the debugger.  In retail mode this macro does nothing.
**
**  ARGUMENTS:
**
**      None.
*/

void
gcoOS_DebugBreak(
    void
    );

void
gckOS_DebugBreak(
    void
    );

#if gcdDEBUG
#   define gcmBREAK             gcoOS_DebugBreak
#   define gcmkBREAK            gckOS_DebugBreak
#else
#   define gcmBREAK()
#   define gcmkBREAK()
#endif

/*******************************************************************************
**
**  gcmASSERT
**
**      Evaluate an expression and break into the debugger if the expression
**      evaluates to false.  In retail mode this macro does nothing.
**
**  ARGUMENTS:
**
**      exp     Expression to evaluate.
*/
#if gcdDEBUG
#   define _gcmASSERT(prefix, exp) \
        do \
        { \
            if (!(exp)) \
            { \
                prefix##TRACE(gcvLEVEL_ERROR, \
                              #prefix "ASSERT at %s(%d) in " __FILE__, \
                              __FUNCTION__, __LINE__); \
                prefix##TRACE(gcvLEVEL_ERROR, \
                              "(%s)", #exp); \
                prefix##BREAK(); \
            } \
        } \
        while (gcvFALSE)
#   define gcmASSERT(exp)           _gcmASSERT(gcm, exp)
#   define gcmkASSERT(exp)          _gcmASSERT(gcmk, exp)
#else
#   define gcmASSERT(exp)
#   define gcmkASSERT(exp)
#endif

/*******************************************************************************
**
**  gcmVERIFY
**
**      Verify if an expression returns true.  If the expression does not
**      evaluates to true, an assertion will happen in debug mode.
**
**  ARGUMENTS:
**
**      exp     Expression to evaluate.
*/
#if gcdDEBUG
#   define gcmVERIFY(exp)           gcmASSERT(exp)
#   define gcmkVERIFY(exp)          gcmkASSERT(exp)
#else
#   define gcmVERIFY(exp)           exp
#   define gcmkVERIFY(exp)          exp
#endif

/*******************************************************************************
**
**  gcmVERIFY_OK
**
**      Verify a fucntion returns gcvSTATUS_OK.  If the function does not return
**      gcvSTATUS_OK, an assertion will happen in debug mode.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/

void
gcoOS_Verify(
    IN gceSTATUS Status
    );

void
gckOS_Verify(
    IN gceSTATUS Status
    );

#if gcdDEBUG
#   define gcmVERIFY_OK(func) \
        do \
        { \
            gceSTATUS verifyStatus = func; \
            gcoOS_Verify(verifyStatus); \
            gcmASSERT(verifyStatus == gcvSTATUS_OK); \
        } \
        while (gcvFALSE)
#   define gcmkVERIFY_OK(func) \
        do \
        { \
            gceSTATUS verifyStatus = func; \
            gckOS_Verify(verifyStatus); \
            gcmkASSERT(verifyStatus == gcvSTATUS_OK); \
        } \
        while (gcvFALSE)
#else
#   define gcmVERIFY_OK(func)       func
#   define gcmkVERIFY_OK(func)      func
#endif

/*******************************************************************************
**
**  gcmERR_BREAK
**
**      Executes a break statement on error.
**
**  ASSUMPTIONS:
**
**      'status' variable of gceSTATUS type must be defined.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
#define _gcmERR_BREAK(prefix, func) \
    status = func; \
    if (gcmIS_ERROR(status)) \
    { \
        prefix##TRACE(gcvLEVEL_ERROR, \
            #prefix "ERR_BREAK: status=%d @ %s(%d) in " __FILE__, \
            status, __FUNCTION__, __LINE__); \
        break; \
    } \
    do { } while (gcvFALSE)
#define gcmERR_BREAK(func)          _gcmERR_BREAK(gcm, func)
#define gcmkERR_BREAK(func)         _gcmERR_BREAK(gcmk, func)

/*******************************************************************************
**
**  gcmERR_RETURN
**
**      Executes a return on error.
**
**  ASSUMPTIONS:
**
**      'status' variable of gceSTATUS type must be defined.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
#define _gcmERR_RETURN(prefix, func) \
    status = func; \
    if (gcmIS_ERROR(status)) \
    { \
        prefix##TRACE(gcvLEVEL_ERROR, \
            #prefix "ERR_RETURN: status=%d @ %s(%d) in " __FILE__, \
            status, __FUNCTION__, __LINE__); \
        return status; \
    } \
    do { } while (gcvFALSE)
#define gcmERR_RETURN(func)         _gcmERR_RETURN(gcm, func)
#define gcmkERR_RETURN(func)        _gcmERR_RETURN(gcmk, func)

/*******************************************************************************
**
**  gcmONERROR
**
**      Jump to the error handler in case there is an error.
**
**  ASSUMPTIONS:
**
**      'status' variable of gceSTATUS type must be defined.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
#define _gcmONERROR(prefix, func) \
    do \
    { \
        status = func; \
        if (gcmIS_ERROR(status)) \
        { \
            prefix##TRACE(gcvLEVEL_ERROR, \
                #prefix "ONERROR: status=%d @ %s(%d) in " __FILE__, \
                status, __FUNCTION__, __LINE__); \
            goto OnError; \
        } \
    } \
    while (gcvFALSE)
#define gcmONERROR(func)            _gcmONERROR(gcm, func)
#define gcmkONERROR(func)           _gcmONERROR(gcmk, func)

/*******************************************************************************
**
**  gcmVERIFY_LOCK
**
**      Verifies whether the surface is locked.
**
**  ARGUMENTS:
**
**      surfaceInfo Pointer to the surface iniformational structure.
*/
#define gcmVERIFY_LOCK(surfaceInfo) \
    if (!surfaceInfo->node.valid) \
    { \
        status = gcvSTATUS_MEMORY_UNLOCKED; \
        break; \
    } \
    do { } while (gcvFALSE)

/*******************************************************************************
**
**  gcmVERIFY_NODE_LOCK
**
**      Verifies whether the surface node is locked.
**
**  ARGUMENTS:
**
**      surfaceInfo Pointer to the surface iniformational structure.
*/
#define gcmVERIFY_NODE_LOCK(surfaceNode) \
    if (!surfaceNode->valid) \
    { \
        status = gcvSTATUS_MEMORY_UNLOCKED; \
        break; \
    } \
    do { } while (gcvFALSE)

/*******************************************************************************
**
**  gcmBADOBJECT_BREAK
**
**      Executes a break statement on bad object.
**
**  ARGUMENTS:
**
**      obj     Object to test.
**      t       Expected type of the object.
*/
#define gcmBADOBJECT_BREAK(obj, t) \
    if ((obj == gcvNULL) \
    ||  (((gcsOBJECT *)(obj))->type != t) \
    ) \
    { \
        status = gcvSTATUS_INVALID_OBJECT; \
        break; \
    } \
    do { } while (gcvFALSE)

/*******************************************************************************
**
**  gcmCHECK_STATUS
**
**      Executes a break statement on error.
**
**  ASSUMPTIONS:
**
**      'status' variable of gceSTATUS type must be defined.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
#define _gcmCHECK_STATUS(prefix, func) \
    do \
    { \
        last = func; \
        if (gcmIS_ERROR(last)) \
        { \
            prefix##TRACE(gcvLEVEL_ERROR, \
                #prefix "CHECK_STATUS: status=%d @ %s(%d) in " __FILE__, \
                last, __FUNCTION__, __LINE__); \
            status = last; \
        } \
    } \
    while (gcvFALSE)
#define gcmCHECK_STATUS(func)       _gcmCHECK_STATUS(gcm, func)
#define gcmkCHECK_STATUS(func)      _gcmCHECK_STATUS(gcmk, func)

/*******************************************************************************
**
**  gcmVERIFY_ARGUMENT
**
**      Assert if an argument does not apply to the specified expression.  If
**      the argument evaluates to false, gcvSTATUS_INVALID_ARGUMENT will be
**      returned from the current function.  In retail mode this macro does
**      nothing.
**
**  ARGUMENTS:
**
**      arg     Argument to evaluate.
*/
#ifndef EGL_API_ANDROID
#   define _gcmVERIFY_ARGUMENT(prefix, arg) \
       do \
       { \
           if (!(arg)) \
           { \
               prefix##TRACE(gcvLEVEL_ERROR, #prefix "VERIFY_ARGUMENT failed:"); \
               prefix##ASSERT(arg); \
               prefix##FOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT); \
               return gcvSTATUS_INVALID_ARGUMENT; \
           } \
       } \
       while (gcvFALSE)
#   define gcmVERIFY_ARGUMENT(arg)     _gcmVERIFY_ARGUMENT(gcm, arg)
#   define gcmkVERIFY_ARGUMENT(arg)    _gcmVERIFY_ARGUMENT(gcmk, arg)
#else
#   define gcmVERIFY_ARGUMENT(arg)
#   define gcmkVERIFY_ARGUMENT(arg)
#endif

/*******************************************************************************
**
**  gcmVERIFY_ARGUMENT_RETURN
**
**      Assert if an argument does not apply to the specified expression.  If
**      the argument evaluates to false, gcvSTATUS_INVALID_ARGUMENT will be
**      returned from the current function.  In retail mode this macro does
**      nothing.
**
**  ARGUMENTS:
**
**      arg     Argument to evaluate.
*/
#ifndef EGL_API_ANDROID
#   define _gcmVERIFY_ARGUMENT_RETURN(prefix, arg, value) \
       do \
       { \
           if (!(arg)) \
           { \
               prefix##TRACE(gcvLEVEL_ERROR, \
                             #prefix "gcmVERIFY_ARGUMENT_RETURN failed:"); \
               prefix##ASSERT(arg); \
               prefix##FOOTER_ARG("value=%d", value); \
               return value; \
           } \
       } \
       while (gcvFALSE)
#   define gcmVERIFY_ARGUMENT_RETURN(arg, value) \
                _gcmVERIFY_ARGUMENT_RETURN(gcm, arg, value)
#   define gcmkVERIFY_ARGUMENT_RETURN(arg, value) \
                _gcmVERIFY_ARGUMENT_RETURN(gcmk, arg, value)
#else
#   define gcmVERIFY_ARGUMENT_RETURN(arg, value)
#   define gcmkVERIFY_ARGUMENT_RETURN(arg, value)
#endif
#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_base_h_ */

