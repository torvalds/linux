/*************************************************************************/ /*!
@File
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System Configuration functions
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

#if defined(_WIN32)
#include <string.h>
#endif

#include "pvr_debug.h"
#include "osfunc.h"
#include "allocmem.h"
#include "pvrsrv_device.h"
#include "syscommon.h"
#include "power.h"
#include "sysinfo.h"
#include "apollo.h"
#include "apollo_regs.h"
#include "sysconfig.h"
#include "physheap.h"
#include "apollo_flasher.h"
#include "pci_support.h"
#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
#include "interrupt_support.h"
#endif
#include "tcf_clk_ctrl.h"
#include "tcf_pll.h"
#if defined(SUPPORT_ION)
#include PVR_ANDROID_ION_HEADER
#include "ion_support.h"
#include "ion_sys_private.h"
#endif

#if defined(LMA)
#define	HOST_PCI_INIT_FLAGS	0
#else
#define	HOST_PCI_INIT_FLAGS	HOST_PCI_INIT_FLAG_BUS_MASTER
#endif

#if defined(LDM_PCI) || defined(SUPPORT_DRM)
/* The following is exported by the Linux module code */
extern struct pci_dev *gpsPVRLDMDev;
#endif

/* Clock speed module parameters */
static IMG_UINT32 ui32MemClockSpeed  = RGX_TC_MEM_CLOCK_SPEED;
static IMG_UINT32 ui32CoreClockSpeed = RGX_TC_CORE_CLOCK_SPEED;

#if defined(LINUX)
#include <linux/module.h>
#include <linux/moduleparam.h>
module_param_named(sys_mem_clk_speed,  ui32MemClockSpeed,  uint, S_IRUGO | S_IWUSR);
module_param_named(sys_core_clk_speed, ui32CoreClockSpeed, uint, S_IRUGO | S_IWUSR);
#endif

typedef struct _SYS_DATA_ SYS_DATA;

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
typedef struct _SYS_INTERRUPT_DATA_
{
	SYS_DATA		*psSysData;
	IMG_CHAR		*pszName;
	PFN_LISR		pfnLISR;
	IMG_VOID		*pvData;
	IMG_UINT32		ui32InterruptFlag;
} SYS_INTERRUPT_DATA;
#endif

struct _SYS_DATA_
{
	IMG_UINT32		uiRefCount;

	IMG_HANDLE		hRGXPCI;

	IMG_CHAR		*pszSystemInfoString;

	IMG_CPU_PHYADDR		sSystemRegCpuPBase;
	IMG_VOID		*pvSystemRegCpuVBase;
	IMG_SIZE_T		uiSystemRegSize;

	IMG_CPU_PHYADDR		sLocalMemCpuPBase;
	IMG_SIZE_T		uiLocalMemSize;

	PVRSRV_SYS_POWER_STATE	ePowerState;

	IMG_HANDLE		hFlashData;

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
#if !defined(SUPPORT_DRM)
	IMG_HANDLE		hLISR;
#endif
	IMG_UINT32		ui32IRQ;
	SYS_INTERRUPT_DATA	sInterruptData[SYS_DEVICE_COUNT];
#endif
};

#define SYSTEM_INFO_FORMAT_STRING	"%s\tFPGA Revision: %s.%s.%s\tTCF Core Revision: %s.%s.%s\tTCF Core Target Build ID: %s\tPCI Version: %s\tMacro Version: %s.%s"
#define SYSTEM_INFO_REV_NUM_LEN		3

#define HEXIDECIMAL_TO_DECIMAL(hexIntVal)	((((hexIntVal) >> 4) * 10) + ((hexIntVal) & 0x0F))

static PVRSRV_ERROR InitMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData);
static INLINE void DeInitMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData);

static PVRSRV_ERROR ApolloFlashInit(IMG_VOID *pvData);
static PVRSRV_ERROR ApolloFlashWrite(IMG_VOID *pvData, const IMG_UINT32 *puiWord);
static PVRSRV_ERROR ApolloFlashGetStatus(IMG_VOID *pvData,
					 IMG_UINT32 *puiWriteSpace,
					 FLASH_STATUS *peFlashStatus);

static IMG_CHAR *GetSystemInfoString(SYS_DATA *psSysData)
{
	IMG_CHAR apszFPGARev[3][SYSTEM_INFO_REV_NUM_LEN];
	IMG_CHAR apszCoreRev[3][SYSTEM_INFO_REV_NUM_LEN];
	IMG_CHAR apszConfigRev[SYSTEM_INFO_REV_NUM_LEN];
	IMG_CHAR apszPCIVer[SYSTEM_INFO_REV_NUM_LEN];
	IMG_CHAR apszMacroVer[2][SYSTEM_INFO_REV_NUM_LEN];
	IMG_CHAR *pszSystemInfoString;
	IMG_UINT32 ui32StringLength;
	IMG_UINT32 ui32Value;
	IMG_CPU_PHYADDR	sHostFPGARegCpuPBase;
	IMG_VOID *pvHostFPGARegCpuVBase;

	/* To get some of the version information we need to read from a register that we don't normally have 
	   mapped. Map it temporarily (without trying to reserve it) to get the information we need. */
	sHostFPGARegCpuPBase.uiAddr	= OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM) + 0x40F0;
	pvHostFPGARegCpuVBase		= OSMapPhysToLin(sHostFPGARegCpuPBase, 0x04, 0);
	if (pvHostFPGARegCpuVBase == NULL)
	{
		return NULL;
	}

	/* Create the components of the PCI and macro versions */
	ui32Value = OSReadHWReg32(pvHostFPGARegCpuVBase, 0);
	OSSNPrintf(&apszPCIVer[0], SYSTEM_INFO_REV_NUM_LEN, "%d", HEXIDECIMAL_TO_DECIMAL((ui32Value & 0x00FF0000) >> 16));
	OSSNPrintf(&apszMacroVer[0][0], SYSTEM_INFO_REV_NUM_LEN, "%d", ((ui32Value & 0x00000F00) >> 8));
	OSSNPrintf(&apszMacroVer[1][0], SYSTEM_INFO_REV_NUM_LEN, "%d", HEXIDECIMAL_TO_DECIMAL((ui32Value & 0x000000FF) >> 0));

	/* Unmap the register now that we no longer need it */
	OSUnMapPhysToLin(pvHostFPGARegCpuVBase, 0x04, 0);

	/* Create the components of the FPGA revision number */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_FPGA_REV_REG);
	OSSNPrintf(&apszFPGARev[0][0], SYSTEM_INFO_REV_NUM_LEN, "%d", HEXIDECIMAL_TO_DECIMAL((ui32Value & FPGA_REV_REG_MAJOR_MASK) >> FPGA_REV_REG_MAJOR_SHIFT));
	OSSNPrintf(&apszFPGARev[1][0], SYSTEM_INFO_REV_NUM_LEN, "%d", HEXIDECIMAL_TO_DECIMAL((ui32Value & FPGA_REV_REG_MINOR_MASK) >> FPGA_REV_REG_MINOR_SHIFT));
	OSSNPrintf(&apszFPGARev[2][0], SYSTEM_INFO_REV_NUM_LEN, "%d", HEXIDECIMAL_TO_DECIMAL((ui32Value & FPGA_REV_REG_MAINT_MASK) >> FPGA_REV_REG_MAINT_SHIFT));

	/* Create the components of the TCF core revision number */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TCF_CORE_REV_REG);
	OSSNPrintf(&apszCoreRev[0][0], SYSTEM_INFO_REV_NUM_LEN, "%d", HEXIDECIMAL_TO_DECIMAL((ui32Value & TCF_CORE_REV_REG_MAJOR_MASK) >> TCF_CORE_REV_REG_MAJOR_SHIFT));
	OSSNPrintf(&apszCoreRev[1][0], SYSTEM_INFO_REV_NUM_LEN, "%d", HEXIDECIMAL_TO_DECIMAL((ui32Value & TCF_CORE_REV_REG_MINOR_MASK) >> TCF_CORE_REV_REG_MINOR_SHIFT));
	OSSNPrintf(&apszCoreRev[2][0], SYSTEM_INFO_REV_NUM_LEN, "%d", HEXIDECIMAL_TO_DECIMAL((ui32Value & TCF_CORE_REV_REG_MAINT_MASK) >> TCF_CORE_REV_REG_MAINT_SHIFT));

	/* Create the component of the TCF core target build ID */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TCF_CORE_TARGET_BUILD_CFG);
	OSSNPrintf(&apszConfigRev[0], SYSTEM_INFO_REV_NUM_LEN, "%d", (ui32Value & TCF_CORE_TARGET_BUILD_ID_MASK) >> TCF_CORE_TARGET_BUILD_ID_SHIFT);

	/* Calculate how much space we need to allocate for the string */
	ui32StringLength = OSStringLength(SYSTEM_INFO_FORMAT_STRING);
	ui32StringLength += OSStringLength(TC_SYSTEM_NAME);
	ui32StringLength += OSStringLength(&apszFPGARev[0][0]) + OSStringLength(&apszFPGARev[1][0]) + OSStringLength(&apszFPGARev[2][0]);
	ui32StringLength += OSStringLength(&apszCoreRev[0][0]) + OSStringLength(&apszCoreRev[1][0]) + OSStringLength(&apszCoreRev[2][0]);
	ui32StringLength += OSStringLength(&apszConfigRev[0]);
	ui32StringLength += OSStringLength(&apszPCIVer[0]);
	ui32StringLength += OSStringLength(&apszMacroVer[0][0]) + OSStringLength(&apszMacroVer[1][0]);

	/* Create the system info string */
	pszSystemInfoString = OSAllocZMem(ui32StringLength * sizeof(IMG_CHAR));
	if (pszSystemInfoString)
	{
		OSSNPrintf(&pszSystemInfoString[0], ui32StringLength, SYSTEM_INFO_FORMAT_STRING, TC_SYSTEM_NAME, 
			   &apszFPGARev[0][0], &apszFPGARev[1][0], &apszFPGARev[2][0],
			   &apszCoreRev[0][0], &apszCoreRev[1][0], &apszCoreRev[2][0],
			   &apszConfigRev[0], &apszPCIVer[0],
			   &apszMacroVer[0][0], &apszMacroVer[1][0]);
	}

	return pszSystemInfoString;
}

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
#if !defined(SUPPORT_DRM)
static
#endif
IMG_BOOL SystemISRHandler(IMG_VOID *pvData)
{
	SYS_DATA *psSysData = (SYS_DATA *)pvData;
	IMG_UINT32 ui32InterruptClear = 0;
	IMG_UINT32 ui32InterruptStatus;
	IMG_UINT32 i;

	ui32InterruptStatus = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_STATUS);

	for (i = 0; i < SYS_DEVICE_COUNT; i++)
	{
		if ((ui32InterruptStatus & psSysData->sInterruptData[i].ui32InterruptFlag) != 0)
		{
			psSysData->sInterruptData[i].pfnLISR(psSysData->sInterruptData[i].pvData);

			ui32InterruptClear |= psSysData->sInterruptData[i].ui32InterruptFlag;
		}
	}

	if (ui32InterruptClear)
	{
		OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_CLEAR, ui32InterruptClear);

		/*
		   CPU-PCI-WRITE-BUFFER

		   On CEPC platforms, this BIOS performance enhancing feature controls the chipset's 
		   CPU-to-PCI write buffer used to store PCI writes from the CPU before being written 
		   onto the PCI bus. By reading from the register, this CPU-to-PCI write
		   buffer is flushed.

		   Without this read, at times the CE kernel reports "Unwanted IRQ(11)" due to above INTR
		   clear not being reflected on the IRQ line after exit from the INTR handler SystemISRWrapper
		   which calls InterruptDone notifying CE kernel of handler completion.
		 */
		(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_CLEAR);

		return IMG_TRUE;
	}

	return IMG_FALSE;
}
#else
IMG_VOID SystemISRHandler(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	SYS_DATA *psSysData = (SYS_DATA *)psDevConfig->hSysData;
	IMG_UINT32 ui32InterruptClear = 0;
	IMG_UINT32 ui32InterruptStatus;

	ui32InterruptStatus = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_STATUS);

	if (ui32InterruptStatus & PDP1_INT_MASK)
	{
		ui32InterruptClear |= (0x1 << PDP1_INT_SHIFT);
	}

	if (ui32InterruptStatus & EXT_INT_MASK)
	{
		ui32InterruptClear = (0x1 << EXT_INT_SHIFT);
	}

	if (ui32InterruptClear)
	{
		OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_CLEAR, ui32InterruptClear);
	}
}
#endif /* defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING) */

#if defined(TC_APOLLO_ES2)

static IMG_VOID SPI_Write(IMG_VOID *pvLinRegBaseAddr,
			  IMG_UINT32 ui32Offset,
			  IMG_UINT32 ui32Value)
{
	OSWriteHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_ADDR_RDNWR, ui32Offset);
	OSWriteHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_WDATA, ui32Value);
	OSWriteHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_GO, TCF_SPI_MST_GO_MASK);
	OSWaitus(1000);
}

static PVRSRV_ERROR SPI_Read(IMG_VOID *pvLinRegBaseAddr,
			     IMG_UINT32 ui32Offset,
			     IMG_UINT32 *pui32Value)
{
	IMG_UINT32 ui32Count = 0;

	OSWriteHWReg32(pvLinRegBaseAddr,
				   TCF_CLK_CTRL_TCF_SPI_MST_ADDR_RDNWR,
				   0x40000 | ui32Offset);
	OSWriteHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_GO, TCF_SPI_MST_GO_MASK);
	OSWaitus(100);

	while (((OSReadHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_STATUS)) != 0x08) &&
			(ui32Count < 10000))
	{
		ui32Count++;
	}

	if (ui32Count < 10000)
	{
		*pui32Value = OSReadHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_RDATA);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "SPI_Read: Time out reading SPI register (0x%x)", ui32Offset));
		return PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE;
	}

	return PVRSRV_OK;
}

static IMG_BOOL IsInterfaceAligned(
		IMG_UINT32 ui32Eyes,
		IMG_UINT32 ui32ClkTaps,
		IMG_UINT32 ui32TrainAck)
{

	IMG_UINT32	ui32MaxEyeStart = ui32Eyes >> 16;
	IMG_UINT32	ui32MinEyeEnd = ui32Eyes & 0xffff;

	IMG_BOOL	bTrainingComplete = (ui32ClkTaps & 0x10000);

	IMG_BOOL	bTrainAckComplete = (ui32TrainAck & 0x100);
	IMG_UINT32	ui32FailedAcks = (ui32TrainAck & 0xf0) >> 4;


	//If either the training or training ack failed, we haven't aligned
	if (!bTrainingComplete || !bTrainAckComplete)
	{
		return IMG_FALSE;
	}

	//If the max eye >= min eye it means the readings are nonsense
	if (ui32MaxEyeStart >= ui32MinEyeEnd)
	{
		return IMG_FALSE;
	}

	//If we failed the ack pattern more than 4 times
	if (ui32FailedAcks > 4)
	{
		return IMG_FALSE;
	}

	//If there is less than 7 taps 
	//(240ps @40ps/tap, this number should be lower for the fpga, since its taps are bigger
	//We should really calculate the "7" based on the interface clock speed.
	if ((ui32MinEyeEnd - ui32MaxEyeStart) < 7)
	{
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

static IMG_UINT32 SAI_Read(void *base, IMG_UINT32 addr)
{
	OSWriteHWReg32(base, 0x300, 0x200 | addr);
	OSWriteHWReg32(base, 0x318, 1);
	return OSReadHWReg32(base, 0x310);
}

static PVRSRV_ERROR ApolloHardReset(SYS_DATA *psSysData)
{
	IMG_UINT32 ui32ClkResetN;
	IMG_UINT32 ui32DUTCtrl;

	IMG_BOOL bAlignmentOK = IMG_FALSE;
	IMG_INT resetAttempts = 0;

	//This is required for SPI reset which is not yet implemented.
	//IMG_UINT32 ui32AuxResetsN;

	//power down
	ui32DUTCtrl = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_DUT_CONTROL_1);
	ui32DUTCtrl &= ~DUT_CTRL_VCC_0V9EN;
	ui32DUTCtrl &= ~DUT_CTRL_VCC_1V8EN;
	ui32DUTCtrl |= DUT_CTRL_VCC_IO_INH;
	ui32DUTCtrl |= DUT_CTRL_VCC_CORE_INH;
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_DUT_CONTROL_1, ui32DUTCtrl);

	OSSleepms(500);

	//set clock speed here, before reset.
#if 0
	//This is if 0 out since the current FPGA builds do not like their core clocks being set (it takes apollo down).
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, 0x1000 | TCF_PLL_PLL_CORE_CLK0, ui32CoreClockSpeed / 1000000);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, 0x1000 | TCF_PLL_PLL_CORE_DRP_GO, 1);
#endif

	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, 0x1000 | TCF_PLL_PLL_MEMIF_CLK0, ui32MemClockSpeed / 1000000);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, 0x1000 | TCF_PLL_PLL_MEM_DRP_GO, 1);

	printk(KERN_ERR "Setting clocks to %uMHz/%uMHz\n", ui32CoreClockSpeed / 1000000, ui32MemClockSpeed / 1000000);

	OSWaitus(400);

	//Put DCM, DUT, DDR, PDP1, and PDP2 into reset
	ui32ClkResetN = (0x1 << GLB_CLKG_EN_SHIFT);
	ui32ClkResetN |= (0x1 << SCB_RESETN_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32ClkResetN);

	OSSleepms(100);

	//Enable the voltage control regulators on DUT
	ui32DUTCtrl = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_DUT_CONTROL_1);
	ui32DUTCtrl |= DUT_CTRL_VCC_0V9EN;
	ui32DUTCtrl |= DUT_CTRL_VCC_1V8EN;
	ui32DUTCtrl &= ~DUT_CTRL_VCC_IO_INH;
	ui32DUTCtrl &= ~DUT_CTRL_VCC_CORE_INH;
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_DUT_CONTROL_1, ui32DUTCtrl);

	OSSleepms(300);


	//Take DCM, DDR, PDP1, and PDP2 out of reset
	ui32ClkResetN |= (0x1 << DDR_RESETN_SHIFT);
	ui32ClkResetN |= (0x1 << DUT_DCM_RESETN_SHIFT);
	ui32ClkResetN |= (0x1 << PDP1_RESETN_SHIFT);
	ui32ClkResetN |= (0x1 << PDP2_RESETN_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32ClkResetN);

	//Set ODT to a specific value that seems to provide the most stable signals.
	SPI_Write(psSysData->pvSystemRegCpuVBase, 0x11, 0x413130);

	//Take DUT out of reset
	ui32ClkResetN |= (0x1 << DUT_RESETN_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32ClkResetN);
	OSSleepms(100);

	//try to enable the core clock PLL
	{
		IMG_UINT32 ui32DUTResets;
		IMG_UINT32 ui32DUTGPIO1;
		IMG_UINT32 ui32PLLStatus = 0;

		/* Un-bypass the PLL on the DUT */
		SPI_Write(psSysData->pvSystemRegCpuVBase, 0x1, 0x0);
		ui32DUTResets = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, 0x320);
		ui32DUTResets |= 0x1;
		OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, 0x320, ui32DUTResets);
		ui32DUTResets &= 0xfffffffe;
		OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, 0x320, ui32DUTResets);
		OSSleepms(1000);

		if (SPI_Read(psSysData->pvSystemRegCpuVBase, 0x2, &ui32PLLStatus) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Unable to read PLL status"));
		}

		if (ui32PLLStatus != 0x1)
		{
			PVR_DPF((PVR_DBG_ERROR, "PLL has failed to lock, status = %x", ui32PLLStatus));
		}
		else
		{
			/* Select DUT PLL as core clock */
			ui32DUTGPIO1 = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, 0x108);
			ui32DUTGPIO1 &= 0xfffffff7;
			OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, 0x108, ui32DUTGPIO1);
		}

		PVR_DPF((PVR_DBG_MESSAGE, "PLL has been set to 6x"));
	}

	while (!bAlignmentOK && resetAttempts < 10)
	{
		IMG_UINT32 ui32Eyes     = 0;
		IMG_UINT32 ui32ClkTaps  = 0;
		IMG_UINT32 ui32TrainAck = 0;

		IMG_INT bank;


		resetAttempts += 1;

		//reset the DUT to allow the SAI to retrain
		ui32ClkResetN &= ~(0x1 << DUT_RESETN_SHIFT);
		OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32ClkResetN);
		OSWaitus(100);
		ui32ClkResetN |= (0x1 << DUT_RESETN_SHIFT);
		OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32ClkResetN);
		OSWaitus(100);

		//Assume aligment passed, if any bank fails on either DUT or FPGA
		//we will set this to false and try again for a max of 10 times.
		bAlignmentOK = IMG_TRUE;

		//for each of the banks
		for (bank = 0; bank < 10; bank++)
		{
			
			//check alignment on the DUT
			IMG_UINT32 ui32BankBase = 0x7000 + (0x1000 * bank);

			SPI_Read(psSysData->pvSystemRegCpuVBase, ui32BankBase + 0x4, &ui32Eyes);
			SPI_Read(psSysData->pvSystemRegCpuVBase, ui32BankBase + 0x3, &ui32ClkTaps);
			SPI_Read(psSysData->pvSystemRegCpuVBase, ui32BankBase + 0x6, &ui32TrainAck);

			if (!IsInterfaceAligned(ui32Eyes, ui32ClkTaps, ui32TrainAck))
			{
				bAlignmentOK = IMG_FALSE;
				break;
			}

			//check alignment on the FPGA
			ui32BankBase = 0xb0 + (0x10 * bank);

			ui32Eyes = SAI_Read(psSysData->pvSystemRegCpuVBase, ui32BankBase + 0x4);
			ui32ClkTaps = SAI_Read(psSysData->pvSystemRegCpuVBase, ui32BankBase + 0x3);
			ui32TrainAck = SAI_Read(psSysData->pvSystemRegCpuVBase, ui32BankBase + 0x6);

			if (!IsInterfaceAligned(ui32Eyes, ui32ClkTaps, ui32TrainAck))
			{
				bAlignmentOK = IMG_FALSE;
				break;
			}
		}
	}

	if (!bAlignmentOK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Unable to intialise the testchip (interface alignment failure), please restart the system."));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	if (resetAttempts > 1)
	{
		PVR_DPF((PVR_DBG_ERROR, "Note: The testchip required more than one reset to find a good interface alingment!"));
		PVR_DPF((PVR_DBG_ERROR, "      This should be harmless, but if you do suspect foul play, please reset the machine."));
		PVR_DPF((PVR_DBG_ERROR, "      If you continue to see this message you may want to report it to IMGWORKS."));
	}

	/* Enable the temperature sensor */
	SPI_Write(psSysData->pvSystemRegCpuVBase, 0xc, 0); //power up
	SPI_Write(psSysData->pvSystemRegCpuVBase, 0xc, 2); //reset
	SPI_Write(psSysData->pvSystemRegCpuVBase, 0xc, 6); //init & run

	{
		IMG_UINT32 ui32RevID = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, 0x10);

		IMG_UINT32 ui32BuildInc = (ui32RevID >> 12) & 0xff;

		if (ui32BuildInc)
		{
			PVR_DPF((PVR_DBG_ERROR, "BE WARNED: You are not running a tagged release of the FPGA image!"));
			PVR_DPF((PVR_DBG_ERROR, "Owner: 0x%01x, Inc: 0x%02x", (ui32RevID >> 20) & 0xf, ui32BuildInc));
		}

		PVR_DPF((PVR_DBG_ERROR, "FPGA Release: %u.%02u", ui32RevID >> 8 & 0xf, ui32RevID & 0xff));

	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR PCIInitDev(SYS_DATA *psSysData)
{
	PVRSRV_DEVICE_CONFIG *psDevice = &gsSysConfig.pasDevices[0];
	IMG_CPU_PHYADDR	sApolloRegCpuPBase;
	IMG_UINT32 uiApolloRegSize;
	PVRSRV_ERROR eError;

#if defined(LDM_PCI) || defined(SUPPORT_DRM)
	/* Use the pci_dev structure pointer from module.c */
	PVR_ASSERT(gpsPVRLDMDev != IMG_NULL);

	psSysData->hRGXPCI = OSPCISetDev((IMG_VOID *)gpsPVRLDMDev, HOST_PCI_INIT_FLAGS);
#else
	psSysData->hRGXPCI = OSPCIAcquireDev(SYS_RGX_DEV_VENDOR_ID, SYS_RGX_DEV_DEVICE_ID, HOST_PCI_INIT_FLAGS);
#endif /* defined(LDM_PCI) || defined(SUPPORT_DRM) */
	if (!psSysData->hRGXPCI)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to acquire PCI device", __FUNCTION__));
		return PVRSRV_ERROR_PCI_DEVICE_NOT_FOUND;
	}

	/* Get Apollo register information */
	sApolloRegCpuPBase.uiAddr	= OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM);
	uiApolloRegSize			= OSPCIAddrRangeLen(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM);

	/* Check the address range is large enough. */
	if (uiApolloRegSize < SYS_APOLLO_REG_REGION_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Apollo register region isn't big enough (was 0x%08X, required 0x%08X)",
			 __FUNCTION__, uiApolloRegSize, SYS_APOLLO_REG_REGION_SIZE));

		eError = PVRSRV_ERROR_PCI_REGION_TOO_SMALL;
		goto ErrorPCIReleaseDevice;
	}

	/* The Apollo register range contains several register regions. Request only the system control register region */
	eError = OSPCIRequestAddrRegion(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM, SYS_APOLLO_REG_SYS_OFFSET, SYS_APOLLO_REG_SYS_SIZE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to request the system register region", __FUNCTION__));

		goto ErrorPCIReleaseDevice;
	}
	psSysData->sSystemRegCpuPBase.uiAddr	= sApolloRegCpuPBase.uiAddr + SYS_APOLLO_REG_SYS_OFFSET;
	psSysData->uiSystemRegSize		= SYS_APOLLO_REG_REGION_SIZE;

	/* Setup Rogue register information */
	psDevice->sRegsCpuPBase.uiAddr	= OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);
	psDevice->ui32RegsSize		= OSPCIAddrRangeLen(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);

	/* Save data for this device */
	psDevice->hSysData = (IMG_HANDLE)psSysData;

	/* Check the address range is large enough. */
	if (psDevice->ui32RegsSize < SYS_RGX_REG_REGION_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Rogue register region isn't big enough (was 0x%08x, required 0x%08x)",
			 __FUNCTION__, psDevice->ui32RegsSize, SYS_RGX_REG_REGION_SIZE));

		eError = PVRSRV_ERROR_PCI_REGION_TOO_SMALL;
		goto ErrorSystemRegReleaseAddrRegion;
	}

	/* Reserve the address range */
	eError = OSPCIRequestAddrRange(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Rogue register memory region not available", __FUNCTION__));

		goto ErrorSystemRegReleaseAddrRegion;
	}

	/* Map in system registers so we can:
	   - Configure the memory mode (LMA, UMA or LMA/UMA hybrid)
	   - Hard reset Apollo
	   - Clear interrupts
	*/
	psSysData->pvSystemRegCpuVBase = OSMapPhysToLin(psSysData->sSystemRegCpuPBase, psSysData->uiSystemRegSize, 0);
	if (psSysData->pvSystemRegCpuVBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to map system registers", __FUNCTION__));

		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorRGXRegReleaseAddrRange;
	}

	eError = ApolloFlasherSetup(&psSysData->hFlashData,
				    ApolloFlashInit,
				    ApolloFlashWrite,
				    ApolloFlashGetStatus,
				    (IMG_VOID *)psSysData);

	if (eError != PVRSRV_OK)
	{
		if (eError != PVRSRV_ERROR_NOT_IMPLEMENTED)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Apollo flash setup failed. You will not be able to flash Apollo.", __FUNCTION__));
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Support for flashing Apollo has not been implemented.", __FUNCTION__));
		}
		psSysData->hFlashData = NULL;
	}


	eError = ApolloHardReset(psSysData);
	if (eError != PVRSRV_OK)
	{
		goto ErrorCleanupFlashingModule;
	}

	/* Make the core clock speed value available to the driver (matching the one set by ApolloHardReset()) */
	((RGX_DATA *)psDevice->hDevData)->psRGXTimingInfo->ui32CoreClockSpeed = ui32CoreClockSpeed * 6;


	eError = InitMemory(&gsSysConfig, psSysData);
	if (eError != PVRSRV_OK)
	{
		goto ErrorCleanupFlashingModule;
	}

	/* Override the system name if we can get the system info string */
	psSysData->pszSystemInfoString = GetSystemInfoString(psSysData);
	if (psSysData->pszSystemInfoString)
	{
		gsSysConfig.pszSystemName = psSysData->pszSystemInfoString;
	}

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
	eError = OSPCIIRQ(psSysData->hRGXPCI, &psSysData->ui32IRQ);
#else
	eError = OSPCIIRQ(psSysData->hRGXPCI, &psDevice->ui32IRQ);
#endif
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't get IRQ", __FUNCTION__));

		goto ErrorDeInitMemory;
	}

#if !defined(SUPPORT_DRM)
#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
	/* Register our handler */
	eError = OSInstallSystemLISR(&psSysData->hLISR,
				     psSysData->ui32IRQ,
#if !defined(UNDER_CE)
				     IMG_NULL,
#else
				     (PVR_IRQ_PRIV_DATA *)&psSysData->sSystemRegCpuPBase,
#endif
				     SystemISRHandler,
				     psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to install the system device interrupt handler", __FUNCTION__));

		goto ErrorDeInitMemory;
	}
#else
	psDevice->pfnInterruptHandled = SystemISRHandler;

	EnableInterrupt(psSysData, (0x1 << EXT_INT_SHIFT) | (0x1 << PDP1_INT_SHIFT));
#endif
#endif /* !defined(SUPPORT_DRM) */

	return PVRSRV_OK;

ErrorDeInitMemory:
	DeInitMemory(&gsSysConfig, psSysData);

ErrorCleanupFlashingModule:
	if (psSysData->hFlashData != NULL)
	{
		ApolloFlasherCleanup(psSysData->hFlashData);
	}

	OSUnMapPhysToLin(psSysData->pvSystemRegCpuVBase, psSysData->uiSystemRegSize, 0);
	psSysData->pvSystemRegCpuVBase = NULL;

ErrorRGXRegReleaseAddrRange:
	OSPCIReleaseAddrRange(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);
	psDevice->sRegsCpuPBase.uiAddr	= 0;
	psDevice->ui32RegsSize		= 0;

ErrorSystemRegReleaseAddrRegion:
	OSPCIReleaseAddrRegion(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM, SYS_APOLLO_REG_SYS_OFFSET, SYS_APOLLO_REG_SYS_SIZE);
	psSysData->sSystemRegCpuPBase.uiAddr	= 0;
	psSysData->uiSystemRegSize		= 0;

ErrorPCIReleaseDevice:
	OSPCIReleaseDev(psSysData->hRGXPCI);
	psSysData->hRGXPCI = IMG_NULL;

	return eError;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_SYSTEM_CONFIG *psSysConfig, DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	PVRSRV_ERROR	eError;
	SYS_DATA		*psSysData = psSysConfig->pasDevices[0].hSysData;
	IMG_UINT32		ui32RegVal;

	PVR_UNREFERENCED_PARAMETER(psSysConfig);

	PVR_DUMPDEBUG_LOG(("------[ rgx_tc system debug ]------"));

	/* Read the temperature */
	eError = SPI_Read(psSysData->pvSystemRegCpuVBase, TCF_TEMP_SENSOR_SPI_OFFSET, &ui32RegVal);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SysDebugInfo: SPI_Read failed for register 0x%x (0x%x)", TCF_TEMP_SENSOR_SPI_OFFSET, eError));
		goto SysDebugInfo_exit;
	}

	PVR_DUMPDEBUG_LOG(("Chip temperature: %d degrees C", TCF_TEMP_SENSOR_TO_C(ui32RegVal)));

	eError = SPI_Read(psSysData->pvSystemRegCpuVBase, 0x2, &ui32RegVal);
	PVR_DUMPDEBUG_LOG(("PLL status: %x", ui32RegVal));

SysDebugInfo_exit:
	return eError;
}

#else

static IMG_VOID SPI_Write(IMG_VOID *pvLinRegBaseAddr,
			  IMG_UINT32 ui32Offset,
			  IMG_UINT32 ui32Value)
{
	OSWriteHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_ADDR_RDNWR, ui32Offset);
	OSWriteHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_WDATA, ui32Value);
	OSWriteHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_GO, TCF_SPI_MST_GO_MASK);
	OSWaitus(1000);
}

static PVRSRV_ERROR SPI_Read(IMG_VOID *pvLinRegBaseAddr,
			     IMG_UINT32 ui32Offset,
			     IMG_UINT32 *pui32Value)
{
	IMG_UINT32 ui32Count = 0;

	OSWriteHWReg32(pvLinRegBaseAddr,
				   TCF_CLK_CTRL_TCF_SPI_MST_ADDR_RDNWR,
				   TCF_SPI_MST_RDNWR_MASK | ui32Offset);
	OSWriteHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_GO, TCF_SPI_MST_GO_MASK);
	OSWaitus(1000);

	while (((OSReadHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_STATUS)) != 0x08) &&
			(ui32Count < 10000))
	{
		ui32Count++;
	}

	if (ui32Count < 10000)
	{
		*pui32Value = OSReadHWReg32(pvLinRegBaseAddr, TCF_CLK_CTRL_TCF_SPI_MST_RDATA);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "SPI_Read: Time out reading SPI register (0x%x)", ui32Offset));
		return PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE;
	}

	return PVRSRV_OK;
}

#define pol(base,reg,val,msk) \
	do { \
		int polnum; \
		for (polnum = 0; polnum < 500; polnum++) \
		{ \
			if ((OSReadHWReg32(base, reg) & msk) == val) \
			{ \
				break; \
			} \
			OSSleepms(1); \
		} \
		if (polnum == 500) \
		{ \
			PVR_DPF((PVR_DBG_WARNING, "Pol failed for register: 0x%08X", (unsigned int)reg)); \
		} \
	} while (0)

#define polrgx(reg,val,msk) pol(pvRegsBaseKM, reg, val, msk)

static IMG_VOID RGXInitBistery(IMG_CPU_PHYADDR sRegisters, IMG_UINT32 ui32Size)
{
	IMG_VOID *pvRegsBaseKM = OSMapPhysToLin(sRegisters, ui32Size, 0);
	IMG_UINT instance;
	IMG_UINT i;

	/* Force clocks on */
	OSWriteHWReg32(pvRegsBaseKM, 0, 0x55555555);
	OSWriteHWReg32(pvRegsBaseKM, 4, 0x55555555);

	polrgx(0xa18, 0x05000000, 0x05000000);
	OSWriteHWReg32(pvRegsBaseKM, 0xa10, 0x048000b0);
	OSWriteHWReg32(pvRegsBaseKM, 0xa08, 0x55111111);
	polrgx(0xa18, 0x05000000, 0x05000000);

	/* Clear PDS CSRM and USRM to prevent ERRORs at end of test */
	OSWriteHWReg32(pvRegsBaseKM, 0x630, 0x1);
	OSWriteHWReg32(pvRegsBaseKM, 0x648, 0x1);
	OSWriteHWReg32(pvRegsBaseKM, 0x608, 0x1);

	/* Run BIST for SLC (43) */
	/* Reset BIST */
	OSWriteHWReg32(pvRegsBaseKM, 0x7000, 0x8);
	OSWaitus(100);

	/* Clear BIST controller */
	OSWriteHWReg32(pvRegsBaseKM, 0x7000, 0x10);
	OSWriteHWReg32(pvRegsBaseKM, 0x7000, 0);
	OSWaitus(100);

	for (i = 0; i < 3; i++)
	{
		IMG_UINT32 ui32Pol = i == 2 ? 0x10000 : 0x20000;

		/* Start BIST */
		OSWriteHWReg32(pvRegsBaseKM, 0x7000, 0x4);

		OSWaitus(100);

		/* Wait for pause */
		polrgx(0x7000, ui32Pol, ui32Pol);
	}
	OSWaitus(100);

	/* Check results for 43 RAMs */
	polrgx(0x7010, 0xffffffff, 0xffffffff);
	polrgx(0x7014, 0x7, 0x7);

	OSWriteHWReg32(pvRegsBaseKM, 0x7000, 8);
	OSWriteHWReg32(pvRegsBaseKM, 0x7008, 0);
	OSWriteHWReg32(pvRegsBaseKM, 0x7000, 6);
	polrgx(0x7000, 0x00010000, 0x00010000);
	OSWaitus(100);

	polrgx(0x75B0, 0, ~0U);
	polrgx(0x75B4, 0, ~0U);
	polrgx(0x75B8, 0, ~0U);
	polrgx(0x75BC, 0, ~0U);
	polrgx(0x75C0, 0, ~0U);
	polrgx(0x75C4, 0, ~0U);
	polrgx(0x75C8, 0, ~0U);
	polrgx(0x75CC, 0, ~0U);

	/* Sidekick */
	OSWriteHWReg32(pvRegsBaseKM, 0x7040, 8);
	OSWaitus(100);

	OSWriteHWReg32(pvRegsBaseKM, 0x7040, 0x10);
	//OSWriteHWReg32(pvRegsBaseKM, 0x7000, 0);
	OSWaitus(100);

	for (i = 0; i < 3; i++)
	{
		IMG_UINT32 ui32Pol = i == 2 ? 0x10000 : 0x20000;

		OSWriteHWReg32(pvRegsBaseKM, 0x7040, 4);
		OSWaitus(100);
		polrgx(0x7040, ui32Pol, ui32Pol);
	}

	OSWaitus(100);
	polrgx(0x7050, 0xffffffff, 0xffffffff);
	polrgx(0x7054, 0xffffffff, 0xffffffff);
	polrgx(0x7058, 0x1, 0x1);

	/* USC */
	for (instance = 0; instance < 4; instance++)
	{
		OSWriteHWReg32(pvRegsBaseKM, 0x8010, instance);

		OSWriteHWReg32(pvRegsBaseKM, 0x7088, 8);
		OSWaitus(100);

		OSWriteHWReg32(pvRegsBaseKM, 0x7088, 0x10);
		OSWaitus(100);

		for (i = 0; i < 3; i++)
		{
			IMG_UINT32 ui32Pol = i == 2 ? 0x10000 : 0x20000;

			OSWriteHWReg32(pvRegsBaseKM, 0x7088, 4);
			OSWaitus(100);
			polrgx(0x7088, ui32Pol, ui32Pol);
		}

		OSWaitus(100);
		polrgx(0x7098, 0xffffffff, 0xffffffff);
		polrgx(0x709c, 0xffffffff, 0xffffffff);
		polrgx(0x70a0, 0x3f, 0x3f);
	}

	/* tpumcul0 DustA and DustB */
	for (instance = 0; instance < 2; instance++)
	{
		OSWriteHWReg32(pvRegsBaseKM, 0x8018, instance);

		OSWriteHWReg32(pvRegsBaseKM, 0x7380, 8);
		OSWaitus(100);

		OSWriteHWReg32(pvRegsBaseKM, 0x7380, 0x10);
		OSWaitus(100);

		for (i = 0; i < 3; i++)
		{
			IMG_UINT32 ui32Pol = i == 2 ? 0x10000 : 0x20000;

			OSWriteHWReg32(pvRegsBaseKM, 0x7380, 4);
			OSWaitus(100);
			polrgx(0x7380, ui32Pol, ui32Pol);
		}

		OSWaitus(100);
		polrgx(0x7390, 0x1fff, 0x1fff);
	}

	/* TA */
	OSWriteHWReg32(pvRegsBaseKM, 0x7500, 8);
	OSWaitus(100);

	OSWriteHWReg32(pvRegsBaseKM, 0x7500, 0x10);
	OSWaitus(100);

	for (i = 0; i < 3; i++)
	{
		IMG_UINT32 ui32Pol = i == 2 ? 0x10000 : 0x20000;

		OSWriteHWReg32(pvRegsBaseKM, 0x7500, 4);
		OSWaitus(100);
		polrgx(0x7500, ui32Pol, ui32Pol);
	}

	OSWaitus(100);
	polrgx(0x7510, 0x1fffffff, 0x1fffffff);

	/* Rasterisation */
	OSWriteHWReg32(pvRegsBaseKM, 0x7540, 8);
	OSWaitus(100);

	OSWriteHWReg32(pvRegsBaseKM, 0x7540, 0x10);
	OSWaitus(100);

	for (i = 0; i < 3; i++)
	{
		IMG_UINT32 ui32Pol = i == 2 ? 0x10000 : 0x20000;

		OSWriteHWReg32(pvRegsBaseKM, 0x7540, 4);
		OSWaitus(100);
		polrgx(0x7540, ui32Pol, ui32Pol);
	}

	OSWaitus(100);
	polrgx(0x7550, 0xffffffff, 0xffffffff);
	polrgx(0x7554, 0xffffffff, 0xffffffff);
	polrgx(0x7558, 0xf, 0xf);

	/* hub_bifpmache */
	OSWriteHWReg32(pvRegsBaseKM, 0x7588, 8);
	OSWaitus(100);

	OSWriteHWReg32(pvRegsBaseKM, 0x7588, 0x10);
	OSWaitus(100);

	for (i = 0; i < 3; i++)
	{
		IMG_UINT32 ui32Pol = i == 2 ? 0x10000 : 0x20000;

		OSWriteHWReg32(pvRegsBaseKM, 0x7588, 4);
		OSWaitus(100);
		polrgx(0x7588, ui32Pol, ui32Pol);
	}

	OSWaitus(100);
	polrgx(0x7598, 0xffffffff, 0xffffffff);
	polrgx(0x759c, 0xffffffff, 0xffffffff);
	polrgx(0x75a0, 0x1111111f, 0x1111111f);

	OSUnMapPhysToLin(pvRegsBaseKM, ui32Size, 0);
}

static IMG_VOID ApolloHardReset(SYS_DATA *psSysData)
{
	IMG_UINT32 ui32Value;

	ui32Value = (0x1 << GLB_CLKG_EN_SHIFT);
	ui32Value |= (0x1 << SCB_RESETN_SHIFT);
	ui32Value |= (0x1 << PDP2_RESETN_SHIFT);
	ui32Value |= (0x1 << PDP1_RESETN_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32Value);

	ui32Value |= (0x1 << DDR_RESETN_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32Value);

	ui32Value |= (0x1 << DUT_RESETN_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32Value);

	ui32Value |= (0x1 << DUT_DCM_RESETN_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_CLK_AND_RST_CTRL, ui32Value);

	OSSleepms(4);
	pol(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_DCM_LOCK_STATUS, 0x7, DCM_LOCK_STATUS_MASK);
}

#undef pol
#undef polrgx

static PVRSRV_ERROR SetClocks(SYS_DATA *psSysData, IMG_UINT32 ui32CoreClock, IMG_UINT32 ui32MemClock)
{
	IMG_CPU_PHYADDR	sPLLRegCpuPBase;
	IMG_VOID *pvPLLRegCpuVBase;
	IMG_UINT32 ui32Value;
	PVRSRV_ERROR eError;

	/* Reserve the PLL register region and map the registers in */
	eError = OSPCIRequestAddrRegion(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM, SYS_APOLLO_REG_PLL_OFFSET, SYS_APOLLO_REG_PLL_SIZE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to request the PLL register region (%d)", __FUNCTION__, eError));

		return eError;
	}
	sPLLRegCpuPBase.uiAddr = OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM) + SYS_APOLLO_REG_PLL_OFFSET;

	pvPLLRegCpuVBase = OSMapPhysToLin(sPLLRegCpuPBase, SYS_APOLLO_REG_PLL_SIZE, 0);
	if (pvPLLRegCpuVBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to map PLL registers", __FUNCTION__));

		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Modify the core clock */
	OSWriteHWReg32(pvPLLRegCpuVBase, TCF_PLL_PLL_CORE_CLK0, (ui32CoreClock / 1000000));

	ui32Value = 0x1 << PLL_CORE_DRP_GO_SHIFT;
	OSWriteHWReg32(pvPLLRegCpuVBase, TCF_PLL_PLL_CORE_DRP_GO, ui32Value);

	OSSleepms(600);

	/* Modify the memory clock */
	OSWriteHWReg32(pvPLLRegCpuVBase, TCF_PLL_PLL_MEMIF_CLK0, (ui32MemClock / 1000000));

	ui32Value = 0x1 << PLL_MEM_DRP_GO_SHIFT;
	OSWriteHWReg32(pvPLLRegCpuVBase, TCF_PLL_PLL_MEM_DRP_GO, ui32Value);

	OSSleepms(600);

	/* Unmap and release the PLL registers since we no longer need access to them */
	OSUnMapPhysToLin(pvPLLRegCpuVBase, SYS_APOLLO_REG_PLL_SIZE, 0);
	OSPCIReleaseAddrRegion(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM, SYS_APOLLO_REG_PLL_OFFSET, SYS_APOLLO_REG_PLL_SIZE);

	return PVRSRV_OK;
}

static PVRSRV_ERROR PCIInitDev(SYS_DATA *psSysData)
{
	PVRSRV_DEVICE_CONFIG *psDevice = &gsSysConfig.pasDevices[0];
	IMG_CPU_PHYADDR	sApolloRegCpuPBase;
	IMG_UINT32 uiApolloRegSize;
	IMG_UINT32 ui32Value;
	PVRSRV_ERROR eError;

#if defined(LDM_PCI) || defined(SUPPORT_DRM)
	/* Use the pci_dev structure pointer from module.c */
	PVR_ASSERT(gpsPVRLDMDev != IMG_NULL);

	psSysData->hRGXPCI = OSPCISetDev((IMG_VOID *)gpsPVRLDMDev, HOST_PCI_INIT_FLAGS);
#else
	psSysData->hRGXPCI = OSPCIAcquireDev(SYS_RGX_DEV_VENDOR_ID, SYS_RGX_DEV_DEVICE_ID, HOST_PCI_INIT_FLAGS);
#endif /* defined(LDM_PCI) || defined(SUPPORT_DRM) */
	if (!psSysData->hRGXPCI)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to acquire PCI device", __FUNCTION__));
		return PVRSRV_ERROR_PCI_DEVICE_NOT_FOUND;
	}

	/* Get Apollo register information */
	sApolloRegCpuPBase.uiAddr	= OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM);
	uiApolloRegSize			= OSPCIAddrRangeLen(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM);

	/* Check the address range is large enough. */
	if (uiApolloRegSize < SYS_APOLLO_REG_REGION_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Apollo register region isn't big enough (was 0x%08X, required 0x%08X)",
			 __FUNCTION__, uiApolloRegSize, SYS_APOLLO_REG_REGION_SIZE));

		eError = PVRSRV_ERROR_PCI_REGION_TOO_SMALL;
		goto ErrorPCIReleaseDevice;
	}

	/* The Apollo register range contains several register regions. Request only the system control register region */
	eError = OSPCIRequestAddrRegion(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM, SYS_APOLLO_REG_SYS_OFFSET, SYS_APOLLO_REG_SYS_SIZE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to request the system register region", __FUNCTION__));

		goto ErrorPCIReleaseDevice;
	}
	psSysData->sSystemRegCpuPBase.uiAddr	= sApolloRegCpuPBase.uiAddr + SYS_APOLLO_REG_SYS_OFFSET;
	psSysData->uiSystemRegSize		= SYS_APOLLO_REG_REGION_SIZE;

	/* Setup Rogue register information */
	psDevice->sRegsCpuPBase.uiAddr	= OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);
	psDevice->ui32RegsSize		= OSPCIAddrRangeLen(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);

	/* Save data for this device */
	psDevice->hSysData = (IMG_HANDLE)psSysData;

	/* Check the address range is large enough. */
	if (psDevice->ui32RegsSize < SYS_RGX_REG_REGION_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Rogue register region isn't big enough (was 0x%08x, required 0x%08x)",
			 __FUNCTION__, psDevice->ui32RegsSize, SYS_RGX_REG_REGION_SIZE));

		eError = PVRSRV_ERROR_PCI_REGION_TOO_SMALL;
		goto ErrorSystemRegReleaseAddrRegion;
	}

	/* Reserve the address range */
	eError = OSPCIRequestAddrRange(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Rogue register memory region not available", __FUNCTION__));

		goto ErrorSystemRegReleaseAddrRegion;
	}

	/* Map in system registers so we can:
	   - Configure the memory mode (LMA, UMA or LMA/UMA hybrid)
	   - Hard reset Apollo
	   - Run BIST
	   - Clear interrupts
	*/
	psSysData->pvSystemRegCpuVBase = OSMapPhysToLin(psSysData->sSystemRegCpuPBase, psSysData->uiSystemRegSize, 0);
	if (psSysData->pvSystemRegCpuVBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to map system registers", __FUNCTION__));

		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorRGXRegReleaseAddrRange;
	}

	eError = ApolloFlasherSetup(&psSysData->hFlashData,
				    ApolloFlashInit,
				    ApolloFlashWrite,
				    ApolloFlashGetStatus,
				    (IMG_VOID *)psSysData);

	if (eError != PVRSRV_OK)
	{
		if (eError != PVRSRV_ERROR_NOT_IMPLEMENTED)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Apollo flash setup failed. You will not be able to flash Apollo.", __FUNCTION__));
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Support for flashing Apollo has not been implemented.", __FUNCTION__));
		}
		psSysData->hFlashData = NULL;
	}

	ApolloHardReset(psSysData);
	RGXInitBistery(psDevice->sRegsCpuPBase, psDevice->ui32RegsSize);
	ApolloHardReset(psSysData);

	eError = InitMemory(&gsSysConfig, psSysData);
	if (eError != PVRSRV_OK)
	{
		goto ErrorCleanupFlashingModule;
	}

	if (SetClocks(psSysData, ui32CoreClockSpeed, ui32MemClockSpeed) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the core and memory clocks", __FUNCTION__));
	}

	/* Enable the rogue PLL (defaults to 3x), giving a Rogue clock of 3 x RGX_TC_CORE_CLOCK_SPEED */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_DUT_CONTROL_1);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_DUT_CONTROL_1, ui32Value & 0xFFFFFFFB);

	((RGX_DATA *)psDevice->hDevData)->psRGXTimingInfo->ui32CoreClockSpeed = ui32CoreClockSpeed * 3;

	OSSleepms(600);

	/* Enable the temperature sensor */
	SPI_Write(psSysData->pvSystemRegCpuVBase, 0x3, 0x46);


	/* Override the system name if we can get the system info string */
	psSysData->pszSystemInfoString = GetSystemInfoString(psSysData);
	if (psSysData->pszSystemInfoString)
	{
		gsSysConfig.pszSystemName = psSysData->pszSystemInfoString;
	}

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
	eError = OSPCIIRQ(psSysData->hRGXPCI, &psSysData->ui32IRQ);
#else
	eError = OSPCIIRQ(psSysData->hRGXPCI, &psDevice->ui32IRQ);
#endif
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't get IRQ", __FUNCTION__));

		goto ErrorDeInitMemory;
	}

#if !defined(SUPPORT_DRM)
#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
	/* Register our handler */
	eError = OSInstallSystemLISR(&psSysData->hLISR,
				     psSysData->ui32IRQ,
#if !defined(UNDER_CE)
				     IMG_NULL,
#else
				     (PVR_IRQ_PRIV_DATA *)&psSysData->sSystemRegCpuPBase,
#endif
				     SystemISRHandler,
				     psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to install the system device interrupt handler", __FUNCTION__));

		goto ErrorDeInitMemory;
	}
#else
	psDevice->pfnInterruptHandled = SystemISRHandler;

	EnableInterrupt(psSysData, (0x1 << EXT_INT_SHIFT) | (0x1 << PDP1_INT_SHIFT));
#endif
#endif /* !defined(SUPPORT_DRM) */

	return PVRSRV_OK;

ErrorDeInitMemory:
	DeInitMemory(&gsSysConfig, psSysData);

ErrorCleanupFlashingModule:
	if (psSysData->hFlashData != NULL)
	{
		ApolloFlasherCleanup(psSysData->hFlashData);
	}

	OSUnMapPhysToLin(psSysData->pvSystemRegCpuVBase, psSysData->uiSystemRegSize, 0);
	psSysData->pvSystemRegCpuVBase = NULL;

ErrorRGXRegReleaseAddrRange:
	OSPCIReleaseAddrRange(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);
	psDevice->sRegsCpuPBase.uiAddr	= 0;
	psDevice->ui32RegsSize		= 0;

ErrorSystemRegReleaseAddrRegion:
	OSPCIReleaseAddrRegion(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM, SYS_APOLLO_REG_SYS_OFFSET, SYS_APOLLO_REG_SYS_SIZE);
	psSysData->sSystemRegCpuPBase.uiAddr	= 0;
	psSysData->uiSystemRegSize		= 0;

ErrorPCIReleaseDevice:
	OSPCIReleaseDev(psSysData->hRGXPCI);
	psSysData->hRGXPCI = IMG_NULL;

	return eError;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_SYSTEM_CONFIG *psSysConfig, DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	PVRSRV_ERROR	eError;
	SYS_DATA		*psSysData = psSysConfig->pasDevices[0].hSysData;
	IMG_UINT32		ui32RegOffset;
	IMG_UINT32		ui32RegVal;

	PVR_DUMPDEBUG_LOG(("------[ rgx_tc system debug ]------"));

	/* Read the temperature */
	ui32RegOffset = 0x5;
	eError = SPI_Read(psSysData->pvSystemRegCpuVBase, ui32RegOffset, &ui32RegVal);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SysDebugInfo: SPI_Read failed for register 0x%x (0x%x)", ui32RegOffset, eError));
		goto SysDebugInfo_exit;
	}

	PVR_DUMPDEBUG_LOG(("Chip temperature: %d degrees C", (ui32RegVal * 233 / 4096) - 66));

SysDebugInfo_exit:
	return eError;
}

#endif

#if (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL) || (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID) || (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED)
static PVRSRV_ERROR AcquireLocalMemory(SYS_DATA *psSysData, IMG_CPU_PHYADDR *psMemCpuPAddr, IMG_SIZE_T *puiMemSize)
{
	IMG_UINT16 uiDevID;
	IMG_UINT32 uiMemSize;
	IMG_UINT32 uiMemLimit;
	IMG_UINT32 ui32Value;
	IMG_UINT32 ui32PCIVersion;
	PVRSRV_ERROR eError;
	IMG_CPU_PHYADDR	sHostFPGARegCpuPBase;
	IMG_VOID *pvHostFPGARegCpuVBase;

	OSPCIDevID(psSysData->hRGXPCI, &uiDevID);
	if (uiDevID != SYS_RGX_DEV_DEVICE_ID)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Unexpected device ID 0x%X", __FUNCTION__, uiDevID));

		return PVRSRV_ERROR_PCI_DEVICE_NOT_FOUND;
	}

	/* To get some of the version information we need to read from a register that we don't normally have 
	   mapped. Map it temporarily (without trying to reserve it) to get the information we need. */
	sHostFPGARegCpuPBase.uiAddr	= OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM) + 0x40F0;
	pvHostFPGARegCpuVBase		= OSMapPhysToLin(sHostFPGARegCpuPBase, 0x04, 0);

	ui32Value = OSReadHWReg32(pvHostFPGARegCpuVBase, 0);

	/* Unmap the register now that we no longer need it */
	OSUnMapPhysToLin(pvHostFPGARegCpuVBase, 0x04, 0);

	ui32PCIVersion = HEXIDECIMAL_TO_DECIMAL((ui32Value & 0x00FF0000) >> 16);

	if (ui32PCIVersion < 18)
	{
		PVR_DPF((PVR_DBG_WARNING,"\n*********************************************************************************"));
		PVR_DPF((PVR_DBG_WARNING, "%s: You have an outdated test chip fpga image.", __FUNCTION__));
		PVR_DPF((PVR_DBG_WARNING, "Restricting available graphics memory to 512mb as a workaround."));
		PVR_DPF((PVR_DBG_WARNING, "This restriction may cause test failures for those tests"));
		PVR_DPF((PVR_DBG_WARNING, "that require a large amount of graphics memory."));
		PVR_DPF((PVR_DBG_WARNING, "Please speak to your customer support representative about"));
		PVR_DPF((PVR_DBG_WARNING, "upgrading your test chip fpga image."));
		PVR_DPF((PVR_DBG_WARNING, "\n********************************************************************************"));

		/* limit to 512mb */
		uiMemLimit = 512 * 1024 * 1024;

		uiMemSize = OSPCIAddrRangeLen(psSysData->hRGXPCI, SYS_DEV_MEM_PCI_BASENUM);
	}
	else
	{
		uiMemLimit = SYS_DEV_MEM_REGION_SIZE;

		uiMemSize = OSPCIAddrRangeLen(psSysData->hRGXPCI, SYS_DEV_MEM_PCI_BASENUM) - SYS_DEV_MEM_BROKEN_BYTES;
	}

	if (uiMemLimit > uiMemSize)
	{
		PVR_DPF((PVR_DBG_WARNING,
			 "%s: Device memory region smaller than requested (got 0x%08X, requested 0x%08X)", 
			 __FUNCTION__, uiMemSize, uiMemLimit));
	}
	else if (uiMemLimit < uiMemSize)
	{
		PVR_DPF((PVR_DBG_WARNING,
			 "%s: Limiting device memory region size to 0x%08X from 0x%08X", 
			 __FUNCTION__, uiMemLimit, uiMemSize));

		uiMemSize = uiMemLimit;
	}

	/* Reserve the address region */
	eError = OSPCIRequestAddrRegion(psSysData->hRGXPCI, SYS_DEV_MEM_PCI_BASENUM, 0, uiMemSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Device memory region not available", __FUNCTION__));

		return eError;
	}

	/* Clear any BIOS-configured MTRRs */
	eError = OSPCIClearResourceMTRRs(psSysData->hRGXPCI, SYS_DEV_MEM_PCI_BASENUM);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Failed to clear BIOS MTRRs", __FUNCTION__));
		/* Soft-fail, the driver can limp along. */
	}

	psMemCpuPAddr->uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_DEV_MEM_PCI_BASENUM));
	*puiMemSize = (IMG_SIZE_T)uiMemSize;

	return PVRSRV_OK;
}

static INLINE void ReleaseLocalMemory(SYS_DATA *psSysData, IMG_CPU_PHYADDR *psMemCpuPAddr, IMG_SIZE_T uiMemSize)
{
	IMG_CPU_PHYADDR sMemCpuPBaseAddr;
	IMG_UINT32 uiOffset;

	sMemCpuPBaseAddr.uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_DEV_MEM_PCI_BASENUM));

	PVR_ASSERT(psMemCpuPAddr->uiAddr >= sMemCpuPBaseAddr.uiAddr);

	uiOffset = (IMG_UINT32)(IMG_UINTPTR_T)(psMemCpuPAddr->uiAddr - sMemCpuPBaseAddr.uiAddr);

	OSPCIReleaseAddrRegion(psSysData->hRGXPCI, SYS_DEV_MEM_PCI_BASENUM, uiOffset, (IMG_UINT32)uiMemSize);
}
#endif /* (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL) || (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID) || (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED) */

#if (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL)
static PVRSRV_ERROR InitLocalMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
	IMG_UINT32 ui32HeapNum = 0;
	IMG_UINT32 ui32Value;
	PVRSRV_ERROR eError;

	eError = AcquireLocalMemory(psSysData, &psSysData->sLocalMemCpuPBase, &psSysData->uiLocalMemSize);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/* Setup the Rogue heap */
	psSysConfig->pasPhysHeaps[ui32HeapNum].sStartAddr.uiAddr =
		psSysData->sLocalMemCpuPBase.uiAddr;
	psSysConfig->pasPhysHeaps[ui32HeapNum].uiSize =
		psSysData->uiLocalMemSize
#if defined(SUPPORT_DISPLAY_CLASS) || defined(SUPPORT_DRM_DC_MODULE)
		- RGX_TC_RESERVE_DC_MEM_SIZE
#endif
#if defined(SUPPORT_ION)
		- RGX_TC_RESERVE_ION_MEM_SIZE
#endif
		;
	ui32HeapNum++;

#if defined(SUPPORT_DISPLAY_CLASS) || defined(SUPPORT_DRM_DC_MODULE)
	/* Setup the DC heap */
	psSysConfig->pasPhysHeaps[ui32HeapNum].sStartAddr.uiAddr =
		psSysConfig->pasPhysHeaps[ui32HeapNum - 1].sStartAddr.uiAddr +
		psSysConfig->pasPhysHeaps[ui32HeapNum - 1].uiSize;
	psSysConfig->pasPhysHeaps[ui32HeapNum].uiSize = RGX_TC_RESERVE_DC_MEM_SIZE;
	ui32HeapNum++;
#endif

#if defined(SUPPORT_ION)
	/* Setup the ion heap */
	psSysConfig->pasPhysHeaps[ui32HeapNum].sStartAddr.uiAddr =
		psSysConfig->pasPhysHeaps[ui32HeapNum - 1].sStartAddr.uiAddr +
		psSysConfig->pasPhysHeaps[ui32HeapNum - 1].uiSize;
	psSysConfig->pasPhysHeaps[ui32HeapNum].uiSize = RGX_TC_RESERVE_ION_MEM_SIZE;
	ui32HeapNum++;
#endif

	/* Configure Apollo for regression compatibility (i.e. local memory) mode */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	ui32Value &= ~(ADDRESS_FORCE_MASK | PCI_TEST_MODE_MASK | HOST_ONLY_MODE_MASK | HOST_PHY_MODE_MASK);
	ui32Value |= (0x1 << ADDRESS_FORCE_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL, ui32Value);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	OSWaitus(10);

	return PVRSRV_OK;
}

static void DeInitLocalMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
	ReleaseLocalMemory(psSysData, &psSysData->sLocalMemCpuPBase, psSysData->uiLocalMemSize);

	psSysConfig->pasPhysHeaps[0].sStartAddr.uiAddr	= 0;
	psSysConfig->pasPhysHeaps[0].uiSize		= 0;
	psSysConfig->pasPhysHeaps[1].sStartAddr.uiAddr	= 0;
	psSysConfig->pasPhysHeaps[1].uiSize		= 0;

	/* Set the register back to the default value */
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL, 0x1 << ADDRESS_FORCE_SHIFT);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	OSWaitus(10);
}
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HOST)
static PVRSRV_ERROR InitHostMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
	IMG_UINT32 ui32Value;

	PVR_UNREFERENCED_PARAMETER(psSysConfig);

	/* Configure Apollo for host only mode */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	ui32Value &= ~(ADDRESS_FORCE_MASK | PCI_TEST_MODE_MASK | HOST_ONLY_MODE_MASK | HOST_PHY_MODE_MASK);
	ui32Value |= (0x1 << HOST_ONLY_MODE_SHIFT);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL, ui32Value);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	OSWaitus(10);

	return PVRSRV_OK;
}

static void DeInitHostMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
	PVR_UNREFERENCED_PARAMETER(psSysConfig);

	/* Set the register back to the default value */
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL, 0x1 << ADDRESS_FORCE_SHIFT);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	OSWaitus(10);
}
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID)
static PVRSRV_ERROR InitHybridMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
	IMG_UINT32 ui32Value;
	PVRSRV_ERROR eError;

	eError = AcquireLocalMemory(psSysData, &psSysData->sLocalMemCpuPBase, &psSysData->uiLocalMemSize);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/* Rogue is using system memory so there is no additional heap setup needed */

	/* Setup the DC heap */
	psSysConfig->pasPhysHeaps[1].sStartAddr.uiAddr	= psSysData->sLocalMemCpuPBase.uiAddr;
	psSysConfig->pasPhysHeaps[1].uiSize		= psSysData->uiLocalMemSize;

	/* Configure Apollo for host physical (i.e. hybrid) mode */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	ui32Value &= ~(ADDRESS_FORCE_MASK | PCI_TEST_MODE_MASK | HOST_ONLY_MODE_MASK | HOST_PHY_MODE_MASK);
	ui32Value |= ((0x1 << HOST_ONLY_MODE_SHIFT) | (0x1 << HOST_PHY_MODE_SHIFT));
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL, ui32Value);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	OSWaitus(10);

	/* Setup the start address of the 1GB window which is redirected to local memory */
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_HOST_PHY_OFFSET, psSysData->sLocalMemCpuPBase.uiAddr);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_HOST_PHY_OFFSET);
	OSWaitus(10);

	return PVRSRV_OK;
}

static void DeInitHybridMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
	ReleaseLocalMemory(psSysData, &psSysData->sLocalMemCpuPBase, psSysData->uiLocalMemSize);

	psSysConfig->pasPhysHeaps[1].sStartAddr.uiAddr	= 0;
	psSysConfig->pasPhysHeaps[1].uiSize		= 0;

	/* Set the register back to the default value */
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL, 0x1 << ADDRESS_FORCE_SHIFT);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	OSWaitus(10);
}
#elif (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED)
static PVRSRV_ERROR InitDirectMappedMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
	IMG_UINT32 ui32Value;
	PVRSRV_ERROR eError;

	eError = AcquireLocalMemory(psSysData, &psSysData->sLocalMemCpuPBase, &psSysData->uiLocalMemSize);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/* Setup the RGX heap */
	psSysConfig->pasPhysHeaps[0].sStartAddr.uiAddr	= psSysData->sLocalMemCpuPBase.uiAddr;
	psSysConfig->pasPhysHeaps[0].uiSize		= RGX_TC_RESERVE_SERVICES_MEM_SIZE;

	PVR_ASSERT(psSysData->uiLocalMemSize >= psSysConfig->pasPhysHeaps[0].uiSize);

	/* Configure Apollo for direct mapping mode */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	ui32Value &= ~(ADDRESS_FORCE_MASK | PCI_TEST_MODE_MASK | HOST_ONLY_MODE_MASK | HOST_PHY_MODE_MASK);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL, ui32Value);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	OSWaitus(10);

	return PVRSRV_OK;
}

static void DeInitDirectMappedMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
	ReleaseLocalMemory(psSysData, &psSysData->sLocalMemCpuPBase, psSysData->uiLocalMemSize);

	psSysConfig->pasPhysHeaps[0].sStartAddr.uiAddr	= 0;
	psSysConfig->pasPhysHeaps[0].uiSize		= 0;

	/* Set the register back to the default value */
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL, 0x1 << ADDRESS_FORCE_SHIFT);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_TEST_CTRL);
	OSWaitus(10);
}
#endif

static PVRSRV_ERROR InitMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
#if (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL)
	return InitLocalMemory(psSysConfig, psSysData);
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HOST)
	return InitHostMemory(psSysConfig, psSysData);
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID)
	return InitHybridMemory(psSysConfig, psSysData);
#elif (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED)
	return InitDirectMappedMemory(psSysConfig, psSysData);
#endif
}

static INLINE void DeInitMemory(PVRSRV_SYSTEM_CONFIG *psSysConfig, SYS_DATA *psSysData)
{
#if (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL)
	DeInitLocalMemory(psSysConfig, psSysData);
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HOST)
	DeInitHostMemory(psSysConfig, psSysData);
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID)
	DeInitHybridMemory(psSysConfig, psSysData);
#elif (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED)
	DeInitDirectMappedMemory(psSysConfig, psSysData);
#endif
}

static IMG_VOID EnableInterrupt(SYS_DATA *psSysData, IMG_UINT32 ui32InterruptFlag)
{
	IMG_UINT32 ui32Value;

	/* Set sense to active high */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_OP_CFG);
	ui32Value &= ~(INT_SENSE_MASK);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_OP_CFG, ui32Value);
	OSWaitus(1000);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_OP_CFG);

	/* Enable Rogue and PDP1 interrupts */
	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_ENABLE);
	ui32Value |= ui32InterruptFlag;
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_ENABLE, ui32Value);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_ENABLE);
	OSWaitus(10);
}

static IMG_VOID DisableInterrupt(SYS_DATA *psSysData, IMG_UINT32 ui32InterruptFlag)
{
	IMG_UINT32 ui32Value;

	ui32Value = OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_ENABLE);
	ui32Value &= ~(ui32InterruptFlag);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_ENABLE, ui32Value);

	/* Flush register write */
	(void)OSReadHWReg32(psSysData->pvSystemRegCpuVBase, TCF_CLK_CTRL_INTERRUPT_ENABLE);
	OSWaitus(10);
}

static PVRSRV_ERROR ApolloFlashInit(IMG_VOID *pvData)
{
	SYS_DATA *psSysData = (SYS_DATA *)pvData;

	if (PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN) ||
	    PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING) ||
	    PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL))
	{
		PVR_DPF((PVR_DBG_ERROR, "You cannot flash while the driver is running. Please unload the driver and run the tool again.\n"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	if (psSysData == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	ApolloHardReset(psSysData);

	PVR_DPF((PVR_DBG_MESSAGE, "Resetting Apollo to prepare for programming.\n"));
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, APOLLO_FLASH_RESET_OFFSET, 0);
	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, APOLLO_FLASH_RESET_OFFSET, 1);

	return PVRSRV_OK;
}

static PVRSRV_ERROR ApolloFlashWrite(IMG_VOID *pvData, const IMG_UINT32 *puiWord)
{
	SYS_DATA *psSysData = (SYS_DATA *)pvData;

	if (psSysData == NULL)
	{
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	OSWriteHWReg32(psSysData->pvSystemRegCpuVBase, APOLLO_FLASH_DATA_WRITE_OFFSET, *puiWord);

	return PVRSRV_OK;
}

static PVRSRV_ERROR ApolloFlashGetStatus(IMG_VOID *pvData,
					 IMG_UINT32 *puiWriteSpace,
					 FLASH_STATUS *peFlashStatus)
{
	SYS_DATA *psSysData = (SYS_DATA *)pvData;
	IMG_UINT32 uiStatus;
	IMG_UINT16 uiProgramStatus;
	IMG_UINT16 uiFifoStatus;

	if (psSysData == NULL)
	{
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	uiStatus	= OSReadHWReg32(psSysData->pvSystemRegCpuVBase, APOLLO_FLASH_STAT_OFFSET);
	uiFifoStatus	= (uiStatus >> APOLLO_FLASH_FIFO_STATUS_SHIFT) & APOLLO_FLASH_FIFO_STATUS_MASK;
	uiProgramStatus	= (uiStatus >> APOLLO_FLASH_PROGAM_STATUS_SHIFT) & APOLLO_FLASH_PROGRAM_STATUS_MASK;

	if (uiProgramStatus & APOLLO_FLASH_PROG_COMPLETE_BIT)
	{
		*peFlashStatus = FLASH_STATUS_FINISHED;
	}
	else if (uiProgramStatus & APOLLO_FLASH_PROG_FAILED_BIT)
	{
		*peFlashStatus = FLASH_STATUS_FAILED;
	}
	else if (!(uiProgramStatus & APOLLO_FLASH_INV_FILETYPE_BIT))
	{
		*peFlashStatus = FLASH_STATUS_FAILED;
	}
	else if (uiProgramStatus & APOLLO_FLASH_PROG_PROGRESS_BIT)
	{
		*peFlashStatus = FLASH_STATUS_IN_PROGRESS;
	}
	else
	{
		*peFlashStatus = FLASH_STATUS_WAITING;
	}

	*puiWriteSpace = APOLLO_FLASH_FIFO_SIZE - uiFifoStatus;

	return PVRSRV_OK;
}


static IMG_VOID PCIDeInitDev(SYS_DATA *psSysData)
{
	PVRSRV_DEVICE_CONFIG *psDevice = &gsSysConfig.pasDevices[0];

#if !defined(SUPPORT_DRM)
#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
	OSUninstallSystemLISR(psSysData->hLISR);
	psSysData->hLISR = NULL;
#else
	DisableInterrupt(psSysData, (0x1 << EXT_INT_SHIFT) | (0x1 << PDP1_INT_SHIFT));
#endif
#endif

	if (psSysData->pszSystemInfoString)
	{
		OSFreeMem(psSysData->pszSystemInfoString);
		psSysData->pszSystemInfoString = NULL;
	}

	if (psSysData->hFlashData != NULL)
	{
		ApolloFlasherCleanup(psSysData->hFlashData);
	}

	DeInitMemory(&gsSysConfig, psSysData);

	OSUnMapPhysToLin(psSysData->pvSystemRegCpuVBase, psSysData->uiSystemRegSize, 0);
	psSysData->pvSystemRegCpuVBase = NULL;

	OSPCIReleaseAddrRange(psSysData->hRGXPCI, SYS_RGX_REG_PCI_BASENUM);
	psDevice->sRegsCpuPBase.uiAddr	= 0;
	psDevice->ui32RegsSize		= 0;

	OSPCIReleaseAddrRegion(psSysData->hRGXPCI, SYS_APOLLO_REG_PCI_BASENUM, SYS_APOLLO_REG_SYS_OFFSET, SYS_APOLLO_REG_SYS_SIZE);
	psSysData->sSystemRegCpuPBase.uiAddr	= 0;
	psSysData->uiSystemRegSize		= 0;

	OSPCIReleaseDev(psSysData->hRGXPCI);
	psSysData->hRGXPCI = IMG_NULL;
}

#if (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL)
static IMG_VOID TCLocalCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					  IMG_DEV_PHYADDR *psDevPAddr,
					  IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVRSRV_SYSTEM_CONFIG *psSysConfig = (PVRSRV_SYSTEM_CONFIG *)hPrivData;

	psDevPAddr->uiAddr = psCpuPAddr->uiAddr - psSysConfig->pasPhysHeaps[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL].sStartAddr.uiAddr;
}

static IMG_VOID TCLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
					  IMG_CPU_PHYADDR *psCpuPAddr,
					  IMG_DEV_PHYADDR *psDevPAddr)
{
	PVRSRV_SYSTEM_CONFIG *psSysConfig = (PVRSRV_SYSTEM_CONFIG *)hPrivData;

	psCpuPAddr->uiAddr = psDevPAddr->uiAddr + psSysConfig->pasPhysHeaps[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL].sStartAddr.uiAddr;
}
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HOST) || (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID) || (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED)
static IMG_VOID TCSystemCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					   IMG_DEV_PHYADDR *psDevPAddr,
					   IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	psDevPAddr->uiAddr = psCpuPAddr->uiAddr;
}

static IMG_VOID TCSystemDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
					   IMG_CPU_PHYADDR *psCpuPAddr,
					   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	psCpuPAddr->uiAddr = (IMG_UINTPTR_T)psDevPAddr->uiAddr;
}
#endif /* (TC_MEMORY_CONFIG == TC_MEMORY_HOST) || (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID) || (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED) */

PVRSRV_ERROR SysCreateConfigData(PVRSRV_SYSTEM_CONFIG **ppsSysConfig)
{
	SYS_DATA *psSysData;
	PVRSRV_ERROR eError;

	psSysData = OSAllocMem(sizeof *psSysData);
	if (psSysData == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psSysData, 0, sizeof *psSysData);

	eError = PCIInitDev(psSysData);
	if (eError != PVRSRV_OK)
	{
		goto ErrorFreeSysData;
	}

#if defined(SUPPORT_ION)
#if TC_MEMORY_CONFIG == TC_MEMORY_LOCAL
	{
		/* Set up the ion heap according to the physical heap we created, by
		   passing down a private data struct that this system's IonInit()
		   should understand. This lets the ion support code continue to
		   work if the heap configuration changes. */
		ION_TC_PRIVATE_DATA sIonPrivateData = {
			.uiHeapBase = gsSysConfig.pasPhysHeaps[2].sStartAddr.uiAddr,
			.uiHeapSize = RGX_TC_RESERVE_ION_MEM_SIZE,
			.ui32IonPhysHeapID = gsSysConfig.pasPhysHeaps[2].ui32PhysHeapID,
			.sPCIAddrRangeStart = gsSysConfig.pasPhysHeaps[0].sStartAddr
		};

		IonInit(&sIonPrivateData);
	}
#elif TC_MEMORY_CONFIG == TC_MEMORY_HYBRID
	IonInit(NULL);
#else
#error Ion support requires TC_MEMORY_LOCAL or TC_MEMORY_HYBRID
#endif /* TC_MEMORY_CONFIG */
#endif /* defined(SUPPORT_ION) */

	(void)SysAcquireSystemData((IMG_HANDLE)psSysData);

	*ppsSysConfig = &gsSysConfig;

	return PVRSRV_OK;

ErrorFreeSysData:
	OSFreeMem(psSysData);

	return eError;
}

IMG_VOID SysDestroyConfigData(PVRSRV_SYSTEM_CONFIG *psSysConfig)
{
	SYS_DATA *psSysData = (SYS_DATA *)psSysConfig->pasDevices[0].hSysData;

	PCIDeInitDev(psSysData);

	(void)SysReleaseSystemData((IMG_HANDLE)psSysData);
}

PVRSRV_ERROR SysAcquireSystemData(IMG_HANDLE hSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hSysData;

	if (psSysData == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psSysData->uiRefCount++;

	return PVRSRV_OK;
}

PVRSRV_ERROR SysReleaseSystemData(IMG_HANDLE hSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hSysData;

	if (psSysData == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_ASSERT(psSysData->uiRefCount != 0);
	psSysData->uiRefCount--;

	if (psSysData->uiRefCount == 0)
	{
		OSFreeMem(psSysData);
	}

	return PVRSRV_OK;
}

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
PVRSRV_ERROR SysInstallDeviceLISR(IMG_UINT32 ui32IRQ,
				  IMG_CHAR *pszName,
				  PFN_LISR pfnLISR,
				  IMG_PVOID pvData,
				  IMG_HANDLE *phLISRData)
{
	PVRSRV_DEVICE_CONFIG *psDevice = &gsSysConfig.pasDevices[0];
	SYS_DATA *psSysData = (SYS_DATA *)psDevice->hSysData;
	IMG_UINT32 ui32InterruptFlag;

	switch (ui32IRQ)
	{
		case 0:
			ui32InterruptFlag = (0x1 << EXT_INT_SHIFT);
			break;
		case 1:
			ui32InterruptFlag = (0x1 << PDP1_INT_SHIFT);
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR, "%s: No device matching IRQ %d", __FUNCTION__, ui32IRQ));
			return PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
	}

	if (psSysData->sInterruptData[ui32IRQ].pfnLISR)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ISR for %s already installed!", __FUNCTION__, pszName));
		return PVRSRV_ERROR_ISR_ALREADY_INSTALLED;
	}

	psSysData->sInterruptData[ui32IRQ].psSysData		= psSysData;
	psSysData->sInterruptData[ui32IRQ].pszName		= pszName;
	psSysData->sInterruptData[ui32IRQ].pfnLISR		= pfnLISR;
	psSysData->sInterruptData[ui32IRQ].pvData		= pvData;
	psSysData->sInterruptData[ui32IRQ].ui32InterruptFlag	= ui32InterruptFlag;

	*phLISRData = &psSysData->sInterruptData[ui32IRQ];

	EnableInterrupt(psSysData, psSysData->sInterruptData[ui32IRQ].ui32InterruptFlag);

	PVR_LOG(("Installed device LISR %s on IRQ %d", pszName, psSysData->ui32IRQ));

	return PVRSRV_OK;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	SYS_INTERRUPT_DATA *psInterruptData = (SYS_INTERRUPT_DATA *)hLISRData;

	PVR_ASSERT(psInterruptData);

	PVR_LOG(("Uninstalling device LISR %s on IRQ %d", psInterruptData->pszName, psInterruptData->psSysData->ui32IRQ));

	/* Disable interrupts for this device */
	DisableInterrupt(psInterruptData->psSysData, psInterruptData->ui32InterruptFlag);

	/* Reset the interrupt data */
	psInterruptData->pszName = NULL;
	psInterruptData->psSysData = NULL;
	psInterruptData->pfnLISR = NULL;
	psInterruptData->pvData = NULL;
	psInterruptData->ui32InterruptFlag = 0;

	return PVRSRV_OK;
}
#endif /* defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING) */

#if defined(_WIN32)
IMG_VOID GetCpuVBase(IMG_VOID ** ppvSystemRegCpuVBase)
{
	SYS_DATA *psSysData = (SYS_DATA *)gsSysConfig.pasDevices[0].hSysData;

	if (psSysData)
	{
		*ppvSystemRegCpuVBase = psSysData->pvSystemRegCpuVBase;
	}
	else
	{
		*ppvSystemRegCpuVBase = IMG_NULL;
	}

}

/* used by local wddmconfig.c as psSysData is not exported */
IMG_UINT32 GetRGXMemBase()
{
	SYS_DATA *psSysData = (SYS_DATA *)gsSysConfig.pasDevices[0].hSysData;
	PVR_ASSERT(psSysData != IMG_NULL);

	return OSPCIAddrRangeStart(psSysData->hRGXPCI, SYS_DEV_MEM_PCI_BASENUM);
}

IMG_UINT64 GetRGXMemHeapSize()
{
	return gsSysConfig.pasPhysHeaps[0].uiSize;
}
#endif
