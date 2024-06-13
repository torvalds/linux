/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 */

#ifndef _HNS_DSAF_MISC_H
#define _HNS_DSAF_MISC_H

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "hns_dsaf_mac.h"

#define CPLD_ADDR_PORT_OFFSET	0x4

#define HS_LED_ON		0xE
#define HS_LED_OFF		0xF

#define CPLD_LED_ON_VALUE	1
#define CPLD_LED_DEFAULT_VALUE	0

#define MAC_SFP_PORT_OFFSET	0x2

#define DSAF_LED_SPEED_S 0
#define DSAF_LED_SPEED_M (0x3 << DSAF_LED_SPEED_S)

#define DSAF_LED_LINK_B 2
#define DSAF_LED_DATA_B 4
#define DSAF_LED_ANCHOR_B 5

struct dsaf_misc_op *hns_misc_op_get(struct dsaf_device *dsaf_dev);
struct
platform_device *hns_dsaf_find_platform_device(struct fwnode_handle *fwnode);
#endif
