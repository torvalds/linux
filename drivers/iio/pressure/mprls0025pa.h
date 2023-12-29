/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MPRLS0025PA - Honeywell MicroPressure pressure sensor series driver
 *
 * Copyright (c) Andreas Klinger <ak@it-klinger.de>
 *
 * Data sheet:
 *  https://prod-edam.honeywell.com/content/dam/honeywell-edam/sps/siot/en-us/products/sensors/pressure-sensors/board-mount-pressure-sensors/micropressure-mpr-series/documents/sps-siot-mpr-series-datasheet-32332628-ciid-172626.pdf
 */

#ifndef _MPRLS0025PA_H
#define _MPRLS0025PA_H

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/stddef.h>
#include <linux/types.h>

#include <linux/iio/iio.h>

#define MPR_MEASUREMENT_RD_SIZE 4
#define MPR_CMD_NOP      0xf0
#define MPR_CMD_SYNC     0xaa
#define MPR_PKT_NOP_LEN  MPR_MEASUREMENT_RD_SIZE
#define MPR_PKT_SYNC_LEN 3

struct device;

struct iio_chan_spec;
struct iio_dev;

struct mpr_data;
struct mpr_ops;

/**
 * struct mpr_chan
 * @pres: pressure value
 * @ts: timestamp
 */
struct mpr_chan {
	s32 pres;
	s64 ts;
};

enum mpr_func_id {
	MPR_FUNCTION_A,
	MPR_FUNCTION_B,
	MPR_FUNCTION_C,
};

/**
 * struct mpr_data
 * @dev: current device structure
 * @ops: functions that implement the sensor reads/writes, bus init
 * @lock: access to device during read
 * @pmin: minimal pressure in pascal
 * @pmax: maximal pressure in pascal
 * @function: transfer function
 * @outmin: minimum raw pressure in counts (based on transfer function)
 * @outmax: maximum raw pressure in counts (based on transfer function)
 * @scale: pressure scale
 * @scale2: pressure scale, decimal number
 * @offset: pressure offset
 * @offset2: pressure offset, decimal number
 * @gpiod_reset: reset
 * @irq: end of conversion irq. used to distinguish between irq mode and
 *       reading in a loop until data is ready
 * @completion: handshake from irq to read
 * @chan: channel values for buffered mode
 * @buffer: raw conversion data
 */
struct mpr_data {
	struct device		*dev;
	const struct mpr_ops	*ops;
	struct mutex		lock;
	u32			pmin;
	u32			pmax;
	enum mpr_func_id	function;
	u32			outmin;
	u32			outmax;
	int			scale;
	int			scale2;
	int			offset;
	int			offset2;
	struct gpio_desc	*gpiod_reset;
	int			irq;
	struct completion	completion;
	struct mpr_chan		chan;
	u8	    buffer[MPR_MEASUREMENT_RD_SIZE] __aligned(IIO_DMA_MINALIGN);
};

struct mpr_ops {
	int (*init)(struct device *dev);
	int (*read)(struct mpr_data *data, const u8 cmd, const u8 cnt);
	int (*write)(struct mpr_data *data, const u8 cmd, const u8 cnt);
};

int mpr_common_probe(struct device *dev, const struct mpr_ops *ops, int irq);

#endif
