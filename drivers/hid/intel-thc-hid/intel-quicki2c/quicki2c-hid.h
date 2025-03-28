/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKI2C_HID_H_
#define _QUICKI2C_HID_H_

struct quicki2c_device;

int quicki2c_hid_send_report(struct quicki2c_device *qcdev,
			     void *data, size_t data_size);
int quicki2c_hid_probe(struct quicki2c_device *qcdev);
void quicki2c_hid_remove(struct quicki2c_device *qcdev);

#endif /* _QUICKI2C_HID_H_ */
