/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 		Samsung Electronics System LSI. modify
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <asm/hardirq.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/memory.h>
#include <plat/regs-fb.h>
#include <linux/console.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "oemfuncs.h"
#include "pvr_debug.h"

#include "s3c_lcd.h"
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
#define S3C_DC_IS_PHYS_DISCONTIG
#endif
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
#include "s3c_fb.h"
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
#define	S3C_CONSOLE_LOCK()		console_lock()
#define	S3C_CONSOLE_UNLOCK()	console_unlock()
#else
#define	S3C_CONSOLE_LOCK()		acquire_console_sem()
#define	S3C_CONSOLE_UNLOCK()	release_console_sem()
#endif

static int fb_idx = 0;
IMG_UINT32 gPVREnableVSync = 1;
IMG_UINT32 gPVRPanDisplaySignal = 1;

#define S3C_MAX_BACKBUFFERS 	5
#define S3C_MAX_BUFFERS (S3C_MAX_BACKBUFFERS+1)

#define S3C_DISPLAY_FORMAT_NUM 1
#define S3C_DISPLAY_DIM_NUM 1

#define VSYCN_IRQ IRQ_FIMD1_VSYNC

#define DC_S3C_LCD_COMMAND_COUNT 1

typedef struct S3C_FRAME_BUFFER_TAG
{
	IMG_CPU_VIRTADDR bufferVAddr;
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	IMG_SYS_PHYADDR* pbufferPAddrs;
#else
	IMG_SYS_PHYADDR bufferPAddr;
#endif
	IMG_UINT32 byteSize;
	IMG_UINT32 yoffset; //y offset from SysBuffer
}S3C_FRAME_BUFFER;

typedef void *		 S3C_HANDLE;

typedef enum tag_s3c_bool
{
	S3C_FALSE = 0,
	S3C_TRUE  = 1,
	
} S3C_BOOL, *S3C_PBOOL;

typedef struct S3C_SWAPCHAIN_TAG
{

	unsigned long   ulBufferCount;

	S3C_FRAME_BUFFER  	*psBuffer;

	unsigned long   ulRefCount;
	
}S3C_SWAPCHAIN;

typedef struct S3C_VSYNC_FLIP_ITEM_TAG
{

	S3C_HANDLE		  hCmdComplete;

	S3C_FRAME_BUFFER	*psFb;

	unsigned long	  ulSwapInterval;

	S3C_BOOL		  bValid;

	S3C_BOOL		  bFlipped;

	S3C_BOOL		  bCmdCompleted;

} S3C_VSYNC_FLIP_ITEM;

typedef struct fb_info S3C_FB_INFO;

typedef struct S3C_LCD_DEVINFO_TAG
{
	IMG_UINT32 						ui32DisplayID;
	DISPLAY_INFO 					sDisplayInfo;
	S3C_FB_INFO 					*psFBInfo;

	// sys surface info
	S3C_FRAME_BUFFER				sSysBuffer;
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	IMG_SYS_PHYADDR					*pbufferPAddrs;
#endif
	// number of supported format
	IMG_UINT32 						ui32NumFormats;
	IMG_UINT32 						ui32NumFrameBuffers;

	// list of supported display format
	DISPLAY_FORMAT 					asDisplayForamtList[S3C_DISPLAY_FORMAT_NUM];

	IMG_UINT32 						ui32NumDims;
	DISPLAY_DIMS					asDisplayDimList[S3C_DISPLAY_DIM_NUM];

	// jump table into pvr services
	PVRSRV_DC_DISP2SRV_KMJTABLE 	sPVRJTable;

	// jump table into DC
	PVRSRV_DC_SRV2DISP_KMJTABLE 	sDCJTable;

	// backbuffer info
	S3C_FRAME_BUFFER				asBackBuffers[S3C_MAX_BACKBUFFERS];


	S3C_SWAPCHAIN					*psSwapChain;


	S3C_VSYNC_FLIP_ITEM				asVSyncFlips[S3C_MAX_BUFFERS];

	unsigned long					ulInsertIndex;
	unsigned long					ulRemoveIndex;
	S3C_BOOL						bFlushCommands;

	struct workqueue_struct 		*psWorkQueue;
	struct work_struct				sWork;
	struct mutex					sVsyncFlipItemMutex;

	S3C_BOOL						bIRQInitialized;
}S3C_LCD_DEVINFO;

static S3C_LCD_DEVINFO *g_psLCDInfo = NULL;

extern IMG_BOOL IMG_IMPORT PVRGetDisplayClassJTable(PVRSRV_DC_DISP2SRV_KMJTABLE *psJTable);

static void AdvanceFlipIndex(S3C_LCD_DEVINFO *psDevInfo,
							 unsigned long	*pulIndex)
{
	unsigned long	ulMaxFlipIndex;

	ulMaxFlipIndex = psDevInfo->psSwapChain->ulBufferCount - 1;
	if (ulMaxFlipIndex >= psDevInfo->ui32NumFrameBuffers)
	{
		ulMaxFlipIndex = psDevInfo->ui32NumFrameBuffers -1;
	}

	(*pulIndex)++;

	if (*pulIndex > ulMaxFlipIndex )
	{
		*pulIndex = 0;
	}
}
static IMG_VOID ResetVSyncFlipItems(S3C_LCD_DEVINFO* psDevInfo)
{
	unsigned long i;

	psDevInfo->ulInsertIndex = 0;
	psDevInfo->ulRemoveIndex = 0;

	for(i=0; i < psDevInfo->ui32NumFrameBuffers; i++)
	{
		psDevInfo->asVSyncFlips[i].bValid = S3C_FALSE;
		psDevInfo->asVSyncFlips[i].bFlipped = S3C_FALSE;
		psDevInfo->asVSyncFlips[i].bCmdCompleted = S3C_FALSE;
	}
}

static IMG_VOID S3C_Flip(S3C_LCD_DEVINFO  *psDevInfo,
					   S3C_FRAME_BUFFER *fb)
{
	struct fb_var_screeninfo sFBVar;
	int res;
	unsigned long ulYResVirtual;

	S3C_CONSOLE_LOCK();

	sFBVar = psDevInfo->psFBInfo->var;

	sFBVar.xoffset = 0;
	sFBVar.yoffset = fb->yoffset;

	ulYResVirtual = fb->yoffset + sFBVar.yres;

	if (sFBVar.xres_virtual != sFBVar.xres || sFBVar.yres_virtual < ulYResVirtual)
	{
		sFBVar.xres_virtual = sFBVar.xres;
		sFBVar.yres_virtual = ulYResVirtual;

		sFBVar.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

		res = fb_set_var(psDevInfo->psFBInfo, &sFBVar);
		if (res != 0)
		{
			printk("%s: fb_set_var failed (Y Offset: %d, Error: %d)\n", __FUNCTION__, fb->yoffset, res);	}
	}
	else
	{
		res = fb_pan_display(psDevInfo->psFBInfo, &sFBVar);
		if (res != 0)
		{
			printk( "%s: fb_pan_display failed (Y Offset: %d, Error: %d)\n", __FUNCTION__, fb->yoffset, res);
		}
	}

	if (gPVRPanDisplaySignal) {
		psDevInfo->sPVRJTable.pfnPVRSRVOEMFunction(
			OEM_COMPLETE_PREPARE_DISPLAY,
			&(fb->yoffset), sizeof(int),
			IMG_NULL, 0);
	}

	S3C_CONSOLE_UNLOCK();
}

static void FlushInternalVSyncQueue(S3C_LCD_DEVINFO*psDevInfo)
{
	S3C_VSYNC_FLIP_ITEM*  psFlipItem;

	mutex_lock(&psDevInfo->sVsyncFlipItemMutex);

	psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulRemoveIndex];

	while(psFlipItem->bValid)
	{
		if(psFlipItem->bFlipped ==S3C_FALSE)
		{
		
			S3C_Flip (psDevInfo, psFlipItem->psFb);
		}

		if(psFlipItem->bCmdCompleted == S3C_FALSE)
		{

			psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete((IMG_HANDLE)psFlipItem->hCmdComplete, IMG_FALSE);
		}

		AdvanceFlipIndex(psDevInfo, &psDevInfo->ulRemoveIndex);

		psFlipItem->bFlipped = S3C_FALSE;
		psFlipItem->bCmdCompleted = S3C_FALSE;
		psFlipItem->bValid = S3C_FALSE;

		psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulRemoveIndex];
	}

	psDevInfo->ulInsertIndex = 0;
	psDevInfo->ulRemoveIndex = 0;

	mutex_unlock(&psDevInfo->sVsyncFlipItemMutex);

}

static void VsyncWorkqueueFunc(struct work_struct *psWork)
{

	S3C_VSYNC_FLIP_ITEM *psFlipItem;
	S3C_LCD_DEVINFO *psDevInfo = container_of(psWork, S3C_LCD_DEVINFO, sWork);

	if(psDevInfo == NULL)
	{
		return;
	}
	mutex_lock(&psDevInfo->sVsyncFlipItemMutex);

	psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulRemoveIndex];
	
	while(psFlipItem->bValid)
	{

		if(psFlipItem->bFlipped)
		{
		
			if(!psFlipItem->bCmdCompleted)
			{
				IMG_BOOL bScheduleMISR;
			
#if 0
				bScheduleMISR = IMG_TRUE;
#else
				bScheduleMISR = IMG_FALSE;
#endif

				psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete((IMG_HANDLE)psFlipItem->hCmdComplete, bScheduleMISR);
			
				psFlipItem->bCmdCompleted = S3C_TRUE;
			}

		
			psFlipItem->ulSwapInterval--;
		
			if(psFlipItem->ulSwapInterval == 0)
			{
		
				AdvanceFlipIndex(psDevInfo, &psDevInfo->ulRemoveIndex);

				psFlipItem->bCmdCompleted = S3C_FALSE;
				psFlipItem->bFlipped = S3C_FALSE;

				psFlipItem->bValid = S3C_FALSE;
			}
			else
			{
				break;
			}
		}
		else
		{
			S3C_Flip (psDevInfo, psFlipItem->psFb);
			psFlipItem->bFlipped = S3C_TRUE;

			break;
		}
	
		psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulRemoveIndex];
	}
	mutex_unlock(&psDevInfo->sVsyncFlipItemMutex);
}

static S3C_BOOL CreateVsyncWorkQueue(S3C_LCD_DEVINFO *psDevInfo)
{
	psDevInfo->psWorkQueue = create_workqueue("vsync_workqueue");

	if (psDevInfo->psWorkQueue == IMG_NULL)
	{
		printk("fail to create vsync_handler workqueue\n");
		return S3C_FALSE;
	}

	INIT_WORK(&psDevInfo->sWork, VsyncWorkqueueFunc);
	mutex_init(&psDevInfo->sVsyncFlipItemMutex);

	return S3C_TRUE;
}
static void destropyVsyncWorkQueue(S3C_LCD_DEVINFO *psDevInfo)
{
	destroy_workqueue(psDevInfo->psWorkQueue);
	mutex_destroy(&psDevInfo->sVsyncFlipItemMutex);
}
static irqreturn_t S3C_VSyncISR(int irq, void *dev_id)
{

	if( dev_id != g_psLCDInfo)
	{
		return IRQ_NONE;
	}

	queue_work(g_psLCDInfo->psWorkQueue, &g_psLCDInfo->sWork);

	return IRQ_HANDLED;
}

static IMG_VOID S3C_InstallVsyncISR(void)
{	
	if(request_irq(VSYCN_IRQ, S3C_VSyncISR, IRQF_SHARED , "s3cfb", g_psLCDInfo))
	{
		printk("S3C_InstallVsyncISR: Couldn't install system LISR on IRQ %d", VSYCN_IRQ);
		g_psLCDInfo->bIRQInitialized = S3C_FALSE;
		return;
	}

	g_psLCDInfo->bIRQInitialized = S3C_TRUE;
}
static IMG_VOID S3C_UninstallVsyncISR(void)
{	
	if (g_psLCDInfo->bIRQInitialized == S3C_TRUE)
		free_irq(VSYCN_IRQ, g_psLCDInfo);
}

static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 ui32DeviceID,
								 IMG_HANDLE *phDevice,
								 PVRSRV_SYNC_DATA* psSystemBufferSyncData)
{
	PVR_UNREFERENCED_PARAMETER(ui32DeviceID);

	*phDevice =  (IMG_HANDLE)g_psLCDInfo;

	return PVRSRV_OK;
}

static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
	S3C_LCD_DEVINFO *psLCDInfo = (S3C_LCD_DEVINFO*)hDevice;

	PVR_UNREFERENCED_PARAMETER(psLCDInfo);

	return PVRSRV_OK;
}

static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE		hDevice,
								  IMG_UINT32		*pui32NumFormats,
								  DISPLAY_FORMAT	*psFormat)
{
	S3C_LCD_DEVINFO *psLCDInfo = (S3C_LCD_DEVINFO*)hDevice;
	int i;

	if(!hDevice || !pui32NumFormats)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*pui32NumFormats = S3C_DISPLAY_FORMAT_NUM;

	if(psFormat)
	{
		for (i = 0 ; i < S3C_DISPLAY_FORMAT_NUM ; i++)
			psFormat[i] = psLCDInfo->asDisplayForamtList[i];
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR EnumDCDims(IMG_HANDLE		hDevice,
							   DISPLAY_FORMAT	*psFormat,
							   IMG_UINT32		*pui32NumDims,
							   DISPLAY_DIMS		*psDim)
{
	int i;

	S3C_LCD_DEVINFO *psLCDInfo = (S3C_LCD_DEVINFO*)hDevice;

	if(!hDevice || !psFormat || !pui32NumDims)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*pui32NumDims = S3C_DISPLAY_DIM_NUM;

	if(psDim)
	{
		for (i = 0 ; i < S3C_DISPLAY_DIM_NUM ; i++)
			psDim[i] = psLCDInfo->asDisplayDimList[i];

	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE *phBuffer)
{
	S3C_LCD_DEVINFO *psLCDInfo = (S3C_LCD_DEVINFO*)hDevice;

	if(!hDevice || !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*phBuffer=(IMG_HANDLE)(&(psLCDInfo->sSysBuffer));
	return PVRSRV_OK;
}

static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO *psDCInfo)
{
	S3C_LCD_DEVINFO *psLCDInfo = (S3C_LCD_DEVINFO*)hDevice;

	if(!hDevice || !psDCInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*psDCInfo = psLCDInfo->sDisplayInfo;

	return PVRSRV_OK;
}

static PVRSRV_ERROR GetDCBufferAddr(IMG_HANDLE		hDevice,
									IMG_HANDLE		hBuffer,
									IMG_SYS_PHYADDR	**ppsSysAddr,
									IMG_UINT32		*pui32ByteSize,
									IMG_VOID		**ppvCpuVAddr,
									IMG_HANDLE		*phOSMapInfo,
									IMG_BOOL		*pbIsContiguous,
									IMG_UINT32		  *pui32TilingStride)
{
	S3C_FRAME_BUFFER *buf = (S3C_FRAME_BUFFER *)hBuffer;
	S3C_LCD_DEVINFO *psLCDInfo = (S3C_LCD_DEVINFO*)hDevice;
	
	PVR_UNREFERENCED_PARAMETER(psLCDInfo);
	PVR_UNREFERENCED_PARAMETER(pui32TilingStride);
	
	if(!hDevice || !hBuffer || !ppsSysAddr || !pui32ByteSize)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*phOSMapInfo = IMG_NULL;
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	*pbIsContiguous = IMG_FALSE;
	*ppsSysAddr = buf->pbufferPAddrs;
#else
	*pbIsContiguous = IMG_TRUE;
	*ppsSysAddr = &(buf->bufferPAddr);
#endif
	*ppvCpuVAddr = (IMG_VOID *)buf->bufferVAddr;
	*pui32ByteSize = buf->byteSize;

	return PVRSRV_OK;
}

static PVRSRV_ERROR CreateDCSwapChain(IMG_HANDLE hDevice,
									  IMG_UINT32 ui32Flags,
									  DISPLAY_SURF_ATTRIBUTES *psDstSurfAttrib,
									  DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
									  IMG_UINT32 ui32BufferCount,
									  PVRSRV_SYNC_DATA **ppsSyncData,
									  IMG_UINT32 ui32OEMFlags,
									  IMG_HANDLE *phSwapChain,
									  IMG_UINT32 *pui32SwapChainID)
{
	IMG_UINT32 i;

	S3C_FRAME_BUFFER *psBuffer;
	S3C_SWAPCHAIN *psSwapChain;
	S3C_LCD_DEVINFO *psDevInfo = (S3C_LCD_DEVINFO*)hDevice;
	
	PVR_UNREFERENCED_PARAMETER(ui32OEMFlags);
	PVR_UNREFERENCED_PARAMETER(pui32SwapChainID);

	if(!hDevice
	|| !psDstSurfAttrib
	|| !psSrcSurfAttrib
	|| !ppsSyncData
	|| !phSwapChain)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if(ui32BufferCount > psDevInfo->ui32NumFrameBuffers)
	{
		return PVRSRV_ERROR_TOOMANYBUFFERS;
	}
	

	if(psDevInfo->psSwapChain)
	{
	/*
	 * To support android test applciation which access FB directly
	 * Original code do not allow direct
	 */
		psDevInfo->psSwapChain->ulRefCount++;
		*phSwapChain = (IMG_HANDLE)psDevInfo->psSwapChain;
		*pui32SwapChainID = (IMG_UINT32)psDevInfo->psSwapChain;
		return PVRSRV_OK;
	}

	psSwapChain = (S3C_SWAPCHAIN *)kmalloc(sizeof(S3C_SWAPCHAIN),GFP_KERNEL);
	psBuffer = (S3C_FRAME_BUFFER*)kmalloc(sizeof(S3C_FRAME_BUFFER) * ui32BufferCount, GFP_KERNEL);
	
	if(!psBuffer)
	{
		kfree(psSwapChain);
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	
	psSwapChain->ulBufferCount = (unsigned long)ui32BufferCount;
	psSwapChain->psBuffer = psBuffer;

#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	psBuffer[0].pbufferPAddrs = psDevInfo->sSysBuffer.pbufferPAddrs;
#else
	psBuffer[0].bufferPAddr = psDevInfo->sSysBuffer.bufferPAddr;
#endif
	psBuffer[0].bufferVAddr = psDevInfo->sSysBuffer.bufferVAddr;
	psBuffer[0].byteSize = psDevInfo->sSysBuffer.byteSize;
	psBuffer[0].yoffset = psDevInfo->sSysBuffer.yoffset;

	for (i=1; i<ui32BufferCount; i++)
	{
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
		psBuffer[i].pbufferPAddrs = psDevInfo->asBackBuffers[i-1].pbufferPAddrs;
#else
		psBuffer[i].bufferPAddr = psDevInfo->asBackBuffers[i-1].bufferPAddr;
#endif
		psBuffer[i].bufferVAddr = psDevInfo->asBackBuffers[i-1].bufferVAddr;
		psBuffer[i].byteSize = psDevInfo->asBackBuffers[i-1].byteSize;
		psBuffer[i].yoffset = psDevInfo->asBackBuffers[i-1].yoffset;
	}
	
	*phSwapChain = (IMG_HANDLE)psSwapChain;
	*pui32SwapChainID =(IMG_UINT32)psSwapChain;	
	
	psSwapChain->ulRefCount++;
	psDevInfo->psSwapChain = psSwapChain;

    ResetVSyncFlipItems(psDevInfo);
	S3C_InstallVsyncISR();

	return PVRSRV_OK;
}

static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice,
									   IMG_HANDLE hSwapChain)
{
	S3C_SWAPCHAIN *sc = (S3C_SWAPCHAIN *)hSwapChain;
	S3C_LCD_DEVINFO *psLCDInfo = (S3C_LCD_DEVINFO*)hDevice;

	if(!hDevice
	|| !hSwapChain)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	sc->ulRefCount--;

	if (sc->ulRefCount != 0)
		return PVRSRV_OK;

	FlushInternalVSyncQueue(psLCDInfo);

	S3C_Flip(psLCDInfo, &psLCDInfo->sSysBuffer);
		
	kfree(sc->psBuffer);
	kfree(sc);

	if (psLCDInfo->psSwapChain == sc)
		psLCDInfo->psSwapChain = NULL;	
	
	ResetVSyncFlipItems(psLCDInfo);

	S3C_UninstallVsyncISR();

	return PVRSRV_OK;
}

static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE	hDevice,
								 IMG_HANDLE	hSwapChain,
								 IMG_RECT	*psRect)
{

	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE	hDevice,
								 IMG_HANDLE	hSwapChain,
								 IMG_RECT	*psRect)
{

	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE	hDevice,
									  IMG_HANDLE	hSwapChain,
									  IMG_UINT32	ui32CKColour)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE	hDevice,
									  IMG_HANDLE	hSwapChain,
									  IMG_UINT32	ui32CKColour)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static IMG_VOID S3CSetState(IMG_HANDLE hDevice, IMG_UINT32 ui32State)
{
	S3C_LCD_DEVINFO	*psDevInfo;

	psDevInfo = (S3C_LCD_DEVINFO*)hDevice;

	if (ui32State == DC_STATE_FLUSH_COMMANDS)
	{
		if (psDevInfo->psSwapChain != 0)
		{
			FlushInternalVSyncQueue(psDevInfo);
		}

		psDevInfo->bFlushCommands =S3C_TRUE;
	}
	else if (ui32State == DC_STATE_NO_FLUSH_COMMANDS)
	{
		psDevInfo->bFlushCommands = S3C_FALSE;
	}
}

static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice,
								 IMG_HANDLE hSwapChain,
								 IMG_UINT32 *pui32BufferCount,
								 IMG_HANDLE *phBuffer)
{
	S3C_LCD_DEVINFO *psLCDInfo = (S3C_LCD_DEVINFO*)hDevice;
	int	i;
	

	if(!hDevice
	|| !hSwapChain
	|| !pui32BufferCount
	|| !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*pui32BufferCount = psLCDInfo->psSwapChain->ulBufferCount;
	phBuffer[0] = (IMG_HANDLE)(&(psLCDInfo->sSysBuffer));
	for (i=0; i < (*pui32BufferCount) - 1; i++)
	{
		phBuffer[i+1] = (IMG_HANDLE)(&(psLCDInfo->asBackBuffers[i]));
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE	hDevice,
								   IMG_HANDLE	hBuffer,
								   IMG_UINT32	ui32SwapInterval,
								   IMG_HANDLE	hPrivateTag,
								   IMG_UINT32	ui32ClipRectCount,
								   IMG_RECT		*psClipRect)
{

	PVR_UNREFERENCED_PARAMETER(ui32SwapInterval);
	PVR_UNREFERENCED_PARAMETER(hPrivateTag);
	PVR_UNREFERENCED_PARAMETER(psClipRect);

	if(!hDevice
	|| !hBuffer
	|| (ui32ClipRectCount != 0))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return PVRSRV_OK;
}

static IMG_BOOL ProcessFlip(IMG_HANDLE	hCmdCookie,
							IMG_UINT32	ui32DataSize,
							IMG_VOID	*pvData)
{
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	S3C_LCD_DEVINFO *psDevInfo;
	S3C_FRAME_BUFFER *fb;
	S3C_VSYNC_FLIP_ITEM* psFlipItem;

	if(!hCmdCookie || !pvData)
	{
		return IMG_FALSE;
	}

	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)pvData;
	if (psFlipCmd == IMG_NULL || sizeof(DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize)
	{
		return IMG_FALSE;
	}

	psDevInfo = (S3C_LCD_DEVINFO*)psFlipCmd->hExtDevice;
	fb = (S3C_FRAME_BUFFER*)psFlipCmd->hExtBuffer; 
	
	if (psDevInfo->bFlushCommands)
	{
		psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete(hCmdCookie, IMG_FALSE);
		return IMG_TRUE;
	}
	if (psFlipCmd->ui32SwapInterval == 0 || gPVREnableVSync == 0)
	{
	
		S3C_Flip(psDevInfo, fb);
	

		psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete(hCmdCookie, IMG_FALSE);
	
		return IMG_TRUE;

	}

	mutex_lock(&psDevInfo->sVsyncFlipItemMutex);

	psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulInsertIndex];
	
	if(!psFlipItem->bValid)
	{
		if(psDevInfo->ulInsertIndex == psDevInfo->ulRemoveIndex)
		{
		
			S3C_Flip(psDevInfo, fb);

			psFlipItem->bFlipped = S3C_TRUE;
		}
		else
		{
			psFlipItem->bFlipped = S3C_FALSE;
		}

		psFlipItem->hCmdComplete = hCmdCookie;
		psFlipItem->psFb= fb;
		psFlipItem->ulSwapInterval = (unsigned long)psFlipCmd->ui32SwapInterval;

		psFlipItem->bValid = S3C_TRUE;

		AdvanceFlipIndex(psDevInfo, &psDevInfo->ulInsertIndex);

		mutex_unlock(&psDevInfo->sVsyncFlipItemMutex);

		return IMG_TRUE;

	}
	mutex_unlock(&psDevInfo->sVsyncFlipItemMutex);

	return IMG_FALSE;
}

static S3C_BOOL InitDev(struct fb_info **s3c_fb_Info)
{
	struct fb_info *psLINFBInfo;
	struct module *psLINFBOwner;
	S3C_BOOL eError = S3C_TRUE;
	
	S3C_CONSOLE_LOCK();

	if (fb_idx < 0 || fb_idx >= num_registered_fb)
	{
		eError = S3C_FALSE;
		goto errRelSem;
	}

	psLINFBInfo = registered_fb[fb_idx];

	psLINFBOwner = psLINFBInfo->fbops->owner;
	if (!try_module_get(psLINFBOwner))
	{
		printk("Couldn't get framebuffer module\n");
		eError = S3C_FALSE;
		goto errRelSem;
	}

	if (psLINFBInfo->fbops->fb_open != NULL)
	{
		int res;

		res = psLINFBInfo->fbops->fb_open(psLINFBInfo, 0);
		if (res != 0)
		{
			printk("Couldn't open framebuffer: %d\n", res);
			eError = S3C_FALSE;
			goto errModPut;
		}
	}

	*s3c_fb_Info = psLINFBInfo;

errModPut:
	module_put(psLINFBOwner);
errRelSem:
	S3C_CONSOLE_UNLOCK();

	return eError;
}

static void DeInitDev(S3C_LCD_DEVINFO  *psDevInfo)
{
	struct fb_info *psLINFBInfo = psDevInfo->psFBInfo;
	struct module *psLINFBOwner;

	S3C_CONSOLE_LOCK();

	psLINFBOwner = psLINFBInfo->fbops->owner;

	if (psLINFBInfo->fbops->fb_release != NULL) 
	{
		(void) psLINFBInfo->fbops->fb_release(psLINFBInfo, 0);
	}

	module_put(psLINFBOwner);

	S3C_CONSOLE_UNLOCK();
}
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
static IMG_BOOL GetPhysAddrFromLCDInfo(struct fb_info *psLINFBInfo,
				IMG_UINT32 *pui32PageCount,
				IMG_SYS_PHYADDR **ppasSysPhysAddr)
{
	IMG_UINT32 i;
	IMG_UINT32 ui32PageCount = 0;
	struct scatterlist *psScatterList;
	struct scatterlist *psTemp;
	struct s3c_fb_win *pfb_win = (struct s3c_fb_win*)psLINFBInfo->par;
	IMG_SYS_PHYADDR *pasSysPhysAddr = NULL;
	psScatterList = pfb_win->dma_buf_data.sg_table->sgl;

	for (i=0;i<2;i++)
	{
		psTemp = psScatterList;
		if (i == 1)
		{
			pasSysPhysAddr = kmalloc(sizeof(IMG_SYS_PHYADDR) * ui32PageCount, GFP_KERNEL);
			if (pasSysPhysAddr == NULL)
			{
				printk("out of memory: cannot alloc mem for structure %s\n", __func__);
				goto exitFailAlloc;
			}
			ui32PageCount = 0;	/* Reset the page count a we use if for the index */
		}

		while(psTemp)
		{
			IMG_UINT32 j;

			for (j=0;j<psTemp->length;j+=PAGE_SIZE)
			{
				if (i == 1)
				{
					pasSysPhysAddr[ui32PageCount].uiAddr = sg_phys(psTemp) + j;
				}
				ui32PageCount++;
			}
			psTemp = sg_next(psTemp);
		}
	}
	printk("\n uiCount = %d\n", ui32PageCount);

	*pui32PageCount = ui32PageCount;
	*ppasSysPhysAddr = pasSysPhysAddr;

	return IMG_TRUE;

exitFailAlloc:
	kfree(pasSysPhysAddr);
	*ppasSysPhysAddr = IMG_NULL;
	return IMG_FALSE;
}
#endif /*S3C_DC_IS_PHYS_DISCONTIG*/

int s3c_displayclass_init(void)
{
	IMG_UINT32 screen_w, screen_h;
	IMG_UINT32 va_fb, fb_size;
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	IMG_SYS_PHYADDR *parr_addr = IMG_NULL;
	IMG_UINT32 page_count;
#else
	IMG_SYS_PHYADDR pa_fb;
#endif

	IMG_UINT32 num_of_fb, num_of_backbuffer;
	IMG_UINT32 byteSize;

	struct fb_info *psLINFBInfo = 0;

	int	i;
	int rgb_format, bytes_per_pixel, bits_per_pixel;

	if(InitDev(&psLINFBInfo) == S3C_FALSE)
	{
		goto err_out;
	}

	va_fb = (unsigned long)psLINFBInfo->screen_base;
	screen_w = psLINFBInfo->var.xres;
	screen_h = psLINFBInfo->var.yres;
	bits_per_pixel = psLINFBInfo->var.bits_per_pixel;
	fb_size = psLINFBInfo->fix.smem_len;

	switch (bits_per_pixel)
	{
	case 16:
		rgb_format = PVRSRV_PIXEL_FORMAT_RGB565;
		bytes_per_pixel = 2;
		break;
	case 32:
		rgb_format = PVRSRV_PIXEL_FORMAT_ARGB8888;
		bytes_per_pixel = 4;
		break;
	default:
		rgb_format = PVRSRV_PIXEL_FORMAT_ARGB8888;
		bytes_per_pixel = 4;
		break;
	}

	byteSize = screen_w * screen_h * bytes_per_pixel;
	num_of_fb = fb_size / (screen_w * screen_h * bytes_per_pixel);

	if(num_of_fb > S3C_MAX_BUFFERS)
	{
		printk("too many frame buffers\n");
		return 0;
	}
	num_of_backbuffer = num_of_fb - 1;

#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	if (GetPhysAddrFromLCDInfo(psLINFBInfo,  &page_count, &parr_addr) == IMG_FALSE)
	{
		printk("PVR s3c_lcd cannot get physical address list form lcd info\n");
		goto err_out;
	}
	printk("PA FB = 0x%X, bits per pixel = %d\n", (unsigned int)parr_addr[0].uiAddr, (unsigned int)bits_per_pixel);
#else
	pa_fb.uiAddr = psLINFBInfo->fix.smem_start;
	printk("PA FB = 0x%X, bits per pixel = %d\n", (unsigned int)pa_fb.uiAddr, (unsigned int)bits_per_pixel);
#endif

	printk("screen width=%d height=%d va=0x%x", (int)screen_w, (int)screen_h, (unsigned int)va_fb);
	printk("xres_virtual = %d, yres_virtual = %d, xoffset = %d, yoffset = %d\n", psLINFBInfo->var.xres_virtual,  psLINFBInfo->var.yres_virtual,  psLINFBInfo->var.xoffset,  psLINFBInfo->var.yoffset);
	printk("fb_size=%d\n", (int)fb_size);

	if (g_psLCDInfo == NULL)
	{
		PFN_CMD_PROC	pfnCmdProcList[DC_S3C_LCD_COMMAND_COUNT];
		IMG_UINT32	aui32SyncCountList[DC_S3C_LCD_COMMAND_COUNT][2];

		g_psLCDInfo = (S3C_LCD_DEVINFO*)kmalloc(sizeof(S3C_LCD_DEVINFO),GFP_KERNEL);
		memset(g_psLCDInfo, 0, sizeof(S3C_LCD_DEVINFO));

		g_psLCDInfo->psFBInfo = psLINFBInfo;
		g_psLCDInfo->ui32NumFrameBuffers = num_of_fb;

		g_psLCDInfo->ui32NumFormats = S3C_DISPLAY_FORMAT_NUM;

		g_psLCDInfo->asDisplayForamtList[0].pixelformat = rgb_format;
		g_psLCDInfo->ui32NumDims = S3C_DISPLAY_DIM_NUM;
		g_psLCDInfo->asDisplayDimList[0].ui32ByteStride = (bytes_per_pixel) * screen_w;
		g_psLCDInfo->asDisplayDimList[0].ui32Height = screen_h;
		g_psLCDInfo->asDisplayDimList[0].ui32Width = screen_w;
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
		g_psLCDInfo->sSysBuffer.pbufferPAddrs = parr_addr;
#else
		g_psLCDInfo->sSysBuffer.bufferPAddr.uiAddr = pa_fb.uiAddr;
#endif
		g_psLCDInfo->sSysBuffer.bufferVAddr = (IMG_CPU_VIRTADDR)va_fb;
		g_psLCDInfo->sSysBuffer.yoffset = 0;
		g_psLCDInfo->sSysBuffer.byteSize = (IMG_UINT32)byteSize;

		for (i=0 ; i < num_of_backbuffer; i++)
		{
			g_psLCDInfo->asBackBuffers[i].byteSize = g_psLCDInfo->sSysBuffer.byteSize;
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
			g_psLCDInfo->asBackBuffers[i].pbufferPAddrs = parr_addr + byteSize * (i+1) / PAGE_SIZE;
#else
			g_psLCDInfo->asBackBuffers[i].bufferPAddr.uiAddr = pa_fb.uiAddr + byteSize * (i+1);
#endif
			g_psLCDInfo->asBackBuffers[i].bufferVAddr = (IMG_CPU_VIRTADDR)(va_fb +  byteSize * (i+1));
			g_psLCDInfo->asBackBuffers[i].yoffset = screen_h * (i + 1);
		
			printk("Back frameBuffer[%d].VAddr=%p PAddr=%p size=%d\n",
				i, 
				(void*)g_psLCDInfo->asBackBuffers[i].bufferVAddr,
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
				(void*)g_psLCDInfo->asBackBuffers[i].pbufferPAddrs[0].uiAddr,
#else
				(void*)g_psLCDInfo->asBackBuffers[i].bufferPAddr.uiAddr,
#endif
				(int)g_psLCDInfo->asBackBuffers[i].byteSize);
		}

		g_psLCDInfo->bFlushCommands = S3C_FALSE;
		g_psLCDInfo->psSwapChain = NULL;

		PVRGetDisplayClassJTable(&(g_psLCDInfo->sPVRJTable));

		g_psLCDInfo->sDCJTable.ui32TableSize = sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE);
		g_psLCDInfo->sDCJTable.pfnOpenDCDevice = OpenDCDevice;
		g_psLCDInfo->sDCJTable.pfnCloseDCDevice = CloseDCDevice;
		g_psLCDInfo->sDCJTable.pfnEnumDCFormats = EnumDCFormats;
		g_psLCDInfo->sDCJTable.pfnEnumDCDims = EnumDCDims;
		g_psLCDInfo->sDCJTable.pfnGetDCSystemBuffer = GetDCSystemBuffer;
		g_psLCDInfo->sDCJTable.pfnGetDCInfo = GetDCInfo;
		g_psLCDInfo->sDCJTable.pfnGetBufferAddr = GetDCBufferAddr;
		g_psLCDInfo->sDCJTable.pfnCreateDCSwapChain = CreateDCSwapChain;
		g_psLCDInfo->sDCJTable.pfnDestroyDCSwapChain = DestroyDCSwapChain;
		g_psLCDInfo->sDCJTable.pfnSetDCDstRect = SetDCDstRect;
		g_psLCDInfo->sDCJTable.pfnSetDCSrcRect = SetDCSrcRect;
		g_psLCDInfo->sDCJTable.pfnSetDCDstColourKey = SetDCDstColourKey;
		g_psLCDInfo->sDCJTable.pfnSetDCSrcColourKey = SetDCSrcColourKey;
		g_psLCDInfo->sDCJTable.pfnGetDCBuffers = GetDCBuffers;
		g_psLCDInfo->sDCJTable.pfnSwapToDCBuffer = SwapToDCBuffer;
		g_psLCDInfo->sDCJTable.pfnSetDCState = S3CSetState;

		g_psLCDInfo->sDisplayInfo.ui32MinSwapInterval=0;
		g_psLCDInfo->sDisplayInfo.ui32MaxSwapInterval=1;
		g_psLCDInfo->sDisplayInfo.ui32MaxSwapChains=1;
		g_psLCDInfo->sDisplayInfo.ui32MaxSwapChainBuffers = num_of_fb;
		g_psLCDInfo->sDisplayInfo.ui32PhysicalWidthmm= psLINFBInfo->var.width;// width of lcd in mm 
		g_psLCDInfo->sDisplayInfo.ui32PhysicalHeightmm= psLINFBInfo->var.height;// height of lcd in mm 

		strncpy(g_psLCDInfo->sDisplayInfo.szDisplayName, "s3c_lcd", MAX_DISPLAY_NAME_SIZE);

		if(g_psLCDInfo->sPVRJTable.pfnPVRSRVRegisterDCDevice	(&(g_psLCDInfo->sDCJTable),
			(IMG_UINT32 *)(&(g_psLCDInfo->ui32DisplayID))) != PVRSRV_OK)
		{
			goto err_out;
		}

		pfnCmdProcList[DC_FLIP_COMMAND] = ProcessFlip;
		aui32SyncCountList[DC_FLIP_COMMAND][0] = 0;
		aui32SyncCountList[DC_FLIP_COMMAND][1] = 2;

		if (g_psLCDInfo->sPVRJTable.pfnPVRSRVRegisterCmdProcList(g_psLCDInfo->ui32DisplayID,
			&pfnCmdProcList[0], aui32SyncCountList, DC_S3C_LCD_COMMAND_COUNT)
			!= PVRSRV_OK)
		{
			printk("failing register commmand proc list deviceID:%d\n",(int)g_psLCDInfo->ui32DisplayID);
			return PVRSRV_ERROR_CANT_REGISTER_CALLBACK;
		}

		if(CreateVsyncWorkQueue(g_psLCDInfo) == S3C_FALSE)
		{
			printk("fail to CreateVsyncWorkQueue\n");
			goto err_out;
		}
	}

#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	g_psLCDInfo->pbufferPAddrs = parr_addr;
#endif
	return 0;

err_out:
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	if (parr_addr != NULL)
		kfree(parr_addr);
#endif
	printk("fail to init s3c lcd for pvr driver\n");
	return 1;
}

void s3c_displayclass_deinit(void)
{
	destropyVsyncWorkQueue(g_psLCDInfo);
	DeInitDev(g_psLCDInfo);
#if defined(S3C_DC_IS_PHYS_DISCONTIG)
	if (g_psLCDInfo->pbufferPAddrs)
		kfree(g_psLCDInfo->pbufferPAddrs);
#endif
	g_psLCDInfo->sPVRJTable.pfnPVRSRVRemoveCmdProcList ((IMG_UINT32)g_psLCDInfo->ui32DisplayID,
														DC_S3C_LCD_COMMAND_COUNT);

	g_psLCDInfo->sPVRJTable.pfnPVRSRVRemoveDCDevice(g_psLCDInfo->ui32DisplayID);

	if (g_psLCDInfo)
		kfree(g_psLCDInfo);

	g_psLCDInfo = NULL;
}
