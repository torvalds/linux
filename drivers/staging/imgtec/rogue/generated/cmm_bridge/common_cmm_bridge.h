/*************************************************************************/ /*!
@File
@Title          Common bridge header for cmm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for cmm
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

#ifndef COMMON_CMM_BRIDGE_H
#define COMMON_CMM_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_types.h"
#include "pvrsrv_error.h"

#include "devicemem_typedefs.h"


#define PVRSRV_BRIDGE_CMM_CMD_FIRST			0
#define PVRSRV_BRIDGE_CMM_DEVMEMINTEXPORTCTX			PVRSRV_BRIDGE_CMM_CMD_FIRST+0
#define PVRSRV_BRIDGE_CMM_DEVMEMINTUNEXPORTCTX			PVRSRV_BRIDGE_CMM_CMD_FIRST+1
#define PVRSRV_BRIDGE_CMM_DEVMEMINTACQUIREREMOTECTX			PVRSRV_BRIDGE_CMM_CMD_FIRST+2
#define PVRSRV_BRIDGE_CMM_CMD_LAST			(PVRSRV_BRIDGE_CMM_CMD_FIRST+2)


/*******************************************
            DevmemIntExportCtx          
 *******************************************/

/* Bridge in structure for DevmemIntExportCtx */
typedef struct PVRSRV_BRIDGE_IN_DEVMEMINTEXPORTCTX_TAG
{
	IMG_HANDLE hContext;
	IMG_HANDLE hPMR;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DEVMEMINTEXPORTCTX;

/* Bridge out structure for DevmemIntExportCtx */
typedef struct PVRSRV_BRIDGE_OUT_DEVMEMINTEXPORTCTX_TAG
{
	IMG_HANDLE hContextExport;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DEVMEMINTEXPORTCTX;


/*******************************************
            DevmemIntUnexportCtx          
 *******************************************/

/* Bridge in structure for DevmemIntUnexportCtx */
typedef struct PVRSRV_BRIDGE_IN_DEVMEMINTUNEXPORTCTX_TAG
{
	IMG_HANDLE hContextExport;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DEVMEMINTUNEXPORTCTX;

/* Bridge out structure for DevmemIntUnexportCtx */
typedef struct PVRSRV_BRIDGE_OUT_DEVMEMINTUNEXPORTCTX_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DEVMEMINTUNEXPORTCTX;


/*******************************************
            DevmemIntAcquireRemoteCtx          
 *******************************************/

/* Bridge in structure for DevmemIntAcquireRemoteCtx */
typedef struct PVRSRV_BRIDGE_IN_DEVMEMINTACQUIREREMOTECTX_TAG
{
	IMG_HANDLE hPMR;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DEVMEMINTACQUIREREMOTECTX;

/* Bridge out structure for DevmemIntAcquireRemoteCtx */
typedef struct PVRSRV_BRIDGE_OUT_DEVMEMINTACQUIREREMOTECTX_TAG
{
	IMG_HANDLE hContext;
	IMG_HANDLE hPrivData;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DEVMEMINTACQUIREREMOTECTX;


#endif /* COMMON_CMM_BRIDGE_H */
