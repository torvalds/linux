/**************************************************************************/ /*!
@File
@Title          Server side connection management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    API for server side connection management
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
*/ /***************************************************************************/

#if !defined(_CONNECTION_SERVER_H_)
#define _CONNECTION_SERVER_H_


#include "img_types.h"
#include "handle.h"
#include "pvrsrv_cleanup.h"

/* Variable used to hold in memory the timeout for the current time slice*/
extern IMG_UINT64 gui64TimesliceLimit;
/* Counter number of handle data freed during the current time slice */
extern IMG_UINT32 gui32HandleDataFreeCounter;
/* Set the maximum time the freeing of the resources can keep the lock */
#define CONNECTION_DEFERRED_CLEANUP_TIMESLICE_NS 3000 * 1000 /* 3ms */

typedef struct _CONNECTION_DATA_
{
	PVRSRV_HANDLE_BASE		*psHandleBase;
	PROCESS_HANDLE_BASE		*psProcessHandleBase;
	struct _SYNC_CONNECTION_DATA_	*psSyncConnectionData;
	struct _PDUMP_CONNECTION_DATA_	*psPDumpConnectionData;

	/* Holds the client flags supplied at connection time */
	IMG_UINT32			ui32ClientFlags;

	/*
	 * OS specific data can be stored via this handle.
	 * See osconnection_server.h for a generic mechanism
	 * for initialising this field.
	 */
	IMG_HANDLE			hOsPrivateData;

	IMG_PID				pid;

	void				*hSecureData;

	IMG_HANDLE			hProcessStats;

	IMG_HANDLE			hClientTLStream;

	/* Structure which is hooked into the cleanup thread work list */
	PVRSRV_CLEANUP_THREAD_WORK sCleanupThreadFn;

	/* List navigation for deferred freeing of connection data */
	struct _CONNECTION_DATA_	**ppsThis;
	struct _CONNECTION_DATA_	*psNext;
} CONNECTION_DATA;

#include "osconnection_server.h"

PVRSRV_ERROR PVRSRVConnectionConnect(void **ppvPrivData, void *pvOSData);
void PVRSRVConnectionDisconnect(void *pvPrivData);

IMG_PID PVRSRVGetPurgeConnectionPid(void);

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVConnectionPrivateData)
#endif
static INLINE
IMG_HANDLE PVRSRVConnectionPrivateData(CONNECTION_DATA *psConnection)
{
	return (psConnection != NULL) ? psConnection->hOsPrivateData : NULL;
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVGetDevData)
#endif
static INLINE
PVRSRV_DEVICE_NODE * PVRSRVGetDevData(CONNECTION_DATA *psConnection)
{
	return OSGetDevData(psConnection);
}

#endif /* !defined(_CONNECTION_SERVER_H_) */
