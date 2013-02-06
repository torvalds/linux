/* drivers/media/video/s5k5ccgx.c
 *
 * Copyright (c) 2010, Samsung Electronics. All rights reserved
 * Author: dongseong.lim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#include "s5k5ccgx.h"

static int s5k5ccgx_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl);

static const struct s5k5ccgx_fps s5k5ccgx_framerates[] = {
	{ I_FPS_0,	FRAME_RATE_AUTO },
	{ I_FPS_15,	FRAME_RATE_15 },
	{ I_FPS_25,	FRAME_RATE_25 },
	{ I_FPS_30,	FRAME_RATE_30 },
};

static const struct s5k5ccgx_framesize s5k5ccgx_preview_frmsizes[] = {
	{ S5K5CCGX_PREVIEW_QCIF,	176,  144 },
	{ S5K5CCGX_PREVIEW_320x240,	320,  240 },
	{ S5K5CCGX_PREVIEW_CIF,		352,  288 },
	{ S5K5CCGX_PREVIEW_528x432,	528,  432 },
	{ S5K5CCGX_PREVIEW_VGA,		640,  480 },
	{ S5K5CCGX_PREVIEW_D1,		720,  480 },
	{ S5K5CCGX_PREVIEW_SVGA,	800,  600 },
#ifdef CONFIG_VIDEO_S5K5CCGX_P2
	{ S5K5CCGX_PREVIEW_1024x552,	1024, 552 },
#else
	{ S5K5CCGX_PREVIEW_1024x576,	1024, 576 },
#endif
	/*{ S5K5CCGX_PREVIEW_1024x616,	1024, 616 },*/
	{ S5K5CCGX_PREVIEW_XGA,		1024, 768 },
	{ S5K5CCGX_PREVIEW_PVGA,	1280, 720 },
};

static const struct s5k5ccgx_framesize s5k5ccgx_capture_frmsizes[] = {
	{ S5K5CCGX_CAPTURE_VGA,		640,  480 },
#ifdef CONFIG_VIDEO_S5K5CCGX_P2
	{ S5K5CCGX_CAPTURE_W2MP,	2048, 1104 },
#else
	{ S5K5CCGX_CAPTURE_W2MP,	2048, 1152 },
#endif
	{ S5K5CCGX_CAPTURE_3MP,		2048, 1536 },
};

static struct s5k5ccgx_control s5k5ccgx_ctrls[] = {
	S5K5CCGX_INIT_CONTROL(V4L2_CID_CAMERA_FLASH_MODE, \
					FLASH_MODE_OFF),

	S5K5CCGX_INIT_CONTROL(V4L2_CID_CAMERA_BRIGHTNESS, \
					EV_DEFAULT),

	S5K5CCGX_INIT_CONTROL(V4L2_CID_CAMERA_METERING, \
					METERING_MATRIX),

	S5K5CCGX_INIT_CONTROL(V4L2_CID_CAMERA_WHITE_BALANCE, \
					WHITE_BALANCE_AUTO),

	S5K5CCGX_INIT_CONTROL(V4L2_CID_CAMERA_EFFECT, \
					IMAGE_EFFECT_NONE),
};

static const struct s5k5ccgx_regs reg_datas = {
	.ev = {
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_MINUS_4),
					s5k5ccgx_brightness_m_4),
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_MINUS_3),
					s5k5ccgx_brightness_m_3),
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_MINUS_2),
					s5k5ccgx_brightness_m_2),
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_MINUS_1),
					s5k5ccgx_brightness_m_1),
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_DEFAULT),
					s5k5ccgx_brightness_0),
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_PLUS_1),
					s5k5ccgx_brightness_p_1),
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_PLUS_2),
					s5k5ccgx_brightness_p_2),
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_PLUS_3),
					s5k5ccgx_brightness_p_3),
		S5K5CCGX_REGSET(GET_EV_INDEX(EV_PLUS_4),
					s5k5ccgx_brightness_p_4),
	},
	.metering = {
		S5K5CCGX_REGSET(METERING_MATRIX, s5k5ccgx_metering_normal),
		S5K5CCGX_REGSET(METERING_CENTER, s5k5ccgx_metering_center),
		S5K5CCGX_REGSET(METERING_SPOT, s5k5ccgx_metering_spot),
	},
	.iso = {
		S5K5CCGX_REGSET(ISO_AUTO, s5k5ccgx_iso_auto),
		S5K5CCGX_REGSET(ISO_100, s5k5ccgx_iso_100),
		S5K5CCGX_REGSET(ISO_200, s5k5ccgx_iso_200),
		S5K5CCGX_REGSET(ISO_400, s5k5ccgx_iso_400),
	},
	.effect = {
		S5K5CCGX_REGSET(IMAGE_EFFECT_NONE, s5k5ccgx_effect_off),
		S5K5CCGX_REGSET(IMAGE_EFFECT_BNW, s5k5ccgx_effect_mono),
		S5K5CCGX_REGSET(IMAGE_EFFECT_SEPIA, s5k5ccgx_effect_sepia),
		S5K5CCGX_REGSET(IMAGE_EFFECT_NEGATIVE,
				s5k5ccgx_effect_negative),
	},
	.white_balance = {
		S5K5CCGX_REGSET(WHITE_BALANCE_AUTO, s5k5ccgx_wb_auto),
		S5K5CCGX_REGSET(WHITE_BALANCE_SUNNY, s5k5ccgx_wb_daylight),
		S5K5CCGX_REGSET(WHITE_BALANCE_CLOUDY, s5k5ccgx_wb_cloudy),
		S5K5CCGX_REGSET(WHITE_BALANCE_TUNGSTEN,
				s5k5ccgx_wb_incandescent),
		S5K5CCGX_REGSET(WHITE_BALANCE_FLUORESCENT,
				s5k5ccgx_wb_fluorescent),
	},
	.preview_size = {
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_QCIF,
				s5k5ccgx_176_144_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_320x240,
				s5k5ccgx_320_240_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_CIF, s5k5ccgx_352_288_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_528x432,
				s5k5ccgx_528_432_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_VGA, s5k5ccgx_640_480_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_D1, s5k5ccgx_720_480_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_SVGA,
				s5k5ccgx_800_600_Preview),
		S5K5CCGX_REGSET(PREVIEW_WIDE_SIZE,
				S5K5CCGX_WIDE_PREVIEW_REG),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_XGA,
				s5k5ccgx_1024_768_Preview),
	},
	.scene_mode = {
		S5K5CCGX_REGSET(SCENE_MODE_NONE, s5k5ccgx_scene_off),
		S5K5CCGX_REGSET(SCENE_MODE_PORTRAIT, s5k5ccgx_scene_portrait),
		S5K5CCGX_REGSET(SCENE_MODE_NIGHTSHOT, s5k5ccgx_scene_nightshot),
		S5K5CCGX_REGSET(SCENE_MODE_LANDSCAPE, s5k5ccgx_scene_landscape),
		S5K5CCGX_REGSET(SCENE_MODE_SPORTS, s5k5ccgx_scene_sports),
		S5K5CCGX_REGSET(SCENE_MODE_PARTY_INDOOR, s5k5ccgx_scene_party),
		S5K5CCGX_REGSET(SCENE_MODE_BEACH_SNOW, s5k5ccgx_scene_beach),
		S5K5CCGX_REGSET(SCENE_MODE_SUNSET, s5k5ccgx_scene_sunset),
		S5K5CCGX_REGSET(SCENE_MODE_DUSK_DAWN, s5k5ccgx_scene_dawn),
		S5K5CCGX_REGSET(SCENE_MODE_TEXT, s5k5ccgx_scene_text),
		S5K5CCGX_REGSET(SCENE_MODE_CANDLE_LIGHT, s5k5ccgx_scene_candle),
	},
	.saturation = {
		S5K5CCGX_REGSET(SATURATION_MINUS_2, s5k5ccgx_saturation_m_2),
		S5K5CCGX_REGSET(SATURATION_MINUS_1, s5k5ccgx_saturation_m_1),
		S5K5CCGX_REGSET(SATURATION_DEFAULT, s5k5ccgx_saturation_0),
		S5K5CCGX_REGSET(SATURATION_PLUS_1, s5k5ccgx_saturation_p_1),
		S5K5CCGX_REGSET(SATURATION_PLUS_2, s5k5ccgx_saturation_p_2),
	},
	.contrast = {
		S5K5CCGX_REGSET(CONTRAST_MINUS_2, s5k5ccgx_contrast_m_2),
		S5K5CCGX_REGSET(CONTRAST_MINUS_1, s5k5ccgx_contrast_m_1),
		S5K5CCGX_REGSET(CONTRAST_DEFAULT, s5k5ccgx_contrast_0),
		S5K5CCGX_REGSET(CONTRAST_PLUS_1, s5k5ccgx_contrast_p_1),
		S5K5CCGX_REGSET(CONTRAST_PLUS_2, s5k5ccgx_contrast_p_2),

	},
	.sharpness = {
		S5K5CCGX_REGSET(SHARPNESS_MINUS_2, s5k5ccgx_sharpness_m_2),
		S5K5CCGX_REGSET(SHARPNESS_MINUS_1, s5k5ccgx_sharpness_m_1),
		S5K5CCGX_REGSET(SHARPNESS_DEFAULT, s5k5ccgx_sharpness_0),
		S5K5CCGX_REGSET(SHARPNESS_PLUS_1, s5k5ccgx_sharpness_p_1),
		S5K5CCGX_REGSET(SHARPNESS_PLUS_2, s5k5ccgx_sharpness_p_2),
	},
	.fps = {
		S5K5CCGX_REGSET(I_FPS_0, s5k5ccgx_fps_auto),
		S5K5CCGX_REGSET(I_FPS_15, s5k5ccgx_fps_15fix),
		S5K5CCGX_REGSET(I_FPS_25, s5k5ccgx_fps_25fix),
		S5K5CCGX_REGSET(I_FPS_30, s5k5ccgx_fps_30fix),
	},
	.preview_return = S5K5CCGX_REGSET_TABLE(s5k5ccgx_preview_return),

	.flash_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_mainflash_start),
	.flash_end = S5K5CCGX_REGSET_TABLE(s5k5ccgx_mainflash_end),
	.af_pre_flash_start =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_preflash_start),
	.af_pre_flash_end =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_preflash_end),
	.flash_ae_set =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_flash_ae_set),
	.flash_ae_clear =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_flash_ae_clear),
	.ae_lock_on =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_ae_lock),
	.ae_lock_off =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_ae_unlock),
	.awb_lock_on =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_awb_lock),
	.awb_lock_off =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_awb_unlock),

	.restore_cap = S5K5CCGX_REGSET_TABLE(s5k5ccgx_restore_capture_reg),
	.change_wide_cap = S5K5CCGX_REGSET_TABLE(s5k5ccgx_change_wide_cap),
#ifdef CONFIG_VIDEO_S5K5CCGX_P8
	.set_lowlight_cap = S5K5CCGX_REGSET_TABLE(s5k5ccgx_set_lowlight_reg),
#endif

	.af_macro_mode = S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_macro_on),
	.af_normal_mode = S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_normal_on),
#if !defined(CONFIG_VIDEO_S5K5CCGX_P2)
	.af_night_normal_mode =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_night_normal_on),
#endif
#ifdef CONFIG_VIDEO_S5K5CCGX_P4W
	.af_off = S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_off_reg),
#endif
	.hd_af_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_720P_af_do),
	.hd_first_af_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_1st_720P_af_do),
	.single_af_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_do),
	.capture_start = {
		S5K5CCGX_REGSET(S5K5CCGX_CAPTURE_VGA, s5k5ccgx_snapshot_vga),
		S5K5CCGX_REGSET(S5K5CCGX_CAPTURE_W2MP, s5k5ccgx_snapshot),
		S5K5CCGX_REGSET(S5K5CCGX_CAPTURE_3MP, s5k5ccgx_snapshot),
	},
	.init_reg = S5K5CCGX_REGSET_TABLE(s5k5ccgx_init_reg),
	.get_esd_status = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_esd_reg),
	.stream_stop = S5K5CCGX_REGSET_TABLE(s5k5ccgx_stream_stop_reg),
	.get_light_level = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_light_status),
	.get_iso = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_iso_reg),
	.get_ae_stable = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_ae_stable_reg),
	.get_shutterspeed =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_shutterspeed_reg),
	.update_preview = S5K5CCGX_REGSET_TABLE(s5k5ccgx_update_preview_reg),
	.update_hd_preview =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_update_hd_preview_reg),
#ifdef CONFIG_VIDEO_S5K5CCGX_P8
	.antibanding = S5K5CCGX_REGSET_TABLE(S5K5CCGX_ANTIBANDING_REG),
#endif
};

static const struct v4l2_mbus_framefmt capture_fmts[] = {
	{
		.code		= V4L2_MBUS_FMT_FIXED,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
};

#ifdef CONFIG_LOAD_FILE
static int loadFile(void)
{
	struct file *fp = NULL;
	struct test *nextBuf = NULL;

	u8 *nBuf = NULL;
	size_t file_size = 0, max_size = 0, testBuf_size = 0;
	ssize_t nread = 0;
	s32 check = 0, starCheck = 0;
	s32 tmp_large_file = 0;
	s32 i = 0;
	int ret = 0;
	loff_t pos;

	mm_segment_t fs = get_fs();
	set_fs(get_ds());

	cam_info("%s: E\n", __func__);

	BUG_ON(testBuf);

	fp = filp_open(TUNING_FILE_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_err("file open error\n");
		return PTR_ERR(fp);
	}

	file_size = (size_t) fp->f_path.dentry->d_inode->i_size;
	max_size = file_size;

	cam_dbg("file_size = %d\n", file_size);

	nBuf = kmalloc(file_size, GFP_ATOMIC);
	if (nBuf == NULL) {
		cam_dbg("Fail to 1st get memory\n");
		nBuf = vmalloc(file_size);
		if (nBuf == NULL) {
			cam_err("ERR: nBuf Out of Memory\n");
			ret = -ENOMEM;
			goto error_out;
		}
		tmp_large_file = 1;
	}

	testBuf_size = sizeof(struct test) * file_size;
	if (tmp_large_file) {
		testBuf = (struct test *)vmalloc(testBuf_size);
		large_file = 1;
	} else {
		testBuf = kmalloc(testBuf_size, GFP_ATOMIC);
		if (testBuf == NULL) {
			cam_dbg("Fail to get mem(%d bytes)\n", testBuf_size);
			testBuf = (struct test *)vmalloc(testBuf_size);
			large_file = 1;
		}
	}
	if (testBuf == NULL) {
		cam_err("ERR: Out of Memory\n");
		ret = -ENOMEM;
		goto error_out;
	}

	pos = 0;
	memset(nBuf, 0, file_size);
	memset(testBuf, 0, file_size * sizeof(struct test));

	nread = vfs_read(fp, (char __user *)nBuf, file_size, &pos);
	if (nread != file_size) {
		cam_err("failed to read file ret = %d\n", nread);
		ret = -1;
		goto error_out;
	}

	set_fs(fs);

	i = max_size;

	printk(KERN_DEBUG "i = %d\n", i);

	while (i) {
		testBuf[max_size - i].data = *nBuf;
		if (i != 1) {
			testBuf[max_size - i].nextBuf =
				&testBuf[max_size - i + 1];
		} else {
			testBuf[max_size - i].nextBuf = NULL;
			break;
		}
		i--;
		nBuf++;
	}

	i = max_size;
	nextBuf = &testBuf[0];

#if 1
	while (i - 1) {
		if (!check && !starCheck) {
			if (testBuf[max_size - i].data == '/') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data
								== '/') {
						check = 1;/* when find '//' */
						i--;
					} else if (
					    testBuf[max_size-i].nextBuf->data
					    == '*') {
						/* when find '/ *' */
						starCheck = 1;
						i--;
					}
				} else
					break;
			}
			if (!check && !starCheck) {
				/* ignore '\t' */
				if (testBuf[max_size - i].data != '\t') {
					nextBuf->nextBuf = &testBuf[max_size-i];
					nextBuf = &testBuf[max_size - i];
				}
			}
		} else if (check && !starCheck) {
			if (testBuf[max_size - i].data == '/') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data
					    == '*') {
						/* when find '/ *' */
						starCheck = 1;
						check = 0;
						i--;
					}
				} else
					break;
			}

			 /* when find '\n' */
			if (testBuf[max_size - i].data == '\n' && check) {
				check = 0;
				nextBuf->nextBuf = &testBuf[max_size - i];
				nextBuf = &testBuf[max_size - i];
			}

		} else if (!check && starCheck) {
			if (testBuf[max_size - i].data == '*') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data
					    == '/') {
						/* when find '* /' */
						starCheck = 0;
						i--;
					}
				} else
					break;
			}
		}

		i--;

		if (i < 2) {
			nextBuf = NULL;
			break;
		}

		if (testBuf[max_size - i].nextBuf == NULL) {
			nextBuf = NULL;
			break;
		}
	}
#endif

#if 0 /* for print */
	cam_dbg("i = %d\n", i);
	nextBuf = &testBuf[0];
	while (1) {
		if (nextBuf->nextBuf == NULL)
			break;
		cam_dbg("%c", nextBuf->data);
		nextBuf = nextBuf->nextBuf;
	}
#endif

error_out:
	tmp_large_file ? vfree(nBuf) : kfree(nBuf);

	if (fp)
		filp_close(fp, current->files);
	return ret;
}


#endif

/**
 * s5k5ccgx_i2c_read_twobyte: Read 2 bytes from sensor
 */
static int s5k5ccgx_i2c_read_twobyte(struct i2c_client *client,
				  u16 subaddr, u16 *data)
{
	int err;
	u8 buf[2];
	struct i2c_msg msg[2];

	cpu_to_be16s(&subaddr);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = (u8 *)&subaddr;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = buf;

	err = i2c_transfer(client->adapter, msg, 2);
	CHECK_ERR_COND_MSG(err != 2, -EIO, "fail to read register\n");

	*data = ((buf[0] << 8) | buf[1]);

	return 0;
}

/**
 * s5k5ccgx_i2c_write_twobyte: Write (I2C) multiple bytes to the camera sensor
 * @client: pointer to i2c_client
 * @cmd: command register
 * @w_data: data to be written
 * @w_len: length of data to be written
 *
 * Returns 0 on success, <0 on error
 */
static int s5k5ccgx_i2c_write_twobyte(struct i2c_client *client,
					 u16 addr, u16 w_data)
{
	int retry_count = 5;
	int ret = 0;
	u8 buf[4] = {0,};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 4,
		.buf	= buf,
	};

	buf[0] = addr >> 8;
	buf[1] = addr;
	buf[2] = w_data >> 8;
	buf[3] = w_data & 0xff;

#if (0)
	s5k5ccgx_debug(S5K5CCGX_DEBUG_I2C, "%s : W(0x%02X%02X%02X%02X)\n",
		__func__, buf[0], buf[1], buf[2], buf[3]);
#else
	/* cam_dbg("I2C writing: 0x%02X%02X%02X%02X\n",
			buf[0], buf[1], buf[2], buf[3]); */
#endif

	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
			break;
		msleep(POLL_TIME_MS);
		cam_err("%s: ERROR(%d), write (%04X, %04X), retry %d.\n",
				__func__, ret, addr, w_data, retry_count);
	} while (retry_count-- > 0);

	CHECK_ERR_COND_MSG(ret != 1, -EIO, "I2C does not working.\n\n");

	return 0;
}

/* PX: */
#ifdef CONFIG_LOAD_FILE
static int s5k5ccgx_write_regs_from_sd(struct v4l2_subdev *sd,
						const u8 s_name[])
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct test *tempData = NULL;

	int ret = -EAGAIN;
	u32 temp;
	u32 delay = 0;
	u8 data[11];
	s32 searched = 0;
	size_t size = strlen(s_name);
	s32 i;
#ifdef DEBUG_WRITE_REGS
	u8 regs_name[128] = {0,};

	BUG_ON(size > sizeof(regs_name));
#endif

	cam_dbg("%s: E size = %d, string = %s\n", __func__, size, s_name);
	tempData = &testBuf[0];
	while (!searched) {
		searched = 1;
		for (i = 0; i < size; i++) {
			if (tempData->data != s_name[i]) {
				searched = 0;
				break;
			}
#ifdef DEBUG_WRITE_REGS
			regs_name[i] = tempData->data;
#endif
			tempData = tempData->nextBuf;
		}
#ifdef DEBUG_WRITE_REGS
		if (i > 9) {
			regs_name[i] = '\0';
			cam_dbg("Searching: regs_name = %s\n", regs_name);
		}
#endif
		tempData = tempData->nextBuf;
	}
	/* structure is get..*/
#ifdef DEBUG_WRITE_REGS
	regs_name[i] = '\0';
	cam_dbg("Searched regs_name = %s\n\n", regs_name);
#endif

	while (1) {
		if (tempData->data == '{')
			break;
		else
			tempData = tempData->nextBuf;
	}

	while (1) {
		searched = 0;
		while (1) {
			if (tempData->data == 'x') {
				/* get 10 strings.*/
				data[0] = '0';
				for (i = 1; i < 11; i++) {
					data[i] = tempData->data;
					tempData = tempData->nextBuf;
				}
				/*cam_dbg("%s\n", data);*/
				temp = simple_strtoul(data, NULL, 16);
				break;
			} else if (tempData->data == '}') {
				searched = 1;
				break;
			} else
				tempData = tempData->nextBuf;

			if (tempData->nextBuf == NULL)
				return -1;
		}

		if (searched)
			break;

		if ((temp & S5K5CCGX_DELAY) == S5K5CCGX_DELAY) {
			delay = temp & 0x0FFFF;
			debug_msleep(sd, delay);
			continue;
		}

		/* cam_dbg("I2C writing: 0x%08X,\n",temp);*/
		ret = s5k5ccgx_i2c_write_twobyte(client,
			(temp >> 16), (u16)temp);

		/* In error circumstances */
		/* Give second shot */
		if (unlikely(ret)) {
			dev_info(&client->dev,
					"s5k5ccgx i2c retry one more time\n");
			ret = s5k5ccgx_i2c_write_twobyte(client,
				(temp >> 16), (u16)temp);

			/* Give it one more shot */
			if (unlikely(ret)) {
				dev_info(&client->dev,
						"s5k5ccgx i2c retry twice\n");
				ret = s5k5ccgx_i2c_write_twobyte(client,
					(temp >> 16), (u16)temp);
			}
		}
	}

	return ret;
}
#endif

/* Write register
 * If success, return value: 0
 * If fail, return value: -EIO
 */
static int s5k5ccgx_write_regs(struct v4l2_subdev *sd, const u32 regs[],
			     int size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 delay = 0;
	int i, err = 0;

	for (i = 0; i < size; i++) {
		if ((regs[i] & S5K5CCGX_DELAY) == S5K5CCGX_DELAY) {
			delay = regs[i] & 0xFFFF;
			debug_msleep(sd, delay);
			continue;
		}

		err = s5k5ccgx_i2c_write_twobyte(client,
			(regs[i] >> 16), regs[i]);
		CHECK_ERR_MSG(err, "write registers\n")
	}

	return 0;
}

#if 0
static int s5k5ccgx_i2c_write_block(struct v4l2_subdev *sd, u8 *buf, u32 size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int retry_count = 5;
	int ret = 0;
	struct i2c_msg msg = {client->addr, 0, size, buf};

#ifdef CONFIG_VIDEO_S5K5CCGX_DEBUG
	if (s5k5ccgx_debug_mask & S5K5CCGX_DEBUG_I2C_BURSTS) {
		if ((buf[0] == 0x0F) && (buf[1] == 0x12))
			pr_info("%s : data[0,1] = 0x%02X%02X,"
				" total data size = %d\n",
				__func__, buf[2], buf[3], size-2);
		else
			pr_info("%s : 0x%02X%02X%02X%02X\n",
				__func__, buf[0], buf[1], buf[2], buf[3]);
	}
#endif

	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
			break;
		msleep(POLL_TIME_MS);
	} while (retry_count-- > 0);
	if (ret != 1) {
		dev_err(&client->dev, "%s: I2C is not working.\n", __func__);
		return -EIO;
	}

	return 0;
}
#endif

#define BURST_MODE_BUFFER_MAX_SIZE 2700
u8 s5k5ccgx_burstmode_buf[BURST_MODE_BUFFER_MAX_SIZE];

/* PX: */
static int s5k5ccgx_burst_write_regs(struct v4l2_subdev *sd,
			const u32 list[], u32 size, char *name)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = -EINVAL;
	int i = 0, idx = 0;
	u16 subaddr = 0, next_subaddr = 0, value = 0;
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 0,
		.buf	= s5k5ccgx_burstmode_buf,
	};

	cam_trace("E\n");

	for (i = 0; i < size; i++) {
		CHECK_ERR_COND_MSG((idx > (BURST_MODE_BUFFER_MAX_SIZE - 10)),
			err, "BURST MOD buffer overflow!\n")

		subaddr = (list[i] & 0xFFFF0000) >> 16;
		if (subaddr == 0x0F12)
			next_subaddr = (list[i+1] & 0xFFFF0000) >> 16;

		value = list[i] & 0x0000FFFF;

		switch (subaddr) {
		case 0x0F12:
			/* make and fill buffer for burst mode write. */
			if (idx == 0) {
				s5k5ccgx_burstmode_buf[idx++] = 0x0F;
				s5k5ccgx_burstmode_buf[idx++] = 0x12;
			}
			s5k5ccgx_burstmode_buf[idx++] = value >> 8;
			s5k5ccgx_burstmode_buf[idx++] = value & 0xFF;

			/* write in burstmode*/
			if (next_subaddr != 0x0F12) {
				msg.len = idx;
				err = i2c_transfer(client->adapter,
					&msg, 1) == 1 ? 0 : -EIO;
				CHECK_ERR_MSG(err, "i2c_transfer\n");
				/* cam_dbg("s5k5ccgx_sensor_burst_write,
						idx = %d\n", idx); */
				idx = 0;
			}
			break;

		case 0xFFFF:
			debug_msleep(sd, value);
			break;

		default:
			idx = 0;
			err = s5k5ccgx_i2c_write_twobyte(client,
						subaddr, value);
			CHECK_ERR_MSG(err, "i2c_write_twobytes\n");
			break;
		}
	}

	return 0;
}

/* PX: */
static int s5k5ccgx_set_from_table(struct v4l2_subdev *sd,
				const char *setting_name,
				const struct s5k5ccgx_regset_table *table,
				u32 table_size, s32 index)
{
	int err = 0;

	/* cam_dbg("%s: set %s index %d\n",
		__func__, setting_name, index); */
	CHECK_ERR_COND_MSG(((index < 0) || (index >= table_size)),
		-EINVAL, "index(%d) out of range[0:%d] for table for %s\n",
		index, table_size, setting_name);

	table += index;
	CHECK_ERR_COND_MSG(!table->reg, -EFAULT, \
		"table=%s, index=%d, reg = NULL\n", setting_name, index);

#ifdef CONFIG_LOAD_FILE
	cam_dbg("%s: \"%s\", reg_name=%s\n", __func__,
			setting_name, table->name);
	return s5k5ccgx_write_regs_from_sd(sd, table->name);

#else /* CONFIG_LOAD_FILE */

# ifdef DEBUG_WRITE_REGS
	cam_dbg("%s: \"%s\", reg_name=%s\n", __func__,
			setting_name, table->name);
# endif /* DEBUG_WRITE_REGS */

	err = s5k5ccgx_write_regs(sd, table->reg, table->array_size);
	CHECK_ERR_MSG(err, "write regs(%s), err=%d\n", setting_name, err);

	return 0;
#endif /* CONFIG_LOAD_FILE */
}

/* PX: */
static inline int s5k5ccgx_save_ctrl(struct v4l2_subdev *sd,
					struct v4l2_control *ctrl)
{
	int ctrl_cnt = ARRAY_SIZE(s5k5ccgx_ctrls);
	int i;

	/* cam_trace("E, Ctrl-ID = 0x%X", ctrl->id);*/

	for (i = 0; i < ctrl_cnt; i++) {
		if (ctrl->id == s5k5ccgx_ctrls[i].id) {
			s5k5ccgx_ctrls[i].value = ctrl->value;
			break;
		}
	}

	if (unlikely(i >= ctrl_cnt))
		cam_trace("WARNING, not saved ctrl-ID=0x%X\n", ctrl->id);

	return 0;
}

/**
 * s5k5ccgx_is_hwflash_on - check whether flash device is on
 *
 * Refer to state->flash_on to check whether flash is in use in driver.
 */
static inline int s5k5ccgx_is_hwflash_on(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);

#ifdef S5K5CCGX_SUPPORT_FLASH
	return state->pdata->is_flash_on();
#else
	return 0;
#endif
}

/**
 * s5k5ccgx_flash_en - contro Flash LED
 * @mode: S5K5CCGX_FLASH_MODE_NORMAL or S5K5CCGX_FLASH_MODE_MOVIE
 * @onoff: S5K5CCGX_FLASH_ON or S5K5CCGX_FLASH_OFF
 */
static int s5k5ccgx_flash_en(struct v4l2_subdev *sd, s32 mode, s32 onoff)
{
	struct s5k5ccgx_state *state = to_state(sd);

	if (unlikely(state->ignore_flash)) {
		cam_warn("WARNING, we ignore flash command.\n");
		return 0;
	}

#ifdef S5K5CCGX_SUPPORT_FLASH
	return state->pdata->flash_en(mode, onoff);
#endif
	return 0;
}

/**
 * s5k5ccgx_flash_torch - turn flash on/off as torch for preflash, recording
 * @onoff: S5K5CCGX_FLASH_ON or S5K5CCGX_FLASH_OFF
 *
 * This func set state->flash_on properly.
 */
static inline int s5k5ccgx_flash_torch(struct v4l2_subdev *sd, s32 onoff)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = 0;

	err = s5k5ccgx_flash_en(sd, S5K5CCGX_FLASH_MODE_MOVIE, onoff);
	state->flash_on = (onoff == S5K5CCGX_FLASH_ON) ? 1 : 0;

	return err;
}

/**
 * s5k5ccgx_flash_oneshot - turn main flash on for capture
 * @onoff: S5K5CCGX_FLASH_ON or S5K5CCGX_FLASH_OFF
 *
 * Main flash is turn off automatically in some milliseconds.
 */
static inline int s5k5ccgx_flash_oneshot(struct v4l2_subdev *sd, s32 onoff)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = 0;

	err = s5k5ccgx_flash_en(sd, S5K5CCGX_FLASH_MODE_NORMAL, onoff);

	/* The flash_on here is only used for EXIF */
	state->flash_on = (onoff == S5K5CCGX_FLASH_ON) ? 1 : 0;

	return err;
}

/* PX: Set scene mode */
static int s5k5ccgx_set_scene_mode(struct v4l2_subdev *sd, s32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -ENODEV;

	cam_trace("E, value %d\n", val);

	if (state->scene_mode == val)
		return 0;

	/* when scene mode is switched,
	 * we frist have to write scene_off.
	 */
	if (state->scene_mode != SCENE_MODE_NONE)
		err = s5k5ccgx_set_from_table(sd, "scene_mode",
			state->regs->scene_mode,
			ARRAY_SIZE(state->regs->scene_mode), SCENE_MODE_NONE);

	if (val != SCENE_MODE_NONE)
		err = s5k5ccgx_set_from_table(sd, "scene_mode",
			state->regs->scene_mode,
			ARRAY_SIZE(state->regs->scene_mode), val);

	state->scene_mode = val;

	cam_trace("X\n");
	return 0;
}

/* PX: Set brightness */
static int s5k5ccgx_set_exposure(struct v4l2_subdev *sd, s32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

	if ((val < EV_MINUS_4) || (val > EV_PLUS_4)) {
		cam_err("%s: ERROR, invalid value(%d)\n", __func__, val);
		return -EINVAL;
	}

	err = s5k5ccgx_set_from_table(sd, "brightness", state->regs->ev,
		ARRAY_SIZE(state->regs->ev), GET_EV_INDEX(val));

	return err;
}

/* PX: Check light level */
static u32 s5k5ccgx_get_light_level(struct v4l2_subdev *sd, u32 *light_level)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k5ccgx_state *state = to_state(sd);
	u16 val_lsb = 0;
	u16 val_msb = 0;
	int err = -ENODEV;

	err = s5k5ccgx_set_from_table(sd, "get_light_level",
			&state->regs->get_light_level, 1, 0);
	CHECK_ERR_MSG(err, "fail to get light level\n");

	err = s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &val_lsb);
	err = s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &val_msb);
	CHECK_ERR_MSG(err, "fail to read light level\n");

	*light_level = val_lsb | (val_msb<<16);

	/* cam_trace("X, light level = 0x%X", *light_level); */

	return 0;
}

static int s5k5ccgx_set_capture_size(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

	if (likely(!state->wide_cmd))
		return 0;

	cam_err("%s: WARNING, reconfiguring sensor register.\n\n", __func__);

	switch (state->wide_cmd) {
	case WIDE_REQ_CHANGE:
		cam_info("%s: Wide Capture setting\n", __func__);
		err = s5k5ccgx_set_from_table(sd, "change_wide_cap",
			&state->regs->change_wide_cap, 1, 0);
		break;

	case WIDE_REQ_RESTORE:
		cam_info("%s: Restore capture setting\n", __func__);
		err = s5k5ccgx_set_from_table(sd, "restore_capture",
				&state->regs->restore_cap, 1, 0);
		break;

	default:
		cam_err("%s: WARNING, invalid argument(%d)\n",
				__func__, state->wide_cmd);
		break;
	}

	/* Don't forget the below codes.
	 * We set here state->preview to NULL after reconfiguring
	 * capure config if capture ratio does't match with preview ratio.
	 */
	state->preview = NULL;
	CHECK_ERR(err);

	return 0;
}

/* PX: Set sensor mode */
static int s5k5ccgx_set_sensor_mode(struct v4l2_subdev *sd, s32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);

	state->hd_videomode = 0;

	switch (val) {
	case SENSOR_MOVIE:
		/* We does not support movie mode when in VT. */
		if (state->vt_mode) {
			state->sensor_mode = SENSOR_CAMERA;
			cam_err("%s: ERROR, Not support movie\n", __func__);
			break;
		}
		/* We do not break. */

	case SENSOR_CAMERA:
		state->sensor_mode = val;
		break;

	case 2:	/* 720p HD video mode */
		state->sensor_mode = SENSOR_MOVIE;
		state->hd_videomode = 1;
		break;

	default:
		cam_err("%s: ERROR, Not support.(%d)\n", __func__, val);
		state->sensor_mode = SENSOR_CAMERA;
		WARN_ON(1);
		break;
	}

	return 0;
}

/* PX: Set framerate */
static int s5k5ccgx_set_frame_rate(struct v4l2_subdev *sd, s32 fps)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EIO;
	int i = 0, fps_index = -1;

	cam_info("set frame rate %d\n", fps);

	for (i = 0; i < ARRAY_SIZE(s5k5ccgx_framerates); i++) {
		if (fps == s5k5ccgx_framerates[i].fps) {
			fps_index = s5k5ccgx_framerates[i].index;
			state->fps = fps;
			state->req_fps = -1;
			break;
		}
	}

	if (unlikely(fps_index < 0)) {
		cam_err("%s: WARNING, Not supported FPS(%d)\n", __func__, fps);
		return 0;
	}

	if (!state->hd_videomode) {
		err = s5k5ccgx_set_from_table(sd, "fps", state->regs->fps,
				ARRAY_SIZE(state->regs->fps), fps_index);
		CHECK_ERR_MSG(err, "fail to set framerate\n")
	}

	return 0;
}

static int s5k5ccgx_set_ae_lock(struct v4l2_subdev *sd, s32 val, bool force)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = 0;

	switch (val) {
	case AE_LOCK:
		if (state->focus.touch)
			return 0;

		err = s5k5ccgx_set_from_table(sd, "ae_lock_on",
				&state->regs->ae_lock_on, 1, 0);
		WARN_ON(state->focus.ae_lock);
		state->focus.ae_lock = 1;
		break;

	case AE_UNLOCK:
		if (unlikely(!force && !state->focus.ae_lock))
			return 0;

		err = s5k5ccgx_set_from_table(sd, "ae_lock_off",
				&state->regs->ae_lock_off, 1, 0);
		state->focus.ae_lock = 0;
		break;

	default:
		cam_err("%s: WARNING, invalid argument(%d)\n", __func__, val);
	}

	CHECK_ERR_MSG(err, "fail to lock AE(%d), err=%d\n", val, err);

	return 0;
}

static int s5k5ccgx_set_awb_lock(struct v4l2_subdev *sd, s32 val, bool force)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = 0;

	switch (val) {
	case AWB_LOCK:
		if (state->flash_on ||
		    (state->wb_mode != WHITE_BALANCE_AUTO))
			return 0;

		err = s5k5ccgx_set_from_table(sd, "awb_lock_on",
				&state->regs->awb_lock_on, 1, 0);
		WARN_ON(state->focus.awb_lock);
		state->focus.awb_lock = 1;
		break;

	case AWB_UNLOCK:
		if (unlikely(!force && !state->focus.awb_lock))
			return 0;

		err = s5k5ccgx_set_from_table(sd, "awb_lock_off",
			&state->regs->awb_lock_off, 1, 0);
		state->focus.awb_lock = 0;
		break;

	default:
		cam_err("%s: WARNING, invalid argument(%d)\n", __func__, val);
	}

	CHECK_ERR_MSG(err, "fail to lock AWB(%d), err=%d\n", val, err);

	return 0;
}

/* PX: Set AE, AWB Lock */
static int s5k5ccgx_set_lock(struct v4l2_subdev *sd, s32 lock, bool force)
{
	int err = -EIO;

	cam_trace("%s\n", lock ? "on" : "off");
	if (unlikely((u32)lock >= AEAWB_LOCK_MAX)) {
		cam_err("%s: ERROR, invalid argument\n", __func__);
		return -EINVAL;
	}

	err = s5k5ccgx_set_ae_lock(sd, (lock == AEAWB_LOCK) ?
				AE_LOCK : AE_UNLOCK, force);
	if (unlikely(err))
		goto out_err;

	err = s5k5ccgx_set_awb_lock(sd, (lock == AEAWB_LOCK) ?
				AWB_LOCK : AWB_UNLOCK, force);
	if (unlikely(err))
		goto out_err;

	cam_trace("X\n");
	return 0;

out_err:
	cam_err("%s: ERROR, failed to set lock\n", __func__);
	return err;
}

#ifdef CONFIG_VIDEO_S5K5CCGX_P4W
static int s5k5ccgx_set_af_softlanding(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_trace("E\n");

	err = s5k5ccgx_set_from_table(sd, "af_off",
			&state->regs->af_off, 1, 0);
	CHECK_ERR_MSG(err, "fail to set softlanding\n");

	cam_trace("X\n");
	return 0;
}
#endif

/* PX: */
static int s5k5ccgx_return_focus(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_trace("E\n");

	switch (state->focus.mode) {
	case FOCUS_MODE_MACRO:
		err = s5k5ccgx_set_from_table(sd, "af_macro_mode",
				&state->regs->af_macro_mode, 1, 0);
		break;

	default:
#if !defined(CONFIG_VIDEO_S5K5CCGX_P2)
		if (state->scene_mode == SCENE_MODE_NIGHTSHOT)
			err = s5k5ccgx_set_from_table(sd,
				"af_night_normal_mode",
				&state->regs->af_night_normal_mode, 1, 0);
		else
#endif
			err = s5k5ccgx_set_from_table(sd,
				"af_norma_mode",
				&state->regs->af_normal_mode, 1, 0);
		break;
	}

	CHECK_ERR(err);
	return 0;
}

#ifdef DEBUG_FILTER_DATA
static void __used s5k5ccgx_display_AF_win_info(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k5ccgx_rect first_win = {0, 0, 0, 0};
	struct s5k5ccgx_rect second_win = {0, 0, 0, 0};

	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x022C);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, (u16 *)&first_win.x);
	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x022E);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, (u16 *)&first_win.y);
	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0230);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, (u16 *)&first_win.width);
	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0232);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, (u16 *)&first_win.height);

	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0234);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, (u16 *)&second_win.x);
	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0236);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, (u16 *)&second_win.y);
	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0238);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, (u16 *)&second_win.width);
	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x023A);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, (u16 *)&second_win.height);

	cam_info("------- AF Window info -------\n");
	cam_info("Firtst Window: (%4d %4d %4d %4d)\n",
		first_win.x, first_win.y, first_win.width, first_win.height);
	cam_info("Second Window: (%4d %4d %4d %4d)\n",
		second_win.x, second_win.y,
		second_win.width, second_win.height);
	cam_info("------- AF Window info -------\n\n");
}
#endif

/* PX: Prepare AF Flash */
static int s5k5ccgx_af_start_preflash(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k5ccgx_state *state = to_state(sd);
	u16 read_value = 0;
	int count = 0;
	int err = 0;

	cam_trace("E\n");

	if (state->sensor_mode == SENSOR_MOVIE)
		return 0;

	cam_dbg("Start SINGLE AF, flash mode %d\n", state->flash_mode);

	/* in case user calls auto_focus repeatedly without a cancel
	 * or a capture, we need to cancel here to allow ae_awb
	 * to work again, or else we could be locked forever while
	 * that app is running, which is not the expected behavior.
	 */
	err = s5k5ccgx_set_lock(sd, AEAWB_UNLOCK, true);
	CHECK_ERR_MSG(err, "fail to set lock\n");

	state->focus.preflash = PREFLASH_OFF;
	state->light_level = 0xFFFFFFFF;

	s5k5ccgx_get_light_level(sd, &state->light_level);

	switch (state->flash_mode) {
	case FLASH_MODE_AUTO:
		if (state->light_level >= FLASH_LOW_LIGHT_LEVEL) {
			/* flash not needed */
			break;
		}

	case FLASH_MODE_ON:
		s5k5ccgx_set_from_table(sd, "af_pre_flash_start",
			&state->regs->af_pre_flash_start, 1, 0);
		s5k5ccgx_set_from_table(sd, "flash_ae_set",
			&state->regs->flash_ae_set, 1, 0);
		s5k5ccgx_flash_torch(sd, S5K5CCGX_FLASH_ON);
		state->focus.preflash = PREFLASH_ON;
		break;

	case FLASH_MODE_OFF:
		if (state->light_level < FLASH_LOW_LIGHT_LEVEL)
			state->one_frame_delay_ms = ONE_FRAME_DELAY_MS_LOW;
		break;

	default:
		break;
	}

	/* Check AE-stable */
	if (state->focus.preflash == PREFLASH_ON) {
		/* We wait for 200ms after pre flash on.
		 * check whether AE is stable.*/
		msleep(200);

		/* Do checking AE-stable */
		for (count = 0; count < AE_STABLE_SEARCH_COUNT; count++) {
			if (state->focus.cancel) {
				cam_info("af_start_preflash: \
					AF is cancelled!\n");
				state->focus.status = AF_RESULT_CANCELLED;
				break;
			}

			s5k5ccgx_set_from_table(sd, "get_ae_stable",
					&state->regs->get_ae_stable, 1, 0);
			s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);

			/* af_dbg("Check AE-Stable: 0x%04X\n", read_value); */
			if (read_value == 0x0001) {
				af_dbg("AE-stable success,"
					" count=%d, delay=%dms\n", count,
					AE_STABLE_SEARCH_DELAY);
				break;
			}

			msleep(AE_STABLE_SEARCH_DELAY);
		}

		/* restore write mode */
		s5k5ccgx_i2c_write_twobyte(client, 0x0028, 0x7000);

		if (unlikely(count >= AE_STABLE_SEARCH_COUNT)) {
			cam_err("%s: ERROR, AE unstable."
				" count=%d, delay=%dms\n",
				__func__, count, AE_STABLE_SEARCH_DELAY);
			/* return -ENODEV; */
		}
	} else if (state->focus.cancel) {
		cam_info("af_start_preflash: AF is cancelled!\n");
		state->focus.status = AF_RESULT_CANCELLED;
	}

	/* If AF cancel, finish pre-flash process. */
	if (state->focus.status == AF_RESULT_CANCELLED) {
		if (state->focus.preflash == PREFLASH_ON) {
			s5k5ccgx_set_from_table(sd, "af_pre_flash_end",
				&state->regs->af_pre_flash_end, 1, 0);
			s5k5ccgx_set_from_table(sd, "flash_ae_clear",
				&state->regs->flash_ae_clear, 1, 0);
			s5k5ccgx_flash_torch(sd, S5K5CCGX_FLASH_OFF);
			state->focus.preflash = PREFLASH_NONE;
		}

		state->focus.cancel = 0;
		if (state->focus.touch)
			state->focus.touch = 0;
	}

	cam_trace("X\n");

	return 0;
}

/* PX: Do AF */
static int s5k5ccgx_do_af(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k5ccgx_state *state = to_state(sd);
	u16 read_value = 0;
	u32 count = 0;

	cam_trace("E\n");

	/* AE, AWB Lock */
	s5k5ccgx_set_lock(sd, AEAWB_LOCK, false);

	if (state->sensor_mode == SENSOR_MOVIE) {
		s5k5ccgx_set_from_table(sd, "hd_af_start",
			&state->regs->hd_af_start, 1, 0);

		cam_info("%s : 720P Auto Focus Operation\n\n", __func__);
	} else
		s5k5ccgx_set_from_table(sd, "single_af_start",
			&state->regs->single_af_start, 1, 0);

	/* Sleep while 2frame */
	if (state->hd_videomode)
		msleep(100); /* 100ms */
	else if (state->scene_mode == SCENE_MODE_NIGHTSHOT)
		msleep(ONE_FRAME_DELAY_MS_NIGHTMODE * 2); /* 330ms */
	else
		msleep(ONE_FRAME_DELAY_MS_LOW * 2); /* 200ms */

	/* AF Searching */
	cam_dbg("AF 1st search\n");

	/*1st search*/
	for (count = 0; count < FIRST_AF_SEARCH_COUNT; count++) {
		if (state->focus.cancel) {
			cam_dbg("do_af: AF is cancelled while doing(1st)\n");
			state->focus.status = AF_RESULT_CANCELLED;
			goto check_done;
		}

		read_value = 0x0;
		s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
		s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x2D12);
		s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);
		af_dbg("1st AF status(%02d) = 0x%04X\n",
					count, read_value);
		if (read_value != 0x01)
			break;

		msleep(AF_SEARCH_DELAY);
	}

	if (read_value != 0x02) {
		cam_err("%s: ERROR, 1st AF failed. count=%d, read_val=0x%X\n\n",
					__func__, count, read_value);
		state->focus.status = AF_RESULT_FAILED;
		goto check_done;
	}

	/*2nd search*/
	cam_dbg("AF 2nd search\n");
	for (count = 0; count < SECOND_AF_SEARCH_COUNT; count++) {
		if (state->focus.cancel) {
			cam_dbg("do_af: AF is cancelled while doing(2nd)\n");
			state->focus.status = AF_RESULT_CANCELLED;
			goto check_done;
		}

		msleep(AF_SEARCH_DELAY);

		read_value = 0x0FFFF;
		s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
		s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x1F2F);
		s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);
		af_dbg("2nd AF status(%02d) = 0x%04X\n",
						count, read_value);
		if ((read_value & 0x0ff00) == 0x0)
			break;
	}

	if (count >= SECOND_AF_SEARCH_COUNT) {
		/* 0x01XX means "Not Finish". */
		cam_err("%s: ERROR, 2nd AF failed. read_val=0x%X\n\n",
			__func__, read_value & 0x0ff00);
		state->focus.status = AF_RESULT_FAILED;
		goto check_done;
	}

	cam_info("AF Success!\n");
	state->focus.status = AF_RESULT_SUCCESS;

check_done:
	/* restore write mode */

	/* We only unlocked AE,AWB in case of being cancelled.
	 * But we now unlock it unconditionally if AF is started,
	 */
	if (state->focus.status == AF_RESULT_CANCELLED) {
		cam_dbg("do_af: Single AF cancelled\n");
		s5k5ccgx_set_lock(sd, AEAWB_UNLOCK, false);
		state->focus.cancel = 0;
	} else {
		state->focus.start = AUTO_FOCUS_OFF;
		cam_dbg("do_af: Single AF finished\n");
	}

	if ((state->focus.preflash == PREFLASH_ON) &&
	    (state->sensor_mode == SENSOR_CAMERA)) {
		s5k5ccgx_set_from_table(sd, "af_pre_flash_end",
				&state->regs->af_pre_flash_end, 1, 0);
		s5k5ccgx_set_from_table(sd, "flash_ae_clear",
			&state->regs->flash_ae_clear, 1, 0);
		s5k5ccgx_flash_torch(sd, S5K5CCGX_FLASH_OFF);
		if (state->focus.status == AF_RESULT_CANCELLED) {
			state->focus.preflash = PREFLASH_NONE;
		}
	}

	/* Notice: we here turn touch flag off set previously
	 * when doing Touch AF. */
	if (state->focus.touch)
		state->focus.touch = 0;

	return 0;
}

/* PX: Set AF */
static int s5k5ccgx_set_af(struct v4l2_subdev *sd, s32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = 0;

	cam_info("%s: %s, focus mode %d\n", __func__,
			val ? "start" : "stop", state->focus.mode);

	if (unlikely((u32)val >= AUTO_FOCUS_MAX)) {
		cam_err("%s: ERROR, invalid value(%d)\n", __func__, val);
		return -EINVAL;
	}

	if (state->focus.start == val)
		return 0;

	state->focus.start = val;

	if (val == AUTO_FOCUS_ON) {
		state->focus.cancel = 0;
		err = queue_work(state->workqueue, &state->af_work);
		if (unlikely(!err)) {
			cam_warn("AF is still operating!\n");
			return 0;
		}

		state->focus.status = AF_RESULT_DOING;
	} else {
		/* Cancel AF */
		cam_info("set_af: AF cancel requested!\n");
		state->focus.cancel = 1;
	}

	cam_trace("X\n");
	return 0;
}

/* PX: Stop AF */
static int s5k5ccgx_stop_af(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = 0;

	cam_trace("E\n");
	/* mutex_lock(&state->af_lock); */

	switch (state->focus.status) {
	case AF_RESULT_FAILED:
	case AF_RESULT_SUCCESS:
		cam_dbg("Stop AF, focus mode %d, AF result %d\n",
			state->focus.mode, state->focus.status);

		err = s5k5ccgx_set_lock(sd, AEAWB_UNLOCK, false);
		if (unlikely(err)) {
			cam_err("%s: ERROR, fail to set lock\n", __func__);
			goto err_out;
		}
		state->focus.status = AF_RESULT_CANCELLED;
		state->focus.preflash = PREFLASH_NONE;
		break;

	case AF_RESULT_CANCELLED:
		break;

	default:
		cam_warn("%s: WARNING, unnecessary calling. AF status=%d\n",
			__func__, state->focus.status);
		/* Return 0. */
		goto err_out;
		break;
	}

	if (state->focus.touch)
		state->focus.touch = 0;

	/* mutex_unlock(&state->af_lock); */
	cam_trace("X\n");
	return 0;

err_out:
	/* mutex_unlock(&state->af_lock); */
	return err;
}

static void s5k5ccgx_af_worker(struct work_struct *work)
{
	struct s5k5ccgx_state *state = container_of(work, \
				struct s5k5ccgx_state, af_work);
	struct v4l2_subdev *sd = &state->sd;
	struct s5k5ccgx_interval *win_stable = &state->focus.win_stable;
	u32 touch_win_delay = 0;
	s32 interval = 0;
	int err = -EINVAL;

	cam_trace("E\n");

	mutex_lock(&state->af_lock);
	state->focus.reset_done = 0;

	if (state->sensor_mode == SENSOR_CAMERA) {
		state->one_frame_delay_ms = ONE_FRAME_DELAY_MS_NORMAL;
		touch_win_delay = ONE_FRAME_DELAY_MS_LOW;
		err = s5k5ccgx_af_start_preflash(sd);
		if (unlikely(err))
			goto out;

		if (state->focus.status == AF_RESULT_CANCELLED)
			goto out;
	} else
		state->one_frame_delay_ms = touch_win_delay = 50;

	/* sleep here for the time needed for af window before do_af. */
	if (state->focus.touch) {
		do_gettimeofday(&win_stable->curr_time);
		interval = GET_ELAPSED_TIME(win_stable->curr_time, \
				win_stable->before_time) / 1000;
		if (interval < touch_win_delay) {
			cam_dbg("window stable: %dms + %dms\n", interval,
				touch_win_delay - interval);
			debug_msleep(sd, touch_win_delay - interval);
		} else
			cam_dbg("window stable: %dms\n", interval);
	}

	s5k5ccgx_do_af(sd);

out:
	mutex_unlock(&state->af_lock);
	cam_trace("X\n");
	return;
}

/* PX: Set focus mode */
static int s5k5ccgx_set_focus_mode(struct v4l2_subdev *sd, s32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);
	u32 cancel = 0;
	u8 focus_mode = (u8)val;
	int err = -EINVAL;

	/* cam_trace("E\n");*/

	if (state->focus.mode == val)
		return 0;

	cancel = (u32)val & FOCUS_MODE_DEFAULT;

	/* Do nothing if cancel request occurs when af is being finished*/
	if (cancel && (state->focus.status == AF_RESULT_DOING)) {
		state->focus.cancel = 1;
		return 0;
	}

	cam_dbg("%s val =%d(0x%X)\n", __func__, val, val);

	mutex_lock(&state->af_lock);
	if (cancel) {
		s5k5ccgx_stop_af(sd);
		if (state->focus.reset_done) {
			cam_dbg("AF is already cancelled fully\n");
			goto out;
		}
		state->focus.reset_done = 1;
	}

	switch (focus_mode) {
	case FOCUS_MODE_MACRO:
		err = s5k5ccgx_set_from_table(sd, "af_macro_mode",
				&state->regs->af_macro_mode, 1, 0);
		if (unlikely(err)) {
			cam_err("%s: ERROR, fail to af_macro_mode (%d)\n",
							__func__, err);
			goto err_out;
		}

		state->focus.mode = focus_mode;
		break;

	case FOCUS_MODE_INFINITY:
	case FOCUS_MODE_AUTO:
	case FOCUS_MODE_FIXED:
		err = s5k5ccgx_set_from_table(sd, "af_norma_mode",
				&state->regs->af_normal_mode, 1, 0);
		if (unlikely(err)) {
			cam_err("%s: ERROR, fail to af_norma_mode (%d)\n",
							__func__, err);
			goto err_out;
		}

		state->focus.mode = focus_mode;
		break;

	case FOCUS_MODE_FACEDETECT:
	case FOCUS_MODE_CONTINOUS:
	case FOCUS_MODE_TOUCH:
		break;

	default:
		cam_err("%s: ERROR, invalid val(0x%X)\n:",
			__func__, val);
		goto err_out;
		break;
	}

out:
	mutex_unlock(&state->af_lock);
	return 0;

err_out:
	mutex_unlock(&state->af_lock);
	return err;
}

/* PX: */
static int s5k5ccgx_set_af_window(struct v4l2_subdev *sd)
{
	int err = -EIO;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k5ccgx_state *state = to_state(sd);
	struct s5k5ccgx_rect inner_window = {0, 0, 0, 0};
	struct s5k5ccgx_rect outter_window = {0, 0, 0, 0};
	struct s5k5ccgx_rect first_window = {0, 0, 0, 0};
	struct s5k5ccgx_rect second_window = {0, 0, 0, 0};
	const s32 mapped_x = state->focus.pos_x;
	const s32 mapped_y = state->focus.pos_y;
	const u32 preview_width = state->preview->width;
	const u32 preview_height = state->preview->height;
	u32 inner_half_width = 0, inner_half_height = 0;
	u32 outter_half_width = 0, outter_half_height = 0;

	cam_trace("E\n");

	mutex_lock(&state->af_lock);

	inner_window.width = SCND_WINSIZE_X * preview_width / 1024;
	inner_window.height = SCND_WINSIZE_Y * preview_height / 1024;
	outter_window.width = FIRST_WINSIZE_X * preview_width / 1024;
	outter_window.height = FIRST_WINSIZE_Y * preview_height / 1024;

	inner_half_width = inner_window.width / 2;
	inner_half_height = inner_window.height / 2;
	outter_half_width = outter_window.width / 2;
	outter_half_height = outter_window.height / 2;

	af_dbg("Preview width=%d, height=%d\n", preview_width, preview_height);
	af_dbg("inner_window_width=%d, inner_window_height=%d, " \
		"outter_window_width=%d, outter_window_height=%d\n ",
		inner_window.width, inner_window.height,
		outter_window.width, outter_window.height);

	/* Get X */
	if (mapped_x <= inner_half_width) {
		inner_window.x = outter_window.x = 0;
		af_dbg("inner & outter window over sensor left."
			"in_x=%d, out_x=%d\n", inner_window.x, outter_window.x);
	} else if (mapped_x <= outter_half_width) {
		inner_window.x = mapped_x - inner_half_width;
		outter_window.x = 0;
		af_dbg("outter window over sensor left. in_x=%d, out_x=%d\n",
					inner_window.x, outter_window.x);
	} else if (mapped_x >= ((preview_width - 1) - inner_half_width)) {
		inner_window.x = (preview_width - 1) - inner_window.width;
		outter_window.x = (preview_width - 1) - outter_window.width;
		af_dbg("inner & outter window over sensor right." \
			"in_x=%d, out_x=%d\n", inner_window.x, outter_window.x);
	} else if (mapped_x >= ((preview_width - 1) - outter_half_width)) {
		inner_window.x = mapped_x - inner_half_width;
		outter_window.x = (preview_width - 1) - outter_window.width;
		af_dbg("outter window over sensor right. in_x=%d, out_x=%d\n",
					inner_window.x, outter_window.x);
	} else {
		inner_window.x = mapped_x - inner_half_width;
		outter_window.x = mapped_x - outter_half_width;
		af_dbg("inner & outter window within sensor area." \
			"in_x=%d, out_x=%d\n", inner_window.x, outter_window.x);
	}

	/* Get Y */
	if (mapped_y <= inner_half_height) {
		inner_window.y = outter_window.y = 0;
		af_dbg("inner & outter window over sensor top." \
			"in_y=%d, out_y=%d\n", inner_window.y, outter_window.y);
	} else if (mapped_y <= outter_half_height) {
		inner_window.y = mapped_y - inner_half_height;
		outter_window.y = 0;
		af_dbg("outter window over sensor top. in_y=%d, out_y=%d\n",
					inner_window.y, outter_window.y);
	} else if (mapped_y >= ((preview_height - 1) - inner_half_height)) {
		inner_window.y = (preview_height - 1) - inner_window.height;
		outter_window.y = (preview_height - 1) - outter_window.height;
		af_dbg("inner & outter window over sensor bottom." \
			"in_y=%d, out_y=%d\n", inner_window.y, outter_window.y);
	} else if (mapped_y >= ((preview_height - 1) - outter_half_height)) {
		inner_window.y = mapped_y - inner_half_height;
		outter_window.y = (preview_height - 1) - outter_window.height;
		af_dbg("outter window over sensor bottom. in_y=%d, out_y=%d\n",
					inner_window.y, outter_window.y);
	} else {
		inner_window.y = mapped_y - inner_half_height;
		outter_window.y = mapped_y - outter_half_height;
		af_dbg("inner & outter window within sensor area." \
			"in_y=%d, out_y=%d\n", inner_window.y, outter_window.y);
	}

	af_dbg("==> inner_window top=(%d,%d), bottom=(%d, %d)\n",
		inner_window.x, inner_window.y,
		inner_window.x + inner_window.width,
		inner_window.y + inner_window.height);
	af_dbg("==> outter_window top=(%d,%d), bottom=(%d, %d)\n",
		outter_window.x, outter_window.y,
		outter_window.x + outter_window.width ,
		outter_window.y + outter_window.height);

	second_window.x = inner_window.x * 1024 / preview_width;
	second_window.y = inner_window.y * 1024 / preview_height;
	first_window.x = outter_window.x * 1024 / preview_width;
	first_window.y = outter_window.y * 1024 / preview_height;

	af_dbg("=> second_window top=(%d, %d)\n",
		second_window.x, second_window.y);
	af_dbg("=> first_window top=(%d, %d)\n",
		first_window.x, first_window.y);

	/* restore write mode */
	err = s5k5ccgx_i2c_write_twobyte(client, 0x0028, 0x7000);

	/* Set first window x, y */
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x002A, 0x022C);
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x0F12,
					(u16)(first_window.x));
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x002A, 0x022E);
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x0F12,
					(u16)(first_window.y));

	/* Set second widnow x, y */
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x002A, 0x0234);
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x0F12,
					(u16)(second_window.x));
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x002A, 0x0236);
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x0F12,
					(u16)(second_window.y));

	/* Update AF window */
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x002A, 0x023C);
	err |= s5k5ccgx_i2c_write_twobyte(client, 0x0F12, 0x0001);

	do_gettimeofday(&state->focus.win_stable.before_time);
	mutex_unlock(&state->af_lock);

	CHECK_ERR(err);
	cam_info("AF window position completed.\n");

	cam_trace("X\n");
	return 0;
}

static int s5k5ccgx_set_touch_af(struct v4l2_subdev *sd, s32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EIO;

	cam_trace("%s, x=%d y=%d\n", val ? "start" : "stop",
			state->focus.pos_x, state->focus.pos_y);

	state->focus.touch = val;

	if (val) {
		if (mutex_is_locked(&state->af_lock)) {
			cam_warn("%s: AF is still operating!\n", __func__);
			return 0;
		}

		err = queue_work(state->workqueue, &state->af_win_work);
		if (likely(!err))
			cam_warn("WARNING, AF window is still processing\n");
	} else
		cam_info("set_touch_af: invalid value %d\n", val);

	cam_trace("X\n");
	return 0;
}

static void s5k5ccgx_af_win_worker(struct work_struct *work)
{
	struct s5k5ccgx_state *state = container_of(work, \
				struct s5k5ccgx_state, af_win_work);
	struct v4l2_subdev *sd = &state->sd;

	cam_trace("E\n");
	s5k5ccgx_set_af_window(sd);
	cam_trace("X\n");
}

static int s5k5ccgx_init_param(struct v4l2_subdev *sd)
{
	struct v4l2_control ctrl;
	int i;

	for (i = 0; i < ARRAY_SIZE(s5k5ccgx_ctrls); i++) {
		if (s5k5ccgx_ctrls[i].value !=
				s5k5ccgx_ctrls[i].default_value) {
			ctrl.id = s5k5ccgx_ctrls[i].id;
			ctrl.value = s5k5ccgx_ctrls[i].value;
			s5k5ccgx_s_ctrl(sd, &ctrl);
		}
	}

	return 0;
}

static int s5k5ccgx_init_regs(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k5ccgx_state *state = to_state(sd);
	u16 read_value = 0;
	int err = -ENODEV;

	/* we'd prefer to do this in probe, but the framework hasn't
	 * turned on the camera yet so our i2c operations would fail
	 * if we tried to do it in probe, so we have to do it here
	 * and keep track if we succeeded or not.
	 */

	/* enter read mode */
	err = s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	if (unlikely(err < 0))
		return -ENODEV;

	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0150);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);
	if (likely(read_value == S5K5CCGX_CHIP_ID))
		cam_info("Sensor ChipID: 0x%04X\n", S5K5CCGX_CHIP_ID);
	else
		cam_info("Sensor ChipID: 0x%04X, unknown ChipID\n", read_value);

	s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0152);
	s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);
	if (likely(read_value == S5K5CCGX_CHIP_REV))
		cam_info("Sensor revision: 0x%04X\n", S5K5CCGX_CHIP_REV);
	else
		cam_info("Sensor revision: 0x%04X, unknown revision\n",
				read_value);

	/* restore write mode */
	err = s5k5ccgx_i2c_write_twobyte(client, 0x0028, 0x7000);
	CHECK_ERR_COND(err < 0, -ENODEV);

	state->regs = &reg_datas;

	return 0;
}

static const struct s5k5ccgx_framesize *s5k5ccgx_get_framesize
	(const struct s5k5ccgx_framesize *frmsizes,
	u32 frmsize_count, u32 index)
{
	int i = 0;

	for (i = 0; i < frmsize_count; i++) {
		if (frmsizes[i].index == index)
			return &frmsizes[i];
	}

	return NULL;
}

/* This function is called from the g_ctrl api
 *
 * This function should be called only after the s_fmt call,
 * which sets the required width/height value.
 *
 * It checks a list of available frame sizes and sets the
 * most appropriate frame size.
 *
 * The list is stored in an increasing order (as far as possible).
 * Hence the first entry (searching from the beginning) where both the
 * width and height is more than the required value is returned.
 * In case of no perfect match, we set the last entry (which is supposed
 * to be the largest resolution supported.)
 */
static void s5k5ccgx_set_framesize(struct v4l2_subdev *sd,
				const struct s5k5ccgx_framesize *frmsizes,
				u32 num_frmsize, bool preview)
{
	struct s5k5ccgx_state *state = to_state(sd);
	const struct s5k5ccgx_framesize **found_frmsize = NULL;
	u32 width = state->req_fmt.width;
	u32 height = state->req_fmt.height;
	int i = 0;

	cam_dbg("%s: Requested Res %dx%d\n", __func__,
			width, height);

	found_frmsize = (const struct s5k5ccgx_framesize **)
			(preview ? &state->preview : &state->capture);

	for (i = 0; i < num_frmsize; i++) {
		if ((frmsizes[i].width == width) &&
			(frmsizes[i].height == height)) {
			*found_frmsize = &frmsizes[i];
			break;
		}
	}

	if (*found_frmsize == NULL) {
		cam_err("%s: ERROR, invalid frame size %dx%d\n", __func__,
						width, height);
		*found_frmsize = preview ?
			s5k5ccgx_get_framesize(frmsizes, num_frmsize,
					S5K5CCGX_PREVIEW_XGA) :
			s5k5ccgx_get_framesize(frmsizes, num_frmsize,
					S5K5CCGX_CAPTURE_3MP);
		BUG_ON(!(*found_frmsize));
	}

	if (preview)
		cam_info("Preview Res Set: %dx%d, index %d\n",
			(*found_frmsize)->width, (*found_frmsize)->height,
			(*found_frmsize)->index);
	else
		cam_info("Capture Res Set: %dx%d, index %d\n",
			(*found_frmsize)->width, (*found_frmsize)->height,
			(*found_frmsize)->index);
}

static int s5k5ccgx_wait_steamoff(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	struct s5k5ccgx_interval *stream_time = &state->stream_time;
	s32 elapsed_msec = 0;

	cam_trace("E\n");

	if (unlikely(!(state->pdata->is_mipi & state->need_wait_streamoff)))
		return 0;

	do_gettimeofday(&stream_time->curr_time);

	elapsed_msec = GET_ELAPSED_TIME(stream_time->curr_time, \
				stream_time->before_time) / 1000;

	if (state->pdata->streamoff_delay > elapsed_msec) {
		cam_info("stream-off: %dms + %dms\n", elapsed_msec,
			state->pdata->streamoff_delay - elapsed_msec);
		debug_msleep(sd, state->pdata->streamoff_delay - elapsed_msec);
	} else
		cam_info("stream-off: %dms\n", elapsed_msec);

	state->need_wait_streamoff = 0;

	return 0;
}

static int s5k5ccgx_control_stream(struct v4l2_subdev *sd, u32 cmd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

	if (unlikely(cmd != STREAM_STOP))
		return 0;

	cam_info("STREAM STOP!!\n");
	err = s5k5ccgx_set_from_table(sd, "stream_stop",
			&state->regs->stream_stop, 1, 0);

#ifdef CONFIG_VIDEO_IMPROVE_STREAMOFF
	do_gettimeofday(&state->stream_time.before_time);
	state->need_wait_streamoff = 1;
#else
	debug_msleep(sd, state->pdata->streamoff_delay);
#endif

	if (state->runmode == S5K5CCGX_RUNMODE_CAPTURING) {
		state->runmode = S5K5CCGX_RUNMODE_CAPTURE_STOP;
		cam_dbg("Capture Stop!\n");
	}

	CHECK_ERR_MSG(err, "failed to stop stream\n");
	return 0;
}

/* PX: Set flash mode */
static int s5k5ccgx_set_flash_mode(struct v4l2_subdev *sd, s32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);

	/* movie flash mode should be set when recording is started */
/*	if (state->sensor_mode == SENSOR_MOVIE && !state->recording)
		return 0;*/

	if (state->flash_mode == val) {
		cam_dbg("the same flash mode=%d\n", val);
		return 0;
	}

	if (val == FLASH_MODE_TORCH)
		s5k5ccgx_flash_torch(sd, S5K5CCGX_FLASH_ON);

	if ((state->flash_mode == FLASH_MODE_TORCH)
	    && (val == FLASH_MODE_OFF))
		s5k5ccgx_flash_torch(sd, S5K5CCGX_FLASH_OFF);

	state->flash_mode = val;
	cam_dbg("Flash mode = %d\n", val);
	return 0;
}

static int s5k5ccgx_check_esd(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = -EINVAL;
	u16 read_value = 0;

	err = s5k5ccgx_set_from_table(sd, "get_esd_status",
		&state->regs->get_esd_status, 1, 0);
	CHECK_ERR(err);
	err = s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);
	CHECK_ERR(err);

	if (read_value != 0xAAAA)
		goto esd_out;

	cam_info("Check ESD: not detected\n\n");
	return 0;

esd_out:
	cam_err("Check ESD: ERROR, ESD Shock detected! (val=0x%X)\n\n",
		read_value);
	return -ERESTART;
}

/* returns the real iso currently used by sensor due to lighting
 * conditions, not the requested iso we sent using s_ctrl.
 */
/* PX: */
static inline int s5k5ccgx_get_exif_iso(struct v4l2_subdev *sd, u16 *iso)
{
	struct s5k5ccgx_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 iso_gain_table[] = {10, 15, 25, 35};
	u16 iso_table[] = {0, 50, 100, 200, 400};
	int err = -EIO;
	u16 val = 0, gain = 0;
	int i = 0;

	err = s5k5ccgx_set_from_table(sd, "get_iso",
				&state->regs->get_iso, 1, 0);
	err |= s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &val);
	CHECK_ERR(err);

	gain = val * 10 / 256;
	for (i = 0; i < ARRAY_SIZE(iso_gain_table); i++) {
		if (gain < iso_gain_table[i])
			break;
	}

	*iso = iso_table[i];

	cam_dbg("gain=%d, ISO=%d\n", gain, *iso);

	/* We do not restore write mode */

	return 0;
}

/* PX: Set ISO */
static int __used s5k5ccgx_set_iso(struct v4l2_subdev *sd, s32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

retry:
	switch (val) {
	case ISO_AUTO:
	case ISO_50:
	case ISO_100:
	case ISO_200:
	case ISO_400:
		err = s5k5ccgx_set_from_table(sd, "iso",
			state->regs->iso, ARRAY_SIZE(state->regs->iso),
			val);
		break;

	default:
		cam_err("%s: ERROR, invalid arguement(%d)\n", __func__, val);
		val = ISO_AUTO;
		goto retry;
		break;
	}

	cam_trace("X\n");
	return 0;
}

/* PX: Return exposure time (ms) */
static inline int s5k5ccgx_get_exif_exptime(struct v4l2_subdev *sd,
						u32 *exp_time)
{
	struct s5k5ccgx_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err;
	u16 read_value_lsb = 0;
	u16 read_value_msb = 0;

	err = s5k5ccgx_set_from_table(sd, "get_shutterspeed",
				&state->regs->get_shutterspeed, 1, 0);
	CHECK_ERR(err);

	err = s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value_lsb);
	err |= s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value_msb);
	CHECK_ERR(err);

	*exp_time = (((read_value_msb << 16) | (read_value_lsb & 0xFFFF))
			* 1000) / 400;

	/* We do not restore write mode */

	return 0;

}

static inline void s5k5ccgx_get_exif_flash(struct v4l2_subdev *sd,
					u16 *flash)
{
	struct s5k5ccgx_state *state = to_state(sd);

	*flash = 0;

	switch (state->flash_mode) {
	case FLASH_MODE_OFF:
		*flash |= EXIF_FLASH_MODE_SUPPRESSION;
		break;

	case FLASH_MODE_AUTO:
		*flash |= EXIF_FLASH_MODE_AUTO;
		break;

	case FLASH_MODE_ON:
	case FLASH_MODE_TORCH:
		*flash |= EXIF_FLASH_MODE_FIRING;
		break;

	default:
		break;
	}

	if (state->flash_on) {
		*flash |= EXIF_FLASH_FIRED;
		if (state->sensor_mode == SENSOR_CAMERA)
			state->flash_on = 0;
	}

}

/* PX: */
static int s5k5ccgx_get_exif(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	u32 exposure_time = 0;

	/* exposure time */
	state->exif.exp_time_den = 0;
	s5k5ccgx_get_exif_exptime(sd, &exposure_time);
	/*WARN(!exposure_time, "WARNING: exposure time is 0\n");*/
	state->exif.exp_time_den = 1000 * 1000 / exposure_time;

	/* iso */
	state->exif.iso = 0;
	s5k5ccgx_get_exif_iso(sd, &state->exif.iso);

	/* flash */
	s5k5ccgx_get_exif_flash(sd, &state->exif.flash);

	cam_dbg("EXIF: ex_time_den=%d, iso=%d, flash=0x%02X\n",
		state->exif.exp_time_den, state->exif.iso, state->exif.flash);

	return 0;
}

static int s5k5ccgx_set_preview_size(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_trace("E, wide_cmd=%d\n", state->wide_cmd);

	switch (state->wide_cmd) {
	case WIDE_REQ_CHANGE:
		cam_info("%s: Wide Capture setting\n", __func__);
		err = s5k5ccgx_set_from_table(sd, "change_wide_cap",
			&state->regs->change_wide_cap, 1, 0);
		break;

	case WIDE_REQ_RESTORE:
		cam_info("%s:Restore capture setting\n", __func__);
		err = s5k5ccgx_set_from_table(sd, "restore_capture",
				&state->regs->restore_cap, 1, 0);
		/* We do not break */

	default:
		cam_dbg("set_preview_size\n");
		err = s5k5ccgx_set_from_table(sd, "preview_size",
				state->regs->preview_size,
				ARRAY_SIZE(state->regs->preview_size),
				state->preview->index);
		BUG_ON(state->preview->index == S5K5CCGX_PREVIEW_PVGA);
		break;
	}
	CHECK_ERR(err);

	return 0;
}

static int s5k5ccgx_set_preview_start(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;
	/* bool set_size = true; */

	cam_dbg("Camera Preview start, runmode = %d\n", state->runmode);

	if ((state->runmode == S5K5CCGX_RUNMODE_NOTREADY) ||
	    (state->runmode == S5K5CCGX_RUNMODE_CAPTURING)) {
		cam_err("%s: ERROR - Invalid runmode\n", __func__);
		return -EPERM;
	}

	state->focus.status = AF_RESULT_NONE;

	if (state->need_update_frmsize) {
		err = s5k5ccgx_set_preview_size(sd);
		state->need_update_frmsize = 0;
		CHECK_ERR_MSG(err, "failed to set preview size(%d)\n", err);
	}

	if (state->runmode == S5K5CCGX_RUNMODE_CAPTURE_STOP) {
		/* We turn flash off if one shot flash is still on. */
		if (s5k5ccgx_is_hwflash_on(sd))
			s5k5ccgx_flash_oneshot(sd, S5K5CCGX_FLASH_OFF);

		err = s5k5ccgx_set_lock(sd, AEAWB_UNLOCK, true);
		CHECK_ERR_MSG(err, "fail to set lock\n");

		cam_info("Sending Preview_Return cmd\n");
		err = s5k5ccgx_set_from_table(sd, "preview_return",
					&state->regs->preview_return, 1, 0);
		CHECK_ERR_MSG(err, "fail to set Preview_Return (%d)\n", err)
	} else {
		err = s5k5ccgx_set_from_table(sd, "update_preview",
			&state->regs->update_preview, 1, 0);
		CHECK_ERR_MSG(err, "failed to update preview(%d)\n", err);
	}

	state->runmode = S5K5CCGX_RUNMODE_RUNNING;

	return 0;
}

static int s5k5ccgx_set_video_preview(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_dbg("Video Preview start, runmode = %d\n", state->runmode);

	if ((state->runmode == S5K5CCGX_RUNMODE_NOTREADY) ||
	    (state->runmode == S5K5CCGX_RUNMODE_CAPTURING)) {
		cam_err("%s: ERROR - Invalid runmode\n", __func__);
		return -EPERM;
	}

	state->focus.status = AF_RESULT_NONE;

	if (state->hd_videomode) {
		s5k5ccgx_init_param(sd);
		err = s5k5ccgx_set_from_table(sd, "update_hd_preview",
			&state->regs->update_hd_preview, 1, 0);
		CHECK_ERR_MSG(err, "failed to update HD preview\n");

		s5k5ccgx_set_from_table(sd, "hd_first_af_start",
				&state->regs->hd_first_af_start, 1, 0);
	} else {
		err = s5k5ccgx_set_from_table(sd, "preview_size",
				state->regs->preview_size,
				ARRAY_SIZE(state->regs->preview_size),
				state->preview->index);
		CHECK_ERR_MSG(err, "failed to set preview size\n");

		err = s5k5ccgx_set_from_table(sd, "update_preview",
			&state->regs->update_preview, 1, 0);
		CHECK_ERR_MSG(err, "failed to update preview\n");
	}

	cam_dbg("runmode now RUNNING\n");
	state->runmode = S5K5CCGX_RUNMODE_RUNNING;

	return 0;
}

/* PX: Start capture */
static int s5k5ccgx_set_capture_start(struct v4l2_subdev *sd)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -ENODEV;
	u32 light_level = 0xFFFFFFFF;

	/* Set capture size */
	err = s5k5ccgx_set_capture_size(sd);
	CHECK_ERR_MSG(err, "fail to set capture size (%d)\n", err);

	/* Set flash */
	switch (state->flash_mode) {
	case FLASH_MODE_AUTO:
		/* 3rd party App may do capturing without AF. So we check
		 * whether AF is executed  before capture and  turn on flash
		 * if needed. But we do not consider low-light capture of Market
		 * App. */
		if (state->focus.preflash == PREFLASH_NONE) {
			s5k5ccgx_get_light_level(sd, &state->light_level);
			if (light_level >= FLASH_LOW_LIGHT_LEVEL)
				break;
		} else if (state->focus.preflash == PREFLASH_OFF)
			break;
		/* We do not break. */

	case FLASH_MODE_ON:
		s5k5ccgx_flash_oneshot(sd, S5K5CCGX_FLASH_ON);
		/* We here don't need to set state->flash_on to 1 */

		err = s5k5ccgx_set_lock(sd, AEAWB_UNLOCK, true);
		CHECK_ERR_MSG(err, "fail to set lock\n");

		/* Full flash start */
		err = s5k5ccgx_set_from_table(sd, "flash_start",
			&state->regs->flash_start, 1, 0);
		break;

	case FLASH_MODE_OFF:
#ifdef CONFIG_VIDEO_S5K5CCGX_P8
		if (state->light_level < CAPTURE_LOW_LIGHT_LEVEL)
			err = s5k5ccgx_set_from_table(sd, "set_lowlight_cap",
				&state->regs->set_lowlight_cap, 1, 0);
		break;
#endif
	default:
		break;
	}

	/* Send capture start command. */
	cam_dbg("Send Capture_Start cmd\n");
	err = s5k5ccgx_set_from_table(sd, "capture_start",
			state->regs->capture_start,
			ARRAY_SIZE(state->regs->capture_start),
			state->capture->index);
	if (state->scene_mode == SCENE_MODE_NIGHTSHOT)
		debug_msleep(sd, 140);

	state->runmode = S5K5CCGX_RUNMODE_CAPTURING;
	state->focus.preflash = PREFLASH_NONE;

	CHECK_ERR_MSG(err, "fail to capture_start (%d)\n", err);

	s5k5ccgx_get_exif(sd);

	return 0;
}

static int s5k5ccgx_s_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	struct s5k5ccgx_state *state = to_state(sd);
	s32 previous_index = 0;

	cam_dbg("%s: pixelformat = 0x%x, colorspace = 0x%x, width = %d, height = %d\n",
		__func__, fmt->code, fmt->colorspace, fmt->width, fmt->height);

	v4l2_fill_pix_format(&state->req_fmt, fmt);
	state->format_mode = fmt->field;
	state->wide_cmd = WIDE_REQ_NONE;

	if (state->format_mode != V4L2_PIX_FMT_MODE_CAPTURE) {
		previous_index = state->preview ? state->preview->index : -1;

		s5k5ccgx_set_framesize(sd, s5k5ccgx_preview_frmsizes,
				ARRAY_SIZE(s5k5ccgx_preview_frmsizes),
				true);

		if (unlikely((state->sensor_mode == SENSOR_CAMERA) &&
		    (state->preview->index == S5K5CCGX_PREVIEW_PVGA))) {
			cam_err("%s: ERROR, invalid preview size\n", __func__);
			return -EINVAL;
		}

		if (previous_index != state->preview->index) {
			if ((state->preview->index == PREVIEW_WIDE_SIZE)
			    && (previous_index != PREVIEW_WIDE_SIZE)) {
				cam_dbg("preview, need to change to WIDE\n");
				state->wide_cmd = WIDE_REQ_CHANGE;
			} else if ((state->preview->index != PREVIEW_WIDE_SIZE)
			    && (previous_index == PREVIEW_WIDE_SIZE)) {
				cam_dbg("preview, need to restore form WIDE\n");
				state->wide_cmd = WIDE_REQ_RESTORE;
			}

			state->need_update_frmsize = 1;
		}
	} else {
		/*
		 * In case of image capture mode,
		 * if the given image resolution is not supported,
		 * use the next higher image resolution. */
		s5k5ccgx_set_framesize(sd, s5k5ccgx_capture_frmsizes,
				ARRAY_SIZE(s5k5ccgx_capture_frmsizes),
				false);

		/* for maket app.
		 * Samsung camera app does not use unmatched ratio.*/
		if (unlikely(FRM_RATIO(state->preview)
		    != FRM_RATIO(state->capture))) {
			cam_warn("%s: WARNING, capture ratio " \
				"is different with preview ratio\n\n",
				__func__);
			if (state->capture->index == CAPTURE_WIDE_SIZE) {
				cam_dbg("captre: need to change to WIDE\n");
				state->wide_cmd = WIDE_REQ_CHANGE;
			} else {
				cam_dbg("capture, need to restore form WIDE\n");
				state->wide_cmd = WIDE_REQ_RESTORE;
			}
		}
	}

	return 0;
}

static int s5k5ccgx_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned int index,
					enum v4l2_mbus_pixelcode *code)
{
	cam_dbg("%s: index = %d\n", __func__, index);

	if (index >= ARRAY_SIZE(capture_fmts))
		return -EINVAL;

	*code = capture_fmts[index].code;

	return 0;
}

static int s5k5ccgx_try_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	int num_entries;
	int i;

	num_entries = ARRAY_SIZE(capture_fmts);

	cam_dbg("%s: code = 0x%x , colorspace = 0x%x, num_entries = %d\n",
		__func__, fmt->code, fmt->colorspace, num_entries);

	for (i = 0; i < num_entries; i++) {
		if (capture_fmts[i].code == fmt->code &&
		    capture_fmts[i].colorspace == fmt->colorspace) {
			cam_dbg("%s: match found, returning 0\n", __func__);
			return 0;
		}
	}

	cam_err("%s: no match found, returning -EINVAL\n", __func__);
	return -EINVAL;
}


static int s5k5ccgx_enum_framesizes(struct v4l2_subdev *sd,
				  struct v4l2_frmsizeenum *fsize)
{
	struct s5k5ccgx_state *state = to_state(sd);

	/*
	* The camera interface should read this value, this is the resolution
	* at which the sensor would provide framedata to the camera i/f
	* In case of image capture,
	* this returns the default camera resolution (VGA)
	*/
	if (state->format_mode != V4L2_PIX_FMT_MODE_CAPTURE) {
		if (unlikely(state->preview == NULL)) {
			cam_err("%s: ERROR\n", __func__);
			return -EFAULT;
		}

		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = state->preview->width;
		fsize->discrete.height = state->preview->height;
	} else {
		if (unlikely(state->capture == NULL)) {
			cam_err("%s: ERROR\n", __func__);
			return -EFAULT;
		}

		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = state->capture->width;
		fsize->discrete.height = state->capture->height;
	}

	return 0;
}

static int s5k5ccgx_g_parm(struct v4l2_subdev *sd,
			struct v4l2_streamparm *param)
{
	return 0;
}

static int s5k5ccgx_s_parm(struct v4l2_subdev *sd,
			struct v4l2_streamparm *param)
{
	int err = 0;
	struct s5k5ccgx_state *state = to_state(sd);

	state->req_fps = param->parm.capture.timeperframe.denominator /
			param->parm.capture.timeperframe.numerator;

	cam_dbg("s_parm state->fps=%d, state->req_fps=%d\n",
		state->fps, state->req_fps);

	if ((state->req_fps < 0) || (state->req_fps > 30)) {
		cam_err("%s: ERROR, invalid frame rate %d. we'll set to 30\n",
				__func__, state->req_fps);
		state->req_fps = 30;
	}

	if (state->initialized && (state->scene_mode == SCENE_MODE_NONE)) {
		err = s5k5ccgx_set_frame_rate(sd, state->req_fps);
		CHECK_ERR(err);
	}

	return 0;
}

static int s5k5ccgx_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = 0;

	if (!state->initialized) {
		cam_err("%s: WARNING, camera not initialized\n", __func__);
		return 0;
	}

	mutex_lock(&state->ctrl_lock);

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_EXIF_EXPTIME:
		if (state->sensor_mode == SENSOR_CAMERA)
			ctrl->value = state->exif.exp_time_den;
		else
			ctrl->value = 24;
		break;

	case V4L2_CID_CAMERA_EXIF_ISO:
		if (state->sensor_mode == SENSOR_CAMERA)
			ctrl->value = state->exif.iso;
		else
			ctrl->value = 100;
		break;

	case V4L2_CID_CAMERA_EXIF_FLASH:
		if (state->sensor_mode == SENSOR_CAMERA)
			ctrl->value = state->exif.flash;
		else
			s5k5ccgx_get_exif_flash(sd, (u16 *)ctrl->value);
		break;

#if !defined(FEATURE_YUV_CAPTURE)
	case V4L2_CID_CAM_JPEG_MAIN_SIZE:
		ctrl->value = state->jpeg.main_size;
		break;

	case V4L2_CID_CAM_JPEG_MAIN_OFFSET:
		ctrl->value = state->jpeg.main_offset;
		break;

	case V4L2_CID_CAM_JPEG_THUMB_SIZE:
		ctrl->value = state->jpeg.thumb_size;
		break;

	case V4L2_CID_CAM_JPEG_THUMB_OFFSET:
		ctrl->value = state->jpeg.thumb_offset;
		break;

	case V4L2_CID_CAM_JPEG_QUALITY:
		ctrl->value = state->jpeg.quality;
		break;

	case V4L2_CID_CAM_JPEG_MEMSIZE:
		ctrl->value = SENSOR_JPEG_SNAPSHOT_MEMSIZE;
		break;
#endif

	case V4L2_CID_CAMERA_AUTO_FOCUS_RESULT:
		ctrl->value = state->focus.status;
		break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:
	case V4L2_CID_CAMERA_EFFECT:
	case V4L2_CID_CAMERA_CONTRAST:
	case V4L2_CID_CAMERA_SATURATION:
	case V4L2_CID_CAMERA_SHARPNESS:
	case V4L2_CID_CAMERA_OBJ_TRACKING_STATUS:
	case V4L2_CID_CAMERA_SMART_AUTO_STATUS:
	default:
		cam_err("%s: WARNING, unknown Ctrl-ID 0x%x\n",
					__func__, ctrl->id);
		err = 0; /* we return no error. */
		break;
	}

	mutex_unlock(&state->ctrl_lock);

	return err;
}

static int s5k5ccgx_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -ENOIOCTLCMD;

	if (unlikely(state->sensor_mode == SENSOR_MOVIE))
		s5k5ccgx_save_ctrl(sd, ctrl);

	if (!state->initialized && ctrl->id != V4L2_CID_CAMERA_SENSOR_MODE) {
		if (state->sensor_mode == SENSOR_MOVIE)
			return 0;

		cam_warn("%s: WARNING, camera not initialized. ID = %d(0x%X)\n",
			__func__, ctrl->id - V4L2_CID_PRIVATE_BASE,
			ctrl->id - V4L2_CID_PRIVATE_BASE);
		return 0;
	}

	cam_dbg("%s: ID =%d, val = %d\n",
		__func__, ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);

	if (ctrl->id != V4L2_CID_CAMERA_SET_AUTO_FOCUS)
		mutex_lock(&state->ctrl_lock);

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_SENSOR_MODE:
		err = s5k5ccgx_set_sensor_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_OBJECT_POSITION_X:
		state->focus.pos_x = ctrl->value;
		err = 0;
		break;

	case V4L2_CID_CAMERA_OBJECT_POSITION_Y:
		state->focus.pos_y = ctrl->value;
		err = 0;
		break;

	case V4L2_CID_CAMERA_TOUCH_AF_START_STOP:
		err = s5k5ccgx_set_touch_af(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FOCUS_MODE:
		err = s5k5ccgx_set_focus_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SET_AUTO_FOCUS:
		err = s5k5ccgx_set_af(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FLASH_MODE:
		err = s5k5ccgx_set_flash_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BRIGHTNESS:
		err = s5k5ccgx_set_exposure(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:
		err = s5k5ccgx_set_from_table(sd, "white balance",
			state->regs->white_balance,
			ARRAY_SIZE(state->regs->white_balance), ctrl->value);
		state->wb_mode = ctrl->value;
		break;

	case V4L2_CID_CAMERA_EFFECT:
		err = s5k5ccgx_set_from_table(sd, "effects",
			state->regs->effect,
			ARRAY_SIZE(state->regs->effect), ctrl->value);
		break;

	case V4L2_CID_CAMERA_METERING:
		err = s5k5ccgx_set_from_table(sd, "metering",
			state->regs->metering,
			ARRAY_SIZE(state->regs->metering), ctrl->value);
		break;

	case V4L2_CID_CAMERA_CONTRAST:
		err = s5k5ccgx_set_from_table(sd, "contrast",
			state->regs->contrast,
			ARRAY_SIZE(state->regs->contrast), ctrl->value);
		break;

	case V4L2_CID_CAMERA_SATURATION:
		err = s5k5ccgx_set_from_table(sd, "saturation",
			state->regs->saturation,
			ARRAY_SIZE(state->regs->saturation), ctrl->value);
		break;

	case V4L2_CID_CAMERA_SHARPNESS:
		err = s5k5ccgx_set_from_table(sd, "sharpness",
			state->regs->sharpness,
			ARRAY_SIZE(state->regs->sharpness), ctrl->value);
		break;

	case V4L2_CID_CAMERA_SCENE_MODE:
		err = s5k5ccgx_set_scene_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_AE_LOCK_UNLOCK:
		err = s5k5ccgx_set_ae_lock(sd, ctrl->value, false);
		break;

	case V4L2_CID_CAMERA_AWB_LOCK_UNLOCK:
		err = s5k5ccgx_set_awb_lock(sd, ctrl->value, false);
		break;

	case V4L2_CID_CAMERA_CHECK_ESD:
		err = s5k5ccgx_check_esd(sd);
		break;

	case V4L2_CID_CAMERA_ISO:
		/* we do not break. */
	case V4L2_CID_CAMERA_FRAME_RATE:
	default:
		cam_err("%s: WARNING, unknown Ctrl-ID 0x%x\n",
			__func__, ctrl->id);
		err = 0; /* we return no error. */
		break;
	}

	if (ctrl->id != V4L2_CID_CAMERA_SET_AUTO_FOCUS)
		mutex_unlock(&state->ctrl_lock);

	CHECK_ERR_MSG(err, "s_ctrl failed %d\n", err)

	return 0;
}

static int s5k5ccgx_s_ext_ctrl(struct v4l2_subdev *sd,
			      struct v4l2_ext_control *ctrl)
{
	return 0;
}

static int s5k5ccgx_s_ext_ctrls(struct v4l2_subdev *sd,
				struct v4l2_ext_controls *ctrls)
{
	struct v4l2_ext_control *ctrl = ctrls->controls;
	int ret;
	int i;

	for (i = 0; i < ctrls->count; i++, ctrl++) {
		ret = s5k5ccgx_s_ext_ctrl(sd, ctrl);

		if (ret) {
			ctrls->error_idx = i;
			break;
		}
	}

	return ret;
}

static int s5k5ccgx_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = 0;

	cam_info("stream mode = %d\n", enable);

	BUG_ON(!state->initialized);

	switch (enable) {
	case STREAM_MODE_CAM_OFF:
		if (state->pdata->is_mipi)
			err = s5k5ccgx_control_stream(sd, STREAM_STOP);
		break;

	case STREAM_MODE_CAM_ON:
		switch (state->sensor_mode) {
		case SENSOR_CAMERA:
			if (state->format_mode == V4L2_PIX_FMT_MODE_CAPTURE)
				err = s5k5ccgx_set_capture_start(sd);
			else
				err = s5k5ccgx_set_preview_start(sd);
			break;

		case SENSOR_MOVIE:
			err = s5k5ccgx_set_video_preview(sd);
			break;

		default:
			break;
		}
		break;

	case STREAM_MODE_MOVIE_ON:
		cam_info("movie on");
		state->recording = 1;
		if (state->flash_mode != FLASH_MODE_OFF)
			s5k5ccgx_flash_torch(sd, S5K5CCGX_FLASH_ON);
		break;

	case STREAM_MODE_MOVIE_OFF:
		cam_info("movie off");
		state->recording = 0;
		if (state->flash_on)
			s5k5ccgx_flash_torch(sd, S5K5CCGX_FLASH_OFF);
		break;

#ifdef CONFIG_VIDEO_IMPROVE_STREAMOFF
	case STREAM_MODE_WAIT_OFF:
		s5k5ccgx_wait_steamoff(sd);
		break;
#endif
	default:
		cam_err("%s: ERROR - Invalid stream mode\n", __func__);
		break;
	}

	CHECK_ERR_MSG(err, "failed\n");

	return 0;
}

static int s5k5ccgx_reset(struct v4l2_subdev *sd, u32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);

	cam_trace("EX\n");

	s5k5ccgx_return_focus(sd);
	state->initialized = 0;

	return 0;
}

static int s5k5ccgx_init(struct v4l2_subdev *sd, u32 val)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_dbg("%s: start\n", __func__);

	err = s5k5ccgx_init_regs(sd);
	CHECK_ERR_MSG(err, "failed to indentify sensor chip\n");

	if (state->hd_videomode) {
		cam_info("init: HD mode\n");
		err = S5K5CCGX_BURST_WRITE_REGS(sd, s5k5ccgx_hd_init_reg);
	} else {
		cam_info("init: Cam, Non-HD mode\n");
		err = S5K5CCGX_BURST_WRITE_REGS(sd, s5k5ccgx_init_reg);
	}
	CHECK_ERR_MSG(err, "failed to initialize camera device\n");

#ifdef CONFIG_VIDEO_S5K5CCGX_P8
	s5k5ccgx_set_from_table(sd, "antibanding",
		&state->regs->antibanding, 1, 0);
#endif

	state->runmode = S5K5CCGX_RUNMODE_INIT;

	/* Default state values */
	state->flash_mode = FLASH_MODE_OFF;
	state->scene_mode = SCENE_MODE_NONE;
	state->flash_on = 0;
	state->light_level = 0xFFFFFFFF;
	memset(&state->focus, 0, sizeof(state->focus));

	state->initialized = 1;

	if (state->sensor_mode == SENSOR_MOVIE)
		s5k5ccgx_init_param(sd);

	if (state->req_fps >= 0) {
		err = s5k5ccgx_set_frame_rate(sd, state->req_fps);
		CHECK_ERR(err);
	}

	return 0;
}

/*
 * s_config subdev ops
 * With camera device, we need to re-initialize
 * every single opening time therefor,
 * it is not necessary to be initialized on probe time.
 * except for version checking
 * NOTE: version checking is optional
 */
static int s5k5ccgx_s_config(struct v4l2_subdev *sd,
			int irq, void *platform_data)
{
	struct s5k5ccgx_state *state = to_state(sd);
	int i;
#ifdef CONFIG_LOAD_FILE
	int err = 0;
#endif

	if (!platform_data) {
		cam_err("%s: ERROR, no platform data\n", __func__);
		return -ENODEV;
	}
	state->pdata = platform_data;
	state->dbg_level = &state->pdata->dbg_level;

	/*
	 * Assign default format and resolution
	 * Use configured default information in platform data
	 * or without them, use default information in driver
	 */
	state->req_fmt.width = state->pdata->default_width;
	state->req_fmt.height = state->pdata->default_height;

	if (!state->pdata->pixelformat)
		state->req_fmt.pixelformat = DEFAULT_PIX_FMT;
	else
		state->req_fmt.pixelformat = state->pdata->pixelformat;

	if (!state->pdata->freq)
		state->freq = DEFAULT_MCLK;	/* 24MHz default */
	else
		state->freq = state->pdata->freq;

	state->preview = state->capture = NULL;
	state->sensor_mode = SENSOR_CAMERA;
	state->hd_videomode = 0;
	state->format_mode = V4L2_PIX_FMT_MODE_PREVIEW;
	state->fps = 0;
	state->req_fps = -1;

	for (i = 0; i < ARRAY_SIZE(s5k5ccgx_ctrls); i++)
		s5k5ccgx_ctrls[i].value = s5k5ccgx_ctrls[i].default_value;

#ifdef S5K5CCGX_SUPPORT_FLASH
	if (s5k5ccgx_is_hwflash_on(sd))
		state->ignore_flash = 1;
#endif

#if !defined(FEATURE_YUV_CAPTURE)
	state->jpeg.enable = 0;
	state->jpeg.quality = 100;
	state->jpeg.main_offset = 1280; /* 0x500 */

	/* Maximum size 2048 * 1536 * 2 = 6291456 */
	state->jpeg.main_size = SENSOR_JPEG_SNAPSHOT_MEMSIZE;

	state->jpeg.thumb_offset = 636; /* 0x27C */
	state->jpeg.thumb_size = 320 * 240 * 2; /* 320 * 240 * 2 = 153600 */
#endif

#ifdef CONFIG_LOAD_FILE
	err = loadFile();
	if (unlikely(err < 0)) {
		cam_err("failed to load file ERR=%d\n", err);
		return err;
	}
#endif

	return 0;
}

static const struct v4l2_subdev_core_ops s5k5ccgx_core_ops = {
	.init = s5k5ccgx_init,	/* initializing API */
	.g_ctrl = s5k5ccgx_g_ctrl,
	.s_ctrl = s5k5ccgx_s_ctrl,
	.s_ext_ctrls = s5k5ccgx_s_ext_ctrls,
	.reset = s5k5ccgx_reset,
};

static const struct v4l2_subdev_video_ops s5k5ccgx_video_ops = {
	.s_mbus_fmt = s5k5ccgx_s_mbus_fmt,
	.enum_framesizes = s5k5ccgx_enum_framesizes,
	.enum_mbus_fmt = s5k5ccgx_enum_mbus_fmt,
	.try_mbus_fmt = s5k5ccgx_try_mbus_fmt,
	.g_parm = s5k5ccgx_g_parm,
	.s_parm = s5k5ccgx_s_parm,
	.s_stream = s5k5ccgx_s_stream,
};

static const struct v4l2_subdev_ops s5k5ccgx_ops = {
	.core = &s5k5ccgx_core_ops,
	.video = &s5k5ccgx_video_ops,
};


/*
 * s5k5ccgx_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int s5k5ccgx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct s5k5ccgx_state *state;
	int err = -EINVAL;

	state = kzalloc(sizeof(struct s5k5ccgx_state), GFP_KERNEL);
	if (unlikely(!state)) {
		dev_err(&client->dev, "probe, fail to get memory\n");
		return -ENOMEM;
	}

	mutex_init(&state->ctrl_lock);
	mutex_init(&state->af_lock);

	state->runmode = S5K5CCGX_RUNMODE_NOTREADY;
	sd = &state->sd;
	strcpy(sd->name, S5K5CCGX_DRIVER_NAME);

	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &s5k5ccgx_ops);

	state->workqueue = create_workqueue("cam_workqueue");
	if (unlikely(!state->workqueue)) {
		dev_err(&client->dev, "probe, fail to create workqueue\n");
		goto err_out;
	}
	INIT_WORK(&state->af_work, s5k5ccgx_af_worker);
	INIT_WORK(&state->af_win_work, s5k5ccgx_af_win_worker);

	err = s5k5ccgx_s_config(sd, 0, client->dev.platform_data);
	CHECK_ERR_MSG(err, "fail to s_config\n");

	printk(KERN_DEBUG "%s %s: driver probed!!\n",
		dev_driver_string(&client->dev), dev_name(&client->dev));

	return 0;

err_out:
	kfree(state);
	return -ENOMEM;
}

static int s5k5ccgx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k5ccgx_state *state = to_state(sd);

	destroy_workqueue(state->workqueue);

	/* for softlanding */
	if (state->initialized) {
#ifdef CONFIG_VIDEO_S5K5CCGX_P4W
		s5k5ccgx_set_af_softlanding(sd);
#else
		s5k5ccgx_return_focus(sd);
#endif
	}

	/* Check whether flash is on when unlolading driver,
	 * to preventing Market App from controlling improperly flash.
	 * It isn't necessary in case that you power flash down
	 * in power routine to turn camera off.*/
	if (unlikely(state->flash_on && !state->ignore_flash))
		s5k5ccgx_flash_torch(sd, S5K5CCGX_FLASH_OFF);

	v4l2_device_unregister_subdev(sd);
	mutex_destroy(&state->ctrl_lock);
	mutex_destroy(&state->af_lock);
	kfree(state);

#ifdef CONFIG_LOAD_FILE
	large_file ? vfree(testBuf) : kfree(testBuf);
	large_file = 0;
	testBuf = NULL;
#endif

	printk(KERN_DEBUG "%s %s: driver removed!!\n",
		dev_driver_string(&client->dev), dev_name(&client->dev));
	return 0;
}

static const struct i2c_device_id s5k5ccgx_id[] = {
	{ S5K5CCGX_DRIVER_NAME, 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, s5k5ccgx_id);

static struct i2c_driver v4l2_i2c_driver = {
	.driver.name	= S5K5CCGX_DRIVER_NAME,
	.probe		= s5k5ccgx_probe,
	.remove		= s5k5ccgx_remove,
	.id_table	= s5k5ccgx_id,
};

static int __init v4l2_i2c_drv_init(void)
{
	pr_info("%s: %s called\n", __func__, S5K5CCGX_DRIVER_NAME); /* dslim*/
	return i2c_add_driver(&v4l2_i2c_driver);
}

static void __exit v4l2_i2c_drv_cleanup(void)
{
	pr_info("%s: %s called\n", __func__, S5K5CCGX_DRIVER_NAME); /* dslim*/
	i2c_del_driver(&v4l2_i2c_driver);
}

module_init(v4l2_i2c_drv_init);
module_exit(v4l2_i2c_drv_cleanup);

MODULE_DESCRIPTION("LSI S5K5CCGX 3MP SOC camera driver");
MODULE_AUTHOR("Dong-Seong Lim <dongseong.lim@samsung.com>");
MODULE_LICENSE("GPL");
