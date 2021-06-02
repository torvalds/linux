/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Common/core components for the Surface System Aggregator Module (SSAM) HID
 * transport driver. Provides support for integrated HID devices on Microsoft
 * Surface models.
 *
 * Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef SURFACE_HID_CORE_H
#define SURFACE_HID_CORE_H

#include <linux/hid.h>
#include <linux/pm.h>
#include <linux/types.h>

#include <linux/surface_aggregator/controller.h>
#include <linux/surface_aggregator/device.h>

enum surface_hid_descriptor_entry {
	SURFACE_HID_DESC_HID    = 0,
	SURFACE_HID_DESC_REPORT = 1,
	SURFACE_HID_DESC_ATTRS  = 2,
};

struct surface_hid_descriptor {
	__u8 desc_len;			/* = 9 */
	__u8 desc_type;			/* = HID_DT_HID */
	__le16 hid_version;
	__u8 country_code;
	__u8 num_descriptors;		/* = 1 */

	__u8 report_desc_type;		/* = HID_DT_REPORT */
	__le16 report_desc_len;
} __packed;

static_assert(sizeof(struct surface_hid_descriptor) == 9);

struct surface_hid_attributes {
	__le32 length;
	__le16 vendor;
	__le16 product;
	__le16 version;
	__u8 _unknown[22];
} __packed;

static_assert(sizeof(struct surface_hid_attributes) == 32);

struct surface_hid_device;

struct surface_hid_device_ops {
	int (*get_descriptor)(struct surface_hid_device *shid, u8 entry, u8 *buf, size_t len);
	int (*output_report)(struct surface_hid_device *shid, u8 rprt_id, u8 *buf, size_t len);
	int (*get_feature_report)(struct surface_hid_device *shid, u8 rprt_id, u8 *buf, size_t len);
	int (*set_feature_report)(struct surface_hid_device *shid, u8 rprt_id, u8 *buf, size_t len);
};

struct surface_hid_device {
	struct device *dev;
	struct ssam_controller *ctrl;
	struct ssam_device_uid uid;

	struct surface_hid_descriptor hid_desc;
	struct surface_hid_attributes attrs;

	struct ssam_event_notifier notif;
	struct hid_device *hid;

	struct surface_hid_device_ops ops;
};

int surface_hid_device_add(struct surface_hid_device *shid);
void surface_hid_device_destroy(struct surface_hid_device *shid);

extern const struct dev_pm_ops surface_hid_pm_ops;

#endif /* SURFACE_HID_CORE_H */
