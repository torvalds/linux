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

static void xe_ip_kunit_desc(const struct xe_ip *param, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%u.%02u %s",
		 param->verx100 / 100, param->verx100 % 100, param->name);
}

KUNIT_ARRAY_PARAM(graphics_ip, graphics_ips, xe_ip_kunit_desc);
KUNIT_ARRAY_PARAM(media_ip, media_ips, xe_ip_kunit_desc);

static void xe_pci_id_kunit_desc(const struct pci_device_id *param, char *desc)
{
	const struct xe_device_desc *dev_desc =
		(const struct xe_device_desc *)param->driver_data;

	if (dev_desc)
		snprintf(desc, KUNIT_PARAM_DESC_SIZE, "0x%X (%s)",
			 param->device, dev_desc->platform_name);
}

KUNIT_ARRAY_PARAM(pci_id, pciidlist, xe_pci_id_kunit_desc);

/**
 * xe_pci_graphics_ip_gen_param - Generate graphics struct xe_ip parameters
 * @prev: the pointer to the previous parameter to iterate from or NULL
 * @desc: output buffer with minimum size of KUNIT_PARAM_DESC_SIZE
 *
 * This function prepares struct xe_ip parameter.
 *
 * To be used only as a parameter generator function in &KUNIT_CASE_PARAM.
 *
 * Return: pointer to the next parameter or NULL if no more parameters
 */
const void *xe_pci_graphics_ip_gen_param(const void *prev, char *desc)
{
	return graphics_ip_gen_params(prev, desc);
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_graphics_ip_gen_param);

/**
 * xe_pci_media_ip_gen_param - Generate media struct xe_ip parameters
 * @prev: the pointer to the previous parameter to iterate from or NULL
 * @desc: output buffer with minimum size of KUNIT_PARAM_DESC_SIZE
 *
 * This function prepares struct xe_ip parameter.
 *
 * To be used only as a parameter generator function in &KUNIT_CASE_PARAM.
 *
 * Return: pointer to the next parameter or NULL if no more parameters
 */
const void *xe_pci_media_ip_gen_param(const void *prev, char *desc)
{
	return media_ip_gen_params(prev, desc);
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_media_ip_gen_param);

/**
 * xe_pci_id_gen_param - Generate struct pci_device_id parameters
 * @prev: the pointer to the previous parameter to iterate from or NULL
 * @desc: output buffer with minimum size of KUNIT_PARAM_DESC_SIZE
 *
 * This function prepares struct pci_device_id parameter.
 *
 * To be used only as a parameter generator function in &KUNIT_CASE_PARAM.
 *
 * Return: pointer to the next parameter or NULL if no more parameters
 */
const void *xe_pci_id_gen_param(const void *prev, char *desc)
{
	const struct pci_device_id *pci = pci_id_gen_params(prev, desc);

	return pci->driver_data ? pci : NULL;
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_id_gen_param);

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
	xe_info_init(xe, desc);

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_fake_device_init);

/**
 * xe_pci_live_device_gen_param - Helper to iterate Xe devices as KUnit parameters
 * @prev: the previously returned value, or NULL for the first iteration
 * @desc: the buffer for a parameter name
 *
 * Iterates over the available Xe devices on the system. Uses the device name
 * as the parameter name.
 *
 * To be used only as a parameter generator function in &KUNIT_CASE_PARAM.
 *
 * Return: pointer to the next &struct xe_device ready to be used as a parameter
 *         or NULL if there are no more Xe devices on the system.
 */
const void *xe_pci_live_device_gen_param(const void *prev, char *desc)
{
	const struct xe_device *xe = prev;
	struct device *dev = xe ? xe->drm.dev : NULL;
	struct device *next;

	next = driver_find_next_device(&xe_pci_driver.driver, dev);
	if (dev)
		put_device(dev);
	if (!next)
		return NULL;

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s", dev_name(next));
	return pdev_to_xe_device(to_pci_dev(next));
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_live_device_gen_param);
