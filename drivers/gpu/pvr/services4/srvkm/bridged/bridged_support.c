/*************************************************************************/ /*!
@Title          PVR Bridge Support Functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    User/kernel mode bridge support.  The functions in here
                may be used beyond the bridge code proper (e.g. Linux
                mmap interface).
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

#include "img_defs.h"
#include "servicesint.h"
#include "bridged_support.h"


/*
 * Derive the internal OS specific memory handle from a secure
 * handle.
 */
PVRSRV_ERROR
PVRSRVLookupOSMemHandle(PVRSRV_HANDLE_BASE *psHandleBase, IMG_HANDLE *phOSMemHandle, IMG_HANDLE hMHandle)
{
	IMG_HANDLE hMHandleInt;
	PVRSRV_HANDLE_TYPE eHandleType;
	PVRSRV_ERROR eError;

	/*
	 * We don't know the type of the handle at this point, so we use
	 * PVRSRVLookupHandleAnyType to look it up.
	 */
	eError = PVRSRVLookupHandleAnyType(psHandleBase, &hMHandleInt,
							  &eHandleType,
							  hMHandle);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	switch(eHandleType)
	{
#if defined(PVR_SECURE_HANDLES)
		case PVRSRV_HANDLE_TYPE_MEM_INFO:
		case PVRSRV_HANDLE_TYPE_MEM_INFO_REF:
		case PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO:
		{
			PVRSRV_KERNEL_MEM_INFO *psMemInfo = (PVRSRV_KERNEL_MEM_INFO *)hMHandleInt;

			*phOSMemHandle = psMemInfo->sMemBlk.hOSMemHandle;

			break;
		}
		case PVRSRV_HANDLE_TYPE_SYNC_INFO:
		{
			PVRSRV_KERNEL_SYNC_INFO *psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)hMHandleInt;
			PVRSRV_KERNEL_MEM_INFO *psMemInfo = psSyncInfo->psSyncDataMemInfoKM;
			
			*phOSMemHandle = psMemInfo->sMemBlk.hOSMemHandle;

			break;
		}
		case  PVRSRV_HANDLE_TYPE_SOC_TIMER:
		{
			*phOSMemHandle = (IMG_VOID *)hMHandleInt;
			break;
		}
#else
		case  PVRSRV_HANDLE_TYPE_NONE:
			*phOSMemHandle = (IMG_VOID *)hMHandleInt;
			break;
#endif
		default:
			return PVRSRV_ERROR_BAD_MAPPING;
	}

	return PVRSRV_OK;
}
/******************************************************************************
 End of file (bridged_support.c)
******************************************************************************/
