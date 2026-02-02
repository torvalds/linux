/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Honeywell ABP2 series pressure sensor driver
 *
 * Copyright (c) 2025 Petre Rodan <petre.rodan@subdimension.ro>
 */

#ifndef _ABP2030PA_H
#define _ABP2030PA_H

#include <linux/completion.h>
#include <linux/types.h>

#include <linux/iio/iio.h>

#define ABP2_MEASUREMENT_RD_SIZE 7

struct device;

struct abp2_data;
struct abp2_ops;

enum abp2_func_id {
	ABP2_FUNCTION_A,
};

/**
 * struct abp2_data
 * @dev: current device structure
 * @ops: pointers for bus specific read and write functions
 * @pmin: minimal pressure in pascal
 * @pmax: maximal pressure in pascal
 * @outmin: minimum raw pressure in counts (based on transfer function)
 * @outmax: maximum raw pressure in counts (based on transfer function)
 * @function: transfer function
 * @p_scale: pressure scale
 * @p_scale_dec: pressure scale, decimal number
 * @p_offset: pressure offset
 * @irq: end of conversion - applies only to the i2c sensor
 * @completion: handshake from irq to read
 * @scan: channel values for buffered mode
 * @tx_buf: transmit buffer used during the SPI communication
 * @rx_buf: raw data provided by sensor
 */
struct abp2_data {
	struct device *dev;
	const struct abp2_ops *ops;
	s32 pmin;
	s32 pmax;
	u32 outmin;
	u32 outmax;
	enum abp2_func_id function;
	int p_scale;
	int p_scale_dec;
	int p_offset;
	int irq;
	struct completion completion;
	struct {
		u32 chan[2];
		aligned_s64 timestamp;
	} scan;
	u8 rx_buf[ABP2_MEASUREMENT_RD_SIZE] __aligned(IIO_DMA_MINALIGN);
	u8 tx_buf[ABP2_MEASUREMENT_RD_SIZE];
};

struct abp2_ops {
	int (*read)(struct abp2_data *data, u8 cmd, u8 nbytes);
	int (*write)(struct abp2_data *data, u8 cmd, u8 nbytes);
};

int abp2_common_probe(struct device *dev, const struct abp2_ops *ops, int irq);

#endif
