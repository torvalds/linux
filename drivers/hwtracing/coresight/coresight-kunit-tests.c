// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <kunit/device.h>
#include <linux/coresight.h>

#include "coresight-priv.h"

static struct coresight_device *coresight_test_device(struct device *dev)
{
	struct coresight_device *csdev = devm_kcalloc(dev, 1,
						     sizeof(struct coresight_device),
						     GFP_KERNEL);
	csdev->pdata = devm_kcalloc(dev, 1,
				   sizeof(struct coresight_platform_data),
				   GFP_KERNEL);
	return csdev;
}

static void test_default_sink(struct kunit *test)
{
	/*
	 * Source -> ETF -> ETR -> CATU
	 *                   ^
	 *                   | default
	 */
	struct device *dev = kunit_device_register(test, "coresight_kunit");
	struct coresight_device *src = coresight_test_device(dev),
				*etf = coresight_test_device(dev),
				*etr = coresight_test_device(dev),
				*catu = coresight_test_device(dev);
	struct coresight_connection conn = {};

	src->type = CORESIGHT_DEV_TYPE_SOURCE;
	/*
	 * Don't use CORESIGHT_DEV_SUBTYPE_SOURCE_PROC, that would always return
	 * a TRBE sink if one is registered.
	 */
	src->subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_BUS;
	etf->type = CORESIGHT_DEV_TYPE_LINKSINK;
	etf->subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_BUFFER;
	etr->type = CORESIGHT_DEV_TYPE_SINK;
	etr->subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_SYSMEM;
	catu->type = CORESIGHT_DEV_TYPE_HELPER;

	conn.src_dev = src;
	conn.dest_dev = etf;
	coresight_add_out_conn(dev, src->pdata, &conn);

	conn.src_dev = etf;
	conn.dest_dev = etr;
	coresight_add_out_conn(dev, etf->pdata, &conn);

	conn.src_dev = etr;
	conn.dest_dev = catu;
	coresight_add_out_conn(dev, etr->pdata, &conn);

	KUNIT_ASSERT_PTR_EQ(test, coresight_find_default_sink(src), etr);
}

static struct kunit_case coresight_testcases[] = {
	KUNIT_CASE(test_default_sink),
	{}
};

static struct kunit_suite coresight_test_suite = {
	.name = "coresight_test_suite",
	.test_cases = coresight_testcases,
};

kunit_test_suites(&coresight_test_suite);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Clark <james.clark@linaro.org>");
MODULE_DESCRIPTION("Arm CoreSight KUnit tests");
