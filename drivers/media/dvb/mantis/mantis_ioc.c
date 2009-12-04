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

#include <linux/kernel.h>
#include <linux/i2c.h>

#include <asm/irq.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "mantis_reg.h"
#include "mantis_ioc.h"

static int read_eeprom_byte(struct mantis_pci *mantis, u8 *data, u8 length)
{
	struct i2c_adapter *adapter = &mantis->adapter;

	int err;
	struct i2c_msg msg[] = {
		{ .addr = 0x50, .flags = 0, .buf = data, .len = 1 },
		{ .addr = 0x50, .flags = I2C_M_RD, .buf = data, .len = length },
	};

	err = i2c_transfer(adapter, msg, 2);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: i2c read: < err=%i d0=0x%02x d1=0x%02x >",
			err, data[0], data[1]);

		return err;
	}

	return 0;
}

static int write_eeprom_byte(struct mantis_pci *mantis, u8 *data, u8 length)
{
	struct i2c_adapter *adapter = &mantis->adapter;
	int err;

	struct i2c_msg msg = { .addr = 0x50, .flags = 0, .buf = data, .len = length };

	err = i2c_transfer(adapter, &msg, 1);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: i2c write: < err=%i length=0x%02x d0=0x%02x, d1=0x%02x >",
			err, length, data[0], data[1]);

		return err;
	}

	return 0;
}

int mantis_get_mac(struct mantis_pci *mantis)
{
	int err;

	mantis->mac_address[0] = 0x08;

	err = read_eeprom_byte(mantis, &mantis->mac_address[0], 6);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Mantis EEPROM read error <%d>", err);

		return err;
	}

	dprintk(MANTIS_ERROR, 0,
		"    MAC Address=[%02x:%02x:%02x:%02x:%02x:%02x]\n",
		mantis->mac_address[0], mantis->mac_address[1],
		mantis->mac_address[2],	mantis->mac_address[3],
		mantis->mac_address[4], mantis->mac_address[5]);

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_get_mac);

/* Turn the given bit on or off. */
void gpio_set_bits(struct mantis_pci *mantis, u32 bitpos, u8 value)
{
	u32 cur;

	cur = mmread(MANTIS_GPIF_ADDR);
	if (value)
		mantis->gpio_status = cur | (1 << bitpos);
	else
		mantis->gpio_status = cur & (~(1 << bitpos));

	mmwrite(mantis->gpio_status, MANTIS_GPIF_ADDR);
	mmwrite(0x00, MANTIS_GPIF_DOUT);
}
EXPORT_SYMBOL_GPL(gpio_set_bits);

int mantis_stream_control(struct mantis_pci *mantis, enum mantis_stream_control stream_ctl)
{
	u32 reg;

	reg = mmread(MANTIS_CONTROL);
	switch (stream_ctl) {
	case STREAM_TO_HIF:
		dprintk(MANTIS_DEBUG, 1, "Set stream to HIF");
		reg &= 0xff - MANTIS_BYPASS;
		mmwrite(reg, MANTIS_CONTROL);
		reg |= MANTIS_BYPASS;
		mmwrite(reg, MANTIS_CONTROL);
		break;

	case STREAM_TO_CAM:
		dprintk(MANTIS_DEBUG, 1, "Set stream to CAM");
		reg |= MANTIS_BYPASS;
		mmwrite(reg, MANTIS_CONTROL);
		reg &= 0xff - MANTIS_BYPASS;
		mmwrite(reg, MANTIS_CONTROL);
		break;
	default:
		dprintk(MANTIS_ERROR, 1, "Unknown MODE <%02x>", stream_ctl);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_stream_control);
