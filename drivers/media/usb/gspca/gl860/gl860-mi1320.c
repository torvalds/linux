// SPDX-License-Identifier: GPL-2.0-or-later
/* Subdriver for the GL860 chip with the MI1320 sensor
 * Author Olivier LORIN from own logs
 */

/* Sensor : MI1320 */

#include "gl860.h"

static struct validx tbl_common[] = {
	{0xba00, 0x00f0}, {0xba00, 0x00f1}, {0xba51, 0x0066}, {0xba02, 0x00f1},
	{0xba05, 0x0067}, {0xba05, 0x00f1}, {0xbaa0, 0x0065}, {0xba00, 0x00f1},
	{0xffff, 0xffff},
	{0xba00, 0x00f0}, {0xba02, 0x00f1}, {0xbafa, 0x0028}, {0xba02, 0x00f1},
	{0xba00, 0x00f0}, {0xba01, 0x00f1}, {0xbaf0, 0x0006}, {0xba0e, 0x00f1},
	{0xba70, 0x0006}, {0xba0e, 0x00f1},
	{0xffff, 0xffff},
	{0xba74, 0x0006}, {0xba0e, 0x00f1},
	{0xffff, 0xffff},
	{0x0061, 0x0000}, {0x0068, 0x000d},
};

static struct validx tbl_init_at_startup[] = {
	{0x0000, 0x0000}, {0x0010, 0x0010},
	{35, 0xffff},
	{0x0008, 0x00c0}, {0x0001, 0x00c1}, {0x0001, 0x00c2}, {0x0020, 0x0006},
	{0x006a, 0x000d},
};

static struct validx tbl_sensor_settings_common[] = {
	{0x0010, 0x0010}, {0x0003, 0x00c1}, {0x0042, 0x00c2}, {0x0040, 0x0000},
	{0x006a, 0x0007}, {0x006a, 0x000d}, {0x0063, 0x0006},
};
static struct validx tbl_sensor_settings_1280[] = {
	{0xba00, 0x00f0}, {0xba00, 0x00f1}, {0xba5a, 0x0066}, {0xba02, 0x00f1},
	{0xba05, 0x0067}, {0xba05, 0x00f1}, {0xba20, 0x0065}, {0xba00, 0x00f1},
};
static struct validx tbl_sensor_settings_800[] = {
	{0xba00, 0x00f0}, {0xba00, 0x00f1}, {0xba5a, 0x0066}, {0xba02, 0x00f1},
	{0xba05, 0x0067}, {0xba05, 0x00f1}, {0xba20, 0x0065}, {0xba00, 0x00f1},
};
static struct validx tbl_sensor_settings_640[] = {
	{0xba00, 0x00f0}, {0xba00, 0x00f1}, {0xbaa0, 0x0065}, {0xba00, 0x00f1},
	{0xba51, 0x0066}, {0xba02, 0x00f1}, {0xba05, 0x0067}, {0xba05, 0x00f1},
	{0xba20, 0x0065}, {0xba00, 0x00f1},
};
static struct validx tbl_post_unset_alt[] = {
	{0xba00, 0x00f0}, {0xba00, 0x00f1}, {0xbaa0, 0x0065}, {0xba00, 0x00f1},
	{0x0061, 0x0000}, {0x0068, 0x000d},
};

static u8 *tbl_1280[] = {
	(u8[]){
		0x0d, 0x80, 0xf1, 0x08, 0x03, 0x04, 0xf1, 0x00,
		0x04, 0x05, 0xf1, 0x02, 0x05, 0x00, 0xf1, 0xf1,
		0x06, 0x00, 0xf1, 0x0d, 0x20, 0x01, 0xf1, 0x00,
		0x21, 0x84, 0xf1, 0x00, 0x0d, 0x00, 0xf1, 0x08,
		0xf0, 0x00, 0xf1, 0x01, 0x34, 0x00, 0xf1, 0x00,
		0x9b, 0x43, 0xf1, 0x00, 0xa6, 0x05, 0xf1, 0x00,
		0xa9, 0x04, 0xf1, 0x00, 0xa1, 0x05, 0xf1, 0x00,
		0xa4, 0x04, 0xf1, 0x00, 0xae, 0x0a, 0xf1, 0x08
	}, (u8[]){
		0xf0, 0x00, 0xf1, 0x02, 0x3a, 0x05, 0xf1, 0xf1,
		0x3c, 0x05, 0xf1, 0xf1, 0x59, 0x01, 0xf1, 0x47,
		0x5a, 0x01, 0xf1, 0x88, 0x5c, 0x0a, 0xf1, 0x06,
		0x5d, 0x0e, 0xf1, 0x0a, 0x64, 0x5e, 0xf1, 0x1c,
		0xd2, 0x00, 0xf1, 0xcf, 0xcb, 0x00, 0xf1, 0x01
	}, (u8[]){
		0xd3, 0x02, 0xd4, 0x28, 0xd5, 0x01, 0xd0, 0x02,
		0xd1, 0x18, 0xd2, 0xc1
	}
};

static u8 *tbl_800[] = {
	(u8[]){
		0x0d, 0x80, 0xf1, 0x08, 0x03, 0x03, 0xf1, 0xc0,
		0x04, 0x05, 0xf1, 0x02, 0x05, 0x00, 0xf1, 0xf1,
		0x06, 0x00, 0xf1, 0x0d, 0x20, 0x01, 0xf1, 0x00,
		0x21, 0x84, 0xf1, 0x00, 0x0d, 0x00, 0xf1, 0x08,
		0xf0, 0x00, 0xf1, 0x01, 0x34, 0x00, 0xf1, 0x00,
		0x9b, 0x43, 0xf1, 0x00, 0xa6, 0x05, 0xf1, 0x00,
		0xa9, 0x03, 0xf1, 0xc0, 0xa1, 0x03, 0xf1, 0x20,
		0xa4, 0x02, 0xf1, 0x5a, 0xae, 0x0a, 0xf1, 0x08
	}, (u8[]){
		0xf0, 0x00, 0xf1, 0x02, 0x3a, 0x05, 0xf1, 0xf1,
		0x3c, 0x05, 0xf1, 0xf1, 0x59, 0x01, 0xf1, 0x47,
		0x5a, 0x01, 0xf1, 0x88, 0x5c, 0x0a, 0xf1, 0x06,
		0x5d, 0x0e, 0xf1, 0x0a, 0x64, 0x5e, 0xf1, 0x1c,
		0xd2, 0x00, 0xf1, 0xcf, 0xcb, 0x00, 0xf1, 0x01
	}, (u8[]){
		0xd3, 0x02, 0xd4, 0x18, 0xd5, 0x21, 0xd0, 0x02,
		0xd1, 0x10, 0xd2, 0x59
	}
};

static u8 *tbl_640[] = {
	(u8[]){
		0x0d, 0x80, 0xf1, 0x08, 0x03, 0x04, 0xf1, 0x04,
		0x04, 0x05, 0xf1, 0x02, 0x07, 0x01, 0xf1, 0x7c,
		0x08, 0x00, 0xf1, 0x0e, 0x21, 0x80, 0xf1, 0x00,
		0x0d, 0x00, 0xf1, 0x08, 0xf0, 0x00, 0xf1, 0x01,
		0x34, 0x10, 0xf1, 0x10, 0x3a, 0x43, 0xf1, 0x00,
		0xa6, 0x05, 0xf1, 0x02, 0xa9, 0x04, 0xf1, 0x04,
		0xa7, 0x02, 0xf1, 0x81, 0xaa, 0x01, 0xf1, 0xe2,
		0xae, 0x0c, 0xf1, 0x09
	}, (u8[]){
		0xf0, 0x00, 0xf1, 0x02, 0x39, 0x03, 0xf1, 0xfc,
		0x3b, 0x04, 0xf1, 0x04, 0x57, 0x01, 0xf1, 0xb6,
		0x58, 0x02, 0xf1, 0x0d, 0x5c, 0x1f, 0xf1, 0x19,
		0x5d, 0x24, 0xf1, 0x1e, 0x64, 0x5e, 0xf1, 0x1c,
		0xd2, 0x00, 0xf1, 0x00, 0xcb, 0x00, 0xf1, 0x01
	}, (u8[]){
		0xd3, 0x02, 0xd4, 0x10, 0xd5, 0x81, 0xd0, 0x02,
		0xd1, 0x08, 0xd2, 0xe1
	}
};

static s32 tbl_sat[] = {0x25, 0x1d, 0x15, 0x0d, 0x05, 0x4d, 0x55, 0x5d, 0x2d};
static s32 tbl_bright[] = {0, 8, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
static s32 tbl_backlight[] = {0x0e, 0x06, 0x02};

static s32 tbl_cntr1[] = {
	0x90, 0x98, 0xa0, 0xa8, 0xb0, 0xb8, 0xc0, 0xc8, 0xd0, 0xe0, 0xf0};
static s32 tbl_cntr2[] = {
	0x70, 0x68, 0x60, 0x58, 0x50, 0x48, 0x40, 0x38, 0x30, 0x20, 0x10};

static u8 dat_wbalNL[] =
	"\xf0\x00\xf1\x01\x05\x00\xf1\x06" "\x3b\x04\xf1\x2a\x47\x10\xf1\x10"
	"\x9d\x3c\xf1\xae\xaf\x10\xf1\x00" "\xf0\x00\xf1\x02\x2f\x91\xf1\x20"
	"\x9c\x91\xf1\x20\x37\x03\xf1\x00" "\x9d\xc5\xf1\x0f\xf0\x00\xf1\x00";

static u8 dat_wbalLL[] =
	"\xf0\x00\xf1\x01\x05\x00\xf1\x0c" "\x3b\x04\xf1\x2a\x47\x40\xf1\x40"
	"\x9d\x20\xf1\xae\xaf\x10\xf1\x00" "\xf0\x00\xf1\x02\x2f\xd1\xf1\x00"
	"\x9c\xd1\xf1\x00\x37\x03\xf1\x00" "\x9d\xc5\xf1\x3f\xf0\x00\xf1\x00";

static u8 dat_wbalBL[] =
	"\xf0\x00\xf1\x01\x05\x00\xf1\x06" "\x47\x10\xf1\x30\x9d\x3c\xf1\xae"
	"\xaf\x10\xf1\x00\xf0\x00\xf1\x02" "\x2f\x91\xf1\x20\x9c\x91\xf1\x20"
	"\x37\x03\xf1\x00\x9d\xc5\xf1\x2f" "\xf0\x00\xf1\x00";

static u8 dat_hvflip1[] = {0xf0, 0x00, 0xf1, 0x00};

static u8 dat_common00[] =
	"\x00\x01\x07\x6a\x06\x63\x0d\x6a" "\xc0\x00\x10\x10\xc1\x03\xc2\x42"
	"\xd8\x04\x58\x00\x04\x02";
static u8 dat_common01[] =
	"\x0d\x00\xf1\x0b\x0d\x00\xf1\x08" "\x35\x00\xf1\x22\x68\x00\xf1\x5d"
	"\xf0\x00\xf1\x01\x06\x70\xf1\x0e" "\xf0\x00\xf1\x02\xdd\x18\xf1\xe0";
static u8 dat_common02[] =
	"\x05\x01\xf1\x84\x06\x00\xf1\x44" "\x07\x00\xf1\xbe\x08\x00\xf1\x1e"
	"\x20\x01\xf1\x03\x21\x84\xf1\x00" "\x22\x0d\xf1\x0f\x24\x80\xf1\x00"
	"\x34\x18\xf1\x2d\x35\x00\xf1\x22" "\x43\x83\xf1\x83\x59\x00\xf1\xff";
static u8 dat_common03[] =
	"\xf0\x00\xf1\x02\x39\x06\xf1\x8c" "\x3a\x06\xf1\x8c\x3b\x03\xf1\xda"
	"\x3c\x05\xf1\x30\x57\x01\xf1\x0c" "\x58\x01\xf1\x42\x59\x01\xf1\x0c"
	"\x5a\x01\xf1\x42\x5c\x13\xf1\x0e" "\x5d\x17\xf1\x12\x64\x1e\xf1\x1c";
static u8 dat_common04[] =
	"\xf0\x00\xf1\x02\x24\x5f\xf1\x20" "\x28\xea\xf1\x02\x5f\x41\xf1\x43";
static u8 dat_common05[] =
	"\x02\x00\xf1\xee\x03\x29\xf1\x1a" "\x04\x02\xf1\xa4\x09\x00\xf1\x68"
	"\x0a\x00\xf1\x2a\x0b\x00\xf1\x04" "\x0c\x00\xf1\x93\x0d\x00\xf1\x82"
	"\x0e\x00\xf1\x40\x0f\x00\xf1\x5f" "\x10\x00\xf1\x4e\x11\x00\xf1\x5b";
static u8 dat_common06[] =
	"\x15\x00\xf1\xc9\x16\x00\xf1\x5e" "\x17\x00\xf1\x9d\x18\x00\xf1\x06"
	"\x19\x00\xf1\x89\x1a\x00\xf1\x12" "\x1b\x00\xf1\xa1\x1c\x00\xf1\xe4"
	"\x1d\x00\xf1\x7a\x1e\x00\xf1\x64" "\xf6\x00\xf1\x5f";
static u8 dat_common07[] =
	"\xf0\x00\xf1\x01\x53\x09\xf1\x03" "\x54\x3d\xf1\x1c\x55\x99\xf1\x72"
	"\x56\xc1\xf1\xb1\x57\xd8\xf1\xce" "\x58\xe0\xf1\x00\xdc\x0a\xf1\x03"
	"\xdd\x45\xf1\x20\xde\xae\xf1\x82" "\xdf\xdc\xf1\xc9\xe0\xf6\xf1\xea"
	"\xe1\xff\xf1\x00";
static u8 dat_common08[] =
	"\xf0\x00\xf1\x01\x80\x00\xf1\x06" "\x81\xf6\xf1\x08\x82\xfb\xf1\xf7"
	"\x83\x00\xf1\xfe\xb6\x07\xf1\x03" "\xb7\x18\xf1\x0c\x84\xfb\xf1\x06"
	"\x85\xfb\xf1\xf9\x86\x00\xf1\xff" "\xb8\x07\xf1\x04\xb9\x16\xf1\x0a";
static u8 dat_common09[] =
	"\x87\xfa\xf1\x05\x88\xfc\xf1\xf9" "\x89\x00\xf1\xff\xba\x06\xf1\x03"
	"\xbb\x17\xf1\x09\x8a\xe8\xf1\x14" "\x8b\xf7\xf1\xf0\x8c\xfd\xf1\xfa"
	"\x8d\x00\xf1\x00\xbc\x05\xf1\x01" "\xbd\x0c\xf1\x08\xbe\x00\xf1\x14";
static u8 dat_common10[] =
	"\x8e\xea\xf1\x13\x8f\xf7\xf1\xf2" "\x90\xfd\xf1\xfa\x91\x00\xf1\x00"
	"\xbf\x05\xf1\x01\xc0\x0a\xf1\x08" "\xc1\x00\xf1\x0c\x92\xed\xf1\x0f"
	"\x93\xf9\xf1\xf4\x94\xfe\xf1\xfb" "\x95\x00\xf1\x00\xc2\x04\xf1\x01"
	"\xc3\x0a\xf1\x07\xc4\x00\xf1\x10";
static u8 dat_common11[] =
	"\xf0\x00\xf1\x01\x05\x00\xf1\x06" "\x25\x00\xf1\x55\x34\x10\xf1\x10"
	"\x35\xf0\xf1\x10\x3a\x02\xf1\x03" "\x3b\x04\xf1\x2a\x9b\x43\xf1\x00"
	"\xa4\x03\xf1\xc0\xa7\x02\xf1\x81";

static int  mi1320_init_at_startup(struct gspca_dev *gspca_dev);
static int  mi1320_configure_alt(struct gspca_dev *gspca_dev);
static int  mi1320_init_pre_alt(struct gspca_dev *gspca_dev);
static int  mi1320_init_post_alt(struct gspca_dev *gspca_dev);
static void mi1320_post_unset_alt(struct gspca_dev *gspca_dev);
static int  mi1320_sensor_settings(struct gspca_dev *gspca_dev);
static int  mi1320_camera_settings(struct gspca_dev *gspca_dev);
/*==========================================================================*/

void mi1320_init_settings(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->vcur.backlight  =  0;
	sd->vcur.brightness =  0;
	sd->vcur.sharpness  =  6;
	sd->vcur.contrast   = 10;
	sd->vcur.gamma      = 20;
	sd->vcur.hue        =  0;
	sd->vcur.saturation =  6;
	sd->vcur.whitebal   =  0;
	sd->vcur.mirror     = 0;
	sd->vcur.flip       = 0;
	sd->vcur.AC50Hz     = 1;

	sd->vmax.backlight  =  2;
	sd->vmax.brightness =  8;
	sd->vmax.sharpness  =  7;
	sd->vmax.contrast   =  0; /* 10 but not working with this driver */
	sd->vmax.gamma      = 40;
	sd->vmax.hue        =  5 + 1;
	sd->vmax.saturation =  8;
	sd->vmax.whitebal   =  2;
	sd->vmax.mirror     = 1;
	sd->vmax.flip       = 1;
	sd->vmax.AC50Hz     = 1;

	sd->dev_camera_settings = mi1320_camera_settings;
	sd->dev_init_at_startup = mi1320_init_at_startup;
	sd->dev_configure_alt   = mi1320_configure_alt;
	sd->dev_init_pre_alt    = mi1320_init_pre_alt;
	sd->dev_post_unset_alt  = mi1320_post_unset_alt;
}

/*==========================================================================*/

static void common(struct gspca_dev *gspca_dev)
{
	s32 n; /* reserved for FETCH functions */

	ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200, 22, dat_common00);
	ctrl_out(gspca_dev, 0x40, 1, 0x0041, 0x0000, 0, NULL);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 32, dat_common01);
	n = fetch_validx(gspca_dev, tbl_common, ARRAY_SIZE(tbl_common));
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 48, dat_common02);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 48, dat_common03);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 16, dat_common04);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 48, dat_common05);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 44, dat_common06);
	keep_on_fetching_validx(gspca_dev, tbl_common,
					ARRAY_SIZE(tbl_common), n);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 52, dat_common07);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 48, dat_common08);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 48, dat_common09);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 56, dat_common10);
	keep_on_fetching_validx(gspca_dev, tbl_common,
					ARRAY_SIZE(tbl_common), n);
	ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 40, dat_common11);
	keep_on_fetching_validx(gspca_dev, tbl_common,
					ARRAY_SIZE(tbl_common), n);
}

static int mi1320_init_at_startup(struct gspca_dev *gspca_dev)
{
	fetch_validx(gspca_dev, tbl_init_at_startup,
				ARRAY_SIZE(tbl_init_at_startup));

	common(gspca_dev);

/*	ctrl_out(gspca_dev, 0x40, 11, 0x0000, 0x0000, 0, NULL); */

	return 0;
}

static int mi1320_init_pre_alt(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->mirrorMask = 0;

	sd->vold.backlight  = -1;
	sd->vold.brightness = -1;
	sd->vold.sharpness  = -1;
	sd->vold.contrast   = -1;
	sd->vold.saturation = -1;
	sd->vold.gamma    = -1;
	sd->vold.hue      = -1;
	sd->vold.whitebal = -1;
	sd->vold.mirror   = -1;
	sd->vold.flip     = -1;
	sd->vold.AC50Hz   = -1;

	common(gspca_dev);

	mi1320_sensor_settings(gspca_dev);

	mi1320_init_post_alt(gspca_dev);

	return 0;
}

static int mi1320_init_post_alt(struct gspca_dev *gspca_dev)
{
	mi1320_camera_settings(gspca_dev);

	return 0;
}

static int mi1320_sensor_settings(struct gspca_dev *gspca_dev)
{
	s32 reso = gspca_dev->cam.cam_mode[(s32) gspca_dev->curr_mode].priv;

	ctrl_out(gspca_dev, 0x40, 5, 0x0001, 0x0000, 0, NULL);

	fetch_validx(gspca_dev, tbl_sensor_settings_common,
				ARRAY_SIZE(tbl_sensor_settings_common));

	switch (reso) {
	case IMAGE_1280:
		fetch_validx(gspca_dev, tbl_sensor_settings_1280,
					ARRAY_SIZE(tbl_sensor_settings_1280));
		ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 64, tbl_1280[0]);
		ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 40, tbl_1280[1]);
		ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200, 12, tbl_1280[2]);
		break;

	case IMAGE_800:
		fetch_validx(gspca_dev, tbl_sensor_settings_800,
					ARRAY_SIZE(tbl_sensor_settings_800));
		ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 64, tbl_800[0]);
		ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 40, tbl_800[1]);
		ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200, 12, tbl_800[2]);
		break;

	default:
		fetch_validx(gspca_dev, tbl_sensor_settings_640,
					ARRAY_SIZE(tbl_sensor_settings_640));
		ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 60, tbl_640[0]);
		ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 40, tbl_640[1]);
		ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200, 12, tbl_640[2]);
		break;
	}
	return 0;
}

static int mi1320_configure_alt(struct gspca_dev *gspca_dev)
{
	s32 reso = gspca_dev->cam.cam_mode[(s32) gspca_dev->curr_mode].priv;

	switch (reso) {
	case IMAGE_640:
		gspca_dev->alt = 3 + 1;
		break;

	case IMAGE_800:
	case IMAGE_1280:
		gspca_dev->alt = 1 + 1;
		break;
	}
	return 0;
}

static int mi1320_camera_settings(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	s32 backlight = sd->vcur.backlight;
	s32 bright = sd->vcur.brightness;
	s32 sharp  = sd->vcur.sharpness;
	s32 cntr   = sd->vcur.contrast;
	s32 gam	   = sd->vcur.gamma;
	s32 hue    = sd->vcur.hue;
	s32 sat	   = sd->vcur.saturation;
	s32 wbal   = sd->vcur.whitebal;
	s32 mirror = (((sd->vcur.mirror > 0) ^ sd->mirrorMask) > 0);
	s32 flip   = (((sd->vcur.flip   > 0) ^ sd->mirrorMask) > 0);
	s32 freq   = (sd->vcur.AC50Hz > 0);
	s32 i;

	if (freq != sd->vold.AC50Hz) {
		sd->vold.AC50Hz = freq;

		freq = 2 * (freq == 0);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba02, 0x00f1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00       , 0x005b, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba01 + freq, 0x00f1, 0, NULL);
	}

	if (wbal != sd->vold.whitebal) {
		sd->vold.whitebal = wbal;
		if (wbal < 0 || wbal > sd->vmax.whitebal)
			wbal = 0;

		for (i = 0; i < 2; i++) {
			if (wbal == 0) { /* Normal light */
				ctrl_out(gspca_dev, 0x40, 1,
						0x0010, 0x0010, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 1,
						0x0003, 0x00c1, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 1,
						0x0042, 0x00c2, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 3,
						0xba00, 0x0200, 48, dat_wbalNL);
			}

			if (wbal == 1) { /* Low light */
				ctrl_out(gspca_dev, 0x40, 1,
						0x0010, 0x0010, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 1,
						0x0004, 0x00c1, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 1,
						0x0043, 0x00c2, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 3,
						0xba00, 0x0200, 48, dat_wbalLL);
			}

			if (wbal == 2) { /* Back light */
				ctrl_out(gspca_dev, 0x40, 1,
						0x0010, 0x0010, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 1,
						0x0003, 0x00c1, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 1,
						0x0042, 0x00c2, 0, NULL);
				ctrl_out(gspca_dev, 0x40, 3,
						0xba00, 0x0200, 44, dat_wbalBL);
			}
		}
	}

	if (bright != sd->vold.brightness) {
		sd->vold.brightness = bright;
		if (bright < 0 || bright > sd->vmax.brightness)
			bright = 0;

		bright = tbl_bright[bright];
		ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba01, 0x00f1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00 + bright, 0x0034, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00 + bright, 0x00f1, 0, NULL);
	}

	if (sat != sd->vold.saturation) {
		sd->vold.saturation = sat;
		if (sat < 0 || sat > sd->vmax.saturation)
			sat = 0;

		sat = tbl_sat[sat];
		ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba01, 0x00f1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00      , 0x0025, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00 + sat, 0x00f1, 0, NULL);
	}

	if (sharp != sd->vold.sharpness) {
		sd->vold.sharpness = sharp;
		if (sharp < 0 || sharp > sd->vmax.sharpness)
			sharp = 0;

		ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba01, 0x00f1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00        , 0x0005, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00 + sharp, 0x00f1, 0, NULL);
	}

	if (hue != sd->vold.hue) {
		/* 0=normal  1=NB  2="sepia"  3=negative  4=other  5=other2 */
		if (hue < 0 || hue > sd->vmax.hue)
			hue = 0;
		if (hue == sd->vmax.hue)
			sd->swapRB = 1;
		else
			sd->swapRB = 0;

		ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba01, 0x00f1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba70, 0x00e2, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00 + hue * (hue < 6), 0x00f1,
							0, NULL);
	}

	if (backlight != sd->vold.backlight) {
		sd->vold.backlight = backlight;
		if (backlight < 0 || backlight > sd->vmax.backlight)
			backlight = 0;

		backlight = tbl_backlight[backlight];
		for (i = 0; i < 2; i++) {
			ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
			ctrl_out(gspca_dev, 0x40, 1, 0xba01, 0x00f1, 0, NULL);
			ctrl_out(gspca_dev, 0x40, 1, 0xba74, 0x0006, 0, NULL);
			ctrl_out(gspca_dev, 0x40, 1, 0xba80 + backlight, 0x00f1,
								0, NULL);
		}
	}

	if (hue != sd->vold.hue) {
		sd->vold.hue = hue;

		ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba01, 0x00f1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba70, 0x00e2, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00 + hue * (hue < 6), 0x00f1,
							0, NULL);
	}

	if (mirror != sd->vold.mirror || flip != sd->vold.flip) {
		u8 dat_hvflip2[4] = {0x20, 0x01, 0xf1, 0x00};
		sd->vold.mirror = mirror;
		sd->vold.flip = flip;

		dat_hvflip2[3] = flip + 2 * mirror;
		ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 4, dat_hvflip1);
		ctrl_out(gspca_dev, 0x40, 3, 0xba00, 0x0200, 4, dat_hvflip2);
	}

	if (gam != sd->vold.gamma) {
		sd->vold.gamma = gam;
		if (gam < 0 || gam > sd->vmax.gamma)
			gam = 0;

		gam = 2 * gam;
		ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba01, 0x00f1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba04      , 0x003b, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba02 + gam, 0x00f1, 0, NULL);
	}

	if (cntr != sd->vold.contrast) {
		sd->vold.contrast = cntr;
		if (cntr < 0 || cntr > sd->vmax.contrast)
			cntr = 0;

		ctrl_out(gspca_dev, 0x40, 1, 0xba00, 0x00f0, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba01, 0x00f1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00 + tbl_cntr1[cntr], 0x0035,
							0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0xba00 + tbl_cntr2[cntr], 0x00f1,
							0, NULL);
	}

	return 0;
}

static void mi1320_post_unset_alt(struct gspca_dev *gspca_dev)
{
	ctrl_out(gspca_dev, 0x40, 5, 0x0000, 0x0000, 0, NULL);

	fetch_validx(gspca_dev, tbl_post_unset_alt,
				ARRAY_SIZE(tbl_post_unset_alt));
}
