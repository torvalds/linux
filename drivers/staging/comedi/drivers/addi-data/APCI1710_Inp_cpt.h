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

#define APCI1710_SINGLE			0
#define APCI1710_CONTINUOUS		1

#define APCI1710_PULSEENCODER_READ	0
#define APCI1710_PULSEENCODER_WRITE	1

int i_APCI1710_InsnConfigInitPulseEncoder(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn, unsigned int *data);

int i_APCI1710_InsnWriteEnableDisablePulseEncoder(struct comedi_device *dev,
						  struct comedi_subdevice *s,
						  struct comedi_insn *insn,
						  unsigned int *data);

/*
 * READ PULSE ENCODER FUNCTIONS
 */
int i_APCI1710_InsnReadInterruptPulseEncoder(struct comedi_device *dev,
					     struct comedi_subdevice *s,
					     struct comedi_insn *insn,
					     unsigned int *data);

/*
 * WRITE PULSE ENCODER FUNCTIONS
 */
int i_APCI1710_InsnBitsReadWritePulseEncoder(struct comedi_device *dev,
					     struct comedi_subdevice *s,
					     struct comedi_insn *insn,
					     unsigned int *data);
