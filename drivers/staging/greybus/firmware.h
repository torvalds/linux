/*
 * Greybus Firmware Management Header
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __FIRMWARE_H
#define __FIRMWARE_H

#include "greybus.h"

/* Firmware Download Protocol specific functions */
int gb_fw_download_request_handler(struct gb_operation *op);
int gb_fw_download_connection_init(struct gb_connection *connection);
void gb_fw_download_connection_exit(struct gb_connection *connection);

#endif /* __FIRMWARE_H */
