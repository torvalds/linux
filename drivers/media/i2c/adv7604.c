/*
 * adv7604 - Analog Devices ADV7604 video decoder driver
 *
 * Copyright 2012 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
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
 * REF_01 - Analog devices, ADV7604, Register Settings Recommendations,
 *		Revision 2.5, June 2010
 * REF_02 - Analog devices, Register map documentation, Documentation of
 *		the register maps, Software manual, Rev. F, June 2010
 * REF_03 - Analog devices, ADV7604, Hardware Manual, Rev. F, August 2010
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>

#include <media/adv7604.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-of.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

MODULE_DESCRIPTION("Analog Devices ADV7604 video decoder driver");
MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_AUTHOR("Mats Randgaard <mats.randgaard@cisco.com>");
MODULE_LICENSE("GPL");

/* ADV7604 system clock frequency */
#define ADV76XX_FSC (28636360)

#define ADV76XX_RGB_OUT					(1 << 1)

#define ADV76XX_OP_FORMAT_SEL_8BIT			(0 << 0)
#define ADV7604_OP_FORMAT_SEL_10BIT			(1 << 0)
#define ADV76XX_OP_FORMAT_SEL_12BIT			(2 << 0)

#define ADV76XX_OP_MODE_SEL_SDR_422			(0 << 5)
#define ADV7604_OP_MODE_SEL_DDR_422			(1 << 5)
#define ADV76XX_OP_MODE_SEL_SDR_444			(2 << 5)
#define ADV7604_OP_MODE_SEL_DDR_444			(3 << 5)
#define ADV76XX_OP_MODE_SEL_SDR_422_2X			(4 << 5)
#define ADV7604_OP_MODE_SEL_ADI_CM			(5 << 5)

#define ADV76XX_OP_CH_SEL_GBR				(0 << 5)
#define ADV76XX_OP_CH_SEL_GRB				(1 << 5)
#define ADV76XX_OP_CH_SEL_BGR				(2 << 5)
#define ADV76XX_OP_CH_SEL_RGB				(3 << 5)
#define ADV76XX_OP_CH_SEL_BRG				(4 << 5)
#define ADV76XX_OP_CH_SEL_RBG				(5 << 5)

#define ADV76XX_OP_SWAP_CB_CR				(1 << 0)

enum adv76xx_type {
	ADV7604,
	ADV7611,
};

struct adv76xx_reg_seq {
	unsigned int reg;
	u8 val;
};

struct adv76xx_format_info {
	u32 code;
	u8 op_ch_sel;
	bool rgb_out;
	bool swap_cb_cr;
	u8 op_format_sel;
};

struct adv76xx_chip_info {
	enum adv76xx_type type;

	bool has_afe;
	unsigned int max_port;
	unsigned int num_dv_ports;

	unsigned int edid_enable_reg;
	unsigned int edid_status_reg;
	unsigned int lcf_reg;

	unsigned int cable_det_mask;
	unsigned int tdms_lock_mask;
	unsigned int fmt_change_digital_mask;
	unsigned int cp_csc;

	const struct adv76xx_format_info *formats;
	unsigned int nformats;

	void (*set_termination)(struct v4l2_subdev *sd, bool enable);
	void (*setup_irqs)(struct v4l2_subdev *sd);
	unsigned int (*read_hdmi_pixelclock)(struct v4l2_subdev *sd);
	unsigned int (*read_cable_det)(struct v4l2_subdev *sd);

	/* 0 = AFE, 1 = HDMI */
	const struct adv76xx_reg_seq *recommended_settings[2];
	unsigned int num_recommended_settings[2];

	unsigned long page_mask;

	/* Masks for timings */
	unsigned int linewidth_mask;
	unsigned int field0_height_mask;
	unsigned int field1_height_mask;
	unsigned int hfrontporch_mask;
	unsigned int hsync_mask;
	unsigned int hbackporch_mask;
	unsigned int field0_vfrontporch_mask;
	unsigned int field1_vfrontporch_mask;
	unsigned int field0_vsync_mask;
	unsigned int field1_vsync_mask;
	unsigned int field0_vbackporch_mask;
	unsigned int field1_vbackporch_mask;
};

/*
 **********************************************************************
 *
 *  Arrays with configuration parameters for the ADV7604
 *
 **********************************************************************
 */

struct adv76xx_state {
	const struct adv76xx_chip_info *info;
	struct adv76xx_platform_data pdata;

	struct gpio_desc *hpd_gpio[4];

	struct v4l2_subdev sd;
	struct media_pad pads[ADV76XX_PAD_MAX];
	unsigned int source_pad;

	struct v4l2_ctrl_handler hdl;

	enum adv76xx_pad selected_input;

	struct v4l2_dv_timings timings;
	const struct adv76xx_format_info *format;

	struct {
		u8 edid[256];
		u32 present;
		unsigned blocks;
	} edid;
	u16 spa_port_a[2];
	struct v4l2_fract aspect_ratio;
	u32 rgb_quantization_range;
	struct workqueue_struct *work_queues;
	struct delayed_work delayed_work_enable_hotplug;
	bool restart_stdi_once;

	/* i2c clients */
	struct i2c_client *i2c_clients[ADV76XX_PAGE_MAX];

	/* controls */
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *analog_sampling_phase_ctrl;
	struct v4l2_ctrl *free_run_color_manual_ctrl;
	struct v4l2_ctrl *free_run_color_ctrl;
	struct v4l2_ctrl *rgb_quantization_range_ctrl;
};

static bool adv76xx_has_afe(struct adv76xx_state *state)
{
	return state->info->has_afe;
}

/* Supported CEA and DMT timings */
static const struct v4l2_dv_timings adv76xx_timings[] = {
	V4L2_DV_BT_CEA_720X480P59_94,
	V4L2_DV_BT_CEA_720X576P50,
	V4L2_DV_BT_CEA_1280X720P24,
	V4L2_DV_BT_CEA_1280X720P25,
	V4L2_DV_BT_CEA_1280X720P50,
	V4L2_DV_BT_CEA_1280X720P60,
	V4L2_DV_BT_CEA_1920X1080P24,
	V4L2_DV_BT_CEA_1920X1080P25,
	V4L2_DV_BT_CEA_1920X1080P30,
	V4L2_DV_BT_CEA_1920X1080P50,
	V4L2_DV_BT_CEA_1920X1080P60,

	/* sorted by DMT ID */
	V4L2_DV_BT_DMT_640X350P85,
	V4L2_DV_BT_DMT_640X400P85,
	V4L2_DV_BT_DMT_720X400P85,
	V4L2_DV_BT_DMT_640X480P60,
	V4L2_DV_BT_DMT_640X480P72,
	V4L2_DV_BT_DMT_640X480P75,
	V4L2_DV_BT_DMT_640X480P85,
	V4L2_DV_BT_DMT_800X600P56,
	V4L2_DV_BT_DMT_800X600P60,
	V4L2_DV_BT_DMT_800X600P72,
	V4L2_DV_BT_DMT_800X600P75,
	V4L2_DV_BT_DMT_800X600P85,
	V4L2_DV_BT_DMT_848X480P60,
	V4L2_DV_BT_DMT_1024X768P60,
	V4L2_DV_BT_DMT_1024X768P70,
	V4L2_DV_BT_DMT_1024X768P75,
	V4L2_DV_BT_DMT_1024X768P85,
	V4L2_DV_BT_DMT_1152X864P75,
	V4L2_DV_BT_DMT_1280X768P60_RB,
	V4L2_DV_BT_DMT_1280X768P60,
	V4L2_DV_BT_DMT_1280X768P75,
	V4L2_DV_BT_DMT_1280X768P85,
	V4L2_DV_BT_DMT_1280X800P60_RB,
	V4L2_DV_BT_DMT_1280X800P60,
	V4L2_DV_BT_DMT_1280X800P75,
	V4L2_DV_BT_DMT_1280X800P85,
	V4L2_DV_BT_DMT_1280X960P60,
	V4L2_DV_BT_DMT_1280X960P85,
	V4L2_DV_BT_DMT_1280X1024P60,
	V4L2_DV_BT_DMT_1280X1024P75,
	V4L2_DV_BT_DMT_1280X1024P85,
	V4L2_DV_BT_DMT_1360X768P60,
	V4L2_DV_BT_DMT_1400X1050P60_RB,
	V4L2_DV_BT_DMT_1400X1050P60,
	V4L2_DV_BT_DMT_1400X1050P75,
	V4L2_DV_BT_DMT_1400X1050P85,
	V4L2_DV_BT_DMT_1440X900P60_RB,
	V4L2_DV_BT_DMT_1440X900P60,
	V4L2_DV_BT_DMT_1600X1200P60,
	V4L2_DV_BT_DMT_1680X1050P60_RB,
	V4L2_DV_BT_DMT_1680X1050P60,
	V4L2_DV_BT_DMT_1792X1344P60,
	V4L2_DV_BT_DMT_1856X1392P60,
	V4L2_DV_BT_DMT_1920X1200P60_RB,
	V4L2_DV_BT_DMT_1366X768P60_RB,
	V4L2_DV_BT_DMT_1366X768P60,
	V4L2_DV_BT_DMT_1920X1080P60,
	{ },
};

struct adv76xx_video_standards {
	struct v4l2_dv_timings timings;
	u8 vid_std;
	u8 v_freq;
};

/* sorted by number of lines */
static const struct adv76xx_video_standards adv7604_prim_mode_comp[] = {
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
static const struct adv76xx_video_standards adv7604_prim_mode_gr[] = {
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
static const struct adv76xx_video_standards adv76xx_prim_mode_hdmi_comp[] = {
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
static const struct adv76xx_video_standards adv76xx_prim_mode_hdmi_gr[] = {
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

static inline struct adv76xx_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv76xx_state, sd);
}

static inline unsigned htotal(const struct v4l2_bt_timings *t)
{
	return V4L2_DV_BT_FRAME_WIDTH(t);
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

static s32 adv_smbus_read_byte_data(struct adv76xx_state *state,
				    enum adv76xx_page page, u8 command)
{
	return adv_smbus_read_byte_data_check(state->i2c_clients[page],
					      command, true);
}

static s32 adv_smbus_write_byte_data(struct adv76xx_state *state,
				     enum adv76xx_page page, u8 command,
				     u8 value)
{
	struct i2c_client *client = state->i2c_clients[page];
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

static s32 adv_smbus_write_i2c_block_data(struct adv76xx_state *state,
					  enum adv76xx_page page, u8 command,
					  unsigned length, const u8 *values)
{
	struct i2c_client *client = state->i2c_clients[page];
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
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV76XX_PAGE_IO, reg);
}

static inline int io_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_IO, reg, val);
}

static inline int io_write_clr_set(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return io_write(sd, reg, (io_read(sd, reg) & ~mask) | val);
}

static inline int avlink_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV7604_PAGE_AVLINK, reg);
}

static inline int avlink_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV7604_PAGE_AVLINK, reg, val);
}

static inline int cec_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV76XX_PAGE_CEC, reg);
}

static inline int cec_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_CEC, reg, val);
}

static inline int infoframe_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV76XX_PAGE_INFOFRAME, reg);
}

static inline int infoframe_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_INFOFRAME,
					 reg, val);
}

static inline int afe_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV76XX_PAGE_AFE, reg);
}

static inline int afe_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_AFE, reg, val);
}

static inline int rep_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV76XX_PAGE_REP, reg);
}

static inline int rep_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_REP, reg, val);
}

static inline int rep_write_clr_set(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return rep_write(sd, reg, (rep_read(sd, reg) & ~mask) | val);
}

static inline int edid_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV76XX_PAGE_EDID, reg);
}

static inline int edid_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_EDID, reg, val);
}

static inline int edid_write_block(struct v4l2_subdev *sd,
					unsigned len, const u8 *val)
{
	struct adv76xx_state *state = to_state(sd);
	int err = 0;
	int i;

	v4l2_dbg(2, debug, sd, "%s: write EDID block (%d byte)\n", __func__, len);

	for (i = 0; !err && i < len; i += I2C_SMBUS_BLOCK_MAX)
		err = adv_smbus_write_i2c_block_data(state, ADV76XX_PAGE_EDID,
				i, I2C_SMBUS_BLOCK_MAX, val + i);
	return err;
}

static void adv76xx_set_hpd(struct adv76xx_state *state, unsigned int hpd)
{
	unsigned int i;

	for (i = 0; i < state->info->num_dv_ports; ++i)
		gpiod_set_value_cansleep(state->hpd_gpio[i], hpd & BIT(i));

	v4l2_subdev_notify(&state->sd, ADV76XX_HOTPLUG, &hpd);
}

static void adv76xx_delayed_work_enable_hotplug(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct adv76xx_state *state = container_of(dwork, struct adv76xx_state,
						delayed_work_enable_hotplug);
	struct v4l2_subdev *sd = &state->sd;

	v4l2_dbg(2, debug, sd, "%s: enable hotplug\n", __func__);

	adv76xx_set_hpd(state, state->edid.present);
}

static inline int hdmi_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV76XX_PAGE_HDMI, reg);
}

static u16 hdmi_read16(struct v4l2_subdev *sd, u8 reg, u16 mask)
{
	return ((hdmi_read(sd, reg) << 8) | hdmi_read(sd, reg + 1)) & mask;
}

static inline int hdmi_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_HDMI, reg, val);
}

static inline int hdmi_write_clr_set(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return hdmi_write(sd, reg, (hdmi_read(sd, reg) & ~mask) | val);
}

static inline int test_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_TEST, reg, val);
}

static inline int cp_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV76XX_PAGE_CP, reg);
}

static u16 cp_read16(struct v4l2_subdev *sd, u8 reg, u16 mask)
{
	return ((cp_read(sd, reg) << 8) | cp_read(sd, reg + 1)) & mask;
}

static inline int cp_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV76XX_PAGE_CP, reg, val);
}

static inline int cp_write_clr_set(struct v4l2_subdev *sd, u8 reg, u8 mask, u8 val)
{
	return cp_write(sd, reg, (cp_read(sd, reg) & ~mask) | val);
}

static inline int vdp_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_read_byte_data(state, ADV7604_PAGE_VDP, reg);
}

static inline int vdp_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);

	return adv_smbus_write_byte_data(state, ADV7604_PAGE_VDP, reg, val);
}

#define ADV76XX_REG(page, offset)	(((page) << 8) | (offset))
#define ADV76XX_REG_SEQ_TERM		0xffff

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int adv76xx_read_reg(struct v4l2_subdev *sd, unsigned int reg)
{
	struct adv76xx_state *state = to_state(sd);
	unsigned int page = reg >> 8;

	if (!(BIT(page) & state->info->page_mask))
		return -EINVAL;

	reg &= 0xff;

	return adv_smbus_read_byte_data(state, page, reg);
}
#endif

static int adv76xx_write_reg(struct v4l2_subdev *sd, unsigned int reg, u8 val)
{
	struct adv76xx_state *state = to_state(sd);
	unsigned int page = reg >> 8;

	if (!(BIT(page) & state->info->page_mask))
		return -EINVAL;

	reg &= 0xff;

	return adv_smbus_write_byte_data(state, page, reg, val);
}

static void adv76xx_write_reg_seq(struct v4l2_subdev *sd,
				  const struct adv76xx_reg_seq *reg_seq)
{
	unsigned int i;

	for (i = 0; reg_seq[i].reg != ADV76XX_REG_SEQ_TERM; i++)
		adv76xx_write_reg(sd, reg_seq[i].reg, reg_seq[i].val);
}

/* -----------------------------------------------------------------------------
 * Format helpers
 */

static const struct adv76xx_format_info adv7604_formats[] = {
	{ MEDIA_BUS_FMT_RGB888_1X24, ADV76XX_OP_CH_SEL_RGB, true, false,
	  ADV76XX_OP_MODE_SEL_SDR_444 | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YUYV8_2X8, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YVYU8_2X8, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YUYV10_2X10, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV7604_OP_FORMAT_SEL_10BIT },
	{ MEDIA_BUS_FMT_YVYU10_2X10, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV7604_OP_FORMAT_SEL_10BIT },
	{ MEDIA_BUS_FMT_YUYV12_2X12, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_YVYU12_2X12, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_UYVY8_1X16, ADV76XX_OP_CH_SEL_RBG, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_VYUY8_1X16, ADV76XX_OP_CH_SEL_RBG, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YUYV8_1X16, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YVYU8_1X16, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_UYVY10_1X20, ADV76XX_OP_CH_SEL_RBG, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV7604_OP_FORMAT_SEL_10BIT },
	{ MEDIA_BUS_FMT_VYUY10_1X20, ADV76XX_OP_CH_SEL_RBG, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV7604_OP_FORMAT_SEL_10BIT },
	{ MEDIA_BUS_FMT_YUYV10_1X20, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV7604_OP_FORMAT_SEL_10BIT },
	{ MEDIA_BUS_FMT_YVYU10_1X20, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV7604_OP_FORMAT_SEL_10BIT },
	{ MEDIA_BUS_FMT_UYVY12_1X24, ADV76XX_OP_CH_SEL_RBG, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_VYUY12_1X24, ADV76XX_OP_CH_SEL_RBG, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_YUYV12_1X24, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_YVYU12_1X24, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_12BIT },
};

static const struct adv76xx_format_info adv7611_formats[] = {
	{ MEDIA_BUS_FMT_RGB888_1X24, ADV76XX_OP_CH_SEL_RGB, true, false,
	  ADV76XX_OP_MODE_SEL_SDR_444 | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YUYV8_2X8, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YVYU8_2X8, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YUYV12_2X12, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_YVYU12_2X12, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422 | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_UYVY8_1X16, ADV76XX_OP_CH_SEL_RBG, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_VYUY8_1X16, ADV76XX_OP_CH_SEL_RBG, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YUYV8_1X16, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_YVYU8_1X16, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_8BIT },
	{ MEDIA_BUS_FMT_UYVY12_1X24, ADV76XX_OP_CH_SEL_RBG, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_VYUY12_1X24, ADV76XX_OP_CH_SEL_RBG, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_YUYV12_1X24, ADV76XX_OP_CH_SEL_RGB, false, false,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_12BIT },
	{ MEDIA_BUS_FMT_YVYU12_1X24, ADV76XX_OP_CH_SEL_RGB, false, true,
	  ADV76XX_OP_MODE_SEL_SDR_422_2X | ADV76XX_OP_FORMAT_SEL_12BIT },
};

static const struct adv76xx_format_info *
adv76xx_format_info(struct adv76xx_state *state, u32 code)
{
	unsigned int i;

	for (i = 0; i < state->info->nformats; ++i) {
		if (state->info->formats[i].code == code)
			return &state->info->formats[i];
	}

	return NULL;
}

/* ----------------------------------------------------------------------- */

static inline bool is_analog_input(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);

	return state->selected_input == ADV7604_PAD_VGA_RGB ||
	       state->selected_input == ADV7604_PAD_VGA_COMP;
}

static inline bool is_digital_input(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);

	return state->selected_input == ADV76XX_PAD_HDMI_PORT_A ||
	       state->selected_input == ADV7604_PAD_HDMI_PORT_B ||
	       state->selected_input == ADV7604_PAD_HDMI_PORT_C ||
	       state->selected_input == ADV7604_PAD_HDMI_PORT_D;
}

/* ----------------------------------------------------------------------- */

#ifdef CONFIG_VIDEO_ADV_DEBUG
static void adv76xx_inv_register(struct v4l2_subdev *sd)
{
	v4l2_info(sd, "0x000-0x0ff: IO Map\n");
	v4l2_info(sd, "0x100-0x1ff: AVLink Map\n");
	v4l2_info(sd, "0x200-0x2ff: CEC Map\n");
	v4l2_info(sd, "0x300-0x3ff: InfoFrame Map\n");
	v4l2_info(sd, "0x400-0x4ff: ESDP Map\n");
	v4l2_info(sd, "0x500-0x5ff: DPP Map\n");
	v4l2_info(sd, "0x600-0x6ff: AFE Map\n");
	v4l2_info(sd, "0x700-0x7ff: Repeater Map\n");
	v4l2_info(sd, "0x800-0x8ff: EDID Map\n");
	v4l2_info(sd, "0x900-0x9ff: HDMI Map\n");
	v4l2_info(sd, "0xa00-0xaff: Test Map\n");
	v4l2_info(sd, "0xb00-0xbff: CP Map\n");
	v4l2_info(sd, "0xc00-0xcff: VDP Map\n");
}

static int adv76xx_g_register(struct v4l2_subdev *sd,
					struct v4l2_dbg_register *reg)
{
	int ret;

	ret = adv76xx_read_reg(sd, reg->reg);
	if (ret < 0) {
		v4l2_info(sd, "Register %03llx not supported\n", reg->reg);
		adv76xx_inv_register(sd);
		return ret;
	}

	reg->size = 1;
	reg->val = ret;

	return 0;
}

static int adv76xx_s_register(struct v4l2_subdev *sd,
					const struct v4l2_dbg_register *reg)
{
	int ret;

	ret = adv76xx_write_reg(sd, reg->reg, reg->val);
	if (ret < 0) {
		v4l2_info(sd, "Register %03llx not supported\n", reg->reg);
		adv76xx_inv_register(sd);
		return ret;
	}

	return 0;
}
#endif

static unsigned int adv7604_read_cable_det(struct v4l2_subdev *sd)
{
	u8 value = io_read(sd, 0x6f);

	return ((value & 0x10) >> 4)
	     | ((value & 0x08) >> 2)
	     | ((value & 0x04) << 0)
	     | ((value & 0x02) << 2);
}

static unsigned int adv7611_read_cable_det(struct v4l2_subdev *sd)
{
	u8 value = io_read(sd, 0x6f);

	return value & 1;
}

static int adv76xx_s_detect_tx_5v_ctrl(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;

	return v4l2_ctrl_s_ctrl(state->detect_tx_5v_ctrl,
				info->read_cable_det(sd));
}

static int find_and_set_predefined_video_timings(struct v4l2_subdev *sd,
		u8 prim_mode,
		const struct adv76xx_video_standards *predef_vid_timings,
		const struct v4l2_dv_timings *timings)
{
	int i;

	for (i = 0; predef_vid_timings[i].timings.bt.width; i++) {
		if (!v4l2_match_dv_timings(timings, &predef_vid_timings[i].timings,
					is_digital_input(sd) ? 250000 : 1000000))
			continue;
		io_write(sd, 0x00, predef_vid_timings[i].vid_std); /* video std */
		io_write(sd, 0x01, (predef_vid_timings[i].v_freq << 4) +
				prim_mode); /* v_freq and prim mode */
		return 0;
	}

	return -1;
}

static int configure_predefined_video_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct adv76xx_state *state = to_state(sd);
	int err;

	v4l2_dbg(1, debug, sd, "%s", __func__);

	if (adv76xx_has_afe(state)) {
		/* reset to default values */
		io_write(sd, 0x16, 0x43);
		io_write(sd, 0x17, 0x5a);
	}
	/* disable embedded syncs for auto graphics mode */
	cp_write_clr_set(sd, 0x81, 0x10, 0x00);
	cp_write(sd, 0x8f, 0x00);
	cp_write(sd, 0x90, 0x00);
	cp_write(sd, 0xa2, 0x00);
	cp_write(sd, 0xa3, 0x00);
	cp_write(sd, 0xa4, 0x00);
	cp_write(sd, 0xa5, 0x00);
	cp_write(sd, 0xa6, 0x00);
	cp_write(sd, 0xa7, 0x00);
	cp_write(sd, 0xab, 0x00);
	cp_write(sd, 0xac, 0x00);

	if (is_analog_input(sd)) {
		err = find_and_set_predefined_video_timings(sd,
				0x01, adv7604_prim_mode_comp, timings);
		if (err)
			err = find_and_set_predefined_video_timings(sd,
					0x02, adv7604_prim_mode_gr, timings);
	} else if (is_digital_input(sd)) {
		err = find_and_set_predefined_video_timings(sd,
				0x05, adv76xx_prim_mode_hdmi_comp, timings);
		if (err)
			err = find_and_set_predefined_video_timings(sd,
					0x06, adv76xx_prim_mode_hdmi_gr, timings);
	} else {
		v4l2_dbg(2, debug, sd, "%s: Unknown port %d selected\n",
				__func__, state->selected_input);
		err = -1;
	}


	return err;
}

static void configure_custom_video_timings(struct v4l2_subdev *sd,
		const struct v4l2_bt_timings *bt)
{
	struct adv76xx_state *state = to_state(sd);
	u32 width = htotal(bt);
	u32 height = vtotal(bt);
	u16 cp_start_sav = bt->hsync + bt->hbackporch - 4;
	u16 cp_start_eav = width - bt->hfrontporch;
	u16 cp_start_vbi = height - bt->vfrontporch;
	u16 cp_end_vbi = bt->vsync + bt->vbackporch;
	u16 ch1_fr_ll = (((u32)bt->pixelclock / 100) > 0) ?
		((width * (ADV76XX_FSC / 100)) / ((u32)bt->pixelclock / 100)) : 0;
	const u8 pll[2] = {
		0xc0 | ((width >> 8) & 0x1f),
		width & 0xff
	};

	v4l2_dbg(2, debug, sd, "%s\n", __func__);

	if (is_analog_input(sd)) {
		/* auto graphics */
		io_write(sd, 0x00, 0x07); /* video std */
		io_write(sd, 0x01, 0x02); /* prim mode */
		/* enable embedded syncs for auto graphics mode */
		cp_write_clr_set(sd, 0x81, 0x10, 0x10);

		/* Should only be set in auto-graphics mode [REF_02, p. 91-92] */
		/* setup PLL_DIV_MAN_EN and PLL_DIV_RATIO */
		/* IO-map reg. 0x16 and 0x17 should be written in sequence */
		if (adv_smbus_write_i2c_block_data(state, ADV76XX_PAGE_IO,
						   0x16, 2, pll))
			v4l2_err(sd, "writing to reg 0x16 and 0x17 failed\n");

		/* active video - horizontal timing */
		cp_write(sd, 0xa2, (cp_start_sav >> 4) & 0xff);
		cp_write(sd, 0xa3, ((cp_start_sav & 0x0f) << 4) |
				   ((cp_start_eav >> 8) & 0x0f));
		cp_write(sd, 0xa4, cp_start_eav & 0xff);

		/* active video - vertical timing */
		cp_write(sd, 0xa5, (cp_start_vbi >> 4) & 0xff);
		cp_write(sd, 0xa6, ((cp_start_vbi & 0xf) << 4) |
				   ((cp_end_vbi >> 8) & 0xf));
		cp_write(sd, 0xa7, cp_end_vbi & 0xff);
	} else if (is_digital_input(sd)) {
		/* set default prim_mode/vid_std for HDMI
		   according to [REF_03, c. 4.2] */
		io_write(sd, 0x00, 0x02); /* video std */
		io_write(sd, 0x01, 0x06); /* prim mode */
	} else {
		v4l2_dbg(2, debug, sd, "%s: Unknown port %d selected\n",
				__func__, state->selected_input);
	}

	cp_write(sd, 0x8f, (ch1_fr_ll >> 8) & 0x7);
	cp_write(sd, 0x90, ch1_fr_ll & 0xff);
	cp_write(sd, 0xab, (height >> 4) & 0xff);
	cp_write(sd, 0xac, (height & 0x0f) << 4);
}

static void adv76xx_set_offset(struct v4l2_subdev *sd, bool auto_offset, u16 offset_a, u16 offset_b, u16 offset_c)
{
	struct adv76xx_state *state = to_state(sd);
	u8 offset_buf[4];

	if (auto_offset) {
		offset_a = 0x3ff;
		offset_b = 0x3ff;
		offset_c = 0x3ff;
	}

	v4l2_dbg(2, debug, sd, "%s: %s offset: a = 0x%x, b = 0x%x, c = 0x%x\n",
			__func__, auto_offset ? "Auto" : "Manual",
			offset_a, offset_b, offset_c);

	offset_buf[0] = (cp_read(sd, 0x77) & 0xc0) | ((offset_a & 0x3f0) >> 4);
	offset_buf[1] = ((offset_a & 0x00f) << 4) | ((offset_b & 0x3c0) >> 6);
	offset_buf[2] = ((offset_b & 0x03f) << 2) | ((offset_c & 0x300) >> 8);
	offset_buf[3] = offset_c & 0x0ff;

	/* Registers must be written in this order with no i2c access in between */
	if (adv_smbus_write_i2c_block_data(state, ADV76XX_PAGE_CP,
					   0x77, 4, offset_buf))
		v4l2_err(sd, "%s: i2c error writing to CP reg 0x77, 0x78, 0x79, 0x7a\n", __func__);
}

static void adv76xx_set_gain(struct v4l2_subdev *sd, bool auto_gain, u16 gain_a, u16 gain_b, u16 gain_c)
{
	struct adv76xx_state *state = to_state(sd);
	u8 gain_buf[4];
	u8 gain_man = 1;
	u8 agc_mode_man = 1;

	if (auto_gain) {
		gain_man = 0;
		agc_mode_man = 0;
		gain_a = 0x100;
		gain_b = 0x100;
		gain_c = 0x100;
	}

	v4l2_dbg(2, debug, sd, "%s: %s gain: a = 0x%x, b = 0x%x, c = 0x%x\n",
			__func__, auto_gain ? "Auto" : "Manual",
			gain_a, gain_b, gain_c);

	gain_buf[0] = ((gain_man << 7) | (agc_mode_man << 6) | ((gain_a & 0x3f0) >> 4));
	gain_buf[1] = (((gain_a & 0x00f) << 4) | ((gain_b & 0x3c0) >> 6));
	gain_buf[2] = (((gain_b & 0x03f) << 2) | ((gain_c & 0x300) >> 8));
	gain_buf[3] = ((gain_c & 0x0ff));

	/* Registers must be written in this order with no i2c access in between */
	if (adv_smbus_write_i2c_block_data(state, ADV76XX_PAGE_CP,
					   0x73, 4, gain_buf))
		v4l2_err(sd, "%s: i2c error writing to CP reg 0x73, 0x74, 0x75, 0x76\n", __func__);
}

static void set_rgb_quantization_range(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);
	bool rgb_output = io_read(sd, 0x02) & 0x02;
	bool hdmi_signal = hdmi_read(sd, 0x05) & 0x80;

	v4l2_dbg(2, debug, sd, "%s: RGB quantization range: %d, RGB out: %d, HDMI: %d\n",
			__func__, state->rgb_quantization_range,
			rgb_output, hdmi_signal);

	adv76xx_set_gain(sd, true, 0x0, 0x0, 0x0);
	adv76xx_set_offset(sd, true, 0x0, 0x0, 0x0);

	switch (state->rgb_quantization_range) {
	case V4L2_DV_RGB_RANGE_AUTO:
		if (state->selected_input == ADV7604_PAD_VGA_RGB) {
			/* Receiving analog RGB signal
			 * Set RGB full range (0-255) */
			io_write_clr_set(sd, 0x02, 0xf0, 0x10);
			break;
		}

		if (state->selected_input == ADV7604_PAD_VGA_COMP) {
			/* Receiving analog YPbPr signal
			 * Set automode */
			io_write_clr_set(sd, 0x02, 0xf0, 0xf0);
			break;
		}

		if (hdmi_signal) {
			/* Receiving HDMI signal
			 * Set automode */
			io_write_clr_set(sd, 0x02, 0xf0, 0xf0);
			break;
		}

		/* Receiving DVI-D signal
		 * ADV7604 selects RGB limited range regardless of
		 * input format (CE/IT) in automatic mode */
		if (state->timings.bt.flags & V4L2_DV_FL_IS_CE_VIDEO) {
			/* RGB limited range (16-235) */
			io_write_clr_set(sd, 0x02, 0xf0, 0x00);
		} else {
			/* RGB full range (0-255) */
			io_write_clr_set(sd, 0x02, 0xf0, 0x10);

			if (is_digital_input(sd) && rgb_output) {
				adv76xx_set_offset(sd, false, 0x40, 0x40, 0x40);
			} else {
				adv76xx_set_gain(sd, false, 0xe0, 0xe0, 0xe0);
				adv76xx_set_offset(sd, false, 0x70, 0x70, 0x70);
			}
		}
		break;
	case V4L2_DV_RGB_RANGE_LIMITED:
		if (state->selected_input == ADV7604_PAD_VGA_COMP) {
			/* YCrCb limited range (16-235) */
			io_write_clr_set(sd, 0x02, 0xf0, 0x20);
			break;
		}

		/* RGB limited range (16-235) */
		io_write_clr_set(sd, 0x02, 0xf0, 0x00);

		break;
	case V4L2_DV_RGB_RANGE_FULL:
		if (state->selected_input == ADV7604_PAD_VGA_COMP) {
			/* YCrCb full range (0-255) */
			io_write_clr_set(sd, 0x02, 0xf0, 0x60);
			break;
		}

		/* RGB full range (0-255) */
		io_write_clr_set(sd, 0x02, 0xf0, 0x10);

		if (is_analog_input(sd) || hdmi_signal)
			break;

		/* Adjust gain/offset for DVI-D signals only */
		if (rgb_output) {
			adv76xx_set_offset(sd, false, 0x40, 0x40, 0x40);
		} else {
			adv76xx_set_gain(sd, false, 0xe0, 0xe0, 0xe0);
			adv76xx_set_offset(sd, false, 0x70, 0x70, 0x70);
		}
		break;
	}
}

static int adv76xx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd =
		&container_of(ctrl->handler, struct adv76xx_state, hdl)->sd;

	struct adv76xx_state *state = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		cp_write(sd, 0x3c, ctrl->val);
		return 0;
	case V4L2_CID_CONTRAST:
		cp_write(sd, 0x3a, ctrl->val);
		return 0;
	case V4L2_CID_SATURATION:
		cp_write(sd, 0x3b, ctrl->val);
		return 0;
	case V4L2_CID_HUE:
		cp_write(sd, 0x3d, ctrl->val);
		return 0;
	case  V4L2_CID_DV_RX_RGB_RANGE:
		state->rgb_quantization_range = ctrl->val;
		set_rgb_quantization_range(sd);
		return 0;
	case V4L2_CID_ADV_RX_ANALOG_SAMPLING_PHASE:
		if (!adv76xx_has_afe(state))
			return -EINVAL;
		/* Set the analog sampling phase. This is needed to find the
		   best sampling phase for analog video: an application or
		   driver has to try a number of phases and analyze the picture
		   quality before settling on the best performing phase. */
		afe_write(sd, 0xc8, ctrl->val);
		return 0;
	case V4L2_CID_ADV_RX_FREE_RUN_COLOR_MANUAL:
		/* Use the default blue color for free running mode,
		   or supply your own. */
		cp_write_clr_set(sd, 0xbf, 0x04, ctrl->val << 2);
		return 0;
	case V4L2_CID_ADV_RX_FREE_RUN_COLOR:
		cp_write(sd, 0xc0, (ctrl->val & 0xff0000) >> 16);
		cp_write(sd, 0xc1, (ctrl->val & 0x00ff00) >> 8);
		cp_write(sd, 0xc2, (u8)(ctrl->val & 0x0000ff));
		return 0;
	}
	return -EINVAL;
}

/* ----------------------------------------------------------------------- */

static inline bool no_power(struct v4l2_subdev *sd)
{
	/* Entire chip or CP powered off */
	return io_read(sd, 0x0c) & 0x24;
}

static inline bool no_signal_tmds(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);

	return !(io_read(sd, 0x6a) & (0x10 >> state->selected_input));
}

static inline bool no_lock_tmds(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;

	return (io_read(sd, 0x6a) & info->tdms_lock_mask) != info->tdms_lock_mask;
}

static inline bool is_hdmi(struct v4l2_subdev *sd)
{
	return hdmi_read(sd, 0x05) & 0x80;
}

static inline bool no_lock_sspd(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);

	/*
	 * Chips without a AFE don't expose registers for the SSPD, so just assume
	 * that we have a lock.
	 */
	if (adv76xx_has_afe(state))
		return false;

	/* TODO channel 2 */
	return ((cp_read(sd, 0xb5) & 0xd0) != 0xd0);
}

static inline bool no_lock_stdi(struct v4l2_subdev *sd)
{
	/* TODO channel 2 */
	return !(cp_read(sd, 0xb1) & 0x80);
}

static inline bool no_signal(struct v4l2_subdev *sd)
{
	bool ret;

	ret = no_power(sd);

	ret |= no_lock_stdi(sd);
	ret |= no_lock_sspd(sd);

	if (is_digital_input(sd)) {
		ret |= no_lock_tmds(sd);
		ret |= no_signal_tmds(sd);
	}

	return ret;
}

static inline bool no_lock_cp(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);

	if (!adv76xx_has_afe(state))
		return false;

	/* CP has detected a non standard number of lines on the incoming
	   video compared to what it is configured to receive by s_dv_timings */
	return io_read(sd, 0x12) & 0x01;
}

static inline bool in_free_run(struct v4l2_subdev *sd)
{
	return cp_read(sd, 0xff) & 0x10;
}

static int adv76xx_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	*status |= no_power(sd) ? V4L2_IN_ST_NO_POWER : 0;
	*status |= no_signal(sd) ? V4L2_IN_ST_NO_SIGNAL : 0;
	if (!in_free_run(sd) && no_lock_cp(sd))
		*status |= is_digital_input(sd) ?
			   V4L2_IN_ST_NO_SYNC : V4L2_IN_ST_NO_H_LOCK;

	v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status);

	return 0;
}

/* ----------------------------------------------------------------------- */

struct stdi_readback {
	u16 bl, lcf, lcvs;
	u8 hs_pol, vs_pol;
	bool interlaced;
};

static int stdi2dv_timings(struct v4l2_subdev *sd,
		struct stdi_readback *stdi,
		struct v4l2_dv_timings *timings)
{
	struct adv76xx_state *state = to_state(sd);
	u32 hfreq = (ADV76XX_FSC * 8) / stdi->bl;
	u32 pix_clk;
	int i;

	for (i = 0; adv76xx_timings[i].bt.height; i++) {
		if (vtotal(&adv76xx_timings[i].bt) != stdi->lcf + 1)
			continue;
		if (adv76xx_timings[i].bt.vsync != stdi->lcvs)
			continue;

		pix_clk = hfreq * htotal(&adv76xx_timings[i].bt);

		if ((pix_clk < adv76xx_timings[i].bt.pixelclock + 1000000) &&
		    (pix_clk > adv76xx_timings[i].bt.pixelclock - 1000000)) {
			*timings = adv76xx_timings[i];
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
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;
	u8 polarity;

	if (no_lock_stdi(sd) || no_lock_sspd(sd)) {
		v4l2_dbg(2, debug, sd, "%s: STDI and/or SSPD not locked\n", __func__);
		return -1;
	}

	/* read STDI */
	stdi->bl = cp_read16(sd, 0xb1, 0x3fff);
	stdi->lcf = cp_read16(sd, info->lcf_reg, 0x7ff);
	stdi->lcvs = cp_read(sd, 0xb3) >> 3;
	stdi->interlaced = io_read(sd, 0x12) & 0x10;

	if (adv76xx_has_afe(state)) {
		/* read SSPD */
		polarity = cp_read(sd, 0xb5);
		if ((polarity & 0x03) == 0x01) {
			stdi->hs_pol = polarity & 0x10
				     ? (polarity & 0x08 ? '+' : '-') : 'x';
			stdi->vs_pol = polarity & 0x40
				     ? (polarity & 0x20 ? '+' : '-') : 'x';
		} else {
			stdi->hs_pol = 'x';
			stdi->vs_pol = 'x';
		}
	} else {
		polarity = hdmi_read(sd, 0x05);
		stdi->hs_pol = polarity & 0x20 ? '+' : '-';
		stdi->vs_pol = polarity & 0x10 ? '+' : '-';
	}

	if (no_lock_stdi(sd) || no_lock_sspd(sd)) {
		v4l2_dbg(2, debug, sd,
			"%s: signal lost during readout of STDI/SSPD\n", __func__);
		return -1;
	}

	if (stdi->lcf < 239 || stdi->bl < 8 || stdi->bl == 0x3fff) {
		v4l2_dbg(2, debug, sd, "%s: invalid signal\n", __func__);
		memset(stdi, 0, sizeof(struct stdi_readback));
		return -1;
	}

	v4l2_dbg(2, debug, sd,
		"%s: lcf (frame height - 1) = %d, bl = %d, lcvs (vsync) = %d, %chsync, %cvsync, %s\n",
		__func__, stdi->lcf, stdi->bl, stdi->lcvs,
		stdi->hs_pol, stdi->vs_pol,
		stdi->interlaced ? "interlaced" : "progressive");

	return 0;
}

static int adv76xx_enum_dv_timings(struct v4l2_subdev *sd,
			struct v4l2_enum_dv_timings *timings)
{
	struct adv76xx_state *state = to_state(sd);

	if (timings->index >= ARRAY_SIZE(adv76xx_timings) - 1)
		return -EINVAL;

	if (timings->pad >= state->source_pad)
		return -EINVAL;

	memset(timings->reserved, 0, sizeof(timings->reserved));
	timings->timings = adv76xx_timings[timings->index];
	return 0;
}

static int adv76xx_dv_timings_cap(struct v4l2_subdev *sd,
			struct v4l2_dv_timings_cap *cap)
{
	struct adv76xx_state *state = to_state(sd);

	if (cap->pad >= state->source_pad)
		return -EINVAL;

	cap->type = V4L2_DV_BT_656_1120;
	cap->bt.max_width = 1920;
	cap->bt.max_height = 1200;
	cap->bt.min_pixelclock = 25000000;

	switch (cap->pad) {
	case ADV76XX_PAD_HDMI_PORT_A:
	case ADV7604_PAD_HDMI_PORT_B:
	case ADV7604_PAD_HDMI_PORT_C:
	case ADV7604_PAD_HDMI_PORT_D:
		cap->bt.max_pixelclock = 225000000;
		break;
	case ADV7604_PAD_VGA_RGB:
	case ADV7604_PAD_VGA_COMP:
	default:
		cap->bt.max_pixelclock = 170000000;
		break;
	}

	cap->bt.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			 V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT;
	cap->bt.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
		V4L2_DV_BT_CAP_REDUCED_BLANKING | V4L2_DV_BT_CAP_CUSTOM;
	return 0;
}

/* Fill the optional fields .standards and .flags in struct v4l2_dv_timings
   if the format is listed in adv76xx_timings[] */
static void adv76xx_fill_optional_dv_timings_fields(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	int i;

	for (i = 0; adv76xx_timings[i].bt.width; i++) {
		if (v4l2_match_dv_timings(timings, &adv76xx_timings[i],
					is_digital_input(sd) ? 250000 : 1000000)) {
			*timings = adv76xx_timings[i];
			break;
		}
	}
}

static unsigned int adv7604_read_hdmi_pixelclock(struct v4l2_subdev *sd)
{
	unsigned int freq;
	int a, b;

	a = hdmi_read(sd, 0x06);
	b = hdmi_read(sd, 0x3b);
	if (a < 0 || b < 0)
		return 0;
	freq =  a * 1000000 + ((b & 0x30) >> 4) * 250000;

	if (is_hdmi(sd)) {
		/* adjust for deep color mode */
		unsigned bits_per_channel = ((hdmi_read(sd, 0x0b) & 0x60) >> 4) + 8;

		freq = freq * 8 / bits_per_channel;
	}

	return freq;
}

static unsigned int adv7611_read_hdmi_pixelclock(struct v4l2_subdev *sd)
{
	int a, b;

	a = hdmi_read(sd, 0x51);
	b = hdmi_read(sd, 0x52);
	if (a < 0 || b < 0)
		return 0;
	return ((a << 1) | (b >> 7)) * 1000000 + (b & 0x7f) * 1000000 / 128;
}

static int adv76xx_query_dv_timings(struct v4l2_subdev *sd,
			struct v4l2_dv_timings *timings)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;
	struct v4l2_bt_timings *bt = &timings->bt;
	struct stdi_readback stdi;

	if (!timings)
		return -EINVAL;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));

	if (no_signal(sd)) {
		state->restart_stdi_once = true;
		v4l2_dbg(1, debug, sd, "%s: no valid signal\n", __func__);
		return -ENOLINK;
	}

	/* read STDI */
	if (read_stdi(sd, &stdi)) {
		v4l2_dbg(1, debug, sd, "%s: STDI/SSPD not locked\n", __func__);
		return -ENOLINK;
	}
	bt->interlaced = stdi.interlaced ?
		V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;

	if (is_digital_input(sd)) {
		timings->type = V4L2_DV_BT_656_1120;

		bt->width = hdmi_read16(sd, 0x07, info->linewidth_mask);
		bt->height = hdmi_read16(sd, 0x09, info->field0_height_mask);
		bt->pixelclock = info->read_hdmi_pixelclock(sd);
		bt->hfrontporch = hdmi_read16(sd, 0x20, info->hfrontporch_mask);
		bt->hsync = hdmi_read16(sd, 0x22, info->hsync_mask);
		bt->hbackporch = hdmi_read16(sd, 0x24, info->hbackporch_mask);
		bt->vfrontporch = hdmi_read16(sd, 0x2a,
			info->field0_vfrontporch_mask) / 2;
		bt->vsync = hdmi_read16(sd, 0x2e, info->field0_vsync_mask) / 2;
		bt->vbackporch = hdmi_read16(sd, 0x32,
			info->field0_vbackporch_mask) / 2;
		bt->polarities = ((hdmi_read(sd, 0x05) & 0x10) ? V4L2_DV_VSYNC_POS_POL : 0) |
			((hdmi_read(sd, 0x05) & 0x20) ? V4L2_DV_HSYNC_POS_POL : 0);
		if (bt->interlaced == V4L2_DV_INTERLACED) {
			bt->height += hdmi_read16(sd, 0x0b,
				info->field1_height_mask);
			bt->il_vfrontporch = hdmi_read16(sd, 0x2c,
				info->field1_vfrontporch_mask) / 2;
			bt->il_vsync = hdmi_read16(sd, 0x30,
				info->field1_vsync_mask) / 2;
			bt->il_vbackporch = hdmi_read16(sd, 0x34,
				info->field1_vbackporch_mask) / 2;
		}
		adv76xx_fill_optional_dv_timings_fields(sd, timings);
	} else {
		/* find format
		 * Since LCVS values are inaccurate [REF_03, p. 275-276],
		 * stdi2dv_timings() is called with lcvs +-1 if the first attempt fails.
		 */
		if (!stdi2dv_timings(sd, &stdi, timings))
			goto found;
		stdi.lcvs += 1;
		v4l2_dbg(1, debug, sd, "%s: lcvs + 1 = %d\n", __func__, stdi.lcvs);
		if (!stdi2dv_timings(sd, &stdi, timings))
			goto found;
		stdi.lcvs -= 2;
		v4l2_dbg(1, debug, sd, "%s: lcvs - 1 = %d\n", __func__, stdi.lcvs);
		if (stdi2dv_timings(sd, &stdi, timings)) {
			/*
			 * The STDI block may measure wrong values, especially
			 * for lcvs and lcf. If the driver can not find any
			 * valid timing, the STDI block is restarted to measure
			 * the video timings again. The function will return an
			 * error, but the restart of STDI will generate a new
			 * STDI interrupt and the format detection process will
			 * restart.
			 */
			if (state->restart_stdi_once) {
				v4l2_dbg(1, debug, sd, "%s: restart STDI\n", __func__);
				/* TODO restart STDI for Sync Channel 2 */
				/* enter one-shot mode */
				cp_write_clr_set(sd, 0x86, 0x06, 0x00);
				/* trigger STDI restart */
				cp_write_clr_set(sd, 0x86, 0x06, 0x04);
				/* reset to continuous mode */
				cp_write_clr_set(sd, 0x86, 0x06, 0x02);
				state->restart_stdi_once = false;
				return -ENOLINK;
			}
			v4l2_dbg(1, debug, sd, "%s: format not supported\n", __func__);
			return -ERANGE;
		}
		state->restart_stdi_once = true;
	}
found:

	if (no_signal(sd)) {
		v4l2_dbg(1, debug, sd, "%s: signal lost during readout\n", __func__);
		memset(timings, 0, sizeof(struct v4l2_dv_timings));
		return -ENOLINK;
	}

	if ((is_analog_input(sd) && bt->pixelclock > 170000000) ||
			(is_digital_input(sd) && bt->pixelclock > 225000000)) {
		v4l2_dbg(1, debug, sd, "%s: pixelclock out of range %d\n",
				__func__, (u32)bt->pixelclock);
		return -ERANGE;
	}

	if (debug > 1)
		v4l2_print_dv_timings(sd->name, "adv76xx_query_dv_timings: ",
				      timings, true);

	return 0;
}

static int adv76xx_s_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct adv76xx_state *state = to_state(sd);
	struct v4l2_bt_timings *bt;
	int err;

	if (!timings)
		return -EINVAL;

	if (v4l2_match_dv_timings(&state->timings, timings, 0)) {
		v4l2_dbg(1, debug, sd, "%s: no change\n", __func__);
		return 0;
	}

	bt = &timings->bt;

	if ((is_analog_input(sd) && bt->pixelclock > 170000000) ||
			(is_digital_input(sd) && bt->pixelclock > 225000000)) {
		v4l2_dbg(1, debug, sd, "%s: pixelclock out of range %d\n",
				__func__, (u32)bt->pixelclock);
		return -ERANGE;
	}

	adv76xx_fill_optional_dv_timings_fields(sd, timings);

	state->timings = *timings;

	cp_write_clr_set(sd, 0x91, 0x40, bt->interlaced ? 0x40 : 0x00);

	/* Use prim_mode and vid_std when available */
	err = configure_predefined_video_timings(sd, timings);
	if (err) {
		/* custom settings when the video format
		 does not have prim_mode/vid_std */
		configure_custom_video_timings(sd, bt);
	}

	set_rgb_quantization_range(sd);

	if (debug > 1)
		v4l2_print_dv_timings(sd->name, "adv76xx_s_dv_timings: ",
				      timings, true);
	return 0;
}

static int adv76xx_g_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct adv76xx_state *state = to_state(sd);

	*timings = state->timings;
	return 0;
}

static void adv7604_set_termination(struct v4l2_subdev *sd, bool enable)
{
	hdmi_write(sd, 0x01, enable ? 0x00 : 0x78);
}

static void adv7611_set_termination(struct v4l2_subdev *sd, bool enable)
{
	hdmi_write(sd, 0x83, enable ? 0xfe : 0xff);
}

static void enable_input(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);

	if (is_analog_input(sd)) {
		io_write(sd, 0x15, 0xb0);   /* Disable Tristate of Pins (no audio) */
	} else if (is_digital_input(sd)) {
		hdmi_write_clr_set(sd, 0x00, 0x03, state->selected_input);
		state->info->set_termination(sd, true);
		io_write(sd, 0x15, 0xa0);   /* Disable Tristate of Pins */
		hdmi_write_clr_set(sd, 0x1a, 0x10, 0x00); /* Unmute audio */
	} else {
		v4l2_dbg(2, debug, sd, "%s: Unknown port %d selected\n",
				__func__, state->selected_input);
	}
}

static void disable_input(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);

	hdmi_write_clr_set(sd, 0x1a, 0x10, 0x10); /* Mute audio */
	msleep(16); /* 512 samples with >= 32 kHz sample rate [REF_03, c. 7.16.10] */
	io_write(sd, 0x15, 0xbe);   /* Tristate all outputs from video core */
	state->info->set_termination(sd, false);
}

static void select_input(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;

	if (is_analog_input(sd)) {
		adv76xx_write_reg_seq(sd, info->recommended_settings[0]);

		afe_write(sd, 0x00, 0x08); /* power up ADC */
		afe_write(sd, 0x01, 0x06); /* power up Analog Front End */
		afe_write(sd, 0xc8, 0x00); /* phase control */
	} else if (is_digital_input(sd)) {
		hdmi_write(sd, 0x00, state->selected_input & 0x03);

		adv76xx_write_reg_seq(sd, info->recommended_settings[1]);

		if (adv76xx_has_afe(state)) {
			afe_write(sd, 0x00, 0xff); /* power down ADC */
			afe_write(sd, 0x01, 0xfe); /* power down Analog Front End */
			afe_write(sd, 0xc8, 0x40); /* phase control */
		}

		cp_write(sd, 0x3e, 0x00); /* CP core pre-gain control */
		cp_write(sd, 0xc3, 0x39); /* CP coast control. Graphics mode */
		cp_write(sd, 0x40, 0x80); /* CP core pre-gain control. Graphics mode */
	} else {
		v4l2_dbg(2, debug, sd, "%s: Unknown port %d selected\n",
				__func__, state->selected_input);
	}
}

static int adv76xx_s_routing(struct v4l2_subdev *sd,
		u32 input, u32 output, u32 config)
{
	struct adv76xx_state *state = to_state(sd);

	v4l2_dbg(2, debug, sd, "%s: input %d, selected input %d",
			__func__, input, state->selected_input);

	if (input == state->selected_input)
		return 0;

	if (input > state->info->max_port)
		return -EINVAL;

	state->selected_input = input;

	disable_input(sd);

	select_input(sd);

	enable_input(sd);

	return 0;
}

static int adv76xx_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct adv76xx_state *state = to_state(sd);

	if (code->index >= state->info->nformats)
		return -EINVAL;

	code->code = state->info->formats[code->index].code;

	return 0;
}

static void adv76xx_fill_format(struct adv76xx_state *state,
				struct v4l2_mbus_framefmt *format)
{
	memset(format, 0, sizeof(*format));

	format->width = state->timings.bt.width;
	format->height = state->timings.bt.height;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	if (state->timings.bt.flags & V4L2_DV_FL_IS_CE_VIDEO)
		format->colorspace = (state->timings.bt.height <= 576) ?
			V4L2_COLORSPACE_SMPTE170M : V4L2_COLORSPACE_REC709;
}

/*
 * Compute the op_ch_sel value required to obtain on the bus the component order
 * corresponding to the selected format taking into account bus reordering
 * applied by the board at the output of the device.
 *
 * The following table gives the op_ch_value from the format component order
 * (expressed as op_ch_sel value in column) and the bus reordering (expressed as
 * adv76xx_bus_order value in row).
 *
 *           |	GBR(0)	GRB(1)	BGR(2)	RGB(3)	BRG(4)	RBG(5)
 * ----------+-------------------------------------------------
 * RGB (NOP) |	GBR	GRB	BGR	RGB	BRG	RBG
 * GRB (1-2) |	BGR	RGB	GBR	GRB	RBG	BRG
 * RBG (2-3) |	GRB	GBR	BRG	RBG	BGR	RGB
 * BGR (1-3) |	RBG	BRG	RGB	BGR	GRB	GBR
 * BRG (ROR) |	BRG	RBG	GRB	GBR	RGB	BGR
 * GBR (ROL) |	RGB	BGR	RBG	BRG	GBR	GRB
 */
static unsigned int adv76xx_op_ch_sel(struct adv76xx_state *state)
{
#define _SEL(a,b,c,d,e,f)	{ \
	ADV76XX_OP_CH_SEL_##a, ADV76XX_OP_CH_SEL_##b, ADV76XX_OP_CH_SEL_##c, \
	ADV76XX_OP_CH_SEL_##d, ADV76XX_OP_CH_SEL_##e, ADV76XX_OP_CH_SEL_##f }
#define _BUS(x)			[ADV7604_BUS_ORDER_##x]

	static const unsigned int op_ch_sel[6][6] = {
		_BUS(RGB) /* NOP */ = _SEL(GBR, GRB, BGR, RGB, BRG, RBG),
		_BUS(GRB) /* 1-2 */ = _SEL(BGR, RGB, GBR, GRB, RBG, BRG),
		_BUS(RBG) /* 2-3 */ = _SEL(GRB, GBR, BRG, RBG, BGR, RGB),
		_BUS(BGR) /* 1-3 */ = _SEL(RBG, BRG, RGB, BGR, GRB, GBR),
		_BUS(BRG) /* ROR */ = _SEL(BRG, RBG, GRB, GBR, RGB, BGR),
		_BUS(GBR) /* ROL */ = _SEL(RGB, BGR, RBG, BRG, GBR, GRB),
	};

	return op_ch_sel[state->pdata.bus_order][state->format->op_ch_sel >> 5];
}

static void adv76xx_setup_format(struct adv76xx_state *state)
{
	struct v4l2_subdev *sd = &state->sd;

	io_write_clr_set(sd, 0x02, 0x02,
			state->format->rgb_out ? ADV76XX_RGB_OUT : 0);
	io_write(sd, 0x03, state->format->op_format_sel |
		 state->pdata.op_format_mode_sel);
	io_write_clr_set(sd, 0x04, 0xe0, adv76xx_op_ch_sel(state));
	io_write_clr_set(sd, 0x05, 0x01,
			state->format->swap_cb_cr ? ADV76XX_OP_SWAP_CB_CR : 0);
}

static int adv76xx_get_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *format)
{
	struct adv76xx_state *state = to_state(sd);

	if (format->pad != state->source_pad)
		return -EINVAL;

	adv76xx_fill_format(state, &format->format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		format->format.code = fmt->code;
	} else {
		format->format.code = state->format->code;
	}

	return 0;
}

static int adv76xx_set_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *format)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_format_info *info;

	if (format->pad != state->source_pad)
		return -EINVAL;

	info = adv76xx_format_info(state, format->format.code);
	if (info == NULL)
		info = adv76xx_format_info(state, MEDIA_BUS_FMT_YUYV8_2X8);

	adv76xx_fill_format(state, &format->format);
	format->format.code = info->code;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		fmt->code = format->format.code;
	} else {
		state->format = info;
		adv76xx_setup_format(state);
	}

	return 0;
}

static int adv76xx_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;
	const u8 irq_reg_0x43 = io_read(sd, 0x43);
	const u8 irq_reg_0x6b = io_read(sd, 0x6b);
	const u8 irq_reg_0x70 = io_read(sd, 0x70);
	u8 fmt_change_digital;
	u8 fmt_change;
	u8 tx_5v;

	if (irq_reg_0x43)
		io_write(sd, 0x44, irq_reg_0x43);
	if (irq_reg_0x70)
		io_write(sd, 0x71, irq_reg_0x70);
	if (irq_reg_0x6b)
		io_write(sd, 0x6c, irq_reg_0x6b);

	v4l2_dbg(2, debug, sd, "%s: ", __func__);

	/* format change */
	fmt_change = irq_reg_0x43 & 0x98;
	fmt_change_digital = is_digital_input(sd)
			   ? irq_reg_0x6b & info->fmt_change_digital_mask
			   : 0;

	if (fmt_change || fmt_change_digital) {
		v4l2_dbg(1, debug, sd,
			"%s: fmt_change = 0x%x, fmt_change_digital = 0x%x\n",
			__func__, fmt_change, fmt_change_digital);

		v4l2_subdev_notify(sd, ADV76XX_FMT_CHANGE, NULL);

		if (handled)
			*handled = true;
	}
	/* HDMI/DVI mode */
	if (irq_reg_0x6b & 0x01) {
		v4l2_dbg(1, debug, sd, "%s: irq %s mode\n", __func__,
			(io_read(sd, 0x6a) & 0x01) ? "HDMI" : "DVI");
		set_rgb_quantization_range(sd);
		if (handled)
			*handled = true;
	}

	/* tx 5v detect */
	tx_5v = io_read(sd, 0x70) & info->cable_det_mask;
	if (tx_5v) {
		v4l2_dbg(1, debug, sd, "%s: tx_5v: 0x%x\n", __func__, tx_5v);
		io_write(sd, 0x71, tx_5v);
		adv76xx_s_detect_tx_5v_ctrl(sd);
		if (handled)
			*handled = true;
	}
	return 0;
}

static int adv76xx_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct adv76xx_state *state = to_state(sd);
	u8 *data = NULL;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	switch (edid->pad) {
	case ADV76XX_PAD_HDMI_PORT_A:
	case ADV7604_PAD_HDMI_PORT_B:
	case ADV7604_PAD_HDMI_PORT_C:
	case ADV7604_PAD_HDMI_PORT_D:
		if (state->edid.present & (1 << edid->pad))
			data = state->edid.edid;
		break;
	default:
		return -EINVAL;
	}

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = data ? state->edid.blocks : 0;
		return 0;
	}

	if (data == NULL)
		return -ENODATA;

	if (edid->start_block >= state->edid.blocks)
		return -EINVAL;

	if (edid->start_block + edid->blocks > state->edid.blocks)
		edid->blocks = state->edid.blocks - edid->start_block;

	memcpy(edid->edid, data + edid->start_block * 128, edid->blocks * 128);

	return 0;
}

static int get_edid_spa_location(const u8 *edid)
{
	u8 d;

	if ((edid[0x7e] != 1) ||
	    (edid[0x80] != 0x02) ||
	    (edid[0x81] != 0x03)) {
		return -1;
	}

	/* search Vendor Specific Data Block (tag 3) */
	d = edid[0x82] & 0x7f;
	if (d > 4) {
		int i = 0x84;
		int end = 0x80 + d;

		do {
			u8 tag = edid[i] >> 5;
			u8 len = edid[i] & 0x1f;

			if ((tag == 3) && (len >= 5))
				return i + 4;
			i += len + 1;
		} while (i < end);
	}
	return -1;
}

static int adv76xx_set_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;
	int spa_loc;
	int err;
	int i;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad > ADV7604_PAD_HDMI_PORT_D)
		return -EINVAL;
	if (edid->start_block != 0)
		return -EINVAL;
	if (edid->blocks == 0) {
		/* Disable hotplug and I2C access to EDID RAM from DDC port */
		state->edid.present &= ~(1 << edid->pad);
		adv76xx_set_hpd(state, state->edid.present);
		rep_write_clr_set(sd, info->edid_enable_reg, 0x0f, state->edid.present);

		/* Fall back to a 16:9 aspect ratio */
		state->aspect_ratio.numerator = 16;
		state->aspect_ratio.denominator = 9;

		if (!state->edid.present)
			state->edid.blocks = 0;

		v4l2_dbg(2, debug, sd, "%s: clear EDID pad %d, edid.present = 0x%x\n",
				__func__, edid->pad, state->edid.present);
		return 0;
	}
	if (edid->blocks > 2) {
		edid->blocks = 2;
		return -E2BIG;
	}

	v4l2_dbg(2, debug, sd, "%s: write EDID pad %d, edid.present = 0x%x\n",
			__func__, edid->pad, state->edid.present);

	/* Disable hotplug and I2C access to EDID RAM from DDC port */
	cancel_delayed_work_sync(&state->delayed_work_enable_hotplug);
	adv76xx_set_hpd(state, 0);
	rep_write_clr_set(sd, info->edid_enable_reg, 0x0f, 0x00);

	spa_loc = get_edid_spa_location(edid->edid);
	if (spa_loc < 0)
		spa_loc = 0xc0; /* Default value [REF_02, p. 116] */

	switch (edid->pad) {
	case ADV76XX_PAD_HDMI_PORT_A:
		state->spa_port_a[0] = edid->edid[spa_loc];
		state->spa_port_a[1] = edid->edid[spa_loc + 1];
		break;
	case ADV7604_PAD_HDMI_PORT_B:
		rep_write(sd, 0x70, edid->edid[spa_loc]);
		rep_write(sd, 0x71, edid->edid[spa_loc + 1]);
		break;
	case ADV7604_PAD_HDMI_PORT_C:
		rep_write(sd, 0x72, edid->edid[spa_loc]);
		rep_write(sd, 0x73, edid->edid[spa_loc + 1]);
		break;
	case ADV7604_PAD_HDMI_PORT_D:
		rep_write(sd, 0x74, edid->edid[spa_loc]);
		rep_write(sd, 0x75, edid->edid[spa_loc + 1]);
		break;
	default:
		return -EINVAL;
	}

	if (info->type == ADV7604) {
		rep_write(sd, 0x76, spa_loc & 0xff);
		rep_write_clr_set(sd, 0x77, 0x40, (spa_loc & 0x100) >> 2);
	} else {
		/* FIXME: Where is the SPA location LSB register ? */
		rep_write_clr_set(sd, 0x71, 0x01, (spa_loc & 0x100) >> 8);
	}

	edid->edid[spa_loc] = state->spa_port_a[0];
	edid->edid[spa_loc + 1] = state->spa_port_a[1];

	memcpy(state->edid.edid, edid->edid, 128 * edid->blocks);
	state->edid.blocks = edid->blocks;
	state->aspect_ratio = v4l2_calc_aspect_ratio(edid->edid[0x15],
			edid->edid[0x16]);
	state->edid.present |= 1 << edid->pad;

	err = edid_write_block(sd, 128 * edid->blocks, state->edid.edid);
	if (err < 0) {
		v4l2_err(sd, "error %d writing edid pad %d\n", err, edid->pad);
		return err;
	}

	/* adv76xx calculates the checksums and enables I2C access to internal
	   EDID RAM from DDC port. */
	rep_write_clr_set(sd, info->edid_enable_reg, 0x0f, state->edid.present);

	for (i = 0; i < 1000; i++) {
		if (rep_read(sd, info->edid_status_reg) & state->edid.present)
			break;
		mdelay(1);
	}
	if (i == 1000) {
		v4l2_err(sd, "error enabling edid (0x%x)\n", state->edid.present);
		return -EIO;
	}

	/* enable hotplug after 100 ms */
	queue_delayed_work(state->work_queues,
			&state->delayed_work_enable_hotplug, HZ / 10);
	return 0;
}

/*********** avi info frame CEA-861-E **************/

static void print_avi_infoframe(struct v4l2_subdev *sd)
{
	int i;
	u8 buf[14];
	u8 avi_len;
	u8 avi_ver;

	if (!is_hdmi(sd)) {
		v4l2_info(sd, "receive DVI-D signal (AVI infoframe not supported)\n");
		return;
	}
	if (!(io_read(sd, 0x60) & 0x01)) {
		v4l2_info(sd, "AVI infoframe not received\n");
		return;
	}

	if (io_read(sd, 0x83) & 0x01) {
		v4l2_info(sd, "AVI infoframe checksum error has occurred earlier\n");
		io_write(sd, 0x85, 0x01); /* clear AVI_INF_CKS_ERR_RAW */
		if (io_read(sd, 0x83) & 0x01) {
			v4l2_info(sd, "AVI infoframe checksum error still present\n");
			io_write(sd, 0x85, 0x01); /* clear AVI_INF_CKS_ERR_RAW */
		}
	}

	avi_len = infoframe_read(sd, 0xe2);
	avi_ver = infoframe_read(sd, 0xe1);
	v4l2_info(sd, "AVI infoframe version %d (%d byte)\n",
			avi_ver, avi_len);

	if (avi_ver != 0x02)
		return;

	for (i = 0; i < 14; i++)
		buf[i] = infoframe_read(sd, i);

	v4l2_info(sd,
		"\t%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
		buf[8], buf[9], buf[10], buf[11], buf[12], buf[13]);
}

static int adv76xx_log_status(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;
	struct v4l2_dv_timings timings;
	struct stdi_readback stdi;
	u8 reg_io_0x02 = io_read(sd, 0x02);
	u8 edid_enabled;
	u8 cable_det;

	static const char * const csc_coeff_sel_rb[16] = {
		"bypassed", "YPbPr601 -> RGB", "reserved", "YPbPr709 -> RGB",
		"reserved", "RGB -> YPbPr601", "reserved", "RGB -> YPbPr709",
		"reserved", "YPbPr709 -> YPbPr601", "YPbPr601 -> YPbPr709",
		"reserved", "reserved", "reserved", "reserved", "manual"
	};
	static const char * const input_color_space_txt[16] = {
		"RGB limited range (16-235)", "RGB full range (0-255)",
		"YCbCr Bt.601 (16-235)", "YCbCr Bt.709 (16-235)",
		"xvYCC Bt.601", "xvYCC Bt.709",
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
	edid_enabled = rep_read(sd, info->edid_status_reg);
	v4l2_info(sd, "EDID enabled port A: %s, B: %s, C: %s, D: %s\n",
			((edid_enabled & 0x01) ? "Yes" : "No"),
			((edid_enabled & 0x02) ? "Yes" : "No"),
			((edid_enabled & 0x04) ? "Yes" : "No"),
			((edid_enabled & 0x08) ? "Yes" : "No"));
	v4l2_info(sd, "CEC: %s\n", !!(cec_read(sd, 0x2a) & 0x01) ?
			"enabled" : "disabled");

	v4l2_info(sd, "-----Signal status-----\n");
	cable_det = info->read_cable_det(sd);
	v4l2_info(sd, "Cable detected (+5V power) port A: %s, B: %s, C: %s, D: %s\n",
			((cable_det & 0x01) ? "Yes" : "No"),
			((cable_det & 0x02) ? "Yes" : "No"),
			((cable_det & 0x04) ? "Yes" : "No"),
			((cable_det & 0x08) ? "Yes" : "No"));
	v4l2_info(sd, "TMDS signal detected: %s\n",
			no_signal_tmds(sd) ? "false" : "true");
	v4l2_info(sd, "TMDS signal locked: %s\n",
			no_lock_tmds(sd) ? "false" : "true");
	v4l2_info(sd, "SSPD locked: %s\n", no_lock_sspd(sd) ? "false" : "true");
	v4l2_info(sd, "STDI locked: %s\n", no_lock_stdi(sd) ? "false" : "true");
	v4l2_info(sd, "CP locked: %s\n", no_lock_cp(sd) ? "false" : "true");
	v4l2_info(sd, "CP free run: %s\n",
			(in_free_run(sd)) ? "on" : "off");
	v4l2_info(sd, "Prim-mode = 0x%x, video std = 0x%x, v_freq = 0x%x\n",
			io_read(sd, 0x01) & 0x0f, io_read(sd, 0x00) & 0x3f,
			(io_read(sd, 0x01) & 0x70) >> 4);

	v4l2_info(sd, "-----Video Timings-----\n");
	if (read_stdi(sd, &stdi))
		v4l2_info(sd, "STDI: not locked\n");
	else
		v4l2_info(sd, "STDI: lcf (frame height - 1) = %d, bl = %d, lcvs (vsync) = %d, %s, %chsync, %cvsync\n",
				stdi.lcf, stdi.bl, stdi.lcvs,
				stdi.interlaced ? "interlaced" : "progressive",
				stdi.hs_pol, stdi.vs_pol);
	if (adv76xx_query_dv_timings(sd, &timings))
		v4l2_info(sd, "No video detected\n");
	else
		v4l2_print_dv_timings(sd->name, "Detected format: ",
				      &timings, true);
	v4l2_print_dv_timings(sd->name, "Configured format: ",
			      &state->timings, true);

	if (no_signal(sd))
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
			csc_coeff_sel_rb[cp_read(sd, info->cp_csc) >> 4]);

	if (!is_digital_input(sd))
		return 0;

	v4l2_info(sd, "-----%s status-----\n", is_hdmi(sd) ? "HDMI" : "DVI-D");
	v4l2_info(sd, "Digital video port selected: %c\n",
			(hdmi_read(sd, 0x00) & 0x03) + 'A');
	v4l2_info(sd, "HDCP encrypted content: %s\n",
			(hdmi_read(sd, 0x05) & 0x40) ? "true" : "false");
	v4l2_info(sd, "HDCP keys read: %s%s\n",
			(hdmi_read(sd, 0x04) & 0x20) ? "yes" : "no",
			(hdmi_read(sd, 0x04) & 0x10) ? "ERROR" : "");
	if (is_hdmi(sd)) {
		bool audio_pll_locked = hdmi_read(sd, 0x04) & 0x01;
		bool audio_sample_packet_detect = hdmi_read(sd, 0x18) & 0x01;
		bool audio_mute = io_read(sd, 0x65) & 0x40;

		v4l2_info(sd, "Audio: pll %s, samples %s, %s\n",
				audio_pll_locked ? "locked" : "not locked",
				audio_sample_packet_detect ? "detected" : "not detected",
				audio_mute ? "muted" : "enabled");
		if (audio_pll_locked && audio_sample_packet_detect) {
			v4l2_info(sd, "Audio format: %s\n",
					(hdmi_read(sd, 0x07) & 0x20) ? "multi-channel" : "stereo");
		}
		v4l2_info(sd, "Audio CTS: %u\n", (hdmi_read(sd, 0x5b) << 12) +
				(hdmi_read(sd, 0x5c) << 8) +
				(hdmi_read(sd, 0x5d) & 0xf0));
		v4l2_info(sd, "Audio N: %u\n", ((hdmi_read(sd, 0x5d) & 0x0f) << 16) +
				(hdmi_read(sd, 0x5e) << 8) +
				hdmi_read(sd, 0x5f));
		v4l2_info(sd, "AV Mute: %s\n", (hdmi_read(sd, 0x04) & 0x40) ? "on" : "off");

		v4l2_info(sd, "Deep color mode: %s\n", deep_color_mode_txt[(hdmi_read(sd, 0x0b) & 0x60) >> 5]);

		print_avi_infoframe(sd);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops adv76xx_ctrl_ops = {
	.s_ctrl = adv76xx_s_ctrl,
};

static const struct v4l2_subdev_core_ops adv76xx_core_ops = {
	.log_status = adv76xx_log_status,
	.interrupt_service_routine = adv76xx_isr,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = adv76xx_g_register,
	.s_register = adv76xx_s_register,
#endif
};

static const struct v4l2_subdev_video_ops adv76xx_video_ops = {
	.s_routing = adv76xx_s_routing,
	.g_input_status = adv76xx_g_input_status,
	.s_dv_timings = adv76xx_s_dv_timings,
	.g_dv_timings = adv76xx_g_dv_timings,
	.query_dv_timings = adv76xx_query_dv_timings,
};

static const struct v4l2_subdev_pad_ops adv76xx_pad_ops = {
	.enum_mbus_code = adv76xx_enum_mbus_code,
	.get_fmt = adv76xx_get_format,
	.set_fmt = adv76xx_set_format,
	.get_edid = adv76xx_get_edid,
	.set_edid = adv76xx_set_edid,
	.dv_timings_cap = adv76xx_dv_timings_cap,
	.enum_dv_timings = adv76xx_enum_dv_timings,
};

static const struct v4l2_subdev_ops adv76xx_ops = {
	.core = &adv76xx_core_ops,
	.video = &adv76xx_video_ops,
	.pad = &adv76xx_pad_ops,
};

/* -------------------------- custom ctrls ---------------------------------- */

static const struct v4l2_ctrl_config adv7604_ctrl_analog_sampling_phase = {
	.ops = &adv76xx_ctrl_ops,
	.id = V4L2_CID_ADV_RX_ANALOG_SAMPLING_PHASE,
	.name = "Analog Sampling Phase",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 0x1f,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config adv76xx_ctrl_free_run_color_manual = {
	.ops = &adv76xx_ctrl_ops,
	.id = V4L2_CID_ADV_RX_FREE_RUN_COLOR_MANUAL,
	.name = "Free Running Color, Manual",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = false,
	.max = true,
	.step = 1,
	.def = false,
};

static const struct v4l2_ctrl_config adv76xx_ctrl_free_run_color = {
	.ops = &adv76xx_ctrl_ops,
	.id = V4L2_CID_ADV_RX_FREE_RUN_COLOR,
	.name = "Free Running Color",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0x0,
	.max = 0xffffff,
	.step = 0x1,
	.def = 0x0,
};

/* ----------------------------------------------------------------------- */

static int adv76xx_core_init(struct v4l2_subdev *sd)
{
	struct adv76xx_state *state = to_state(sd);
	const struct adv76xx_chip_info *info = state->info;
	struct adv76xx_platform_data *pdata = &state->pdata;

	hdmi_write(sd, 0x48,
		(pdata->disable_pwrdnb ? 0x80 : 0) |
		(pdata->disable_cable_det_rst ? 0x40 : 0));

	disable_input(sd);

	if (pdata->default_input >= 0 &&
	    pdata->default_input < state->source_pad) {
		state->selected_input = pdata->default_input;
		select_input(sd);
		enable_input(sd);
	}

	/* power */
	io_write(sd, 0x0c, 0x42);   /* Power up part and power down VDP */
	io_write(sd, 0x0b, 0x44);   /* Power down ESDP block */
	cp_write(sd, 0xcf, 0x01);   /* Power down macrovision */

	/* video format */
	io_write_clr_set(sd, 0x02, 0x0f,
			pdata->alt_gamma << 3 |
			pdata->op_656_range << 2 |
			pdata->alt_data_sat << 0);
	io_write_clr_set(sd, 0x05, 0x0e, pdata->blank_data << 3 |
			pdata->insert_av_codes << 2 |
			pdata->replicate_av_codes << 1);
	adv76xx_setup_format(state);

	cp_write(sd, 0x69, 0x30);   /* Enable CP CSC */

	/* VS, HS polarities */
	io_write(sd, 0x06, 0xa0 | pdata->inv_vs_pol << 2 |
		 pdata->inv_hs_pol << 1 | pdata->inv_llc_pol);

	/* Adjust drive strength */
	io_write(sd, 0x14, 0x40 | pdata->dr_str_data << 4 |
				pdata->dr_str_clk << 2 |
				pdata->dr_str_sync);

	cp_write(sd, 0xba, (pdata->hdmi_free_run_mode << 1) | 0x01); /* HDMI free run */
	cp_write(sd, 0xf3, 0xdc); /* Low threshold to enter/exit free run mode */
	cp_write(sd, 0xf9, 0x23); /*  STDI ch. 1 - LCVS change threshold -
				      ADI recommended setting [REF_01, c. 2.3.3] */
	cp_write(sd, 0x45, 0x23); /*  STDI ch. 2 - LCVS change threshold -
				      ADI recommended setting [REF_01, c. 2.3.3] */
	cp_write(sd, 0xc9, 0x2d); /* use prim_mode and vid_std as free run resolution
				     for digital formats */

	/* HDMI audio */
	hdmi_write_clr_set(sd, 0x15, 0x03, 0x03); /* Mute on FIFO over-/underflow [REF_01, c. 1.2.18] */
	hdmi_write_clr_set(sd, 0x1a, 0x0e, 0x08); /* Wait 1 s before unmute */
	hdmi_write_clr_set(sd, 0x68, 0x06, 0x06); /* FIFO reset on over-/underflow [REF_01, c. 1.2.19] */

	/* TODO from platform data */
	afe_write(sd, 0xb5, 0x01);  /* Setting MCLK to 256Fs */

	if (adv76xx_has_afe(state)) {
		afe_write(sd, 0x02, pdata->ain_sel); /* Select analog input muxing mode */
		io_write_clr_set(sd, 0x30, 1 << 4, pdata->output_bus_lsb_to_msb << 4);
	}

	/* interrupts */
	io_write(sd, 0x40, 0xc0 | pdata->int1_config); /* Configure INT1 */
	io_write(sd, 0x46, 0x98); /* Enable SSPD, STDI and CP unlocked interrupts */
	io_write(sd, 0x6e, info->fmt_change_digital_mask); /* Enable V_LOCKED and DE_REGEN_LCK interrupts */
	io_write(sd, 0x73, info->cable_det_mask); /* Enable cable detection (+5v) interrupts */
	info->setup_irqs(sd);

	return v4l2_ctrl_handler_setup(sd->ctrl_handler);
}

static void adv7604_setup_irqs(struct v4l2_subdev *sd)
{
	io_write(sd, 0x41, 0xd7); /* STDI irq for any change, disable INT2 */
}

static void adv7611_setup_irqs(struct v4l2_subdev *sd)
{
	io_write(sd, 0x41, 0xd0); /* STDI irq for any change, disable INT2 */
}

static void adv76xx_unregister_clients(struct adv76xx_state *state)
{
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(state->i2c_clients); ++i) {
		if (state->i2c_clients[i])
			i2c_unregister_device(state->i2c_clients[i]);
	}
}

static struct i2c_client *adv76xx_dummy_client(struct v4l2_subdev *sd,
							u8 addr, u8 io_reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (addr)
		io_write(sd, io_reg, addr << 1);
	return i2c_new_dummy(client->adapter, io_read(sd, io_reg) >> 1);
}

static const struct adv76xx_reg_seq adv7604_recommended_settings_afe[] = {
	/* reset ADI recommended settings for HDMI: */
	/* "ADV7604 Register Settings Recommendations (rev. 2.5, June 2010)" p. 4. */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x0d), 0x04 }, /* HDMI filter optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x0d), 0x04 }, /* HDMI filter optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x3d), 0x00 }, /* DDC bus active pull-up control */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x3e), 0x74 }, /* TMDS PLL optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x4e), 0x3b }, /* TMDS PLL optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x57), 0x74 }, /* TMDS PLL optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x58), 0x63 }, /* TMDS PLL optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x8d), 0x18 }, /* equaliser */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x8e), 0x34 }, /* equaliser */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x93), 0x88 }, /* equaliser */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x94), 0x2e }, /* equaliser */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x96), 0x00 }, /* enable automatic EQ changing */

	/* set ADI recommended settings for digitizer */
	/* "ADV7604 Register Settings Recommendations (rev. 2.5, June 2010)" p. 17. */
	{ ADV76XX_REG(ADV76XX_PAGE_AFE, 0x12), 0x7b }, /* ADC noise shaping filter controls */
	{ ADV76XX_REG(ADV76XX_PAGE_AFE, 0x0c), 0x1f }, /* CP core gain controls */
	{ ADV76XX_REG(ADV76XX_PAGE_CP, 0x3e), 0x04 }, /* CP core pre-gain control */
	{ ADV76XX_REG(ADV76XX_PAGE_CP, 0xc3), 0x39 }, /* CP coast control. Graphics mode */
	{ ADV76XX_REG(ADV76XX_PAGE_CP, 0x40), 0x5c }, /* CP core pre-gain control. Graphics mode */

	{ ADV76XX_REG_SEQ_TERM, 0 },
};

static const struct adv76xx_reg_seq adv7604_recommended_settings_hdmi[] = {
	/* set ADI recommended settings for HDMI: */
	/* "ADV7604 Register Settings Recommendations (rev. 2.5, June 2010)" p. 4. */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x0d), 0x84 }, /* HDMI filter optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x3d), 0x10 }, /* DDC bus active pull-up control */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x3e), 0x39 }, /* TMDS PLL optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x4e), 0x3b }, /* TMDS PLL optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x57), 0xb6 }, /* TMDS PLL optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x58), 0x03 }, /* TMDS PLL optimization */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x8d), 0x18 }, /* equaliser */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x8e), 0x34 }, /* equaliser */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x93), 0x8b }, /* equaliser */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x94), 0x2d }, /* equaliser */
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x96), 0x01 }, /* enable automatic EQ changing */

	/* reset ADI recommended settings for digitizer */
	/* "ADV7604 Register Settings Recommendations (rev. 2.5, June 2010)" p. 17. */
	{ ADV76XX_REG(ADV76XX_PAGE_AFE, 0x12), 0xfb }, /* ADC noise shaping filter controls */
	{ ADV76XX_REG(ADV76XX_PAGE_AFE, 0x0c), 0x0d }, /* CP core gain controls */

	{ ADV76XX_REG_SEQ_TERM, 0 },
};

static const struct adv76xx_reg_seq adv7611_recommended_settings_hdmi[] = {
	/* ADV7611 Register Settings Recommendations Rev 1.5, May 2014 */
	{ ADV76XX_REG(ADV76XX_PAGE_CP, 0x6c), 0x00 },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x9b), 0x03 },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x6f), 0x08 },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x85), 0x1f },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x87), 0x70 },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x57), 0xda },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x58), 0x01 },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x03), 0x98 },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x4c), 0x44 },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x8d), 0x04 },
	{ ADV76XX_REG(ADV76XX_PAGE_HDMI, 0x8e), 0x1e },

	{ ADV76XX_REG_SEQ_TERM, 0 },
};

static const struct adv76xx_chip_info adv76xx_chip_info[] = {
	[ADV7604] = {
		.type = ADV7604,
		.has_afe = true,
		.max_port = ADV7604_PAD_VGA_COMP,
		.num_dv_ports = 4,
		.edid_enable_reg = 0x77,
		.edid_status_reg = 0x7d,
		.lcf_reg = 0xb3,
		.tdms_lock_mask = 0xe0,
		.cable_det_mask = 0x1e,
		.fmt_change_digital_mask = 0xc1,
		.cp_csc = 0xfc,
		.formats = adv7604_formats,
		.nformats = ARRAY_SIZE(adv7604_formats),
		.set_termination = adv7604_set_termination,
		.setup_irqs = adv7604_setup_irqs,
		.read_hdmi_pixelclock = adv7604_read_hdmi_pixelclock,
		.read_cable_det = adv7604_read_cable_det,
		.recommended_settings = {
		    [0] = adv7604_recommended_settings_afe,
		    [1] = adv7604_recommended_settings_hdmi,
		},
		.num_recommended_settings = {
		    [0] = ARRAY_SIZE(adv7604_recommended_settings_afe),
		    [1] = ARRAY_SIZE(adv7604_recommended_settings_hdmi),
		},
		.page_mask = BIT(ADV76XX_PAGE_IO) | BIT(ADV7604_PAGE_AVLINK) |
			BIT(ADV76XX_PAGE_CEC) | BIT(ADV76XX_PAGE_INFOFRAME) |
			BIT(ADV7604_PAGE_ESDP) | BIT(ADV7604_PAGE_DPP) |
			BIT(ADV76XX_PAGE_AFE) | BIT(ADV76XX_PAGE_REP) |
			BIT(ADV76XX_PAGE_EDID) | BIT(ADV76XX_PAGE_HDMI) |
			BIT(ADV76XX_PAGE_TEST) | BIT(ADV76XX_PAGE_CP) |
			BIT(ADV7604_PAGE_VDP),
		.linewidth_mask = 0xfff,
		.field0_height_mask = 0xfff,
		.field1_height_mask = 0xfff,
		.hfrontporch_mask = 0x3ff,
		.hsync_mask = 0x3ff,
		.hbackporch_mask = 0x3ff,
		.field0_vfrontporch_mask = 0x1fff,
		.field0_vsync_mask = 0x1fff,
		.field0_vbackporch_mask = 0x1fff,
		.field1_vfrontporch_mask = 0x1fff,
		.field1_vsync_mask = 0x1fff,
		.field1_vbackporch_mask = 0x1fff,
	},
	[ADV7611] = {
		.type = ADV7611,
		.has_afe = false,
		.max_port = ADV76XX_PAD_HDMI_PORT_A,
		.num_dv_ports = 1,
		.edid_enable_reg = 0x74,
		.edid_status_reg = 0x76,
		.lcf_reg = 0xa3,
		.tdms_lock_mask = 0x43,
		.cable_det_mask = 0x01,
		.fmt_change_digital_mask = 0x03,
		.cp_csc = 0xf4,
		.formats = adv7611_formats,
		.nformats = ARRAY_SIZE(adv7611_formats),
		.set_termination = adv7611_set_termination,
		.setup_irqs = adv7611_setup_irqs,
		.read_hdmi_pixelclock = adv7611_read_hdmi_pixelclock,
		.read_cable_det = adv7611_read_cable_det,
		.recommended_settings = {
		    [1] = adv7611_recommended_settings_hdmi,
		},
		.num_recommended_settings = {
		    [1] = ARRAY_SIZE(adv7611_recommended_settings_hdmi),
		},
		.page_mask = BIT(ADV76XX_PAGE_IO) | BIT(ADV76XX_PAGE_CEC) |
			BIT(ADV76XX_PAGE_INFOFRAME) | BIT(ADV76XX_PAGE_AFE) |
			BIT(ADV76XX_PAGE_REP) |  BIT(ADV76XX_PAGE_EDID) |
			BIT(ADV76XX_PAGE_HDMI) | BIT(ADV76XX_PAGE_CP),
		.linewidth_mask = 0x1fff,
		.field0_height_mask = 0x1fff,
		.field1_height_mask = 0x1fff,
		.hfrontporch_mask = 0x1fff,
		.hsync_mask = 0x1fff,
		.hbackporch_mask = 0x1fff,
		.field0_vfrontporch_mask = 0x3fff,
		.field0_vsync_mask = 0x3fff,
		.field0_vbackporch_mask = 0x3fff,
		.field1_vfrontporch_mask = 0x3fff,
		.field1_vsync_mask = 0x3fff,
		.field1_vbackporch_mask = 0x3fff,
	},
};

static struct i2c_device_id adv76xx_i2c_id[] = {
	{ "adv7604", (kernel_ulong_t)&adv76xx_chip_info[ADV7604] },
	{ "adv7611", (kernel_ulong_t)&adv76xx_chip_info[ADV7611] },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv76xx_i2c_id);

static struct of_device_id adv76xx_of_id[] __maybe_unused = {
	{ .compatible = "adi,adv7611", .data = &adv76xx_chip_info[ADV7611] },
	{ }
};
MODULE_DEVICE_TABLE(of, adv76xx_of_id);

static int adv76xx_parse_dt(struct adv76xx_state *state)
{
	struct v4l2_of_endpoint bus_cfg;
	struct device_node *endpoint;
	struct device_node *np;
	unsigned int flags;

	np = state->i2c_clients[ADV76XX_PAGE_IO]->dev.of_node;

	/* Parse the endpoint. */
	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint)
		return -EINVAL;

	v4l2_of_parse_endpoint(endpoint, &bus_cfg);
	of_node_put(endpoint);

	flags = bus_cfg.bus.parallel.flags;

	if (flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		state->pdata.inv_hs_pol = 1;

	if (flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		state->pdata.inv_vs_pol = 1;

	if (flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		state->pdata.inv_llc_pol = 1;

	if (bus_cfg.bus_type == V4L2_MBUS_BT656) {
		state->pdata.insert_av_codes = 1;
		state->pdata.op_656_range = 1;
	}

	/* Disable the interrupt for now as no DT-based board uses it. */
	state->pdata.int1_config = ADV76XX_INT1_CONFIG_DISABLED;

	/* Use the default I2C addresses. */
	state->pdata.i2c_addresses[ADV7604_PAGE_AVLINK] = 0x42;
	state->pdata.i2c_addresses[ADV76XX_PAGE_CEC] = 0x40;
	state->pdata.i2c_addresses[ADV76XX_PAGE_INFOFRAME] = 0x3e;
	state->pdata.i2c_addresses[ADV7604_PAGE_ESDP] = 0x38;
	state->pdata.i2c_addresses[ADV7604_PAGE_DPP] = 0x3c;
	state->pdata.i2c_addresses[ADV76XX_PAGE_AFE] = 0x26;
	state->pdata.i2c_addresses[ADV76XX_PAGE_REP] = 0x32;
	state->pdata.i2c_addresses[ADV76XX_PAGE_EDID] = 0x36;
	state->pdata.i2c_addresses[ADV76XX_PAGE_HDMI] = 0x34;
	state->pdata.i2c_addresses[ADV76XX_PAGE_TEST] = 0x30;
	state->pdata.i2c_addresses[ADV76XX_PAGE_CP] = 0x22;
	state->pdata.i2c_addresses[ADV7604_PAGE_VDP] = 0x24;

	/* Hardcode the remaining platform data fields. */
	state->pdata.disable_pwrdnb = 0;
	state->pdata.disable_cable_det_rst = 0;
	state->pdata.default_input = -1;
	state->pdata.blank_data = 1;
	state->pdata.alt_data_sat = 1;
	state->pdata.op_format_mode_sel = ADV7604_OP_FORMAT_MODE0;
	state->pdata.bus_order = ADV7604_BUS_ORDER_RGB;

	return 0;
}

static int adv76xx_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	static const struct v4l2_dv_timings cea640x480 =
		V4L2_DV_BT_CEA_640X480P59_94;
	struct adv76xx_state *state;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_subdev *sd;
	unsigned int i;
	u16 val;
	int err;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;
	v4l_dbg(1, debug, client, "detecting adv76xx client on address 0x%x\n",
			client->addr << 1);

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (!state) {
		v4l_err(client, "Could not allocate adv76xx_state memory!\n");
		return -ENOMEM;
	}

	state->i2c_clients[ADV76XX_PAGE_IO] = client;

	/* initialize variables */
	state->restart_stdi_once = true;
	state->selected_input = ~0;

	if (IS_ENABLED(CONFIG_OF) && client->dev.of_node) {
		const struct of_device_id *oid;

		oid = of_match_node(adv76xx_of_id, client->dev.of_node);
		state->info = oid->data;

		err = adv76xx_parse_dt(state);
		if (err < 0) {
			v4l_err(client, "DT parsing error\n");
			return err;
		}
	} else if (client->dev.platform_data) {
		struct adv76xx_platform_data *pdata = client->dev.platform_data;

		state->info = (const struct adv76xx_chip_info *)id->driver_data;
		state->pdata = *pdata;
	} else {
		v4l_err(client, "No platform data!\n");
		return -ENODEV;
	}

	/* Request GPIOs. */
	for (i = 0; i < state->info->num_dv_ports; ++i) {
		state->hpd_gpio[i] =
			devm_gpiod_get_index_optional(&client->dev, "hpd", i,
						      GPIOD_OUT_LOW);
		if (IS_ERR(state->hpd_gpio[i]))
			return PTR_ERR(state->hpd_gpio[i]);

		if (state->hpd_gpio[i])
			v4l_info(client, "Handling HPD %u GPIO\n", i);
	}

	state->timings = cea640x480;
	state->format = adv76xx_format_info(state, MEDIA_BUS_FMT_YUYV8_2X8);

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &adv76xx_ops);
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x",
		id->name, i2c_adapter_id(client->adapter),
		client->addr);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/*
	 * Verify that the chip is present. On ADV7604 the RD_INFO register only
	 * identifies the revision, while on ADV7611 it identifies the model as
	 * well. Use the HDMI slave address on ADV7604 and RD_INFO on ADV7611.
	 */
	if (state->info->type == ADV7604) {
		val = adv_smbus_read_byte_data_check(client, 0xfb, false);
		if (val != 0x68) {
			v4l2_info(sd, "not an adv7604 on address 0x%x\n",
					client->addr << 1);
			return -ENODEV;
		}
	} else {
		val = (adv_smbus_read_byte_data_check(client, 0xea, false) << 8)
		    | (adv_smbus_read_byte_data_check(client, 0xeb, false) << 0);
		if (val != 0x2051) {
			v4l2_info(sd, "not an adv7611 on address 0x%x\n",
					client->addr << 1);
			return -ENODEV;
		}
	}

	/* control handlers */
	hdl = &state->hdl;
	v4l2_ctrl_handler_init(hdl, adv76xx_has_afe(state) ? 9 : 8);

	v4l2_ctrl_new_std(hdl, &adv76xx_ctrl_ops,
			V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &adv76xx_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &adv76xx_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &adv76xx_ctrl_ops,
			V4L2_CID_HUE, 0, 128, 1, 0);

	/* private controls */
	state->detect_tx_5v_ctrl = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_DV_RX_POWER_PRESENT, 0,
			(1 << state->info->num_dv_ports) - 1, 0, 0);
	state->rgb_quantization_range_ctrl =
		v4l2_ctrl_new_std_menu(hdl, &adv76xx_ctrl_ops,
			V4L2_CID_DV_RX_RGB_RANGE, V4L2_DV_RGB_RANGE_FULL,
			0, V4L2_DV_RGB_RANGE_AUTO);

	/* custom controls */
	if (adv76xx_has_afe(state))
		state->analog_sampling_phase_ctrl =
			v4l2_ctrl_new_custom(hdl, &adv7604_ctrl_analog_sampling_phase, NULL);
	state->free_run_color_manual_ctrl =
		v4l2_ctrl_new_custom(hdl, &adv76xx_ctrl_free_run_color_manual, NULL);
	state->free_run_color_ctrl =
		v4l2_ctrl_new_custom(hdl, &adv76xx_ctrl_free_run_color, NULL);

	sd->ctrl_handler = hdl;
	if (hdl->error) {
		err = hdl->error;
		goto err_hdl;
	}
	state->detect_tx_5v_ctrl->is_private = true;
	state->rgb_quantization_range_ctrl->is_private = true;
	if (adv76xx_has_afe(state))
		state->analog_sampling_phase_ctrl->is_private = true;
	state->free_run_color_manual_ctrl->is_private = true;
	state->free_run_color_ctrl->is_private = true;

	if (adv76xx_s_detect_tx_5v_ctrl(sd)) {
		err = -ENODEV;
		goto err_hdl;
	}

	for (i = 1; i < ADV76XX_PAGE_MAX; ++i) {
		if (!(BIT(i) & state->info->page_mask))
			continue;

		state->i2c_clients[i] =
			adv76xx_dummy_client(sd, state->pdata.i2c_addresses[i],
					     0xf2 + i);
		if (state->i2c_clients[i] == NULL) {
			err = -ENOMEM;
			v4l2_err(sd, "failed to create i2c client %u\n", i);
			goto err_i2c;
		}
	}

	/* work queues */
	state->work_queues = create_singlethread_workqueue(client->name);
	if (!state->work_queues) {
		v4l2_err(sd, "Could not create work queue\n");
		err = -ENOMEM;
		goto err_i2c;
	}

	INIT_DELAYED_WORK(&state->delayed_work_enable_hotplug,
			adv76xx_delayed_work_enable_hotplug);

	state->source_pad = state->info->num_dv_ports
			  + (state->info->has_afe ? 2 : 0);
	for (i = 0; i < state->source_pad; ++i)
		state->pads[i].flags = MEDIA_PAD_FL_SINK;
	state->pads[state->source_pad].flags = MEDIA_PAD_FL_SOURCE;

	err = media_entity_init(&sd->entity, state->source_pad + 1,
				state->pads, 0);
	if (err)
		goto err_work_queues;

	err = adv76xx_core_init(sd);
	if (err)
		goto err_entity;
	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
			client->addr << 1, client->adapter->name);

	err = v4l2_async_register_subdev(sd);
	if (err)
		goto err_entity;

	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_work_queues:
	cancel_delayed_work(&state->delayed_work_enable_hotplug);
	destroy_workqueue(state->work_queues);
err_i2c:
	adv76xx_unregister_clients(state);
err_hdl:
	v4l2_ctrl_handler_free(hdl);
	return err;
}

/* ----------------------------------------------------------------------- */

static int adv76xx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv76xx_state *state = to_state(sd);

	cancel_delayed_work(&state->delayed_work_enable_hotplug);
	destroy_workqueue(state->work_queues);
	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	adv76xx_unregister_clients(to_state(sd));
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver adv76xx_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "adv7604",
		.of_match_table = of_match_ptr(adv76xx_of_id),
	},
	.probe = adv76xx_probe,
	.remove = adv76xx_remove,
	.id_table = adv76xx_i2c_id,
};

module_i2c_driver(adv76xx_driver);
