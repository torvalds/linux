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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include "mantis_common.h"

#define I2C_HW_B_MANTIS		0x1c

static int mantis_ack_wait(struct mantis_pci *mantis)
{
	int rc = 0;
	u32 timeout = 0;

	if (wait_event_interruptible_timeout(mantis->i2c_wq,
					     mantis->mantis_int_stat & MANTIS_INT_I2CDONE,
					     msecs_to_jiffies(50)) == -ERESTARTSYS) {

		dprintk(verbose, MANTIS_DEBUG, 1, "Master !I2CDONE");
		rc = -EREMOTEIO;
	}
	while (!(mantis->mantis_int_stat & MANTIS_INT_I2CRACK)) {
		dprintk(verbose, MANTIS_DEBUG, 1, "Waiting for Slave RACK");
		mantis->mantis_int_stat = mmread(MANTIS_INT_STAT);
		msleep(5);
		timeout++;
		if (timeout > 500) {
			dprintk(verbose, MANTIS_ERROR, 1, "Slave RACK Fail !");
			rc = -EREMOTEIO;
			break;
		}
	}
	udelay(350);

	return rc;
}

static int mantis_i2c_read(struct mantis_pci *mantis, const struct i2c_msg *msg)
{
	u32 rxd, i;

	dprintk(verbose, MANTIS_INFO, 0, "        %s:  Address=[0x%02x] <R>[ ",
		__func__, msg->addr);

	for (i = 0; i < msg->len; i++) {
		rxd = (msg->addr << 25) | (1 << 24)
					| MANTIS_I2C_RATE_3
					| MANTIS_I2C_STOP
					| MANTIS_I2C_PGMODE;

		if (i == (msg->len - 1))
			rxd &= ~MANTIS_I2C_STOP;

		mmwrite(MANTIS_INT_I2CDONE, MANTIS_INT_STAT);
		mmwrite(rxd, MANTIS_I2CDATA_CTL);
		if (mantis_ack_wait(mantis) != 0) {
			dprintk(verbose, MANTIS_DEBUG, 1, "ACK failed<R>");
			return -EREMOTEIO;
		}
		rxd = mmread(MANTIS_I2CDATA_CTL);
		msg->buf[i] = (u8)((rxd >> 8) & 0xFF);
		dprintk(verbose, MANTIS_INFO, 0, "%02x ", msg->buf[i]);
	}
	dprintk(verbose, MANTIS_INFO, 0, "]\n");

	return 0;
}

static int mantis_i2c_write(struct mantis_pci *mantis, const struct i2c_msg *msg)
{
	int i;
	u32 txd = 0;

	dprintk(verbose, MANTIS_INFO, 0, "        %s: Address=[0x%02x] <W>[ ",
		__func__, msg->addr);

	for (i = 0; i < msg->len; i++) {
		dprintk(verbose, MANTIS_INFO, 0, "%02x ", msg->buf[i]);
		txd = (msg->addr << 25) | (msg->buf[i] << 8)
					| MANTIS_I2C_RATE_3
					| MANTIS_I2C_STOP
					| MANTIS_I2C_PGMODE;

		if (i == (msg->len - 1))
			txd &= ~MANTIS_I2C_STOP;

		mmwrite(MANTIS_INT_I2CDONE, MANTIS_INT_STAT);
		mmwrite(txd, MANTIS_I2CDATA_CTL);
		if (mantis_ack_wait(mantis) != 0) {
			dprintk(verbose, MANTIS_DEBUG, 1, "ACK failed<W>");
			return -EREMOTEIO;
		}
	}
	dprintk(verbose, MANTIS_INFO, 0, "]\n");

	return 0;
}

static int mantis_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	int ret = 0, i;
	struct mantis_pci *mantis;

	mantis = i2c_get_adapdata(adapter);
	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD)
			ret = mantis_i2c_read(mantis, &msgs[i]);
		else
			ret = mantis_i2c_write(mantis, &msgs[i]);

		if (ret < 0)
			return ret;
	}

	return num;
}

static u32 mantis_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm mantis_algo = {
	.master_xfer		= mantis_i2c_xfer,
	.functionality		= mantis_i2c_func,
};

static struct i2c_adapter mantis_i2c_adapter = {
	.owner			= THIS_MODULE,
	.name			= "Mantis I2C",
	.id			= I2C_HW_B_MANTIS,
	.class			= I2C_CLASS_TV_DIGITAL,
	.algo			= &mantis_algo,
};

int __devinit mantis_i2c_init(struct mantis_pci *mantis)
{
	u32 intstat, intmask;

	memcpy(&mantis->adapter, &mantis_i2c_adapter, sizeof (mantis_i2c_adapter));
	i2c_set_adapdata(&mantis->adapter, mantis);
	mantis->i2c_rc = i2c_add_adapter(&mantis->adapter);
	if (mantis->i2c_rc < 0)
		return mantis->i2c_rc;

	dprintk(verbose, MANTIS_DEBUG, 1, "Initializing I2C ..");

	intstat = mmread(MANTIS_INT_STAT);
	intmask = mmread(MANTIS_INT_MASK);
	mmwrite(intstat, MANTIS_INT_STAT);
	mmwrite(intmask | MANTIS_INT_I2CDONE, MANTIS_INT_MASK);

	dprintk(verbose, MANTIS_DEBUG, 1, "[0x%08x/%08x]", intstat, intmask);

	return 0;
}

int __devexit mantis_i2c_exit(struct mantis_pci *mantis)
{
	dprintk(verbose, MANTIS_DEBUG, 1, "Removing I2C adapter");
	return i2c_del_adapter(&mantis->adapter);
}
