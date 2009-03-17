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

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

#ifndef ADDIDATA_ENABLE
#define ADDIDATA_ENABLE  1
#define ADDIDATA_DISABLE 0
#endif

#define APCI16XX_TTL_INIT           0
#define APCI16XX_TTL_INITDIRECTION  1
#define APCI16XX_TTL_OUTPUTMEMORY   2

#define APCI16XX_TTL_READCHANNEL            0
#define APCI16XX_TTL_READPORT               1

#define APCI16XX_TTL_WRITECHANNEL_ON        0
#define APCI16XX_TTL_WRITECHANNEL_OFF       1
#define APCI16XX_TTL_WRITEPORT_ON           2
#define APCI16XX_TTL_WRITEPORT_OFF          3

#define APCI16XX_TTL_READ_ALL_INPUTS        0
#define APCI16XX_TTL_READ_ALL_OUTPUTS       1

#ifdef __KERNEL__

static const struct comedi_lrange range_apci16xx_ttl = { 12,
	{BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1),
	 BIP_RANGE(1)}
};

/*
+----------------------------------------------------------------------------+
|                       TTL INISIALISATION FUNCTION                          |
+----------------------------------------------------------------------------+
*/

int i_APCI16XX_InsnConfigInitTTLIO(struct comedi_device *dev,
				   struct comedi_subdevice *s, struct comedi_insn *insn,
				   unsigned int *data);

/*
+----------------------------------------------------------------------------+
|                       TTL INPUT FUNCTION                                   |
+----------------------------------------------------------------------------+
*/

int i_APCI16XX_InsnBitsReadTTLIO(struct comedi_device *dev,
				 struct comedi_subdevice *s, struct comedi_insn *insn,
				 unsigned int *data);

int i_APCI16XX_InsnReadTTLIOAllPortValue(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 struct comedi_insn *insn, unsigned int *data);

/*
+----------------------------------------------------------------------------+
|                            TTL OUTPUT FUNCTIONS                            |
+----------------------------------------------------------------------------+
*/

int i_APCI16XX_InsnBitsWriteTTLIO(struct comedi_device *dev,
				  struct comedi_subdevice *s, struct comedi_insn *insn,
				  unsigned int *data);

int i_APCI16XX_Reset(struct comedi_device *dev);
#endif
