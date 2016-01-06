/*************************************************************************/ /*!
@File
@Title          RGX API Header kernel mode
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Exported RGX API details
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

#ifndef __RGXAPI_KM_H__
#define __RGXAPI_KM_H__

#if defined(SUPPORT_SHARED_SLC)
/*!
******************************************************************************

 @Function	RGXInitSLC

 @Description Init the SLC after a power up. It is required to call this 
              function if using SUPPORT_SHARED_SLC. Otherwise, it shouldn't
			  be called.

 @Input	   hDevHandle : RGX Device Node

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXInitSLC(IMG_HANDLE hDevHandle);
#endif

#if defined(SUPPORT_KERNEL_HWPERF)

#include "rgx_hwperf_km.h"


/******************************************************************************
 * RGX HW Performance Profiling Control API(s)
 *****************************************************************************/

/**************************************************************************/ /*!
@Function      RGXHWPerfConnect
@Description   Obtain a connection object to the HWPerf device
@Output        phDevData      Address of a handle to a connection object
@Return        PVRSRV_ERROR:  for system error codes
*/ /***************************************************************************/
PVRSRV_ERROR RGXHWPerfConnect(
		IMG_HANDLE* phDevData);


/**************************************************************************/ /*!
@Function       RGXHWPerfDisconnect
@Description    Disconnect from the HWPerf device
@Input          hSrvHandle    Handle to connection object as returned from
                                RGXHWPerfConnect()
@Return         PVRSRV_ERROR: for system error codes
*/ /***************************************************************************/
PVRSRV_ERROR RGXHWPerfDisconnect(
		IMG_HANDLE hDevData);


/**************************************************************************/ /*!
@Function       RGXHWPerfControl
@Description    Enable or disable the generation of RGX HWPerf event packets.
                 See RGXCtrlHWPerf().
@Input          hDevData         Handle to connection object
@Input          bToggle          Switch to toggle or apply mask.
@Input          ui64Mask         Mask of events to control.
@Return         PVRSRV_ERROR:    for system error codes
*/ /***************************************************************************/
PVRSRV_ERROR IMG_CALLCONV RGXHWPerfControl(
		IMG_HANDLE  hDevData,
		IMG_BOOL    bToggle,
		IMG_UINT64  ui64Mask);


/**************************************************************************/ /*!
@Function       RGXHWPerfConfigureAndEnableCounters
@Description    Enable and configure the performance counter block for
                 one or more device layout modules.
                 See RGXConfigureAndEnableHWPerfCounters().
@Input          hDevData         Handle to connection object
@Input          ui32NumBlocks    Number of elements in the array
@Input          asBlockConfigs   Address of the array of configuration blocks
@Return         PVRSRV_ERROR:    for system error codes
*/ /***************************************************************************/
PVRSRV_ERROR IMG_CALLCONV RGXHWPerfConfigureAndEnableCounters(
		IMG_HANDLE                 hDevData,
		IMG_UINT32                 ui32NumBlocks,
		RGX_HWPERF_CONFIG_CNTBLK*  asBlockConfigs);


/**************************************************************************/ /*!
@Function       RGXDisableHWPerfCounters
@Description    Disable the performance counter block for one or more
                 device layout modules. See RGXDisableHWPerfCounters().
@Input          hDevData        Handle to connection/device object
@Input          ui32NumBlocks   Number of elements in the array
@Input          aeBlockIDs      An array of bytes with values taken from
                                 the RGX_HWPERF_CNTBLK_ID enumeration.
@Return         PVRSRV_ERROR:   for system error codes
*/ /***************************************************************************/
PVRSRV_ERROR IMG_CALLCONV RGXHWPerfDisableCounters(
		IMG_HANDLE   hDevData,
		IMG_UINT32   ui32NumBlocks,
		IMG_UINT16*   aeBlockIDs);


/******************************************************************************
 * RGX HW Performance Profiling Retrieval API(s)
 *
 * The client must ensure their use of this acquire/release API for a single 
 * connection/stream must not be shared with multiple execution contexts e.g.
 * between a kernel thread and an ISR handler. It is the client’s
 * responsibility to ensure this API is not interrupted by a high priority
 * thread/ISR
 *****************************************************************************/

/**************************************************************************/ /*!
@Function       RGXHWPerfAcquireData
@Description    When there is data available to read this call returns with
                 the address and length of the data buffer the
                 client can safely read. This buffer may contain one or more
                 event packets. If no data is available then this call 
				 returns OK and sets *puiBufLen to 0 on exit.
				 Clients must pair this call with a ReleaseData call.
@Input          hDevData        Handle to connection/device object
@Output         ppBuf           Address of a pointer to a byte buffer. On exit
                                 it contains the address of buffer to read from
@Output         puiBufLen       Pointer to an integer. On exit it is the size
                                 of the data to read from the buffer
@Return         PVRSRV_ERROR:   for system error codes
*/ /***************************************************************************/
PVRSRV_ERROR RGXHWPerfAcquireData(
		IMG_HANDLE  hDevData,
		IMG_PBYTE*  ppBuf,
		IMG_UINT32* pui32BufLen);


/**************************************************************************/ /*!
@Function       RGXHWPerfReleaseData
@Description    Called after client has read the event data out of the buffer
                 retrieved from the Acquire Data call to release resources.
@Input          hDevData        Handle to connection/device object
@Return         PVRSRV_ERROR:   for system error codes
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR RGXHWPerfReleaseData(
		IMG_HANDLE hDevData);


#endif /* SUPPORT_KERNEL_HWPERF */


#endif /* __RGXAPI_KM_H__ */

/******************************************************************************
 End of file (rgxapi_km.h)
******************************************************************************/
