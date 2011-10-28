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
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*********      Definitions for APCI-2200 card  *****/

/* Card Specific information */
#define APCI2200_BOARD_VENDOR_ID                 0x15b8
#define APCI2200_ADDRESS_RANGE                   64

/* DIGITAL INPUT-OUTPUT DEFINE */

#define APCI2200_DIGITAL_OP                 	4
#define APCI2200_DIGITAL_IP                     0

/* TIMER COUNTER WATCHDOG DEFINES */

#define APCI2200_WATCHDOG                          0x08
#define APCI2200_WATCHDOG_ENABLEDISABLE            12
#define APCI2200_WATCHDOG_RELOAD_VALUE             4
#define APCI2200_WATCHDOG_STATUS                   16

/* Hardware Layer  functions for Apci2200 */

/* Digital Input */
int i_APCI2200_ReadMoreDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				    struct comedi_insn *insn, unsigned int *data);
int i_APCI2200_Read1DigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);

/* Digital Output */
int i_APCI2200_ConfigDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data);
int i_APCI2200_WriteDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data);
int i_APCI2200_ReadDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);

/* TIMER */
int i_APCI2200_ConfigWatchdog(struct comedi_device *dev, struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);
int i_APCI2200_StartStopWriteWatchdog(struct comedi_device *dev, struct comedi_subdevice *s,
				      struct comedi_insn *insn, unsigned int *data);
int i_APCI2200_ReadWatchdog(struct comedi_device *dev, struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);

/* reset */
int i_APCI2200_Reset(struct comedi_device *dev);
