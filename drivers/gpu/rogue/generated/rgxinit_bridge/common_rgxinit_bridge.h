/*************************************************************************/ /*!
@File
@Title          Common bridge header for rgxinit
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
                the client and sever side of the bridge for rgxinit
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

#ifndef COMMON_RGXINIT_BRIDGE_H
#define COMMON_RGXINIT_BRIDGE_H

#include "rgx_bridge.h"
#include "rgxscript.h"
#include "devicemem_typedefs.h"
#include "rgx_fwif_shared.h"
#include "rgx_fwif.h"


#include "pvr_bridge_io.h"

#define PVRSRV_BRIDGE_RGXINIT_CMD_FIRST			(PVRSRV_BRIDGE_RGXINIT_START)
#define PVRSRV_BRIDGE_RGXINIT_RGXINITALLOCFWIMGMEM			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+0)
#define PVRSRV_BRIDGE_RGXINIT_RGXINITFIRMWARE			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+1)
#define PVRSRV_BRIDGE_RGXINIT_RGXINITLOADFWIMAGE			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+2)
#define PVRSRV_BRIDGE_RGXINIT_RGXINITDEVPART2			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+3)
#define PVRSRV_BRIDGE_RGXINIT_CMD_LAST			(PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+3)


/*******************************************
            RGXInitAllocFWImgMem          
 *******************************************/

/* Bridge in structure for RGXInitAllocFWImgMem */
typedef struct PVRSRV_BRIDGE_IN_RGXINITALLOCFWIMGMEM_TAG
{
	IMG_HANDLE hDevNode;
	IMG_DEVMEM_SIZE_T uiFWCodeLen;
	IMG_DEVMEM_SIZE_T uiFWDataLen;
	IMG_DEVMEM_SIZE_T uiFWCoremem;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITALLOCFWIMGMEM;


/* Bridge out structure for RGXInitAllocFWImgMem */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITALLOCFWIMGMEM_TAG
{
	DEVMEM_SERVER_EXPORTCOOKIE hFWCodeAllocServerExportCookie;
	IMG_DEV_VIRTADDR sFWCodeDevVAddrBase;
	DEVMEM_SERVER_EXPORTCOOKIE hFWDataAllocServerExportCookie;
	IMG_DEV_VIRTADDR sFWDataDevVAddrBase;
	DEVMEM_SERVER_EXPORTCOOKIE hFWCorememAllocServerExportCookie;
	IMG_DEV_VIRTADDR sFWCorememDevVAddrBase;
	RGXFWIF_DEV_VIRTADDR sFWCorememMetaVAddrBase;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITALLOCFWIMGMEM;

/*******************************************
            RGXInitFirmware          
 *******************************************/

/* Bridge in structure for RGXInitFirmware */
typedef struct PVRSRV_BRIDGE_IN_RGXINITFIRMWARE_TAG
{
	IMG_HANDLE hDevNode;
	IMG_BOOL bEnableSignatureChecks;
	IMG_UINT32 ui32SignatureChecksBufSize;
	IMG_UINT32 ui32HWPerfFWBufSizeKB;
	IMG_UINT64 ui64HWPerfFilter;
	IMG_UINT32 ui32RGXFWAlignChecksSize;
	IMG_UINT32 * pui32RGXFWAlignChecks;
	IMG_UINT32 ui32ConfigFlags;
	IMG_UINT32 ui32LogType;
	IMG_UINT32 ui32FilterFlags;
	RGXFWIF_COMPCHECKS_BVNC sClientBVNC;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITFIRMWARE;


/* Bridge out structure for RGXInitFirmware */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITFIRMWARE_TAG
{
	RGXFWIF_DEV_VIRTADDR spsRGXFwInit;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITFIRMWARE;

/*******************************************
            RGXInitLoadFWImage          
 *******************************************/

/* Bridge in structure for RGXInitLoadFWImage */
typedef struct PVRSRV_BRIDGE_IN_RGXINITLOADFWIMAGE_TAG
{
	IMG_HANDLE hImgDestImport;
	IMG_HANDLE hImgSrcImport;
	IMG_UINT64 ui64ImgLen;
	IMG_HANDLE hSigImport;
	IMG_UINT64 ui64SigLen;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITLOADFWIMAGE;


/* Bridge out structure for RGXInitLoadFWImage */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITLOADFWIMAGE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITLOADFWIMAGE;

/*******************************************
            RGXInitDevPart2          
 *******************************************/

/* Bridge in structure for RGXInitDevPart2 */
typedef struct PVRSRV_BRIDGE_IN_RGXINITDEVPART2_TAG
{
	IMG_HANDLE hDevNode;
	RGX_INIT_COMMAND * psInitScript;
	RGX_INIT_COMMAND * psDbgScript;
	RGX_INIT_COMMAND * psDbgBusScript;
	RGX_INIT_COMMAND * psDeinitScript;
	IMG_UINT32 ui32ui32KernelCatBaseIdReg;
	IMG_UINT32 ui32KernelCatBaseId;
	IMG_UINT32 ui32KernelCatBaseReg;
	IMG_UINT32 ui32KernelCatBaseWordSize;
	IMG_UINT32 ui32KernelCatBaseAlignShift;
	IMG_UINT32 ui32KernelCatBaseShift;
	IMG_UINT64 ui64KernelCatBaseMask;
	IMG_UINT32 ui32DeviceFlags;
	IMG_UINT32 ui32RGXActivePMConf;
	DEVMEM_SERVER_EXPORTCOOKIE hFWCodeAllocServerExportCookie;
	DEVMEM_SERVER_EXPORTCOOKIE hFWDataAllocServerExportCookie;
	DEVMEM_SERVER_EXPORTCOOKIE hFWCorememAllocServerExportCookie;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITDEVPART2;


/* Bridge out structure for RGXInitDevPart2 */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITDEVPART2_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITDEVPART2;

#endif /* COMMON_RGXINIT_BRIDGE_H */
