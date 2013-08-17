/*************************************************************************/ /*!
@Title          Common System APIs and structures
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides common system-specific declarations and macros
                that are supported by all system's
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

#include "sysconfig.h"      /* System specific system defines */
#include "sysinfo.h"		/* globally accessible system info */
#include "servicesint.h"
#include "queue.h"
#include "power.h"
#include "resman.h"
#include "ra.h"
#include "device.h"
#include "buffer_manager.h"
#include "pvr_debug.h"
#include "services.h"

#if defined(NO_HARDWARE) && defined(__linux__) && defined(__KERNEL__)
#include <asm/io.h>
#endif

#if defined (__cplusplus)
extern "C" {
#endif

/*!
 ****************************************************************************
	device id management structure
 ****************************************************************************/
typedef struct _SYS_DEVICE_ID_TAG
{
	IMG_UINT32	uiID;
	IMG_BOOL	bInUse;

} SYS_DEVICE_ID;


/*
	the max number of independent local backing stores services supports
	(grow this number if ever required)
*/
#define SYS_MAX_LOCAL_DEVMEM_ARENAS	4

typedef IMG_HANDLE (*PFN_HTIMER_CREATE) (IMG_VOID);
typedef IMG_UINT32 (*PFN_HTIMER_GETUS) (IMG_HANDLE);
typedef IMG_VOID (*PFN_HTIMER_DESTROY) (IMG_HANDLE);
/*!
 ****************************************************************************
	Top level system data structure
 ****************************************************************************/
typedef struct _SYS_DATA_TAG_
{
    IMG_UINT32                  ui32NumDevices;      	   	/*!< number of devices in system */
	SYS_DEVICE_ID				sDeviceID[SYS_DEVICE_COUNT];
    PVRSRV_DEVICE_NODE			*psDeviceNodeList;			/*!< list of private device info structures */
    PVRSRV_POWER_DEV			*psPowerDeviceList;			/*!< list of devices registered with the power manager */
	PVRSRV_RESOURCE				sPowerStateChangeResource;	/*!< lock for power state transitions */
   	PVRSRV_SYS_POWER_STATE		eCurrentPowerState;			/*!< current Kernel services power state */
   	PVRSRV_SYS_POWER_STATE		eFailedPowerState;			/*!< Kernel services power state (Failed to transition to) */
   	IMG_UINT32		 			ui32CurrentOSPowerState;	/*!< current OS specific power state */
    PVRSRV_QUEUE_INFO           *psQueueList;           	/*!< list of all command queues in the system */
   	PVRSRV_KERNEL_SYNC_INFO 	*psSharedSyncInfoList;		/*!< list of cross process syncinfos */
    IMG_PVOID                   pvEnvSpecificData;      	/*!< Environment specific data */
    IMG_PVOID                   pvSysSpecificData;    	  	/*!< Unique to system, accessible at system layer only */
	PVRSRV_RESOURCE				sQProcessResource;			/*!< Command Q processing access lock */
	IMG_VOID					*pvSOCRegsBase;				/*!< SOC registers base linear address */
    IMG_HANDLE                  hSOCTimerRegisterOSMemHandle; /*!< SOC Timer register (if present) */
	IMG_UINT32					*pvSOCTimerRegisterKM;		/*!< SOC Timer register (if present) */
	IMG_VOID					*pvSOCClockGateRegsBase;	/*!< SOC Clock gating registers (if present) */
	IMG_UINT32					ui32SOCClockGateRegsSize;
															
	struct _DEVICE_COMMAND_DATA_ *apsDeviceCommandData[SYS_DEVICE_COUNT];
															/*!< command complete data and callback function store for every command for every device */

	RA_ARENA					*apsLocalDevMemArena[SYS_MAX_LOCAL_DEVMEM_ARENAS]; /*!< RA Arenas for local device memory heap management */

    IMG_CHAR                    *pszVersionString;          /*!< Human readable string showing relevent system version info */
	PVRSRV_EVENTOBJECT			*psGlobalEventObject;		/*!< OS Global Event Object */

	PVRSRV_MISC_INFO_CPUCACHEOP_TYPE ePendingCacheOpType;	/*!< Deferred CPU cache op control */

	PFN_HTIMER_CREATE	pfnHighResTimerCreate;
	PFN_HTIMER_GETUS	pfnHighResTimerGetus;
	PFN_HTIMER_DESTROY	pfnHighResTimerDestroy;
} SYS_DATA;


/****************************************************************************
 *	common function prototypes
 ****************************************************************************/

#if defined (CUSTOM_DISPLAY_SEGMENT)
PVRSRV_ERROR SysGetDisplaySegmentAddress (IMG_VOID *pvDevInfo, IMG_VOID *pvPhysicalAddress, IMG_UINT32 *pui32Length);
#endif

PVRSRV_ERROR SysInitialise(IMG_VOID);
PVRSRV_ERROR SysFinalise(IMG_VOID);

PVRSRV_ERROR SysDeinitialise(SYS_DATA *psSysData);
PVRSRV_ERROR SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE eDeviceType,
									IMG_VOID **ppvDeviceMap);

IMG_VOID SysRegisterExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode);
IMG_VOID SysRemoveExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_UINT32 SysGetInterruptSource(SYS_DATA			*psSysData,
								 PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_VOID SysClearInterrupts(SYS_DATA* psSysData, IMG_UINT32 ui32ClearBits);

PVRSRV_ERROR SysResetDevice(IMG_UINT32 ui32DeviceIndex);

PVRSRV_ERROR SysSystemSetPowerMargin(IMG_UINT32 ui32offset);
PVRSRV_ERROR SysSystemPrePowerState(PVRSRV_SYS_POWER_STATE eNewPowerState);
PVRSRV_ERROR SysSystemPostPowerState(PVRSRV_SYS_POWER_STATE eNewPowerState);
PVRSRV_ERROR SysDevicePrePowerState(IMG_UINT32 ui32DeviceIndex,
									PVRSRV_DEV_POWER_STATE eNewPowerState,
									PVRSRV_DEV_POWER_STATE eCurrentPowerState);
PVRSRV_ERROR SysDevicePostPowerState(IMG_UINT32 ui32DeviceIndex,
									 PVRSRV_DEV_POWER_STATE eNewPowerState,
									 PVRSRV_DEV_POWER_STATE eCurrentPowerState);

#if defined(SYS_SUPPORTS_SGX_IDLE_CALLBACK)
IMG_VOID SysSGXIdleTransition(IMG_BOOL bSGXIdle);
#endif /* SYS_SUPPORTS_SGX_IDLE_CALLBACK */

#if defined(SYS_CUSTOM_POWERLOCK_WRAP)
PVRSRV_ERROR SysPowerLockWrap(IMG_BOOL bTryLock);
IMG_VOID SysPowerLockUnwrap(IMG_VOID);
#endif

PVRSRV_ERROR SysOEMFunction (	IMG_UINT32	ui32ID,
								IMG_VOID	*pvIn,
								IMG_UINT32  ulInSize,
								IMG_VOID	*pvOut,
								IMG_UINT32	ulOutSize);


IMG_DEV_PHYADDR SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_CPU_PHYADDR cpu_paddr);
IMG_DEV_PHYADDR SysSysPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_SYS_PHYADDR SysPAddr);
IMG_SYS_PHYADDR SysDevPAddrToSysPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_DEV_PHYADDR SysPAddr);
IMG_CPU_PHYADDR SysSysPAddrToCpuPAddr (IMG_SYS_PHYADDR SysPAddr);
IMG_SYS_PHYADDR SysCpuPAddrToSysPAddr (IMG_CPU_PHYADDR cpu_paddr);
#if defined(PVR_LMA)
IMG_BOOL SysVerifyCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_CPU_PHYADDR CpuPAddr);
IMG_BOOL SysVerifySysPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_SYS_PHYADDR SysPAddr);
#endif

extern SYS_DATA* gpsSysData;


#if !defined(USE_CODE)

/*!
******************************************************************************

 @Function	SysAcquireData

 @Description returns reference to to sysdata
 				creating one on first call

 @Input    ppsSysData - pointer to copy reference into

 @Return   ppsSysData updated

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(SysAcquireData)
#endif
static INLINE IMG_VOID SysAcquireData(SYS_DATA **ppsSysData)
{
	/* Copy pointer back system information pointer */
	*ppsSysData = gpsSysData;

	/*
		Verify we've not been called before being initialised. Instinctively
		we should do this check first, but in the failing case we'll just write
		null back and the compiler won't warn about an uninitialised varible.
	*/
	PVR_ASSERT (gpsSysData != IMG_NULL);
}


/*!
******************************************************************************

 @Function	SysAcquireDataNoCheck

 @Description returns reference to to sysdata
 				creating one on first call

 @Input    none

 @Return   psSysData - pointer to copy reference into

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(SysAcquireDataNoCheck)
#endif
static INLINE SYS_DATA * SysAcquireDataNoCheck(IMG_VOID)
{
	/* return pointer back system information pointer */
	return gpsSysData;
}


/*!
******************************************************************************

 @Function	SysInitialiseCommon

 @Description Performs system initialisation common to all systems

 @Input    psSysData - pointer to system data

 @Return   PVRSRV_ERROR  :

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(SysInitialiseCommon)
#endif
static INLINE PVRSRV_ERROR SysInitialiseCommon(SYS_DATA *psSysData)
{
	PVRSRV_ERROR	eError;

	/* Initialise Services */
	eError = PVRSRVInit(psSysData);

	return eError;
}

/*!
******************************************************************************

 @Function	SysDeinitialiseCommon

 @Description Performs system deinitialisation common to all systems

 @Input    psSysData - pointer to system data

 @Return   PVRSRV_ERROR  :

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(SysDeinitialiseCommon)
#endif
static INLINE IMG_VOID SysDeinitialiseCommon(SYS_DATA *psSysData)
{
	/* De-initialise Services */
	PVRSRVDeInit(psSysData);

	OSDestroyResource(&psSysData->sPowerStateChangeResource);
}
#endif /* !defined(USE_CODE) */


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

#if defined(__cplusplus)
}
#endif

#ifdef INLINE_IS_PRAGMA
#pragma inline(SysHighResTimerCreate)
#endif
static INLINE IMG_HANDLE SysHighResTimerCreate(IMG_VOID)
{
	SYS_DATA *psSysData;

	SysAcquireData(&psSysData);
	return psSysData->pfnHighResTimerCreate();
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SysHighResTimerGetus)
#endif
static INLINE IMG_UINT32 SysHighResTimerGetus(IMG_HANDLE hTimer)
{
	SYS_DATA *psSysData;

	SysAcquireData(&psSysData);
	return psSysData->pfnHighResTimerGetus(hTimer);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SysHighResTimerDestroy)
#endif
static INLINE IMG_VOID SysHighResTimerDestroy(IMG_HANDLE hTimer)
{
	SYS_DATA *psSysData;

	SysAcquireData(&psSysData);
	psSysData->pfnHighResTimerDestroy(hTimer);
}
#endif

/*****************************************************************************
 End of file (syscommon.h)
*****************************************************************************/
