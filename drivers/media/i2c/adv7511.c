/*
 * Analog Devices ADV7511 HDMI Transmitter Device Driver
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
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/hdmi.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dv-timings.h>
#include <media/i2c/adv7511.h>
#include <media/cec.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

MODULE_DESCRIPTION("Analog Devices ADV7511 HDMI Transmitter Device Driver");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL v2");

#define MASK_ADV7511_EDID_RDY_INT   0x04
#define MASK_ADV7511_MSEN_INT       0x40
#define MASK_ADV7511_HPD_INT        0x80

#define MASK_ADV7511_HPD_DETECT     0x40
#define MASK_ADV7511_MSEN_DETECT    0x20
#define MASK_ADV7511_EDID_RDY       0x10

#define EDID_MAX_RETRIES (8)
#define EDID_DELAY 250
#define EDID_MAX_SEGM 8

#define ADV7511_MAX_WIDTH 1920
#define ADV7511_MAX_HEIGHT 1200
#define ADV7511_MIN_PIXELCLOCK 20000000
#define ADV7511_MAX_PIXELCLOCK 225000000

#define ADV7511_MAX_ADDRS (3)

/*
**********************************************************************
*
*  Arrays with configuration parameters for the ADV7511
*
**********************************************************************
*/

struct i2c_reg_value {
	unsigned char reg;
	unsigned char value;
};

struct adv7511_state_edid {
	/* total number of blocks */
	u32 blocks;
	/* Number of segments read */
	u32 segments;
	u8 data[EDID_MAX_SEGM * 256];
	/* Number of EDID read retries left */
	unsigned read_retries;
	bool complete;
};

struct adv7511_state {
	struct adv7511_platform_data pdata;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler hdl;
	int chip_revision;
	u8 i2c_edid_addr;
	u8 i2c_pktmem_addr;
	u8 i2c_cec_addr;

	struct i2c_client *i2c_cec;
	struct cec_adapter *cec_adap;
	u8   cec_addr[ADV7511_MAX_ADDRS];
	u8   cec_valid_addrs;
	bool cec_enabled_adap;

	/* Is the adv7511 powered on? */
	bool power_on;
	/* Did we receive hotplug and rx-sense signals? */
	bool have_monitor;
	bool enabled_irq;
	/* timings from s_dv_timings */
	struct v4l2_dv_timings dv_timings;
	u32 fmt_code;
	u32 colorspace;
	u32 ycbcr_enc;
	u32 quantization;
	u32 xfer_func;
	u32 content_type;
	/* controls */
	struct v4l2_ctrl *hdmi_mode_ctrl;
	struct v4l2_ctrl *hotplug_ctrl;
	struct v4l2_ctrl *rx_sense_ctrl;
	struct v4l2_ctrl *have_edid0_ctrl;
	struct v4l2_ctrl *rgb_quantization_range_ctrl;
	struct v4l2_ctrl *content_type_ctrl;
	struct i2c_client *i2c_edid;
	struct i2c_client *i2c_pktmem;
	struct adv7511_state_edid edid;
	/* Running counter of the number of detected EDIDs (for debugging) */
	unsigned edid_detect_counter;
	struct workqueue_struct *work_queue;
	struct delayed_work edid_handler; /* work entry */
};

static void adv7511_check_monitor_present_status(struct v4l2_subdev *sd);
static bool adv7511_check_edid_status(struct v4l2_subdev *sd);
static void adv7511_setup(struct v4l2_subdev *sd);
static int adv7511_s_i2s_clock_freq(struct v4l2_subdev *sd, u32 freq);
static int adv7511_s_clock_freq(struct v4l2_subdev *sd, u32 freq);


static const struct v4l2_dv_timings_cap adv7511_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(0, ADV7511_MAX_WIDTH, 0, ADV7511_MAX_HEIGHT,
		ADV7511_MIN_PIXELCLOCK, ADV7511_MAX_PIXELCLOCK,
		V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
		V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};

static inline struct adv7511_state *get_adv7511_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7511_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct adv7511_state, hdl)->sd;
}

/* ------------------------ I2C ----------------------------------------------- */

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
	return -1;
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
	return -1;
}

static int adv7511_rd(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return adv_smbus_read_byte_data(client, reg);
}

static int adv7511_wr(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	int i;

	for (i = 0; i < 3; i++) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (ret == 0)
			return 0;
	}
	v4l2_err(sd, "%s: i2c write error\n", __func__);
	return ret;
}

/* To set specific bits in the register, a clear-mask is given (to be AND-ed),
   and then the value-mask (to be OR-ed). */
static inline void adv7511_wr_and_or(struct v4l2_subdev *sd, u8 reg, u8 clr_mask, u8 val_mask)
{
	adv7511_wr(sd, reg, (adv7511_rd(sd, reg) & clr_mask) | val_mask);
}

static int adv_smbus_read_i2c_block_data(struct i2c_client *client,
					 u8 command, unsigned length, u8 *values)
{
	union i2c_smbus_data data;
	int ret;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;

	ret = i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			     I2C_SMBUS_READ, command,
			     I2C_SMBUS_I2C_BLOCK_DATA, &data);
	memcpy(values, data.block + 1, length);
	return ret;
}

static void adv7511_edid_rd(struct v4l2_subdev *sd, uint16_t len, uint8_t *buf)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	int i;
	int err = 0;

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	for (i = 0; !err && i < len; i += I2C_SMBUS_BLOCK_MAX)
		err = adv_smbus_read_i2c_block_data(state->i2c_edid, i,
						    I2C_SMBUS_BLOCK_MAX, buf + i);
	if (err)
		v4l2_err(sd, "%s: i2c read error\n", __func__);
}

static inline int adv7511_cec_read(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	return i2c_smbus_read_byte_data(state->i2c_cec, reg);
}

static int adv7511_cec_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	int ret;
	int i;

	for (i = 0; i < 3; i++) {
		ret = i2c_smbus_write_byte_data(state->i2c_cec, reg, val);
		if (ret == 0)
			return 0;
	}
	v4l2_err(sd, "%s: I2C Write Problem\n", __func__);
	return ret;
}

static inline int adv7511_cec_write_and_or(struct v4l2_subdev *sd, u8 reg, u8 mask,
				   u8 val)
{
	return adv7511_cec_write(sd, reg, (adv7511_cec_read(sd, reg) & mask) | val);
}

static int adv7511_pktmem_rd(struct v4l2_subdev *sd, u8 reg)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	return adv_smbus_read_byte_data(state->i2c_pktmem, reg);
}

static int adv7511_pktmem_wr(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	int ret;
	int i;

	for (i = 0; i < 3; i++) {
		ret = i2c_smbus_write_byte_data(state->i2c_pktmem, reg, val);
		if (ret == 0)
			return 0;
	}
	v4l2_err(sd, "%s: i2c write error\n", __func__);
	return ret;
}

/* To set specific bits in the register, a clear-mask is given (to be AND-ed),
   and then the value-mask (to be OR-ed). */
static inline void adv7511_pktmem_wr_and_or(struct v4l2_subdev *sd, u8 reg, u8 clr_mask, u8 val_mask)
{
	adv7511_pktmem_wr(sd, reg, (adv7511_pktmem_rd(sd, reg) & clr_mask) | val_mask);
}

static inline bool adv7511_have_hotplug(struct v4l2_subdev *sd)
{
	return adv7511_rd(sd, 0x42) & MASK_ADV7511_HPD_DETECT;
}

static inline bool adv7511_have_rx_sense(struct v4l2_subdev *sd)
{
	return adv7511_rd(sd, 0x42) & MASK_ADV7511_MSEN_DETECT;
}

static void adv7511_csc_conversion_mode(struct v4l2_subdev *sd, u8 mode)
{
	adv7511_wr_and_or(sd, 0x18, 0x9f, (mode & 0x3)<<5);
}

static void adv7511_csc_coeff(struct v4l2_subdev *sd,
			      u16 A1, u16 A2, u16 A3, u16 A4,
			      u16 B1, u16 B2, u16 B3, u16 B4,
			      u16 C1, u16 C2, u16 C3, u16 C4)
{
	/* A */
	adv7511_wr_and_or(sd, 0x18, 0xe0, A1>>8);
	adv7511_wr(sd, 0x19, A1);
	adv7511_wr_and_or(sd, 0x1A, 0xe0, A2>>8);
	adv7511_wr(sd, 0x1B, A2);
	adv7511_wr_and_or(sd, 0x1c, 0xe0, A3>>8);
	adv7511_wr(sd, 0x1d, A3);
	adv7511_wr_and_or(sd, 0x1e, 0xe0, A4>>8);
	adv7511_wr(sd, 0x1f, A4);

	/* B */
	adv7511_wr_and_or(sd, 0x20, 0xe0, B1>>8);
	adv7511_wr(sd, 0x21, B1);
	adv7511_wr_and_or(sd, 0x22, 0xe0, B2>>8);
	adv7511_wr(sd, 0x23, B2);
	adv7511_wr_and_or(sd, 0x24, 0xe0, B3>>8);
	adv7511_wr(sd, 0x25, B3);
	adv7511_wr_and_or(sd, 0x26, 0xe0, B4>>8);
	adv7511_wr(sd, 0x27, B4);

	/* C */
	adv7511_wr_and_or(sd, 0x28, 0xe0, C1>>8);
	adv7511_wr(sd, 0x29, C1);
	adv7511_wr_and_or(sd, 0x2A, 0xe0, C2>>8);
	adv7511_wr(sd, 0x2B, C2);
	adv7511_wr_and_or(sd, 0x2C, 0xe0, C3>>8);
	adv7511_wr(sd, 0x2D, C3);
	adv7511_wr_and_or(sd, 0x2E, 0xe0, C4>>8);
	adv7511_wr(sd, 0x2F, C4);
}

static void adv7511_csc_rgb_full2limit(struct v4l2_subdev *sd, bool enable)
{
	if (enable) {
		u8 csc_mode = 0;
		adv7511_csc_conversion_mode(sd, csc_mode);
		adv7511_csc_coeff(sd,
				  4096-564, 0, 0, 256,
				  0, 4096-564, 0, 256,
				  0, 0, 4096-564, 256);
		/* enable CSC */
		adv7511_wr_and_or(sd, 0x18, 0x7f, 0x80);
		/* AVI infoframe: Limited range RGB (16-235) */
		adv7511_wr_and_or(sd, 0x57, 0xf3, 0x04);
	} else {
		/* disable CSC */
		adv7511_wr_and_or(sd, 0x18, 0x7f, 0x0);
		/* AVI infoframe: Full range RGB (0-255) */
		adv7511_wr_and_or(sd, 0x57, 0xf3, 0x08);
	}
}

static void adv7511_set_rgb_quantization_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	/* Only makes sense for RGB formats */
	if (state->fmt_code != MEDIA_BUS_FMT_RGB888_1X24) {
		/* so just keep quantization */
		adv7511_csc_rgb_full2limit(sd, false);
		return;
	}

	switch (ctrl->val) {
	case V4L2_DV_RGB_RANGE_AUTO:
		/* automatic */
		if (state->dv_timings.bt.flags & V4L2_DV_FL_IS_CE_VIDEO) {
			/* CE format, RGB limited range (16-235) */
			adv7511_csc_rgb_full2limit(sd, true);
		} else {
			/* not CE format, RGB full range (0-255) */
			adv7511_csc_rgb_full2limit(sd, false);
		}
		break;
	case V4L2_DV_RGB_RANGE_LIMITED:
		/* RGB limited range (16-235) */
		adv7511_csc_rgb_full2limit(sd, true);
		break;
	case V4L2_DV_RGB_RANGE_FULL:
		/* RGB full range (0-255) */
		adv7511_csc_rgb_full2limit(sd, false);
		break;
	}
}

/* ------------------------------ CTRL OPS ------------------------------ */

static int adv7511_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct adv7511_state *state = get_adv7511_state(sd);

	v4l2_dbg(1, debug, sd, "%s: ctrl id: %d, ctrl->val %d\n", __func__, ctrl->id, ctrl->val);

	if (state->hdmi_mode_ctrl == ctrl) {
		/* Set HDMI or DVI-D */
		adv7511_wr_and_or(sd, 0xaf, 0xfd, ctrl->val == V4L2_DV_TX_MODE_HDMI ? 0x02 : 0x00);
		return 0;
	}
	if (state->rgb_quantization_range_ctrl == ctrl) {
		adv7511_set_rgb_quantization_mode(sd, ctrl);
		return 0;
	}
	if (state->content_type_ctrl == ctrl) {
		u8 itc, cn;

		state->content_type = ctrl->val;
		itc = state->content_type != V4L2_DV_IT_CONTENT_TYPE_NO_ITC;
		cn = itc ? state->content_type : V4L2_DV_IT_CONTENT_TYPE_GRAPHICS;
		adv7511_wr_and_or(sd, 0x57, 0x7f, itc << 7);
		adv7511_wr_and_or(sd, 0x59, 0xcf, cn << 4);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops adv7511_ctrl_ops = {
	.s_ctrl = adv7511_s_ctrl,
};

/* ---------------------------- CORE OPS ------------------------------------------- */

#ifdef CONFIG_VIDEO_ADV_DEBUG
static void adv7511_inv_register(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	v4l2_info(sd, "0x000-0x0ff: Main Map\n");
	if (state->i2c_cec)
		v4l2_info(sd, "0x100-0x1ff: CEC Map\n");
}

static int adv7511_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	reg->size = 1;
	switch (reg->reg >> 8) {
	case 0:
		reg->val = adv7511_rd(sd, reg->reg & 0xff);
		break;
	case 1:
		if (state->i2c_cec) {
			reg->val = adv7511_cec_read(sd, reg->reg & 0xff);
			break;
		}
		/* fall through */
	default:
		v4l2_info(sd, "Register %03llx not supported\n", reg->reg);
		adv7511_inv_register(sd);
		break;
	}
	return 0;
}

static int adv7511_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	switch (reg->reg >> 8) {
	case 0:
		adv7511_wr(sd, reg->reg & 0xff, reg->val & 0xff);
		break;
	case 1:
		if (state->i2c_cec) {
			adv7511_cec_write(sd, reg->reg & 0xff, reg->val & 0xff);
			break;
		}
		/* fall through */
	default:
		v4l2_info(sd, "Register %03llx not supported\n", reg->reg);
		adv7511_inv_register(sd);
		break;
	}
	return 0;
}
#endif

struct adv7511_cfg_read_infoframe {
	const char *desc;
	u8 present_reg;
	u8 present_mask;
	u8 header[3];
	u16 payload_addr;
};

static u8 hdmi_infoframe_checksum(u8 *ptr, size_t size)
{
	u8 csum = 0;
	size_t i;

	/* compute checksum */
	for (i = 0; i < size; i++)
		csum += ptr[i];

	return 256 - csum;
}

static void log_infoframe(struct v4l2_subdev *sd, const struct adv7511_cfg_read_infoframe *cri)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	union hdmi_infoframe frame;
	u8 buffer[32];
	u8 len;
	int i;

	if (!(adv7511_rd(sd, cri->present_reg) & cri->present_mask)) {
		v4l2_info(sd, "%s infoframe not transmitted\n", cri->desc);
		return;
	}

	memcpy(buffer, cri->header, sizeof(cri->header));

	len = buffer[2];

	if (len + 4 > sizeof(buffer)) {
		v4l2_err(sd, "%s: invalid %s infoframe length %d\n", __func__, cri->desc, len);
		return;
	}

	if (cri->payload_addr >= 0x100) {
		for (i = 0; i < len; i++)
			buffer[i + 4] = adv7511_pktmem_rd(sd, cri->payload_addr + i - 0x100);
	} else {
		for (i = 0; i < len; i++)
			buffer[i + 4] = adv7511_rd(sd, cri->payload_addr + i);
	}
	buffer[3] = 0;
	buffer[3] = hdmi_infoframe_checksum(buffer, len + 4);

	if (hdmi_infoframe_unpack(&frame, buffer) < 0) {
		v4l2_err(sd, "%s: unpack of %s infoframe failed\n", __func__, cri->desc);
		return;
	}

	hdmi_infoframe_log(KERN_INFO, dev, &frame);
}

static void adv7511_log_infoframes(struct v4l2_subdev *sd)
{
	static const struct adv7511_cfg_read_infoframe cri[] = {
		{ "AVI", 0x44, 0x10, { 0x82, 2, 13 }, 0x55 },
		{ "Audio", 0x44, 0x08, { 0x84, 1, 10 }, 0x73 },
		{ "SDP", 0x40, 0x40, { 0x83, 1, 25 }, 0x103 },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(cri); i++)
		log_infoframe(sd, &cri[i]);
}

static int adv7511_log_status(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct adv7511_state_edid *edid = &state->edid;
	int i;

	static const char * const states[] = {
		"in reset",
		"reading EDID",
		"idle",
		"initializing HDCP",
		"HDCP enabled",
		"initializing HDCP repeater",
		"6", "7", "8", "9", "A", "B", "C", "D", "E", "F"
	};
	static const char * const errors[] = {
		"no error",
		"bad receiver BKSV",
		"Ri mismatch",
		"Pj mismatch",
		"i2c error",
		"timed out",
		"max repeater cascade exceeded",
		"hash check failed",
		"too many devices",
		"9", "A", "B", "C", "D", "E", "F"
	};

	v4l2_info(sd, "power %s\n", state->power_on ? "on" : "off");
	v4l2_info(sd, "%s hotplug, %s Rx Sense, %s EDID (%d block(s))\n",
		  (adv7511_rd(sd, 0x42) & MASK_ADV7511_HPD_DETECT) ? "detected" : "no",
		  (adv7511_rd(sd, 0x42) & MASK_ADV7511_MSEN_DETECT) ? "detected" : "no",
		  edid->segments ? "found" : "no",
		  edid->blocks);
	v4l2_info(sd, "%s output %s\n",
		  (adv7511_rd(sd, 0xaf) & 0x02) ?
		  "HDMI" : "DVI-D",
		  (adv7511_rd(sd, 0xa1) & 0x3c) ?
		  "disabled" : "enabled");
	v4l2_info(sd, "state: %s, error: %s, detect count: %u, msk/irq: %02x/%02x\n",
			  states[adv7511_rd(sd, 0xc8) & 0xf],
			  errors[adv7511_rd(sd, 0xc8) >> 4], state->edid_detect_counter,
			  adv7511_rd(sd, 0x94), adv7511_rd(sd, 0x96));
	v4l2_info(sd, "RGB quantization: %s range\n", adv7511_rd(sd, 0x18) & 0x80 ? "limited" : "full");
	if (adv7511_rd(sd, 0xaf) & 0x02) {
		/* HDMI only */
		u8 manual_cts = adv7511_rd(sd, 0x0a) & 0x80;
		u32 N = (adv7511_rd(sd, 0x01) & 0xf) << 16 |
			adv7511_rd(sd, 0x02) << 8 |
			adv7511_rd(sd, 0x03);
		u8 vic_detect = adv7511_rd(sd, 0x3e) >> 2;
		u8 vic_sent = adv7511_rd(sd, 0x3d) & 0x3f;
		u32 CTS;

		if (manual_cts)
			CTS = (adv7511_rd(sd, 0x07) & 0xf) << 16 |
			      adv7511_rd(sd, 0x08) << 8 |
			      adv7511_rd(sd, 0x09);
		else
			CTS = (adv7511_rd(sd, 0x04) & 0xf) << 16 |
			      adv7511_rd(sd, 0x05) << 8 |
			      adv7511_rd(sd, 0x06);
		v4l2_info(sd, "CTS %s mode: N %d, CTS %d\n",
			  manual_cts ? "manual" : "automatic", N, CTS);
		v4l2_info(sd, "VIC: detected %d, sent %d\n",
			  vic_detect, vic_sent);
		adv7511_log_infoframes(sd);
	}
	if (state->dv_timings.type == V4L2_DV_BT_656_1120)
		v4l2_print_dv_timings(sd->name, "timings: ",
				&state->dv_timings, false);
	else
		v4l2_info(sd, "no timings set\n");
	v4l2_info(sd, "i2c edid addr: 0x%x\n", state->i2c_edid_addr);

	if (state->i2c_cec == NULL)
		return 0;

	v4l2_info(sd, "i2c cec addr: 0x%x\n", state->i2c_cec_addr);

	v4l2_info(sd, "CEC: %s\n", state->cec_enabled_adap ?
			"enabled" : "disabled");
	if (state->cec_enabled_adap) {
		for (i = 0; i < ADV7511_MAX_ADDRS; i++) {
			bool is_valid = state->cec_valid_addrs & (1 << i);

			if (is_valid)
				v4l2_info(sd, "CEC Logical Address: 0x%x\n",
					  state->cec_addr[i]);
		}
	}
	v4l2_info(sd, "i2c pktmem addr: 0x%x\n", state->i2c_pktmem_addr);
	return 0;
}

/* Power up/down adv7511 */
static int adv7511_s_power(struct v4l2_subdev *sd, int on)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	const int retries = 20;
	int i;

	v4l2_dbg(1, debug, sd, "%s: power %s\n", __func__, on ? "on" : "off");

	state->power_on = on;

	if (!on) {
		/* Power down */
		adv7511_wr_and_or(sd, 0x41, 0xbf, 0x40);
		return true;
	}

	/* Power up */
	/* The adv7511 does not always come up immediately.
	   Retry multiple times. */
	for (i = 0; i < retries; i++) {
		adv7511_wr_and_or(sd, 0x41, 0xbf, 0x0);
		if ((adv7511_rd(sd, 0x41) & 0x40) == 0)
			break;
		adv7511_wr_and_or(sd, 0x41, 0xbf, 0x40);
		msleep(10);
	}
	if (i == retries) {
		v4l2_dbg(1, debug, sd, "%s: failed to powerup the adv7511!\n", __func__);
		adv7511_s_power(sd, 0);
		return false;
	}
	if (i > 1)
		v4l2_dbg(1, debug, sd, "%s: needed %d retries to powerup the adv7511\n", __func__, i);

	/* Reserved registers that must be set */
	adv7511_wr(sd, 0x98, 0x03);
	adv7511_wr_and_or(sd, 0x9a, 0xfe, 0x70);
	adv7511_wr(sd, 0x9c, 0x30);
	adv7511_wr_and_or(sd, 0x9d, 0xfc, 0x01);
	adv7511_wr(sd, 0xa2, 0xa4);
	adv7511_wr(sd, 0xa3, 0xa4);
	adv7511_wr(sd, 0xe0, 0xd0);
	adv7511_wr(sd, 0xf9, 0x00);

	adv7511_wr(sd, 0x43, state->i2c_edid_addr);
	adv7511_wr(sd, 0x45, state->i2c_pktmem_addr);

	/* Set number of attempts to read the EDID */
	adv7511_wr(sd, 0xc9, 0xf);
	return true;
}

#if IS_ENABLED(CONFIG_VIDEO_ADV7511_CEC)
static int adv7511_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct adv7511_state *state = adap->priv;
	struct v4l2_subdev *sd = &state->sd;

	if (state->i2c_cec == NULL)
		return -EIO;

	if (!state->cec_enabled_adap && enable) {
		/* power up cec section */
		adv7511_cec_write_and_or(sd, 0x4e, 0xfc, 0x01);
		/* legacy mode and clear all rx buffers */
		adv7511_cec_write(sd, 0x4a, 0x07);
		adv7511_cec_write(sd, 0x4a, 0);
		adv7511_cec_write_and_or(sd, 0x11, 0xfe, 0); /* initially disable tx */
		/* enabled irqs: */
		/* tx: ready */
		/* tx: arbitration lost */
		/* tx: retry timeout */
		/* rx: ready 1 */
		if (state->enabled_irq)
			adv7511_wr_and_or(sd, 0x95, 0xc0, 0x39);
	} else if (state->cec_enabled_adap && !enable) {
		if (state->enabled_irq)
			adv7511_wr_and_or(sd, 0x95, 0xc0, 0x00);
		/* disable address mask 1-3 */
		adv7511_cec_write_and_or(sd, 0x4b, 0x8f, 0x00);
		/* power down cec section */
		adv7511_cec_write_and_or(sd, 0x4e, 0xfc, 0x00);
		state->cec_valid_addrs = 0;
	}
	state->cec_enabled_adap = enable;
	return 0;
}

static int adv7511_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct adv7511_state *state = adap->priv;
	struct v4l2_subdev *sd = &state->sd;
	unsigned int i, free_idx = ADV7511_MAX_ADDRS;

	if (!state->cec_enabled_adap)
		return addr == CEC_LOG_ADDR_INVALID ? 0 : -EIO;

	if (addr == CEC_LOG_ADDR_INVALID) {
		adv7511_cec_write_and_or(sd, 0x4b, 0x8f, 0);
		state->cec_valid_addrs = 0;
		return 0;
	}

	for (i = 0; i < ADV7511_MAX_ADDRS; i++) {
		bool is_valid = state->cec_valid_addrs & (1 << i);

		if (free_idx == ADV7511_MAX_ADDRS && !is_valid)
			free_idx = i;
		if (is_valid && state->cec_addr[i] == addr)
			return 0;
	}
	if (i == ADV7511_MAX_ADDRS) {
		i = free_idx;
		if (i == ADV7511_MAX_ADDRS)
			return -ENXIO;
	}
	state->cec_addr[i] = addr;
	state->cec_valid_addrs |= 1 << i;

	switch (i) {
	case 0:
		/* enable address mask 0 */
		adv7511_cec_write_and_or(sd, 0x4b, 0xef, 0x10);
		/* set address for mask 0 */
		adv7511_cec_write_and_or(sd, 0x4c, 0xf0, addr);
		break;
	case 1:
		/* enable address mask 1 */
		adv7511_cec_write_and_or(sd, 0x4b, 0xdf, 0x20);
		/* set address for mask 1 */
		adv7511_cec_write_and_or(sd, 0x4c, 0x0f, addr << 4);
		break;
	case 2:
		/* enable address mask 2 */
		adv7511_cec_write_and_or(sd, 0x4b, 0xbf, 0x40);
		/* set address for mask 1 */
		adv7511_cec_write_and_or(sd, 0x4d, 0xf0, addr);
		break;
	}
	return 0;
}

static int adv7511_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				     u32 signal_free_time, struct cec_msg *msg)
{
	struct adv7511_state *state = adap->priv;
	struct v4l2_subdev *sd = &state->sd;
	u8 len = msg->len;
	unsigned int i;

	v4l2_dbg(1, debug, sd, "%s: len %d\n", __func__, len);

	if (len > 16) {
		v4l2_err(sd, "%s: len exceeded 16 (%d)\n", __func__, len);
		return -EINVAL;
	}

	/*
	 * The number of retries is the number of attempts - 1, but retry
	 * at least once. It's not clear if a value of 0 is allowed, so
	 * let's do at least one retry.
	 */
	adv7511_cec_write_and_or(sd, 0x12, ~0x70, max(1, attempts - 1) << 4);

	/* blocking, clear cec tx irq status */
	adv7511_wr_and_or(sd, 0x97, 0xc7, 0x38);

	/* write data */
	for (i = 0; i < len; i++)
		adv7511_cec_write(sd, i, msg->msg[i]);

	/* set length (data + header) */
	adv7511_cec_write(sd, 0x10, len);
	/* start transmit, enable tx */
	adv7511_cec_write(sd, 0x11, 0x01);
	return 0;
}

static void adv_cec_tx_raw_status(struct v4l2_subdev *sd, u8 tx_raw_status)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	if ((adv7511_cec_read(sd, 0x11) & 0x01) == 0) {
		v4l2_dbg(1, debug, sd, "%s: tx raw: tx disabled\n", __func__);
		return;
	}

	if (tx_raw_status & 0x10) {
		v4l2_dbg(1, debug, sd,
			 "%s: tx raw: arbitration lost\n", __func__);
		cec_transmit_done(state->cec_adap, CEC_TX_STATUS_ARB_LOST,
				  1, 0, 0, 0);
		return;
	}
	if (tx_raw_status & 0x08) {
		u8 status;
		u8 nack_cnt;
		u8 low_drive_cnt;

		v4l2_dbg(1, debug, sd, "%s: tx raw: retry failed\n", __func__);
		/*
		 * We set this status bit since this hardware performs
		 * retransmissions.
		 */
		status = CEC_TX_STATUS_MAX_RETRIES;
		nack_cnt = adv7511_cec_read(sd, 0x14) & 0xf;
		if (nack_cnt)
			status |= CEC_TX_STATUS_NACK;
		low_drive_cnt = adv7511_cec_read(sd, 0x14) >> 4;
		if (low_drive_cnt)
			status |= CEC_TX_STATUS_LOW_DRIVE;
		cec_transmit_done(state->cec_adap, status,
				  0, nack_cnt, low_drive_cnt, 0);
		return;
	}
	if (tx_raw_status & 0x20) {
		v4l2_dbg(1, debug, sd, "%s: tx raw: ready ok\n", __func__);
		cec_transmit_done(state->cec_adap, CEC_TX_STATUS_OK, 0, 0, 0, 0);
		return;
	}
}

static const struct cec_adap_ops adv7511_cec_adap_ops = {
	.adap_enable = adv7511_cec_adap_enable,
	.adap_log_addr = adv7511_cec_adap_log_addr,
	.adap_transmit = adv7511_cec_adap_transmit,
};
#endif

/* Enable interrupts */
static void adv7511_set_isr(struct v4l2_subdev *sd, bool enable)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	u8 irqs = MASK_ADV7511_HPD_INT | MASK_ADV7511_MSEN_INT;
	u8 irqs_rd;
	int retries = 100;

	v4l2_dbg(2, debug, sd, "%s: %s\n", __func__, enable ? "enable" : "disable");

	if (state->enabled_irq == enable)
		return;
	state->enabled_irq = enable;

	/* The datasheet says that the EDID ready interrupt should be
	   disabled if there is no hotplug. */
	if (!enable)
		irqs = 0;
	else if (adv7511_have_hotplug(sd))
		irqs |= MASK_ADV7511_EDID_RDY_INT;

	adv7511_wr_and_or(sd, 0x95, 0xc0,
			  (state->cec_enabled_adap && enable) ? 0x39 : 0x00);

	/*
	 * This i2c write can fail (approx. 1 in 1000 writes). But it
	 * is essential that this register is correct, so retry it
	 * multiple times.
	 *
	 * Note that the i2c write does not report an error, but the readback
	 * clearly shows the wrong value.
	 */
	do {
		adv7511_wr(sd, 0x94, irqs);
		irqs_rd = adv7511_rd(sd, 0x94);
	} while (retries-- && irqs_rd != irqs);

	if (irqs_rd == irqs)
		return;
	v4l2_err(sd, "Could not set interrupts: hw failure?\n");
}

/* Interrupt handler */
static int adv7511_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	u8 irq_status;
	u8 cec_irq;

	/* disable interrupts to prevent a race condition */
	adv7511_set_isr(sd, false);
	irq_status = adv7511_rd(sd, 0x96);
	cec_irq = adv7511_rd(sd, 0x97);
	/* clear detected interrupts */
	adv7511_wr(sd, 0x96, irq_status);
	adv7511_wr(sd, 0x97, cec_irq);

	v4l2_dbg(1, debug, sd, "%s: irq 0x%x, cec-irq 0x%x\n", __func__,
		 irq_status, cec_irq);

	if (irq_status & (MASK_ADV7511_HPD_INT | MASK_ADV7511_MSEN_INT))
		adv7511_check_monitor_present_status(sd);
	if (irq_status & MASK_ADV7511_EDID_RDY_INT)
		adv7511_check_edid_status(sd);

#if IS_ENABLED(CONFIG_VIDEO_ADV7511_CEC)
	if (cec_irq & 0x38)
		adv_cec_tx_raw_status(sd, cec_irq);

	if (cec_irq & 1) {
		struct adv7511_state *state = get_adv7511_state(sd);
		struct cec_msg msg;

		msg.len = adv7511_cec_read(sd, 0x25) & 0x1f;

		v4l2_dbg(1, debug, sd, "%s: cec msg len %d\n", __func__,
			 msg.len);

		if (msg.len > 16)
			msg.len = 16;

		if (msg.len) {
			u8 i;

			for (i = 0; i < msg.len; i++)
				msg.msg[i] = adv7511_cec_read(sd, i + 0x15);

			adv7511_cec_write(sd, 0x4a, 1); /* toggle to re-enable rx 1 */
			adv7511_cec_write(sd, 0x4a, 0);
			cec_received_msg(state->cec_adap, &msg);
		}
	}
#endif

	/* enable interrupts */
	adv7511_set_isr(sd, true);

	if (handled)
		*handled = true;
	return 0;
}

static const struct v4l2_subdev_core_ops adv7511_core_ops = {
	.log_status = adv7511_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = adv7511_g_register,
	.s_register = adv7511_s_register,
#endif
	.s_power = adv7511_s_power,
	.interrupt_service_routine = adv7511_isr,
};

/* ------------------------------ VIDEO OPS ------------------------------ */

/* Enable/disable adv7511 output */
static int adv7511_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	v4l2_dbg(1, debug, sd, "%s: %sable\n", __func__, (enable ? "en" : "dis"));
	adv7511_wr_and_or(sd, 0xa1, ~0x3c, (enable ? 0 : 0x3c));
	if (enable) {
		adv7511_check_monitor_present_status(sd);
	} else {
		adv7511_s_power(sd, 0);
		state->have_monitor = false;
	}
	return 0;
}

static int adv7511_s_dv_timings(struct v4l2_subdev *sd,
			       struct v4l2_dv_timings *timings)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 fps;

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	/* quick sanity check */
	if (!v4l2_valid_dv_timings(timings, &adv7511_timings_cap, NULL, NULL))
		return -EINVAL;

	/* Fill the optional fields .standards and .flags in struct v4l2_dv_timings
	   if the format is one of the CEA or DMT timings. */
	v4l2_find_dv_timings_cap(timings, &adv7511_timings_cap, 0, NULL, NULL);

	/* save timings */
	state->dv_timings = *timings;

	/* set h/vsync polarities */
	adv7511_wr_and_or(sd, 0x17, 0x9f,
		((bt->polarities & V4L2_DV_VSYNC_POS_POL) ? 0 : 0x40) |
		((bt->polarities & V4L2_DV_HSYNC_POS_POL) ? 0 : 0x20));

	fps = (u32)bt->pixelclock / (V4L2_DV_BT_FRAME_WIDTH(bt) * V4L2_DV_BT_FRAME_HEIGHT(bt));
	switch (fps) {
	case 24:
		adv7511_wr_and_or(sd, 0xfb, 0xf9, 1 << 1);
		break;
	case 25:
		adv7511_wr_and_or(sd, 0xfb, 0xf9, 2 << 1);
		break;
	case 30:
		adv7511_wr_and_or(sd, 0xfb, 0xf9, 3 << 1);
		break;
	default:
		adv7511_wr_and_or(sd, 0xfb, 0xf9, 0);
		break;
	}

	/* update quantization range based on new dv_timings */
	adv7511_set_rgb_quantization_mode(sd, state->rgb_quantization_range_ctrl);

	return 0;
}

static int adv7511_g_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (!timings)
		return -EINVAL;

	*timings = state->dv_timings;

	return 0;
}

static int adv7511_enum_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings, &adv7511_timings_cap, NULL, NULL);
}

static int adv7511_dv_timings_cap(struct v4l2_subdev *sd,
				  struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = adv7511_timings_cap;
	return 0;
}

static const struct v4l2_subdev_video_ops adv7511_video_ops = {
	.s_stream = adv7511_s_stream,
	.s_dv_timings = adv7511_s_dv_timings,
	.g_dv_timings = adv7511_g_dv_timings,
};

/* ------------------------------ AUDIO OPS ------------------------------ */
static int adv7511_s_audio_stream(struct v4l2_subdev *sd, int enable)
{
	v4l2_dbg(1, debug, sd, "%s: %sable\n", __func__, (enable ? "en" : "dis"));

	if (enable)
		adv7511_wr_and_or(sd, 0x4b, 0x3f, 0x80);
	else
		adv7511_wr_and_or(sd, 0x4b, 0x3f, 0x40);

	return 0;
}

static int adv7511_s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	u32 N;

	switch (freq) {
	case 32000:  N = 4096;  break;
	case 44100:  N = 6272;  break;
	case 48000:  N = 6144;  break;
	case 88200:  N = 12544; break;
	case 96000:  N = 12288; break;
	case 176400: N = 25088; break;
	case 192000: N = 24576; break;
	default:
		return -EINVAL;
	}

	/* Set N (used with CTS to regenerate the audio clock) */
	adv7511_wr(sd, 0x01, (N >> 16) & 0xf);
	adv7511_wr(sd, 0x02, (N >> 8) & 0xff);
	adv7511_wr(sd, 0x03, N & 0xff);

	return 0;
}

static int adv7511_s_i2s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	u32 i2s_sf;

	switch (freq) {
	case 32000:  i2s_sf = 0x30; break;
	case 44100:  i2s_sf = 0x00; break;
	case 48000:  i2s_sf = 0x20; break;
	case 88200:  i2s_sf = 0x80; break;
	case 96000:  i2s_sf = 0xa0; break;
	case 176400: i2s_sf = 0xc0; break;
	case 192000: i2s_sf = 0xe0; break;
	default:
		return -EINVAL;
	}

	/* Set sampling frequency for I2S audio to 48 kHz */
	adv7511_wr_and_or(sd, 0x15, 0xf, i2s_sf);

	return 0;
}

static int adv7511_s_routing(struct v4l2_subdev *sd, u32 input, u32 output, u32 config)
{
	/* Only 2 channels in use for application */
	adv7511_wr_and_or(sd, 0x73, 0xf8, 0x1);
	/* Speaker mapping */
	adv7511_wr(sd, 0x76, 0x00);

	/* 16 bit audio word length */
	adv7511_wr_and_or(sd, 0x14, 0xf0, 0x02);

	return 0;
}

static const struct v4l2_subdev_audio_ops adv7511_audio_ops = {
	.s_stream = adv7511_s_audio_stream,
	.s_clock_freq = adv7511_s_clock_freq,
	.s_i2s_clock_freq = adv7511_s_i2s_clock_freq,
	.s_routing = adv7511_s_routing,
};

/* ---------------------------- PAD OPS ------------------------------------- */

static int adv7511_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad != 0)
		return -EINVAL;

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = state->edid.segments * 2;
		return 0;
	}

	if (state->edid.segments == 0)
		return -ENODATA;

	if (edid->start_block >= state->edid.segments * 2)
		return -EINVAL;

	if (edid->start_block + edid->blocks > state->edid.segments * 2)
		edid->blocks = state->edid.segments * 2 - edid->start_block;

	memcpy(edid->edid, &state->edid.data[edid->start_block * 128],
			128 * edid->blocks);

	return 0;
}

static int adv7511_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad != 0)
		return -EINVAL;

	switch (code->index) {
	case 0:
		code->code = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	case 1:
		code->code = MEDIA_BUS_FMT_YUYV8_1X16;
		break;
	case 2:
		code->code = MEDIA_BUS_FMT_UYVY8_1X16;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void adv7511_fill_format(struct adv7511_state *state,
				struct v4l2_mbus_framefmt *format)
{
	format->width = state->dv_timings.bt.width;
	format->height = state->dv_timings.bt.height;
	format->field = V4L2_FIELD_NONE;
}

static int adv7511_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	if (format->pad != 0)
		return -EINVAL;

	memset(&format->format, 0, sizeof(format->format));
	adv7511_fill_format(state, &format->format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		format->format.code = fmt->code;
		format->format.colorspace = fmt->colorspace;
		format->format.ycbcr_enc = fmt->ycbcr_enc;
		format->format.quantization = fmt->quantization;
		format->format.xfer_func = fmt->xfer_func;
	} else {
		format->format.code = state->fmt_code;
		format->format.colorspace = state->colorspace;
		format->format.ycbcr_enc = state->ycbcr_enc;
		format->format.quantization = state->quantization;
		format->format.xfer_func = state->xfer_func;
	}

	return 0;
}

static int adv7511_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	/*
	 * Bitfield namings come the CEA-861-F standard, table 8 "Auxiliary
	 * Video Information (AVI) InfoFrame Format"
	 *
	 * c = Colorimetry
	 * ec = Extended Colorimetry
	 * y = RGB or YCbCr
	 * q = RGB Quantization Range
	 * yq = YCC Quantization Range
	 */
	u8 c = HDMI_COLORIMETRY_NONE;
	u8 ec = HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
	u8 y = HDMI_COLORSPACE_RGB;
	u8 q = HDMI_QUANTIZATION_RANGE_DEFAULT;
	u8 yq = HDMI_YCC_QUANTIZATION_RANGE_LIMITED;
	u8 itc = state->content_type != V4L2_DV_IT_CONTENT_TYPE_NO_ITC;
	u8 cn = itc ? state->content_type : V4L2_DV_IT_CONTENT_TYPE_GRAPHICS;

	if (format->pad != 0)
		return -EINVAL;
	switch (format->format.code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_RGB888_1X24:
		break;
	default:
		return -EINVAL;
	}

	adv7511_fill_format(state, &format->format);
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		fmt->code = format->format.code;
		fmt->colorspace = format->format.colorspace;
		fmt->ycbcr_enc = format->format.ycbcr_enc;
		fmt->quantization = format->format.quantization;
		fmt->xfer_func = format->format.xfer_func;
		return 0;
	}

	switch (format->format.code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
		adv7511_wr_and_or(sd, 0x15, 0xf0, 0x01);
		adv7511_wr_and_or(sd, 0x16, 0x03, 0xb8);
		y = HDMI_COLORSPACE_YUV422;
		break;
	case MEDIA_BUS_FMT_YUYV8_1X16:
		adv7511_wr_and_or(sd, 0x15, 0xf0, 0x01);
		adv7511_wr_and_or(sd, 0x16, 0x03, 0xbc);
		y = HDMI_COLORSPACE_YUV422;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	default:
		adv7511_wr_and_or(sd, 0x15, 0xf0, 0x00);
		adv7511_wr_and_or(sd, 0x16, 0x03, 0x00);
		break;
	}
	state->fmt_code = format->format.code;
	state->colorspace = format->format.colorspace;
	state->ycbcr_enc = format->format.ycbcr_enc;
	state->quantization = format->format.quantization;
	state->xfer_func = format->format.xfer_func;

	switch (format->format.colorspace) {
	case V4L2_COLORSPACE_ADOBERGB:
		c = HDMI_COLORIMETRY_EXTENDED;
		ec = y ? HDMI_EXTENDED_COLORIMETRY_ADOBE_YCC_601 :
			 HDMI_EXTENDED_COLORIMETRY_ADOBE_RGB;
		break;
	case V4L2_COLORSPACE_SMPTE170M:
		c = y ? HDMI_COLORIMETRY_ITU_601 : HDMI_COLORIMETRY_NONE;
		if (y && format->format.ycbcr_enc == V4L2_YCBCR_ENC_XV601) {
			c = HDMI_COLORIMETRY_EXTENDED;
			ec = HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
		}
		break;
	case V4L2_COLORSPACE_REC709:
		c = y ? HDMI_COLORIMETRY_ITU_709 : HDMI_COLORIMETRY_NONE;
		if (y && format->format.ycbcr_enc == V4L2_YCBCR_ENC_XV709) {
			c = HDMI_COLORIMETRY_EXTENDED;
			ec = HDMI_EXTENDED_COLORIMETRY_XV_YCC_709;
		}
		break;
	case V4L2_COLORSPACE_SRGB:
		c = y ? HDMI_COLORIMETRY_EXTENDED : HDMI_COLORIMETRY_NONE;
		ec = y ? HDMI_EXTENDED_COLORIMETRY_S_YCC_601 :
			 HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
		break;
	case V4L2_COLORSPACE_BT2020:
		c = HDMI_COLORIMETRY_EXTENDED;
		if (y && format->format.ycbcr_enc == V4L2_YCBCR_ENC_BT2020_CONST_LUM)
			ec = 5; /* Not yet available in hdmi.h */
		else
			ec = 6; /* Not yet available in hdmi.h */
		break;
	default:
		break;
	}

	/*
	 * CEA-861-F says that for RGB formats the YCC range must match the
	 * RGB range, although sources should ignore the YCC range.
	 *
	 * The RGB quantization range shouldn't be non-zero if the EDID doesn't
	 * have the Q bit set in the Video Capabilities Data Block, however this
	 * isn't checked at the moment. The assumption is that the application
	 * knows the EDID and can detect this.
	 *
	 * The same is true for the YCC quantization range: non-standard YCC
	 * quantization ranges should only be sent if the EDID has the YQ bit
	 * set in the Video Capabilities Data Block.
	 */
	switch (format->format.quantization) {
	case V4L2_QUANTIZATION_FULL_RANGE:
		q = y ? HDMI_QUANTIZATION_RANGE_DEFAULT :
			HDMI_QUANTIZATION_RANGE_FULL;
		yq = q ? q - 1 : HDMI_YCC_QUANTIZATION_RANGE_FULL;
		break;
	case V4L2_QUANTIZATION_LIM_RANGE:
		q = y ? HDMI_QUANTIZATION_RANGE_DEFAULT :
			HDMI_QUANTIZATION_RANGE_LIMITED;
		yq = q ? q - 1 : HDMI_YCC_QUANTIZATION_RANGE_LIMITED;
		break;
	}

	adv7511_wr_and_or(sd, 0x4a, 0xbf, 0);
	adv7511_wr_and_or(sd, 0x55, 0x9f, y << 5);
	adv7511_wr_and_or(sd, 0x56, 0x3f, c << 6);
	adv7511_wr_and_or(sd, 0x57, 0x83, (ec << 4) | (q << 2) | (itc << 7));
	adv7511_wr_and_or(sd, 0x59, 0x0f, (yq << 6) | (cn << 4));
	adv7511_wr_and_or(sd, 0x4a, 0xff, 1);
	adv7511_set_rgb_quantization_mode(sd, state->rgb_quantization_range_ctrl);

	return 0;
}

static const struct v4l2_subdev_pad_ops adv7511_pad_ops = {
	.get_edid = adv7511_get_edid,
	.enum_mbus_code = adv7511_enum_mbus_code,
	.get_fmt = adv7511_get_fmt,
	.set_fmt = adv7511_set_fmt,
	.enum_dv_timings = adv7511_enum_dv_timings,
	.dv_timings_cap = adv7511_dv_timings_cap,
};

/* --------------------- SUBDEV OPS --------------------------------------- */

static const struct v4l2_subdev_ops adv7511_ops = {
	.core  = &adv7511_core_ops,
	.pad  = &adv7511_pad_ops,
	.video = &adv7511_video_ops,
	.audio = &adv7511_audio_ops,
};

/* ----------------------------------------------------------------------- */
static void adv7511_dbg_dump_edid(int lvl, int debug, struct v4l2_subdev *sd, int segment, u8 *buf)
{
	if (debug >= lvl) {
		int i, j;
		v4l2_dbg(lvl, debug, sd, "edid segment %d\n", segment);
		for (i = 0; i < 256; i += 16) {
			u8 b[128];
			u8 *bp = b;
			if (i == 128)
				v4l2_dbg(lvl, debug, sd, "\n");
			for (j = i; j < i + 16; j++) {
				sprintf(bp, "0x%02x, ", buf[j]);
				bp += 6;
			}
			bp[0] = '\0';
			v4l2_dbg(lvl, debug, sd, "%s\n", b);
		}
	}
}

static void adv7511_notify_no_edid(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct adv7511_edid_detect ed;

	/* We failed to read the EDID, so send an event for this. */
	ed.present = false;
	ed.segment = adv7511_rd(sd, 0xc4);
	ed.phys_addr = CEC_PHYS_ADDR_INVALID;
	cec_s_phys_addr(state->cec_adap, ed.phys_addr, false);
	v4l2_subdev_notify(sd, ADV7511_EDID_DETECT, (void *)&ed);
	v4l2_ctrl_s_ctrl(state->have_edid0_ctrl, 0x0);
}

static void adv7511_edid_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct adv7511_state *state = container_of(dwork, struct adv7511_state, edid_handler);
	struct v4l2_subdev *sd = &state->sd;

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (adv7511_check_edid_status(sd)) {
		/* Return if we received the EDID. */
		return;
	}

	if (adv7511_have_hotplug(sd)) {
		/* We must retry reading the EDID several times, it is possible
		 * that initially the EDID couldn't be read due to i2c errors
		 * (DVI connectors are particularly prone to this problem). */
		if (state->edid.read_retries) {
			state->edid.read_retries--;
			v4l2_dbg(1, debug, sd, "%s: edid read failed\n", __func__);
			state->have_monitor = false;
			adv7511_s_power(sd, false);
			adv7511_s_power(sd, true);
			queue_delayed_work(state->work_queue, &state->edid_handler, EDID_DELAY);
			return;
		}
	}

	/* We failed to read the EDID, so send an event for this. */
	adv7511_notify_no_edid(sd);
	v4l2_dbg(1, debug, sd, "%s: no edid found\n", __func__);
}

static void adv7511_audio_setup(struct v4l2_subdev *sd)
{
	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	adv7511_s_i2s_clock_freq(sd, 48000);
	adv7511_s_clock_freq(sd, 48000);
	adv7511_s_routing(sd, 0, 0, 0);
}

/* Configure hdmi transmitter. */
static void adv7511_setup(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	/* Input format: RGB 4:4:4 */
	adv7511_wr_and_or(sd, 0x15, 0xf0, 0x0);
	/* Output format: RGB 4:4:4 */
	adv7511_wr_and_or(sd, 0x16, 0x7f, 0x0);
	/* 1st order interpolation 4:2:2 -> 4:4:4 up conversion, Aspect ratio: 16:9 */
	adv7511_wr_and_or(sd, 0x17, 0xf9, 0x06);
	/* Disable pixel repetition */
	adv7511_wr_and_or(sd, 0x3b, 0x9f, 0x0);
	/* Disable CSC */
	adv7511_wr_and_or(sd, 0x18, 0x7f, 0x0);
	/* Output format: RGB 4:4:4, Active Format Information is valid,
	 * underscanned */
	adv7511_wr_and_or(sd, 0x55, 0x9c, 0x12);
	/* AVI Info frame packet enable, Audio Info frame disable */
	adv7511_wr_and_or(sd, 0x44, 0xe7, 0x10);
	/* Colorimetry, Active format aspect ratio: same as picure. */
	adv7511_wr(sd, 0x56, 0xa8);
	/* No encryption */
	adv7511_wr_and_or(sd, 0xaf, 0xed, 0x0);

	/* Positive clk edge capture for input video clock */
	adv7511_wr_and_or(sd, 0xba, 0x1f, 0x60);

	adv7511_audio_setup(sd);

	v4l2_ctrl_handler_setup(&state->hdl);
}

static void adv7511_notify_monitor_detect(struct v4l2_subdev *sd)
{
	struct adv7511_monitor_detect mdt;
	struct adv7511_state *state = get_adv7511_state(sd);

	mdt.present = state->have_monitor;
	v4l2_subdev_notify(sd, ADV7511_MONITOR_DETECT, (void *)&mdt);
}

static void adv7511_check_monitor_present_status(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	/* read hotplug and rx-sense state */
	u8 status = adv7511_rd(sd, 0x42);

	v4l2_dbg(1, debug, sd, "%s: status: 0x%x%s%s\n",
			 __func__,
			 status,
			 status & MASK_ADV7511_HPD_DETECT ? ", hotplug" : "",
			 status & MASK_ADV7511_MSEN_DETECT ? ", rx-sense" : "");

	/* update read only ctrls */
	v4l2_ctrl_s_ctrl(state->hotplug_ctrl, adv7511_have_hotplug(sd) ? 0x1 : 0x0);
	v4l2_ctrl_s_ctrl(state->rx_sense_ctrl, adv7511_have_rx_sense(sd) ? 0x1 : 0x0);

	if ((status & MASK_ADV7511_HPD_DETECT) && ((status & MASK_ADV7511_MSEN_DETECT) || state->edid.segments)) {
		v4l2_dbg(1, debug, sd, "%s: hotplug and (rx-sense or edid)\n", __func__);
		if (!state->have_monitor) {
			v4l2_dbg(1, debug, sd, "%s: monitor detected\n", __func__);
			state->have_monitor = true;
			adv7511_set_isr(sd, true);
			if (!adv7511_s_power(sd, true)) {
				v4l2_dbg(1, debug, sd, "%s: monitor detected, powerup failed\n", __func__);
				return;
			}
			adv7511_setup(sd);
			adv7511_notify_monitor_detect(sd);
			state->edid.read_retries = EDID_MAX_RETRIES;
			queue_delayed_work(state->work_queue, &state->edid_handler, EDID_DELAY);
		}
	} else if (status & MASK_ADV7511_HPD_DETECT) {
		v4l2_dbg(1, debug, sd, "%s: hotplug detected\n", __func__);
		state->edid.read_retries = EDID_MAX_RETRIES;
		queue_delayed_work(state->work_queue, &state->edid_handler, EDID_DELAY);
	} else if (!(status & MASK_ADV7511_HPD_DETECT)) {
		v4l2_dbg(1, debug, sd, "%s: hotplug not detected\n", __func__);
		if (state->have_monitor) {
			v4l2_dbg(1, debug, sd, "%s: monitor not detected\n", __func__);
			state->have_monitor = false;
			adv7511_notify_monitor_detect(sd);
		}
		adv7511_s_power(sd, false);
		memset(&state->edid, 0, sizeof(struct adv7511_state_edid));
		adv7511_notify_no_edid(sd);
	}
}

static bool edid_block_verify_crc(u8 *edid_block)
{
	u8 sum = 0;
	int i;

	for (i = 0; i < 128; i++)
		sum += edid_block[i];
	return sum == 0;
}

static bool edid_verify_crc(struct v4l2_subdev *sd, u32 segment)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	u32 blocks = state->edid.blocks;
	u8 *data = state->edid.data;

	if (!edid_block_verify_crc(&data[segment * 256]))
		return false;
	if ((segment + 1) * 2 <= blocks)
		return edid_block_verify_crc(&data[segment * 256 + 128]);
	return true;
}

static bool edid_verify_header(struct v4l2_subdev *sd, u32 segment)
{
	static const u8 hdmi_header[] = {
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
	};
	struct adv7511_state *state = get_adv7511_state(sd);
	u8 *data = state->edid.data;

	if (segment != 0)
		return true;
	return !memcmp(data, hdmi_header, sizeof(hdmi_header));
}

static bool adv7511_check_edid_status(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	u8 edidRdy = adv7511_rd(sd, 0xc5);

	v4l2_dbg(1, debug, sd, "%s: edid ready (retries: %d)\n",
			 __func__, EDID_MAX_RETRIES - state->edid.read_retries);

	if (state->edid.complete)
		return true;

	if (edidRdy & MASK_ADV7511_EDID_RDY) {
		int segment = adv7511_rd(sd, 0xc4);
		struct adv7511_edid_detect ed;

		if (segment >= EDID_MAX_SEGM) {
			v4l2_err(sd, "edid segment number too big\n");
			return false;
		}
		v4l2_dbg(1, debug, sd, "%s: got segment %d\n", __func__, segment);
		adv7511_edid_rd(sd, 256, &state->edid.data[segment * 256]);
		adv7511_dbg_dump_edid(2, debug, sd, segment, &state->edid.data[segment * 256]);
		if (segment == 0) {
			state->edid.blocks = state->edid.data[0x7e] + 1;
			v4l2_dbg(1, debug, sd, "%s: %d blocks in total\n", __func__, state->edid.blocks);
		}
		if (!edid_verify_crc(sd, segment) ||
		    !edid_verify_header(sd, segment)) {
			/* edid crc error, force reread of edid segment */
			v4l2_err(sd, "%s: edid crc or header error\n", __func__);
			state->have_monitor = false;
			adv7511_s_power(sd, false);
			adv7511_s_power(sd, true);
			return false;
		}
		/* one more segment read ok */
		state->edid.segments = segment + 1;
		v4l2_ctrl_s_ctrl(state->have_edid0_ctrl, 0x1);
		if (((state->edid.data[0x7e] >> 1) + 1) > state->edid.segments) {
			/* Request next EDID segment */
			v4l2_dbg(1, debug, sd, "%s: request segment %d\n", __func__, state->edid.segments);
			adv7511_wr(sd, 0xc9, 0xf);
			adv7511_wr(sd, 0xc4, state->edid.segments);
			state->edid.read_retries = EDID_MAX_RETRIES;
			queue_delayed_work(state->work_queue, &state->edid_handler, EDID_DELAY);
			return false;
		}

		v4l2_dbg(1, debug, sd, "%s: edid complete with %d segment(s)\n", __func__, state->edid.segments);
		state->edid.complete = true;
		ed.phys_addr = cec_get_edid_phys_addr(state->edid.data,
						      state->edid.segments * 256,
						      NULL);
		/* report when we have all segments
		   but report only for segment 0
		 */
		ed.present = true;
		ed.segment = 0;
		state->edid_detect_counter++;
		cec_s_phys_addr(state->cec_adap, ed.phys_addr, false);
		v4l2_subdev_notify(sd, ADV7511_EDID_DETECT, (void *)&ed);
		return ed.present;
	}

	return false;
}

static int adv7511_registered(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	int err;

	err = cec_register_adapter(state->cec_adap);
	if (err)
		cec_delete_adapter(state->cec_adap);
	return err;
}

static void adv7511_unregistered(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);

	cec_unregister_adapter(state->cec_adap);
}

static const struct v4l2_subdev_internal_ops adv7511_int_ops = {
	.registered = adv7511_registered,
	.unregistered = adv7511_unregistered,
};

/* ----------------------------------------------------------------------- */
/* Setup ADV7511 */
static void adv7511_init_setup(struct v4l2_subdev *sd)
{
	struct adv7511_state *state = get_adv7511_state(sd);
	struct adv7511_state_edid *edid = &state->edid;
	u32 cec_clk = state->pdata.cec_clk;
	u8 ratio;

	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	/* clear all interrupts */
	adv7511_wr(sd, 0x96, 0xff);
	adv7511_wr(sd, 0x97, 0xff);
	/*
	 * Stop HPD from resetting a lot of registers.
	 * It might leave the chip in a partly un-initialized state,
	 * in particular with regards to hotplug bounces.
	 */
	adv7511_wr_and_or(sd, 0xd6, 0x3f, 0xc0);
	memset(edid, 0, sizeof(struct adv7511_state_edid));
	state->have_monitor = false;
	adv7511_set_isr(sd, false);
	adv7511_s_stream(sd, false);
	adv7511_s_audio_stream(sd, false);

	if (state->i2c_cec == NULL)
		return;

	v4l2_dbg(1, debug, sd, "%s: cec_clk %d\n", __func__, cec_clk);

	/* cec soft reset */
	adv7511_cec_write(sd, 0x50, 0x01);
	adv7511_cec_write(sd, 0x50, 0x00);

	/* legacy mode */
	adv7511_cec_write(sd, 0x4a, 0x00);

	if (cec_clk % 750000 != 0)
		v4l2_err(sd, "%s: cec_clk %d, not multiple of 750 Khz\n",
			 __func__, cec_clk);

	ratio = (cec_clk / 750000) - 1;
	adv7511_cec_write(sd, 0x4e, ratio << 2);
}

static int adv7511_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct adv7511_state *state;
	struct adv7511_platform_data *pdata = client->dev.platform_data;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_subdev *sd;
	u8 chip_id[2];
	int err = -EIO;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	state = devm_kzalloc(&client->dev, sizeof(struct adv7511_state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	/* Platform data */
	if (!pdata) {
		v4l_err(client, "No platform data!\n");
		return -ENODEV;
	}
	memcpy(&state->pdata, pdata, sizeof(state->pdata));
	state->fmt_code = MEDIA_BUS_FMT_RGB888_1X24;
	state->colorspace = V4L2_COLORSPACE_SRGB;

	sd = &state->sd;

	v4l2_dbg(1, debug, sd, "detecting adv7511 client on address 0x%x\n",
			 client->addr << 1);

	v4l2_i2c_subdev_init(sd, client, &adv7511_ops);
	sd->internal_ops = &adv7511_int_ops;

	hdl = &state->hdl;
	v4l2_ctrl_handler_init(hdl, 10);
	/* add in ascending ID order */
	state->hdmi_mode_ctrl = v4l2_ctrl_new_std_menu(hdl, &adv7511_ctrl_ops,
			V4L2_CID_DV_TX_MODE, V4L2_DV_TX_MODE_HDMI,
			0, V4L2_DV_TX_MODE_DVI_D);
	state->hotplug_ctrl = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_DV_TX_HOTPLUG, 0, 1, 0, 0);
	state->rx_sense_ctrl = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_DV_TX_RXSENSE, 0, 1, 0, 0);
	state->have_edid0_ctrl = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_DV_TX_EDID_PRESENT, 0, 1, 0, 0);
	state->rgb_quantization_range_ctrl =
		v4l2_ctrl_new_std_menu(hdl, &adv7511_ctrl_ops,
			V4L2_CID_DV_TX_RGB_RANGE, V4L2_DV_RGB_RANGE_FULL,
			0, V4L2_DV_RGB_RANGE_AUTO);
	state->content_type_ctrl =
		v4l2_ctrl_new_std_menu(hdl, &adv7511_ctrl_ops,
			V4L2_CID_DV_TX_IT_CONTENT_TYPE, V4L2_DV_IT_CONTENT_TYPE_NO_ITC,
			0, V4L2_DV_IT_CONTENT_TYPE_NO_ITC);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		err = hdl->error;
		goto err_hdl;
	}
	state->pad.flags = MEDIA_PAD_FL_SINK;
	err = media_entity_pads_init(&sd->entity, 1, &state->pad);
	if (err)
		goto err_hdl;

	/* EDID and CEC i2c addr */
	state->i2c_edid_addr = state->pdata.i2c_edid << 1;
	state->i2c_cec_addr = state->pdata.i2c_cec << 1;
	state->i2c_pktmem_addr = state->pdata.i2c_pktmem << 1;

	state->chip_revision = adv7511_rd(sd, 0x0);
	chip_id[0] = adv7511_rd(sd, 0xf5);
	chip_id[1] = adv7511_rd(sd, 0xf6);
	if (chip_id[0] != 0x75 || chip_id[1] != 0x11) {
		v4l2_err(sd, "chip_id != 0x7511, read 0x%02x%02x\n", chip_id[0],
			 chip_id[1]);
		err = -EIO;
		goto err_entity;
	}

	state->i2c_edid = i2c_new_dummy(client->adapter,
					state->i2c_edid_addr >> 1);
	if (state->i2c_edid == NULL) {
		v4l2_err(sd, "failed to register edid i2c client\n");
		err = -ENOMEM;
		goto err_entity;
	}

	adv7511_wr(sd, 0xe1, state->i2c_cec_addr);
	if (state->pdata.cec_clk < 3000000 ||
	    state->pdata.cec_clk > 100000000) {
		v4l2_err(sd, "%s: cec_clk %u outside range, disabling cec\n",
				__func__, state->pdata.cec_clk);
		state->pdata.cec_clk = 0;
	}

	if (state->pdata.cec_clk) {
		state->i2c_cec = i2c_new_dummy(client->adapter,
					       state->i2c_cec_addr >> 1);
		if (state->i2c_cec == NULL) {
			v4l2_err(sd, "failed to register cec i2c client\n");
			goto err_unreg_edid;
		}
		adv7511_wr(sd, 0xe2, 0x00); /* power up cec section */
	} else {
		adv7511_wr(sd, 0xe2, 0x01); /* power down cec section */
	}

	state->i2c_pktmem = i2c_new_dummy(client->adapter, state->i2c_pktmem_addr >> 1);
	if (state->i2c_pktmem == NULL) {
		v4l2_err(sd, "failed to register pktmem i2c client\n");
		err = -ENOMEM;
		goto err_unreg_cec;
	}

	state->work_queue = create_singlethread_workqueue(sd->name);
	if (state->work_queue == NULL) {
		v4l2_err(sd, "could not create workqueue\n");
		err = -ENOMEM;
		goto err_unreg_pktmem;
	}

	INIT_DELAYED_WORK(&state->edid_handler, adv7511_edid_handler);

	adv7511_init_setup(sd);

#if IS_ENABLED(CONFIG_VIDEO_ADV7511_CEC)
	state->cec_adap = cec_allocate_adapter(&adv7511_cec_adap_ops,
		state, dev_name(&client->dev), CEC_CAP_TRANSMIT |
		CEC_CAP_LOG_ADDRS | CEC_CAP_PASSTHROUGH | CEC_CAP_RC,
		ADV7511_MAX_ADDRS, &client->dev);
	err = PTR_ERR_OR_ZERO(state->cec_adap);
	if (err) {
		destroy_workqueue(state->work_queue);
		goto err_unreg_pktmem;
	}
#endif

	adv7511_set_isr(sd, true);
	adv7511_check_monitor_present_status(sd);

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
			  client->addr << 1, client->adapter->name);
	return 0;

err_unreg_pktmem:
	i2c_unregister_device(state->i2c_pktmem);
err_unreg_cec:
	if (state->i2c_cec)
		i2c_unregister_device(state->i2c_cec);
err_unreg_edid:
	i2c_unregister_device(state->i2c_edid);
err_entity:
	media_entity_cleanup(&sd->entity);
err_hdl:
	v4l2_ctrl_handler_free(&state->hdl);
	return err;
}

/* ----------------------------------------------------------------------- */

static int adv7511_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7511_state *state = get_adv7511_state(sd);

	state->chip_revision = -1;

	v4l2_dbg(1, debug, sd, "%s removed @ 0x%x (%s)\n", client->name,
		 client->addr << 1, client->adapter->name);

	adv7511_set_isr(sd, false);
	adv7511_init_setup(sd);
	cancel_delayed_work(&state->edid_handler);
	i2c_unregister_device(state->i2c_edid);
	if (state->i2c_cec)
		i2c_unregister_device(state->i2c_cec);
	i2c_unregister_device(state->i2c_pktmem);
	destroy_workqueue(state->work_queue);
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_device_id adv7511_id[] = {
	{ "adv7511", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7511_id);

static struct i2c_driver adv7511_driver = {
	.driver = {
		.name = "adv7511",
	},
	.probe = adv7511_probe,
	.remove = adv7511_remove,
	.id_table = adv7511_id,
};

module_i2c_driver(adv7511_driver);
