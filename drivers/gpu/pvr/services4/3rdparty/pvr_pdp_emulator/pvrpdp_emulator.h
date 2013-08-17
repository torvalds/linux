/*************************************************************************/ /*!
@Title          PVRPDP_EMULATOR kernel driver structures and prototypes
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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
#ifndef __PVRPDP_EMULATOR_H__
#define __PVRPDP_EMULATOR_H__

#include "../pvr_pdp_common/pdpcommon.h"

/* System specific poll/timeout details */
#define MAX_HW_TIME_US			(500000)
#define WAIT_TRY_COUNT			(10000)

#define PVRPDP_EMULATOR_MAXFORMATS	(1)
#define PVRPDP_EMULATOR_MAXDIMS		(1)
#define PVRPDP_EMULATOR_MAX_BACKBUFFERS (3)

#if defined (PVRPDP_EMULATOR_WIDTH)
#if !defined (PVRPDP_EMULATOR_HEIGHT)
#error ERROR: PVRPDP_EMULATOR_HEIGHT not defined
#endif
#else
/* attributes of the default display mode: */
#define PVRPDP_EMULATOR_WIDTH		(640)
#define PVRPDP_EMULATOR_HEIGHT		(480)
#endif /* #if defined (PVRPDP_EMULATOR_WIDTH) */

#define PVRPDP_EMULATOR_STRIDE		(PVRPDP_EMULATOR_WIDTH * 4)
#define PVRPDP_EMULATOR_PIXELFORMAT	(PVRSRV_PIXEL_FORMAT_ARGB8888)

#if defined(PVRPDP_GET_BUFFER_DIMENSIONS)
#define	PVRPDP_DEPTH_BITS_PER_BYTE	8

#define	pvrpdp_byte_depth_from_bit_depth(bit_depth) (((IMG_UINT32)(bit_depth) + PVRPDP_DEPTH_BITS_PER_BYTE - 1)/PVRPDP_DEPTH_BITS_PER_BYTE)
#define	pvrpdp_bit_depth_from_byte_depth(byte_depth) ((IMG_UINT32)(byte_depth) * PVRPDP_DEPTH_BITS_PER_BYTE)
#define pvrpdp_roundup_bit_depth(bd) pvrpdp_bit_depth_from_byte_depth(pvrpdp_byte_depth_from_bit_depth(bd))

#define	pvrpdp_byte_stride(width, bit_depth) ((IMG_UINT32)(width) * pvrpdp_byte_depth_from_bit_depth(bit_depth))

IMG_BOOL GetBufferDimensions(IMG_UINT32 *pui32Width, IMG_UINT32 *pui32Height, PVRSRV_PIXEL_FORMAT *pePixelFormat, IMG_UINT32 *pui32Stride);
#endif

/* PVRPDP_EMULATOR buffer structure */
typedef struct PVRPDP_EMULATOR_BUFFER_TAG
{
	IMG_HANDLE					hSwapChain;
	IMG_SYS_PHYADDR				sSysAddr;
	IMG_DEV_VIRTADDR			sDevVAddr;
	IMG_CPU_VIRTADDR			sCPUVAddr;
	PVRSRV_SYNC_DATA			*psSyncData;	
	struct PVRPDP_EMULATOR_BUFFER_TAG	*psNext;
} PVRPDP_EMULATOR_BUFFER;


/* PVRPDP_EMULATOR buffer structure */
typedef struct PVRPDP_EMULATOR_SWAPCHAIN_TAG
{
	IMG_UINT32 ui32BufferCount;
	PVRPDP_EMULATOR_BUFFER *psBuffer;
} PVRPDP_EMULATOR_SWAPCHAIN;


typedef struct PVRPDP_EMULATOR_FBINFO_TAG
{
	IMG_SYS_PHYADDR			sSysAddr;
	IMG_CPU_VIRTADDR		sCPUVAddr;
	IMG_UINT32				ui32Width;
	IMG_UINT32				ui32Height;
	IMG_UINT32				ui32ByteStride;
	PVRSRV_PIXEL_FORMAT		ePixelFormat;
	IMG_SYS_PHYADDR			sRegSysAddr;
	IMG_SYS_PHYADDR			sSOCSysAddr;
	IMG_VOID				*pvRegs;
	IMG_VOID				*pvSOCRegs;
}PVRPDP_EMULATOR_FBINFO;


/* kernel device information structure */
typedef struct PVRPDP_EMULATOR_DEVINFO_TAG
{
	/* device ID assigned by services */
	unsigned int			uiDeviceID;
	DISPLAY_INFO			sDisplayInfo;

	/* system surface info */
	PVRPDP_EMULATOR_BUFFER			sSystemBuffer;
	DISPLAY_FORMAT 			sSysFormat;
	DISPLAY_DIMS			sSysDims;

	/* number of supported display formats */
	IMG_UINT32				ui32NumFormats;

	/* list of supported display formats */
	DISPLAY_FORMAT			asDisplayFormatList[PVRPDP_EMULATOR_MAXFORMATS];
	
	/* number of supported display dims */
	IMG_UINT32				ui32NumDims;
	
	/* list of supported display formats */
	DISPLAY_DIMS			asDisplayDimList[PVRPDP_EMULATOR_MAXDIMS];	
	
	/* jump table into PVR services */
	PVRSRV_DC_DISP2SRV_KMJTABLE		sPVRJTable;
	
	/* jump table into DC */
	PVRSRV_DC_SRV2DISP_KMJTABLE		sDCJTable;

	/* 
		handle for connection to kernel services
		- OS specific - may not be required
	*/
	IMG_HANDLE				hPVRServices;

	/* back buffer info */
	PVRPDP_EMULATOR_BUFFER			asBackBuffers[PVRPDP_EMULATOR_MAX_BACKBUFFERS];
	DISPLAY_FORMAT 			sBackBufferFormat[PVRPDP_EMULATOR_MAXFORMATS]; 

	/* fb info structure */
	PVRPDP_EMULATOR_FBINFO			sFBInfo;

	/* ref count */
	IMG_UINT32				ui32RefCount;
	
	PVRPDP_EMULATOR_SWAPCHAIN		*psSwapChain;

}  PVRPDP_EMULATOR_DEVINFO;

#if !defined(__linux__)
IMG_UINT32 PCIReadDword(IMG_UINT32 ui32Bus, IMG_UINT32 ui32Dev, IMG_UINT32 ui32Func, IMG_UINT32 ui32Reg);
IMG_VOID PCIWriteDword(IMG_UINT32 ui32Bus, IMG_UINT32 ui32Dev, IMG_UINT32 ui32Func, IMG_UINT32 ui32Reg, IMG_UINT32 ui32Value);
#endif

/*
	Give ourselves 32mb to play around in. It must be after services localmem
	region to avoid conflicts.
	
	NOTE: this value must be kept in sync with SYS_LOCALMEM_FOR_SGX_RESERVE_SIZE
*/
#define PVRPDP_EMULATOR_SYSSURFACE_SIZE (32 * 1024 * 1024)

#if !defined(SGX545) || defined(EMULATE_ATLAS)
#define PVRPDP_EMULATOR_SYSSURFACE_OFFSET ((256 * 1024 * 1024) - PVRPDP_EMULATOR_SYSSURFACE_SIZE) /* Must be after services localmem region */
#else
#define PVRPDP_EMULATOR_SYSSURFACE_OFFSET ((512 * 1024 * 1024) - PVRPDP_EMULATOR_SYSSURFACE_SIZE) /* Must be after services localmem region */
#endif


PVRSRV_ERROR Init(IMG_VOID);
PVRSRV_ERROR Deinit(IMG_VOID);
PVRSRV_ERROR SetMode(PVRPDP_EMULATOR_DEVINFO		*psDevInfo,
					IMG_SYS_PHYADDR		sSysBusAddr,
					DISPLAY_MODE_INFO	*psModeInfo);

#define JDBASE_ADDR                   0x0000

// Original register compatable with driver
#define JDISPLAY_FRAME_BASE_ADD       (JDBASE_ADDR+0x0000)
#define JDISPLAY_FRAME_STRIDE_ADD     (JDBASE_ADDR+0x0004)
#define JDISPLAY_FBCTRL_ADD           (JDBASE_ADDR+0x0008)
#define JDISPLAY_SPGKICK_ADD          (JDBASE_ADDR+0x000C)
#define JDISPLAY_SPGSTATUS_ADD        (JDBASE_ADDR+0x0010)

#define JDISPLAY_FPGA_SOFT_RESETS_ADD (JDBASE_ADDR+0x0014)
//#define JDISPLAY_FPGA_IRQ_STAT_ADD    (JDBASE_ADDR+0x0018)
//#define JDISPLAY_FPGA_IRQ_MASK_ADD    (JDBASE_ADDR+0x001C)
//#define JDISPLAY_FPGA_IRQ_CLEAR_ADD   (JDBASE_ADDR+0x0020)
#define JDISPLAY_TRAS_ADD             (JDBASE_ADDR+0x0024)
#define JDISPLAY_TRP_ADD              (JDBASE_ADDR+0x0028)
#define JDISPLAY_TRCD_ADD             (JDBASE_ADDR+0x002C)
#define JDISPLAY_TRC_ADD              (JDBASE_ADDR+0x0030)
#define JDISPLAY_TREF_ADD             (JDBASE_ADDR+0x0034)
#define JDISPLAY_RFEN_REG_ADD         (JDBASE_ADDR+0x0038)
#define JDISPLAY_MEM_CONTROL_ADD      (JDBASE_ADDR+0x003C)

#define JDISPLAY_CURSOR_EN_ADD        (JDBASE_ADDR+0x0040)
#define JDISPLAY_CURSOR_XY_ADD        (JDBASE_ADDR+0x0044)
#define JDISPLAY_CURSOR_ADDR_ADD      (JDBASE_ADDR+0x0048)
#define JDISPLAY_CURSOR_DATA_ADD      (JDBASE_ADDR+0x004C)

//***********************
// New registers
//***********************
#define JDISPLAY_FRAME_SIZE_ADD       (JDBASE_ADDR+0x0050)
#define JDISPLAY_CURSOR_POSI_ADD      (JDBASE_ADDR+0x0058)
#define JDISPLAY_CURSRAM_CTRL_ADD     (JDBASE_ADDR+0x005C)
#define JDISPLAY_STATUS_ADD           (JDBASE_ADDR+0x0060)
#define JDISPLAY_GPIOREG_IN_ADD       (JDBASE_ADDR+0x0064)
#define JDISPLAY_GPIOREG_OUT_ADD      (JDBASE_ADDR+0x0068)
// jdisplay control register is the mirror of the signal in the top level of lcdfeed
#define JDISPLAY_CONTROL_ADD          (JDBASE_ADDR+0x0054)
// the next six register brakedown jdisplay control in to correct register chunks
#define JDISPLAY_FRAME_PACK_ADD       (JDBASE_ADDR+0x00B4)
#define JDISPLAY_STATIC_COLOUR_ADD    (JDBASE_ADDR+0x00B8)
#define JDISPLAY_MISC_CTRL_ADD        (JDBASE_ADDR+0x00BC)
#define JDISPLAY_SYNC_TRIG_ADD        (JDBASE_ADDR+0x00C0)
#define JDISPLAY_START_SYNC_ADD       (JDBASE_ADDR+0x00C4)
#define JDISPLAY_FETCH_FRAME_ADD      (JDBASE_ADDR+0x00C8)

// Syncgen timing register
#define JDISPLAY_TIM_VBPS_ADD         (JDBASE_ADDR+0x0080)
#define JDISPLAY_TIM_VTBS_ADD         (JDBASE_ADDR+0x0084)
#define JDISPLAY_TIM_VAS_ADD          (JDBASE_ADDR+0x0088)
#define JDISPLAY_TIM_VBBS_ADD         (JDBASE_ADDR+0x008C)
#define JDISPLAY_TIM_VFPS_ADD         (JDBASE_ADDR+0x0090)
#define JDISPLAY_TIM_VT_ADD           (JDBASE_ADDR+0x0094)
#define JDISPLAY_TIM_HBPS_ADD         (JDBASE_ADDR+0x0098)
#define JDISPLAY_TIM_HLBS_ADD         (JDBASE_ADDR+0x009C)
#define JDISPLAY_TIM_HAS_ADD          (JDBASE_ADDR+0x00A0)
#define JDISPLAY_TIM_HRBS_ADD         (JDBASE_ADDR+0x00A4)
#define JDISPLAY_TIM_HFPS_ADD         (JDBASE_ADDR+0x00A8)
#define JDISPLAY_TIM_HT_ADD           (JDBASE_ADDR+0x00AC)
#define JDISPLAY_TIM_VOFF_ADD         (JDBASE_ADDR+0x00B0)

/* OS Specific APIs */
PVRSRV_ERROR OpenPVRServices  (IMG_HANDLE *phPVRServices);
PVRSRV_ERROR ClosePVRServices (IMG_HANDLE hPVRServices);
IMG_VOID *AllocKernelMem(IMG_UINT32 ui32Size);
IMG_VOID FreeKernelMem  (IMG_VOID *pvMem);
IMG_VOID WriteSOCReg(PVRPDP_EMULATOR_DEVINFO *psDevInfo, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value);
IMG_UINT32 ReadSOCReg(PVRPDP_EMULATOR_DEVINFO *psDevInfo, IMG_UINT32 ui32Offset);
IMG_VOID *MapPhysAddr(IMG_SYS_PHYADDR sSysAddr, IMG_UINT32 ui32Size);
IMG_VOID UnMapPhysAddr(IMG_VOID *pvAddr, IMG_UINT32 ui32Size);
PVRSRV_ERROR InstallVsyncISR (PVRPDP_EMULATOR_DEVINFO *psDevInfo);
PVRSRV_ERROR UninstallVsyncISR (PVRPDP_EMULATOR_DEVINFO *psDevInfo);
PVRSRV_ERROR GetLibFuncAddr (IMG_HANDLE hExtDrv, IMG_CHAR *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable);
PVRSRV_ERROR OSGetDeviceAddresses(IMG_UINT32 *pui32RegBaseAddr, IMG_UINT32 *pui32SOCBaseAddr, IMG_UINT32 *pui32MemBaseAddr, IMG_UINT32 *pui32MemSize);
IMG_VOID PDPVSyncFlip(PVRPDP_EMULATOR_DEVINFO *psDevInfo);

#endif /* __PVRPDP_EMULATOR_H__ */

/******************************************************************************
 End of file (pvrpdp.h)
******************************************************************************/

