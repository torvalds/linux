/*************************************************************************/ /*!
@Title          Display device class API structures and prototypes
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides display device class API structures and prototypes
                for kernel services to kernel 3rd party display.
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

#if !defined (__KERNELDISPLAY_H__)
#define __KERNELDISPLAY_H__

#if defined (__cplusplus)
extern "C" {
#endif

typedef PVRSRV_ERROR (*PFN_OPEN_DC_DEVICE)(IMG_UINT32, IMG_HANDLE*, PVRSRV_SYNC_DATA*);
typedef PVRSRV_ERROR (*PFN_CLOSE_DC_DEVICE)(IMG_HANDLE);
typedef PVRSRV_ERROR (*PFN_ENUM_DC_FORMATS)(IMG_HANDLE, IMG_UINT32*, DISPLAY_FORMAT*);
typedef PVRSRV_ERROR (*PFN_ENUM_DC_DIMS)(IMG_HANDLE,
										 DISPLAY_FORMAT*,
										 IMG_UINT32*,
										 DISPLAY_DIMS*);
typedef PVRSRV_ERROR (*PFN_GET_DC_SYSTEMBUFFER)(IMG_HANDLE, IMG_HANDLE*);
typedef PVRSRV_ERROR (*PFN_GET_DC_INFO)(IMG_HANDLE, DISPLAY_INFO*);
typedef PVRSRV_ERROR (*PFN_CREATE_DC_SWAPCHAIN)(IMG_HANDLE,
												IMG_UINT32, 
												DISPLAY_SURF_ATTRIBUTES*, 
												DISPLAY_SURF_ATTRIBUTES*,
												IMG_UINT32, 
												PVRSRV_SYNC_DATA**,
												IMG_UINT32,
												IMG_HANDLE*, 
												IMG_UINT32*);
typedef PVRSRV_ERROR (*PFN_DESTROY_DC_SWAPCHAIN)(IMG_HANDLE, 
												 IMG_HANDLE);
typedef PVRSRV_ERROR (*PFN_SET_DC_DSTRECT)(IMG_HANDLE, IMG_HANDLE, IMG_RECT*);
typedef PVRSRV_ERROR (*PFN_SET_DC_SRCRECT)(IMG_HANDLE, IMG_HANDLE, IMG_RECT*);
typedef PVRSRV_ERROR (*PFN_SET_DC_DSTCK)(IMG_HANDLE, IMG_HANDLE, IMG_UINT32);
typedef PVRSRV_ERROR (*PFN_SET_DC_SRCCK)(IMG_HANDLE, IMG_HANDLE, IMG_UINT32);
typedef PVRSRV_ERROR (*PFN_GET_DC_BUFFERS)(IMG_HANDLE,
										   IMG_HANDLE,
										   IMG_UINT32*,
										   IMG_HANDLE*);
typedef PVRSRV_ERROR (*PFN_SWAP_TO_DC_BUFFER)(IMG_HANDLE,
											  IMG_HANDLE,
											  IMG_UINT32,
											  IMG_HANDLE,
											  IMG_UINT32,
											  IMG_RECT*);
typedef IMG_VOID (*PFN_QUERY_SWAP_COMMAND_ID)(IMG_HANDLE, IMG_HANDLE, IMG_HANDLE, IMG_HANDLE, IMG_UINT16*, IMG_BOOL*);
typedef IMG_VOID (*PFN_SET_DC_STATE)(IMG_HANDLE, IMG_UINT32);

/*
	Function table for SRVKM->DISPLAY
*/
typedef struct PVRSRV_DC_SRV2DISP_KMJTABLE_TAG
{
	IMG_UINT32						ui32TableSize;
	PFN_OPEN_DC_DEVICE				pfnOpenDCDevice;
	PFN_CLOSE_DC_DEVICE				pfnCloseDCDevice;
	PFN_ENUM_DC_FORMATS				pfnEnumDCFormats;
	PFN_ENUM_DC_DIMS				pfnEnumDCDims;
	PFN_GET_DC_SYSTEMBUFFER			pfnGetDCSystemBuffer;
	PFN_GET_DC_INFO					pfnGetDCInfo;
	PFN_GET_BUFFER_ADDR				pfnGetBufferAddr;
	PFN_CREATE_DC_SWAPCHAIN			pfnCreateDCSwapChain;
	PFN_DESTROY_DC_SWAPCHAIN		pfnDestroyDCSwapChain;
	PFN_SET_DC_DSTRECT				pfnSetDCDstRect;
	PFN_SET_DC_SRCRECT				pfnSetDCSrcRect;
	PFN_SET_DC_DSTCK				pfnSetDCDstColourKey;
	PFN_SET_DC_SRCCK				pfnSetDCSrcColourKey;
	PFN_GET_DC_BUFFERS				pfnGetDCBuffers;
	PFN_SWAP_TO_DC_BUFFER			pfnSwapToDCBuffer;
	PFN_SET_DC_STATE				pfnSetDCState;
	PFN_QUERY_SWAP_COMMAND_ID		pfnQuerySwapCommandID;

} PVRSRV_DC_SRV2DISP_KMJTABLE;

/* ISR callback pfn prototype */
typedef IMG_BOOL (*PFN_ISR_HANDLER)(IMG_VOID*);

/*
	functions exported by kernel services for use by 3rd party kernel display class device driver
*/
typedef PVRSRV_ERROR (*PFN_DC_REGISTER_DISPLAY_DEV)(PVRSRV_DC_SRV2DISP_KMJTABLE*, IMG_UINT32*);
typedef PVRSRV_ERROR (*PFN_DC_REMOVE_DISPLAY_DEV)(IMG_UINT32);
typedef PVRSRV_ERROR (*PFN_DC_OEM_FUNCTION)(IMG_UINT32, IMG_VOID*, IMG_UINT32, IMG_VOID*, IMG_UINT32);
typedef PVRSRV_ERROR (*PFN_DC_REGISTER_COMMANDPROCLIST)(IMG_UINT32, PPFN_CMD_PROC,IMG_UINT32[][2], IMG_UINT32);
typedef PVRSRV_ERROR (*PFN_DC_REMOVE_COMMANDPROCLIST)(IMG_UINT32, IMG_UINT32);
typedef IMG_VOID (*PFN_DC_CMD_COMPLETE)(IMG_HANDLE, IMG_BOOL);
typedef PVRSRV_ERROR (*PFN_DC_REGISTER_SYS_ISR)(PFN_ISR_HANDLER, IMG_VOID*, IMG_UINT32, IMG_UINT32);
typedef PVRSRV_ERROR (*PFN_DC_REGISTER_POWER)(IMG_UINT32, PFN_PRE_POWER, PFN_POST_POWER,
											  PFN_PRE_CLOCKSPEED_CHANGE, PFN_POST_CLOCKSPEED_CHANGE,
											  IMG_HANDLE, PVRSRV_DEV_POWER_STATE, PVRSRV_DEV_POWER_STATE);

typedef struct _PVRSRV_KERNEL_MEM_INFO_* PDC_MEM_INFO;

typedef PVRSRV_ERROR (*PFN_DC_MEMINFO_GET_CPU_VADDR)(PDC_MEM_INFO, IMG_CPU_VIRTADDR *pVAddr);
typedef PVRSRV_ERROR (*PFN_DC_MEMINFO_GET_CPU_PADDR)(PDC_MEM_INFO, IMG_SIZE_T uByteOffset, IMG_CPU_PHYADDR *pPAddr);
typedef PVRSRV_ERROR (*PFN_DC_MEMINFO_GET_BYTE_SIZE)(PDC_MEM_INFO, IMG_SIZE_T *uByteSize);
typedef IMG_BOOL (*PFN_DC_MEMINFO_IS_PHYS_CONTIG)(PDC_MEM_INFO);

/*
	Function table for DISPLAY->SRVKM
*/
typedef struct PVRSRV_DC_DISP2SRV_KMJTABLE_TAG
{
	IMG_UINT32						ui32TableSize;
	PFN_DC_REGISTER_DISPLAY_DEV		pfnPVRSRVRegisterDCDevice;
	PFN_DC_REMOVE_DISPLAY_DEV		pfnPVRSRVRemoveDCDevice;
	PFN_DC_OEM_FUNCTION				pfnPVRSRVOEMFunction;
	PFN_DC_REGISTER_COMMANDPROCLIST	pfnPVRSRVRegisterCmdProcList;
	PFN_DC_REMOVE_COMMANDPROCLIST	pfnPVRSRVRemoveCmdProcList;
	PFN_DC_CMD_COMPLETE				pfnPVRSRVCmdComplete;
	PFN_DC_REGISTER_SYS_ISR			pfnPVRSRVRegisterSystemISRHandler;
	PFN_DC_REGISTER_POWER			pfnPVRSRVRegisterPowerDevice;
	PFN_DC_CMD_COMPLETE				pfnPVRSRVFreeCmdCompletePacket;
	PFN_DC_MEMINFO_GET_CPU_VADDR	pfnPVRSRVDCMemInfoGetCpuVAddr;
	PFN_DC_MEMINFO_GET_CPU_PADDR	pfnPVRSRVDCMemInfoGetCpuPAddr;
	PFN_DC_MEMINFO_GET_BYTE_SIZE	pfnPVRSRVDCMemInfoGetByteSize;
	PFN_DC_MEMINFO_IS_PHYS_CONTIG	pfnPVRSRVDCMemInfoIsPhysContig;

} PVRSRV_DC_DISP2SRV_KMJTABLE, *PPVRSRV_DC_DISP2SRV_KMJTABLE;


typedef struct DISPLAYCLASS_FLIP_COMMAND_TAG
{
	/* Ext Device Handle */
	IMG_HANDLE hExtDevice;

	/* Ext SwapChain Handle */
	IMG_HANDLE hExtSwapChain;

	/* Ext Buffer Handle (Buffer to Flip to) */
	IMG_HANDLE hExtBuffer;

	/* private tag */
	IMG_HANDLE hPrivateTag;

	/* number of clip rects */
	IMG_UINT32 ui32ClipRectCount;

	/* clip rects */
	IMG_RECT *psClipRect;

	/* number of vsync intervals between successive flips */
	IMG_UINT32	ui32SwapInterval;

} DISPLAYCLASS_FLIP_COMMAND;


typedef struct DISPLAYCLASS_FLIP_COMMAND2_TAG
{
	/* Ext Device Handle */
	IMG_HANDLE hExtDevice;

	/* Ext SwapChain Handle */
	IMG_HANDLE hExtSwapChain;

	/* Unused field, padding for compatibility with above structure */
	IMG_HANDLE hUnused;

	/* number of vsync intervals between successive flips */
	IMG_UINT32 ui32SwapInterval;

	/* private data from userspace */
	IMG_PVOID  pvPrivData;

	/* length of private data in bytes */
	IMG_UINT32 ui32PrivDataLength;

	/* meminfos */
	PDC_MEM_INFO *ppsMemInfos;

	/* number of meminfos */
	IMG_UINT32 ui32NumMemInfos;

} DISPLAYCLASS_FLIP_COMMAND2;

/* start command IDs from 0 */
#define DC_FLIP_COMMAND		0

/* States used in PFN_SET_DC_STATE */
#define DC_STATE_NO_FLUSH_COMMANDS		0
#define DC_STATE_FLUSH_COMMANDS			1
#define DC_STATE_FORCE_SWAP_TO_SYSTEM	2

/* function to retrieve kernel services function table from kernel services */
typedef IMG_BOOL (*PFN_DC_GET_PVRJTABLE)(PPVRSRV_DC_DISP2SRV_KMJTABLE);

/* Prototype for platforms that access the JTable via linkage */
IMG_IMPORT IMG_BOOL PVRGetDisplayClassJTable(PVRSRV_DC_DISP2SRV_KMJTABLE *psJTable);


#if defined (__cplusplus)
}
#endif

#endif/* #if !defined (__KERNELDISPLAY_H__) */

/******************************************************************************
 End of file (kerneldisplay.h)
******************************************************************************/
