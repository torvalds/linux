/*************************************************************************/ /*!
@Title          Linux framebuffer display driver structures and prototypes
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
#ifndef __PVRLFB_H__
#define __PVRLFB_H__

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
#define	PVRLFB_CONSOLE_LOCK()		console_lock()
#define	PVRLFB_CONSOLE_UNLOCK()		console_unlock()
#else
#define	PVRLFB_CONSOLE_LOCK()		acquire_console_sem()
#define	PVRLFB_CONSOLE_UNLOCK()		release_console_sem()
#endif

#define PVRLFB_MAXFORMATS	(1)
#define PVRLFB_MAXDIMS		(1)

typedef void *PVRLFB_HANDLE;

typedef bool PVRLFB_BOOL, *PVRLFB_PBOOL;
#define	PVRLFB_FALSE false
#define PVRLFB_TRUE true

/* PVRLFB buffer structure */
typedef struct PVRLFB_BUFFER_TAG
{
	PVRLFB_HANDLE            hSwapChain;
	unsigned long            ulBufferSize;

	/* IMG structures used, to minimise API function code */
	/* replace with own structures where necessary */
	/* system physical address of the buffer */
	IMG_SYS_PHYADDR          sSysAddr;
	/* CPU virtual address */
	IMG_CPU_VIRTADDR         sCPUVAddr;
	/* 
		ptr to synchronisation information structure
		associated with the buffer 
	*/
	PVRSRV_SYNC_DATA         *psSyncData;

	struct PVRLFB_BUFFER_TAG *psNext;
} PVRLFB_BUFFER;


typedef struct PVRLFB_FBINFO_TAG
{
	unsigned long           ulScreenSize;
	unsigned long           ulWidth;
	unsigned long           ulHeight;
	unsigned long           ulByteStride;

	/* IMG structures used, to minimise API function code */
	/* replace with own structures where necessary */
	IMG_SYS_PHYADDR         sSysAddr;
	IMG_CPU_VIRTADDR        sCPUVAddr;
	PVRSRV_PIXEL_FORMAT     ePixelFormat;
}PVRLFB_FBINFO;


/* kernel device information structure */
typedef struct PVRLFB_DEVINFO_TAG
{
	/* Linux framebuffer ID */
	unsigned int            uiFBDevID;

	/* PVR Services device ID */
	unsigned int            uiPVRDevID;

	/* number of supported display formats */
	unsigned long           ulNumFormats;

	/* number of supported display dims */
	unsigned long           ulNumDims;

	/* jump table into PVR services */
	PVRSRV_DC_DISP2SRV_KMJTABLE sPVRJTable;

	/* jump table into DC */
	PVRSRV_DC_SRV2DISP_KMJTABLE sDCJTable;

	/* fb info structure */
	PVRLFB_FBINFO           sFBInfo;

	/* pointer to linux frame buffer information structure */
	struct fb_info          *psLINFBInfo;

#if !defined(PVRLFB_NO_AUTO_UNBLANK)
	/* Linux Framebuffer event notification block */
	struct notifier_block   sLINNotifBlock;

	/* Linux work queue structure */
	struct	work_struct     sLINWork;
#endif	/* !defined(PVRLFB_NO_AUTO_UNBLANK) */

	/* system surface info */
	PVRLFB_BUFFER           sSystemBuffer;

	/* IMG structures used, to minimise API function code */
	/* replace with own structures where necessary */
	DISPLAY_INFO            sDisplayInfo;

	DISPLAY_FORMAT          sSysFormat;
	DISPLAY_DIMS            sSysDims;

	/* list of supported display formats */
	DISPLAY_FORMAT          asDisplayFormatList[PVRLFB_MAXFORMATS];
	
	/* list of supported display formats */
	DISPLAY_DIMS            asDisplayDimList[PVRLFB_MAXDIMS];
	
} PVRLFB_DEVINFO;


/*!
 *****************************************************************************
 * Error values
 *****************************************************************************/
typedef enum _PVRLFB_ERROR_
{
	PVRLFB_OK                              =  0,
	PVRLFB_ERROR_GENERIC                   =  1,
	PVRLFB_ERROR_OUT_OF_MEMORY             =  2,
	PVRLFB_ERROR_TOO_FEW_BUFFERS           =  3,
	PVRLFB_ERROR_INVALID_PARAMS            =  4,
	PVRLFB_ERROR_INIT_FAILURE              =  5,
	PVRLFB_ERROR_CANT_REGISTER_CALLBACK    =  6,
	PVRLFB_ERROR_INVALID_DEVICE            =  7,
	PVRLFB_ERROR_DEVICE_REGISTER_FAILED    =  8
} PVRLFB_ERROR;


#ifndef UNREFERENCED_PARAMETER
#define	UNREFERENCED_PARAMETER(param) (param) = (param)
#endif

/* DEBUG only printk */
#ifdef	DEBUG
#define	DEBUG_PRINTK(x) printk x
#else
#define	DEBUG_PRINTK(x)
#endif

#define DISPLAY_DEVICE_NAME "PowerVR Linux Framebuffer Display Driver"
#define	DRVNAME	"pvrlfb"
#define	DEVNAME	DRVNAME
#define	DRIVER_PREFIX DRVNAME


PVRLFB_ERROR PVRLFBInit(void);
PVRLFB_ERROR PVRLFBDeInit(void);

void *PVRLFBAllocKernelMem(unsigned long ulSize);
void PVRLFBFreeKernelMem(void *pvMem);
PVRLFB_ERROR PVRLFBGetLibFuncAddr(char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable);

#endif /* __PVRLFB_H__ */

/******************************************************************************
 End of file (pvrlfb.h)
******************************************************************************/

