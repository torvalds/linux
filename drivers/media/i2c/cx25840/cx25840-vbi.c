// SPDX-License-Identifier: GPL-2.0-or-later
/* cx25840 VBI functions
 */


#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <media/v4l2-common.h>
#include <media/drv-intf/cx25840.h>

#include "cx25840-core.h"

static int odd_parity(u8 c)
{
	c ^= (c >> 4);
	c ^= (c >> 2);
	c ^= (c >> 1);

	return c & 1;
}

static int decode_vps(u8 * dst, u8 * p)
{
	static const u8 biphase_tbl[] = {
		0xf0, 0x78, 0x70, 0xf0, 0xb4, 0x3c, 0x34, 0xb4,
		0xb0, 0x38, 0x30, 0xb0, 0xf0, 0x78, 0x70, 0xf0,
		0xd2, 0x5a, 0x52, 0xd2, 0x96, 0x1e, 0x16, 0x96,
		0x92, 0x1a, 0x12, 0x92, 0xd2, 0x5a, 0x52, 0xd2,
		0xd0, 0x58, 0x50, 0xd0, 0x94, 0x1c, 0x14, 0x94,
		0x90, 0x18, 0x10, 0x90, 0xd0, 0x58, 0x50, 0xd0,
		0xf0, 0x78, 0x70, 0xf0, 0xb4, 0x3c, 0x34, 0xb4,
		0xb0, 0x38, 0x30, 0xb0, 0xf0, 0x78, 0x70, 0xf0,
		0xe1, 0x69, 0x61, 0xe1, 0xa5, 0x2d, 0x25, 0xa5,
		0xa1, 0x29, 0x21, 0xa1, 0xe1, 0x69, 0x61, 0xe1,
		0xc3, 0x4b, 0x43, 0xc3, 0x87, 0x0f, 0x07, 0x87,
		0x83, 0x0b, 0x03, 0x83, 0xc3, 0x4b, 0x43, 0xc3,
		0xc1, 0x49, 0x41, 0xc1, 0x85, 0x0d, 0x05, 0x85,
		0x81, 0x09, 0x01, 0x81, 0xc1, 0x49, 0x41, 0xc1,
		0xe1, 0x69, 0x61, 0xe1, 0xa5, 0x2d, 0x25, 0xa5,
		0xa1, 0x29, 0x21, 0xa1, 0xe1, 0x69, 0x61, 0xe1,
		0xe0, 0x68, 0x60, 0xe0, 0xa4, 0x2c, 0x24, 0xa4,
		0xa0, 0x28, 0x20, 0xa0, 0xe0, 0x68, 0x60, 0xe0,
		0xc2, 0x4a, 0x42, 0xc2, 0x86, 0x0e, 0x06, 0x86,
		0x82, 0x0a, 0x02, 0x82, 0xc2, 0x4a, 0x42, 0xc2,
		0xc0, 0x48, 0x40, 0xc0, 0x84, 0x0c, 0x04, 0x84,
		0x80, 0x08, 0x00, 0x80, 0xc0, 0x48, 0x40, 0xc0,
		0xe0, 0x68, 0x60, 0xe0, 0xa4, 0x2c, 0x24, 0xa4,
		0xa0, 0x28, 0x20, 0xa0, 0xe0, 0x68, 0x60, 0xe0,
		0xf0, 0x78, 0x70, 0xf0, 0xb4, 0x3c, 0x34, 0xb4,
		0xb0, 0x38, 0x30, 0xb0, 0xf0, 0x78, 0x70, 0xf0,
		0xd2, 0x5a, 0x52, 0xd2, 0x96, 0x1e, 0x16, 0x96,
		0x92, 0x1a, 0x12, 0x92, 0xd2, 0x5a, 0x52, 0xd2,
		0xd0, 0x58, 0x50, 0xd0, 0x94, 0x1c, 0x14, 0x94,
		0x90, 0x18, 0x10, 0x90, 0xd0, 0x58, 0x50, 0xd0,
		0xf0, 0x78, 0x70, 0xf0, 0xb4, 0x3c, 0x34, 0xb4,
		0xb0, 0x38, 0x30, 0xb0, 0xf0, 0x78, 0x70, 0xf0,
	};

	u8 c, err = 0;
	int i;

	for (i = 0; i < 2 * 13; i += 2) {
		err |= biphase_tbl[p[i]] | biphase_tbl[p[i + 1]];
		c = (biphase_tbl[p[i + 1]] & 0xf) |
		    ((biphase_tbl[p[i]] & 0xf) << 4);
		dst[i / 2] = c;
	}

	return err & 0xf0;
}

int cx25840_g_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *svbi)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cx25840_state *state = to_state(sd);
	static const u16 lcr2vbi[] = {
		0, V4L2_SLICED_TELETEXT_B, 0,	/* 1 */
		0, V4L2_SLICED_WSS_625, 0,	/* 4 */
		V4L2_SLICED_CAPTION_525,	/* 6 */
		0, 0, V4L2_SLICED_VPS, 0, 0,	/* 9 */
		0, 0, 0, 0
	};
	int is_pal = !(state->std & V4L2_STD_525_60);
	int i;

	memset(svbi->service_lines, 0, sizeof(svbi->service_lines));
	svbi->service_set = 0;
	/* we're done if raw VBI is active */
	if ((cx25840_read(client, 0x404) & 0x10) == 0)
		return 0;

	if (is_pal) {
		for (i = 7; i <= 23; i++) {
			u8 v = cx25840_read(client,
				 state->vbi_regs_offset + 0x424 + i - 7);

			svbi->service_lines[0][i] = lcr2vbi[v >> 4];
			svbi->service_lines[1][i] = lcr2vbi[v & 0xf];
			svbi->service_set |= svbi->service_lines[0][i] |
					     svbi->service_lines[1][i];
		}
	} else {
		for (i = 10; i <= 21; i++) {
			u8 v = cx25840_read(client,
				state->vbi_regs_offset + 0x424 + i - 10);

			svbi->service_lines[0][i] = lcr2vbi[v >> 4];
			svbi->service_lines[1][i] = lcr2vbi[v & 0xf];
			svbi->service_set |= svbi->service_lines[0][i] |
					     svbi->service_lines[1][i];
		}
	}
	return 0;
}

int cx25840_s_raw_fmt(struct v4l2_subdev *sd, struct v4l2_vbi_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cx25840_state *state = to_state(sd);
	int is_pal = !(state->std & V4L2_STD_525_60);
	int vbi_offset = is_pal ? 1 : 0;

	/* Setup standard */
	cx25840_std_setup(client);

	/* VBI Offset */
	if (is_cx23888(state))
		cx25840_write(client, 0x54f, vbi_offset);
	else
		cx25840_write(client, 0x47f, vbi_offset);
	cx25840_write(client, 0x404, 0x2e);
	return 0;
}

int cx25840_s_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *svbi)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cx25840_state *state = to_state(sd);
	int is_pal = !(state->std & V4L2_STD_525_60);
	int vbi_offset = is_pal ? 1 : 0;
	int i, x;
	u8 lcr[24];

	for (x = 0; x <= 23; x++)
		lcr[x] = 0x00;

	/* Setup standard */
	cx25840_std_setup(client);

	/* Sliced VBI */
	cx25840_write(client, 0x404, 0x32);	/* Ancillary data */
	cx25840_write(client, 0x406, 0x13);
	if (is_cx23888(state))
		cx25840_write(client, 0x54f, vbi_offset);
	else
		cx25840_write(client, 0x47f, vbi_offset);

	if (is_pal) {
		for (i = 0; i <= 6; i++)
			svbi->service_lines[0][i] =
				svbi->service_lines[1][i] = 0;
	} else {
		for (i = 0; i <= 9; i++)
			svbi->service_lines[0][i] =
				svbi->service_lines[1][i] = 0;

		for (i = 22; i <= 23; i++)
			svbi->service_lines[0][i] =
				svbi->service_lines[1][i] = 0;
	}

	for (i = 7; i <= 23; i++) {
		for (x = 0; x <= 1; x++) {
			switch (svbi->service_lines[1-x][i]) {
			case V4L2_SLICED_TELETEXT_B:
				lcr[i] |= 1 << (4 * x);
				break;
			case V4L2_SLICED_WSS_625:
				lcr[i] |= 4 << (4 * x);
				break;
			case V4L2_SLICED_CAPTION_525:
				lcr[i] |= 6 << (4 * x);
				break;
			case V4L2_SLICED_VPS:
				lcr[i] |= 9 << (4 * x);
				break;
			}
		}
	}

	if (is_pal) {
		for (x = 1, i = state->vbi_regs_offset + 0x424;
		     i <= state->vbi_regs_offset + 0x434; i++, x++)
			cx25840_write(client, i, lcr[6 + x]);
	} else {
		for (x = 1, i = state->vbi_regs_offset + 0x424;
		     i <= state->vbi_regs_offset + 0x430; i++, x++)
			cx25840_write(client, i, lcr[9 + x]);
		for (i = state->vbi_regs_offset + 0x431;
		     i <= state->vbi_regs_offset + 0x434; i++)
			cx25840_write(client, i, 0);
	}

	cx25840_write(client, state->vbi_regs_offset + 0x43c, 0x16);
	if (is_cx23888(state))
		cx25840_write(client, 0x428, is_pal ? 0x2a : 0x22);
	else
		cx25840_write(client, 0x474, is_pal ? 0x2a : 0x22);
	return 0;
}

int cx25840_decode_vbi_line(struct v4l2_subdev *sd, struct v4l2_decode_vbi_line *vbi)
{
	struct cx25840_state *state = to_state(sd);
	u8 *p = vbi->p;
	int id1, id2, l, err = 0;

	if (p[0] || p[1] != 0xff || p[2] != 0xff ||
			(p[3] != 0x55 && p[3] != 0x91)) {
		vbi->line = vbi->type = 0;
		return 0;
	}

	p += 4;
	id1 = p[-1];
	id2 = p[0] & 0xf;
	l = p[2] & 0x3f;
	l += state->vbi_line_offset;
	p += 4;

	switch (id2) {
	case 1:
		id2 = V4L2_SLICED_TELETEXT_B;
		break;
	case 4:
		id2 = V4L2_SLICED_WSS_625;
		break;
	case 6:
		id2 = V4L2_SLICED_CAPTION_525;
		err = !odd_parity(p[0]) || !odd_parity(p[1]);
		break;
	case 9:
		id2 = V4L2_SLICED_VPS;
		if (decode_vps(p, p) != 0)
			err = 1;
		break;
	default:
		id2 = 0;
		err = 1;
		break;
	}

	vbi->type = err ? 0 : id2;
	vbi->line = err ? 0 : l;
	vbi->is_second_field = err ? 0 : (id1 == 0x55);
	vbi->p = p;
	return 0;
}
