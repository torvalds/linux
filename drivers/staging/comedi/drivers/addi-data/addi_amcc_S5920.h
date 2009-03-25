/*
 *  Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
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

#define AMCC_OP_REG_MCSR	0x3c
#define EEPROM_BUSY		0x80000000
#define NVCMD_LOAD_LOW		(0x4 << 5)	/* nvRam load low command */
#define NVCMD_LOAD_HIGH		(0x5 << 5)	/* nvRam load high command */
#define NVCMD_BEGIN_READ	(0x7 << 5)	/* nvRam begin read command */
#define NVCMD_BEGIN_WRITE	(0x6 << 5)	/* EEPROM begin write command */

int i_AddiHeaderRW_ReadEeprom(int i_NbOfWordsToRead,
			      unsigned int dw_PCIBoardEepromAddress,
			      unsigned short w_EepromStartAddress, unsigned short * pw_DataRead);
