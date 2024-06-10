/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2017 Pengutronix, Uwe Kleine-König <kernel@pengutronix.de>
 */
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/siox.h>

#define to_siox_master(_dev)	container_of((_dev), struct siox_master, dev)
struct siox_master {
	/* these fields should be initialized by the driver */
	int busno;
	int (*pushpull)(struct siox_master *smaster,
			size_t setbuf_len, const u8 setbuf[],
			size_t getbuf_len, u8 getbuf[]);

	/* might be initialized by the driver, if 0 it is set to HZ / 40 */
	unsigned long poll_interval; /* in jiffies */

	/* framework private stuff */
	struct mutex lock;
	bool active;
	struct module *owner;
	struct device dev;
	unsigned int num_devices;
	struct list_head devices;

	size_t setbuf_len, getbuf_len;
	size_t buf_len;
	u8 *buf;
	u8 status;

	unsigned long last_poll;
	struct task_struct *poll_thread;
};

static inline void *siox_master_get_devdata(struct siox_master *smaster)
{
	return dev_get_drvdata(&smaster->dev);
}

struct siox_master *siox_master_alloc(struct device *dev, size_t size);
static inline void siox_master_put(struct siox_master *smaster)
{
	put_device(&smaster->dev);
}

struct siox_master *devm_siox_master_alloc(struct device *dev, size_t size);

int siox_master_register(struct siox_master *smaster);
void siox_master_unregister(struct siox_master *smaster);

int devm_siox_master_register(struct device *dev, struct siox_master *smaster);
