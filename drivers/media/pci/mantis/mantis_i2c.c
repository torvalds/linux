/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/i2c.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "mantis_reg.h"
#include "mantis_i2c.h"

#define TRIALS			10000

static int mantis_i2c_read(struct mantis_pci *mantis, const struct i2c_msg *msg)
{
	u32 rxd, i, stat, trials;

	dprintk(MANTIS_INFO, 0, "        %s:  Address=[0x%02x] <R>[ ",
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

		/* wait for xfer completion */
		for (trials = 0; trials < TRIALS; trials++) {
			stat = mmread(MANTIS_INT_STAT);
			if (stat & MANTIS_INT_I2CDONE)
				break;
		}

		dprintk(MANTIS_TMG, 0, "I2CDONE: trials=%d\n", trials);

		/* wait for xfer completion */
		for (trials = 0; trials < TRIALS; trials++) {
			stat = mmread(MANTIS_INT_STAT);
			if (stat & MANTIS_INT_I2CRACK)
				break;
		}

		dprintk(MANTIS_TMG, 0, "I2CRACK: trials=%d\n", trials);

		rxd = mmread(MANTIS_I2CDATA_CTL);
		msg->buf[i] = (u8)((rxd >> 8) & 0xFF);
		dprintk(MANTIS_INFO, 0, "%02x ", msg->buf[i]);
	}
	dprintk(MANTIS_INFO, 0, "]\n");

	return 0;
}

static int mantis_i2c_write(struct mantis_pci *mantis, const struct i2c_msg *msg)
{
	int i;
	u32 txd = 0, stat, trials;

	dprintk(MANTIS_INFO, 0, "        %s: Address=[0x%02x] <W>[ ",
		__func__, msg->addr);

	for (i = 0; i < msg->len; i++) {
		dprintk(MANTIS_INFO, 0, "%02x ", msg->buf[i]);
		txd = (msg->addr << 25) | (msg->buf[i] << 8)
					| MANTIS_I2C_RATE_3
					| MANTIS_I2C_STOP
					| MANTIS_I2C_PGMODE;

		if (i == (msg->len - 1))
			txd &= ~MANTIS_I2C_STOP;

		mmwrite(MANTIS_INT_I2CDONE, MANTIS_INT_STAT);
		mmwrite(txd, MANTIS_I2CDATA_CTL);

		/* wait for xfer completion */
		for (trials = 0; trials < TRIALS; trials++) {
			stat = mmread(MANTIS_INT_STAT);
			if (stat & MANTIS_INT_I2CDONE)
				break;
		}

		dprintk(MANTIS_TMG, 0, "I2CDONE: trials=%d\n", trials);

		/* wait for xfer completion */
		for (trials = 0; trials < TRIALS; trials++) {
			stat = mmread(MANTIS_INT_STAT);
			if (stat & MANTIS_INT_I2CRACK)
				break;
		}

		dprintk(MANTIS_TMG, 0, "I2CRACK: trials=%d\n", trials);
	}
	dprintk(MANTIS_INFO, 0, "]\n");

	return 0;
}

static int mantis_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	int ret = 0, i = 0, trials;
	u32 stat, data, txd;
	struct mantis_pci *mantis;
	struct mantis_hwconfig *config;

	mantis = i2c_get_adapdata(adapter);
	BUG_ON(!mantis);
	config = mantis->hwconfig;
	BUG_ON(!config);

	dprintk(MANTIS_DEBUG, 1, "Messages:%d", num);
	mutex_lock(&mantis->i2c_lock);

	while (i < num) {
		/* Byte MODE */
		if ((config->i2c_mode & MANTIS_BYTE_MODE) &&
		    ((i + 1) < num)			&&
		    (msgs[i].len < 2)			&&
		    (msgs[i + 1].len < 2)		&&
		    (msgs[i + 1].flags & I2C_M_RD)) {

			dprintk(MANTIS_DEBUG, 0, "        Byte MODE:\n");

			/* Read operation */
			txd = msgs[i].addr << 25 | (0x1 << 24)
						 | (msgs[i].buf[0] << 16)
						 | MANTIS_I2C_RATE_3;

			mmwrite(txd, MANTIS_I2CDATA_CTL);
			/* wait for xfer completion */
			for (trials = 0; trials < TRIALS; trials++) {
				stat = mmread(MANTIS_INT_STAT);
				if (stat & MANTIS_INT_I2CDONE)
					break;
			}

			/* check for xfer completion */
			if (stat & MANTIS_INT_I2CDONE) {
				/* check xfer was acknowledged */
				if (stat & MANTIS_INT_I2CRACK) {
					data = mmread(MANTIS_I2CDATA_CTL);
					msgs[i + 1].buf[0] = (data >> 8) & 0xff;
					dprintk(MANTIS_DEBUG, 0, "        Byte <%d> RXD=0x%02x  [%02x]\n", 0x0, data, msgs[i + 1].buf[0]);
				} else {
					/* I/O error */
					dprintk(MANTIS_ERROR, 1, "        I/O error, LINE:%d", __LINE__);
					ret = -EIO;
					break;
				}
			} else {
				/* I/O error */
				dprintk(MANTIS_ERROR, 1, "        I/O error, LINE:%d", __LINE__);
				ret = -EIO;
				break;
			}
			i += 2; /* Write/Read operation in one go */
		}

		if (i < num) {
			if (msgs[i].flags & I2C_M_RD)
				ret = mantis_i2c_read(mantis, &msgs[i]);
			else
				ret = mantis_i2c_write(mantis, &msgs[i]);

			i++;
			if (ret < 0)
				goto bail_out;
		}

	}

	mutex_unlock(&mantis->i2c_lock);

	return num;

bail_out:
	mutex_unlock(&mantis->i2c_lock);
	return ret;
}

static u32 mantis_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm mantis_algo = {
	.master_xfer		= mantis_i2c_xfer,
	.functionality		= mantis_i2c_func,
};

int mantis_i2c_init(struct mantis_pci *mantis)
{
	u32 intstat;
	struct i2c_adapter *i2c_adapter = &mantis->adapter;
	struct pci_dev *pdev		= mantis->pdev;

	init_waitqueue_head(&mantis->i2c_wq);
	mutex_init(&mantis->i2c_lock);
	strncpy(i2c_adapter->name, "Mantis I2C", sizeof(i2c_adapter->name));
	i2c_set_adapdata(i2c_adapter, mantis);

	i2c_adapter->owner	= THIS_MODULE;
	i2c_adapter->algo	= &mantis_algo;
	i2c_adapter->algo_data	= NULL;
	i2c_adapter->timeout	= 500;
	i2c_adapter->retries	= 3;
	i2c_adapter->dev.parent	= &pdev->dev;

	mantis->i2c_rc		= i2c_add_adapter(i2c_adapter);
	if (mantis->i2c_rc < 0)
		return mantis->i2c_rc;

	dprintk(MANTIS_DEBUG, 1, "Initializing I2C ..");

	intstat = mmread(MANTIS_INT_STAT);
	mmread(MANTIS_INT_MASK);
	mmwrite(intstat, MANTIS_INT_STAT);
	dprintk(MANTIS_DEBUG, 1, "Disabling I2C interrupt");
	mantis_mask_ints(mantis, MANTIS_INT_I2CDONE);

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_i2c_init);

int mantis_i2c_exit(struct mantis_pci *mantis)
{
	dprintk(MANTIS_DEBUG, 1, "Disabling I2C interrupt");
	mantis_mask_ints(mantis, MANTIS_INT_I2CDONE);

	dprintk(MANTIS_DEBUG, 1, "Removing I2C adapter");
	i2c_del_adapter(&mantis->adapter);

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_i2c_exit);
