/* cx25840 internal API header
 *
 * Copyright (C) 2003-2004 Chris Kennedy
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _CX25840_CORE_H_
#define _CX25840_CORE_H_


#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/i2c.h>

struct cx25840_ir_state;

enum cx25840_model {
	CX23885_AV,
	CX23887_AV,
	CX23888_AV,
	CX2310X_AV,
	CX25840,
	CX25841,
	CX25842,
	CX25843,
	CX25836,
	CX25837,
};

enum cx25840_media_pads {
	CX25840_PAD_INPUT,
	CX25840_PAD_VID_OUT,
	CX25840_PAD_VBI_OUT,

	CX25840_NUM_PADS
};

struct cx25840_state {
	struct i2c_client *c;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct {
		/* volume cluster */
		struct v4l2_ctrl *volume;
		struct v4l2_ctrl *mute;
	};
	int pvr150_workaround;
	int radio;
	v4l2_std_id std;
	enum cx25840_video_input vid_input;
	enum cx25840_audio_input aud_input;
	u32 audclk_freq;
	int audmode;
	int vbi_line_offset;
	enum cx25840_model id;
	u32 rev;
	int is_initialized;
	wait_queue_head_t fw_wait;    /* wake up when the fw load is finished */
	struct work_struct fw_work;   /* work entry for fw load */
	struct cx25840_ir_state *ir_state;
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_pad	pads[CX25840_NUM_PADS];
#endif
};

static inline struct cx25840_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct cx25840_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct cx25840_state, hdl)->sd;
}

static inline bool is_cx2583x(struct cx25840_state *state)
{
	return state->id == CX25836 ||
	       state->id == CX25837;
}

static inline bool is_cx231xx(struct cx25840_state *state)
{
	return state->id == CX2310X_AV;
}

static inline bool is_cx2388x(struct cx25840_state *state)
{
	return state->id == CX23885_AV ||
	       state->id == CX23887_AV ||
	       state->id == CX23888_AV;
}

static inline bool is_cx23885(struct cx25840_state *state)
{
	return state->id == CX23885_AV;
}

static inline bool is_cx23887(struct cx25840_state *state)
{
	return state->id == CX23887_AV;
}

static inline bool is_cx23888(struct cx25840_state *state)
{
	return state->id == CX23888_AV;
}

/* ----------------------------------------------------------------------- */
/* cx25850-core.c 							   */
int cx25840_write(struct i2c_client *client, u16 addr, u8 value);
int cx25840_write4(struct i2c_client *client, u16 addr, u32 value);
u8 cx25840_read(struct i2c_client *client, u16 addr);
u32 cx25840_read4(struct i2c_client *client, u16 addr);
int cx25840_and_or(struct i2c_client *client, u16 addr, unsigned mask, u8 value);
int cx25840_and_or4(struct i2c_client *client, u16 addr, u32 and_mask,
		    u32 or_value);
void cx25840_std_setup(struct i2c_client *client);

/* ----------------------------------------------------------------------- */
/* cx25850-firmware.c                                                      */
int cx25840_loadfw(struct i2c_client *client);

/* ----------------------------------------------------------------------- */
/* cx25850-audio.c                                                         */
void cx25840_audio_set_path(struct i2c_client *client);
int cx25840_s_clock_freq(struct v4l2_subdev *sd, u32 freq);

extern const struct v4l2_ctrl_ops cx25840_audio_ctrl_ops;

/* ----------------------------------------------------------------------- */
/* cx25850-vbi.c                                                           */
int cx25840_s_raw_fmt(struct v4l2_subdev *sd, struct v4l2_vbi_format *fmt);
int cx25840_s_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *fmt);
int cx25840_g_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *fmt);
int cx25840_decode_vbi_line(struct v4l2_subdev *sd, struct v4l2_decode_vbi_line *vbi);

/* ----------------------------------------------------------------------- */
/* cx25850-ir.c                                                            */
extern const struct v4l2_subdev_ir_ops cx25840_ir_ops;
int cx25840_ir_log_status(struct v4l2_subdev *sd);
int cx25840_ir_irq_handler(struct v4l2_subdev *sd, u32 status, bool *handled);
int cx25840_ir_probe(struct v4l2_subdev *sd);
int cx25840_ir_remove(struct v4l2_subdev *sd);

#endif
