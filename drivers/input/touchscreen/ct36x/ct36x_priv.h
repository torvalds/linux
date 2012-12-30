#ifndef __CT36X_PRIV__
#define __CT36X_PRIV__

#include <linux/earlysuspend.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/input/mt.h>

#include <linux/ct36x.h>

#include <mach/board.h>
#include <mach/gpio.h>
#if 1
#define ct36x_dbg(ts, format, arg...)            \
	        dev_printk(KERN_INFO , ts->dev , format , ## arg)
#else
#define ct36x_dbg(ts, format, arg...)
#endif

#define CT36X_I2C_RATE	(100 * 1000)
struct ct36x_data;

struct ct36x_ops{
	int (*init)(struct ct36x_data *);
	void (*deinit)(struct ct36x_data *);
	int (*suspend)(struct ct36x_data *);
	int (*resume)(struct ct36x_data *);
	void (*report)(struct ct36x_data *);
};
struct ct36x_data{
	int irq;
	int model;
	int x_max;
	int y_max;
	int orientation[4];
	int point_num;

	struct ct36x_gpio rst_io;
	struct ct36x_gpio irq_io;

	struct device *dev;
	struct i2c_client *client;

	struct      input_dev *input;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct      early_suspend early_suspend;
#endif
	struct ct36x_ops *ops;
	void *priv;
};

static inline int ct36x_read(struct ct36x_data *ts, char *buf, int len)
{
	return i2c_master_normal_recv(ts->client, buf, len, CT36X_I2C_RATE);
}

static inline int ct36x_write(struct ct36x_data *ts, char *buf, int len)
{
	return i2c_master_normal_send(ts->client, buf, len, CT36X_I2C_RATE);
}

static inline int ct36x_update_read(struct ct36x_data *ts, unsigned short addr, char *buf, int len)
{
	int ret;
	unsigned short bak = ts->client->addr;

	ts->client->addr = addr;
	ret = ct36x_read(ts, buf, len);
	ts->client->addr = bak;

	return ret;
}

static inline int ct36x_update_write(struct ct36x_data *ts, unsigned short addr, char *buf, int len)
{
	int ret;
	unsigned short bak = ts->client->addr;

	ts->client->addr = addr;
	ret = ct36x_write(ts, buf, len);
	ts->client->addr = bak;

	return ret;
}
int ct36x_chip_set_idle(struct ct36x_data *ts);
int ct36x_chip_get_binchksum(void);
int ct36x_chip_get_fwchksum(struct ct36x_data *ts);
int ct36x_chip_go_bootloader(struct ct36x_data *ts);
int ct36x_chip_get_fwchksum(struct ct36x_data *ts);
int ct36x_chip_get_ver(struct ct36x_data *ts);

#endif
