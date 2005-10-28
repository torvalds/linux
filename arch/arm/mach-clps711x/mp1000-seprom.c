/*`
 * mp1000-seprom.c
 *
 *  This file contains the Serial EEPROM code for the MP1000 board
 *
 *  Copyright (C) 2005 Comdial Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/hardware.h>
#include <asm/hardware/clps7111.h>
#include <asm/arch/mp1000-seprom.h>

/* If SepromInit() can initialize and checksum the seprom successfully, */
/* then it will point seprom_data_ptr at the shadow copy.  */

static eeprom_struct seprom_data;			/* shadow copy of seprom content */

eeprom_struct *seprom_data_ptr = 0;		/* 0 => not initialized */

/*
 * Port D Bit 5 is Chip Select for EEPROM
 * Port E Bit 0 is Input, Data out from EEPROM
 * Port E Bit 1 is Output, Data in to EEPROM
 * Port E Bit 2 is Output, CLK to EEPROM
 */

static char *port_d_ptr = (char *)(CLPS7111_VIRT_BASE + PDDR);
static char *port_e_ptr = (char *)(CLPS7111_VIRT_BASE + PEDR);

#define NO_OF_SHORTS	64	// Device is 64 x 16 bits
#define ENABLE_RW	0
#define DISABLE_RW	1

static inline void toggle_seprom_clock(void)
{
	*port_e_ptr |= HwPortESepromCLK;
	*port_e_ptr &= ~(HwPortESepromCLK);
}

static inline void select_eeprom(void)
{
	*port_d_ptr |= HwPortDEECS;
	*port_e_ptr &= ~(HwPortESepromCLK);
}

static inline void deselect_eeprom(void)
{
	*port_d_ptr &= ~(HwPortDEECS);
	*port_e_ptr &= ~(HwPortESepromDIn);
}

/*
 * GetSepromDataPtr - returns pointer to shadow (RAM) copy of seprom
 *                    and returns 0 if seprom is not initialized or
 *                    has a checksum error.
 */

eeprom_struct* get_seprom_ptr(void)
{
	return seprom_data_ptr;
}

unsigned char* get_eeprom_mac_address(void)
{
	return seprom_data_ptr->variant.eprom_struct.mac_Address;
}

/*
 * ReadSProm, Physically reads data from the Serial PROM
 */
static void read_sprom(short address, int length, eeprom_struct *buffer)
{
	short data = COMMAND_READ | (address & 0x3F);
	short bit;
	int i;

	select_eeprom();

	// Clock in 9 bits of the command
	for (i = 0, bit = 0x100; i < 9; i++, bit >>= 1) {
		if (data & bit)
			*port_e_ptr |= HwPortESepromDIn;
		else
			*port_e_ptr &= ~(HwPortESepromDIn);

		toggle_seprom_clock();
	}

	//
	// Now read one or more shorts of data from the Seprom
	//
	while (length-- > 0) {
		data = 0;

		// Read 16 bits at a time
		for (i = 0; i < 16; i++) {
			data <<= 1;
			toggle_seprom_clock();
			data |= *port_e_ptr & HwPortESepromDOut;

		}

		buffer->variant.eprom_short_data[address++] = data;
	}

	deselect_eeprom();

	return;
}



/*
 * ReadSerialPROM
 *
 * Input: Pointer to array of 64 x 16 Bits
 *
 * Output: if no problem reading data is filled in
 */
static void read_serial_prom(eeprom_struct *data)
{
	read_sprom(0, 64, data);
}


//
// Compute Serial EEPROM checksum
//
// Input: Pointer to struct with Eprom data
//
// Output: The computed Eprom checksum
//
static short compute_seprom_checksum(eeprom_struct *data)
{
	short checksum = 0;
	int i;

	for (i = 0; i < 126; i++) {
		checksum += (short)data->variant.eprom_byte_data[i];
	}

	return((short)(0x5555 - (checksum & 0xFFFF)));
}

//
// Make sure the data port bits for the SEPROM are correctly initialised
//

void __init seprom_init(void)
{
	short checksum;

	// Init Port D
	*(char *)(CLPS7111_VIRT_BASE + PDDDR) = 0x0;
	*(char *)(CLPS7111_VIRT_BASE + PDDR) = 0x15;

	// Init Port E
	*(int *)(CLPS7111_VIRT_BASE + PEDDR) = 0x06;
	*(int *)(CLPS7111_VIRT_BASE + PEDR) = 0x04;

	//
	// Make sure that EEPROM struct size never exceeds 128 bytes
	//
	if (sizeof(eeprom_struct) > 128) {
		panic("Serial PROM struct size > 128, aborting read\n");
	}

	read_serial_prom(&seprom_data);

	checksum = compute_seprom_checksum(&seprom_data);

	if (checksum != seprom_data.variant.eprom_short_data[63]) {
		panic("Serial EEPROM checksum failed\n");
	}

	seprom_data_ptr = &seprom_data;
}

