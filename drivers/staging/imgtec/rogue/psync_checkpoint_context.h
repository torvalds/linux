/* SPDX-License-Identifier: GPL-2.0 */

/*  --------------------------------------------------------------------------------------------------------
 *  File:   psync_checkpoint_context.h
 *  --------------------------------------------------------------------------------------------------------
 */


#ifndef __PSYNC_CHECKPOINT_CONTEXT_H__
#define __PSYNC_CHECKPOINT_CONTEXT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------
 *  Include Files
 * ---------------------------------------------------------------------------------------------------------
 */
#include <linux/kernel.h>



/* ---------------------------------------------------------------------------------------------------------
 *  Macros Definition
 * ---------------------------------------------------------------------------------------------------------
 */

#define SYNC_CHECKPOINT_MAX_CLASS_NAME_LEN 32

#define SYNC_CHECKPOINT_NAME_SIZE	SYNC_CHECKPOINT_MAX_CLASS_NAME_LEN

/* ---------------------------------------------------------------------------------------------------------
 *  Types and Structures Definition
 * ---------------------------------------------------------------------------------------------------------
 */

typedef struct _SYNC_CHECKPOINT_CONTEXT_ {
	PVRSRV_DEVICE_NODE     *psDevNode;
	IMG_CHAR               azName[SYNC_CHECKPOINT_NAME_SIZE];     /*!< Name of the RA */
	RA_ARENA               *psSubAllocRA;                         /*!< RA context */
	IMG_CHAR               azSpanName[SYNC_CHECKPOINT_NAME_SIZE]; /*!< Name of the span RA */
	RA_ARENA               *psSpanRA;                             /*!< RA used for span management of SubAllocRA */
	ATOMIC_T               hRefCount;                             /*!< Ref count for this context */
	ATOMIC_T               hCheckpointCount;                      /*!< Checkpoint count for this context */
	POS_LOCK               hCheckpointListLock;                   /*!< Checkpoint list lock */
	DLLIST_NODE            sCheckpointList;                       /*!< List of checkpoints created on this context */
	IMG_HANDLE             hCheckpointNotify;                     /*!< Handle for debug notifier callback */
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	POS_LOCK               hCheckpointRecordLock;
	DLLIST_NODE            sCheckpointRecordList;
	struct SYNC_CHECKPOINT_RECORD *apsCheckpointRecordsFreed[PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN];
	IMG_UINT32             uiCheckpointRecordFreeIdx;
	IMG_HANDLE             hCheckpointRecordNotify;
#endif
} _SYNC_CHECKPOINT_CONTEXT;
typedef struct _SYNC_CHECKPOINT_CONTEXT *PSYNC_CHECKPOINT_CONTEXT;


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

#endif /* __PSYNC_CHECKPOINT_CONTEXT_H__ */

