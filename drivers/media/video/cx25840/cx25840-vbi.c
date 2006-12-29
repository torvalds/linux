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
	struct cx25840_state *state = i2c_get_clientdata(client);
	v4l2_std_id std = cx25840_get_v4lstd(client);
	int hblank,hactive,burst,vblank,vactive,sc,vblank656,src_decimation;
	int luma_lpf,uv_lpf, comb;
	u32 pll_int,pll_frac,pll_post;

	/* datasheet startup, step 8d */
	if (std & ~V4L2_STD_NTSC) {
		cx25840_write(client, 0x49f, 0x11);
	} else {
		cx25840_write(client, 0x49f, 0x14);
	}

	if (std & V4L2_STD_625_50) {
		hblank=0x084;
		hactive=0x2d0;
		burst=0x5d;
		vblank=0x024;
		vactive=0x244;
		vblank656=0x28;
		src_decimation=0x21f;

		luma_lpf=2;
		if (std & V4L2_STD_SECAM) {
			uv_lpf=0;
			comb=0;
			sc=0x0a425f;
		} else if (std == V4L2_STD_PAL_Nc) {
			uv_lpf=1;
			comb=0x20;
			sc=556453;
		} else {
			uv_lpf=1;
			comb=0x20;
			sc=0x0a8263;
		}
	} else {
		hactive=720;
		hblank=122;
		vactive=487;
		luma_lpf=1;
		uv_lpf=1;

		src_decimation=0x21f;
		if (std == V4L2_STD_PAL_60) {
			vblank=26;
			vblank656=26;
			burst=0x5b;
			luma_lpf=2;
			comb=0x20;
			sc=0x0a8263;
		} else if (std == V4L2_STD_PAL_M) {
			vblank=20;
			vblank656=24;
			burst=0x61;
			comb=0x20;

			sc=555452;
		} else {
			vblank=26;
			vblank656=26;
			burst=0x5b;
			comb=0x66;
			sc=556063;
		}
	}

	/* DEBUG: Displays configured PLL frequency */
	pll_int=cx25840_read(client, 0x108);
	pll_frac=cx25840_read4(client, 0x10c)&0x1ffffff;
	pll_post=cx25840_read(client, 0x109);
	v4l_dbg(1, cx25840_debug, client,
				"PLL regs = int: %u, frac: %u, post: %u\n",
				pll_int,pll_frac,pll_post);

	if (pll_post) {
		int fin, fsc;
		int pll= (28636363L*((((u64)pll_int)<<25L)+pll_frac)) >>25L;

		pll/=pll_post;
		v4l_dbg(1, cx25840_debug, client, "PLL = %d.%06d MHz\n",
						pll/1000000, pll%1000000);
		v4l_dbg(1, cx25840_debug, client, "PLL/8 = %d.%06d MHz\n",
						pll/8000000, (pll/8)%1000000);

		fin=((u64)src_decimation*pll)>>12;
		v4l_dbg(1, cx25840_debug, client, "ADC Sampling freq = "
						"%d.%06d MHz\n",
						fin/1000000,fin%1000000);

		fsc= (((u64)sc)*pll) >> 24L;
		v4l_dbg(1, cx25840_debug, client, "Chroma sub-carrier freq = "
						"%d.%06d MHz\n",
						fsc/1000000,fsc%1000000);

		v4l_dbg(1, cx25840_debug, client, "hblank %i, hactive %i, "
			"vblank %i , vactive %i, vblank656 %i, src_dec %i,"
			"burst 0x%02x, luma_lpf %i, uv_lpf %i, comb 0x%02x,"
			" sc 0x%06x\n",
			hblank, hactive, vblank, vactive, vblank656,
			src_decimation, burst, luma_lpf, uv_lpf, comb, sc);
	}

	/* Sets horizontal blanking delay and active lines */
	cx25840_write(client, 0x470, hblank);
	cx25840_write(client, 0x471, 0xff&(((hblank>>8)&0x3)|(hactive <<4)));
	cx25840_write(client, 0x472, hactive>>4);

	/* Sets burst gate delay */
	cx25840_write(client, 0x473, burst);

	/* Sets vertical blanking delay and active duration */
	cx25840_write(client, 0x474, vblank);
	cx25840_write(client, 0x475, 0xff&(((vblank>>8)&0x3)|(vactive <<4)));
	cx25840_write(client, 0x476, vactive>>4);
	cx25840_write(client, 0x477, vblank656);

	/* Sets src decimation rate */
	cx25840_write(client, 0x478, 0xff&src_decimation);
	cx25840_write(client, 0x479, 0xff&(src_decimation>>8));

	/* Sets Luma and UV Low pass filters */
	cx25840_write(client, 0x47a, luma_lpf<<6|((uv_lpf<<4)&0x30));

	/* Enables comb filters */
	cx25840_write(client, 0x47b, comb);

	/* Sets SC Step*/
	cx25840_write(client, 0x47c, sc);
	cx25840_write(client, 0x47d, 0xff&sc>>8);
	cx25840_write(client, 0x47e, 0xff&sc>>16);

	/* Sets VBI parameters */
	if (std & V4L2_STD_625_50) {
		cx25840_write(client, 0x47f, 0x01);
		state->vbi_line_offset = 5;
	} else {
		cx25840_write(client, 0x47f, 0x00);
		state->vbi_line_offset = 8;
	}
}

int cx25840_vbi(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct cx25840_state *state = i2c_get_clientdata(client);
	struct v4l2_format *fmt;
	struct v4l2_sliced_vbi_format *svbi;

	switch (cmd) {
	case VIDIOC_G_FMT:
	{
		static u16 lcr2vbi[] = {
			0, V4L2_SLICED_TELETEXT_B, 0,	/* 1 */
			0, V4L2_SLICED_WSS_625, 0,	/* 4 */
			V4L2_SLICED_CAPTION_525,	/* 6 */
			0, 0, V4L2_SLICED_VPS, 0, 0,	/* 9 */
			0, 0, 0, 0
		};
		int is_pal = !(cx25840_get_v4lstd(client) & V4L2_STD_525_60);
		int i;

		fmt = arg;
		if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
			return -EINVAL;
		svbi = &fmt->fmt.sliced;
		memset(svbi, 0, sizeof(*svbi));
		/* we're done if raw VBI is active */
		if ((cx25840_read(client, 0x404) & 0x10) == 0)
			break;

		if (is_pal) {
			for (i = 7; i <= 23; i++) {
				u8 v = cx25840_read(client, 0x424 + i - 7);

				svbi->service_lines[0][i] = lcr2vbi[v >> 4];
				svbi->service_lines[1][i] = lcr2vbi[v & 0xf];
				svbi->service_set |=
					svbi->service_lines[0][i] | svbi->service_lines[1][i];
			}
		}
		else {
			for (i = 10; i <= 21; i++) {
				u8 v = cx25840_read(client, 0x424 + i - 10);

				svbi->service_lines[0][i] = lcr2vbi[v >> 4];
				svbi->service_lines[1][i] = lcr2vbi[v & 0xf];
				svbi->service_set |=
					svbi->service_lines[0][i] | svbi->service_lines[1][i];
			}
		}
		break;
	}

	case VIDIOC_S_FMT:
	{
		int is_pal = !(cx25840_get_v4lstd(client) & V4L2_STD_525_60);
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
		cx25840_write(client, 0x404, 0x32);	/* Ancillary data */
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
			for (x = 1, i = 0x424; i <= 0x434; i++, x++) {
				cx25840_write(client, i, lcr[6 + x]);
			}
		}
		else {
			for (x = 1, i = 0x424; i <= 0x430; i++, x++) {
				cx25840_write(client, i, lcr[9 + x]);
			}
			for (i = 0x431; i <= 0x434; i++) {
				cx25840_write(client, i, 0);
			}
		}

		cx25840_write(client, 0x43c, 0x16);

		if (is_pal) {
			cx25840_write(client, 0x474, 0x2a);
		} else {
			cx25840_write(client, 0x474, 0x22);
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
