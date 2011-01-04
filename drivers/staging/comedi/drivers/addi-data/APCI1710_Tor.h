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

#define APCI1710_30MHZ		30
#define APCI1710_33MHZ		33
#define APCI1710_40MHZ		40

#define APCI1710_GATE_INPUT	10

#define APCI1710_TOR_SIMPLE_MODE	2
#define APCI1710_TOR_DOUBLE_MODE	3
#define APCI1710_TOR_QUADRUPLE_MODE	4

#define APCI1710_SINGLE			0
#define APCI1710_CONTINUOUS		1

#define APCI1710_TOR_GETPROGRESSSTATUS	0
#define APCI1710_TOR_GETCOUNTERVALUE	1
#define APCI1710_TOR_READINTERRUPT	2

/*
 * TOR_COUNTER INISIALISATION FUNCTION
 */
int i_APCI1710_InsnConfigInitTorCounter(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn, unsigned int *data);

int i_APCI1710_InsnWriteEnableDisableTorCounter(struct comedi_device *dev,
						struct comedi_subdevice *s,
						struct comedi_insn *insn,
						unsigned int *data);

int i_APCI1710_InsnReadGetTorCounterInitialisation(struct comedi_device *dev,
						   struct comedi_subdevice *s,
						   struct comedi_insn *insn,
						   unsigned int *data);
/*
 * TOR_COUNTER READ FUNCTION
 */
int i_APCI1710_InsnBitsGetTorCounterProgressStatusAndValue(struct comedi_device *dev,
							   struct comedi_subdevice *s,
							   struct comedi_insn *insn,
							   unsigned int *data);
