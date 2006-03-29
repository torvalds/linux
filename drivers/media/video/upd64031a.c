/*
 * upd64031A - NEC Electronics Ghost Reduction for NTSC in Japan
 *
 * 2003 by T.Adachi <tadachi@tadachi-net.com>
 * 2003 by Takeru KOMORIYA <komoriya@paken.org>
 * 2006 by Hans Verkuil <hverkuil@xs4all.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/upd64031a.h>

// --------------------- read registers functions define -----------------------

/* bit masks */
#define GR_MODE_MASK              0xc0
#define DIRECT_3DYCS_CONNECT_MASK 0xc0
#define SYNC_CIRCUIT_MASK         0xa0

// -----------------------------------------------------------------------------

MODULE_DESCRIPTION("uPD64031A driver");
MODULE_AUTHOR("T. Adachi, Takeru KOMORIYA, Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug = 0;
module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");

static unsigned short normal_i2c[] = { 0x24 >> 1, 0x26 >> 1, I2C_CLIENT_END };


I2C_CLIENT_INSMOD;

enum {
	R00 = 0, R01, R02, R03, R04,
	R05, R06, R07, R08, R09,
	R0A, R0B, R0C, R0D, R0E, R0F,
	/* unused registers
	 R10, R11, R12, R13, R14,
	 R15, R16, R17,
	 */
	TOT_REGS
};

struct upd64031a_state {
	u8 regs[TOT_REGS];
	u8 gr_mode;
	u8 direct_3dycs_connect;
	u8 ext_comp_sync;
	u8 ext_vert_sync;
};

static u8 upd64031a_init[] = {
	0x00, 0xb8, 0x48, 0xd2, 0xe6,
	0x03, 0x10, 0x0b, 0xaf, 0x7f,
	0x00, 0x00, 0x1d, 0x5e, 0x00,
	0xd0
};

/* ------------------------------------------------------------------------ */

static u8 upd64031a_read(struct i2c_client *client, u8 reg)
{
	u8 buf[2];

	if (reg >= sizeof(buf))
		return 0xff;
	i2c_master_recv(client, buf, 2);
	return buf[reg];
}

/* ------------------------------------------------------------------------ */

static void upd64031a_write(struct i2c_client *client, u8 reg, u8 val)
{
	u8 buf[2];

	buf[0] = reg;
	buf[1] = val;
	v4l_dbg(1, debug, client, "writing reg addr: %02X val: %02X\n", reg, val);
	if (i2c_master_send(client, buf, 2) != 2)
		v4l_err(client, "I/O error write 0x%02x/0x%02x\n", reg, val);
}

/* ------------------------------------------------------------------------ */

/* The input changed due to new input or channel changed */
static void upd64031a_change(struct i2c_client *client)
{
	struct upd64031a_state *state = i2c_get_clientdata(client);
	u8 reg = state->regs[R00];

	v4l_dbg(1, debug, client, "changed input or channel\n");
	upd64031a_write(client, R00, reg | 0x10);
	upd64031a_write(client, R00, reg & ~0x10);
}

/* ------------------------------------------------------------------------ */

static int upd64031a_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct upd64031a_state *state = i2c_get_clientdata(client);
	struct v4l2_routing *route = arg;

	switch (cmd) {
	case VIDIOC_S_FREQUENCY:
		upd64031a_change(client);
		break;

	case VIDIOC_INT_G_VIDEO_ROUTING:
		route->input = (state->gr_mode >> 6) |
			(state->direct_3dycs_connect >> 4) |
			(state->ext_comp_sync >> 1) |
			(state->ext_vert_sync >> 2);
		route->output = 0;
		break;

	case VIDIOC_INT_S_VIDEO_ROUTING:
	{
		u8 r00, r05, r08;

		state->gr_mode = (route->input & 3) << 6;
		state->direct_3dycs_connect = (route->input & 0xc) << 4;
		state->ext_comp_sync = (route->input & UPD64031A_COMPOSITE_EXTERNAL) << 1;
		state->ext_vert_sync = (route->input & UPD64031A_VERTICAL_EXTERNAL) << 2;
		r00 = (state->regs[R00] & ~GR_MODE_MASK) | state->gr_mode;
		r05 = (state->regs[R00] & ~SYNC_CIRCUIT_MASK) |
			state->ext_comp_sync | state->ext_vert_sync;
		r08 = (state->regs[R08] & ~DIRECT_3DYCS_CONNECT_MASK) |
			state->direct_3dycs_connect;
		upd64031a_write(client, R00, r00);
		upd64031a_write(client, R05, r05);
		upd64031a_write(client, R08, r08);
		upd64031a_change(client);
		break;
	}

	case VIDIOC_LOG_STATUS:
		v4l_info(client, "Status: SA00=0x%02x SA01=0x%02x\n",
			upd64031a_read(client, 0), upd64031a_read(client, 1));
		break;

#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_INT_G_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (reg->i2c_id != I2C_DRIVERID_UPD64031A)
			return -EINVAL;
		reg->val = upd64031a_read(client, reg->reg & 0xff);
		break;
	}

	case VIDIOC_INT_S_REGISTER:
	{
		struct v4l2_register *reg = arg;
		u8 addr = reg->reg & 0xff;
		u8 val = reg->val & 0xff;

		if (reg->i2c_id != I2C_DRIVERID_UPD64031A)
			return -EINVAL;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		upd64031a_write(client, addr, val);
		break;
	}
#endif

	default:
		break;
	}
	return 0;
}

/* ------------------------------------------------------------------------ */

/* i2c implementation */

static struct i2c_driver i2c_driver;

static int upd64031a_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct upd64031a_state *state;
	int i;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == NULL) {
		return -ENOMEM;
	}

	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver;
	snprintf(client->name, sizeof(client->name) - 1, "uPD64031A");

	v4l_info(client, "chip found @ 0x%x (%s)\n", address << 1, adapter->name);

	state = kmalloc(sizeof(struct upd64031a_state), GFP_KERNEL);
	if (state == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	i2c_set_clientdata(client, state);
	memcpy(state->regs, upd64031a_init, sizeof(state->regs));
	state->gr_mode = UPD64031A_GR_ON << 6;
	state->direct_3dycs_connect = UPD64031A_3DYCS_COMPOSITE << 4;
	state->ext_comp_sync = state->ext_vert_sync = 0;
	for (i = 0; i < TOT_REGS; i++) {
		upd64031a_write(client, i, state->regs[i]);
	}

	i2c_attach_client(client);

	return 0;
}

static int upd64031a_probe(struct i2c_adapter *adapter)
{
	if (adapter->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adapter, &addr_data, upd64031a_attach);
	return 0;
}

static int upd64031a_detach(struct i2c_client *client)
{
	int err;

	err = i2c_detach_client(client);
	if (err)
		return err;

	kfree(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver = {
	.driver = {
		.name = "upd64031a",
	},
	.id = I2C_DRIVERID_UPD64031A,
	.attach_adapter = upd64031a_probe,
	.detach_client  = upd64031a_detach,
	.command = upd64031a_command,
};


static int __init upd64031a_init_module(void)
{
	return i2c_add_driver(&i2c_driver);
}

static void __exit upd64031a_exit_module(void)
{
	i2c_del_driver(&i2c_driver);
}

module_init(upd64031a_init_module);
module_exit(upd64031a_exit_module);

