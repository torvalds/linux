/*************************************************************************/ /*!
@File			ri_server.h
@Title          Resource Information abstraction
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Resource Information (RI) functions
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

#ifndef _RI_SERVER_H_
#define _RI_SERVER_H_

#include <img_defs.h>
#include <ri_typedefs.h>
#include <pmr.h>
#include <pvrsrv_error.h>

PVRSRV_ERROR RIInitKM(void);
void RIDeInitKM(void);

PVRSRV_ERROR RIWritePMREntryKM(PMR *hPMR,
					   	   	   IMG_UINT32 ui32TextASize,
					   	   	   const IMG_CHAR ai8TextA[RI_MAX_TEXT_LEN+1],
					   	   	   IMG_UINT64 uiLogicalSize);

PVRSRV_ERROR RIWriteMEMDESCEntryKM(PMR *hPMR,
					   	   	   	   IMG_UINT32 ui32TextBSize,
					   	   	   	   const IMG_CHAR ai8TextB[RI_MAX_TEXT_LEN+1],
					   	   	   	   IMG_UINT64 uiOffset,
					   	   	   	   IMG_UINT64 uiSize,
					   	   	   	   IMG_UINT64 uiBackedSize,
					   	   	   	   IMG_BOOL bIsImport,
					   	   	   	   IMG_BOOL bIsExportable,
					   	   	   	   RI_HANDLE *phRIHandle);

PVRSRV_ERROR RIWriteProcListEntryKM(IMG_UINT32 ui32TextBSize,
                                    const IMG_CHAR *psz8TextB,
                                    IMG_UINT64 ui64Size,
                                    IMG_UINT64 uiBackedSize,
                                    IMG_UINT64 ui64DevVAddr,
                                    RI_HANDLE *phRIHandle);

PVRSRV_ERROR RIUpdateMEMDESCAddrKM(RI_HANDLE hRIHandle,
								   IMG_DEV_VIRTADDR sVAddr);

PVRSRV_ERROR RIUpdateMEMDESCPinningKM(RI_HANDLE hRIHandle,
								   IMG_BOOL bIsPinned);

PVRSRV_ERROR RIUpdateMEMDESCBackingKM(RI_HANDLE hRIHandle,
                                      IMG_INT32 iNumModified);

PVRSRV_ERROR RIDeletePMREntryKM(RI_HANDLE hRIHandle);
PVRSRV_ERROR RIDeleteMEMDESCEntryKM(RI_HANDLE hRIHandle);

PVRSRV_ERROR RIDeleteListKM(void);

PVRSRV_ERROR RIDumpListKM(PMR *hPMR);

PVRSRV_ERROR RIDumpAllKM(void);

PVRSRV_ERROR RIDumpProcessKM(IMG_PID pid);

#if defined(DEBUG)
PVRSRV_ERROR RIDumpProcessListKM(PMR *hPMR,
								 IMG_PID pid,
								 IMG_UINT64 ui64Offset,
								 IMG_DEV_VIRTADDR *psDevVAddr);
#endif

IMG_BOOL RIGetListEntryKM(IMG_PID pid,
						  IMG_HANDLE **ppHandle,
						  IMG_CHAR **ppszEntryString);

#endif /* #ifndef _RI_SERVER_H _*/
