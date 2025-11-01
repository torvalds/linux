// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitory Technology Discovery KUNIT tests
 *
 * Copyright (c) 2025, Intel Corporation.
 * All Rights Reserved.
 */

#include <kunit/test.h>
#include <linux/err.h>
#include <linux/intel_pmt_features.h>
#include <linux/intel_vsec.h>
#include <linux/module.h>
#include <linux/slab.h>

#define PMT_FEATURE_COUNT (FEATURE_MAX + 1)

static void
validate_pmt_regions(struct kunit *test, struct pmt_feature_group *feature_group, int feature_id)
{
	int i;

	kunit_info(test, "Feature ID %d [%s] has %d regions.\n", feature_id,
		   pmt_feature_names[feature_id], feature_group->count);

	for (i = 0; i < feature_group->count; i++) {
		struct telemetry_region *region = &feature_group->regions[i];

		kunit_info(test, "  - Region %d: cdie_mask=%u, package_id=%u, partition=%u, segment=%u,",
			   i, region->plat_info.cdie_mask, region->plat_info.package_id,
			   region->plat_info.partition, region->plat_info.segment);
		kunit_info(test, "\t\tbus=%u, device=%u, function=%u, guid=0x%x,",
			   region->plat_info.bus_number, region->plat_info.device_number,
			   region->plat_info.function_number, region->guid);
		kunit_info(test, "\t\taddr=%p, size=%zu, num_rmids=%u", region->addr, region->size,
			   region->num_rmids);


		KUNIT_ASSERT_GE(test, region->plat_info.cdie_mask, 0);
		KUNIT_ASSERT_GE(test, region->plat_info.package_id, 0);
		KUNIT_ASSERT_GE(test, region->plat_info.partition, 0);
		KUNIT_ASSERT_GE(test, region->plat_info.segment, 0);
		KUNIT_ASSERT_GE(test, region->plat_info.bus_number, 0);
		KUNIT_ASSERT_GE(test, region->plat_info.device_number, 0);
		KUNIT_ASSERT_GE(test, region->plat_info.function_number, 0);

		KUNIT_ASSERT_NE(test, region->guid, 0);

		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, (__force const void *)region->addr);
	}
}

static void linebreak(struct kunit *test)
{
	kunit_info(test, "*****************************************************************************\n");
}

static void test_intel_pmt_get_regions_by_feature(struct kunit *test)
{
	struct pmt_feature_group *feature_group;
	int num_available = 0;
	int feature_id;

	/* Iterate through all possible feature IDs */
	for (feature_id = 1; feature_id < PMT_FEATURE_COUNT; feature_id++, linebreak(test)) {
		const char *name;

		if (!pmt_feature_id_is_valid(feature_id))
			continue;

		name = pmt_feature_names[feature_id];

		feature_group = intel_pmt_get_regions_by_feature(feature_id);
		if (IS_ERR(feature_group)) {
			if (PTR_ERR(feature_group) == -ENOENT)
				kunit_warn(test, "intel_pmt_get_regions_by_feature() reporting feature %d [%s] is not present.\n",
					   feature_id, name);
			else
				kunit_warn(test, "intel_pmt_get_regions_by_feature() returned error %ld while attempt to lookup %d [%s].\n",
					   PTR_ERR(feature_group), feature_id, name);

			continue;
		}

		if (!feature_group) {
			kunit_warn(test, "Feature ID %d: %s is not available.\n", feature_id, name);
			continue;
		}

		num_available++;

		validate_pmt_regions(test, feature_group, feature_id);

		intel_pmt_put_feature_group(feature_group);
	}

	if (num_available == 0)
		kunit_warn(test, "No PMT region groups were available for any feature ID (0-10).\n");
}

static struct kunit_case intel_pmt_discovery_test_cases[] = {
	KUNIT_CASE(test_intel_pmt_get_regions_by_feature),
	{}
};

static struct kunit_suite intel_pmt_discovery_test_suite = {
	.name = "pmt_discovery_test",
	.test_cases = intel_pmt_discovery_test_cases,
};

kunit_test_suite(intel_pmt_discovery_test_suite);

MODULE_IMPORT_NS("INTEL_PMT_DISCOVERY");
MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel PMT Discovery KUNIT test driver");
MODULE_LICENSE("GPL");
