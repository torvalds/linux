// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "amdgpu.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_ras_eeprom.h"
#include "amdgpu_ras_mgr.h"
#include "amdgpu_ras_eeprom_i2c.h"
#include "ras_eeprom.h"

/* These are memory addresses as would be seen by one or more EEPROM
 * chips strung on the I2C bus, usually by manipulating pins 1-3 of a
 * set of EEPROM devices. They form a continuous memory space.
 *
 * The I2C device address includes the device type identifier, 1010b,
 * which is a reserved value and indicates that this is an I2C EEPROM
 * device. It also includes the top 3 bits of the 19 bit EEPROM memory
 * address, namely bits 18, 17, and 16. This makes up the 7 bit
 * address sent on the I2C bus with bit 0 being the direction bit,
 * which is not represented here, and sent by the hardware directly.
 *
 * For instance,
 *   50h = 1010000b => device type identifier 1010b, bits 18:16 = 000b, address 0.
 *   54h = 1010100b => --"--, bits 18:16 = 100b, address 40000h.
 *   56h = 1010110b => --"--, bits 18:16 = 110b, address 60000h.
 * Depending on the size of the I2C EEPROM device(s), bits 18:16 may
 * address memory in a device or a device on the I2C bus, depending on
 * the status of pins 1-3. See top of amdgpu_eeprom.c.
 *
 * The RAS table lives either at address 0 or address 40000h of EEPROM.
 */
#define EEPROM_I2C_MADDR_0      0x0
#define EEPROM_I2C_MADDR_4      0x40000

#define MAKE_I2C_ADDR(_aa) ((0xA << 3) | (((_aa) >> 16) & 0xF))
#define to_amdgpu_ras(x) (container_of(x, struct amdgpu_ras, eeprom_control))

#define EEPROM_PAGE_BITS   8
#define EEPROM_PAGE_SIZE   (1U << EEPROM_PAGE_BITS)
#define EEPROM_PAGE_MASK   (EEPROM_PAGE_SIZE - 1)

#define EEPROM_OFFSET_SIZE 2

static int ras_eeprom_i2c_config(struct ras_core_context *ras_core)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct ras_eeprom_control *control = &ras_core->ras_eeprom;
	u8 i2c_addr;

	if (amdgpu_atomfirmware_ras_rom_addr(adev, &i2c_addr)) {
		/* The address given by VBIOS is an 8-bit, wire-format
		 * address, i.e. the most significant byte.
		 *
		 * Normalize it to a 19-bit EEPROM address. Remove the
		 * device type identifier and make it a 7-bit address;
		 * then make it a 19-bit EEPROM address. See top of
		 * amdgpu_eeprom.c.
		 */
		i2c_addr = (i2c_addr & 0x0F) >> 1;
		control->i2c_address = ((u32) i2c_addr) << 16;
		return 0;
	}

	switch (amdgpu_ip_version(adev, MP1_HWIP, 0)) {
	case IP_VERSION(13, 0, 5):
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 10):
	case IP_VERSION(13, 0, 12):
	case IP_VERSION(13, 0, 14):
		control->i2c_address = EEPROM_I2C_MADDR_4;
		return 0;
	default:
		return -ENODATA;
	}
	return -ENODATA;
}

static int ras_eeprom_i2c_xfer(struct ras_core_context *ras_core, u32 eeprom_addr,
				u8 *eeprom_buf, u32 buf_size, bool read)
{
	struct i2c_adapter *i2c_adap = ras_core->ras_eeprom.i2c_adapter;
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
			len = min(EEPROM_PAGE_SIZE - (eeprom_addr & EEPROM_PAGE_MASK),
					buf_size);
		} else {
			/* Reading from the EEPROM has no limitation
			 * on the number of bytes read from the EEPROM
			 * device--they are simply sequenced out.
			 * Keep in mind that i2c_msg.len is u16 type.
			 */
			len = min(U16_MAX, buf_size);
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

const struct ras_eeprom_sys_func amdgpu_ras_eeprom_i2c_sys_func = {
	.eeprom_i2c_xfer = ras_eeprom_i2c_xfer,
	.update_eeprom_i2c_config = ras_eeprom_i2c_config,
};
