/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Device probe and register.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 */
#ifndef WFX_MAIN_H
#define WFX_MAIN_H

#include <linux/device.h>
#include <linux/gpio/consumer.h>

#include "bus.h"
#include "hif_api_general.h"

struct wfx_dev;

struct wfx_platform_data {
	/* Keyset and ".sec" extention will appended to this string */
	const char *file_fw;
	const char *file_pds;
	struct gpio_desc *gpio_wakeup;
	/*
	 * if true HIF D_out is sampled on the rising edge of the clock
	 * (intended to be used in 50Mhz SDIO)
	 */
	bool use_rising_clk;
};

struct wfx_dev *wfx_init_common(struct device *dev,
				const struct wfx_platform_data *pdata,
				const struct hwbus_ops *hwbus_ops,
				void *hwbus_priv);

int wfx_probe(struct wfx_dev *wdev);
void wfx_release(struct wfx_dev *wdev);

struct gpio_desc *wfx_get_gpio(struct device *dev, int override,
			       const char *label);
bool wfx_api_older_than(struct wfx_dev *wdev, int major, int minor);
int wfx_send_pds(struct wfx_dev *wdev, unsigned char *buf, size_t len);

#endif
