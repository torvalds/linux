// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb/cdc.h>
#include <linux/serial.h>
#include "gdm_tty.h"

#define GDM_TTY_MAJOR 0
#define GDM_TTY_MINOR 32

#define WRITE_SIZE 2048

#define MUX_TX_MAX_SIZE 2048

static inline bool gdm_tty_ready(struct gdm *gdm)
{
	return gdm && gdm->tty_dev && gdm->port.count;
}

static struct tty_driver *gdm_driver[TTY_MAX_COUNT];
static struct gdm *gdm_table[TTY_MAX_COUNT][GDM_TTY_MINOR];
static DEFINE_MUTEX(gdm_table_lock);

static const char *DRIVER_STRING[TTY_MAX_COUNT] = {"GCTATC", "GCTDM"};
static char *DEVICE_STRING[TTY_MAX_COUNT] = {"GCT-ATC", "GCT-DM"};

static void gdm_port_destruct(struct tty_port *port)
{
	struct gdm *gdm = container_of(port, struct gdm, port);

	mutex_lock(&gdm_table_lock);
	gdm_table[gdm->index][gdm->minor] = NULL;
	mutex_unlock(&gdm_table_lock);

	kfree(gdm);
}

static const struct tty_port_operations gdm_port_ops = {
	.destruct = gdm_port_destruct,
};

static int gdm_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct gdm *gdm = NULL;
	int ret;

	ret = match_string(DRIVER_STRING, TTY_MAX_COUNT,
			   tty->driver->driver_name);
	if (ret < 0)
		return -ENODEV;

	mutex_lock(&gdm_table_lock);
	gdm = gdm_table[ret][tty->index];
	if (!gdm) {
		mutex_unlock(&gdm_table_lock);
		return -ENODEV;
	}

	tty_port_get(&gdm->port);

	ret = tty_standard_install(driver, tty);
	if (ret) {
		tty_port_put(&gdm->port);
		mutex_unlock(&gdm_table_lock);
		return ret;
	}

	tty->driver_data = gdm;
	mutex_unlock(&gdm_table_lock);

	return 0;
}

static int gdm_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct gdm *gdm = tty->driver_data;

	return tty_port_open(&gdm->port, tty, filp);
}

static void gdm_tty_cleanup(struct tty_struct *tty)
{
	struct gdm *gdm = tty->driver_data;

	tty_port_put(&gdm->port);
}

static void gdm_tty_hangup(struct tty_struct *tty)
{
	struct gdm *gdm = tty->driver_data;

	tty_port_hangup(&gdm->port);
}

static void gdm_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct gdm *gdm = tty->driver_data;

	tty_port_close(&gdm->port, tty, filp);
}

static int gdm_tty_recv_complete(void *data,
				 int len,
				 int index,
				 struct tty_dev *tty_dev,
				 int complete)
{
	struct gdm *gdm = tty_dev->gdm[index];

	if (!gdm_tty_ready(gdm)) {
		if (complete == RECV_PACKET_PROCESS_COMPLETE)
			gdm->tty_dev->recv_func(gdm->tty_dev->priv_dev,
						gdm_tty_recv_complete);
		return TO_HOST_PORT_CLOSE;
	}

	if (data && len) {
		if (tty_buffer_request_room(&gdm->port, len) == len) {
			tty_insert_flip_string(&gdm->port, data, len);
			tty_flip_buffer_push(&gdm->port);
		} else {
			return TO_HOST_BUFFER_REQUEST_FAIL;
		}
	}

	if (complete == RECV_PACKET_PROCESS_COMPLETE)
		gdm->tty_dev->recv_func(gdm->tty_dev->priv_dev,
					gdm_tty_recv_complete);

	return 0;
}

static void gdm_tty_send_complete(void *arg)
{
	struct gdm *gdm = arg;

	if (!gdm_tty_ready(gdm))
		return;

	tty_port_tty_wakeup(&gdm->port);
}

static ssize_t gdm_tty_write(struct tty_struct *tty, const u8 *buf, size_t len)
{
	struct gdm *gdm = tty->driver_data;
	size_t remain = len;
	size_t sent_len = 0;

	if (!gdm_tty_ready(gdm))
		return -ENODEV;

	while (remain) {
		size_t sending_len = min_t(size_t, MUX_TX_MAX_SIZE, remain);

		gdm->tty_dev->send_func(gdm->tty_dev->priv_dev,
					(void *)(buf + sent_len),
					sending_len,
					gdm->index,
					gdm_tty_send_complete,
					gdm);
		sent_len += sending_len;
		remain -= sending_len;
	}

	return len;
}

static unsigned int gdm_tty_write_room(struct tty_struct *tty)
{
	struct gdm *gdm = tty->driver_data;

	if (!gdm_tty_ready(gdm))
		return 0;

	return WRITE_SIZE;
}

int register_lte_tty_device(struct tty_dev *tty_dev, struct device *device)
{
	struct gdm *gdm;
	int i;
	int j;

	for (i = 0; i < TTY_MAX_COUNT; i++) {
		gdm = kmalloc(sizeof(*gdm), GFP_KERNEL);
		if (!gdm)
			return -ENOMEM;

		mutex_lock(&gdm_table_lock);
		for (j = 0; j < GDM_TTY_MINOR; j++) {
			if (!gdm_table[i][j])
				break;
		}

		if (j == GDM_TTY_MINOR) {
			kfree(gdm);
			mutex_unlock(&gdm_table_lock);
			return -EINVAL;
		}

		gdm_table[i][j] = gdm;
		mutex_unlock(&gdm_table_lock);

		tty_dev->gdm[i] = gdm;
		tty_port_init(&gdm->port);

		gdm->port.ops = &gdm_port_ops;
		gdm->index = i;
		gdm->minor = j;
		gdm->tty_dev = tty_dev;

		tty_port_register_device(&gdm->port, gdm_driver[i],
					 gdm->minor, device);
	}

	for (i = 0; i < MAX_ISSUE_NUM; i++)
		gdm->tty_dev->recv_func(gdm->tty_dev->priv_dev,
					gdm_tty_recv_complete);

	return 0;
}

void unregister_lte_tty_device(struct tty_dev *tty_dev)
{
	struct gdm *gdm;
	struct tty_struct *tty;
	int i;

	for (i = 0; i < TTY_MAX_COUNT; i++) {
		gdm = tty_dev->gdm[i];
		if (!gdm)
			continue;

		mutex_lock(&gdm_table_lock);
		gdm_table[gdm->index][gdm->minor] = NULL;
		mutex_unlock(&gdm_table_lock);

		tty = tty_port_tty_get(&gdm->port);
		if (tty) {
			tty_vhangup(tty);
			tty_kref_put(tty);
		}

		tty_unregister_device(gdm_driver[i], gdm->minor);
		tty_port_put(&gdm->port);
	}
}

static const struct tty_operations gdm_tty_ops = {
	.install =	gdm_tty_install,
	.open =		gdm_tty_open,
	.close =	gdm_tty_close,
	.cleanup =	gdm_tty_cleanup,
	.hangup =	gdm_tty_hangup,
	.write =	gdm_tty_write,
	.write_room =	gdm_tty_write_room,
};

int register_lte_tty_driver(void)
{
	struct tty_driver *tty_driver;
	int i;
	int ret;

	for (i = 0; i < TTY_MAX_COUNT; i++) {
		tty_driver = tty_alloc_driver(GDM_TTY_MINOR,
				TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV);
		if (IS_ERR(tty_driver))
			return PTR_ERR(tty_driver);

		tty_driver->owner = THIS_MODULE;
		tty_driver->driver_name = DRIVER_STRING[i];
		tty_driver->name = DEVICE_STRING[i];
		tty_driver->major = GDM_TTY_MAJOR;
		tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
		tty_driver->subtype = SERIAL_TYPE_NORMAL;
		tty_driver->init_termios = tty_std_termios;
		tty_driver->init_termios.c_cflag = B9600 | CS8 | HUPCL | CLOCAL;
		tty_driver->init_termios.c_lflag = ISIG | ICANON | IEXTEN;
		tty_set_operations(tty_driver, &gdm_tty_ops);

		ret = tty_register_driver(tty_driver);
		if (ret) {
			tty_driver_kref_put(tty_driver);
			return ret;
		}

		gdm_driver[i] = tty_driver;
	}

	return ret;
}

void unregister_lte_tty_driver(void)
{
	struct tty_driver *tty_driver;
	int i;

	for (i = 0; i < TTY_MAX_COUNT; i++) {
		tty_driver = gdm_driver[i];
		if (tty_driver) {
			tty_unregister_driver(tty_driver);
			tty_driver_kref_put(tty_driver);
		}
	}
}

