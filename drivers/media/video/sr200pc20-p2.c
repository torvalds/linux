/*
 * Driver for SR200PC20 from Samsung Electronics
 *
 *  2Mp CMOS Image Sensor SoC with an Embedded Image Processor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#ifdef CONFIG_VIDEO_SAMSUNG_V4L2
#include <linux/videodev2_exynos_camera.h>
#endif
#include <media/sr200pc20_platform.h>

#include "sr200pc20-p2.h"

#define DELAY_SEQ               0xFF

#define INIT_NUM_OF_REGS                (sizeof(front_init_50hz_regs) / sizeof(regs_short_t))
#define INIT_VT_NUM_OF_REGS             (sizeof(front_init_vt_50hz_regs) / sizeof(regs_short_t))
#define INIT_VT_15_NUM_OF_REGS             (sizeof(front_init_vt_15_50hz_regs) / sizeof(regs_short_t))
#define PREVIEW_CAMERA_NUM_OF_REGS      (sizeof(front_preview_camera_50hz_regs) / sizeof(regs_short_t))

#define SNAPSHOT_NORMAL_NUM_OF_REGS     (sizeof(front_snapshot_normal_regs) / sizeof(regs_short_t))

#define WB_AUTO_NUM_OF_REGS	            (sizeof(front_wb_auto_regs) / sizeof(regs_short_t))
#define WB_SUNNY_NUM_OF_REGS	        (sizeof(front_wb_sunny_regs) / sizeof(regs_short_t))
#define WB_CLOUDY_NUM_OF_REGS	        (sizeof(front_wb_cloudy_regs) / sizeof(regs_short_t))
#define WB_TUNSTEN_NUM_OF_REGS	        (sizeof(front_wb_tungsten_regs) / sizeof(regs_short_t))
#define WB_FLUORESCENT_NUM_OF_REGS	    (sizeof(front_wb_fluorescent_regs) / sizeof(regs_short_t))

#define EFFECT_NORMAL_NUM_OF_REGS	    (sizeof(front_effect_normal_regs) / sizeof(regs_short_t))
#define EFFECT_NEGATIVE_NUM_OF_REGS	    (sizeof(front_effect_negative_regs) / sizeof(regs_short_t))
#define EFFECT_SEPIA_NUM_OF_REGS	    (sizeof(front_effect_sepia_regs) / sizeof(regs_short_t))
#define EFFECT_MONO_NUM_OF_REGS         (sizeof(front_effect_mono_regs) / sizeof(regs_short_t))

#define EV_M4_NUM_OF_REGS	            (sizeof(front_ev_minus_4_regs) / sizeof(regs_short_t))
#define EV_M3_NUM_OF_REGS	            (sizeof(front_ev_minus_3_regs) / sizeof(regs_short_t))
#define EV_M2_NUM_OF_REGS	            (sizeof(front_ev_minus_2_regs) / sizeof(regs_short_t))
#define EV_M1_NUM_OF_REGS	            (sizeof(front_ev_minus_1_regs) / sizeof(regs_short_t))
#define EV_DEFAULT_NUM_OF_REGS	        (sizeof(front_ev_default_regs) / sizeof(regs_short_t))
#define EV_P1_NUM_OF_REGS	            (sizeof(front_ev_plus_1_regs) / sizeof(regs_short_t))
#define EV_P2_NUM_OF_REGS	            (sizeof(front_ev_plus_2_regs) / sizeof(regs_short_t))
#define EV_P3_NUM_OF_REGS	            (sizeof(front_ev_plus_3_regs) / sizeof(regs_short_t))
#define EV_P4_NUM_OF_REGS	            (sizeof(front_ev_plus_4_regs) / sizeof(regs_short_t))

#define FPS_AUTO_NUM_OF_REGS	        (sizeof(front_fps_auto_regs) / sizeof(regs_short_t))
#define FPS_7_NUM_OF_REGS	            (sizeof(front_fps_7_50hz_regs) / sizeof(regs_short_t))
#define FPS_10_NUM_OF_REGS	            (sizeof(front_fps_10_50hz_regs) / sizeof(regs_short_t))
#define FPS_15_NUM_OF_REGS	            (sizeof(front_fps_15_50hz_regs) / sizeof(regs_short_t))

#define PATTERN_ON_NUM_OF_REGS	        (sizeof(front_pattern_on_regs) / sizeof(regs_short_t))
#define PATTERN_OFF_NUM_OF_REGS	        (sizeof(front_pattern_off_regs) / sizeof(regs_short_t))

static int sr200pc20_i2c_read_byte(struct i2c_client *client,
							unsigned short subaddr,
							unsigned short *data);

static int sr200pc20_i2c_write_byte(struct i2c_client *client,
							unsigned short subaddr,
							unsigned short data);

static int sr200pc20_i2c_read_word(struct i2c_client *client,
							unsigned short subaddr,
							unsigned short *data);

static int sr200pc20_i2c_write_word(struct i2c_client *client,
							unsigned short subaddr,
							unsigned short data);

static int sr200pc20_i2c_set_data_burst(struct i2c_client *client,
								regs_short_t reg_buffer[],
								int num_of_regs);

static int sr200pc20_i2c_set_config_register(struct i2c_client *client,
									regs_short_t reg_buffer[],
									int num_of_regs,
									char *name);

#ifdef CONFIG_LOAD_FILE
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#define MAX_REG_TABLE_LEN 3500
#define MAX_ONE_LINE_LEN 500

typedef struct
{
    char name[100];
    char *location_ptr;
} reg_hash_t;

static char *front_regs_buf_ptr = NULL;
static char *front_curr_pos_ptr = NULL;
static char front_current_line[MAX_ONE_LINE_LEN];
static regs_short_t front_reg_table[MAX_REG_TABLE_LEN];
static int front_reg_num_of_element = 0;

static reg_hash_t front_reg_hash_table[] =
{
	{"front_init_50hz_regs",                   NULL},
	{"front_init_vt_50hz_regs",                NULL},
	{"front_init_vt_15_50hz_regs",             NULL},
	{"front_preview_camera_50hz_regs",         NULL},
	{"front_init_60hz_regs",				  NULL},
	{"front_init_vt_60hz_regs",				  NULL},
	{"front_init_vt_15_60hz_regs",			  NULL},
	{"front_preview_camera_60hz_regs",		  NULL},
	{"front_snapshot_normal_regs",        NULL},
	{"front_ev_minus_4_regs",             NULL},
	{"front_ev_minus_3_regs",             NULL},
	{"front_ev_minus_2_regs",             NULL},
	{"front_ev_minus_1_regs",             NULL},
	{"front_ev_default_regs",             NULL},
	{"front_ev_plus_1_regs",              NULL},
	{"front_ev_plus_2_regs",              NULL},
	{"front_ev_plus_3_regs",              NULL},
	{"front_ev_plus_4_regs",              NULL},
	{"front_wb_auto_regs",                NULL},
	{"front_wb_sunny_regs",               NULL},
	{"front_wb_cloudy_regs",              NULL},
	{"front_wb_tungsten_regs",            NULL},
	{"front_wb_fluorescent_regs",         NULL},
	{"front_effect_normal_regs",          NULL},
	{"front_effect_negative_regs",        NULL},
	{"front_effect_sepia_regs",           NULL},
	{"front_effect_mono_regs",            NULL},
	{"front_fps_auto_regs",               NULL},
	{"front_fps_7_50hz_regs",                  NULL},
	{"front_fps_10_50hz_regs",                 NULL},
	{"front_fps_15_50hz_regs",                 NULL},
	{"front_fps_7_60hz_regs",				  NULL},
	{"front_fps_10_60hz_regs",				  NULL},
	{"front_fps_15_60hz_regs",				  NULL},
	{"front_pattern_on_regs",             NULL},
	{"front_pattern_off_regs",            NULL},
};

static bool sr200pc20_regs_get_line(char *line_buf)
{
	int i;
	char *r_n_ptr = NULL;

	memset(line_buf, 0, MAX_ONE_LINE_LEN);

	r_n_ptr = strstr(front_curr_pos_ptr, "\n");

	if(r_n_ptr ) {
		for(i = 0; i < MAX_ONE_LINE_LEN; i++) {
			if(front_curr_pos_ptr + i == r_n_ptr) {
				front_curr_pos_ptr = r_n_ptr + 1;
				break;
			}
			line_buf[i] = front_curr_pos_ptr[i];
		}
		line_buf[i] = '\0';

		return true;
	} else {
		if(strlen(front_curr_pos_ptr) > 0) {
			strcpy(line_buf, front_curr_pos_ptr);
			return true;
		} else {
			return false;
		}
	}
}

static bool sr200pc20_regs_trim(char *line_buf)
{
	int left_index;
	int buff_len;
	int i;

	buff_len = strlen(line_buf);
	left_index  = -1;

	if(buff_len == 0)
		return false;

	/* Find the first letter that is not a white space from left side */
	for(i = 0; i < buff_len; i++) {
		if((line_buf[i] != ' ') && (line_buf[i] != '\t') && (line_buf[i] != '\n') && (line_buf[i] != '\r'))	{
			left_index = i;
			break;
		}
	}

	if(left_index == -1) {
		return false;
	}

	if((line_buf[left_index] == '\0') || ((line_buf[left_index] == '/') && (line_buf[left_index + 1] == '/')))	{
		return false;
	}

	if(left_index != 0)	{
		strcpy(line_buf, line_buf + left_index);
	}

	return true;
}

static int sr200pc20_regs_parse_table(void)
{
#if 0 /* Parsing a register format : {0x0000, 0x0000}, */
	char reg_buf[7], data_buf[7];
	int reg_index = 0;

	reg_buf[6] = '\0';
	data_buf[6] = '\0';

    while(sr200pc20_regs_get_line(front_current_line))
    {
        if(sr200pc20_regs_trim(front_current_line) == false)
        {
            continue;
        }

        /* Check End line of a table.*/
        if((front_current_line[0] == '}') && (front_current_line[1] == ';'))
        {
            break;
        }

        /* Parsing a register format : {0x0000, 0x0000},*/
        if((front_current_line[0] == '{') && (front_current_line[1] == '0') && (front_current_line[15] == '}'))
        {
            memcpy(reg_buf, (const void *)&front_current_line[1], 6);
            memcpy(data_buf, (const void *)&front_current_line[9], 6);

            front_reg_table[reg_index].subaddr = (unsigned short)simple_strtoul(reg_buf, NULL, 16);
            front_reg_table[reg_index].value = (unsigned int)simple_strtoul(data_buf, NULL, 16);

            reg_index++;
        }
    }

#else /* Parsing a register format : {0x00, 0x00}, */

	char reg_buf[5], data_buf[5];
	int reg_index = 0;

	reg_buf[4] = '\0';
	data_buf[4] = '\0';

	while(sr200pc20_regs_get_line(front_current_line)) {
		if(sr200pc20_regs_trim(front_current_line) == false)
			continue;

	/* Check End line of a table.*/
	if((front_current_line[0] == '}') && (front_current_line[1] == ';'))
		break;

	/* Parsing a register format : {0x00, 0x00},*/
	if((front_current_line[0] == '{') && (front_current_line[1] == '0') && (front_current_line[11] == '}'))	{
		memcpy(reg_buf, (const void *)&front_current_line[1], 4);
		memcpy(data_buf, (const void *)&front_current_line[7], 4);

		front_reg_table[reg_index].subaddr = (unsigned short)simple_strtoul(reg_buf, NULL, 16);
		front_reg_table[reg_index].value = (unsigned int)simple_strtoul(data_buf, NULL, 16);

	reg_index++;
	}
	}
#endif

    return reg_index;
}

static int sr200pc20_regs_table_write(struct i2c_client *client, char *name)
{
	bool bFound_table = false;
	int i, err = 0;

	front_reg_num_of_element = 0;

	for(i = 0; i < sizeof(front_reg_hash_table)/sizeof(reg_hash_t); i++) {
		if(strcmp(name, front_reg_hash_table[i].name) == 0) {
			bFound_table = true;

			front_curr_pos_ptr = front_reg_hash_table[i].location_ptr;
			break;
		}
	}

	if(bFound_table) {
		front_reg_num_of_element = sr200pc20_regs_parse_table();
	} else {
		cam_err("%s doesn't exist\n", name);
		return -EIO;
	}

	err = sr200pc20_i2c_set_data_burst(client, front_reg_table, front_reg_num_of_element);
	if(err < 0) {
		cam_err(" ERROR! sr200pc20_i2c_set_data_burst failed\n");
		return -EIO;
	}

	return err;
}

int sr200pc20_regs_table_init(void)
{
	struct file *filp;
	char *dp;
	long l;
	loff_t pos;
	int ret, i, retry_cnt;
	mm_segment_t fs = get_fs();
	char *location_ptr = NULL;
	bool bFound_name;

	cam_dbg("E\n");

	set_fs(get_ds());
#if defined (CONFIG_MACH_P2_REV02)
	filp = filp_open("/mnt/sdcard/sr200pc20_regs-p2.h", O_RDONLY, 0);
#else
	filp = filp_open("/mnt/sdcard/sr200pc20_regs.h", O_RDONLY, 0);
#endif
	if(IS_ERR(filp)) {
		cam_err("file open error\n");
		return -EIO;
	}
	l = filp->f_path.dentry->d_inode->i_size;
	cam_info("file size = %ld\n",l);

	//msleep(50);
	cam_dbg("Start vmalloc\n");
	for(retry_cnt = 5; retry_cnt > 0; retry_cnt--) {
		dp = kmalloc(l, GFP_KERNEL);
		if(dp != NULL)
			break;

		msleep(50);
	}
	if(dp == NULL) {
		cam_err("Out of Memory\n");
		filp_close(filp, current->files);
		return -ENOMEM;
	}
	cam_dbg("End vmalloc\n");

	pos = 0;
	memset(dp, 0, l);
	cam_dbg("Start vfs_read\n");
	ret = vfs_read(filp, (char __user *)dp, l, &pos);
	if(ret != l) {
		cam_err("Failed to read file\n");
		vfree(dp);
		filp_close(filp, current->files);
		return -EINVAL;
	}
	cam_dbg("End vfs_read\n");

	filp_close(filp, current->files);

	set_fs(fs);

	front_regs_buf_ptr = dp;

	*((front_regs_buf_ptr + l) - 1) = '\0';

	/* Make hash table to enhance speed.*/
	front_curr_pos_ptr = front_regs_buf_ptr;
	location_ptr = front_curr_pos_ptr;

	for(i = 0; i < sizeof(front_reg_hash_table)/sizeof(reg_hash_t); i++) {
		front_reg_hash_table[i].location_ptr = NULL;
		bFound_name = false;

		while(sr200pc20_regs_get_line(front_current_line)) {
			if(strstr(front_current_line, front_reg_hash_table[i].name) != NULL) {
				bFound_name = true;
				front_reg_hash_table[i].location_ptr = location_ptr;
				break;
			}
			location_ptr = front_curr_pos_ptr;
		}

		if(bFound_name == false) {
			if(i == 0)	{
				cam_err(" ERROR! Couldn't find the reg name in hash table\n");
				return -EIO;
			} else {
				front_curr_pos_ptr = front_reg_hash_table[i-1].location_ptr;
			}
			location_ptr = front_curr_pos_ptr;

			cam_err(" ERROR! Couldn't find the reg name in hash table\n");
		}
	}

	cam_dbg("X\n");

	return 0;
}

void sr200pc20_regs_table_exit(void)
{
	cam_dbg(" start\n");

	if(front_regs_buf_ptr) {
		vfree(front_regs_buf_ptr);
		front_regs_buf_ptr = NULL;
	}

	cam_dbg(" done\n");
}
#endif

static int sr200pc20_i2c_read_byte(struct i2c_client *client,
                                     unsigned short subaddr,
                                     unsigned short *data)
{
	unsigned char buf[2] = {0,};
	struct i2c_msg msg = {client->addr, 0, 1, buf};
	int err = 0;

	if(!client->adapter) {
		cam_err("ERROR! can't search i2c client adapter\n");
		return -EIO;
	}

	buf[0] = (unsigned char)subaddr;

	err = i2c_transfer(client->adapter, &msg, 1);
	if(err < 0) {
		cam_err(" ERROR! %d register read failed\n",subaddr);
		return -EIO;
	}

	msg.flags = I2C_M_RD;
	msg.len = 1;

	err = i2c_transfer(client->adapter, &msg, 1);
	if(err < 0) {
		cam_err(" ERROR! %d register read failed\n",subaddr);
		return -EIO;
	}

	*data = (unsigned short)buf[0];

	return 0;
}

static int sr200pc20_i2c_write_byte(struct i2c_client *client,
                                     unsigned short subaddr,
                                     unsigned short data)
{
	unsigned char buf[2] = {0,};
	struct i2c_msg msg = {client->addr, 0, 2, buf};
	int err = 0;

	if(!client->adapter) {
		cam_err(" ERROR! can't search i2c client adapter\n");
		return -EIO;
	}

	buf[0] = subaddr & 0xFF;
	buf[1] = data & 0xFF;

	err = i2c_transfer(client->adapter, &msg, 1);

	return (err == 1)? 0 : -EIO;
}

static int sr200pc20_i2c_read_word(struct i2c_client *client,
                                     unsigned short subaddr,
                                     unsigned short *data)
{
	unsigned char buf[4];
	struct i2c_msg msg = {client->addr, 0, 2, buf};
	int err = 0;

	if(!client->adapter) {
		cam_err(" ERROR! can't search i2c client adapter\n");
		return -EIO;
	}

	buf[0] = subaddr>> 8;
	buf[1] = subaddr & 0xff;

	err = i2c_transfer(client->adapter, &msg, 1);
	if(err < 0) {
		cam_err(" ERROR! %d register read failed\n", subaddr);
		return -EIO;
	}

	msg.flags = I2C_M_RD;
	msg.len = 2;

	err = i2c_transfer(client->adapter, &msg, 1);
	if(err < 0) {
		cam_err(" ERROR! %d register read failed\n", subaddr);
		return -EIO;
	}

	*data = ((buf[0] << 8) | buf[1]);

	return 0;
}

static int sr200pc20_i2c_write_word(struct i2c_client *client,
                                     unsigned short subaddr,
                                     unsigned short data)
{
	unsigned char buf[4];
	struct i2c_msg msg = {client->addr, 0, 4, buf};
	int err = 0;

	if(!client->adapter) {
		cam_err(" ERROR! can't search i2c client adapter\n");
		return -EIO;
	}

	buf[0] = subaddr >> 8;
	buf[1] = subaddr & 0xFF;
	buf[2] = data >> 8;
	buf[3] = data & 0xFF;

	err = i2c_transfer(client->adapter, &msg, 1);

	return (err == 1)? 0 : -EIO;
}

static int sr200pc20_i2c_set_data_burst(struct i2c_client *client,
                                         regs_short_t reg_buffer[],
					                 int num_of_regs)
{
	unsigned short subaddr, data_value;
	int i, err = 0;

	for(i = 0; i < num_of_regs; i++) {
		subaddr = reg_buffer[i].subaddr;
		data_value = reg_buffer[i].value;

		switch(subaddr) {
		case DELAY_SEQ:
			cam_err("delay = %d\n",data_value*10);
			msleep(data_value * 10);
			break;
		default:
			err = sr200pc20_i2c_write_byte(client, subaddr, data_value);
			if(err < 0) {
				cam_err("i2c write fail\n");
				return -EIO;
			}
			break;
		}
	}

	return 0;
}

static int sr200pc20_i2c_set_config_register(struct i2c_client *client,
                                         regs_short_t reg_buffer[],
					                 int num_of_regs,
					                 char *name)
{
	int err = 0;

	cam_err("sr200pc20_i2c_set_config_register E: %s, %d\n",  name, err);

#ifdef CONFIG_LOAD_FILE
	err = sr200pc20_regs_table_write(client, name);
#else
	err = sr200pc20_i2c_set_data_burst(client, reg_buffer, num_of_regs);
#endif

	cam_err("sr200pc20_i2c_set_config_register X: %s, %d\n",  name, err);

	return err;
}

static int sr200pc20_get_iso_speed_rate(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned short read_value;
	int gain;
	int isospeedrating = 0;

	sr200pc20_i2c_write_byte(client, 0x03, 0x20);
	sr200pc20_i2c_read_byte(client, 0xb0, &read_value);
	gain = ((int)read_value * 100  / 32) + 50;

	if (read_value < 125)
		isospeedrating = 50;
	else if (read_value < 175)
		isospeedrating = 100;
	else if (read_value < 250)
		isospeedrating = 200;
	else if (read_value < 375)
		isospeedrating = 400;
	else if (read_value < 550)
		isospeedrating = 800;
	else
		isospeedrating = 1600;

	cam_dbg("get iso = %d\n", isospeedrating);
	return isospeedrating;
}

static int sr200pc20_get_shutterspeed(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned short read_value;
	int cintr;
	int ShutterSpeed = 0;

	sr200pc20_i2c_write_byte(client, 0x03, 0x20);
	sr200pc20_i2c_read_byte(client, 0x80, &read_value);
	cintr = (int)read_value << 19;
	sr200pc20_i2c_read_byte(client, 0x81, &read_value);
	cintr += (int)read_value << 11;
	sr200pc20_i2c_read_byte(client, 0x82, &read_value);
	cintr += (int)read_value << 3;

	cintr = cintr/24;
	ShutterSpeed =  1000000/cintr;

	cam_dbg("get shutterspeed = %d\n", ShutterSpeed);
	return ShutterSpeed;
}

static int sr200pc20_get_exif(struct v4l2_subdev *sd)
{
	struct sr200pc20_state *state = to_state(sd);

	state->exif.shutter_speed = 100;
	state->exif.iso = 0;

	/* Get shutter speed */
	state->exif.shutter_speed = sr200pc20_get_shutterspeed(sd);

	/* Get ISO */
	state->exif.iso = sr200pc20_get_iso_speed_rate(sd);

	cam_dbg("Shutter speed=%d, ISO=%d\n",state->exif.shutter_speed, state->exif.iso);
	return 0;
}

static int sr200pc20_check_dataline(struct v4l2_subdev *sd, s32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	int err = 0;

	cam_info("DTP %s\n", val ? "ON" : "OFF");

	if (val) {
		cam_dbg("load sr200pc20_pattern_on\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_pattern_on_regs,
										  PATTERN_ON_NUM_OF_REGS,
										  "front_pattern_on_regs");
	} else {
		cam_dbg("load sr200pc20_pattern_off\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_pattern_off_regs,
										  PATTERN_OFF_NUM_OF_REGS,
										  "front_pattern_off_regs");
	}
	if (unlikely(err)) {
		cam_err("fail to DTP setting\n");
		return err;
	}

	return 0;
}

static int sr200pc20_set_preview_start(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	cam_info("reset preview\n");

	cam_dbg("load sr200pc20_preview\n");
	if(state->anti_banding == ANTI_BANDING_50HZ) {
	err = sr200pc20_i2c_set_config_register(client,
									front_preview_camera_50hz_regs,
									PREVIEW_CAMERA_NUM_OF_REGS,
									"front_preview_camera_50hz_regs");
	} else {
	err = sr200pc20_i2c_set_config_register(client,
									front_preview_camera_60hz_regs,
									PREVIEW_CAMERA_NUM_OF_REGS,
									"front_preview_camera_60hz_regs");
	}

	if (state->check_dataline)
		err = sr200pc20_check_dataline(sd, 1);
	if (unlikely(err)) {
		cam_err("fail to make preview\n");
		return err;
	}

	return 0;
}

static int sr200pc20_set_preview_stop(struct v4l2_subdev *sd)
{
	int err = 0;
	cam_info("do nothing.\n");

	return err;
}

static int sr200pc20_set_capture_start(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = -EINVAL;

	/* set initial regster value */
	cam_dbg("load sr200pc20_capture\n");
	err = sr200pc20_i2c_set_config_register(client,
									front_snapshot_normal_regs,
									SNAPSHOT_NORMAL_NUM_OF_REGS,
									"front_snapshot_normal_regs");
	if (unlikely(err)) {
		cam_err("failed to make capture\n");
		return err;
	}
	sr200pc20_get_exif(sd);
	cam_info("Capture ConfigSync\n");
	return err;
}

static int sr200pc20_set_sensor_mode(struct v4l2_subdev *sd,
					struct v4l2_control *ctrl)
{
	struct sr200pc20_state *state = to_state(sd);

	if ((ctrl->value != SENSOR_CAMERA) &&
	(ctrl->value != SENSOR_MOVIE)) {
		cam_err("ERR: Not support.(%d)\n", ctrl->value);
		return -EINVAL;
	}

	/* We does not support movie mode when in VT. */
	if ((ctrl->value == SENSOR_MOVIE) && state->vt_mode) {
		state->sensor_mode = SENSOR_CAMERA;
		cam_warn("ERR: Not support movie\n");
	} else
		state->sensor_mode = ctrl->value;

	return 0;
}

#ifdef NEW_CAM_DRV
static int sr200pc20_g_mbus_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *fmt)
#else
static int sr200pc20_g_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
#endif
{
	cam_dbg("E\n");
	return 0;
}

static int sr200pc20_enum_framesizes(struct v4l2_subdev *sd, \
					struct v4l2_frmsizeenum *fsize)
{
	struct sr200pc20_state *state = to_state(sd);

	cam_dbg("E\n");

	/*
	 * Return the actual output settings programmed to the camera
	 */
	if (state->req_fmt.priv == V4L2_PIX_FMT_MODE_CAPTURE) {
		fsize->discrete.width = state->capture_frmsizes.width;
		fsize->discrete.height = state->capture_frmsizes.height;
	} else {
		fsize->discrete.width = state->preview_frmsizes.width;
		fsize->discrete.height = state->preview_frmsizes.height;
	}

	cam_info("enum_framesizes: width - %d , height - %d\n",
		fsize->discrete.width, fsize->discrete.height);

	return 0;
}

#ifdef NEW_CAM_DRV
static int sr200pc20_try_mbus_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *fmt)
#else
static int sr200pc20_try_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
#endif
{
	int err = 0;

	cam_dbg("E\n");

	return err;
}

#ifdef NEW_CAM_DRV
static int sr200pc20_s_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
#else
static int sr200pc20_s_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
#endif
{
	struct sr200pc20_state *state = to_state(sd);
	u32 *width = NULL, *height = NULL;

	cam_dbg("E\n");
	/*
	 * Just copying the requested format as of now.
	 * We need to check here what are the formats the camera support, and
	 * set the most appropriate one according to the request from FIMC
	 */
#ifdef NEW_CAM_DRV
	v4l2_fill_pix_format(&state->req_fmt, fmt);
	state->req_fmt.priv = fmt->field;
#else
	memcpy(&state->req_fmt, &fmt->fmt.pix, sizeof(fmt->fmt.pix));
#endif

	switch (state->req_fmt.priv) {
	case V4L2_PIX_FMT_MODE_PREVIEW:
		width = &state->preview_frmsizes.width;
		height = &state->preview_frmsizes.height;
		break;

	case V4L2_PIX_FMT_MODE_CAPTURE:
		width = &state->capture_frmsizes.width;
		height = &state->capture_frmsizes.height;
		break;

	default:
		cam_err("ERR(EINVAL)\n");
		return -EINVAL;
	}

	if ((*width != state->req_fmt.width) ||
		(*height != state->req_fmt.height)) {
		cam_err("ERR: Invalid size. width= %d, height= %d\n",
			state->req_fmt.width, state->req_fmt.height);
	}

	return 0;
}

static int sr200pc20_set_frame_rate(struct v4l2_subdev *sd, u32 fps)
{
	struct sr200pc20_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = 0;

	cam_info("frame rate %d\n\n", fps);

	switch (fps) {
	case 7:
		if(state->anti_banding == ANTI_BANDING_50HZ) {
		cam_dbg("load front_fps_7_50hz_regs\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_fps_7_50hz_regs,
										  FPS_7_NUM_OF_REGS,
										  "front_fps_7_50hz_regs");
		} else {
		cam_dbg("load front_fps_7_60hz_regs\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_fps_7_60hz_regs,
										  FPS_7_NUM_OF_REGS,
										  "front_fps_7_60hz_regs");
		}
		break;
	case 10:
		if(state->anti_banding == ANTI_BANDING_50HZ) {
		cam_dbg("load front_fps_10_50hz_regs\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_fps_10_50hz_regs,
										  FPS_10_NUM_OF_REGS,
										  "front_fps_10_50hz_regs");
		} else {
		cam_dbg("load front_fps_10_60hz_regs\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_fps_10_60hz_regs,
										  FPS_10_NUM_OF_REGS,
										  "front_fps_10_60hz_regs");
		}
		break;
	case 15:
		if(state->anti_banding == ANTI_BANDING_50HZ) {
		cam_dbg("load front_fps_15_50hz_regs\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_fps_15_50hz_regs,
										  FPS_15_NUM_OF_REGS,
										  "front_fps_15_50hz_regs");
		} else {
		cam_dbg("load front_fps_15_60hz_regs\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_fps_15_60hz_regs,
										  FPS_15_NUM_OF_REGS,
										  "front_fps_15_60hz_regs");
		}
		break;
	default:
		err = sr200pc20_i2c_set_config_register(client,
										  front_fps_auto_regs,
										  FPS_AUTO_NUM_OF_REGS,
										  "front_fps_auto_regs");
		break;
	}
	if (unlikely(err < 0)) {
		cam_err("i2c_write for set framerate\n");
		return -EIO;
	}

	return err;
}

static int sr200pc20_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	int err = 0;

	cam_dbg("E\n");

	return err;
}

static int sr200pc20_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	int err = 0;
	u32 fps = 0;
	struct sr200pc20_state *state = to_state(sd);

	if (!state->vt_mode)
		return 0;

	cam_dbg("E\n");

	fps = parms->parm.capture.timeperframe.denominator /
			parms->parm.capture.timeperframe.numerator;

	if (fps != state->set_fps) {
		if (fps < 0 && fps > 15) {
			cam_err("invalid frame rate %d\n", fps);
			fps = 15;
		}
		state->req_fps = fps;

		if (state->initialized) {
			err = sr200pc20_set_frame_rate(sd, state->req_fps);
			if (err >= 0)
				state->set_fps = state->req_fps;
		}

	}

	return err;
}

static int sr200pc20_control_stream(struct v4l2_subdev *sd, stream_cmd_t cmd)
{
	int err = 0;

	switch (cmd) {
	case STREAM_START:
		cam_warn("WARN: do nothing\n");
		break;

	case STREAM_STOP:
		cam_dbg("stream stop!!!\n");
		break;
	default:
		cam_err("ERR: Invalid cmd\n");
		break;
	}

	if (unlikely(err))
		cam_err("failed to stream start(stop)\n");

	return err;
}

static int sr200pc20_init(struct v4l2_subdev *sd, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	cam_dbg("E\n");

	/* set initial regster value */
#ifdef CONFIG_LOAD_FILE
	err = sr200pc20_regs_table_init();
	if(err < 0)
	{
		cam_err("sr200pc20_regs_table_init failed\n");
		return -ENOIOCTLCMD;
	}
#endif

	cam_dbg("sr200pc20_init: anti_banding = %d\n", state->anti_banding);

	if (state->sensor_mode == SENSOR_CAMERA) {
		if (!state->vt_mode) {
			if(state->anti_banding == ANTI_BANDING_50HZ) {
			cam_info("load camera common 50hz setting\n");
			err = sr200pc20_i2c_set_config_register(client,
											  front_init_50hz_regs,
											  INIT_NUM_OF_REGS,
											  "front_init_50hz_regs");
			} else {
			cam_info("load camera common 60hz setting\n");
			err = sr200pc20_i2c_set_config_register(client,
											  front_init_60hz_regs,
											  INIT_NUM_OF_REGS,
											  "front_init_60hz_regs");
			}
		} else {
			if(state->anti_banding == ANTI_BANDING_50HZ) {
			cam_info("load camera WIFI VT call 50hz setting\n");
			err = sr200pc20_i2c_set_config_register(client,
											  front_init_vt_15_50hz_regs,
											  INIT_VT_15_NUM_OF_REGS,
											  "front_init_vt_15_50hz_regs");
			} else {
			cam_info("load camera WIFI VT call 60hz setting\n");
			err = sr200pc20_i2c_set_config_register(client,
											  front_init_vt_15_60hz_regs,
											  INIT_VT_15_NUM_OF_REGS,
											  "front_init_vt_15_60hz_regs");
			}
		}
	} else {
		if(state->anti_banding == ANTI_BANDING_50HZ) {
		cam_info("load recording 50hz setting\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_init_vt_50hz_regs,
										  INIT_VT_NUM_OF_REGS,
										  "front_init_vt_50hz_regs");
		} else {
		cam_info("load recording 60hz setting\n");
		err = sr200pc20_i2c_set_config_register(client,
										  front_init_vt_60hz_regs,
										  INIT_VT_NUM_OF_REGS,
										  "front_init_vt_60hz_regs");
		}
	}
	if (unlikely(err)) {
		cam_err("failed to init\n");
		return err;
	}

	/* We stop stream-output from sensor when starting camera. */
	err = sr200pc20_control_stream(sd, STREAM_STOP);
	if (unlikely(err < 0))
		return err;
	msleep(150);

	if (state->vt_mode && (state->req_fps != state->set_fps)) {
		err = sr200pc20_set_frame_rate(sd, state->req_fps);
		if (unlikely(err < 0))
			return err;
		else
			state->set_fps = state->req_fps;
	}

	state->initialized = 1;

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
static int sr200pc20_s_config(struct v4l2_subdev *sd,
		int irq, void *platform_data)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20_state *state = to_state(sd);
	struct sr200pc20_platform_data *pdata;

	cam_dbg("E\n");

	state->initialized = 0;
	state->req_fps = state->set_fps = 8;
	state->sensor_mode = SENSOR_CAMERA;

	pdata = client->dev.platform_data;

	if (!pdata) {
		cam_err("no platform data\n");
		return -ENODEV;
	}

	/*
	 * Assign default format and resolution
	 * Use configured default information in platform data
	 * or without them, use default information in driver
	 */
	if (!(pdata->default_width && pdata->default_height)) {
		state->preview_frmsizes.width = DEFAULT_PREVIEW_WIDTH;
		state->preview_frmsizes.height = DEFAULT_PREVIEW_HEIGHT;

	} else {
		state->preview_frmsizes.width = pdata->default_width;
		state->preview_frmsizes.height = pdata->default_height;
	}
	state->capture_frmsizes.width = DEFAULT_CAPTURE_WIDTH;
	state->capture_frmsizes.height = DEFAULT_CAPTURE_HEIGHT;

	cam_dbg("preview_width: %d , preview_height: %d, "
		"capture_width: %d, capture_height: %d",
		state->preview_frmsizes.width, state->preview_frmsizes.height,
		state->capture_frmsizes.width, state->capture_frmsizes.height);

	state->req_fmt.width = state->preview_frmsizes.width;
	state->req_fmt.height = state->preview_frmsizes.height;
	if (!pdata->pixelformat)
		state->req_fmt.pixelformat = DEFAULT_FMT;
	else
		state->req_fmt.pixelformat = pdata->pixelformat;

	return 0;
}

static int sr200pc20_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sr200pc20_state *state = to_state(sd);
	/* struct i2c_client *client = v4l2_get_subdevdata(sd); */
	int err = 0;

	cam_info("stream mode = %d\n", enable);

	switch (enable) {
	case STREAM_MODE_CAM_OFF:
		if (state->sensor_mode == SENSOR_CAMERA) {
			if (state->check_dataline)
				err = sr200pc20_check_dataline(sd, 0);
			else
				err = sr200pc20_control_stream(sd, STREAM_STOP);
		}
		break;

	case STREAM_MODE_CAM_ON:
		/* The position of this code need to be adjusted later */
		if (state->sensor_mode == SENSOR_CAMERA) {
			if (state->req_fmt.priv == V4L2_PIX_FMT_MODE_CAPTURE)
				err = sr200pc20_set_capture_start(sd);
			else
				err = sr200pc20_set_preview_start(sd);
		}
		break;

	case STREAM_MODE_MOVIE_ON:
		cam_dbg("do nothing(movie on)!!\n");
		break;

	case STREAM_MODE_MOVIE_OFF:
		cam_dbg("do nothing(movie off)!!\n");
		break;

	default:
		cam_err("ERR: Invalid stream mode\n");
		break;
	}

	if (unlikely(err < 0)) {
		cam_err("ERR: faild\n");
		return err;
	}

	return 0;
}

static int sr200pc20_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = 0;

	cam_dbg("ctrl->id : %d\n", ctrl->id - V4L2_CID_PRIVATE_BASE);

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_EXIF_EXPTIME:
		ctrl->value = state->exif.shutter_speed;
		break;
	case V4L2_CID_CAMERA_EXIF_ISO:
		ctrl->value = state->exif.iso;
		break;
	default:
		cam_err("no such control id %d\n",
				ctrl->id - V4L2_CID_PRIVATE_BASE);
		break;
	}

	return err;
}

static int sr200pc20_set_brightness(struct v4l2_subdev *sd, struct v4l2_control *ctrl) {
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	cam_dbg("E\n");

	if (state->check_dataline)
		return 0;

	switch (ctrl->value) {
	case EV_MINUS_4:
		cam_dbg("load sr200pc20_bright_m4\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_minus_4_regs,
										EV_M4_NUM_OF_REGS,
										"front_ev_minus_4_regs");
		break;
	case EV_MINUS_3:
		cam_dbg("load sr200pc20_bright_m3\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_minus_3_regs,
										EV_M3_NUM_OF_REGS,
										"front_ev_minus_3_regs");
		break;
	case EV_MINUS_2:
		cam_dbg("load sr200pc20_bright_m2\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_minus_2_regs,
										EV_M2_NUM_OF_REGS,
										"front_ev_minus_2_regs");
		break;
	case EV_MINUS_1:
		cam_dbg("load sr200pc20_bright_m1\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_minus_1_regs,
										EV_M1_NUM_OF_REGS,
										"front_ev_minus_1_regs");
		break;
	case EV_DEFAULT:
		cam_dbg("load sr200pc20_bright_default\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_default_regs,
										EV_DEFAULT_NUM_OF_REGS,
										"front_ev_default_regs");
		break;
	case EV_PLUS_1:
		cam_dbg("load sr200pc20_bright_p1\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_plus_1_regs,
										EV_P1_NUM_OF_REGS,
										"front_ev_plus_1_regs");
		break;
	case EV_PLUS_2:
		cam_dbg("load sr200pc20_bright_p2\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_plus_2_regs,
										EV_P2_NUM_OF_REGS,
										"front_ev_plus_2_regs");
		break;
	case EV_PLUS_3:
		cam_dbg("load sr200pc20_bright_p3\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_plus_3_regs,
										EV_P3_NUM_OF_REGS,
										"front_ev_plus_3_regs");
		break;
	case EV_PLUS_4:
		cam_dbg("load sr200pc20_bright_p4\n");
		err = sr200pc20_i2c_set_config_register(client,
										front_ev_plus_4_regs,
										EV_P4_NUM_OF_REGS,
										"front_ev_plus_4_regs");
		break;
	default:
		cam_err("ERR: invalid brightness(%d)\n", ctrl->value);
		return err;
		break;
	}

	if (unlikely(err < 0)) {
		cam_err("ERR: i2c_write for set brightness\n");
		return -EIO;
	}

	return 0;
}

static int sr200pc20_check_dataline_stop(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	extern int sr200pc20_power_reset(void);

	cam_warn("Warning: do nothing!!\n");
	return err;

	//sr200pc20_write(client, 0xFCFCD000);
	//sr200pc20_write(client, 0x0028D000);
	//sr200pc20_write(client, 0x002A3100);
	//sr200pc20_write(client, 0x0F120000);

	//	err =  sr200pc20_write_regs(sd, sr200pc20_pattern_off,	sizeof(sr200pc20_pattern_off) / sizeof(sr200pc20_pattern_off[0]));
	cam_dbg("sensor reset\n");
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_TARGET_LOCALE_EUR) || defined(CONFIG_TARGET_LOCALE_HKTW) || defined(CONFIG_TARGET_LOCALE_HKTW_FET) || defined(CONFIG_TARGET_LOCALE_USAGSM)
        // dont't know where this code came from - comment out for compile error
        // sr200pc20_power_reset();
 #endif
 if(state->anti_banding == ANTI_BANDING_50HZ) {
 cam_info("load camera common 50hz setting\n");
 err = sr200pc20_i2c_set_config_register(client,
								   front_init_50hz_regs,
								   INIT_NUM_OF_REGS,
								   "front_init_50hz_regs");
 } else {
 cam_info("load camera common 60hz setting\n");
 err = sr200pc20_i2c_set_config_register(client,
								   front_init_60hz_regs,
								   INIT_NUM_OF_REGS,
								   "front_init_60hz_regs");
 }

	state->check_dataline = 0;
	/* mdelay(100); */
	return err;
}

static int sr200pc20_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	/* struct i2c_client *client = v4l2_get_subdevdata(sd); */
	struct sr200pc20_state *state = to_state(sd);
	int err = 0;

	cam_info("ctrl->id : %d, value=%d\n", ctrl->id - V4L2_CID_PRIVATE_BASE,
	ctrl->value);

	if ((ctrl->id != V4L2_CID_CAMERA_CHECK_DATALINE)
	&& (ctrl->id != V4L2_CID_CAMERA_SENSOR_MODE)
	&& (ctrl->id != V4L2_CID_CAMERA_VT_MODE)
	&& (ctrl->id != V4L2_CID_CAMERA_ANTI_BANDING)
	&& (!state->initialized)) {
		cam_warn("camera isn't initialized\n");
		return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_CAM_PREVIEW_ONOFF:
		if (ctrl->value)
			err = sr200pc20_set_preview_start(sd);
		else
			err = sr200pc20_set_preview_stop(sd);
		cam_dbg("V4L2_CID_CAM_PREVIEW_ONOFF [%d]\n", ctrl->value);
		break;

	case V4L2_CID_CAM_CAPTURE:
		err = sr200pc20_set_capture_start(sd);
		cam_dbg("V4L2_CID_CAM_CAPTURE\n");
		break;

	case V4L2_CID_CAMERA_BRIGHTNESS:
		err = sr200pc20_set_brightness(sd, ctrl);
		cam_dbg("V4L2_CID_CAMERA_BRIGHTNESS [%d]\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_VT_MODE:
		state->vt_mode = ctrl->value;
		break;

	case V4L2_CID_CAMERA_CHECK_DATALINE:
		state->check_dataline = ctrl->value;
		cam_dbg("check_dataline = %d\n", state->check_dataline);
		err = 0;
		break;

	case V4L2_CID_CAMERA_SENSOR_MODE:
		err = sr200pc20_set_sensor_mode(sd, ctrl);
		cam_dbg("sensor_mode = %d\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_CHECK_DATALINE_STOP:
		err = sr200pc20_check_dataline_stop(sd);
		break;

	case V4L2_CID_CAMERA_FRAME_RATE:
		cam_dbg("do nothing\n");
		break;

	case V4L2_CID_CAMERA_ANTI_BANDING:
		state->anti_banding = ctrl->value;
		cam_dbg("anti_banding = %d\n", state->anti_banding);
		err = 0;
		break;

	default:
		cam_err("ERR(ENOIOCTLCMD)\n");
		/* no errors return.*/
		err = 0;
	}

	cam_dbg("X\n");
	return err;
}

static const struct v4l2_subdev_core_ops sr200pc20_core_ops = {
	.init = sr200pc20_init,		/* initializing API */
	.g_ctrl = sr200pc20_g_ctrl,
	.s_ctrl = sr200pc20_s_ctrl,
};

static const struct v4l2_subdev_video_ops sr200pc20_video_ops = {
	/*.s_crystal_freq = sr200pc20_s_crystal_freq,*/
#ifdef NEW_CAM_DRV
	.g_mbus_fmt = sr200pc20_g_mbus_fmt,
	.s_mbus_fmt = sr200pc20_s_mbus_fmt,
#else
	.g_fmt	= sr200pc20_g_fmt,
	.s_fmt	= sr200pc20_s_fmt,
#endif
	.s_stream = sr200pc20_s_stream,
	.enum_framesizes = sr200pc20_enum_framesizes,
	/*.enum_frameintervals = sr200pc20_enum_frameintervals,*/
#ifdef NEW_CAM_DRV
	/* .enum_mbus_fmt = sr200pc20_enum_mbus_fmt, */
	.try_mbus_fmt = sr200pc20_try_mbus_fmt,
#else
	/*.enum_fmt = sr200pc20_enum_fmt,*/
	.try_fmt = sr200pc20_try_fmt,
#endif
	.g_parm	= sr200pc20_g_parm,
	.s_parm	= sr200pc20_s_parm,
};

static const struct v4l2_subdev_ops sr200pc20_ops = {
	.core = &sr200pc20_core_ops,
	.video = &sr200pc20_video_ops,
};

ssize_t sr200pc20_camera_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *cam_type = "SILICONFILE_SR200PC20";
	cam_info("%s\n", __func__);

	return sprintf(buf, "%s\n", cam_type);
}

static DEVICE_ATTR(camera_type, S_IRUGO, sr200pc20_camera_type_show, NULL);

/*
 * sr200pc20_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int sr200pc20_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct sr200pc20_state *state = NULL;
	struct v4l2_subdev *sd = NULL;
	int err = 0;

	cam_dbg("E\n");

	state = kzalloc(sizeof(struct sr200pc20_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	strcpy(sd->name, SR200PC20_DRIVER_NAME);

	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &sr200pc20_ops);

	err = sr200pc20_s_config(sd, 0, client->dev.platform_data);
	CHECK_ERR_MSG(err, "fail to s_config\n");

	if (device_create_file(&client->dev, &dev_attr_camera_type) < 0) {
		cam_warn("failed to create device file, %s\n",
				dev_attr_camera_type.attr.name);
	}
	cam_dbg("probed!!\n");

	return 0;
}

static int sr200pc20_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr200pc20_state *state = to_state(sd);

	cam_dbg("E\n");

	state->initialized = 0;

	device_remove_file(&client->dev, &dev_attr_camera_type);
	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id sr200pc20_id[] = {
	{ SR200PC20_DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sr200pc20_id);

static struct i2c_driver v4l2_i2c_driver = {
	.driver.name	= SR200PC20_DRIVER_NAME,
	.probe		= sr200pc20_probe,
	.remove		= sr200pc20_remove,
	.id_table	= sr200pc20_id,
};

static int __init v4l2_i2c_drv_init(void)
{
	pr_info("%s: %s called\n", __func__, SR200PC20_DRIVER_NAME); /* dslim*/
	return i2c_add_driver(&v4l2_i2c_driver);
}

static void __exit v4l2_i2c_drv_cleanup(void)
{
	pr_info("%s: %s called\n", __func__, SR200PC20_DRIVER_NAME); /* dslim*/
	i2c_del_driver(&v4l2_i2c_driver);
}

module_init(v4l2_i2c_drv_init);
module_exit(v4l2_i2c_drv_cleanup);

MODULE_DESCRIPTION("SR200PC20 ISP driver");
MODULE_AUTHOR("DongSeong Lim<dongseong.lim@samsung.com>");
MODULE_LICENSE("GPL");
