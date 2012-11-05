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
  | Description :   Hardware Layer Access For APCI-1032                   |
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
 * I/O Register Map
 */
#define APCI1032_DI_REG			0x00
#define APCI1032_MODE1_REG		0x04
#define APCI1032_MODE2_REG		0x08
#define APCI1032_STATUS_REG		0x0c
#define APCI1032_CTRL_REG		0x10
#define APCI1032_CTRL_INT_OR		(0 << 1)
#define APCI1032_CTRL_INT_AND		(1 << 1)
#define APCI1032_CTRL_INT_ENA		(1 << 2)

/* Digital Input IRQ Function Selection */
#define ADDIDATA_OR				0
#define ADDIDATA_AND				1

static unsigned int ui_InterruptStatus;

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

static int i_APCI1032_ConfigDigitalInput(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 struct comedi_insn *insn,
					 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
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
		outl(ul_Command1, dev->iobase + APCI1032_MODE1_REG);
		outl(ul_Command2, dev->iobase + APCI1032_MODE2_REG);
		if (data[1] == ADDIDATA_OR) {
			outl(APCI1032_CTRL_INT_ENA |
			     APCI1032_CTRL_INT_OR,
			     dev->iobase + APCI1032_CTRL_REG);
			ui_TmpValue =
				inl(dev->iobase + APCI1032_CTRL_REG);
		}		/* if (data[1] == ADDIDATA_OR) */
		else
			outl(APCI1032_CTRL_INT_ENA |
			     APCI1032_CTRL_INT_AND,
			     dev->iobase + APCI1032_CTRL_REG);
				/* else if(data[1] == ADDIDATA_OR) */
	}			/*  if( data[0] == ADDIDATA_ENABLE) */
	else {
		ul_Command1 = ul_Command1 & 0xFFFF0000;
		ul_Command2 = ul_Command2 & 0xFFFF0000;
		outl(ul_Command1, dev->iobase + APCI1032_MODE1_REG);
		outl(ul_Command2, dev->iobase + APCI1032_MODE2_REG);
		outl(0x0, dev->iobase + APCI1032_CTRL_REG);
	}			/* else if  ( data[0] == ADDIDATA_ENABLE) */

	return insn->n;
}
