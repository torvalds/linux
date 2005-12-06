/*
*
* 3780i.c -- helper routines for the 3780i DSP
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/sched.h>	/* cond_resched() */

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/irq.h>
#include "smapi.h"
#include "mwavedd.h"
#include "3780i.h"

static DEFINE_SPINLOCK(dsp_lock);
static unsigned long flags;


static void PaceMsaAccess(unsigned short usDspBaseIO)
{
	cond_resched();
	udelay(100);
	cond_resched();
}

unsigned short dsp3780I_ReadMsaCfg(unsigned short usDspBaseIO,
                                   unsigned long ulMsaAddr)
{
	unsigned short val;

	PRINTK_3(TRACE_3780I,
		"3780i::dsp3780I_ReadMsaCfg entry usDspBaseIO %x ulMsaAddr %lx\n",
		usDspBaseIO, ulMsaAddr);

	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_MsaAddrLow, (unsigned short) ulMsaAddr);
	OutWordDsp(DSP_MsaAddrHigh, (unsigned short) (ulMsaAddr >> 16));
	val = InWordDsp(DSP_MsaDataDSISHigh);
	spin_unlock_irqrestore(&dsp_lock, flags);

	PRINTK_2(TRACE_3780I, "3780i::dsp3780I_ReadMsaCfg exit val %x\n", val);

	return val;
}

void dsp3780I_WriteMsaCfg(unsigned short usDspBaseIO,
                          unsigned long ulMsaAddr, unsigned short usValue)
{

	PRINTK_4(TRACE_3780I,
		"3780i::dsp3780i_WriteMsaCfg entry usDspBaseIO %x ulMsaAddr %lx usValue %x\n",
		usDspBaseIO, ulMsaAddr, usValue);

	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_MsaAddrLow, (unsigned short) ulMsaAddr);
	OutWordDsp(DSP_MsaAddrHigh, (unsigned short) (ulMsaAddr >> 16));
	OutWordDsp(DSP_MsaDataDSISHigh, usValue);
	spin_unlock_irqrestore(&dsp_lock, flags);
}

static void dsp3780I_WriteGenCfg(unsigned short usDspBaseIO, unsigned uIndex,
				 unsigned char ucValue)
{
	DSP_ISA_SLAVE_CONTROL rSlaveControl;
	DSP_ISA_SLAVE_CONTROL rSlaveControl_Save;


	PRINTK_4(TRACE_3780I,
		"3780i::dsp3780i_WriteGenCfg entry usDspBaseIO %x uIndex %x ucValue %x\n",
		usDspBaseIO, uIndex, ucValue);

	MKBYTE(rSlaveControl) = InByteDsp(DSP_IsaSlaveControl);

	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780i_WriteGenCfg rSlaveControl %x\n",
		MKBYTE(rSlaveControl));

	rSlaveControl_Save = rSlaveControl;
	rSlaveControl.ConfigMode = TRUE;

	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780i_WriteGenCfg entry rSlaveControl+ConfigMode %x\n",
		MKBYTE(rSlaveControl));

	OutByteDsp(DSP_IsaSlaveControl, MKBYTE(rSlaveControl));
	OutByteDsp(DSP_ConfigAddress, (unsigned char) uIndex);
	OutByteDsp(DSP_ConfigData, ucValue);
	OutByteDsp(DSP_IsaSlaveControl, MKBYTE(rSlaveControl_Save));

	PRINTK_1(TRACE_3780I, "3780i::dsp3780i_WriteGenCfg exit\n");


}

#if 0
unsigned char dsp3780I_ReadGenCfg(unsigned short usDspBaseIO,
                                  unsigned uIndex)
{
	DSP_ISA_SLAVE_CONTROL rSlaveControl;
	DSP_ISA_SLAVE_CONTROL rSlaveControl_Save;
	unsigned char ucValue;


	PRINTK_3(TRACE_3780I,
		"3780i::dsp3780i_ReadGenCfg entry usDspBaseIO %x uIndex %x\n",
		usDspBaseIO, uIndex);

	MKBYTE(rSlaveControl) = InByteDsp(DSP_IsaSlaveControl);
	rSlaveControl_Save = rSlaveControl;
	rSlaveControl.ConfigMode = TRUE;
	OutByteDsp(DSP_IsaSlaveControl, MKBYTE(rSlaveControl));
	OutByteDsp(DSP_ConfigAddress, (unsigned char) uIndex);
	ucValue = InByteDsp(DSP_ConfigData);
	OutByteDsp(DSP_IsaSlaveControl, MKBYTE(rSlaveControl_Save));

	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780i_ReadGenCfg exit ucValue %x\n", ucValue);


	return ucValue;
}
#endif  /*  0  */

int dsp3780I_EnableDSP(DSP_3780I_CONFIG_SETTINGS * pSettings,
                       unsigned short *pIrqMap,
                       unsigned short *pDmaMap)
{
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	int i;
	DSP_UART_CFG_1 rUartCfg1;
	DSP_UART_CFG_2 rUartCfg2;
	DSP_HBRIDGE_CFG_1 rHBridgeCfg1;
	DSP_HBRIDGE_CFG_2 rHBridgeCfg2;
	DSP_BUSMASTER_CFG_1 rBusmasterCfg1;
	DSP_BUSMASTER_CFG_2 rBusmasterCfg2;
	DSP_ISA_PROT_CFG rIsaProtCfg;
	DSP_POWER_MGMT_CFG rPowerMgmtCfg;
	DSP_HBUS_TIMER_CFG rHBusTimerCfg;
	DSP_LBUS_TIMEOUT_DISABLE rLBusTimeoutDisable;
	DSP_CHIP_RESET rChipReset;
	DSP_CLOCK_CONTROL_1 rClockControl1;
	DSP_CLOCK_CONTROL_2 rClockControl2;
	DSP_ISA_SLAVE_CONTROL rSlaveControl;
	DSP_HBRIDGE_CONTROL rHBridgeControl;
	unsigned short ChipID = 0;
	unsigned short tval;


	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780I_EnableDSP entry pSettings->bDSPEnabled %x\n",
		pSettings->bDSPEnabled);


	if (!pSettings->bDSPEnabled) {
		PRINTK_ERROR( KERN_ERR "3780i::dsp3780I_EnableDSP: Error: DSP not enabled. Aborting.\n" );
		return -EIO;
	}


	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780i_EnableDSP entry pSettings->bModemEnabled %x\n",
		pSettings->bModemEnabled);

	if (pSettings->bModemEnabled) {
		rUartCfg1.Reserved = rUartCfg2.Reserved = 0;
		rUartCfg1.IrqActiveLow = pSettings->bUartIrqActiveLow;
		rUartCfg1.IrqPulse = pSettings->bUartIrqPulse;
		rUartCfg1.Irq =
			(unsigned char) pIrqMap[pSettings->usUartIrq];
		switch (pSettings->usUartBaseIO) {
		case 0x03F8:
			rUartCfg1.BaseIO = 0;
			break;
		case 0x02F8:
			rUartCfg1.BaseIO = 1;
			break;
		case 0x03E8:
			rUartCfg1.BaseIO = 2;
			break;
		case 0x02E8:
			rUartCfg1.BaseIO = 3;
			break;
		}
		rUartCfg2.Enable = TRUE;
	}

	rHBridgeCfg1.Reserved = rHBridgeCfg2.Reserved = 0;
	rHBridgeCfg1.IrqActiveLow = pSettings->bDspIrqActiveLow;
	rHBridgeCfg1.IrqPulse = pSettings->bDspIrqPulse;
	rHBridgeCfg1.Irq = (unsigned char) pIrqMap[pSettings->usDspIrq];
	rHBridgeCfg1.AccessMode = 1;
	rHBridgeCfg2.Enable = TRUE;


	rBusmasterCfg2.Reserved = 0;
	rBusmasterCfg1.Dma = (unsigned char) pDmaMap[pSettings->usDspDma];
	rBusmasterCfg1.NumTransfers =
		(unsigned char) pSettings->usNumTransfers;
	rBusmasterCfg1.ReRequest = (unsigned char) pSettings->usReRequest;
	rBusmasterCfg1.MEMCS16 = pSettings->bEnableMEMCS16;
	rBusmasterCfg2.IsaMemCmdWidth =
		(unsigned char) pSettings->usIsaMemCmdWidth;


	rIsaProtCfg.Reserved = 0;
	rIsaProtCfg.GateIOCHRDY = pSettings->bGateIOCHRDY;

	rPowerMgmtCfg.Reserved = 0;
	rPowerMgmtCfg.Enable = pSettings->bEnablePwrMgmt;

	rHBusTimerCfg.LoadValue =
		(unsigned char) pSettings->usHBusTimerLoadValue;

	rLBusTimeoutDisable.Reserved = 0;
	rLBusTimeoutDisable.DisableTimeout =
		pSettings->bDisableLBusTimeout;

	MKWORD(rChipReset) = ~pSettings->usChipletEnable;

	rClockControl1.Reserved1 = rClockControl1.Reserved2 = 0;
	rClockControl1.N_Divisor = pSettings->usN_Divisor;
	rClockControl1.M_Multiplier = pSettings->usM_Multiplier;

	rClockControl2.Reserved = 0;
	rClockControl2.PllBypass = pSettings->bPllBypass;

	/* Issue a soft reset to the chip */
	/* Note: Since we may be coming in with 3780i clocks suspended, we must keep
	* soft-reset active for 10ms.
	*/
	rSlaveControl.ClockControl = 0;
	rSlaveControl.SoftReset = TRUE;
	rSlaveControl.ConfigMode = FALSE;
	rSlaveControl.Reserved = 0;

	PRINTK_4(TRACE_3780I,
		"3780i::dsp3780i_EnableDSP usDspBaseIO %x index %x taddr %x\n",
		usDspBaseIO, DSP_IsaSlaveControl,
		usDspBaseIO + DSP_IsaSlaveControl);

	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780i_EnableDSP rSlaveContrl %x\n",
		MKWORD(rSlaveControl));

	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_IsaSlaveControl, MKWORD(rSlaveControl));
	MKWORD(tval) = InWordDsp(DSP_IsaSlaveControl);

	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780i_EnableDSP rSlaveControl 2 %x\n", tval);


	for (i = 0; i < 11; i++)
		udelay(2000);

	rSlaveControl.SoftReset = FALSE;
	OutWordDsp(DSP_IsaSlaveControl, MKWORD(rSlaveControl));

	MKWORD(tval) = InWordDsp(DSP_IsaSlaveControl);

	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780i_EnableDSP rSlaveControl 3 %x\n", tval);


	/* Program our general configuration registers */
	WriteGenCfg(DSP_HBridgeCfg1Index, MKBYTE(rHBridgeCfg1));
	WriteGenCfg(DSP_HBridgeCfg2Index, MKBYTE(rHBridgeCfg2));
	WriteGenCfg(DSP_BusMasterCfg1Index, MKBYTE(rBusmasterCfg1));
	WriteGenCfg(DSP_BusMasterCfg2Index, MKBYTE(rBusmasterCfg2));
	WriteGenCfg(DSP_IsaProtCfgIndex, MKBYTE(rIsaProtCfg));
	WriteGenCfg(DSP_PowerMgCfgIndex, MKBYTE(rPowerMgmtCfg));
	WriteGenCfg(DSP_HBusTimerCfgIndex, MKBYTE(rHBusTimerCfg));

	if (pSettings->bModemEnabled) {
		WriteGenCfg(DSP_UartCfg1Index, MKBYTE(rUartCfg1));
		WriteGenCfg(DSP_UartCfg2Index, MKBYTE(rUartCfg2));
	}


	rHBridgeControl.EnableDspInt = FALSE;
	rHBridgeControl.MemAutoInc = TRUE;
	rHBridgeControl.IoAutoInc = FALSE;
	rHBridgeControl.DiagnosticMode = FALSE;

	PRINTK_3(TRACE_3780I,
		"3780i::dsp3780i_EnableDSP DSP_HBridgeControl %x rHBridgeControl %x\n",
		DSP_HBridgeControl, MKWORD(rHBridgeControl));

	OutWordDsp(DSP_HBridgeControl, MKWORD(rHBridgeControl));
	spin_unlock_irqrestore(&dsp_lock, flags);
	WriteMsaCfg(DSP_LBusTimeoutDisable, MKWORD(rLBusTimeoutDisable));
	WriteMsaCfg(DSP_ClockControl_1, MKWORD(rClockControl1));
	WriteMsaCfg(DSP_ClockControl_2, MKWORD(rClockControl2));
	WriteMsaCfg(DSP_ChipReset, MKWORD(rChipReset));

	ChipID = ReadMsaCfg(DSP_ChipID);

	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780I_EnableDSP exiting bRC=TRUE, ChipID %x\n",
		ChipID);

	return 0;
}

int dsp3780I_DisableDSP(DSP_3780I_CONFIG_SETTINGS * pSettings)
{
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	DSP_ISA_SLAVE_CONTROL rSlaveControl;


	PRINTK_1(TRACE_3780I, "3780i::dsp3780i_DisableDSP entry\n");

	rSlaveControl.ClockControl = 0;
	rSlaveControl.SoftReset = TRUE;
	rSlaveControl.ConfigMode = FALSE;
	rSlaveControl.Reserved = 0;
	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_IsaSlaveControl, MKWORD(rSlaveControl));

	udelay(5);

	rSlaveControl.ClockControl = 1;
	OutWordDsp(DSP_IsaSlaveControl, MKWORD(rSlaveControl));
	spin_unlock_irqrestore(&dsp_lock, flags);

	udelay(5);


	PRINTK_1(TRACE_3780I, "3780i::dsp3780i_DisableDSP exit\n");

	return 0;
}

int dsp3780I_Reset(DSP_3780I_CONFIG_SETTINGS * pSettings)
{
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	DSP_BOOT_DOMAIN rBootDomain;
	DSP_HBRIDGE_CONTROL rHBridgeControl;


	PRINTK_1(TRACE_3780I, "3780i::dsp3780i_Reset entry\n");

	spin_lock_irqsave(&dsp_lock, flags);
	/* Mask DSP to PC interrupt */
	MKWORD(rHBridgeControl) = InWordDsp(DSP_HBridgeControl);

	PRINTK_2(TRACE_3780I, "3780i::dsp3780i_Reset rHBridgeControl %x\n",
		MKWORD(rHBridgeControl));

	rHBridgeControl.EnableDspInt = FALSE;
	OutWordDsp(DSP_HBridgeControl, MKWORD(rHBridgeControl));
	spin_unlock_irqrestore(&dsp_lock, flags);

	/* Reset the core via the boot domain register */
	rBootDomain.ResetCore = TRUE;
	rBootDomain.Halt = TRUE;
	rBootDomain.NMI = TRUE;
	rBootDomain.Reserved = 0;

	PRINTK_2(TRACE_3780I, "3780i::dsp3780i_Reset rBootDomain %x\n",
		MKWORD(rBootDomain));

	WriteMsaCfg(DSP_MspBootDomain, MKWORD(rBootDomain));

	/* Reset all the chiplets and then reactivate them */
	WriteMsaCfg(DSP_ChipReset, 0xFFFF);
	udelay(5);
	WriteMsaCfg(DSP_ChipReset,
			(unsigned short) (~pSettings->usChipletEnable));


	PRINTK_1(TRACE_3780I, "3780i::dsp3780i_Reset exit bRC=0\n");

	return 0;
}


int dsp3780I_Run(DSP_3780I_CONFIG_SETTINGS * pSettings)
{
	unsigned short usDspBaseIO = pSettings->usDspBaseIO;
	DSP_BOOT_DOMAIN rBootDomain;
	DSP_HBRIDGE_CONTROL rHBridgeControl;


	PRINTK_1(TRACE_3780I, "3780i::dsp3780i_Run entry\n");


	/* Transition the core to a running state */
	rBootDomain.ResetCore = TRUE;
	rBootDomain.Halt = FALSE;
	rBootDomain.NMI = TRUE;
	rBootDomain.Reserved = 0;
	WriteMsaCfg(DSP_MspBootDomain, MKWORD(rBootDomain));

	udelay(5);

	rBootDomain.ResetCore = FALSE;
	WriteMsaCfg(DSP_MspBootDomain, MKWORD(rBootDomain));
	udelay(5);

	rBootDomain.NMI = FALSE;
	WriteMsaCfg(DSP_MspBootDomain, MKWORD(rBootDomain));
	udelay(5);

	/* Enable DSP to PC interrupt */
	spin_lock_irqsave(&dsp_lock, flags);
	MKWORD(rHBridgeControl) = InWordDsp(DSP_HBridgeControl);
	rHBridgeControl.EnableDspInt = TRUE;

	PRINTK_2(TRACE_3780I, "3780i::dsp3780i_Run rHBridgeControl %x\n",
		MKWORD(rHBridgeControl));

	OutWordDsp(DSP_HBridgeControl, MKWORD(rHBridgeControl));
	spin_unlock_irqrestore(&dsp_lock, flags);


	PRINTK_1(TRACE_3780I, "3780i::dsp3780i_Run exit bRC=TRUE\n");

	return 0;
}


int dsp3780I_ReadDStore(unsigned short usDspBaseIO, void __user *pvBuffer,
                        unsigned uCount, unsigned long ulDSPAddr)
{
	unsigned short __user *pusBuffer = pvBuffer;
	unsigned short val;


	PRINTK_5(TRACE_3780I,
		"3780i::dsp3780I_ReadDStore entry usDspBaseIO %x, pusBuffer %p, uCount %x, ulDSPAddr %lx\n",
		usDspBaseIO, pusBuffer, uCount, ulDSPAddr);


	/* Set the initial MSA address. No adjustments need to be made to data store addresses */
	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_MsaAddrLow, (unsigned short) ulDSPAddr);
	OutWordDsp(DSP_MsaAddrHigh, (unsigned short) (ulDSPAddr >> 16));
	spin_unlock_irqrestore(&dsp_lock, flags);

	/* Transfer the memory block */
	while (uCount-- != 0) {
		spin_lock_irqsave(&dsp_lock, flags);
		val = InWordDsp(DSP_MsaDataDSISHigh);
		spin_unlock_irqrestore(&dsp_lock, flags);
		if(put_user(val, pusBuffer++))
			return -EFAULT;

		PRINTK_3(TRACE_3780I,
			"3780I::dsp3780I_ReadDStore uCount %x val %x\n",
			uCount, val);

		PaceMsaAccess(usDspBaseIO);
	}


	PRINTK_1(TRACE_3780I,
		"3780I::dsp3780I_ReadDStore exit bRC=TRUE\n");

	return 0;
}

int dsp3780I_ReadAndClearDStore(unsigned short usDspBaseIO,
                                void __user *pvBuffer, unsigned uCount,
                                unsigned long ulDSPAddr)
{
	unsigned short __user *pusBuffer = pvBuffer;
	unsigned short val;


	PRINTK_5(TRACE_3780I,
		"3780i::dsp3780I_ReadAndDStore entry usDspBaseIO %x, pusBuffer %p, uCount %x, ulDSPAddr %lx\n",
		usDspBaseIO, pusBuffer, uCount, ulDSPAddr);


	/* Set the initial MSA address. No adjustments need to be made to data store addresses */
	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_MsaAddrLow, (unsigned short) ulDSPAddr);
	OutWordDsp(DSP_MsaAddrHigh, (unsigned short) (ulDSPAddr >> 16));
	spin_unlock_irqrestore(&dsp_lock, flags);

	/* Transfer the memory block */
	while (uCount-- != 0) {
		spin_lock_irqsave(&dsp_lock, flags);
		val = InWordDsp(DSP_ReadAndClear);
		spin_unlock_irqrestore(&dsp_lock, flags);
		if(put_user(val, pusBuffer++))
			return -EFAULT;

		PRINTK_3(TRACE_3780I,
			"3780I::dsp3780I_ReadAndCleanDStore uCount %x val %x\n",
			uCount, val);

		PaceMsaAccess(usDspBaseIO);
	}


	PRINTK_1(TRACE_3780I,
		"3780I::dsp3780I_ReadAndClearDStore exit bRC=TRUE\n");

	return 0;
}


int dsp3780I_WriteDStore(unsigned short usDspBaseIO, void __user *pvBuffer,
                         unsigned uCount, unsigned long ulDSPAddr)
{
	unsigned short __user *pusBuffer = pvBuffer;


	PRINTK_5(TRACE_3780I,
		"3780i::dsp3780D_WriteDStore entry usDspBaseIO %x, pusBuffer %p, uCount %x, ulDSPAddr %lx\n",
		usDspBaseIO, pusBuffer, uCount, ulDSPAddr);


	/* Set the initial MSA address. No adjustments need to be made to data store addresses */
	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_MsaAddrLow, (unsigned short) ulDSPAddr);
	OutWordDsp(DSP_MsaAddrHigh, (unsigned short) (ulDSPAddr >> 16));
	spin_unlock_irqrestore(&dsp_lock, flags);

	/* Transfer the memory block */
	while (uCount-- != 0) {
		unsigned short val;
		if(get_user(val, pusBuffer++))
			return -EFAULT;
		spin_lock_irqsave(&dsp_lock, flags);
		OutWordDsp(DSP_MsaDataDSISHigh, val);
		spin_unlock_irqrestore(&dsp_lock, flags);

		PRINTK_3(TRACE_3780I,
			"3780I::dsp3780I_WriteDStore uCount %x val %x\n",
			uCount, val);

		PaceMsaAccess(usDspBaseIO);
	}


	PRINTK_1(TRACE_3780I,
		"3780I::dsp3780D_WriteDStore exit bRC=TRUE\n");

	return 0;
}


int dsp3780I_ReadIStore(unsigned short usDspBaseIO, void __user *pvBuffer,
                        unsigned uCount, unsigned long ulDSPAddr)
{
	unsigned short __user *pusBuffer = pvBuffer;

	PRINTK_5(TRACE_3780I,
		"3780i::dsp3780I_ReadIStore entry usDspBaseIO %x, pusBuffer %p, uCount %x, ulDSPAddr %lx\n",
		usDspBaseIO, pusBuffer, uCount, ulDSPAddr);

	/*
	* Set the initial MSA address. To convert from an instruction store
	* address to an MSA address
	* shift the address two bits to the left and set bit 22
	*/
	ulDSPAddr = (ulDSPAddr << 2) | (1 << 22);
	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_MsaAddrLow, (unsigned short) ulDSPAddr);
	OutWordDsp(DSP_MsaAddrHigh, (unsigned short) (ulDSPAddr >> 16));
	spin_unlock_irqrestore(&dsp_lock, flags);

	/* Transfer the memory block */
	while (uCount-- != 0) {
		unsigned short val_lo, val_hi;
		spin_lock_irqsave(&dsp_lock, flags);
		val_lo = InWordDsp(DSP_MsaDataISLow);
		val_hi = InWordDsp(DSP_MsaDataDSISHigh);
		spin_unlock_irqrestore(&dsp_lock, flags);
		if(put_user(val_lo, pusBuffer++))
			return -EFAULT;
		if(put_user(val_hi, pusBuffer++))
			return -EFAULT;

		PRINTK_4(TRACE_3780I,
			"3780I::dsp3780I_ReadIStore uCount %x val_lo %x val_hi %x\n",
			uCount, val_lo, val_hi);

		PaceMsaAccess(usDspBaseIO);

	}

	PRINTK_1(TRACE_3780I,
		"3780I::dsp3780I_ReadIStore exit bRC=TRUE\n");

	return 0;
}


int dsp3780I_WriteIStore(unsigned short usDspBaseIO, void __user *pvBuffer,
                         unsigned uCount, unsigned long ulDSPAddr)
{
	unsigned short __user *pusBuffer = pvBuffer;

	PRINTK_5(TRACE_3780I,
		"3780i::dsp3780I_WriteIStore entry usDspBaseIO %x, pusBuffer %p, uCount %x, ulDSPAddr %lx\n",
		usDspBaseIO, pusBuffer, uCount, ulDSPAddr);


	/*
	* Set the initial MSA address. To convert from an instruction store
	* address to an MSA address
	* shift the address two bits to the left and set bit 22
	*/
	ulDSPAddr = (ulDSPAddr << 2) | (1 << 22);
	spin_lock_irqsave(&dsp_lock, flags);
	OutWordDsp(DSP_MsaAddrLow, (unsigned short) ulDSPAddr);
	OutWordDsp(DSP_MsaAddrHigh, (unsigned short) (ulDSPAddr >> 16));
	spin_unlock_irqrestore(&dsp_lock, flags);

	/* Transfer the memory block */
	while (uCount-- != 0) {
		unsigned short val_lo, val_hi;
		if(get_user(val_lo, pusBuffer++))
			return -EFAULT;
		if(get_user(val_hi, pusBuffer++))
			return -EFAULT;
		spin_lock_irqsave(&dsp_lock, flags);
		OutWordDsp(DSP_MsaDataISLow, val_lo);
		OutWordDsp(DSP_MsaDataDSISHigh, val_hi);
		spin_unlock_irqrestore(&dsp_lock, flags);

		PRINTK_4(TRACE_3780I,
			"3780I::dsp3780I_WriteIStore uCount %x val_lo %x val_hi %x\n",
			uCount, val_lo, val_hi);

		PaceMsaAccess(usDspBaseIO);

	}

	PRINTK_1(TRACE_3780I,
		"3780I::dsp3780I_WriteIStore exit bRC=TRUE\n");

	return 0;
}


int dsp3780I_GetIPCSource(unsigned short usDspBaseIO,
                          unsigned short *pusIPCSource)
{
	DSP_HBRIDGE_CONTROL rHBridgeControl;
	unsigned short temp;


	PRINTK_3(TRACE_3780I,
		"3780i::dsp3780I_GetIPCSource entry usDspBaseIO %x pusIPCSource %p\n",
		usDspBaseIO, pusIPCSource);

	/*
	* Disable DSP to PC interrupts, read the interrupt register,
	* clear the pending IPC bits, and reenable DSP to PC interrupts
	*/
	spin_lock_irqsave(&dsp_lock, flags);
	MKWORD(rHBridgeControl) = InWordDsp(DSP_HBridgeControl);
	rHBridgeControl.EnableDspInt = FALSE;
	OutWordDsp(DSP_HBridgeControl, MKWORD(rHBridgeControl));

	*pusIPCSource = InWordDsp(DSP_Interrupt);
	temp = (unsigned short) ~(*pusIPCSource);

	PRINTK_3(TRACE_3780I,
		"3780i::dsp3780I_GetIPCSource, usIPCSource %x ~ %x\n",
		*pusIPCSource, temp);

	OutWordDsp(DSP_Interrupt, (unsigned short) ~(*pusIPCSource));

	rHBridgeControl.EnableDspInt = TRUE;
	OutWordDsp(DSP_HBridgeControl, MKWORD(rHBridgeControl));
	spin_unlock_irqrestore(&dsp_lock, flags);


	PRINTK_2(TRACE_3780I,
		"3780i::dsp3780I_GetIPCSource exit usIPCSource %x\n",
		*pusIPCSource);

	return 0;
}
