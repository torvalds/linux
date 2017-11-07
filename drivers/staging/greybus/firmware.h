// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus Firmware Management Header
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 */

#ifndef __FIRMWARE_H
#define __FIRMWARE_H

#include "greybus.h"

#define FW_NAME_PREFIX	"gmp_"

/*
 * Length of the string in format: "FW_NAME_PREFIX""%08x_%08x_%08x_%08x_%s.tftf"
 *                                  (3 + 1 + 4 * (8 + 1) + 10 + 1 + 4 + 1)
 */
#define FW_NAME_SIZE		56

/* Firmware Management Protocol specific functions */
int fw_mgmt_init(void);
void fw_mgmt_exit(void);
struct gb_connection *to_fw_mgmt_connection(struct device *dev);
int gb_fw_mgmt_request_handler(struct gb_operation *op);
int gb_fw_mgmt_connection_init(struct gb_connection *connection);
void gb_fw_mgmt_connection_exit(struct gb_connection *connection);

/* Firmware Download Protocol specific functions */
int gb_fw_download_request_handler(struct gb_operation *op);
int gb_fw_download_connection_init(struct gb_connection *connection);
void gb_fw_download_connection_exit(struct gb_connection *connection);

/* CAP Protocol specific functions */
int cap_init(void);
void cap_exit(void);
int gb_cap_connection_init(struct gb_connection *connection);
void gb_cap_connection_exit(struct gb_connection *connection);

#endif /* __FIRMWARE_H */
