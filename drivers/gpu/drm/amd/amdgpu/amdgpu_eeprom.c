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

#define EEPROM_OFFSET_LENGTH 2

int amdgpu_eeprom_xfer(struct i2c_adapter *i2c_adap,
		       u16 slave_addr, u16 eeprom_addr,
		       u8 *eeprom_buf, u16 bytes, bool read)
{
	u8 eeprom_offset_buf[2];
	u16 bytes_transferred;
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = EEPROM_OFFSET_LENGTH,
			.buf = eeprom_offset_buf,
		},
		{
			.addr = slave_addr,
			.flags = read ? I2C_M_RD : 0,
			.len = bytes,
			.buf = eeprom_buf,
		},
	};
	int r;

	msgs[0].buf[0] = ((eeprom_addr >> 8) & 0xff);
	msgs[0].buf[1] = (eeprom_addr & 0xff);

	while (msgs[1].len) {
		r = i2c_transfer(i2c_adap, msgs, ARRAY_SIZE(msgs));
		if (r <= 0)
			return r;

		/* Only for write data */
		if (!msgs[1].flags)
			/*
			 * According to EEPROM spec there is a MAX of 10 ms required for
			 * EEPROM to flush internal RX buffer after STOP was issued at the
			 * end of write transaction. During this time the EEPROM will not be
			 * responsive to any more commands - so wait a bit more.
			 *
			 * TODO Improve to wait for first ACK for slave address after
			 * internal write cycle done.
			 */
			msleep(10);


		bytes_transferred = r - EEPROM_OFFSET_LENGTH;
		eeprom_addr += bytes_transferred;
		msgs[0].buf[0] = ((eeprom_addr >> 8) & 0xff);
		msgs[0].buf[1] = (eeprom_addr & 0xff);
		msgs[1].buf += bytes_transferred;
		msgs[1].len -= bytes_transferred;
	}

	return 0;
}
