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

/*********      Definitions for APCI-1032 card  *****/

#define APCI1032_BOARD_VENDOR_ID 0x15B8
#define APCI1032_ADDRESS_RANGE  20
//DIGITAL INPUT DEFINE

#define APCI1032_DIGITAL_IP                     0
#define APCI1032_DIGITAL_IP_INTERRUPT_MODE1     4
#define APCI1032_DIGITAL_IP_INTERRUPT_MODE2     8
#define APCI1032_DIGITAL_IP_IRQ                 16

//Digital Input IRQ Function Selection
#define ADDIDATA_OR                  0
#define ADDIDATA_AND                 1

//Digital Input Interrupt Status
#define APCI1032_DIGITAL_IP_INTERRUPT_STATUS    12

//Digital Input Interrupt Enable Disable.
#define APCI1032_DIGITAL_IP_INTERRUPT_ENABLE    0x4
#define APCI1032_DIGITAL_IP_INTERRUPT_DISABLE   0xFFFFFFFB

//ADDIDATA Enable Disable

#define ADDIDATA_ENABLE                            1
#define ADDIDATA_DISABLE                           0

// Hardware Layer  functions for Apci1032

//DI
// for di read

int i_APCI1032_ConfigDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data);

int i_APCI1032_Read1DigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);

int i_APCI1032_ReadMoreDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				    struct comedi_insn *insn, unsigned int *data);

// Interrupt functions.....

static void v_APCI1032_Interrupt(int irq, void *d);
//Reset
int i_APCI1032_Reset(struct comedi_device *dev);
