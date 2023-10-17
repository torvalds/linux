// SPDX-License-Identifier: GPL-2.0
/*
 * techpoint dev driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 */

#include "techpoint_dev.h"
#include "techpoint_tp9930.h"
#include "techpoint_tp9950.h"
#include "techpoint_tp2855.h"
#include "techpoint_tp2815.h"
#include "techpoint_tp9951.h"

static DEFINE_MUTEX(reg_sem);

int techpoint_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0) {
		usleep_range(300, 400);
		return 0;
	}

	dev_err(&client->dev,
		"techpoint write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

int techpoint_write_array(struct i2c_client *client,
			  const struct regval *regs, int size)
{
	int i, ret = 0;

	i = 0;

	while (i < size) {
		ret = techpoint_write_reg(client, regs[i].addr, regs[i].val);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}
		i++;
	}

	return ret;
}

int techpoint_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev, "techpoint read reg(0x%x) failed !\n", reg);

	return ret;
}

static int check_chip_id(struct techpoint *techpoint)
{
	struct i2c_client *client = techpoint->client;
	struct device *dev = &client->dev;
	unsigned char chip_id_h = 0xFF, chip_id_l = 0xFF;

	techpoint_read_reg(client, CHIP_ID_H_REG, &chip_id_h);
	techpoint_read_reg(client, CHIP_ID_L_REG, &chip_id_l);
	dev_err(dev, "chip_id_h:0x%2x chip_id_l:0x%2x\n", chip_id_h, chip_id_l);
	if (chip_id_h == TP9930_CHIP_ID_H_VALUE &&
	    chip_id_l == TP9930_CHIP_ID_L_VALUE) {		//tp2832
		dev_info(&client->dev,
			 "techpoint check chip id CHIP_TP9930 !\n");
		techpoint->chip_id = CHIP_TP9930;
		techpoint->input_type = TECHPOINT_DVP_BT1120;
		return 0;
	} else if (chip_id_h == TP2855_CHIP_ID_H_VALUE &&
		   chip_id_l == TP2855_CHIP_ID_L_VALUE) {	//tp2855
		dev_info(&client->dev,
			 "techpoint check chip id CHIP_TP2855 !\n");
		techpoint->chip_id = CHIP_TP2855;
		techpoint->input_type = TECHPOINT_MIPI;
		return 0;
	} else if (chip_id_h == TP2815_CHIP_ID_H_VALUE &&
		   chip_id_l == TP2815_CHIP_ID_L_VALUE) {	//tp2815
		dev_info(&client->dev,
			 "techpoint check chip id CHIP_TP2815 !\n");
		techpoint->chip_id = CHIP_TP2855;
		techpoint->input_type = TECHPOINT_MIPI;
		return 0;
	} else if (chip_id_h == TP9950_CHIP_ID_H_VALUE &&
		   chip_id_l == TP9950_CHIP_ID_L_VALUE) {	//tp2850
		dev_info(&client->dev,
			 "techpoint check chip id CHIP_TP9950 !\n");
		techpoint->chip_id = CHIP_TP9950;
		techpoint->input_type = TECHPOINT_MIPI;
		return 0;
	} else if (chip_id_h == TP9951_CHIP_ID_H_VALUE &&
		   chip_id_l == TP9951_CHIP_ID_L_VALUE) {	//tp2860
		dev_info(&client->dev,
			 "techpoint check chip id CHIP_TP9951 !\n");
		techpoint->chip_id = CHIP_TP9951;
		techpoint->input_type = TECHPOINT_MIPI;
		return 0;
	}

	dev_info(&client->dev, "techpoint check chip id failed !\n");
	return -1;
}

int techpoint_initialize_devices(struct techpoint *techpoint)
{
	if (check_chip_id(techpoint))
		return -1;

	if (techpoint->chip_id == CHIP_TP9930)
		tp9930_initialize(techpoint);
	else if (techpoint->chip_id == CHIP_TP2855)
		tp2855_initialize(techpoint);
	else if (techpoint->chip_id == CHIP_TP9950)
		tp9950_initialize(techpoint);
	else if (techpoint->chip_id == CHIP_TP9951)
		tp9951_initialize(techpoint);

	return 0;
}

static int detect_thread_function(void *data)
{
	struct techpoint *techpoint = (struct techpoint *)data;
	struct i2c_client *client = techpoint->client;
	u8 detect_status = 0, i;
	int need_reset_wait = -1;

	if (techpoint->power_on) {
		mutex_lock(&reg_sem);
		if (techpoint->chip_id == CHIP_TP9930) {
			tp9930_get_all_input_status(techpoint,
						    techpoint->detect_status);
			for (i = 0; i < PAD_MAX; i++)
				tp9930_set_decoder_mode(client, i,
							techpoint->detect_status[i]);
		} else if (techpoint->chip_id == CHIP_TP2855) {
			tp2855_get_all_input_status(techpoint,
						    techpoint->detect_status);
			for (i = 0; i < PAD_MAX; i++)
				tp2855_set_decoder_mode(client, i,
							techpoint->detect_status[i]);
		}
		mutex_unlock(&reg_sem);
		techpoint->do_reset = 0;
	}

	while (!kthread_should_stop()) {
		mutex_lock(&reg_sem);
		if (techpoint->power_on) {
			for (i = 0; i < PAD_MAX; i++) {
				if (techpoint->chip_id == CHIP_TP9930)
					detect_status =
					    tp9930_get_channel_input_status
					    (techpoint, i);
				else if (techpoint->chip_id == CHIP_TP2855)
					detect_status =
					    tp2855_get_channel_input_status
					    (techpoint, i);
				else if (techpoint->chip_id == CHIP_TP9951)
					detect_status =
					    tp9951_get_channel_input_status
					    (techpoint, i);

				if (techpoint->detect_status[i] !=
				    detect_status) {
					if (!detect_status)
						dev_info(&client->dev,
							"detect channel %d video plug out\n",
							i);
					else
						dev_info(&client->dev,
							"detect channel %d video plug in\n",
							i);

					if (techpoint->chip_id == CHIP_TP9930)
						tp9930_set_decoder_mode(client, i, detect_status);
					else if (techpoint->chip_id == CHIP_TP2855)
						tp2855_set_decoder_mode(client, i, detect_status);

					techpoint->detect_status[i] = detect_status;
					need_reset_wait = 5;
				}
			}
			if (need_reset_wait > 0) {
				need_reset_wait--;
			} else if (need_reset_wait == 0) {
				need_reset_wait = -1;
				techpoint->do_reset = 1;
				dev_info(&client->dev,
					"trigger reset time up\n");
			}
		}
		mutex_unlock(&reg_sem);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(200));
	}
	return 0;
}

static int __maybe_unused detect_thread_start(struct techpoint *techpoint)
{
	int ret = 0;
	struct i2c_client *client = techpoint->client;

	techpoint->detect_thread = kthread_create(detect_thread_function,
						  techpoint,
						  "techpoint_kthread");
	if (IS_ERR(techpoint->detect_thread)) {
		dev_err(&client->dev,
			"kthread_create techpoint_kthread failed\n");
		ret = PTR_ERR(techpoint->detect_thread);
		techpoint->detect_thread = NULL;
		return ret;
	}
	wake_up_process(techpoint->detect_thread);
	return ret;
}

static int __maybe_unused detect_thread_stop(struct techpoint *techpoint)
{
	if (techpoint->detect_thread)
		kthread_stop(techpoint->detect_thread);
	techpoint->detect_thread = NULL;
	return 0;
}

static __maybe_unused int auto_detect_channel_fmt(struct techpoint *techpoint)
{
	int ch = 0;
	enum techpoint_support_reso reso = 0xff;
	struct i2c_client *client = techpoint->client;

	mutex_lock(&reg_sem);

	for (ch = 0; ch < PAD_MAX; ch++) {
		if (techpoint->chip_id == CHIP_TP9930) {
			reso = tp9930_get_channel_reso(client, ch);
			tp9930_set_channel_reso(client, ch, reso);
		} else if (techpoint->chip_id == CHIP_TP2855) {
			reso = tp2855_get_channel_reso(client, ch);
			tp2855_set_channel_reso(client, ch, reso);
		}
	}

	if (techpoint->chip_id == CHIP_TP9950) {
		reso = tp9950_get_channel_reso(client, 0);
		tp9950_set_channel_reso(client, 0, reso);
	}

	if (techpoint->chip_id == CHIP_TP9951) {
		reso = tp9951_get_channel_reso(client, 0);
		tp9951_set_channel_reso(client, 0, reso);
	}

	mutex_unlock(&reg_sem);

	return 0;
}

void __techpoint_get_vc_fmt_inf(struct techpoint *techpoint,
				struct rkmodule_vc_fmt_info *inf)
{
	int ch = 0;
	int val = 0;
	enum techpoint_support_reso reso = 0xff;
	struct i2c_client *client = techpoint->client;

	mutex_lock(&reg_sem);

	for (ch = 0; ch < PAD_MAX; ch++) {
		if (techpoint->chip_id == CHIP_TP9930) {
			reso = tp9930_get_channel_reso(client, ch);
			techpoint->cur_video_mode->channel_reso[ch] = reso;
		} else if (techpoint->chip_id == CHIP_TP2855) {
			reso = tp2855_get_channel_reso(client, ch);
			techpoint->cur_video_mode->channel_reso[ch] = reso;
		}
		val = reso;
		switch (val) {
		case TECHPOINT_S_RESO_1080P_30:
			inf->width[ch] = 1920;
			inf->height[ch] = 1080;
			inf->fps[ch] = 30;
			break;
		case TECHPOINT_S_RESO_1080P_25:
			inf->width[ch] = 1920;
			inf->height[ch] = 1080;
			inf->fps[ch] = 25;
			break;
		case TECHPOINT_S_RESO_720P_30:
			inf->width[ch] = 1280;
			inf->height[ch] = 720;
			inf->fps[ch] = 30;
			break;
		case TECHPOINT_S_RESO_720P_25:
			inf->width[ch] = 1280;
			inf->height[ch] = 720;
			inf->fps[ch] = 25;
			break;
		case TECHPOINT_S_RESO_SD:
			inf->width[ch] = 720;
			inf->height[ch] = 560;
			inf->fps[ch] = 25;
			break;
		default:
#if DEF_1080P
			inf->width[ch] = 1920;
			inf->height[ch] = 1080;
			inf->fps[ch] = 25;
#else
			inf->width[ch] = 1280;
			inf->height[ch] = 720;
			inf->fps[ch] = 25;
#endif
			break;
		}
	}

	mutex_unlock(&reg_sem);
}

void techpoint_get_vc_fmt_inf(struct techpoint *techpoint,
			      struct rkmodule_vc_fmt_info *inf)
{
	mutex_lock(&reg_sem);

	if (techpoint->chip_id == CHIP_TP9930)
		tp9930_pll_reset(techpoint->client);

	techpoint_write_array(techpoint->client,
			      techpoint->cur_video_mode->common_reg_list,
			      techpoint->cur_video_mode->common_reg_size);

	if (techpoint->chip_id == CHIP_TP9930)
		tp9930_do_reset_pll(techpoint->client);

	mutex_unlock(&reg_sem);

	__techpoint_get_vc_fmt_inf(techpoint, inf);
}

void techpoint_get_vc_hotplug_inf(struct techpoint *techpoint,
				  struct rkmodule_vc_hotplug_info *inf)
{
	int ch = 0;
	int detect_status = 0;

	memset(inf, 0, sizeof(*inf));

	mutex_lock(&reg_sem);

	for (ch = 0; ch < 4; ch++) {
		if (techpoint->chip_id == CHIP_TP9930)
			detect_status =
			    tp9930_get_channel_input_status(techpoint, ch);
		else if (techpoint->chip_id == CHIP_TP2855)
			detect_status =
			    tp2855_get_channel_input_status(techpoint, ch);

		inf->detect_status |= detect_status << ch;
	}

	mutex_unlock(&reg_sem);
}

void techpoint_set_quick_stream(struct techpoint *techpoint, u32 stream)
{
	if (techpoint->chip_id == CHIP_TP2855)
		tp2855_set_quick_stream(techpoint, stream);
}

int techpoint_start_video_stream(struct techpoint *techpoint)
{
	int ret = 0;
	struct i2c_client *client = techpoint->client;

	mutex_lock(&reg_sem);
	if (techpoint->chip_id == CHIP_TP9930)
		tp9930_pll_reset(techpoint->client);
	mutex_unlock(&reg_sem);

	auto_detect_channel_fmt(techpoint);
	ret = techpoint_write_array(techpoint->client,
				    techpoint->cur_video_mode->common_reg_list,
				    techpoint->cur_video_mode->common_reg_size);
	if (ret) {
		dev_err(&client->dev,
			"%s common_reg_list failed", __func__);
		return ret;
	}

	mutex_lock(&reg_sem);
	if (techpoint->chip_id == CHIP_TP9930)
		tp9930_do_reset_pll(techpoint->client);
	mutex_unlock(&reg_sem);

	usleep_range(500 * 1000, 1000 * 1000);

	detect_thread_start(techpoint);

	return 0;
}

int techpoint_stop_video_stream(struct techpoint *techpoint)
{
	detect_thread_stop(techpoint);
	return 0;
}
