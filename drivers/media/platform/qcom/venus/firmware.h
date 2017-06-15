/*
 * Copyright (C) 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __VENUS_FIRMWARE_H__
#define __VENUS_FIRMWARE_H__

struct device;

int venus_boot(struct device *parent, struct device *fw_dev,
	       const char *fwname);
int venus_shutdown(struct device *fw_dev);

#endif
