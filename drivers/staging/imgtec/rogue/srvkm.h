/**************************************************************************/ /*!
@File
@Title          Services kernel module internal header file
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
*/ /***************************************************************************/

#ifndef SRVKM_H
#define SRVKM_H

#include "servicesext.h"

#if defined(__KERNEL__) && defined(LINUX) && !defined(__GENKSYMS__)
#define __pvrsrv_defined_struct_enum__
#include <services_kernel_client.h>
#endif

struct _PVRSRV_DEVICE_NODE_;

/*************************************************************************/ /*!
@Function     PVRSRVDriverInit
@Description  Performs one time initialisation of Services.
@Return       PVRSRV_ERROR   PVRSRV_OK on success and an error otherwise
*/ /**************************************************************************/
PVRSRV_ERROR IMG_CALLCONV PVRSRVDriverInit(void);

/*************************************************************************/ /*!
@Function     PVRSRVDriverInit
@Description  Performs one time de-initialisation of Services.
@Return       void 
*/ /**************************************************************************/
void IMG_CALLCONV PVRSRVDriverDeInit(void);

/*************************************************************************/ /*!
@Function     PVRSRVDeviceCreate
@Description  Creates a PVR Services device node for an OS native device.
@Input        pvOSDevice     OS native device
@Output       ppsDeviceNode  Points to the new device node on success
@Return       PVRSRV_ERROR   PVRSRV_OK on success and an error otherwise
*/ /**************************************************************************/
PVRSRV_ERROR IMG_CALLCONV
PVRSRVDeviceCreate(void *pvOSDevice,
				   struct _PVRSRV_DEVICE_NODE_ **ppsDeviceNode);

#if defined(SUPPORT_KERNEL_SRVINIT)
/*************************************************************************/ /*!
@Function     PVRSRVDeviceInitialise
@Description  Initialises the given device, created by PVRSRVDeviceCreate, so
              that's in a functional state ready to be used.
@Input        psDeviceNode  Device node of the device to be initialised
@Return       PVRSRV_ERROR  PVRSRV_OK on success and an error otherwise
*/ /**************************************************************************/
PVRSRV_ERROR PVRSRVDeviceInitialise(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode);
#endif

/*************************************************************************/ /*!
@Function     PVRSRVDeviceDestroy
@Description  Destroys a PVR Services device node.
@Input        psDeviceNode  Device node to destroy
@Return       PVRSRV_ERROR  PVRSRV_OK on success and an error otherwise
*/ /**************************************************************************/
PVRSRV_ERROR IMG_CALLCONV
PVRSRVDeviceDestroy(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode);

/******************
HIGHER LEVEL MACROS
*******************/

/*----------------------------------------------------------------------------
Repeats the body of the loop for a certain minimum time, or until the body
exits by its own means (break, return, goto, etc.)

Example of usage:

LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
{
	if(psQueueInfo->ui32ReadOffset == psQueueInfo->ui32WriteOffset)
	{
		bTimeout = IMG_FALSE;
		break;
	}
	
	OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
} END_LOOP_UNTIL_TIMEOUT();

-----------------------------------------------------------------------------*/

/*	uiNotLastLoop will remain at 1 until the timeout has expired, at which time		
 * 	it will be decremented and the loop executed one final time. This is necessary
 *	when preemption is enabled. 
 */
/* PRQA S 3411,3431 12 */ /* critical format, leave alone */
#define LOOP_UNTIL_TIMEOUT(TIMEOUT) \
{\
	IMG_UINT32 uiOffset, uiStart, uiCurrent; \
	IMG_INT32 iNotLastLoop;					 \
	for(uiOffset = 0, uiStart = OSClockus(), uiCurrent = uiStart + 1, iNotLastLoop = 1;\
		((uiCurrent - uiStart + uiOffset) < (TIMEOUT)) || iNotLastLoop--;				\
		uiCurrent = OSClockus(),													\
		uiOffset = uiCurrent < uiStart ? IMG_UINT32_MAX - uiStart : uiOffset,		\
		uiStart = uiCurrent < uiStart ? 0 : uiStart)

#define END_LOOP_UNTIL_TIMEOUT() \
}

#endif /* SRVKM_H */
