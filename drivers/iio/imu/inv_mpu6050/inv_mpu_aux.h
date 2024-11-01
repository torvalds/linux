/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 TDK-InvenSense, Inc.
 */

#ifndef INV_MPU_AUX_H_
#define INV_MPU_AUX_H_

#include "inv_mpu_iio.h"

int inv_mpu_aux_init(const struct inv_mpu6050_state *st);

int inv_mpu_aux_read(const struct inv_mpu6050_state *st, uint8_t addr,
		     uint8_t reg, uint8_t *val, size_t size);

int inv_mpu_aux_write(const struct inv_mpu6050_state *st, uint8_t addr,
		      uint8_t reg, uint8_t val);

#endif		/* INV_MPU_AUX_H_ */
