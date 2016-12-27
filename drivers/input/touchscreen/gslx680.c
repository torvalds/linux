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
#include <linux/gpio.h>
#include <asm/irq.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
//#include "rockchip_gslX680_rk3168.h"
#include "tp_suspend.h"
#include "gslx680.h"
#include <linux/of_gpio.h>
#include <linux/wakelock.h>

#define GSL_DEBUG

/*
struct fw_data
{
	u32 offset : 8;
	u32 : 0;
	u32 val;
};
*/

#define RK_GEAR_TOUCH
#define REPORT_DATA_ANDROID_4_0
#define HAVE_TOUCH_KEY
//#define SLEEP_CLEAR_POINT

//#define FILTER_POINT

#ifdef FILTER_POINT
#define FILTER_MAX	9	//6
#endif

#define GSLX680_I2C_NAME	"gslX680"
#define GSLX680_I2C_ADDR	0x40

//#define IRQ_PORT	RK2928_PIN1_PB0//RK30_PIN1_PB7
//#define WAKE_PORT	RK30_PIN0_PA1//RK30_PIN0_PB6

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define TPD_PROC_DEBUG
#ifdef TPD_PROC_DEBUG
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>
//static struct proc_dir_entry *gsl_config_proc = NULL;
#define GSL_CONFIG_PROC_FILE "gsl_config"
#define CONFIG_LEN 31
static char gsl_read[CONFIG_LEN];
static u8 gsl_data_proc[8] = { 0 };
static u8 gsl_proc_flag = 0;
static struct i2c_client *i2c_client = NULL;
#endif
#define GSL_MONITOR
#define PRESS_MAX		255
#define MAX_FINGERS		10
#define MAX_CONTACTS		10
#define DMA_TRANS_LEN		0x20
#ifdef GSL_MONITOR

#ifdef RK_GEAR_TOUCH
static int g_istouch=0;
#endif

static struct workqueue_struct *gsl_monitor_workqueue = NULL;
static u8 int_1st[4] = { 0 };
static u8 int_2nd[4] = { 0 };
//static char dac_counter = 0;
static char b0_counter = 0;
static char bc_counter = 0;
static char i2c_lock_flag = 0;
#endif

#define WRITE_I2C_SPEED	(350*1000)
#define I2C_SPEED	(200*1000)
#define CLOSE_TP_POWER   0
//add by yuandan
//#define HAVE_CLICK_TIMER

#ifdef HAVE_CLICK_TIMER

static struct workqueue_struct *gsl_timer_workqueue = NULL;
bool send_key = false;
struct semaphore my_sem;
#endif

#ifdef HAVE_TOUCH_KEY
static u16 key = 0;
static int key_state_flag = 0;
struct key_data {
	u16 key;
	u16 x_min;
	u16 x_max;
	u16 y_min;
	u16 y_max;
};

const u16 key_array[] = {
	KEY_LEFT,
	KEY_RIGHT,
	KEY_UP,
	KEY_DOWN,
	KEY_ENTER,
};

#define MAX_KEY_NUM     (sizeof(key_array)/sizeof(key_array[0]))
//add by yuandan
int key_x[512];
int key_y[512];
int key_count = 0;
int key_repeat;
struct key_data gsl_key_data[MAX_KEY_NUM] = {
	{KEY_BACK, 550, 650, 1400, 1600},
	{KEY_HOMEPAGE, 350, 450, 1400, 1600},
	{KEY_MENU, 150, 250, 1400, 1600},
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
	int flag_irq_is_disable;
	spinlock_t irq_lock;
	u8 *touch_data;
	u8 device_id;
	int irq;
	int rst;
	struct delayed_work gsl_monitor_work;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif

#if defined (HAVE_CLICK_TIMER)
	struct work_struct click_work;
#endif

	struct tp_device tp;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_sleep;
	struct pinctrl_state *pins_inactive;
};

#ifdef GSL_DEBUG
#define print_info(fmt, args...)	printk(fmt, ##args);
#else
#define print_info(fmt, args...)
#endif

static u32 id_sign[MAX_CONTACTS + 1] = { 0 };
static u8 id_state_flag[MAX_CONTACTS + 1] = { 0 };
static u8 id_state_old_flag[MAX_CONTACTS + 1] = { 0 };
static u16 x_old[MAX_CONTACTS + 1] = { 0 };
static u16 y_old[MAX_CONTACTS + 1] = { 0 };
static u16 x_new = 0;
static u16 y_new = 0;

int gslx680_set_pinctrl_state(struct gsl_ts *ts, struct pinctrl_state *state)
{
	int ret = 0;

	if (!IS_ERR(state)) {
		ret = pinctrl_select_state(ts->pinctrl, state);
		if (ret)
			printk("could not set pins \n");
	}

	return ret;
}

static int gslX680_init(struct gsl_ts *ts)
{
	struct device_node *np = ts->client->dev.of_node;
	int err = 0;
	int ret = 0;

	ts->irq = of_get_named_gpio_flags(np, "touch-gpio", 0, NULL);
	ts->rst = of_get_named_gpio_flags(np, "reset-gpio", 0, NULL);

	//msleep(20);
#if 0	//#if defined (CONFIG_BOARD_ZM71C)||defined (CONFIG_BOARD_ZM72CP) ||
	defined(CONFIG_BOARD_ZM726C) || defined(CONFIG_BOARD_ZM726CE)
	    if (gpio_request(ts->rst, NULL) != 0) {
		gpio_free(ts->rst);
		printk("gslX680_init gpio_request error\n");
		return -EIO;
	}
#endif

	/* pinctrl */
	ts->pinctrl = devm_pinctrl_get(&ts->client->dev);
	if (IS_ERR(ts->pinctrl)) {
		ret = PTR_ERR(ts->pinctrl);
		//goto out;
	}

	ts->pins_default =
	    pinctrl_lookup_state(ts->pinctrl, PINCTRL_STATE_DEFAULT);
	//if (IS_ERR(ts->pins_default))
	//      dev_err(&client->dev, "could not get default pinstate\n");

	ts->pins_sleep = pinctrl_lookup_state(ts->pinctrl, PINCTRL_STATE_SLEEP);
	//if (IS_ERR(ts->pins_sleep))
	//      dev_err(&client->dev, "could not get sleep pinstate\n");

	ts->pins_inactive = pinctrl_lookup_state(ts->pinctrl, "inactive");
	//if (IS_ERR(ts->pins_inactive))
	//      dev_err(&client->dev, "could not get inactive pinstate\n");

	err = gpio_request(ts->rst, "tp reset");
	if (err) {
		printk("gslx680 reset gpio request failed.\n");
		return -1;
	}

	gslx680_set_pinctrl_state(ts, ts->pins_default);
	gpio_direction_output(ts->rst, 1);
	gpio_set_value(ts->rst, 1);

	return 0;
}

static int gslX680_shutdown_low(struct gsl_ts *ts)
{
	printk("gsl  gslX680_shutdown_low\n");
	gpio_direction_output(ts->rst, 0);
	gpio_set_value(ts->rst, 0);

	return 0;
}

static int gslX680_shutdown_high(struct gsl_ts *ts)
{
	printk("gsl  gslX680_shutdown_high\n");
	gpio_direction_output(ts->rst, 1);
	gpio_set_value(ts->rst, 1);

	return 0;
}

static inline u16 join_bytes(u8 a, u8 b)
{
	u16 ab = 0;

	ab = ab | a;
	ab = ab << 8 | b;

	return ab;
}

/*
static u32 gsl_read_interface(struct i2c_client *client,
	u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = &reg;
	xfer_msg[0].scl_rate=300*1000;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags |= I2C_M_RD;
	xfer_msg[1].buf = buf;
	xfer_msg[1].scl_rate=300*1000;

	if (reg < 0x80) {
		i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg));
		msleep(5);
	}

	return i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg)) \
		== ARRAY_SIZE(xfer_msg) ? 0 : -EFAULT;
}
 */

static u32 gsl_write_interface(struct i2c_client *client,
			       const u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;
	//xfer_msg[0].scl_rate = 100 * 1000;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static int gsl_ts_write(struct i2c_client *client,
			u8 addr, u8 *pdata, int datalen)
{
	int ret = 0;
	u8 tmp_buf[128];
	unsigned int bytelen = 0;

	if (datalen > 125) {
		printk("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}

	tmp_buf[0] = addr;
	bytelen++;

	if (datalen != 0 && pdata != NULL) {
		memcpy(&tmp_buf[bytelen], pdata, datalen);
		bytelen += datalen;
	}

	ret = i2c_master_send(client, tmp_buf, bytelen);
	return ret;
}

static int gsl_ts_read(struct i2c_client *client, u8 addr,
		       u8 *pdata, unsigned int datalen)
{
	int ret = 0;

	if (datalen > 126) {
		printk("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}

	ret = gsl_ts_write(client, addr, NULL, 0);
	if (ret < 0) {
		printk("%s set data address fail!\n", __func__);
		return ret;
	}

	return i2c_master_recv(client, pdata, datalen);
}

static __inline__ void fw2buf(u8 *buf, const u32 *fw)
{
	u32 *u32_buf = (int *)buf;
	*u32_buf = *fw;
}

static void gsl_load_fw(struct i2c_client *client)
{
	u8 buf[DMA_TRANS_LEN * 4 + 1] = { 0 };
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len;
	//u8 read_buf[4] = {0};
	struct fw_data const *ptr_fw;

	ptr_fw = GSLX680_FW;
	source_len = ARRAY_SIZE(GSLX680_FW);

	for (source_line = 0; source_line < source_len; source_line++) {
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == ptr_fw[source_line].offset) {
			fw2buf(cur, &ptr_fw[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		} else {
			if (1 ==
			    send_flag % (DMA_TRANS_LEN <
					 0x20 ? DMA_TRANS_LEN : 0x20))
				buf[0] = (u8) ptr_fw[source_line].offset;

			fw2buf(cur, &ptr_fw[source_line].val);
			cur += 4;

			if (0 ==
			    send_flag % (DMA_TRANS_LEN <
					 0x20 ? DMA_TRANS_LEN : 0x20)) {
				gsl_write_interface(client, buf[0], buf,
						    cur - buf - 1);
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
	if (ret < 0)
		rc--;
	else
		printk("gsl I read reg 0xf0 is %x\n", read_buf);

	msleep(2);
	ret = gsl_ts_write(client, 0xf0, &write_buf, sizeof(write_buf));
	if (ret >= 0)
		printk("gsl I write reg 0xf0 0x12\n");

	msleep(2);
	ret = gsl_ts_read(client, 0xf0, &read_buf, sizeof(read_buf));
	if (ret < 0)
		rc--;
	else
		printk("gsl I read reg 0xf0 is 0x%x\n", read_buf);

	return rc;
}
static void startup_chip(struct i2c_client *client)
{
	u8 tmp = 0x00;

	printk("gsl  startup_chip\n");

#ifdef GSL_NOID_VERSION
	gsl_DataInit(gsl_config_data_id);
#endif
	gsl_ts_write(client, 0xe0, &tmp, 1);
	mdelay(10);
}

static void reset_chip(struct i2c_client *client)
{
	u8 tmp = 0x88;
	u8 buf[4] = { 0x00 };

	printk("gsl  reset_chip\n");

	gsl_ts_write(client, 0xe0, &tmp, sizeof(tmp));
	mdelay(20);
	tmp = 0x04;
	gsl_ts_write(client, 0xe4, &tmp, sizeof(tmp));
	mdelay(10);
	gsl_ts_write(client, 0xbc, buf, sizeof(buf));
	mdelay(10);
}

static void clr_reg(struct i2c_client *client)
{
	u8 write_buf[4] = { 0 };

	write_buf[0] = 0x88;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	mdelay(20);
	write_buf[0] = 0x03;
	gsl_ts_write(client, 0x80, &write_buf[0], 1);
	mdelay(5);
	write_buf[0] = 0x04;
	gsl_ts_write(client, 0xe4, &write_buf[0], 1);
	mdelay(5);
	write_buf[0] = 0x00;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	mdelay(20);
}

static void init_chip(struct i2c_client *client, struct gsl_ts *ts)
{
	int rc;

	printk("gsl  init_chip\n");

	gslX680_shutdown_low(ts);
	mdelay(20);
	gslX680_shutdown_high(ts);
	mdelay(20);
	rc = test_i2c(client);
	if (rc < 0) {
		printk("gslX680 test_i2c error\n");
		return;
	}
	clr_reg(client);
	reset_chip(client);
	gsl_load_fw(client);
	startup_chip(client);
	reset_chip(client);
	startup_chip(client);
}

static void check_mem_data(struct i2c_client *client, struct gsl_ts *ts)
{
	u8 read_buf[4] = { 0 };

	mdelay(30);
	gsl_ts_read(client, 0xb0, read_buf, sizeof(read_buf));
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a ||
		read_buf[1] != 0x5a || read_buf[0] != 0x5a) {
		init_chip(client, ts);
	}
}

#ifdef TPD_PROC_DEBUG
static int char_to_int(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	else
		return (ch - 'a' + 10);
}

static int gsl_config_read_proc(struct seq_file *m, void *v)
{
	//char *ptr = page;
	char temp_data[5] = { 0 };
	unsigned int tmp = 0;
	//unsigned int *ptr_fw;

	if ('v' == gsl_read[0] && 's' == gsl_read[1]) {
#ifdef GSL_NOID_VERSION
		tmp = gsl_version_id();
#else
		tmp = 0x20121215;
#endif
		seq_printf(m, "version:%x\n", tmp);
	} else if ('r' == gsl_read[0] && 'e' == gsl_read[1]) {
		if ('i' == gsl_read[3]) {
#ifdef GSL_NOID_VERSION
			/*      tmp=(gsl_data_proc[5]<<8) | gsl_data_proc[4];
			   seq_printf(m,"gsl_config_data_id[%d] = ",tmp);
			   if(tmp>=0&&tmp<gsl_cfg_table[gsl_cfg_index].data_size)
			   seq_printf(m,"%d\n",gsl_cfg_table[gsl_cfg_index].data_id[tmp]); */

			tmp = (gsl_data_proc[5] << 8) | gsl_data_proc[4];
			seq_printf(m, "gsl_config_data_id[%d] = ", tmp);
			if (tmp >= 0 && tmp < 512)
				seq_printf(m, "%d\n", gsl_config_data_id[tmp]);
#endif
		} else {
			i2c_smbus_write_i2c_block_data(i2c_client, 0xf0, 4,
						       &gsl_data_proc[4]);
			if (gsl_data_proc[0] < 0x80)
				i2c_smbus_read_i2c_block_data(i2c_client,
							      gsl_data_proc[0],
							      4, temp_data);
			i2c_smbus_read_i2c_block_data(i2c_client,
						      gsl_data_proc[0], 4,
						      temp_data);

			seq_printf(m, "offset : {0x%02x,0x", gsl_data_proc[0]);
			seq_printf(m, "%02x", temp_data[3]);
			seq_printf(m, "%02x", temp_data[2]);
			seq_printf(m, "%02x", temp_data[1]);
			seq_printf(m, "%02x};\n", temp_data[0]);
		}
	}
	return 0;
}
static ssize_t gsl_config_write_proc(struct file *file, const char *buffer,
				 size_t count, loff_t *data)
{
	u8 buf[8] = { 0 };
	char temp_buf[CONFIG_LEN];
	char *path_buf;
	int tmp = 0;
	int tmp1 = 0;

	print_info("[tp-gsl][%s] \n", __func__);
	if (count > 512) {
		//print_info("size not match [%d:%d]\n", CONFIG_LEN, count);
		return -EFAULT;
	}
	path_buf = kzalloc(count, GFP_KERNEL);
	if (!path_buf) {
		printk("alloc path_buf memory error \n");
	}
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
		//version //vs
		memcpy(gsl_read, temp_buf, 4);
		printk("gsl version\n");
	} else if ('s' == temp_buf[0] && 't' == temp_buf[1]) {
		//start //st
		gsl_proc_flag = 1;
		reset_chip(i2c_client);
	} else if ('e' == temp_buf[0] && 'n' == temp_buf[1]) {
		//end //en
		mdelay(20);
		reset_chip(i2c_client);
		startup_chip(i2c_client);
		gsl_proc_flag = 0;
	} else if ('r' == temp_buf[0] && 'e' == temp_buf[1]) {
		//read buf //
		memcpy(gsl_read, temp_buf, 4);
		memcpy(gsl_data_proc, buf, 8);
	} else if ('w' == temp_buf[0] && 'r' == temp_buf[1]) {
		//write buf
		i2c_smbus_write_i2c_block_data(i2c_client, buf[4], 4, buf);
	}
#ifdef GSL_NOID_VERSION
	else if ('i' == temp_buf[0] && 'd' == temp_buf[1]) {
		//write id config //
		tmp1 = (buf[7] << 24) | (buf[6] << 16) | (buf[5] << 8) | buf[4];
		tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

		if (tmp1 >= 0 && tmp1 < 512) {
			gsl_config_data_id[tmp1] = tmp;
		}
	}
#endif
	exit_write_proc_out:
	kfree(path_buf);
	return count;
}

static int gsl_server_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, gsl_config_read_proc, NULL);
}
static const struct file_operations gsl_seq_fops = {
	.open = gsl_server_list_open,
	.read = seq_read,
	.release = single_release,
	.write = gsl_config_write_proc,
	.owner = THIS_MODULE,
};

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

	if ((x_err > FILTER_MAX && y_err > FILTER_MAX / 3) ||
		(x_err > FILTER_MAX / 3 && y_err > FILTER_MAX)) {
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

	x_new =
	    x >
	    x_old[id] ? (x_old[id] + filter_step_x) : (x_old[id] -
						       filter_step_x);
	y_new =
	    y >
	    y_old[id] ? (y_old[id] + filter_step_y) : (y_old[id] -
						       filter_step_y);

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

	if (x > x_old[id]) {
		x_err = x - x_old[id];
	} else {
		x_err = x_old[id] - x;
	}

	if (y > y_old[id]) {
		y_err = y - y_old[id];
	} else {
		y_err = y_old[id] - y;
	}

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


#ifdef SLEEP_CLEAR_POINT
#ifdef HAVE_TOUCH_KEY
static void report_key(struct gsl_ts *ts, u16 x, u16 y)
{
	u16 i = 0;

	for (i = 0; i < MAX_KEY_NUM; i++) {
		if ((gsl_key_data[i].x_min < x)
		    && (x < gsl_key_data[i].x_max)
		    && (gsl_key_data[i].y_min < y)
		    && (y < gsl_key_data[i].y_max)) {
			key = gsl_key_data[i].key;
			input_report_key(ts->input, key, 1);
			input_sync(ts->input);
			key_state_flag = 1;
			break;
		}
	}
}
#endif
#endif

#ifdef RK_GEAR_TOUCH
static void report_data(struct gsl_ts *ts, u16 x, u16 y, u8 pressure, u8 id)
{
#ifdef RK_GEAR_TOUCH
	int delt_x,delt_y;
	static int old_x=0, old_y=0;
#endif
	//#ifndef SWAP_XY
	//      swap(x, y);
	//#endif
	//printk("#####id=%d,x=%d,y=%d######\n",id,x,y);

	if (x > SCREEN_MAX_X || y > SCREEN_MAX_Y) {
#ifdef HAVE_TOUCH_KEY
		//report_key(ts, x, y);
		//printk("#####report_key x=%d,y=%d######\n",x,y);
#endif
		return;
	}

	/*
	   input_mt_slot(ts->input_dev, id);
	   input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, id);
	   input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	   input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	   input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, w);
	   input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
	 */
#ifdef RK_GEAR_TOUCH
	if (g_istouch == 0){
		g_istouch = 1;
		input_event(ts->input, EV_MSC, MSC_SCAN, 0x90001);
		input_report_key(ts->input, 0x110, 1);
		input_sync(ts->input);
	}
	delt_x = (int)x - old_x;
	delt_y = (int)y - old_y;
	delt_x /= 10;
	delt_y /= 10;
	input_report_rel(ts->input, REL_Y, -delt_x);
    input_report_rel(ts->input, REL_X, -delt_y);
	input_sync(ts->input);
	old_x = x;
	old_y = y;
	return;
#endif

#ifdef REPORT_DATA_ANDROID_4_0
	//printk("#####REPORT_DATA_ANDROID_4_0######\n");
	input_mt_slot(ts->input, id);
	//input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, 1);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
#ifdef X_POL
	input_report_abs(ts->input, ABS_MT_POSITION_X, SCREEN_MAX_X - x);
#else
	input_report_abs(ts->input, ABS_MT_POSITION_X, x);
#endif
#ifdef Y_POL
	input_report_abs(ts->input, ABS_MT_POSITION_Y, (SCREEN_MAX_Y - y));
#else
	input_report_abs(ts->input, ABS_MT_POSITION_Y, (y));
#endif
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
#else
	//printk("#####nonono REPORT_DATA_ANDROID_4_0######\n");
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
	input_mt_sync(ts->input);
#endif
}
#endif

void glsx680_ts_irq_disable(struct gsl_ts *ts)
{
	unsigned long irqflags;

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (!ts->flag_irq_is_disable) {
		disable_irq_nosync(ts->client->irq);
		ts->flag_irq_is_disable = 1;
	}
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

void glsx680_ts_irq_enable(struct gsl_ts *ts)
{
	unsigned long irqflags = 0;

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (ts->flag_irq_is_disable) {
		enable_irq(ts->client->irq);
		ts->flag_irq_is_disable = 0;
	}
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

static void gslX680_ts_worker(struct work_struct *work)
{
	int rc, i;
	u8 id, touches;
	u16 x, y;

#ifdef GSL_NOID_VERSION
	u32 tmp1;
	u8 buf[4] = { 0 };
	struct gsl_touch_info cinfo;
#endif

	struct gsl_ts *ts = container_of(work, struct gsl_ts, work);

#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1)
		goto schedule;
#endif

#ifdef GSL_MONITOR
	if (i2c_lock_flag != 0)
		goto i2c_lock_schedule;
	else
		i2c_lock_flag = 1;
#endif

	rc = gsl_ts_read(ts->client, 0x80, ts->touch_data, ts->dd->data_size);
	if (rc < 0) {
		dev_err(&ts->client->dev, "read failed\n");
		goto schedule;
	}

	touches = ts->touch_data[ts->dd->touch_index];
	//print_info("-----touches: %d -----\n", touches);
#ifdef GSL_NOID_VERSION

	cinfo.finger_num = touches;
	//print_info("tp-gsl  finger_num = %d\n",cinfo.finger_num);
	for (i = 0; i < (touches < MAX_CONTACTS ? touches : MAX_CONTACTS); i++) {
		cinfo.x[i] =
		    join_bytes((ts->
				touch_data[ts->dd->x_index + 4 * i + 1] & 0xf),
			       ts->touch_data[ts->dd->x_index + 4 * i]);
		cinfo.y[i] =
		    join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
			       ts->touch_data[ts->dd->y_index + 4 * i]);
		cinfo.id[i] =
		    ((ts->touch_data[ts->dd->x_index + 4 * i + 1] & 0xf0) >> 4);
		/*print_info("tp-gsl  before: x[%d] = %d, y[%d] = %d,
		 id[%d] = %d \n",i,cinfo.x[i],i,cinfo.y[i],i,cinfo.id[i]);*/
	}
	cinfo.finger_num = (ts->touch_data[3] << 24) | (ts->touch_data[2] << 16)
	    | (ts->touch_data[1] << 8) | (ts->touch_data[0]);
	gsl_alg_id_main(&cinfo);
	tmp1 = gsl_mask_tiaoping();
	//print_info("[tp-gsl] tmp1 = %x\n", tmp1);
	if (tmp1 > 0 && tmp1 < 0xffffffff) {
		buf[0] = 0xa;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		gsl_ts_write(ts->client, 0xf0, buf, 4);
		buf[0] = (u8) (tmp1 & 0xff);
		buf[1] = (u8) ((tmp1 >> 8) & 0xff);
		buf[2] = (u8) ((tmp1 >> 16) & 0xff);
		buf[3] = (u8) ((tmp1 >> 24) & 0xff);
		print_info("tmp1=%08x,buf[0]=%02x,buf[1]=%02x,buf[2]=%02x, \
			buf[3]=%02x\n", tmp1, buf[0], buf[1], buf[2], buf[3]);
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
		x = cinfo.x[i];
		y = cinfo.y[i];
#else
		x = join_bytes((ts->
				touch_data[ts->dd->x_index + 4 * i + 1] & 0xf),
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
#ifdef RK_GEAR_TOUCH
			report_data(ts, x_new, y_new, 10, id);
#endif
			if (key_count <= 512) {
				key_x[key_count] = x_new;
				key_y[key_count] = y_new;
				key_count++;
				/*printk("test in key store in here,
				x_new is %d , y_new is %d ,
				key_count is %d \n", x_new ,y_new,key_count);*/
			}
			id_state_flag[id] = 1;
		}
	}
	for (i = 1; i <= MAX_CONTACTS; i++) {
		if ((0 == touches)
		    || ((0 != id_state_old_flag[i])
			&& (0 == id_state_flag[i]))) {
#ifdef RK_GEAR_TOUCH
			if (g_istouch == 1){
				g_istouch = 0;
				input_event(ts->input, EV_MSC, MSC_SCAN, 0x90001);
				input_report_key(ts->input, 0x110, 0);
				input_sync(ts->input);
			}
			g_istouch = 0;
#endif
#ifdef REPORT_DATA_ANDROID_4_0
			input_mt_slot(ts->input, i);
			//input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER,
						   false);
#endif
			id_sign[i] = 0;
		}
		id_state_old_flag[i] = id_state_flag[i];
	}

	if (0 == touches) {
#ifdef REPORT_DATA_ANDROID_4_0
#ifndef RK_GEAR_TOUCH
		//input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 0);
		//input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 0);
		//input_mt_sync(ts->input);

		int temp_x = 0;
		int temp_y = 0;
		temp_x =
		    (((key_x[key_count - 1] - key_x[0]) >
		      0) ? (key_x[key_count - 1] - key_x[0])
		     : (key_x[0] - key_x[key_count - 1]));
		temp_y =
		    (((key_y[key_count - 1] - key_y[0]) >
		      0) ? (key_y[key_count - 1] - key_y[0])
		     : (key_y[0] - key_y[key_count - 1]));
		if (key_count <= 512) {
			if (temp_x > temp_y) {
				if ((key_x[key_count - 1] - key_x[0]) > 100) {
					printk(" send up key \n");
					input_report_key(ts->input,
							 key_array[2], 1);
					input_sync(ts->input);
					input_report_key(ts->input,
							 key_array[2], 0);
					input_sync(ts->input);
				} else if ((key_x[0] - key_x[key_count - 1]) >
					   100) {
					printk(" send down key \n");
					input_report_key(ts->input,
							 key_array[3], 1);
					input_sync(ts->input);
					input_report_key(ts->input,
							 key_array[3], 0);
					input_sync(ts->input);
				}
			} else if (temp_x <= temp_y) {
				if ((key_y[key_count - 1] - key_y[0]) > 100) {
					printk(" send left key \n");
					input_report_key(ts->input,
							 key_array[0], 1);
					input_sync(ts->input);
					input_report_key(ts->input,
							 key_array[0], 0);
					input_sync(ts->input);
				} else if ((key_y[0] - key_y[key_count - 1]) >
					   100) {
					printk(" send right key \n");
					input_report_key(ts->input,
							 key_array[1], 1);
					input_sync(ts->input);
					input_report_key(ts->input,
							 key_array[1], 0);
					input_sync(ts->input);
				}
			}
			/*printk(" key_x[key_count -1],  key_x[0],
			key_y[key_count -1], key_y[0] is %d ,%d , %d , %d\n",
			key_x[key_count -1], key_x[0], key_y[key_count -1],
			key_y[0]);*/
			if ((key_x[key_count - 1] - key_x[0] < 50)
			    && (key_x[key_count - 1] - key_x[0] >= -50)
			    && (key_y[key_count - 1] - key_y[0] < 50)
			    && (key_y[key_count - 1] - key_y[0] >= -50)
			    && (key_x[0] != 0) && (key_y[0] != 0)) {
				//queue_work(gsl_timer_workqueue,&ts->click_work);
				//printk(" send enter2 key by yuandan \n");
				//if(send_key)
				//      {
				printk(" send enter key \n");
				input_report_key(ts->input, key_array[4], 1);
				input_sync(ts->input);
				input_report_key(ts->input, key_array[4], 0);
				input_sync(ts->input);
				//      }else
				//              {
				//down(&my_sem);
				//                      send_key = true;
				//up(&my_sem);
				//              }
			}
		} else if (key_count > 512) {
			if (temp_x > temp_y) {
				if ((key_x[511] - key_x[0]) > 100) {
					printk(" send up key \n");
					input_report_key(ts->input,
							 key_array[2], 1);
					input_sync(ts->input);
					input_report_key(ts->input,
							 key_array[2], 0);
					input_sync(ts->input);
				} else if ((key_x[0] - key_x[511]) > 100) {
					printk(" send down key \n");
					input_report_key(ts->input,
							 key_array[3], 1);
					input_sync(ts->input);
					input_report_key(ts->input,
							 key_array[3], 0);
					input_sync(ts->input);
				}
			} else if (temp_x <= temp_y) {

				if ((key_y[511] - key_y[0]) > 100) {
					printk(" send left key \n");
					input_report_key(ts->input,
							 key_array[0], 1);
					input_sync(ts->input);
					input_report_key(ts->input,
							 key_array[0], 0);
					input_sync(ts->input);
				} else if ((key_y[0] - key_y[511]) > 100) {
					printk(" send right key \n");
					input_report_key(ts->input,
							 key_array[1], 1);
					input_sync(ts->input);
					input_report_key(ts->input,
							 key_array[1], 0);
					input_sync(ts->input);
				}
			}
		}
		memset(key_y, 0, sizeof(int) * 512);
		memset(key_x, 0, sizeof(int) * 512);
		key_count = 0;
#endif
#endif
#ifdef HAVE_TOUCH_KEY
		if (key_state_flag) {
			input_report_key(ts->input, key, 0);
			input_sync(ts->input);
			key_state_flag = 0;
		}
#endif

	}

	input_sync(ts->input);

      schedule:
#ifdef GSL_MONITOR
	i2c_lock_flag = 0;
      i2c_lock_schedule:
#endif
	glsx680_ts_irq_enable(ts);

}

#ifdef HAVE_CLICK_TIMER

static void click_timer_worker(struct work_struct *work)
{
	while (true) {
		mdelay(500);
		//down(&my_sem);
		send_key = false;
		//up(&my_sem);
	}
}

#endif

#ifdef GSL_MONITOR
static void gsl_monitor_worker(struct work_struct *work)
{
	//u8 write_buf[4] = {0};
	u8 read_buf[4] = { 0 };
	char init_chip_flag = 0;

	//print_info("gsl_monitor_worker\n");
	struct gsl_ts *ts =
	    container_of(work, struct gsl_ts, gsl_monitor_work.work);
	if (i2c_lock_flag != 0) {
		i2c_lock_flag = 1;
	}
	//goto queue_monitor_work;
	else
		i2c_lock_flag = 1;

	//gsl_ts_read(ts->client, 0x80, read_buf, 4);
	/*printk("======read 0x80: %x %x %x %x ======tony0geshu\n",
	read_buf[3], read_buf[2], read_buf[1], read_buf[0]);*/

	gsl_ts_read(ts->client, 0xb0, read_buf, 4);
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a
	    || read_buf[0] != 0x5a)
		b0_counter++;
	else
		b0_counter = 0;

	if (b0_counter > 1) {
		/*printk("======read 0xb0: %x %x %x %x ======\n",
		read_buf[3], read_buf[2], read_buf[1], read_buf[0]);*/
		init_chip_flag = 1;
		b0_counter = 0;
	}

	gsl_ts_read(ts->client, 0xb4, read_buf, 4);
	int_2nd[3] = int_1st[3];
	int_2nd[2] = int_1st[2];
	int_2nd[1] = int_1st[1];
	int_2nd[0] = int_1st[0];
	int_1st[3] = read_buf[3];
	int_1st[2] = read_buf[2];
	int_1st[1] = read_buf[1];
	int_1st[0] = read_buf[0];

	/*printk("int_1st: %x %x %x %x , int_2nd: %x %x %x %x\n",
	int_1st[3], int_1st[2], int_1st[1], int_1st[0],
	int_2nd[3], int_2nd[2],int_2nd[1],int_2nd[0]);*/

	if (int_1st[3] == int_2nd[3] && int_1st[2] == int_2nd[2]
	    && int_1st[1] == int_2nd[1] && int_1st[0] == int_2nd[0]) {
		/*printk("int_1st: %x %x %x %x , int_2nd: %x %x %x %x\n",
		int_1st[3], int_1st[2], int_1st[1], int_1st[0],
		int_2nd[3], int_2nd[2],int_2nd[1],int_2nd[0]);*/
		init_chip_flag = 1;
		//goto queue_monitor_init_chip;
	}

	gsl_ts_read(ts->client, 0xbc, read_buf, 4);
	if (read_buf[3] != 0 || read_buf[2] != 0 || read_buf[1] != 0
	    || read_buf[0] != 0)
		bc_counter++;
	else
		bc_counter = 0;
	if (bc_counter > 1) {
		/*printk("======read 0xbc: %x %x %x %x======\n",
		read_buf[3], read_buf[2], read_buf[1], read_buf[0]);*/
		init_chip_flag = 1;
		bc_counter = 0;
	}

	/*
	   write_buf[3] = 0x01;
	   write_buf[2] = 0xfe;
	   write_buf[1] = 0x10;
	   write_buf[0] = 0x00;
	   gsl_ts_write(ts->client, 0xf0, write_buf, 4);
	   gsl_ts_read(ts->client, 0x10, read_buf, 4);
	   gsl_ts_read(ts->client, 0x10, read_buf, 4);

	   if(read_buf[3] < 10
	   	&& read_buf[2] < 10
		&& read_buf[1] < 10
		&& read_buf[0] < 10)
	   dac_counter ++;
	   else
	   dac_counter = 0;

	   if(dac_counter > 1)
	   {
	   printk("read DAC1_0: %x %x %x %x\n",
	   	read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
	   init_chip_flag = 1;
	   dac_counter = 0;
	   }
	 */
	//queue_monitor_init_chip:
	if (init_chip_flag)
		init_chip(ts->client, ts);

	i2c_lock_flag = 0;

	//queue_monitor_work:
	//queue_delayed_work(gsl_monitor_workqueue, &ts->gsl_monitor_work, 100);
}
#endif

static irqreturn_t gsl_ts_irq(int irq, void *dev_id)
{
	///struct gsl_ts *ts = dev_id;
	struct gsl_ts *ts = (struct gsl_ts *)dev_id;
	//print_info("========gslX680 Interrupt=========\n");

	glsx680_ts_irq_disable(ts);

	if (!work_pending(&ts->work)) {
		queue_work(ts->wq, &ts->work);
	}

	return IRQ_HANDLED;

}

static int gslX680_ts_init(struct i2c_client *client, struct gsl_ts *ts)
{
	struct input_dev *input_device;
	int rc = 0;
	int i = 0;

	printk("[GSLX680] Enter %s\n", __func__);

	ts->dd = &devices[ts->device_id];

	if (ts->device_id == 0) {
		ts->dd->data_size =
		    MAX_FINGERS * ts->dd->touch_bytes + ts->dd->touch_meta_data;
		ts->dd->touch_index = 0;
	}

	ts->touch_data =
	    devm_kzalloc(&client->dev, ts->dd->data_size, GFP_KERNEL);
	if (!ts->touch_data) {
		pr_err("%s: Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}

	input_device = devm_input_allocate_device(&ts->client->dev);
	if (!input_device) {
		rc = -ENOMEM;
		goto init_err_ret;
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
	__set_bit(EV_SYN, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	__set_bit(MT_TOOL_FINGER, input_device->keybit);
	input_mt_init_slots(input_device, (MAX_CONTACTS + 1), 0);
#else
	input_set_abs_params(input_device, ABS_MT_TRACKING_ID, 0,
			     (MAX_CONTACTS + 1), 0, 0);
	set_bit(EV_ABS, input_device->evbit);
	set_bit(EV_KEY, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	input_device->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif

#ifdef HAVE_TOUCH_KEY
	input_device->evbit[0] = BIT_MASK(EV_KEY);
	/*input_device->evbit[0] = BIT_MASK(EV_SYN)
		| BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);*/
	for (i = 0; i < MAX_KEY_NUM; i++)
		set_bit(key_array[i], input_device->keybit);
#endif

#ifdef RK_GEAR_TOUCH
	set_bit(EV_REL, input_device->evbit);
	input_set_capability(input_device, EV_REL, REL_X);
	input_set_capability(input_device, EV_REL, REL_Y);
	input_set_capability(input_device, EV_MSC, MSC_SCAN);
	input_set_capability(input_device, EV_KEY, 0x110);
#endif

	set_bit(ABS_MT_POSITION_X, input_device->absbit);
	set_bit(ABS_MT_POSITION_Y, input_device->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_device->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_device->absbit);

	input_set_abs_params(input_device, ABS_MT_POSITION_X, 0, SCREEN_MAX_X,
			     0, 0);
	input_set_abs_params(input_device, ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y,
			     0, 0);
	input_set_abs_params(input_device, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0,
			     0);
	input_set_abs_params(input_device, ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

	//client->irq = IRQ_PORT;
	//ts->irq = client->irq;

	ts->wq = create_singlethread_workqueue("kworkqueue_ts");
	if (!ts->wq) {
		dev_err(&client->dev, "gsl Could not create workqueue\n");
		goto init_err_ret;
	}
	flush_workqueue(ts->wq);

	INIT_WORK(&ts->work, gslX680_ts_worker);

	rc = input_register_device(input_device);
	if (rc)
		goto error_unreg_device;

	return 0;

      error_unreg_device:
	destroy_workqueue(ts->wq);
      init_err_ret:
	return rc;
}

#if 0
static int gsl_ts_suspend(struct i2c_client *dev, pm_message_t mesg)
{
#if 0
	struct gsl_ts *ts = dev_get_drvdata(dev);

	printk("I'am in gsl_ts_suspend() start\n");

#ifdef GSL_MONITOR
	printk("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&ts->gsl_monitor_work);
#endif

#ifdef HAVE_CLICK_TIMER
	//cancel_work_sync(&ts->click_work);
#endif
	disable_irq_nosync(ts->irq);

	gslX680_shutdown_low(ts);

#ifdef SLEEP_CLEAR_POINT
	mdelay(10);
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
	mdelay(10);
	report_data(ts, 1, 1, 10, 1);
	input_sync(ts->input);
#endif

#endif
	return 0;
}
#endif

#if 0
static int gsl_ts_resume(struct i2c_client *dev)
{
#if 0
	struct gsl_ts *ts = dev_get_drvdata(dev);

	printk("I'am in gsl_ts_resume() start\n");

	gslX680_shutdown_high(ts);
	msleep(20);
	reset_chip(ts->client);
	startup_chip(ts->client);
	check_mem_data(ts->client, ts);

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
#ifdef GSL_MONITOR
	printk("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &ts->gsl_monitor_work, 300);
#endif

#ifdef HAVE_CLICK_TIMER
	//queue_work(gsl_timer_workqueue,&ts->click_work);
#endif

	disable_irq_nosync(ts->irq);
	enable_irq(ts->irq);
#endif

	return 0;
}
#endif 

static int gsl_ts_early_suspend(struct tp_device *tp_d)
{
	struct gsl_ts *ts = container_of(tp_d, struct gsl_ts, tp);
	printk("[GSLX680] Enter %s\n", __func__);
	//gsl_ts_suspend(&ts->client->dev);
#ifdef GSL_MONITOR
	printk("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&ts->gsl_monitor_work);
#endif

	glsx680_ts_irq_disable(ts);
	cancel_work_sync(&ts->work);

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
	gslX680_shutdown_low(ts);
	return 0;
}

static int gsl_ts_late_resume(struct tp_device *tp_d)
{
	struct gsl_ts *ts = container_of(tp_d, struct gsl_ts, tp);
	printk("[GSLX680] Enter %s\n", __func__);
	//gsl_ts_resume(&ts->client->dev);

	printk("I'am in gsl_ts_resume() start\n");

	gslX680_shutdown_high(ts);
	msleep(20);
	reset_chip(ts->client);
	startup_chip(ts->client);
	check_mem_data(ts->client, ts);

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
#ifdef GSL_MONITOR
	printk("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &ts->gsl_monitor_work, 300);
#endif
	glsx680_ts_irq_enable(ts);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND

static void gsl_ts_early_suspend(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);
	printk("[GSLX680] Enter %s\n", __func__);
	//gsl_ts_suspend(&ts->client->dev);
#ifdef GSL_MONITOR
	printk("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&ts->gsl_monitor_work);
#endif

	glsx680_ts_irq_disable(ts);
	cancel_work_sync(&ts->work);

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
	gslX680_shutdown_low(ts);
	return 0;
}

static void gsl_ts_late_resume(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);
	printk("[GSLX680] Enter %s\n", __func__);
	//gsl_ts_resume(&ts->client->dev);
	int i;

	printk("I'am in gsl_ts_resume() start\n");

	gslX680_shutdown_high(ts);
	msleep(20);
	reset_chip(ts->client);
	startup_chip(ts->client);
	check_mem_data(ts->client, ts);

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
#ifdef GSL_MONITOR
	printk("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &ts->gsl_monitor_work, 300);
#endif
	glsx680_ts_irq_enable(ts);

}
#endif

//static struct wake_lock touch_wakelock;

static int gsl_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct gsl_ts *ts;
	int rc;

	printk("GSLX680 Enter %s\n", __func__);
	//wake_lock_init(&touch_wakelock, WAKE_LOCK_SUSPEND, "touch");
	//wake_lock(&touch_wakelock); //system do not enter deep sleep
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "gsl I2C functionality not supported\n");
		return -ENODEV;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->tp.tp_suspend = gsl_ts_early_suspend;
	ts->tp.tp_resume = gsl_ts_late_resume;
	tp_register_fb(&ts->tp);

	ts->client = client;
	i2c_set_clientdata(client, ts);
	//ts->device_id = id->driver_data;

	gslX680_init(ts);
	rc = gslX680_ts_init(client, ts);
	if (rc < 0) {
		dev_err(&client->dev, "gsl GSLX680 init failed\n");
		goto porbe_err_ret;
	}
	//#ifdef GSLX680_COMPATIBLE
	//      judge_chip_type(client);
	//#endif
	//printk("#####################  probe [2]chip_type=%c .\n",chip_type);
	init_chip(ts->client, ts);
	check_mem_data(ts->client, ts);
	spin_lock_init(&ts->irq_lock);
	client->irq = gpio_to_irq(ts->irq);
	rc = request_irq(client->irq, gsl_ts_irq, IRQF_TRIGGER_RISING,
			 client->name, ts);
	if (rc < 0) {
		printk("gsl_probe: request irq failed\n");
		goto porbe_err_ret;
	}

	/* create debug attribute */
	//rc = device_create_file(&ts->input->dev, &dev_attr_debug_enable);

#ifdef CONFIG_HAS_EARLYSUSPEND

	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	//ts->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	ts->early_suspend.suspend = gsl_ts_early_suspend;
	ts->early_suspend.resume = gsl_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

#ifdef GSL_MONITOR

	INIT_DELAYED_WORK(&ts->gsl_monitor_work, gsl_monitor_worker);
	gsl_monitor_workqueue =
	    create_singlethread_workqueue("gsl_monitor_workqueue");
	queue_delayed_work(gsl_monitor_workqueue, &ts->gsl_monitor_work, 1000);
#endif

#ifdef HAVE_CLICK_TIMER
	sema_init(&my_sem, 1);
	INIT_WORK(&ts->click_work, click_timer_worker);
	gsl_timer_workqueue = create_singlethread_workqueue("click_timer");
	queue_work(gsl_timer_workqueue, &ts->click_work);
#endif

#ifdef TPD_PROC_DEBUG
#if 0
	gsl_config_proc = create_proc_entry(GSL_CONFIG_PROC_FILE, 0666, NULL);
	printk("[tp-gsl] [%s] gsl_config_proc = %x \n", __func__,
	       gsl_config_proc);
	if (gsl_config_proc == NULL) {
		print_info("create_proc_entry %s failed\n",
			   GSL_CONFIG_PROC_FILE);
	} else {
		gsl_config_proc->read_proc = gsl_config_read_proc;
		gsl_config_proc->write_proc = gsl_config_write_proc;
	}
#else
	i2c_client = client;
	proc_create(GSL_CONFIG_PROC_FILE, 0666, NULL, &gsl_seq_fops);
#endif
	gsl_proc_flag = 0;
#endif
	//disable_irq_nosync(->irq);
	printk("[GSLX680] End %s\n", __func__);

	return 0;

      porbe_err_ret:
	return rc;
}

static int gsl_ts_remove(struct i2c_client *client)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif

#ifdef GSL_MONITOR
	cancel_delayed_work_sync(&ts->gsl_monitor_work);
	destroy_workqueue(gsl_monitor_workqueue);
#endif

#ifdef HAVE_CLICK_TIMER
	cancel_work_sync(&ts->click_work);
	destroy_workqueue(gsl_timer_workqueue);
#endif

	device_init_wakeup(&client->dev, 0);
	cancel_work_sync(&ts->work);
	free_irq(ts->client->irq, ts);
	destroy_workqueue(ts->wq);
	//device_remove_file(&ts->input->dev, &dev_attr_debug_enable);

	return 0;
}

static struct of_device_id gsl_ts_ids[] = {
	{.compatible = "gslX680"},
	{}
};

static const struct i2c_device_id gsl_ts_id[] = {
	{GSLX680_I2C_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, gsl_ts_id);

static struct i2c_driver gsl_ts_driver = {
	.driver = {
		   .name = GSLX680_I2C_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(gsl_ts_ids),
		   },
#if 0 //ndef CONFIG_HAS_EARLYSUSPEND
	.suspend = gsl_ts_suspend,
	.resume = gsl_ts_resume,
#endif
	.probe = gsl_ts_probe,
	.remove = gsl_ts_remove,
	.id_table = gsl_ts_id,
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
	return;
}

module_init(gsl_ts_init);
module_exit(gsl_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GSLX680 touchscreen controller driver");
MODULE_AUTHOR("Guan Yuwei, guanyuwei@basewin.com");
MODULE_ALIAS("platform:gsl_ts");
