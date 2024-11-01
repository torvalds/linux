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

/* AT24CM02 and M24M02-R have a 256-byte write page size.
 */
#define EEPROM_PAGE_BITS   8
#define EEPROM_PAGE_SIZE   (1U << EEPROM_PAGE_BITS)
#define EEPROM_PAGE_MASK   (EEPROM_PAGE_SIZE - 1)

#define EEPROM_OFFSET_SIZE 2

/* EEPROM memory addresses are 19-bits long, which can
 * be partitioned into 3, 8, 8 bits, for a total of 19.
 * The upper 3 bits are sent as part of the 7-bit
 * "Device Type Identifier"--an I2C concept, which for EEPROM devices
 * is hard-coded as 1010b, indicating that it is an EEPROM
 * device--this is the wire format, followed by the upper
 * 3 bits of the 19-bit address, followed by the direction,
 * followed by two bytes holding the rest of the 16-bits of
 * the EEPROM memory address. The format on the wire for EEPROM
 * devices is: 1010XYZD, A15:A8, A7:A0,
 * Where D is the direction and sequenced out by the hardware.
 * Bits XYZ are memory address bits 18, 17 and 16.
 * These bits are compared to how pins 1-3 of the part are connected,
 * depending on the size of the part, more on that later.
 *
 * Note that of this wire format, a client is in control
 * of, and needs to specify only XYZ, A15:A8, A7:0, bits,
 * which is exactly the EEPROM memory address, or offset,
 * in order to address up to 8 EEPROM devices on the I2C bus.
 *
 * For instance, a 2-Mbit I2C EEPROM part, addresses all its bytes,
 * using an 18-bit address, bit 17 to 0 and thus would use all but one bit of
 * the 19 bits previously mentioned. The designer would then not connect
 * pins 1 and 2, and pin 3 usually named "A_2" or "E2", would be connected to
 * either Vcc or GND. This would allow for up to two 2-Mbit parts on
 * the same bus, where one would be addressable with bit 18 as 1, and
 * the other with bit 18 of the address as 0.
 *
 * For a 2-Mbit part, bit 18 is usually known as the "Chip Enable" or
 * "Hardware Address Bit". This bit is compared to the load on pin 3
 * of the device, described above, and if there is a match, then this
 * device responds to the command. This way, you can connect two
 * 2-Mbit EEPROM devices on the same bus, but see one contiguous
 * memory from 0 to 7FFFFh, where address 0 to 3FFFF is in the device
 * whose pin 3 is connected to GND, and address 40000 to 7FFFFh is in
 * the 2nd device, whose pin 3 is connected to Vcc.
 *
 * This addressing you encode in the 32-bit "eeprom_addr" below,
 * namely the 19-bits "XYZ,A15:A0", as a single 19-bit address. For
 * instance, eeprom_addr = 0x6DA01, is 110_1101_1010_0000_0001, where
 * XYZ=110b, and A15:A0=DA01h. The XYZ bits become part of the device
 * address, and the rest of the address bits are sent as the memory
 * address bytes.
 *
 * That is, for an I2C EEPROM driver everything is controlled by
 * the "eeprom_addr".
 *
 * See also top of amdgpu_ras_eeprom.c.
 *
 * P.S. If you need to write, lock and read the Identification Page,
 * (M24M02-DR device only, which we do not use), change the "7" to
 * "0xF" in the macro below, and let the client set bit 20 to 1 in
 * "eeprom_addr", and set A10 to 0 to write into it, and A10 and A1 to
 * 1 to lock it permanently.
 */
#define MAKE_I2C_ADDR(_aa) ((0xA << 3) | (((_aa) >> 16) & 7))

static int __amdgpu_eeprom_xfer(struct i2c_adapter *i2c_adap, u32 eeprom_addr,
				u8 *eeprom_buf, u16 buf_size, bool read)
{
	u8 eeprom_offset_buf[EEPROM_OFFSET_SIZE];
	struct i2c_msg msgs[] = {
		{
			.flags = 0,
			.len = EEPROM_OFFSET_SIZE,
			.buf = eeprom_offset_buf,
		},
		{
			.flags = read ? I2C_M_RD : 0,
		},
	};
	const u8 *p = eeprom_buf;
	int r;
	u16 len;

	for (r = 0; buf_size > 0;
	      buf_size -= len, eeprom_addr += len, eeprom_buf += len) {
		/* Set the EEPROM address we want to write to/read from.
		 */
		msgs[0].addr = MAKE_I2C_ADDR(eeprom_addr);
		msgs[1].addr = msgs[0].addr;
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
			 * writing into a page, the I2C driver should
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

		/* This constitutes a START-STOP transaction.
		 */
		r = i2c_transfer(i2c_adap, msgs, ARRAY_SIZE(msgs));
		if (r != ARRAY_SIZE(msgs))
			break;

		if (!read) {
			/* According to EEPROM specs the length of the
			 * self-writing cycle, tWR (tW), is 10 ms.
			 *
			 * TODO: Use polling on ACK, aka Acknowledge
			 * Polling, to minimize waiting for the
			 * internal write cycle to complete, as it is
			 * usually smaller than tWR (tW).
			 */
			msleep(10);
		}
	}

	return r < 0 ? r : eeprom_buf - p;
}

/**
 * amdgpu_eeprom_xfer -- Read/write from/to an I2C EEPROM device
 * @i2c_adap: pointer to the I2C adapter to use
 * @eeprom_addr: EEPROM address from which to read/write
 * @eeprom_buf: pointer to data buffer to read into/write from
 * @buf_size: the size of @eeprom_buf
 * @read: True if reading from the EEPROM, false if writing
 *
 * Returns the number of bytes read/written; -errno on error.
 */
static int amdgpu_eeprom_xfer(struct i2c_adapter *i2c_adap, u32 eeprom_addr,
			      u8 *eeprom_buf, u32 buf_size, bool read)
{
	const struct i2c_adapter_quirks *quirks = i2c_adap->quirks;
	u16 limit;

	if (!quirks)
		limit = 0;
	else if (read)
		limit = quirks->max_read_len;
	else
		limit = quirks->max_write_len;

	if (limit == 0) {
		return __amdgpu_eeprom_xfer(i2c_adap, eeprom_addr,
					    eeprom_buf, buf_size, read);
	} else if (limit <= EEPROM_OFFSET_SIZE) {
		dev_err_ratelimited(&i2c_adap->dev,
				    "maddr:0x%04X size:0x%02X:quirk max_%s_len must be > %d",
				    eeprom_addr, buf_size,
				    read ? "read" : "write", EEPROM_OFFSET_SIZE);
		return -EINVAL;
	} else {
		u16 ps; /* Partial size */
		int res = 0, r;

		/* The "limit" includes all data bytes sent/received,
		 * which would include the EEPROM_OFFSET_SIZE bytes.
		 * Account for them here.
		 */
		limit -= EEPROM_OFFSET_SIZE;
		for ( ; buf_size > 0;
		      buf_size -= ps, eeprom_addr += ps, eeprom_buf += ps) {
			ps = min(limit, buf_size);

			r = __amdgpu_eeprom_xfer(i2c_adap, eeprom_addr,
						 eeprom_buf, ps, read);
			if (r < 0)
				return r;
			res += r;
		}

		return res;
	}
}

int amdgpu_eeprom_read(struct i2c_adapter *i2c_adap,
		       u32 eeprom_addr, u8 *eeprom_buf,
		       u32 bytes)
{
	return amdgpu_eeprom_xfer(i2c_adap, eeprom_addr, eeprom_buf, bytes,
				  true);
}

int amdgpu_eeprom_write(struct i2c_adapter *i2c_adap,
			u32 eeprom_addr, u8 *eeprom_buf,
			u32 bytes)
{
	return amdgpu_eeprom_xfer(i2c_adap, eeprom_addr, eeprom_buf, bytes,
				  false);
}
