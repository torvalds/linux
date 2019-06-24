// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2004 - 2006 rt2x00 SourceForge Project
 * <http://rt2x00.serialmonkey.com>
 *
 * Module: eeprom_93cx6
 * Abstract: EEPROM reader routines for 93cx6 chipsets.
 * Supported chipsets: 93c46 & 93c66.
 */

#include <linux/kernel.h>
#include <linux/module.h>
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
	eeprom->drive_data = 1;
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
	eeprom->drive_data = 1;

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
	eeprom->drive_data = 0;

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
 * eeprom_93cx6_read - Read a word from eeprom
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

/**
 * eeprom_93cx6_readb - Read a byte from eeprom
 * @eeprom: Pointer to eeprom structure
 * @word: Byte index from where we should start reading
 * @data: target pointer where the information will have to be stored
 *
 * This function will read a byte of the eeprom data
 * into the given data pointer.
 */
void eeprom_93cx6_readb(struct eeprom_93cx6 *eeprom, const u8 byte,
	u8 *data)
{
	u16 command;
	u16 tmp;

	/*
	 * Initialize the eeprom register
	 */
	eeprom_93cx6_startup(eeprom);

	/*
	 * Select the read opcode and the byte to be read.
	 */
	command = (PCI_EEPROM_READ_OPCODE << (eeprom->width + 1)) | byte;
	eeprom_93cx6_write_bits(eeprom, command,
		PCI_EEPROM_WIDTH_OPCODE + eeprom->width + 1);

	/*
	 * Read the requested 8 bits.
	 */
	eeprom_93cx6_read_bits(eeprom, &tmp, 8);
	*data = tmp & 0xff;

	/*
	 * Cleanup eeprom register.
	 */
	eeprom_93cx6_cleanup(eeprom);
}
EXPORT_SYMBOL_GPL(eeprom_93cx6_readb);

/**
 * eeprom_93cx6_multireadb - Read multiple bytes from eeprom
 * @eeprom: Pointer to eeprom structure
 * @byte: Index from where we should start reading
 * @data: target pointer where the information will have to be stored
 * @words: Number of bytes that should be read.
 *
 * This function will read all requested bytes from the eeprom,
 * this is done by calling eeprom_93cx6_readb() multiple times.
 */
void eeprom_93cx6_multireadb(struct eeprom_93cx6 *eeprom, const u8 byte,
	u8 *data, const u16 bytes)
{
	unsigned int i;

	for (i = 0; i < bytes; i++)
		eeprom_93cx6_readb(eeprom, byte + i, &data[i]);
}
EXPORT_SYMBOL_GPL(eeprom_93cx6_multireadb);

/**
 * eeprom_93cx6_wren - set the write enable state
 * @eeprom: Pointer to eeprom structure
 * @enable: true to enable writes, otherwise disable writes
 *
 * Set the EEPROM write enable state to either allow or deny
 * writes depending on the @enable value.
 */
void eeprom_93cx6_wren(struct eeprom_93cx6 *eeprom, bool enable)
{
	u16 command;

	/* start the command */
	eeprom_93cx6_startup(eeprom);

	/* create command to enable/disable */

	command = enable ? PCI_EEPROM_EWEN_OPCODE : PCI_EEPROM_EWDS_OPCODE;
	command <<= (eeprom->width - 2);

	eeprom_93cx6_write_bits(eeprom, command,
				PCI_EEPROM_WIDTH_OPCODE + eeprom->width);

	eeprom_93cx6_cleanup(eeprom);
}
EXPORT_SYMBOL_GPL(eeprom_93cx6_wren);

/**
 * eeprom_93cx6_write - write data to the EEPROM
 * @eeprom: Pointer to eeprom structure
 * @addr: Address to write data to.
 * @data: The data to write to address @addr.
 *
 * Write the @data to the specified @addr in the EEPROM and
 * waiting for the device to finish writing.
 *
 * Note, since we do not expect large number of write operations
 * we delay in between parts of the operation to avoid using excessive
 * amounts of CPU time busy waiting.
 */
void eeprom_93cx6_write(struct eeprom_93cx6 *eeprom, u8 addr, u16 data)
{
	int timeout = 100;
	u16 command;

	/* start the command */
	eeprom_93cx6_startup(eeprom);

	command = PCI_EEPROM_WRITE_OPCODE << eeprom->width;
	command |= addr;

	/* send write command */
	eeprom_93cx6_write_bits(eeprom, command,
				PCI_EEPROM_WIDTH_OPCODE + eeprom->width);

	/* send data */
	eeprom_93cx6_write_bits(eeprom, data, 16);

	/* get ready to check for busy */
	eeprom->drive_data = 0;
	eeprom->reg_chip_select = 1;
	eeprom->register_write(eeprom);

	/* wait at-least 250ns to get DO to be the busy signal */
	usleep_range(1000, 2000);

	/* wait for DO to go high to signify finish */

	while (true) {
		eeprom->register_read(eeprom);

		if (eeprom->reg_data_out)
			break;

		usleep_range(1000, 2000);

		if (--timeout <= 0) {
			printk(KERN_ERR "%s: timeout\n", __func__);
			break;
		}
	}

	eeprom_93cx6_cleanup(eeprom);
}
EXPORT_SYMBOL_GPL(eeprom_93cx6_write);
