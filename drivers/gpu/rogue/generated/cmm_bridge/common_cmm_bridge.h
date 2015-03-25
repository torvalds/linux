/*************************************************************************/ /*!
@File
@Title          Common bridge header for cmm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
                the client and sever side of the bridge for cmm
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

#include "devicemem_typedefs.h"


#include "pvr_bridge_io.h"

#define PVRSRV_BRIDGE_CMM_CMD_FIRST			(PVRSRV_BRIDGE_CMM_START)
#define PVRSRV_BRIDGE_CMM_PMRWRITEPMPAGELIST			PVRSRV_IOWR(PVRSRV_BRIDGE_CMM_CMD_FIRST+0)
#define PVRSRV_BRIDGE_CMM_PMRWRITEVFPPAGELIST			PVRSRV_IOWR(PVRSRV_BRIDGE_CMM_CMD_FIRST+1)
#define PVRSRV_BRIDGE_CMM_PMRUNWRITEPMPAGELIST			PVRSRV_IOWR(PVRSRV_BRIDGE_CMM_CMD_FIRST+2)
#define PVRSRV_BRIDGE_CMM_DEVMEMINTCTXEXPORT			PVRSRV_IOWR(PVRSRV_BRIDGE_CMM_CMD_FIRST+3)
#define PVRSRV_BRIDGE_CMM_DEVMEMINTCTXUNEXPORT			PVRSRV_IOWR(PVRSRV_BRIDGE_CMM_CMD_FIRST+4)
#define PVRSRV_BRIDGE_CMM_DEVMEMINTCTXIMPORT			PVRSRV_IOWR(PVRSRV_BRIDGE_CMM_CMD_FIRST+5)
#define PVRSRV_BRIDGE_CMM_CMD_LAST			(PVRSRV_BRIDGE_CMM_CMD_FIRST+5)


/*******************************************
            PMRWritePMPageList          
 *******************************************/

/* Bridge in structure for PMRWritePMPageList */
typedef struct PVRSRV_BRIDGE_IN_PMRWRITEPMPAGELIST_TAG
{
	IMG_HANDLE hPageListPMR;
	IMG_DEVMEM_OFFSET_T uiTableOffset;
	IMG_DEVMEM_SIZE_T uiTableLength;
	IMG_HANDLE hReferencePMR;
	IMG_UINT32 ui32Log2PageSize;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRWRITEPMPAGELIST;


/* Bridge out structure for PMRWritePMPageList */
typedef struct PVRSRV_BRIDGE_OUT_PMRWRITEPMPAGELIST_TAG
{
	IMG_HANDLE hPageList;
	IMG_UINT64 ui64CheckSum;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRWRITEPMPAGELIST;

/*******************************************
            PMRWriteVFPPageList          
 *******************************************/

/* Bridge in structure for PMRWriteVFPPageList */
typedef struct PVRSRV_BRIDGE_IN_PMRWRITEVFPPAGELIST_TAG
{
	IMG_HANDLE hFreeListPMR;
	IMG_DEVMEM_OFFSET_T uiTableOffset;
	IMG_DEVMEM_SIZE_T uiTableLength;
	IMG_UINT32 ui32TableBase;
	IMG_UINT32 ui32Log2PageSize;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRWRITEVFPPAGELIST;


/* Bridge out structure for PMRWriteVFPPageList */
typedef struct PVRSRV_BRIDGE_OUT_PMRWRITEVFPPAGELIST_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRWRITEVFPPAGELIST;

/*******************************************
            PMRUnwritePMPageList          
 *******************************************/

/* Bridge in structure for PMRUnwritePMPageList */
typedef struct PVRSRV_BRIDGE_IN_PMRUNWRITEPMPAGELIST_TAG
{
	IMG_HANDLE hPageList;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRUNWRITEPMPAGELIST;


/* Bridge out structure for PMRUnwritePMPageList */
typedef struct PVRSRV_BRIDGE_OUT_PMRUNWRITEPMPAGELIST_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRUNWRITEPMPAGELIST;

/*******************************************
            DevmemIntCtxExport          
 *******************************************/

/* Bridge in structure for DevmemIntCtxExport */
typedef struct PVRSRV_BRIDGE_IN_DEVMEMINTCTXEXPORT_TAG
{
	IMG_HANDLE hDevMemServerContext;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DEVMEMINTCTXEXPORT;


/* Bridge out structure for DevmemIntCtxExport */
typedef struct PVRSRV_BRIDGE_OUT_DEVMEMINTCTXEXPORT_TAG
{
	IMG_HANDLE hDevMemIntCtxExport;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DEVMEMINTCTXEXPORT;

/*******************************************
            DevmemIntCtxUnexport          
 *******************************************/

/* Bridge in structure for DevmemIntCtxUnexport */
typedef struct PVRSRV_BRIDGE_IN_DEVMEMINTCTXUNEXPORT_TAG
{
	IMG_HANDLE hDevMemIntCtxExport;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DEVMEMINTCTXUNEXPORT;


/* Bridge out structure for DevmemIntCtxUnexport */
typedef struct PVRSRV_BRIDGE_OUT_DEVMEMINTCTXUNEXPORT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DEVMEMINTCTXUNEXPORT;

/*******************************************
            DevmemIntCtxImport          
 *******************************************/

/* Bridge in structure for DevmemIntCtxImport */
typedef struct PVRSRV_BRIDGE_IN_DEVMEMINTCTXIMPORT_TAG
{
	IMG_HANDLE hDevMemIntCtxExport;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DEVMEMINTCTXIMPORT;


/* Bridge out structure for DevmemIntCtxImport */
typedef struct PVRSRV_BRIDGE_OUT_DEVMEMINTCTXIMPORT_TAG
{
	IMG_HANDLE hDevMemServerContext;
	IMG_HANDLE hPrivData;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DEVMEMINTCTXIMPORT;

#endif /* COMMON_CMM_BRIDGE_H */
