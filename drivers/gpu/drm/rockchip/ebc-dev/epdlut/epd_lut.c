// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>

#include "../ebc_dev.h"
#include "epd_lut.h"

static int (*lut_get)(struct epd_lut_data *, enum epd_lut_type, int);

int epd_lut_from_mem_init(void *waveform)
{
	int ret = -1;

	ret = rkf_wf_input(waveform);
	if (ret < 0) {
		printk("[lut]: failed to input RKF waveform\n");
	} else {
		printk("[lut]: RKF waveform\n");
		lut_get = rkf_wf_get_lut;
		return 0;
	}

	ret = pvi_wf_input(waveform);
	if (ret < 0) {
		printk("[lut]: failed to input PVI waveform\n");
	} else {
		printk("[lut]: PVI waveform\n");
		lut_get = pvi_wf_get_lut;
		return 0;
	}

	return ret;
}

int epd_lut_from_file_init(struct device *dev, void *waveform, int size)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware_into_buf(&fw, "waveform.bin", dev, waveform, size);
	if (ret) {
		dev_err(dev, "failed to load waveform firmware: %d\n", ret);
		return ret;
	}

	return epd_lut_from_mem_init(waveform);
}

const char *epd_lut_get_wf_version(void)
{
	if (rkf_wf_get_version())
		return rkf_wf_get_version();
	if (pvi_wf_get_version())
		return pvi_wf_get_version();
	return NULL;
}

int epd_lut_get(struct epd_lut_data *output, enum epd_lut_type lut_type, int temperture)
{
	return lut_get(output, lut_type, temperture);
}
