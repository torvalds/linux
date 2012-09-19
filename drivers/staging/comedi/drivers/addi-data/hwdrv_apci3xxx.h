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

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

#ifndef ADDIDATA_ENABLE
#define ADDIDATA_ENABLE  1
#define ADDIDATA_DISABLE 0
#endif

#define APCI3XXX_SINGLE                              0
#define APCI3XXX_DIFF                                1
#define APCI3XXX_CONFIGURATION                       0

#define APCI3XXX_TTL_INIT_DIRECTION_PORT2   0

#ifdef __KERNEL__

static const struct comedi_lrange range_apci3XXX_ai = { 8, {BIP_RANGE(10),
						     BIP_RANGE(5),
						     BIP_RANGE(2),
						     BIP_RANGE(1),
						     UNI_RANGE(10),
						     UNI_RANGE(5),
						     UNI_RANGE(2),
						     UNI_RANGE(1)}
};

static const struct comedi_lrange range_apci3XXX_ao = { 2, {BIP_RANGE(10),
						     UNI_RANGE(10)}
};
#endif
