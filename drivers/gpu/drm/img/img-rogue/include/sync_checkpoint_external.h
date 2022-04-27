/*************************************************************************/ /*!
@File
@Title          Services external synchronisation checkpoint interface header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines synchronisation checkpoint structures that are visible
				internally and externally
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

#ifndef SYNC_CHECKPOINT_EXTERNAL_H
#define SYNC_CHECKPOINT_EXTERNAL_H

#include "img_types.h"

#ifndef CHECKPOINT_TYPES
#define CHECKPOINT_TYPES
typedef struct _SYNC_CHECKPOINT_CONTEXT *PSYNC_CHECKPOINT_CONTEXT;

typedef struct _SYNC_CHECKPOINT *PSYNC_CHECKPOINT;
#endif

/* PVRSRV_SYNC_CHECKPOINT states.
 * The OS native sync implementation should call pfnIsSignalled() to determine if a
 * PVRSRV_SYNC_CHECKPOINT has signalled (which will return an IMG_BOOL), but can set the
 * state for a PVRSRV_SYNC_CHECKPOINT (which is currently in the NOT_SIGNALLED state)
 * where that PVRSRV_SYNC_CHECKPOINT is representing a foreign sync.
 */
typedef IMG_UINT32 PVRSRV_SYNC_CHECKPOINT_STATE;

#define PVRSRV_SYNC_CHECKPOINT_UNDEF         0x000U
#define PVRSRV_SYNC_CHECKPOINT_ACTIVE        0xac1U  /*!< checkpoint has not signalled */
#define PVRSRV_SYNC_CHECKPOINT_SIGNALLED     0x519U  /*!< checkpoint has signalled */
#define PVRSRV_SYNC_CHECKPOINT_ERRORED       0xeffU   /*!< checkpoint has been errored */


#define PVRSRV_UFO_IS_SYNC_CHECKPOINT_FWADDR(fwaddr)	((fwaddr) & 0x1U)
#define PVRSRV_UFO_IS_SYNC_CHECKPOINT(ufoptr)			(PVRSRV_UFO_IS_SYNC_CHECKPOINT_FWADDR((ufoptr)->puiAddrUFO.ui32Addr))

/* Maximum number of sync checkpoints the firmware supports in one fence */
#define MAX_SYNC_CHECKPOINTS_PER_FENCE 32U

/*!
 * Define to be used with SyncCheckpointAlloc() to indicate a checkpoint which
 * represents a foreign sync point or collection of foreign sync points.
 */
#define SYNC_CHECKPOINT_FOREIGN_CHECKPOINT ((PVRSRV_TIMELINE) - 2U)

#endif /* SYNC_CHECKPOINT_EXTERNAL_H */
