/*************************************************************************/ /*!
@File
@Title          RGX debug header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX debugging functions
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

#if !defined(RGXDEBUG_H)
#define RGXDEBUG_H

#include "pvrsrv_error.h"
#include "img_types.h"
#include "device.h"
#include "pvr_notifier.h"
#include "pvrsrv.h"
#include "rgxdevice.h"

/**
 * Debug utility macro for printing FW IRQ count and Last sampled IRQ count in
 * LISR for each RGX FW thread.
 * Macro takes pointer to PVRSRV_RGXDEV_INFO as input.
 */

#if defined(RGX_FW_IRQ_OS_COUNTERS)
#define for_each_irq_cnt(ui32idx) \
	for (ui32idx = 0; ui32idx < RGX_NUM_OS_SUPPORTED; ui32idx++)

#define get_irq_cnt_val(ui32Dest, ui32idx, psRgxDevInfo) \
	do { \
		extern const IMG_UINT32 gaui32FwOsIrqCntRegAddr[RGXFW_MAX_NUM_OS]; \
		ui32Dest = PVRSRV_VZ_MODE_IS(GUEST) ? 0 : OSReadHWReg32((psRgxDevInfo)->pvRegsBaseKM, gaui32FwOsIrqCntRegAddr[ui32idx]); \
	} while (false)

#define MSG_IRQ_CNT_TYPE "OS"

#else

#define for_each_irq_cnt(ui32idx) \
	for (ui32idx = 0; ui32idx < RGXFW_THREAD_NUM; ui32idx++)

#define get_irq_cnt_val(ui32Dest, ui32idx, psRgxDevInfo) \
	ui32Dest = (psRgxDevInfo)->psRGXFWIfFwOsData->aui32InterruptCount[ui32idx]

#define MSG_IRQ_CNT_TYPE "Thread"
#endif /* RGX_FW_IRQ_OS_COUNTERS */

static inline void RGXDEBUG_PRINT_IRQ_COUNT(PVRSRV_RGXDEV_INFO* psRgxDevInfo)
{
#if defined(PVRSRV_NEED_PVR_DPF) && defined(DEBUG)
	IMG_UINT32 ui32idx;

	for_each_irq_cnt(ui32idx)
	{
		IMG_UINT32 ui32IrqCnt;

		get_irq_cnt_val(ui32IrqCnt, ui32idx, psRgxDevInfo);

		PVR_DPF((DBGPRIV_VERBOSE, MSG_IRQ_CNT_TYPE
		         " %u FW IRQ count = %u", ui32idx, ui32IrqCnt));

#if defined(RGX_FW_IRQ_OS_COUNTERS)
		if (ui32idx == RGXFW_HOST_OS)
#endif
		{
			PVR_DPF((DBGPRIV_VERBOSE, "Last sampled IRQ count in LISR = %u",
			        (psRgxDevInfo)->aui32SampleIRQCount[ui32idx]));
		}
	}
#endif /* PVRSRV_NEED_PVR_DPF */
}

/*!
*******************************************************************************

 @Function	RGXDumpRGXRegisters

 @Description

 Dumps an extensive list of RGX registers required for debugging

 @Input pfnDumpDebugPrintf  - Optional replacement print function
 @Input pvDumpDebugFile     - Optional file identifier to be passed to the
                              'printf' function if required
 @Input psDevInfo           - RGX device info

 @Return PVRSRV_ERROR         PVRSRV_OK on success, error code otherwise

******************************************************************************/
PVRSRV_ERROR RGXDumpRGXRegisters(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
								 void *pvDumpDebugFile,
								 PVRSRV_RGXDEV_INFO *psDevInfo);

/*!
*******************************************************************************

 @Function	RGXDumpFirmwareTrace

 @Description Dumps the decoded version of the firmware trace buffer.

 Dump useful debugging info

 @Input pfnDumpDebugPrintf  - Optional replacement print function
 @Input pvDumpDebugFile     - Optional file identifier to be passed to the
                              'printf' function if required
 @Input psDevInfo           - RGX device info

 @Return   void

******************************************************************************/
void RGXDumpFirmwareTrace(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				PVRSRV_RGXDEV_INFO  *psDevInfo);

#if defined(SUPPORT_POWER_VALIDATION_VIA_DEBUGFS)
void RGXDumpPowerMonitoring(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				PVRSRV_RGXDEV_INFO  *psDevInfo);
#endif

/*!
*******************************************************************************

 @Function	RGXReadWithSP

 @Description

 Reads data from a memory location (FW memory map) using the META Slave Port

 @Input  psDevInfo  - Pointer to RGX DevInfo to be used while reading
 @Input  ui32FWAddr - 32 bit FW address
 @Input  pui32Value - When the read is successful, value at above FW address
                      is returned at this location

 @Return PVRSRV_ERROR PVRSRV_OK if read success, error code otherwise.
******************************************************************************/
PVRSRV_ERROR RGXReadWithSP(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32FWAddr, IMG_UINT32 *pui32Value);

/*!
*******************************************************************************

 @Function	RGXWriteWithSP

 @Description

 Writes data to a memory location (FW memory map) using the META Slave Port

 @Input  psDevInfo  - Pointer to RGX DevInfo to be used while writing
 @Input  ui32FWAddr - 32 bit FW address

 @Input  ui32Value  - 32 bit Value to write

 @Return PVRSRV_ERROR PVRSRV_OK if write success, error code otherwise.
******************************************************************************/
PVRSRV_ERROR RGXWriteWithSP(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32FWAddr, IMG_UINT32 ui32Value);

#if defined(SUPPORT_EXTRA_METASP_DEBUG)
/*!
*******************************************************************************

 @Function     ValidateFWOnLoad

 @Description  Compare the Firmware image as seen from the CPU point of view
               against the same memory area as seen from the META point of view
               after first power up.

 @Input        psDevInfo - Device Info

 @Return       PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR ValidateFWOnLoad(PVRSRV_RGXDEV_INFO *psDevInfo);
#endif

/*!
*******************************************************************************

 @Function	RGXDumpRGXDebugSummary

 @Description

 Dump a summary in human readable form with the RGX state

 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input pvDumpDebugFile      - Optional file identifier to be passed to the
                               'printf' function if required
 @Input psDevInfo	     - RGX device info
 @Input bRGXPoweredON        - IMG_TRUE if RGX device is on

 @Return   void

******************************************************************************/
void RGXDumpRGXDebugSummary(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					PVRSRV_RGXDEV_INFO *psDevInfo,
					IMG_BOOL bRGXPoweredON);

/*!
*******************************************************************************

 @Function RGXDebugInit

 @Description

 Setup debug requests, calls into PVRSRVRegisterDbgRequestNotify

 @Input          psDevInfo            RGX device info
 @Return         PVRSRV_ERROR         PVRSRV_OK on success otherwise an error

******************************************************************************/
PVRSRV_ERROR RGXDebugInit(PVRSRV_RGXDEV_INFO *psDevInfo);

/*!
*******************************************************************************

 @Function RGXDebugDeinit

 @Description

 Remove debug requests, calls into PVRSRVUnregisterDbgRequestNotify

 @Output         phNotify             Points to debug notifier handle
 @Return         PVRSRV_ERROR         PVRSRV_OK on success otherwise an error

******************************************************************************/
PVRSRV_ERROR RGXDebugDeinit(PVRSRV_RGXDEV_INFO *psDevInfo);

#endif /* RGXDEBUG_H */
