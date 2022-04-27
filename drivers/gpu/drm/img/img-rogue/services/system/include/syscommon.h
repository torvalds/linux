/**************************************************************************/ /*!
@File
@Title          Common System APIs and structures
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides common system-specific declarations and
                macros that are supported by all systems
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

#if !defined(SYSCOMMON_H)
#define SYSCOMMON_H

#include "img_types.h"
#include "pvr_notifier.h"
#include "pvrsrv_device.h"
#include "pvrsrv_error.h"

/*************************************************************************/ /*!
@Description    Pointer to a Low-level Interrupt Service Routine (LISR).
@Input  pvData  Private data provided to the LISR.
@Return         True if interrupt handled, false otherwise.
*/ /**************************************************************************/
typedef IMG_BOOL (*PFN_LISR)(void *pvData);

/**************************************************************************/ /*!
@Function       SysDevInit
@Description    System specific device initialisation function.
@Input          pvOSDevice          pointer to the OS device reference
@Input          ppsDevConfig        returned device configuration info
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /***************************************************************************/
PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig);

/**************************************************************************/ /*!
@Function       SysDevDeInit
@Description    System specific device deinitialisation function.
@Input          psDevConfig        device configuration info of the device to be
                                   deinitialised
@Return         None.
*/ /***************************************************************************/
void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig);

/**************************************************************************/ /*!
@Function       SysDebugInfo
@Description    Dump system specific device debug information.
@Input          psDevConfig         pointer to device configuration info
@Input          pfnDumpDebugPrintf  the 'printf' function to be called to
                                    display the debug info
@Input          pvDumpDebugFile     optional file identifier to be passed to
                                    the 'printf' function if required
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /***************************************************************************/
PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile);

/**************************************************************************/ /*!
@Function       SysInstallDeviceLISR
@Description    Installs the system Low-level Interrupt Service Routine (LISR)
                which handles low-level processing of interrupts from the device
                (GPU).
                The LISR will be invoked when the device raises an interrupt. An
                LISR may not be descheduled, so code which needs to do so should
                be placed in an MISR.
                The installed LISR will schedule any MISRs once it has completed
                its interrupt processing, by calling OSScheduleMISR().
@Input          hSysData      pointer to the system data of the device
@Input          ui32IRQ       the IRQ on which the LISR is to be installed
@Input          pszName       name of the module installing the LISR
@Input          pfnLISR       pointer to the function to be installed as the
                              LISR
@Input          pvData        private data provided to the LISR
@Output         phLISRData    handle to the installed LISR (to be used for a
                              subsequent uninstall)
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /***************************************************************************/
PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
								  IMG_UINT32 ui32IRQ,
								  const IMG_CHAR *pszName,
								  PFN_LISR pfnLISR,
								  void *pvData,
								  IMG_HANDLE *phLISRData);

/**************************************************************************/ /*!
@Function       SysUninstallDeviceLISR
@Description    Uninstalls the system Low-level Interrupt Service Routine (LISR)
                which handles low-level processing of interrupts from the device
                (GPU).
@Input          hLISRData     handle of the LISR to be uninstalled
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /***************************************************************************/
PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData);

/**************************************************************************/ /*!
@Function       SysRGXErrorNotify
@Description    Error reporting callback function, registered as the
                pfnSysDevErrorNotify member of the PVRSRV_DEVICE_CONFIG
                struct. System layer will be notified of device errors and
                resets via this callback.
                NB. implementers should ensure that the minimal amount of
                work is done in this callback function, as it will be
                executed in the main RGX MISR. (e.g. any blocking or lengthy
                work should be performed by a worker queue/thread instead).
@Input          hSysData      pointer to the system data of the device
@Output         psErrorData   structure containing details of the reported error
@Return         None.
*/ /***************************************************************************/
void SysRGXErrorNotify(IMG_HANDLE hSysData,
                       PVRSRV_ROBUSTNESS_NOTIFY_DATA *psErrorData);

#endif /* !defined(SYSCOMMON_H) */
