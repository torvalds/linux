/*************************************************************************/ /*!
@File
@Title          Debug Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Creates common debug info entries.
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

#if !defined(__linux__)
#include <errno.h>
#endif /* #if !defined(__linux__) */

#include "debug_common.h"
#include "pvrsrv.h"
#include "di_server.h"
#include "lists.h"
#include "pvrversion.h"
#include "rgx_options.h"
#include "allocmem.h"
#include "rgxfwutils.h"

#ifdef SUPPORT_RGX
#include "rgxdevice.h"
#include "rgxdebug.h"
#include "rgxinit.h"
static IMG_HANDLE ghGpuUtilUserDebugFS;
#endif

static DI_ENTRY *gpsVersionDIEntry;
static DI_ENTRY *gpsStatusDIEntry;
static DI_ENTRY *gpsDumpDebugDIEntry;

#ifdef SUPPORT_RGX
static DI_ENTRY *gpsFWTraceDIEntry;
#ifdef SUPPORT_POWER_VALIDATION_VIA_DEBUGFS
static DI_ENTRY *gpsPowMonDIEntry;
#endif /* SUPPORT_POWER_VALIDATION_VIA_DEBUGFS */
#ifdef SUPPORT_FIRMWARE_GCOV
static DI_ENTRY *gpsFirmwareGcovDIEntry;
#endif /* SUPPORT_FIRMWARE_GCOV */
#ifdef SUPPORT_POWER_SAMPLING_VIA_DEBUGFS
static DI_ENTRY *gpsPowerDataDIEntry;
#endif /* SUPPORT_POWER_SAMPLING_VIA_DEBUGFS */
#ifdef SUPPORT_VALIDATION
static DI_ENTRY *gpsRGXRegsDIEntry;
#endif /* SUPPORT_VALIDATION */
#endif /* SUPPORT_RGX */
#ifdef SUPPORT_VALIDATION
static DI_ENTRY *gpsTestMemLeakDIEntry;
#endif /* SUPPORT_VALIDATION */
#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)
static DI_ENTRY *gpsDebugLevelDIEntry;
#endif /* defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON) */

/*************************************************************************/ /*!
 Version DebugFS entry
*/ /**************************************************************************/

static void *_DebugVersionCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode,
                                          va_list va)
{
	IMG_UINT64 *pui64CurrentPosition = va_arg(va, IMG_UINT64 *);
	IMG_UINT64 ui64Position = va_arg(va, IMG_UINT64);
	IMG_UINT64 ui64CurrentPosition = *pui64CurrentPosition;

	(*pui64CurrentPosition)++;

	return (ui64CurrentPosition == ui64Position) ? psDevNode : NULL;
}

static void *_VersionDIStart(OSDI_IMPL_ENTRY *psEntry, IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(psEntry);

	if (psPVRSRVData == NULL) {
		PVR_DPF((PVR_DBG_ERROR, "psPVRSRVData = NULL"));
		return NULL;
	}

	if (*pui64Pos == 0)
	{
		return DI_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
	                                      _DebugVersionCompare_AnyVaCb,
	                                      &uiCurrentPosition,
	                                      *pui64Pos);
}

static void _VersionDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvPriv)
{
	PVR_UNREFERENCED_PARAMETER(psEntry);
	PVR_UNREFERENCED_PARAMETER(pvPriv);
}

static void *_VersionDINext(OSDI_IMPL_ENTRY *psEntry,void *pvPriv,
                            IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	(*pui64Pos)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
	                                      _DebugVersionCompare_AnyVaCb,
	                                      &uiCurrentPosition,
	                                      *pui64Pos);
}

#define DI_PRINT_VERSION_FMTSPEC \
		"%s Version: %u.%u @ %u (%s) build options: 0x%08x %s\n"
#define STR_DEBUG   "debug"
#define STR_RELEASE "release"

#define BUILD_OPT_LEN 80

static inline void _AppendOptionStr(IMG_CHAR pszBuildOptions[], const IMG_CHAR* str, OSDI_IMPL_ENTRY *psEntry, IMG_UINT32* pui32BuildOptionLen)
{
	IMG_UINT32 ui32BuildOptionLen = *pui32BuildOptionLen;
	const IMG_UINT32 strLen = OSStringLength(str);
	const IMG_UINT32 optStrLen = sizeof(IMG_CHAR) * (BUILD_OPT_LEN-1);

	if ((ui32BuildOptionLen + strLen) > optStrLen)
	{
		pszBuildOptions[ui32BuildOptionLen] = '\0';
		DIPrintf(psEntry, "%s\n", pszBuildOptions);
		ui32BuildOptionLen = 0;
	}
	if (strLen < optStrLen)
	{
		OSStringLCopy(pszBuildOptions+ui32BuildOptionLen, str, strLen);
		ui32BuildOptionLen += strLen - 1;
	}
	*pui32BuildOptionLen = ui32BuildOptionLen;
}

static int _VersionDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvPriv)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);

	if (pvPriv == DI_START_TOKEN)
	{
		if (psPVRSRVData->sDriverInfo.bIsNoMatch)
		{
			const BUILD_INFO *psBuildInfo;

			psBuildInfo = &psPVRSRVData->sDriverInfo.sUMBuildInfo;
			DIPrintf(psEntry, DI_PRINT_VERSION_FMTSPEC,
			         "UM Driver",
			         PVRVERSION_UNPACK_MAJ(psBuildInfo->ui32BuildVersion),
			         PVRVERSION_UNPACK_MIN(psBuildInfo->ui32BuildVersion),
			         psBuildInfo->ui32BuildRevision,
			         (psBuildInfo->ui32BuildType == BUILD_TYPE_DEBUG) ?
			                 STR_DEBUG : STR_RELEASE,
			         psBuildInfo->ui32BuildOptions,
			         PVR_BUILD_DIR);

			psBuildInfo = &psPVRSRVData->sDriverInfo.sKMBuildInfo;
			DIPrintf(psEntry, DI_PRINT_VERSION_FMTSPEC,
			         "KM Driver (" PVR_ARCH_NAME ")",
			         PVRVERSION_UNPACK_MAJ(psBuildInfo->ui32BuildVersion),
			         PVRVERSION_UNPACK_MIN(psBuildInfo->ui32BuildVersion),
			         psBuildInfo->ui32BuildRevision,
			         (psBuildInfo->ui32BuildType == BUILD_TYPE_DEBUG) ?
			                 STR_DEBUG : STR_RELEASE,
			         psBuildInfo->ui32BuildOptions,
			         PVR_BUILD_DIR);
		}
		else
		{
			/* bIsNoMatch is `false` in one of the following cases:
			 * - UM & KM version parameters actually match.
			 * - A comparison between UM & KM has not been made yet, because no
			 *   client ever connected.
			 *
			 * In both cases, available (KM) version info is the best output we
			 * can provide.
			 */
			DIPrintf(psEntry, "Driver Version: %s (%s) (%s) build options: "
			         "0x%08lx %s\n", PVRVERSION_STRING, PVR_ARCH_NAME,
			         PVR_BUILD_TYPE, RGX_BUILD_OPTIONS_KM, PVR_BUILD_DIR);
		}
	}
	else if (pvPriv != NULL)
	{
		PVRSRV_DEVICE_NODE *psDevNode = (PVRSRV_DEVICE_NODE *) pvPriv;
		PVRSRV_DEVICE_CONFIG *psDevConfig = psDevNode->psDevConfig;
#ifdef SUPPORT_RGX
		PVRSRV_RGXDEV_INFO *psDevInfo = psDevNode->pvDevice;
#if defined(DEBUG) || defined(SUPPORT_VALIDATION)
		IMG_CHAR pszBuildOptions[BUILD_OPT_LEN];
		IMG_UINT32 ui32BuildOptionLen = 0;
		static const char* aszOptions[] = RGX_BUILD_OPTIONS_LIST;
		int i = 0;
#endif
#endif /* SUPPORT_RGX */
		IMG_BOOL bFwVersionInfoPrinted = IMG_FALSE;

		DIPrintf(psEntry, "\nDevice Name: %s\n", psDevConfig->pszName);

		if (psDevConfig->pszVersion)
		{
			DIPrintf(psEntry, "Device Version: %s\n",
			          psDevConfig->pszVersion);
		}

		if (psDevNode->pfnDeviceVersionString)
		{
			IMG_CHAR *pszVerStr;

			if (psDevNode->pfnDeviceVersionString(psDevNode,
			                                      &pszVerStr) == PVRSRV_OK)
			{
				DIPrintf(psEntry, "%s\n", pszVerStr);

				OSFreeMem(pszVerStr);
			}
		}

#ifdef SUPPORT_RGX
		/* print device's firmware version info */
		if (psDevInfo->psRGXFWIfOsInitMemDesc != NULL)
		{
			/* psDevInfo->psRGXFWIfOsInitMemDesc should be permanently mapped */
			if (psDevInfo->psRGXFWIfOsInit != NULL)
			{
				if (psDevInfo->psRGXFWIfOsInit->sRGXCompChecks.bUpdated)
				{
					const RGXFWIF_COMPCHECKS *psRGXCompChecks =
					        &psDevInfo->psRGXFWIfOsInit->sRGXCompChecks;
					IMG_UINT32 ui32DDKVer = psRGXCompChecks->ui32DDKVersion;

					DIPrintf(psEntry, DI_PRINT_VERSION_FMTSPEC,
					         "Firmware",
					         PVRVERSION_UNPACK_MAJ(ui32DDKVer),
					         PVRVERSION_UNPACK_MIN(ui32DDKVer),
					         psRGXCompChecks->ui32DDKBuild,
					         ((psRGXCompChecks->ui32BuildOptions &
					          OPTIONS_DEBUG_MASK) ? STR_DEBUG : STR_RELEASE),
					         psRGXCompChecks->ui32BuildOptions,
					         PVR_BUILD_DIR);
					bFwVersionInfoPrinted = IMG_TRUE;

#if defined(DEBUG) || defined(SUPPORT_VALIDATION)
					DIPrintf(psEntry, "Firmware Build Options:\n");

					for (i = 0; i < ARRAY_SIZE(aszOptions); i++)
					{
						if ((psRGXCompChecks->ui32BuildOptions & 1<<i))
						{
							_AppendOptionStr(pszBuildOptions, aszOptions[i], psEntry, &ui32BuildOptionLen);
						}
					}

					if (ui32BuildOptionLen != 0)
					{
						DIPrintf(psEntry, "%s", pszBuildOptions);
					}
					DIPrintf(psEntry, "\n");
#endif
				}
			}
			else
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Error acquiring CPU virtual "
				        "address of FWInitMemDesc", __func__));
			}
		}
#endif /* SUPPORT_RGX */

		if (!bFwVersionInfoPrinted)
		{
			DIPrintf(psEntry, "Firmware Version: Info unavailable %s\n",
#ifdef NO_HARDWARE
			         "on NoHW driver"
#else /* NO_HARDWARE */
			         "(Is INIT complete?)"
#endif /* NO_HARDWARE */
			         );
		}
	}

	return 0;
}

#if defined(SUPPORT_RGX) && defined(SUPPORT_POWER_SAMPLING_VIA_DEBUGFS)

/*************************************************************************/ /*!
 Power data DebugFS entry
*/ /**************************************************************************/

static void *_DebugPowerDataCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode,
					  va_list va)
{
	IMG_UINT64 *pui64CurrentPosition = va_arg(va, IMG_UINT64 *);
	IMG_UINT64 ui64Position = va_arg(va, IMG_UINT64;
	IMG_UINT64 ui64CurrentPosition = *pui64CurrentPosition;

	(*puiCurrentPosition)++;

	return (ui64CurrentPosition == ui64Position) ? psDevNode : NULL;
}

static void *_DebugPowerDataDIStart(OSDI_IMPL_ENTRY *psEntry,
									 IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 0;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
	                                      _DebugPowerDataCompare_AnyVaCb,
	                                      &uiCurrentPosition,
	                                      *pui64Pos);
}

static void _DebugPowerDataDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psEntry);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugPowerDataDINext(OSDI_IMPL_ENTRY *psEntry,
									void *pvData,
									IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 0;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*pui64Pos)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
	                                      _DebugPowerDataCompare_AnyVaCb,
	                                      &uiCurrentPosition,
	                                      *pui64Pos);
}

static PVRSRV_ERROR SendPowerCounterCommand(PVRSRV_DEVICE_NODE* psDeviceNode,
                                            RGXFWIF_COUNTER_DUMP_REQUEST eRequestType,
                                            IMG_UINT32 *pui32kCCBCommandSlot)
{
	PVRSRV_ERROR eError;

	RGXFWIF_KCCB_CMD sCounterDumpCmd;

	sCounterDumpCmd.eCmdType = RGXFWIF_KCCB_CMD_COUNTER_DUMP;
	sCounterDumpCmd.uCmdData.sCounterDumpConfigData.eCounterDumpRequest = eRequestType;

	eError = RGXScheduleCommandAndGetKCCBSlot(psDeviceNode->pvDevice,
				RGXFWIF_DM_GP,
				&sCounterDumpCmd,
				0,
				PDUMP_FLAGS_CONTINUOUS,
				pui32kCCBCommandSlot);
	PVR_LOG_IF_ERROR(eError, "RGXScheduleCommandAndGetKCCBSlot");

	return eError;
}

static void *_IsDevNodeNotInitialised(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	return psDeviceNode->eDevState == PVRSRV_DEVICE_STATE_ACTIVE ?
	        NULL : psDeviceNode;
}

static void _SendPowerCounterCommand(PVRSRV_DEVICE_NODE* psDeviceNode,
									 va_list va)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32         ui32kCCBCommandSlot;

	OSLockAcquire(psDevInfo->hCounterDumpingLock);

	SendPowerCounterCommand(psDeviceNode,
	                        va_arg(va, RGXFWIF_COUNTER_DUMP_REQUEST),
	                        &ui32kCCBCommandSlot);

	OSLockRelease(psDevInfo->hCounterDumpingLock);
}

static int _DebugPowerDataDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
#ifdef CACHE_TEST
	struct DEVMEM_MEMDESC_TAG *pxmdsc = NULL;
#endif
	IMG_UINT32 ui32kCCBCommandSlot;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (pvData != NULL)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;
		PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

		if (psDeviceNode->eDevState != PVRSRV_DEVICE_STATE_ACTIVE)
		{
			PVR_DPF((PVR_DBG_ERROR, "Not all device nodes were initialised "
			        "when power counter data was requested!"));
			return -EIO;
		}

		OSLockAcquire(psDevInfo->hCounterDumpingLock);

		eError = SendPowerCounterCommand(psDeviceNode,
		                                 RGXFWIF_PWR_COUNTER_DUMP_SAMPLE,
		                                 &ui32kCCBCommandSlot);

		if (eError != PVRSRV_OK)
		{
			OSLockRelease(psDevInfo->hCounterDumpingLock);
			return -EIO;
		}

		/* Wait for FW complete completion */
		eError = RGXWaitForKCCBSlotUpdate(psDevInfo,
		                                  ui32kCCBCommandSlot,
		                                  PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "RGXWaitForKCCBSlotUpdate");
			OSLockRelease(psDevInfo->hCounterDumpingLock);
			return -EIO;
		}

		/* Read back the buffer */
		{
			IMG_UINT32* pui32PowerBuffer;
			IMG_UINT32 ui32NumOfRegs, ui32SamplePeriod;
			IMG_UINT32 i, j;

			eError = DevmemAcquireCpuVirtAddr(psDevInfo->psCounterBufferMemDesc,
			                                  (void**)&pui32PowerBuffer);
			if (eError != PVRSRV_OK)
			{
				PVR_LOG_ERROR(eError, "DevmemAcquireCpuVirtAddr");
				OSLockRelease(psDevInfo->hCounterDumpingLock);
				return -EIO;
			}
#ifdef CACHE_TEST
			pxmdsc = (struct DEVMEM_MEMDESC_TAG *)psDevInfo->psCounterBufferMemDesc;
			printk("in %s L:%d mdsc->size:%lld, import->size:%lld, flag:%llx\n", __func__, __LINE__, pxmdsc->uiAllocSize, pxmdsc->psImport->uiSize, (unsigned long long)(pxmdsc->psImport->uiFlags & PVRSRV_MEMALLOCFLAG_CPU_CACHE_MODE_MASK));
			if(pxmdsc->uiAllocSize > 4096 && !(PVRSRV_CHECK_CPU_UNCACHED(pxmdsc->psImport->uiFlags) || PVRSRV_CHECK_CPU_WRITE_COMBINE(pxmdsc->psImport->uiFlags)))
			{
			    printk("in %s L:%d cache_op:%d\n", __func__, __LINE__,PVRSRV_CACHE_OP_INVALIDATE);
			    BridgeCacheOpExec (GetBridgeHandle(pxmdsc->psImport->hDevConnection),pxmdsc->psImport->hPMR,(IMG_UINT64)(uintptr_t)pui32PowerBuffer - pxmdsc->uiOffset,pxmdsc->uiOffset,pxmdsc->uiAllocSize,PVRSRV_CACHE_OP_INVALIDATE);
			}
#endif

			ui32NumOfRegs = *pui32PowerBuffer++;
			ui32SamplePeriod = *pui32PowerBuffer++;

			if (ui32NumOfRegs)
			{
				DIPrintf(psEntry, "Power counter data for device id: %d\n",
				         psDeviceNode->sDevId.i32OsDeviceID);
				DIPrintf(psEntry, "Sample period: 0x%08x\n", ui32SamplePeriod);

				for (i = 0; i < ui32NumOfRegs; i++)
				{
					IMG_UINT32 ui32High, ui32Low;
					IMG_UINT32 ui32RegOffset = *pui32PowerBuffer++;
					IMG_UINT32 ui32NumOfInstances = *pui32PowerBuffer++;

					PVR_ASSERT(ui32NumOfInstances);

					DIPrintf(psEntry, "0x%08x:", ui32RegOffset);

					for (j = 0; j < ui32NumOfInstances; j++)
					{
						ui32Low = *pui32PowerBuffer++;
						ui32High = *pui32PowerBuffer++;

						DIPrintf(psEntry, " 0x%016llx",
						        (IMG_UINT64) ui32Low | (IMG_UINT64) ui32High << 32);
					}

					DIPrintf(psEntry, "\n");
				}
			}

			DevmemReleaseCpuVirtAddr(psDevInfo->psCounterBufferMemDesc);
		}

		OSLockRelease(psDevInfo->hCounterDumpingLock);
	}

	return eError;
}

static IMG_INT64 PowerDataSet(const IMG_CHAR __user *pcBuffer,
                              IMG_UINT64 ui64Count, IMG_UINT64 *pui64Pos,
                              void *pvData)
{
	PVRSRV_DATA* psPVRSRVData = (PVRSRV_DATA*) pvData;
	RGXFWIF_COUNTER_DUMP_REQUEST eRequest;

	PVR_RETURN_IF_FALSE(pcBuffer != NULL, -EIO);
	PVR_RETURN_IF_FALSE(pui64Pos != NULL && *pui64Pos == 0, -EIO);
	PVR_RETURN_IF_FALSE(ui64Count >= 1, -EINVAL);
	PVR_RETURN_IF_FALSE(pcBuffer[ui64Count - 1] == '\0', -EINVAL);

	if (List_PVRSRV_DEVICE_NODE_Any(psPVRSRVData->psDeviceNodeList,
	                                _IsDevNodeNotInitialised))
	{
		PVR_DPF((PVR_DBG_ERROR, "Not all device nodes were initialised when "
		        "power counter data was requested!"));
		return -EIO;
	}

	if (pcBuffer[0] == '1')
	{
		eRequest = RGXFWIF_PWR_COUNTER_DUMP_START;
	}
	else if (pcBuffer[0] == '0')
	{
		eRequest = RGXFWIF_PWR_COUNTER_DUMP_STOP;
	}
	else
	{
		return -EINVAL;
	}

	List_PVRSRV_DEVICE_NODE_ForEach_va(psPVRSRVData->psDeviceNodeList,
									   _SendPowerCounterCommand, eRequest);

	*pui64Pos += ui64Count;
	return ui64Count;
}

#endif /* defined(SUPPORT_RGX) && defined(SUPPORT_POWER_SAMPLING_VIA_DEBUGFS) */

/*************************************************************************/ /*!
 Status DebugFS entry
*/ /**************************************************************************/

static void *_DebugStatusCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode,
										 va_list va)
{
	IMG_UINT64 *pui64CurrentPosition = va_arg(va, IMG_UINT64 *);
	IMG_UINT64 ui64Position = va_arg(va, IMG_UINT64);
	IMG_UINT64 ui64CurrentPosition = *pui64CurrentPosition;

	(*pui64CurrentPosition)++;

	return (ui64CurrentPosition == ui64Position) ? psDevNode : NULL;
}

static void *_DebugStatusDIStart(OSDI_IMPL_ENTRY *psEntry, IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	if (*pui64Pos == 0)
	{
		return DI_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugStatusCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *pui64Pos);
}

static void _DebugStatusDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psEntry);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugStatusDINext(OSDI_IMPL_ENTRY *psEntry,
								 void *pvData,
								 IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*pui64Pos)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugStatusCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *pui64Pos);
}

static int _DebugStatusDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	if (pvData == DI_START_TOKEN)
	{
		PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);

		if (psPVRSRVData != NULL)
		{
			switch (psPVRSRVData->eServicesState)
			{
				case PVRSRV_SERVICES_STATE_OK:
					DIPrintf(psEntry, "Driver Status:   OK\n");
					break;
				case PVRSRV_SERVICES_STATE_BAD:
					DIPrintf(psEntry, "Driver Status:   BAD\n");
					break;
				case PVRSRV_SERVICES_STATE_UNDEFINED:
					DIPrintf(psEntry, "Driver Status:   UNDEFINED\n");
					break;
				default:
					DIPrintf(psEntry, "Driver Status:   UNKNOWN (%d)\n",
					         psPVRSRVData->eServicesState);
					break;
			}
		}
	}
	else if (pvData != NULL)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;
		IMG_CHAR           *pszStatus = "";
		IMG_CHAR           *pszReason = "";
		PVRSRV_DEVICE_HEALTH_STATUS eHealthStatus;
		PVRSRV_DEVICE_HEALTH_REASON eHealthReason;

		/* Update the health status now if possible... */
		if (psDeviceNode->pfnUpdateHealthStatus)
		{
			psDeviceNode->pfnUpdateHealthStatus(psDeviceNode, IMG_FALSE);
		}
		eHealthStatus = OSAtomicRead(&psDeviceNode->eHealthStatus);
		eHealthReason = OSAtomicRead(&psDeviceNode->eHealthReason);

		switch (eHealthStatus)
		{
			case PVRSRV_DEVICE_HEALTH_STATUS_OK:  pszStatus = "OK";  break;
			case PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING:  pszStatus = "NOT RESPONDING";  break;
			case PVRSRV_DEVICE_HEALTH_STATUS_DEAD:  pszStatus = "DEAD";  break;
			case PVRSRV_DEVICE_HEALTH_STATUS_FAULT:  pszStatus = "FAULT";  break;
			case PVRSRV_DEVICE_HEALTH_STATUS_UNDEFINED:  pszStatus = "UNDEFINED";  break;
			default:  pszStatus = "UNKNOWN";  break;
		}

		switch (eHealthReason)
		{
			case PVRSRV_DEVICE_HEALTH_REASON_NONE:  pszReason = "";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_ASSERTED:  pszReason = " (Asserted)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING:  pszReason = " (Poll failing)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS:  pszReason = " (Global Event Object timeouts rising)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT:  pszReason = " (KCCB offset invalid)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED:  pszReason = " (KCCB stalled)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_IDLING:  pszReason = " (Idling)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_RESTARTING:  pszReason = " (Restarting)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_MISSING_INTERRUPTS:  pszReason = " (Missing interrupts)";  break;
			default:  pszReason = " (Unknown reason)";  break;
		}

		DIPrintf(psEntry, "Firmware Status: %s%s\n", pszStatus, pszReason);
		if (PVRSRV_ERROR_LIMIT_REACHED)
		{
			DIPrintf(psEntry, "Server Errors:   %d+\n", IMG_UINT32_MAX);
		}
		else
		{
			DIPrintf(psEntry, "Server Errors:   %d\n", PVRSRV_KM_ERRORS);
		}


		/* Write other useful stats to aid the test cycle... */
		if (psDeviceNode->pvDevice != NULL)
		{
#ifdef SUPPORT_RGX
			PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
			RGXFWIF_HWRINFOBUF *psHWRInfoBuf = psDevInfo->psRGXFWIfHWRInfoBufCtl;
			RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;

#ifdef PVRSRV_DEBUG_LISR_EXECUTION
			/* Show the detected #LISR, #MISR scheduled calls */
			DIPrintf(psEntry, "RGX #LISR: %llu\n", psDeviceNode->ui64nLISR);
			DIPrintf(psEntry, "RGX #MISR: %llu\n", psDeviceNode->ui64nMISR);
#endif /* PVRSRV_DEBUG_LISR_EXECUTION */

			/* Calculate the number of HWR events in total across all the DMs... */
			if (psHWRInfoBuf != NULL)
			{
				IMG_UINT32 ui32HWREventCount = 0;
				IMG_UINT32 ui32CRREventCount = 0;
				IMG_UINT32 ui32DMIndex;

				for (ui32DMIndex = 0; ui32DMIndex < RGXFWIF_DM_MAX; ui32DMIndex++)
				{
					ui32HWREventCount += psHWRInfoBuf->aui32HwrDmLockedUpCount[ui32DMIndex];
					ui32CRREventCount += psHWRInfoBuf->aui32HwrDmOverranCount[ui32DMIndex];
				}

				DIPrintf(psEntry, "HWR Event Count: %d\n", ui32HWREventCount);
				DIPrintf(psEntry, "CRR Event Count: %d\n", ui32CRREventCount);
#ifdef PVRSRV_STALLED_CCB_ACTION
				/* Write the number of Sync Lockup Recovery (SLR) events... */
				DIPrintf(psEntry, "SLR Event Count: %d\n", psDevInfo->psRGXFWIfFwOsData->ui32ForcedUpdatesRequested);
#endif /* PVRSRV_STALLED_CCB_ACTION */
			}

			/* Show error counts */
			DIPrintf(psEntry, "WGP Error Count: %d\n", psDevInfo->sErrorCounts.ui32WGPErrorCount);
			DIPrintf(psEntry, "TRP Error Count: %d\n", psDevInfo->sErrorCounts.ui32TRPErrorCount);

			/*
			 * Guest drivers do not support the following functionality:
			 *	- Perform actual on-chip fw tracing.
			 *	- Collect actual on-chip GPU utilization stats.
			 *	- Perform actual on-chip GPU power/dvfs management.
			 *	- As a result no more information can be provided.
			 */
			if (!PVRSRV_VZ_MODE_IS(GUEST))
			{
				if (psFwSysData != NULL)
				{
					DIPrintf(psEntry, "FWF Event Count: %d\n", psFwSysData->ui32FWFaults);
				}

				/* Write the number of APM events... */
				DIPrintf(psEntry, "APM Event Count: %d\n", psDevInfo->ui32ActivePMReqTotal);

				/* Write the current GPU Utilisation values... */
				if (psDevInfo->pfnGetGpuUtilStats &&
					eHealthStatus == PVRSRV_DEVICE_HEALTH_STATUS_OK)
				{
					RGXFWIF_GPU_UTIL_STATS sGpuUtilStats;
					PVRSRV_ERROR eError = PVRSRV_OK;

					eError = psDevInfo->pfnGetGpuUtilStats(psDeviceNode,
														   ghGpuUtilUserDebugFS,
														   &sGpuUtilStats);

					if ((eError == PVRSRV_OK) &&
						((IMG_UINT32)sGpuUtilStats.ui64GpuStatCumulative))
					{
						IMG_UINT64 util;
						IMG_UINT32 rem;

						util = 100 * sGpuUtilStats.ui64GpuStatActive;
						util = OSDivide64(util, (IMG_UINT32)sGpuUtilStats.ui64GpuStatCumulative, &rem);

						DIPrintf(psEntry, "GPU Utilisation: %u%%\n", (IMG_UINT32)util);
					}
					else
					{
						DIPrintf(psEntry, "GPU Utilisation: -\n");
					}
				}
			}
#endif /* SUPPORT_RGX */
		}
	}

	return 0;
}

static IMG_INT64 DebugStatusSet(const IMG_CHAR *pcBuffer, IMG_UINT64 ui64Count,
                                IMG_UINT64 *pui64Pos, void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	PVR_RETURN_IF_FALSE(pcBuffer != NULL, -EIO);
	PVR_RETURN_IF_FALSE(pui64Pos != NULL && *pui64Pos == 0, -EIO);
	PVR_RETURN_IF_FALSE(ui64Count >= 1, -EINVAL);
	PVR_RETURN_IF_FALSE(pcBuffer[0] == 'k' || pcBuffer[0] == 'K', -EINVAL);
	PVR_RETURN_IF_FALSE(pcBuffer[ui64Count - 1] == '\0', -EINVAL);

	psPVRSRVData->eServicesState = PVRSRV_SERVICES_STATE_BAD;

	*pui64Pos += ui64Count;
	return ui64Count;
}

/*************************************************************************/ /*!
 Dump Debug DebugFS entry
*/ /**************************************************************************/

static void *_DebugDumpDebugCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode, va_list va)
{
	IMG_UINT64 *pui64CurrentPosition = va_arg(va, IMG_UINT64 *);
	IMG_UINT64 ui64Position = va_arg(va, IMG_UINT64);
	IMG_UINT64 ui64CurrentPosition = *pui64CurrentPosition;

	(*pui64CurrentPosition)++;

	return (ui64CurrentPosition == ui64Position) ? psDevNode : NULL;
}

static void *_DebugDumpDebugDIStart(OSDI_IMPL_ENTRY *psEntry,
                                    IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	if (*pui64Pos == 0)
	{
		return DI_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugDumpDebugCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *pui64Pos);
}

static void _DebugDumpDebugDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psEntry);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugDumpDebugDINext(OSDI_IMPL_ENTRY *psEntry,
									void *pvData,
									IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*pui64Pos)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugDumpDebugCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *pui64Pos);
}

static void _DumpDebugDIPrintf(void *pvDumpDebugFile,
                               const IMG_CHAR *pszFormat, ...)
{
	OSDI_IMPL_ENTRY *psEntry = pvDumpDebugFile;
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	va_list ArgList;

	va_start(ArgList, pszFormat);
	vsnprintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN, pszFormat, ArgList);
	va_end(ArgList);
	DIPrintf(psEntry, "%s\n", szBuffer);
}

static int _DebugDumpDebugDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	if (pvData != NULL  &&  pvData != DI_START_TOKEN)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;

		if (psDeviceNode->pvDevice != NULL)
		{
			PVRSRVDebugRequest(psDeviceNode, DEBUG_REQUEST_VERBOSITY_MAX,
			                   _DumpDebugDIPrintf, psEntry);
		}
	}

	return 0;
}

#ifdef SUPPORT_RGX

/*************************************************************************/ /*!
 Firmware Trace DebugFS entry
*/ /**************************************************************************/

static void *_DebugFWTraceCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode, va_list va)
{
	IMG_UINT64 *pui64CurrentPosition = va_arg(va, IMG_UINT64 *);
	IMG_UINT64 ui64Position = va_arg(va, IMG_UINT64);
	IMG_UINT64 ui64CurrentPosition = *pui64CurrentPosition;

	(*pui64CurrentPosition)++;

	return (ui64CurrentPosition == ui64Position) ? psDevNode : NULL;
}

static void *_DebugFWTraceDIStart(OSDI_IMPL_ENTRY *psEntry, IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	if (*pui64Pos == 0)
	{
		return DI_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugFWTraceCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *pui64Pos);
}

static void _DebugFWTraceDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psEntry);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugFWTraceDINext(OSDI_IMPL_ENTRY *psEntry,
								  void *pvData,
								  IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*pui64Pos)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugFWTraceCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *pui64Pos);
}

static void _FWTraceDIPrintf(void *pvDumpDebugFile,
				const IMG_CHAR *pszFormat, ...)
{
	OSDI_IMPL_ENTRY *psEntry = pvDumpDebugFile;
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	va_list ArgList;

	va_start(ArgList, pszFormat);
	vsnprintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN, pszFormat, ArgList);
	va_end(ArgList);
	DIPrintf(psEntry, "%s\n", szBuffer);
}

static int _DebugFWTraceDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	if (pvData != NULL  &&  pvData != DI_START_TOKEN)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;

		if (psDeviceNode->pvDevice != NULL)
		{
			PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

			RGXDumpFirmwareTrace(_FWTraceDIPrintf, psEntry, psDevInfo);
		}
	}

	return 0;
}

#ifdef SUPPORT_FIRMWARE_GCOV

static PVRSRV_RGXDEV_INFO *getPsDevInfo(OSDI_IMPL_ENTRY *psEntry)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);

	if (psPVRSRVData != NULL)
	{
		if (psPVRSRVData->psDeviceNodeList != NULL)
		{
			PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO*)psPVRSRVData->psDeviceNodeList->pvDevice;
			return psDevInfo;
		}
	}
	return NULL;
}

static void *_FirmwareGcovDIStart(OSDI_IMPL_ENTRY *psEntry, IMG_UINT64 *pui64Pos)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = getPsDevInfo(psEntry);

	if (psDevInfo != NULL)
	{
		if (psDevInfo->psFirmwareGcovBufferMemDesc != NULL)
		{
			void *pvCpuVirtAddr;
			DevmemAcquireCpuVirtAddr(psDevInfo->psFirmwareGcovBufferMemDesc, &pvCpuVirtAddr);
			return *pui64Pos ? NULL : pvCpuVirtAddr;
		}
	}

	return NULL;
}

static void _FirmwareGcovDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = getPsDevInfo(psEntry);

	PVR_UNREFERENCED_PARAMETER(pvData);

	if (psDevInfo != NULL)
	{
		if (psDevInfo->psFirmwareGcovBufferMemDesc != NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->psFirmwareGcovBufferMemDesc);
		}
	}
}

static void *_FirmwareGcovDINext(OSDI_IMPL_ENTRY *psEntry,
								  void *pvData,
								  IMG_UINT64 *pui64Pos)
{
	PVR_UNREFERENCED_PARAMETER(psEntry);
	PVR_UNREFERENCED_PARAMETER(pvData);
	PVR_UNREFERENCED_PARAMETER(pui64Pos);
	return NULL;
}

static int _FirmwareGcovDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = getPsDevInfo(psEntry);

	if (psDevInfo != NULL)
	{
		DIWrite(psEntry, pvData, psDevInfo->ui32FirmwareGcovSize);
	}
	return 0;
}

#endif /* SUPPORT_FIRMWARE_GCOV */

#ifdef SUPPORT_POWER_VALIDATION_VIA_DEBUGFS

/*************************************************************************/ /*!
 Power monitoring DebugFS entry
*/ /**************************************************************************/

static void *_PowMonCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode, va_list va)
{
	IMG_UINT64 *pui64CurrentPosition = va_arg(va, IMG_UINT64 *);
	IMG_UINT64 ui64Position = va_arg(va, IMG_UINT64);
	IMG_UINT64 ui64CurrentPosition = *pui64CurrentPosition;

	(*pui64CurrentPosition)++;

	return (ui64CurrentPosition == ui64Position) ? psDevNode : NULL;
}

static void *_PowMonTraceDIStart(OSDI_IMPL_ENTRY *psEntry, IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	if (*pui64Pos == 0)
	{
		return DI_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _PowMonCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *pui64Pos);
}

static void _PowMonTraceDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psEntry);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_PowMonTraceDINext(OSDI_IMPL_ENTRY *psEntry,
								 void *pvData,
								 IMG_UINT64 *pui64Pos)
{
	PVRSRV_DATA *psPVRSRVData = DIGetPrivData(psEntry);
	IMG_UINT64 uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*pui64Pos)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _PowMonCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *pui64Pos);
}

static void _PowMonTraceDIPrintf(void *pvDumpDebugFile,
				const IMG_CHAR *pszFormat, ...)
{
	OSDI_IMPL_ENTRY *psEntry = pvDumpDebugFile;
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	va_list ArgList;

	va_start(ArgList, pszFormat);
	vsnprintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN, pszFormat, ArgList);
	va_end(ArgList);
	DIPrintf(psEntry, "%s\n", szBuffer);
}

static int _PowMonTraceDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	if (pvData != NULL  &&  pvData != DI_START_TOKEN)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;

		if (psDeviceNode->pvDevice != NULL)
		{
			PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

			RGXDumpPowerMonitoring(_PowMonTraceDIPrintf, psEntry, psDevInfo);
		}
	}

	return 0;
}

#endif /* SUPPORT_POWER_VALIDATION_VIA_DEBUGFS */

#ifdef SUPPORT_VALIDATION

#ifndef SYS_RGX_DEV_UNMAPPED_FW_REG
#define SYS_RGX_DEV_UNMAPPED_FW_REG 0XFFFFFFFF
#endif
#define DI_RGXREGS_TIMEOUT_MS 1000

/*************************************************************************/ /*!
 RGX Registers Dump DebugFS entry
*/ /**************************************************************************/

static IMG_INT64 _RgxRegsSeek(IMG_UINT64 ui64Offset, void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = pvData;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_LOG_RETURN_IF_FALSE(psPVRSRVData != NULL, "psPVRSRVData is NULL", -1);

	psDevInfo = psPVRSRVData->psDeviceNodeList->pvDevice;

	PVR_LOG_RETURN_IF_FALSE(ui64Offset <= (psDevInfo->ui32RegSize - 4),
	                        "register offset is too big", -1);

	return ui64Offset;
}

static IMG_INT64 _RgxRegsRead(IMG_CHAR *pcBuffer, IMG_UINT64 ui64Count,
                              IMG_UINT64 *pui64Pos, void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = pvData;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT64 ui64RegVal = 0;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT64 ui64CompRes;

	PVR_LOG_RETURN_IF_FALSE(psPVRSRVData != NULL,
	                        "psPVRSRVData is NULL", -ENXIO);
	PVR_LOG_RETURN_IF_FALSE(ui64Count == 4 || ui64Count == 8,
	                        "wrong RGX register size", -EIO);
	PVR_LOG_RETURN_IF_FALSE(!(*pui64Pos & (ui64Count - 1)),
	                        "register read offset isn't aligned", -EINVAL);

	psDevInfo = psPVRSRVData->psDeviceNodeList->pvDevice;

	if (*pui64Pos >= SYS_RGX_DEV_UNMAPPED_FW_REG)
	{
		if (!psDevInfo->bFirmwareInitialised)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGX Register offset is above PCI mapped range but "
					 "Firmware isn't yet initialised\n"));
			return -EIO;
		}

		reinit_completion(&psDevInfo->sFwRegs.sRegComp);

		eError = RGXScheduleRgxRegCommand(psDevInfo,
										  0x00,
										  ui64Count,
										  (IMG_UINT32) *pui64Pos,
										  IMG_FALSE);

		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "RGXScheduleRgxRegCommand");
			return -EIO;
		}

		ui64CompRes = wait_for_completion_timeout(&psDevInfo->sFwRegs.sRegComp,
												  msecs_to_jiffies(DI_RGXREGS_TIMEOUT_MS));
		if (!ui64CompRes)
		{
				PVR_DPF((PVR_DBG_ERROR, "FW RGX Register access timeout %#x\n",
				   (IMG_UINT32) *pui64Pos));
				return -EIO;
		}

		OSCachedMemCopy(pcBuffer, &psDevInfo->sFwRegs.ui64RegVal, ui64Count);
	}
	else
	{
		ui64RegVal = ui64Count == 4 ?
	        OSReadHWReg32(psDevInfo->pvRegsBaseKM, *pui64Pos) :
			OSReadHWReg64(psDevInfo->pvRegsBaseKM, *pui64Pos);
		OSCachedMemCopy(pcBuffer, &ui64RegVal, ui64Count);
	}

	return ui64Count;
}

static IMG_INT64 _RgxRegsWrite(const IMG_CHAR *pcBuffer, IMG_UINT64 ui64Count,
                               IMG_UINT64 *pui64Pos, void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = pvData;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT64 ui64RegVal = 0;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	/* ignore the '\0' character */
	ui64Count -= 1;

	PVR_LOG_RETURN_IF_FALSE(psPVRSRVData != NULL,
	                        "psPVRSRVData == NULL", -ENXIO);
	PVR_LOG_RETURN_IF_FALSE(ui64Count == 4 || ui64Count == 8,
	                        "wrong RGX register size", -EIO);
	PVR_LOG_RETURN_IF_FALSE(!(*pui64Pos & (ui64Count - 1)),
	                        "register read offset isn't aligned", -EINVAL);

	psDevInfo = psPVRSRVData->psDeviceNodeList->pvDevice;

	if (*pui64Pos >= SYS_RGX_DEV_UNMAPPED_FW_REG)
	{
		if (!psDevInfo->bFirmwareInitialised)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGX Register offset is above PCI mapped range but "
					 "Firmware isn't yet initialised\n"));
			return -EIO;
		}

		if (ui64Count == 4)
			ui64RegVal = (IMG_UINT64) *((IMG_UINT32 *) pcBuffer);
		else
			ui64RegVal = *((IMG_UINT64 *) pcBuffer);

		eError = RGXScheduleRgxRegCommand(psDevInfo,
										  ui64RegVal,
										  ui64Count,
										  (IMG_UINT32) *pui64Pos,
										  IMG_TRUE);
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "RGXScheduleRgxRegCommand");
			return -EIO;
		}

	}
	else
	{
		if (ui64Count == 4)
		{
			OSWriteHWReg32(psDevInfo->pvRegsBaseKM, *pui64Pos,
						   *((IMG_UINT32 *) (void *) pcBuffer));
		}
		else
		{
			OSWriteHWReg64(psDevInfo->pvRegsBaseKM, *pui64Pos,
						   *((IMG_UINT64 *) (void *) pcBuffer));
		}
	}

	return ui64Count;
}

#endif /* SUPPORT_VALIDATION */

#endif /* SUPPORT_RGX */

#ifdef SUPPORT_VALIDATION

static int TestMemLeakDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	PVR_UNREFERENCED_PARAMETER(pvData);

	PVR_RETURN_IF_FALSE(pvData != NULL, -EINVAL);

	DIPrintf(psEntry, "os: %s, %u\ngpu: %s, %u\nmmu: %s, %u\n",
	         psPVRSRVData->sMemLeakIntervals.ui32OSAlloc ? "enabled" : "disabled",
	         psPVRSRVData->sMemLeakIntervals.ui32OSAlloc,
	         psPVRSRVData->sMemLeakIntervals.ui32GPU ? "enabled" : "disabled",
	         psPVRSRVData->sMemLeakIntervals.ui32GPU,
	         psPVRSRVData->sMemLeakIntervals.ui32MMU ? "enabled" : "disabled",
	         psPVRSRVData->sMemLeakIntervals.ui32MMU);

	return 0;
}

static IMG_INT64 TestMemLeakDISet(const IMG_CHAR *pcBuffer, IMG_UINT64 ui64Count,
                                  IMG_UINT64 *pui64Pos, void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	IMG_CHAR *pcTemp;
	unsigned long ui32MemLeakInterval;

	PVR_UNREFERENCED_PARAMETER(pvData);

	PVR_RETURN_IF_FALSE(pcBuffer != NULL, -EIO);
	PVR_RETURN_IF_FALSE(pui64Pos != NULL && *pui64Pos == 0, -EIO);
	PVR_RETURN_IF_FALSE(ui64Count <= 16, -EINVAL);
	PVR_RETURN_IF_FALSE(pcBuffer[ui64Count - 1] == '\0', -EINVAL);

	pcTemp = strchr(pcBuffer, ',');

	if (kstrtoul(pcTemp+1, 0, &ui32MemLeakInterval) != 0)
	{
		return -EINVAL;
	}

	if (strncmp(pcBuffer, "os", pcTemp-pcBuffer) == 0)
	{
		psPVRSRVData->sMemLeakIntervals.ui32OSAlloc = ui32MemLeakInterval;
	}
	else if (strncmp(pcBuffer, "gpu", pcTemp-pcBuffer) == 0)
	{
		psPVRSRVData->sMemLeakIntervals.ui32GPU = ui32MemLeakInterval;
	}
	else if (strncmp(pcBuffer, "mmu", pcTemp-pcBuffer) == 0)
	{
		psPVRSRVData->sMemLeakIntervals.ui32MMU = ui32MemLeakInterval;
	}
	else
	{
		return -EINVAL;
	}

	*pui64Pos += ui64Count;
	return ui64Count;
}

#endif /* SUPPORT_VALIDATION */

#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)

/*************************************************************************/ /*!
 Debug level DebugFS entry
*/ /**************************************************************************/

static int DebugLevelDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	DIPrintf(psEntry, "%u\n", OSDebugLevel());

	return 0;
}

#ifndef __GNUC__
static int __builtin_ffsl(long int x)
{
	for (size_t i = 0; i < sizeof(x) * 8; i++)
	{
		if (x & (1 << i))
		{
			return i + 1;
		}
	}
	return 0;
}
#endif /* __GNUC__ */

static IMG_INT64 DebugLevelSet(const IMG_CHAR *pcBuffer, IMG_UINT64 ui64Count,
                               IMG_UINT64 *pui64Pos, void *pvData)
{
	const IMG_UINT uiMaxBufferSize = 6;
	IMG_UINT32 ui32Level;

	PVR_RETURN_IF_FALSE(pcBuffer != NULL, -EIO);
	PVR_RETURN_IF_FALSE(pui64Pos != NULL && *pui64Pos == 0, -EIO);
	PVR_RETURN_IF_FALSE(ui64Count > 0 && ui64Count < uiMaxBufferSize, -EINVAL);
	PVR_RETURN_IF_FALSE(pcBuffer[ui64Count - 1] == '\0', -EINVAL);

	if (sscanf(pcBuffer, "%u", &ui32Level) == 0)
	{
		return -EINVAL;
	}

	OSSetDebugLevel(ui32Level & ((1 << __builtin_ffsl(DBGPRIV_LAST)) - 1));

	*pui64Pos += ui64Count;
	return ui64Count;
}
#endif /* defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON) */

PVRSRV_ERROR DebugCommonInit(void)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_ERROR eError;

	PVR_ASSERT(psPVRSRVData != NULL);

	/*
	 * The DebugFS entries are designed to work in a single device system but
	 * this function will be called multiple times in a multi-device system.
	 * Return an error in this case.
	 */
	if (gpsVersionDIEntry)
	{
		return -EEXIST;
	}

#if defined(SUPPORT_RGX) && !defined(NO_HARDWARE)
	if (SORgxGpuUtilStatsRegister(&ghGpuUtilUserDebugFS) != PVRSRV_OK)
	{
		return -ENOMEM;
	}
#endif /* defined(SUPPORT_RGX) && !defined(NO_HARDWARE) */

	{
		DI_ITERATOR_CB sIterator = {
			.pfnStart = _VersionDIStart,
			.pfnStop = _VersionDIStop,
			.pfnNext = _VersionDINext,
			.pfnShow = _VersionDIShow
		};

		eError = DICreateEntry("version", NULL, &sIterator, psPVRSRVData,
		                       DI_ENTRY_TYPE_GENERIC, &gpsVersionDIEntry);
		PVR_GOTO_IF_ERROR(eError, return_error_);
	}

	{
		DI_ITERATOR_CB sIterator = {
			.pfnStart = _DebugStatusDIStart,
			.pfnStop = _DebugStatusDIStop,
			.pfnNext = _DebugStatusDINext,
			.pfnShow = _DebugStatusDIShow,
			.pfnWrite = DebugStatusSet
		};
		eError = DICreateEntry("status", NULL, &sIterator, psPVRSRVData,
		                       DI_ENTRY_TYPE_GENERIC, &gpsStatusDIEntry);
		PVR_GOTO_IF_ERROR(eError, return_error_);
	}

	{
		DI_ITERATOR_CB sIterator = {
			.pfnStart = _DebugDumpDebugDIStart,
			.pfnStop = _DebugDumpDebugDIStop,
			.pfnNext = _DebugDumpDebugDINext,
			.pfnShow = _DebugDumpDebugDIShow
		};
		eError = DICreateEntry("debug_dump", NULL, &sIterator, psPVRSRVData,
		                       DI_ENTRY_TYPE_GENERIC, &gpsDumpDebugDIEntry);
		PVR_GOTO_IF_ERROR(eError, return_error_);
	}

#ifdef SUPPORT_RGX
	if (! PVRSRV_VZ_MODE_IS(GUEST))
	{
		{
			DI_ITERATOR_CB sIterator = {
				.pfnStart = _DebugFWTraceDIStart,
				.pfnStop = _DebugFWTraceDIStop,
				.pfnNext = _DebugFWTraceDINext,
				.pfnShow = _DebugFWTraceDIShow
			};
			eError = DICreateEntry("firmware_trace", NULL, &sIterator,
			                       psPVRSRVData, DI_ENTRY_TYPE_GENERIC,
			                       &gpsFWTraceDIEntry);
			PVR_GOTO_IF_ERROR(eError, return_error_);
		}

#ifdef SUPPORT_POWER_VALIDATION_VIA_DEBUGFS
		{
			DI_ITERATOR_CB sIterator = {
				.pfnStart = _PowMonTraceDIStart,
				.pfnStop = _PowMonTraceDIStop,
				.pfnNext = _PowMonTraceDINext,
				.pfnShow = _PowMonTraceDIShow
			};
			eError = DICreateEntry("power_mon", NULL, &sIterator, psPVRSRVData,
			                       DI_ENTRY_TYPE_GENERIC, &gpsPowMonDIEntry);
			PVR_GOTO_IF_ERROR(eError, return_error_);
		}
#endif /* SUPPORT_POWER_VALIDATION_VIA_DEBUGFS */
	}

#ifdef SUPPORT_FIRMWARE_GCOV
	{
		DI_ITERATOR_CB sIterator = {
			.pfnStart = _FirmwareGcovDIStart,
			.pfnStop = _FirmwareGcovDIStop,
			.pfnNext = _FirmwareGcovDINext,
			.pfnShow = _FirmwareGcovDIShow
		};

		eError = DICreateEntry("firmware_gcov", NULL, &sIterator, psPVRSRVData,
		                       DI_ENTRY_TYPE_GENERIC, &gpsFirmwareGcovDIEntry);
		PVR_GOTO_IF_ERROR(eError, return_error_);
	}
#endif /* SUPPORT_FIRMWARE_GCOV */

#ifdef SUPPORT_POWER_SAMPLING_VIA_DEBUGFS
	{
		DI_ITERATOR_CB sIterator = {
			.pfnStart = _DebugPowerDataDIStart,
			.pfnStop = _DebugPowerDataDIStop,
			.pfnNext = _DebugPowerDataDINext,
			.pfnShow = _DebugPowerDataDIShow,
			.pfnWrite = PowerDataSet
		};
		eError = DICreateEntry("power_data", NULL, &sIterator, psPVRSRVData,
		                       DI_ENTRY_TYPE_GENERIC, &gpsPowerDataDIEntry);
		PVR_GOTO_IF_ERROR(eError, return_error_);
	}
#endif /* SUPPORT_POWER_SAMPLING_VIA_DEBUGFS */

#ifdef SUPPORT_VALIDATION
	{
		DI_ITERATOR_CB sIterator = {
			.pfnSeek = _RgxRegsSeek,
			.pfnRead = _RgxRegsRead,
			.pfnWrite = _RgxRegsWrite
		};
		eError = DICreateEntry("rgxregs", NULL, &sIterator, psPVRSRVData,
		                       DI_ENTRY_TYPE_RANDOM_ACCESS, &gpsRGXRegsDIEntry);

		PVR_GOTO_IF_ERROR(eError, return_error_);
	}
#endif /* SUPPORT_VALIDATION */
#endif /* SUPPORT_RGX */

#ifdef SUPPORT_VALIDATION
	{
		DI_ITERATOR_CB sIterator = {
			.pfnShow = TestMemLeakDIShow,
			.pfnWrite = TestMemLeakDISet
		};
		eError = DICreateEntry("test_memleak", NULL, &sIterator, psPVRSRVData,
		                       DI_ENTRY_TYPE_GENERIC, &gpsTestMemLeakDIEntry);
		PVR_GOTO_IF_ERROR(eError, return_error_);
	}
#endif /* SUPPORT_VALIDATION */

#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)
	{
		DI_ITERATOR_CB sIterator = {
			.pfnShow = DebugLevelDIShow,
			.pfnWrite = DebugLevelSet
		};
		eError = DICreateEntry("debug_level", NULL, &sIterator, NULL,
		                       DI_ENTRY_TYPE_GENERIC, &gpsDebugLevelDIEntry);
		PVR_GOTO_IF_ERROR(eError, return_error_);
	}
#endif /* defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON) */

	return PVRSRV_OK;

return_error_:
	DebugCommonDeInit();

	return eError;
}

void DebugCommonDeInit(void)
{
#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)
	if (gpsDebugLevelDIEntry != NULL)
	{
		DIDestroyEntry(gpsDebugLevelDIEntry);
	}
#endif /* defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON) */

#if defined(SUPPORT_RGX) && !defined(NO_HARDWARE)
	if (ghGpuUtilUserDebugFS != NULL)
	{
		SORgxGpuUtilStatsUnregister(ghGpuUtilUserDebugFS);
		ghGpuUtilUserDebugFS = NULL;
	}
#endif /* defined(SUPPORT_RGX) && !defined(NO_HARDWARE) */

#ifdef SUPPORT_RGX
	if (gpsFWTraceDIEntry != NULL)
	{
		DIDestroyEntry(gpsFWTraceDIEntry);
	}

#ifdef SUPPORT_POWER_VALIDATION_VIA_DEBUGFS
	if (gpsPowMonDIEntry != NULL)
	{
		DIDestroyEntry(gpsPowMonDIEntry);
	}
#endif /* SUPPORT_POWER_VALIDATION_VIA_DEBUGFS */

#ifdef SUPPORT_FIRMWARE_GCOV
	if (gpsFirmwareGcovDIEntry != NULL)
	{
		DIDestroyEntry(gpsFirmwareGcovDIEntry);
	}
#endif /* SUPPORT_FIRMWARE_GCOV */

#ifdef SUPPORT_POWER_SAMPLING_VIA_DEBUGFS
	if (gpsPowerDataDIEntry != NULL)
	{
		DIDestroyEntry(gpsPowerDataDIEntry);
	}
#endif /* SUPPORT_POWER_SAMPLING_VIA_DEBUGFS */

#ifdef SUPPORT_VALIDATION
	if (gpsRGXRegsDIEntry != NULL)
	{
		DIDestroyEntry(gpsRGXRegsDIEntry);
	}
#endif /* SUPPORT_VALIDATION */
#endif /* SUPPORT_RGX */

#ifdef SUPPORT_VALIDATION
	if (gpsTestMemLeakDIEntry != NULL)
	{
		DIDestroyEntry(gpsTestMemLeakDIEntry);
	}
#endif /* SUPPORT_VALIDATION */

	if (gpsDumpDebugDIEntry != NULL)
	{
		DIDestroyEntry(gpsDumpDebugDIEntry);
	}

	if (gpsStatusDIEntry != NULL)
	{
		DIDestroyEntry(gpsStatusDIEntry);
	}

	if (gpsVersionDIEntry != NULL)
	{
		DIDestroyEntry(gpsVersionDIEntry);
	}
}
