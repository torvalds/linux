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

#include "global.h"

static void via_i2c_setscl(void *data, int state)
{
	u8 val;
	struct via_i2c_adap_cfg *adap_data = data;

	printk(KERN_DEBUG "reading index 0x%02x from IO 0x%x\n",
		adap_data->ioport_index, adap_data->io_port);
	val = viafb_read_reg(adap_data->io_port,
			     adap_data->ioport_index) & 0xF0;
	if (state)
		val |= 0x20;
	else
		val &= ~0x20;
	switch (adap_data->type) {
	case VIA_I2C_I2C:
		val |= 0x01;
		break;
	case VIA_I2C_GPIO:
		val |= 0x80;
		break;
	default:
		DEBUG_MSG("viafb_i2c: specify wrong i2c type.\n");
	}
	viafb_write_reg(adap_data->ioport_index,
			adap_data->io_port, val);
}

static int via_i2c_getscl(void *data)
{
	struct via_i2c_adap_cfg *adap_data = data;

	if (viafb_read_reg(adap_data->io_port, adap_data->ioport_index) & 0x08)
		return 1;
	return 0;
}

static int via_i2c_getsda(void *data)
{
	struct via_i2c_adap_cfg *adap_data = data;

	if (viafb_read_reg(adap_data->io_port, adap_data->ioport_index) & 0x04)
		return 1;
	return 0;
}

static void via_i2c_setsda(void *data, int state)
{
	u8 val;
	struct via_i2c_adap_cfg *adap_data = data;

	val = viafb_read_reg(adap_data->io_port,
			     adap_data->ioport_index) & 0xF0;
	if (state)
		val |= 0x10;
	else
		val &= ~0x10;
	switch (adap_data->type) {
	case VIA_I2C_I2C:
		val |= 0x01;
		break;
	case VIA_I2C_GPIO:
		val |= 0x40;
		break;
	default:
		DEBUG_MSG("viafb_i2c: specify wrong i2c type.\n");
	}
	viafb_write_reg(adap_data->ioport_index,
			adap_data->io_port, val);
}

int viafb_i2c_readbyte(u8 adap, u8 slave_addr, u8 index, u8 *pdata)
{
	u8 mm1[] = {0x00};
	struct i2c_msg msgs[2];

	*pdata = 0;
	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = slave_addr / 2;
	mm1[0] = index;
	msgs[0].len = 1; msgs[1].len = 1;
	msgs[0].buf = mm1; msgs[1].buf = pdata;
	return i2c_transfer(&viaparinfo->shared->i2c_stuff[adap].adapter,
			msgs, 2);
}

int viafb_i2c_writebyte(u8 adap, u8 slave_addr, u8 index, u8 data)
{
	u8 msg[2] = { index, data };
	struct i2c_msg msgs;

	msgs.flags = 0;
	msgs.addr = slave_addr / 2;
	msgs.len = 2;
	msgs.buf = msg;
	return i2c_transfer(&viaparinfo->shared->i2c_stuff[adap].adapter,
			&msgs, 1);
}

int viafb_i2c_readbytes(u8 adap, u8 slave_addr, u8 index, u8 *buff, int buff_len)
{
	u8 mm1[] = {0x00};
	struct i2c_msg msgs[2];

	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = slave_addr / 2;
	mm1[0] = index;
	msgs[0].len = 1; msgs[1].len = buff_len;
	msgs[0].buf = mm1; msgs[1].buf = buff;
	return i2c_transfer(&viaparinfo->shared->i2c_stuff[adap].adapter,
			msgs, 2);
}

static int create_i2c_bus(struct i2c_adapter *adapter,
			  struct i2c_algo_bit_data *algo,
			  struct via_i2c_adap_cfg *adap_cfg,
			  struct pci_dev *pdev)
{
	printk(KERN_DEBUG "viafb: creating bus adap=0x%p, algo_bit_data=0x%p, adap_cfg=0x%p\n", adapter, algo, adap_cfg);

	algo->setsda = via_i2c_setsda;
	algo->setscl = via_i2c_setscl;
	algo->getsda = via_i2c_getsda;
	algo->getscl = via_i2c_getscl;
	algo->udelay = 40;
	algo->timeout = 20;
	algo->data = adap_cfg;

	sprintf(adapter->name, "viafb i2c io_port idx 0x%02x",
		adap_cfg->ioport_index);
	adapter->owner = THIS_MODULE;
	adapter->id = 0x01FFFF;
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

static struct via_i2c_adap_cfg adap_configs[] = {
	[VIA_I2C_ADAP_26]	= { VIA_I2C_I2C,  VIASR, 0x26 },
	[VIA_I2C_ADAP_31]	= { VIA_I2C_I2C,  VIASR, 0x31 },
	[VIA_I2C_ADAP_25]	= { VIA_I2C_GPIO, VIASR, 0x25 },
	[VIA_I2C_ADAP_2C]	= { VIA_I2C_GPIO, VIASR, 0x2c },
	[VIA_I2C_ADAP_3D]	= { VIA_I2C_GPIO, VIASR, 0x3d },
	{ 0, 0, 0 }
};

int viafb_create_i2c_busses(struct viafb_par *viapar)
{
	int i, ret;

	for (i = 0; i < VIAFB_NUM_I2C; i++) {
		struct via_i2c_adap_cfg *adap_cfg = &adap_configs[i];
		struct via_i2c_stuff *i2c_stuff = &viapar->shared->i2c_stuff[i];

		if (adap_cfg->type == 0)
			break;

		ret = create_i2c_bus(&i2c_stuff->adapter,
				     &i2c_stuff->algo, adap_cfg,
				NULL); /* FIXME: PCIDEV */
		if (ret < 0) {
			printk(KERN_ERR "viafb: cannot create i2c bus %u:%d\n",
				i, ret);
			/* FIXME: properly release previous busses */
			return ret;
		}
	}

	return 0;
}

void viafb_delete_i2c_busses(struct viafb_par *par)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(par->shared->i2c_stuff); i++) {
		struct via_i2c_stuff *i2c_stuff = &par->shared->i2c_stuff[i];
		/* only remove those entries in the array that we've
		 * actually used (and thus initialized algo_data) */
		if (i2c_stuff->adapter.algo_data == &i2c_stuff->algo)
			i2c_del_adapter(&i2c_stuff->adapter);
	}
}
