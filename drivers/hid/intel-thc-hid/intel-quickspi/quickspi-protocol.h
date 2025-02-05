/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKSPI_PROTOCOL_H_
#define _QUICKSPI_PROTOCOL_H_

#include <linux/hid-over-spi.h>

#define QUICKSPI_ACK_WAIT_TIMEOUT    5

struct quickspi_device;

void quickspi_handle_input_data(struct quickspi_device *qsdev, u32 buf_len);
int quickspi_get_report(struct quickspi_device *qsdev, u8 report_type,
			unsigned int report_id, void *buf);
int quickspi_set_report(struct quickspi_device *qsdev, u8 report_type,
			unsigned int report_id, void *buf, u32 buf_len);
int quickspi_get_report_descriptor(struct quickspi_device *qsdev);

int quickspi_set_power(struct quickspi_device *qsdev,
		       enum hidspi_power_state power_state);

int reset_tic(struct quickspi_device *qsdev);

#endif /* _QUICKSPI_PROTOCOL_H_ */
