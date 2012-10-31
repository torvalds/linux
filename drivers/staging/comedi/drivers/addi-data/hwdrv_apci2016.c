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
  | Project     : APCI-2016       | Compiler   : GCC                      |
  | Module name : hwdrv_apci2016.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-------------------------------+---------------------------------------+
  | Description :   Hardware Layer Access For APCI-2016                   |
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

/*********      Definitions for APCI-2016 card  *****/

#define APCI2016_ADDRESS_RANGE		8

/* DIGITAL INPUT-OUTPUT DEFINE */

#define APCI2016_DIGITAL_OP		0x04
#define APCI2016_DIGITAL_OP_RW		4

/* TIMER COUNTER WATCHDOG DEFINES */

#define ADDIDATA_WATCHDOG		2
#define APCI2016_DIGITAL_OP_WATCHDOG	0
#define APCI2016_WATCHDOG_ENABLEDISABLE	12
#define APCI2016_WATCHDOG_RELOAD_VALUE	4
#define APCI2016_WATCHDOG_STATUS	16

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2016_ConfigDigitalOutput                     |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Digital Output Subdevice.               |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|			  data[0]            : 1 Digital Memory On               |
|				     			   0 Digital Memory Off              |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI2016_ConfigDigitalOutput(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	if ((data[0] != 0) && (data[0] != 1)) {
		comedi_error(dev,
			"Not a valid Data !!! ,Data should be 1 or 0\n");
		return -EINVAL;
	}			/*  if  ((data[0]!=0) && (data[0]!=1)) */
	if (data[0]) {
		devpriv->b_OutputMemoryStatus = ADDIDATA_ENABLE;
	}			/*  if  (data[0] */
	else {
		devpriv->b_OutputMemoryStatus = ADDIDATA_DISABLE;
	}			/*  else if  (data[0] */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2016_WriteDigitalOutput                      |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Writes port value  To the selected port                |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     unsigned int ui_NoOfChannels    : No Of Channels To Write      |
|                     unsigned int *data              : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI2016_WriteDigitalOutput(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 struct comedi_insn *insn,
					 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ui_NoOfChannel;
	unsigned int ui_Temp, ui_Temp1;

	ui_NoOfChannel = CR_CHAN(insn->chanspec);
	if (ui_NoOfChannel > 15) {
		comedi_error(dev,
			"Invalid Channel Numbers !!!, Channel Numbers must be between 0 and 15\n");
		return -EINVAL;
	}			/*  if  ((ui_NoOfChannel<0) || (ui_NoOfChannel>15)) */
	if (devpriv->b_OutputMemoryStatus) {
		ui_Temp = inw(devpriv->iobase + APCI2016_DIGITAL_OP);
	}			/*  if  (devpriv->b_OutputMemoryStatus ) */
	else {
		ui_Temp = 0;
	}			/*  else if  (devpriv->b_OutputMemoryStatus ) */
	if ((data[1] != 0) && (data[1] != 1)) {
		comedi_error(dev,
			"Invalid Data[1] value !!!, Data[1] should be 0 or 1\n");
		return -EINVAL;
	}			/*  if  ((data[1]!=0) && (data[1]!=1)) */

	if (data[3] == 0) {
		if (data[1] == 0) {
			data[0] = (data[0] << ui_NoOfChannel) | ui_Temp;
			outw(data[0], devpriv->iobase + APCI2016_DIGITAL_OP);
		}		/*  if (data[1]==0) */
		else {
			if (data[1] == 1) {
				switch (ui_NoOfChannel) {
				case 2:
					data[0] =
						(data[0] << (2 *
							data[2])) | ui_Temp;
					break;
				case 4:
					data[0] =
						(data[0] << (4 *
							data[2])) | ui_Temp;
					break;
				case 8:
					data[0] =
						(data[0] << (8 *
							data[2])) | ui_Temp;
					break;
				case 15:
					data[0] = data[0] | ui_Temp;
					break;
				default:
					comedi_error(dev, " chan spec wrong");
					return -EINVAL;	/*  "sorry channel spec wrong " */
				}	/* switch(ui_NoOfChannels) */
				outw(data[0],
					devpriv->iobase + APCI2016_DIGITAL_OP);
			}	/*  if  (data[1]==1) */
			else {
				printk("\nSpecified channel not supported\n");
			}	/*  else if  (data[1]==1) */
		}		/*  else if (data[1]==0) */
	}			/*  if (data[3]==0) */
	else {
		if (data[3] == 1) {
			if (data[1] == 0) {
				data[0] = ~data[0] & 0x1;
				ui_Temp1 = 1;
				ui_Temp1 = ui_Temp1 << ui_NoOfChannel;
				ui_Temp = ui_Temp | ui_Temp1;
				data[0] = (data[0] << ui_NoOfChannel) ^ 0xffff;
				data[0] = data[0] & ui_Temp;
				outw(data[0],
					devpriv->iobase + APCI2016_DIGITAL_OP);
			}	/*  if  (data[1]==0) */
			else {
				if (data[1] == 1) {
					switch (ui_NoOfChannel) {
					case 2:
						data[0] = ~data[0] & 0x3;
						ui_Temp1 = 3;
						ui_Temp1 =
							ui_Temp1 << 2 * data[2];
						ui_Temp = ui_Temp | ui_Temp1;
						data[0] =
							((data[0] << (2 *
									data
									[2])) ^
							0xffff) & ui_Temp;
						break;
					case 4:
						data[0] = ~data[0] & 0xf;
						ui_Temp1 = 15;
						ui_Temp1 =
							ui_Temp1 << 4 * data[2];
						ui_Temp = ui_Temp | ui_Temp1;
						data[0] =
							((data[0] << (4 *
									data
									[2])) ^
							0xffff) & ui_Temp;
						break;
					case 8:
						data[0] = ~data[0] & 0xff;
						ui_Temp1 = 255;
						ui_Temp1 =
							ui_Temp1 << 8 * data[2];
						ui_Temp = ui_Temp | ui_Temp1;
						data[0] =
							((data[0] << (8 *
									data
									[2])) ^
							0xffff) & ui_Temp;
						break;
					case 15:
						break;
					default:
						comedi_error(dev,
							" chan spec wrong");
						return -EINVAL;	/*  "sorry channel spec wrong " */
					}	/* switch(ui_NoOfChannels) */
					outw(data[0],
						devpriv->iobase +
						APCI2016_DIGITAL_OP);
				}	/*  if(data[1]==1) */
				else {
					printk("\nSpecified channel not supported\n");
				}	/* else if(data[1]==1) */
			}	/* elseif(data[1]==0) */
		}		/* if(data[3]==1); */
		else {
			printk("\nSpecified functionality does not exist\n");
			return -EINVAL;
		}		/* if else data[3]==1) */
	}			/* if else data[3]==0) */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2016_BitsDigitalOutput                       |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Read  value  of the selected channel or port           |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     unsigned int ui_NoOfChannels    : No Of Channels To read       |
|                     unsigned int *data              : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI2016_BitsDigitalOutput(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn,
					unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ui_Temp;
	unsigned int ui_NoOfChannel;

	ui_NoOfChannel = CR_CHAN(insn->chanspec);
	if (ui_NoOfChannel > 15) {
		comedi_error(dev,
			"Invalid Channel Numbers !!!, Channel Numbers must be between 0 and 15\n");
		return -EINVAL;
	}			/*  if  ((ui_NoOfChannel<0) || (ui_NoOfChannel>15)) */
	if ((data[0] != 0) && (data[0] != 1)) {
		comedi_error(dev,
			"Invalid Data[0] value !!!, Data[0] should be 0 or 1\n");
		return -EINVAL;
	}			/*  if  ((data[0]!=0) && (data[0]!=1)) */
	ui_Temp = data[0];
	*data = inw(devpriv->iobase + APCI2016_DIGITAL_OP_RW);
	if (ui_Temp == 0) {
		*data = (*data >> ui_NoOfChannel) & 0x1;
	}			/*  if  (ui_Temp==0) */
	else {
		if (ui_Temp == 1) {
			switch (ui_NoOfChannel) {
			case 2:
				*data = (*data >> (2 * data[1])) & 3;
				break;

			case 4:
				*data = (*data >> (4 * data[1])) & 15;
				break;

			case 8:
				*data = (*data >> (8 * data[1])) & 255;
				break;

			case 15:
				break;

			default:
				comedi_error(dev, " chan spec wrong");
				return -EINVAL;	/*  "sorry channel spec wrong " */
			}	/* switch(ui_NoOfChannel) */
		}		/*  if  (ui_Temp==1) */
		else {
			printk("\nSpecified channel not supported \n");
		}		/*  else if  (ui_Temp==1) */
	}			/*  if  (ui_Temp==0) */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2016_ConfigWatchdog                          |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Watchdog                                |
+----------------------------------------------------------------------------+
| Input Parameters  :   struct comedi_device *dev      : Driver handle              |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure |
|                     struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data          : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI2016_ConfigWatchdog(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	if (data[0] == 0) {
		/* Disable the watchdog */
		outw(0x0,
			devpriv->i_IobaseAddon +
			APCI2016_WATCHDOG_ENABLEDISABLE);
		/* Loading the Reload value */
		outw(data[1],
			devpriv->i_IobaseAddon +
			APCI2016_WATCHDOG_RELOAD_VALUE);
		data[1] = data[1] >> 16;
		outw(data[1],
			devpriv->i_IobaseAddon +
			APCI2016_WATCHDOG_RELOAD_VALUE + 2);
	} else {
		printk("\nThe input parameters are wrong\n");
	}
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2016_StartStopWriteWatchdog                  |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Start / Stop The Watchdog                              |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure |
|                     struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data          : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI2016_StartStopWriteWatchdog(struct comedi_device *dev,
					     struct comedi_subdevice *s,
					     struct comedi_insn *insn,
					     unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	switch (data[0]) {
	case 0:		/* stop the watchdog */
		outw(0x0, devpriv->i_IobaseAddon + APCI2016_WATCHDOG_ENABLEDISABLE);	/* disable the watchdog */
		break;
	case 1:		/* start the watchdog */
		outw(0x0001,
			devpriv->i_IobaseAddon +
			APCI2016_WATCHDOG_ENABLEDISABLE);
		break;
	case 2:		/* Software trigger */
		outw(0x0201,
			devpriv->i_IobaseAddon +
			APCI2016_WATCHDOG_ENABLEDISABLE);
		break;
	default:
		printk("\nSpecified functionality does not exist\n");
		return -EINVAL;
	}			/*  switch(data[0]) */

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2016_ReadWatchdog                            |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Read The Watchdog                                      |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure |
|                     struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data          : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

static int i_APCI2016_ReadWatchdog(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	udelay(5);
	data[0] = inw(devpriv->i_IobaseAddon + APCI2016_WATCHDOG_STATUS) & 0x1;
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2016_Reset(struct comedi_device *dev)               |                                                       |
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

static int i_APCI2016_Reset(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	outw(0x0, devpriv->iobase + APCI2016_DIGITAL_OP);	/*  Resets the digital output channels */
	outw(0x0, devpriv->i_IobaseAddon + APCI2016_WATCHDOG_ENABLEDISABLE);
	outw(0x0, devpriv->i_IobaseAddon + APCI2016_WATCHDOG_RELOAD_VALUE);
	outw(0x0, devpriv->i_IobaseAddon + APCI2016_WATCHDOG_RELOAD_VALUE + 2);
	return 0;
}
