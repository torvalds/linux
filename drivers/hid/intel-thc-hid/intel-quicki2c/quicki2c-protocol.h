/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKI2C_PROTOCOL_H_
#define _QUICKI2C_PROTOCOL_H_

#include <linux/hid-over-i2c.h>

struct quicki2c_device;

int quicki2c_set_power(struct quicki2c_device *qcdev, enum hidi2c_power_state power_state);
int quicki2c_get_report(struct quicki2c_device *qcdev, u8 report_type,
			unsigned int reportnum, void *buf, u32 buf_len);
int quicki2c_set_report(struct quicki2c_device *qcdev, u8 report_type,
			unsigned int reportnum, void *buf, u32 buf_len);
int quicki2c_get_device_descriptor(struct quicki2c_device *qcdev);
int quicki2c_get_report_descriptor(struct quicki2c_device *qcdev);
int quicki2c_reset(struct quicki2c_device *qcdev);

#endif /* _QUICKI2C_PROTOCOL_H_ */
