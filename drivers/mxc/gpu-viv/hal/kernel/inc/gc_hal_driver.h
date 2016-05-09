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

#ifndef __gc_hal_driver_h_
#define __gc_hal_driver_h_

#include "gc_hal_enum.h"
#include "gc_hal_types.h"

#if gcdENABLE_VG
#include "gc_hal_driver_vg.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
******************************* I/O Control Codes ******************************
\******************************************************************************/

#define gcvHAL_CLASS                    "galcore"
#define IOCTL_GCHAL_INTERFACE           30000
#define IOCTL_GCHAL_KERNEL_INTERFACE    30001
#define IOCTL_GCHAL_TERMINATE           30002

#undef CONFIG_ANDROID_RESERVED_MEMORY_ACCOUNT
/******************************************************************************\
********************************* Command Codes ********************************
\******************************************************************************/

typedef enum _gceHAL_COMMAND_CODES
{
    /* Generic query. */
    gcvHAL_QUERY_VIDEO_MEMORY,
    gcvHAL_QUERY_CHIP_IDENTITY,

    /* Contiguous memory. */
    gcvHAL_ALLOCATE_NON_PAGED_MEMORY,
    gcvHAL_FREE_NON_PAGED_MEMORY,
    gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY,
    gcvHAL_FREE_CONTIGUOUS_MEMORY,

    /* Video memory allocation. */
    gcvHAL_ALLOCATE_VIDEO_MEMORY,           /* Enforced alignment. */
    gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY,    /* No alignment. */
    gcvHAL_RELEASE_VIDEO_MEMORY,

    /* Physical-to-logical mapping. */
    gcvHAL_MAP_MEMORY,
    gcvHAL_UNMAP_MEMORY,

    /* Logical-to-physical mapping. */
    gcvHAL_MAP_USER_MEMORY,
    gcvHAL_UNMAP_USER_MEMORY,

    /* Surface lock/unlock. */
    gcvHAL_LOCK_VIDEO_MEMORY,
    gcvHAL_UNLOCK_VIDEO_MEMORY,

    /* Event queue. */
    gcvHAL_EVENT_COMMIT,

    gcvHAL_USER_SIGNAL,
    gcvHAL_SIGNAL,
    gcvHAL_WRITE_DATA,

    gcvHAL_COMMIT,
    gcvHAL_STALL,

    gcvHAL_READ_REGISTER,
    gcvHAL_WRITE_REGISTER,

    gcvHAL_GET_PROFILE_SETTING,
    gcvHAL_SET_PROFILE_SETTING,

    gcvHAL_READ_ALL_PROFILE_REGISTERS,
    gcvHAL_PROFILE_REGISTERS_2D,
#if VIVANTE_PROFILER_PERDRAW
    gcvHAL_READ_PROFILER_REGISTER_SETTING,
#endif

    /* Power management. */
    gcvHAL_SET_POWER_MANAGEMENT_STATE,
    gcvHAL_QUERY_POWER_MANAGEMENT_STATE,

    gcvHAL_GET_BASE_ADDRESS,

    gcvHAL_SET_IDLE, /* reserved */

    /* Queries. */
    gcvHAL_QUERY_KERNEL_SETTINGS,

    /* Reset. */
    gcvHAL_RESET,

    /* Map physical address into handle. */
    gcvHAL_MAP_PHYSICAL,

    /* Debugger stuff. */
    gcvHAL_DEBUG,

    /* Cache stuff. */
    gcvHAL_CACHE,

    /* TimeStamp */
    gcvHAL_TIMESTAMP,

    /* Database. */
    gcvHAL_DATABASE,

    /* Version. */
    gcvHAL_VERSION,

    /* Chip info */
    gcvHAL_CHIP_INFO,

    /* Process attaching/detaching. */
    gcvHAL_ATTACH,
    gcvHAL_DETACH,

    /* Composition. */
    gcvHAL_COMPOSE,

    /* Set timeOut value */
    gcvHAL_SET_TIMEOUT,

    /* Frame database. */
    gcvHAL_GET_FRAME_INFO,

    gcvHAL_QUERY_COMMAND_BUFFER,

    gcvHAL_COMMIT_DONE,

    /* GPU and event dump */
    gcvHAL_DUMP_GPU_STATE,
    gcvHAL_DUMP_EVENT,

    /* Virtual command buffer. */
    gcvHAL_ALLOCATE_VIRTUAL_COMMAND_BUFFER,
    gcvHAL_FREE_VIRTUAL_COMMAND_BUFFER,

    /* FSCALE_VAL. */
    gcvHAL_SET_FSCALE_VALUE,
    gcvHAL_GET_FSCALE_VALUE,

    gcvHAL_NAME_VIDEO_MEMORY,
    gcvHAL_IMPORT_VIDEO_MEMORY,

    /* Reset time stamp. */
    gcvHAL_QUERY_RESET_TIME_STAMP,

    /* Multi-GPU read/write. */
    gcvHAL_READ_REGISTER_EX,
    gcvHAL_WRITE_REGISTER_EX,

    /* Sync point operations. */
    gcvHAL_SYNC_POINT,

    /* Create native fence and return its fd. */
    gcvHAL_CREATE_NATIVE_FENCE,

    /* Let GPU wait on native fence. */
    gcvHAL_WAIT_NATIVE_FENCE,

    /* Destory MMU. */
    gcvHAL_DESTROY_MMU,

    /* Shared buffer. */
    gcvHAL_SHBUF,


    /* Connect a video node to an OS native fd. */
    gcvHAL_GET_VIDEO_MEMORY_FD,

    /* Config power management. */
    gcvHAL_CONFIG_POWER_MANAGEMENT,

    /* Wrap a user memory into a video memory node. */
    gcvHAL_WRAP_USER_MEMORY,

    /* Wait until GPU finishes access to a resource. */
    gcvHAL_WAIT_FENCE,

#if gcdENABLE_DEC_COMPRESSION && gcdDEC_ENABLE_AHB
    gcvHAL_DEC300_READ,
    gcvHAL_DEC300_WRITE,
    gcvHAL_DEC300_FLUSH,
    gcvHAL_DEC300_FLUSH_WAIT,
#endif
}
gceHAL_COMMAND_CODES;

/******************************************************************************\
****************************** Interface Structure *****************************
\******************************************************************************/

#define gcdMAX_PROFILE_FILE_NAME    128

/* Kernel settings. */
typedef struct _gcsKERNEL_SETTINGS
{
    /* Used RealTime signal between kernel and user. */
    gctINT signal;
}
gcsKERNEL_SETTINGS;

typedef struct _gcsUSER_MEMORY_DESC
{
    /* Import flag. */
    gctUINT32                  flag;

    /* gcvALLOC_FLAG_DMABUF */
    gctUINT32                  handle;

    /* gcvALLOC_FLAG_USERMEMORY */
    gctUINT64                  logical;
    gctUINT32                  physical;
    gctUINT32                  size;

    /* gcvALLOC_FLAG_EXTERNAL_MEMORY */
    gcsEXTERNAL_MEMORY_INFO    externalMemoryInfo;
}
gcsUSER_MEMORY_DESC;

/* gcvHAL_QUERY_CHIP_IDENTITY */
typedef struct _gcsHAL_QUERY_CHIP_IDENTITY * gcsHAL_QUERY_CHIP_IDENTITY_PTR;
typedef struct _gcsHAL_QUERY_CHIP_IDENTITY
{

    /* Chip model. */
    gceCHIPMODEL                chipModel;

    /* Revision value.*/
    gctUINT32                   chipRevision;

    /* Chip date. */
    gctUINT32                   chipDate;

#if gcdENABLE_VG
    /* Supported feature fields. */
    gctUINT32                   chipFeatures;

    /* Supported minor feature fields. */
    gctUINT32                   chipMinorFeatures;

    /* Supported minor feature 1 fields. */
    gctUINT32                   chipMinorFeatures1;

    /* Supported minor feature 2 fields. */
    gctUINT32                   chipMinorFeatures2;

    /* Supported minor feature 3 fields. */
    gctUINT32                   chipMinorFeatures3;

    /* Supported minor feature 4 fields. */
    gctUINT32                   chipMinorFeatures4;

    /* Supported minor feature 5 fields. */
    gctUINT32                   chipMinorFeatures5;

    /* Supported minor feature 6 fields. */
    gctUINT32                   chipMinorFeatures6;
#endif

    /* Number of streams supported. */
    gctUINT32                   streamCount;

    /* Number of pixel pipes. */
    gctUINT32                   pixelPipes;

    /* Number of resolve pipes. */
    gctUINT32                   resolvePipes;

    /* Number of instructions. */
    gctUINT32                   instructionCount;

    /* Number of constants. */
    gctUINT32                   numConstants;

    /* Number of varyings */
    gctUINT32                   varyingsCount;

    /* Number of 3D GPUs */
    gctUINT32                   gpuCoreCount;

    /* Product ID */
    gctUINT32                   productID;

    /* Special chip flag bits */
    gceCHIP_FLAG                chipFlags;

    /* ECO ID. */
    gctUINT32                   ecoID;

    /* Customer ID. */
    gctUINT32                   customerID;
}
gcsHAL_QUERY_CHIP_IDENTITY;

/* gcvHAL_COMPOSE. */
typedef struct _gcsHAL_COMPOSE * gcsHAL_COMPOSE_PTR;
typedef struct _gcsHAL_COMPOSE
{
    /* Composition state buffer. */
    gctUINT64                   physical;
    gctUINT64                   logical;
    gctUINT                     offset;
    gctUINT                     size;

    /* Composition end signal. */
    gctUINT64                   process;
    gctUINT64                   signal;

    /* User signals. */
    gctUINT64                   userProcess;
    gctUINT64                   userSignal1;
    gctUINT64                   userSignal2;

#if defined(__QNXNTO__)
    /* Client pulse side-channel connection ID. */
    gctINT32                    coid;

    /* Set by server. */
    gctINT32                    rcvid;
#endif
}
gcsHAL_COMPOSE;


typedef struct _gcsHAL_INTERFACE
{
    /* Command code. */
    gceHAL_COMMAND_CODES        command;

    /* Hardware type. */
    gceHARDWARE_TYPE            hardwareType;

    /* Core index for current hardware type. */
    gctUINT32                   coreIndex;

    /* Status value. */
    gceSTATUS                   status;

    /* Handle to this interface channel. */
    gctUINT64                   handle;

    /* Pid of the client. */
    gctUINT32                   pid;

    /* Engine */
    gceENGINE                   engine;

    /* Union of command structures. */
    union _u
    {
        /* gcvHAL_GET_BASE_ADDRESS */
        struct _gcsHAL_GET_BASE_ADDRESS
        {
            /* Physical memory address of internal memory. */
            OUT gctUINT32               baseAddress;

            /* Start of flat mapping range. */
            OUT gctUINT32               flatMappingStart;

            /* End of flat mapping range. */
            OUT gctUINT32               flatMappingEnd;
        }
        GetBaseAddress;

        /* gcvHAL_QUERY_VIDEO_MEMORY */
        struct _gcsHAL_QUERY_VIDEO_MEMORY
        {
            /* Physical memory address of internal memory. Just a name. */
            OUT gctUINT32               internalPhysical;

            /* Size in bytes of internal memory. */
            OUT gctUINT64               internalSize;

            /* Physical memory address of external memory. Just a name. */
            OUT gctUINT32               externalPhysical;

            /* Size in bytes of external memory.*/
            OUT gctUINT64               externalSize;

            /* Physical memory address of contiguous memory. Just a name. */
            OUT gctUINT32               contiguousPhysical;

            /* Size in bytes of contiguous memory.*/
            OUT gctUINT64               contiguousSize;
        }
        QueryVideoMemory;

        /* gcvHAL_QUERY_CHIP_IDENTITY */
        gcsHAL_QUERY_CHIP_IDENTITY      QueryChipIdentity;

        /* gcvHAL_MAP_MEMORY */
        struct _gcsHAL_MAP_MEMORY
        {
            /* Physical memory address to map. Just a name on Linux/Qnx. */
            IN gctUINT32                physical;

            /* Number of bytes in physical memory to map. */
            IN gctUINT64                bytes;

            /* Address of mapped memory. */
            OUT gctUINT64               logical;
        }
        MapMemory;

        /* gcvHAL_UNMAP_MEMORY */
        struct _gcsHAL_UNMAP_MEMORY
        {
            /* Physical memory address to unmap. Just a name on Linux/Qnx. */
            IN gctUINT32                physical;

            /* Number of bytes in physical memory to unmap. */
            IN gctUINT64                bytes;

            /* Address of mapped memory to unmap. */
            IN gctUINT64                logical;
        }
        UnmapMemory;

        /* gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY */
        struct _gcsHAL_ALLOCATE_LINEAR_VIDEO_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT gctUINT              bytes;

            /* Buffer alignment. */
            IN gctUINT                  alignment;

            /* Type of allocation. */
            IN gceSURF_TYPE             type;

            /* Flag of allocation. */
            IN gctUINT32                flag;

            /* Memory pool to allocate from. */
            IN OUT gcePOOL              pool;

            /* Allocated video memory. */
            OUT gctUINT32               node;
        }
        AllocateLinearVideoMemory;

        /* gcvHAL_ALLOCATE_VIDEO_MEMORY */
        struct _gcsHAL_ALLOCATE_VIDEO_MEMORY
        {
            /* Width of rectangle to allocate. */
            IN OUT gctUINT              width;

            /* Height of rectangle to allocate. */
            IN OUT gctUINT              height;

            /* Depth of rectangle to allocate. */
            IN gctUINT                  depth;

            /* Format rectangle to allocate in gceSURF_FORMAT. */
            IN gceSURF_FORMAT           format;

            /* Type of allocation. */
            IN gceSURF_TYPE             type;

            /* Memory pool to allocate from. */
            IN OUT gcePOOL              pool;

            /* Allocated video memory. */
            OUT gctUINT32               node;
        }
        AllocateVideoMemory;

        /* gcvHAL_RELEASE_VIDEO_MEMORY */
        struct _gcsHAL_RELEASE_VIDEO_MEMORY
        {
            /* Allocated video memory. */
            IN gctUINT32                node;

#ifdef __QNXNTO__
/* TODO: This is part of the unlock - why is it here? */
            /* Mapped logical address to unmap in user space. */
            OUT gctUINT64               memory;

            /* Number of bytes to allocated. */
            OUT gctUINT64               bytes;
#endif
        }
        ReleaseVideoMemory;

        /* gcvHAL_LOCK_VIDEO_MEMORY */
        struct _gcsHAL_LOCK_VIDEO_MEMORY
        {
            /* Allocated video memory. */
            IN gctUINT32                node;

            /* Cache configuration. */
            /* Only gcvPOOL_CONTIGUOUS and gcvPOOL_VIRUTAL
            ** can be configured */
            IN gctBOOL                  cacheable;

            /* Hardware specific address. */
            OUT gctUINT32               address;

            /* Mapped logical address. */
            OUT gctUINT64               memory;

            /* Customer priviate handle*/
            OUT gctUINT32               gid;

            /* Bus address of a contiguous video node. */
            OUT gctUINT64               physicalAddress;
        }
        LockVideoMemory;

        /* gcvHAL_UNLOCK_VIDEO_MEMORY */
        struct _gcsHAL_UNLOCK_VIDEO_MEMORY
        {
            /* Allocated video memory. */
            IN gctUINT64                node;

            /* Type of surface. */
            IN gceSURF_TYPE             type;

            /* Flag to unlock surface asynchroneously. */
            IN OUT gctBOOL              asynchroneous;
        }
        UnlockVideoMemory;

        /* gcvHAL_ALLOCATE_NON_PAGED_MEMORY */
        struct _gcsHAL_ALLOCATE_NON_PAGED_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT gctUINT64        bytes;

            /* Physical address of allocation. Just a name. */
            OUT gctUINT32           physical;

            /* Logical address of allocation. */
            OUT gctUINT64           logical;
        }
        AllocateNonPagedMemory;

        /* gcvHAL_FREE_NON_PAGED_MEMORY */
        struct _gcsHAL_FREE_NON_PAGED_MEMORY
        {
            /* Number of bytes allocated. */
            IN gctUINT64            bytes;

            /* Physical address of allocation. Just a name. */
            IN gctUINT32            physical;

            /* Logical address of allocation. */
            IN gctUINT64            logical;
        }
        FreeNonPagedMemory;

        /* gcvHAL_ALLOCATE_NON_PAGED_MEMORY */
        struct _gcsHAL_ALLOCATE_VIRTUAL_COMMAND_BUFFER
        {
            /* Number of bytes to allocate. */
            IN OUT gctUINT64        bytes;

            /* Physical address of allocation. Just a name. */
            OUT gctUINT32           physical;

            /* Logical address of allocation. */
            OUT gctUINT64           logical;
        }
        AllocateVirtualCommandBuffer;

        /* gcvHAL_FREE_NON_PAGED_MEMORY */
        struct _gcsHAL_FREE_VIRTUAL_COMMAND_BUFFER
        {
            /* Number of bytes allocated. */
            IN gctUINT64            bytes;

            /* Physical address of allocation. Just a name. */
            IN gctUINT32            physical;

            /* Logical address of allocation. */
            IN gctUINT64            logical;
        }
        FreeVirtualCommandBuffer;

        /* gcvHAL_EVENT_COMMIT. */
        struct _gcsHAL_EVENT_COMMIT
        {
            /* Event queue in gcsQUEUE. */
            IN gctUINT64            queue;

            IN gceENGINE            engine;
        }
        Event;

        /* gcvHAL_COMMIT */
        struct _gcsHAL_COMMIT
        {
            /* Context buffer object gckCONTEXT. */
            IN gctUINT64            context;

            /* Command buffer gcoCMDBUF. */
            IN gctUINT64            commandBuffer;

            /* State delta buffer in gcsSTATE_DELTA. */
            gctUINT64               delta;

            /* Event queue in gcsQUEUE. */
            IN gctUINT64            queue;

            /* Used to distinguish different FE. */
            IN gceENGINE            engine;

            /* The command buffer is linked to multiple command queue. */
            IN gctBOOL              shared;

            /* Index of command queue. */
            IN gctUINT32            index;
        }
        Commit;

        /* gcvHAL_MAP_USER_MEMORY */
        struct _gcsHAL_MAP_USER_MEMORY
        {
            /* Base address of user memory to map. */
            IN gctUINT64                memory;

            /* Physical address of user memory to map. */
            IN gctUINT32                physical;

            /* Size of user memory in bytes to map. */
            IN gctUINT64                size;

            /* Info record required by gcvHAL_UNMAP_USER_MEMORY. Just a name. */
            OUT gctUINT32               info;

            /* Physical address of mapped memory. */
            OUT gctUINT32               address;
        }
        MapUserMemory;

        /* gcvHAL_UNMAP_USER_MEMORY */
        struct _gcsHAL_UNMAP_USER_MEMORY
        {
            /* Base address of user memory to unmap. */
            IN gctUINT64                memory;

            /* Size of user memory in bytes to unmap. */
            IN gctUINT64                size;

            /* Info record returned by gcvHAL_MAP_USER_MEMORY. Just a name. */
            IN gctUINT32                info;

            /* Physical address of mapped memory as returned by
               gcvHAL_MAP_USER_MEMORY. */
            IN gctUINT32                address;
        }
        UnmapUserMemory;
#if !USE_NEW_LINUX_SIGNAL
        /* gcsHAL_USER_SIGNAL  */
        struct _gcsHAL_USER_SIGNAL
        {
            /* Command. */
            gceUSER_SIGNAL_COMMAND_CODES command;

            /* Signal ID. */
            IN OUT gctINT               id;

            /* Reset mode. */
            IN gctBOOL                  manualReset;

            /* Wait timedout. */
            IN gctUINT32                wait;

            /* State. */
            IN gctBOOL                  state;
        }
        UserSignal;
#endif

        /* gcvHAL_SIGNAL. */
        struct _gcsHAL_SIGNAL
        {
            /* Signal handle to signal gctSIGNAL. */
            IN gctUINT64                signal;

            /* Reserved gctSIGNAL. */
            IN gctUINT64                auxSignal;

            /* Process owning the signal gctHANDLE. */
            IN gctUINT64                process;

#if defined(__QNXNTO__)
            /* Client pulse side-channel connection ID. Set by client in gcoOS_CreateSignal. */
            IN gctINT32                 coid;

            /* Set by server. */
            IN gctINT32                 rcvid;
#endif
            /* Event generated from where of pipeline */
            IN gceKERNEL_WHERE          fromWhere;
        }
        Signal;

        /* gcvHAL_WRITE_DATA. */
        struct _gcsHAL_WRITE_DATA
        {
            /* Address to write data to. */
            IN gctUINT32                address;

            /* Data to write. */
            IN gctUINT32                data;
        }
        WriteData;

        /* gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY */
        struct _gcsHAL_ALLOCATE_CONTIGUOUS_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT gctUINT64            bytes;

            /* Hardware address of allocation. */
            OUT gctUINT32               address;

            /* Physical address of allocation. Just a name. */
            OUT gctUINT32               physical;

            /* Logical address of allocation. */
            OUT gctUINT64               logical;
        }
        AllocateContiguousMemory;

        /* gcvHAL_FREE_CONTIGUOUS_MEMORY */
        struct _gcsHAL_FREE_CONTIGUOUS_MEMORY
        {
            /* Number of bytes allocated. */
            IN gctUINT64                bytes;

            /* Physical address of allocation. Just a name. */
            IN gctUINT32                physical;

            /* Logical address of allocation. */
            IN gctUINT64                logical;
        }
        FreeContiguousMemory;

        /* gcvHAL_READ_REGISTER */
        struct _gcsHAL_READ_REGISTER
        {
            /* Logical address of memory to write data to. */
            IN gctUINT32            address;

            /* Data read. */
            OUT gctUINT32           data;
        }
        ReadRegisterData;

        /* gcvHAL_WRITE_REGISTER */
        struct _gcsHAL_WRITE_REGISTER
        {
            /* Logical address of memory to write data to. */
            IN gctUINT32            address;

            /* Data read. */
            IN gctUINT32            data;
        }
        WriteRegisterData;

        /* gcvHAL_READ_REGISTER_EX */
        struct _gcsHAL_READ_REGISTER_EX
        {
            /* Logical address of memory to write data to. */
            IN gctUINT32            address;

            IN gctUINT32            coreSelect;

            /* Data read. */
            OUT gctUINT32           data[4];
        }
        ReadRegisterDataEx;

        /* gcvHAL_WRITE_REGISTER_EX */
        struct _gcsHAL_WRITE_REGISTER_EX
        {
            /* Logical address of memory to write data to. */
            IN gctUINT32            address;

            IN gctUINT32            coreSelect;

            /* Data read. */
            IN gctUINT32            data[4];
        }
        WriteRegisterDataEx;

#if VIVANTE_PROFILER
        /* gcvHAL_GET_PROFILE_SETTING */
        struct _gcsHAL_GET_PROFILE_SETTING
        {
            /* Enable profiling */
            OUT gctBOOL             enable;
            OUT gctBOOL             syncMode;
        }
        GetProfileSetting;

        /* gcvHAL_SET_PROFILE_SETTING */
        struct _gcsHAL_SET_PROFILE_SETTING
        {
            /* Enable profiling */
            IN gctBOOL              enable;
            IN gctBOOL              syncMode;
        }
        SetProfileSetting;

#if VIVANTE_PROFILER_PERDRAW
        /* gcvHAL_READ_PROFILER_REGISTER_SETTING */
        struct _gcsHAL_READ_PROFILER_REGISTER_SETTING
         {
            /*Should Clear Register*/
            IN gctBOOL               bclear;
         }
        SetProfilerRegisterClear;
#endif

        /* gcvHAL_READ_ALL_PROFILE_REGISTERS */
        struct _gcsHAL_READ_ALL_PROFILE_REGISTERS
        {
#if VIVANTE_PROFILER_CONTEXT
            /* Context buffer object gckCONTEXT. Just a name. */
            IN gctUINT32                context;
#endif

            /* Data read. */
            OUT gcsPROFILER_COUNTERS    counters;
        }
        RegisterProfileData;

        /* gcvHAL_PROFILE_REGISTERS_2D */
        struct _gcsHAL_PROFILE_REGISTERS_2D
        {
            /* Data read in gcs2D_PROFILE. */
            OUT gctUINT64       hwProfile2D;
        }
        RegisterProfileData2D;
#endif

        /* Power management. */
        /* gcvHAL_SET_POWER_MANAGEMENT_STATE */
        struct _gcsHAL_SET_POWER_MANAGEMENT
        {
            /* Data read. */
            IN gceCHIPPOWERSTATE        state;
        }
        SetPowerManagement;

        /* gcvHAL_QUERY_POWER_MANAGEMENT_STATE */
        struct _gcsHAL_QUERY_POWER_MANAGEMENT
        {
            /* Data read. */
            OUT gceCHIPPOWERSTATE       state;

            /* Idle query. */
            OUT gctBOOL                 isIdle;
        }
        QueryPowerManagement;

        /* gcvHAL_QUERY_KERNEL_SETTINGS */
        struct _gcsHAL_QUERY_KERNEL_SETTINGS
        {
            /* Settings.*/
            OUT gcsKERNEL_SETTINGS      settings;
        }
        QueryKernelSettings;

        /* gcvHAL_MAP_PHYSICAL */
        struct _gcsHAL_MAP_PHYSICAL
        {
            /* gcvTRUE to map, gcvFALSE to unmap. */
            IN gctBOOL                  map;

            /* Physical address. */
            IN OUT gctUINT64            physical;
        }
        MapPhysical;

        /* gcvHAL_DEBUG */
        struct _gcsHAL_DEBUG
        {
            /* If gcvTRUE, set the debug information. */
            IN gctBOOL                  set;
            IN gctUINT32                level;
            IN gctUINT32                zones;
            IN gctBOOL                  enable;

            IN gceDEBUG_MESSAGE_TYPE    type;
            IN gctUINT32                messageSize;

            /* Message to print if not empty. */
            IN gctCHAR                  message[80];
        }
        Debug;

        /* gcvHAL_CACHE */
        struct _gcsHAL_CACHE
        {
            IN gceCACHEOPERATION        operation;
            IN gctUINT64                process;
            IN gctUINT64                logical;
            IN gctUINT64                bytes;
            IN gctUINT32                node;
        }
        Cache;

        /* gcvHAL_TIMESTAMP */
        struct _gcsHAL_TIMESTAMP
        {
            /* Timer select. */
            IN gctUINT32                timer;

            /* Timer request type (0-stop, 1-start, 2-send delta). */
            IN gctUINT32                request;

            /* Result of delta time in microseconds. */
            OUT gctINT32                timeDelta;
        }
        TimeStamp;

        /* gcvHAL_DATABASE */
        struct _gcsHAL_DATABASE
        {
            /* Set to gcvTRUE if you want to query a particular process ID.
            ** Set to gcvFALSE to query the last detached process. */
            IN gctBOOL                  validProcessID;

            /* Process ID to query. */
            IN gctUINT32                processID;

            /* Information. */
            OUT gcuDATABASE_INFO        vidMem;
            OUT gcuDATABASE_INFO        nonPaged;
            OUT gcuDATABASE_INFO        contiguous;
            OUT gcuDATABASE_INFO        gpuIdle;

            /* Detail information about video memory. */
            OUT gcuDATABASE_INFO        vidMemPool[3];
        }
        Database;

        /* gcvHAL_VERSION */
        struct _gcsHAL_VERSION
        {
            /* Major version: N.n.n. */
            OUT gctINT32                major;

            /* Minor version: n.N.n. */
            OUT gctINT32                minor;

            /* Patch version: n.n.N. */
            OUT gctINT32                patch;

            /* Build version. */
            OUT gctUINT32               build;
        }
        Version;

        /* gcvHAL_CHIP_INFO */
        struct _gcsHAL_CHIP_INFO
        {
            /* Chip count. */
            OUT gctINT32                count;

            /* Chip types. */
            OUT gceHARDWARE_TYPE        types[gcdCHIP_COUNT];

            /* Chip IDs. */
            OUT gctUINT32               ids[gcvCORE_COUNT];
        }
        ChipInfo;

        /* gcvHAL_ATTACH */
        struct _gcsHAL_ATTACH
        {
            /* Handle of context buffer object. */
            OUT gctUINT32               context;

            /* Maximum state in the buffer. */
            OUT gctUINT64               maxState;

            /* Number of states in the buffer. */
            OUT gctUINT32               numStates;

            /* Map context buffer to user or not. */
            IN gctBOOL                  map;

            /* Physical of context buffer. */
            OUT gctUINT32               physicals[2];

            /* Physical of context buffer. */
            OUT gctUINT64               logicals[2];

            /* Bytes of context buffer. */
            OUT gctUINT32               bytes;
        }
        Attach;

        /* gcvHAL_DETACH */
        struct _gcsHAL_DETACH
        {
            /* Context buffer object gckCONTEXT. Just a name. */
            IN gctUINT32                context;
        }
        Detach;

        /* gcvHAL_COMPOSE. */
        gcsHAL_COMPOSE            Compose;

        /* gcvHAL_GET_FRAME_INFO. */
        struct _gcsHAL_GET_FRAME_INFO
        {
            /* gcsHAL_FRAME_INFO* */
            OUT gctUINT64     frameInfo;
        }
        GetFrameInfo;

        /* gcvHAL_SET_TIME_OUT. */
        struct _gcsHAL_SET_TIMEOUT
        {
            gctUINT32                   timeOut;
        }
        SetTimeOut;

#if gcdENABLE_VG
        /* gcvHAL_COMMIT */
        struct _gcsHAL_VGCOMMIT
        {
            /* Context buffer. gcsVGCONTEXT_PTR */
            IN gctUINT64                context;

            /* Command queue. gcsVGCMDQUEUE_PTR */
            IN gctUINT64                queue;

            /* Number of entries in the queue. */
            IN gctUINT                  entryCount;

            /* Task table. gcsTASK_MASTER_TABLE_PTR */
            IN gctUINT64                taskTable;
        }
        VGCommit;

        /* gcvHAL_QUERY_COMMAND_BUFFER */
        struct _gcsHAL_QUERY_COMMAND_BUFFER
        {
            /* Command buffer attributes. */
            OUT gcsCOMMAND_BUFFER_INFO    information;
        }
        QueryCommandBuffer;

#endif

        struct _gcsHAL_SET_FSCALE_VALUE
        {
            IN gctUINT              value;
        }
        SetFscaleValue;

        struct _gcsHAL_GET_FSCALE_VALUE
        {
            OUT gctUINT             value;
            OUT gctUINT             minValue;
            OUT gctUINT             maxValue;
        }
        GetFscaleValue;

        struct _gcsHAL_NAME_VIDEO_MEMORY
        {
            IN gctUINT32            handle;
            OUT gctUINT32           name;
        }
        NameVideoMemory;

        struct _gcsHAL_IMPORT_VIDEO_MEMORY
        {
            IN gctUINT32            name;
            OUT gctUINT32           handle;
        }
        ImportVideoMemory;

        struct _gcsHAL_QUERY_RESET_TIME_STAMP
        {
            OUT gctUINT64           timeStamp;
            OUT gctUINT64           contextID;
        }
        QueryResetTimeStamp;

        struct _gcsHAL_SYNC_POINT
        {
            /* Command. */
            gceSYNC_POINT_COMMAND_CODES command;

            /* Sync point. */
            IN OUT gctUINT64            syncPoint;

            /* From where. */
            IN gceKERNEL_WHERE          fromWhere;

            /* Signaled state. */
            OUT gctBOOL                 state;
        }
        SyncPoint;

        struct _gcsHAL_CREATE_NATIVE_FENCE
        {
            /* Signal id to dup. */
            IN gctUINT64                syncPoint;

            /* Native fence file descriptor. */
            OUT gctINT                  fenceFD;

        }
        CreateNativeFence;

        struct _gcsHAL_WAIT_NATIVE_FENCE
        {
            /* Native fence file descriptor. */
            IN gctINT                   fenceFD;

            /* Wait timeout. */
            IN gctUINT32                timeout;
        }
        WaitNativeFence;

        struct _gcsHAL_DESTROY_MMU
        {
            /* Mmu object. */
            IN gctUINT64                mmu;
        }
        DestroyMmu;

        struct _gcsHAL_SHBUF
        {
            gceSHBUF_COMMAND_CODES      command;

            /* Shared buffer. */
            IN OUT gctUINT64            id;

            /* User data to be shared. */
            IN gctUINT64                data;

            /* Data size. */
            IN OUT gctUINT32            bytes;
        }
        ShBuf;


        struct _gcsHAL_GET_VIDEO_MEMORY_FD
        {
            IN gctUINT32            handle;
            OUT gctINT              fd;
        }
        GetVideoMemoryFd;

        struct _gcsHAL_CONFIG_POWER_MANAGEMENT
        {
            IN gctBOOL                  enable;
        }
        ConfigPowerManagement;

        struct _gcsHAL_WRAP_USER_MEMORY
        {
            /* Handle from other allocators. */
            IN gctUINT32                handle;

            /* Allocation flag to wrap user memory*/
            IN gctUINT32                flag;

            /* Video mmory node. */
            OUT gctUINT32               node;

            /* Description of user memory. */
            IN gcsUSER_MEMORY_DESC      desc;
        }
        WrapUserMemory;

        struct _gcsHAL_WAIT_FENCE
        {
            IN gctUINT32                handle;
            IN gctUINT32                timeOut;
        }
        WaitFence;

        struct _gcsHAL_COMMIT_DONE
        {
            IN gctUINT64                context;
        }
        CommitDone;

#if gcdENABLE_DEC_COMPRESSION && gcdDEC_ENABLE_AHB
        struct _gcsHAL_DEC300_READ
        {
            gctUINT32      enable;
            gctUINT32      readId;
            gctUINT32      format;
            gctUINT32      strides[3];
            gctUINT32      is3D;
            gctUINT32      isMSAA;
            gctUINT32      clearValue;
            gctUINT32      isTPC;
            gctUINT32      isTPCCompressed;
            gctUINT32      surfAddrs[3];
            gctUINT32      tileAddrs[3];
        }
        DEC300Read;

        struct _gcsHAL_DEC300_WRITE
        {
            gctUINT32       enable;
            gctUINT32       readId;
            gctUINT32       writeId;
            gctUINT32       format;
            gctUINT32       surfAddr;
            gctUINT32       tileAddr;
        }
        DEC300Write;

        struct _gcsHAL_DEC300_FLUSH
        {
            IN gctUINT8     useless;
        }
        DEC300Flush;

        struct _gcsHAL_DEC300_FLUSH_WAIT
        {
            IN gctUINT32    done;
        }
        DEC300FlushWait;
#endif
    }
    u;
}
gcsHAL_INTERFACE;


#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_driver_h_ */
