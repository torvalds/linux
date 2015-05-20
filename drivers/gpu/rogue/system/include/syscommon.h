/*************************************************************************/ /*!
@File
@Title          Common System APIs and structures
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides common system-specific declarations and macros
                that are supported by all systems
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

#ifndef _SYSCOMMON_H
#define _SYSCOMMON_H

#include "osfunc.h"

#if defined(NO_HARDWARE) && defined(__linux__) && defined(__KERNEL__)
#include <asm/io.h>
#endif

#if defined (__cplusplus)
extern "C" {
#endif
#include "pvrsrv.h"

PVRSRV_ERROR SysCreateConfigData(PVRSRV_SYSTEM_CONFIG **ppsSysConfig);
IMG_VOID SysDestroyConfigData(PVRSRV_SYSTEM_CONFIG *psSysConfig);
PVRSRV_ERROR SysAcquireSystemData(IMG_HANDLE hSysData);
PVRSRV_ERROR SysReleaseSystemData(IMG_HANDLE hSysData);
PVRSRV_ERROR SysDebugInfo(PVRSRV_SYSTEM_CONFIG *psSysConfig, DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf);

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
PVRSRV_ERROR SysInstallDeviceLISR(IMG_UINT32 ui32IRQ,
				  IMG_CHAR *pszName,
				  PFN_LISR pfnLISR,
				  IMG_PVOID pvData,
				  IMG_HANDLE *phLISRData);

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData);

#if defined(SUPPORT_DRM)
IMG_BOOL SystemISRHandler(IMG_VOID *pvData);
#endif
#endif /* defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING) */


/*
 * SysReadHWReg and SysWriteHWReg differ from OSReadHWReg and OSWriteHWReg
 * in that they are always intended for use with real hardware, even on
 * NO_HARDWARE systems.
 */
#if !(defined(NO_HARDWARE) && defined(__linux__) && defined(__KERNEL__))
#define	SysReadHWReg(p, o) OSReadHWReg(p, o)
#define SysWriteHWReg(p, o, v) OSWriteHWReg(p, o, v)
#else	/* !(defined(NO_HARDWARE) && defined(__linux__)) */
/*!
******************************************************************************

 @Function	SysReadHWReg

 @Description

 register read function

 @input pvLinRegBaseAddr :	lin addr of register block base

 @input ui32Offset :

 @Return   register value

******************************************************************************/
static inline IMG_UINT32 SysReadHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset)
{
	return (IMG_UINT32) readl(pvLinRegBaseAddr + ui32Offset);
}

/*!
******************************************************************************

 @Function	SysWriteHWReg

 @Description

 register write function

 @input pvLinRegBaseAddr :	lin addr of register block base

 @input ui32Offset :

 @input ui32Value :

 @Return   none

******************************************************************************/
static inline IMG_VOID SysWriteHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value)
{
	writel(ui32Value, pvLinRegBaseAddr + ui32Offset);
}
#endif	/* !(defined(NO_HARDWARE) && defined(__linux__)) */

/*!
******************************************************************************

 @Function		SysCheckMemAllocSize

 @Description	Function to apply memory budgeting policies

 @input			psDevNode

 @input			uiChunkSize

 @input			ui32NumPhysChunks

 @Return		PVRSRV_ERROR

******************************************************************************/
FORCE_INLINE PVRSRV_ERROR SysCheckMemAllocSize(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
												IMG_UINT64 ui64MemSize)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(ui64MemSize);

	return PVRSRV_OK;
}

#if defined(__cplusplus)
}
#endif

#endif

/*****************************************************************************
 End of file (syscommon.h)
*****************************************************************************/
