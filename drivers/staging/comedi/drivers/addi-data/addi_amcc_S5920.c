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
  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstraße 3       D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-------------------------------+---------------------------------------+
  | Project : ADDI HEADER READ WRITER |     Compiler   : Visual C++       |
  | Module name : S5920.cpp           |     Version    : 6.0              |
  +-------------------------------+---------------------------------------+
  | Author : E. LIBS                      Date : 02/05/2002               |
  +-----------------------------------------------------------------------+
  | Description   : DLL with the S5920 PCI Controller functions           |
  +-----------------------------------------------------------------------+
  |                             UPDATE'S                                  |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  | 28/08/02 | LIBS Eric | Add return codes each time a function of the   |
  |          |           | Addi Library is called                         |
  +-----------------------------------------------------------------------+
  | 31/07/03 | KRAUTH J. | Changes for the MSX-Box                        |
  +-----------------------------------------------------------------------+
*/

#include "addi_amcc_S5920.h"

/*+----------------------------------------------------------------------------+*/
/*| Function   Name   : int i_AddiHeaderRW_ReadEeprom                          |*/
/*|                               (int    i_NbOfWordsToRead,                   |*/
/*|                                DWORD dw_PCIBoardEepromAddress,             |*/
/*|                                unsigned short   w_EepromStartAddress,                |*/
/*|                                unsigned short * pw_DataRead)                          |*/
/*+----------------------------------------------------------------------------+*/
/*| Task              : Read word from the 5920 eeprom.                        |*/
/*+----------------------------------------------------------------------------+*/
/*| Input Parameters  : int    i_NbOfWordsToRead : Nbr. of word to read        |*/
/*|                     DWORD dw_PCIBoardEepromAddress : Address of the eeprom |*/
/*|                     unsigned short   w_EepromStartAddress : Eeprom strat address     |*/
/*+----------------------------------------------------------------------------+*/
/*| Output Parameters : unsigned short * pw_DataRead : Read data                          |*/
/*+----------------------------------------------------------------------------+*/
/*| Return Value      : -                                                      |*/
/*+----------------------------------------------------------------------------+*/

int i_AddiHeaderRW_ReadEeprom(int i_NbOfWordsToRead,
	DWORD dw_PCIBoardEepromAddress,
	unsigned short w_EepromStartAddress, unsigned short * pw_DataRead)
{
	DWORD dw_eeprom_busy = 0;
	int i_Counter = 0;
	int i_WordCounter;
	int i;
	unsigned char pb_ReadByte[1];
	unsigned char b_ReadLowByte = 0;
	unsigned char b_ReadHighByte = 0;
	unsigned char b_SelectedAddressLow = 0;
	unsigned char b_SelectedAddressHigh = 0;
	unsigned short w_ReadWord = 0;

	for (i_WordCounter = 0; i_WordCounter < i_NbOfWordsToRead;
		i_WordCounter++) {
		do {
			dw_eeprom_busy =
				inl(dw_PCIBoardEepromAddress +
				AMCC_OP_REG_MCSR);
			dw_eeprom_busy = dw_eeprom_busy & EEPROM_BUSY;
		}
		while (dw_eeprom_busy == EEPROM_BUSY);

		for (i_Counter = 0; i_Counter < 2; i_Counter++) {
			b_SelectedAddressLow = (w_EepromStartAddress + i_Counter) % 256;	//Read the low 8 bit part
			b_SelectedAddressHigh = (w_EepromStartAddress + i_Counter) / 256;	//Read the high 8 bit part

			//Select the load low address mode
			outb(NVCMD_LOAD_LOW,
				dw_PCIBoardEepromAddress + AMCC_OP_REG_MCSR +
				3);

			//Wait on busy
			do {
				dw_eeprom_busy =
					inl(dw_PCIBoardEepromAddress +
					AMCC_OP_REG_MCSR);
				dw_eeprom_busy = dw_eeprom_busy & EEPROM_BUSY;
			}
			while (dw_eeprom_busy == EEPROM_BUSY);

			//Load the low address
			outb(b_SelectedAddressLow,
				dw_PCIBoardEepromAddress + AMCC_OP_REG_MCSR +
				2);

			//Wait on busy
			do {
				dw_eeprom_busy =
					inl(dw_PCIBoardEepromAddress +
					AMCC_OP_REG_MCSR);
				dw_eeprom_busy = dw_eeprom_busy & EEPROM_BUSY;
			}
			while (dw_eeprom_busy == EEPROM_BUSY);

			//Select the load high address mode
			outb(NVCMD_LOAD_HIGH,
				dw_PCIBoardEepromAddress + AMCC_OP_REG_MCSR +
				3);

			//Wait on busy
			do {
				dw_eeprom_busy =
					inl(dw_PCIBoardEepromAddress +
					AMCC_OP_REG_MCSR);
				dw_eeprom_busy = dw_eeprom_busy & EEPROM_BUSY;
			}
			while (dw_eeprom_busy == EEPROM_BUSY);

			//Load the high address
			outb(b_SelectedAddressHigh,
				dw_PCIBoardEepromAddress + AMCC_OP_REG_MCSR +
				2);

			//Wait on busy
			do {
				dw_eeprom_busy =
					inl(dw_PCIBoardEepromAddress +
					AMCC_OP_REG_MCSR);
				dw_eeprom_busy = dw_eeprom_busy & EEPROM_BUSY;
			}
			while (dw_eeprom_busy == EEPROM_BUSY);

			//Select the READ mode
			outb(NVCMD_BEGIN_READ,
				dw_PCIBoardEepromAddress + AMCC_OP_REG_MCSR +
				3);

			//Wait on busy
			do {
				dw_eeprom_busy =
					inl(dw_PCIBoardEepromAddress +
					AMCC_OP_REG_MCSR);
				dw_eeprom_busy = dw_eeprom_busy & EEPROM_BUSY;
			}
			while (dw_eeprom_busy == EEPROM_BUSY);

			//Read data into the EEPROM
			*pb_ReadByte =
				inb(dw_PCIBoardEepromAddress +
				AMCC_OP_REG_MCSR + 2);

			//Wait on busy
			do {
				dw_eeprom_busy =
					inl(dw_PCIBoardEepromAddress +
					AMCC_OP_REG_MCSR);
				dw_eeprom_busy = dw_eeprom_busy & EEPROM_BUSY;
			}
			while (dw_eeprom_busy == EEPROM_BUSY);

			//Select the upper address part
			if (i_Counter == 0) {
				b_ReadLowByte = pb_ReadByte[0];
			} else {
				b_ReadHighByte = pb_ReadByte[0];
			}

			//Sleep
			for (i = 0; i < 10000; i++) ;

		}
		w_ReadWord =
			(b_ReadLowByte | (((unsigned short)b_ReadHighByte) *
				256));

		pw_DataRead[i_WordCounter] = w_ReadWord;

		w_EepromStartAddress += 2;	// to read the next word

	}			// for (...) i_NbOfWordsToRead
	return (0);
}
