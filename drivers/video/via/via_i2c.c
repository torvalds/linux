/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
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
	struct via_i2c_stuff *via_i2c_chan = (struct via_i2c_stuff *)data;

	val = viafb_read_reg(VIASR, via_i2c_chan->i2c_port) & 0xF0;
	if (state)
		val |= 0x20;
	else
		val &= ~0x20;
	switch (via_i2c_chan->i2c_port) {
	case I2CPORTINDEX:
		val |= 0x01;
		break;
	case GPIOPORTINDEX:
		val |= 0x80;
		break;
	default:
		DEBUG_MSG("via_i2c: specify wrong i2c port.\n");
	}
	viafb_write_reg(via_i2c_chan->i2c_port, VIASR, val);
}

static int via_i2c_getscl(void *data)
{
	struct via_i2c_stuff *via_i2c_chan = (struct via_i2c_stuff *)data;

	if (viafb_read_reg(VIASR, via_i2c_chan->i2c_port) & 0x08)
		return 1;
	return 0;
}

static int via_i2c_getsda(void *data)
{
	struct via_i2c_stuff *via_i2c_chan = (struct via_i2c_stuff *)data;

	if (viafb_read_reg(VIASR, via_i2c_chan->i2c_port) & 0x04)
		return 1;
	return 0;
}

static void via_i2c_setsda(void *data, int state)
{
	u8 val;
	struct via_i2c_stuff *via_i2c_chan = (struct via_i2c_stuff *)data;

	val = viafb_read_reg(VIASR, via_i2c_chan->i2c_port) & 0xF0;
	if (state)
		val |= 0x10;
	else
		val &= ~0x10;
	switch (via_i2c_chan->i2c_port) {
	case I2CPORTINDEX:
		val |= 0x01;
		break;
	case GPIOPORTINDEX:
		val |= 0x40;
		break;
	default:
		DEBUG_MSG("via_i2c: specify wrong i2c port.\n");
	}
	viafb_write_reg(via_i2c_chan->i2c_port, VIASR, val);
}

int viafb_i2c_readbyte(u8 slave_addr, u8 index, u8 *pdata)
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
	i2c_transfer(&viaparinfo->shared->i2c_stuff.adapter, msgs, 2);

	return 0;
}

int viafb_i2c_writebyte(u8 slave_addr, u8 index, u8 data)
{
	u8 msg[2] = { index, data };
	struct i2c_msg msgs;

	msgs.flags = 0;
	msgs.addr = slave_addr / 2;
	msgs.len = 2;
	msgs.buf = msg;
	return i2c_transfer(&viaparinfo->shared->i2c_stuff.adapter, &msgs, 1);
}

int viafb_i2c_readbytes(u8 slave_addr, u8 index, u8 *buff, int buff_len)
{
	u8 mm1[] = {0x00};
	struct i2c_msg msgs[2];

	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = slave_addr / 2;
	mm1[0] = index;
	msgs[0].len = 1; msgs[1].len = buff_len;
	msgs[0].buf = mm1; msgs[1].buf = buff;
	i2c_transfer(&viaparinfo->shared->i2c_stuff.adapter, msgs, 2);
	return 0;
}

int viafb_create_i2c_bus(void *viapar)
{
	int ret;
	struct via_i2c_stuff *i2c_stuff =
		&((struct viafb_par *)viapar)->shared->i2c_stuff;

	strcpy(i2c_stuff->adapter.name, "via_i2c");
	i2c_stuff->i2c_port = 0x0;
	i2c_stuff->adapter.owner = THIS_MODULE;
	i2c_stuff->adapter.id = 0x01FFFF;
	i2c_stuff->adapter.class = 0;
	i2c_stuff->adapter.algo_data = &i2c_stuff->algo;
	i2c_stuff->adapter.dev.parent = NULL;
	i2c_stuff->algo.setsda = via_i2c_setsda;
	i2c_stuff->algo.setscl = via_i2c_setscl;
	i2c_stuff->algo.getsda = via_i2c_getsda;
	i2c_stuff->algo.getscl = via_i2c_getscl;
	i2c_stuff->algo.udelay = 40;
	i2c_stuff->algo.timeout = 20;
	i2c_stuff->algo.data = i2c_stuff;

	i2c_set_adapdata(&i2c_stuff->adapter, i2c_stuff);

	/* Raise SCL and SDA */
	i2c_stuff->i2c_port = I2CPORTINDEX;
	via_i2c_setsda(i2c_stuff, 1);
	via_i2c_setscl(i2c_stuff, 1);

	i2c_stuff->i2c_port = GPIOPORTINDEX;
	via_i2c_setsda(i2c_stuff, 1);
	via_i2c_setscl(i2c_stuff, 1);
	udelay(20);

	ret = i2c_bit_add_bus(&i2c_stuff->adapter);
	if (ret == 0)
		DEBUG_MSG("I2C bus %s registered.\n", i2c_stuff->adapter.name);
	else
		DEBUG_MSG("Failed to register I2C bus %s.\n",
			i2c_stuff->adapter.name);
	return ret;
}

void viafb_delete_i2c_buss(void *par)
{
	i2c_del_adapter(&((struct viafb_par *)par)->shared->i2c_stuff.adapter);
}
