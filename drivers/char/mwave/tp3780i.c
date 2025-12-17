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

#define pr_fmt(fmt) "tp3780i: " fmt

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


static void EnableSRAM(struct thinkpad_bd_data *pBDData)
{
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	DSP_GPIO_OUTPUT_DATA_15_8 rGpioOutputData;
	DSP_GPIO_DRIVER_ENABLE_15_8 rGpioDriverEnable;
	DSP_GPIO_MODE_15_8 rGpioMode;

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
}


static irqreturn_t UartInterrupt(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static irqreturn_t DspInterrupt(int irq, void *dev_id)
{
	struct mwave_device_data *pDrvData = &mwave_s_mdd;
	struct dsp_3780i_config_settings *pSettings = &pDrvData->rBDData.rDspSettings;
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	unsigned short usIPCSource = 0, usIsolationMask, usPCNum;

	if (dsp3780I_GetIPCSource(usDspBaseIO, &usIPCSource) == 0) {
		usIsolationMask = 1;
		for (usPCNum = 1; usPCNum <= 16; usPCNum++) {
			if (usIPCSource & usIsolationMask) {
				usIPCSource &= ~usIsolationMask;
				if (pDrvData->IPCs[usPCNum - 1].usIntCount == 0) {
					pDrvData->IPCs[usPCNum - 1].usIntCount = 1;
				}
				if (pDrvData->IPCs[usPCNum - 1].bIsEnabled == true) {
					wake_up_interruptible(&pDrvData->IPCs[usPCNum - 1].ipc_wait_queue);
				}
			}
			if (usIPCSource == 0)
				break;
			/* try next IPC */
			usIsolationMask = usIsolationMask << 1;
		}
	}
	return IRQ_HANDLED;
}


int tp3780I_InitializeBoardData(struct thinkpad_bd_data *pBDData)
{
	int retval = 0;
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;

	pBDData->bDSPEnabled = false;
	pSettings->bInterruptClaimed = false;

	retval = smapi_init();
	if (retval) {
		pr_err("%s: Error: SMAPI is not available on this machine\n", __func__);
	} else {
		if (mwave_3780i_irq || mwave_3780i_io || mwave_uart_irq || mwave_uart_io) {
			retval = smapi_set_DSP_cfg();
		}
	}

	return retval;
}

void tp3780I_Cleanup(struct thinkpad_bd_data *pBDData)
{
}

int tp3780I_CalcResources(struct thinkpad_bd_data *pBDData)
{
	struct smapi_dsp_settings rSmapiInfo;
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;

	if (smapi_query_DSP_cfg(&rSmapiInfo)) {
		pr_err("%s: Error: Could not query DSP config. Aborting.\n", __func__);
		return -EIO;
	}

	/* Sanity check */
	if (
		( rSmapiInfo.usDspIRQ == 0 )
		|| ( rSmapiInfo.usDspBaseIO ==  0 )
		|| ( rSmapiInfo.usUartIRQ ==  0 )
		|| ( rSmapiInfo.usUartBaseIO ==  0 )
	) {
		pr_err("%s: Error: Illegal resource setting. Aborting.\n", __func__);
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

	return 0;
}


int tp3780I_ClaimResources(struct thinkpad_bd_data *pBDData)
{
	int retval = 0;
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;
	struct resource *pres;

	pres = request_region(pSettings->usDspBaseIO, 16, "mwave_3780i");
	if ( pres == NULL ) retval = -EIO;

	if (retval) {
		pr_err("%s: Error: Could not claim I/O region starting at %x\n", __func__,
		       pSettings->usDspBaseIO);
		return -EIO;
	}

	return retval;
}

int tp3780I_ReleaseResources(struct thinkpad_bd_data *pBDData)
{
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;

	release_region(pSettings->usDspBaseIO & (~3), 16);

	if (pSettings->bInterruptClaimed) {
		free_irq(pSettings->usDspIrq, NULL);
		pSettings->bInterruptClaimed = false;
	}

	return 0;
}



int tp3780I_EnableDSP(struct thinkpad_bd_data *pBDData)
{
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;
	bool bDSPPoweredUp = false, bInterruptAllocated = false;

	if (pBDData->bDSPEnabled) {
		pr_err("%s: Error: DSP already enabled!\n", __func__);
		goto exit_cleanup;
	}

	if (!pSettings->bDSPEnabled) {
		pr_err("%s: Error: pSettings->bDSPEnabled not set\n", __func__);
		goto exit_cleanup;
	}

	if (
		(pSettings->usDspIrq >= s_numIrqs)
		|| (pSettings->usDspDma >= s_numDmas)
		|| (s_ausThinkpadIrqToField[pSettings->usDspIrq] == 0xFFFF)
		|| (s_ausThinkpadDmaToField[pSettings->usDspDma] == 0xFFFF)
	) {
		pr_err("%s: Error: invalid irq %x\n", __func__, pSettings->usDspIrq);
		goto exit_cleanup;
	}

	if (
		((pSettings->usDspBaseIO & 0xF00F) != 0)
		|| (pSettings->usDspBaseIO & 0x0FF0) == 0
	) {
		pr_err("%s: Error: Invalid DSP base I/O address %x\n", __func__,
		       pSettings->usDspBaseIO);
		goto exit_cleanup;
	}

	if (pSettings->bModemEnabled) {
		if (
			pSettings->usUartIrq >= s_numIrqs
			|| s_ausThinkpadIrqToField[pSettings->usUartIrq] == 0xFFFF
		) {
			pr_err("%s: Error: Invalid UART IRQ %x\n", __func__, pSettings->usUartIrq);
			goto exit_cleanup;
		}
		switch (pSettings->usUartBaseIO) {
			case 0x03F8:
			case 0x02F8:
			case 0x03E8:
			case 0x02E8:
				break;

			default:
				pr_err("%s: Error: Invalid UART base I/O address %x\n", __func__,
				       pSettings->usUartBaseIO);
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
		pr_err("%s: Error: Could not get UART IRQ %x\n", __func__, pSettings->usUartIrq);
		goto exit_cleanup;
	} else {		/* no conflict just release */
		free_irq(pSettings->usUartIrq, NULL);
	}

	if (request_irq(pSettings->usDspIrq, &DspInterrupt, 0, "mwave_3780i", NULL)) {
		pr_err("%s: Error: Could not get 3780i IRQ %x\n", __func__, pSettings->usDspIrq);
		goto exit_cleanup;
	} else {
		bInterruptAllocated = true;
		pSettings->bInterruptClaimed = true;
	}

	smapi_set_DSP_power_state(false);
	if (smapi_set_DSP_power_state(true)) {
		pr_err("%s: Error: smapi_set_DSP_power_state(true) failed\n", __func__);
		goto exit_cleanup;
	} else {
		bDSPPoweredUp = true;
	}

	if (dsp3780I_EnableDSP(pSettings, s_ausThinkpadIrqToField, s_ausThinkpadDmaToField)) {
		pr_err("%s: Error: dsp7880I_EnableDSP() failed\n", __func__);
		goto exit_cleanup;
	}

	EnableSRAM(pBDData);

	pBDData->bDSPEnabled = true;

	return 0;

exit_cleanup:
	pr_err("%s: Cleaning up\n", __func__);
	if (bDSPPoweredUp)
		smapi_set_DSP_power_state(false);
	if (bInterruptAllocated) {
		free_irq(pSettings->usDspIrq, NULL);
		pSettings->bInterruptClaimed = false;
	}
	return -EIO;
}


int tp3780I_DisableDSP(struct thinkpad_bd_data *pBDData)
{
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;

	if (pBDData->bDSPEnabled) {
		dsp3780I_DisableDSP(&pBDData->rDspSettings);
		if (pSettings->bInterruptClaimed) {
			free_irq(pSettings->usDspIrq, NULL);
			pSettings->bInterruptClaimed = false;
		}
		smapi_set_DSP_power_state(false);
		pBDData->bDSPEnabled = false;
	}

	return 0;
}


int tp3780I_ResetDSP(struct thinkpad_bd_data *pBDData)
{
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;

	if (dsp3780I_Reset(pSettings) == 0) {
		EnableSRAM(pBDData);
		return 0;
	}
	return -EIO;
}


int tp3780I_StartDSP(struct thinkpad_bd_data *pBDData)
{
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;

	if (dsp3780I_Run(pSettings) == 0) {
		// @BUG @TBD EnableSRAM(pBDData);
	} else {
		return -EIO;
	}

	return 0;
}


int tp3780I_QueryAbilities(struct thinkpad_bd_data *pBDData, struct mw_abilities *pAbilities)
{
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

	return 0;
}

int tp3780I_ReadWriteDspDStore(struct thinkpad_bd_data *pBDData, unsigned int uOpcode,
                               void __user *pvBuffer, unsigned int uCount,
                               unsigned long ulDSPAddr)
{
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	bool bRC = 0;

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

	return bRC ? -EIO : 0;
}


int tp3780I_ReadWriteDspIStore(struct thinkpad_bd_data *pBDData, unsigned int uOpcode,
                               void __user *pvBuffer, unsigned int uCount,
                               unsigned long ulDSPAddr)
{
	struct dsp_3780i_config_settings *pSettings = &pBDData->rDspSettings;
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	bool bRC = 0;

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

	return bRC ? -EIO : 0;
}

