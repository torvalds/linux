/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * Copyright 2013 by S.LSI. Samsung Electronics Inc.
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

#include "sgxdefs.h"
#include "services_headers.h"
#include "kerneldisplay.h"
#include "oemfuncs.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "pdump_km.h"
#include "servicesext.h"


#if defined(SLSI_EXYNOS5410)
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <mach/map.h>
#include "secutils.h"
#include "sec_dvfs.h"
#include "sec_control_pwr_clk.h"
#endif

#define REAL_HARDWARE 1
#define SGX544_BASEADDR 0x11800000
#define SGX_REG_SIZE 	0x20000
#define SGX_SP_SIZE		(0x10000-SGX_REG_SIZE)
//#define IRQ_3D 106
#define SGX544_IRQ GPU_IRQ_NUMBER
//static struct resource		*mem;
//static void __iomem		*io;

#define SYS_SGX_CLOCK_SPEED					(333000000)

#if 0
#define SYS_SGX_HWRECOVERY_TIMEOUT_FREQ		(100) // 10ms (100hz)
#define SYS_SGX_PDS_TIMER_FREQ				(1000) // 1ms (1000hz)
#define SYS_SGX_ACTIVE_POWER_LATENCY_MS		(500)
#else
#define SYS_SGX_HWRECOVERY_TIMEOUT_FREQ		(100) // 10ms (100hz)
#define SYS_SGX_PDS_TIMER_FREQ				(1000) // 1ms (1000hz)
#ifndef SYS_SGX_ACTIVE_POWER_LATENCY_MS
#define SYS_SGX_ACTIVE_POWER_LATENCY_MS		(500)
#endif
#endif

typedef struct _SYS_SPECIFIC_DATA_TAG_
{
	IMG_UINT32 ui32SysSpecificData;
	struct mutex sPowerLock;
} SYS_SPECIFIC_DATA;

#define SYS_SPECIFIC_DATA_ENABLE_IRQ		0x00000001UL
#define SYS_SPECIFIC_DATA_ENABLE_LISR		0x00000002UL
#define SYS_SPECIFIC_DATA_ENABLE_MISR		0x00000004UL


SYS_SPECIFIC_DATA gsSysSpecificData;


SYS_DATA* gpsSysData = (SYS_DATA*)IMG_NULL;
SYS_DATA  gsSysData;


static IMG_UINT32		gui32SGXDeviceID;
static SGX_DEVICE_MAP	gsSGXDeviceMap;


IMG_CPU_VIRTADDR gsSGXRegsCPUVAddr;
IMG_CPU_VIRTADDR gsSGXSPCPUVAddr;

char version_string[] = "SGX544 MP EXYNOS5410";

IMG_UINT32   PVRSRV_BridgeDispatchKM( IMG_UINT32  Ioctl,
									IMG_BYTE   *pInBuf,
									IMG_UINT32  InBufLen, 
									IMG_BYTE   *pOutBuf,
									IMG_UINT32  OutBufLen,
									IMG_UINT32 *pdwBytesTransferred);


static PVRSRV_ERROR SysLocateDevices(SYS_DATA *psSysData)
{
//	PVRSRV_ERROR eError;
//	IMG_CPU_PHYADDR sCpuPAddr;

	PVR_UNREFERENCED_PARAMETER(psSysData);


#if 0
	
	gsSGXDeviceMap.ui32Flags = 0x0;
	sCpuPAddr.uiAddr = SGX544_BASEADDR;
	gsSGXDeviceMap.sRegsCpuPBase = sCpuPAddr;
	gsSGXDeviceMap.sRegsSysPBase = SysCpuPAddrToSysPAddr(gsSGXDeviceMap.sRegsCpuPBase);;
	gsSGXDeviceMap.ui32RegsSize = SGX_REG_SIZE;
//	gsSGXDeviceMap.pvRegsCpuVBase = (IMG_CPU_VIRTADDR)io;

#else

	gsSGXDeviceMap.sRegsSysPBase.uiAddr = SGX544_BASEADDR;
	gsSGXDeviceMap.sRegsCpuPBase = SysSysPAddrToCpuPAddr(gsSGXDeviceMap.sRegsSysPBase);
	gsSGXDeviceMap.ui32RegsSize = SGX_REG_SIZE;
	gsSGXDeviceMap.ui32IRQ = SGX544_IRQ;
#endif


#if defined(SGX_FEATURE_HOST_PORT)
	
	gsSGXDeviceMap.sHPSysPBase.uiAddr = 0;
	gsSGXDeviceMap.sHPCpuPBase.uiAddr = 0;
	gsSGXDeviceMap.ui32HPSize = 0;
#endif

	
	gsSGXDeviceMap.sLocalMemSysPBase.uiAddr = 0;
	gsSGXDeviceMap.sLocalMemDevPBase.uiAddr = 0;
	gsSGXDeviceMap.sLocalMemCpuPBase.uiAddr = 0;
	gsSGXDeviceMap.ui32LocalMemSize = 0;

	
	gsSGXDeviceMap.ui32IRQ = SGX544_IRQ;

	

	return PVRSRV_OK;
}



PVRSRV_ERROR SysInitialise()
{
	IMG_UINT32			i;
	PVRSRV_ERROR 		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SGX_TIMING_INFORMATION*	psTimingInfo;
	struct platform_device	*pdev;

	gpsSysData = &gsSysData;
	OSMemSet(gpsSysData, 0, sizeof(SYS_DATA));
	mutex_init(&gsSysSpecificData.sPowerLock);

	pdev = gpsPVRLDMDev;

	PVR_LOG(("SysInitialise: start"));
	eError = sec_gpu_pwr_clk_init();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SysInitialise: Failed to sec_gpu_pwr_clk_init"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	eError = sec_gpu_pwr_clk_state_set(GPU_PWR_CLK_STATE_ON);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SysInitialise: Failed to sec_gpu_pwr_clk_state_set GPU_PWR_CLK_STATE_ON"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

#if defined(CONFIG_PVR_SGX_DVFS)
	sec_gpu_dvfs_init();
	sec_gpu_utilization_init();
#endif

	eError = OSInitEnvData(&gpsSysData->pvEnvSpecificData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to setup env structure"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

	gpsSysData->pvSysSpecificData = (IMG_PVOID)&gsSysSpecificData;
	OSMemSet(&gsSGXDeviceMap, 0, sizeof(SGX_DEVICE_MAP));
	
 #if !defined(SGX_DYNAMIC_TIMING_INFO)
	psTimingInfo = &gsSGXDeviceMap.sTimingInfo;
	psTimingInfo->ui32CoreClockSpeed = SYS_SGX_CLOCK_SPEED;
	psTimingInfo->ui32HWRecoveryFreq = SYS_SGX_HWRECOVERY_TIMEOUT_FREQ; 
	psTimingInfo->ui32ActivePowManLatencyms = SYS_SGX_ACTIVE_POWER_LATENCY_MS; 
	psTimingInfo->ui32uKernelFreq = SYS_SGX_PDS_TIMER_FREQ; 

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	psTimingInfo->bEnableActivePM = IMG_TRUE;
#else	
	psTimingInfo->bEnableActivePM = IMG_FALSE;
#endif 
#endif 
	gpsSysData->ui32NumDevices = SYS_DEVICE_COUNT;

	
	for(i=0; i<SYS_DEVICE_COUNT; i++)
	{
		gpsSysData->sDeviceID[i].uiID = i;
		gpsSysData->sDeviceID[i].bInUse = IMG_FALSE;
	}

	gpsSysData->psDeviceNodeList = IMG_NULL;
	gpsSysData->psQueueList = IMG_NULL;

	eError = SysInitialiseCommon(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed in SysInitialiseCommon"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

	
	eError = SysLocateDevices(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to locate devices"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

	
	eError = PVRSRVRegisterDevice(gpsSysData, SGXRegisterDevice, 1, &gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to register device!"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

		
	psDeviceNode = gpsSysData->psDeviceNodeList;
	while(psDeviceNode)
	{
		
		switch(psDeviceNode->sDevId.eDeviceType)
		{
			case PVRSRV_DEVICE_TYPE_SGX:
			{
				DEVICE_MEMORY_INFO *psDevMemoryInfo;
				DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
				IMG_UINT32 ui32MemConfig;

				if(gpsSysData->apsLocalDevMemArena[0] != IMG_NULL)
				{
					
					psDeviceNode->psLocalDevMemArena = gpsSysData->apsLocalDevMemArena[0];
					ui32MemConfig = PVRSRV_BACKINGSTORE_LOCALMEM_CONTIG;
				}
				else
				{
					
					psDeviceNode->psLocalDevMemArena = IMG_NULL;
					ui32MemConfig = PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG;
				}

				
				psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
				psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;

				
				for(i=0; i<psDevMemoryInfo->ui32HeapCount; i++)
				{	
					psDeviceMemoryHeap[i].psLocalDevMemArena = gpsSysData->apsLocalDevMemArena[0];	
					psDeviceMemoryHeap[i].ui32Attribs |= ui32MemConfig;
				}

				break;
			}
			default:
				PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to find SGX device node!"));
				return PVRSRV_ERROR_INIT_FAILURE;
		}

		
		psDeviceNode = psDeviceNode->psNext;
	}



	PDUMPINIT();

	
	eError = PVRSRVInitialiseDevice (gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to initialise device!"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	eError = sec_gpu_pwr_clk_state_set(GPU_PWR_CLK_STATE_OFF);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SysInitialise: SGX mode change (GPU_PWR_CLK_STATE_OFF) fail"));
		return eError;
	}
#endif

	PVR_LOG(("SysInitialise: end"));
	return PVRSRV_OK;
}


PVRSRV_ERROR SysFinalise(IMG_VOID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
    
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	eError = sec_gpu_pwr_clk_state_set(GPU_PWR_CLK_STATE_ON);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SysFinalise: SGX mode change (GPU_PWR_CLK_STATE_ON) fail"));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
#endif	
#if defined(SYS_USING_INTERRUPTS)

	eError = OSInstallMISR(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"OSInstallMISR: Failed to install MISR"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	gsSysSpecificData.ui32SysSpecificData |= SYS_SPECIFIC_DATA_ENABLE_MISR;

	
	eError = OSInstallSystemLISR(gpsSysData, gsSGXDeviceMap.ui32IRQ);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"OSInstallSystemLISR: Failed to install ISR"));
		OSUninstallMISR(gpsSysData);
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	gsSysSpecificData.ui32SysSpecificData |= SYS_SPECIFIC_DATA_ENABLE_LISR;
	
//	SysEnableInterrupts(gpsSysData);
	gsSysSpecificData.ui32SysSpecificData |= SYS_SPECIFIC_DATA_ENABLE_IRQ;
#endif 

	
#if 0
	gpsSysData->pszVersionString = SysCreateVersionString(gsSGXDeviceMap.sRegsCpuPBase);
#else
	gpsSysData->pszVersionString=version_string;
#endif
	if (!gpsSysData->pszVersionString)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to create a system version string"));
    }
	else
	{
		PVR_LOG(("SysFinalise: Version string: %s", gpsSysData->pszVersionString));
	}

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	eError = sec_gpu_pwr_clk_state_set(GPU_PWR_CLK_STATE_OFF);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SysFinalise: SGX mode change (GPU_PWR_CLK_STATE_OFF) fail"));
		return eError;
	}
#endif
	
	return PVRSRV_OK;
}



PVRSRV_ERROR SysDeinitialise (SYS_DATA *psSysData)
{
	SYS_SPECIFIC_DATA * psSysSpecData;
	PVRSRV_ERROR eError;


	PVR_UNREFERENCED_PARAMETER(psSysData);

	if (psSysData == IMG_NULL) {
		PVR_DPF((PVR_DBG_ERROR, "SysDeinitialise: Called with NULL SYS_DATA pointer.  Probably called before."));
		return PVRSRV_OK;
	}

#if defined(SYS_USING_INTERRUPTS)

	psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

	if (psSysSpecData->ui32SysSpecificData & SYS_SPECIFIC_DATA_ENABLE_LISR)
	{	
		eError = OSUninstallSystemLISR(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: OSUninstallSystemLISR failed"));
			return eError;
		}
	}
	if (psSysSpecData->ui32SysSpecificData & SYS_SPECIFIC_DATA_ENABLE_MISR)
	{
		eError = OSUninstallMISR(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: OSUninstallMISR failed"));
			return eError;
		}
	}
#endif

	
	eError = PVRSRVDeinitialiseDevice (gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: failed to de-init the device"));
		return eError;
	}

	eError = OSDeInitEnvData(gpsSysData->pvEnvSpecificData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: failed to de-init env structure"));
		return eError;
	}

	SysDeinitialiseCommon(gpsSysData);


	OSBaseFreeContigMemory(SGX_REG_SIZE, gsSGXRegsCPUVAddr, gsSGXDeviceMap.sRegsCpuPBase);

	gpsSysData = IMG_NULL;

	sec_gpu_pwr_clk_deinit();
	PDUMPDEINIT();

	return PVRSRV_OK;
}



PVRSRV_ERROR SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE eDeviceType,
									IMG_VOID **ppvDeviceMap)
{

	switch(eDeviceType)
	{
		case PVRSRV_DEVICE_TYPE_SGX:
		{
			
			*ppvDeviceMap = (IMG_VOID*)&gsSGXDeviceMap;

			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR,"SysGetDeviceMemoryMap: unsupported device type"));
		}
	}
	return PVRSRV_OK;
}



IMG_DEV_PHYADDR SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType, 
										IMG_CPU_PHYADDR CpuPAddr)
{
	IMG_DEV_PHYADDR DevPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	DevPAddr.uiAddr = CpuPAddr.uiAddr;
	
	return DevPAddr;
}


IMG_CPU_PHYADDR SysSysPAddrToCpuPAddr (IMG_SYS_PHYADDR sys_paddr)
{
	IMG_CPU_PHYADDR cpu_paddr;

	
	cpu_paddr.uiAddr = sys_paddr.uiAddr;
	return cpu_paddr;
}


IMG_SYS_PHYADDR SysCpuPAddrToSysPAddr (IMG_CPU_PHYADDR cpu_paddr)
{
	IMG_SYS_PHYADDR sys_paddr;

	
	sys_paddr.uiAddr = cpu_paddr.uiAddr;
	return sys_paddr;
}



IMG_DEV_PHYADDR SysSysPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_SYS_PHYADDR SysPAddr)
{
	IMG_DEV_PHYADDR DevPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	DevPAddr.uiAddr = SysPAddr.uiAddr;

	return DevPAddr;
}



IMG_SYS_PHYADDR SysDevPAddrToSysPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_DEV_PHYADDR DevPAddr)
{
	IMG_SYS_PHYADDR SysPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	SysPAddr.uiAddr = DevPAddr.uiAddr;

	return SysPAddr;
}



IMG_VOID SysRegisterExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}



IMG_VOID SysRemoveExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}



IMG_UINT32 SysGetInterruptSource(SYS_DATA* psSysData,
								 PVRSRV_DEVICE_NODE *psDeviceNode)
{	
	PVR_UNREFERENCED_PARAMETER(psSysData);
#if defined(NO_HARDWARE)
	
	return 0xFFFFFFFF;
#else
	
	//return psDeviceNode->ui32SOCInterruptBit;
	return 0x1;
#endif
}



IMG_VOID SysClearInterrupts(SYS_DATA* psSysData, IMG_UINT32 ui32ClearBits)
{
	PVR_UNREFERENCED_PARAMETER(psSysData);
	PVR_UNREFERENCED_PARAMETER(ui32ClearBits);
}



PVRSRV_ERROR SysSystemPrePowerState(PVRSRV_SYS_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	return eError;
}


PVRSRV_ERROR SysSystemPostPowerState(PVRSRV_SYS_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	return eError;
}

PVRSRV_ERROR SysSystemSetPowerMargin(IMG_UINT32 ui32offset)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_LOG(("%s: set cold voltage margin:%d", __func__, ui32offset));
	sec_gpu_pwr_clk_margin_set(ui32offset);
	return eError;
}

PVRSRV_ERROR SysDevicePrePowerState(IMG_UINT32			ui32DeviceIndex,
									PVRSRV_DEV_POWER_STATE		eNewPowerState,
									PVRSRV_DEV_POWER_STATE		eCurrentPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_UNREFERENCED_PARAMETER(eCurrentPowerState);

	if (ui32DeviceIndex != gui32SGXDeviceID)
	{
		return PVRSRV_OK;
	}

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
	{
		PVRSRVSetDCState(DC_STATE_FLUSH_COMMANDS);
		PVR_DPF((PVR_DBG_MESSAGE, "SysDevicePrePowerState: SGX Entering state D3"));
		eError = sec_gpu_pwr_clk_state_set(GPU_PWR_CLK_STATE_OFF);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "SysDevicePrePowerState: SGX mode change (GPU_PWR_CLK_STATE_OFF) fail"));
			return eError;
		}
		PVRSRVSetDCState(DC_STATE_NO_FLUSH_COMMANDS);
	}


	return eError;
}



PVRSRV_ERROR SysDevicePostPowerState(IMG_UINT32			ui32DeviceIndex,
									PVRSRV_DEV_POWER_STATE		eNewPowerState,
									PVRSRV_DEV_POWER_STATE		eCurrentPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(eNewPowerState);

	if (ui32DeviceIndex != gui32SGXDeviceID)
	{
		return eError;
	}

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SysDevicePostPowerState: SGX Leaving state D3"));
		sec_gpu_dvfs_down_requirement_reset();
		eError = sec_gpu_pwr_clk_state_set(GPU_PWR_CLK_STATE_ON);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "SysDevicePostPowerState: SGX mode change (GPU_PWR_CLK_STATE_ON) fail"));
			return eError;
		}
	}

	return eError;
}



PVRSRV_ERROR SysOEMFunction(IMG_UINT32	ui32ID, 
							IMG_VOID	*pvIn,
							IMG_UINT32	ulInSize,
							IMG_VOID	*pvOut,
							IMG_UINT32	ulOutSize)
{
	if ((ui32ID == OEM_GET_EXT_FUNCS) &&
		(ulOutSize == sizeof(PVRSRV_DC_OEM_JTABLE)))
	{
		PVRSRV_DC_OEM_JTABLE *psOEMJTable = (PVRSRV_DC_OEM_JTABLE*)pvOut;
		psOEMJTable->pfnOEMBridgeDispatch = &PVRSRV_BridgeDispatchKM;

		return PVRSRV_OK;
	}

	if (ui32ID == OEM_COMPLETE_PREPARE_DISPLAY)
	{
		struct platform_device *pDev;
		char strBuf[64];
		IMG_UINT32 ui32YOffset = *((IMG_UINT32*)pvIn);
		char *arrStrEnv[2];

		pDev = gpsPVRLDMDev;

		snprintf(strBuf, sizeof(strBuf), "PAN_DISPLAY_YOFFSET=%d", ui32YOffset);
		arrStrEnv[0] = strBuf;
		arrStrEnv[1] = IMG_NULL;

		kobject_uevent_env(&pDev->dev.kobj, KOBJ_CHANGE, arrStrEnv);

		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_INVALID_PARAMS;
}

static PVRSRV_ERROR PowerLockWrap(SYS_SPECIFIC_DATA *psSysSpecData, IMG_BOOL bTryLock)
{
	if (!in_interrupt())
	{
		if (bTryLock)
		{
			int locked = mutex_trylock(&psSysSpecData->sPowerLock);
			if (locked == 0)
			{
				return PVRSRV_ERROR_RETRY;
			}
		}
		else
		{
			mutex_lock(&psSysSpecData->sPowerLock);
		}
	}

	return PVRSRV_OK;
}

static IMG_VOID PowerLockUnwrap(SYS_SPECIFIC_DATA *psSysSpecData)
{
	if (!in_interrupt())
	{
		mutex_unlock(&psSysSpecData->sPowerLock);
	}
}

PVRSRV_ERROR SysPowerLockWrap(IMG_BOOL bTryLock)
{
	SYS_DATA	*psSysData;

	SysAcquireData(&psSysData);

	return PowerLockWrap(psSysData->pvSysSpecificData, bTryLock);
}

IMG_VOID SysPowerLockUnwrap(IMG_VOID)
{
	SYS_DATA	*psSysData;

	SysAcquireData(&psSysData);

	PowerLockUnwrap(psSysData->pvSysSpecificData);
}                                                               
