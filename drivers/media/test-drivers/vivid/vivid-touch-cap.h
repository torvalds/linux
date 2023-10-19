/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-touch-cap.h - touch support functions.
 */
#ifndef _VIVID_TOUCH_CAP_H_
#define _VIVID_TOUCH_CAP_H_

#define VIVID_TCH_HEIGHT	12
#define VIVID_TCH_WIDTH		21
#define VIVID_MIN_PRESSURE	180
#define VIVID_PRESSURE_LIMIT	40
#define TCH_SEQ_COUNT		16
#define TCH_PATTERN_COUNT	12

enum vivid_tch_test {
	SINGLE_TAP,
	DOUBLE_TAP,
	TRIPLE_TAP,
	MOVE_LEFT_TO_RIGHT,
	ZOOM_IN,
	ZOOM_OUT,
	PALM_PRESS,
	MULTIPLE_PRESS,
	TEST_CASE_MAX
};

extern const struct vb2_ops vivid_touch_cap_qops;

int vivid_enum_fmt_tch(struct file *file, void  *priv, struct v4l2_fmtdesc *f);
int vivid_g_fmt_tch(struct file *file, void *priv, struct v4l2_format *f);
int vivid_g_fmt_tch_mplane(struct file *file, void *priv, struct v4l2_format *f);
int vivid_enum_input_tch(struct file *file, void *priv, struct v4l2_input *inp);
int vivid_g_input_tch(struct file *file, void *priv, unsigned int *i);
int vivid_s_input_tch(struct file *file, void *priv, unsigned int i);
void vivid_fillbuff_tch(struct vivid_dev *dev, struct vivid_buffer *buf);
int vivid_set_touch(struct vivid_dev *dev, unsigned int i);
int vivid_g_parm_tch(struct file *file, void *priv,
		     struct v4l2_streamparm *parm);
#endif
