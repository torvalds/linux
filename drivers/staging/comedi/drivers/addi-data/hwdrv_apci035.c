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
/*

  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstraße 3       D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-------------------------------+---------------------------------------+
  | Project     : APCI-035        | Compiler   : GCC                      |
  | Module name : hwdrv_apci035.c | Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-------------------------------+---------------------------------------+
  | Description :   Hardware Layer Acces For APCI-035                     |
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
#include "hwdrv_apci035.h"
int i_WatchdogNbr = 0;
int i_Temp = 0;
int i_Flag = 1;
/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI035_ConfigTimerWatchdog                      |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Timer , Counter or Watchdog             |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     UINT *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|					  data[0]            : 0 Configure As Timer      |
|										   1 Configure As Watchdog   |
                              data[1]            : Watchdog number
|					  data[2]            : Time base Unit            |
|					  data[3]			 : Reload Value			     |
                              data[4]            : External Trigger          |
                                                   1:Enable
                                                   0:Disable
                              data[5]            :External Trigger Level
                                                  00 Trigger Disabled
                                                  01 Trigger Enabled (Low level)
                                                  10 Trigger Enabled (High Level)
                                                  11 Trigger Enabled (High/Low level)
                              data[6]            : External Gate            |
                                                   1:Enable
                                                   0:Disable
                              data[7]            : External Gate level
                                                  00 Gate Disabled
                                                  01 Gate Enabled (Low level)
                                                  10 Gate Enabled (High Level)
                              data[8]            :Warning Relay
                                                  1: ENABLE
                                                  0: DISABLE
                              data[9]            :Warning Delay available
                              data[10]           :Warning Relay Time unit
                              data[11]           :Warning Relay Time Reload value
                              data[12]           :Reset Relay
                                                  1 : ENABLE
                                                  0 : DISABLE
                              data[13]           :Interrupt
                                                  1 : ENABLE
                                                  0 : DISABLE

|
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI035_ConfigTimerWatchdog(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	UINT ui_Status = 0;
	UINT ui_Command = 0;
	UINT ui_Mode = 0;
	i_Temp = 0;
	devpriv->tsk_Current = current;
	devpriv->b_TimerSelectMode = data[0];
	i_WatchdogNbr = data[1];
	if (data[0] == 0) {
		ui_Mode = 2;
	} else {
		ui_Mode = 0;
	}
//ui_Command = inl(devpriv->iobase+((i_WatchdogNbr-1)*32)+12);
	ui_Command = 0;
//ui_Command = ui_Command & 0xFFFFF9FEUL;
	outl(ui_Command, devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	ui_Command = 0;
	ui_Command = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
/************************/
/* Set the reload value */
/************************/
	outl(data[3], devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 4);
/*********************/
/* Set the time unit */
/*********************/
	outl(data[2], devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 8);
	if (data[0] == ADDIDATA_TIMER) {

		 /******************************/
		/* Set the mode :             */
		/* - Disable the hardware     */
		/* - Disable the counter mode */
		/* - Disable the warning      */
		/* - Disable the reset        */
		/* - Enable the timer mode    */
		/* - Set the timer mode       */
		 /******************************/

		ui_Command =
			(ui_Command & 0xFFF719E2UL) | ui_Mode << 13UL | 0x10UL;

	}			//if (data[0] == ADDIDATA_TIMER)
	else {
		if (data[0] == ADDIDATA_WATCHDOG) {

		 /******************************/
			/* Set the mode :             */
			/* - Disable the hardware     */
			/* - Disable the counter mode */
			/* - Disable the warning      */
			/* - Disable the reset        */
			/* - Disable the timer mode   */
		 /******************************/

			ui_Command = ui_Command & 0xFFF819E2UL;

		} else {
			printk("\n The parameter for Timer/watchdog selection is in error\n");
			return -EINVAL;
		}
	}
	outl(ui_Command, devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	ui_Command = 0;
	ui_Command = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
/********************************/
/* Disable the hardware trigger */
/********************************/
	ui_Command = ui_Command & 0xFFFFF89FUL;
	if (data[4] == ADDIDATA_ENABLE) {
    /**********************************/
		/* Set the hardware trigger level */
    /**********************************/
		ui_Command = ui_Command | (data[5] << 5);
	}
	outl(ui_Command, devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	ui_Command = 0;
	ui_Command = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
/*****************************/
/* Disable the hardware gate */
/*****************************/
	ui_Command = ui_Command & 0xFFFFF87FUL;
	if (data[6] == ADDIDATA_ENABLE) {
/*******************************/
/* Set the hardware gate level */
/*******************************/
		ui_Command = ui_Command | (data[7] << 7);
	}
	outl(ui_Command, devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	ui_Command = 0;
	ui_Command = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
/*******************************/
/* Disable the hardware output */
/*******************************/
	ui_Command = ui_Command & 0xFFFFF9FBUL;
/*********************************/
/* Set the hardware output level */
/*********************************/
	ui_Command = ui_Command | (data[8] << 2);
	outl(ui_Command, devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	if (data[9] == ADDIDATA_ENABLE) {
   /************************/
		/* Set the reload value */
   /************************/
		outl(data[11],
			devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 24);
   /**********************/
		/* Set the time unite */
   /**********************/
		outl(data[10],
			devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 28);
	}

	ui_Command = 0;
	ui_Command = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
 /*******************************/
	/* Disable the hardware output */
 /*******************************/
	ui_Command = ui_Command & 0xFFFFF9F7UL;
   /*********************************/
	/* Set the hardware output level */
   /*********************************/
	ui_Command = ui_Command | (data[12] << 3);
	outl(ui_Command, devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
 /*************************************/
 /**  Enable the watchdog interrupt  **/
 /*************************************/
	ui_Command = 0;
	ui_Command = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
/*******************************/
/* Set the interrupt selection */
/*******************************/
	ui_Status = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 16);

	ui_Command = (ui_Command & 0xFFFFF9FDUL) | (data[13] << 1);
	outl(ui_Command, devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI035_StartStopWriteTimerWatchdog              |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Start / Stop The Selected Timer , or Watchdog  |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     UINT *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|					                                                 |
|					  data[0] : 0 - Stop Selected Timer/Watchdog     |
|					            1 - Start Selected Timer/Watchdog    |
|					            2 - Trigger Selected Timer/Watchdog  |
|					            3 - Stop All Timer/Watchdog          |
|					            4 - Start All Timer/Watchdog         |
|					            5 - Trigger All Timer/Watchdog       |
|					                                                 |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error			 |
|					                                                 |
+----------------------------------------------------------------------------+
*/
int i_APCI035_StartStopWriteTimerWatchdog(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	UINT ui_Command = 0;
	int i_Count = 0;
	if (data[0] == 1) {
		ui_Command =
			inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	 /**********************/
		/* Start the hardware */
	 /**********************/
		ui_Command = (ui_Command & 0xFFFFF9FFUL) | 0x1UL;
		outl(ui_Command,
			devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	}			// if  (data[0]==1)
	if (data[0] == 2) {
		ui_Command =
			inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	 /***************************/
		/* Set the trigger command */
	 /***************************/
		ui_Command = (ui_Command & 0xFFFFF9FFUL) | 0x200UL;
		outl(ui_Command,
			devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	}

	if (data[0] == 0)	//Stop The Watchdog
	{
		//Stop The Watchdog
		ui_Command = 0;
		//ui_Command = inl(devpriv->iobase+((i_WatchdogNbr-1)*32)+12);
		//ui_Command = ui_Command & 0xFFFFF9FEUL;
		outl(ui_Command,
			devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 12);
	}			//  if (data[1]==0)
	if (data[0] == 3)	//stop all Watchdogs
	{
		ui_Command = 0;
		for (i_Count = 1; i_Count <= 4; i_Count++) {
			if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
				ui_Command = 0x2UL;
			} else {
				ui_Command = 0x10UL;
			}
			i_WatchdogNbr = i_Count;
			outl(ui_Command,
				devpriv->iobase + ((i_WatchdogNbr - 1) * 32) +
				0);
		}

	}
	if (data[0] == 4)	//start all Watchdogs
	{
		ui_Command = 0;
		for (i_Count = 1; i_Count <= 4; i_Count++) {
			if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
				ui_Command = 0x1UL;
			} else {
				ui_Command = 0x8UL;
			}
			i_WatchdogNbr = i_Count;
			outl(ui_Command,
				devpriv->iobase + ((i_WatchdogNbr - 1) * 32) +
				0);
		}
	}
	if (data[0] == 5)	//trigger all Watchdogs
	{
		ui_Command = 0;
		for (i_Count = 1; i_Count <= 4; i_Count++) {
			if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
				ui_Command = 0x4UL;
			} else {
				ui_Command = 0x20UL;
			}

			i_WatchdogNbr = i_Count;
			outl(ui_Command,
				devpriv->iobase + ((i_WatchdogNbr - 1) * 32) +
				0);
		}
		i_Temp = 1;
	}
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI035_ReadTimerWatchdog                        |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Read The Selected Timer , Counter or Watchdog          |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     UINT *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|     																	 |
+----------------------------------------------------------------------------+
| Output Parameters :	data[0]            : software trigger status
              data[1]            : hardware trigger status
|     				data[2]            : Software clear status
                        data[3]            : Overflow status
                     data[4]            : Timer actual value


+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI035_ReadTimerWatchdog(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	UINT ui_Status = 0;	// Status register
	i_WatchdogNbr = insn->unused[0];
	      /******************/
	/* Get the status */
	      /******************/
	ui_Status = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 16);
      /***********************************/
	/* Get the software trigger status */
      /***********************************/
	data[0] = ((ui_Status >> 1) & 1);
      /***********************************/
	/* Get the hardware trigger status */
      /***********************************/
	data[1] = ((ui_Status >> 2) & 1);
      /*********************************/
	/* Get the software clear status */
      /*********************************/
	data[2] = ((ui_Status >> 3) & 1);
      /***************************/
	/* Get the overflow status */
      /***************************/
	data[3] = ((ui_Status >> 0) & 1);
	if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		data[4] = inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 0);

	}			//  if  (devpriv->b_TimerSelectMode==ADDIDATA_TIMER)

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI035_ConfigAnalogInput                        |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Analog Input Subdevice                  |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     struct comedi_subdevice *s     : Subdevice Pointer            |
|                     struct comedi_insn *insn       : Insn Structure Pointer       |
|                     unsigned int *data          : Data Pointer contains        |
|                                          configuration parameters as below |
|                     data[0]                  : Warning delay value
|                                                                            |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI035_ConfigAnalogInput(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	devpriv->tsk_Current = current;
	outl(0x200 | 0, devpriv->iobase + 128 + 0x4);
	outl(0, devpriv->iobase + 128 + 0);
/********************************/
/* Initialise the warning value */
/********************************/
	outl(0x300 | 0, devpriv->iobase + 128 + 0x4);
	outl((data[0] << 8), devpriv->iobase + 128 + 0);
	outl(0x200000UL, devpriv->iobase + 128 + 12);

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI035_ReadAnalogInput                          |
|			          (struct comedi_device *dev,struct comedi_subdevice *s,       |
|                     struct comedi_insn *insn,unsigned int *data)                      |
+----------------------------------------------------------------------------+
| Task              : Read  value  of the selected channel			         |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     UINT ui_NoOfChannels    : No Of Channels To read       |
|                     UINT *data              : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
|			          data[0]  : Digital Value Of Input              |
|			                                                         |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI035_ReadAnalogInput(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	UINT ui_CommandRegister = 0;
/******************/
/*  Set the start */
/******************/
	ui_CommandRegister = 0x80000;
 /******************************/
	/* Write the command register */
 /******************************/
	outl(ui_CommandRegister, devpriv->iobase + 128 + 8);

/***************************************/
/* Read the digital value of the input */
/***************************************/
	data[0] = inl(devpriv->iobase + 128 + 28);
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   :  int i_APCI035_Reset(struct comedi_device *dev)			     |
|					                                                         |
+----------------------------------------------------------------------------+
| Task              :Resets the registers of the card                        |
+----------------------------------------------------------------------------+
| Input Parameters  :                                                        |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      :                                                        |
|			                                                                 |
+----------------------------------------------------------------------------+
*/
int i_APCI035_Reset(struct comedi_device * dev)
{
	int i_Count = 0;
	for (i_Count = 1; i_Count <= 4; i_Count++) {
		i_WatchdogNbr = i_Count;
		outl(0x0, devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 0);	//stop all timers
	}
	outl(0x0, devpriv->iobase + 128 + 12);	//Disable the warning delay

	return 0;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : static void v_APCI035_Interrupt					     |
|					  (int irq , void *d)      |
+----------------------------------------------------------------------------+
| Task              : Interrupt processing Routine                           |
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
static void v_APCI035_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	UINT ui_StatusRegister1 = 0;
	UINT ui_StatusRegister2 = 0;
	UINT ui_ReadCommand = 0;
	UINT ui_ChannelNumber = 0;
	UINT ui_DigitalTemperature = 0;
	if (i_Temp == 1) {
		i_WatchdogNbr = i_Flag;
		i_Flag = i_Flag + 1;
	}
  /**************************************/
	/* Read the interrupt status register of temperature Warning */
  /**************************************/
	ui_StatusRegister1 = inl(devpriv->iobase + 128 + 16);
  /**************************************/
	/* Read the interrupt status register for Watchdog/timer */
   /**************************************/

	ui_StatusRegister2 =
		inl(devpriv->iobase + ((i_WatchdogNbr - 1) * 32) + 20);

	if ((((ui_StatusRegister1) & 0x8) == 0x8))	//Test if warning relay interrupt
	{
	/**********************************/
		/* Disable the temperature warning */
	/**********************************/
		ui_ReadCommand = inl(devpriv->iobase + 128 + 12);
		ui_ReadCommand = ui_ReadCommand & 0xFFDF0000UL;
		outl(ui_ReadCommand, devpriv->iobase + 128 + 12);
      /***************************/
		/* Read the channel number */
      /***************************/
		ui_ChannelNumber = inl(devpriv->iobase + 128 + 60);
	/**************************************/
		/* Read the digital temperature value */
	/**************************************/
		ui_DigitalTemperature = inl(devpriv->iobase + 128 + 60);
		send_sig(SIGIO, devpriv->tsk_Current, 0);	// send signal to the sample
	}			//if (((ui_StatusRegister1 & 0x8) == 0x8))

	else {
		if ((ui_StatusRegister2 & 0x1) == 0x1) {
			send_sig(SIGIO, devpriv->tsk_Current, 0);	// send signal to the sample
		}
	}			//else if (((ui_StatusRegister1 & 0x8) == 0x8))

	return;
}
