/*
 * I2C multiplexer driver for PCA9541 bus master selector
 *
 * Copyright (c) 2010 Ericsson AB.
 *
 * Author: Guenter Roeck <linux@roeck-us.net>
 *
 * Derived from:
 *  pca954x.c
 *
 *  Copyright (c) 2008-2009 Rodolfo Giometti <giometti@linux.it>
 *  Copyright (c) 2008-2009 Eurotech S.p.A. <info@eurotech.it>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>

#include <linux/i2c/pca954x.h>

/*
 * The PCA9541 is a bus master selector. It supports two I2C masters connected
 * to a single slave bus.
 *
 * Before each bus transaction, a master has to acquire bus ownership. After the
 * transaction is complete, bus ownership has to be released. This fits well
 * into the I2C multiplexer framework, which provides select and release
 * functions for this purpose. For this reason, this driver is modeled as
 * single-channel I2C bus multiplexer.
 *
 * This driver assumes that the two bus masters are controlled by two different
 * hosts. If a single host controls both masters, platform code has to ensure
 * that only one of the masters is instantiated at any given time.
 */

#define PCA9541_CONTROL		0x01
#define PCA9541_ISTAT		0x02

#define PCA9541_CTL_MYBUS	(1 << 0)
#define PCA9541_CTL_NMYBUS	(1 << 1)
#define PCA9541_CTL_BUSON	(1 << 2)
#define PCA9541_CTL_NBUSON	(1 << 3)
#define PCA9541_CTL_BUSINIT	(1 << 4)
#define PCA9541_CTL_TESTON	(1 << 6)
#define PCA9541_CTL_NTESTON	(1 << 7)

#define PCA9541_ISTAT_INTIN	(1 << 0)
#define PCA9541_ISTAT_BUSINIT	(1 << 1)
#define PCA9541_ISTAT_BUSOK	(1 << 2)
#define PCA9541_ISTAT_BUSLOST	(1 << 3)
#define PCA9541_ISTAT_MYTEST	(1 << 6)
#define PCA9541_ISTAT_NMYTEST	(1 << 7)

#define BUSON		(PCA9541_CTL_BUSON | PCA9541_CTL_NBUSON)
#define MYBUS		(PCA9541_CTL_MYBUS | PCA9541_CTL_NMYBUS)
#define mybus(x)	(!((x) & MYBUS) || ((x) & MYBUS) == MYBUS)
#define busoff(x)	(!((x) & BUSON) || ((x) & BUSON) == BUSON)

/* arbitration timeouts, in jiffies */
#define ARB_TIMEOUT	(HZ / 8)	/* 125 ms until forcing bus ownership */
#define ARB2_TIMEOUT	(HZ / 4)	/* 250 ms until acquisition failure */

/* arbitration retry delays, in us */
#define SELECT_DELAY_SHORT	50
#define SELECT_DELAY_LONG	1000

struct pca9541 {
	struct i2c_adapter *mux_adap;
	unsigned long select_timeout;
	unsigned long arb_timeout;
};

static const struct i2c_device_id pca9541_id[] = {
	{"pca9541", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, pca9541_id);

/*
 * Write to chip register. Don't use i2c_transfer()/i2c_smbus_xfer()
 * as they will try to lock the adapter a second time.
 */
static int pca9541_reg_write(struct i2c_client *client, u8 command, u8 val)
{
	struct i2c_adapter *adap = client->adapter;
	int ret;

	if (adap->algo->master_xfer) {
		struct i2c_msg msg;
		char buf[2];

		msg.addr = client->addr;
		msg.flags = 0;
		msg.len = 2;
		buf[0] = command;
		buf[1] = val;
		msg.buf = buf;
		ret = adap->algo->master_xfer(adap, &msg, 1);
	} else {
		union i2c_smbus_data data;

		data.byte = val;
		ret = adap->algo->smbus_xfer(adap, client->addr,
					     client->flags,
					     I2C_SMBUS_WRITE,
					     command,
					     I2C_SMBUS_BYTE_DATA, &data);
	}

	return ret;
}

/*
 * Read from chip register. Don't use i2c_transfer()/i2c_smbus_xfer()
 * as they will try to lock adapter a second time.
 */
static int pca9541_reg_read(struct i2c_client *client, u8 command)
{
	struct i2c_adapter *adap = client->adapter;
	int ret;
	u8 val;

	if (adap->algo->master_xfer) {
		struct i2c_msg msg[2] = {
			{
				.addr = client->addr,
				.flags = 0,
				.len = 1,
				.buf = &command
			},
			{
				.addr = client->addr,
				.flags = I2C_M_RD,
				.len = 1,
				.buf = &val
			}
		};
		ret = adap->algo->master_xfer(adap, msg, 2);
		if (ret == 2)
			ret = val;
		else if (ret >= 0)
			ret = -EIO;
	} else {
		union i2c_smbus_data data;

		ret = adap->algo->smbus_xfer(adap, client->addr,
					     client->flags,
					     I2C_SMBUS_READ,
					     command,
					     I2C_SMBUS_BYTE_DATA, &data);
		if (!ret)
			ret = data.byte;
	}
	return ret;
}

/*
 * Arbitration management functions
 */

/* Release bus. Also reset NTESTON and BUSINIT if it was set. */
static void pca9541_release_bus(struct i2c_client *client)
{
	int reg;

	reg = pca9541_reg_read(client, PCA9541_CONTROL);
	if (reg >= 0 && !busoff(reg) && mybus(reg))
		pca9541_reg_write(client, PCA9541_CONTROL,
				  (reg & PCA9541_CTL_NBUSON) >> 1);
}

/*
 * Arbitration is defined as a two-step process. A bus master can only activate
 * the slave bus if it owns it; otherwise it has to request ownership first.
 * This multi-step process ensures that access contention is resolved
 * gracefully.
 *
 * Bus	Ownership	Other master	Action
 * state		requested access
 * ----------------------------------------------------
 * off	-		yes		wait for arbitration timeout or
 *					for other master to drop request
 * off	no		no		take ownership
 * off	yes		no		turn on bus
 * on	yes		-		done
 * on	no		-		wait for arbitration timeout or
 *					for other master to release bus
 *
 * The main contention point occurs if the slave bus is off and both masters
 * request ownership at the same time. In this case, one master will turn on
 * the slave bus, believing that it owns it. The other master will request
 * bus ownership. Result is that the bus is turned on, and master which did
 * _not_ own the slave bus before ends up owning it.
 */

/* Control commands per PCA9541 datasheet */
static const u8 pca9541_control[16] = {
	4, 0, 1, 5, 4, 4, 5, 5, 0, 0, 1, 1, 0, 4, 5, 1
};

/*
 * Channel arbitration
 *
 * Return values:
 *  <0: error
 *  0 : bus not acquired
 *  1 : bus acquired
 */
static int pca9541_arbitrate(struct i2c_client *client)
{
	struct pca9541 *data = i2c_get_clientdata(client);
	int reg;

	reg = pca9541_reg_read(client, PCA9541_CONTROL);
	if (reg < 0)
		return reg;

	if (busoff(reg)) {
		int istat;
		/*
		 * Bus is off. Request ownership or turn it on unless
		 * other master requested ownership.
		 */
		istat = pca9541_reg_read(client, PCA9541_ISTAT);
		if (!(istat & PCA9541_ISTAT_NMYTEST)
		    || time_is_before_eq_jiffies(data->arb_timeout)) {
			/*
			 * Other master did not request ownership,
			 * or arbitration timeout expired. Take the bus.
			 */
			pca9541_reg_write(client,
					  PCA9541_CONTROL,
					  pca9541_control[reg & 0x0f]
					  | PCA9541_CTL_NTESTON);
			data->select_timeout = SELECT_DELAY_SHORT;
		} else {
			/*
			 * Other master requested ownership.
			 * Set extra long timeout to give it time to acquire it.
			 */
			data->select_timeout = SELECT_DELAY_LONG * 2;
		}
	} else if (mybus(reg)) {
		/*
		 * Bus is on, and we own it. We are done with acquisition.
		 * Reset NTESTON and BUSINIT, then return success.
		 */
		if (reg & (PCA9541_CTL_NTESTON | PCA9541_CTL_BUSINIT))
			pca9541_reg_write(client,
					  PCA9541_CONTROL,
					  reg & ~(PCA9541_CTL_NTESTON
						  | PCA9541_CTL_BUSINIT));
		return 1;
	} else {
		/*
		 * Other master owns the bus.
		 * If arbitration timeout has expired, force ownership.
		 * Otherwise request it.
		 */
		data->select_timeout = SELECT_DELAY_LONG;
		if (time_is_before_eq_jiffies(data->arb_timeout)) {
			/* Time is up, take the bus and reset it. */
			pca9541_reg_write(client,
					  PCA9541_CONTROL,
					  pca9541_control[reg & 0x0f]
					  | PCA9541_CTL_BUSINIT
					  | PCA9541_CTL_NTESTON);
		} else {
			/* Request bus ownership if needed */
			if (!(reg & PCA9541_CTL_NTESTON))
				pca9541_reg_write(client,
						  PCA9541_CONTROL,
						  reg | PCA9541_CTL_NTESTON);
		}
	}
	return 0;
}

static int pca9541_select_chan(struct i2c_adapter *adap, void *client, u32 chan)
{
	struct pca9541 *data = i2c_get_clientdata(client);
	int ret;
	unsigned long timeout = jiffies + ARB2_TIMEOUT;
		/* give up after this time */

	data->arb_timeout = jiffies + ARB_TIMEOUT;
		/* force bus ownership after this time */

	do {
		ret = pca9541_arbitrate(client);
		if (ret)
			return ret < 0 ? ret : 0;

		if (data->select_timeout == SELECT_DELAY_SHORT)
			udelay(data->select_timeout);
		else
			msleep(data->select_timeout / 1000);
	} while (time_is_after_eq_jiffies(timeout));

	return -ETIMEDOUT;
}

static int pca9541_release_chan(struct i2c_adapter *adap,
				void *client, u32 chan)
{
	pca9541_release_bus(client);
	return 0;
}

/*
 * I2C init/probing/exit functions
 */
static int pca9541_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adap = client->adapter;
	struct pca954x_platform_data *pdata = dev_get_platdata(&client->dev);
	struct pca9541 *data;
	int force;
	int ret = -ENODEV;

	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_BYTE_DATA))
		goto err;

	data = kzalloc(sizeof(struct pca9541), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	i2c_set_clientdata(client, data);

	/*
	 * I2C accesses are unprotected here.
	 * We have to lock the adapter before releasing the bus.
	 */
	i2c_lock_adapter(adap);
	pca9541_release_bus(client);
	i2c_unlock_adapter(adap);

	/* Create mux adapter */

	force = 0;
	if (pdata)
		force = pdata->modes[0].adap_id;
	data->mux_adap = i2c_add_mux_adapter(adap, &client->dev, client,
					     force, 0, 0,
					     pca9541_select_chan,
					     pca9541_release_chan);

	if (data->mux_adap == NULL) {
		dev_err(&client->dev, "failed to register master selector\n");
		goto exit_free;
	}

	dev_info(&client->dev, "registered master selector for I2C %s\n",
		 client->name);

	return 0;

exit_free:
	kfree(data);
err:
	return ret;
}

static int pca9541_remove(struct i2c_client *client)
{
	struct pca9541 *data = i2c_get_clientdata(client);

	i2c_del_mux_adapter(data->mux_adap);

	kfree(data);
	return 0;
}

static struct i2c_driver pca9541_driver = {
	.driver = {
		   .name = "pca9541",
		   .owner = THIS_MODULE,
		   },
	.probe = pca9541_probe,
	.remove = pca9541_remove,
	.id_table = pca9541_id,
};

module_i2c_driver(pca9541_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("PCA9541 I2C master selector driver");
MODULE_LICENSE("GPL v2");
