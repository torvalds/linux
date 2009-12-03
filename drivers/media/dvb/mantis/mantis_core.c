/*
	Mantis PCI bridge driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "mantis_common.h"
#include "mantis_core.h"


static int read_eeprom_byte(struct mantis_pci *mantis, u8 *data, u8 length)
{
	int err;
	struct i2c_msg msg[] = {
		{
			.addr = 0x50,
			.flags = 0,
			.buf = data,
			.len = 1
		},{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.buf = data,
			.len = length
		},
	};
	if ((err = i2c_transfer(&mantis->adapter, msg, 2)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1,
			"ERROR: i2c read: < err=%i d0=0x%02x d1=0x%02x >",
			err, data[0], data[1]);

		return err;
	}
	msleep_interruptible(2);

	return 0;
}

static int write_eeprom_byte(struct mantis_pci *mantis, u8 *data, u8 length)
{
	int err;

	struct i2c_msg msg = {
		.addr = 0x50,
		.flags = 0,
		.buf = data,
		.len = length
	};

	if ((err = i2c_transfer(&mantis->adapter, &msg, 1)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1,
			"ERROR: i2c write: < err=%i length=0x%02x d0=0x%02x, d1=0x%02x >",
			err, length, data[0], data[1]);

		return err;
	}

	return 0;
}

static int get_subdevice_id(struct mantis_pci *mantis)
{
	int err;
	static u8 sub_device_id[2];

	mantis->sub_device_id = 0;
	sub_device_id[0] = 0xfc;
	if ((err = read_eeprom_byte(mantis, &sub_device_id[0], 2)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "Mantis EEPROM read error");
		return err;
	}
	mantis->sub_device_id = (sub_device_id[0] << 8) | sub_device_id[1];
	dprintk(verbose, MANTIS_ERROR, 1, "Sub Device ID=[0x%04x]",
		mantis->sub_device_id);

	return 0;
}

static int get_subvendor_id(struct mantis_pci *mantis)
{
	int err;
	static u8 sub_vendor_id[2];

	mantis->sub_vendor_id = 0;
	sub_vendor_id[0] = 0xfe;
	if ((err = read_eeprom_byte(mantis, &sub_vendor_id[0], 2)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "Mantis EEPROM read error");
		return err;
	}
	mantis->sub_vendor_id = (sub_vendor_id[0] << 8) | sub_vendor_id[1];
	dprintk(verbose, MANTIS_ERROR, 1, "Sub Vendor ID=[0x%04x]",
		mantis->sub_vendor_id);

	return 0;
}

static int get_mac_address(struct mantis_pci *mantis)
{
	int err;

	mantis->mac_address[0] = 0x08;
	if ((err = read_eeprom_byte(mantis, &mantis->mac_address[0], 6)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "Mantis EEPROM read error");

		return err;
	}
	dprintk(verbose, MANTIS_ERROR, 1,
		"MAC Address=[%02x:%02x:%02x:%02x:%02x:%02x]",
		mantis->mac_address[0], mantis->mac_address[1],
		mantis->mac_address[2],	mantis->mac_address[3],
		mantis->mac_address[4], mantis->mac_address[5]);

	return 0;
}


int mantis_core_init(struct mantis_pci *mantis)
{
	int err = 0;

	if ((err = mantis_i2c_init(mantis)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "Mantis I2C init failed");
		return err;
	}
	if ((err = get_mac_address(mantis)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "get MAC address failed");
		return err;
	}
	if ((err = get_subvendor_id(mantis)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "get Sub vendor ID failed");
		return err;
	}
	if ((err = get_subdevice_id(mantis)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "get Sub device ID failed");
		return err;
	}
	if ((err = mantis_dma_init(mantis)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "Mantis DMA init failed");
		return err;
	}
	if ((err = mantis_dvb_init(mantis)) < 0) {
		dprintk(verbose, MANTIS_DEBUG, 1, "Mantis DVB init failed");
		return err;
	}

	return 0;
}

int mantis_core_exit(struct mantis_pci *mantis)
{

	mantis_dma_stop(mantis);
	dprintk(verbose, MANTIS_ERROR, 1, "DMA engine stopping");
	if (mantis_dma_exit(mantis) < 0)
		dprintk(verbose, MANTIS_ERROR, 1, "DMA exit failed");
	if (mantis_dvb_exit(mantis) < 0)
		dprintk(verbose, MANTIS_ERROR, 1, "DVB exit failed");
	if (mantis_i2c_exit(mantis) < 0)
		dprintk(verbose, MANTIS_ERROR, 1, "I2C adapter delete.. failed");

	return 0;
}

void gpio_set_bits(struct mantis_pci *mantis, u32 bitpos, u8 value)
{
	u32 reg;

	if (value)
		reg = 0x0000;
	else
		reg = 0xffff;

	reg = (value << bitpos);

	mmwrite(mmread(MANTIS_GPIF_ADDR) | reg, MANTIS_GPIF_ADDR);
	mmwrite(0x00, MANTIS_GPIF_DOUT);
	udelay(100);
	mmwrite(mmread(MANTIS_GPIF_ADDR) | reg, MANTIS_GPIF_ADDR);
	mmwrite(0x00, MANTIS_GPIF_DOUT);
}


//direction = 0 , no CI passthrough ; 1 , CI passthrough
void mantis_set_direction(struct mantis_pci *mantis, int direction)
{
	u32 reg;

	reg = mmread(0x28);
	dprintk(verbose, MANTIS_DEBUG, 1, "TS direction setup");
	if (direction == 0x01) { //to CI
		reg |= 0x04;
		mmwrite(reg, 0x28);
		reg &= 0xff - 0x04;
		mmwrite(reg, 0x28);
	} else {
		reg &= 0xff - 0x04;
		mmwrite(reg, 0x28);
		reg |= 0x04;
		mmwrite(reg, 0x28);
	}
}
