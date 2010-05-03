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

You should also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/
/*

  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstraße 3       D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-------------------------------+---------------------------------------+
  | Project     : APCI-1032       | Compiler   : GCC                      |
  | Module name : hwdrv_apci1032.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-------------------------------+---------------------------------------+
  | Description :   Hardware Layer Acces For APCI-1032                    |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +----------+-----------+------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          |           |                                                |
  |          |           |                                                |
  |          |           |                                                |
  +----------+-----------+------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
|                               Included files                               |
+----------------------------------------------------------------------------+
*/
#include "hwdrv_apci1032.h"
#include <linux/delay.h>
/* Global variables */
unsigned int ui_InterruptStatus;

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1032_ConfigDigitalInput                      |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Configures the digital input Subdevice                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains         |
|                                          configuration parameters as below |
|                                                                            |
|			  data[0]            : 1 Enable  Digital Input Interrupt |
|								   0 Disable Digital Input Interrupt |
|			  data[1]            : 0 ADDIDATA Interrupt OR LOGIC	 |
|								 : 1 ADDIDATA Interrupt AND LOGIC    |
|			  data[2]			 : Interrupt mask for the mode 1	 |
|			  data[3]			 : Interrupt mask for the mode 2	 |
|																	 |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1032_ConfigDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned int ui_TmpValue;

	unsigned int ul_Command1 = 0;
	unsigned int ul_Command2 = 0;
	devpriv->tsk_Current = current;

  /*******************************/
	/* Set the digital input logic */
  /*******************************/
	if (data[0] == ADDIDATA_ENABLE) {
		ul_Command1 = ul_Command1 | data[2];
		ul_Command2 = ul_Command2 | data[3];
		outl(ul_Command1,
			devpriv->iobase + APCI1032_DIGITAL_IP_INTERRUPT_MODE1);
		outl(ul_Command2,
			devpriv->iobase + APCI1032_DIGITAL_IP_INTERRUPT_MODE2);
		if (data[1] == ADDIDATA_OR) {
			outl(0x4, devpriv->iobase + APCI1032_DIGITAL_IP_IRQ);
			ui_TmpValue =
				inl(devpriv->iobase + APCI1032_DIGITAL_IP_IRQ);
		}		/* if (data[1] == ADDIDATA_OR) */
		else
			outl(0x6, devpriv->iobase + APCI1032_DIGITAL_IP_IRQ);
				/* else if(data[1] == ADDIDATA_OR) */
	}			/*  if( data[0] == ADDIDATA_ENABLE) */
	else {
		ul_Command1 = ul_Command1 & 0xFFFF0000;
		ul_Command2 = ul_Command2 & 0xFFFF0000;
		outl(ul_Command1,
			devpriv->iobase + APCI1032_DIGITAL_IP_INTERRUPT_MODE1);
		outl(ul_Command2,
			devpriv->iobase + APCI1032_DIGITAL_IP_INTERRUPT_MODE2);
		outl(0x0, devpriv->iobase + APCI1032_DIGITAL_IP_IRQ);
	}			/* else if  ( data[0] == ADDIDATA_ENABLE) */

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1032_Read1DigitalInput                       |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Return the status of the digital input                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|		              unsigned int ui_Channel : Channel number to read       |
|                     unsigned int *data          : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI1032_Read1DigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned int ui_TmpValue = 0;
	unsigned int ui_Channel;
	ui_Channel = CR_CHAN(insn->chanspec);
	if (ui_Channel <= 31) {
		ui_TmpValue = (unsigned int) inl(devpriv->iobase + APCI1032_DIGITAL_IP);
/*
* since only 1 channel reqd to bring it to last bit it is rotated 8
* +(chan - 1) times then ANDed with 1 for last bit.
*/
		*data = (ui_TmpValue >> ui_Channel) & 0x1;
	}			/* if(ui_Channel >= 0 && ui_Channel <=31) */
	else {
		/* comedi_error(dev," \n chan spec wrong\n"); */
		return -EINVAL;	/*  "sorry channel spec wrong " */
	}			/* else if(ui_Channel >= 0 && ui_Channel <=31) */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1032_ReadMoreDigitalInput                    |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                     struct comedi_insn *insn,unsigned int *data)                      |
+----------------------------------------------------------------------------+
| Task              : Return the status of the Requested digital inputs      |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     unsigned int ui_NoOfChannels    : No Of Channels To be Read    |
|                      unsigned int *data             : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1032_ReadMoreDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned int ui_PortValue = data[0];
	unsigned int ui_Mask = 0;
	unsigned int ui_NoOfChannels;

	ui_NoOfChannels = CR_CHAN(insn->chanspec);
	if (data[1] == 0) {
		*data = (unsigned int) inl(devpriv->iobase + APCI1032_DIGITAL_IP);
		switch (ui_NoOfChannels) {
		case 2:
			ui_Mask = 3;
			*data = (*data >> (2 * ui_PortValue)) & ui_Mask;
			break;
		case 4:
			ui_Mask = 15;
			*data = (*data >> (4 * ui_PortValue)) & ui_Mask;
			break;
		case 8:
			ui_Mask = 255;
			*data = (*data >> (8 * ui_PortValue)) & ui_Mask;
			break;
		case 16:
			ui_Mask = 65535;
			*data = (*data >> (16 * ui_PortValue)) & ui_Mask;
			break;
		case 31:
			break;
		default:
			/* comedi_error(dev," \nchan spec wrong\n"); */
			return -EINVAL;	/*  "sorry channel spec wrong " */
			break;
		}		/* switch(ui_NoOfChannels) */
	}			/* if(data[1]==0) */
	else {
		if (data[1] == 1)
			*data = ui_InterruptStatus;
				/* if(data[1]==1) */
	}			/* else if(data[1]==0) */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : static void v_APCI1032_Interrupt					     |
|					  (int irq , void *d)      |
+----------------------------------------------------------------------------+
| Task              : Interrupt handler for the interruptible digital inputs |
+----------------------------------------------------------------------------+
| Input Parameters  : int irq                 : irq number                   |
|                     void *d                 : void pointer                 |
+----------------------------------------------------------------------------+
| Output Parameters :	--						     |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                     |
+----------------------------------------------------------------------------+
*/
static void v_APCI1032_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;

	unsigned int ui_Temp;
	/* disable the interrupt */
	ui_Temp = inl(devpriv->iobase + APCI1032_DIGITAL_IP_IRQ);
	outl(ui_Temp & APCI1032_DIGITAL_IP_INTERRUPT_DISABLE,
		devpriv->iobase + APCI1032_DIGITAL_IP_IRQ);
	ui_InterruptStatus =
		inl(devpriv->iobase + APCI1032_DIGITAL_IP_INTERRUPT_STATUS);
	ui_InterruptStatus = ui_InterruptStatus & 0X0000FFFF;
	send_sig(SIGIO, devpriv->tsk_Current, 0);	/*  send signal to the sample */
	outl(ui_Temp, devpriv->iobase + APCI1032_DIGITAL_IP_IRQ);	/* enable the interrupt */
	return;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1032_Reset(struct comedi_device *dev)               |                                                       |
+----------------------------------------------------------------------------+
| Task              :resets all the registers                                |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      :                                                        |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1032_Reset(struct comedi_device *dev)
{
	outl(0x0, devpriv->iobase + APCI1032_DIGITAL_IP_IRQ);	/* disable the interrupts */
	inl(devpriv->iobase + APCI1032_DIGITAL_IP_INTERRUPT_STATUS);	/* Reset the interrupt status register */
	outl(0x0, devpriv->iobase + APCI1032_DIGITAL_IP_INTERRUPT_MODE1);	/* Disable the and/or interrupt */
	outl(0x0, devpriv->iobase + APCI1032_DIGITAL_IP_INTERRUPT_MODE2);
	return 0;
}
