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

#ifndef _CONNECTION_SERVER_H_
#define _CONNECTION_SERVER_H_

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_types.h"
#include "resman.h"

#include "handle.h"

typedef struct _CONNECTION_DATA_
{
	PRESMAN_CONTEXT 	hResManContext;
	PVRSRV_HANDLE_BASE 	*psHandleBase;
	struct _SYNC_CONNECTION_DATA_ *psSyncConnectionData;
	struct _PDUMP_CONNECTION_DATA_ *psPDumpConnectionData;

	/* True if the process is the initialisation server. */
	IMG_BOOL		bInitProcess;

	/*
	 * OS specific data can be stored via this handle.
	 * See osconnection_server.h for a generic mechanism
	 * for initialising this field.
	 */
	IMG_HANDLE		hOsPrivateData;

	IMG_PVOID		hSecureData;

	IMG_HANDLE      hProcessStats;
} CONNECTION_DATA;

PVRSRV_ERROR PVRSRVConnectionConnect(IMG_PVOID *ppvPrivData, IMG_PVOID pvOSData);
IMG_VOID PVRSRVConnectionDisconnect(IMG_PVOID pvPrivData);

PVRSRV_ERROR PVRSRVConnectionInit(IMG_VOID);
PVRSRV_ERROR PVRSRVConnectionDeInit(IMG_VOID);


#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVConnectionPrivateData)
#endif
static INLINE
IMG_HANDLE PVRSRVConnectionPrivateData(CONNECTION_DATA *psConnection)
{
	return (psConnection != IMG_NULL) ? psConnection->hOsPrivateData : IMG_NULL;
}

#if defined (__cplusplus)
}
#endif

#endif /* _CONNECTION_SERVER_H_ */
