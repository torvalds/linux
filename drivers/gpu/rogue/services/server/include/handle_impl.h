/**************************************************************************/ /*!
@File
@Title          Implementation Callbacks for Handle Manager API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the handle manager API. This file is for declarations 
                and definitions that are private/internal to the handle manager 
                API but need to be shared between the generic handle manager 
                code and the various handle manager backends, i.e. the code that 
                implements the various callbacks.
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

#if !defined(__HANDLE_IMPL_H__)
#define __HANDLE_IMPL_H__

#include "img_types.h"
#include "pvrsrv_error.h"

typedef struct _HANDLE_IMPL_BASE_ HANDLE_IMPL_BASE;

typedef PVRSRV_ERROR (*PFN_HANDLE_ITER)(IMG_HANDLE hHandle, IMG_VOID *pvData);

typedef struct _HANDLE_IMPL_FUNCTAB_
{
	/* Acquire a new handle which is associated with the given data */
	PVRSRV_ERROR (*pfnAcquireHandle)(HANDLE_IMPL_BASE *psHandleBase, IMG_HANDLE *phHandle, IMG_VOID *pvData);

	/* Release the given handle (optionally returning the data associated with it) */
	PVRSRV_ERROR (*pfnReleaseHandle)(HANDLE_IMPL_BASE *psHandleBase, IMG_HANDLE hHandle, IMG_VOID **ppvData);

	/* Get the data associated with the given handle */
	PVRSRV_ERROR (*pfnGetHandleData)(HANDLE_IMPL_BASE *psHandleBase, IMG_HANDLE hHandle, IMG_VOID **ppvData);

	PVRSRV_ERROR (*pfnIterateOverHandles)(HANDLE_IMPL_BASE *psHandleBase, PFN_HANDLE_ITER pfnHandleIter, IMG_VOID *pvHandleIterData);

	/* Enable handle purging on the given handle base */
	PVRSRV_ERROR (*pfnEnableHandlePurging)(HANDLE_IMPL_BASE *psHandleBase);

	/* Purge handles on the given handle base */
	PVRSRV_ERROR (*pfnPurgeHandles)(HANDLE_IMPL_BASE *psHandleBase);

	/* Create handle base */
	PVRSRV_ERROR (*pfnCreateHandleBase)(HANDLE_IMPL_BASE **psHandleBase);

	/* Destroy handle base */
	PVRSRV_ERROR (*pfnDestroyHandleBase)(HANDLE_IMPL_BASE *psHandleBase);
} HANDLE_IMPL_FUNCTAB;

PVRSRV_ERROR PVRSRVHandleGetFuncTable(HANDLE_IMPL_FUNCTAB const **ppsFuncs);

#endif /* !defined(__HANDLE_IMPL_H__) */
