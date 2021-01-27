/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "amdgpu_eeprom.h"
#include "amdgpu.h"

/* AT24CM02 has a 256-byte write page size.
 */
#define EEPROM_PAGE_BITS   8
#define EEPROM_PAGE_SIZE   (1U << EEPROM_PAGE_BITS)
#define EEPROM_PAGE_MASK   (EEPROM_PAGE_SIZE - 1)

#define EEPROM_OFFSET_SIZE 2

/**
 * amdgpu_eeprom_xfer -- Read/write from/to an I2C EEPROM device
 * @i2c_adap: pointer to the I2C adapter to use
 * @slave_addr: I2C address of the slave device
 * @eeprom_addr: EEPROM address from which to read/write
 * @eeprom_buf: pointer to data buffer to read into/write from
 * @buf_size: the size of @eeprom_buf
 * @read: True if reading from the EEPROM, false if writing
 *
 * Returns the number of bytes read/written; -errno on error.
 */
int amdgpu_eeprom_xfer(struct i2c_adapter *i2c_adap,
		       u16 slave_addr, u16 eeprom_addr,
		       u8 *eeprom_buf, u16 buf_size, bool read)
{
	u8 eeprom_offset_buf[EEPROM_OFFSET_SIZE];
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = EEPROM_OFFSET_SIZE,
			.buf = eeprom_offset_buf,
		},
		{
			.addr = slave_addr,
			.flags = read ? I2C_M_RD : 0,
		},
	};
	const u8 *p = eeprom_buf;
	int r;
	u16 len;

	r = 0;
	for (len = 0; buf_size > 0;
	     buf_size -= len, eeprom_addr += len, eeprom_buf += len) {
		/* Set the EEPROM address we want to write to/read from.
		 */
		msgs[0].buf[0] = (eeprom_addr >> 8) & 0xff;
		msgs[0].buf[1] = eeprom_addr & 0xff;

		if (!read) {
			/* Write the maximum amount of data, without
			 * crossing the device's page boundary, as per
			 * its spec. Partial page writes are allowed,
			 * starting at any location within the page,
			 * so long as the page boundary isn't crossed
			 * over (actually the page pointer rolls
			 * over).
			 *
			 * As per the AT24CM02 EEPROM spec, after
			 * writing into a page, the I2C driver MUST
			 * terminate the transfer, i.e. in
			 * "i2c_transfer()" below, with a STOP
			 * condition, so that the self-timed write
			 * cycle begins. This is implied for the
			 * "i2c_transfer()" abstraction.
			 */
			len = min(EEPROM_PAGE_SIZE - (eeprom_addr &
						      EEPROM_PAGE_MASK),
				  (u32)buf_size);
		} else {
			/* Reading from the EEPROM has no limitation
			 * on the number of bytes read from the EEPROM
			 * device--they are simply sequenced out.
			 */
			len = buf_size;
		}
		msgs[1].len = len;
		msgs[1].buf = eeprom_buf;

		r = i2c_transfer(i2c_adap, msgs, ARRAY_SIZE(msgs));
		if (r < ARRAY_SIZE(msgs))
			break;

		if (!read) {
			/* According to the AT24CM02 EEPROM spec the
			 * length of the self-writing cycle, tWR, is
			 * 10 ms.
			 *
			 * TODO Improve to wait for first ACK for slave address after
			 * internal write cycle done.
			 */
			msleep(10);
		}
	}

	return r < 0 ? r : eeprom_buf - p;
}
