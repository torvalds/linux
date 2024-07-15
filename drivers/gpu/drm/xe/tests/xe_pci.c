// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "tests/xe_pci_test.h"

#include "tests/xe_test.h"

#include <kunit/test-bug.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <kunit/visibility.h>

struct kunit_test_data {
	int ndevs;
	xe_device_fn xe_fn;
};

static int dev_to_xe_device_fn(struct device *dev, void *__data)

{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct kunit_test_data *data = __data;
	int ret = 0;
	int idx;

	data->ndevs++;

	if (drm_dev_enter(drm, &idx))
		ret = data->xe_fn(to_xe_device(dev_get_drvdata(dev)));
	drm_dev_exit(idx);

	return ret;
}

/**
 * xe_call_for_each_device - Iterate over all devices this driver binds to
 * @xe_fn: Function to call for each device.
 *
 * This function iterated over all devices this driver binds to, and calls
 * @xe_fn: for each one of them. If the called function returns anything else
 * than 0, iteration is stopped and the return value is returned by this
 * function. Across each function call, drm_dev_enter() / drm_dev_exit() is
 * called for the corresponding drm device.
 *
 * Return: Number of devices iterated or
 *         the error code of a call to @xe_fn returning an error code.
 */
int xe_call_for_each_device(xe_device_fn xe_fn)
{
	int ret;
	struct kunit_test_data data = {
	    .xe_fn = xe_fn,
	    .ndevs = 0,
	};

	ret = driver_for_each_device(&xe_pci_driver.driver, NULL,
				     &data, dev_to_xe_device_fn);

	if (!data.ndevs)
		kunit_skip(current->kunit_test, "test runs only on hardware\n");

	return ret ?: data.ndevs;
}

/**
 * xe_call_for_each_graphics_ip - Iterate over all recognized graphics IPs
 * @xe_fn: Function to call for each device.
 *
 * This function iterates over the descriptors for all graphics IPs recognized
 * by the driver and calls @xe_fn: for each one of them.
 */
void xe_call_for_each_graphics_ip(xe_graphics_fn xe_fn)
{
	const struct xe_graphics_desc *ip, *last = NULL;

	for (int i = 0; i < ARRAY_SIZE(graphics_ip_map); i++) {
		ip = graphics_ip_map[i].ip;
		if (ip == last)
			continue;

		xe_fn(ip);
		last = ip;
	}
}
EXPORT_SYMBOL_IF_KUNIT(xe_call_for_each_graphics_ip);

/**
 * xe_call_for_each_media_ip - Iterate over all recognized media IPs
 * @xe_fn: Function to call for each device.
 *
 * This function iterates over the descriptors for all media IPs recognized
 * by the driver and calls @xe_fn: for each one of them.
 */
void xe_call_for_each_media_ip(xe_media_fn xe_fn)
{
	const struct xe_media_desc *ip, *last = NULL;

	for (int i = 0; i < ARRAY_SIZE(media_ip_map); i++) {
		ip = media_ip_map[i].ip;
		if (ip == last)
			continue;

		xe_fn(ip);
		last = ip;
	}
}
EXPORT_SYMBOL_IF_KUNIT(xe_call_for_each_media_ip);

static void fake_read_gmdid(struct xe_device *xe, enum xe_gmdid_type type,
			    u32 *ver, u32 *revid)
{
	struct kunit *test = kunit_get_current_test();
	struct xe_pci_fake_data *data = test->priv;

	if (type == GMDID_MEDIA) {
		*ver = data->media_verx100;
		*revid = xe_step_to_gmdid(data->media_step);
	} else {
		*ver = data->graphics_verx100;
		*revid = xe_step_to_gmdid(data->graphics_step);
	}
}

int xe_pci_fake_device_init(struct xe_device *xe)
{
	struct kunit *test = kunit_get_current_test();
	struct xe_pci_fake_data *data = test->priv;
	const struct pci_device_id *ent = pciidlist;
	const struct xe_device_desc *desc;
	const struct xe_subplatform_desc *subplatform_desc;

	if (!data) {
		desc = (const void *)ent->driver_data;
		subplatform_desc = NULL;
		goto done;
	}

	for (ent = pciidlist; ent->device; ent++) {
		desc = (const void *)ent->driver_data;
		if (desc->platform == data->platform)
			break;
	}

	if (!ent->device)
		return -ENODEV;

	for (subplatform_desc = desc->subplatforms;
	     subplatform_desc && subplatform_desc->subplatform;
	     subplatform_desc++)
		if (subplatform_desc->subplatform == data->subplatform)
			break;

	if (data->subplatform != XE_SUBPLATFORM_NONE && !subplatform_desc)
		return -ENODEV;

done:
	xe->sriov.__mode = data && data->sriov_mode ?
			   data->sriov_mode : XE_SRIOV_MODE_NONE;

	kunit_activate_static_stub(test, read_gmdid, fake_read_gmdid);

	xe_info_init_early(xe, desc, subplatform_desc);
	xe_info_init(xe, desc->graphics, desc->media);

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_fake_device_init);
