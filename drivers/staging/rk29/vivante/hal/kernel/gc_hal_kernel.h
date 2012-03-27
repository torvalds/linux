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




#ifndef __gc_hal_kernel_h_
#define __gc_hal_kernel_h_

#include "gc_hal.h"
#include "gc_hal_kernel_hardware.h"
#include "gc_hal_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
********************************** Structures **********************************
\******************************************************************************/

#if gcdSECURE_USER
typedef struct _gckLOGICAL_CACHE
{
    gctHANDLE       process;
    gctPOINTER      logical;
    gctUINT32       dma;
    gctUINT64       stamp;
}
gckLOGICAL_CACHE;
#endif

/* gckKERNEL object. */
struct _gckKERNEL
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Pointer to gckHARDWARE object. */
    gckHARDWARE                 hardware;

    /* Pointer to gckCOMMAND object. */
    gckCOMMAND                  command;

    /* Pointer to gckEVENT object. */
    gckEVENT                    event;

    /* Pointer to context. */
    gctPOINTER                  context;

    /* Pointer to gckMMU object. */
    gckMMU                      mmu;

    /* Arom holding number of clients. */
    gctPOINTER                  atomClients;

#if VIVANTE_PROFILER
    /* Enable profiling */
    gctBOOL                     profileEnable;

    /* The profile file name */
    gctCHAR                     profileFileName[gcdMAX_PROFILE_FILE_NAME];
#endif

#if gcdSECURE_USER
    gckLOGICAL_CACHE            cache[gcdSECURE_CACHE_SLOTS];
    gctUINT                     cacheSlots;
    gctUINT64                   cacheTimeStamp;
#endif
};

#define gcdCOMMAND_QUEUES       2

/* gckCOMMAND object. */
struct _gckCOMMAND
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to required object. */
    gckKERNEL                   kernel;
    gckOS                       os;

    /* Number of bytes per page. */
    gctSIZE_T                   pageSize;

    /* Current pipe select. */
    gctUINT32                   pipeSelect;

    /* Command queue running flag. */
    gctBOOL                     running;

    /* Idle flag and commit stamp. */
    gctBOOL                     idle;
    gctUINT64                   commitStamp;

    /* Command queue mutex. */
    gctPOINTER                  mutexQueue;

    /* Context switching mutex. */
    gctPOINTER                  mutexContext;

    /* Command queue power semaphore. */
    gctPOINTER                  powerSemaphore;

    /* Current command queue. */
    struct _gcskCOMMAND_QUEUE
    {
        gctSIGNAL               signal;
        gctPHYS_ADDR            physical;
        gctPOINTER              logical;
    }
    queues[gcdCOMMAND_QUEUES];

    gctPHYS_ADDR                physical;
    gctPOINTER                  logical;
    gctINT                      index;
    gctUINT32                   offset;

    /* The command queue is new. */
    gctBOOL                     newQueue;
    gctBOOL                     submit;

    /* Context counter used for unique ID. */
    gctUINT64                   contextCounter;

    /* Current context ID. */
    gctUINT64                   currentContext;

    /* Pointer to last WAIT command. */
    gctPOINTER                  wait;
    gctSIZE_T                   waitSize;

    /* Command buffer alignment. */
    gctSIZE_T                   alignment;
    gctSIZE_T                   reservedHead;
    gctSIZE_T                   reservedTail;

    /* Commit counter. */
    gctPOINTER                  atomCommit;
};

typedef struct _gcsEVENT *      gcsEVENT_PTR;

/* Structure holding one event to be processed. */
typedef struct _gcsEVENT
{
    /* Pointer to next event in queue. */
    gcsEVENT_PTR                next;

    /* Event information. */
    gcsHAL_INTERFACE            event;

#ifdef __QNXNTO__
    /* Kernel. */
    gckKERNEL                   kernel;
#endif
}
gcsEVENT;

/* Structure holding a list of events to be processed by an interrupt. */
typedef struct _gcsEVENT_QUEUE
{
    /* Time stamp. */
    gctUINT64                   stamp;

    /* Source of the event. */
    gceKERNEL_WHERE             source;

    /* Pointer to head of event queue. */
    gcsEVENT_PTR                head;

    /* Pointer to tail of event queue. */
    gcsEVENT_PTR                tail;

    /* Process ID owning the event queue. */
    gctUINT32                   processID;
}
gcsEVENT_QUEUE;

/* gckEVENT object. */
struct _gckEVENT
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to required objects. */
    gckOS                       os;
    gckKERNEL                   kernel;

    /* Time stamp. */
    gctUINT64                   stamp;
    gctUINT64                   lastCommitStamp;

    /* Queue mutex. */
    gctPOINTER                  mutexQueue;

    /* Array of event queues. */
    gcsEVENT_QUEUE              queues[31];
    gctUINT8                    lastID;

    /* Pending events. */
    volatile gctUINT            pending;

    /* List of free event structures and its mutex. */
    gcsEVENT_PTR                freeList;
    gctSIZE_T                   freeCount;
    gctPOINTER                  freeMutex;

    /* Events queued to be added to an event queue and its mutex. */
    gcsEVENT_QUEUE              list;
    gctPOINTER                  listMutex;
};

/* gcuVIDMEM_NODE structure. */
typedef union _gcuVIDMEM_NODE
{
    /* Allocated from gckVIDMEM. */
    struct _gcsVIDMEM_NODE_VIDMEM
    {
        /* Owner of this node. */
        gckVIDMEM               memory;

        /* Dual-linked list of nodes. */
        gcuVIDMEM_NODE_PTR      next;
        gcuVIDMEM_NODE_PTR      prev;

        /* Dual linked list of free nodes. */
        gcuVIDMEM_NODE_PTR      nextFree;
        gcuVIDMEM_NODE_PTR      prevFree;

        /* Information for this node. */
        gctUINT32               offset;
        gctSIZE_T               bytes;
        gctUINT32               alignment;

#ifdef __QNXNTO__
        /* Client/server vaddr (mapped using mmap_join). */
        gctPOINTER              logical;

        /* Unique handle of the caller process channel. */
        gctHANDLE               handle;
#endif

        /* Locked counter. */
        gctINT32                locked;

        /* Memory pool. */
        gcePOOL                 pool;
        gctUINT32               physical;
    }
    VidMem;

    /* Allocated from gckOS. */
    struct _gcsVIDMEM_NODE_VIRTUAL
    {
        /* Pointer to gckKERNEL object. */
        gckKERNEL               kernel;

        /* Information for this node. */
        gctBOOL                 contiguous;
        gctPHYS_ADDR            physical;
        gctSIZE_T               bytes;
        gctPOINTER              logical;

        /* Page table information. */
        gctSIZE_T               pageCount;
        gctPOINTER              pageTable;
        gctUINT32               address;

        /* Mutex. */
        gctPOINTER              mutex;

        /* Locked counter. */
        gctINT32                locked;

#ifdef __QNXNTO__
        /* Single linked list of nodes. */
        gcuVIDMEM_NODE_PTR      next;

        /* PID of the caller process channel. */
        gctUINT32               userPID;

        /* Unique handle of the caller process channel. */
        gctHANDLE               handle;

        /* Unlock pending flag. */
        gctBOOL                 unlockPending;

        /* Free pending flag. */
        gctBOOL                 freePending;
#else
        /* Pending flag. */
        gctBOOL                 pending;
#endif
    }
    Virtual;
}
gcuVIDMEM_NODE;

/* gckVIDMEM object. */
struct _gckVIDMEM
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Information for this video memory heap. */
    gctUINT32                   baseAddress;
    gctSIZE_T                   bytes;
    gctSIZE_T                   freeBytes;

    /* Mapping for each type of surface. */
    gctINT                      mapping[gcvSURF_NUM_TYPES];

    /* Sentinel nodes for up to 8 banks. */
    gcuVIDMEM_NODE              sentinel[8];

    /* Allocation threshold. */
    gctSIZE_T                   threshold;

    /* The heap mutex. */
    gctPOINTER                  mutex;

#if gcdTILESTATUS_SINGLE_BANK
    gctINT                      tilestatusBank;
#endif
};

/* gckMMU object. */
struct _gckMMU
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Pointer to gckHARDWARE object. */
    gckHARDWARE                 hardware;

    /* The page table mutex. */
    gctPOINTER                  pageTableMutex;

    /* Page table information. */
    gctSIZE_T                   pageTableSize;
    gctPHYS_ADDR                pageTablePhysical;
    gctUINT32_PTR               pageTableLogical;
    gctUINT32                   pageTableEntries;

#if gcdENABLE_MMU_PROTECTING
	gctPHYS_ADDR                FreePagePhysical;
	gctUINT32_PTR				FreePageLogical;
#endif

    /* Free entries. */
    gctUINT32                   heapList;
    gctBOOL                     freeNodes;

#ifdef __QNXNTO__
    /* Single linked list of all allocated nodes. */
    gctPOINTER                  nodeMutex;
    gcuVIDMEM_NODE_PTR          nodeList;
#endif
};

gceSTATUS
gckKERNEL_AttachProcess(
    IN gckKERNEL Kernel,
    IN gctBOOL Attach
    );

#if gcdSECURE_USER
gceSTATUS
gckKERNEL_MapLogicalToPhysical(
    IN gckKERNEL Kernel,
    IN gctHANDLE Process,
    IN OUT gctPOINTER * Data
    );
#endif

gceSTATUS
gckHARDWARE_QueryIdle(
    IN gckHARDWARE Hardware,
    OUT gctBOOL_PTR IsIdle
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_kernel_h_ */
