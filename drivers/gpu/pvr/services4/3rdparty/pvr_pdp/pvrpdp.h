/*************************************************************************/ /*!
@Title          PVRPDP kernel driver structures and prototypes
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
#ifndef __PVRPDP_H__
#define __PVRPDP_H__

#include "../pvr_pdp_common/pdpcommon.h"
//#include "vesagtf.h"

/* maximum number of supported display pixel formats */
#define PVRPDP_MAXFORMATS	(1)
/* maximum number of supported display pixel dimensions */
#define PVRPDP_MAXDIMS		(1)

/* maximum number of backbuffers to be included in a swapchain */
#ifdef USE_PRIMARY_SURFACE_IN_FLIP_CHAIN
#define PVRPDP_MAX_BACKBUFFERS (2)
#else
#define PVRPDP_MAX_BACKBUFFERS (3)
#endif

#if defined (PVRPDP_WIDTH)
#if !defined (PVRPDP_HEIGHT)
#error ERROR: PVRPDP_HEIGHT not defined
#endif
#else
/* attributes of the default display mode: */
#ifndef MODE_320_240
#define PVRPDP_WIDTH		(640)
#define PVRPDP_HEIGHT		(480)
#else
#define PVRPDP_WIDTH		(320)
#define PVRPDP_HEIGHT		(240)
#endif /* #ifndef MODE_320_240 */
#endif /* #if defined (PVRPDP_WIDTH) */

/* physical display attributes */
#define PDP_DISPLAY_WIDTH_MM	280
#define PDP_DISPLAY_HEIGHT_MM	210

#define PVRPDP_STRIDE		(PVRPDP_WIDTH * 4)
#define PVRPDP_PIXELFORMAT	(PVRSRV_PIXEL_FORMAT_ARGB8888)

#define PAGESIZE            0x1000UL
#define PAGEMASK            (~(PAGESIZE-1))
#define PAGEALIGN(addr)     (((addr)+PAGESIZE-1)&PAGEMASK)

typedef void *       PDP_HANDLE;

typedef enum tag_pdp_bool
{
	PDP_FALSE = 0,
	PDP_TRUE  = 1,
} PDP_BOOL, *PDP_PBOOL;

/* PVRPDP buffer structure */
typedef struct PVRPDP_BUFFER_TAG
{
	/* handle to swapchain */
	PDP_HANDLE					hSwapChain;

	/* members using IMG structures to minimise API function code */
	/* replace with own structures where necessary */
	/* system physical address of the buffer */
	IMG_SYS_PHYADDR				sSysAddr;
	/*
		device virtual address of the buffer
		(only meaningfully different if display
		has an MMU or some other address translation
		from the system physical address
	*/
	IMG_DEV_VIRTADDR			sDevVAddr;
	/* CPU virtual address */
	IMG_CPU_VIRTADDR			sCPUVAddr;
	/*
		ptr to synchronisation information structure
		associated with the buffer
	*/
	PVRSRV_SYNC_DATA			*psSyncData;

	/* ptr to next buffer in a swapchain */
	struct PVRPDP_BUFFER_TAG	*psNext;
} PVRPDP_BUFFER;


/* PVRPDP swapchain structure */
typedef struct PVRPDP_SWAPCHAIN_TAG
{
	/* number of buffers in swapchain */
	unsigned long   ulBufferCount;
	/* list of buffers in the swapchain */
	PVRPDP_BUFFER  *psBuffer;
} PVRPDP_SWAPCHAIN;

/* PVRPDP display mode information */
typedef struct PVRPDP_FBINFO_TAG
{
	/* pixel width of system/primary surface */
	unsigned long           ulWidth;
	/* pixel height of system/primary surface */
	unsigned long           ulHeight;
	/* byte stride of system/primary surface */
	unsigned long           ulByteStride;

	/* members using IMG structures to minimise API function code */
	/* replace with own structures where necessary */
	/* system physical address of system/primary surface */
	IMG_SYS_PHYADDR         sSysAddr;
	/* device virtual address of system/primary surface */
	IMG_DEV_VIRTADDR        sDevVAddr;
	/* cpu virtual address of system/primary surface */
	IMG_CPU_VIRTADDR        sCPUVAddr;
	/* pixelformat of system/primary surface */
	PVRSRV_PIXEL_FORMAT		ePixelFormat;
	/* system physical address of display device's registers */
	IMG_SYS_PHYADDR			sRegSysAddr;
	/* CPU virtual address of display device's registers */
	IMG_VOID				*pvRegs;
#if defined (SUPPORT_DYNAMIC_GTF_TIMING)
	/* CPU virtual address of TCF registers */
	IMG_VOID				*pvTCFRegs;
#endif
}PVRPDP_FBINFO;

/* flip item structure used for queuing of flips */
typedef struct PVRPDP_VSYNC_FLIP_ITEM_TAG
{
	/*
		command complete cookie to be passed to services
		command complete callback function
	*/
	PDP_HANDLE        hCmdComplete;
	/* device virtual address of surface to flip to	*/
	IMG_DEV_VIRTADDR* psDevVAddr;
	/* swap interval between flips */
	unsigned long     ulSwapInterval;
	/* is this item valid? */
	PDP_BOOL          bValid;
	/* has this item been flipped? */
	PDP_BOOL          bFlipped;
	/* has the flip cmd completed? */
	PDP_BOOL          bCmdCompleted;

} PVRPDP_VSYNC_FLIP_ITEM;

typedef struct tagHVTIMEREGS
{
	unsigned long ulHBackPorch;
	unsigned long ulHTotal;
	unsigned long ulHActiveStart;
	unsigned long ulHLeftBorder;
	unsigned long ulHRightBorder;
	unsigned long ulHFrontPorch;

	unsigned long ulVBackPorch;
	unsigned long ulVTotal;
	unsigned long ulVActiveStart;
	unsigned long ulVTopBorder;
	unsigned long ulVBottomBorder;
	unsigned long ulVFrontPorch;

	unsigned long ulClockFreq;
}
HVTIMEREGS, *PHVTIMEREGS;

/* kernel device information structure */
typedef struct PVRPDP_DEVINFO_TAG
{
	/* device ID assigned by services */
	unsigned int			uiDeviceID;

	/* system surface info */
	PVRPDP_BUFFER           sSystemBuffer;
	DISPLAY_FORMAT          sSysFormat;
	DISPLAY_DIMS            sSysDims;

	/* number of supported display formats */
	unsigned long           ulNumFormats;

	/* number of supported display dims */
	unsigned long           ulNumDims;

	/*
		handle for connection to kernel services
		- OS specific - may not be required
	*/
	PDP_HANDLE              hPVRServices;

	/* back buffer info */
	PVRPDP_BUFFER           asBackBuffers[PVRPDP_MAX_BACKBUFFERS];

	/* set of vsync flip items - enough for 1 outstanding flip per back buffer */
	PVRPDP_VSYNC_FLIP_ITEM  asVSyncFlips[PVRPDP_MAX_BACKBUFFERS];

	/* insert index for the internal queue of flip items */
	unsigned long           ulInsertIndex;

	/* remove index for the internal queue of flip items */
	unsigned long           ulRemoveIndex;

	/* display mode information */
	PVRPDP_FBINFO           sFBInfo;

	/* ref count on this structure */
	unsigned long           ulRefCount;

	/* only one swapchain supported by this device so hang it here */
	PVRPDP_SWAPCHAIN       *psSwapChain;

	/* jump table into PVR services */
	PVRSRV_DC_DISP2SRV_KMJTABLE		sPVRJTable;

	/* jump table into DC */
	PVRSRV_DC_SRV2DISP_KMJTABLE		sDCJTable;

	/* OS registration handle */
	PDP_HANDLE              hOSHandle;

	/* True if PVR is flushing its command queues */
	PDP_BOOL                bFlushCommands;

	/* members using IMG structures to minimise API function code */
	/* replace with own structures where necessary */

	/* misc. display information */
	DISPLAY_INFO            sDisplayInfo;
	/* list of supported display formats */
	DISPLAY_FORMAT          asDisplayFormatList[PVRPDP_MAXFORMATS];
	/* list of supported display formats */
	DISPLAY_DIMS            asDisplayDimList[PVRPDP_MAXDIMS];

	/* back buffer info */
	DISPLAY_FORMAT          sBackBufferFormat[PVRPDP_MAXFORMATS];

	/* Address of the surface being displayed */
	IMG_DEV_VIRTADDR        sDisplayDevVAddr;
	
	/* Offset of the first display surface after the start of device mem */
	unsigned long 			ulSysSurfaceOffset;

	/* VESA GTF timing table */
	HVTIMEREGS              sHVTRegs;

} PVRPDP_DEVINFO;

/*!
 *****************************************************************************
 * Error values
 *****************************************************************************/
typedef enum _PDP_ERROR_
{
	PDP_OK                              =  0,
	PDP_ERROR_GENERIC                   =  1,
	PDP_ERROR_OUT_OF_MEMORY             =  2,
	PDP_ERROR_TOO_FEW_BUFFERS           =  3,
	PDP_ERROR_INVALID_PARAMS            =  4,
	PDP_ERROR_INIT_FAILURE              =  5,
	PDP_ERROR_CANT_REGISTER_CALLBACK    =  6,
	PDP_ERROR_INVALID_DEVICE            =  7,
	PDP_ERROR_DEVICE_REGISTER_FAILED    =  8
} PDP_ERROR;


#ifndef UNREFERENCED_PARAMETER
#define	UNREFERENCED_PARAMETER(param) (param) = (param)
#endif


/* prototypes for init/de-init functions */
PDP_ERROR Init(void);
PDP_ERROR Deinit(void);

/* OS Specific APIs */
PDP_ERROR OpenPVRServices  (PDP_HANDLE *phPVRServices);
PDP_ERROR ClosePVRServices (PDP_HANDLE hPVRServices);
void *AllocKernelMem(unsigned long ulSize);
void FreeKernelMem  (void *pvMem);
void WriteReg(PVRPDP_DEVINFO *psDevInfo, unsigned long ulOffset, unsigned long ulValue);
unsigned long ReadReg(PVRPDP_DEVINFO *psDevInfo, unsigned long ulOffset);
void WriteTCFReg(PVRPDP_DEVINFO *psDevInfo, unsigned long ulOffset, unsigned long ulValue);
unsigned long ReadTCFReg(PVRPDP_DEVINFO *psDevInfo, unsigned long ulOffset);
void *MapPhysAddr(IMG_SYS_PHYADDR sSysAddr, unsigned long ulSize);
void UnMapPhysAddr(void *pvAddr, unsigned long ul2Size);
PDP_ERROR InstallVsyncISR (PVRPDP_DEVINFO *psDevInfo);
PDP_ERROR UninstallVsyncISR (PVRPDP_DEVINFO *psDevInfo);
PDP_ERROR GetLibFuncAddr (PDP_HANDLE hExtDrv, char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable);
PDP_ERROR OSGetDeviceAddresses(unsigned long *pui32RegBaseAddr, unsigned long *pui32MemBaseAddr, unsigned long *pulSysSurfaceOffset);
PDP_ERROR OSRegisterDevice(PVRPDP_DEVINFO *psDevInfo);
PDP_ERROR OSUnregisterDevice(PVRPDP_DEVINFO *psDevInfo);
#if defined(SYS_USING_INTERRUPTS)
IMG_BOOL PDPVSyncISR(IMG_VOID *pvDevInfo);
#endif
#if defined (SUPPORT_DYNAMIC_GTF_TIMING)
void PDPReleaseThreadQuanta (void);
unsigned long PDPClockus (void);
void  PDPWaitus (unsigned long ulTimeus);
#endif

#if !defined(__linux__)
unsigned long PCIReadDword(unsigned long ulBus, unsigned long ulDev, unsigned long ulFunc, unsigned long ulReg);
void PCIWriteDword(unsigned long ulBus, unsigned long ulDev, unsigned long ulFunc, unsigned long ulReg, unsigned long ulValue);
#endif

/* PVRPDP hardware details */
#define PVRPDP_STR1SURF			0x0000
#define PVRPDP_STR1ADDRCTRL		0x0004
#define PVRPDP_STR1POSN			0x0008
#define PVRPDP_MEMCTRL			0x000C
#define PVRPDP_STRCTRL			0x0010
#define PVRPDP_SYNCTRL			0x0014
#define PVRPDP_BORDCOL			0x0018
#define PVRPDP_UPDCTRL			0x001C
#define PVRPDP_HSYNC1			0x0020
#define PVRPDP_HSYNC2			0x0024
#define PVRPDP_HSYNC3			0x0028
#define PVRPDP_VSYNC1			0x002C
#define PVRPDP_VSYNC2			0x0030
#define PVRPDP_VSYNC3			0x0034
#define PVRPDP_HDECTRL			0x0038
#define PVRPDP_VDECTRL			0x003C
#define PVRPDP_VEVENT			0x0040
#define PVRPDP_ODMASK			0x0044
#define PVRPDP_INTSTATUS		0x0048
#define PVRPDP_INTENABLE		0x004C
#define PVRPDP_INTCLEAR			0x0050
#define PVRPDP_INTCTRL			0x0054

#define PVRPDP_STR1SURF_STRFORMAT_SHIFT			24
#define PVRPDP_STR1SURF_FORMAT_ARGB8888			0xE

#define PVRPDP_STR1SURF_STRWIDTH_SHIFT			11
#define PVRPDP_STR1SURF_STRHEIGHT_SHIFT			0

#define PVRPDP_STR1ADDRCTRL_ADDR_ALIGNSHIFT		4
#define PVRPDP_STR1ADDRCTRL_STREAMENABLE		0x80000000

#define PVRPDP_STR1POSN_STRIDE_ALIGNSHIFT		4

#define PVRPDP_INTS_VBLNK0_SHIFT				2
#define PVRPDP_INTE_VBLNK0_SHIFT				2
#define PVRPDP_INTCLR_VBLNK0_SHIFT				2
#define PVRPDP_INTS_VBLNK1_SHIFT				3
#define PVRPDP_INTE_VBLNK1_SHIFT				3
#define PVRPDP_INTCLR_VBLNK1_SHIFT				3

void InitPDPRegisters(PVRPDP_DEVINFO *psDevInfo);

#if defined (ENABLE_DISPLAY_MODE_TRACKING)
PDP_ERROR Shadow_Desktop_Resolution(PVRPDP_DEVINFO	*psDevInfo);
PDP_ERROR OpenMiniport(void);
PDP_ERROR CloseMiniport(void);
#endif

#endif /* __PVRPDP_H__ */

/******************************************************************************
 End of file (pvrpdp.h)
******************************************************************************/

