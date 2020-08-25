/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#ifndef INV_ICM42600_BUFFER_H_
#define INV_ICM42600_BUFFER_H_

#include <linux/kernel.h>
#include <linux/bits.h>

struct inv_icm42600_state;

#define INV_ICM42600_SENSOR_GYRO	BIT(0)
#define INV_ICM42600_SENSOR_ACCEL	BIT(1)
#define INV_ICM42600_SENSOR_TEMP	BIT(2)

/**
 * struct inv_icm42600_fifo - FIFO state variables
 * @on:		reference counter for FIFO on.
 * @en:		bits field of INV_ICM42600_SENSOR_* for FIFO EN bits.
 * @period:	FIFO internal period.
 * @watermark:	watermark configuration values for accel and gyro.
 * @count:	number of bytes in the FIFO data buffer.
 * @nb:		gyro, accel and total samples in the FIFO data buffer.
 * @data:	FIFO data buffer aligned for DMA (2kB + 32 bytes of read cache).
 */
struct inv_icm42600_fifo {
	unsigned int on;
	unsigned int en;
	uint32_t period;
	struct {
		unsigned int gyro;
		unsigned int accel;
	} watermark;
	size_t count;
	struct {
		size_t gyro;
		size_t accel;
		size_t total;
	} nb;
	uint8_t data[2080] ____cacheline_aligned;
};

/* FIFO data packet */
struct inv_icm42600_fifo_sensor_data {
	__be16 x;
	__be16 y;
	__be16 z;
} __packed;
#define INV_ICM42600_FIFO_DATA_INVALID		-32768

static inline int16_t inv_icm42600_fifo_get_sensor_data(__be16 d)
{
	return be16_to_cpu(d);
}

static inline bool
inv_icm42600_fifo_is_data_valid(const struct inv_icm42600_fifo_sensor_data *s)
{
	int16_t x, y, z;

	x = inv_icm42600_fifo_get_sensor_data(s->x);
	y = inv_icm42600_fifo_get_sensor_data(s->y);
	z = inv_icm42600_fifo_get_sensor_data(s->z);

	if (x == INV_ICM42600_FIFO_DATA_INVALID &&
	    y == INV_ICM42600_FIFO_DATA_INVALID &&
	    z == INV_ICM42600_FIFO_DATA_INVALID)
		return false;

	return true;
}

ssize_t inv_icm42600_fifo_decode_packet(const void *packet, const void **accel,
					const void **gyro, const int8_t **temp,
					const void **timestamp, unsigned int *odr);

extern const struct iio_buffer_setup_ops inv_icm42600_buffer_ops;

int inv_icm42600_buffer_init(struct inv_icm42600_state *st);

void inv_icm42600_buffer_update_fifo_period(struct inv_icm42600_state *st);

int inv_icm42600_buffer_set_fifo_en(struct inv_icm42600_state *st,
				    unsigned int fifo_en);

int inv_icm42600_buffer_update_watermark(struct inv_icm42600_state *st);

int inv_icm42600_buffer_fifo_read(struct inv_icm42600_state *st,
				  unsigned int max);

int inv_icm42600_buffer_fifo_parse(struct inv_icm42600_state *st);

int inv_icm42600_buffer_hwfifo_flush(struct inv_icm42600_state *st,
				     unsigned int count);

#endif
