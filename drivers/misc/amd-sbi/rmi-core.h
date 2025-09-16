/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _SBRMI_CORE_H_
#define _SBRMI_CORE_H_

#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <uapi/misc/amd-apml.h>

/* SB-RMI registers */
enum sbrmi_reg {
	SBRMI_REV,
	SBRMI_CTRL,
	SBRMI_STATUS,
	SBRMI_OUTBNDMSG0	= 0x30,
	SBRMI_OUTBNDMSG1,
	SBRMI_OUTBNDMSG2,
	SBRMI_OUTBNDMSG3,
	SBRMI_OUTBNDMSG4,
	SBRMI_OUTBNDMSG5,
	SBRMI_OUTBNDMSG6,
	SBRMI_OUTBNDMSG7,
	SBRMI_INBNDMSG0,
	SBRMI_INBNDMSG1,
	SBRMI_INBNDMSG2,
	SBRMI_INBNDMSG3,
	SBRMI_INBNDMSG4,
	SBRMI_INBNDMSG5,
	SBRMI_INBNDMSG6,
	SBRMI_INBNDMSG7,
	SBRMI_SW_INTERRUPT,
	SBRMI_THREAD128CS	= 0x4b,
};

/*
 * SB-RMI supports soft mailbox service request to MP1 (power management
 * firmware) through SBRMI inbound/outbound message registers.
 * SB-RMI message IDs
 */
enum sbrmi_msg_id {
	SBRMI_READ_PKG_PWR_CONSUMPTION = 0x1,
	SBRMI_WRITE_PKG_PWR_LIMIT,
	SBRMI_READ_PKG_PWR_LIMIT,
	SBRMI_READ_PKG_MAX_PWR_LIMIT,
};

/* Each client has this additional data */
struct sbrmi_data {
	struct miscdevice sbrmi_misc_dev;
	struct regmap *regmap;
	/* Mutex locking */
	struct mutex lock;
	u32 pwr_limit_max;
	u8 dev_static_addr;
	u8 rev;
};

int rmi_mailbox_xfer(struct sbrmi_data *data, struct apml_mbox_msg *msg);
#ifdef CONFIG_AMD_SBRMI_HWMON
int create_hwmon_sensor_device(struct device *dev, struct sbrmi_data *data);
#else
static inline int create_hwmon_sensor_device(struct device *dev, struct sbrmi_data *data)
{
	return 0;
}
#endif
int create_misc_rmi_device(struct sbrmi_data *data, struct device *dev);
#endif /*_SBRMI_CORE_H_*/
