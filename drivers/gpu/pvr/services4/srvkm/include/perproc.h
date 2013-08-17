/*************************************************************************/ /*!
@Title          Handle Manager API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Perprocess data
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
#ifndef __PERPROC_H__
#define __PERPROC_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_types.h"
#include "resman.h"

#include "handle.h"

typedef struct _PVRSRV_PER_PROCESS_DATA_
{
	IMG_UINT32		ui32PID;
	IMG_HANDLE		hBlockAlloc;
	PRESMAN_CONTEXT 	hResManContext;
	IMG_HANDLE		hPerProcData;
	PVRSRV_HANDLE_BASE 	*psHandleBase;
#if defined (PVR_SECURE_HANDLES)
	/* Handles are being allocated in batches */
	IMG_BOOL		bHandlesBatched;
#endif  /* PVR_SECURE_HANDLES */
	IMG_UINT32		ui32RefCount;

	/* True if the process is the initialisation server. */
	IMG_BOOL		bInitProcess;
#if defined(PDUMP)
	/* True if pdump data from the process is 'persistent' */
	IMG_BOOL		bPDumpPersistent;
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	/* True if this process is marked for pdumping. This flag is
	 * significant in a multi-app environment.
	 */
	IMG_BOOL		bPDumpActive;
#endif /* SUPPORT_PDUMP_MULTI_PROCESS */
#endif
	/*
	 * OS specific data can be stored via this handle.
	 * See osperproc.h for a generic mechanism for initialising
	 * this field.
	 */
	IMG_HANDLE		hOsPrivateData;
} PVRSRV_PER_PROCESS_DATA;

PVRSRV_PER_PROCESS_DATA *PVRSRVPerProcessData(IMG_UINT32 ui32PID);

PVRSRV_ERROR PVRSRVPerProcessDataConnect(IMG_UINT32	ui32PID, IMG_UINT32 ui32Flags);
IMG_VOID PVRSRVPerProcessDataDisconnect(IMG_UINT32	ui32PID);

PVRSRV_ERROR PVRSRVPerProcessDataInit(IMG_VOID);
PVRSRV_ERROR PVRSRVPerProcessDataDeInit(IMG_VOID);

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVFindPerProcessData)
#endif
static INLINE
PVRSRV_PER_PROCESS_DATA *PVRSRVFindPerProcessData(IMG_VOID)
{
	return PVRSRVPerProcessData(OSGetCurrentProcessIDKM());
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVProcessPrivateData)
#endif
static INLINE
IMG_HANDLE PVRSRVProcessPrivateData(PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	return (psPerProc != IMG_NULL) ? psPerProc->hOsPrivateData : IMG_NULL;
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVPerProcessPrivateData)
#endif
static INLINE
IMG_HANDLE PVRSRVPerProcessPrivateData(IMG_UINT32 ui32PID)
{
	return PVRSRVProcessPrivateData(PVRSRVPerProcessData(ui32PID));
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVFindPerProcessPrivateData)
#endif
static INLINE
IMG_HANDLE PVRSRVFindPerProcessPrivateData(IMG_VOID)
{
	return PVRSRVProcessPrivateData(PVRSRVFindPerProcessData());
}

#if defined (__cplusplus)
}
#endif

#endif /* __PERPROC_H__ */

/******************************************************************************
 End of file (perproc.h)
******************************************************************************/
