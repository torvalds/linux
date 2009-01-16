/*
 *  cx18 I2C functions
 *
 *  Derived from ivtv-i2c.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-cards.h"
#include "cx18-gpio.h"
#include "cx18-av-core.h"
#include "cx18-i2c.h"
#include "cx18-irq.h"

#define CX18_REG_I2C_1_WR   0xf15000
#define CX18_REG_I2C_1_RD   0xf15008
#define CX18_REG_I2C_2_WR   0xf25100
#define CX18_REG_I2C_2_RD   0xf25108

#define SETSCL_BIT      0x0001
#define SETSDL_BIT      0x0002
#define GETSCL_BIT      0x0004
#define GETSDL_BIT      0x0008

#define CX18_CS5345_I2C_ADDR		0x4c

/* This array should match the CX18_HW_ defines */
static const u8 hw_driverids[] = {
	I2C_DRIVERID_TUNER,
	I2C_DRIVERID_TVEEPROM,
	I2C_DRIVERID_CS5345,
	0, 		/* CX18_HW_GPIO dummy driver ID */
	0 		/* CX18_HW_CX23418 dummy driver ID */
};

/* This array should match the CX18_HW_ defines */
static const u8 hw_addrs[] = {
	0,
	0,
	CX18_CS5345_I2C_ADDR,
	0, 		/* CX18_HW_GPIO dummy driver ID */
	0,		/* CX18_HW_CX23418 dummy driver ID */
};

/* This array should match the CX18_HW_ defines */
/* This might well become a card-specific array */
static const u8 hw_bus[] = {
	0,
	0,
	0,
	0, 		/* CX18_HW_GPIO dummy driver ID */
	0,		/* CX18_HW_CX23418 dummy driver ID */
};

/* This array should match the CX18_HW_ defines */
static const char * const hw_devicenames[] = {
	"tuner",
	"tveeprom",
	"cs5345",
	"gpio",
	"cx23418",
};

int cx18_i2c_register(struct cx18 *cx, unsigned idx)
{
	struct i2c_board_info info;
	struct i2c_client *c;
	u8 id, bus;
	int i;

	CX18_DEBUG_I2C("i2c client register\n");
	if (idx >= ARRAY_SIZE(hw_driverids) || hw_driverids[idx] == 0)
		return -1;
	id = hw_driverids[idx];
	bus = hw_bus[idx];
	memset(&info, 0, sizeof(info));
	strlcpy(info.type, hw_devicenames[idx], sizeof(info.type));
	info.addr = hw_addrs[idx];
	for (i = 0; i < I2C_CLIENTS_MAX; i++)
		if (cx->i2c_clients[i] == NULL)
			break;

	if (i == I2C_CLIENTS_MAX) {
		CX18_ERR("insufficient room for new I2C client!\n");
		return -ENOMEM;
	}

	if (id != I2C_DRIVERID_TUNER) {
		c = i2c_new_device(&cx->i2c_adap[bus], &info);
		if (c->driver == NULL)
			i2c_unregister_device(c);
		else
			cx->i2c_clients[i] = c;
		return cx->i2c_clients[i] ? 0 : -ENODEV;
	}

	/* special tuner handling */
	c = i2c_new_probed_device(&cx->i2c_adap[1], &info, cx->card_i2c->radio);
	if (c && c->driver == NULL)
		i2c_unregister_device(c);
	else if (c)
		cx->i2c_clients[i++] = c;
	c = i2c_new_probed_device(&cx->i2c_adap[1], &info, cx->card_i2c->demod);
	if (c && c->driver == NULL)
		i2c_unregister_device(c);
	else if (c)
		cx->i2c_clients[i++] = c;
	c = i2c_new_probed_device(&cx->i2c_adap[1], &info, cx->card_i2c->tv);
	if (c && c->driver == NULL)
		i2c_unregister_device(c);
	else if (c)
		cx->i2c_clients[i++] = c;
	return 0;
}

static int attach_inform(struct i2c_client *client)
{
	return 0;
}

static int detach_inform(struct i2c_client *client)
{
	int i;
	struct cx18 *cx = (struct cx18 *)i2c_get_adapdata(client->adapter);

	CX18_DEBUG_I2C("i2c client detach\n");
	for (i = 0; i < I2C_CLIENTS_MAX; i++) {
		if (cx->i2c_clients[i] == client) {
			cx->i2c_clients[i] = NULL;
			break;
		}
	}
	CX18_DEBUG_I2C("i2c detach [client=%s,%s]\n",
		   client->name, (i < I2C_CLIENTS_MAX) ? "ok" : "failed");

	return 0;
}

static void cx18_setscl(void *data, int state)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	int bus_index = ((struct cx18_i2c_algo_callback_data *)data)->bus_index;
	u32 addr = bus_index ? CX18_REG_I2C_2_WR : CX18_REG_I2C_1_WR;
	u32 r = cx18_read_reg(cx, addr);

	if (state)
		cx18_write_reg(cx, r | SETSCL_BIT, addr);
	else
		cx18_write_reg(cx, r & ~SETSCL_BIT, addr);
}

static void cx18_setsda(void *data, int state)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	int bus_index = ((struct cx18_i2c_algo_callback_data *)data)->bus_index;
	u32 addr = bus_index ? CX18_REG_I2C_2_WR : CX18_REG_I2C_1_WR;
	u32 r = cx18_read_reg(cx, addr);

	if (state)
		cx18_write_reg(cx, r | SETSDL_BIT, addr);
	else
		cx18_write_reg(cx, r & ~SETSDL_BIT, addr);
}

static int cx18_getscl(void *data)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	int bus_index = ((struct cx18_i2c_algo_callback_data *)data)->bus_index;
	u32 addr = bus_index ? CX18_REG_I2C_2_RD : CX18_REG_I2C_1_RD;

	return cx18_read_reg(cx, addr) & GETSCL_BIT;
}

static int cx18_getsda(void *data)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	int bus_index = ((struct cx18_i2c_algo_callback_data *)data)->bus_index;
	u32 addr = bus_index ? CX18_REG_I2C_2_RD : CX18_REG_I2C_1_RD;

	return cx18_read_reg(cx, addr) & GETSDL_BIT;
}

/* template for i2c-bit-algo */
static struct i2c_adapter cx18_i2c_adap_template = {
	.name = "cx18 i2c driver",
	.id = I2C_HW_B_CX2341X,
	.algo = NULL,                   /* set by i2c-algo-bit */
	.algo_data = NULL,              /* filled from template */
	.client_register = attach_inform,
	.client_unregister = detach_inform,
	.owner = THIS_MODULE,
};

#define CX18_SCL_PERIOD (10) /* usecs. 10 usec is period for a 100 KHz clock */
#define CX18_ALGO_BIT_TIMEOUT (2) /* seconds */

static struct i2c_algo_bit_data cx18_i2c_algo_template = {
	.setsda		= cx18_setsda,
	.setscl		= cx18_setscl,
	.getsda		= cx18_getsda,
	.getscl		= cx18_getscl,
	.udelay		= CX18_SCL_PERIOD/2,       /* 1/2 clock period in usec*/
	.timeout	= CX18_ALGO_BIT_TIMEOUT*HZ /* jiffies */
};

static struct i2c_client cx18_i2c_client_template = {
	.name = "cx18 internal",
};

int cx18_call_i2c_client(struct cx18 *cx, int addr, unsigned cmd, void *arg)
{
	struct i2c_client *client;
	int retval;
	int i;

	CX18_DEBUG_I2C("call_i2c_client addr=%02x\n", addr);
	for (i = 0; i < I2C_CLIENTS_MAX; i++) {
		client = cx->i2c_clients[i];
		if (client == NULL || client->driver == NULL ||
				client->driver->command == NULL)
			continue;
		if (addr == client->addr) {
			retval = client->driver->command(client, cmd, arg);
			return retval;
		}
	}
	if (cmd != VIDIOC_DBG_G_CHIP_IDENT)
		CX18_ERR("i2c addr 0x%02x not found for cmd 0x%x!\n",
			       addr, cmd);
	return -ENODEV;
}

/* Find the i2c device based on the driver ID and return
   its i2c address or -ENODEV if no matching device was found. */
static int cx18_i2c_id_addr(struct cx18 *cx, u32 id)
{
	struct i2c_client *client;
	int retval = -ENODEV;
	int i;

	for (i = 0; i < I2C_CLIENTS_MAX; i++) {
		client = cx->i2c_clients[i];
		if (client == NULL || client->driver == NULL)
			continue;
		if (id == client->driver->id) {
			retval = client->addr;
			break;
		}
	}
	return retval;
}

/* Find the i2c device name matching the CX18_HW_ flag */
static const char *cx18_i2c_hw_name(u32 hw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hw_driverids); i++)
		if (1 << i == hw)
			return hw_devicenames[i];
	return "unknown device";
}

/* Find the i2c device matching the CX18_HW_ flag and return
   its i2c address or -ENODEV if no matching device was found. */
int cx18_i2c_hw_addr(struct cx18 *cx, u32 hw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hw_driverids); i++)
		if (1 << i == hw)
			return cx18_i2c_id_addr(cx, hw_driverids[i]);
	return -ENODEV;
}

/* Calls i2c device based on CX18_HW_ flag. If hw == 0, then do nothing.
   If hw == CX18_HW_GPIO then call the gpio handler. */
int cx18_i2c_hw(struct cx18 *cx, u32 hw, unsigned int cmd, void *arg)
{
	int addr;

	if (hw == 0)
		return 0;

	if (hw == CX18_HW_GPIO)
		return cx18_gpio(cx, cmd, arg);

	if (hw == CX18_HW_CX23418)
		return cx18_av_cmd(cx, cmd, arg);

	addr = cx18_i2c_hw_addr(cx, hw);
	if (addr < 0) {
		CX18_ERR("i2c hardware 0x%08x (%s) not found for cmd 0x%x!\n",
			       hw, cx18_i2c_hw_name(hw), cmd);
		return addr;
	}
	return cx18_call_i2c_client(cx, addr, cmd, arg);
}

/* broadcast cmd for all I2C clients and for the gpio subsystem */
void cx18_call_i2c_clients(struct cx18 *cx, unsigned int cmd, void *arg)
{
	if (cx->i2c_adap[0].algo == NULL || cx->i2c_adap[1].algo == NULL) {
		CX18_ERR("adapter is not set\n");
		return;
	}
	cx18_av_cmd(cx, cmd, arg);
	i2c_clients_command(&cx->i2c_adap[0], cmd, arg);
	i2c_clients_command(&cx->i2c_adap[1], cmd, arg);
	if (cx->hw_flags & CX18_HW_GPIO)
		cx18_gpio(cx, cmd, arg);
}

/* init + register i2c algo-bit adapter */
int init_cx18_i2c(struct cx18 *cx)
{
	int i;
	CX18_DEBUG_I2C("i2c init\n");

	/* Sanity checks for the I2C hardware arrays. They must be the
	 * same size and GPIO/CX23418 must be the last entries.
	 */
	if (ARRAY_SIZE(hw_driverids) != ARRAY_SIZE(hw_addrs) ||
	    ARRAY_SIZE(hw_devicenames) != ARRAY_SIZE(hw_addrs) ||
	    CX18_HW_GPIO != (1 << (ARRAY_SIZE(hw_addrs) - 2)) ||
	    CX18_HW_CX23418 != (1 << (ARRAY_SIZE(hw_addrs) - 1)) ||
	    hw_driverids[ARRAY_SIZE(hw_addrs) - 1]) {
		CX18_ERR("Mismatched I2C hardware arrays\n");
		return -ENODEV;
	}

	for (i = 0; i < 2; i++) {
		memcpy(&cx->i2c_adap[i], &cx18_i2c_adap_template,
			sizeof(struct i2c_adapter));
		memcpy(&cx->i2c_algo[i], &cx18_i2c_algo_template,
			sizeof(struct i2c_algo_bit_data));
		cx->i2c_algo_cb_data[i].cx = cx;
		cx->i2c_algo_cb_data[i].bus_index = i;
		cx->i2c_algo[i].data = &cx->i2c_algo_cb_data[i];
		cx->i2c_adap[i].algo_data = &cx->i2c_algo[i];

		sprintf(cx->i2c_adap[i].name + strlen(cx->i2c_adap[i].name),
				" #%d-%d", cx->num, i);
		i2c_set_adapdata(&cx->i2c_adap[i], cx);

		memcpy(&cx->i2c_client[i], &cx18_i2c_client_template,
			sizeof(struct i2c_client));
		sprintf(cx->i2c_client[i].name +
				strlen(cx->i2c_client[i].name), "%d", i);
		cx->i2c_client[i].adapter = &cx->i2c_adap[i];
		cx->i2c_adap[i].dev.parent = &cx->dev->dev;
	}

	if (cx18_read_reg(cx, CX18_REG_I2C_2_WR) != 0x0003c02f) {
		/* Reset/Unreset I2C hardware block */
		/* Clock select 220MHz */
		cx18_write_reg_expect(cx, 0x10000000, 0xc71004,
					  0x00000000, 0x10001000);
		/* Clock Enable */
		cx18_write_reg_expect(cx, 0x10001000, 0xc71024,
					  0x00001000, 0x10001000);
	}
	/* courtesy of Steven Toth <stoth@hauppauge.com> */
	cx18_write_reg_expect(cx, 0x00c00000, 0xc7001c, 0x00000000, 0x00c000c0);
	mdelay(10);
	cx18_write_reg_expect(cx, 0x00c000c0, 0xc7001c, 0x000000c0, 0x00c000c0);
	mdelay(10);
	cx18_write_reg_expect(cx, 0x00c00000, 0xc7001c, 0x00000000, 0x00c000c0);
	mdelay(10);

	/* Set to edge-triggered intrs. */
	cx18_write_reg(cx, 0x00c00000, 0xc730c8);
	/* Clear any stale intrs */
	cx18_write_reg_expect(cx, HW2_I2C1_INT|HW2_I2C2_INT, HW2_INT_CLR_STATUS,
		       ~(HW2_I2C1_INT|HW2_I2C2_INT), HW2_I2C1_INT|HW2_I2C2_INT);

	/* Hw I2C1 Clock Freq ~100kHz */
	cx18_write_reg(cx, 0x00021c0f & ~4, CX18_REG_I2C_1_WR);
	cx18_setscl(&cx->i2c_algo_cb_data[0], 1);
	cx18_setsda(&cx->i2c_algo_cb_data[0], 1);

	/* Hw I2C2 Clock Freq ~100kHz */
	cx18_write_reg(cx, 0x00021c0f & ~4, CX18_REG_I2C_2_WR);
	cx18_setscl(&cx->i2c_algo_cb_data[1], 1);
	cx18_setsda(&cx->i2c_algo_cb_data[1], 1);

	cx18_reset_i2c_slaves_gpio(cx);

	return i2c_bit_add_bus(&cx->i2c_adap[0]) ||
		i2c_bit_add_bus(&cx->i2c_adap[1]);
}

void exit_cx18_i2c(struct cx18 *cx)
{
	int i;
	CX18_DEBUG_I2C("i2c exit\n");
	cx18_write_reg(cx, cx18_read_reg(cx, CX18_REG_I2C_1_WR) | 4,
							CX18_REG_I2C_1_WR);
	cx18_write_reg(cx, cx18_read_reg(cx, CX18_REG_I2C_2_WR) | 4,
							CX18_REG_I2C_2_WR);

	for (i = 0; i < 2; i++) {
		i2c_del_adapter(&cx->i2c_adap[i]);
	}
}

/*
   Hauppauge HVR1600 should have:
   32 cx24227
   98 unknown
   a0 eeprom
   c2 tuner
   e? zilog ir
   */
