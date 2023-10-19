// SPDX-License-Identifier: GPL-2.0
/*
 * ddbridge-i2c.c: Digital Devices bridge i2c driver
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/swab.h>
#include <linux/vmalloc.h>

#include "ddbridge.h"
#include "ddbridge-i2c.h"
#include "ddbridge-regs.h"
#include "ddbridge-io.h"

/******************************************************************************/

static int ddb_i2c_cmd(struct ddb_i2c *i2c, u32 adr, u32 cmd)
{
	struct ddb *dev = i2c->dev;
	unsigned long stat;
	u32 val;

	ddbwritel(dev, (adr << 9) | cmd, i2c->regs + I2C_COMMAND);
	stat = wait_for_completion_timeout(&i2c->completion, HZ);
	val = ddbreadl(dev, i2c->regs + I2C_COMMAND);
	if (stat == 0) {
		dev_err(dev->dev, "I2C timeout, card %d, port %d, link %u\n",
			dev->nr, i2c->nr, i2c->link);
		{
			u32 istat = ddbreadl(dev, INTERRUPT_STATUS);

			dev_err(dev->dev, "DDBridge IRS %08x\n", istat);
			if (i2c->link) {
				u32 listat = ddbreadl(dev,
					DDB_LINK_TAG(i2c->link) |
					INTERRUPT_STATUS);

				dev_err(dev->dev, "DDBridge link %u IRS %08x\n",
					i2c->link, listat);
			}
			if (istat & 1) {
				ddbwritel(dev, istat & 1, INTERRUPT_ACK);
			} else {
				u32 mon = ddbreadl(dev,
					i2c->regs + I2C_MONITOR);

				dev_err(dev->dev, "I2C cmd=%08x mon=%08x\n",
					val, mon);
			}
		}
		return -EIO;
	}
	val &= 0x70000;
	if (val == 0x20000)
		dev_err(dev->dev, "I2C bus error\n");
	if (val)
		return -EIO;
	return 0;
}

static int ddb_i2c_master_xfer(struct i2c_adapter *adapter,
			       struct i2c_msg msg[], int num)
{
	struct ddb_i2c *i2c = (struct ddb_i2c *)i2c_get_adapdata(adapter);
	struct ddb *dev = i2c->dev;
	u8 addr = 0;

	addr = msg[0].addr;
	if (msg[0].len > i2c->bsize)
		return -EIO;
	switch (num) {
	case 1:
		if (msg[0].flags & I2C_M_RD) {
			ddbwritel(dev, msg[0].len << 16,
				  i2c->regs + I2C_TASKLENGTH);
			if (ddb_i2c_cmd(i2c, addr, 3))
				break;
			ddbcpyfrom(dev, msg[0].buf,
				   i2c->rbuf, msg[0].len);
			return num;
		}
		ddbcpyto(dev, i2c->wbuf, msg[0].buf, msg[0].len);
		ddbwritel(dev, msg[0].len, i2c->regs + I2C_TASKLENGTH);
		if (ddb_i2c_cmd(i2c, addr, 2))
			break;
		return num;
	case 2:
		if ((msg[0].flags & I2C_M_RD) == I2C_M_RD)
			break;
		if ((msg[1].flags & I2C_M_RD) != I2C_M_RD)
			break;
		if (msg[1].len > i2c->bsize)
			break;
		ddbcpyto(dev, i2c->wbuf, msg[0].buf, msg[0].len);
		ddbwritel(dev, msg[0].len | (msg[1].len << 16),
			  i2c->regs + I2C_TASKLENGTH);
		if (ddb_i2c_cmd(i2c, addr, 1))
			break;
		ddbcpyfrom(dev, msg[1].buf,
			   i2c->rbuf,
			   msg[1].len);
		return num;
	default:
		break;
	}
	return -EIO;
}

static u32 ddb_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm ddb_i2c_algo = {
	.master_xfer   = ddb_i2c_master_xfer,
	.functionality = ddb_i2c_functionality,
};

void ddb_i2c_release(struct ddb *dev)
{
	int i;
	struct ddb_i2c *i2c;

	for (i = 0; i < dev->i2c_num; i++) {
		i2c = &dev->i2c[i];
		i2c_del_adapter(&i2c->adap);
	}
}

static void i2c_handler(void *priv)
{
	struct ddb_i2c *i2c = (struct ddb_i2c *)priv;

	complete(&i2c->completion);
}

static int ddb_i2c_add(struct ddb *dev, struct ddb_i2c *i2c,
		       const struct ddb_regmap *regmap, int link,
		       int i, int num)
{
	struct i2c_adapter *adap;

	i2c->nr = i;
	i2c->dev = dev;
	i2c->link = link;
	i2c->bsize = regmap->i2c_buf->size;
	i2c->wbuf = DDB_LINK_TAG(link) |
		(regmap->i2c_buf->base + i2c->bsize * i);
	i2c->rbuf = i2c->wbuf; /* + i2c->bsize / 2 */
	i2c->regs = DDB_LINK_TAG(link) |
		(regmap->i2c->base + regmap->i2c->size * i);
	ddbwritel(dev, I2C_SPEED_100, i2c->regs + I2C_TIMING);
	ddbwritel(dev, ((i2c->rbuf & 0xffff) << 16) | (i2c->wbuf & 0xffff),
		  i2c->regs + I2C_TASKADDRESS);
	init_completion(&i2c->completion);

	adap = &i2c->adap;
	i2c_set_adapdata(adap, i2c);
#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	adap->class = I2C_ADAP_CLASS_TV_DIGITAL | I2C_CLASS_TV_ANALOG;
#else
#ifdef I2C_CLASS_TV_ANALOG
	adap->class = I2C_CLASS_TV_ANALOG;
#endif
#endif
	snprintf(adap->name, I2C_NAME_SIZE, "ddbridge_%02x.%x.%x",
		 dev->nr, i2c->link, i);
	adap->algo = &ddb_i2c_algo;
	adap->algo_data = (void *)i2c;
	adap->dev.parent = dev->dev;
	return i2c_add_adapter(adap);
}

int ddb_i2c_init(struct ddb *dev)
{
	int stat = 0;
	u32 i, j, num = 0, l, base;
	struct ddb_i2c *i2c;
	struct i2c_adapter *adap;
	const struct ddb_regmap *regmap;

	for (l = 0; l < DDB_MAX_LINK; l++) {
		if (!dev->link[l].info)
			continue;
		regmap = dev->link[l].info->regmap;
		if (!regmap || !regmap->i2c)
			continue;
		base = regmap->irq_base_i2c;
		for (i = 0; i < regmap->i2c->num; i++) {
			if (!(dev->link[l].info->i2c_mask & (1 << i)))
				continue;
			i2c = &dev->i2c[num];
			ddb_irq_set(dev, l, i + base, i2c_handler, i2c);
			stat = ddb_i2c_add(dev, i2c, regmap, l, i, num);
			if (stat)
				break;
			num++;
		}
	}
	if (stat) {
		for (j = 0; j < num; j++) {
			i2c = &dev->i2c[j];
			adap = &i2c->adap;
			i2c_del_adapter(adap);
		}
	} else {
		dev->i2c_num = num;
	}

	return stat;
}
