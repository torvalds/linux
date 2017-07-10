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

#include "img_types.h"
#include "physheap.h"
#include "pvrsrv_error.h"
#include "rgx_fwif_km.h"
#include "servicesext.h"

#if defined(PVR_DVFS) || defined(SUPPORT_PDVFS)
#include "pvr_dvfs.h"
#endif

typedef struct _PVRSRV_DEVICE_CONFIG_ PVRSRV_DEVICE_CONFIG;

/*
 * All the heaps from which regular device memory allocations can be made in
 * terms of their locality to the respective device.
 */
typedef enum
{
	PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL = 0,
	PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL = 1,
	PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL = 2,
	PVRSRV_DEVICE_PHYS_HEAP_LAST
} PVRSRV_DEVICE_PHYS_HEAP;

typedef enum
{
	PVRSRV_DEVICE_LOCAL_MEMORY_ARENA_MAPPABLE = 0,
	PVRSRV_DEVICE_LOCAL_MEMORY_ARENA_NON_MAPPABLE = 1,
	PVRSRV_DEVICE_LOCAL_MEMORY_ARENA_LAST
} PVRSRV_DEVICE_LOCAL_MEMORY_ARENA;

typedef enum _PVRSRV_DEVICE_SNOOP_MODE_
{
	PVRSRV_DEVICE_SNOOP_NONE = 0,
	PVRSRV_DEVICE_SNOOP_CPU_ONLY,
	PVRSRV_DEVICE_SNOOP_DEVICE_ONLY,
	PVRSRV_DEVICE_SNOOP_CROSS,
} PVRSRV_DEVICE_SNOOP_MODE;

typedef IMG_UINT32
(*PFN_SYS_DEV_CLK_FREQ_GET)(IMG_HANDLE hSysData);

typedef PVRSRV_ERROR
(*PFN_SYS_DEV_PRE_POWER)(IMG_HANDLE hSysData,
						 PVRSRV_DEV_POWER_STATE eNewPowerState,
						 PVRSRV_DEV_POWER_STATE eCurrentPowerState,
						 IMG_BOOL bForced);

typedef PVRSRV_ERROR
(*PFN_SYS_DEV_POST_POWER)(IMG_HANDLE hSysData,
						  PVRSRV_DEV_POWER_STATE eNewPowerState,
						  PVRSRV_DEV_POWER_STATE eCurrentPowerState,
						  IMG_BOOL bForced);

typedef void
(*PFN_SYS_DEV_INTERRUPT_HANDLED)(PVRSRV_DEVICE_CONFIG *psDevConfig);

typedef PVRSRV_ERROR
(*PFN_SYS_DEV_CHECK_MEM_ALLOC_SIZE)(IMG_HANDLE hSysData,
									IMG_UINT64 ui64MemSize);

typedef void (*PFN_SYS_DEV_FEAT_DEP_INIT)(PVRSRV_DEVICE_CONFIG *, IMG_UINT64);

#if defined(SUPPORT_TRUSTED_DEVICE)

#define PVRSRV_DEVICE_FW_CODE_REGION          (0)
#define PVRSRV_DEVICE_FW_COREMEM_CODE_REGION  (1)

typedef struct _PVRSRV_TD_FW_PARAMS_
{
	const void *pvFirmware;
	IMG_UINT32 ui32FirmwareSize;
	IMG_DEV_VIRTADDR sFWCodeDevVAddrBase;
	IMG_DEV_VIRTADDR sFWDataDevVAddrBase;
	RGXFWIF_DEV_VIRTADDR sFWCorememCodeFWAddr;
	RGXFWIF_DEV_VIRTADDR sFWInitFWAddr;
} PVRSRV_TD_FW_PARAMS;

typedef PVRSRV_ERROR
(*PFN_TD_SEND_FW_IMAGE)(IMG_HANDLE hSysData,
						PVRSRV_TD_FW_PARAMS *psTDFWParams);

typedef struct _PVRSRV_TD_POWER_PARAMS_
{
	IMG_DEV_PHYADDR sPCAddr; /* META only used param */

	/* MIPS only used fields */
	IMG_DEV_PHYADDR sGPURegAddr;
	IMG_DEV_PHYADDR sBootRemapAddr;
	IMG_DEV_PHYADDR sCodeRemapAddr;
	IMG_DEV_PHYADDR sDataRemapAddr;
} PVRSRV_TD_POWER_PARAMS;

typedef PVRSRV_ERROR
(*PFN_TD_SET_POWER_PARAMS)(IMG_HANDLE hSysData,
						   PVRSRV_TD_POWER_PARAMS *psTDPowerParams);

typedef PVRSRV_ERROR
(*PFN_TD_RGXSTART)(IMG_HANDLE hSysData);

typedef PVRSRV_ERROR
(*PFN_TD_RGXSTOP)(IMG_HANDLE hSysData);

typedef struct _PVRSRV_TD_SECBUF_PARAMS_
{
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_DEVMEM_ALIGN_T uiAlign;
	IMG_CPU_PHYADDR *psSecBufAddr;
	IMG_UINT64 *pui64SecBufHandle;
} PVRSRV_TD_SECBUF_PARAMS;

typedef PVRSRV_ERROR
(*PFN_TD_SECUREBUF_ALLOC)(IMG_HANDLE hSysData,
						  PVRSRV_TD_SECBUF_PARAMS *psTDSecBufParams);

typedef PVRSRV_ERROR
(*PFN_TD_SECUREBUF_FREE)(IMG_HANDLE hSysData,
						 IMG_UINT64 ui64SecBufHandle);
#endif /* defined(SUPPORT_TRUSTED_DEVICE) */

struct _PVRSRV_DEVICE_CONFIG_
{
	/*! OS device passed to SysDevInit (linux: 'struct device') */
	void *pvOSDevice;

	/*!
	 *! Service representation of pvOSDevice. Should be set to NULL when the
	 *! config is created in SysDevInit. Set by Services once a device node has
	 *! been created for this config and unset before SysDevDeInit is called.
	 */
	struct _PVRSRV_DEVICE_NODE_ *psDevNode;

	/*! Name of the device */
	IMG_CHAR *pszName;

	/*! Version of the device (optional) */
	IMG_CHAR *pszVersion;

	/*! Register bank address */
	IMG_CPU_PHYADDR sRegsCpuPBase;
	/*! Register bank size */
	IMG_UINT32 ui32RegsSize;
	/*! Device interrupt number */
	IMG_UINT32 ui32IRQ;

	PVRSRV_DEVICE_SNOOP_MODE eCacheSnoopingMode;

	/*! Device specific data handle */
	IMG_HANDLE hDevData;

	/*! System specific data that gets passed into system callback functions. */
	IMG_HANDLE hSysData;

	IMG_BOOL bHasNonMappableLocalMemory;

	PHYS_HEAP_CONFIG *pasPhysHeaps;
	IMG_UINT32 ui32PhysHeapCount;

	/*!
	 *! ID of the Physical memory heap to use.
	 *!
	 *! The first entry (aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL])
	 *! will be used for allocations where the PVRSRV_MEMALLOCFLAG_CPU_LOCAL
	 *! flag is not set. Normally this will be the PhysHeapID of an LMA heap
	 *! but the configuration could specify a UMA heap here (if desired).
	 *!
	 *! The second entry (aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL])
	 *! will be used for allocations where the PVRSRV_MEMALLOCFLAG_CPU_LOCAL
	 *! flag is set. Normally this will be the PhysHeapID of a UMA heap but
	 *! the configuration could specify an LMA heap here (if desired).
	 *!
	 *! The third entry (aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL])
	 *! will be used for allocations where the PVRSRV_MEMALLOCFLAG_FW_LOCAL
	 *! flag is set.
	 *!
	 *! In the event of there being only one Physical Heap, the configuration
	 *! should specify the same heap details in all entries.
	 */
	IMG_UINT32 aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_LAST];

	RGXFWIF_BIFTILINGMODE eBIFTilingMode;
	IMG_UINT32 *pui32BIFTilingHeapConfigs;
	IMG_UINT32 ui32BIFTilingHeapCount;

	/*!
	 *! Callbacks to change system device power state at the beginning and end
	 *! of a power state change (optional).
	 */
	PFN_SYS_DEV_PRE_POWER pfnPrePowerState;
	PFN_SYS_DEV_POST_POWER pfnPostPowerState;

	/*! Callback to obtain the clock frequency from the device (optional). */
	PFN_SYS_DEV_CLK_FREQ_GET pfnClockFreqGet;

	/*!
	 *! Callback to handle memory budgeting. Can be used to reject allocations
	 *! over a certain size (optional).
	 */
	PFN_SYS_DEV_CHECK_MEM_ALLOC_SIZE pfnCheckMemAllocSize;

#if defined(SUPPORT_TRUSTED_DEVICE)
	/*!
	 *! Callback to send FW image and FW boot time parameters to the trusted
	 *! device.
	 */
	PFN_TD_SEND_FW_IMAGE pfnTDSendFWImage;

	/*!
	 *! Callback to send parameters needed in a power transition to the trusted
	 *! device.
	 */
	PFN_TD_SET_POWER_PARAMS pfnTDSetPowerParams;

	/*! Callbacks to ping the trusted device to securely run RGXStart/Stop() */
	PFN_TD_RGXSTART pfnTDRGXStart;
	PFN_TD_RGXSTOP pfnTDRGXStop;

	/*! Callback to request allocation/freeing of secure buffers */
	PFN_TD_SECUREBUF_ALLOC pfnTDSecureBufAlloc;
	PFN_TD_SECUREBUF_FREE pfnTDSecureBufFree;
#endif /* defined(SUPPORT_TRUSTED_DEVICE) */

	/*! Function that does device feature specific system layer initialisation */
	PFN_SYS_DEV_FEAT_DEP_INIT	pfnSysDevFeatureDepInit;

#if defined(PVR_DVFS) || defined(SUPPORT_PDVFS)
	PVRSRV_DVFS sDVFS;
#endif
};

#endif /* __PVRSRV_DEVICE_H__*/
