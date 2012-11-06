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
  | Project     : APCI-2032       | Compiler   : GCC                      |
  | Module name : hwdrv_apci2032.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-------------------------------+---------------------------------------+
  | Description :   Hardware Layer Access For APCI-2032                   |
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

/*********      Definitions for APCI-2032 card  *****/

/* Card Specific information */
#define APCI2032_ADDRESS_RANGE				63

/* DIGITAL INPUT-OUTPUT DEFINE */

#define APCI2032_DIGITAL_OP				0
#define APCI2032_DIGITAL_OP_RW				0
#define APCI2032_DIGITAL_OP_INTERRUPT			4
#define APCI2032_DIGITAL_OP_IRQ				12

/* Digital Output Interrupt Status */
#define APCI2032_DIGITAL_OP_INTERRUPT_STATUS		8

/* Digital Output Interrupt Enable Disable. */
#define APCI2032_DIGITAL_OP_VCC_INTERRUPT_ENABLE	0x1
#define APCI2032_DIGITAL_OP_VCC_INTERRUPT_DISABLE	0xfffffffe
#define APCI2032_DIGITAL_OP_CC_INTERRUPT_ENABLE		0x2
#define APCI2032_DIGITAL_OP_CC_INTERRUPT_DISABLE	0xfffffffd

/* TIMER COUNTER WATCHDOG DEFINES */

#define ADDIDATA_WATCHDOG				2
#define APCI2032_DIGITAL_OP_WATCHDOG			16
#define APCI2032_TCW_RELOAD_VALUE			4
#define APCI2032_TCW_TIMEBASE				8
#define APCI2032_TCW_PROG				12
#define APCI2032_TCW_TRIG_STATUS			16
#define APCI2032_TCW_IRQ				20

static unsigned int ui_InterruptData, ui_Type;

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2032_ConfigDigitalOutput                     |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Digital Output Subdevice.               |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|					  data[1]            : 1 Enable  VCC  Interrupt  |
|										   0 Disable VCC  Interrupt  |
|					  data[2]            : 1 Enable  CC  Interrupt   |
|										   0 Disable CC  Interrupt   |
|																	 |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI2032_ConfigDigitalOutput(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ul_Command = 0;

	devpriv->tsk_Current = current;

	if ((data[0] != 0) && (data[0] != 1)) {
		comedi_error(dev,
			"Not a valid Data !!! ,Data should be 1 or 0\n");
		return -EINVAL;
	}			/* if  ( (data[0]!=0) && (data[0]!=1) ) */
	if (data[0]) {
		devpriv->b_OutputMemoryStatus = ADDIDATA_ENABLE;
	}			/*  if  (data[0]) */
	else {
		devpriv->b_OutputMemoryStatus = ADDIDATA_DISABLE;
	}			/* else if  (data[0]) */

	if (data[1] == ADDIDATA_ENABLE) {
		ul_Command = ul_Command | 0x1;
	}			/* if  (data[1] == ADDIDATA_ENABLE) */
	else {
		ul_Command = ul_Command & 0xFFFFFFFE;
	}			/* elseif  (data[1] == ADDIDATA_ENABLE) */
	if (data[2] == ADDIDATA_ENABLE) {
		ul_Command = ul_Command | 0x2;
	}			/* if  (data[2] == ADDIDATA_ENABLE) */
	else {
		ul_Command = ul_Command & 0xFFFFFFFD;
	}			/* elseif  (data[2] == ADDIDATA_ENABLE) */
	outl(ul_Command, devpriv->iobase + APCI2032_DIGITAL_OP_INTERRUPT);
	ui_InterruptData = inl(devpriv->iobase + APCI2032_DIGITAL_OP_INTERRUPT);
	return insn->n;
}

static int apci2032_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	s->state = inl(devpriv->iobase + APCI2032_DIGITAL_OP_RW);
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outl(s->state, devpriv->iobase + APCI2032_DIGITAL_OP);
	}

	data[1] = s->state;

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2032_ConfigWatchdog(comedi_device
|                   *dev,struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)|
|				                                                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Watchdog                                |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure
|                      struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data          : Data Pointer to read status                                                                                                             |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI2032_ConfigWatchdog(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	if (data[0] == 0) {
		/* Disable the watchdog */
		outl(0x0,
			devpriv->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
			APCI2032_TCW_PROG);
		/* Loading the Reload value */
		outl(data[1],
			devpriv->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
			APCI2032_TCW_RELOAD_VALUE);
	} else {
		printk("\nThe input parameters are wrong\n");
		return -EINVAL;
	}

	return insn->n;
}

 /*
    +----------------------------------------------------------------------------+
    | Function   Name   : int i_APCI2032_StartStopWriteWatchdog                  |
    |                           (struct comedi_device *dev,struct comedi_subdevice *s,
    struct comedi_insn *insn,unsigned int *data);                      |
    +----------------------------------------------------------------------------+
    | Task              : Start / Stop The Watchdog                              |
    +----------------------------------------------------------------------------+
    | Input Parameters  : struct comedi_device *dev      : Driver handle                |
    |                     struct comedi_subdevice *s,   :pointer to subdevice structure
    struct comedi_insn *insn      :pointer to insn structure      |
    |                     unsigned int *data          : Data Pointer to read status  |
    +----------------------------------------------------------------------------+
    | Output Parameters :       --                                                                                                       |
    +----------------------------------------------------------------------------+
    | Return Value      : TRUE  : No error occur                                 |
    |                       : FALSE : Error occur. Return the error          |
    |                                                                            |
    +----------------------------------------------------------------------------+
  */

static int i_APCI2032_StartStopWriteWatchdog(struct comedi_device *dev,
					     struct comedi_subdevice *s,
					     struct comedi_insn *insn,
					     unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	switch (data[0]) {
	case 0:		/* stop the watchdog */
		outl(0x0, devpriv->iobase + APCI2032_DIGITAL_OP_WATCHDOG + APCI2032_TCW_PROG);	/* disable the watchdog */
		break;
	case 1:		/* start the watchdog */
		outl(0x0001,
			devpriv->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
			APCI2032_TCW_PROG);
		break;
	case 2:		/* Software trigger */
		outl(0x0201,
			devpriv->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
			APCI2032_TCW_PROG);
		break;
	default:
		printk("\nSpecified functionality does not exist\n");
		return -EINVAL;
	}
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2032_ReadWatchdog                            |
|			(struct comedi_device *dev,struct comedi_subdevice *s,struct comedi_insn *insn,
|                    unsigned int *data); 	                                     |
+----------------------------------------------------------------------------+
| Task              : Read The Watchdog                                      |
+----------------------------------------------------------------------------+
| Input Parameters  :   struct comedi_device *dev      : Driver handle              |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure
|                      struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data          : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

static int i_APCI2032_ReadWatchdog(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	data[0] =
		inl(devpriv->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
		APCI2032_TCW_TRIG_STATUS) & 0x1;
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   :  void v_APCI2032_Interrupt					         |
|					  (int irq , void *d)      |
+----------------------------------------------------------------------------+
| Task              : Writes port value  To the selected port                |
+----------------------------------------------------------------------------+
| Input Parameters  : int irq                 : irq number                   |
|                     void *d                 : void pointer                 |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static void v_APCI2032_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct addi_private *devpriv = dev->private;
	unsigned int ui_DO;

	ui_DO = inl(devpriv->iobase + APCI2032_DIGITAL_OP_IRQ) & 0x1;	/* Check if VCC OR CC interrupt has occurred. */

	if (ui_DO == 0) {
		printk("\nInterrupt from unKnown source\n");
	}			/*  if(ui_DO==0) */
	if (ui_DO) {
		/*  Check for Digital Output interrupt Type - 1: Vcc interrupt 2: CC interrupt. */
		ui_Type =
			inl(devpriv->iobase +
			APCI2032_DIGITAL_OP_INTERRUPT_STATUS) & 0x3;
		outl(0x0,
			devpriv->iobase + APCI2032_DIGITAL_OP +
			APCI2032_DIGITAL_OP_INTERRUPT);
		if (ui_Type == 1) {
			/* Sends signal to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);
		}		/*  if (ui_Type==1) */
		else {
			if (ui_Type == 2) {
				/*  Sends signal to user space */
				send_sig(SIGIO, devpriv->tsk_Current, 0);
			}	/* if (ui_Type==2) */
		}		/* else if (ui_Type==1) */
	}			/* if(ui_DO) */

	return;

}

/*
+----------------------------------------------------------------------------+
| Function   Name   :  int i_APCI2032_ReadInterruptStatus                    |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              :Reads the interrupt status register                     |
+----------------------------------------------------------------------------+
| Input Parameters  :                                                        |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      :                                                        |
|			                                                         |
+----------------------------------------------------------------------------+
*/

static int i_APCI2032_ReadInterruptStatus(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	*data = ui_Type;
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   :  int i_APCI2032_Reset(struct comedi_device *dev)			     |
|					                                                 |
+----------------------------------------------------------------------------+
| Task              :Resets the registers of the card                        |
+----------------------------------------------------------------------------+
| Input Parameters  :                                                        |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      :                                                        |
|			                                                         |
+----------------------------------------------------------------------------+
*/

static int i_APCI2032_Reset(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	devpriv->b_DigitalOutputRegister = 0;
	ui_Type = 0;
	outl(0x0, devpriv->iobase + APCI2032_DIGITAL_OP);	/* Resets the output channels */
	outl(0x0, devpriv->iobase + APCI2032_DIGITAL_OP_INTERRUPT);	/* Disables the interrupt. */
	outl(0x0, devpriv->iobase + APCI2032_DIGITAL_OP_WATCHDOG + APCI2032_TCW_PROG);	/* disable the watchdog */
	outl(0x0, devpriv->iobase + APCI2032_DIGITAL_OP_WATCHDOG + APCI2032_TCW_RELOAD_VALUE);	/* reload=0 */
	return 0;
}
