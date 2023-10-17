// SPDX-License-Identifier: GPL-2.0
/*
 * techpoint lib
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 */

#include "techpoint_tp9951.h"
#include "techpoint_dev.h"

static struct techpoint_video_modes supported_modes[] = {
#if TP9951_DEF_PAL
	{
	 .bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
	 .width = 960,
	 .height = 576,
	 .max_fps = {
			.numerator = 10000,
			.denominator = 250000,
			},
	 .link_freq_value = TP9951_LINK_FREQ_148M,
	 .common_reg_list = NULL,
	 .common_reg_size = 0,
	 .bpp = TP9951_BITS_PER_SAMPLE,
	 .lane = TP9951_LANES,
	 .vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	 .vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
	 .vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
	 .vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
#endif
#if TP9951_DEF_NTSC
	{
	 .bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
	 .width = 960,
	 .height = 480,
	 .max_fps = {
			.numerator = 10000,
			.denominator = 250000,
			},
	 .link_freq_value = TP9951_LINK_FREQ_148M,
	 .common_reg_list = NULL,
	 .common_reg_size = 0,
	 .bpp = TP9951_BITS_PER_SAMPLE,
	 .lane = TP9951_LANES,
	 .vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	 .vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
	 .vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
	 .vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
#endif
#if TP9951_DEF_1080P
	{
	 .bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
	 .width = 1920,
	 .height = 1080,
	 .max_fps = {
			.numerator = 10000,
			.denominator = 250000,
			},
	//	.link_freq_value = TP9951_LINK_FREQ_594M,
	 .link_freq_value = TP9951_LINK_FREQ_297M,
	 .common_reg_list = NULL,
	 .common_reg_size = 0,
	 .bpp = TP9951_BITS_PER_SAMPLE,
	 .lane = TP9951_LANES,
	 .vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	 .vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
	 .vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
	 .vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
#endif
#if TP9951_DEF_720P
	{
	 .bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
	 .width = 1280,
	 .height = 720,
	 .max_fps = {
			.numerator = 10000,
			.denominator = 250000,
			},
	 .link_freq_value = TP9951_LINK_FREQ_297M,
	 .common_reg_list = NULL,
	 .common_reg_size = 0,
	 .bpp = TP9951_BITS_PER_SAMPLE,
	 .lane = TP9951_LANES,
	 .vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	 .vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
	 .vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
	 .vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
#endif
};

int tp9951_initialize(struct techpoint *techpoint)
{
	int array_size = 0;
	struct i2c_client *client = techpoint->client;
	struct device *dev = &client->dev;

	techpoint->video_modes_num = ARRAY_SIZE(supported_modes);
	array_size =
		sizeof(struct techpoint_video_modes) * techpoint->video_modes_num;
	techpoint->video_modes = devm_kzalloc(dev, array_size, GFP_KERNEL);
	memcpy(techpoint->video_modes, supported_modes, array_size);

	techpoint->cur_video_mode = &techpoint->video_modes[0];

	return 0;
}

int tp9951_get_channel_input_status(struct techpoint *techpoint, u8 ch)
{
	u8 val = 0;
	struct i2c_client *client = techpoint->client;

	if (ch != 0)	// tp9951 just support 1 chn
		return 0;
	techpoint_write_reg(client, PAGE_REG, ch);
	techpoint_read_reg(client, INPUT_STATUS_REG, &val);
	dev_dbg(&client->dev, "input_status ch %d : %x\n", ch, val);

	return (val & INPUT_STATUS_MASK) ? 0 : 1;
}

int tp9951_get_all_input_status(struct techpoint *techpoint, u8 *detect_status)
{
	u8 val = 0;
	struct i2c_client *client = techpoint->client;

	// tp9951 just support 1 chn
	techpoint_write_reg(client, PAGE_REG, 0);
	techpoint_read_reg(client, INPUT_STATUS_REG, &val);
	detect_status[0] = tp9951_get_channel_input_status(techpoint, 0);
	return 0;
}

static void tp9951_set_mipi_out(struct i2c_client *client,
								enum techpoint_support_reso reso,
								unsigned char lane)
{
	u8 tmp;
	//mipi setting
	techpoint_write_reg(client, 0x40, 0x08); //select MIPI page
	techpoint_write_reg(client, 0x02, 0x7d);
	techpoint_write_reg(client, 0x03, 0x75);
	techpoint_write_reg(client, 0x04, 0x75);
	techpoint_write_reg(client, 0x13, 0xef);
	techpoint_write_reg(client, 0x20, 0x00);
	techpoint_write_reg(client, 0x23, 0x9e);

	if (lane == MIPI_1LANE) {
		techpoint_write_reg(client, 0x21, 0x11);

		if (TECHPOINT_S_RESO_1080P_30 == reso || TECHPOINT_S_RESO_1080P_25 == reso) {
			techpoint_write_reg(client, 0x12, 0x54);
			techpoint_write_reg(client, 0x14, 0x00);
			techpoint_write_reg(client, 0x15, 0x02);

			techpoint_write_reg(client, 0x2a, 0x08);
			techpoint_write_reg(client, 0x2b, 0x06);
			techpoint_write_reg(client, 0x2c, 0x12);
			techpoint_write_reg(client, 0x2e, 0x0a);
		} else if (TECHPOINT_S_RESO_720P_30 == reso || TECHPOINT_S_RESO_720P_25 == reso) {
			techpoint_write_reg(client, 0x12, 0x54);
			techpoint_write_reg(client, 0x14, 0x00);
			techpoint_write_reg(client, 0x15, 0x12);

			techpoint_write_reg(client, 0x2a, 0x04);
			techpoint_write_reg(client, 0x2b, 0x03);
			techpoint_write_reg(client, 0x2c, 0x0a);
			techpoint_write_reg(client, 0x2e, 0x02);
		} else if (TECHPOINT_S_RESO_NTSC == reso || TECHPOINT_S_RESO_PAL == reso) {
			techpoint_write_reg(client, 0x12, 0x54);
			techpoint_write_reg(client, 0x14, 0x51);
			techpoint_write_reg(client, 0x15, 0x07);

			techpoint_write_reg(client, 0x2a, 0x02);
			techpoint_write_reg(client, 0x2b, 0x01);
			techpoint_write_reg(client, 0x2c, 0x06);
			techpoint_write_reg(client, 0x2e, 0x02);
		}
	} else {	// 2 lane
		techpoint_write_reg(client, 0x21, 0x12);

		if (TECHPOINT_S_RESO_1080P_30 == reso || TECHPOINT_S_RESO_1080P_25 == reso) {
			techpoint_write_reg(client, 0x12, 0x54);
			techpoint_write_reg(client, 0x14, 0x41);
			techpoint_write_reg(client, 0x15, 0x02);

			techpoint_write_reg(client, 0x2a, 0x04);
			techpoint_write_reg(client, 0x2b, 0x03);
			techpoint_write_reg(client, 0x2c, 0x0a);
			techpoint_write_reg(client, 0x2e, 0x02);
		} else if (TECHPOINT_S_RESO_720P_30 == reso || TECHPOINT_S_RESO_720P_25 == reso) {
			techpoint_write_reg(client, 0x12, 0x54);
			techpoint_write_reg(client, 0x14, 0x41);
			techpoint_write_reg(client, 0x15, 0x12);

			techpoint_write_reg(client, 0x2a, 0x02);
			techpoint_write_reg(client, 0x2b, 0x01);
			techpoint_write_reg(client, 0x2c, 0x06);
			techpoint_write_reg(client, 0x2e, 0x02);
		} else if (TECHPOINT_S_RESO_NTSC == reso || TECHPOINT_S_RESO_PAL == reso) {
			techpoint_write_reg(client, 0x12, 0x54);
			techpoint_write_reg(client, 0x14, 0x62);
			techpoint_write_reg(client, 0x15, 0x07);

			techpoint_write_reg(client, 0x2a, 0x02);
			techpoint_write_reg(client, 0x2b, 0x00);
			techpoint_write_reg(client, 0x2c, 0x04);
			techpoint_write_reg(client, 0x2e, 0x02);
		}
	}

	techpoint_write_reg(client, 0x40, 0x00); //back to decoder page
	techpoint_read_reg(client, 0x06, &tmp); //PLL reset
	techpoint_write_reg(client, 0x06, 0x80 | tmp);

	techpoint_write_reg(client, 0x40, 0x08); //back to mipi page

	techpoint_read_reg(client, 0x14, &tmp); //PLL reset
	techpoint_write_reg(client, 0x14, 0x80 | tmp);
	techpoint_write_reg(client, 0x14, tmp);

	/* Enable MIPI CSI2 output */
	techpoint_write_reg(client, 0x28, 0x02);	 //stream off
	techpoint_write_reg(client, 0x28, 0x00);	 //stream on
	techpoint_write_reg(client, 0x40, 0x00);	 //back to decoder page
}

int tp9951_set_channel_reso(struct i2c_client *client, int ch,
				enum techpoint_support_reso reso)
{
	int val = reso;

	dev_info(&client->dev, "##$$ %s", __func__);
	techpoint_write_reg(client, 0x40, 0x00);	 //select decoder page
	techpoint_write_reg(client, 0x06, 0x12);	 //default value
	techpoint_write_reg(client, 0x42, 0x00);	 //common setting for all format
	techpoint_write_reg(client, 0x4e, 0x00);	 //common setting for MIPI output
	techpoint_write_reg(client, 0x54, 0x00);	 //common setting for MIPI output
	techpoint_write_reg(client, 0x41, ch);		 //video MUX select

	switch (val) {
	case TECHPOINT_S_RESO_720P_25:
#if TP9951_DEF_720P
	default:
#endif
		dev_err(&client->dev, "set channel 720P_25\n");
		techpoint_write_reg(client, 0x02, 0x42);
		techpoint_write_reg(client, 0x07, 0xc0);
		techpoint_write_reg(client, 0x0b, 0xc0);
		techpoint_write_reg(client, 0x0c, 0x13);
		techpoint_write_reg(client, 0x0d, 0x50);

		techpoint_write_reg(client, 0x15, 0x13);
		techpoint_write_reg(client, 0x16, 0x15);
		techpoint_write_reg(client, 0x17, 0x00);
		techpoint_write_reg(client, 0x18, 0x19);
		techpoint_write_reg(client, 0x19, 0xd0);
		techpoint_write_reg(client, 0x1a, 0x25);
		techpoint_write_reg(client, 0x1c, 0x07);//1280*720, 25fps
		techpoint_write_reg(client, 0x1d, 0xbc);//1280*720, 25fps

		techpoint_write_reg(client, 0x20, 0x30);
		techpoint_write_reg(client, 0x21, 0x84);
		techpoint_write_reg(client, 0x22, 0x36);
		techpoint_write_reg(client, 0x23, 0x3c);

		techpoint_write_reg(client, 0x2b, 0x60);
		techpoint_write_reg(client, 0x2c, 0x2a);
		techpoint_write_reg(client, 0x2d, 0x30);
		techpoint_write_reg(client, 0x2e, 0x70);

		techpoint_write_reg(client, 0x30, 0x48);
		techpoint_write_reg(client, 0x31, 0xbb);
		techpoint_write_reg(client, 0x32, 0x2e);
		techpoint_write_reg(client, 0x33, 0x90);
		techpoint_write_reg(client, 0x35, 0x25);
		techpoint_write_reg(client, 0x38, 0x00);
		techpoint_write_reg(client, 0x39, 0x18);
		if (STD_HDA) {
			techpoint_write_reg(client, 0x02, 0x46);
			techpoint_write_reg(client, 0x0d, 0x71);
			techpoint_write_reg(client, 0x18, 0x1b);
			techpoint_write_reg(client, 0x20, 0x40);
			techpoint_write_reg(client, 0x21, 0x46);
			techpoint_write_reg(client, 0x25, 0xfe);
			techpoint_write_reg(client, 0x26, 0x01);
			techpoint_write_reg(client, 0x2c, 0x3a);
			techpoint_write_reg(client, 0x2d, 0x5a);
			techpoint_write_reg(client, 0x2e, 0x40);
			techpoint_write_reg(client, 0x30, 0x9e);
			techpoint_write_reg(client, 0x31, 0x20);
			techpoint_write_reg(client, 0x32, 0x10);
			techpoint_write_reg(client, 0x33, 0x90);
		}

		tp9951_set_mipi_out(client, reso, MIPI_2LANE);	// 2 lane

		break;
	case TECHPOINT_S_RESO_1080P_25:		// FHD25
#if TP9951_DEF_1080P
	default:
#endif
		dev_err(&client->dev, "set channel 1080P_25\n");
		techpoint_write_reg(client, 0x02, 0x40);
		techpoint_write_reg(client, 0x07, 0xc0);
		techpoint_write_reg(client, 0x0b, 0xc0);
		techpoint_write_reg(client, 0x0c, 0x03);
		techpoint_write_reg(client, 0x0d, 0x50);

		techpoint_write_reg(client, 0x15, 0x03);
		techpoint_write_reg(client, 0x16, 0xd2);
		techpoint_write_reg(client, 0x17, 0x80);
		techpoint_write_reg(client, 0x18, 0x29);
		techpoint_write_reg(client, 0x19, 0x38);
		techpoint_write_reg(client, 0x1a, 0x47);
		techpoint_write_reg(client, 0x1c, 0x0a);//1920*1080, 25fps
		techpoint_write_reg(client, 0x1d, 0x50);//

		techpoint_write_reg(client, 0x20, 0x30);
		techpoint_write_reg(client, 0x21, 0x84);
		techpoint_write_reg(client, 0x22, 0x36);
		techpoint_write_reg(client, 0x23, 0x3c);

		techpoint_write_reg(client, 0x2b, 0x60);
		techpoint_write_reg(client, 0x2c, 0x2a);
		techpoint_write_reg(client, 0x2d, 0x30);
		techpoint_write_reg(client, 0x2e, 0x70);

		techpoint_write_reg(client, 0x30, 0x48);
		techpoint_write_reg(client, 0x31, 0xbb);
		techpoint_write_reg(client, 0x32, 0x2e);
		techpoint_write_reg(client, 0x33, 0x90);
		techpoint_write_reg(client, 0x35, 0x05);
		techpoint_write_reg(client, 0x38, 0x00);
		techpoint_write_reg(client, 0x39, 0x1C);
		if (STD_HDA) {
			techpoint_write_reg(client, 0x02, 0x44);
			techpoint_write_reg(client, 0x0d, 0x73);
			techpoint_write_reg(client, 0x15, 0x01);
			techpoint_write_reg(client, 0x16, 0xf0);
			techpoint_write_reg(client, 0x18, 0x2a);
			techpoint_write_reg(client, 0x20, 0x3c);
			techpoint_write_reg(client, 0x21, 0x46);
			techpoint_write_reg(client, 0x25, 0xfe);
			techpoint_write_reg(client, 0x26, 0x0d);
			techpoint_write_reg(client, 0x2c, 0x3a);
			techpoint_write_reg(client, 0x2d, 0x54);
			techpoint_write_reg(client, 0x2e, 0x40);
			techpoint_write_reg(client, 0x30, 0xa5);
			techpoint_write_reg(client, 0x31, 0x86);
			techpoint_write_reg(client, 0x32, 0xfb);
			techpoint_write_reg(client, 0x33, 0x60);
		}

		tp9951_set_mipi_out(client, reso, MIPI_2LANE);	// 2 lane

		break;
	case TECHPOINT_S_RESO_1080P_30:		// FHD30
		dev_err(&client->dev, "set channel PAL\n");
			techpoint_write_reg(client, 0x02, 0x40);
			techpoint_write_reg(client, 0x07, 0xc0);
			techpoint_write_reg(client, 0x0b, 0xc0);
			techpoint_write_reg(client, 0x0c, 0x03);
			techpoint_write_reg(client, 0x0d, 0x50);

			techpoint_write_reg(client, 0x15, 0x03);
			techpoint_write_reg(client, 0x16, 0xd2);
			techpoint_write_reg(client, 0x17, 0x80);
			techpoint_write_reg(client, 0x18, 0x29);
			techpoint_write_reg(client, 0x19, 0x38);
			techpoint_write_reg(client, 0x1a, 0x47);
			techpoint_write_reg(client, 0x1c, 0x08);  //1920*1080, 30fps
			techpoint_write_reg(client, 0x1d, 0x98);  //

			techpoint_write_reg(client, 0x20, 0x30);
			techpoint_write_reg(client, 0x21, 0x84);
			techpoint_write_reg(client, 0x22, 0x36);
			techpoint_write_reg(client, 0x23, 0x3c);

			techpoint_write_reg(client, 0x2b, 0x60);
			techpoint_write_reg(client, 0x2c, 0x2a);
			techpoint_write_reg(client, 0x2d, 0x30);
			techpoint_write_reg(client, 0x2e, 0x70);

			techpoint_write_reg(client, 0x30, 0x48);
			techpoint_write_reg(client, 0x31, 0xbb);
			techpoint_write_reg(client, 0x32, 0x2e);
			techpoint_write_reg(client, 0x33, 0x90);

			techpoint_write_reg(client, 0x35, 0x05);
			techpoint_write_reg(client, 0x38, 0x00);
			techpoint_write_reg(client, 0x39, 0x1C);

			if (STD_HDA) { //AHD1080p30 extra
				techpoint_write_reg(client, 0x02, 0x44);
				techpoint_write_reg(client, 0x0d, 0x72);

				techpoint_write_reg(client, 0x15, 0x01);
				techpoint_write_reg(client, 0x16, 0xf0);
				techpoint_write_reg(client, 0x18, 0x2a);

				techpoint_write_reg(client, 0x20, 0x38);
				techpoint_write_reg(client, 0x21, 0x46);

				techpoint_write_reg(client, 0x25, 0xfe);
				techpoint_write_reg(client, 0x26, 0x0d);

				techpoint_write_reg(client, 0x2c, 0x3a);
				techpoint_write_reg(client, 0x2d, 0x54);
				techpoint_write_reg(client, 0x2e, 0x40);

				techpoint_write_reg(client, 0x30, 0xa5);
				techpoint_write_reg(client, 0x31, 0x95);
				techpoint_write_reg(client, 0x32, 0xe0);
				techpoint_write_reg(client, 0x33, 0x60);
			}

			tp9951_set_mipi_out(client, reso, MIPI_2LANE);	// 2 lane
	break;

	case TECHPOINT_S_RESO_PAL:
#if TP9951_DEF_PAL
	default:
#endif

#if CVBS_960H
		dev_err(&client->dev, "set channel CVBS_960H\n");

		techpoint_write_reg(client, 0x02, 0x47);
		techpoint_write_reg(client, 0x0c, 0x13);
		techpoint_write_reg(client, 0x0d, 0x51);

		techpoint_write_reg(client, 0x15, 0x13);
		techpoint_write_reg(client, 0x16, 0x76);
		techpoint_write_reg(client, 0x17, 0x80);
		techpoint_write_reg(client, 0x18, 0x17);
		techpoint_write_reg(client, 0x19, 0x20);
		techpoint_write_reg(client, 0x1a, 0x17);
		techpoint_write_reg(client, 0x1c, 0x09);
		techpoint_write_reg(client, 0x1d, 0x48);

		techpoint_write_reg(client, 0x20, 0x48);
		techpoint_write_reg(client, 0x21, 0x84);
		techpoint_write_reg(client, 0x22, 0x37);
		techpoint_write_reg(client, 0x23, 0x3f);

		techpoint_write_reg(client, 0x2b, 0x70);
		techpoint_write_reg(client, 0x2c, 0x2a);
		techpoint_write_reg(client, 0x2d, 0x64);
		techpoint_write_reg(client, 0x2e, 0x56);

		techpoint_write_reg(client, 0x30, 0x7a);
		techpoint_write_reg(client, 0x31, 0x4a);
		techpoint_write_reg(client, 0x32, 0x4d);
		techpoint_write_reg(client, 0x33, 0xf0);

		techpoint_write_reg(client, 0x35, 0x65);
		techpoint_write_reg(client, 0x38, 0x00);
		techpoint_write_reg(client, 0x39, 0x04);

#else	 //PAL 720H
		dev_err(&client->dev, "set channel PAL 720H\n");
		techpoint_write_reg(client, 0x02, 0x47);
		techpoint_write_reg(client, 0x06, 0x32);
		techpoint_write_reg(client, 0x0c, 0x13);
		techpoint_write_reg(client, 0x0d, 0x51);

		techpoint_write_reg(client, 0x15, 0x03);
		techpoint_write_reg(client, 0x16, 0xf0);
		techpoint_write_reg(client, 0x17, 0xa0);
		techpoint_write_reg(client, 0x18, 0x17);
		techpoint_write_reg(client, 0x19, 0x20);
		techpoint_write_reg(client, 0x1a, 0x15);
		techpoint_write_reg(client, 0x1c, 0x06);
		techpoint_write_reg(client, 0x1d, 0xc0);

		techpoint_write_reg(client, 0x20, 0x48);
		techpoint_write_reg(client, 0x21, 0x84);
		techpoint_write_reg(client, 0x22, 0x37);
		techpoint_write_reg(client, 0x23, 0x3f);

		techpoint_write_reg(client, 0x2b, 0x70);
		techpoint_write_reg(client, 0x2c, 0x2a);
		techpoint_write_reg(client, 0x2d, 0x4b);
		techpoint_write_reg(client, 0x2e, 0x56);

		techpoint_write_reg(client, 0x30, 0x7a);
		techpoint_write_reg(client, 0x31, 0x4a);
		techpoint_write_reg(client, 0x32, 0x4d);
		techpoint_write_reg(client, 0x33, 0xfb);

		techpoint_write_reg(client, 0x35, 0x65);
		techpoint_write_reg(client, 0x38, 0x00);
		techpoint_write_reg(client, 0x39, 0x04);
#endif
		tp9951_set_mipi_out(client, reso, MIPI_2LANE);	// 2 lane

		break;
	case TECHPOINT_S_RESO_NTSC:
#if TP9951_DEF_NTSC
	default:
#endif

#if CVBS_960H
		dev_err(&client->dev, "set channel NTSC CVBS_960H\n");
		techpoint_write_reg(client, 0x02, 0x47);
		techpoint_write_reg(client, 0x0c, 0x13);
		techpoint_write_reg(client, 0x0d, 0x50);

		techpoint_write_reg(client, 0x15, 0x13);
		techpoint_write_reg(client, 0x16, 0x60);
		techpoint_write_reg(client, 0x17, 0x80);
		techpoint_write_reg(client, 0x18, 0x12);
		techpoint_write_reg(client, 0x19, 0xf0);
		techpoint_write_reg(client, 0x1a, 0x07);
		techpoint_write_reg(client, 0x1c, 0x09);
		techpoint_write_reg(client, 0x1d, 0x38);

		techpoint_write_reg(client, 0x20, 0x40);
		techpoint_write_reg(client, 0x21, 0x84);
		techpoint_write_reg(client, 0x22, 0x36);
		techpoint_write_reg(client, 0x23, 0x3c);

		techpoint_write_reg(client, 0x2b, 0x70);
		techpoint_write_reg(client, 0x2c, 0x2a);
		techpoint_write_reg(client, 0x2d, 0x68);
		techpoint_write_reg(client, 0x2e, 0x57);

		techpoint_write_reg(client, 0x30, 0x62);
		techpoint_write_reg(client, 0x31, 0xbb);
		techpoint_write_reg(client, 0x32, 0x96);
		techpoint_write_reg(client, 0x33, 0xc0);

		techpoint_write_reg(client, 0x35, 0x65);
		techpoint_write_reg(client, 0x38, 0x00);
		techpoint_write_reg(client, 0x39, 0x04);
#else
		dev_err(&client->dev, "set channel NTSC 720H\n");
		techpoint_write_reg(client, 0x02, 0x47);
		techpoint_write_reg(client, 0x0c, 0x13);
		techpoint_write_reg(client, 0x0d, 0x50);

		techpoint_write_reg(client, 0x15, 0x03);
		techpoint_write_reg(client, 0x16, 0xd6);
		techpoint_write_reg(client, 0x17, 0xa0);
		techpoint_write_reg(client, 0x18, 0x12);
		techpoint_write_reg(client, 0x19, 0xf0);
		techpoint_write_reg(client, 0x1a, 0x05);
		techpoint_write_reg(client, 0x1c, 0x06);
		techpoint_write_reg(client, 0x1d, 0xb4);

		techpoint_write_reg(client, 0x20, 0x40);
		techpoint_write_reg(client, 0x21, 0x84);
		techpoint_write_reg(client, 0x22, 0x36);
		techpoint_write_reg(client, 0x23, 0x3c);

		techpoint_write_reg(client, 0x2b, 0x70);
		techpoint_write_reg(client, 0x2c, 0x2a);
		techpoint_write_reg(client, 0x2d, 0x4b);
		techpoint_write_reg(client, 0x2e, 0x57);

		techpoint_write_reg(client, 0x30, 0x62);
		techpoint_write_reg(client, 0x31, 0xbb);
		techpoint_write_reg(client, 0x32, 0x96);
		techpoint_write_reg(client, 0x33, 0xcb);

		techpoint_write_reg(client, 0x35, 0x65);
		techpoint_write_reg(client, 0x38, 0x00);
		techpoint_write_reg(client, 0x39, 0x04);
#endif
		break;
	}

#if TECHPOINT_TEST_PATTERN
	techpoint_write_reg(client, 0x2a, 0x3c);
#endif

	return 0;
}

// detect for reference only, the accuracy may be affected by the current invoked decoding standard.
int tp9951_get_channel_reso(struct i2c_client *client, int ch)
{
	u8 detect_fmt = 0xff;
	u8 reso = 0xff;

	techpoint_write_reg(client, 0x40, ch);
	techpoint_read_reg(client, 0x03, &detect_fmt);
	reso = detect_fmt & 0x7;

	switch (reso) {
	case TP9951_CVSTD_720P_25:
#if TP9951_DEF_720P
	default:
#endif
		dev_err(&client->dev, "detect channel %d 720P_25\n", ch);
		return TECHPOINT_S_RESO_720P_25;
	case TP9951_CVSTD_1080P_25:
#if TP9951_DEF_1080P
	default:
#endif
		dev_err(&client->dev, "detect channel %d 1080P_25\n", ch);
		return TECHPOINT_S_RESO_1080P_25;
	case TP9951_CVSTD_PAL:
#if TP9951_DEF_PAL
	default:
#endif
		dev_err(&client->dev, "detect channel %d PAL\n", ch);
		return TECHPOINT_S_RESO_PAL;
	case TP9951_CVSTD_NTSC:
#if TP9951_DEF_NTSC
	default:
#endif
		dev_err(&client->dev, "detect channel %d NTSC\n", ch);
		return TECHPOINT_S_RESO_NTSC;
	}

	return reso;
}

int tp9951_set_quick_stream(struct i2c_client *client, u32 stream)
{
	// mutex_lock(&techpoint->mutex);
	if (stream) {
		techpoint_write_reg(client, 0x40, 0x8);
		techpoint_write_reg(client, 0x28, 0x0);
	} else {
		techpoint_write_reg(client, 0x40, 0x8);
		techpoint_write_reg(client, 0x28, 0x2);
		usleep_range(40 * 1000, 50 * 1000);
	}
	// mutex_unlock(&techpoint->mutex);
	return 0;
}
