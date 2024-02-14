// SPDX-License-Identifier: GPL-2.0+
/*
 * Common/core components for the Surface System Aggregator Module (SSAM) HID
 * transport driver. Provides support for integrated HID devices on Microsoft
 * Surface models.
 *
 * Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/hid.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>

#include <linux/surface_aggregator/controller.h>

#include "surface_hid_core.h"


/* -- Utility functions. ---------------------------------------------------- */

static bool surface_hid_is_hot_removed(struct surface_hid_device *shid)
{
	/*
	 * Non-ssam client devices, i.e. platform client devices, cannot be
	 * hot-removed.
	 */
	if (!is_ssam_device(shid->dev))
		return false;

	return ssam_device_is_hot_removed(to_ssam_device(shid->dev));
}


/* -- Device descriptor access. --------------------------------------------- */

static int surface_hid_load_hid_descriptor(struct surface_hid_device *shid)
{
	int status;

	if (surface_hid_is_hot_removed(shid))
		return -ENODEV;

	status = shid->ops.get_descriptor(shid, SURFACE_HID_DESC_HID,
			(u8 *)&shid->hid_desc, sizeof(shid->hid_desc));
	if (status)
		return status;

	if (shid->hid_desc.desc_len != sizeof(shid->hid_desc)) {
		dev_err(shid->dev, "unexpected HID descriptor length: got %u, expected %zu\n",
			shid->hid_desc.desc_len, sizeof(shid->hid_desc));
		return -EPROTO;
	}

	if (shid->hid_desc.desc_type != HID_DT_HID) {
		dev_err(shid->dev, "unexpected HID descriptor type: got %#04x, expected %#04x\n",
			shid->hid_desc.desc_type, HID_DT_HID);
		return -EPROTO;
	}

	if (shid->hid_desc.num_descriptors != 1) {
		dev_err(shid->dev, "unexpected number of descriptors: got %u, expected 1\n",
			shid->hid_desc.num_descriptors);
		return -EPROTO;
	}

	if (shid->hid_desc.report_desc_type != HID_DT_REPORT) {
		dev_err(shid->dev, "unexpected report descriptor type: got %#04x, expected %#04x\n",
			shid->hid_desc.report_desc_type, HID_DT_REPORT);
		return -EPROTO;
	}

	return 0;
}

static int surface_hid_load_device_attributes(struct surface_hid_device *shid)
{
	int status;

	if (surface_hid_is_hot_removed(shid))
		return -ENODEV;

	status = shid->ops.get_descriptor(shid, SURFACE_HID_DESC_ATTRS,
			(u8 *)&shid->attrs, sizeof(shid->attrs));
	if (status)
		return status;

	if (get_unaligned_le32(&shid->attrs.length) != sizeof(shid->attrs)) {
		dev_err(shid->dev, "unexpected attribute length: got %u, expected %zu\n",
			get_unaligned_le32(&shid->attrs.length), sizeof(shid->attrs));
		return -EPROTO;
	}

	return 0;
}


/* -- Transport driver (common). -------------------------------------------- */

static int surface_hid_start(struct hid_device *hid)
{
	struct surface_hid_device *shid = hid->driver_data;

	return ssam_notifier_register(shid->ctrl, &shid->notif);
}

static void surface_hid_stop(struct hid_device *hid)
{
	struct surface_hid_device *shid = hid->driver_data;
	bool hot_removed;

	/*
	 * Communication may fail for devices that have been hot-removed. This
	 * also includes unregistration of HID events, so we need to check this
	 * here. Only if the device has not been marked as hot-removed, we can
	 * safely disable events.
	 */
	hot_removed = surface_hid_is_hot_removed(shid);

	/* Note: This call will log errors for us, so ignore them here. */
	__ssam_notifier_unregister(shid->ctrl, &shid->notif, !hot_removed);
}

static int surface_hid_open(struct hid_device *hid)
{
	return 0;
}

static void surface_hid_close(struct hid_device *hid)
{
}

static int surface_hid_parse(struct hid_device *hid)
{
	struct surface_hid_device *shid = hid->driver_data;
	size_t len = get_unaligned_le16(&shid->hid_desc.report_desc_len);
	u8 *buf;
	int status;

	if (surface_hid_is_hot_removed(shid))
		return -ENODEV;

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	status = shid->ops.get_descriptor(shid, SURFACE_HID_DESC_REPORT, buf, len);
	if (!status)
		status = hid_parse_report(hid, buf, len);

	kfree(buf);
	return status;
}

static int surface_hid_raw_request(struct hid_device *hid, unsigned char reportnum, u8 *buf,
				   size_t len, unsigned char rtype, int reqtype)
{
	struct surface_hid_device *shid = hid->driver_data;

	if (surface_hid_is_hot_removed(shid))
		return -ENODEV;

	if (rtype == HID_OUTPUT_REPORT && reqtype == HID_REQ_SET_REPORT)
		return shid->ops.output_report(shid, reportnum, buf, len);

	else if (rtype == HID_FEATURE_REPORT && reqtype == HID_REQ_GET_REPORT)
		return shid->ops.get_feature_report(shid, reportnum, buf, len);

	else if (rtype == HID_FEATURE_REPORT && reqtype == HID_REQ_SET_REPORT)
		return shid->ops.set_feature_report(shid, reportnum, buf, len);

	return -EIO;
}

static struct hid_ll_driver surface_hid_ll_driver = {
	.start       = surface_hid_start,
	.stop        = surface_hid_stop,
	.open        = surface_hid_open,
	.close       = surface_hid_close,
	.parse       = surface_hid_parse,
	.raw_request = surface_hid_raw_request,
};


/* -- Common device setup. -------------------------------------------------- */

int surface_hid_device_add(struct surface_hid_device *shid)
{
	int status;

	status = surface_hid_load_hid_descriptor(shid);
	if (status)
		return status;

	status = surface_hid_load_device_attributes(shid);
	if (status)
		return status;

	shid->hid = hid_allocate_device();
	if (IS_ERR(shid->hid))
		return PTR_ERR(shid->hid);

	shid->hid->dev.parent = shid->dev;
	shid->hid->bus = BUS_HOST;
	shid->hid->vendor = get_unaligned_le16(&shid->attrs.vendor);
	shid->hid->product = get_unaligned_le16(&shid->attrs.product);
	shid->hid->version = get_unaligned_le16(&shid->hid_desc.hid_version);
	shid->hid->country = shid->hid_desc.country_code;

	snprintf(shid->hid->name, sizeof(shid->hid->name), "Microsoft Surface %04X:%04X",
		 shid->hid->vendor, shid->hid->product);

	strscpy(shid->hid->phys, dev_name(shid->dev), sizeof(shid->hid->phys));

	shid->hid->driver_data = shid;
	shid->hid->ll_driver = &surface_hid_ll_driver;

	status = hid_add_device(shid->hid);
	if (status)
		hid_destroy_device(shid->hid);

	return status;
}
EXPORT_SYMBOL_GPL(surface_hid_device_add);

void surface_hid_device_destroy(struct surface_hid_device *shid)
{
	hid_destroy_device(shid->hid);
}
EXPORT_SYMBOL_GPL(surface_hid_device_destroy);


/* -- PM ops. --------------------------------------------------------------- */

#ifdef CONFIG_PM_SLEEP

static int surface_hid_suspend(struct device *dev)
{
	struct surface_hid_device *d = dev_get_drvdata(dev);

	return hid_driver_suspend(d->hid, PMSG_SUSPEND);
}

static int surface_hid_resume(struct device *dev)
{
	struct surface_hid_device *d = dev_get_drvdata(dev);

	return hid_driver_resume(d->hid);
}

static int surface_hid_freeze(struct device *dev)
{
	struct surface_hid_device *d = dev_get_drvdata(dev);

	return hid_driver_suspend(d->hid, PMSG_FREEZE);
}

static int surface_hid_poweroff(struct device *dev)
{
	struct surface_hid_device *d = dev_get_drvdata(dev);

	return hid_driver_suspend(d->hid, PMSG_HIBERNATE);
}

static int surface_hid_restore(struct device *dev)
{
	struct surface_hid_device *d = dev_get_drvdata(dev);

	return hid_driver_reset_resume(d->hid);
}

const struct dev_pm_ops surface_hid_pm_ops = {
	.freeze   = surface_hid_freeze,
	.thaw     = surface_hid_resume,
	.suspend  = surface_hid_suspend,
	.resume   = surface_hid_resume,
	.poweroff = surface_hid_poweroff,
	.restore  = surface_hid_restore,
};
EXPORT_SYMBOL_GPL(surface_hid_pm_ops);

#else /* CONFIG_PM_SLEEP */

const struct dev_pm_ops surface_hid_pm_ops = { };
EXPORT_SYMBOL_GPL(surface_hid_pm_ops);

#endif /* CONFIG_PM_SLEEP */

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("HID transport driver core for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
