/*
 * Copyright 1998-2009 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/via-core.h>
#include <linux/via_i2c.h>

/*
 * There can only be one set of these, so there's no point in having
 * them be dynamically allocated...
 */
#define VIAFB_NUM_I2C		5
static struct via_i2c_stuff via_i2c_par[VIAFB_NUM_I2C];
static struct viafb_dev *i2c_vdev;  /* Passed in from core */

static void via_i2c_setscl(void *data, int state)
{
	u8 val;
	struct via_port_cfg *adap_data = data;
	unsigned long flags;

	spin_lock_irqsave(&i2c_vdev->reg_lock, flags);
	val = via_read_reg(adap_data->io_port, adap_data->ioport_index) & 0xF0;
	if (state)
		val |= 0x20;
	else
		val &= ~0x20;
	switch (adap_data->type) {
	case VIA_PORT_I2C:
		val |= 0x01;
		break;
	case VIA_PORT_GPIO:
		val |= 0x82;
		break;
	default:
		printk(KERN_ERR "viafb_i2c: specify wrong i2c type.\n");
	}
	via_write_reg(adap_data->io_port, adap_data->ioport_index, val);
	spin_unlock_irqrestore(&i2c_vdev->reg_lock, flags);
}

static int via_i2c_getscl(void *data)
{
	struct via_port_cfg *adap_data = data;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&i2c_vdev->reg_lock, flags);
	if (adap_data->type == VIA_PORT_GPIO)
		via_write_reg_mask(adap_data->io_port, adap_data->ioport_index,
			0, 0x80);
	if (via_read_reg(adap_data->io_port, adap_data->ioport_index) & 0x08)
		ret = 1;
	spin_unlock_irqrestore(&i2c_vdev->reg_lock, flags);
	return ret;
}

static int via_i2c_getsda(void *data)
{
	struct via_port_cfg *adap_data = data;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&i2c_vdev->reg_lock, flags);
	if (adap_data->type == VIA_PORT_GPIO)
		via_write_reg_mask(adap_data->io_port, adap_data->ioport_index,
			0, 0x40);
	if (via_read_reg(adap_data->io_port, adap_data->ioport_index) & 0x04)
		ret = 1;
	spin_unlock_irqrestore(&i2c_vdev->reg_lock, flags);
	return ret;
}

static void via_i2c_setsda(void *data, int state)
{
	u8 val;
	struct via_port_cfg *adap_data = data;
	unsigned long flags;

	spin_lock_irqsave(&i2c_vdev->reg_lock, flags);
	val = via_read_reg(adap_data->io_port, adap_data->ioport_index) & 0xF0;
	if (state)
		val |= 0x10;
	else
		val &= ~0x10;
	switch (adap_data->type) {
	case VIA_PORT_I2C:
		val |= 0x01;
		break;
	case VIA_PORT_GPIO:
		val |= 0x42;
		break;
	default:
		printk(KERN_ERR "viafb_i2c: specify wrong i2c type.\n");
	}
	via_write_reg(adap_data->io_port, adap_data->ioport_index, val);
	spin_unlock_irqrestore(&i2c_vdev->reg_lock, flags);
}

int viafb_i2c_readbyte(u8 adap, u8 slave_addr, u8 index, u8 *pdata)
{
	int ret;
	u8 mm1[] = {0x00};
	struct i2c_msg msgs[2];

	if (!via_i2c_par[adap].is_active)
		return -ENODEV;
	*pdata = 0;
	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = slave_addr / 2;
	mm1[0] = index;
	msgs[0].len = 1; msgs[1].len = 1;
	msgs[0].buf = mm1; msgs[1].buf = pdata;
	ret = i2c_transfer(&via_i2c_par[adap].adapter, msgs, 2);
	if (ret == 2)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	return ret;
}

int viafb_i2c_writebyte(u8 adap, u8 slave_addr, u8 index, u8 data)
{
	int ret;
	u8 msg[2] = { index, data };
	struct i2c_msg msgs;

	if (!via_i2c_par[adap].is_active)
		return -ENODEV;
	msgs.flags = 0;
	msgs.addr = slave_addr / 2;
	msgs.len = 2;
	msgs.buf = msg;
	ret = i2c_transfer(&via_i2c_par[adap].adapter, &msgs, 1);
	if (ret == 1)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	return ret;
}

int viafb_i2c_readbytes(u8 adap, u8 slave_addr, u8 index, u8 *buff, int buff_len)
{
	int ret;
	u8 mm1[] = {0x00};
	struct i2c_msg msgs[2];

	if (!via_i2c_par[adap].is_active)
		return -ENODEV;
	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = slave_addr / 2;
	mm1[0] = index;
	msgs[0].len = 1; msgs[1].len = buff_len;
	msgs[0].buf = mm1; msgs[1].buf = buff;
	ret = i2c_transfer(&via_i2c_par[adap].adapter, msgs, 2);
	if (ret == 2)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	return ret;
}

/*
 * Allow other viafb subdevices to look up a specific adapter
 * by port name.
 */
struct i2c_adapter *viafb_find_i2c_adapter(enum viafb_i2c_adap which)
{
	struct via_i2c_stuff *stuff = &via_i2c_par[which];

	return &stuff->adapter;
}
EXPORT_SYMBOL_GPL(viafb_find_i2c_adapter);


static int create_i2c_bus(struct i2c_adapter *adapter,
			  struct i2c_algo_bit_data *algo,
			  struct via_port_cfg *adap_cfg,
			  struct pci_dev *pdev)
{
	algo->setsda = via_i2c_setsda;
	algo->setscl = via_i2c_setscl;
	algo->getsda = via_i2c_getsda;
	algo->getscl = via_i2c_getscl;
	algo->udelay = 10;
	algo->timeout = 2;
	algo->data = adap_cfg;

	sprintf(adapter->name, "viafb i2c io_port idx 0x%02x",
		adap_cfg->ioport_index);
	adapter->owner = THIS_MODULE;
	adapter->class = I2C_CLASS_DDC;
	adapter->algo_data = algo;
	if (pdev)
		adapter->dev.parent = &pdev->dev;
	else
		adapter->dev.parent = NULL;
	/* i2c_set_adapdata(adapter, adap_cfg); */

	/* Raise SCL and SDA */
	via_i2c_setsda(adap_cfg, 1);
	via_i2c_setscl(adap_cfg, 1);
	udelay(20);

	return i2c_bit_add_bus(adapter);
}

static int viafb_i2c_probe(struct platform_device *platdev)
{
	int i, ret;
	struct via_port_cfg *configs;

	i2c_vdev = platdev->dev.platform_data;
	configs = i2c_vdev->port_cfg;

	for (i = 0; i < VIAFB_NUM_PORTS; i++) {
		struct via_port_cfg *adap_cfg = configs++;
		struct via_i2c_stuff *i2c_stuff = &via_i2c_par[i];

		i2c_stuff->is_active = 0;
		if (adap_cfg->type == 0 || adap_cfg->mode != VIA_MODE_I2C)
			continue;
		ret = create_i2c_bus(&i2c_stuff->adapter,
				     &i2c_stuff->algo, adap_cfg,
				NULL); /* FIXME: PCIDEV */
		if (ret < 0) {
			printk(KERN_ERR "viafb: cannot create i2c bus %u:%d\n",
				i, ret);
			continue;  /* Still try to make the rest */
		}
		i2c_stuff->is_active = 1;
	}

	return 0;
}

static int viafb_i2c_remove(struct platform_device *platdev)
{
	int i;

	for (i = 0; i < VIAFB_NUM_PORTS; i++) {
		struct via_i2c_stuff *i2c_stuff = &via_i2c_par[i];
		/*
		 * Only remove those entries in the array that we've
		 * actually used (and thus initialized algo_data)
		 */
		if (i2c_stuff->is_active)
			i2c_del_adapter(&i2c_stuff->adapter);
	}
	return 0;
}

static struct platform_driver via_i2c_driver = {
	.driver = {
		.name = "viafb-i2c",
	},
	.probe = viafb_i2c_probe,
	.remove = viafb_i2c_remove,
};

int viafb_i2c_init(void)
{
	return platform_driver_register(&via_i2c_driver);
}

void viafb_i2c_exit(void)
{
	platform_driver_unregister(&via_i2c_driver);
}
