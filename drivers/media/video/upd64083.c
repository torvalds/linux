/*
 * upd6408x - NEC Electronics 3-Dimensional Y/C separation driver
 *
 * 2003 by T.Adachi (tadachi@tadachi-net.com)
 * 2003 by Takeru KOMORIYA <komoriya@paken.org>
 * 2006 by Hans Verkuil <hverkuil@xs4all.nl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv.h>
#include <media/upd64083.h>

MODULE_DESCRIPTION("uPD64083 driver");
MODULE_AUTHOR("T. Adachi, Takeru KOMORIYA, Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");


enum {
	R00 = 0, R01, R02, R03, R04,
	R05, R06, R07, R08, R09,
	R0A, R0B, R0C, R0D, R0E, R0F,
	R10, R11, R12, R13, R14,
	R15, R16,
	TOT_REGS
};

struct upd64083_state {
	u8 mode;
	u8 ext_y_adc;
	u8 regs[TOT_REGS];
};

/* Initial values when used in combination with the
   NEC upd64031a ghost reduction chip. */
static u8 upd64083_init[] = {
	0x1f, 0x01, 0xa0, 0x2d, 0x29,  /* we use EXCSS=0 */
	0x36, 0xdd, 0x05, 0x56, 0x48,
	0x00, 0x3a, 0xa0, 0x05, 0x08,
	0x44, 0x60, 0x08, 0x52, 0xf8,
	0x53, 0x60, 0x10
};

/* ------------------------------------------------------------------------ */

static void upd64083_log_status(struct i2c_client *client)
{
	u8 buf[7];

	i2c_master_recv(client, buf, 7);
	v4l_info(client, "Status: SA00=%02x SA01=%02x SA02=%02x SA03=%02x "
		      "SA04=%02x SA05=%02x SA06=%02x\n",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
}

/* ------------------------------------------------------------------------ */

static void upd64083_write(struct i2c_client *client, u8 reg, u8 val)
{
	u8 buf[2];

	buf[0] = reg;
	buf[1] = val;
	v4l_dbg(1, debug, client, "write reg: %02x val: %02x\n", reg, val);
	if (i2c_master_send(client, buf, 2) != 2)
		v4l_err(client, "I/O error write 0x%02x/0x%02x\n", reg, val);
}

/* ------------------------------------------------------------------------ */

#ifdef CONFIG_VIDEO_ADV_DEBUG
static u8 upd64083_read(struct i2c_client *client, u8 reg)
{
	u8 buf[7];

	if (reg >= sizeof(buf))
		return 0xff;
	i2c_master_recv(client, buf, sizeof(buf));
	return buf[reg];
}
#endif

/* ------------------------------------------------------------------------ */

static int upd64083_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct upd64083_state *state = i2c_get_clientdata(client);
	struct v4l2_routing *route = arg;

	switch (cmd) {
	case VIDIOC_INT_G_VIDEO_ROUTING:
		route->input = (state->mode >> 6) | (state->ext_y_adc >> 3);
		route->output = 0;
		break;

	case VIDIOC_INT_S_VIDEO_ROUTING:
	{
		u8 r00, r02;

		if (route->input > 7 || (route->input & 6) == 6)
			return -EINVAL;
		state->mode = (route->input & 3) << 6;
		state->ext_y_adc = (route->input & UPD64083_EXT_Y_ADC) << 3;
		r00 = (state->regs[R00] & ~(3 << 6)) | state->mode;
		r02 = (state->regs[R02] & ~(1 << 5)) | state->ext_y_adc;
		upd64083_write(client, R00, r00);
		upd64083_write(client, R02, r02);
		break;
	}

	case VIDIOC_LOG_STATUS:
		upd64083_log_status(client);
		break;

#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_DBG_G_REGISTER:
	case VIDIOC_DBG_S_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (!v4l2_chip_match_i2c_client(client,
				reg->match_type, reg->match_chip))
			return -EINVAL;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (cmd == VIDIOC_DBG_G_REGISTER) {
			reg->val = upd64083_read(client, reg->reg & 0xff);
			break;
		}
		upd64083_write(client, reg->reg & 0xff, reg->val & 0xff);
		break;
	}
#endif

	case VIDIOC_G_CHIP_IDENT:
		return v4l2_chip_ident_i2c_client(client, arg,
				V4L2_IDENT_UPD64083, 0);

	default:
		break;
	}

	return 0;
}

/* ------------------------------------------------------------------------ */

/* i2c implementation */

static int upd64083_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct upd64083_state *state;
	int i;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = kmalloc(sizeof(struct upd64083_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	i2c_set_clientdata(client, state);
	/* Initially assume that a ghost reduction chip is present */
	state->mode = 0;  /* YCS mode */
	state->ext_y_adc = (1 << 5);
	memcpy(state->regs, upd64083_init, TOT_REGS);
	for (i = 0; i < TOT_REGS; i++)
		upd64083_write(client, i, state->regs[i]);
	return 0;
}

static int upd64083_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id upd64083_id[] = {
	{ "upd64083", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, upd64083_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "upd64083",
	.driverid = I2C_DRIVERID_UPD64083,
	.command = upd64083_command,
	.probe = upd64083_probe,
	.remove = upd64083_remove,
	.id_table = upd64083_id,
};
