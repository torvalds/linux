// SPDX-License-Identifier: GPL-2.0
// LPC variant I/O for Microchip EC
//
// Copyright (C) 2016 Google, Inc

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "cros_ec_lpc_mec.h"

/*
 * This mutex must be held while accessing the EMI unit. We can't rely on the
 * EC mutex because memmap data may be accessed without it being held.
 */
static struct mutex io_mutex;

/*
 * cros_ec_lpc_mec_emi_write_address
 *
 * Initialize EMI read / write at a given address.
 *
 * @addr:        Starting read / write address
 * @access_type: Type of access, typically 32-bit auto-increment
 */
static void cros_ec_lpc_mec_emi_write_address(u16 addr,
			enum cros_ec_lpc_mec_emi_access_mode access_type)
{
	/* Address relative to start of EMI range */
	addr -= MEC_EMI_RANGE_START;
	outb((addr & 0xfc) | access_type, MEC_EMI_EC_ADDRESS_B0);
	outb((addr >> 8) & 0x7f, MEC_EMI_EC_ADDRESS_B1);
}

/*
 * cros_ec_lpc_io_bytes_mec - Read / write bytes to MEC EMI port
 *
 * @io_type: MEC_IO_READ or MEC_IO_WRITE, depending on request
 * @offset:  Base read / write address
 * @length:  Number of bytes to read / write
 * @buf:     Destination / source buffer
 *
 * @return 8-bit checksum of all bytes read / written
 */
u8 cros_ec_lpc_io_bytes_mec(enum cros_ec_lpc_mec_io_type io_type,
			    unsigned int offset, unsigned int length,
			    u8 *buf)
{
	int i = 0;
	int io_addr;
	u8 sum = 0;
	enum cros_ec_lpc_mec_emi_access_mode access, new_access;

	/*
	 * Long access cannot be used on misaligned data since reading B0 loads
	 * the data register and writing B3 flushes.
	 */
	if (offset & 0x3 || length < 4)
		access = ACCESS_TYPE_BYTE;
	else
		access = ACCESS_TYPE_LONG_AUTO_INCREMENT;

	mutex_lock(&io_mutex);

	/* Initialize I/O at desired address */
	cros_ec_lpc_mec_emi_write_address(offset, access);

	/* Skip bytes in case of misaligned offset */
	io_addr = MEC_EMI_EC_DATA_B0 + (offset & 0x3);
	while (i < length) {
		while (io_addr <= MEC_EMI_EC_DATA_B3) {
			if (io_type == MEC_IO_READ)
				buf[i] = inb(io_addr++);
			else
				outb(buf[i], io_addr++);

			sum += buf[i++];
			offset++;

			/* Extra bounds check in case of misaligned length */
			if (i == length)
				goto done;
		}

		/*
		 * Use long auto-increment access except for misaligned write,
		 * since writing B3 triggers the flush.
		 */
		if (length - i < 4 && io_type == MEC_IO_WRITE)
			new_access = ACCESS_TYPE_BYTE;
		else
			new_access = ACCESS_TYPE_LONG_AUTO_INCREMENT;

		if (new_access != access ||
		    access != ACCESS_TYPE_LONG_AUTO_INCREMENT) {
			access = new_access;
			cros_ec_lpc_mec_emi_write_address(offset, access);
		}

		/* Access [B0, B3] on each loop pass */
		io_addr = MEC_EMI_EC_DATA_B0;
	}

done:
	mutex_unlock(&io_mutex);

	return sum;
}
EXPORT_SYMBOL(cros_ec_lpc_io_bytes_mec);

void cros_ec_lpc_mec_init(void)
{
	mutex_init(&io_mutex);
}
EXPORT_SYMBOL(cros_ec_lpc_mec_init);

void cros_ec_lpc_mec_destroy(void)
{
	mutex_destroy(&io_mutex);
}
EXPORT_SYMBOL(cros_ec_lpc_mec_destroy);
