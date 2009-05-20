/*
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data-com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
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
int i_APCI2032_ConfigDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data);
INT i_APCI2032_WriteDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data);
INT i_APCI2032_ReadDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);
int i_APCI2032_ReadInterruptStatus(struct comedi_device *dev, struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data);

// TIMER
// timer value is passed as u seconds
INT i_APCI2032_ConfigWatchdog(struct comedi_device *dev, struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);
int i_APCI2032_StartStopWriteWatchdog(struct comedi_device *dev, struct comedi_subdevice *s,
				      struct comedi_insn *insn, unsigned int *data);
int i_APCI2032_ReadWatchdog(struct comedi_device *dev, struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);

// Interrupt functions.....

void v_APCI2032_Interrupt(int irq, void *d);

//Reset functions
int i_APCI2032_Reset(struct comedi_device *dev);
