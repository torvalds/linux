/*************************************************************************/ /*!
@File
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the system layer for QEMU vexpress virtual-platform
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

#if defined(__linux__)
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <soc/starfive/jh7110_pmu.h>
#endif

#include "pvr_debug.h"
#include "allocmem.h"
#include "interrupt_support.h"

#include "pvrsrv_device.h"
#include "syscommon.h"
#include "sysinfo.h"
#include "sysconfig.h"
#include "physheap.h"
#include "interrupt_support.h"
#include "vz_vmm_pvz.h"
#if defined(SUPPORT_PDVFS)
#include "rgxpdvfs.h"
#endif
#include <asm/pgtable.h>
#include "pvr_vmap.h"
#include "linux/highmem.h"

#include "kernel_compatibility.h"
#include <linux/pm_runtime.h>

struct sf7110_cfg  sf_cfg_t = {0,};

static RGX_TIMING_INFORMATION	gsRGXTimingInfo;
static RGX_DATA			gsRGXData;
static PVRSRV_DEVICE_CONFIG	gsDevices[1];
static PHYS_HEAP_FUNCTIONS	gsPhysHeapFuncs;
static PHYS_HEAP_CONFIG		gsPhysHeapConfig[1];
static struct page *g_zero_page = NULL;


#if defined(SUPPORT_PDVFS)
static const IMG_OPP asOPPTable[] =
{
	{ 824,  240000000},
	{ 856,  280000000},
	{ 935,  380000000},
	{ 982,  440000000},
	{ 1061, 540000000},
};

#define LEVEL_COUNT (sizeof(asOPPTable) / sizeof(asOPPTable[0]))

static void SetFrequency(IMG_UINT32 ui32Frequency) {}
static void SetVoltage(IMG_UINT32 ui32Volt) {}
#endif

extern void sifive_l2_flush64_range(unsigned long start, unsigned long len);

void do_sifive_l2_flush64_range(unsigned long start, unsigned long len)
{
	sifive_l2_flush64_range(ALIGN_DOWN(start, 64), len + start % 64);
}

void do_invalid_range(unsigned long start, unsigned long len)
{
	unsigned long sz = 2 * 1024 * 1024;
	unsigned long *pv = NULL;

	if(NULL == g_zero_page)
	{
		g_zero_page = alloc_pages(GFP_KERNEL, get_order(sz));
		if (NULL == g_zero_page)
		{
			printk("alloc zero invalid page failed!\\n");
			return;
		}
	}
	pv = page_address(g_zero_page);
	memset(pv, 0, sz);

	do_sifive_l2_flush64_range(page_to_phys(g_zero_page), sz);
}

/*
	CPU to Device physical address translation
*/
static
void UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					IMG_UINT32 ui32NumOfAddr,
					IMG_DEV_PHYADDR *psDevPAddr,
					IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr;
		}
	}
}

/*
	Device to CPU physical address translation
*/
static
void UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
								   IMG_UINT32 ui32NumOfAddr,
								   IMG_CPU_PHYADDR *psCpuPAddr,
								   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr;
		}
	}
}

static void SysDevFeatureDepInit(PVRSRV_DEVICE_CONFIG *psDevConfig, IMG_UINT64 ui64Features)
{
#if defined(SUPPORT_AXI_ACE_TEST)
	if ( ui64Features & RGX_FEATURE_AXI_ACELITE_BIT_MASK)
	{
		gsDevices[0].eCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_CPU_ONLY;
	}
	else
#endif
	{
		psDevConfig->eCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_EMULATED;
	}
}

#if 1

void SysDevHost_Cache_Maintenance(IMG_HANDLE hSysData,
									PVRSRV_CACHE_OP eRequestType,
									void *pvVirtStart,
									void *pvVirtEnd,
									IMG_CPU_PHYADDR sCPUPhysStart,
									IMG_CPU_PHYADDR sCPUPhysEnd)
{
	unsigned long len = 0;

	/* valid the input phy address */
	if(sCPUPhysStart.uiAddr == 0 || sCPUPhysEnd.uiAddr == 0 || sCPUPhysEnd.uiAddr < sCPUPhysStart.uiAddr)
	{
		PVR_DPF((PVR_DBG_ERROR,	"%s: Invalid cache op[%d] range [%llx---%llx]\n",
			__func__, (uint32_t)eRequestType,
			(uint64_t)sCPUPhysStart.uiAddr, (uint64_t)sCPUPhysEnd.uiAddr));
		return;
	}
	len = (unsigned long)(sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr);
	//printk("FF cop:%d, s:%llx, len:%lx\n", eRequestType, sCPUPhysStart.uiAddr, len);
	//if(len < 64)
	//	dump_stack();
	switch (eRequestType)
	{
		case PVRSRV_CACHE_OP_INVALIDATE:
			do_sifive_l2_flush64_range(sCPUPhysStart.uiAddr, len);
			break;
		case PVRSRV_CACHE_OP_CLEAN:
		case PVRSRV_CACHE_OP_FLUSH:
			do_sifive_l2_flush64_range(sCPUPhysStart.uiAddr, len);
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,	"%s: Invalid cache operation type %d", __func__, (uint32_t)eRequestType));
			break;
	}

}
#endif

static IMG_UINT32 sys_gpu_runtime_resume(IMG_HANDLE hd)
{
	starfive_pmu_hw_event_turn_off_mask(0);
	clk_prepare_enable(sf_cfg_t.clk_axi);
	u0_img_gpu_enable();

	return 0;
}

static IMG_UINT32 sys_gpu_runtime_suspend(IMG_HANDLE hd)
{
	u0_img_gpu_disable();
	clk_disable_unprepare(sf_cfg_t.clk_axi);
	starfive_pmu_hw_event_turn_off_mask((uint32_t)-1);

	return 0;
}

static int create_sf7110_cfg(struct device *dev)
{
	struct sf7110_cfg *psf = &sf_cfg_t;

	psf->dev = dev;
	mutex_init(&psf->set_power_state);
	psf->gpu_reg_base = ioremap(STARFIVE_7110_GPU_PBASE, STARFIVE_7110_GPU_SIZE);
	if(!psf->gpu_reg_base)
		return -ENOMEM;
	psf->gpu_reg_start = STARFIVE_7110_GPU_PBASE;
	psf->gpu_reg_size = STARFIVE_7110_GPU_SIZE;

	psf->clk_apb = devm_clk_get_optional(dev, "clk_apb");
	if (IS_ERR(psf->clk_apb)) {
		dev_err(dev, "failed to get gpu clk_apb\n");
		goto err_gpu_unmap;
	}

	psf->clk_rtc = devm_clk_get_optional(dev, "clk_rtc");
	if (IS_ERR(psf->clk_rtc)) {
		dev_err(dev, "failed to get gpu clk_rtc\n");
		goto err_gpu_unmap;
	}

	psf->clk_core = devm_clk_get_optional(dev, "clk_core");
	if (IS_ERR(psf->clk_core)) {
		dev_err(dev, "failed to get gpu clk_core\n");
		goto err_gpu_unmap;
	}

	psf->clk_sys = devm_clk_get_optional(dev, "clk_sys");
	if (IS_ERR(psf->clk_sys)) {
		dev_err(dev, "failed to get gpu clk_sys\n");
		goto err_gpu_unmap;
	}

	psf->clk_axi = devm_clk_get_optional(dev, "clk_axi");
	if (IS_ERR(psf->clk_axi)) {
		dev_err(dev, "failed to get gpu clk_axi\n");
		goto err_gpu_unmap;
	}

	psf->clk_div = devm_clk_get_optional(dev, "clk_bv");
	if (IS_ERR(psf->clk_div)) {
		dev_err(dev, "failed to get gpu clk_div\n");
		goto err_gpu_unmap;
	}

	psf->rst_apb = devm_reset_control_get_exclusive(dev, "rst_apb");
	if (IS_ERR(psf->rst_apb)) {
		dev_err(dev, "failed to get GPU rst_apb\n");
		goto err_gpu_unmap;
	}

	psf->rst_doma = devm_reset_control_get_exclusive(dev, "rst_doma");
	if (IS_ERR(psf->rst_doma)) {
		dev_err(dev, "failed to get GPU rst_doma\n");
		goto err_gpu_unmap;
	}

	psf->runtime_resume = sys_gpu_runtime_resume;
	psf->runtime_suspend = sys_gpu_runtime_suspend;

	return 0;
err_gpu_unmap:
	iounmap(psf->gpu_reg_base);
	return -ENOMEM;
}

void u0_img_gpu_enable(void)
{
	clk_prepare_enable(sf_cfg_t.clk_apb);
	clk_prepare_enable(sf_cfg_t.clk_rtc);
	clk_set_rate(sf_cfg_t.clk_div, RGX_STARFIVE_7100_CORE_CLOCK_SPEED);
	clk_prepare_enable(sf_cfg_t.clk_core);
	clk_prepare_enable(sf_cfg_t.clk_sys);

	reset_control_deassert(sf_cfg_t.rst_apb);
	reset_control_deassert(sf_cfg_t.rst_doma);
}


void u0_img_gpu_disable(void)
{
	reset_control_assert(sf_cfg_t.rst_apb);
	reset_control_assert(sf_cfg_t.rst_doma);

	clk_disable_unprepare(sf_cfg_t.clk_apb);
	clk_disable_unprepare(sf_cfg_t.clk_rtc);
	clk_disable_unprepare(sf_cfg_t.clk_core);
	clk_disable_unprepare(sf_cfg_t.clk_sys);
}

static int sys_gpu_enable(void)
{
	int ret;

	ret = pm_runtime_get_sync(sf_cfg_t.dev);
	if (ret < 0) {
		dev_err(sf_cfg_t.dev, "gpu: failed to get pm runtime: %d\n", ret);
		return ret;
	}

	return 0;
}

static int sys_gpu_disable(void)
{
	pm_runtime_put_sync(sf_cfg_t.dev);
	//pm_runtime_disable(sf_cfg_t.dev);
	return 0;
}

static PVRSRV_ERROR sfSysDevPrePowerState(
		IMG_HANDLE hSysData,
		PVRSRV_SYS_POWER_STATE eNewPowerState,
		PVRSRV_SYS_POWER_STATE eCurrentPowerState,
		PVRSRV_POWER_FLAGS ePwrFlags)
{
	struct sf7110_cfg *psf = hSysData;

	pr_debug("(%s()) state: current=%d, new=%d; flags: 0x%08x", __func__,
	     eCurrentPowerState, eNewPowerState, ePwrFlags);

	mutex_lock(&psf->set_power_state);

	if ((PVRSRV_SYS_POWER_STATE_OFF == eNewPowerState) &&
		(PVRSRV_SYS_POWER_STATE_ON == eCurrentPowerState))
		sys_gpu_disable();

	mutex_unlock(&psf->set_power_state);
	return PVRSRV_OK;
}

static PVRSRV_ERROR sfSysDevPostPowerState(
		IMG_HANDLE hSysData,
		PVRSRV_SYS_POWER_STATE eNewPowerState,
		PVRSRV_SYS_POWER_STATE eCurrentPowerState,
		PVRSRV_POWER_FLAGS ePwrFlags)
{
	struct sf7110_cfg *psf = hSysData;
	PVRSRV_ERROR ret;

	pr_debug("(%s()) state: current=%d, new=%d; flags: 0x%08x", __func__,
	     eCurrentPowerState, eNewPowerState, ePwrFlags);

	mutex_lock(&psf->set_power_state);

	if ((PVRSRV_SYS_POWER_STATE_ON == eNewPowerState) &&
		(PVRSRV_SYS_POWER_STATE_OFF == eCurrentPowerState)) {
		if (sys_gpu_enable()) {
			ret = PVRSRV_ERROR_DEVICE_POWER_CHANGE_FAILURE;
			goto done;
		}
	}
	ret = PVRSRV_OK;
done:
	mutex_unlock(&psf->set_power_state);

	return ret;
}


PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
#if defined(__linux__)
	int iIrq;
	struct resource *psDevMemRes = NULL;
	struct platform_device *psDev;

	psDev = to_platform_device((struct device *)pvOSDevice);
	printk("@@ dev ptr:%llx/%d/%d\n", (uint64_t)psDev,DEVICES_WATCHDOG_POWER_ON_SLEEP_TIMEOUT,
	PVRSRV_APPHINT_ENABLEPAGEFAULTDEBUG);
#endif

	if (gsDevices[0].pvOSDevice)
		return PVRSRV_ERROR_INVALID_DEVICE;

#if defined(__linux__)
	dma_set_mask(pvOSDevice, DMA_BIT_MASK(32));
#endif

	/*
	 * Setup information about physical memory heap(s) we have
	 */
	gsPhysHeapFuncs.pfnCpuPAddrToDevPAddr = UMAPhysHeapCpuPAddrToDevPAddr;
	gsPhysHeapFuncs.pfnDevPAddrToCpuPAddr = UMAPhysHeapDevPAddrToCpuPAddr;

	gsPhysHeapConfig[0].pszPDumpMemspaceName = "SYSMEM";
	gsPhysHeapConfig[0].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[0].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[0].hPrivData = NULL;
	gsPhysHeapConfig[0].ui32UsageFlags = PHYS_HEAP_USAGE_GPU_LOCAL;
	//ui32NextPhysHeapID += 1;

	/*
	 * Setup RGX specific timing data
	 */
	gsRGXTimingInfo.ui32CoreClockSpeed	= RGX_STARFIVE_7100_CORE_CLOCK_SPEED;
	gsRGXTimingInfo.bEnableActivePM		= IMG_TRUE;
	gsRGXTimingInfo.bEnableRDPowIsland	= IMG_TRUE;
	gsRGXTimingInfo.ui32ActivePMLatencyms	= SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/*
	 *Setup RGX specific data
	 */
	gsRGXData.psRGXTimingInfo = &gsRGXTimingInfo;

	/*
	 * Setup device
	 */
	gsDevices[0].pvOSDevice		= pvOSDevice;
	gsDevices[0].pszName		= "starfive_7110";
	gsDevices[0].pszVersion		= NULL;

	/* Device setup information */
#if defined(__linux__)
	psDevMemRes = platform_get_resource(psDev, IORESOURCE_MEM, 0);
	if (psDevMemRes) {
		gsDevices[0].sRegsCpuPBase.uiAddr = psDevMemRes->start;
		gsDevices[0].ui32RegsSize = (unsigned int)(psDevMemRes->end - psDevMemRes->start);
	} else
#endif
	{
#if defined(__linux__)
		PVR_LOG(("%s: platform_get_resource() failed, using mmio/sz 0x%x/0x%x",
				__func__,
				STARFIVE_7110_GPU_PBASE,
				STARFIVE_7110_GPU_SIZE));
#endif
		gsDevices[0].sRegsCpuPBase.uiAddr   = STARFIVE_7110_GPU_PBASE;
		gsDevices[0].ui32RegsSize           = STARFIVE_7110_GPU_SIZE;
	}

	gsDevices[0].eDefaultHeap = PVRSRV_PHYS_HEAP_GPU_LOCAL;
#if defined(__linux__)
	iIrq = platform_get_irq(psDev, 0);
	if (iIrq >= 0) {
		gsDevices[0].ui32IRQ = (IMG_UINT32) iIrq;
	} else
#endif
	{
#if defined(__linux__)
		PVR_LOG(("%s: platform_get_irq() failed, using irq %d",
				__func__,
				STARFIVE_7110_IRQ_GPU));
#endif
		gsDevices[0].ui32IRQ = STARFIVE_7110_IRQ_GPU;
	}

	/* Device's physical heaps */
	gsDevices[0].pasPhysHeaps = gsPhysHeapConfig;
	gsDevices[0].ui32PhysHeapCount = ARRAY_SIZE(gsPhysHeapConfig);

	if (create_sf7110_cfg(&psDev->dev)) {
		return PVRSRV_ERROR_BAD_MAPPING;
	}
	gsDevices[0].hSysData = &sf_cfg_t;

	pm_runtime_enable(sf_cfg_t.dev);
	/* power management on HW system */
	gsDevices[0].pfnPrePowerState = sfSysDevPrePowerState;
	gsDevices[0].pfnPostPowerState = sfSysDevPostPowerState;

	/* No clock frequency either */
	gsDevices[0].pfnClockFreqGet = NULL;

	gsDevices[0].hDevData = &gsRGXData;

	gsDevices[0].pfnSysDevFeatureDepInit = &SysDevFeatureDepInit;

	gsDevices[0].bHasFBCDCVersion31 = IMG_FALSE;
	gsDevices[0].bDevicePA0IsValid = IMG_FALSE;

	/* device error notify callback function */
	gsDevices[0].pfnSysDevErrorNotify = NULL;
	gsDevices[0].pfnHostCacheMaintenance = SysDevHost_Cache_Maintenance;

#if defined(SUPPORT_PDVFS)
	gsDevices[0].sDVFS.sDVFSDeviceCfg.pasOPPTable = asOPPTable;
	gsDevices[0].sDVFS.sDVFSDeviceCfg.ui32OPPTableSize = LEVEL_COUNT;
	gsDevices[0].sDVFS.sDVFSDeviceCfg.pfnSetFrequency = SetFrequency;
	gsDevices[0].sDVFS.sDVFSDeviceCfg.pfnSetVoltage = SetVoltage;
#endif

	*ppsDevConfig = &gsDevices[0];

	return PVRSRV_OK;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	psDevConfig->pvOSDevice = NULL;
}

typedef struct LISR_DATA_TAG
{
	IMG_UINT32	ui32IRQ;
	PFN_SYS_LISR	pfnLISR;
	void		*pvData;
} LISR_DATA;

static irqreturn_t img_interrupt(int irq, void *dev_id)
{
	LISR_DATA *psLISRData = (LISR_DATA *)dev_id;

	PVR_UNREFERENCED_PARAMETER(irq);

	if (psLISRData) {
		if (psLISRData->pfnLISR(psLISRData->pvData))
			return IRQ_HANDLED;
	} else {
		PVR_DPF((PVR_DBG_ERROR, "%s: Missing interrupt data", __func__));
	}

	return IRQ_NONE;
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
					IMG_UINT32 ui32IRQ,
					const IMG_CHAR *pszName,
					PFN_LISR pfnLISR,
					void *pvData,
					IMG_HANDLE *phLISRData)
{
	unsigned long ui32Flags = SYS_IRQ_FLAG_TRIGGER_DEFAULT;
	LISR_DATA *psLISRData;
	unsigned long ulIRQFlags = 0;

	PVR_UNREFERENCED_PARAMETER(hSysData);

	if (pfnLISR == NULL || pvData == NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	if (ui32Flags & ~SYS_IRQ_FLAG_MASK)
		return PVRSRV_ERROR_INVALID_PARAMS;

	switch (ui32Flags & SYS_IRQ_FLAG_TRIGGER_MASK)
	{
		case SYS_IRQ_FLAG_TRIGGER_DEFAULT:
			break;
		case SYS_IRQ_FLAG_TRIGGER_LOW:
			ulIRQFlags |= IRQF_TRIGGER_LOW;
			break;
		case SYS_IRQ_FLAG_TRIGGER_HIGH:
			ulIRQFlags |= IRQF_TRIGGER_HIGH;
			break;
		default:
			return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32Flags & SYS_IRQ_FLAG_SHARED)
		ulIRQFlags |= IRQF_SHARED;

	psLISRData = OSAllocMem(sizeof(*psLISRData));
	if (psLISRData == NULL)
		return PVRSRV_ERROR_OUT_OF_MEMORY;

	psLISRData->ui32IRQ = ui32IRQ;
	psLISRData->pfnLISR = pfnLISR;
	psLISRData->pvData = pvData;
	if (request_irq(ui32IRQ, img_interrupt, ulIRQFlags, pszName, psLISRData)) {
		OSFreeMem(psLISRData);
		return PVRSRV_ERROR_UNABLE_TO_REGISTER_ISR_HANDLER;
	}
	*phLISRData = (IMG_HANDLE)psLISRData;

	return PVRSRV_OK;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	LISR_DATA *psLISRData = (LISR_DATA *)hLISRData;

	if (psLISRData == NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	free_irq(psLISRData->ui32IRQ, psLISRData);

	OSFreeMem(psLISRData);

	return PVRSRV_OK;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	return PVRSRV_OK;
}

struct sf7110_cfg *sys_get_privdata(void)
{
	return &sf_cfg_t;
}

/******************************************************************************
 End of file (sysconfig.c)
******************************************************************************/
