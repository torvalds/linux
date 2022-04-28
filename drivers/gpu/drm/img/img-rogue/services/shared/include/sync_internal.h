/*************************************************************************/ /*!
@File
@Title          Services internal synchronisation interface header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines the internal client side interface for services
                synchronisation
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

#ifndef SYNC_INTERNAL
#define SYNC_INTERNAL

#include "img_types.h"
#include "img_defs.h"
#include "ra.h"
#include "dllist.h"
#include "lock.h"
#include "devicemem.h"
#include "sync_prim_internal.h"

#define LOCAL_SYNC_PRIM_RESET_VALUE 0
#define LOCAL_SYNC_PRIM_POISON_VALUE 0xa5a5a5a5u

/*
	Debug feature to protect against GP DM page faults when
	sync prims are freed by client before work is completed.
*/
#define LOCAL_SYNC_BLOCK_RETAIN_FIRST

/*
	Private structure's
*/
#define SYNC_PRIM_NAME_SIZE		50
typedef struct SYNC_PRIM_CONTEXT
{
	SHARED_DEV_CONNECTION       hDevConnection;
	IMG_CHAR					azName[SYNC_PRIM_NAME_SIZE];	/*!< Name of the RA */
	RA_ARENA					*psSubAllocRA;					/*!< RA context */
	IMG_CHAR					azSpanName[SYNC_PRIM_NAME_SIZE];/*!< Name of the span RA */
	RA_ARENA					*psSpanRA;						/*!< RA used for span management of SubAllocRA */
	ATOMIC_T				hRefCount;	/*!< Ref count for this context */
#if defined(LOCAL_SYNC_BLOCK_RETAIN_FIRST)
	IMG_HANDLE					hFirstSyncPrim; /*!< Handle to the first allocated sync prim */
#endif
} SYNC_PRIM_CONTEXT;

typedef struct SYNC_PRIM_BLOCK_TAG
{
	SYNC_PRIM_CONTEXT	*psContext;				/*!< Our copy of the services connection */
	IMG_HANDLE			hServerSyncPrimBlock;	/*!< Server handle for this block */
	IMG_UINT32			ui32SyncBlockSize;		/*!< Size of the sync prim block */
	IMG_UINT32			ui32FirmwareAddr;		/*!< Firmware address */
	DEVMEM_MEMDESC		*hMemDesc;				/*!< Host mapping handle */
	IMG_UINT32 __iomem	*pui32LinAddr;			/*!< User CPU mapping */
	IMG_UINT64			uiSpanBase;				/*!< Base of this import in the span RA */
	DLLIST_NODE			sListNode;				/*!< List node for the sync block list */
} SYNC_PRIM_BLOCK;

typedef enum SYNC_PRIM_TYPE_TAG
{
	SYNC_PRIM_TYPE_UNKNOWN = 0,
	SYNC_PRIM_TYPE_LOCAL,
	SYNC_PRIM_TYPE_SERVER,
} SYNC_PRIM_TYPE;

typedef struct SYNC_PRIM_LOCAL_TAG
{
	ATOMIC_T				hRefCount;	/*!< Ref count for this sync */
	SYNC_PRIM_BLOCK			*psSyncBlock;	/*!< Synchronisation block this primitive is allocated on */
	IMG_UINT64				uiSpanAddr;		/*!< Span address of the sync */
	IMG_HANDLE				hRecord;		/*!< Sync record handle */
} SYNC_PRIM_LOCAL;

typedef struct SYNC_PRIM_TAG
{
	PVRSRV_CLIENT_SYNC_PRIM	sCommon;		/*!< Client visible part of the sync prim */
	SYNC_PRIM_TYPE			eType;			/*!< Sync primitive type */
	union {
		SYNC_PRIM_LOCAL		sLocal;			/*!< Local sync primitive data */
	} u;
} SYNC_PRIM;


IMG_INTERNAL PVRSRV_ERROR
SyncPrimGetFirmwareAddr(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 *pui32FwAddr);

IMG_INTERNAL PVRSRV_ERROR SyncPrimLocalGetHandleAndOffset(PVRSRV_CLIENT_SYNC_PRIM *psSync,
							IMG_HANDLE *phBlock,
							IMG_UINT32 *pui32Offset);


#endif	/* SYNC_INTERNAL */
