// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * upd64031A - NEC Electronics Ghost Reduction for NTSC in Japan
 *
 * 2003 by T.Adachi <tadachi@tadachi-net.com>
 * 2003 by Takeru KOMORIYA <komoriya@paken.org>
 * 2006 by Hans Verkuil <hverkuil@xs4all.nl>
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/i2c/upd64031a.h>

/* --------------------- read registers functions define -------------------- */

/* bit masks */
#define GR_MODE_MASK              0xc0
#define DIRECT_3DYCS_CONNECT_MASK 0xc0
#define SYNC_CIRCUIT_MASK         0xa0

/* -------------------------------------------------------------------------- */

MODULE_DESCRIPTION("uPD64031A driver");
MODULE_AUTHOR("T. Adachi, Takeru KOMORIYA, Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level (0-1)");


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
	struct v4l2_subdev sd;
	u8 regs[TOT_REGS];
	u8 gr_mode;
	u8 direct_3dycs_connect;
	u8 ext_comp_sync;
	u8 ext_vert_sync;
};

static inline struct upd64031a_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct upd64031a_state, sd);
}

static u8 upd64031a_init[] = {
	0x00, 0xb8, 0x48, 0xd2, 0xe6,
	0x03, 0x10, 0x0b, 0xaf, 0x7f,
	0x00, 0x00, 0x1d, 0x5e, 0x00,
	0xd0
};

/* ------------------------------------------------------------------------ */

static u8 upd64031a_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 buf[2];

	if (reg >= sizeof(buf))
		return 0xff;
	i2c_master_recv(client, buf, 2);
	return buf[reg];
}

/* ------------------------------------------------------------------------ */

static void upd64031a_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 buf[2];

	buf[0] = reg;
	buf[1] = val;
	v4l2_dbg(1, debug, sd, "write reg: %02X val: %02X\n", reg, val);
	if (i2c_master_send(client, buf, 2) != 2)
		v4l2_err(sd, "I/O error write 0x%02x/0x%02x\n", reg, val);
}

/* ------------------------------------------------------------------------ */

/* The input changed due to new input or channel changed */
static int upd64031a_s_frequency(struct v4l2_subdev *sd, const struct v4l2_frequency *freq)
{
	struct upd64031a_state *state = to_state(sd);
	u8 reg = state->regs[R00];

	v4l2_dbg(1, debug, sd, "changed input or channel\n");
	upd64031a_write(sd, R00, reg | 0x10);
	upd64031a_write(sd, R00, reg & ~0x10);
	return 0;
}

/* ------------------------------------------------------------------------ */

static int upd64031a_s_routing(struct v4l2_subdev *sd,
			       u32 input, u32 output, u32 config)
{
	struct upd64031a_state *state = to_state(sd);
	u8 r00, r05, r08;

	state->gr_mode = (input & 3) << 6;
	state->direct_3dycs_connect = (input & 0xc) << 4;
	state->ext_comp_sync =
		(input & UPD64031A_COMPOSITE_EXTERNAL) << 1;
	state->ext_vert_sync =
		(input & UPD64031A_VERTICAL_EXTERNAL) << 2;
	r00 = (state->regs[R00] & ~GR_MODE_MASK) | state->gr_mode;
	r05 = (state->regs[R00] & ~SYNC_CIRCUIT_MASK) |
		state->ext_comp_sync | state->ext_vert_sync;
	r08 = (state->regs[R08] & ~DIRECT_3DYCS_CONNECT_MASK) |
		state->direct_3dycs_connect;
	upd64031a_write(sd, R00, r00);
	upd64031a_write(sd, R05, r05);
	upd64031a_write(sd, R08, r08);
	return upd64031a_s_frequency(sd, NULL);
}

static int upd64031a_log_status(struct v4l2_subdev *sd)
{
	v4l2_info(sd, "Status: SA00=0x%02x SA01=0x%02x\n",
			upd64031a_read(sd, 0), upd64031a_read(sd, 1));
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int upd64031a_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	reg->val = upd64031a_read(sd, reg->reg & 0xff);
	reg->size = 1;
	return 0;
}

static int upd64031a_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	upd64031a_write(sd, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops upd64031a_core_ops = {
	.log_status = upd64031a_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = upd64031a_g_register,
	.s_register = upd64031a_s_register,
#endif
};

static const struct v4l2_subdev_tuner_ops upd64031a_tuner_ops = {
	.s_frequency = upd64031a_s_frequency,
};

static const struct v4l2_subdev_video_ops upd64031a_video_ops = {
	.s_routing = upd64031a_s_routing,
};

static const struct v4l2_subdev_ops upd64031a_ops = {
	.core = &upd64031a_core_ops,
	.tuner = &upd64031a_tuner_ops,
	.video = &upd64031a_video_ops,
};

/* ------------------------------------------------------------------------ */

/* i2c implementation */

static int upd64031a_probe(struct i2c_client *client)
{
	struct upd64031a_state *state;
	struct v4l2_subdev *sd;
	int i;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &upd64031a_ops);
	memcpy(state->regs, upd64031a_init, sizeof(state->regs));
	state->gr_mode = UPD64031A_GR_ON << 6;
	state->direct_3dycs_connect = UPD64031A_3DYCS_COMPOSITE << 4;
	state->ext_comp_sync = state->ext_vert_sync = 0;
	for (i = 0; i < TOT_REGS; i++)
		upd64031a_write(sd, i, state->regs[i]);
	return 0;
}

static void upd64031a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id upd64031a_id[] = {
	{ "upd64031a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, upd64031a_id);

static struct i2c_driver upd64031a_driver = {
	.driver = {
		.name	= "upd64031a",
	},
	.probe_new	= upd64031a_probe,
	.remove		= upd64031a_remove,
	.id_table	= upd64031a_id,
};

module_i2c_driver(upd64031a_driver);
