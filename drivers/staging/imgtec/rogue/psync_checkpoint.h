/* SPDX-License-Identifier: GPL-2.0 */

/*  --------------------------------------------------------------------------------------------------------
 *  File:   psync_checkpoint.h
 *  --------------------------------------------------------------------------------------------------------
 */


#ifndef __PSYNC_CHECKPOINT_H__
#define __PSYNC_CHECKPOINT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------
 *  Include Files
 * ---------------------------------------------------------------------------------------------------------
 */
#include "psync_checkpoint_context.h"
#include "sync_checkpoint_internal_fw.h"

/* ---------------------------------------------------------------------------------------------------------
 *  Macros Definition
 * ---------------------------------------------------------------------------------------------------------
 */


/* ---------------------------------------------------------------------------------------------------------
 *  Types and Structures Definition
 * ---------------------------------------------------------------------------------------------------------
 */

typedef struct _SYNC_CHECKPOINT_BLOCK_ {
	ATOMIC_T                  hRefCount;                  /*!< Ref count for this sync block */
	_SYNC_CHECKPOINT_CONTEXT  *psContext;                 /*!< Our copy of the services connection */
	PVRSRV_DEVICE_NODE        *psDevNode;
	IMG_UINT32                ui32SyncBlockSize;          /*!< Size of the sync checkpoint block */
	IMG_UINT32                ui32FirmwareAddr;           /*!< Firmware address */
	DEVMEM_MEMDESC            *hMemDesc;                  /*!< DevMem allocation for block */
	volatile IMG_UINT32       *pui32LinAddr;              /*!< Server-code CPU mapping */
	IMG_UINT64                uiSpanBase;                 /*!< Base of this import (FW DevMem) in the span RA */
	DLLIST_NODE               sListNode;                  /*!< List node for the sync chkpt block list */
} SYNC_CHECKPOINT_BLOCK;

// .CP :
typedef struct _SYNC_CHECKPOINT_ {
	/* A sync checkpoint is assigned a unique ID, to avoid any confusion should
	 * the same memory be re-used later for a different checkpoint
	 */
	IMG_UINT32                      ui32UID;                /*!< Unique ID assigned to sync checkpoint (to distinguish checkpoints if memory is re-used)*/
	ATOMIC_T                        hRefCount;              /*!< Ref count for this sync */
	ATOMIC_T                        hEnqueuedCCBCount;      /*!< Num times sync has been put in CCBs */
	SYNC_CHECKPOINT_BLOCK           *psSyncCheckpointBlock; /*!< Synchronisation block this checkpoint is allocated on */
	IMG_UINT64                      uiSpanAddr;             /*!< Span address of the sync */
	volatile _SYNC_CHECKPOINT_FW_OBJ *psSyncCheckpointFwObj; /*!< CPU view of the data held in the sync block */
	IMG_CHAR                        azName[SYNC_CHECKPOINT_NAME_SIZE]; /*!< Name of the checkpoint */
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	PSYNC_CHECKPOINT_RECORD_HANDLE  hRecord;                /*!< Sync record handle */
#endif
	DLLIST_NODE                     sListNode;              /*!< List node for the sync chkpt list */
} _SYNC_CHECKPOINT;
typedef struct _SYNC_CHECKPOINT *PSYNC_CHECKPOINT;

/* ---------------------------------------------------------------------------------------------------------
 *  Global Functions' Prototype
 * ---------------------------------------------------------------------------------------------------------
 */


/* ---------------------------------------------------------------------------------------------------------
 *  Inline Functions Implementation
 * ---------------------------------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* __PSYNC_CHECKPOINT_H__ */

