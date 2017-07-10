/*************************************************************************/ /*!
@File
@Title          Common bridge header for pdumpmm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for pdumpmm
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

#ifndef COMMON_PDUMPMM_BRIDGE_H
#define COMMON_PDUMPMM_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_types.h"
#include "pvrsrv_error.h"

#include "pdump.h"
#include "pdumpdefs.h"
#include "pvrsrv_memallocflags.h"
#include "devicemem_typedefs.h"


#define PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST			0
#define PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEM			PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+0
#define PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEMVALUE32			PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+1
#define PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEMVALUE64			PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+2
#define PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPSAVETOFILE			PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+3
#define PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPSYMBOLICADDR			PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+4
#define PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPPOL32			PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+5
#define PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPCBP			PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+6
#define PVRSRV_BRIDGE_PDUMPMM_DEVMEMINTPDUMPSAVETOFILEVIRTUAL			PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+7
#define PVRSRV_BRIDGE_PDUMPMM_CMD_LAST			(PVRSRV_BRIDGE_PDUMPMM_CMD_FIRST+7)


/*******************************************
            PMRPDumpLoadMem          
 *******************************************/

/* Bridge in structure for PMRPDumpLoadMem */
typedef struct PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEM_TAG
{
	IMG_HANDLE hPMR;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT32 ui32PDumpFlags;
	IMG_BOOL bbZero;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEM;

/* Bridge out structure for PMRPDumpLoadMem */
typedef struct PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEM_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEM;


/*******************************************
            PMRPDumpLoadMemValue32          
 *******************************************/

/* Bridge in structure for PMRPDumpLoadMemValue32 */
typedef struct PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEMVALUE32_TAG
{
	IMG_HANDLE hPMR;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_UINT32 ui32Value;
	IMG_UINT32 ui32PDumpFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEMVALUE32;

/* Bridge out structure for PMRPDumpLoadMemValue32 */
typedef struct PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEMVALUE32_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEMVALUE32;


/*******************************************
            PMRPDumpLoadMemValue64          
 *******************************************/

/* Bridge in structure for PMRPDumpLoadMemValue64 */
typedef struct PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEMVALUE64_TAG
{
	IMG_HANDLE hPMR;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_UINT64 ui64Value;
	IMG_UINT32 ui32PDumpFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEMVALUE64;

/* Bridge out structure for PMRPDumpLoadMemValue64 */
typedef struct PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEMVALUE64_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEMVALUE64;


/*******************************************
            PMRPDumpSaveToFile          
 *******************************************/

/* Bridge in structure for PMRPDumpSaveToFile */
typedef struct PVRSRV_BRIDGE_IN_PMRPDUMPSAVETOFILE_TAG
{
	IMG_HANDLE hPMR;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT32 ui32ArraySize;
	const IMG_CHAR * puiFileName;
	IMG_UINT32 ui32uiFileOffset;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRPDUMPSAVETOFILE;

/* Bridge out structure for PMRPDumpSaveToFile */
typedef struct PVRSRV_BRIDGE_OUT_PMRPDUMPSAVETOFILE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRPDUMPSAVETOFILE;


/*******************************************
            PMRPDumpSymbolicAddr          
 *******************************************/

/* Bridge in structure for PMRPDumpSymbolicAddr */
typedef struct PVRSRV_BRIDGE_IN_PMRPDUMPSYMBOLICADDR_TAG
{
	IMG_HANDLE hPMR;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_UINT32 ui32MemspaceNameLen;
	IMG_UINT32 ui32SymbolicAddrLen;
	/* Output pointer puiMemspaceName is also an implied input */
	IMG_CHAR * puiMemspaceName;
	/* Output pointer puiSymbolicAddr is also an implied input */
	IMG_CHAR * puiSymbolicAddr;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRPDUMPSYMBOLICADDR;

/* Bridge out structure for PMRPDumpSymbolicAddr */
typedef struct PVRSRV_BRIDGE_OUT_PMRPDUMPSYMBOLICADDR_TAG
{
	IMG_CHAR * puiMemspaceName;
	IMG_CHAR * puiSymbolicAddr;
	IMG_DEVMEM_OFFSET_T uiNewOffset;
	IMG_DEVMEM_OFFSET_T uiNextSymName;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRPDUMPSYMBOLICADDR;


/*******************************************
            PMRPDumpPol32          
 *******************************************/

/* Bridge in structure for PMRPDumpPol32 */
typedef struct PVRSRV_BRIDGE_IN_PMRPDUMPPOL32_TAG
{
	IMG_HANDLE hPMR;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_UINT32 ui32Value;
	IMG_UINT32 ui32Mask;
	PDUMP_POLL_OPERATOR eOperator;
	IMG_UINT32 ui32PDumpFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRPDUMPPOL32;

/* Bridge out structure for PMRPDumpPol32 */
typedef struct PVRSRV_BRIDGE_OUT_PMRPDUMPPOL32_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRPDUMPPOL32;


/*******************************************
            PMRPDumpCBP          
 *******************************************/

/* Bridge in structure for PMRPDumpCBP */
typedef struct PVRSRV_BRIDGE_IN_PMRPDUMPCBP_TAG
{
	IMG_HANDLE hPMR;
	IMG_DEVMEM_OFFSET_T uiReadOffset;
	IMG_DEVMEM_OFFSET_T uiWriteOffset;
	IMG_DEVMEM_SIZE_T uiPacketSize;
	IMG_DEVMEM_SIZE_T uiBufferSize;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_PMRPDUMPCBP;

/* Bridge out structure for PMRPDumpCBP */
typedef struct PVRSRV_BRIDGE_OUT_PMRPDUMPCBP_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_PMRPDUMPCBP;


/*******************************************
            DevmemIntPDumpSaveToFileVirtual          
 *******************************************/

/* Bridge in structure for DevmemIntPDumpSaveToFileVirtual */
typedef struct PVRSRV_BRIDGE_IN_DEVMEMINTPDUMPSAVETOFILEVIRTUAL_TAG
{
	IMG_HANDLE hDevmemServerContext;
	IMG_DEV_VIRTADDR sAddress;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT32 ui32ArraySize;
	const IMG_CHAR * puiFileName;
	IMG_UINT32 ui32FileOffset;
	IMG_UINT32 ui32PDumpFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DEVMEMINTPDUMPSAVETOFILEVIRTUAL;

/* Bridge out structure for DevmemIntPDumpSaveToFileVirtual */
typedef struct PVRSRV_BRIDGE_OUT_DEVMEMINTPDUMPSAVETOFILEVIRTUAL_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DEVMEMINTPDUMPSAVETOFILEVIRTUAL;


#endif /* COMMON_PDUMPMM_BRIDGE_H */
