/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKSPI_HID_H_
#define _QUICKSPI_HID_H_

struct quickspi_device;

int quickspi_hid_send_report(struct quickspi_device *qsdev,
			     void *data, size_t data_size);
int quickspi_hid_probe(struct quickspi_device *qsdev);
void quickspi_hid_remove(struct quickspi_device *qsdev);

#endif /* _QUICKSPI_HID_H_ */
