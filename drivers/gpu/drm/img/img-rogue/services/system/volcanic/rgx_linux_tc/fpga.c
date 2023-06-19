/*************************************************************************/ /*!
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

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
#include <linux/module.h>
#endif

#include "img_types.h"
#include "img_defs.h"
#include "pci_support.h"
#include "oskm_apphint.h"
#include "pdump_km.h"
#include "rgxtbdefs_km.h"


#include "fpga.h"

/*!
 ******************************************************************************
 * System FPGA parameters
 *****************************************************************************/
static IMG_UINT32 gui32SysTBBWDropN                 = 0;
static IMG_UINT32 gui32SysTBBWPeriod                = 0;
static IMG_UINT32 gui32SysTBQOS0Min                 = 0;
static IMG_UINT32 gui32SysTBQOS0Max                 = 0;
static IMG_UINT32 gui32SysTBQOS15Min                = 0;
static IMG_UINT32 gui32SysTBQOS15Max                = 0;
static IMG_UINT32 gui32SysTBQOSDist                 = 0;
static IMG_UINT32 gui32SysTBMemArbiter              = 0;
static IMG_UINT32 gui32SysTBMaxIdOutRW              = 0;
static IMG_UINT32 gui32SysTBMaxIdOutWr              = 0;
static IMG_UINT32 gui32SysTBMaxIdOutRd              = 0;
/* these allow raw writes to RGX_TB_QOS_RD_LATENCY and RGX_TB_QOS_WR_LATENCY */
static IMG_UINT64 gui64SysTBQOSLatencyRd            = 0;
static IMG_UINT64 gui64SysTBQOSLatencyWr            = 0;

#if defined(__linux__)
#include <linux/module.h>
#include <linux/dma-mapping.h>
module_param_named(sys_tb_bandwidth_drop,            gui32SysTBBWDropN,                 uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_bandwidth_period,          gui32SysTBBWPeriod,                uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_qos0_min,                  gui32SysTBQOS0Min,                 uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_qos0_max,                  gui32SysTBQOS0Max,                 uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_qos15_min,                 gui32SysTBQOS15Min,                uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_qos15_max,                 gui32SysTBQOS15Max,                uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_qos_dist,                  gui32SysTBQOSDist,                 uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_mem_arbiter,               gui32SysTBMemArbiter,              uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_max_id_outstanding_rdwr,   gui32SysTBMaxIdOutRW,              uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_max_id_outstanding_wr,     gui32SysTBMaxIdOutWr,              uint,   S_IRUGO | S_IWUSR);
module_param_named(sys_tb_max_id_outstanding_rd,     gui32SysTBMaxIdOutRd,              uint,   S_IRUGO | S_IWUSR);

module_param_named(sys_tb_qos_latency_rd,            gui64SysTBQOSLatencyRd,            ullong,  S_IRUGO | S_IWUSR);
module_param_named(sys_tb_qos_latency_wr,            gui64SysTBQOSLatencyWr,            ullong,  S_IRUGO | S_IWUSR);
#endif


/*
 * work out the length in bits for a register field from its ~CLRMSK.
 * Assumes the relevant bits are all contiguous.
 */
static IMG_UINT32 FieldLengthBits(IMG_UINT32 mask)
{
	IMG_UINT32 count = 0;

	while (mask != 0)
	{
		count += (mask & 1);
		mask >>= 1;
	}
	return count;
}


static IMG_UINT32 TBBandwidthLimiterGet(void)
{
	IMG_UINT32 ui32BandwidthLimiter = 0;

	/* create bandwidth limiter reg value */
	if (gui32SysTBBWDropN != 0  ||  gui32SysTBBWPeriod != 0)
	{
		IMG_UINT32 ui31DropN_ext;
		IMG_UINT32 ui31Period_ext;

		/* get EXT bits from the apphint values */
		ui31DropN_ext = gui32SysTBBWDropN >> FieldLengthBits(~RGX_TB_BW_LIMITER_DROPN_CLRMSK);
		ui31Period_ext = gui32SysTBBWPeriod >> FieldLengthBits(~RGX_TB_BW_LIMITER_PERIOD_CLRMSK);

		ui32BandwidthLimiter = (RGX_TB_BW_LIMITER_ENABLE_EN << RGX_TB_BW_LIMITER_ENABLE_SHIFT) |
							   ((gui32SysTBBWDropN << RGX_TB_BW_LIMITER_DROPN_SHIFT) & ~RGX_TB_BW_LIMITER_DROPN_CLRMSK) |
							   ((gui32SysTBBWPeriod << RGX_TB_BW_LIMITER_PERIOD_SHIFT) & ~RGX_TB_BW_LIMITER_PERIOD_CLRMSK) |
							   ((ui31DropN_ext << RGX_TB_BW_LIMITER_DROPN_EXT_SHIFT) & ~RGX_TB_BW_LIMITER_DROPN_EXT_CLRMSK) |
							   ((ui31Period_ext << RGX_TB_BW_LIMITER_PERIOD_EXT_SHIFT) & ~RGX_TB_BW_LIMITER_PERIOD_EXT_CLRMSK);
	}

	return ui32BandwidthLimiter;
}


/*
 * These latencies can be specified in total using gui64SysTBQOSLatencyRd/Wr or in individual fields.
 * If full register is specified then the individual fields are ignored.
 * Individual fields only allow same values to be set for read and write.
 */
static IMG_UINT64 TBQOSReadLatencyGet(void)
{
	if (gui64SysTBQOSLatencyRd != 0)
	{
		return gui64SysTBQOSLatencyRd;
	}

	return ((((IMG_UINT64)gui32SysTBQOS15Max) << RGX_TB_QOS_RD_LATENCY_MAX_15_SHIFT) & ~RGX_TB_QOS_RD_LATENCY_MAX_15_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBQOS15Min) << RGX_TB_QOS_RD_LATENCY_MIN_15_SHIFT) & ~RGX_TB_QOS_RD_LATENCY_MIN_15_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBQOS0Max)  << RGX_TB_QOS_RD_LATENCY_MAX_0_SHIFT)  & ~RGX_TB_QOS_RD_LATENCY_MAX_0_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBQOS0Min)  << RGX_TB_QOS_RD_LATENCY_MIN_0_SHIFT)  & ~RGX_TB_QOS_RD_LATENCY_MIN_0_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBQOSDist)  << RGX_TB_QOS_RD_LATENCY_DIST_SHIFT)  & ~RGX_TB_QOS_RD_LATENCY_DIST_CLRMSK);
}

static IMG_UINT64 TBQOSWriteLatencyGet(void)
{
	if (gui64SysTBQOSLatencyWr != 0)
	{
		return gui64SysTBQOSLatencyWr;
	}

	return ((((IMG_UINT64)gui32SysTBQOS15Max) << RGX_TB_QOS_WR_LATENCY_MAX_15_SHIFT) & ~RGX_TB_QOS_WR_LATENCY_MAX_15_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBQOS15Min) << RGX_TB_QOS_WR_LATENCY_MIN_15_SHIFT) & ~RGX_TB_QOS_WR_LATENCY_MIN_15_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBQOS0Max)  << RGX_TB_QOS_WR_LATENCY_MAX_0_SHIFT)  & ~RGX_TB_QOS_WR_LATENCY_MAX_0_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBQOS0Min)  << RGX_TB_QOS_WR_LATENCY_MIN_0_SHIFT)  & ~RGX_TB_QOS_WR_LATENCY_MIN_0_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBQOSDist)  << RGX_TB_QOS_WR_LATENCY_DIST_SHIFT)  & ~RGX_TB_QOS_WR_LATENCY_DIST_CLRMSK);
}

static IMG_UINT32 TBMemArbiterGet(void)
{
	return gui32SysTBMemArbiter;
}

static IMG_UINT64 TBQOSMaxIdOutstandingGet(void)
{
	return ((((IMG_UINT64)gui32SysTBMaxIdOutRW) << RGX_TB_MAX_ID_OUTSTANDING_RD_WR_SHIFT) & ~RGX_TB_MAX_ID_OUTSTANDING_RD_WR_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBMaxIdOutWr) << RGX_TB_MAX_ID_OUTSTANDING_WRITE_SHIFT) & ~RGX_TB_MAX_ID_OUTSTANDING_WRITE_CLRMSK) |
	       ((((IMG_UINT64)gui32SysTBMaxIdOutRd) << RGX_TB_MAX_ID_OUTSTANDING_READ_SHIFT) & ~RGX_TB_MAX_ID_OUTSTANDING_READ_CLRMSK);
}


PVRSRV_ERROR FPGA_Reset(struct resource *registers, IMG_BOOL bFullReset)
{
	IMG_CPU_PHYADDR	sWrapperRegsCpuPBase;
	void			*pvWrapperRegs;
	IMG_UINT32		ui32BandwidthLimiter;
	IMG_UINT64		ui64ReadQOSLatency;
	IMG_UINT64		ui64WriteQOSLatency;
	IMG_UINT32		ui32MemArbiter;
	IMG_UINT64		ui64MaxIdOutstanding;

	sWrapperRegsCpuPBase.uiAddr = registers->start + FPGA_RGX_TB_REG_WRAPPER_OFFSET;

	/*
		Create a temporary mapping of the FPGA wrapper registers in order to reset
		required registers.
	*/
	pvWrapperRegs = OSMapPhysToLin(sWrapperRegsCpuPBase,
								   FPGA_RGX_TB_REG_WRAPPER_SIZE,
								   PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);
	if (pvWrapperRegs == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to create wrapper register mapping", __func__));
		return PVRSRV_ERROR_BAD_MAPPING;
	}

	/*
		Set the bandwidth limiter if required.
	*/
	ui32BandwidthLimiter = TBBandwidthLimiterGet();
	if (ui32BandwidthLimiter != 0)
	{
		OSWriteHWReg32(pvWrapperRegs, RGX_TB_BW_LIMITER, ui32BandwidthLimiter);
		(void) OSReadHWReg32(pvWrapperRegs, RGX_TB_BW_LIMITER);
		PVR_LOG(("%s: Bandwidth limiter = 0x%08X", __func__, OSReadHWReg32(pvWrapperRegs, RGX_TB_BW_LIMITER)));
	}

	/*
		Set the read/write QoS latency values if required.
	*/
	ui64ReadQOSLatency = TBQOSReadLatencyGet();
	if (ui64ReadQOSLatency != 0)
	{
		OSWriteHWReg64(pvWrapperRegs, RGX_TB_QOS_RD_LATENCY, ui64ReadQOSLatency);
		(void) OSReadHWReg64(pvWrapperRegs, RGX_TB_QOS_RD_LATENCY);
		PVR_LOG(("%s: QOS Read latency = 0x%016llX", __func__, OSReadHWReg64(pvWrapperRegs, RGX_TB_QOS_RD_LATENCY)));
	}

	ui64WriteQOSLatency = TBQOSWriteLatencyGet();
	if (ui64WriteQOSLatency != 0)
	{
		OSWriteHWReg64(pvWrapperRegs, RGX_TB_QOS_WR_LATENCY, ui64WriteQOSLatency);
		(void) OSReadHWReg64(pvWrapperRegs, RGX_TB_QOS_WR_LATENCY);
		PVR_LOG(("%s: QOS Write latency = 0x%016llX", __func__, OSReadHWReg64(pvWrapperRegs, RGX_TB_QOS_WR_LATENCY)));
	}

	ui32MemArbiter = TBMemArbiterGet();
	if (ui32MemArbiter != 0)
	{
		OSWriteHWReg32(pvWrapperRegs, RGX_TB_MEM_ARBITER, ui32MemArbiter);
		(void) OSReadHWReg32(pvWrapperRegs, RGX_TB_MEM_ARBITER);
		PVR_LOG(("%s: Mem arbiter = 0x%08X", __func__, OSReadHWReg32(pvWrapperRegs, RGX_TB_MEM_ARBITER)));
	}

	ui64MaxIdOutstanding = TBQOSMaxIdOutstandingGet();
	if (ui64MaxIdOutstanding != 0)
	{
		OSWriteHWReg64(pvWrapperRegs, RGX_TB_MAX_ID_OUTSTANDING, ui64MaxIdOutstanding);
		(void) OSReadHWReg64(pvWrapperRegs, RGX_TB_MAX_ID_OUTSTANDING);
		PVR_LOG(("%s: Max Id Outstanding = 0x%016llX", __func__, OSReadHWReg64(pvWrapperRegs, RGX_TB_MAX_ID_OUTSTANDING)));
	}

	/*
		Remove the temporary register mapping.
	*/
	OSUnMapPhysToLin(pvWrapperRegs, FPGA_RGX_TB_REG_WRAPPER_SIZE);

	return PVRSRV_OK;
}


PVRSRV_ERROR FPGA_SysDebugInfo(struct resource *registers,
                               DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                               void *pvDumpDebugFile)
{
	IMG_CPU_PHYADDR	sWrapperRegsCpuPBase;
	void			*pvWrapperRegs;

	sWrapperRegsCpuPBase.uiAddr = registers->start + FPGA_RGX_TB_REG_WRAPPER_OFFSET;

	/*
		Create a temporary mapping of the FPGA wrapper registers.
	*/
	pvWrapperRegs = OSMapPhysToLin(sWrapperRegsCpuPBase,
								   FPGA_RGX_TB_REG_WRAPPER_SIZE,
								   PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);
	if (pvWrapperRegs == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to create wrapper register mapping", __func__));
		return PVRSRV_ERROR_BAD_MAPPING;
	}

#define SYS_FPGA_DBG_R32(R)	PVR_DUMPDEBUG_LOG("%-29s : 0x%08X", #R, (IMG_UINT32)OSReadHWReg32(pvWrapperRegs, RGX_TB_##R))
#define SYS_FPGA_DBG_R64(R)	PVR_DUMPDEBUG_LOG("%-29s : 0x%016llX", #R, (IMG_UINT64)OSReadHWReg64(pvWrapperRegs, RGX_TB_##R))

	SYS_FPGA_DBG_R32(BW_LIMITER);

	SYS_FPGA_DBG_R64(QOS_RD_LATENCY);
	SYS_FPGA_DBG_R64(QOS_WR_LATENCY);

	SYS_FPGA_DBG_R32(MEM_ARBITER);
	SYS_FPGA_DBG_R64(MAX_ID_OUTSTANDING);

	/*
		Remove the temporary register mapping.
	*/
	OSUnMapPhysToLin(pvWrapperRegs, FPGA_RGX_TB_REG_WRAPPER_SIZE);

	return PVRSRV_OK;
}
