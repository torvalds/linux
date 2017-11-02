/*
 * drivers/input/touchscreen/gslX680.c
 *
 * Copyright (c) 2012 Shanghai Basewin
 *	Guan Yuwei<guanyuwei@basewin.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "tp_suspend.h"

#include "gslx680_firefly.h"

#define REPORT_DATA_ANDROID_4_0
#define SLEEP_CLEAR_POINT
#ifdef FILTER_POINT
#define FILTER_MAX	9
#endif

#define GSLX680_I2C_NAME	"gslX680"

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define PRESS_MAX			255
#define MAX_FINGERS		10
#define MAX_CONTACTS		10
#define DMA_TRANS_LEN		0x20

#define GPIO_LOW	0
#define GPIO_HIGH	1

/*#define TPD_PROC_DEBUG*/
#ifdef TPD_PROC_DEBUG
#include <linux/uaccess.h>
static struct proc_dir_entry *gsl_config_proc;
#define GSL_CONFIG_PROC_FILE "gsl_config"
#define CONFIG_LEN 31
static char gsl_read[CONFIG_LEN];
static u8 gsl_data_proc[8] = {0};
static u8 gsl_proc_flag;
#endif

#ifdef GSLX680_COMPATIBLE
static char chip_type = 0x36;
#endif

static char is_noid_version;
static struct i2c_client *gsl_client;
#define I2C_SPEED (200 * 1000)

#ifdef HAVE_TOUCH_KEY
static u16 key;
static int key_state_flag;
struct key_data {
	u16 key;
	u16 x_min;
	u16 x_max;
	u16 y_min;
	u16 y_max;
};

const u16 key_array[] = {
	KEY_BACK,
	KEY_HOME,
	KEY_MENU,
	KEY_SEARCH,
	};
#define MAX_KEY_NUM		ARRAY_SIZE(key_array)

struct key_data gsl_key_data[MAX_KEY_NUM] = {
	{KEY_BACK, 2048, 2048, 2048, 2048},
	{KEY_HOME, 2048, 2048, 2048, 2048},
	{KEY_MENU, 2048, 2048, 2048, 2048},
	{KEY_SEARCH, 2048, 2048, 2048, 2048},
};
#endif

struct gsl_ts_data {
	u8 x_index;
	u8 y_index;
	u8 z_index;
	u8 id_index;
	u8 touch_index;
	u8 data_reg;
	u8 status_reg;
	u8 data_size;
	u8 touch_bytes;
	u8 update_data;
	u8 touch_meta_data;
	u8 finger_size;
};

static struct gsl_ts_data devices[] = {
	{
	.x_index = 6,
	.y_index = 4,
	.z_index = 5,
	.id_index = 7,
	.data_reg = GSL_DATA_REG,
	.status_reg = GSL_STATUS_REG,
	.update_data = 0x4,
	.touch_bytes = 4,
	.touch_meta_data = 4,
	.finger_size = 70,
	},
};

struct gsl_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *wq;
	struct gsl_ts_data *dd;
	u8 *touch_data;
	u8 device_id;
	int irq;
	int irq_pin;
	int rst_pin;
	int rst_val;
	int flip_x;
	int flip_y;
	int swap_xy;
	struct  tp_device  tp;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
};

#ifdef GSL_DEBUG
#define print_info(fmt, args...)   \
	do {                              \
		printk(fmt, ##args);     \
	} while (0)
#else
#define print_info(fmt, args...)
#endif

static u32 id_sign[MAX_CONTACTS + 1] = {0};
static u8 id_state_flag[MAX_CONTACTS + 1] = {0};
static u8 id_state_old_flag[MAX_CONTACTS + 1] = {0};
static u16 x_old[MAX_CONTACTS + 1] = {0};
static u16 y_old[MAX_CONTACTS + 1] = {0};
static u16 x_new;
static u16 y_new;
static struct gsl_ts *gts;

static int gslX680_shutdown_low(void)
{
	if (gpio_is_valid(gts->rst_pin))
		gpio_set_value(gts->rst_pin, GPIO_LOW);

	return 0;
}

static int gslX680_shutdown_high(void)
{
	if (gpio_is_valid(gts->rst_pin))
		gpio_set_value(gts->rst_pin, GPIO_HIGH);

	return 0;
}

static inline u16 join_bytes(u8 a, u8 b)
{
	u16 ab = 0;

	ab = ab | a;
	ab = ab << 8 | b;
	return ab;
}

static u32 gsl_write_interface(struct i2c_client *client, const u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;
	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static int gsl_ts_write(struct i2c_client *client, u8 addr, u8 *pdata, int datalen)
{
	int ret = 0;
	u8 tmp_buf[128];
	unsigned int bytelen = 0;

	if (datalen > 125)
		return -1;

	tmp_buf[0] = addr;
	bytelen++;
	if (datalen != 0 && pdata != NULL) {
		memcpy(&tmp_buf[bytelen], pdata, datalen);
		bytelen += datalen;
	}
	ret = i2c_master_send(client, tmp_buf, bytelen);
	return ret;
}

static int gsl_ts_read(struct i2c_client *client, u8 addr, u8 *pdata, unsigned int datalen)
{
	int ret = 0;

	if (datalen > 126)
		return -1;

	ret = gsl_ts_write(client, addr, NULL, 0);
	if (ret < 0)
		return ret;

	return i2c_master_recv(client, pdata, datalen);
}

#ifdef GSLX680_COMPATIBLE
static void judge_chip_type(struct i2c_client *client)
{
	u8 read_buf[4]  = {0, 0, 0, 0};

	printk("org chip_type=%x\n", chip_type);
	msleep(50);
	gsl_ts_read(client, 0xfc, read_buf, sizeof(read_buf));

	if (read_buf[2] != 0x36 && read_buf[2] != 0x88) {
		msleep(50);
		gsl_ts_read(client, 0xfc, read_buf, sizeof(read_buf));
	}

	if (read_buf[2] == 0x36) {
		chip_type = 0x36;
		is_noid_version = 1;
	} else {
		chip_type = 0x88;
		is_noid_version = 0;
	}

	print_info("chip_type=%x, reg=%x\n", chip_type, read_buf[2]);
}
#endif

static __inline__ void fw2buf(u8 *buf, const u32 *fw)
{
	u32 *u32_buf = (int *)buf;
	*u32_buf = *fw;
}

static void gsl_load_fw(struct i2c_client *client)
{
	u8 buf[DMA_TRANS_LEN * 4 + 1] = {0};
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len = 0;
	const struct fw_data *ptr_fw = NULL;

#ifdef GSLX680_COMPATIBLE
	if (0x36 == chip_type) {
		ptr_fw = GSL3680B_FW;
		source_len = ARRAY_SIZE(GSL3680B_FW);
	}
#endif
	for (source_line = 0; source_line < source_len; source_line++) {
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == ptr_fw[source_line].offset) {
			fw2buf(cur, &ptr_fw[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		} else {
			if (1 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
				buf[0] = (u8)ptr_fw[source_line].offset;

			fw2buf(cur, &ptr_fw[source_line].val);
			cur += 4;

			if (0 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20)) {
				gsl_write_interface(client, buf[0], buf, cur - buf - 1);
				cur = buf + 1;
			}
			send_flag++;
		}
	}
}

static int test_i2c(struct i2c_client *client)
{
	u8 read_buf = 0;
	u8 write_buf = 0x12;
	int ret, rc = 1;

	ret = gsl_ts_read(client, 0xf0, &read_buf, sizeof(read_buf));
	if (ret  < 0)
		rc--;

	msleep(2);
	ret = gsl_ts_write(client, 0xf0, &write_buf, sizeof(write_buf));

	msleep(2);
	ret = gsl_ts_read(client, 0xf0, &read_buf, sizeof(read_buf));
	if (ret <  0)
		rc--;

	return rc;
}

static void startup_chip(struct i2c_client *client)
{
	u8 tmp = 0x00;
#ifdef GSL_NOID_VERSION
	if (is_noid_version)
		gsl_DataInit(gsl_config_data_id_3680B);
#endif
	gsl_ts_write(client, 0xe0, &tmp, 1);
	msleep(10);
}

static void reset_chip(struct i2c_client *client)
{
	u8 tmp = 0x88;
	u8 buf[4] = {0x00};

	gsl_ts_write(client, 0xe0, &tmp, sizeof(tmp));
	msleep(20);
	tmp = 0x04;
	gsl_ts_write(client, 0xe4, &tmp, sizeof(tmp));
	msleep(10);
	gsl_ts_write(client, 0xbc, buf, sizeof(buf));
	msleep(10);
}

static void clr_reg(struct i2c_client *client)
{
	u8 write_buf[4]	= {0};

	write_buf[0] = 0x88;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	msleep(20);
	write_buf[0] = 0x03;
	gsl_ts_write(client, 0x80, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x04;
	gsl_ts_write(client, 0xe4, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x00;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	msleep(20);
}

static void init_chip(struct i2c_client *client)
{
	int rc;

	gslX680_shutdown_low();
	msleep(20);
	gslX680_shutdown_high();
	msleep(20);
	rc = test_i2c(client);
	if (rc < 0) {
		print_info("------gslX680 test_i2c error------\n");
		return;
	}
	clr_reg(client);
	reset_chip(client);
	gsl_load_fw(client);
	startup_chip(client);
	reset_chip(client);
	startup_chip(client);
}

static void check_mem_data(struct i2c_client *client)
{
	u8 read_buf[4]  = {0};

	msleep(30);
	gsl_ts_read(client, 0xb0, read_buf, sizeof(read_buf));

	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a || read_buf[0] != 0x5a)
		init_chip(client);
}

#ifdef TPD_PROC_DEBUG
static int char_to_int(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	else
		return (ch - 'a' + 10);
}

static int gsl_config_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *ptr = page;
	char temp_data[5] = {0};
	unsigned int tmp = 0;

	if ('v' == gsl_read[0] && 's' == gsl_read[1]) {
#ifdef GSL_NOID_VERSION
		tmp = gsl_version_id();
#else
		tmp = 0x20121215;
#endif
		ptr += sprintf(ptr, "version:%x\n", tmp);
	} else if ('r' == gsl_read[0] && 'e' == gsl_read[1]) {
		if ('i' == gsl_read[3]) {
		tmp = (gsl_data_proc[5] << 8) | gsl_data_proc[4];
		ptr += s printf(ptr, "gsl_config_data_id[%d] = ", tmp);
		if (tmp >= 0 && tmp < ARRAY_SIZE(gsl_config_data_id_3680B)) {
			ptr += sprintf(ptr, "%d\n", gsl_config_data_id_3680B[tmp]);
		} else {
			gsl_ts_write(gsl_client, 0Xf0, &gsl_data_proc[4], 4);
			if (gsl_data_proc[0] < 0x80)
				gsl_ts_read(gsl_client, gsl_data_proc[0], temp_data, 4);
			gsl_ts_read(gsl_client, gsl_data_proc[0], temp_data, 4);

			ptr += sprintf(ptr, "offset : {0x%02x,0x", gsl_data_proc[0]);
			ptr += sprintf(ptr, "%02x", temp_data[3]);
			ptr += sprintf(ptr, "%02x", temp_data[2]);
			ptr += sprintf(ptr, "%02x", temp_data[1]);
			ptr += sprintf(ptr, "%02x};\n", temp_data[0]);
		}
	}
	*eof = 1;
	return (ptr - page);
}

static int gsl_config_write_proc(struct file *file, const char *buffer, unsigned long count, void *data)
{
	u8 buf[8] = {0};
	char temp_buf[CONFIG_LEN];
	char *path_buf;
	int tmp = 0;
	int tmp1 = 0;

	print_info("[tp-gsl][%s]\n", __func__);
	if (count > 512) {
		print_info("size not match [%d:%ld]\n", CONFIG_LEN, count);
		return -EFAULT;
	}
	path_buf = devm_kzalloc(&gts->client->dev, count, GFP_KERNEL);
	if (!path_buf)
		print_info("alloc path_buf memory error\n");

	if (copy_from_user(path_buf, buffer, count)) {
		print_info("copy from user fail\n");
		goto exit_write_proc_out;
	}
	memcpy(temp_buf, path_buf, (count < CONFIG_LEN ? count : CONFIG_LEN));
	print_info("[tp-gsl][%s][%s]\n", __func__, temp_buf);

	buf[3] = char_to_int(temp_buf[14]) << 4 | char_to_int(temp_buf[15]);
	buf[2] = char_to_int(temp_buf[16]) << 4 | char_to_int(temp_buf[17]);
	buf[1] = char_to_int(temp_buf[18]) << 4 | char_to_int(temp_buf[19]);
	buf[0] = char_to_int(temp_buf[20]) << 4 | char_to_int(temp_buf[21]);

	buf[7] = char_to_int(temp_buf[5]) << 4 | char_to_int(temp_buf[6]);
	buf[6] = char_to_int(temp_buf[7]) << 4 | char_to_int(temp_buf[8]);
	buf[5] = char_to_int(temp_buf[9]) << 4 | char_to_int(temp_buf[10]);
	buf[4] = char_to_int(temp_buf[11]) << 4 | char_to_int(temp_buf[12]);
	if ('v' == temp_buf[0] && 's' == temp_buf[1]) {
		memcpy(gsl_read, temp_buf, 4);
	} else if ('s' == temp_buf[0] && 't' == temp_buf[1]) {
		gsl_proc_flag = 1;
		reset_chip(gsl_client);
	} else if ('e' == temp_buf[0] && 'n' == temp_buf[1]) {
		msleep(20);
		reset_chip(gsl_client);
		startup_chip(gsl_client);
		gsl_proc_flag = 0;
	} else if ('r' == temp_buf[0] && 'e' == temp_buf[1]) {
		memcpy(gsl_read, temp_buf, 4);
		memcpy(gsl_data_proc, buf, 8);
	} else if ('w' == temp_buf[0] && 'r' == temp_buf[1]) {
		gsl_ts_write(gsl_client, buf[4], buf, 4);
	}
#ifdef GSL_NOID_VERSION
	else if ('i' == temp_buf[0] && 'd' == temp_buf[1]) {
		tmp1 = (buf[7] << 24) | (buf[6] << 16) | (buf[5] << 8) | buf[4];
		tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
		if (tmp1 >= 0 && tmp1 < ARRAY_SIZE(gsl_config_data_id_3680B))
			gsl_config_data_id_3680B[tmp1] = tmp;
	}
#endif
exit_write_proc_out:
	devm_kfree(&client->dev, path_buf);
	return count;
}
#endif

#ifdef FILTER_POINT
static void filter_point(u16 x, u16 y, u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;
	u16 filter_step_x = 0, filter_step_y = 0;

	id_sign[id] = id_sign[id] + 1;
	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x_err = x > x_old[id] ? (x - x_old[id]) : (x_old[id] - x);
	y_err = y > y_old[id] ? (y - y_old[id]) : (y_old[id] - y);

	if ((x_err > FILTER_MAX && y_err > FILTER_MAX / 3) || (x_err > FILTER_MAX / 3 && y_err > FILTER_MAX)) {
		filter_step_x = x_err;
		filter_step_y = y_err;
	} else {
		if (x_err > FILTER_MAX)
			filter_step_x = x_err;
		if (y_err > FILTER_MAX)
			filter_step_y = y_err;
	}

	if (x_err <= 2 * FILTER_MAX && y_err <= 2 * FILTER_MAX) {
		filter_step_x >>= 2;
		filter_step_y >>= 2;
	} else if (x_err <= 3 * FILTER_MAX && y_err <= 3 * FILTER_MAX) {
		filter_step_x >>= 1;
		filter_step_y >>= 1;
	} else if (x_err <= 4 * FILTER_MAX && y_err <= 4 * FILTER_MAX) {
		filter_step_x = filter_step_x * 3 / 4;
		filter_step_y = filter_step_y * 3 / 4;
	}

	x_new = x > x_old[id] ? (x_old[id] + filter_step_x) : (x_old[id] - filter_step_x);
	y_new = y > y_old[id] ? (y_old[id] + filter_step_y) : (y_old[id] - filter_step_y);

	x_old[id] = x_new;
	y_old[id] = y_new;
}
#else
static void record_point(u16 x, u16 y, u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;

	id_sign[id] = id_sign[id] + 1;

	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x = (x_old[id] + x) / 2;
	y = (y_old[id] + y) / 2;

	if (x > x_old[id])
		x_err = x - x_old[id];
	else
		x_err = x_old[id] - x;

	if (y > y_old[id])
		y_err = y - y_old[id];
	else
		y_err = y_old[id] - y;

	if ((x_err > 3 && y_err > 1) || (x_err > 1 && y_err > 3)) {
		x_new = x;
		x_old[id] = x;
		y_new = y;
		y_old[id] = y;
	} else {
		if (x_err > 3) {
			x_new = x;
			x_old[id] = x;
		} else {
			x_new = x_old[id];
		}

		if (y_err > 3) {
			y_new = y;
			y_old[id] = y;
		} else {
			y_new = y_old[id];
		}
	}

	if (id_sign[id] == 1) {
		x_new = x_old[id];
		y_new = y_old[id];
	}
}

#endif

#ifdef HAVE_TOUCH_KEY
static void report_key(struct gsl_ts *ts, u16 x, u16 y)
{
	u16 i = 0;

	for (i = 0; i < MAX_KEY_NUM; i++) {
		if ((gsl_key_data[i].x_min < x) && (x < gsl_key_data[i].x_max) && (gsl_key_data[i].y_min < y) && (y < gsl_key_data[i].y_max)) {
			key = gsl_key_data[i].key;
			input_report_key(ts->input, key, 1);
			input_sync(ts->input);
			key_state_flag = 1;
			break;
		}
	}
}
#endif

static void report_data(struct gsl_ts *ts, u16 x, u16 y, u8 pressure, u8 id)
{
	if (ts->flip_x == 1)
		x = SCREEN_MAX_X - x;

	if (ts->flip_y == 1)
		y = SCREEN_MAX_Y - y;

	if (ts->swap_xy == 1)
		swap(x, y);

	print_info("#####id=%d,x=%d,y=%d######\n", id, x, y);

	if (x > SCREEN_MAX_X || y > SCREEN_MAX_Y) {
	#ifdef HAVE_TOUCH_KEY
		report_key(ts, x, y);
	#endif
		return;
	}

#ifdef CONFIG_TCHIP_MACH_BACK_MUSIC
	y = SCREEN_MAX_Y - y;
#endif

#ifdef REPORT_DATA_ANDROID_4_0
	input_mt_slot(ts->input, id);
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
#else
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
	input_mt_sync(ts->input);
#endif
}

static void gslX680_ts_worker(struct work_struct *work)
{
	int rc, i;
	u8 id, touches;
	u16 x, y;

#ifdef GSL_NOID_VERSION
	u32 tmp1;
	u8 buf[4] = {0};
	struct gsl_touch_info cinfo = {{0} };
#endif

	struct gsl_ts *ts = container_of(work, struct gsl_ts, work);

#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1)
		goto schedule;
#endif

	rc = gsl_ts_read(ts->client, 0x80, ts->touch_data, ts->dd->data_size);
	if (rc < 0) {
		dev_err(&ts->client->dev, "read failed\n");
		goto schedule;
	}

	touches = ts->touch_data[ts->dd->touch_index];
	print_info("-----touches: %d -----\n", touches);
#ifdef GSL_NOID_VERSION
	cinfo.finger_num = touches;
	print_info("tp-gsl  finger_num = %d\n", cinfo.finger_num);
	for (i = 0; i < (touches < MAX_CONTACTS ? touches : MAX_CONTACTS); i++) {
		cinfo.x[i] = join_bytes((ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),
				ts->touch_data[ts->dd->x_index + 4 * i]);
		cinfo.y[i] = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
				ts->touch_data[ts->dd->y_index + 4 * i]);
		print_info("tp-gsl  x = %d y = %d\n", cinfo.x[i], cinfo.y[i]);
	}
	cinfo.finger_num = (ts->touch_data[3] << 24) | (ts->touch_data[2] << 16)
		| (ts->touch_data[1] << 8) | (ts->touch_data[0]);
	gsl_alg_id_main(&cinfo);
	tmp1 = gsl_mask_tiaoping();
	print_info("[tp-gsl] tmp1=%x\n", tmp1);
	if (tmp1 > 0 && tmp1 < 0xffffffff) {
		buf[0] = 0xa; buf[1] = 0; buf[2] = 0; buf[3] = 0;
		gsl_ts_write(ts->client, 0xf0, buf, 4);
		buf[0] = (u8)(tmp1 & 0xff);
		buf[1] = (u8)((tmp1 >> 8) & 0xff);
		buf[2] = (u8)((tmp1 >> 16) & 0xff);
		buf[3] = (u8)((tmp1 >> 24) & 0xff);
		print_info("tmp1=%08x,buf[0]=%02x,buf[1]=%02x,buf[2]=%02x,buf[3]=%02x\n",
			   tmp1, buf[0], buf[1], buf[2], buf[3]);
		gsl_ts_write(ts->client, 0x8, buf, 4);
	}
	touches = cinfo.finger_num;
#endif

	for (i = 1; i <= MAX_CONTACTS; i++) {
		if (touches == 0)
			id_sign[i] = 0;
		id_state_flag[i] = 0;
	}
	for (i = 0; i < (touches > MAX_FINGERS ? MAX_FINGERS : touches); i++) {
	#ifdef GSL_NOID_VERSION
		id = cinfo.id[i];
		x =  cinfo.x[i];
		y =  cinfo.y[i];
	#else
		x = join_bytes((ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),
			       ts->touch_data[ts->dd->x_index + 4 * i]);
		y = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
			       ts->touch_data[ts->dd->y_index + 4 * i]);
		id = ts->touch_data[ts->dd->id_index + 4 * i] >> 4;
	#endif

		if (1 <= id && id <= MAX_CONTACTS) {
		#ifdef FILTER_POINT
			filter_point(x, y, id);
		#else
			record_point(x, y, id);
		#endif
			report_data(ts, x_new, y_new, 10, id);
			id_state_flag[id] = 1;
		}
	}
	for (i = 1; i <= MAX_CONTACTS; i++) {
		if ((touches == 0) || ((id_state_old_flag[i] != 0) && (id_state_flag[i] == 0))) {
		#ifdef REPORT_DATA_ANDROID_4_0
			input_mt_slot(ts->input, i);
			input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
		#endif
			id_sign[i] = 0;
		}
		id_state_old_flag[i] = id_state_flag[i];
	}
#ifndef REPORT_DATA_ANDROID_4_0
	if (touches == 0) {
		input_mt_sync(ts->input);
	#ifdef HAVE_TOUCH_KEY
		if (key_state_flag) {
			input_report_key(ts->input, key, 0);
			input_sync(ts->input);
			key_state_flag = 0;
		}
	#endif
	}
#endif
	input_sync(ts->input);

schedule:
	enable_irq(ts->irq);
}

extern void regulator_ctrl_vcc_tp(bool on);

static irqreturn_t gsl_ts_irq(int irq, void *dev_id)
{
	struct gsl_ts *ts = dev_id;

	print_info("========gslX680 Interrupt=========\n");

	disable_irq_nosync(ts->irq);
	if (!work_pending(&ts->work))
		queue_work(ts->wq, &ts->work);

	return IRQ_HANDLED;
}

static int gslX680_ts_init(struct i2c_client *client, struct gsl_ts *ts)
{
	struct input_dev *input_device;
	int rc = 0;

	ts->dd = &devices[ts->device_id];

	if (ts->device_id == 0) {
		ts->dd->data_size = MAX_FINGERS * ts->dd->touch_bytes + ts->dd->touch_meta_data;
		ts->dd->touch_index = 0;
	}

	ts->touch_data = devm_kzalloc(&client->dev, ts->dd->data_size, GFP_KERNEL);
	if (!ts->touch_data) {
		pr_err("%s: Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}

	input_device = input_allocate_device();
	if (!input_device) {
		rc = -ENOMEM;
		goto error_alloc_dev;
	}

	ts->input = input_device;
	input_device->name = GSLX680_I2C_NAME;
	input_device->id.bustype = BUS_I2C;
	input_device->dev.parent = &client->dev;
	input_set_drvdata(input_device, ts);

#ifdef REPORT_DATA_ANDROID_4_0
	__set_bit(EV_ABS, input_device->evbit);
	__set_bit(EV_KEY, input_device->evbit);
	__set_bit(EV_REP, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	input_mt_init_slots(input_device, (MAX_CONTACTS + 1), 0);
#else
	input_set_abs_params(input_device, ABS_MT_TRACKING_ID, 0, (MAX_CONTACTS + 1), 0, 0);
	set_bit(EV_ABS, input_device->evbit);
	set_bit(EV_KEY, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	input_device->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif

#ifdef HAVE_TOUCH_KEY
	input_device->evbit[0] = BIT_MASK(EV_KEY);
	for (i = 0; i < MAX_KEY_NUM; i++)
		set_bit(key_array[i], input_device->keybit);
#endif

	set_bit(ABS_MT_POSITION_X, input_device->absbit);
	set_bit(ABS_MT_POSITION_Y, input_device->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_device->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_device->absbit);

	input_set_abs_params(input_device, ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_device, ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);

	input_set_abs_params(input_device, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_device, ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

	client->irq = gts->irq_pin;
	ts->irq = client->irq;
	ts->wq = create_singlethread_workqueue("kworkqueue_ts");
	if (!ts->wq) {
		dev_err(&client->dev, "Could not create workqueue\n");
		goto error_wq_create;
	}
	flush_workqueue(ts->wq);

	INIT_WORK(&ts->work, gslX680_ts_worker);

	rc = input_register_device(input_device);
	if (rc)
		goto error_unreg_device;

	return 0;

error_unreg_device:
	destroy_workqueue(ts->wq);
error_wq_create:
	input_free_device(input_device);
error_alloc_dev:
	devm_kfree(&client->dev, ts->touch_data);

	return rc;
}

static int rk_ts_early_suspend(struct tp_device *tp_d)
{
	struct gsl_ts *ts = container_of(tp_d, struct gsl_ts, tp);
	int i;

	disable_irq_nosync(ts->irq);
	gslX680_shutdown_low();

#ifdef SLEEP_CLEAR_POINT
	msleep(10);
	#ifdef REPORT_DATA_ANDROID_4_0
	for (i = 1; i <= MAX_CONTACTS; i++) {
		input_mt_slot(ts->input, i);
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
	#else
	input_mt_sync(ts->input);
	#endif
	input_sync(ts->input);
	msleep(10);
	report_data(ts, 1, 1, 10, 1);
	input_sync(ts->input);
#endif

	return 0;
}

static int rk_ts_early_resume(struct tp_device *tp_d)
{
	struct gsl_ts *ts = container_of(tp_d, struct gsl_ts, tp);
	int i;

	gslX680_shutdown_high();
	msleep(20);
	reset_chip(ts->client);
	startup_chip(ts->client);
	check_mem_data(ts->client);

#ifdef SLEEP_CLEAR_POINT
	#ifdef REPORT_DATA_ANDROID_4_0
	for (i = 1; i <= MAX_CONTACTS; i++) {
		input_mt_slot(ts->input, i);
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
	#else
	input_mt_sync(ts->input);
	#endif
	input_sync(ts->input);
#endif
	enable_irq(ts->irq);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void gsl_ts_early_suspend(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);

	gsl_ts_suspend(&ts->client->dev);
}

static void gsl_ts_late_resume(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);

	gsl_ts_resume(&ts->client->dev);
}
#endif

static int gsl_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct gsl_ts *ts;
	int rc;
	int timer = 3;
	int ret = 0;
	char buffer = 0;
	struct device_node *np = client->dev.of_node;
	enum of_gpio_flags rst_flags;
	unsigned long irq_flags;

	while (timer > 0) {
		ret = i2c_master_recv(client, &buffer, 1);
		if (ret >= 0)
			break;
		timer--;
		msleep(100);
	}

	if (ret < 0)
		return ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C functionality not supported\n");
		return -ENODEV;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	i2c_set_clientdata(client, ts);
	ts->device_id = 0;
	ts->tp.tp_resume = rk_ts_early_resume;
	ts->tp.tp_suspend = rk_ts_early_suspend;
	tp_register_fb(&ts->tp);

	ts->irq_pin = of_get_named_gpio_flags(np, "touch-gpio", 0, (enum of_gpio_flags *)&irq_flags);
	ts->rst_pin = of_get_named_gpio_flags(np, "reset-gpio", 0, &rst_flags);
	if (of_property_read_u32(np, "flip-x", &ts->flip_x) < 0)
		ts->flip_x = 0;

	if (of_property_read_u32(np, "flip-y", &ts->flip_y) < 0)
		ts->flip_y = 0;

	if (of_property_read_u32(np, "swap-xy", &ts->swap_xy) < 0)
		ts->swap_xy = 0;

	if (gpio_is_valid(ts->rst_pin)) {
		ts->rst_val = (rst_flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = devm_gpio_request_one(&client->dev, ts->rst_pin, (rst_flags & OF_GPIO_ACTIVE_LOW) ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW, "goodix reset pin");
		if (ret != 0) {
			dev_err(&client->dev, "goodix gpio_request error\n");
			return -EIO;
		}
		gpio_direction_output(ts->rst_pin, 0);
		gpio_set_value(ts->rst_pin, GPIO_HIGH);
		msleep(20);
	} else {
		dev_info(&client->dev, "reset pin invalid\n");
	}
	gts = ts;

	rc = gslX680_ts_init(client, ts);
	if (rc < 0) {
		dev_err(&client->dev, "GSLX680 init failed\n");
		goto error_mutex_destroy;
	}

	gsl_client = client;

#ifdef GSLX680_COMPATIBLE
	judge_chip_type(ts->client);
#endif
	init_chip(ts->client);
	check_mem_data(ts->client);

	ts->irq = gpio_to_irq(ts->irq_pin);		/*If not defined in client*/
	if (ts->irq) {
		rc = devm_request_threaded_irq(&client->dev, ts->irq, NULL,
					       gsl_ts_irq, irq_flags | IRQF_ONESHOT, client->name, ts);
		if (rc != 0) {
			print_info("Cannot allocate ts INT!ERRNO:%d\n", ret);
			goto error_req_irq_fail;
		}
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = gsl_ts_early_suspend;
	ts->early_suspend.resume = gsl_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

#ifdef TPD_PROC_DEBUG
	gsl_config_proc = create_proc_entry(GSL_CONFIG_PROC_FILE, 0666, NULL);
	if (!gsl_config_proc) {
		print_info("create_proc_entry %s failed\n", GSL_CONFIG_PROC_FILE);
	} else {
		gsl_config_proc->read_proc = gsl_config_read_proc;
		gsl_config_proc->write_proc = gsl_config_write_proc;
	}
	gsl_proc_flag = 0;
#endif
	return 0;

error_req_irq_fail:
	free_irq(ts->irq, ts);

error_mutex_destroy:
	input_free_device(ts->input);
	devm_kfree(&client->dev, ts);
	return rc;
}

static int gsl_ts_remove(struct i2c_client *client)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	device_init_wakeup(&client->dev, 0);
	cancel_work_sync(&ts->work);
	free_irq(ts->irq, ts);
	destroy_workqueue(ts->wq);
	input_unregister_device(ts->input);

	devm_kfree(&client->dev, ts->touch_data);
	devm_kfree(&client->dev, ts);

	return 0;
}

static const struct i2c_device_id gsl_ts_id[] = {
	{GSLX680_I2C_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, gsl_ts_id);

static struct of_device_id goodix_ts_dt_ids[] = {
	{ .compatible = "gslX680" },
	{ }
};

static struct i2c_driver gsl_ts_driver = {
	.driver = {
		.name = GSLX680_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goodix_ts_dt_ids),
	},
	.probe		= gsl_ts_probe,
	.remove		= gsl_ts_remove,
	.id_table	= gsl_ts_id,
};

static int __init gsl_ts_init(void)
{
	int ret;

	ret = i2c_add_driver(&gsl_ts_driver);
	return ret;
}

static void __exit gsl_ts_exit(void)
{
	i2c_del_driver(&gsl_ts_driver);
}

module_init(gsl_ts_init);
module_exit(gsl_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GSLX680 touchscreen controller driver");
