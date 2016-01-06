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

#ifndef _SYNC_INTERNAL_
#define _SYNC_INTERNAL_

#include "img_types.h"
#include "sync_external.h"
#include "ra.h"
#include "dllist.h"
#include "lock.h"
#include "devicemem.h"

/*
	Private structure's
*/
#define SYNC_PRIM_NAME_SIZE		50
typedef struct SYNC_PRIM_CONTEXT
{
	SYNC_BRIDGE_HANDLE			hBridge;						/*!< Bridge handle */
	IMG_HANDLE					hDeviceNode;					/*!< The device we're operating on */
	IMG_CHAR					azName[SYNC_PRIM_NAME_SIZE];	/*!< Name of the RA */
	RA_ARENA					*psSubAllocRA;					/*!< RA context */
	IMG_CHAR					azSpanName[SYNC_PRIM_NAME_SIZE];/*!< Name of the span RA */
	RA_ARENA					*psSpanRA;						/*!< RA used for span management of SubAllocRA */
	ATOMIC_T				hRefCount;	/*!< Ref count for this context */
} SYNC_PRIM_CONTEXT;

typedef struct _SYNC_PRIM_BLOCK_
{
	SYNC_PRIM_CONTEXT	*psContext;				/*!< Our copy of the services connection */
	IMG_HANDLE			hServerSyncPrimBlock;	/*!< Server handle for this block */
	IMG_UINT32			ui32SyncBlockSize;		/*!< Size of the sync prim block */
	IMG_UINT32			ui32FirmwareAddr;		/*!< Firmware address */
	DEVMEM_MEMDESC		*hMemDesc;				/*!< Host mapping handle */
	IMG_UINT32			*pui32LinAddr;			/*!< User CPU mapping */
	IMG_UINT64			uiSpanBase;				/*!< Base of this import in the span RA */
	DLLIST_NODE			sListNode;				/*!< List node for the sync block list */
} SYNC_PRIM_BLOCK;

typedef enum _SYNC_PRIM_TYPE_
{
	SYNC_PRIM_TYPE_UNKNOWN = 0,
	SYNC_PRIM_TYPE_LOCAL,
	SYNC_PRIM_TYPE_SERVER,
} SYNC_PRIM_TYPE;

typedef struct _SYNC_PRIM_LOCAL_
{
	ATOMIC_T				hRefCount;	/*!< Ref count for this sync */
	SYNC_PRIM_BLOCK			*psSyncBlock;	/*!< Synchronisation block this primitive is allocated on */
	IMG_UINT64				uiSpanAddr;		/*!< Span address of the sync */
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	IMG_HANDLE				hRecord;		/*!< Sync record handle */
#endif
} SYNC_PRIM_LOCAL;

typedef struct _SYNC_PRIM_SERVER_
{
	SYNC_BRIDGE_HANDLE		hBridge;			/*!< Bridge handle */
	IMG_HANDLE				hServerSync;		/*!< Handle to the server sync */
	IMG_UINT32				ui32FirmwareAddr;	/*!< Firmware address of the sync */
} SYNC_PRIM_SERVER;

typedef struct _SYNC_PRIM_
{
	PVRSRV_CLIENT_SYNC_PRIM	sCommon;		/*!< Client visible part of the sync prim */
	SYNC_PRIM_TYPE			eType;			/*!< Sync primative type */
	union {
		SYNC_PRIM_LOCAL		sLocal;			/*!< Local sync primative data */
		SYNC_PRIM_SERVER	sServer;		/*!< Server sync primative data */
	} u;
} SYNC_PRIM;


/* FIXME this must return a correctly typed pointer */
IMG_INTERNAL IMG_UINT32 SyncPrimGetFirmwareAddr(PVRSRV_CLIENT_SYNC_PRIM *psSync);

IMG_INTERNAL PVRSRV_ERROR SyncPrimLocalGetHandleAndOffset(PVRSRV_CLIENT_SYNC_PRIM *psSync,
							IMG_HANDLE *phBlock,
							IMG_UINT32 *pui32Offset);


#endif	/* _SYNC_INTERNAL_ */
