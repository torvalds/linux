// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wm831x-otp.c  --  OTP for Wolfson WM831x PMICs
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/random.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/otp.h>

/* In bytes */
#define WM831X_UNIQUE_ID_LEN 16

/* Read the unique ID from the chip into id */
static int wm831x_unique_id_read(struct wm831x *wm831x, char *id)
{
	int i, val;

	for (i = 0; i < WM831X_UNIQUE_ID_LEN / 2; i++) {
		val = wm831x_reg_read(wm831x, WM831X_UNIQUE_ID_1 + i);
		if (val < 0)
			return val;

		id[i * 2]       = (val >> 8) & 0xff;
		id[(i * 2) + 1] = val & 0xff;
	}

	return 0;
}

static ssize_t wm831x_unique_id_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct wm831x *wm831x = dev_get_drvdata(dev);
	int rval;
	char id[WM831X_UNIQUE_ID_LEN];

	rval = wm831x_unique_id_read(wm831x, id);
	if (rval < 0)
		return 0;

	return sprintf(buf, "%*phN\n", WM831X_UNIQUE_ID_LEN, id);
}

static DEVICE_ATTR(unique_id, 0444, wm831x_unique_id_show, NULL);

int wm831x_otp_init(struct wm831x *wm831x)
{
	char uuid[WM831X_UNIQUE_ID_LEN];
	int ret;

	ret = device_create_file(wm831x->dev, &dev_attr_unique_id);
	if (ret != 0)
		dev_err(wm831x->dev, "Unique ID attribute not created: %d\n",
			ret);

	ret = wm831x_unique_id_read(wm831x, uuid);
	if (ret == 0)
		add_device_randomness(uuid, sizeof(uuid));
	else
		dev_err(wm831x->dev, "Failed to read UUID: %d\n", ret);

	return ret;
}

void wm831x_otp_exit(struct wm831x *wm831x)
{
	device_remove_file(wm831x->dev, &dev_attr_unique_id);
}

