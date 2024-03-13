/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Anybus-S client adapter definitions
 *
 * Copyright 2018 Arcx Inc
 */

#ifndef __LINUX_ANYBUSS_CLIENT_H__
#define __LINUX_ANYBUSS_CLIENT_H__

#include <linux/device.h>
#include <linux/types.h>
#include <linux/poll.h>

/* move to <linux/fieldbus_dev.h> when taking this out of staging */
#include "../fieldbus_dev.h"

struct anybuss_host;

struct anybuss_client {
	struct device dev;
	struct anybuss_host *host;
	__be16 anybus_id;
	/*
	 * these can be optionally set by the client to receive event
	 * notifications from the host.
	 */
	void (*on_area_updated)(struct anybuss_client *client);
	void (*on_online_changed)(struct anybuss_client *client, bool online);
};

struct anybuss_client_driver {
	struct device_driver driver;
	int (*probe)(struct anybuss_client *adev);
	void (*remove)(struct anybuss_client *adev);
	u16 anybus_id;
};

int anybuss_client_driver_register(struct anybuss_client_driver *drv);
void anybuss_client_driver_unregister(struct anybuss_client_driver *drv);

static inline struct anybuss_client *to_anybuss_client(struct device *dev)
{
	return container_of(dev, struct anybuss_client, dev);
}

static inline struct anybuss_client_driver *
to_anybuss_client_driver(struct device_driver *drv)
{
	return container_of(drv, struct anybuss_client_driver, driver);
}

static inline void *
anybuss_get_drvdata(const struct anybuss_client *client)
{
	return dev_get_drvdata(&client->dev);
}

static inline void
anybuss_set_drvdata(struct anybuss_client *client, void *data)
{
	dev_set_drvdata(&client->dev, data);
}

int anybuss_set_power(struct anybuss_client *client, bool power_on);

struct anybuss_memcfg {
	u16 input_io;
	u16 input_dpram;
	u16 input_total;

	u16 output_io;
	u16 output_dpram;
	u16 output_total;

	enum fieldbus_dev_offl_mode offl_mode;
};

int anybuss_start_init(struct anybuss_client *client,
		       const struct anybuss_memcfg *cfg);
int anybuss_finish_init(struct anybuss_client *client);
int anybuss_read_fbctrl(struct anybuss_client *client, u16 addr,
			void *buf, size_t count);
int anybuss_send_msg(struct anybuss_client *client, u16 cmd_num,
		     const void *buf, size_t count);
int anybuss_send_ext(struct anybuss_client *client, u16 cmd_num,
		     const void *buf, size_t count);
int anybuss_recv_msg(struct anybuss_client *client, u16 cmd_num,
		     void *buf, size_t count);

/* these help clients make a struct file_operations */
int anybuss_write_input(struct anybuss_client *client,
			const char __user *buf, size_t size,
				loff_t *offset);
int anybuss_read_output(struct anybuss_client *client,
			char __user *buf, size_t size,
				loff_t *offset);

#endif /* __LINUX_ANYBUSS_CLIENT_H__ */
