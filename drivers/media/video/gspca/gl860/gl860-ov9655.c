/* @file gl860-ov9655.c
 * @author Olivier LORIN, from logs done by Simon (Sur3) and Almighurt
 * on dsd's weblog
 * @date 2009-08-27
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Sensor : OV9655 */

#include "gl860.h"

static struct validx tbl_init_at_startup[] = {
	{0x0000, 0x0000}, {0x0010, 0x0010}, {0x0008, 0x00c0}, {0x0001, 0x00c1},
	{0x0001, 0x00c2}, {0x0020, 0x0006}, {0x006a, 0x000d},

	{0x0040, 0x0000},
};

static struct validx tbl_commmon[] = {
	{0x0041, 0x0000}, {0x006a, 0x0007}, {0x0063, 0x0006}, {0x006a, 0x000d},
	{0x0000, 0x00c0}, {0x0010, 0x0010}, {0x0001, 0x00c1}, {0x0041, 0x00c2},
	{0x0004, 0x00d8}, {0x0012, 0x0004}, {0x0000, 0x0058}, {0x0040, 0x0000},
	{0x00f3, 0x0006}, {0x0058, 0x0000}, {0x0048, 0x0000}, {0x0061, 0x0000},
};

static s32 tbl_length[] = {12, 56, 52, 54, 56, 42, 32, 12};

static u8 *tbl_640[] = {
	"\x00\x40\x07\x6a\x06\xf3\x0d\x6a" "\x10\x10\xc1\x01"
	,
	"\x12\x80\x00\x00\x01\x98\x02\x80" "\x03\x12\x04\x03\x0b\x57\x0e\x61"
	"\x0f\x42\x11\x01\x12\x60\x13\x00" "\x14\x3a\x16\x24\x17\x14\x18\x00"
	"\x19\x01\x1a\x3d\x1e\x04\x24\x3c" "\x25\x36\x26\x72\x27\x08\x28\x08"
	"\x29\x15\x2a\x00\x2b\x00\x2c\x08"
	,
	"\x32\xff\x33\x00\x34\x3d\x35\x00" "\x36\xfa\x38\x72\x39\x57\x3a\x00"
	"\x3b\x0c\x3d\x99\x3e\x0c\x3f\xc1" "\x40\xc0\x41\x00\x42\xc0\x43\x0a"
	"\x44\xf0\x45\x46\x46\x62\x47\x2a" "\x48\x3c\x4a\xee\x4b\xe7\x4c\xe7"
	"\x4d\xe7\x4e\xe7"
	,
	"\x4f\x98\x50\x98\x51\x00\x52\x28" "\x53\x70\x54\x98\x58\x1a\x59\x85"
	"\x5a\xa9\x5b\x64\x5c\x84\x5d\x53" "\x5e\x0e\x5f\xf0\x60\xf0\x61\xf0"
	"\x62\x00\x63\x00\x64\x02\x65\x20" "\x66\x00\x69\x0a\x6b\x5a\x6c\x04"
	"\x6d\x55\x6e\x00\x6f\x9d"
	,
	"\x70\x15\x71\x78\x72\x00\x73\x00" "\x74\x3a\x75\x35\x76\x01\x77\x02"
	"\x7a\x24\x7b\x04\x7c\x07\x7d\x10" "\x7e\x28\x7f\x36\x80\x44\x81\x52"
	"\x82\x60\x83\x6c\x84\x78\x85\x8c" "\x86\x9e\x87\xbb\x88\xd2\x89\xe5"
	"\x8a\x23\x8c\x8d\x90\x7c\x91\x7b"
	,
	"\x9d\x02\x9e\x02\x9f\x74\xa0\x73" "\xa1\x40\xa4\x50\xa5\x68\xa6\x70"
	"\xa8\xc1\xa9\xef\xaa\x92\xab\x04" "\xac\x80\xad\x80\xae\x80\xaf\x80"
	"\xb2\xf2\xb3\x20\xb4\x20\xb5\x00" "\xb6\xaf"
	,
	"\xbb\xae\xbc\x4f\xbd\x4e\xbe\x6a" "\xbf\x68\xc0\xaa\xc1\xc0\xc2\x01"
	"\xc3\x4e\xc6\x85\xc7\x81\xc9\xe0" "\xca\xe8\xcb\xf0\xcc\xd8\xcd\x93"
	,
	"\xd0\x01\xd1\x08\xd2\xe0\xd3\x01" "\xd4\x10\xd5\x80"
};

static u8 *tbl_800[] = {
	"\x00\x40\x07\x6a\x06\xf3\x0d\x6a" "\x10\x10\xc1\x01"
	,
	"\x12\x80\x00\x00\x01\x98\x02\x80" "\x03\x12\x04\x01\x0b\x57\x0e\x61"
	"\x0f\x42\x11\x00\x12\x00\x13\x00" "\x14\x3a\x16\x24\x17\x1b\x18\xbb"
	"\x19\x01\x1a\x81\x1e\x04\x24\x3c" "\x25\x36\x26\x72\x27\x08\x28\x08"
	"\x29\x15\x2a\x00\x2b\x00\x2c\x08"
	,
	"\x32\xa4\x33\x00\x34\x3d\x35\x00" "\x36\xf8\x38\x72\x39\x57\x3a\x00"
	"\x3b\x0c\x3d\x99\x3e\x0c\x3f\xc2" "\x40\xc0\x41\x00\x42\xc0\x43\x0a"
	"\x44\xf0\x45\x46\x46\x62\x47\x2a" "\x48\x3c\x4a\xec\x4b\xe8\x4c\xe8"
	"\x4d\xe8\x4e\xe8"
	,
	"\x4f\x98\x50\x98\x51\x00\x52\x28" "\x53\x70\x54\x98\x58\x1a\x59\x85"
	"\x5a\xa9\x5b\x64\x5c\x84\x5d\x53" "\x5e\x0e\x5f\xf0\x60\xf0\x61\xf0"
	"\x62\x00\x63\x00\x64\x02\x65\x20" "\x66\x00\x69\x02\x6b\x5a\x6c\x04"
	"\x6d\x55\x6e\x00\x6f\x9d"
	,
	"\x70\x08\x71\x78\x72\x00\x73\x01" "\x74\x3a\x75\x35\x76\x01\x77\x02"
	"\x7a\x24\x7b\x04\x7c\x07\x7d\x10" "\x7e\x28\x7f\x36\x80\x44\x81\x52"
	"\x82\x60\x83\x6c\x84\x78\x85\x8c" "\x86\x9e\x87\xbb\x88\xd2\x89\xe5"
	"\x8a\x23\x8c\x0d\x90\x90\x91\x90"
	,
	"\x9d\x02\x9e\x02\x9f\x94\xa0\x94" "\xa1\x01\xa4\x50\xa5\x68\xa6\x70"
	"\xa8\xc1\xa9\xef\xaa\x92\xab\x04" "\xac\x80\xad\x80\xae\x80\xaf\x80"
	"\xb2\xf2\xb3\x20\xb4\x20\xb5\x00" "\xb6\xaf"
	,
	"\xbb\xae\xbc\x38\xbd\x39\xbe\x01" "\xbf\x01\xc0\xe2\xc1\xc0\xc2\x01"
	"\xc3\x4e\xc6\x85\xc7\x81\xc9\xe0" "\xca\xe8\xcb\xf0\xcc\xd8\xcd\x93"
	,
	"\xd0\x21\xd1\x18\xd2\xe0\xd3\x01" "\xd4\x28\xd5\x00"
};

static u8 c04[] = {0x04};
static u8 dat_post_1[] = "\x04\x00\x10\x20\xa1\x00\x00\x02";
static u8 dat_post_2[] = "\x10\x10\xc1\x02";
static u8 dat_post_3[] = "\x04\x00\x10\x7c\xa1\x00\x00\x04";
static u8 dat_post_4[] = "\x10\x02\xc1\x06";
static u8 dat_post_5[] = "\x04\x00\x10\x7b\xa1\x00\x00\x08";
static u8 dat_post_6[] = "\x10\x10\xc1\x05";
static u8 dat_post_7[] = "\x04\x00\x10\x7c\xa1\x00\x00\x08";
static u8 dat_post_8[] = "\x04\x00\x10\x7c\xa1\x00\x00\x09";

static struct validx tbl_init_post_alt[] = {
	{0x6032, 0x00ff}, {0x6032, 0x00ff}, {0x6032, 0x00ff}, {0x603c, 0x00ff},
	{0x6003, 0x00ff}, {0x6032, 0x00ff}, {0x6032, 0x00ff}, {0x6001, 0x00ff},
	{0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6012, 0x0003}, {0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6012, 0x0003},
	{0xffff, 0xffff},
	{0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6012, 0x0003}, {0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6012, 0x0003},
	{0xffff, 0xffff},
	{0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6000, 0x801e},
	{0xffff, 0xffff},
	{0x6004, 0x001e}, {0x6012, 0x0003},
};

static int  ov9655_init_at_startup(struct gspca_dev *gspca_dev);
static int  ov9655_configure_alt(struct gspca_dev *gspca_dev);
static int  ov9655_init_pre_alt(struct gspca_dev *gspca_dev);
static int  ov9655_init_post_alt(struct gspca_dev *gspca_dev);
static void ov9655_post_unset_alt(struct gspca_dev *gspca_dev);
static int  ov9655_camera_settings(struct gspca_dev *gspca_dev);
/*==========================================================================*/

void ov9655_init_settings(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->vcur.backlight  =   0;
	sd->vcur.brightness = 128;
	sd->vcur.sharpness  =   0;
	sd->vcur.contrast   =   0;
	sd->vcur.gamma      =   0;
	sd->vcur.hue        =   0;
	sd->vcur.saturation =   0;
	sd->vcur.whitebal   =   0;

	sd->vmax.backlight  =   0;
	sd->vmax.brightness = 255;
	sd->vmax.sharpness  =   0;
	sd->vmax.contrast   =   0;
	sd->vmax.gamma      =   0;
	sd->vmax.hue        =   0 + 1;
	sd->vmax.saturation =   0;
	sd->vmax.whitebal   =   0;
	sd->vmax.mirror     = 0;
	sd->vmax.flip       = 0;
	sd->vmax.AC50Hz     = 0;

	sd->dev_camera_settings = ov9655_camera_settings;
	sd->dev_init_at_startup = ov9655_init_at_startup;
	sd->dev_configure_alt   = ov9655_configure_alt;
	sd->dev_init_pre_alt    = ov9655_init_pre_alt;
	sd->dev_post_unset_alt  = ov9655_post_unset_alt;
}

/*==========================================================================*/

static int ov9655_init_at_startup(struct gspca_dev *gspca_dev)
{
	fetch_validx(gspca_dev, tbl_init_at_startup,
			ARRAY_SIZE(tbl_init_at_startup));
	fetch_validx(gspca_dev, tbl_commmon, ARRAY_SIZE(tbl_commmon));
/*	ctrl_out(gspca_dev, 0x40, 11, 0x0000, 0x0000, 0, NULL);*/

	return 0;
}

static int ov9655_init_pre_alt(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->vold.brightness = -1;
	sd->vold.hue = -1;

	fetch_validx(gspca_dev, tbl_commmon, ARRAY_SIZE(tbl_commmon));

	ov9655_init_post_alt(gspca_dev);

	return 0;
}

static int ov9655_init_post_alt(struct gspca_dev *gspca_dev)
{
	s32 reso = gspca_dev->cam.cam_mode[(s32) gspca_dev->curr_mode].priv;
	s32 n; /* reserved for FETCH macros */
	s32 i;
	u8 **tbl;

	ctrl_out(gspca_dev, 0x40, 5, 0x0001, 0x0000, 0, NULL);

	tbl = (reso == IMAGE_640) ? tbl_640 : tbl_800;

	ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200,
			tbl_length[0], tbl[0]);
	for (i = 1; i < 7; i++)
		ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200,
				tbl_length[i], tbl[i]);
	ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200,
			tbl_length[7], tbl[7]);

	n = fetch_validx(gspca_dev, tbl_init_post_alt,
			ARRAY_SIZE(tbl_init_post_alt));

	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200, 8, dat_post_1);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);

	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200, 8, dat_post_1);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);

	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);
	ctrl_in(gspca_dev, 0xc0, 2, 0x6000, 0x801e, 1, c04);
	keep_on_fetching_validx(gspca_dev, tbl_init_post_alt,
					ARRAY_SIZE(tbl_init_post_alt), n);

	ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200, 8, dat_post_1);

	ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200, 4, dat_post_2);
	ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200, 8, dat_post_3);

	ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200, 4, dat_post_4);
	ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200, 8, dat_post_5);

	ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200, 4, dat_post_6);
	ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200, 8, dat_post_7);

	ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200, 8, dat_post_8);

	ov9655_camera_settings(gspca_dev);

	return 0;
}

static int ov9655_configure_alt(struct gspca_dev *gspca_dev)
{
	s32 reso = gspca_dev->cam.cam_mode[(s32) gspca_dev->curr_mode].priv;

	switch (reso) {
	case IMAGE_640:
		gspca_dev->alt = 1 + 1;
		break;

	default:
		gspca_dev->alt = 1 + 1;
		break;
	}
	return 0;
}

static int ov9655_camera_settings(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	u8 dat_bright[] = "\x04\x00\x10\x7c\xa1\x00\x00\x70";

	s32 bright = sd->vcur.brightness;
	s32 hue    = sd->vcur.hue;

	if (bright != sd->vold.brightness) {
		sd->vold.brightness = bright;
		if (bright < 0 || bright > sd->vmax.brightness)
			bright = 0;

		dat_bright[3] = bright;
		ctrl_out(gspca_dev, 0x40, 3, 0x6000, 0x0200, 8, dat_bright);
	}

	if (hue != sd->vold.hue) {
		sd->vold.hue = hue;
		sd->swapRB = (hue != 0);
	}

	return 0;
}

static void ov9655_post_unset_alt(struct gspca_dev *gspca_dev)
{
	ctrl_out(gspca_dev, 0x40, 5, 0x0000, 0x0000, 0, NULL);
	ctrl_out(gspca_dev, 0x40, 1, 0x0061, 0x0000, 0, NULL);
}
