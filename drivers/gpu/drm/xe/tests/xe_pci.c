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

#define PLATFORM_CASE(platform__, graphics_step__)					\
	{										\
		.platform = XE_ ## platform__,						\
		.subplatform = XE_SUBPLATFORM_NONE,					\
		.step = { .graphics = STEP_ ## graphics_step__ }			\
	}

#define SUBPLATFORM_CASE(platform__, subplatform__, graphics_step__)			\
	{										\
		.platform = XE_ ## platform__,						\
		.subplatform = XE_SUBPLATFORM_ ## platform__ ## _ ## subplatform__,	\
		.step = { .graphics = STEP_ ## graphics_step__ }			\
	}

#define GMDID_CASE(platform__, graphics_verx100__, graphics_step__,			\
		   media_verx100__, media_step__)					\
	{										\
		.platform = XE_ ## platform__,						\
		.subplatform = XE_SUBPLATFORM_NONE,					\
		.graphics_verx100 = graphics_verx100__,					\
		.media_verx100 = media_verx100__,					\
		.step = { .graphics = STEP_ ## graphics_step__,				\
			   .media = STEP_ ## media_step__ }				\
	}

static const struct xe_pci_fake_data cases[] = {
	PLATFORM_CASE(TIGERLAKE, B0),
	PLATFORM_CASE(DG1, A0),
	PLATFORM_CASE(DG1, B0),
	PLATFORM_CASE(ALDERLAKE_S, A0),
	PLATFORM_CASE(ALDERLAKE_S, B0),
	PLATFORM_CASE(ALDERLAKE_S, C0),
	PLATFORM_CASE(ALDERLAKE_S, D0),
	PLATFORM_CASE(ALDERLAKE_P, A0),
	PLATFORM_CASE(ALDERLAKE_P, B0),
	PLATFORM_CASE(ALDERLAKE_P, C0),
	SUBPLATFORM_CASE(ALDERLAKE_S, RPLS, D0),
	SUBPLATFORM_CASE(ALDERLAKE_P, RPLU, E0),
	SUBPLATFORM_CASE(DG2, G10, C0),
	SUBPLATFORM_CASE(DG2, G11, B1),
	SUBPLATFORM_CASE(DG2, G12, A1),
	GMDID_CASE(METEORLAKE, 1270, A0, 1300, A0),
	GMDID_CASE(METEORLAKE, 1271, A0, 1300, A0),
	GMDID_CASE(METEORLAKE, 1274, A0, 1300, A0),
	GMDID_CASE(LUNARLAKE, 2004, A0, 2000, A0),
	GMDID_CASE(LUNARLAKE, 2004, B0, 2000, A0),
	GMDID_CASE(BATTLEMAGE, 2001, A0, 1301, A1),
	GMDID_CASE(PANTHERLAKE, 3000, A0, 3000, A0),
};

KUNIT_ARRAY_PARAM(platform, cases, xe_pci_fake_data_desc);

/**
 * xe_pci_fake_data_gen_params - Generate struct xe_pci_fake_data parameters
 * @test: test context object
 * @prev: the pointer to the previous parameter to iterate from or NULL
 * @desc: output buffer with minimum size of KUNIT_PARAM_DESC_SIZE
 *
 * This function prepares struct xe_pci_fake_data parameter.
 *
 * To be used only as a parameter generator function in &KUNIT_CASE_PARAM.
 *
 * Return: pointer to the next parameter or NULL if no more parameters
 */
const void *xe_pci_fake_data_gen_params(struct kunit *test, const void *prev, char *desc)
{
	return platform_gen_params(test, prev, desc);
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_fake_data_gen_params);

static const struct xe_device_desc *lookup_desc(enum xe_platform p)
{
	const struct xe_device_desc *desc;
	const struct pci_device_id *ids;

	for (ids = pciidlist; ids->driver_data; ids++) {
		desc = (const void *)ids->driver_data;
		if (desc->platform == p)
			return desc;
	}
	return NULL;
}

static const struct xe_subplatform_desc *lookup_sub_desc(enum xe_platform p, enum xe_subplatform s)
{
	const struct xe_device_desc *desc = lookup_desc(p);
	const struct xe_subplatform_desc *spd;

	if (desc && desc->subplatforms)
		for (spd = desc->subplatforms; spd->subplatform; spd++)
			if (spd->subplatform == s)
				return spd;
	return NULL;
}

static const char *lookup_platform_name(enum xe_platform p)
{
	const struct xe_device_desc *desc = lookup_desc(p);

	return desc ? desc->platform_name : "INVALID";
}

static const char *__lookup_subplatform_name(enum xe_platform p, enum xe_subplatform s)
{
	const struct xe_subplatform_desc *desc = lookup_sub_desc(p, s);

	return desc ? desc->name : "INVALID";
}

static const char *lookup_subplatform_name(enum xe_platform p, enum xe_subplatform s)
{
	return s == XE_SUBPLATFORM_NONE ? "" : __lookup_subplatform_name(p, s);
}

static const char *subplatform_prefix(enum xe_subplatform s)
{
	return s == XE_SUBPLATFORM_NONE ? "" : " ";
}

static const char *step_prefix(enum xe_step step)
{
	return step == STEP_NONE ? "" : " ";
}

static const char *step_name(enum xe_step step)
{
	return step == STEP_NONE ? "" : xe_step_name(step);
}

static const char *sriov_prefix(enum xe_sriov_mode mode)
{
	return mode <= XE_SRIOV_MODE_NONE ? "" : " ";
}

static const char *sriov_name(enum xe_sriov_mode mode)
{
	return mode <= XE_SRIOV_MODE_NONE ? "" : xe_sriov_mode_to_string(mode);
}

static const char *lookup_graphics_name(unsigned int verx100)
{
	const struct xe_ip *ip = find_graphics_ip(verx100);

	return ip ? ip->name : "";
}

static const char *lookup_media_name(unsigned int verx100)
{
	const struct xe_ip *ip = find_media_ip(verx100);

	return ip ? ip->name : "";
}

/**
 * xe_pci_fake_data_desc - Describe struct xe_pci_fake_data parameter
 * @param: the &struct xe_pci_fake_data parameter to describe
 * @desc: output buffer with minimum size of KUNIT_PARAM_DESC_SIZE
 *
 * This function prepares description of the struct xe_pci_fake_data parameter.
 *
 * It is tailored for use in parameterized KUnit tests where parameter generator
 * is based on the struct xe_pci_fake_data arrays.
 */
void xe_pci_fake_data_desc(const struct xe_pci_fake_data *param, char *desc)
{
	if (param->graphics_verx100 || param->media_verx100)
		snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s%s%s %u.%02u(%s)%s%s %u.%02u(%s)%s%s%s%s",
			 lookup_platform_name(param->platform),
			 subplatform_prefix(param->subplatform),
			 lookup_subplatform_name(param->platform, param->subplatform),
			 param->graphics_verx100 / 100, param->graphics_verx100 % 100,
			 lookup_graphics_name(param->graphics_verx100),
			 step_prefix(param->step.graphics), step_name(param->step.graphics),
			 param->media_verx100 / 100, param->media_verx100 % 100,
			 lookup_media_name(param->media_verx100),
			 step_prefix(param->step.media), step_name(param->step.media),
			 sriov_prefix(param->sriov_mode), sriov_name(param->sriov_mode));
	else
		snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s%s%s%s%s%s%s",
			 lookup_platform_name(param->platform),
			 subplatform_prefix(param->subplatform),
			 lookup_subplatform_name(param->platform, param->subplatform),
			 step_prefix(param->step.graphics), step_name(param->step.graphics),
			 sriov_prefix(param->sriov_mode), sriov_name(param->sriov_mode));
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_fake_data_desc);

static void xe_ip_kunit_desc(const struct xe_ip *param, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%u.%02u %s",
		 param->verx100 / 100, param->verx100 % 100, param->name);
}

/*
 * Pre-GMDID Graphics and Media IPs definitions.
 *
 * Mimic the way GMDID IPs are declared so the same
 * param generator can be used for both
 */
static const struct xe_ip pre_gmdid_graphics_ips[] = {
	{ 1200, "Xe_LP", &graphics_xelp },
	{ 1210, "Xe_LP+", &graphics_xelp },
	{ 1255, "Xe_HPG", &graphics_xehpg },
	{ 1260, "Xe_HPC", &graphics_xehpc },
};

static const struct xe_ip pre_gmdid_media_ips[] = {
	{ 1200, "Xe_M", &media_xem },
	{ 1255, "Xe_HPM", &media_xem },
};

KUNIT_ARRAY_PARAM(pre_gmdid_graphics_ip, pre_gmdid_graphics_ips, xe_ip_kunit_desc);
KUNIT_ARRAY_PARAM(pre_gmdid_media_ip, pre_gmdid_media_ips, xe_ip_kunit_desc);

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
 * @test: test context object
 * @prev: the pointer to the previous parameter to iterate from or NULL
 * @desc: output buffer with minimum size of KUNIT_PARAM_DESC_SIZE
 *
 * This function prepares struct xe_ip parameter.
 *
 * To be used only as a parameter generator function in &KUNIT_CASE_PARAM.
 *
 * Return: pointer to the next parameter or NULL if no more parameters
 */
const void *xe_pci_graphics_ip_gen_param(struct kunit *test, const void *prev, char *desc)
{
	const void *next = pre_gmdid_graphics_ip_gen_params(test, prev, desc);

	if (next)
		return next;
	if (is_insidevar(prev, pre_gmdid_graphics_ips))
		prev = NULL;

	return graphics_ip_gen_params(test, prev, desc);
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_graphics_ip_gen_param);

/**
 * xe_pci_media_ip_gen_param - Generate media struct xe_ip parameters
 * @test: test context object
 * @prev: the pointer to the previous parameter to iterate from or NULL
 * @desc: output buffer with minimum size of KUNIT_PARAM_DESC_SIZE
 *
 * This function prepares struct xe_ip parameter.
 *
 * To be used only as a parameter generator function in &KUNIT_CASE_PARAM.
 *
 * Return: pointer to the next parameter or NULL if no more parameters
 */
const void *xe_pci_media_ip_gen_param(struct kunit *test, const void *prev, char *desc)
{
	const void *next = pre_gmdid_media_ip_gen_params(test, prev, desc);

	if (next)
		return next;
	if (is_insidevar(prev, pre_gmdid_media_ips))
		prev = NULL;

	return media_ip_gen_params(test, prev, desc);
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_media_ip_gen_param);

/**
 * xe_pci_id_gen_param - Generate struct pci_device_id parameters
 * @test: test context object
 * @prev: the pointer to the previous parameter to iterate from or NULL
 * @desc: output buffer with minimum size of KUNIT_PARAM_DESC_SIZE
 *
 * This function prepares struct pci_device_id parameter.
 *
 * To be used only as a parameter generator function in &KUNIT_CASE_PARAM.
 *
 * Return: pointer to the next parameter or NULL if no more parameters
 */
const void *xe_pci_id_gen_param(struct kunit *test, const void *prev, char *desc)
{
	const struct pci_device_id *pci = pci_id_gen_params(test, prev, desc);

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
		*revid = xe_step_to_gmdid(data->step.media);
	} else {
		*ver = data->graphics_verx100;
		*revid = xe_step_to_gmdid(data->step.graphics);
	}
}

static void fake_xe_info_probe_tile_count(struct xe_device *xe)
{
	/* Nothing to do, just use the statically defined value. */
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
	kunit_activate_static_stub(test, xe_info_probe_tile_count,
				   fake_xe_info_probe_tile_count);

	xe_info_init_early(xe, desc, subplatform_desc);
	xe_info_init(xe, desc);

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(xe_pci_fake_device_init);

/**
 * xe_pci_live_device_gen_param - Helper to iterate Xe devices as KUnit parameters
 * @test: test context object
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
const void *xe_pci_live_device_gen_param(struct kunit *test, const void *prev, char *desc)
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
