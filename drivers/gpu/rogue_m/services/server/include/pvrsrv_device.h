/**************************************************************************/ /*!
@File
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

#ifndef __PVRSRV_DEVICE_H__
#define __PVRSRV_DEVICE_H__

#include "servicesext.h"
#include "pvrsrv_device_types.h"
#include "img_types.h"
#include "ra.h"
#include "physheap.h"
#include "rgx_fwif_km.h"
#include "pmr.h"
#include "lock.h"
#include "pvr_dvfs.h"

typedef struct _PVRSRV_DEVICE_CONFIG_ PVRSRV_DEVICE_CONFIG;

/*! The CPU physical base of the LMA physical heap is used as the base for
 *  device memory physical heap allocations */
#define PVRSRV_DEVICE_CONFIG_LMA_USE_CPU_ADDR	(1<<0)

/*
 *  The maximum number of physical heaps associated
 *  with a device
 */
typedef enum
{
	PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL = 0,
	PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL = 1,
	PVRSRV_DEVICE_PHYS_HEAP_LAST
}PVRSRV_DEVICE_PHYS_HEAP;

typedef enum
{
	PVRSRV_DEVICE_IRQ_ACTIVE_SYSDEFAULT = 0,
	PVRSRV_DEVICE_IRQ_ACTIVE_LOW,
	PVRSRV_DEVICE_IRQ_ACTIVE_HIGH
}PVRSRV_DEVICE_IRQ_ACTIVE_LEVEL;

typedef IMG_VOID (*PFN_MISR)(IMG_VOID *pvData);

typedef IMG_BOOL (*PFN_LISR)(IMG_VOID *pvData);

typedef IMG_UINT32 (*PFN_SYS_DEV_CLK_FREQ_GET)(IMG_HANDLE hSysData);

typedef PVRSRV_ERROR (*PFN_SYS_DEV_PRE_POWER)(PVRSRV_DEV_POWER_STATE eNewPowerState,
                                              PVRSRV_DEV_POWER_STATE eCurrentPowerState,
											  IMG_BOOL bForced);


typedef PVRSRV_ERROR (*PFN_SYS_DEV_POST_POWER)(PVRSRV_DEV_POWER_STATE eNewPowerState,
                                               PVRSRV_DEV_POWER_STATE eCurrentPowerState,
											   IMG_BOOL bForced);

typedef IMG_VOID (*PFN_SYS_DEV_INTERRUPT_HANDLED)(PVRSRV_DEVICE_CONFIG *psDevConfig);

typedef PVRSRV_ERROR (*PFN_SYS_DEV_CHECK_MEM_ALLOC_SIZE)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
														 IMG_UINT64 ui64MemSize);

struct _PVRSRV_DEVICE_CONFIG_
{
	/*! Configuration flags */
	IMG_UINT32			uiFlags;

	/*! Name of the device (used when registering the IRQ) */
	IMG_CHAR			*pszName;

	/*! Type of device this is */
	PVRSRV_DEVICE_TYPE		eDeviceType;

	/*! Register bank address */
	IMG_CPU_PHYADDR			sRegsCpuPBase;
	/*! Register bank size */
	IMG_UINT32			ui32RegsSize;
	/*! Device interrupt number */
	IMG_UINT32			ui32IRQ;

	/*! The device interrupt is shared */
	IMG_BOOL			bIRQIsShared;

	/*! IRQ polarity */
	PVRSRV_DEVICE_IRQ_ACTIVE_LEVEL	eIRQActiveLevel;

	/*! Device specific data handle */
	IMG_HANDLE			hDevData;

	/*! System specific data. This gets passed into system callback functions */
	IMG_HANDLE			hSysData;

	/*! ID of the Physical memory heap to use
	 *! The first entry (aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL])  will be used for allocations
	 *!  where the PVRSRV_MEMALLOCFLAG_CPU_LOCAL flag is not set. Normally this will be the PhysHeapID
	 *!  of an LMA heap (but the configuration could specify a UMA heap here, if desired)
	 *! The second entry (aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL]) will be used for allocations
	 *!  where the PVRSRV_MEMALLOCFLAG_CPU_LOCAL flag is set. Normally this will be the PhysHeapID
	 *!  of a UMA heap (but the configuration could specify an LMA heap here, if desired)
	 *! In the event of there being only one Physical Heap, the configuration should specify the
	 *!  same heap details in both entries */
	IMG_UINT32			aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_LAST];

	/*! Callback to inform the device we about to change power state */
	PFN_SYS_DEV_PRE_POWER		pfnPrePowerState;

	/*! Callback to inform the device we have finished the power state change */
	PFN_SYS_DEV_POST_POWER		pfnPostPowerState;

	/*! Callback to obtain the clock frequency from the device */
	PFN_SYS_DEV_CLK_FREQ_GET	pfnClockFreqGet;

	/*! Callback to inform the device that an interrupt has been handled */
	PFN_SYS_DEV_INTERRUPT_HANDLED	pfnInterruptHandled;

	/*! Callback to handle memory budgeting */
	PFN_SYS_DEV_CHECK_MEM_ALLOC_SIZE	pfnCheckMemAllocSize;

	/*! Current breakpoint data master */
	RGXFWIF_DM			eBPDM;
	/*! A Breakpoint has been set */
	IMG_BOOL			bBPSet;	

#if defined(PVR_DVFS)
	PVRSRV_DVFS			sDVFS;
#endif
};

typedef PVRSRV_ERROR (*PFN_SYSTEM_PRE_POWER_STATE)(PVRSRV_SYS_POWER_STATE eNewPowerState);
typedef PVRSRV_ERROR (*PFN_SYSTEM_POST_POWER_STATE)(PVRSRV_SYS_POWER_STATE eNewPowerState);

typedef enum _PVRSRV_SYSTEM_SNOOP_MODE_ {
	PVRSRV_SYSTEM_SNOOP_NONE = 0,
	PVRSRV_SYSTEM_SNOOP_CPU_ONLY,
	PVRSRV_SYSTEM_SNOOP_DEVICE_ONLY,
	PVRSRV_SYSTEM_SNOOP_CROSS,
} PVRSRV_SYSTEM_SNOOP_MODE;

typedef struct _PVRSRV_SYSTEM_CONFIG_
{
	IMG_UINT32				uiSysFlags;
	IMG_CHAR				*pszSystemName;
	IMG_UINT32				uiDeviceCount;
	PVRSRV_DEVICE_CONFIG	*pasDevices;
	PFN_SYSTEM_PRE_POWER_STATE pfnSysPrePowerState;
	PFN_SYSTEM_POST_POWER_STATE pfnSysPostPowerState;
	PVRSRV_SYSTEM_SNOOP_MODE eCacheSnoopingMode;

	PHYS_HEAP_CONFIG		*pasPhysHeaps;
	IMG_UINT32				ui32PhysHeapCount;

	IMG_UINT32              *pui32BIFTilingHeapConfigs;
	IMG_UINT32              ui32BIFTilingHeapCount;
} PVRSRV_SYSTEM_CONFIG;


#endif /* __PVRSRV_DEVICE_H__*/
