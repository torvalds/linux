/*************************************************************************/ /*!
@File
@Title          Synchronisation checkpoint interface header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines the client side interface for synchronisation
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef _SYNC_CHECKPOINT_
#define _SYNC_CHECKPOINT_

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_sync_km.h"
#include "pdumpdefs.h"
#include "dllist.h"
#include "pvr_debug.h"

#include "device_connection.h"

typedef struct _PVRSRV_DEVICE_NODE_ PVRSRV_DEVICE_NODE;

typedef struct _SYNC_CHECKPOINT_CONTEXT *PSYNC_CHECKPOINT_CONTEXT;

typedef struct _SYNC_CHECKPOINT *PSYNC_CHECKPOINT;


/*************************************************************************/ /*!
@Function       SyncCheckpointContextCreate

@Description    Create a new synchronisation checkpoint context

@Input          psDevNode                 Device node

@Output         ppsSyncCheckpointContext  Handle to the created synchronisation
                                          checkpoint context

@Return         PVRSRV_OK if the synchronisation checkpoint context was
                successfully created
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncCheckpointContextCreate(PVRSRV_DEVICE_NODE *psDevNode,
                            PSYNC_CHECKPOINT_CONTEXT *ppsSyncCheckpointContext);

/*************************************************************************/ /*!
@Function       SyncCheckpointContextDestroy

@Description    Destroy a synchronisation checkpoint context

@Input          psSyncCheckpointContext  Handle to the synchronisation
                                         checkpoint context to destroy

@Return         PVRSRV_OK if the synchronisation checkpoint context was
                successfully destroyed.
                PVRSRV_ERROR_UNABLE_TO_DESTROY_CONTEXT if the context still
                has sync checkpoints defined
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncCheckpointContextDestroy(PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext);

/*************************************************************************/ /*!
@Function       SyncCheckpointAlloc

@Description    Allocate a new synchronisation checkpoint on the specified
                synchronisation checkpoint context

@Input          hSyncCheckpointContext  Handle to the synchronisation
                                        checkpoint context

@Input          pszClassName            Sync checkpoint source annotation
                                        (will be truncated to at most
                                         SYNC_CHECKPOINT_NAME_SIZE chars)

@Output         ppsSyncCheckpoint       Created synchronisation checkpoint

@Return         PVRSRV_OK if the synchronisation checkpoint was
                successfully created
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncCheckpointAlloc(PSYNC_CHECKPOINT_CONTEXT psSyncContext,
                    const IMG_CHAR *pszCheckpointName,
                    PSYNC_CHECKPOINT *ppsSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointFree

@Description    Free a synchronization checkpoint
                The reference count held for the synchronization checkpoint
                is decremented - if it has becomes zero, it is also freed.

@Input          psSyncCheckpoint        The synchronisation checkpoint to free

@Return         None
*/
/*****************************************************************************/
void
SyncCheckpointFree(PSYNC_CHECKPOINT psSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointSignal

@Description    Signal the synchronisation checkpoint

@Input          psSyncCheckpoint        The synchronisation checkpoint to signal

@Return         None
*/
/*****************************************************************************/
void
SyncCheckpointSignal(PSYNC_CHECKPOINT psSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointError

@Description    Error the synchronisation checkpoint

@Input          psSyncCheckpoint        The synchronisation checkpoint to error

@Return         None
*/
/*****************************************************************************/
void
SyncCheckpointError(PSYNC_CHECKPOINT psSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointErrorFromUFO

@Description    Error the synchronisation checkpoint which has the
                given UFO firmware address

@Input          hSyncCheckpointContext  Handle to the synchronisation checkpoint
                                        context to which the checkpoint belongs

@Input          ui32FwAddr              The firmware address of the sync
                                        checkpoint to be errored

@Return         None
*/
/*****************************************************************************/
void
SyncCheckpointErrorFromUFO(PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext,
						   IMG_UINT32 ui32FwAddr);

/*************************************************************************/ /*!
@Function       SyncCheckpointIsSignalled

@Description    Returns IMG_TRUE if the synchronisation checkpoint is
                signalled or errored

@Input          psSyncCheckpoint        The synchronisation checkpoint to test

@Return         None
*/
/*****************************************************************************/
IMG_BOOL
SyncCheckpointIsSignalled(PSYNC_CHECKPOINT psSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointIsErrored

@Description    Returns IMG_TRUE if the synchronisation checkpoint is
                errored

@Input          psSyncCheckpoint        The synchronisation checkpoint to test

@Return         None
*/
/*****************************************************************************/
IMG_BOOL
SyncCheckpointIsErrored(PSYNC_CHECKPOINT psSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointTakeRef

@Description    Take a reference on a synchronisation checkpoint

@Input          psSyncCheckpoint        Synchronisation checkpoint to take a
                                        reference on

@Return         PVRSRV_OK if a reference was taken on the synchronisation
                primitive
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncCheckpointTakeRef(PSYNC_CHECKPOINT psSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointDropRef

@Description    Drop a reference on a synchronisation checkpoint

@Input          psSyncCheckpoint        Synchronisation checkpoint to drop a
                                        reference on

@Return         PVRSRV_OK if a reference was dropped on the synchronisation
                primitive
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncCheckpointDropRef(PSYNC_CHECKPOINT psSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointResolveFence

@Description    Resolve a fence, returning a list of the sync checkpoints
                that fence contains.
                This function in turn calls a function provided by the
                OS native sync implementation.

@Input          hFence                  The fence to be resolved

@Output         pui32NumSyncCheckpoints The number of sync checkpoints the
                                        fence contains. Can return 0 if
                                        passed a null (-1) fence.

@Output         psSyncCheckpoints       List of sync checkpoints the fence
                                        contains

@Return         PVRSRV_OK if a valid fence was provided.
                PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED if the OS native
                sync has not registered a callback function.
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncCheckpointResolveFence(PVRSRV_FENCE_KM hFence, IMG_UINT32 *pui32NumSyncCheckpoints, PSYNC_CHECKPOINT *psSyncCheckpoints);

/*************************************************************************/ /*!
@Function       SyncCheckpointCreateFence

@Description    Create a fence containing a single sync checkpoint.
                Return the fence and a ptr to sync checkpoint it contains.
                This function in turn calls a function provided by the
                OS native sync implementation.

@Input          pszFenceName            String to assign to the new fence
                                        (for debugging purposes)

@Input          hTimeline               Timeline on which the new fence is
                                        to be created

@Output         phNewFence              The newly created fence

@Output         psNewSyncCheckpoint     The sync checkpoint contained in
                                        the new fence

@Return         PVRSRV_OK if a valid fence was provided.
                PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED if the OS native
                sync has not registered a callback function.
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncCheckpointCreateFence(const IMG_CHAR *pszFenceName, PVRSRV_TIMELINE_KM hTimeline, PVRSRV_FENCE_KM *phNewFence, PSYNC_CHECKPOINT *psNewSyncCheckpoint);

#endif	/* _SYNC_CHECKPOINT_ */

