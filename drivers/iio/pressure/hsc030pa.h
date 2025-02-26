/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Honeywell TruStability HSC Series pressure/temperature sensor
 *
 * Copyright (c) 2023 Petre Rodan <petre.rodan@subdimension.ro>
 */

#ifndef _HSC030PA_H
#define _HSC030PA_H

#include <linux/types.h>

#include <linux/iio/iio.h>

#define HSC_REG_MEASUREMENT_RD_SIZE 4
#define HSC_RESP_TIME_MS            2

struct device;

struct iio_chan_spec;
struct iio_dev;

struct hsc_data;
struct hsc_chip_data;

typedef int (*hsc_recv_fn)(struct hsc_data *);

/**
 * struct hsc_data
 * @dev: current device structure
 * @chip: structure containing chip's channel properties
 * @recv_cb: function that implements the chip reads
 * @is_valid: true if last transfer has been validated
 * @pmin: minimum measurable pressure limit
 * @pmax: maximum measurable pressure limit
 * @outmin: minimum raw pressure in counts (based on transfer function)
 * @outmax: maximum raw pressure in counts (based on transfer function)
 * @function: transfer function
 * @p_scale: pressure scale
 * @p_scale_dec: pressure scale, decimal places
 * @p_offset: pressure offset
 * @p_offset_dec: pressure offset, decimal places
 * @buffer: raw conversion data
 */
struct hsc_data {
	struct device *dev;
	const struct hsc_chip_data *chip;
	hsc_recv_fn recv_cb;
	bool is_valid;
	s32 pmin;
	s32 pmax;
	u32 outmin;
	u32 outmax;
	u32 function;
	s64 p_scale;
	s32 p_scale_dec;
	s64 p_offset;
	s32 p_offset_dec;
	struct {
		__be16 chan[2];
		aligned_s64 timestamp;
	} scan;
	u8 buffer[HSC_REG_MEASUREMENT_RD_SIZE] __aligned(IIO_DMA_MINALIGN);
};

struct hsc_chip_data {
	bool (*valid)(struct hsc_data *data);
	const struct iio_chan_spec *channels;
	u8 num_channels;
};

enum hsc_func_id {
	HSC_FUNCTION_A,
	HSC_FUNCTION_B,
	HSC_FUNCTION_C,
	HSC_FUNCTION_F,
};

int hsc_common_probe(struct device *dev, hsc_recv_fn recv);

#endif
