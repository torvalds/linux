/*************************************************************************/ /*!
@File
@Title          Common bridge header for rgxinit
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for rgxinit
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

#include <powervr/mem_types.h>

#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include "rgxscript.h"
#include "devicemem_typedefs.h"
#include "rgx_fwif.h"


#define PVRSRV_BRIDGE_RGXINIT_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXINIT_RGXINITALLOCFWIMGMEM			PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXINIT_RGXINITFIRMWARE			PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXINIT_RGXINITFINALISEFWIMAGE			PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXINIT_RGXINITDEVPART2			PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXINIT_GPUVIRTPOPULATELMASUBARENAS			PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXINIT_RGXINITGUEST			PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+5
#define PVRSRV_BRIDGE_RGXINIT_RGXINITFIRMWAREEXTENDED			PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+6
#define PVRSRV_BRIDGE_RGXINIT_CMD_LAST			(PVRSRV_BRIDGE_RGXINIT_CMD_FIRST+6)


/*******************************************
            RGXInitAllocFWImgMem          
 *******************************************/

/* Bridge in structure for RGXInitAllocFWImgMem */
typedef struct PVRSRV_BRIDGE_IN_RGXINITALLOCFWIMGMEM_TAG
{
	IMG_DEVMEM_SIZE_T uiFWCodeLen;
	IMG_DEVMEM_SIZE_T uiFWDataLen;
	IMG_DEVMEM_SIZE_T uiFWCoremem;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITALLOCFWIMGMEM;

/* Bridge out structure for RGXInitAllocFWImgMem */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITALLOCFWIMGMEM_TAG
{
	IMG_HANDLE hFWCodePMR;
	IMG_DEV_VIRTADDR sFWCodeDevVAddrBase;
	IMG_HANDLE hFWDataPMR;
	IMG_DEV_VIRTADDR sFWDataDevVAddrBase;
	IMG_HANDLE hFWCorememPMR;
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
	IMG_BOOL bEnableSignatureChecks;
	IMG_UINT32 ui32SignatureChecksBufSize;
	IMG_UINT32 ui32HWPerfFWBufSizeKB;
	IMG_UINT64 ui64HWPerfFilter;
	IMG_UINT32 ui32RGXFWAlignChecksArrLength;
	IMG_UINT32 * pui32RGXFWAlignChecks;
	IMG_UINT32 ui32ConfigFlags;
	IMG_UINT32 ui32LogType;
	IMG_UINT32 ui32FilterFlags;
	IMG_UINT32 ui32JonesDisableMask;
	IMG_UINT32 ui32ui32HWRDebugDumpLimit;
	RGXFWIF_COMPCHECKS_BVNC sClientBVNC;
	IMG_UINT32 ui32HWPerfCountersDataSize;
	RGX_RD_POWER_ISLAND_CONF eRGXRDPowerIslandConf;
	FW_PERF_CONF eFirmwarePerf;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITFIRMWARE;

/* Bridge out structure for RGXInitFirmware */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITFIRMWARE_TAG
{
	RGXFWIF_DEV_VIRTADDR spsRGXFwInit;
	IMG_HANDLE hHWPerfPMR;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITFIRMWARE;


/*******************************************
            RGXInitFinaliseFWImage          
 *******************************************/

/* Bridge in structure for RGXInitFinaliseFWImage */
typedef struct PVRSRV_BRIDGE_IN_RGXINITFINALISEFWIMAGE_TAG
{
	 IMG_UINT32 ui32EmptyStructPlaceholder;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITFINALISEFWIMAGE;

/* Bridge out structure for RGXInitFinaliseFWImage */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITFINALISEFWIMAGE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITFINALISEFWIMAGE;


/*******************************************
            RGXInitDevPart2          
 *******************************************/

/* Bridge in structure for RGXInitDevPart2 */
typedef struct PVRSRV_BRIDGE_IN_RGXINITDEVPART2_TAG
{
	RGX_INIT_COMMAND * psDbgScript;
	IMG_UINT32 ui32DeviceFlags;
	IMG_UINT32 ui32HWPerfHostBufSize;
	IMG_UINT32 ui32HWPerfHostFilter;
	IMG_UINT32 ui32RGXActivePMConf;
	IMG_HANDLE hFWCodePMR;
	IMG_HANDLE hFWDataPMR;
	IMG_HANDLE hFWCorememPMR;
	IMG_HANDLE hHWPerfPMR;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITDEVPART2;

/* Bridge out structure for RGXInitDevPart2 */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITDEVPART2_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITDEVPART2;


/*******************************************
            GPUVIRTPopulateLMASubArenas          
 *******************************************/

/* Bridge in structure for GPUVIRTPopulateLMASubArenas */
typedef struct PVRSRV_BRIDGE_IN_GPUVIRTPOPULATELMASUBARENAS_TAG
{
	IMG_UINT32 ui32NumElements;
	IMG_UINT32 * pui32Elements;
	IMG_BOOL bEnableTrustedDeviceAceConfig;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_GPUVIRTPOPULATELMASUBARENAS;

/* Bridge out structure for GPUVIRTPopulateLMASubArenas */
typedef struct PVRSRV_BRIDGE_OUT_GPUVIRTPOPULATELMASUBARENAS_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_GPUVIRTPOPULATELMASUBARENAS;


/*******************************************
            RGXInitGuest          
 *******************************************/

/* Bridge in structure for RGXInitGuest */
typedef struct PVRSRV_BRIDGE_IN_RGXINITGUEST_TAG
{
	IMG_BOOL bEnableSignatureChecks;
	IMG_UINT32 ui32SignatureChecksBufSize;
	IMG_UINT32 ui32RGXFWAlignChecksArrLength;
	IMG_UINT32 * pui32RGXFWAlignChecks;
	IMG_UINT32 ui32DeviceFlags;
	RGXFWIF_COMPCHECKS_BVNC sClientBVNC;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITGUEST;

/* Bridge out structure for RGXInitGuest */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITGUEST_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITGUEST;


/*******************************************
            RGXInitFirmwareExtended          
 *******************************************/

/* Bridge in structure for RGXInitFirmwareExtended */
typedef struct PVRSRV_BRIDGE_IN_RGXINITFIRMWAREEXTENDED_TAG
{
	IMG_UINT32 ui32RGXFWAlignChecksArrLength;
	IMG_UINT32 * pui32RGXFWAlignChecks;
	RGX_FW_INIT_IN_PARAMS spsInParams;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXINITFIRMWAREEXTENDED;

/* Bridge out structure for RGXInitFirmwareExtended */
typedef struct PVRSRV_BRIDGE_OUT_RGXINITFIRMWAREEXTENDED_TAG
{
	RGXFWIF_DEV_VIRTADDR spsRGXFwInit;
	IMG_HANDLE hHWPerfPMR2;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXINITFIRMWAREEXTENDED;


#endif /* COMMON_RGXINIT_BRIDGE_H */
