/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

        ADDI-DATA GmbH
        Dieselstrasse 3
        D-77833 Ottersweier
        Tel: +19(0)7223/9493-0
        Fax: +49(0)7223/9493-92
        http://www.addi-data-com
        info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You shoud also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/
/*
#define VOID           void
#define INT            int
#define UINT           unsigned int
#define SHORT          short
#define USHORT         unsigned short
#define CHAR           char
#define BYTE           unsigned char
#define WORD           unsigned int
#define LONG           long
#define ULONG          unsigned long
#define DWORD          unsigned long
#define DOUBLE         double
#define PINT           int *
#define PUINT          unsigned int *
#define PSHORT         short *
#define PUSHORT        unsigned short *
#define PCHAR          char *
#define PBYTE          unsigned char *
#define PWORD          unsigned int *
#define PLONG          long *
#define PULONG         unsigned long *
#define PDWORD         unsigned long *
#define PDOUBLE        double *
*/

#define AMCC_OP_REG_MCSR         0x3c
#define EEPROM_BUSY   0x80000000
#define NVCMD_LOAD_LOW  (0x4 << 5 )	// nvRam load low command
#define NVCMD_LOAD_HIGH (0x5 << 5 )	// nvRam load high command
#define NVCMD_BEGIN_READ (0x7 << 5 )	// nvRam begin read command
#define NVCMD_BEGIN_WRITE  (0x6 << 5)	//EEPROM begin write command

INT i_AddiHeaderRW_ReadEeprom(INT i_NbOfWordsToRead,
	DWORD dw_PCIBoardEepromAddress,
	WORD w_EepromStartAddress, PWORD pw_DataRead);
