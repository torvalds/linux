/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2025 Invensense, Inc. */

#ifndef INV_ICM45600_BUFFER_H_
#define INV_ICM45600_BUFFER_H_

#include <linux/bits.h>
#include <linux/limits.h>
#include <linux/types.h>

#include <asm/byteorder.h>

#include <linux/iio/iio.h>

struct inv_icm45600_state;

#define INV_ICM45600_SENSOR_GYRO	BIT(0)
#define INV_ICM45600_SENSOR_ACCEL	BIT(1)
#define INV_ICM45600_SENSOR_TEMP	BIT(2)

/**
 * struct inv_icm45600_fifo - FIFO state variables
 * @on:		reference counter for FIFO on.
 * @en:		bits field of INV_ICM45600_SENSOR_* for FIFO EN bits.
 * @period:	FIFO internal period.
 * @watermark:	watermark configuration values for accel and gyro.
 * @watermark.gyro:	 requested watermark for gyro.
 * @watermark.accel:	 requested watermark for accel.
 * @watermark.eff_gyro:	 effective watermark for gyro.
 * @watermark.eff_accel: effective watermark for accel.
 * @count:	number of bytes in the FIFO data buffer.
 * @nb:		gyro, accel and total samples in the FIFO data buffer.
 * @data:	FIFO data buffer aligned for DMA (8kB)
 */
struct inv_icm45600_fifo {
	unsigned int on;
	unsigned int en;
	u32 period;
	struct {
		unsigned int gyro;
		unsigned int accel;
		unsigned int eff_gyro;
		unsigned int eff_accel;
	} watermark;
	size_t count;
	struct {
		size_t gyro;
		size_t accel;
		size_t total;
	} nb;
	u8 *data;
};

/* FIFO data packet */
struct inv_icm45600_fifo_sensor_data {
	__le16 x;
	__le16 y;
	__le16 z;
} __packed;
#define INV_ICM45600_DATA_INVALID		S16_MIN

static inline bool
inv_icm45600_fifo_is_data_valid(const struct inv_icm45600_fifo_sensor_data *s)
{
	s16 x, y, z;

	x = le16_to_cpu(s->x);
	y = le16_to_cpu(s->y);
	z = le16_to_cpu(s->z);

	return (x != INV_ICM45600_DATA_INVALID ||
		y != INV_ICM45600_DATA_INVALID ||
		z != INV_ICM45600_DATA_INVALID);
}

ssize_t inv_icm45600_fifo_decode_packet(const void *packet,
					const struct inv_icm45600_fifo_sensor_data **accel,
					const struct inv_icm45600_fifo_sensor_data **gyro,
					const s8 **temp,
					const __le16 **timestamp, unsigned int *odr);

extern const struct iio_buffer_setup_ops inv_icm45600_buffer_ops;

int inv_icm45600_buffer_init(struct inv_icm45600_state *st);

void inv_icm45600_buffer_update_fifo_period(struct inv_icm45600_state *st);

int inv_icm45600_buffer_set_fifo_en(struct inv_icm45600_state *st,
				    unsigned int fifo_en);

int inv_icm45600_buffer_update_watermark(struct inv_icm45600_state *st);

int inv_icm45600_buffer_fifo_read(struct inv_icm45600_state *st,
				  unsigned int max);

int inv_icm45600_buffer_fifo_parse(struct inv_icm45600_state *st);

int inv_icm45600_buffer_hwfifo_flush(struct inv_icm45600_state *st,
				     unsigned int count);

#endif
