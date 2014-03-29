#ifndef __CT36X_PRIV__
#define __CT36X_PRIV__

//#include <linux/earlysuspend.h>
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

#include "../tp_suspend.h"

int flag_ct36x_model;

int ct36x_dbg_level = 0;
module_param_named(dbg_level, ct36x_dbg_level, int, 0644);
#if 1
#define ct36x_dbg(ts, format, arg...)            \
	do { \
		if (ct36x_dbg_level) { \
			dev_printk(KERN_INFO , ts->dev , format , ## arg) ;\
		} \
	} while (0)
#else 
#define DBG(x...)
#endif




#define CT36X_I2C_RATE	(200 * 1000)
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

	struct  tp_device  tp;
	struct ct36x_ops *ops;
	void *priv;
};

static int i2c_master_normal_send(const struct i2c_client *client, const char *buf, int count, int scl_rate)
{
        int ret;
        struct i2c_adapter *adap=client->adapter;
        struct i2c_msg msg;

        msg.addr = client->addr;
        msg.flags = client->flags;
        msg.len = count;
        msg.buf = (char *)buf;
        msg.scl_rate = scl_rate;
        //msg.udelay = client->udelay;

        ret = i2c_transfer(adap, &msg, 1);
        return (ret == 1) ? count : ret;
}

static int i2c_master_normal_recv(const struct i2c_client *client, char *buf, int count, int scl_rate)
{
        struct i2c_adapter *adap=client->adapter;
        struct i2c_msg msg;
        int ret;

        msg.addr = client->addr;
        msg.flags = client->flags | I2C_M_RD;
        msg.len = count;
        msg.buf = (char *)buf;
        msg.scl_rate = scl_rate;
        //msg.udelay = client->udelay;

        ret = i2c_transfer(adap, &msg, 1);

        return (ret == 1) ? count : ret;
}
EXPORT_SYMBOL(i2c_master_normal_recv);

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
int ct36x_chip_go_sleep(struct ct36x_data *ts);
int ct36x_chip_get_binchksum(void);
int ct36x_chip_get_fwchksum(struct ct36x_data *ts);
int ct36x_chip_go_bootloader(struct ct36x_data *ts);
int ct36x_chip_get_fwchksum(struct ct36x_data *ts);
int ct36x_chip_get_ver(struct ct36x_data *ts);

#endif
