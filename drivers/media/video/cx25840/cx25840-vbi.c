/* cx25840 VBI functions
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


#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <media/v4l2-common.h>
#include <media/cx25840.h>

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

void cx25840_vbi_setup(struct i2c_client *client)
{
	v4l2_std_id std = cx25840_get_v4lstd(client);

	if (std & ~V4L2_STD_NTSC) {
		/* datasheet startup, step 8d */
		cx25840_write(client, 0x49f, 0x11);

		cx25840_write(client, 0x470, 0x84);
		cx25840_write(client, 0x471, 0x00);
		cx25840_write(client, 0x472, 0x2d);
		cx25840_write(client, 0x473, 0x5d);

		cx25840_write(client, 0x474, 0x24);
		cx25840_write(client, 0x475, 0x40);
		cx25840_write(client, 0x476, 0x24);
		cx25840_write(client, 0x477, 0x28);

		cx25840_write(client, 0x478, 0x1f);
		cx25840_write(client, 0x479, 0x02);

		if (std & V4L2_STD_SECAM) {
			cx25840_write(client, 0x47a, 0x80);
			cx25840_write(client, 0x47b, 0x00);
			cx25840_write(client, 0x47c, 0x5f);
			cx25840_write(client, 0x47d, 0x42);
		} else {
			cx25840_write(client, 0x47a, 0x90);
			cx25840_write(client, 0x47b, 0x20);
			cx25840_write(client, 0x47c, 0x63);
			cx25840_write(client, 0x47d, 0x82);
		}

		cx25840_write(client, 0x47e, 0x0a);
		cx25840_write(client, 0x47f, 0x01);
	} else {
		/* datasheet startup, step 8d */
		cx25840_write(client, 0x49f, 0x14);

		cx25840_write(client, 0x470, 0x7a);
		cx25840_write(client, 0x471, 0x00);
		cx25840_write(client, 0x472, 0x2d);
		cx25840_write(client, 0x473, 0x5b);

		cx25840_write(client, 0x474, 0x1a);
		cx25840_write(client, 0x475, 0x70);
		cx25840_write(client, 0x476, 0x1e);
		cx25840_write(client, 0x477, 0x1e);

		cx25840_write(client, 0x478, 0x1f);
		cx25840_write(client, 0x479, 0x02);
		cx25840_write(client, 0x47a, 0x50);
		cx25840_write(client, 0x47b, 0x66);

		cx25840_write(client, 0x47c, 0x1f);
		cx25840_write(client, 0x47d, 0x7c);
		cx25840_write(client, 0x47e, 0x08);
		cx25840_write(client, 0x47f, 0x00);
	}
}

int cx25840_vbi(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct v4l2_format *fmt;
	struct v4l2_sliced_vbi_format *svbi;

	switch (cmd) {
	case VIDIOC_G_FMT:
	{
		static u16 lcr2vbi[] = {
			0, V4L2_SLICED_TELETEXT_PAL_B, 0,	/* 1 */
			0, V4L2_SLICED_WSS_625, 0,	/* 4 */
			V4L2_SLICED_CAPTION_525,	/* 6 */
			0, 0, V4L2_SLICED_VPS, 0, 0,	/* 9 */
			0, 0, 0, 0
		};
		int i;

		fmt = arg;
		if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
			return -EINVAL;
		svbi = &fmt->fmt.sliced;
		memset(svbi, 0, sizeof(*svbi));
		/* we're done if raw VBI is active */
		if ((cx25840_read(client, 0x404) & 0x10) == 0)
			break;

		for (i = 7; i <= 23; i++) {
			u8 v = cx25840_read(client, 0x424 + i - 7);

			svbi->service_lines[0][i] = lcr2vbi[v >> 4];
			svbi->service_lines[1][i] = lcr2vbi[v & 0xf];
			svbi->service_set |=
				 svbi->service_lines[0][i] | svbi->service_lines[1][i];
		}
		break;
	}

	case VIDIOC_S_FMT:
	{
		int is_pal = !(cx25840_get_v4lstd(client) & V4L2_STD_NTSC);
		int vbi_offset = is_pal ? 1 : 0;
		int i, x;
		u8 lcr[24];

		fmt = arg;
		if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
			return -EINVAL;
		svbi = &fmt->fmt.sliced;
		if (svbi->service_set == 0) {
			/* raw VBI */
			memset(svbi, 0, sizeof(*svbi));

			/* Setup VBI */
			cx25840_vbi_setup(client);

			/* VBI Offset */
			cx25840_write(client, 0x47f, vbi_offset);
			cx25840_write(client, 0x404, 0x2e);
			break;
		}

		for (x = 0; x <= 23; x++)
			lcr[x] = 0x00;

		/* Setup VBI */
		cx25840_vbi_setup(client);

		/* Sliced VBI */
		cx25840_write(client, 0x404, 0x36);	/* Ancillery data */
		cx25840_write(client, 0x406, 0x13);
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
				case V4L2_SLICED_TELETEXT_PAL_B:
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

		for (x = 1, i = 0x424; i <= 0x434; i++, x++) {
			cx25840_write(client, i, lcr[6 + x]);
		}

		cx25840_write(client, 0x43c, 0x16);

		if (is_pal) {
			cx25840_write(client, 0x474, 0x2a);
		} else {
			cx25840_write(client, 0x474, 0x1a + 6);
		}
		break;
	}

	case VIDIOC_INT_DECODE_VBI_LINE:
	{
		struct v4l2_decode_vbi_line *vbi = arg;
		u8 *p = vbi->p;
		int id1, id2, l, err = 0;

		if (p[0] || p[1] != 0xff || p[2] != 0xff ||
		    (p[3] != 0x55 && p[3] != 0x91)) {
			vbi->line = vbi->type = 0;
			break;
		}

		p += 4;
		id1 = p[-1];
		id2 = p[0] & 0xf;
		l = p[2] & 0x3f;
		l += 5;
		p += 4;

		switch (id2) {
		case 1:
			id2 = V4L2_SLICED_TELETEXT_PAL_B;
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
			if (decode_vps(p, p) != 0) {
				err = 1;
			}
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
		break;
	}
	}

	return 0;
}
