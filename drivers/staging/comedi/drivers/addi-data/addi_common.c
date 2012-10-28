/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

	ADDI-DATA GmbH
	Dieselstrasse 3
	D-77833 Ottersweier
	Tel: +19(0)7223/9493-0
	Fax: +49(0)7223/9493-92
	http://www.addi-data.com
	info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You should also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/
/*

  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstrasse 3      D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project   : ADDI DATA         | Compiler : GCC 		          |
  | Modulname : addi_common.c     | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Author    :           | Date     :                    		  |
  +-----------------------------------------------------------------------+
  | Description : ADDI COMMON Main Module                                 |
  +-----------------------------------------------------------------------+
  | CONFIG OPTIONS                                                        |
  |	option[0] - PCI bus number - if bus number and slot number are 0, |
  |			         then driver search for first unused card |
  |	option[1] - PCI slot number                                       |
  |							                  |
  |	option[2] = 0  - DMA ENABLE                                       |
  |               = 1  - DMA DISABLE                                      |
  +----------+-----------+------------------------------------------------+
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include "../../comedidev.h"
#if defined(CONFIG_APCI_1710) || defined(CONFIG_APCI_3200) || defined(CONFIG_APCI_3300)
#include <asm/i387.h>
#endif
#include "../comedi_fc.h"

#include "addi_common.h"
#include "addi_amcc_s5933.h"

#ifndef ADDIDATA_DRIVER_NAME
#define ADDIDATA_DRIVER_NAME	"addi_common"
#endif

/* Update-0.7.57->0.7.68MODULE_AUTHOR("ADDI-DATA GmbH <info@addi-data.com>"); */
/* Update-0.7.57->0.7.68MODULE_DESCRIPTION("Comedi ADDI-DATA module"); */
/* Update-0.7.57->0.7.68MODULE_LICENSE("GPL"); */

#define devpriv ((struct addi_private *)dev->private)
#define this_board ((const struct addi_board *)dev->board_ptr)

#if defined(CONFIG_APCI_1710) || defined(CONFIG_APCI_3200) || defined(CONFIG_APCI_3300)
/* BYTE b_SaveFPUReg [94]; */

void fpu_begin(void)
{
	/* asm ("fstenv b_SaveFPUReg"); */
	kernel_fpu_begin();
}

void fpu_end(void)
{
	/*  asm ("frstor b_SaveFPUReg"); */
	kernel_fpu_end();
}
#endif

#include "addi_eeprom.c"
#if (defined (CONFIG_APCI_3120) || defined (CONFIG_APCI_3001))
#include "hwdrv_apci3120.c"
#endif
#ifdef CONFIG_APCI_1032
#include "hwdrv_apci1032.c"
#endif
#ifdef CONFIG_APCI_1516
#include "hwdrv_apci1516.c"
#endif
#ifdef CONFIG_APCI_2016
#include "hwdrv_apci2016.c"
#endif
#ifdef CONFIG_APCI_2032
#include "hwdrv_apci2032.c"
#endif
#ifdef CONFIG_APCI_2200
#include "hwdrv_apci2200.c"
#endif
#ifdef CONFIG_APCI_1564
#include "hwdrv_apci1564.c"
#endif
#ifdef CONFIG_APCI_1500
#include "hwdrv_apci1500.c"
#endif
#ifdef CONFIG_APCI_3501
#include "hwdrv_apci3501.c"
#endif
#ifdef CONFIG_APCI_035
#include "hwdrv_apci035.c"
#endif
#if (defined (CONFIG_APCI_3200) || defined (CONFIG_APCI_3300))
#include "hwdrv_apci3200.c"
#endif
#ifdef CONFIG_APCI_1710
#include "hwdrv_APCI1710.c"
#endif
#ifdef CONFIG_APCI_16XX
#include "hwdrv_apci16xx.c"
#endif
#ifdef CONFIG_APCI_3XXX
#include "hwdrv_apci3xxx.c"
#endif

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
#ifdef CONFIG_APCI_3120
	{PCI_DEVICE(APCI3120_BOARD_VENDOR_ID, 0x818D)},
#endif
#ifdef CONFIG_APCI_1032
	{PCI_DEVICE(APCI1032_BOARD_VENDOR_ID, 0x1003)},
#endif
#ifdef CONFIG_APCI_1516
	{PCI_DEVICE(APCI1516_BOARD_VENDOR_ID, 0x1001)},
#endif
#ifdef CONFIG_APCI_2016
	{PCI_DEVICE(APCI2016_BOARD_VENDOR_ID, 0x1002)},
#endif
#ifdef CONFIG_APCI_2032
	{PCI_DEVICE(APCI2032_BOARD_VENDOR_ID, 0x1004)},
#endif
#ifdef CONFIG_APCI_2200
	{PCI_DEVICE(APCI2200_BOARD_VENDOR_ID, 0x1005)},
#endif
#ifdef CONFIG_APCI_1564
	{PCI_DEVICE(APCI1564_BOARD_VENDOR_ID, 0x1006)},
#endif
#ifdef CONFIG_APCI_1500
	{PCI_DEVICE(APCI1500_BOARD_VENDOR_ID, 0x80fc)},
#endif
#ifdef CONFIG_APCI_3001
	{PCI_DEVICE(APCI3120_BOARD_VENDOR_ID, 0x828D)},
#endif
#ifdef CONFIG_APCI_3501
	{PCI_DEVICE(APCI3501_BOARD_VENDOR_ID, 0x3001)},
#endif
#ifdef CONFIG_APCI_035
	{PCI_DEVICE(APCI035_BOARD_VENDOR_ID,  0x0300)},
#endif
#ifdef CONFIG_APCI_3200
	{PCI_DEVICE(APCI3200_BOARD_VENDOR_ID, 0x3000)},
#endif
#ifdef CONFIG_APCI_3300
	{PCI_DEVICE(APCI3200_BOARD_VENDOR_ID, 0x3007)},
#endif
#ifdef CONFIG_APCI_1710
	{PCI_DEVICE(APCI1710_BOARD_VENDOR_ID, APCI1710_BOARD_DEVICE_ID)},
#endif
#ifdef CONFIG_APCI_16XX
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1009)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x100A)},
#endif
#ifdef CONFIG_APCI_3XXX
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3010)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x300F)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x300E)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3013)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3014)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3015)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3016)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3017)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3018)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3019)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x301A)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x301B)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x301C)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x301D)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x301E)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x301F)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3020)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3021)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3022)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3023)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x300B)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3002)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3003)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3004)},
	{PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3024)},
#endif
	{0}
};

MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

static const struct addi_board boardtypes[] = {
#ifdef CONFIG_APCI_3120
	{
		.pc_DriverName		= "apci3120",
		.i_VendorId		= APCI3120_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x818D,
		.i_IorangeBase0		= AMCC_OP_REG_SIZE,
		.i_IorangeBase1		= APCI3120_ADDRESS_RANGE,
		.i_IorangeBase2		= 8,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 8,
		.i_AiMaxdata		= 0xffff,
		.i_AoMaxdata		= 0x3fff,
		.pr_AiRangelist		= &range_apci3120_ai,
		.pr_AoRangelist		= &range_apci3120_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 0x0f,
		.i_Dma			= 1,
		.i_Timer		= 1,
		.b_AvailableConvertUnit	= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI3120_Interrupt,
		.reset			= i_APCI3120_Reset,
		.ai_config		= i_APCI3120_InsnConfigAnalogInput,
		.ai_read		= i_APCI3120_InsnReadAnalogInput,
		.ai_cmdtest		= i_APCI3120_CommandTestAnalogInput,
		.ai_cmd			= i_APCI3120_CommandAnalogInput,
		.ai_cancel		= i_APCI3120_StopCyclicAcquisition,
		.ao_write		= i_APCI3120_InsnWriteAnalogOutput,
		.di_read		= i_APCI3120_InsnReadDigitalInput,
		.di_bits		= i_APCI3120_InsnBitsDigitalInput,
		.do_config		= i_APCI3120_InsnConfigDigitalOutput,
		.do_write		= i_APCI3120_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3120_InsnBitsDigitalOutput,
		.timer_config		= i_APCI3120_InsnConfigTimer,
		.timer_write		= i_APCI3120_InsnWriteTimer,
		.timer_read		= i_APCI3120_InsnReadTimer,
	},
#endif
#ifdef CONFIG_APCI_1032
	{
		.pc_DriverName		= "apci1032",
		.i_VendorId		= APCI1032_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x1003,
		.i_IorangeBase0		= 4,
		.i_IorangeBase1		= APCI1032_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDiChannel		= 32,
		.interrupt		= v_APCI1032_Interrupt,
		.reset			= i_APCI1032_Reset,
		.di_config		= i_APCI1032_ConfigDigitalInput,
		.di_read		= i_APCI1032_Read1DigitalInput,
		.di_bits		= i_APCI1032_ReadMoreDigitalInput,
	},
#endif
#ifdef CONFIG_APCI_1516
	{
		.pc_DriverName		= "apci1516",
		.i_VendorId		= APCI1516_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x1001,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= APCI1516_ADDRESS_RANGE,
		.i_IorangeBase2		= 32,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrDiChannel		= 8,
		.i_NbrDoChannel		= 8,
		.i_Timer		= 1,
		.reset			= i_APCI1516_Reset,
		.di_read		= i_APCI1516_Read1DigitalInput,
		.di_bits		= i_APCI1516_ReadMoreDigitalInput,
		.do_config		= i_APCI1516_ConfigDigitalOutput,
		.do_write		= i_APCI1516_WriteDigitalOutput,
		.do_bits		= i_APCI1516_ReadDigitalOutput,
		.timer_config		= i_APCI1516_ConfigWatchdog,
		.timer_write		= i_APCI1516_StartStopWriteWatchdog,
		.timer_read		= i_APCI1516_ReadWatchdog,
	},
#endif
#ifdef CONFIG_APCI_2016
	{
		.pc_DriverName		= "apci2016",
		.i_VendorId		= APCI2016_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x1002,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= APCI2016_ADDRESS_RANGE,
		.i_IorangeBase2		= 32,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrDoChannel		= 16,
		.i_Timer		= 1,
		.reset			= i_APCI2016_Reset,
		.do_config		= i_APCI2016_ConfigDigitalOutput,
		.do_write		= i_APCI2016_WriteDigitalOutput,
		.do_bits		= i_APCI2016_BitsDigitalOutput,
		.timer_config		= i_APCI2016_ConfigWatchdog,
		.timer_write		= i_APCI2016_StartStopWriteWatchdog,
		.timer_read		= i_APCI2016_ReadWatchdog,
	},
#endif
#ifdef CONFIG_APCI_2032
	{
		.pc_DriverName		= "apci2032",
		.i_VendorId		= APCI2032_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x1004,
		.i_IorangeBase0		= 4,
		.i_IorangeBase1		= APCI2032_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDoChannel		= 32,
		.i_DoMaxdata		= 0xffffffff,
		.i_Timer		= 1,
		.interrupt		= v_APCI2032_Interrupt,
		.reset			= i_APCI2032_Reset,
		.do_config		= i_APCI2032_ConfigDigitalOutput,
		.do_write		= i_APCI2032_WriteDigitalOutput,
		.do_bits		= i_APCI2032_ReadDigitalOutput,
		.do_read		= i_APCI2032_ReadInterruptStatus,
		.timer_config		= i_APCI2032_ConfigWatchdog,
		.timer_write		= i_APCI2032_StartStopWriteWatchdog,
		.timer_read		= i_APCI2032_ReadWatchdog,
	},
#endif
#ifdef CONFIG_APCI_2200
	{
		.pc_DriverName		= "apci2200",
		.i_VendorId		= APCI2200_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x1005,
		.i_IorangeBase0		= 4,
		.i_IorangeBase1		= APCI2200_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDiChannel		= 8,
		.i_NbrDoChannel		= 16,
		.i_Timer		= 1,
		.reset			= i_APCI2200_Reset,
		.di_read		= i_APCI2200_Read1DigitalInput,
		.di_bits		= i_APCI2200_ReadMoreDigitalInput,
		.do_config		= i_APCI2200_ConfigDigitalOutput,
		.do_write		= i_APCI2200_WriteDigitalOutput,
		.do_bits		= i_APCI2200_ReadDigitalOutput,
		.timer_config		= i_APCI2200_ConfigWatchdog,
		.timer_write		= i_APCI2200_StartStopWriteWatchdog,
		.timer_read		= i_APCI2200_ReadWatchdog,
	},
#endif
#ifdef CONFIG_APCI_1564
	{
		.pc_DriverName		= "apci1564",
		.i_VendorId		= APCI1564_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x1006,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= APCI1564_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDiChannel		= 32,
		.i_NbrDoChannel		= 32,
		.i_DoMaxdata		= 0xffffffff,
		.i_Timer		= 1,
		.interrupt		= v_APCI1564_Interrupt,
		.reset			= i_APCI1564_Reset,
		.di_config		= i_APCI1564_ConfigDigitalInput,
		.di_read		= i_APCI1564_Read1DigitalInput,
		.di_bits		= i_APCI1564_ReadMoreDigitalInput,
		.do_config		= i_APCI1564_ConfigDigitalOutput,
		.do_write		= i_APCI1564_WriteDigitalOutput,
		.do_bits		= i_APCI1564_ReadDigitalOutput,
		.do_read		= i_APCI1564_ReadInterruptStatus,
		.timer_config		= i_APCI1564_ConfigTimerCounterWatchdog,
		.timer_write		= i_APCI1564_StartStopWriteTimerCounterWatchdog,
		.timer_read		= i_APCI1564_ReadTimerCounterWatchdog,
	},
#endif
#ifdef CONFIG_APCI_1500
	{
		.pc_DriverName		= "apci1500",
		.i_VendorId		= APCI1500_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x80fc,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= APCI1500_ADDRESS_RANGE,
		.i_IorangeBase2		= 4,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrDiChannel		= 16,
		.i_NbrDoChannel		= 16,
		.i_DoMaxdata		= 0xffff,
		.i_Timer		= 1,
		.interrupt		= v_APCI1500_Interrupt,
		.reset			= i_APCI1500_Reset,
		.di_config		= i_APCI1500_ConfigDigitalInputEvent,
		.di_read		= i_APCI1500_Initialisation,
		.di_write		= i_APCI1500_StartStopInputEvent,
		.di_bits		= i_APCI1500_ReadMoreDigitalInput,
		.do_config		= i_APCI1500_ConfigDigitalOutputErrorInterrupt,
		.do_write		= i_APCI1500_WriteDigitalOutput,
		.do_bits		= i_APCI1500_ConfigureInterrupt,
		.timer_config		= i_APCI1500_ConfigCounterTimerWatchdog,
		.timer_write		= i_APCI1500_StartStopTriggerTimerCounterWatchdog,
		.timer_read		= i_APCI1500_ReadInterruptMask,
		.timer_bits		= i_APCI1500_ReadCounterTimerWatchdog,
	},
#endif
#ifdef CONFIG_APCI_3001
	{
		.pc_DriverName		= "apci3001",
		.i_VendorId		= APCI3120_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x828D,
		.i_IorangeBase0		= AMCC_OP_REG_SIZE,
		.i_IorangeBase1		= APCI3120_ADDRESS_RANGE,
		.i_IorangeBase2		= 8,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xfff,
		.pr_AiRangelist		= &range_apci3120_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 0x0f,
		.i_Dma			= 1,
		.i_Timer		= 1,
		.b_AvailableConvertUnit	= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI3120_Interrupt,
		.reset			= i_APCI3120_Reset,
		.ai_config		= i_APCI3120_InsnConfigAnalogInput,
		.ai_read		= i_APCI3120_InsnReadAnalogInput,
		.ai_cmdtest		= i_APCI3120_CommandTestAnalogInput,
		.ai_cmd			= i_APCI3120_CommandAnalogInput,
		.ai_cancel		= i_APCI3120_StopCyclicAcquisition,
		.di_read		= i_APCI3120_InsnReadDigitalInput,
		.di_bits		= i_APCI3120_InsnBitsDigitalInput,
		.do_config		= i_APCI3120_InsnConfigDigitalOutput,
		.do_write		= i_APCI3120_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3120_InsnBitsDigitalOutput,
		.timer_config		= i_APCI3120_InsnConfigTimer,
		.timer_write		= i_APCI3120_InsnWriteTimer,
		.timer_read		= i_APCI3120_InsnReadTimer,
	},
#endif
#ifdef CONFIG_APCI_3501
	{
		.pc_DriverName		= "apci3501",
		.i_VendorId		= APCI3501_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x3001,
		.i_IorangeBase0		= 64,
		.i_IorangeBase1		= APCI3501_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5933,
		.i_AoMaxdata		= 16383,
		.pr_AoRangelist		= &range_apci3501_ao,
		.i_NbrDiChannel		= 2,
		.i_NbrDoChannel		= 2,
		.i_DoMaxdata		= 0x3,
		.i_Timer		= 1,
		.interrupt		= v_APCI3501_Interrupt,
		.reset			= i_APCI3501_Reset,
		.ao_config		= i_APCI3501_ConfigAnalogOutput,
		.ao_write		= i_APCI3501_WriteAnalogOutput,
		.di_bits		= i_APCI3501_ReadDigitalInput,
		.do_config		= i_APCI3501_ConfigDigitalOutput,
		.do_write		= i_APCI3501_WriteDigitalOutput,
		.do_bits		= i_APCI3501_ReadDigitalOutput,
		.timer_config		= i_APCI3501_ConfigTimerCounterWatchdog,
		.timer_write		= i_APCI3501_StartStopWriteTimerCounterWatchdog,
		.timer_read		= i_APCI3501_ReadTimerCounterWatchdog,
	},
#endif
#ifdef CONFIG_APCI_035
	{
		.pc_DriverName		= "apci035",
		.i_VendorId		= APCI035_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x0300,
		.i_IorangeBase0		= 127,
		.i_IorangeBase1		= APCI035_ADDRESS_RANGE,
		.i_PCIEeprom		= 1,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xff,
		.pr_AiRangelist		= &range_apci035_ai,
		.i_Timer		= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI035_Interrupt,
		.reset			= i_APCI035_Reset,
		.ai_config		= i_APCI035_ConfigAnalogInput,
		.ai_read		= i_APCI035_ReadAnalogInput,
		.timer_config		= i_APCI035_ConfigTimerWatchdog,
		.timer_write		= i_APCI035_StartStopWriteTimerWatchdog,
		.timer_read		= i_APCI035_ReadTimerWatchdog,
	},
#endif
#ifdef CONFIG_APCI_3200
	{
		.pc_DriverName		= "apci3200",
		.i_VendorId		= APCI3200_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x3000,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 4,
		.i_IorangeBase3		= 4,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0x3ffff,
		.pr_AiRangelist		= &range_apci3200_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI3200_Interrupt,
		.reset			= i_APCI3200_Reset,
		.ai_config		= i_APCI3200_ConfigAnalogInput,
		.ai_read		= i_APCI3200_ReadAnalogInput,
		.ai_write		= i_APCI3200_InsnWriteReleaseAnalogInput,
		.ai_bits		= i_APCI3200_InsnBits_AnalogInput_Test,
		.ai_cmdtest		= i_APCI3200_CommandTestAnalogInput,
		.ai_cmd			= i_APCI3200_CommandAnalogInput,
		.ai_cancel		= i_APCI3200_StopCyclicAcquisition,
		.di_bits		= i_APCI3200_ReadDigitalInput,
		.do_config		= i_APCI3200_ConfigDigitalOutput,
		.do_write		= i_APCI3200_WriteDigitalOutput,
		.do_bits		= i_APCI3200_ReadDigitalOutput,
	},
#endif
#ifdef CONFIG_APCI_3300
	/* Begin JK	.20.10.2004 = APCI-3300 integration */
	{
		.pc_DriverName		= "apci3300",
		.i_VendorId		= APCI3200_BOARD_VENDOR_ID,
		.i_DeviceId		= 0x3007,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 4,
		.i_IorangeBase3		= 4,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 0x3ffff,
		.pr_AiRangelist		= &range_apci3300_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI3200_Interrupt,
		.reset			= i_APCI3200_Reset,
		.ai_config		= i_APCI3200_ConfigAnalogInput,
		.ai_read		= i_APCI3200_ReadAnalogInput,
		.ai_write		= i_APCI3200_InsnWriteReleaseAnalogInput,
		.ai_bits		= i_APCI3200_InsnBits_AnalogInput_Test,
		.ai_cmdtest		= i_APCI3200_CommandTestAnalogInput,
		.ai_cmd			= i_APCI3200_CommandAnalogInput,
		.ai_cancel		= i_APCI3200_StopCyclicAcquisition,
		.di_bits		= i_APCI3200_ReadDigitalInput,
		.do_config		= i_APCI3200_ConfigDigitalOutput,
		.do_write		= i_APCI3200_WriteDigitalOutput,
		.do_bits		= i_APCI3200_ReadDigitalOutput,
	},
#endif
#ifdef CONFIG_APCI_1710
	{
		.pc_DriverName		= "apci1710",
		.i_VendorId		= APCI1710_BOARD_VENDOR_ID,
		.i_DeviceId		= APCI1710_BOARD_DEVICE_ID,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= 8,
		.i_IorangeBase2		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.interrupt		= v_APCI1710_Interrupt,
		.reset			= i_APCI1710_Reset,
	},
#endif
#ifdef CONFIG_APCI_16XX
	{
		.pc_DriverName		= "apci1648",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1009,
		.i_IorangeBase0		= 128,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrTTLChannel	= 48,
		.reset			= i_APCI16XX_Reset,
		.ttl_config		= i_APCI16XX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI16XX_InsnBitsReadTTLIO,
		.ttl_read		= i_APCI16XX_InsnReadTTLIOAllPortValue,
		.ttl_write		= i_APCI16XX_InsnBitsWriteTTLIO,
	}, {
		.pc_DriverName		= "apci1696",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x100A,
		.i_IorangeBase0		= 128,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrTTLChannel	= 96,
		.reset			= i_APCI16XX_Reset,
		.ttl_config		= i_APCI16XX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI16XX_InsnBitsReadTTLIO,
		.ttl_read		= i_APCI16XX_InsnReadTTLIOAllPortValue,
		.ttl_write		= i_APCI16XX_InsnBitsWriteTTLIO,
	},
#endif
#ifdef CONFIG_APCI_3XXX
	{
		.pc_DriverName		= "apci3000-16",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3010,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3000-8",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x300F,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3000-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x300E,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 4,
		.i_NbrAiChannelDiff	= 2,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3006-16",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3013,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3006-8",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3014,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3006-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3015,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 4,
		.i_NbrAiChannelDiff	= 2,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3010-16",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3016,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3010-8",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3017,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3010-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3018,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 4,
		.i_NbrAiChannelDiff	= 2,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3016-16",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3019,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3016-8",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x301A,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3016-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x301B,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 4,
		.i_NbrAiChannelDiff	= 2,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3100-16-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x301C,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 4095,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3100-8-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x301D,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 4095,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3106-16-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x301E,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 65535,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3106-8-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x301F,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 65535,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3110-16-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3020,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 4095,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3110-8-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3021,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 4095,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3116-16-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3022,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 65535,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3116-8-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3023,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 65535,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	}, {
		.pc_DriverName		= "apci3003",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x300B,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.b_AvailableConvertUnit	= 7,
		.ui_MinAcquisitiontimeNs = 2500,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
	}, {
		.pc_DriverName		= "apci3002-16",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3002,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannelDiff	= 16,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
	}, {
		.pc_DriverName		= "apci3002-8",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3003,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
	}, {
		.pc_DriverName		= "apci3002-4",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3004,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_read		= i_APCI3XXX_InsnReadDigitalInput,
		.di_bits		= i_APCI3XXX_InsnBitsDigitalInput,
		.do_write		= i_APCI3XXX_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3XXX_InsnBitsDigitalOutput,
		.do_read		= i_APCI3XXX_InsnReadDigitalOutput,
	}, {
		.pc_DriverName		= "apci3500",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3024,
		.i_IorangeBase0		= 256,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 256,
		.i_IorangeBase3		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAoChannel		= 4,
		.i_AoMaxdata		= 4095,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.interrupt		= v_APCI3XXX_Interrupt,
		.reset			= i_APCI3XXX_Reset,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
#endif
};

static struct comedi_driver driver_addi = {
	.driver_name = ADDIDATA_DRIVER_NAME,
	.module = THIS_MODULE,
	.attach = i_ADDI_Attach,
	.detach = i_ADDI_Detach,
	.num_names = ARRAY_SIZE(boardtypes),
	.board_name = &boardtypes[0].pc_DriverName,
	.offset = sizeof(struct addi_board),
};

static int __devinit driver_addi_pci_probe(struct pci_dev *dev,
					   const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &driver_addi);
}

static void __devexit driver_addi_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver driver_addi_pci_driver = {
	.id_table = addi_apci_tbl,
	.probe = &driver_addi_pci_probe,
	.remove = __devexit_p(&driver_addi_pci_remove)
};

static int __init driver_addi_init_module(void)
{
	int retval;

	retval = comedi_driver_register(&driver_addi);
	if (retval < 0)
		return retval;

	driver_addi_pci_driver.name = (char *)driver_addi.driver_name;
	return pci_register_driver(&driver_addi_pci_driver);
}

static void __exit driver_addi_cleanup_module(void)
{
	pci_unregister_driver(&driver_addi_pci_driver);
	comedi_driver_unregister(&driver_addi);
}

module_init(driver_addi_init_module);
module_exit(driver_addi_cleanup_module);

/*
+----------------------------------------------------------------------------+
| Function name     :static int i_ADDI_Attach(struct comedi_device *dev,            |
|										struct comedi_devconfig *it)        |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              :Detects the card.                                       |
|  			 Configure the driver for a particular board.            |
|  			 This function does all the initializations and memory   |
|			 allocation of data structures for the driver.	         |
+----------------------------------------------------------------------------+
| Input Parameters  :struct comedi_device *dev										 |
|                    struct comedi_devconfig *it									 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :  0            					                     |
|                    													     |
+----------------------------------------------------------------------------+
*/

static int i_ADDI_Attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret, pages, i, n_subdevices;
	unsigned int dw_Dummy;
	resource_size_t io_addr[5];
	unsigned int irq;
	resource_size_t iobase_a, iobase_main, iobase_addon, iobase_reserved;
	struct pcilst_struct *card = NULL;
	unsigned char pci_bus, pci_slot, pci_func;
	int i_Dma = 0;

	ret = alloc_private(dev, sizeof(struct addi_private));
	if (ret < 0)
		return -ENOMEM;

	if (!pci_list_builded) {
		v_pci_card_list_init(this_board->i_VendorId, 1);	/* 1 for displaying the list.. */
		pci_list_builded = 1;
	}
	/* printk("comedi%d: "ADDIDATA_DRIVER_NAME": board=%s",dev->minor,this_board->pc_DriverName); */

	if ((this_board->i_Dma) && (it->options[2] == 0)) {
		i_Dma = 1;
	}

	card = ptr_select_and_alloc_pci_card(this_board->i_VendorId,
					     this_board->i_DeviceId,
					     it->options[0],
					     it->options[1], i_Dma);

	if (card == NULL)
		return -EIO;

	devpriv->allocated = 1;

	if ((i_pci_card_data(card, &pci_bus, &pci_slot, &pci_func, &io_addr[0],
				&irq)) < 0) {
		i_pci_card_free(card);
		printk(" - Can't get AMCC data!\n");
		return -EIO;
	}

	iobase_a = io_addr[0];
	iobase_main = io_addr[1];
	iobase_addon = io_addr[2];
	iobase_reserved = io_addr[3];
	printk("\nBus %d: Slot %d: Funct%d\nBase0: 0x%8llx\nBase1: 0x%8llx\nBase2: 0x%8llx\nBase3: 0x%8llx\n", pci_bus, pci_slot, pci_func, (unsigned long long)io_addr[0], (unsigned long long)io_addr[1], (unsigned long long)io_addr[2], (unsigned long long)io_addr[3]);

	if ((this_board->pc_EepromChip == NULL)
		|| (strcmp(this_board->pc_EepromChip, ADDIDATA_9054) != 0)) {
	   /************************************/
		/* Test if more that 1 address used */
	   /************************************/

		if (this_board->i_IorangeBase1 != 0) {
			dev->iobase = (unsigned long)iobase_main;	/*  DAQ base address... */
		} else {
			dev->iobase = (unsigned long)iobase_a;	/*  DAQ base address... */
		}

		dev->board_name = this_board->pc_DriverName;
		devpriv->amcc = card;
		devpriv->iobase = (int) dev->iobase;
		devpriv->i_IobaseAmcc = (int) iobase_a;	/* AMCC base address... */
		devpriv->i_IobaseAddon = (int) iobase_addon;	/* ADD ON base address.... */
		devpriv->i_IobaseReserved = (int) iobase_reserved;
	} else {
		dev->board_name = this_board->pc_DriverName;
		dev->iobase = (unsigned long)io_addr[2];
		devpriv->amcc = card;
		devpriv->iobase = (int) io_addr[2];
		devpriv->i_IobaseReserved = (int) io_addr[3];
		printk("\nioremap begin");
		devpriv->dw_AiBase = ioremap(io_addr[3],
					     this_board->i_IorangeBase3);
		printk("\nioremap end");
	}

	/* Initialize parameters that can be overridden in EEPROM */
	devpriv->s_EeParameters.i_NbrAiChannel = this_board->i_NbrAiChannel;
	devpriv->s_EeParameters.i_NbrAoChannel = this_board->i_NbrAoChannel;
	devpriv->s_EeParameters.i_AiMaxdata = this_board->i_AiMaxdata;
	devpriv->s_EeParameters.i_AoMaxdata = this_board->i_AoMaxdata;
	devpriv->s_EeParameters.i_NbrDiChannel = this_board->i_NbrDiChannel;
	devpriv->s_EeParameters.i_NbrDoChannel = this_board->i_NbrDoChannel;
	devpriv->s_EeParameters.i_DoMaxdata = this_board->i_DoMaxdata;
	devpriv->s_EeParameters.i_Dma = this_board->i_Dma;
	devpriv->s_EeParameters.i_Timer = this_board->i_Timer;
	devpriv->s_EeParameters.ui_MinAcquisitiontimeNs =
		this_board->ui_MinAcquisitiontimeNs;
	devpriv->s_EeParameters.ui_MinDelaytimeNs =
		this_board->ui_MinDelaytimeNs;

	/* ## */

	if (irq > 0) {
		if (request_irq(irq, v_ADDI_Interrupt, IRQF_SHARED,
				this_board->pc_DriverName, dev) < 0) {
			printk(", unable to allocate IRQ %u, DISABLING IT",
				irq);
			irq = 0;	/* Can't use IRQ */
		} else {
			printk("\nirq=%u", irq);
		}
	} else {
		printk(", IRQ disabled");
	}

	printk("\nOption %d %d %d\n", it->options[0], it->options[1],
		it->options[2]);
	dev->irq = irq;

	/*  Read eepeom and fill addi_board Structure */

	if (this_board->i_PCIEeprom) {
		printk("\nPCI Eeprom used");
		if (!(strcmp(this_board->pc_EepromChip, "S5920"))) {
			/*  Set 3 wait stait */
			if (!(strcmp(this_board->pc_DriverName, "apci035"))) {
				outl(0x80808082, devpriv->i_IobaseAmcc + 0x60);
			} else {
				outl(0x83838383, devpriv->i_IobaseAmcc + 0x60);
			}
			/*  Enable the interrupt for the controller */
			dw_Dummy = inl(devpriv->i_IobaseAmcc + 0x38);
			outl(dw_Dummy | 0x2000, devpriv->i_IobaseAmcc + 0x38);
			printk("\nEnable the interrupt for the controller");
		}
		printk("\nRead Eeprom");
		i_EepromReadMainHeader(io_addr[0], this_board->pc_EepromChip,
			dev);
	} else {
		printk("\nPCI Eeprom unused");
	}

	if (it->options[2] > 0) {
		devpriv->us_UseDma = ADDI_DISABLE;
	} else {
		devpriv->us_UseDma = ADDI_ENABLE;
	}

	if (devpriv->s_EeParameters.i_Dma) {
		printk("\nDMA used");
		if (devpriv->us_UseDma == ADDI_ENABLE) {
			/*  alloc DMA buffers */
			devpriv->b_DmaDoubleBuffer = 0;
			for (i = 0; i < 2; i++) {
				for (pages = 4; pages >= 0; pages--) {
					devpriv->ul_DmaBufferVirtual[i] =
						(void *) __get_free_pages(GFP_KERNEL, pages);

					if (devpriv->ul_DmaBufferVirtual[i])
						break;
				}
				if (devpriv->ul_DmaBufferVirtual[i]) {
					devpriv->ui_DmaBufferPages[i] = pages;
					devpriv->ui_DmaBufferSize[i] =
						PAGE_SIZE * pages;
					devpriv->ui_DmaBufferSamples[i] =
						devpriv->
						ui_DmaBufferSize[i] >> 1;
					devpriv->ul_DmaBufferHw[i] =
						virt_to_bus((void *)devpriv->
						ul_DmaBufferVirtual[i]);
				}
			}
			if (!devpriv->ul_DmaBufferVirtual[0]) {
				printk
					(", Can't allocate DMA buffer, DMA disabled!");
				devpriv->us_UseDma = ADDI_DISABLE;
			}

			if (devpriv->ul_DmaBufferVirtual[1]) {
				devpriv->b_DmaDoubleBuffer = 1;
			}
		}

		if ((devpriv->us_UseDma == ADDI_ENABLE)) {
			printk("\nDMA ENABLED\n");
		} else {
			printk("\nDMA DISABLED\n");
		}
	}

	if (!strcmp(this_board->pc_DriverName, "apci1710")) {
#ifdef CONFIG_APCI_1710
		i_ADDI_AttachPCI1710(dev);

		/*  save base address */
		devpriv->s_BoardInfos.ui_Address = io_addr[2];
#endif
	} else {
		n_subdevices = 7;
		ret = comedi_alloc_subdevices(dev, n_subdevices);
		if (ret)
			return ret;

		/*  Allocate and Initialise AI Subdevice Structures */
		s = &dev->subdevices[0];
		if ((devpriv->s_EeParameters.i_NbrAiChannel)
			|| (this_board->i_NbrAiChannelDiff)) {
			dev->read_subdev = s;
			s->type = COMEDI_SUBD_AI;
			s->subdev_flags =
				SDF_READABLE | SDF_COMMON | SDF_GROUND
				| SDF_DIFF;
			if (devpriv->s_EeParameters.i_NbrAiChannel) {
				s->n_chan =
					devpriv->s_EeParameters.i_NbrAiChannel;
				devpriv->b_SingelDiff = 0;
			} else {
				s->n_chan = this_board->i_NbrAiChannelDiff;
				devpriv->b_SingelDiff = 1;
			}
			s->maxdata = devpriv->s_EeParameters.i_AiMaxdata;
			s->len_chanlist = this_board->i_AiChannelList;
			s->range_table = this_board->pr_AiRangelist;

			/* Set the initialisation flag */
			devpriv->b_AiInitialisation = 1;

			s->insn_config = this_board->ai_config;
			s->insn_read = this_board->ai_read;
			s->insn_write = this_board->ai_write;
			s->insn_bits = this_board->ai_bits;
			s->do_cmdtest = this_board->ai_cmdtest;
			s->do_cmd = this_board->ai_cmd;
			s->cancel = this_board->ai_cancel;

		} else {
			s->type = COMEDI_SUBD_UNUSED;
		}

		/*  Allocate and Initialise AO Subdevice Structures */
		s = &dev->subdevices[1];
		if (devpriv->s_EeParameters.i_NbrAoChannel) {
			s->type = COMEDI_SUBD_AO;
			s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
			s->n_chan = devpriv->s_EeParameters.i_NbrAoChannel;
			s->maxdata = devpriv->s_EeParameters.i_AoMaxdata;
			s->len_chanlist =
				devpriv->s_EeParameters.i_NbrAoChannel;
			s->range_table = this_board->pr_AoRangelist;
			s->insn_config = this_board->ao_config;
			s->insn_write = this_board->ao_write;
		} else {
			s->type = COMEDI_SUBD_UNUSED;
		}
		/*  Allocate and Initialise DI Subdevice Structures */
		s = &dev->subdevices[2];
		if (devpriv->s_EeParameters.i_NbrDiChannel) {
			s->type = COMEDI_SUBD_DI;
			s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
			s->n_chan = devpriv->s_EeParameters.i_NbrDiChannel;
			s->maxdata = 1;
			s->len_chanlist =
				devpriv->s_EeParameters.i_NbrDiChannel;
			s->range_table = &range_digital;
			s->io_bits = 0;	/* all bits input */
			s->insn_config = this_board->di_config;
			s->insn_read = this_board->di_read;
			s->insn_write = this_board->di_write;
			s->insn_bits = this_board->di_bits;
		} else {
			s->type = COMEDI_SUBD_UNUSED;
		}
		/*  Allocate and Initialise DO Subdevice Structures */
		s = &dev->subdevices[3];
		if (devpriv->s_EeParameters.i_NbrDoChannel) {
			s->type = COMEDI_SUBD_DO;
			s->subdev_flags =
				SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
			s->n_chan = devpriv->s_EeParameters.i_NbrDoChannel;
			s->maxdata = devpriv->s_EeParameters.i_DoMaxdata;
			s->len_chanlist =
				devpriv->s_EeParameters.i_NbrDoChannel;
			s->range_table = &range_digital;
			s->io_bits = 0xf;	/* all bits output */

			/* insn_config - for digital output memory */
			s->insn_config = this_board->do_config;
			s->insn_write = this_board->do_write;
			s->insn_bits = this_board->do_bits;
			s->insn_read = this_board->do_read;
		} else {
			s->type = COMEDI_SUBD_UNUSED;
		}

		/*  Allocate and Initialise Timer Subdevice Structures */
		s = &dev->subdevices[4];
		if (devpriv->s_EeParameters.i_Timer) {
			s->type = COMEDI_SUBD_TIMER;
			s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
			s->n_chan = 1;
			s->maxdata = 0;
			s->len_chanlist = 1;
			s->range_table = &range_digital;

			s->insn_write = this_board->timer_write;
			s->insn_read = this_board->timer_read;
			s->insn_config = this_board->timer_config;
			s->insn_bits = this_board->timer_bits;
		} else {
			s->type = COMEDI_SUBD_UNUSED;
		}

		/*  Allocate and Initialise TTL */
		s = &dev->subdevices[5];
		if (this_board->i_NbrTTLChannel) {
			s->type = COMEDI_SUBD_TTLIO;
			s->subdev_flags =
				SDF_WRITEABLE | SDF_READABLE | SDF_GROUND | SDF_COMMON;
			s->n_chan = this_board->i_NbrTTLChannel;
			s->maxdata = 1;
			s->io_bits = 0;	/* all bits input */
			s->len_chanlist = this_board->i_NbrTTLChannel;
			s->range_table = &range_digital;
			s->insn_config = this_board->ttl_config;
			s->insn_bits = this_board->ttl_bits;
			s->insn_read = this_board->ttl_read;
			s->insn_write = this_board->ttl_write;
		} else {
			s->type = COMEDI_SUBD_UNUSED;
		}

		/* EEPROM */
		s = &dev->subdevices[6];
		if (this_board->i_PCIEeprom) {
			s->type = COMEDI_SUBD_MEMORY;
			s->subdev_flags = SDF_READABLE | SDF_INTERNAL;
			s->n_chan = 256;
			s->maxdata = 0xffff;
			s->insn_read = i_ADDIDATA_InsnReadEeprom;
		} else {
			s->type = COMEDI_SUBD_UNUSED;
		}
	}

	printk("\ni_ADDI_Attach end\n");
	i_ADDI_Reset(dev);
	devpriv->b_ValidDriver = 1;
	return 0;
}

static void i_ADDI_Detach(struct comedi_device *dev)
{
	if (dev->private) {
		if (devpriv->b_ValidDriver)
			i_ADDI_Reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
		if ((this_board->pc_EepromChip == NULL) ||
		    (strcmp(this_board->pc_EepromChip, ADDIDATA_9054) != 0)) {
			if (devpriv->allocated)
				i_pci_card_free(devpriv->amcc);
			if (devpriv->ul_DmaBufferVirtual[0]) {
				free_pages((unsigned long)devpriv->
					ul_DmaBufferVirtual[0],
					devpriv->ui_DmaBufferPages[0]);
			}
			if (devpriv->ul_DmaBufferVirtual[1]) {
				free_pages((unsigned long)devpriv->
					ul_DmaBufferVirtual[1],
					devpriv->ui_DmaBufferPages[1]);
			}
		} else {
			iounmap(devpriv->dw_AiBase);
			if (devpriv->allocated)
				i_pci_card_free(devpriv->amcc);
		}
		if (pci_list_builded) {
			v_pci_card_list_cleanup(this_board->i_VendorId);
			pci_list_builded = 0;
		}
	}
}

/*
+----------------------------------------------------------------------------+
| Function name     : static int i_ADDI_Reset(struct comedi_device *dev)			 |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : Disables all interrupts, Resets digital output to low, |
|				Set all analog output to low						 |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     														 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      : 0           					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

static int i_ADDI_Reset(struct comedi_device *dev)
{

	this_board->reset(dev);
	return 0;
}

/* Interrupt function */
/*
+----------------------------------------------------------------------------+
| Function name     :                                                        |
|static void v_ADDI_Interrupt(int irq, void *d)                 |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : Registerd interrupt routine						     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : 	int irq												 |
|                     														 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	this_board->interrupt(irq, d);
	return IRQ_RETVAL(1);
}

/* EEPROM Read Function */
/*
+----------------------------------------------------------------------------+
| Function name     :                                                        |
|INT i_ADDIDATA_InsnReadEeprom(struct comedi_device *dev,struct comedi_subdevice *s,
							struct comedi_insn *insn,unsigned int *data)
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : Read 256 words from EEPROM          				     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  :(struct comedi_device *dev,struct comedi_subdevice *s,
			struct comedi_insn *insn,unsigned int *data) 						 |
|                     														 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

static int i_ADDIDATA_InsnReadEeprom(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned short w_Data;
	unsigned short w_Address;
	w_Address = CR_CHAN(insn->chanspec);	/*  address to be read as 0,1,2,3...255 */

	w_Data = w_EepromReadWord(devpriv->i_IobaseAmcc,
		this_board->pc_EepromChip, 0x100 + (2 * w_Address));
	data[0] = w_Data;
	/* multiplied by 2 bcozinput will be like 0,1,2...255 */
	return insn->n;

}
