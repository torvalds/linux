/*
 * Copyright (c) 2016 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#ifndef __HID_RKVR_H
#define __HID_RKVR_H

#define HIDRKVRHANDSHAKE(len)	_IOC(_IOC_WRITE, 'H', 0x1A, len)

#define HID_REPORT_ID_RKVR	3
#define RKVR_ID_IDLE	1
#define RKVR_ID_SYNC	2
#define RKVR_ID_CAPS	3

#define HID_REPORT_ID_R	4
#define HID_REPORT_ID_W	5
#define HID_REPORT_ID_CRYP	6

#define HID_REGR_REPORT	(4 - 1)
#define HID_REGW_REPORT	(5 - 1)
#define HID_MISC_REPORT	(6 - 1)

#define HID_SYNCW_REPORT	(7 - 1)
#define HID_SYNCR_REPORT	(8 - 1)

enum tracker_message_type {
	TrackerMessage_None              = 0,
	TrackerMessage_Sensors           = 1,
	TrackerMessage_Unknown           = 0x100,
	TrackerMessage_SizeError         = 0x101,
};

#define DEBUG_SYS 1

#define DYNAMIC_LOAD_MPU6500 0

int rkvr_sensor_register_callback(int (*callback)(char *, size_t, void *), void *priv);

struct rkvr_iio_hw_device {
	struct device *dev;
	const char *name;
	int is_open;
	struct list_head l;
	int (*open)(struct rkvr_iio_hw_device *hdev);
	void (*close)(struct rkvr_iio_hw_device *hdev);
	int (*power)(struct rkvr_iio_hw_device *hdev, int level);
	int (*idle)(struct rkvr_iio_hw_device *hdev, int report, int idle, int reqtype);
	int (*read)(struct rkvr_iio_hw_device *hdev, int reg, unsigned char *data, int len);
	int (*write)(struct rkvr_iio_hw_device *hdev, int reg, unsigned char data);
};

#endif
