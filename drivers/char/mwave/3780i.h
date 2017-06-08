/*
*
* 3780i.h -- declarations for 3780i.c
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

#ifndef _LINUX_3780I_H
#define _LINUX_3780I_H

#include <asm/io.h>

/* DSP I/O port offsets and definitions */
#define DSP_IsaSlaveControl        0x0000	/* ISA slave control register */
#define DSP_IsaSlaveStatus         0x0001	/* ISA slave status register */
#define DSP_ConfigAddress          0x0002	/* General config address register */
#define DSP_ConfigData             0x0003	/* General config data register */
#define DSP_HBridgeControl         0x0002	/* HBridge control register */
#define DSP_MsaAddrLow             0x0004	/* MSP System Address, low word */
#define DSP_MsaAddrHigh            0x0006	/* MSP System Address, high word */
#define DSP_MsaDataDSISHigh        0x0008	/* MSA data register: d-store word or high byte of i-store */
#define DSP_MsaDataISLow           0x000A	/* MSA data register: low word of i-store */
#define DSP_ReadAndClear           0x000C	/* MSA read and clear data register */
#define DSP_Interrupt              0x000E	/* Interrupt register (IPC source) */

typedef struct {
	unsigned char ClockControl:1;	/* RW: Clock control: 0=normal, 1=stop 3780i clocks */
	unsigned char SoftReset:1;	/* RW: Soft reset 0=normal, 1=soft reset active */
	unsigned char ConfigMode:1;	/* RW: Configuration mode, 0=normal, 1=config mode */
	unsigned char Reserved:5;	/* 0: Reserved */
} DSP_ISA_SLAVE_CONTROL;


typedef struct {
	unsigned short EnableDspInt:1;	/* RW: Enable DSP to X86 ISA interrupt 0=mask it, 1=enable it */
	unsigned short MemAutoInc:1;	/* RW: Memory address auto increment, 0=disable, 1=enable */
	unsigned short IoAutoInc:1;	/* RW: I/O address auto increment, 0=disable, 1=enable */
	unsigned short DiagnosticMode:1;	/* RW: Disgnostic mode 0=nromal, 1=diagnostic mode */
	unsigned short IsaPacingTimer:12;	/* R: ISA access pacing timer: count of core cycles stolen */
} DSP_HBRIDGE_CONTROL;


/* DSP register indexes used with the configuration register address (index) register */
#define DSP_UartCfg1Index          0x0003	/* UART config register 1 */
#define DSP_UartCfg2Index          0x0004	/* UART config register 2 */
#define DSP_HBridgeCfg1Index       0x0007	/* HBridge config register 1 */
#define DSP_HBridgeCfg2Index       0x0008	/* HBridge config register 2 */
#define DSP_BusMasterCfg1Index     0x0009	/* ISA bus master config register 1 */
#define DSP_BusMasterCfg2Index     0x000A	/* ISA bus master config register 2 */
#define DSP_IsaProtCfgIndex        0x000F	/* ISA protocol control register */
#define DSP_PowerMgCfgIndex        0x0010	/* Low poser suspend/resume enable */
#define DSP_HBusTimerCfgIndex      0x0011	/* HBUS timer load value */

typedef struct {
	unsigned char IrqActiveLow:1;	/* RW: IRQ active high or low: 0=high, 1=low */
	unsigned char IrqPulse:1;	/* RW: IRQ pulse or level: 0=level, 1=pulse  */
	unsigned char Irq:3;	/* RW: IRQ selection */
	unsigned char BaseIO:2;	/* RW: Base I/O selection */
	unsigned char Reserved:1;	/* 0: Reserved */
} DSP_UART_CFG_1;

typedef struct {
	unsigned char Enable:1;	/* RW: Enable I/O and IRQ: 0=false, 1=true */
	unsigned char Reserved:7;	/* 0: Reserved */
} DSP_UART_CFG_2;

typedef struct {
	unsigned char IrqActiveLow:1;	/* RW: IRQ active high=0 or low=1 */
	unsigned char IrqPulse:1;	/* RW: IRQ pulse=1 or level=0 */
	unsigned char Irq:3;	/* RW: IRQ selection */
	unsigned char AccessMode:1;	/* RW: 16-bit register access method 0=byte, 1=word */
	unsigned char Reserved:2;	/* 0: Reserved */
} DSP_HBRIDGE_CFG_1;

typedef struct {
	unsigned char Enable:1;	/* RW: enable I/O and IRQ: 0=false, 1=true */
	unsigned char Reserved:7;	/* 0: Reserved */
} DSP_HBRIDGE_CFG_2;


typedef struct {
	unsigned char Dma:3;	/* RW: DMA channel selection */
	unsigned char NumTransfers:2;	/* RW: Maximum # of transfers once being granted the ISA bus */
	unsigned char ReRequest:2;	/* RW: Minimum delay between releasing the ISA bus and requesting it again */
	unsigned char MEMCS16:1;	/* RW: ISA signal MEMCS16: 0=disabled, 1=enabled */
} DSP_BUSMASTER_CFG_1;

typedef struct {
	unsigned char IsaMemCmdWidth:2;	/* RW: ISA memory command width */
	unsigned char Reserved:6;	/* 0: Reserved */
} DSP_BUSMASTER_CFG_2;


typedef struct {
	unsigned char GateIOCHRDY:1;	/* RW: Enable IOCHRDY gating: 0=false, 1=true */
	unsigned char Reserved:7;	/* 0: Reserved */
} DSP_ISA_PROT_CFG;

typedef struct {
	unsigned char Enable:1;	/* RW: Enable low power suspend/resume 0=false, 1=true */
	unsigned char Reserved:7;	/* 0: Reserved */
} DSP_POWER_MGMT_CFG;

typedef struct {
	unsigned char LoadValue:8;	/* RW: HBUS timer load value */
} DSP_HBUS_TIMER_CFG;



/* DSP registers that exist in MSA I/O space */
#define DSP_ChipID                 0x80000000
#define DSP_MspBootDomain          0x80000580
#define DSP_LBusTimeoutDisable     0x80000580
#define DSP_ClockControl_1         0x8000058A
#define DSP_ClockControl_2         0x8000058C
#define DSP_ChipReset              0x80000588
#define DSP_GpioModeControl_15_8   0x80000082
#define DSP_GpioDriverEnable_15_8  0x80000076
#define DSP_GpioOutputData_15_8    0x80000072

typedef struct {
	unsigned short NMI:1;	/* RW: non maskable interrupt */
	unsigned short Halt:1;	/* RW: Halt MSP clock */
	unsigned short ResetCore:1;	/* RW: Reset MSP core interface */
	unsigned short Reserved:13;	/* 0: Reserved */
} DSP_BOOT_DOMAIN;

typedef struct {
	unsigned short DisableTimeout:1;	/* RW: Disable LBus timeout */
	unsigned short Reserved:15;	/* 0: Reserved */
} DSP_LBUS_TIMEOUT_DISABLE;

typedef struct {
	unsigned short Memory:1;	/* RW: Reset memory interface */
	unsigned short SerialPort1:1;	/* RW: Reset serial port 1 interface */
	unsigned short SerialPort2:1;	/* RW: Reset serial port 2 interface */
	unsigned short SerialPort3:1;	/* RW: Reset serial port 3 interface */
	unsigned short Gpio:1;	/* RW: Reset GPIO interface */
	unsigned short Dma:1;	/* RW: Reset DMA interface */
	unsigned short SoundBlaster:1;	/* RW: Reset soundblaster interface */
	unsigned short Uart:1;	/* RW: Reset UART interface */
	unsigned short Midi:1;	/* RW: Reset MIDI interface */
	unsigned short IsaMaster:1;	/* RW: Reset ISA master interface */
	unsigned short Reserved:6;	/* 0: Reserved */
} DSP_CHIP_RESET;

typedef struct {
	unsigned short N_Divisor:6;	/* RW: (N) PLL output clock divisor */
	unsigned short Reserved1:2;	/* 0: reserved */
	unsigned short M_Multiplier:6;	/* RW: (M) PLL feedback clock multiplier */
	unsigned short Reserved2:2;	/* 0: reserved */
} DSP_CLOCK_CONTROL_1;

typedef struct {
	unsigned short PllBypass:1;	/* RW: PLL Bypass */
	unsigned short Reserved:15;	/* 0: Reserved */
} DSP_CLOCK_CONTROL_2;

typedef struct {
	unsigned short Latch8:1;
	unsigned short Latch9:1;
	unsigned short Latch10:1;
	unsigned short Latch11:1;
	unsigned short Latch12:1;
	unsigned short Latch13:1;
	unsigned short Latch14:1;
	unsigned short Latch15:1;
	unsigned short Mask8:1;
	unsigned short Mask9:1;
	unsigned short Mask10:1;
	unsigned short Mask11:1;
	unsigned short Mask12:1;
	unsigned short Mask13:1;
	unsigned short Mask14:1;
	unsigned short Mask15:1;
} DSP_GPIO_OUTPUT_DATA_15_8;

typedef struct {
	unsigned short Enable8:1;
	unsigned short Enable9:1;
	unsigned short Enable10:1;
	unsigned short Enable11:1;
	unsigned short Enable12:1;
	unsigned short Enable13:1;
	unsigned short Enable14:1;
	unsigned short Enable15:1;
	unsigned short Mask8:1;
	unsigned short Mask9:1;
	unsigned short Mask10:1;
	unsigned short Mask11:1;
	unsigned short Mask12:1;
	unsigned short Mask13:1;
	unsigned short Mask14:1;
	unsigned short Mask15:1;
} DSP_GPIO_DRIVER_ENABLE_15_8;

typedef struct {
	unsigned short GpioMode8:2;
	unsigned short GpioMode9:2;
	unsigned short GpioMode10:2;
	unsigned short GpioMode11:2;
	unsigned short GpioMode12:2;
	unsigned short GpioMode13:2;
	unsigned short GpioMode14:2;
	unsigned short GpioMode15:2;
} DSP_GPIO_MODE_15_8;

/* Component masks that are defined in dspmgr.h */
#define MW_ADC_MASK    0x0001
#define MW_AIC2_MASK   0x0006
#define MW_MIDI_MASK   0x0008
#define MW_CDDAC_MASK  0x8001
#define MW_AIC1_MASK   0xE006
#define MW_UART_MASK   0xE00A
#define MW_ACI_MASK    0xE00B

/*
* Definition of 3780i configuration structure.  Unless otherwise stated,
* these values are provided as input to the 3780i support layer.  At present,
* the only values maintained by the 3780i support layer are the saved UART
* registers.
*/
typedef struct _DSP_3780I_CONFIG_SETTINGS {

	/* Location of base configuration register */
	unsigned short usBaseConfigIO;

	/* Enables for various DSP components */
	int bDSPEnabled;
	int bModemEnabled;
	int bInterruptClaimed;

	/* IRQ, DMA, and Base I/O addresses for various DSP components */
	unsigned short usDspIrq;
	unsigned short usDspDma;
	unsigned short usDspBaseIO;
	unsigned short usUartIrq;
	unsigned short usUartBaseIO;

	/* IRQ modes for various DSP components */
	int bDspIrqActiveLow;
	int bUartIrqActiveLow;
	int bDspIrqPulse;
	int bUartIrqPulse;

	/* Card abilities */
	unsigned uIps;
	unsigned uDStoreSize;
	unsigned uIStoreSize;
	unsigned uDmaBandwidth;

	/* Adapter specific 3780i settings */
	unsigned short usNumTransfers;
	unsigned short usReRequest;
	int bEnableMEMCS16;
	unsigned short usIsaMemCmdWidth;
	int bGateIOCHRDY;
	int bEnablePwrMgmt;
	unsigned short usHBusTimerLoadValue;
	int bDisableLBusTimeout;
	unsigned short usN_Divisor;
	unsigned short usM_Multiplier;
	int bPllBypass;
	unsigned short usChipletEnable;	/* Used with the chip reset register to enable specific chiplets */

	/* Saved UART registers. These are maintained by the 3780i support layer. */
	int bUartSaved;		/* True after a successful save of the UART registers */
	unsigned char ucIER;	/* Interrupt enable register */
	unsigned char ucFCR;	/* FIFO control register */
	unsigned char ucLCR;	/* Line control register */
	unsigned char ucMCR;	/* Modem control register */
	unsigned char ucSCR;	/* Scratch register */
	unsigned char ucDLL;	/* Divisor latch, low byte */
	unsigned char ucDLM;	/* Divisor latch, high byte */
} DSP_3780I_CONFIG_SETTINGS;


/* 3780i support functions */
int dsp3780I_EnableDSP(DSP_3780I_CONFIG_SETTINGS * pSettings,
                       unsigned short *pIrqMap,
                       unsigned short *pDmaMap);
int dsp3780I_DisableDSP(DSP_3780I_CONFIG_SETTINGS * pSettings);
int dsp3780I_Reset(DSP_3780I_CONFIG_SETTINGS * pSettings);
int dsp3780I_Run(DSP_3780I_CONFIG_SETTINGS * pSettings);
int dsp3780I_ReadDStore(unsigned short usDspBaseIO, void __user *pvBuffer,
                        unsigned uCount, unsigned long ulDSPAddr);
int dsp3780I_ReadAndClearDStore(unsigned short usDspBaseIO,
                                void __user *pvBuffer, unsigned uCount,
                                unsigned long ulDSPAddr);
int dsp3780I_WriteDStore(unsigned short usDspBaseIO, void __user *pvBuffer,
                         unsigned uCount, unsigned long ulDSPAddr);
int dsp3780I_ReadIStore(unsigned short usDspBaseIO, void __user *pvBuffer,
                        unsigned uCount, unsigned long ulDSPAddr);
int dsp3780I_WriteIStore(unsigned short usDspBaseIO, void __user *pvBuffer,
                         unsigned uCount, unsigned long ulDSPAddr);
unsigned short dsp3780I_ReadMsaCfg(unsigned short usDspBaseIO,
                                   unsigned long ulMsaAddr);
void dsp3780I_WriteMsaCfg(unsigned short usDspBaseIO,
                          unsigned long ulMsaAddr, unsigned short usValue);
int dsp3780I_GetIPCSource(unsigned short usDspBaseIO,
                          unsigned short *pusIPCSource);

/* I/O port access macros */
#define MKWORD(var) (*((unsigned short *)(&var)))
#define MKBYTE(var) (*((unsigned char *)(&var)))

#define WriteMsaCfg(addr,value) dsp3780I_WriteMsaCfg(usDspBaseIO,addr,value)
#define ReadMsaCfg(addr) dsp3780I_ReadMsaCfg(usDspBaseIO,addr)
#define WriteGenCfg(index,value) dsp3780I_WriteGenCfg(usDspBaseIO,index,value)
#define ReadGenCfg(index) dsp3780I_ReadGenCfg(usDspBaseIO,index)

#define InWordDsp(index)          inw(usDspBaseIO+index)
#define InByteDsp(index)          inb(usDspBaseIO+index)
#define OutWordDsp(index,value)   outw(value,usDspBaseIO+index)
#define OutByteDsp(index,value)   outb(value,usDspBaseIO+index)

#endif
