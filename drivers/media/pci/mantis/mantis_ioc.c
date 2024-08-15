// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#include <linux/kernel.h>
#include <linux/i2c.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>

#include "mantis_common.h"
#include "mantis_reg.h"
#include "mantis_ioc.h"

static int read_eeprom_bytes(struct mantis_pci *mantis, u8 reg, u8 *data, u8 length)
{
	struct i2c_adapter *adapter = &mantis->adapter;
	int err;
	u8 buf = reg;

	struct i2c_msg msg[] = {
		{ .addr = 0x50, .flags = 0, .buf = &buf, .len = 1 },
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
int mantis_get_mac(struct mantis_pci *mantis)
{
	int err;
	u8 mac_addr[6] = {0};

	err = read_eeprom_bytes(mantis, 0x08, mac_addr, 6);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Mantis EEPROM read error <%d>", err);

		return err;
	}

	dprintk(MANTIS_ERROR, 0, "    MAC Address=[%pM]\n", mac_addr);

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_get_mac);

/* Turn the given bit on or off. */
void mantis_gpio_set_bits(struct mantis_pci *mantis, u32 bitpos, u8 value)
{
	u32 cur;

	dprintk(MANTIS_DEBUG, 1, "Set Bit <%d> to <%d>", bitpos, value);
	cur = mmread(MANTIS_GPIF_ADDR);
	if (value)
		mantis->gpio_status = cur | (1 << bitpos);
	else
		mantis->gpio_status = cur & (~(1 << bitpos));

	dprintk(MANTIS_DEBUG, 1, "GPIO Value <%02x>", mantis->gpio_status);
	mmwrite(mantis->gpio_status, MANTIS_GPIF_ADDR);
	mmwrite(0x00, MANTIS_GPIF_DOUT);
}
EXPORT_SYMBOL_GPL(mantis_gpio_set_bits);

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
