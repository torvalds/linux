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
/*********      Definitions for APCI-2016 card  *****/

#define APCI2016_ADDRESS_RANGE   8

/* DIGITAL INPUT-OUTPUT DEFINE */

#define APCI2016_DIGITAL_OP                 	0x04
#define APCI2016_DIGITAL_OP_RW                 	4

/* ADDIDATA Enable Disable */

#define ADDIDATA_ENABLE                            1
#define ADDIDATA_DISABLE                           0

/* TIMER COUNTER WATCHDOG DEFINES */

#define ADDIDATA_WATCHDOG                          2
#define APCI2016_DIGITAL_OP_WATCHDOG               0
#define APCI2016_WATCHDOG_ENABLEDISABLE            12
#define APCI2016_WATCHDOG_RELOAD_VALUE             4
#define APCI2016_WATCHDOG_STATUS                   16
