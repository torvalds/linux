/*
 * adv7842 - Analog Devices ADV7842 video decoder driver
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * References (c = chapter, p = page):
 * REF_01 - Analog devices, ADV7842, Register Settings Recommendations,
 *		Revision 2.5, June 2010
 * REF_02 - Analog devices, Register map documentation, Documentation of
 *		the register maps, Software manual, Rev. F, June 2010
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dv-timings.h>
#include <media/adv7842.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

MODULE_DESCRIPTION("Analog Devices ADV7842 video decoder driver");
MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_AUTHOR("Martin Bugge <marbugge@cisco.com>");
MODULE_LICENSE("GPL");

/* ADV7842 system clock frequency */
#define ADV7842_fsc (28636360)

/*
**********************************************************************
*
*  Arrays with configuration parameters for the ADV7842
*
**********************************************************************
*/

struct adv7842_state {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler hdl;
	enum adv7842_mode mode;
	struct v4l2_dv_timings timings;
	enum adv7842_vid_std_select vid_std_select;
	v4l2_std_id norm;
	struct {
		u8 edid[256];
		u32 present;
	} hdmi_edid;
	struct {
		u8 edid[256];
		u32 present;
	} vga_edid;
	struct v4l2_fract aspect_ratio;
	u32 rgb_quantization_range;
	bool is_cea_format;
	struct workqueue_struct *work_queues;
	struct delayed_work delayed_work_enable_hotplug;
	bool connector_hdmi;
	bool hdmi_port_a;

	/* i2c clients */
	struct i2c_client *i2c_sdp_io;
	struct i2c_client *i2c_sdp;
	struct i2c_client *i2c_cp;
	struct i2c_client *i2c_vdp;
	struct i2c_client *i2c_afe;
	struct i2c_client *i2c_hdmi;
	struct i2c_client *i2c_repeater;
	struct i2c_client *i2c_edid;
	struct i2c_client *i2c_infoframe;
	struct i2c_client *i2c_cec;
	struct i2c_client *i2c_avlink;

	/* controls */
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *analog_sampling_phase_ctrl;
	struct v4l2_ctrl *free_run_color_ctrl_manual;
	struct v4l2_ctrl *free_run_color_ctrl;
	struct v4l2_ctrl *rgb_quantization_range_ctrl;
};

/* Unsupported timings. This device cannot support 720p30. */
static const struct v4l2_dv_timings adv7842_timings_exceptions[] = {
	V4L2_DV_BT_CEA_1280X720P30,
	{ }
};

static bool adv7842_check_dv_timings(const struct v4l2_dv_timings *t, void *hdl)
{
	int i;

	for (i = 0; adv7842_timings_exceptions[i].bt.width; i++)
		if (v4l2_match_dv_timings(t, adv7842_timings_exceptions + i, 0))
			return false;
	return true;
}

struct adv7842_video_standards {
	struct v4l2_dv_timings timings;
	u8 vid_std;
	u8 v_freq;
};

/* sorted by number of lines */
static const struct adv7842_video_standards adv7842_prim_mode_comp[] = {
	/* { V4L2_DV_BT_CEA_720X480P59_94, 0x0a, 0x00 }, TODO flickering */
	{ V4L2_DV_BT_CEA_720X576P50, 0x0b, 0x00 },
	{ V4L2_DV_BT_CEA_1280X720P50, 0x19, 0x01 },
	{ V4L2_DV_BT_CEA_1280X720P60, 0x19, 0x00 },
	{ V4L2_DV_BT_CEA_1920X1080P24, 0x1e, 0x04 },
	{ V4L2_DV_BT_CEA_1920X1080P25, 0x1e, 0x03 },
	{ V4L2_DV_BT_CEA_1920X1080P30, 0x1e, 0x02 },
	{ V4L2_DV_BT_CEA_1920X1080P50, 0x1e, 0x01 },
	{ V4L2_DV_BT_CEA_1920X1080P60, 0x1e, 0x00 },
	/* TODO add 1920x1080P60_RB (CVT timing) */
	{ },
};

/* sorted by number of lines */
static const struct adv7842_video_standards adv7842_prim_mode_gr[] = {
	{ V4L2_DV_BT_DMT_640X480P60, 0x08, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P72, 0x09, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P75, 0x0a, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P85, 0x0b, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P56, 0x00, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P60, 0x01, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P72, 0x02, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P75, 0x03, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P85, 0x04, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P60, 0x0c, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P70, 0x0d, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P75, 0x0e, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P85, 0x0f, 0x00 },
	{ V4L2_DV_BT_DMT_1280X1024P60, 0x05, 0x00 },
	{ V4L2_DV_BT_DMT_1280X1024P75, 0x06, 0x00 },
	{ V4L2_DV_BT_DMT_1360X768P60, 0x12, 0x00 },
	{ V4L2_DV_BT_DMT_1366X768P60, 0x13, 0x00 },
	{ V4L2_DV_BT_DMT_1400X1050P60, 0x14, 0x00 },
	{ V4L2_DV_BT_DMT_1400X1050P75, 0x15, 0x00 },
	{ V4L2_DV_BT_DMT_1600X1200P60, 0x16, 0x00 }, /* TODO not tested */
	/* TODO add 1600X1200P60_RB (not a DMT timing) */
	{ V4L2_DV_BT_DMT_1680X1050P60, 0x18, 0x00 },
	{ V4L2_DV_BT_DMT_1920X1200P60_RB, 0x19, 0x00 }, /* TODO not tested */
	{ },
};

/* sorted by number of lines */
static const struct adv7842_video_standards adv7842_prim_mode_hdmi_comp[] = {
	{ V4L2_DV_BT_CEA_720X480P59_94, 0x0a, 0x00 },
	{ V4L2_DV_BT_CEA_720X576P50, 0x0b, 0x00 },
	{ V4L2_DV_BT_CEA_1280X720P50, 0x13, 0x01 },
	{ V4L2_DV_BT_CEA_1280X720P60, 0x13, 0x00 },
	{ V4L2_DV_BT_CEA_1920X1080P24, 0x1e, 0x04 },
	{ V4L2_DV_BT_CEA_1920X1080P25, 0x1e, 0x03 },
	{ V4L2_DV_BT_CEA_1920X1080P30, 0x1e, 0x02 },
	{ V4L2_DV_BT_CEA_1920X1080P50, 0x1e, 0x01 },
	{ V4L2_DV_BT_CEA_1920X1080P60, 0x1e, 0x00 },
	{ },
};

/* sorted by number of lines */
static const struct adv7842_video_standards adv7842_prim_mode_hdmi_gr[] = {
	{ V4L2_DV_BT_DMT_640X480P60, 0x08, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P72, 0x09, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P75, 0x0a, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P85, 0x0b, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P56, 0x00, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P60, 0x01, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P72, 0x02, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P75, 0x03, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P85, 0x04, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P60, 0x0c, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P70, 0x0d, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P75, 0x0e, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P85, 0x0f, 0x00 },
	{ V4L2_DV_BT_DMT_1280X1024P60, 0x05, 0x00 },
	{ V4L2_DV_BT_DMT_1280X1024P75, 0x06, 0x00 },
	{ },
};

/* ----------------------------------------------------------------------- */

static inline struct adv7842_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7842_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct adv7842_state, hdl)->sd;
}

static inline unsigned hblanking(const struct v4l2_bt_timings *t)
{
	return V4L2_DV_BT_BLANKING_WIDTH(t);
}

static inline unsigned htotal(const struct v4l2_bt_timings *t)
{
	return V4L2_DV_BT_FRAME_WIDTH(t);
}

static inline unsigned vblanking(const struct v4l2_bt_timings *t)
{
	return V4L2_DV_BT_BLANKING_HEIGHT(t);
}

static inline unsigned vtotal(const struct v4l2_bt_timings *t)
{
	return V4L2_DV_BT_FRAME_HEIGHT(t);
}


/* ----------------------------------------------------------------------- */

static s32 adv_smbus_read_byte_data_check(struct i2c_client *client,
					  u8 command, bool check)
{
	union i2c_smbus_data data;

	if (!i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			    I2C_SMBUS_READ, command,
			    I2C_SMBUS_BYTE_DATA, &data))
		return data.byte;
	if (check)
		v4l_err(client, "error reading %02x, %02x\n",
			client->addr, command);
	return -EIO;
}

static s32 adv_smbus_read_byte_data(struct i2c_client *client, u8 command)
{
	int i;

	for (i = 0; i < 3; i++) {
		int ret = adv_smbus_read_byte_data_check(client, command, true);

		if (ret >= 0) {
			if (i)
				v4l_err(client, "read ok after %d retries\n", i);
			return ret;
		}
	}
	v4l_err(client, "read failed\n");
	return -EIO;
}

static s32 adv_smbus_write_byte_data(struct i2c_client *client,
				     u8 command, u8 value)
{
	union i2c_smbus_data data;
	int err;
	int i;

	data.byte = value;
	for (i = 0; i < 3; i++) {
		err = i2c_smbus_xfer(client->adapter, client->addr,
				     client->flags,
				     I2C_SMBUS_WRITE, command,
				     I2C_SMBUS_BYTE_DATA, &data);
		if (!err)
			break;
	}
	if (err < 0)
		v4l_err(client, "error writing %02x, %02x, %02x\n",
			client->addr, command, value);
	return err;
}

static void adv_smbus_write_byte_no_check(struct i2c_client *client,
					  u8 command, u8 value)
{
	union i2c_smbus_data data;
	data.byte = value;

	i2c_smbus_xfer(client->adapter, client->addr,
		       client->flags,
		       I2C_SMBUS_WRITE, command,
		       I2C_SMBUS_BYTE_DATA, &data);
}

static s32 adv_smbus_write_i2c_block_data(struct i2c_client *client,
				  u8 command, unsigned length, const u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	memcpy(data.block + 1, values, length);
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_I2C_BLOCK_DATA, &data);
}

/* ----------------------------------------------------------------------- */

static inline int io_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return adv_smbus_read_byte_data(client, reg);
}

static inline int io_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return adv_smbus_write_byte_data(client, reg, val);
}

static inline int io_write_and_or(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return io_write(sd, reg, (io_read(sd, reg) & mask) | val);
}

static inline int avlink_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_avlink, reg);
}

static inline int avlink_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_avlink, reg, val);
}

static inline int cec_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_cec, reg);
}

static inline int cec_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_cec, reg, val);
}

static inline int cec_write_and_or(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return cec_write(sd, reg, (cec_read(sd, reg) & mask) | val);
}

static inline int infoframe_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_infoframe, reg);
}

static inline int infoframe_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_infoframe, reg, val);
}

static inline int sdp_io_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_sdp_io, reg);
}

static inline int sdp_io_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_sdp_io, reg, val);
}

static inline int sdp_io_write_and_or(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return sdp_io_write(sd, reg, (sdp_io_read(sd, reg) & mask) | val);
}

static inline int sdp_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_sdp, reg);
}

static inline int sdp_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_sdp, reg, val);
}

static inline int sdp_write_and_or(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return sdp_write(sd, reg, (sdp_read(sd, reg) & mask) | val);
}

static inline int afe_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_afe, reg);
}

static inline int afe_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_afe, reg, val);
}

static inline int afe_write_and_or(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return afe_write(sd, reg, (afe_read(sd, reg) & mask) | val);
}

static inline int rep_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_repeater, reg);
}

static inline int rep_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_repeater, reg, val);
}

static inline int rep_write_and_or(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return rep_write(sd, reg, (rep_read(sd, reg) & mask) | val);
}

static inline int edid_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_edid, reg);
}

static inline int edid_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_edid, reg, val);
}

static inline int hdmi_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_hdmi, reg);
}

static inline int hdmi_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_hdmi, reg, val);
}

static inline int cp_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_cp, reg);
}

static inline int cp_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_cp, reg, val);
}

static inline int cp_write_and_or(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return cp_write(sd, reg, (cp_read(sd, reg) & mask) | val);
}

static inline int vdp_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state->i2c_vdp, reg);
}

static inline int vdp_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7842_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state->i2c_vdp, reg, val);
}

static void main_reset(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	adv_smbus_write_byte_no_check(client, 0xff, 0x80);

	mdelay(2);
}

/* ----------------------------------------------------------------------- */

static inline bool is_digital_input(struct v4l2_subdev *sd)
{
	struct adv7842_state *state = to_state(sd);

	return state->mode == ADV7842_MODE_HDMI;
}

static const struct v4l2_dv_timings_cap adv7842_timings_cap_analog = {
	.type = V4L2_DV_BT_656_1120,
	.bt = {
		.max_width = 1920,
		.max_height = 1200,
		.min_pixelclock = 25000000,
		.max_pixelclock = 170000000,
		.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
		.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING | V4L2_DV_BT_CAP_CUSTOM,
	},
};

static const struct v4l2_dv_timings_cap adv7842_timings_cap_digital = {
	.type = V4L2_DV_BT_656_1120,
	.bt = {
		.max_width = 1920,
		.max_height = 1200,
		.min_pixelclock = 25000000,
		.max_pixelclock = 225000000,
		.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
		.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING | V4L2_DV_BT_CAP_CUSTOM,
	},
};

static inline const struct v4l2_dv_timings_cap *
adv7842_get_dv_timings_cap(struct v4l2_subdev *sd)
{
	return is_digital_input(sd) ? &adv7842_timings_cap_digital :
				      &adv7842_timings_cap_analog;
}

/* ----------------------------------------------------------------------- */

static void adv7842_delayed_work_enable_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct adv7842_state *state = container_of(dwork,
			struct adv7842_state, delayed_work_enable_hotplug);
	struct v4l2_subdev *sd = &state->sd;
	int present = state->hdmi_edid.present;
	u8 mask = 0;

	v4l2_dbg(2, debug, sd, "%s: enable hotplug on ports: 0x%x\n",
			__func__, present);

	if (present & 0x1)
		mask |= 0x20; /* port A */
	if (present & 0x2)
		mask |= 0x10; /* port B */
	io_write_and_or(sd, 0x20, 0xcf, mask);
}

static int edid_write_vga_segment(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct adv7842_state *state = to_state(sd);
	const u8 *val = state->vga_edid.edid;
	int err = 0;
	int i;

	v4l2_dbg(2, debug, sd, "%s: write EDID on VGA port\n", __func__);

	/* HPA disable on port A and B */
	io_write_and_or(sd, 0x20, 0xcf, 0x00);

	/* Disable I2C access to internal EDID ram from VGA DDC port */
	rep_write_and_or(sd, 0x7f, 0x7f, 0x00);

	/* edid segment pointer '1' for VGA port */
	rep_write_and_or(sd, 0x77, 0xef, 0x10);

	for (i = 0; !err && i < 256; i += I2C_SMBUS_BLOCK_MAX)
		err = adv_smbus_write_i2c_block_data(state->i2c_edid, i,
					     I2C_SMBUS_BLOCK_MAX, val + i);
	if (err)
		return err;

	/* Calculates the checksums and enables I2C access
	 * to internal EDID ram from VGA DDC port.
	 */
	rep_write_and_or(sd, 0x7f, 0x7f, 0x80);

	for (i = 0; i < 1000; i++) {
		if (rep_read(sd, 0x79) & 0x20)
			break;
		mdelay(1);
	}
	if (i == 1000) {
		v4l_err(client, "error enabling edid on VGA port\n");
		return -EIO;
	}

	/* enable hotplug after 200 ms */
	queue_delayed_work(state->work_queues,
			&state->delayed_work_enable_hotplug, HZ / 5);

	return 0;
}

static int edid_spa_location(const u8 *edid)
{
	u8 d;

	/*
	 * TODO, improve and update for other CEA extensions
	 * currently only for 1 segment (256 bytes),
	 * i.e. 1 extension block and CEA revision 3.
	 */
	if ((edid[0x7e] != 1) ||
	    (edid[0x80] != 0x02) ||
	    (edid[0x81] != 0x03)) {
		return -EINVAL;
	}
	/*
	 * search Vendor Specific Data Block (tag 3)
	 */
	d = edid[0x82] & 0x7f;
	if (d > 4) {
		int i = 0x84;
		int end = 0x80 + d;
		do {
			u8 tag = edid[i]>>5;
			u8 len = edid[i] & 0x1f;

			if ((tag == 3) && (len >= 5))
				return i + 4;
			i += len + 1;
		} while (i < end);
	}
	return -EINVAL;
}

static int edid_write_hdmi_segment(struct v4l2_subdev *sd, u8 port)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct adv7842_state *state = to_state(sd);
	const u8 *val = state->hdmi_edid.edid;
	u8 cur_mask = rep_read(sd, 0x77) & 0x0c;
	u8 mask = port == 0 ? 0x4 : 0x8;
	int spa_loc = edid_spa_location(val);
	int err = 0;
	int i;

	v4l2_dbg(2, debug, sd, "%s: write EDID on port %d (spa at 0x%x)\n",
			__func__, port, spa_loc);

	/* HPA disable on port A and B */
	io_write_and_or(sd, 0x20, 0xcf, 0x00);

	/* Disable I2C access to internal EDID ram from HDMI DDC ports */
	rep_write_and_or(sd, 0x77, 0xf3, 0x00);

	/* edid segment pointer '0' for HDMI ports */
	rep_write_and_or(sd, 0x77, 0xef, 0x00);

	for (i = 0; !err && i < 256; i += I2C_SMBUS_BLOCK_MAX)
		err = adv_smbus_write_i2c_block_data(state->i2c_edid, i,
						     I2C_SMBUS_BLOCK_MAX, val + i);
	if (err)
		return err;

	if (spa_loc > 0) {
		if (port == 0) {
			/* port A SPA */
			rep_write(sd, 0x72, val[spa_loc]);
			rep_write(sd, 0x73, val[spa_loc + 1]);
		} else {
			/* port B SPA */
			rep_write(sd, 0x74, val[spa_loc]);
			rep_write(sd, 0x75, val[spa_loc + 1]);
		}
		rep_write(sd, 0x76, spa_loc);
	} else {
		/* default register values for SPA */
		if (port == 0) {
			/* port A SPA */
			rep_write(sd, 0x72, 0);
			rep_write(sd, 0x73, 0);
		} else {
			/* port B SPA */
			rep_write(sd, 0x74, 0);
			rep_write(sd, 0x75, 0);
		}
		rep_write(sd, 0x76, 0xc0);
	}
	rep_write_and_or(sd, 0x77, 0xbf, 0x00);

	/* Calculates the checksums and enables I2C access to internal
	 * EDID ram from HDMI DDC ports
	 */
	rep_write_and_or(sd, 0x77, 0xf3, mask | cur_mask);

	for (i = 0; i < 1000; i++) {
		if (rep_read(sd, 0x7d) & mask)
			break;
		mdelay(1);
	}
	if (i == 1000) {
		v4l_err(client, "error enabling edid on port %d\n", port);
		return -EIO;
	}

	/* enable hotplug after 200 ms */
	queue_delayed_work(state->work_queues,
			&state->delayed_work_enable_hotplug, HZ / 5);

	return 0;
}

/* ----------------------------------------------------------------------- */

#ifdef CONFIG_VIDEO_ADV_DEBUG
static void adv7842_inv_register(struct v4l2_subdev *sd)
{
	v4l2_info(sd, "0x000-0x0ff: IO Map\n");
	v4l2_info(sd, "0x100-0x1ff: AVLink Map\n");
	v4l2_info(sd, "0x200-0x2ff: CEC Map\n");
	v4l2_info(sd, "0x300-0x3ff: InfoFrame Map\n");
	v4l2_info(sd, "0x400-0x4ff: SDP_IO Map\n");
	v4l2_info(sd, "0x500-0x5ff: SDP Map\n");
	v4l2_info(sd, "0x600-0x6ff: AFE Map\n");
	v4l2_info(sd, "0x700-0x7ff: Repeater Map\n");
	v4l2_info(sd, "0x800-0x8ff: EDID Map\n");
	v4l2_info(sd, "0x900-0x9ff: HDMI Map\n");
	v4l2_info(sd, "0xa00-0xaff: CP Map\n");
	v4l2_info(sd, "0xb00-0xbff: VDP Map\n");
}

static int adv7842_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	reg->size = 1;
	switch (reg->reg >> 8) {
	case 0:
		reg->val = io_read(sd, reg->reg & 0xff);
		break;
	case 1:
		reg->val = avlink_read(sd, reg->reg & 0xff);
		break;
	case 2:
		reg->val = cec_read(sd, reg->reg & 0xff);
		break;
	case 3:
		reg->val = infoframe_read(sd, reg->reg & 0xff);
		break;
	case 4:
		reg->val = sdp_io_read(sd, reg->reg & 0xff);
		break;
	case 5:
		reg->val = sdp_read(sd, reg->reg & 0xff);
		break;
	case 6:
		reg->val = afe_read(sd, reg->reg & 0xff);
		break;
	case 7:
		reg->val = rep_read(sd, reg->reg & 0xff);
		break;
	case 8:
		reg->val = edid_read(sd, reg->reg & 0xff);
		break;
	case 9:
		reg->val = hdmi_read(sd, reg->reg & 0xff);
		break;
	case 0xa:
		reg->val = cp_read(sd, reg->reg & 0xff);
		break;
	case 0xb:
		reg->val = vdp_read(sd, reg->reg & 0xff);
		break;
	default:
		v4l2_info(sd, "Register %03llx not supported\n", reg->reg);
		adv7842_inv_register(sd);
		break;
	}
	return 0;
}

static int adv7842_s_register(struct v4l2_subdev *sd,
		const struct v4l2_dbg_register *reg)
{
	u8 val = reg->val & 0xff;

	switch (reg->reg >> 8) {
	case 0:
		io_write(sd, reg->reg & 0xff, val);
		break;
	case 1:
		avlink_write(sd, reg->reg & 0xff, val);
		break;
	case 2:
		cec_write(sd, reg->reg & 0xff, val);
		break;
	case 3:
		infoframe_write(sd, reg->reg & 0xff, val);
		break;
	case 4:
		sdp_io_write(sd, reg->reg & 0xff, val);
		break;
	case 5:
		sdp_write(sd, reg->reg & 0xff, val);
		break;
	case 6:
		afe_write(sd, reg->reg & 0xff, val);
		break;
	case 7:
		rep_write(sd, reg->reg & 0xff, val);
		break;
	case 8:
		edid_write(sd, reg->reg & 0xff, val);
		break;
	case 9:
		hdmi_write(sd, reg->reg & 0xff, val);
		break;
	case 0xa:
		cp_write(sd, reg->reg & 0xff, val);
		break;
	case 0xb:
		vdp_write(sd, reg->reg & 0xff, val);
		break;
	default:
		v4l2_info(sd, "Register %03llx not supported\n", reg->reg);
		adv7842_inv_register(sd);
		break;
	}
	return 0;
}
#endif

static int adv7842_s_detect_tx_5v_ctrl(struct v4l2_subdev *sd)
{
	struct adv7842_state *state = to_state(sd);
	int prev = v4l2_ctrl_g_ctrl(state->detect_tx_5v_ctrl);
	u8 reg_io_6f = io_read(sd, 0x6f);
	int val = 0;

	if (reg_io_6f & 0x02)
		val |= 1; /* port A */
	if (reg_io_6f & 0x01)
		val |= 2; /* port B */

	v4l2_dbg(1, debug, sd, "%s: 0x%x -> 0x%x\n", __func__, prev, val);

	if (val != prev)
		return v4l2_ctrl_s_ctrl(state->detect_tx_5v_ctrl, val);
	return 0;
}

static int find_and_set_predefined_video_timings(struct v4l2_subdev *sd,
		u8 prim_mode,
		const struct adv7842_video_standards *predef_vid_timings,
		const struct v4l2_dv_timings *timings)
{
	int i;

	for (i = 0; predef_vid_timings[i].timings.bt.width; i++) {
		if (!v4l2_match_dv_timings(timings, &predef_vid_timings[i].timings,
					  is_digital_input(sd) ? 250000 : 1000000))
			continue;
		/* video std */
		io_write(sd, 0x00, predef_vid_timings[i].vid_std);
		/* v_freq and prim mode */
		io_write(sd, 0x01, (predef_vid_timings[i].v_freq << 4) + prim_mode);
		return 0;
	}

	return -1;
}

static int configure_predefined_video_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct adv7842_state *state = to_state(sd);
	int err;

	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	/* reset to default values */
	io_write(sd, 0x16, 0x43);
	io_write(sd, 0x17, 0x5a);
	/* disable embedded syncs for auto graphics mode */
	cp_write_and_or(sd, 0x81, 0xef, 0x00);
	cp_write(sd, 0x26, 0x00);
	cp_write(sd, 0x27, 0x00);
	cp_write(sd, 0x28, 0x00);
	cp_write(sd, 0x29, 0x00);
	cp_write(sd, 0x8f, 0x00);
	cp_write(sd, 0x90, 0x00);
	cp_write(sd, 0xa5, 0x00);
	cp_write(sd, 0xa6, 0x00);
	cp_write(sd, 0xa7, 0x00);
	cp_write(sd, 0xab, 0x00);
	cp_write(sd, 0xac, 0x00);

	switch (state->mode) {
	case ADV7842_MODE_COMP:
	case ADV7842_MODE_RGB:
		err = find_and_set_predefined_video_timings(sd,
				0x01, adv7842_prim_mode_comp, timings);
		if (err)
			err = find_and_set_predefined_video_timings(sd,
					0x02, adv7842_prim_mode_gr, timings);
		break;
	case ADV7842_MODE_HDMI:
		err = find_and_set_predefined_video_timings(sd,
				0x05, adv7842_prim_mode_hdmi_comp, timings);
		if (err)
			err = find_and_set_predefined_video_timings(sd,
					0x06, adv7842_prim_mode_hdmi_gr, timings);
		break;
	default:
		v4l2_dbg(2, debug, sd, "%s: Unknown mode %d\n",
				__func__, state->mode);
		err = -1;
		break;
	}


	return err;
}

static void configure_custom_video_timings(struct v4l2_subdev *sd,
		const struct v4l2_bt_timings *bt)
{
	struct adv7842_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u32 width = htotal(bt);
	u32 height = vtotal(bt);
	u16 cp_start_sav = bt->hsync + bt->hbackporch - 4;
	u16 cp_start_eav = width - bt->hfrontporch;
	u16 cp_start_vbi = height - bt->vfrontporch + 1;
	u16 cp_end_vbi = bt->vsync + bt->vbackporch + 1;
	u16 ch1_fr_ll = (((u32)bt->pixelclock / 100) > 0) ?
		((width * (ADV7842_fsc / 100)) / ((u32)bt->pixelclock / 100)) : 0;
	const u8 pll[2] = {
		0xc0 | ((width >> 8) & 0x1f),
		width & 0xff
	};

	v4l2_dbg(2, debug, sd, "%s\n", __func__);

	switch (state->mode) {
	case ADV7842_MODE_COMP:
	case ADV7842_MODE_RGB:
		/* auto graphics */
		io_write(sd, 0x00, 0x07); /* video std */
		io_write(sd, 0x01, 0x02); /* prim mode */
		/* enable embedded syncs for auto graphics mode */
		cp_write_and_or(sd, 0x81, 0xef, 0x10);

		/* Should only be set in auto-graphics mode [REF_02, p. 91-92] */
		/* setup PLL_DIV_MAN_EN and PLL_DIV_RATIO */
		/* IO-map reg. 0x16 and 0x17 should be written in sequence */
		if (adv_smbus_write_i2c_block_data(client, 0x16, 2, pll)) {
			v4l2_err(sd, "writing to reg 0x16 and 0x17 failed\n");
			break;
		}

		/* active video - horizontal timing */
		cp_write(sd, 0x26, (cp_start_sav >> 8) & 0xf);
		cp_write(sd, 0x27, (cp_start_sav & 0xff));
		cp_write(sd, 0x28, (cp_start_eav >> 8) & 0xf);
		cp_write(sd, 0x29, (cp_start_eav & 0xff));

		/* active video - vertical timing */
		cp_write(sd, 0xa5, (cp_start_vbi >> 4) & 0xff);
		cp_write(sd, 0xa6, ((cp_start_vbi & 0xf) << 4) |
					((cp_end_vbi >> 8) & 0xf));
		cp_write(sd, 0xa7, cp_end_vbi & 0xff);
		break;
	case ADV7842_MODE_HDMI:
		/* set default prim_mode/vid_std for HDMI
		   accoring to [REF_03, c. 4.2] */
		io_write(sd, 0x00, 0x02); /* video std */
		io_write(sd, 0x01, 0x06); /* prim mode */
		break;
	default:
		v4l2_dbg(2, debug, sd, "%s: Unknown mode %d\n",
				__func__, state->mode);
		break;
	}

	cp_write(sd, 0x8f, (ch1_fr_ll >> 8) & 0x7);
	cp_write(sd, 0x90, ch1_fr_ll & 0xff);
	cp_write(sd, 0xab, (height >> 4) & 0xff);
	cp_write(sd, 0xac, (height & 0x0f) << 4);
}

static void set_rgb_quantization_range(struct v4l2_subdev *sd)
{
	struct adv7842_state *state = to_state(sd);

	switch (state->rgb_quantization_range) {
	case V4L2_DV_RGB_RANGE_AUTO:
		/* automatic */
		if (is_digital_input(sd) && !(hdmi_read(sd, 0x05) & 0x80)) {
			/* receiving DVI-D signal */

			/* ADV7842 selects RGB limited range regardless of
			   input format (CE/IT) in automatic mode */
			if (state->timings.bt.standards & V4L2_DV_BT_STD_CEA861) {
				/* RGB limited range (16-235) */
				io_write_and_or(sd, 0x02, 0x0f, 0x00);

			} else {
				/* RGB full range (0-255) */
				io_write_and_or(sd, 0x02, 0x0f, 0x10);
			}
		} else {
			/* receiving HDMI or analog signal, set automode */
			io_write_and_or(sd, 0x02, 0x0f, 0xf0);
		}
		break;
	case V4L2_DV_RGB_RANGE_LIMITED:
		/* RGB limited range (16-235) */
		io_write_and_or(sd, 0x02, 0x0f, 0x00);
		break;
	case V4L2_DV_RGB_RANGE_FULL:
		/* RGB full range (0-255) */
		io_write_and_or(sd, 0x02, 0x0f, 0x10);
		break;
	}
}

static int adv7842_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct adv7842_state *state = to_state(sd);

	/* TODO SDP ctrls
	   contrast/brightness/hue/free run is acting a bit strange,
	   not sure if sdp csc is correct.
	 */
	switch (ctrl->id) {
	/* standard ctrls */
	case V4L2_CID_BRIGHTNESS:
		cp_write(sd, 0x3c, ctrl->val);
		sdp_write(sd, 0x14, ctrl->val);
		/* ignore lsb sdp 0x17[3:2] */
		return 0;
	case V4L2_CID_CONTRAST:
		cp_write(sd, 0x3a, ctrl->val);
		sdp_write(sd, 0x13, ctrl->val);
		/* ignore lsb sdp 0x17[1:0] */
		return 0;
	case V4L2_CID_SATURATION:
		cp_write(sd, 0x3b, ctrl->val);
		sdp_write(sd, 0x15, ctrl->val);
		/* ignore lsb sdp 0x17[5:4] */
		return 0;
	case V4L2_CID_HUE:
		cp_write(sd, 0x3d, ctrl->val);
		sdp_write(sd, 0x16, ctrl->val);
		/* ignore lsb sdp 0x17[7:6] */
		return 0;
		/* custom ctrls */
	case V4L2_CID_ADV_RX_ANALOG_SAMPLING_PHASE:
		afe_write(sd, 0xc8, ctrl->val);
		return 0;
	case V4L2_CID_ADV_RX_FREE_RUN_COLOR_MANUAL:
		cp_write_and_or(sd, 0xbf, ~0x04, (ctrl->val << 2));
		sdp_write_and_or(sd, 0xdd, ~0x04, (ctrl->val << 2));
		return 0;
	case V4L2_CID_ADV_RX_FREE_RUN_COLOR: {
		u8 R = (ctrl->val & 0xff0000) >> 16;
		u8 G = (ctrl->val & 0x00ff00) >> 8;
		u8 B = (ctrl->val & 0x0000ff);
		/* RGB -> YUV, numerical approximation */
		int Y = 66 * R + 129 * G + 25 * B;
		int U = -38 * R - 74 * G + 112 * B;
		int V = 112 * R - 94 * G - 18 * B;

		/* Scale down to 8 bits with rounding */
		Y = (Y + 128) >> 8;
		U = (U + 128) >> 8;
		V = (V + 128) >> 8;
		/* make U,V positive */
		Y += 16;
		U += 128;
		V += 128;

		v4l2_dbg(1, debug, sd, "R %x, G %x, B %x\n", R, G, B);
		v4l2_dbg(1, debug, sd, "Y %x, U %x, V %x\n", Y, U, V);

		/* CP */
		cp_write(sd, 0xc1, R);
		cp_write(sd, 0xc0, G);
		cp_write(sd, 0xc2, B);
		/* SDP */
		sdp_write(sd, 0xde, Y);
		sdp_write(sd, 0xdf, (V & 0xf0) | ((U >> 4) & 0x0f));
		return 0;
	}
	case V4L2_CID_DV_RX_RGB_RANGE:
		state->rgb_quantization_range = ctrl->val;
		set_rgb_quantization_range(sd);
		return 0;
	}
	return -EINVAL;
}

static inline bool no_power(struct v4l2_subdev *sd)
{
	return io_read(sd, 0x0c) & 0x24;
}

static inline bool no_cp_signal(struct v4l2_subdev *sd)
{
	return ((cp_read(sd, 0xb5) & 0xd0) != 0xd0) || !(cp_read(sd, 0xb1) & 0x80);
}

static inline bool is_hdmi(struct v4l2_subdev *sd)
{
	return hdmi_read(sd, 0x05) & 0x80;
}

static int adv7842_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct adv7842_state *state = to_state(sd);

	*status = 0;

	if (io_read(sd, 0x0c) & 0x24)
		*status |= V4L2_IN_ST_NO_POWER;

	if (state->mode == ADV7842_MODE_SDP) {
		/* status from SDP block */
		if (!(sdp_read(sd, 0x5A) & 0x01))
			*status |= V4L2_IN_ST_NO_SIGNAL;

		v4l2_dbg(1, debug, sd, "%s: SDP status = 0x%x\n",
				__func__, *status);
		return 0;
	}
	/* status from CP block */
	if ((cp_read(sd, 0xb5) & 0xd0) != 0xd0 ||
			!(cp_read(sd, 0xb1) & 0x80))
		/* TODO channel 2 */
		*status |= V4L2_IN_ST_NO_SIGNAL;

	if (is_digital_input(sd) && ((io_read(sd, 0x74) & 0x03) != 0x03))
		*status |= V4L2_IN_ST_NO_SIGNAL;

	v4l2_dbg(1, debug, sd, "%s: CP status = 0x%x\n",
			__func__, *status);

	return 0;
}

struct stdi_readback {
	u16 bl, lcf, lcvs;
	u8 hs_pol, vs_pol;
	bool interlaced;
};

static int stdi2dv_timings(struct v4l2_subdev *sd,
		struct stdi_readback *stdi,
		struct v4l2_dv_timings *timings)
{
	struct adv7842_state *state = to_state(sd);
	u32 hfreq = (ADV7842_fsc * 8) / stdi->bl;
	u32 pix_clk;
	int i;

	for (i = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		const struct v4l2_bt_timings *bt = &v4l2_dv_timings_presets[i].bt;

		if (!v4l2_valid_dv_timings(&v4l2_dv_timings_presets[i],
					   adv7842_get_dv_timings_cap(sd),
					   adv7842_check_dv_timings, NULL))
			continue;
		if (vtotal(bt) != stdi->lcf + 1)
			continue;
		if (bt->vsync != stdi->lcvs)
			continue;

		pix_clk = hfreq * htotal(bt);

		if ((pix_clk < bt->pixelclock + 1000000) &&
		    (pix_clk > bt->pixelclock - 1000000)) {
			*timings = v4l2_dv_timings_presets[i];
			return 0;
		}
	}

	if (v4l2_detect_cvt(stdi->lcf + 1, hfreq, stdi->lcvs,
			(stdi->hs_pol == '+' ? V4L2_DV_HSYNC_POS_POL : 0) |
			(stdi->vs_pol == '+' ? V4L2_DV_VSYNC_POS_POL : 0),
			    timings))
		return 0;
	if (v4l2_detect_gtf(stdi->lcf + 1, hfreq, stdi->lcvs,
			(stdi->hs_pol == '+' ? V4L2_DV_HSYNC_POS_POL : 0) |
			(stdi->vs_pol == '+' ? V4L2_DV_VSYNC_POS_POL : 0),
			    state->aspect_ratio, timings))
		return 0;

	v4l2_dbg(2, debug, sd,
		"%s: No format candidate found for lcvs = %d, lcf=%d, bl = %d, %chsync, %cvsync\n",
		__func__, stdi->lcvs, stdi->lcf, stdi->bl,
		stdi->hs_pol, stdi->vs_pol);
	return -1;
}

static int read_stdi(struct v4l2_subdev *sd, struct stdi_readback *stdi)
{
	u32 status;

	adv7842_g_input_status(sd, &status);
	if (status & V4L2_IN_ST_NO_SIGNAL) {
		v4l2_dbg(2, debug, sd, "%s: no signal\n", __func__);
		return -ENOLINK;
	}

	stdi->bl = ((cp_read(sd, 0xb1) & 0x3f) << 8) | cp_read(sd, 0xb2);
	stdi->lcf = ((cp_read(sd, 0xb3) & 0x7) << 8) | cp_read(sd, 0xb4);
	stdi->lcvs = cp_read(sd, 0xb3) >> 3;

	if ((cp_read(sd, 0xb5) & 0x80) && ((cp_read(sd, 0xb5) & 0x03) == 0x01)) {
		stdi->hs_pol = ((cp_read(sd, 0xb5) & 0x10) ?
			((cp_read(sd, 0xb5) & 0x08) ? '+' : '-') : 'x');
		stdi->vs_pol = ((cp_read(sd, 0xb5) & 0x40) ?
			((cp_read(sd, 0xb5) & 0x20) ? '+' : '-') : 'x');
	} else {
		stdi->hs_pol = 'x';
		stdi->vs_pol = 'x';
	}
	stdi->interlaced = (cp_read(sd, 0xb1) & 0x40) ? true : false;

	if (stdi->lcf < 239 || stdi->bl < 8 || stdi->bl == 0x3fff) {
		v4l2_dbg(2, debug, sd, "%s: invalid signal\n", __func__);
		return -ENOLINK;
	}

	v4l2_dbg(2, debug, sd,
		"%s: lcf (frame height - 1) = %d, bl = %d, lcvs (vsync) = %d, %chsync, %cvsync, %s\n",
		 __func__, stdi->lcf, stdi->bl, stdi->lcvs,
		 stdi->hs_pol, stdi->vs_pol,
		 stdi->interlaced ? "interlaced" : "progressive");

	return 0;
}

static int adv7842_enum_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings,
		adv7842_get_dv_timings_cap(sd), adv7842_check_dv_timings, NULL);
}

static int adv7842_dv_timings_cap(struct v4l2_subdev *sd,
				  struct v4l2_dv_timings_cap *cap)
{
	*cap = *adv7842_get_dv_timings_cap(sd);
	return 0;
}

/* Fill the optional fields .standards and .flags in struct v4l2_dv_timings
   if the format is listed in adv7604_timings[] */
static void adv7842_fill_optional_dv_timings_fields(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	v4l2_find_dv_timings_cap(timings, adv7842_get_dv_timings_cap(sd),
			is_digital_input(sd) ? 250000 : 1000000,
			adv7842_check_dv_timings, NULL);
}

static int adv7842_query_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_dv_timings *timings)
{
	struct adv7842_state *state = to_state(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	struct stdi_readback stdi = { 0 };

	/* SDP block */
	if (state->mode == ADV7842_MODE_SDP)
		return -ENODATA;

	/* read STDI */
	if (read_stdi(sd, &stdi)) {
		v4l2_dbg(1, debug, sd, "%s: no valid signal\n", __func__);
		return -ENOLINK;
	}
	bt->interlaced = stdi.interlaced ?
		V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;
	bt->polarities = ((hdmi_read(sd, 0x05) & 0x10) ? V4L2_DV_VSYNC_POS_POL : 0) |
		((hdmi_read(sd, 0x05) & 0x20) ? V4L2_DV_HSYNC_POS_POL : 0);
	bt->vsync = stdi.lcvs;

	if (is_digital_input(sd)) {
		bool lock = hdmi_read(sd, 0x04) & 0x02;
		bool interlaced = hdmi_read(sd, 0x0b) & 0x20;
		unsigned w = (hdmi_read(sd, 0x07) & 0x1f) * 256 + hdmi_read(sd, 0x08);
		unsigned h = (hdmi_read(sd, 0x09) & 0x1f) * 256 + hdmi_read(sd, 0x0a);
		unsigned w_total = (hdmi_read(sd, 0x1e) & 0x3f) * 256 +
			hdmi_read(sd, 0x1f);
		unsigned h_total = ((hdmi_read(sd, 0x26) & 0x3f) * 256 +
				    hdmi_read(sd, 0x27)) / 2;
		unsigned freq = (((hdmi_read(sd, 0x51) << 1) +
					(hdmi_read(sd, 0x52) >> 7)) * 1000000) +
			((hdmi_read(sd, 0x52) & 0x7f) * 1000000) / 128;
		int i;

		if (is_hdmi(sd)) {
			/* adjust for deep color mode */
			freq = freq * 8 / (((hdmi_read(sd, 0x0b) & 0xc0)>>6) * 2 + 8);
		}

		/* No lock? */
		if (!lock) {
			v4l2_dbg(1, debug, sd, "%s: no lock on TMDS signal\n", __func__);
			return -ENOLCK;
		}
		/* Interlaced? */
		if (interlaced) {
			v4l2_dbg(1, debug, sd, "%s: interlaced video not supported\n", __func__);
			return -ERANGE;
		}

		for (i = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
			const struct v4l2_bt_timings *bt = &v4l2_dv_timings_presets[i].bt;

			if (!v4l2_valid_dv_timings(&v4l2_dv_timings_presets[i],
						   adv7842_get_dv_timings_cap(sd),
						   adv7842_check_dv_timings, NULL))
				continue;
			if (w_total != htotal(bt) || h_total != vtotal(bt))
				continue;

			if (w != bt->width || h != bt->height)
				continue;

			if (abs(freq - bt->pixelclock) > 1000000)
				continue;
			*timings = v4l2_dv_timings_presets[i];
			return 0;
		}

		timings->type = V4L2_DV_BT_656_1120;

		bt->width = w;
		bt->height = h;
		bt->interlaced = (hdmi_read(sd, 0x0b) & 0x20) ?
			V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;
		bt->polarities = ((hdmi_read(sd, 0x05) & 0x10) ?
			V4L2_DV_VSYNC_POS_POL : 0) | ((hdmi_read(sd, 0x05) & 0x20) ?
			V4L2_DV_HSYNC_POS_POL : 0);
		bt->pixelclock = (((hdmi_read(sd, 0x51) << 1) +
				   (hdmi_read(sd, 0x52) >> 7)) * 1000000) +
				 ((hdmi_read(sd, 0x52) & 0x7f) * 1000000) / 128;
		bt->hfrontporch = (hdmi_read(sd, 0x20) & 0x1f) * 256 +
			hdmi_read(sd, 0x21);
		bt->hsync = (hdmi_read(sd, 0x22) & 0x1f) * 256 +
			hdmi_read(sd, 0x23);
		bt->hbackporch = (hdmi_read(sd, 0x24) & 0x1f) * 256 +
			hdmi_read(sd, 0x25);
		bt->vfrontporch = ((hdmi_read(sd, 0x2a) & 0x3f) * 256 +
				   hdmi_read(sd, 0x2b)) / 2;
		bt->il_vfrontporch = ((hdmi_read(sd, 0x2c) & 0x3f) * 256 +
				      hdmi_read(sd, 0x2d)) / 2;
		bt->vsync = ((hdmi_read(sd, 0x2e) & 0x3f) * 256 +
			     hdmi_read(sd, 0x2f)) / 2;
		bt->il_vsync = ((hdmi_read(sd, 0x30) & 0x3f) * 256 +
				hdmi_read(sd, 0x31)) / 2;
		bt->vbackporch = ((hdmi_read(sd, 0x32) & 0x3f) * 256 +
				  hdmi_read(sd, 0x33)) / 2;
		bt->il_vbackporch = ((hdmi_read(sd, 0x34) & 0x3f) * 256 +
				     hdmi_read(sd, 0x35)) / 2;

		bt->standards = 0;
		bt->flags = 0;
	} else {
		/* Interlaced? */
		if (stdi.interlaced) {
			v4l2_dbg(1, debug, sd, "%s: interlaced video not supported\n", __func__);
			return -ERANGE;
		}

		if (stdi2dv_timings(sd, &stdi, timings)) {
			v4l2_dbg(1, debug, sd, "%s: format not supported\n", __func__);
			return -ERANGE;
		}
	}

	if (debug > 1)
		v4l2_print_dv_timings(sd->name, "adv7842_query_dv_timings: ",
				      timings, true);
	return 0;
}

static int adv7842_s_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct adv7842_state *state = to_state(sd);
	struct v4l2_bt_timings *bt;
	int err;

	if (state->mode == ADV7842_MODE_SDP)
		return -ENODATA;

	bt = &timings->bt;

	if (!v4l2_valid_dv_timings(timings, adv7842_get_dv_timings_cap(sd),
				   adv7842_check_dv_timings, NULL))
		return -ERANGE;

	adv7842_fill_optional_dv_timings_fields(sd, timings);

	state->timings = *timings;

	cp_write(sd, 0x91, bt->interlaced ? 0x50 : 0x10);

	/* Use prim_mode and vid_std when available */
	err = configure_predefined_video_timings(sd, timings);
	if (err) {
		/* custom settings when the video format
		  does not have prim_mode/vid_std */
		configure_custom_video_timings(sd, bt);
	}

	set_rgb_quantization_range(sd);


	if (debug > 1)
		v4l2_print_dv_timings(sd->name, "adv7842_s_dv_timings: ",
				      timings, true);
	return 0;
}

static int adv7842_g_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct adv7842_state *state = to_state(sd);

	if (state->mode == ADV7842_MODE_SDP)
		return -ENODATA;
	*timings = state->timings;
	return 0;
}

static void enable_input(struct v4l2_subdev *sd)
{
	struct adv7842_state *state = to_state(sd);
	switch (state->mode) {
	case ADV7842_MODE_SDP:
	case ADV7842_MODE_COMP:
	case ADV7842_MODE_RGB:
		/* enable */
		io_write(sd, 0x15, 0xb0);   /* Disable Tristate of Pins (no audio) */
		break;
	case ADV7842_MODE_HDMI:
		/* enable */
		hdmi_write(sd, 0x1a, 0x0a); /* Unmute audio */
		hdmi_write(sd, 0x01, 0x00); /* Enable HDMI clock terminators */
		io_write(sd, 0x15, 0xa0);   /* Disable Tristate of Pins */
		break;
	default:
		v4l2_dbg(2, debug, sd, "%s: Unknown mode %d\n",
			 __func__, state->mode);
		break;
	}
}

static void disable_input(struct v4l2_subdev *sd)
{
	/* disable */
	io_write(sd, 0x15, 0xbe);   /* Tristate all outputs from video core */
	hdmi_write(sd, 0x1a, 0x1a); /* Mute audio */
	hdmi_write(sd, 0x01, 0x78); /* Disable HDMI clock terminators */
}

static void sdp_csc_coeff(struct v4l2_subdev *sd,
			  const struct adv7842_sdp_csc_coeff *c)
{
	/* csc auto/manual */
	sdp_io_write_and_or(sd, 0xe0, 0xbf, c->manual ? 0x00 : 0x40);

	if (!c->manual)
		return;

	/* csc scaling */
	sdp_io_write_and_or(sd, 0xe0, 0x7f, c->scaling == 2 ? 0x80 : 0x00);

	/* A coeff */
	sdp_io_write_and_or(sd, 0xe0, 0xe0, c->A1 >> 8);
	sdp_io_write(sd, 0xe1, c->A1);
	sdp_io_write_and_or(sd, 0xe2, 0xe0, c->A2 >> 8);
	sdp_io_write(sd, 0xe3, c->A2);
	sdp_io_write_and_or(sd, 0xe4, 0xe0, c->A3 >> 8);
	sdp_io_write(sd, 0xe5, c->A3);

	/* A scale */
	sdp_io_write_and_or(sd, 0xe6, 0x80, c->A4 >> 8);
	sdp_io_write(sd, 0xe7, c->A4);

	/* B coeff */
	sdp_io_write_and_or(sd, 0xe8, 0xe0, c->B1 >> 8);
	sdp_io_write(sd, 0xe9, c->B1);
	sdp_io_write_and_or(sd, 0xea, 0xe0, c->B2 >> 8);
	sdp_io_write(sd, 0xeb, c->B2);
	sdp_io_write_and_or(sd, 0xec, 0xe0, c->B3 >> 8);
	sdp_io_write(sd, 0xed, c->B3);

	/* B scale */
	sdp_io_write_and_or(sd, 0xee, 0x80, c->B4 >> 8);
	sdp_io_write(sd, 0xef, c->B4);

	/* C coeff */
	sdp_io_write_and_or(sd, 0xf0, 0xe0, c->C1 >> 8);
	sdp_io_write(sd, 0xf1, c->C1);
	sdp_io_write_and_or(sd, 0xf2, 0xe0, c->C2 >> 8);
	sdp_io_write(sd, 0xf3, c->C2);
	sdp_io_write_and_or(sd, 0xf4, 0xe0, c->C3 >> 8);
	sdp_io_write(sd, 0xf5, c->C3);

	/* C scale */
	sdp_io_write_and_or(sd, 0xf6, 0x80, c->C4 >> 8);
	sdp_io_write(sd, 0xf7, c->C4);
}

static void select_input(struct v4l2_subdev *sd,
			 enum adv7842_vid_std_select vid_std_select)
{
	struct adv7842_state *state = to_state(sd);

	switch (state->mode) {
	case ADV7842_MODE_SDP:
		io_write(sd, 0x00, vid_std_select); /* video std: CVBS or YC mode */
		io_write(sd, 0x01, 0); /* prim mode */
		/* enable embedded syncs for auto graphics mode */
		cp_write_and_or(sd, 0x81, 0xef, 0x10);

		afe_write(sd, 0x00, 0x00); /* power up ADC */
		afe_write(sd, 0xc8, 0x00); /* phase control */

		io_write(sd, 0x19, 0x83); /* LLC DLL phase */
		io_write(sd, 0x33, 0x40); /* LLC DLL enable */

		io_write(sd, 0xdd, 0x90); /* Manual 2x output clock */
		/* script says register 0xde, which don't exist in manual */

		/* Manual analog input muxing mode, CVBS (6.4)*/
		afe_write_and_or(sd, 0x02, 0x7f, 0x80);
		if (vid_std_select == ADV7842_SDP_VID_STD_CVBS_SD_4x1) {
			afe_write(sd, 0x03, 0xa0); /* ADC0 to AIN10 (CVBS), ADC1 N/C*/
			afe_write(sd, 0x04, 0x00); /* ADC2 N/C,ADC3 N/C*/
		} else {
			afe_write(sd, 0x03, 0xa0); /* ADC0 to AIN10 (CVBS), ADC1 N/C*/
			afe_write(sd, 0x04, 0xc0); /* ADC2 to AIN12, ADC3 N/C*/
		}
		afe_write(sd, 0x0c, 0x1f); /* ADI recommend write */
		afe_write(sd, 0x12, 0x63); /* ADI recommend write */

		sdp_io_write(sd, 0xb2, 0x60); /* Disable AV codes */
		sdp_io_write(sd, 0xc8, 0xe3); /* Disable Ancillary data */

		/* SDP recommended settings */
		sdp_write(sd, 0x00, 0x3F); /* Autodetect PAL NTSC (not SECAM) */
		sdp_write(sd, 0x01, 0x00); /* Pedestal Off */

		sdp_write(sd, 0x03, 0xE4); /* Manual VCR Gain Luma 0x40B */
		sdp_write(sd, 0x04, 0x0B); /* Manual Luma setting */
		sdp_write(sd, 0x05, 0xC3); /* Manual Chroma setting 0x3FE */
		sdp_write(sd, 0x06, 0xFE); /* Manual Chroma setting */
		sdp_write(sd, 0x12, 0x0D); /* Frame TBC,I_P, 3D comb enabled */
		sdp_write(sd, 0xA7, 0x00); /* ADI Recommended Write */
		sdp_io_write(sd, 0xB0, 0x00); /* Disable H and v blanking */

		/* deinterlacer enabled and 3D comb */
		sdp_write_and_or(sd, 0x12, 0xf6, 0x09);

		sdp_write(sd, 0xdd, 0x08); /* free run auto */

		break;

	case ADV7842_MODE_COMP:
	case ADV7842_MODE_RGB:
		/* Automatic analog input muxing mode */
		afe_write_and_or(sd, 0x02, 0x7f, 0x00);
		/* set mode and select free run resolution */
		io_write(sd, 0x00, vid_std_select); /* video std */
		io_write(sd, 0x01, 0x02); /* prim mode */
		cp_write_and_or(sd, 0x81, 0xef, 0x10); /* enable embedded syncs
							  for auto graphics mode */

		afe_write(sd, 0x00, 0x00); /* power up ADC */
		afe_write(sd, 0xc8, 0x00); /* phase control */

		/* set ADI recommended settings for digitizer */
		/* "ADV7842 Register Settings Recommendations
		 * (rev. 1.8, November 2010)" p. 9. */
		afe_write(sd, 0x0c, 0x1f); /* ADC Range improvement */
		afe_write(sd, 0x12, 0x63); /* ADC Range improvement */

		/* set to default gain for RGB */
		cp_write(sd, 0x73, 0x10);
		cp_write(sd, 0x74, 0x04);
		cp_write(sd, 0x75, 0x01);
		cp_write(sd, 0x76, 0x00);

		cp_write(sd, 0x3e, 0x04); /* CP core pre-gain control */
		cp_write(sd, 0xc3, 0x39); /* CP coast control. Graphics mode */
		cp_write(sd, 0x40, 0x5c); /* CP core pre-gain control. Graphics mode */
		break;

	case ADV7842_MODE_HDMI:
		/* Automatic analog input muxing mode */
		afe_write_and_or(sd, 0x02, 0x7f, 0x00);
		/* set mode and select free run resolution */
		if (state->hdmi_port_a)
			hdmi_write(sd, 0x00, 0x02); /* select port A */
		else
			hdmi_write(sd, 0x00, 0x03); /* select port B */
		io_write(sd, 0x00, vid_std_select); /* video std */
		io_write(sd, 0x01, 5); /* prim mode */
		cp_write_and_or(sd, 0x81, 0xef, 0x00); /* disable embedded syncs
							  for auto graphics mode */

		/* set ADI recommended settings for HDMI: */
		/* "ADV7842 Register Settings Recommendations
		 * (rev. 1.8, November 2010)" p. 3. */
		hdmi_write(sd, 0xc0, 0x00);
		hdmi_write(sd, 0x0d, 0x34); /* ADI recommended write */
		hdmi_write(sd, 0x3d, 0x10); /* ADI recommended write */
		hdmi_write(sd, 0x44, 0x85); /* TMDS PLL optimization */
		hdmi_write(sd, 0x46, 0x1f); /* ADI recommended write */
		hdmi_write(sd, 0x57, 0xb6); /* TMDS PLL optimization */
		hdmi_write(sd, 0x58, 0x03); /* TMDS PLL optimization */
		hdmi_write(sd, 0x60, 0x88); /* TMDS PLL optimization */
		hdmi_write(sd, 0x61, 0x88); /* TMDS PLL optimization */
		hdmi_write(sd, 0x6c, 0x18); /* Disable ISRC clearing bit,
					       Improve robustness */
		hdmi_write(sd, 0x75, 0x10); /* DDC drive strength */
		hdmi_write(sd, 0x85, 0x1f); /* equaliser */
		hdmi_write(sd, 0x87, 0x70); /* ADI recommended write */
		hdmi_write(sd, 0x89, 0x04); /* equaliser */
		hdmi_write(sd, 0x8a, 0x1e); /* equaliser */
		hdmi_write(sd, 0x93, 0x04); /* equaliser */
		hdmi_write(sd, 0x94, 0x1e); /* equaliser */
		hdmi_write(sd, 0x99, 0xa1); /* ADI recommended write */
		hdmi_write(sd, 0x9b, 0x09); /* ADI recommended write */
		hdmi_write(sd, 0x9d, 0x02); /* equaliser */

		afe_write(sd, 0x00, 0xff); /* power down ADC */
		afe_write(sd, 0xc8, 0x40); /* phase control */

		/* set to default gain for HDMI */
		cp_write(sd, 0x73, 0x10);
		cp_write(sd, 0x74, 0x04);
		cp_write(sd, 0x75, 0x01);
		cp_write(sd, 0x76, 0x00);

		/* reset ADI recommended settings for digitizer */
		/* "ADV7842 Register Settings Recommendations
		 * (rev. 2.5, June 2010)" p. 17. */
		afe_write(sd, 0x12, 0xfb); /* ADC noise shaping filter controls */
		afe_write(sd, 0x0c, 0x0d); /* CP core gain controls */
		cp_write(sd, 0x3e, 0x80); /* CP core pre-gain control,
					     enable color control */
		/* CP coast control */
		cp_write(sd, 0xc3, 0x33); /* Component mode */

		/* color space conversion, autodetect color space */
		io_write_and_or(sd, 0x02, 0x0f, 0xf0);
		break;

	default:
		v4l2_dbg(2, debug, sd, "%s: Unknown mode %d\n",
			 __func__, state->mode);
		break;
	}
}

static int adv7842_s_routing(struct v4l2_subdev *sd,
		u32 input, u32 output, u32 config)
{
	struct adv7842_state *state = to_state(sd);

	v4l2_dbg(2, debug, sd, "%s: input %d\n", __func__, input);

	switch (input) {
	case ADV7842_SELECT_HDMI_PORT_A:
		/* TODO select HDMI_COMP or HDMI_GR */
		state->mode = ADV7842_MODE_HDMI;
		state->vid_std_select = ADV7842_HDMI_COMP_VID_STD_HD_1250P;
		state->hdmi_port_a = true;
		break;
	case ADV7842_SELECT_HDMI_PORT_B:
		/* TODO select HDMI_COMP or HDMI_GR */
		state->mode = ADV7842_MODE_HDMI;
		state->vid_std_select = ADV7842_HDMI_COMP_VID_STD_HD_1250P;
		state->hdmi_port_a = false;
		break;
	case ADV7842_SELECT_VGA_COMP:
		v4l2_info(sd, "%s: VGA component: todo\n", __func__);
	case ADV7842_SELECT_VGA_RGB:
		state->mode = ADV7842_MODE_RGB;
		state->vid_std_select = ADV7842_RGB_VID_STD_AUTO_GRAPH_MODE;
		break;
	case ADV7842_SELECT_SDP_CVBS:
		state->mode = ADV7842_MODE_SDP;
		state->vid_std_select = ADV7842_SDP_VID_STD_CVBS_SD_4x1;
		break;
	case ADV7842_SELECT_SDP_YC:
		state->mode = ADV7842_MODE_SDP;
		state->vid_std_select = ADV7842_SDP_VID_STD_YC_SD4_x1;
		break;
	default:
		return -EINVAL;
	}

	disable_input(sd);
	select_input(sd, state->vid_std_select);
	enable_input(sd);

	v4l2_subdev_notify(sd, ADV7842_FMT_CHANGE, NULL);

	return 0;
}

static int adv7842_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned int index,
				 enum v4l2_mbus_pixelcode *code)
{
	if (index)
		return -EINVAL;
	/* Good enough for now */
	*code = V4L2_MBUS_FMT_FIXED;
	return 0;
}

static int adv7842_g_mbus_fmt(struct v4l2_subdev *sd,
			      struct v4l2_mbus_framefmt *fmt)
{
	struct adv7842_state *state = to_state(sd);

	fmt->width = state->timings.bt.width;
	fmt->height = state->timings.bt.height;
	fmt->code = V4L2_MBUS_FMT_FIXED;
	fmt->field = V4L2_FIELD_NONE;

	if (state->mode == ADV7842_MODE_SDP) {
		/* SPD block */
		if (!(sdp_read(sd, 0x5A) & 0x01))
			return -EINVAL;
		fmt->width = 720;
		/* valid signal */
		if (state->norm & V4L2_STD_525_60)
			fmt->height = 480;
		else
			fmt->height = 576;
		fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
		return 0;
	}

	if (state->timings.bt.standards & V4L2_DV_BT_STD_CEA861) {
		fmt->colorspace = (state->timings.bt.height <= 576) ?
			V4L2_COLORSPACE_SMPTE170M : V4L2_COLORSPACE_REC709;
	}
	return 0;
}

static void adv7842_irq_enable(struct v4l2_subdev *sd, bool enable)
{
	if (enable) {
		/* Enable SSPD, STDI and CP locked/unlocked interrupts */
		io_write(sd, 0x46, 0x9c);
		/* ESDP_50HZ_DET interrupt */
		io_write(sd, 0x5a, 0x10);
		/* Enable CABLE_DET_A/B_ST (+5v) interrupt */
		io_write(sd, 0x73, 0x03);
		/* Enable V_LOCKED and DE_REGEN_LCK interrupts */
		io_write(sd, 0x78, 0x03);
		/* Enable SDP Standard Detection Change and SDP Video Detected */
		io_write(sd, 0xa0, 0x09);
	} else {
		io_write(sd, 0x46, 0x0);
		io_write(sd, 0x5a, 0x0);
		io_write(sd, 0x73, 0x0);
		io_write(sd, 0x78, 0x0);
		io_write(sd, 0xa0, 0x0);
	}
}

static int adv7842_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	struct adv7842_state *state = to_state(sd);
	u8 fmt_change_cp, fmt_change_digital, fmt_change_sdp;
	u8 irq_status[5];
	u8 irq_cfg = io_read(sd, 0x40);

	/* disable irq-pin output */
	io_write(sd, 0x40, irq_cfg | 0x3);

	/* read status */
	irq_status[0] = io_read(sd, 0x43);
	irq_status[1] = io_read(sd, 0x57);
	irq_status[2] = io_read(sd, 0x70);
	irq_status[3] = io_read(sd, 0x75);
	irq_status[4] = io_read(sd, 0x9d);

	/* and clear */
	if (irq_status[0])
		io_write(sd, 0x44, irq_status[0]);
	if (irq_status[1])
		io_write(sd, 0x58, irq_status[1]);
	if (irq_status[2])
		io_write(sd, 0x71, irq_status[2]);
	if (irq_status[3])
		io_write(sd, 0x76, irq_status[3]);
	if (irq_status[4])
		io_write(sd, 0x9e, irq_status[4]);

	v4l2_dbg(1, debug, sd, "%s: irq %x, %x, %x, %x, %x\n", __func__,
		 irq_status[0], irq_status[1], irq_status[2],
		 irq_status[3], irq_status[4]);

	/* format change CP */
	fmt_change_cp = irq_status[0] & 0x9c;

	/* format change SDP */
	if (state->mode == ADV7842_MODE_SDP)
		fmt_change_sdp = (irq_status[1] & 0x30) | (irq_status[4] & 0x09);
	else
		fmt_change_sdp = 0;

	/* digital format CP */
	if (is_digital_input(sd))
		fmt_change_digital = irq_status[3] & 0x03;
	else
		fmt_change_digital = 0;

	/* notify */
	if (fmt_change_cp || fmt_change_digital || fmt_change_sdp) {
		v4l2_dbg(1, debug, sd,
			 "%s: fmt_change_cp = 0x%x, fmt_change_digital = 0x%x, fmt_change_sdp = 0x%x\n",
			 __func__, fmt_change_cp, fmt_change_digital,
			 fmt_change_sdp);
		v4l2_subdev_notify(sd, ADV7842_FMT_CHANGE, NULL);
	}

	/* 5v cable detect */
	if (irq_status[2])
		adv7842_s_detect_tx_5v_ctrl(sd);

	if (handled)
		*handled = true;

	/* re-enable irq-pin output */
	io_write(sd, 0x40, irq_cfg);

	return 0;
}

static int adv7842_set_edid(struct v4l2_subdev *sd, struct v4l2_subdev_edid *e)
{
	struct adv7842_state *state = to_state(sd);
	int err = 0;

	if (e->pad > 2)
		return -EINVAL;
	if (e->start_block != 0)
		return -EINVAL;
	if (e->blocks > 2)
		return -E2BIG;
	if (!e->edid)
		return -EINVAL;

	/* todo, per edid */
	state->aspect_ratio = v4l2_calc_aspect_ratio(e->edid[0x15],
			e->edid[0x16]);

	if (e->pad == 2) {
		memset(&state->vga_edid.edid, 0, 256);
		state->vga_edid.present = e->blocks ? 0x1 : 0x0;
		memcpy(&state->vga_edid.edid, e->edid, 128 * e->blocks);
		err = edid_write_vga_segment(sd);
	} else {
		u32 mask = 0x1<<e->pad;
		memset(&state->hdmi_edid.edid, 0, 256);
		if (e->blocks)
			state->hdmi_edid.present |= mask;
		else
			state->hdmi_edid.present &= ~mask;
		memcpy(&state->hdmi_edid.edid, e->edid, 128*e->blocks);
		err = edid_write_hdmi_segment(sd, e->pad);
	}
	if (err < 0)
		v4l2_err(sd, "error %d writing edid on port %d\n", err, e->pad);
	return err;
}

/*********** avi info frame CEA-861-E **************/
/* TODO move to common library */

struct avi_info_frame {
	uint8_t f17;
	uint8_t y10;
	uint8_t a0;
	uint8_t b10;
	uint8_t s10;
	uint8_t c10;
	uint8_t m10;
	uint8_t r3210;
	uint8_t itc;
	uint8_t ec210;
	uint8_t q10;
	uint8_t sc10;
	uint8_t f47;
	uint8_t vic;
	uint8_t yq10;
	uint8_t cn10;
	uint8_t pr3210;
	uint16_t etb;
	uint16_t sbb;
	uint16_t elb;
	uint16_t srb;
};

static const char *y10_txt[4] = {
	"RGB",
	"YCbCr 4:2:2",
	"YCbCr 4:4:4",
	"Future",
};

static const char *c10_txt[4] = {
	"No Data",
	"SMPTE 170M",
	"ITU-R 709",
	"Extended Colorimetry information valied",
};

static const char *itc_txt[2] = {
	"No Data",
	"IT content",
};

static const char *ec210_txt[8] = {
	"xvYCC601",
	"xvYCC709",
	"sYCC601",
	"AdobeYCC601",
	"AdobeRGB",
	"5 reserved",
	"6 reserved",
	"7 reserved",
};

static const char *q10_txt[4] = {
	"Default",
	"Limited Range",
	"Full Range",
	"Reserved",
};

static void parse_avi_infoframe(struct v4l2_subdev *sd, uint8_t *buf,
				struct avi_info_frame *avi)
{
	avi->f17 = (buf[1] >> 7) & 0x1;
	avi->y10 = (buf[1] >> 5) & 0x3;
	avi->a0 = (buf[1] >> 4) & 0x1;
	avi->b10 = (buf[1] >> 2) & 0x3;
	avi->s10 = buf[1] & 0x3;
	avi->c10 = (buf[2] >> 6) & 0x3;
	avi->m10 = (buf[2] >> 4) & 0x3;
	avi->r3210 = buf[2] & 0xf;
	avi->itc = (buf[3] >> 7) & 0x1;
	avi->ec210 = (buf[3] >> 4) & 0x7;
	avi->q10 = (buf[3] >> 2) & 0x3;
	avi->sc10 = buf[3] & 0x3;
	avi->f47 = (buf[4] >> 7) & 0x1;
	avi->vic = buf[4] & 0x7f;
	avi->yq10 = (buf[5] >> 6) & 0x3;
	avi->cn10 = (buf[5] >> 4) & 0x3;
	avi->pr3210 = buf[5] & 0xf;
	avi->etb = buf[6] + 256*buf[7];
	avi->sbb = buf[8] + 256*buf[9];
	avi->elb = buf[10] + 256*buf[11];
	avi->srb = buf[12] + 256*buf[13];
}

static void print_avi_infoframe(struct v4l2_subdev *sd)
{
	int i;
	uint8_t buf[14];
	uint8_t avi_inf_len;
	struct avi_info_frame avi;

	if (!(hdmi_read(sd, 0x05) & 0x80)) {
		v4l2_info(sd, "receive DVI-D signal (AVI infoframe not supported)\n");
		return;
	}
	if (!(io_read(sd, 0x60) & 0x01)) {
		v4l2_info(sd, "AVI infoframe not received\n");
		return;
	}

	if (io_read(sd, 0x88) & 0x10) {
		/* Note: the ADV7842 calculated incorrect checksums for InfoFrames
		   with a length of 14 or 15. See the ADV7842 Register Settings
		   Recommendations document for more details. */
		v4l2_info(sd, "AVI infoframe checksum error\n");
		return;
	}

	avi_inf_len = infoframe_read(sd, 0xe2);
	v4l2_info(sd, "AVI infoframe version %d (%d byte)\n",
		  infoframe_read(sd, 0xe1), avi_inf_len);

	if (infoframe_read(sd, 0xe1) != 0x02)
		return;

	for (i = 0; i < 14; i++)
		buf[i] = infoframe_read(sd, i);

	v4l2_info(sd, "\t%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		  buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
		  buf[8], buf[9], buf[10], buf[11], buf[12], buf[13]);

	parse_avi_infoframe(sd, buf, &avi);

	if (avi.vic)
		v4l2_info(sd, "\tVIC: %d\n", avi.vic);
	if (avi.itc)
		v4l2_info(sd, "\t%s\n", itc_txt[avi.itc]);

	if (avi.y10)
		v4l2_info(sd, "\t%s %s\n", y10_txt[avi.y10], !avi.c10 ? "" :
			(avi.c10 == 0x3 ? ec210_txt[avi.ec210] : c10_txt[avi.c10]));
	else
		v4l2_info(sd, "\t%s %s\n", y10_txt[avi.y10], q10_txt[avi.q10]);
}

static const char * const prim_mode_txt[] = {
	"SDP",
	"Component",
	"Graphics",
	"Reserved",
	"CVBS & HDMI AUDIO",
	"HDMI-Comp",
	"HDMI-GR",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
};

static int adv7842_sdp_log_status(struct v4l2_subdev *sd)
{
	/* SDP (Standard definition processor) block */
	uint8_t sdp_signal_detected = sdp_read(sd, 0x5A) & 0x01;

	v4l2_info(sd, "Chip powered %s\n", no_power(sd) ? "off" : "on");
	v4l2_info(sd, "Prim-mode = 0x%x, video std = 0x%x\n",
		  io_read(sd, 0x01) & 0x0f, io_read(sd, 0x00) & 0x3f);

	v4l2_info(sd, "SDP: free run: %s\n",
		(sdp_read(sd, 0x56) & 0x01) ? "on" : "off");
	v4l2_info(sd, "SDP: %s\n", sdp_signal_detected ?
		"valid SD/PR signal detected" : "invalid/no signal");
	if (sdp_signal_detected) {
		static const char * const sdp_std_txt[] = {
			"NTSC-M/J",
			"1?",
			"NTSC-443",
			"60HzSECAM",
			"PAL-M",
			"5?",
			"PAL-60",
			"7?", "8?", "9?", "a?", "b?",
			"PAL-CombN",
			"d?",
			"PAL-BGHID",
			"SECAM"
		};
		v4l2_info(sd, "SDP: standard %s\n",
			sdp_std_txt[sdp_read(sd, 0x52) & 0x0f]);
		v4l2_info(sd, "SDP: %s\n",
			(sdp_read(sd, 0x59) & 0x08) ? "50Hz" : "60Hz");
		v4l2_info(sd, "SDP: %s\n",
			(sdp_read(sd, 0x57) & 0x08) ? "Interlaced" : "Progressive");
		v4l2_info(sd, "SDP: deinterlacer %s\n",
			(sdp_read(sd, 0x12) & 0x08) ? "enabled" : "disabled");
		v4l2_info(sd, "SDP: csc %s mode\n",
			(sdp_io_read(sd, 0xe0) & 0x40) ? "auto" : "manual");
	}
	return 0;
}

static int adv7842_cp_log_status(struct v4l2_subdev *sd)
{
	/* CP block */
	struct adv7842_state *state = to_state(sd);
	struct v4l2_dv_timings timings;
	uint8_t reg_io_0x02 = io_read(sd, 0x02);
	uint8_t reg_io_0x21 = io_read(sd, 0x21);
	uint8_t reg_rep_0x77 = rep_read(sd, 0x77);
	uint8_t reg_rep_0x7d = rep_read(sd, 0x7d);
	bool audio_pll_locked = hdmi_read(sd, 0x04) & 0x01;
	bool audio_sample_packet_detect = hdmi_read(sd, 0x18) & 0x01;
	bool audio_mute = io_read(sd, 0x65) & 0x40;

	static const char * const csc_coeff_sel_rb[16] = {
		"bypassed", "YPbPr601 -> RGB", "reserved", "YPbPr709 -> RGB",
		"reserved", "RGB -> YPbPr601", "reserved", "RGB -> YPbPr709",
		"reserved", "YPbPr709 -> YPbPr601", "YPbPr601 -> YPbPr709",
		"reserved", "reserved", "reserved", "reserved", "manual"
	};
	static const char * const input_color_space_txt[16] = {
		"RGB limited range (16-235)", "RGB full range (0-255)",
		"YCbCr Bt.601 (16-235)", "YCbCr Bt.709 (16-235)",
		"XvYCC Bt.601", "XvYCC Bt.709",
		"YCbCr Bt.601 (0-255)", "YCbCr Bt.709 (0-255)",
		"invalid", "invalid", "invalid", "invalid", "invalid",
		"invalid", "invalid", "automatic"
	};
	static const char * const rgb_quantization_range_txt[] = {
		"Automatic",
		"RGB limited range (16-235)",
		"RGB full range (0-255)",
	};
	static const char * const deep_color_mode_txt[4] = {
		"8-bits per channel",
		"10-bits per channel",
		"12-bits per channel",
		"16-bits per channel (not supported)"
	};

	v4l2_info(sd, "-----Chip status-----\n");
	v4l2_info(sd, "Chip power: %s\n", no_power(sd) ? "off" : "on");
	v4l2_info(sd, "Connector type: %s\n", state->connector_hdmi ?
			"HDMI" : (is_digital_input(sd) ? "DVI-D" : "DVI-A"));
	v4l2_info(sd, "HDMI/DVI-D port selected: %s\n",
			state->hdmi_port_a ? "A" : "B");
	v4l2_info(sd, "EDID A %s, B %s\n",
		  ((reg_rep_0x7d & 0x04) && (reg_rep_0x77 & 0x04)) ?
		  "enabled" : "disabled",
		  ((reg_rep_0x7d & 0x08) && (reg_rep_0x77 & 0x08)) ?
		  "enabled" : "disabled");
	v4l2_info(sd, "HPD A %s, B %s\n",
		  reg_io_0x21 & 0x02 ? "enabled" : "disabled",
		  reg_io_0x21 & 0x01 ? "enabled" : "disabled");
	v4l2_info(sd, "CEC %s\n", !!(cec_read(sd, 0x2a) & 0x01) ?
			"enabled" : "disabled");

	v4l2_info(sd, "-----Signal status-----\n");
	if (state->hdmi_port_a) {
		v4l2_info(sd, "Cable detected (+5V power): %s\n",
			  io_read(sd, 0x6f) & 0x02 ? "true" : "false");
		v4l2_info(sd, "TMDS signal detected: %s\n",
			  (io_read(sd, 0x6a) & 0x02) ? "true" : "false");
		v4l2_info(sd, "TMDS signal locked: %s\n",
			  (io_read(sd, 0x6a) & 0x20) ? "true" : "false");
	} else {
		v4l2_info(sd, "Cable detected (+5V power):%s\n",
			  io_read(sd, 0x6f) & 0x01 ? "true" : "false");
		v4l2_info(sd, "TMDS signal detected: %s\n",
			  (io_read(sd, 0x6a) & 0x01) ? "true" : "false");
		v4l2_info(sd, "TMDS signal locked: %s\n",
			  (io_read(sd, 0x6a) & 0x10) ? "true" : "false");
	}
	v4l2_info(sd, "CP free run: %s\n",
		  (!!(cp_read(sd, 0xff) & 0x10) ? "on" : "off"));
	v4l2_info(sd, "Prim-mode = 0x%x, video std = 0x%x, v_freq = 0x%x\n",
		  io_read(sd, 0x01) & 0x0f, io_read(sd, 0x00) & 0x3f,
		  (io_read(sd, 0x01) & 0x70) >> 4);

	v4l2_info(sd, "-----Video Timings-----\n");
	if (no_cp_signal(sd)) {
		v4l2_info(sd, "STDI: not locked\n");
	} else {
		uint32_t bl = ((cp_read(sd, 0xb1) & 0x3f) << 8) | cp_read(sd, 0xb2);
		uint32_t lcf = ((cp_read(sd, 0xb3) & 0x7) << 8) | cp_read(sd, 0xb4);
		uint32_t lcvs = cp_read(sd, 0xb3) >> 3;
		uint32_t fcl = ((cp_read(sd, 0xb8) & 0x1f) << 8) | cp_read(sd, 0xb9);
		char hs_pol = ((cp_read(sd, 0xb5) & 0x10) ?
				((cp_read(sd, 0xb5) & 0x08) ? '+' : '-') : 'x');
		char vs_pol = ((cp_read(sd, 0xb5) & 0x40) ?
				((cp_read(sd, 0xb5) & 0x20) ? '+' : '-') : 'x');
		v4l2_info(sd,
			"STDI: lcf (frame height - 1) = %d, bl = %d, lcvs (vsync) = %d, fcl = %d, %s, %chsync, %cvsync\n",
			lcf, bl, lcvs, fcl,
			(cp_read(sd, 0xb1) & 0x40) ?
				"interlaced" : "progressive",
			hs_pol, vs_pol);
	}
	if (adv7842_query_dv_timings(sd, &timings))
		v4l2_info(sd, "No video detected\n");
	else
		v4l2_print_dv_timings(sd->name, "Detected format: ",
				      &timings, true);
	v4l2_print_dv_timings(sd->name, "Configured format: ",
			&state->timings, true);

	if (no_cp_signal(sd))
		return 0;

	v4l2_info(sd, "-----Color space-----\n");
	v4l2_info(sd, "RGB quantization range ctrl: %s\n",
		  rgb_quantization_range_txt[state->rgb_quantization_range]);
	v4l2_info(sd, "Input color space: %s\n",
		  input_color_space_txt[reg_io_0x02 >> 4]);
	v4l2_info(sd, "Output color space: %s %s, saturator %s\n",
		  (reg_io_0x02 & 0x02) ? "RGB" : "YCbCr",
		  (reg_io_0x02 & 0x04) ? "(16-235)" : "(0-255)",
		  ((reg_io_0x02 & 0x04) ^ (reg_io_0x02 & 0x01)) ?
					"enabled" : "disabled");
	v4l2_info(sd, "Color space conversion: %s\n",
		  csc_coeff_sel_rb[cp_read(sd, 0xf4) >> 4]);

	if (!is_digital_input(sd))
		return 0;

	v4l2_info(sd, "-----%s status-----\n", is_hdmi(sd) ? "HDMI" : "DVI-D");
	v4l2_info(sd, "HDCP encrypted content: %s\n",
			(hdmi_read(sd, 0x05) & 0x40) ? "true" : "false");
	v4l2_info(sd, "HDCP keys read: %s%s\n",
			(hdmi_read(sd, 0x04) & 0x20) ? "yes" : "no",
			(hdmi_read(sd, 0x04) & 0x10) ? "ERROR" : "");
	if (!is_hdmi(sd))
		return 0;

	v4l2_info(sd, "Audio: pll %s, samples %s, %s\n",
			audio_pll_locked ? "locked" : "not locked",
			audio_sample_packet_detect ? "detected" : "not detected",
			audio_mute ? "muted" : "enabled");
	if (audio_pll_locked && audio_sample_packet_detect) {
		v4l2_info(sd, "Audio format: %s\n",
			(hdmi_read(sd, 0x07) & 0x40) ? "multi-channel" : "stereo");
	}
	v4l2_info(sd, "Audio CTS: %u\n", (hdmi_read(sd, 0x5b) << 12) +
			(hdmi_read(sd, 0x5c) << 8) +
			(hdmi_read(sd, 0x5d) & 0xf0));
	v4l2_info(sd, "Audio N: %u\n", ((hdmi_read(sd, 0x5d) & 0x0f) << 16) +
			(hdmi_read(sd, 0x5e) << 8) +
			hdmi_read(sd, 0x5f));
	v4l2_info(sd, "AV Mute: %s\n",
			(hdmi_read(sd, 0x04) & 0x40) ? "on" : "off");
	v4l2_info(sd, "Deep color mode: %s\n",
			deep_color_mode_txt[hdmi_read(sd, 0x0b) >> 6]);

	print_avi_infoframe(sd);
	return 0;
}

static int adv7842_log_status(struct v4l2_subdev *sd)
{
	struct adv7842_state *state = to_state(sd);

	if (state->mode == ADV7842_MODE_SDP)
		return adv7842_sdp_log_status(sd);
	return adv7842_cp_log_status(sd);
}

static int adv7842_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct adv7842_state *state = to_state(sd);

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (state->mode != ADV7842_MODE_SDP)
		return -ENODATA;

	if (!(sdp_read(sd, 0x5A) & 0x01)) {
		*std = 0;
		v4l2_dbg(1, debug, sd, "%s: no valid signal\n", __func__);
		return 0;
	}

	switch (sdp_read(sd, 0x52) & 0x0f) {
	case 0:
		/* NTSC-M/J */
		*std &= V4L2_STD_NTSC;
		break;
	case 2:
		/* NTSC-443 */
		*std &= V4L2_STD_NTSC_443;
		break;
	case 3:
		/* 60HzSECAM */
		*std &= V4L2_STD_SECAM;
		break;
	case 4:
		/* PAL-M */
		*std &= V4L2_STD_PAL_M;
		break;
	case 6:
		/* PAL-60 */
		*std &= V4L2_STD_PAL_60;
		break;
	case 0xc:
		/* PAL-CombN */
		*std &= V4L2_STD_PAL_Nc;
		break;
	case 0xe:
		/* PAL-BGHID */
		*std &= V4L2_STD_PAL;
		break;
	case 0xf:
		/* SECAM */
		*std &= V4L2_STD_SECAM;
		break;
	default:
		*std &= V4L2_STD_ALL;
		break;
	}
	return 0;
}

static int adv7842_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct adv7842_state *state = to_state(sd);

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (state->mode != ADV7842_MODE_SDP)
		return -ENODATA;

	if (norm & V4L2_STD_ALL) {
		state->norm = norm;
		return 0;
	}
	return -EINVAL;
}

static int adv7842_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct adv7842_state *state = to_state(sd);

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (state->mode != ADV7842_MODE_SDP)
		return -ENODATA;

	*norm = state->norm;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int adv7842_core_init(struct v4l2_subdev *sd,
		const struct adv7842_platform_data *pdata)
{
	hdmi_write(sd, 0x48,
		   (pdata->disable_pwrdnb ? 0x80 : 0) |
		   (pdata->disable_cable_det_rst ? 0x40 : 0));

	disable_input(sd);

	/* power */
	io_write(sd, 0x0c, 0x42);   /* Power up part and power down VDP */
	io_write(sd, 0x15, 0x80);   /* Power up pads */

	/* video format */
	io_write(sd, 0x02,
		 pdata->inp_color_space << 4 |
		 pdata->alt_gamma << 3 |
		 pdata->op_656_range << 2 |
		 pdata->rgb_out << 1 |
		 pdata->alt_data_sat << 0);
	io_write(sd, 0x03, pdata->op_format_sel);
	io_write_and_or(sd, 0x04, 0x1f, pdata->op_ch_sel << 5);
	io_write_and_or(sd, 0x05, 0xf0, pdata->blank_data << 3 |
			pdata->insert_av_codes << 2 |
			pdata->replicate_av_codes << 1 |
			pdata->invert_cbcr << 0);

	/* Drive strength */
	io_write_and_or(sd, 0x14, 0xc0, pdata->drive_strength.data<<4 |
			pdata->drive_strength.clock<<2 |
			pdata->drive_strength.sync);

	/* HDMI free run */
	cp_write(sd, 0xba, (pdata->hdmi_free_run_mode << 1) | 0x01);

	/* TODO from platform data */
	cp_write(sd, 0x69, 0x14);   /* Enable CP CSC */
	io_write(sd, 0x06, 0xa6);   /* positive VS and HS and DE */
	cp_write(sd, 0xf3, 0xdc); /* Low threshold to enter/exit free run mode */
	afe_write(sd, 0xb5, 0x01);  /* Setting MCLK to 256Fs */

	afe_write(sd, 0x02, pdata->ain_sel); /* Select analog input muxing mode */
	io_write_and_or(sd, 0x30, ~(1 << 4), pdata->output_bus_lsb_to_msb << 4);

	sdp_csc_coeff(sd, &pdata->sdp_csc_coeff);

	if (pdata->sdp_io_sync.adjust) {
		const struct adv7842_sdp_io_sync_adjustment *s = &pdata->sdp_io_sync;
		sdp_io_write(sd, 0x94, (s->hs_beg>>8) & 0xf);
		sdp_io_write(sd, 0x95, s->hs_beg & 0xff);
		sdp_io_write(sd, 0x96, (s->hs_width>>8) & 0xf);
		sdp_io_write(sd, 0x97, s->hs_width & 0xff);
		sdp_io_write(sd, 0x98, (s->de_beg>>8) & 0xf);
		sdp_io_write(sd, 0x99, s->de_beg & 0xff);
		sdp_io_write(sd, 0x9a, (s->de_end>>8) & 0xf);
		sdp_io_write(sd, 0x9b, s->de_end & 0xff);
	}

	/* todo, improve settings for sdram */
	if (pdata->sd_ram_size >= 128) {
		sdp_write(sd, 0x12, 0x0d); /* Frame TBC,3D comb enabled */
		if (pdata->sd_ram_ddr) {
			/* SDP setup for the AD eval board */
			sdp_io_write(sd, 0x6f, 0x00); /* DDR mode */
			sdp_io_write(sd, 0x75, 0x0a); /* 128 MB memory size */
			sdp_io_write(sd, 0x7a, 0xa5); /* Timing Adjustment */
			sdp_io_write(sd, 0x7b, 0x8f); /* Timing Adjustment */
			sdp_io_write(sd, 0x60, 0x01); /* SDRAM reset */
		} else {
			sdp_io_write(sd, 0x75, 0x0a); /* 64 MB memory size ?*/
			sdp_io_write(sd, 0x74, 0x00); /* must be zero for sdr sdram */
			sdp_io_write(sd, 0x79, 0x33); /* CAS latency to 3,
							 depends on memory */
			sdp_io_write(sd, 0x6f, 0x01); /* SDR mode */
			sdp_io_write(sd, 0x7a, 0xa5); /* Timing Adjustment */
			sdp_io_write(sd, 0x7b, 0x8f); /* Timing Adjustment */
			sdp_io_write(sd, 0x60, 0x01); /* SDRAM reset */
		}
	} else {
		/*
		 * Manual UG-214, rev 0 is bit confusing on this bit
		 * but a '1' disables any signal if the Ram is active.
		 */
		sdp_io_write(sd, 0x29, 0x10); /* Tristate memory interface */
	}

	select_input(sd, pdata->vid_std_select);

	enable_input(sd);

	/* disable I2C access to internal EDID ram from HDMI DDC ports */
	rep_write_and_or(sd, 0x77, 0xf3, 0x00);

	hdmi_write(sd, 0x69, 0xa3); /* HPA manual */
	/* HPA disable on port A and B */
	io_write_and_or(sd, 0x20, 0xcf, 0x00);

	/* LLC */
	/* Set phase to 16. TODO: get this from platform_data */
	io_write(sd, 0x19, 0x90);
	io_write(sd, 0x33, 0x40);

	/* interrupts */
	io_write(sd, 0x40, 0xe2); /* Configure INT1 */

	adv7842_irq_enable(sd, true);

	return v4l2_ctrl_handler_setup(sd->ctrl_handler);
}

/* ----------------------------------------------------------------------- */

static int adv7842_ddr_ram_test(struct v4l2_subdev *sd)
{
	/*
	 * From ADV784x external Memory test.pdf
	 *
	 * Reset must just been performed before running test.
	 * Recommended to reset after test.
	 */
	int i;
	int pass = 0;
	int fail = 0;
	int complete = 0;

	io_write(sd, 0x00, 0x01);  /* Program SDP 4x1 */
	io_write(sd, 0x01, 0x00);  /* Program SDP mode */
	afe_write(sd, 0x80, 0x92); /* SDP Recommeneded Write */
	afe_write(sd, 0x9B, 0x01); /* SDP Recommeneded Write ADV7844ES1 */
	afe_write(sd, 0x9C, 0x60); /* SDP Recommeneded Write ADV7844ES1 */
	afe_write(sd, 0x9E, 0x02); /* SDP Recommeneded Write ADV7844ES1 */
	afe_write(sd, 0xA0, 0x0B); /* SDP Recommeneded Write ADV7844ES1 */
	afe_write(sd, 0xC3, 0x02); /* Memory BIST Initialisation */
	io_write(sd, 0x0C, 0x40);  /* Power up ADV7844 */
	io_write(sd, 0x15, 0xBA);  /* Enable outputs */
	sdp_write(sd, 0x12, 0x00); /* Disable 3D comb, Frame TBC & 3DNR */
	io_write(sd, 0xFF, 0x04);  /* Reset memory controller */

	mdelay(5);

	sdp_write(sd, 0x12, 0x00);    /* Disable 3D Comb, Frame TBC & 3DNR */
	sdp_io_write(sd, 0x2A, 0x01); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x7c, 0x19); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x80, 0x87); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x81, 0x4a); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x82, 0x2c); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x83, 0x0e); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x84, 0x94); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x85, 0x62); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x7d, 0x00); /* Memory BIST Initialisation */
	sdp_io_write(sd, 0x7e, 0x1a); /* Memory BIST Initialisation */

	mdelay(5);

	sdp_io_write(sd, 0xd9, 0xd5); /* Enable BIST Test */
	sdp_write(sd, 0x12, 0x05); /* Enable FRAME TBC & 3D COMB */

	mdelay(20);

	for (i = 0; i < 10; i++) {
		u8 result = sdp_io_read(sd, 0xdb);
		if (result & 0x10) {
			complete++;
			if (result & 0x20)
				fail++;
			else
				pass++;
		}
		mdelay(20);
	}

	v4l2_dbg(1, debug, sd,
		"Ram Test: completed %d of %d: pass %d, fail %d\n",
		complete, i, pass, fail);

	if (!complete || fail)
		return -EIO;
	return 0;
}

static void adv7842_rewrite_i2c_addresses(struct v4l2_subdev *sd,
		struct adv7842_platform_data *pdata)
{
	io_write(sd, 0xf1, pdata->i2c_sdp << 1);
	io_write(sd, 0xf2, pdata->i2c_sdp_io << 1);
	io_write(sd, 0xf3, pdata->i2c_avlink << 1);
	io_write(sd, 0xf4, pdata->i2c_cec << 1);
	io_write(sd, 0xf5, pdata->i2c_infoframe << 1);

	io_write(sd, 0xf8, pdata->i2c_afe << 1);
	io_write(sd, 0xf9, pdata->i2c_repeater << 1);
	io_write(sd, 0xfa, pdata->i2c_edid << 1);
	io_write(sd, 0xfb, pdata->i2c_hdmi << 1);

	io_write(sd, 0xfd, pdata->i2c_cp << 1);
	io_write(sd, 0xfe, pdata->i2c_vdp << 1);
}

static int adv7842_command_ram_test(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct adv7842_state *state = to_state(sd);
	struct adv7842_platform_data *pdata = client->dev.platform_data;
	int ret = 0;

	if (!pdata)
		return -ENODEV;

	if (!pdata->sd_ram_size || !pdata->sd_ram_ddr) {
		v4l2_info(sd, "no sdram or no ddr sdram\n");
		return -EINVAL;
	}

	main_reset(sd);

	adv7842_rewrite_i2c_addresses(sd, pdata);

	/* run ram test */
	ret = adv7842_ddr_ram_test(sd);

	main_reset(sd);

	adv7842_rewrite_i2c_addresses(sd, pdata);

	/* and re-init chip and state */
	adv7842_core_init(sd, pdata);

	disable_input(sd);

	select_input(sd, state->vid_std_select);

	enable_input(sd);

	adv7842_s_dv_timings(sd, &state->timings);

	edid_write_vga_segment(sd);
	edid_write_hdmi_segment(sd, 0);
	edid_write_hdmi_segment(sd, 1);

	return ret;
}

static long adv7842_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case ADV7842_CMD_RAM_TEST:
		return adv7842_command_ram_test(sd);
	}
	return -ENOTTY;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops adv7842_ctrl_ops = {
	.s_ctrl = adv7842_s_ctrl,
};

static const struct v4l2_subdev_core_ops adv7842_core_ops = {
	.log_status = adv7842_log_status,
	.g_std = adv7842_g_std,
	.s_std = adv7842_s_std,
	.ioctl = adv7842_ioctl,
	.interrupt_service_routine = adv7842_isr,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = adv7842_g_register,
	.s_register = adv7842_s_register,
#endif
};

static const struct v4l2_subdev_video_ops adv7842_video_ops = {
	.s_routing = adv7842_s_routing,
	.querystd = adv7842_querystd,
	.g_input_status = adv7842_g_input_status,
	.s_dv_timings = adv7842_s_dv_timings,
	.g_dv_timings = adv7842_g_dv_timings,
	.query_dv_timings = adv7842_query_dv_timings,
	.enum_dv_timings = adv7842_enum_dv_timings,
	.dv_timings_cap = adv7842_dv_timings_cap,
	.enum_mbus_fmt = adv7842_enum_mbus_fmt,
	.g_mbus_fmt = adv7842_g_mbus_fmt,
	.try_mbus_fmt = adv7842_g_mbus_fmt,
	.s_mbus_fmt = adv7842_g_mbus_fmt,
};

static const struct v4l2_subdev_pad_ops adv7842_pad_ops = {
	.set_edid = adv7842_set_edid,
};

static const struct v4l2_subdev_ops adv7842_ops = {
	.core = &adv7842_core_ops,
	.video = &adv7842_video_ops,
	.pad = &adv7842_pad_ops,
};

/* -------------------------- custom ctrls ---------------------------------- */

static const struct v4l2_ctrl_config adv7842_ctrl_analog_sampling_phase = {
	.ops = &adv7842_ctrl_ops,
	.id = V4L2_CID_ADV_RX_ANALOG_SAMPLING_PHASE,
	.name = "Analog Sampling Phase",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 0x1f,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config adv7842_ctrl_free_run_color_manual = {
	.ops = &adv7842_ctrl_ops,
	.id = V4L2_CID_ADV_RX_FREE_RUN_COLOR_MANUAL,
	.name = "Free Running Color, Manual",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
	.def = 1,
};

static const struct v4l2_ctrl_config adv7842_ctrl_free_run_color = {
	.ops = &adv7842_ctrl_ops,
	.id = V4L2_CID_ADV_RX_FREE_RUN_COLOR,
	.name = "Free Running Color",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.max = 0xffffff,
	.step = 0x1,
};


static void adv7842_unregister_clients(struct adv7842_state *state)
{
	if (state->i2c_avlink)
		i2c_unregister_device(state->i2c_avlink);
	if (state->i2c_cec)
		i2c_unregister_device(state->i2c_cec);
	if (state->i2c_infoframe)
		i2c_unregister_device(state->i2c_infoframe);
	if (state->i2c_sdp_io)
		i2c_unregister_device(state->i2c_sdp_io);
	if (state->i2c_sdp)
		i2c_unregister_device(state->i2c_sdp);
	if (state->i2c_afe)
		i2c_unregister_device(state->i2c_afe);
	if (state->i2c_repeater)
		i2c_unregister_device(state->i2c_repeater);
	if (state->i2c_edid)
		i2c_unregister_device(state->i2c_edid);
	if (state->i2c_hdmi)
		i2c_unregister_device(state->i2c_hdmi);
	if (state->i2c_cp)
		i2c_unregister_device(state->i2c_cp);
	if (state->i2c_vdp)
		i2c_unregister_device(state->i2c_vdp);
}

static struct i2c_client *adv7842_dummy_client(struct v4l2_subdev *sd,
					       u8 addr, u8 io_reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	io_write(sd, io_reg, addr << 1);
	return i2c_new_dummy(client->adapter, io_read(sd, io_reg) >> 1);
}

static int adv7842_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adv7842_state *state;
	struct adv7842_platform_data *pdata = client->dev.platform_data;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_subdev *sd;
	u16 rev;
	int err;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_dbg(1, debug, client, "detecting adv7842 client on address 0x%x\n",
		client->addr << 1);

	if (!pdata) {
		v4l_err(client, "No platform data!\n");
		return -ENODEV;
	}

	state = devm_kzalloc(&client->dev, sizeof(struct adv7842_state), GFP_KERNEL);
	if (!state) {
		v4l_err(client, "Could not allocate adv7842_state memory!\n");
		return -ENOMEM;
	}

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &adv7842_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	state->connector_hdmi = pdata->connector_hdmi;
	state->mode = pdata->mode;

	state->hdmi_port_a = true;

	/* i2c access to adv7842? */
	rev = adv_smbus_read_byte_data_check(client, 0xea, false) << 8 |
		adv_smbus_read_byte_data_check(client, 0xeb, false);
	if (rev != 0x2012) {
		v4l2_info(sd, "got rev=0x%04x on first read attempt\n", rev);
		rev = adv_smbus_read_byte_data_check(client, 0xea, false) << 8 |
			adv_smbus_read_byte_data_check(client, 0xeb, false);
	}
	if (rev != 0x2012) {
		v4l2_info(sd, "not an adv7842 on address 0x%x (rev=0x%04x)\n",
			  client->addr << 1, rev);
		return -ENODEV;
	}

	if (pdata->chip_reset)
		main_reset(sd);

	/* control handlers */
	hdl = &state->hdl;
	v4l2_ctrl_handler_init(hdl, 6);

	/* add in ascending ID order */
	v4l2_ctrl_new_std(hdl, &adv7842_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &adv7842_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &adv7842_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &adv7842_ctrl_ops,
			  V4L2_CID_HUE, 0, 128, 1, 0);

	/* custom controls */
	state->detect_tx_5v_ctrl = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_DV_RX_POWER_PRESENT, 0, 3, 0, 0);
	state->analog_sampling_phase_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7842_ctrl_analog_sampling_phase, NULL);
	state->free_run_color_ctrl_manual = v4l2_ctrl_new_custom(hdl,
			&adv7842_ctrl_free_run_color_manual, NULL);
	state->free_run_color_ctrl = v4l2_ctrl_new_custom(hdl,
			&adv7842_ctrl_free_run_color, NULL);
	state->rgb_quantization_range_ctrl =
		v4l2_ctrl_new_std_menu(hdl, &adv7842_ctrl_ops,
			V4L2_CID_DV_RX_RGB_RANGE, V4L2_DV_RGB_RANGE_FULL,
			0, V4L2_DV_RGB_RANGE_AUTO);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		err = hdl->error;
		goto err_hdl;
	}
	state->detect_tx_5v_ctrl->is_private = true;
	state->rgb_quantization_range_ctrl->is_private = true;
	state->analog_sampling_phase_ctrl->is_private = true;
	state->free_run_color_ctrl_manual->is_private = true;
	state->free_run_color_ctrl->is_private = true;

	if (adv7842_s_detect_tx_5v_ctrl(sd)) {
		err = -ENODEV;
		goto err_hdl;
	}

	state->i2c_avlink = adv7842_dummy_client(sd, pdata->i2c_avlink, 0xf3);
	state->i2c_cec = adv7842_dummy_client(sd, pdata->i2c_cec, 0xf4);
	state->i2c_infoframe = adv7842_dummy_client(sd, pdata->i2c_infoframe, 0xf5);
	state->i2c_sdp_io = adv7842_dummy_client(sd, pdata->i2c_sdp_io, 0xf2);
	state->i2c_sdp = adv7842_dummy_client(sd, pdata->i2c_sdp, 0xf1);
	state->i2c_afe = adv7842_dummy_client(sd, pdata->i2c_afe, 0xf8);
	state->i2c_repeater = adv7842_dummy_client(sd, pdata->i2c_repeater, 0xf9);
	state->i2c_edid = adv7842_dummy_client(sd, pdata->i2c_edid, 0xfa);
	state->i2c_hdmi = adv7842_dummy_client(sd, pdata->i2c_hdmi, 0xfb);
	state->i2c_cp = adv7842_dummy_client(sd, pdata->i2c_cp, 0xfd);
	state->i2c_vdp = adv7842_dummy_client(sd, pdata->i2c_vdp, 0xfe);
	if (!state->i2c_avlink || !state->i2c_cec || !state->i2c_infoframe ||
	    !state->i2c_sdp_io || !state->i2c_sdp || !state->i2c_afe ||
	    !state->i2c_repeater || !state->i2c_edid || !state->i2c_hdmi ||
	    !state->i2c_cp || !state->i2c_vdp) {
		err = -ENOMEM;
		v4l2_err(sd, "failed to create all i2c clients\n");
		goto err_i2c;
	}

	/* work queues */
	state->work_queues = create_singlethread_workqueue(client->name);
	if (!state->work_queues) {
		v4l2_err(sd, "Could not create work queue\n");
		err = -ENOMEM;
		goto err_i2c;
	}

	INIT_DELAYED_WORK(&state->delayed_work_enable_hotplug,
			adv7842_delayed_work_enable_hotplug);

	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	err = media_entity_init(&sd->entity, 1, &state->pad, 0);
	if (err)
		goto err_work_queues;

	err = adv7842_core_init(sd, pdata);
	if (err)
		goto err_entity;

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
		  client->addr << 1, client->adapter->name);
	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_work_queues:
	cancel_delayed_work(&state->delayed_work_enable_hotplug);
	destroy_workqueue(state->work_queues);
err_i2c:
	adv7842_unregister_clients(state);
err_hdl:
	v4l2_ctrl_handler_free(hdl);
	return err;
}

/* ----------------------------------------------------------------------- */

static int adv7842_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7842_state *state = to_state(sd);

	adv7842_irq_enable(sd, false);

	cancel_delayed_work(&state->delayed_work_enable_hotplug);
	destroy_workqueue(state->work_queues);
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	adv7842_unregister_clients(to_state(sd));
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_device_id adv7842_id[] = {
	{ "adv7842", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7842_id);

/* ----------------------------------------------------------------------- */

static struct i2c_driver adv7842_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "adv7842",
	},
	.probe = adv7842_probe,
	.remove = adv7842_remove,
	.id_table = adv7842_id,
};

module_i2c_driver(adv7842_driver);
