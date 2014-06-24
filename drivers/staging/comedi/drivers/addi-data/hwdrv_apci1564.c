/*
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data.com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 */

#define APCI1564_ADDRESS_RANGE				128

/* Digital Input IRQ Function Selection */
#define ADDIDATA_OR					0
#define ADDIDATA_AND					1

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
#define APCI1564_COUNTER1				0
#define APCI1564_COUNTER2				1
#define APCI1564_COUNTER3				2
#define APCI1564_COUNTER4				3

/*
 * devpriv->i_IobaseAmcc Register Map
 */
#define APCI1564_DI_REG					0x04
#define APCI1564_DI_INT_MODE1_REG			0x08
#define APCI1564_DI_INT_MODE2_REG			0x0c
#define APCI1564_DI_INT_STATUS_REG			0x10
#define APCI1564_DI_IRQ_REG				0x14
#define APCI1564_DO_REG					0x18
#define APCI1564_DO_INT_CTRL_REG			0x1c
#define APCI1564_DO_INT_STATUS_REG			0x20
#define APCI1564_DO_IRQ_REG				0x24
#define APCI1564_WDOG_REG				0x28
#define APCI1564_WDOG_RELOAD_REG			0x2c
#define APCI1564_WDOG_TIMEBASE_REG			0x30
#define APCI1564_WDOG_CTRL_REG				0x34
#define APCI1564_WDOG_STATUS_REG			0x38
#define APCI1564_WDOG_IRQ_REG				0x3c
#define APCI1564_WDOG_WARN_TIMEVAL_REG			0x40
#define APCI1564_WDOG_WARN_TIMEBASE_REG			0x44
#define APCI1564_TIMER_REG				0x48
#define APCI1564_TIMER_RELOAD_REG			0x4c
#define APCI1564_TIMER_TIMEBASE_REG			0x50
#define APCI1564_TIMER_CTRL_REG				0x54
#define APCI1564_TIMER_STATUS_REG			0x58
#define APCI1564_TIMER_IRQ_REG				0x5c
#define APCI1564_TIMER_WARN_TIMEVAL_REG			0x60
#define APCI1564_TIMER_WARN_TIMEBASE_REG		0x64

/*
 * devpriv->iobase Register Map
 */
#define APCI1564_TCW_REG(x)				(0x00 + ((x) * 0x20))
#define APCI1564_TCW_RELOAD_REG(x)			(0x04 + ((x) * 0x20))
#define APCI1564_TCW_TIMEBASE_REG(x)			(0x08 + ((x) * 0x20))
#define APCI1564_TCW_CTRL_REG(x)			(0x0c + ((x) * 0x20))
#define APCI1564_TCW_STATUS_REG(x)			(0x10 + ((x) * 0x20))
#define APCI1564_TCW_IRQ_REG(x)				(0x14 + ((x) * 0x20))
#define APCI1564_TCW_WARN_TIMEVAL_REG(x)		(0x18 + ((x) * 0x20))
#define APCI1564_TCW_WARN_TIMEBASE_REG(x)		(0x1c + ((x) * 0x20))

/* Global variables */
static unsigned int ui_InterruptStatus_1564;
static unsigned int ui_InterruptData, ui_Type;

/*
 * Configures the digital input Subdevice
 *
 * data[0] 1 = Enable interrupt, 0 = Disable interrupt
 * data[1] 0 = ADDIDATA Interrupt OR LOGIC, 1 = ADDIDATA Interrupt AND LOGIC
 * data[2] Interrupt mask for the mode 1
 * data[3] Interrupt mask for the mode 2
 */
static int apci1564_di_config(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	devpriv->tsk_Current = current;

	/* Set the digital input logic */
	if (data[0] == ADDIDATA_ENABLE) {
		data[2] = data[2] << 4;
		data[3] = data[3] << 4;
		outl(data[2], devpriv->i_IobaseAmcc + APCI1564_DI_INT_MODE1_REG);
		outl(data[3], devpriv->i_IobaseAmcc + APCI1564_DI_INT_MODE2_REG);
		if (data[1] == ADDIDATA_OR)
			outl(0x4, devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG);
		else
			outl(0x6, devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG);
	} else {
		outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DI_INT_MODE1_REG);
		outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DI_INT_MODE2_REG);
		outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG);
	}

	return insn->n;
}

static int apci1564_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	data[1] = inl(devpriv->i_IobaseAmcc + APCI1564_DI_REG);

	return insn->n;
}

/*
 * Configures The Digital Output Subdevice.
 *
 * data[1] 0 = Disable VCC Interrupt, 1 = Enable VCC Interrupt
 * data[2] 0 = Disable CC Interrupt, 1 = Enable CC Interrupt
 */
static int apci1564_do_config(struct comedi_device *dev,
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
	}

	if (data[0])
		devpriv->b_OutputMemoryStatus = ADDIDATA_ENABLE;
	else
		devpriv->b_OutputMemoryStatus = ADDIDATA_DISABLE;

	if (data[1] == ADDIDATA_ENABLE)
		ul_Command = ul_Command | 0x1;
	else
		ul_Command = ul_Command & 0xFFFFFFFE;

	if (data[2] == ADDIDATA_ENABLE)
		ul_Command = ul_Command | 0x2;
	else
		ul_Command = ul_Command & 0xFFFFFFFD;

	outl(ul_Command, devpriv->i_IobaseAmcc + APCI1564_DO_INT_CTRL_REG);
	ui_InterruptData = inl(devpriv->i_IobaseAmcc + APCI1564_DO_INT_CTRL_REG);
	devpriv->tsk_Current = current;
	return insn->n;
}

static int apci1564_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;

	s->state = inl(devpriv->i_IobaseAmcc + APCI1564_DO_REG);

	if (comedi_dio_update_state(s, data))
		outl(s->state, devpriv->i_IobaseAmcc + APCI1564_DO_REG);

	data[1] = s->state;

	return insn->n;
}

/*
 * Configures The Timer, Counter or Watchdog
 *
 * data[0] Configure as: 0 = Timer, 1 = Counter, 2 = Watchdog
 * data[1] 1 = Enable Interrupt, 0 = Disable Interrupt
 * data[2] Time Unit
 * data[3] Reload Value
 * data[4] Timer Mode
 * data[5] Timer Counter Watchdog Number
 * data[6] Counter Direction
 */
static int apci1564_timer_config(struct comedi_device *dev,
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
		outl(0x0, devpriv->i_IobaseAmcc + APCI1564_WDOG_CTRL_REG);
		/* Loading the Reload value */
		outl(data[3], devpriv->i_IobaseAmcc + APCI1564_WDOG_RELOAD_REG);
	} else if (data[0] == ADDIDATA_TIMER) {
		/* First Stop The Timer */
		ul_Command1 = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
		ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
		/* Stop The Timer */
		outl(ul_Command1, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);

		devpriv->b_TimerSelectMode = ADDIDATA_TIMER;
		if (data[1] == 1) {
			/* Enable TIMER int & DISABLE ALL THE OTHER int SOURCES */
			outl(0x02, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
			outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG);
			outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DO_IRQ_REG);
			outl(0x0, devpriv->i_IobaseAmcc + APCI1564_WDOG_IRQ_REG);
			outl(0x0,
				devpriv->iobase + APCI1564_TCW_IRQ_REG(APCI1564_COUNTER1));
			outl(0x0,
				devpriv->iobase + APCI1564_TCW_IRQ_REG(APCI1564_COUNTER2));
			outl(0x0,
				devpriv->iobase + APCI1564_TCW_IRQ_REG(APCI1564_COUNTER3));
			outl(0x0,
				devpriv->iobase + APCI1564_TCW_IRQ_REG(APCI1564_COUNTER4));
		} else {
			/* disable Timer interrupt */
			outl(0x0, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
		}

		/*  Loading Timebase */
		outl(data[2], devpriv->i_IobaseAmcc + APCI1564_TIMER_TIMEBASE_REG);

		/* Loading the Reload value */
		outl(data[3], devpriv->i_IobaseAmcc + APCI1564_TIMER_RELOAD_REG);

		ul_Command1 = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
		ul_Command1 = (ul_Command1 & 0xFFF719E2UL) | 2UL << 13UL | 0x10UL;
		/* mode 2 */
		outl(ul_Command1, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
	} else if (data[0] == ADDIDATA_COUNTER) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		devpriv->b_ModeSelectRegister = data[5];

		/* First Stop The Counter */
		ul_Command1 = inl(devpriv->iobase + APCI1564_TCW_CTRL_REG(data[5] - 1));
		ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
		/* Stop The Timer */
		outl(ul_Command1, devpriv->iobase + APCI1564_TCW_CTRL_REG(data[5] - 1));

		/* Set the reload value */
		outl(data[3], devpriv->iobase + APCI1564_TCW_RELOAD_REG(data[5] - 1));

		/* Set the mode :             */
		/* - Disable the hardware     */
		/* - Disable the counter mode */
		/* - Disable the warning      */
		/* - Disable the reset        */
		/* - Disable the timer mode   */
		/* - Enable the counter mode  */

		ul_Command1 =
			(ul_Command1 & 0xFFFC19E2UL) | 0x80000UL |
			(unsigned int) ((unsigned int) data[4] << 16UL);
		outl(ul_Command1, devpriv->iobase + APCI1564_TCW_CTRL_REG(data[5] - 1));

		/*  Enable or Disable Interrupt */
		ul_Command1 = (ul_Command1 & 0xFFFFF9FD) | (data[1] << 1);
		outl(ul_Command1, devpriv->iobase + APCI1564_TCW_CTRL_REG(data[5] - 1));

		/* Set the Up/Down selection */
		ul_Command1 = (ul_Command1 & 0xFFFBF9FFUL) | (data[6] << 18);
		outl(ul_Command1, devpriv->iobase + APCI1564_TCW_CTRL_REG(data[5] - 1));
	} else {
		dev_err(dev->class_dev, "Invalid subdevice.\n");
	}

	return insn->n;
}

/*
 * Start / Stop The Selected Timer, Counter or Watchdog
 *
 * data[0] Configure as: 0 = Timer, 1 = Counter, 2 = Watchdog
 * data[1] 0 = Stop, 1 = Start, 2 = Trigger Clear (Only Counter)
 */
static int apci1564_timer_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ul_Command1 = 0;

	if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
		switch (data[1]) {
		case 0:	/* stop the watchdog */
			/* disable the watchdog */
			outl(0x0, devpriv->i_IobaseAmcc + APCI1564_WDOG_CTRL_REG);
			break;
		case 1:	/* start the watchdog */
			outl(0x0001, devpriv->i_IobaseAmcc + APCI1564_WDOG_CTRL_REG);
			break;
		case 2:	/* Software trigger */
			outl(0x0201, devpriv->i_IobaseAmcc + APCI1564_WDOG_CTRL_REG);
			break;
		default:
			dev_err(dev->class_dev, "Specified functionality does not exist.\n");
			return -EINVAL;
		}
	}
	if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		if (data[1] == 1) {
			ul_Command1 = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;

			/* Enable the Timer */
			outl(ul_Command1, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
		} else if (data[1] == 0) {
			/* Stop The Timer */

			ul_Command1 = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
			ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
			outl(ul_Command1, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
		}
	}
	if (devpriv->b_TimerSelectMode == ADDIDATA_COUNTER) {
		ul_Command1 =
			inl(devpriv->iobase +
				APCI1564_TCW_CTRL_REG(devpriv->b_ModeSelectRegister - 1));
		if (data[1] == 1) {
			/* Start the Counter subdevice */
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;
		} else if (data[1] == 0) {
			/*  Stops the Counter subdevice */
			ul_Command1 = 0;

		} else if (data[1] == 2) {
			/*  Clears the Counter subdevice */
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x400;
		}
		outl(ul_Command1,
			devpriv->iobase +
			APCI1564_TCW_CTRL_REG(devpriv->b_ModeSelectRegister - 1));
	}
	return insn->n;
}

/*
 * Read The Selected Timer, Counter or Watchdog
 */
static int apci1564_timer_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ul_Command1 = 0;

	if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
		/*  Stores the status of the Watchdog */
		data[0] = inl(devpriv->i_IobaseAmcc + APCI1564_WDOG_STATUS_REG) & 0x1;
		data[1] = inl(devpriv->i_IobaseAmcc + APCI1564_WDOG_REG);
	} else if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		/*  Stores the status of the Timer */
		data[0] = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER_STATUS_REG) & 0x1;

		/*  Stores the Actual value of the Timer */
		data[1] = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER_REG);
	} else if (devpriv->b_TimerSelectMode == ADDIDATA_COUNTER) {
		/*  Read the Counter Actual Value. */
		data[0] =
			inl(devpriv->iobase +
				APCI1564_TCW_REG(devpriv->b_ModeSelectRegister - 1));
		ul_Command1 =
			inl(devpriv->iobase +
				APCI1564_TCW_STATUS_REG(devpriv->b_ModeSelectRegister - 1));

		/* Get the software trigger status */
		data[1] = (unsigned char) ((ul_Command1 >> 1) & 1);

		/* Get the hardware trigger status */
		data[2] = (unsigned char) ((ul_Command1 >> 2) & 1);

		/* Get the software clear status */
		data[3] = (unsigned char) ((ul_Command1 >> 3) & 1);

		/* Get the overflow status */
		data[4] = (unsigned char) ((ul_Command1 >> 0) & 1);
	} else if ((devpriv->b_TimerSelectMode != ADDIDATA_TIMER)
		&& (devpriv->b_TimerSelectMode != ADDIDATA_WATCHDOG)
		&& (devpriv->b_TimerSelectMode != ADDIDATA_COUNTER)) {
		dev_err(dev->class_dev, "Invalid Subdevice!\n");
	}
	return insn->n;
}

/*
 * Reads the interrupt status register
 */
static int apci1564_do_read(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned int *data)
{
	*data = ui_Type;
	return insn->n;
}

/*
 * Interrupt handler for the interruptible digital inputs
 */
static void apci1564_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct addi_private *devpriv = dev->private;
	unsigned int ui_DO, ui_DI;
	unsigned int ui_Timer;
	unsigned int ui_C1, ui_C2, ui_C3, ui_C4;
	unsigned int ul_Command2 = 0;

	ui_DI = inl(devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG) & 0x01;
	ui_DO = inl(devpriv->i_IobaseAmcc + APCI1564_DO_IRQ_REG) & 0x01;
	ui_Timer = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER_IRQ_REG) & 0x01;
	ui_C1 =
		inl(devpriv->iobase + APCI1564_TCW_IRQ_REG(APCI1564_COUNTER1)) & 0x1;
	ui_C2 =
		inl(devpriv->iobase + APCI1564_TCW_IRQ_REG(APCI1564_COUNTER2)) & 0x1;
	ui_C3 =
		inl(devpriv->iobase + APCI1564_TCW_IRQ_REG(APCI1564_COUNTER3)) & 0x1;
	ui_C4 =
		inl(devpriv->iobase + APCI1564_TCW_IRQ_REG(APCI1564_COUNTER4)) & 0x1;
	if (ui_DI == 0 && ui_DO == 0 && ui_Timer == 0 && ui_C1 == 0
		&& ui_C2 == 0 && ui_C3 == 0 && ui_C4 == 0) {
		dev_err(dev->class_dev, "Interrupt from unknown source.\n");
	}

	if (ui_DI == 1) {
		ui_DI = inl(devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG);
		outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG);
		ui_InterruptStatus_1564 =
			inl(devpriv->i_IobaseAmcc + APCI1564_DI_INT_STATUS_REG);
		ui_InterruptStatus_1564 = ui_InterruptStatus_1564 & 0X000FFFF0;
		/* send signal to the sample */
		send_sig(SIGIO, devpriv->tsk_Current, 0);
		/* enable the interrupt */
		outl(ui_DI, devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG);
		return;
	}

	if (ui_DO == 1) {
		/* Check for Digital Output interrupt Type */
		/* 1: VCC interrupt			   */
		/* 2: CC interrupt			   */
		ui_Type = inl(devpriv->i_IobaseAmcc + APCI1564_DO_INT_STATUS_REG) & 0x3;
		/* Disable the  Interrupt */
		outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DO_INT_CTRL_REG);

		/* Sends signal to user space */
		send_sig(SIGIO, devpriv->tsk_Current, 0);
	}

	if (ui_Timer == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_TIMER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Timer Interrupt */
			ul_Command2 = inl(devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
			outl(0x0, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Timer Interrupt */

			outl(ul_Command2, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);
		}
	}

	if (ui_C1 == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Counter Interrupt */
			ul_Command2 =
				inl(devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER1));
			outl(0x0,
			     devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER1));

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Counter Interrupt */
			outl(ul_Command2,
			     devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER1));
		}
	}

	if (ui_C2 == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Counter Interrupt */
			ul_Command2 =
				inl(devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER2));
			outl(0x0,
			     devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER2));

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Counter Interrupt */
			outl(ul_Command2,
			     devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER2));
		}
	}

	if (ui_C3 == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Counter Interrupt */
			ul_Command2 =
				inl(devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER3));
			outl(0x0,
			     devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER3));

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Counter Interrupt */
			outl(ul_Command2,
			     devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER3));
		}
	}

	if (ui_C4 == 1) {
		devpriv->b_TimerSelectMode = ADDIDATA_COUNTER;
		if (devpriv->b_TimerSelectMode) {

			/*  Disable Counter Interrupt */
			ul_Command2 =
				inl(devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER4));
			outl(0x0,
			     devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER4));

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);

			/*  Enable Counter Interrupt */
			outl(ul_Command2,
			     devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER4));
		}
	}
	return;
}

static int apci1564_reset(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	/* disable the interrupts */
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DI_IRQ_REG);
	/* Reset the interrupt status register */
	inl(devpriv->i_IobaseAmcc + APCI1564_DI_INT_STATUS_REG);
	/* Disable the and/or interrupt */
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DI_INT_MODE1_REG);
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DI_INT_MODE2_REG);
	devpriv->b_DigitalOutputRegister = 0;
	ui_Type = 0;
	/* Resets the output channels */
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DO_REG);
	/* Disables the interrupt. */
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_DO_INT_CTRL_REG);
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_WDOG_RELOAD_REG);
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_TIMER_REG);
	outl(0x0, devpriv->i_IobaseAmcc + APCI1564_TIMER_CTRL_REG);

	outl(0x0, devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER1));
	outl(0x0, devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER2));
	outl(0x0, devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER3));
	outl(0x0, devpriv->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER4));
	return 0;
}
