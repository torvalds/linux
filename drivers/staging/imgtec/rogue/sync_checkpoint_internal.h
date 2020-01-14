/*************************************************************************/ /*!
@File
@Title          Services internal synchronisation checkpoint interface header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines the internal server interface for services
                synchronisation checkpoints.
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

#ifndef __SYNC_CHECKPOINT__
#define __SYNC_CHECKPOINT__

#include "pvrsrv_device_node.h"
#include "psync_checkpoint.h"
#include "psync_checkpoint_context.h"
#include "img_types.h"
#include "sync_checkpoint_internal_fw.h"
#include "sync_checkpoint.h"
#include "ra.h"
#include "dllist.h"
#include "lock.h"
#include "devicemem.h"

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
struct SYNC_CHECKPOINT_RECORD;
#endif

/*
	Private structures
*/

typedef struct SYNC_CHECKPOINT_RECORD* PSYNC_CHECKPOINT_RECORD_HANDLE;

/*************************************************************************/ /*!
@Function       SyncCheckpointGetFirmwareAddr

@Description    .

@Input          psSyncCheckpoint        Synchronisation checkpoint to get
                                        the firmware address of

@Return         None

*/
/*****************************************************************************/
IMG_UINT32
SyncCheckpointGetFirmwareAddr(PSYNC_CHECKPOINT psSyncCheckpoint);

/*************************************************************************/ /*!
@Function       SyncCheckpointCCBEnqueued

@Description    Increment the CCB enqueued reference count for a
                synchronisation checkpoint. This indicates how many FW
                operations (checks/update) have been placed into CCBs for the
                sync checkpoint.
                When the FW services these operation, it increments its own
                reference count. When these two values are equal, we know
                there are not outstanding FW operating for the checkpoint
                in any CCB.

@Input          psSyncCheckpoint        Synchronisation checkpoint for which
                                        to increment the enqueued reference
                                        count

@Return         None

*/
/*****************************************************************************/
void
SyncCheckpointCCBEnqueued(PSYNC_CHECKPOINT psSyncCheckpoint);

#endif	/* __SYNC_CHECKPOINT__ */
