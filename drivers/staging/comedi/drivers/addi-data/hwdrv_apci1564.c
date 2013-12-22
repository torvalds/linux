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

@endverbatim
*/
/*

  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstraße 3       D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-------------------------------+---------------------------------------+
  | Project     : APCI-1564       | Compiler   : GCC                      |
  | Module name : hwdrv_apci1564.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-------------------------------+---------------------------------------+
  | Description :   Hardware Layer Access For APCI-1564                   |
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

/*********      Definitions for APCI-1564 card  *****/

#define APCI1564_ADDRESS_RANGE				128

/* DIGITAL INPUT-OUTPUT DEFINE */
/* Input defines */
#define APCI1564_DIGITAL_IP				0x04
#define APCI1564_DIGITAL_IP_INTERRUPT_MODE1		4
#define APCI1564_DIGITAL_IP_INTERRUPT_MODE2		8
#define APCI1564_DIGITAL_IP_IRQ				16

/* Output defines */
#define APCI1564_DIGITAL_OP				0x18
#define APCI1564_DIGITAL_OP_RW				0
#define APCI1564_DIGITAL_OP_INTERRUPT			4
#define APCI1564_DIGITAL_OP_IRQ				12

/* Digital Input IRQ Function Selection */
#define ADDIDATA_OR					0
#define ADDIDATA_AND					1

/* Digital Input Interrupt Status */
#define APCI1564_DIGITAL_IP_INTERRUPT_STATUS		12

/* Digital Output Interrupt Status */
#define APCI1564_DIGITAL_OP_INTERRUPT_STATUS		8

/* Digital Input Interrupt Enable Disable. */
#define APCI1564_DIGITAL_IP_INTERRUPT_ENABLE		0x4
#define APCI1564_DIGITAL_IP_INTERRUPT_DISABLE		0xfffffffb

/* Digital Output Interrupt Enable Disable. */
#define APCI1564_DIGITAL_OP_VCC_INTERRUPT_ENABLE	0x1
#define APCI1564_DIGITAL_OP_VCC_INTERRUPT_DISABLE	0xfffffffe
#define APCI1564_DIGITAL_OP_CC_INTERRUPT_ENABLE		0x2
#define APCI1564_DIGITAL_OP_CC_INTERRUPT_DISABLE	0xfffffffd

/* TIMER COUNTER WATCHDOG DEFINES */

#define ADDIDATA_TIMER					0
#define ADDIDATA_COUNTER				1
#define ADDIDATA_WATCHDOG				2
#define APCI1564_DIGITAL_OP_WATCHDOG			0x28
#define APCI1564_TIMER					0x48
#define APCI1564_COUNTER1				0x0
#define APCI1564_COUNTER2				0x20
#define APCI1564_COUNTER3				0x40
#define APCI1564_COUNTER4				0x60
#define APCI1564_TCW_SYNC_ENABLEDISABLE			0
#define APCI1564_TCW_RELOAD_VALUE			4
#define APCI1564_TCW_TIMEBASE				8
#define APCI1564_TCW_PROG				12
#define APCI1564_TCW_TRIG_STATUS			16
#define APCI1564_TCW_IRQ				20
#define APCI1564_TCW_WARN_TIMEVAL			24
#define APCI1564_TCW_WARN_TIMEBASE			28

/* Global variables */
static unsigned int ui_InterruptStatus_1564 = 0;
static unsigned int ui_InterruptData, ui_Type;

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1564_ConfigDigitalInput                      |
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
static int i_APCI1564_ConfigDigitalInput(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 struct comedi_insn *insn,
					 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	devpriv->tsk_Current = current;
   /*******************************/
	/* Set the digital input logic */
   /*******************************/
	if (data[0] == ADDIDATA_ENABLE) {
		data[2] = data[2] << 4;
		data[3] = data[3] << 4;
		outl(data[2],
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
			APCI1564_DIGITAL_IP_INTERRUPT_MODE1);
		outl(data[3],
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
			APCI1564_DIGITAL_IP_INTERRUPT_MODE2);
		if (data[1] == ADDIDATA_OR) {
			outl(0x4,
				devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
				APCI1564_DIGITAL_IP_IRQ);
		}		/*  if  (data[1] == ADDIDATA_OR) */
		else {
			outl(0x6,
				devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
				APCI1564_DIGITAL_IP_IRQ);
		}		/*  else if  (data[1] == ADDIDATA_OR) */
	}			/*  if  (data[0] == ADDIDATA_ENABLE) */
	else {
		outl(0x0,
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
			APCI1564_DIGITAL_IP_INTERRUPT_MODE1);
		outl(0x0,
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
			APCI1564_DIGITAL_IP_INTERRUPT_MODE2);
		outl(0x0,
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
			APCI1564_DIGITAL_IP_IRQ);
	}			/*  else if  (data[0] == ADDIDATA_ENABLE) */

	return insn->n;
}

static int apci1564_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	data[1] = inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP);

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1564_ConfigDigitalOutput                     |
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
static int i_APCI1564_ConfigDigitalOutput(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ul_Command = 0;

	if ((data[0] != 0) && (data[0] != 1)) {
		comedi_error(dev,
			"Not a valid Data !!! ,Data should be 1 or 0\n");
		return -EINVAL;
	}			/*  if  ((data[0]!=0) && (data[0]!=1)) */
	if (data[0]) {
		devpriv->b_OutputMemoryStatus = ADDIDATA_ENABLE;
	}			/*  if  (data[0]) */
	else {
		devpriv->b_OutputMemoryStatus = ADDIDATA_DISABLE;
	}			/*  else if  (data[0]) */
	if (data[1] == ADDIDATA_ENABLE) {
		ul_Command = ul_Command | 0x1;
	}			/*  if  (data[1] == ADDIDATA_ENABLE) */
	else {
		ul_Command = ul_Command & 0xFFFFFFFE;
	}			/*  else if  (data[1] == ADDIDATA_ENABLE) */
	if (data[2] == ADDIDATA_ENABLE) {
		ul_Command = ul_Command | 0x2;
	}			/*  if  (data[2] == ADDIDATA_ENABLE) */
	else {
		ul_Command = ul_Command & 0xFFFFFFFD;
	}			/*  else if  (data[2] == ADDIDATA_ENABLE) */
	outl(ul_Command,
		devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP +
		APCI1564_DIGITAL_OP_INTERRUPT);
	ui_InterruptData =
		inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP +
		APCI1564_DIGITAL_OP_INTERRUPT);
	devpriv->tsk_Current = current;
	return insn->n;
}

static int apci1564_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	s->state = inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP +
			APCI1564_DIGITAL_OP_RW);

	if (comedi_dio_update_state(s, data))
		outl(s->state, devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP +
			APCI1564_DIGITAL_OP_RW);

	data[1] = s->state;

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1564_ConfigTimerCounterWatchdog              |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Timer , Counter or Watchdog             |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|					  data[0]            : 0 Configure As Timer      |
|										   1 Configure As Counter    |
|										   2 Configure As Watchdog   |
|					  data[1]            : 1 Enable  Interrupt       |
|										   0 Disable Interrupt 	     |
|					  data[2]            : Time Unit                 |
|					  data[3]			 : Reload Value			     |
|					  data[4]            : Timer Mode             	 |
|					  data[5]			 : Timer Counter Watchdog Number|
                              data[6]            :  Counter Direction
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI1564_ConfigTimerCounterWatchdog(struct comedi_device *dev,
						 struct comedi_subdevice *s,
						 struct comedi_insn *insn,
						 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ul_Command1 = 0;

	devpriv->tsk_Current = current;
	if (data[0] == ADDIDATA_WATCHDOG) {
		devpriv->b_TimerSelectMode = ADDIDATA_WATCHDOG;

		/* Disable the watchdog */
		outl(0x0,
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP_WATCHDOG +
			APCI1564_TCW_PROG);
		/* Loading the Reload value */
		outl(data[3],
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP_WATCHDOG +
			APCI1564_TCW_RELOAD_VALUE);
	}			/*  if  (data[0]==ADDIDATA_WATCHDOG) */
	else if (data[0] == ADDIDATA_TIMER) {
		/* First Stop The Timer */
		ul_Command1 =
			inl(devpriv->i_IobaseAmcc + APCI1564_TIMER +
			APCI1564_TCW_PROG);
		ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
		outl(ul_Command1, devpriv->i_IobaseAmcc + APCI1564_TIMER + APCI1564_TCW_PROG);	/* Stop The Timer */

		devpriv->b_TimerSelectMode = ADDIDATA_TIMER;
		if (data[1] == 1) {
			outl(0x02, devpriv->i_IobaseAmcc + APCI1564_TIMER + APCI1564_TCW_PROG);	/* Enable TIMER int & DISABLE ALL THE OTHER int SOURCES */
			outl(0x0,
				devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
				APCI1564_DIGITAL_IP_IRQ);
			outl(0x0,
				devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP +
				APCI1564_DIGITAL_OP_IRQ);
			outl(0x0,
				devpriv->i_IobaseAmcc +
				APCI1564_DIGITAL_OP_WATCHDOG +
				APCI1564_TCW_IRQ);
			outl(0x0,
				devpriv->iobase + APCI1564_COUNTER1 +
				APCI1564_TCW_IRQ);
			outl(0x0,
				devpriv->iobase + APCI1564_COUNTER2 +
				APCI1564_TCW_IRQ);
			outl(0x0,
				devpriv->iobase + APCI1564_COUNTER3 +
				APCI1564_TCW_IRQ);
			outl(0x0,
				devpriv->iobase + APCI1564_COUNTER4 +
				APCI1564_TCW_IRQ);
		}		/*  if  (data[1]==1) */
		else {
			outl(0x0, devpriv->i_IobaseAmcc + APCI1564_TIMER + APCI1564_TCW_PROG);	/* disable Timer interrupt */
		}		/*  else if  (data[1]==1) */

		/*  Loading Timebase */

		outl(data[2],
			devpriv->i_IobaseAmcc + APCI1564_TIMER +
			APCI1564_TCW_TIMEBASE);

		/* Loading the Reload value */
		outl(data[3],
			devpriv->i_IobaseAmcc + APCI1564_TIMER +
			APCI1564_TCW_RELOAD_VALUE);

		ul_Command1 =
			inl(devpriv->i_IobaseAmcc + APCI1564_TIMER +
			APCI1564_TCW_PROG);
		ul_Command1 =
			(ul_Command1 & 0xFFF719E2UL) | 2UL << 13UL | 0x10UL;
		outl(ul_Command1, devpriv->i_IobaseAmcc + APCI1564_TIMER + APCI1564_TCW_PROG);	/* mode 2 */
	}			/*  else if  (data[0]==ADDIDATA_TIMER) */
	else if (data[0] == ADDIDATA_COUNTER) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		devpriv->b_ModeSelectRegister = data[5];

		/* First Stop The Counter */
		ul_Command1 =
			inl(devpriv->iobase + ((data[5] - 1) * 0x20) +
			APCI1564_TCW_PROG);
		ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
		outl(ul_Command1, devpriv->iobase + ((data[5] - 1) * 0x20) + APCI1564_TCW_PROG);	/* Stop The Timer */

      /************************/
		/* Set the reload value */
      /************************/
		outl(data[3],
			devpriv->iobase + ((data[5] - 1) * 0x20) +
			APCI1564_TCW_RELOAD_VALUE);

      /******************************/
		/* Set the mode :             */
		/* - Disable the hardware     */
		/* - Disable the counter mode */
		/* - Disable the warning      */
		/* - Disable the reset        */
		/* - Disable the timer mode   */
		/* - Enable the counter mode  */
      /******************************/
		ul_Command1 =
			(ul_Command1 & 0xFFFC19E2UL) | 0x80000UL |
			(unsigned int) ((unsigned int) data[4] << 16UL);
		outl(ul_Command1,
			devpriv->iobase + ((data[5] - 1) * 0x20) +
			APCI1564_TCW_PROG);

		/*  Enable or Disable Interrupt */
		ul_Command1 = (ul_Command1 & 0xFFFFF9FD) | (data[1] << 1);
		outl(ul_Command1,
			devpriv->iobase + ((data[5] - 1) * 0x20) +
			APCI1564_TCW_PROG);

      /*****************************/
		/* Set the Up/Down selection */
      /*****************************/
		ul_Command1 = (ul_Command1 & 0xFFFBF9FFUL) | (data[6] << 18);
		outl(ul_Command1,
			devpriv->iobase + ((data[5] - 1) * 0x20) +
			APCI1564_TCW_PROG);
	}			/*  else if  (data[0]==ADDIDATA_COUNTER) */
	else {
		printk(" Invalid subdevice.");
	}			/*  else if  (data[0]==ADDIDATA_WATCHDOG) */

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1564_StartStopWriteTimerCounterWatchdog      |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Start / Stop The Selected Timer , Counter or Watchdog  |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|					  data[0]            : 0 Timer                   |
|										   1 Counter                 |
|										   2 Watchdog        		 |                             |					         data[1]            : 1 Start                   |
|										   0 Stop                    |
|                                                  2 Trigger             	 |
|                                                    Clear (Only Counter)    |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI1564_StartStopWriteTimerCounterWatchdog(struct comedi_device *dev,
							 struct comedi_subdevice *s,
							 struct comedi_insn *insn,
							 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ul_Command1 = 0;

	if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
		switch (data[1]) {
		case 0:	/* stop the watchdog */
			outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP_WATCHDOG + APCI1564_TCW_PROG);	/* disable the watchdog */
			break;
		case 1:	/* start the watchdog */
			outl(0x0001,
				devpriv->i_IobaseAmcc +
				APCI1564_DIGITAL_OP_WATCHDOG +
				APCI1564_TCW_PROG);
			break;
		case 2:	/* Software trigger */
			outl(0x0201,
				devpriv->i_IobaseAmcc +
				APCI1564_DIGITAL_OP_WATCHDOG +
				APCI1564_TCW_PROG);
			break;
		default:
			printk("\nSpecified functionality does not exist\n");
			return -EINVAL;
		}		/*  switch (data[1]) */
	}			/*  if  (devpriv->b_TimerSelectMode==ADDIDATA_WATCHDOG) */
	if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		if (data[1] == 1) {
			ul_Command1 =
				inl(devpriv->i_IobaseAmcc + APCI1564_TIMER +
				APCI1564_TCW_PROG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;

			/* Enable the Timer */
			outl(ul_Command1,
				devpriv->i_IobaseAmcc + APCI1564_TIMER +
				APCI1564_TCW_PROG);
		}		/*  if  (data[1]==1) */
		else if (data[1] == 0) {
			/* Stop The Timer */

			ul_Command1 =
				inl(devpriv->i_IobaseAmcc + APCI1564_TIMER +
				APCI1564_TCW_PROG);
			ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
			outl(ul_Command1,
				devpriv->i_IobaseAmcc + APCI1564_TIMER +
				APCI1564_TCW_PROG);
		}		/*  else if(data[1]==0) */
	}			/*  if  (devpriv->b_TimerSelectMode==ADDIDATA_TIMER) */
	if (devpriv->b_TimerSelectMode == ADDIDATA_COUNTER) {
		ul_Command1 =
			inl(devpriv->iobase + ((devpriv->b_ModeSelectRegister -
					1) * 0x20) + APCI1564_TCW_PROG);
		if (data[1] == 1) {
			/* Start the Counter subdevice */
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;
		}		/*  if  (data[1] == 1) */
		else if (data[1] == 0) {
			/*  Stops the Counter subdevice */
			ul_Command1 = 0;

		}		/*  else if  (data[1] == 0) */
		else if (data[1] == 2) {
			/*  Clears the Counter subdevice */
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x400;
		}		/*  else if  (data[1] == 3) */
		outl(ul_Command1,
			devpriv->iobase + ((devpriv->b_ModeSelectRegister -
					1) * 0x20) + APCI1564_TCW_PROG);
	}			/*  if (devpriv->b_TimerSelectMode==ADDIDATA_COUNTER) */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1564_ReadTimerCounterWatchdog                |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Read The Selected Timer , Counter or Watchdog          |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |

+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI1564_ReadTimerCounterWatchdog(struct comedi_device *dev,
					       struct comedi_subdevice *s,
					       struct comedi_insn *insn,
					       unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ul_Command1 = 0;

	if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
		/*  Stores the status of the Watchdog */
		data[0] =
			inl(devpriv->i_IobaseAmcc +
			APCI1564_DIGITAL_OP_WATCHDOG +
			APCI1564_TCW_TRIG_STATUS) & 0x1;
		data[1] =
			inl(devpriv->i_IobaseAmcc +
			APCI1564_DIGITAL_OP_WATCHDOG);
	}			/*  if  (devpriv->b_TimerSelectMode==ADDIDATA_WATCHDOG) */
	else if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		/*  Stores the status of the Timer */
		data[0] =
			inl(devpriv->i_IobaseAmcc + APCI1564_TIMER +
			APCI1564_TCW_TRIG_STATUS) & 0x1;

		/*  Stores the Actual value of the Timer */
		data[1] = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER);
	}			/*  else if  (devpriv->b_TimerSelectMode==ADDIDATA_TIMER) */
	else if (devpriv->b_TimerSelectMode == ADDIDATA_COUNTER) {
		/*  Read the Counter Actual Value. */
		data[0] =
			inl(devpriv->iobase + ((devpriv->b_ModeSelectRegister -
					1) * 0x20) +
			APCI1564_TCW_SYNC_ENABLEDISABLE);
		ul_Command1 =
			inl(devpriv->iobase + ((devpriv->b_ModeSelectRegister -
					1) * 0x20) + APCI1564_TCW_TRIG_STATUS);

      /***********************************/
		/* Get the software trigger status */
      /***********************************/
		data[1] = (unsigned char) ((ul_Command1 >> 1) & 1);

      /***********************************/
		/* Get the hardware trigger status */
      /***********************************/
		data[2] = (unsigned char) ((ul_Command1 >> 2) & 1);

      /*********************************/
		/* Get the software clear status */
      /*********************************/
		data[3] = (unsigned char) ((ul_Command1 >> 3) & 1);

      /***************************/
		/* Get the overflow status */
      /***************************/
		data[4] = (unsigned char) ((ul_Command1 >> 0) & 1);
	}			/*  else  if  (devpriv->b_TimerSelectMode==ADDIDATA_COUNTER) */
	else if ((devpriv->b_TimerSelectMode != ADDIDATA_TIMER)
		&& (devpriv->b_TimerSelectMode != ADDIDATA_WATCHDOG)
		&& (devpriv->b_TimerSelectMode != ADDIDATA_COUNTER)) {
		printk("\n Invalid Subdevice !!!\n");
	}			/*  else if ((devpriv->b_TimerSelectMode!=ADDIDATA_TIMER) && (devpriv->b_TimerSelectMode!=ADDIDATA_WATCHDOG)&& (devpriv->b_TimerSelectMode!=ADDIDATA_COUNTER)) */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   :  int i_APCI1564_ReadInterruptStatus                    |
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

static int i_APCI1564_ReadInterruptStatus(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	*data = ui_Type;
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : static void v_APCI1564_Interrupt					     |
|					  (int irq , void *d)      |
+----------------------------------------------------------------------------+
| Task              : Interrupt handler for the interruptible digital inputs |
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
static void v_APCI1564_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct addi_private *devpriv = dev->private;
	unsigned int ui_DO, ui_DI;
	unsigned int ui_Timer;
	unsigned int ui_C1, ui_C2, ui_C3, ui_C4;
	unsigned int ul_Command2 = 0;

	ui_DI = inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
		APCI1564_DIGITAL_IP_IRQ) & 0x01;
	ui_DO = inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP +
		APCI1564_DIGITAL_OP_IRQ) & 0x01;
	ui_Timer =
		inl(devpriv->i_IobaseAmcc + APCI1564_TIMER +
		APCI1564_TCW_IRQ) & 0x01;
	ui_C1 = inl(devpriv->iobase + APCI1564_COUNTER1 +
		APCI1564_TCW_IRQ) & 0x1;
	ui_C2 = inl(devpriv->iobase + APCI1564_COUNTER2 +
		APCI1564_TCW_IRQ) & 0x1;
	ui_C3 = inl(devpriv->iobase + APCI1564_COUNTER3 +
		APCI1564_TCW_IRQ) & 0x1;
	ui_C4 = inl(devpriv->iobase + APCI1564_COUNTER4 +
		APCI1564_TCW_IRQ) & 0x1;
	if (ui_DI == 0 && ui_DO == 0 && ui_Timer == 0 && ui_C1 == 0
		&& ui_C2 == 0 && ui_C3 == 0 && ui_C4 == 0) {
		printk("\nInterrupt from unknown source\n");
	}			/*  if(ui_DI==0 && ui_DO==0 && ui_Timer==0 && ui_C1==0 && ui_C2==0 && ui_C3==0 && ui_C4==0) */

	if (ui_DI == 1) {
		ui_DI = inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
			APCI1564_DIGITAL_IP_IRQ);
		outl(0x0,
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
			APCI1564_DIGITAL_IP_IRQ);
		ui_InterruptStatus_1564 =
			inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP +
			APCI1564_DIGITAL_IP_INTERRUPT_STATUS);
		ui_InterruptStatus_1564 = ui_InterruptStatus_1564 & 0X000FFFF0;
		send_sig(SIGIO, devpriv->tsk_Current, 0);	/*  send signal to the sample */
		outl(ui_DI, devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP + APCI1564_DIGITAL_IP_IRQ);	/* enable the interrupt */
		return;
	}

	if (ui_DO == 1) {
		/*  Check for Digital Output interrupt Type - 1: Vcc interrupt 2: CC interrupt. */
		ui_Type =
			inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP +
			APCI1564_DIGITAL_OP_INTERRUPT_STATUS) & 0x3;
		/* Disable the  Interrupt */
		outl(0x0,
			devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP +
			APCI1564_DIGITAL_OP_INTERRUPT);

		/* Sends signal to user space */
		send_sig(SIGIO, devpriv->tsk_Current, 0);

	}			/*  if  (ui_DO) */

	if (ui_Timer == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_TIMER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Timer Interrupt */
			ul_Command2 =
				inl(devpriv->i_IobaseAmcc + APCI1564_TIMER +
				    APCI1564_TCW_PROG);
			outl(0x0,
			     devpriv->i_IobaseAmcc + APCI1564_TIMER +
			     APCI1564_TCW_PROG);

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Timer Interrupt */

			outl(ul_Command2,
			     devpriv->i_IobaseAmcc + APCI1564_TIMER +
			     APCI1564_TCW_PROG);
		}
	}/* if  (ui_Timer == 1) */


	if (ui_C1 == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Counter Interrupt */
			ul_Command2 =
				inl(devpriv->iobase + APCI1564_COUNTER1 +
				    APCI1564_TCW_PROG);
			outl(0x0,
			     devpriv->iobase + APCI1564_COUNTER1 +
			     APCI1564_TCW_PROG);

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Counter Interrupt */
			outl(ul_Command2,
			     devpriv->iobase + APCI1564_COUNTER1 +
			     APCI1564_TCW_PROG);
		}
	} /* if  (ui_C1 == 1) */

	if (ui_C2 == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Counter Interrupt */
			ul_Command2 =
				inl(devpriv->iobase + APCI1564_COUNTER2 +
				    APCI1564_TCW_PROG);
			outl(0x0,
			     devpriv->iobase + APCI1564_COUNTER2 +
			     APCI1564_TCW_PROG);

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Counter Interrupt */
			outl(ul_Command2,
			     devpriv->iobase + APCI1564_COUNTER2 +
			     APCI1564_TCW_PROG);
		}
	} /*  if  ((ui_C2 == 1) */

	if (ui_C3 == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Counter Interrupt */
			ul_Command2 =
				inl(devpriv->iobase + APCI1564_COUNTER3 +
				    APCI1564_TCW_PROG);
			outl(0x0,
			     devpriv->iobase + APCI1564_COUNTER3 +
			     APCI1564_TCW_PROG);

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Counter Interrupt */
			outl(ul_Command2,
			     devpriv->iobase + APCI1564_COUNTER3 +
			     APCI1564_TCW_PROG);
		}
	}	/*  if ((ui_C3 == 1) */

	if (ui_C4 == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Counter Interrupt */
			ul_Command2 =
				inl(devpriv->iobase + APCI1564_COUNTER4 +
				    APCI1564_TCW_PROG);
			outl(0x0,
			     devpriv->iobase + APCI1564_COUNTER4 +
			     APCI1564_TCW_PROG);

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Counter Interrupt */
			outl(ul_Command2,
			     devpriv->iobase + APCI1564_COUNTER4 +
			     APCI1564_TCW_PROG);
		}
	}	/*  if (ui_C4 == 1) */
	return;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1564_Reset(struct comedi_device *dev)               |                                                       |
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

static int i_APCI1564_Reset(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP_IRQ);	/* disable the interrupts */
	inl(devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP_INTERRUPT_STATUS);	/* Reset the interrupt status register */
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP_INTERRUPT_MODE1);	/* Disable the and/or interrupt */
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DIGITAL_IP_INTERRUPT_MODE2);
	devpriv->b_DigitalOutputRegister = 0;
	ui_Type = 0;
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP);	/* Resets the output channels */
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP_INTERRUPT);	/* Disables the interrupt. */
	outl(0x0,
		devpriv->i_IobaseAmcc + APCI1564_DIGITAL_OP_WATCHDOG +
		APCI1564_TCW_RELOAD_VALUE);
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_TIMER);
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_TIMER + APCI1564_TCW_PROG);

	outl(0x0, devpriv->iobase + APCI1564_COUNTER1 + APCI1564_TCW_PROG);
	outl(0x0, devpriv->iobase + APCI1564_COUNTER2 + APCI1564_TCW_PROG);
	outl(0x0, devpriv->iobase + APCI1564_COUNTER3 + APCI1564_TCW_PROG);
	outl(0x0, devpriv->iobase + APCI1564_COUNTER4 + APCI1564_TCW_PROG);
	return 0;
}
