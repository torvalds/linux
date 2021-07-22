/*
*
* tp3780i.c -- board driver for 3780i on ThinkPads
*
*
* Written By: Mike Sullivan IBM Corporation
*
* Copyright (C) 1999 IBM Corporation
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* NO WARRANTY
* THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
* LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
* solely responsible for determining the appropriateness of using and
* distributing the Program and assumes all risks associated with its
* exercise of rights under this Agreement, including but not limited to
* the risks and costs of program errors, damage to or loss of data,
* programs or equipment, and unavailability or interruption of operations.
*
* DISCLAIMER OF LIABILITY
* NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
* USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
* HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*
* 10/23/2000 - Alpha Release
*	First release to the public
*/

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include "smapi.h"
#include "mwavedd.h"
#include "tp3780i.h"
#include "3780i.h"
#include "mwavepub.h"

static unsigned short s_ausThinkpadIrqToField[16] =
	{ 0xFFFF, 0xFFFF, 0xFFFF, 0x0001, 0x0002, 0x0003, 0xFFFF, 0x0004,
	0xFFFF, 0xFFFF, 0x0005, 0x0006, 0xFFFF, 0xFFFF, 0xFFFF, 0x0007 };
static unsigned short s_ausThinkpadDmaToField[8] =
	{ 0x0001, 0x0002, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0003, 0x0004 };
static unsigned short s_numIrqs = 16, s_numDmas = 8;


static void EnableSRAM(THINKPAD_BD_DATA * pBDData)
{
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	DSP_GPIO_OUTPUT_DATA_15_8 rGpioOutputData;
	DSP_GPIO_DRIVER_ENABLE_15_8 rGpioDriverEnable;
	DSP_GPIO_MODE_15_8 rGpioMode;

	PRINTK_1(TRACE_TP3780I, "tp3780i::EnableSRAM, entry\n");

	MKWORD(rGpioMode) = ReadMsaCfg(DSP_GpioModeControl_15_8);
	rGpioMode.GpioMode10 = 0;
	WriteMsaCfg(DSP_GpioModeControl_15_8, MKWORD(rGpioMode));

	MKWORD(rGpioDriverEnable) = 0;
	rGpioDriverEnable.Enable10 = true;
	rGpioDriverEnable.Mask10 = true;
	WriteMsaCfg(DSP_GpioDriverEnable_15_8, MKWORD(rGpioDriverEnable));

	MKWORD(rGpioOutputData) = 0;
	rGpioOutputData.Latch10 = 0;
	rGpioOutputData.Mask10 = true;
	WriteMsaCfg(DSP_GpioOutputData_15_8, MKWORD(rGpioOutputData));

	PRINTK_1(TRACE_TP3780I, "tp3780i::EnableSRAM exit\n");
}


static irqreturn_t UartInterrupt(int irq, void *dev_id)
{
	PRINTK_3(TRACE_TP3780I,
		"tp3780i::UartInterrupt entry irq %x dev_id %p\n", irq, dev_id);
	return IRQ_HANDLED;
}

static irqreturn_t DspInterrupt(int irq, void *dev_id)
{
	pMWAVE_DEVICE_DATA pDrvData = &mwave_s_mdd;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pDrvData->rBDData.rDspSettings;
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	unsigned short usIPCSource = 0, usIsolationMask, usPCNum;

	PRINTK_3(TRACE_TP3780I,
		"tp3780i::DspInterrupt entry irq %x dev_id %p\n", irq, dev_id);

	if (dsp3780I_GetIPCSource(usDspBaseIO, &usIPCSource) == 0) {
		PRINTK_2(TRACE_TP3780I,
			"tp3780i::DspInterrupt, return from dsp3780i_GetIPCSource, usIPCSource %x\n",
			usIPCSource);
		usIsolationMask = 1;
		for (usPCNum = 1; usPCNum <= 16; usPCNum++) {
			if (usIPCSource & usIsolationMask) {
				usIPCSource &= ~usIsolationMask;
				PRINTK_3(TRACE_TP3780I,
					"tp3780i::DspInterrupt usPCNum %x usIPCSource %x\n",
					usPCNum, usIPCSource);
				if (pDrvData->IPCs[usPCNum - 1].usIntCount == 0) {
					pDrvData->IPCs[usPCNum - 1].usIntCount = 1;
				}
				PRINTK_2(TRACE_TP3780I,
					"tp3780i::DspInterrupt usIntCount %x\n",
					pDrvData->IPCs[usPCNum - 1].usIntCount);
				if (pDrvData->IPCs[usPCNum - 1].bIsEnabled == true) {
					PRINTK_2(TRACE_TP3780I,
						"tp3780i::DspInterrupt, waking up usPCNum %x\n",
						usPCNum - 1);
					wake_up_interruptible(&pDrvData->IPCs[usPCNum - 1].ipc_wait_queue);
				} else {
					PRINTK_2(TRACE_TP3780I,
						"tp3780i::DspInterrupt, no one waiting for IPC %x\n",
						usPCNum - 1);
				}
			}
			if (usIPCSource == 0)
				break;
			/* try next IPC */
			usIsolationMask = usIsolationMask << 1;
		}
	} else {
		PRINTK_1(TRACE_TP3780I,
			"tp3780i::DspInterrupt, return false from dsp3780i_GetIPCSource\n");
	}
	PRINTK_1(TRACE_TP3780I, "tp3780i::DspInterrupt exit\n");
	return IRQ_HANDLED;
}


int tp3780I_InitializeBoardData(THINKPAD_BD_DATA * pBDData)
{
	int retval = 0;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;


	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_InitializeBoardData entry pBDData %p\n", pBDData);

	pBDData->bDSPEnabled = false;
	pSettings->bInterruptClaimed = false;

	retval = smapi_init();
	if (retval) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_InitializeBoardData: Error: SMAPI is not available on this machine\n");
	} else {
		if (mwave_3780i_irq || mwave_3780i_io || mwave_uart_irq || mwave_uart_io) {
			retval = smapi_set_DSP_cfg();
		}
	}

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_InitializeBoardData exit retval %x\n", retval);

	return retval;
}

void tp3780I_Cleanup(THINKPAD_BD_DATA *pBDData)
{
	PRINTK_2(TRACE_TP3780I,
		"tp3780i::tp3780I_Cleanup entry and exit pBDData %p\n", pBDData);
}

int tp3780I_CalcResources(THINKPAD_BD_DATA * pBDData)
{
	SMAPI_DSP_SETTINGS rSmapiInfo;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;

	PRINTK_2(TRACE_TP3780I,
		"tp3780i::tp3780I_CalcResources entry pBDData %p\n", pBDData);

	if (smapi_query_DSP_cfg(&rSmapiInfo)) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_CalcResources: Error: Could not query DSP config. Aborting.\n");
		return -EIO;
	}

	/* Sanity check */
	if (
		( rSmapiInfo.usDspIRQ == 0 )
		|| ( rSmapiInfo.usDspBaseIO ==  0 )
		|| ( rSmapiInfo.usUartIRQ ==  0 )
		|| ( rSmapiInfo.usUartBaseIO ==  0 )
	) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_CalcResources: Error: Illegal resource setting. Aborting.\n");
		return -EIO;
	}

	pSettings->bDSPEnabled = (rSmapiInfo.bDSPEnabled && rSmapiInfo.bDSPPresent);
	pSettings->bModemEnabled = rSmapiInfo.bModemEnabled;
	pSettings->usDspIrq = rSmapiInfo.usDspIRQ;
	pSettings->usDspDma = rSmapiInfo.usDspDMA;
	pSettings->usDspBaseIO = rSmapiInfo.usDspBaseIO;
	pSettings->usUartIrq = rSmapiInfo.usUartIRQ;
	pSettings->usUartBaseIO = rSmapiInfo.usUartBaseIO;

	pSettings->uDStoreSize = TP_ABILITIES_DATA_SIZE;
	pSettings->uIStoreSize = TP_ABILITIES_INST_SIZE;
	pSettings->uIps = TP_ABILITIES_INTS_PER_SEC;

	if (pSettings->bDSPEnabled && pSettings->bModemEnabled && pSettings->usDspIrq == pSettings->usUartIrq) {
		pBDData->bShareDspIrq = pBDData->bShareUartIrq = 1;
	} else {
		pBDData->bShareDspIrq = pBDData->bShareUartIrq = 0;
	}

	PRINTK_1(TRACE_TP3780I, "tp3780i::tp3780I_CalcResources exit\n");

	return 0;
}


int tp3780I_ClaimResources(THINKPAD_BD_DATA * pBDData)
{
	int retval = 0;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;
	struct resource *pres;

	PRINTK_2(TRACE_TP3780I,
		"tp3780i::tp3780I_ClaimResources entry pBDData %p\n", pBDData);

	pres = request_region(pSettings->usDspBaseIO, 16, "mwave_3780i");
	if ( pres == NULL ) retval = -EIO;

	if (retval) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_ClaimResources: Error: Could not claim I/O region starting at %x\n", pSettings->usDspBaseIO);
		retval = -EIO;
	}

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_ClaimResources exit retval %x\n", retval);

	return retval;
}

int tp3780I_ReleaseResources(THINKPAD_BD_DATA * pBDData)
{
	int retval = 0;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;

	PRINTK_2(TRACE_TP3780I,
		"tp3780i::tp3780I_ReleaseResources entry pBDData %p\n", pBDData);

	release_region(pSettings->usDspBaseIO & (~3), 16);

	if (pSettings->bInterruptClaimed) {
		free_irq(pSettings->usDspIrq, NULL);
		pSettings->bInterruptClaimed = false;
	}

	PRINTK_2(TRACE_TP3780I,
		"tp3780i::tp3780I_ReleaseResources exit retval %x\n", retval);

	return retval;
}



int tp3780I_EnableDSP(THINKPAD_BD_DATA * pBDData)
{
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;
	bool bDSPPoweredUp = false, bInterruptAllocated = false;

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_EnableDSP entry pBDData %p\n", pBDData);

	if (pBDData->bDSPEnabled) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_EnableDSP: Error: DSP already enabled!\n");
		goto exit_cleanup;
	}

	if (!pSettings->bDSPEnabled) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780::tp3780I_EnableDSP: Error: pSettings->bDSPEnabled not set\n");
		goto exit_cleanup;
	}

	if (
		(pSettings->usDspIrq >= s_numIrqs)
		|| (pSettings->usDspDma >= s_numDmas)
		|| (s_ausThinkpadIrqToField[pSettings->usDspIrq] == 0xFFFF)
		|| (s_ausThinkpadDmaToField[pSettings->usDspDma] == 0xFFFF)
	) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_EnableDSP: Error: invalid irq %x\n", pSettings->usDspIrq);
		goto exit_cleanup;
	}

	if (
		((pSettings->usDspBaseIO & 0xF00F) != 0)
		|| (pSettings->usDspBaseIO & 0x0FF0) == 0
	) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_EnableDSP: Error: Invalid DSP base I/O address %x\n", pSettings->usDspBaseIO);
		goto exit_cleanup;
	}

	if (pSettings->bModemEnabled) {
		if (
			pSettings->usUartIrq >= s_numIrqs
			|| s_ausThinkpadIrqToField[pSettings->usUartIrq] == 0xFFFF
		) {
			PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_EnableDSP: Error: Invalid UART IRQ %x\n", pSettings->usUartIrq);
			goto exit_cleanup;
		}
		switch (pSettings->usUartBaseIO) {
			case 0x03F8:
			case 0x02F8:
			case 0x03E8:
			case 0x02E8:
				break;

			default:
				PRINTK_ERROR("tp3780i::tp3780I_EnableDSP: Error: Invalid UART base I/O address %x\n", pSettings->usUartBaseIO);
				goto exit_cleanup;
		}
	}

	pSettings->bDspIrqActiveLow = pSettings->bDspIrqPulse = true;
	pSettings->bUartIrqActiveLow = pSettings->bUartIrqPulse = true;

	if (pBDData->bShareDspIrq) {
		pSettings->bDspIrqActiveLow = false;
	}
	if (pBDData->bShareUartIrq) {
		pSettings->bUartIrqActiveLow = false;
	}

	pSettings->usNumTransfers = TP_CFG_NumTransfers;
	pSettings->usReRequest = TP_CFG_RerequestTimer;
	pSettings->bEnableMEMCS16 = TP_CFG_MEMCS16;
	pSettings->usIsaMemCmdWidth = TP_CFG_IsaMemCmdWidth;
	pSettings->bGateIOCHRDY = TP_CFG_GateIOCHRDY;
	pSettings->bEnablePwrMgmt = TP_CFG_EnablePwrMgmt;
	pSettings->usHBusTimerLoadValue = TP_CFG_HBusTimerValue;
	pSettings->bDisableLBusTimeout = TP_CFG_DisableLBusTimeout;
	pSettings->usN_Divisor = TP_CFG_N_Divisor;
	pSettings->usM_Multiplier = TP_CFG_M_Multiplier;
	pSettings->bPllBypass = TP_CFG_PllBypass;
	pSettings->usChipletEnable = TP_CFG_ChipletEnable;

	if (request_irq(pSettings->usUartIrq, &UartInterrupt, 0, "mwave_uart", NULL)) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_EnableDSP: Error: Could not get UART IRQ %x\n", pSettings->usUartIrq);
		goto exit_cleanup;
	} else {		/* no conflict just release */
		free_irq(pSettings->usUartIrq, NULL);
	}

	if (request_irq(pSettings->usDspIrq, &DspInterrupt, 0, "mwave_3780i", NULL)) {
		PRINTK_ERROR("tp3780i::tp3780I_EnableDSP: Error: Could not get 3780i IRQ %x\n", pSettings->usDspIrq);
		goto exit_cleanup;
	} else {
		PRINTK_3(TRACE_TP3780I,
			"tp3780i::tp3780I_EnableDSP, got interrupt %x bShareDspIrq %x\n",
			pSettings->usDspIrq, pBDData->bShareDspIrq);
		bInterruptAllocated = true;
		pSettings->bInterruptClaimed = true;
	}

	smapi_set_DSP_power_state(false);
	if (smapi_set_DSP_power_state(true)) {
		PRINTK_ERROR(KERN_ERR_MWAVE "tp3780i::tp3780I_EnableDSP: Error: smapi_set_DSP_power_state(true) failed\n");
		goto exit_cleanup;
	} else {
		bDSPPoweredUp = true;
	}

	if (dsp3780I_EnableDSP(pSettings, s_ausThinkpadIrqToField, s_ausThinkpadDmaToField)) {
		PRINTK_ERROR("tp3780i::tp3780I_EnableDSP: Error: dsp7880I_EnableDSP() failed\n");
		goto exit_cleanup;
	}

	EnableSRAM(pBDData);

	pBDData->bDSPEnabled = true;

	PRINTK_1(TRACE_TP3780I, "tp3780i::tp3780I_EnableDSP exit\n");

	return 0;

exit_cleanup:
	PRINTK_ERROR("tp3780i::tp3780I_EnableDSP: Cleaning up\n");
	if (bDSPPoweredUp)
		smapi_set_DSP_power_state(false);
	if (bInterruptAllocated) {
		free_irq(pSettings->usDspIrq, NULL);
		pSettings->bInterruptClaimed = false;
	}
	return -EIO;
}


int tp3780I_DisableDSP(THINKPAD_BD_DATA * pBDData)
{
	int retval = 0;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_DisableDSP entry pBDData %p\n", pBDData);

	if (pBDData->bDSPEnabled) {
		dsp3780I_DisableDSP(&pBDData->rDspSettings);
		if (pSettings->bInterruptClaimed) {
			free_irq(pSettings->usDspIrq, NULL);
			pSettings->bInterruptClaimed = false;
		}
		smapi_set_DSP_power_state(false);
		pBDData->bDSPEnabled = false;
	}

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_DisableDSP exit retval %x\n", retval);

	return retval;
}


int tp3780I_ResetDSP(THINKPAD_BD_DATA * pBDData)
{
	int retval = 0;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_ResetDSP entry pBDData %p\n",
		pBDData);

	if (dsp3780I_Reset(pSettings) == 0) {
		EnableSRAM(pBDData);
	} else {
		retval = -EIO;
	}

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_ResetDSP exit retval %x\n", retval);

	return retval;
}


int tp3780I_StartDSP(THINKPAD_BD_DATA * pBDData)
{
	int retval = 0;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_StartDSP entry pBDData %p\n", pBDData);

	if (dsp3780I_Run(pSettings) == 0) {
		// @BUG @TBD EnableSRAM(pBDData);
	} else {
		retval = -EIO;
	}

	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_StartDSP exit retval %x\n", retval);

	return retval;
}


int tp3780I_QueryAbilities(THINKPAD_BD_DATA * pBDData, MW_ABILITIES * pAbilities)
{
	int retval = 0;

	PRINTK_2(TRACE_TP3780I,
		"tp3780i::tp3780I_QueryAbilities entry pBDData %p\n", pBDData);

	memset(pAbilities, 0, sizeof(*pAbilities));
	/* fill out standard constant fields */
	pAbilities->instr_per_sec = pBDData->rDspSettings.uIps;
	pAbilities->data_size = pBDData->rDspSettings.uDStoreSize;
	pAbilities->inst_size = pBDData->rDspSettings.uIStoreSize;
	pAbilities->bus_dma_bw = pBDData->rDspSettings.uDmaBandwidth;

	/* fill out dynamically determined fields */
	pAbilities->component_list[0] = 0x00010000 | MW_ADC_MASK;
	pAbilities->component_list[1] = 0x00010000 | MW_ACI_MASK;
	pAbilities->component_list[2] = 0x00010000 | MW_AIC1_MASK;
	pAbilities->component_list[3] = 0x00010000 | MW_AIC2_MASK;
	pAbilities->component_list[4] = 0x00010000 | MW_CDDAC_MASK;
	pAbilities->component_list[5] = 0x00010000 | MW_MIDI_MASK;
	pAbilities->component_list[6] = 0x00010000 | MW_UART_MASK;
	pAbilities->component_count = 7;

	/* Fill out Mwave OS and BIOS task names */

	memcpy(pAbilities->mwave_os_name, TP_ABILITIES_MWAVEOS_NAME,
		sizeof(TP_ABILITIES_MWAVEOS_NAME));
	memcpy(pAbilities->bios_task_name, TP_ABILITIES_BIOSTASK_NAME,
		sizeof(TP_ABILITIES_BIOSTASK_NAME));

	PRINTK_1(TRACE_TP3780I,
		"tp3780i::tp3780I_QueryAbilities exit retval=SUCCESSFUL\n");

	return retval;
}

int tp3780I_ReadWriteDspDStore(THINKPAD_BD_DATA * pBDData, unsigned int uOpcode,
                               void __user *pvBuffer, unsigned int uCount,
                               unsigned long ulDSPAddr)
{
	int retval = 0;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	bool bRC = 0;

	PRINTK_6(TRACE_TP3780I,
		"tp3780i::tp3780I_ReadWriteDspDStore entry pBDData %p, uOpcode %x, pvBuffer %p, uCount %x, ulDSPAddr %lx\n",
		pBDData, uOpcode, pvBuffer, uCount, ulDSPAddr);

	if (pBDData->bDSPEnabled) {
		switch (uOpcode) {
		case IOCTL_MW_READ_DATA:
			bRC = dsp3780I_ReadDStore(usDspBaseIO, pvBuffer, uCount, ulDSPAddr);
			break;

		case IOCTL_MW_READCLEAR_DATA:
			bRC = dsp3780I_ReadAndClearDStore(usDspBaseIO, pvBuffer, uCount, ulDSPAddr);
			break;

		case IOCTL_MW_WRITE_DATA:
			bRC = dsp3780I_WriteDStore(usDspBaseIO, pvBuffer, uCount, ulDSPAddr);
			break;
		}
	}

	retval = (bRC) ? -EIO : 0;
	PRINTK_2(TRACE_TP3780I, "tp3780i::tp3780I_ReadWriteDspDStore exit retval %x\n", retval);

	return retval;
}


int tp3780I_ReadWriteDspIStore(THINKPAD_BD_DATA * pBDData, unsigned int uOpcode,
                               void __user *pvBuffer, unsigned int uCount,
                               unsigned long ulDSPAddr)
{
	int retval = 0;
	DSP_3780I_CONFIG_SETTINGS *pSettings = &pBDData->rDspSettings;
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	bool bRC = 0;

	PRINTK_6(TRACE_TP3780I,
		"tp3780i::tp3780I_ReadWriteDspIStore entry pBDData %p, uOpcode %x, pvBuffer %p, uCount %x, ulDSPAddr %lx\n",
		pBDData, uOpcode, pvBuffer, uCount, ulDSPAddr);

	if (pBDData->bDSPEnabled) {
		switch (uOpcode) {
		case IOCTL_MW_READ_INST:
			bRC = dsp3780I_ReadIStore(usDspBaseIO, pvBuffer, uCount, ulDSPAddr);
			break;

		case IOCTL_MW_WRITE_INST:
			bRC = dsp3780I_WriteIStore(usDspBaseIO, pvBuffer, uCount, ulDSPAddr);
			break;
		}
	}

	retval = (bRC) ? -EIO : 0;

	PRINTK_2(TRACE_TP3780I,
		"tp3780i::tp3780I_ReadWriteDspIStore exit retval %x\n", retval);

	return retval;
}

