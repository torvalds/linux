// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface System Aggregator Module (SSAM) HID transport driver for the legacy
 * keyboard interface (KBD/TC=0x08 subsystem). Provides support for the
 * integrated HID keyboard on Surface Laptops 1 and 2.
 *
 * Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/unaligned.h>
#include <linux/hid.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/surface_aggregator/controller.h>

#include "surface_hid_core.h"


/* -- SAM interface (KBD). -------------------------------------------------- */

#define KBD_FEATURE_REPORT_SIZE			7  /* 6 + report ID */

enum surface_kbd_cid {
	SURFACE_KBD_CID_GET_DESCRIPTOR		= 0x00,
	SURFACE_KBD_CID_SET_CAPSLOCK_LED	= 0x01,
	SURFACE_KBD_CID_EVT_INPUT_GENERIC	= 0x03,
	SURFACE_KBD_CID_EVT_INPUT_HOTKEYS	= 0x04,
	SURFACE_KBD_CID_GET_FEATURE_REPORT	= 0x0b,
};

static int ssam_kbd_get_descriptor(struct surface_hid_device *shid, u8 entry, u8 *buf, size_t len)
{
	struct ssam_request rqst;
	struct ssam_response rsp;
	int status;

	rqst.target_category = shid->uid.category;
	rqst.target_id = shid->uid.target;
	rqst.command_id = SURFACE_KBD_CID_GET_DESCRIPTOR;
	rqst.instance_id = shid->uid.instance;
	rqst.flags = SSAM_REQUEST_HAS_RESPONSE;
	rqst.length = sizeof(entry);
	rqst.payload = &entry;

	rsp.capacity = len;
	rsp.length = 0;
	rsp.pointer = buf;

	status = ssam_retry(ssam_request_do_sync_onstack, shid->ctrl, &rqst, &rsp, sizeof(entry));
	if (status)
		return status;

	if (rsp.length != len) {
		dev_err(shid->dev, "invalid descriptor length: got %zu, expected, %zu\n",
			rsp.length, len);
		return -EPROTO;
	}

	return 0;
}

static int ssam_kbd_set_caps_led(struct surface_hid_device *shid, bool value)
{
	struct ssam_request rqst;
	u8 value_u8 = value;

	rqst.target_category = shid->uid.category;
	rqst.target_id = shid->uid.target;
	rqst.command_id = SURFACE_KBD_CID_SET_CAPSLOCK_LED;
	rqst.instance_id = shid->uid.instance;
	rqst.flags = 0;
	rqst.length = sizeof(value_u8);
	rqst.payload = &value_u8;

	return ssam_retry(ssam_request_do_sync_onstack, shid->ctrl, &rqst, NULL, sizeof(value_u8));
}

static int ssam_kbd_get_feature_report(struct surface_hid_device *shid, u8 *buf, size_t len)
{
	struct ssam_request rqst;
	struct ssam_response rsp;
	u8 payload = 0;
	int status;

	rqst.target_category = shid->uid.category;
	rqst.target_id = shid->uid.target;
	rqst.command_id = SURFACE_KBD_CID_GET_FEATURE_REPORT;
	rqst.instance_id = shid->uid.instance;
	rqst.flags = SSAM_REQUEST_HAS_RESPONSE;
	rqst.length = sizeof(payload);
	rqst.payload = &payload;

	rsp.capacity = len;
	rsp.length = 0;
	rsp.pointer = buf;

	status = ssam_retry(ssam_request_do_sync_onstack, shid->ctrl, &rqst, &rsp, sizeof(payload));
	if (status)
		return status;

	if (rsp.length != len) {
		dev_err(shid->dev, "invalid feature report length: got %zu, expected, %zu\n",
			rsp.length, len);
		return -EPROTO;
	}

	return 0;
}

static bool ssam_kbd_is_input_event(const struct ssam_event *event)
{
	if (event->command_id == SURFACE_KBD_CID_EVT_INPUT_GENERIC)
		return true;

	if (event->command_id == SURFACE_KBD_CID_EVT_INPUT_HOTKEYS)
		return true;

	return false;
}

static u32 ssam_kbd_event_fn(struct ssam_event_notifier *nf, const struct ssam_event *event)
{
	struct surface_hid_device *shid = container_of(nf, struct surface_hid_device, notif);

	/*
	 * Check against device UID manually, as registry and device target
	 * category doesn't line up.
	 */

	if (shid->uid.category != event->target_category)
		return 0;

	if (shid->uid.target != event->target_id)
		return 0;

	if (shid->uid.instance != event->instance_id)
		return 0;

	if (!ssam_kbd_is_input_event(event))
		return 0;

	hid_input_report(shid->hid, HID_INPUT_REPORT, (u8 *)&event->data[0], event->length, 0);
	return SSAM_NOTIF_HANDLED;
}


/* -- Transport driver (KBD). ----------------------------------------------- */

static int skbd_get_caps_led_value(struct hid_device *hid, u8 rprt_id, u8 *buf, size_t len)
{
	struct hid_field *field;
	unsigned int offset, size;
	int i;

	/* Get LED field. */
	field = hidinput_get_led_field(hid);
	if (!field)
		return -ENOENT;

	/* Check if we got the correct report. */
	if (len != hid_report_len(field->report))
		return -ENOENT;

	if (rprt_id != field->report->id)
		return -ENOENT;

	/* Get caps lock LED index. */
	for (i = 0; i < field->report_count; i++)
		if ((field->usage[i].hid & 0xffff) == 0x02)
			break;

	if (i == field->report_count)
		return -ENOENT;

	/* Extract value. */
	size = field->report_size;
	offset = field->report_offset + i * size;
	return !!hid_field_extract(hid, buf + 1, size, offset);
}

static int skbd_output_report(struct surface_hid_device *shid, u8 rprt_id, u8 *buf, size_t len)
{
	int caps_led;
	int status;

	caps_led = skbd_get_caps_led_value(shid->hid, rprt_id, buf, len);
	if (caps_led < 0)
		return -EIO;  /* Only caps LED output reports are supported. */

	status = ssam_kbd_set_caps_led(shid, caps_led);
	if (status < 0)
		return status;

	return len;
}

static int skbd_get_feature_report(struct surface_hid_device *shid, u8 rprt_id, u8 *buf, size_t len)
{
	u8 report[KBD_FEATURE_REPORT_SIZE];
	int status;

	/*
	 * The keyboard only has a single hard-coded read-only feature report
	 * of size KBD_FEATURE_REPORT_SIZE. Try to load it and compare its
	 * report ID against the requested one.
	 */

	if (len < ARRAY_SIZE(report))
		return -ENOSPC;

	status = ssam_kbd_get_feature_report(shid, report, ARRAY_SIZE(report));
	if (status < 0)
		return status;

	if (rprt_id != report[0])
		return -ENOENT;

	memcpy(buf, report, ARRAY_SIZE(report));
	return len;
}

static int skbd_set_feature_report(struct surface_hid_device *shid, u8 rprt_id, u8 *buf, size_t len)
{
	/* Not supported. See skbd_get_feature_report() for details. */
	return -EIO;
}


/* -- Driver setup. --------------------------------------------------------- */

static int surface_kbd_probe(struct platform_device *pdev)
{
	struct ssam_controller *ctrl;
	struct surface_hid_device *shid;

	/* Add device link to EC. */
	ctrl = ssam_client_bind(&pdev->dev);
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl) == -ENODEV ? -EPROBE_DEFER : PTR_ERR(ctrl);

	shid = devm_kzalloc(&pdev->dev, sizeof(*shid), GFP_KERNEL);
	if (!shid)
		return -ENOMEM;

	shid->dev = &pdev->dev;
	shid->ctrl = ctrl;

	shid->uid.domain = SSAM_DOMAIN_SERIALHUB;
	shid->uid.category = SSAM_SSH_TC_KBD;
	shid->uid.target = SSAM_SSH_TID_KIP;
	shid->uid.instance = 0;
	shid->uid.function = 0;

	shid->notif.base.priority = 1;
	shid->notif.base.fn = ssam_kbd_event_fn;
	shid->notif.event.reg = SSAM_EVENT_REGISTRY_SAM;
	shid->notif.event.id.target_category = shid->uid.category;
	shid->notif.event.id.instance = shid->uid.instance;
	shid->notif.event.mask = SSAM_EVENT_MASK_NONE;
	shid->notif.event.flags = 0;

	shid->ops.get_descriptor = ssam_kbd_get_descriptor;
	shid->ops.output_report = skbd_output_report;
	shid->ops.get_feature_report = skbd_get_feature_report;
	shid->ops.set_feature_report = skbd_set_feature_report;

	platform_set_drvdata(pdev, shid);
	return surface_hid_device_add(shid);
}

static void surface_kbd_remove(struct platform_device *pdev)
{
	surface_hid_device_destroy(platform_get_drvdata(pdev));
}

static const struct acpi_device_id surface_kbd_match[] = {
	{ "MSHW0096" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, surface_kbd_match);

static struct platform_driver surface_kbd_driver = {
	.probe = surface_kbd_probe,
	.remove = surface_kbd_remove,
	.driver = {
		.name = "surface_keyboard",
		.acpi_match_table = surface_kbd_match,
		.pm = &surface_hid_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(surface_kbd_driver);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("HID legacy transport driver for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
