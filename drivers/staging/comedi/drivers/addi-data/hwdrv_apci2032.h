/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

        ADDI-DATA GmbH
        Dieselstrasse 3
        D-77833 Ottersweier
        Tel: +19(0)7223/9493-0
        Fax: +49(0)7223/9493-92
        http://www.addi-data-com
        info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You shoud also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/
/*********      Definitions for APCI-2032 card  *****/

// Card Specific information
#define APCI2032_BOARD_VENDOR_ID                 0x15B8
#define APCI2032_ADDRESS_RANGE                   63

//DIGITAL INPUT-OUTPUT DEFINE

#define APCI2032_DIGITAL_OP                 	0
#define APCI2032_DIGITAL_OP_RW                 	0
#define APCI2032_DIGITAL_OP_INTERRUPT           4
#define APCI2032_DIGITAL_OP_IRQ                 12

//Digital Output Interrupt Status
#define APCI2032_DIGITAL_OP_INTERRUPT_STATUS    8

//Digital Output Interrupt Enable Disable.
#define APCI2032_DIGITAL_OP_VCC_INTERRUPT_ENABLE   0x1
#define APCI2032_DIGITAL_OP_VCC_INTERRUPT_DISABLE  0xFFFFFFFE
#define APCI2032_DIGITAL_OP_CC_INTERRUPT_ENABLE    0x2
#define APCI2032_DIGITAL_OP_CC_INTERRUPT_DISABLE   0xFFFFFFFD

//ADDIDATA Enable Disable

#define ADDIDATA_ENABLE                            1
#define ADDIDATA_DISABLE                           0

// TIMER COUNTER WATCHDOG DEFINES

#define ADDIDATA_WATCHDOG                          2
#define APCI2032_DIGITAL_OP_WATCHDOG               16
#define APCI2032_TCW_RELOAD_VALUE                  4
#define APCI2032_TCW_TIMEBASE                      8
#define APCI2032_TCW_PROG                          12
#define APCI2032_TCW_TRIG_STATUS                   16
#define APCI2032_TCW_IRQ                           20

// Hardware Layer  functions for Apci2032

//DO
int i_APCI2032_ConfigDigitalOutput(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);
INT i_APCI2032_WriteDigitalOutput(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);
INT i_APCI2032_ReadDigitalOutput(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);
int i_APCI2032_ReadInterruptStatus(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);

// TIMER
// timer value is passed as u seconds
INT i_APCI2032_ConfigWatchdog(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);
int i_APCI2032_StartStopWriteWatchdog(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);
int i_APCI2032_ReadWatchdog(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);

// Interrupt functions.....

void v_APCI2032_Interrupt(int irq, void *d);

//Reset functions
int i_APCI2032_Reset(comedi_device * dev);
