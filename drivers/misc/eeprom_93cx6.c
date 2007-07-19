/*
	Copyright (C) 2004 - 2006 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: eeprom_93cx6
	Abstract: EEPROM reader routines for 93cx6 chipsets.
	Supported chipsets: 93c46 & 93c66.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/eeprom_93cx6.h>

MODULE_AUTHOR("http://rt2x00.serialmonkey.com");
MODULE_VERSION("1.0");
MODULE_DESCRIPTION("EEPROM 93cx6 chip driver");
MODULE_LICENSE("GPL");

static inline void eeprom_93cx6_pulse_high(struct eeprom_93cx6 *eeprom)
{
	eeprom->reg_data_clock = 1;
	eeprom->register_write(eeprom);

	/*
	 * Add a short delay for the pulse to work.
	 * According to the specifications the "maximum minimum"
	 * time should be 450ns.
	 */
	ndelay(450);
}

static inline void eeprom_93cx6_pulse_low(struct eeprom_93cx6 *eeprom)
{
	eeprom->reg_data_clock = 0;
	eeprom->register_write(eeprom);

	/*
	 * Add a short delay for the pulse to work.
	 * According to the specifications the "maximum minimum"
	 * time should be 450ns.
	 */
	ndelay(450);
}

static void eeprom_93cx6_startup(struct eeprom_93cx6 *eeprom)
{
	/*
	 * Clear all flags, and enable chip select.
	 */
	eeprom->register_read(eeprom);
	eeprom->reg_data_in = 0;
	eeprom->reg_data_out = 0;
	eeprom->reg_data_clock = 0;
	eeprom->reg_chip_select = 1;
	eeprom->register_write(eeprom);

	/*
	 * kick a pulse.
	 */
	eeprom_93cx6_pulse_high(eeprom);
	eeprom_93cx6_pulse_low(eeprom);
}

static void eeprom_93cx6_cleanup(struct eeprom_93cx6 *eeprom)
{
	/*
	 * Clear chip_select and data_in flags.
	 */
	eeprom->register_read(eeprom);
	eeprom->reg_data_in = 0;
	eeprom->reg_chip_select = 0;
	eeprom->register_write(eeprom);

	/*
	 * kick a pulse.
	 */
	eeprom_93cx6_pulse_high(eeprom);
	eeprom_93cx6_pulse_low(eeprom);
}

static void eeprom_93cx6_write_bits(struct eeprom_93cx6 *eeprom,
	const u16 data, const u16 count)
{
	unsigned int i;

	eeprom->register_read(eeprom);

	/*
	 * Clear data flags.
	 */
	eeprom->reg_data_in = 0;
	eeprom->reg_data_out = 0;

	/*
	 * Start writing all bits.
	 */
	for (i = count; i > 0; i--) {
		/*
		 * Check if this bit needs to be set.
		 */
		eeprom->reg_data_in = !!(data & (1 << (i - 1)));

		/*
		 * Write the bit to the eeprom register.
		 */
		eeprom->register_write(eeprom);

		/*
		 * Kick a pulse.
		 */
		eeprom_93cx6_pulse_high(eeprom);
		eeprom_93cx6_pulse_low(eeprom);
	}

	eeprom->reg_data_in = 0;
	eeprom->register_write(eeprom);
}

static void eeprom_93cx6_read_bits(struct eeprom_93cx6 *eeprom,
	u16 *data, const u16 count)
{
	unsigned int i;
	u16 buf = 0;

	eeprom->register_read(eeprom);

	/*
	 * Clear data flags.
	 */
	eeprom->reg_data_in = 0;
	eeprom->reg_data_out = 0;

	/*
	 * Start reading all bits.
	 */
	for (i = count; i > 0; i--) {
		eeprom_93cx6_pulse_high(eeprom);

		eeprom->register_read(eeprom);

		/*
		 * Clear data_in flag.
		 */
		eeprom->reg_data_in = 0;

		/*
		 * Read if the bit has been set.
		 */
		if (eeprom->reg_data_out)
			buf |= (1 << (i - 1));

		eeprom_93cx6_pulse_low(eeprom);
	}

	*data = buf;
}

/**
 * eeprom_93cx6_read - Read multiple words from eeprom
 * @eeprom: Pointer to eeprom structure
 * @word: Word index from where we should start reading
 * @data: target pointer where the information will have to be stored
 *
 * This function will read the eeprom data as host-endian word
 * into the given data pointer.
 */
void eeprom_93cx6_read(struct eeprom_93cx6 *eeprom, const u8 word,
	u16 *data)
{
	u16 command;

	/*
	 * Initialize the eeprom register
	 */
	eeprom_93cx6_startup(eeprom);

	/*
	 * Select the read opcode and the word to be read.
	 */
	command = (PCI_EEPROM_READ_OPCODE << eeprom->width) | word;
	eeprom_93cx6_write_bits(eeprom, command,
		PCI_EEPROM_WIDTH_OPCODE + eeprom->width);

	/*
	 * Read the requested 16 bits.
	 */
	eeprom_93cx6_read_bits(eeprom, data, 16);

	/*
	 * Cleanup eeprom register.
	 */
	eeprom_93cx6_cleanup(eeprom);
}
EXPORT_SYMBOL_GPL(eeprom_93cx6_read);

/**
 * eeprom_93cx6_multiread - Read multiple words from eeprom
 * @eeprom: Pointer to eeprom structure
 * @word: Word index from where we should start reading
 * @data: target pointer where the information will have to be stored
 * @words: Number of words that should be read.
 *
 * This function will read all requested words from the eeprom,
 * this is done by calling eeprom_93cx6_read() multiple times.
 * But with the additional change that while the eeprom_93cx6_read
 * will return host ordered bytes, this method will return little
 * endian words.
 */
void eeprom_93cx6_multiread(struct eeprom_93cx6 *eeprom, const u8 word,
	__le16 *data, const u16 words)
{
	unsigned int i;
	u16 tmp;

	for (i = 0; i < words; i++) {
		tmp = 0;
		eeprom_93cx6_read(eeprom, word + i, &tmp);
		data[i] = cpu_to_le16(tmp);
	}
}
EXPORT_SYMBOL_GPL(eeprom_93cx6_multiread);

