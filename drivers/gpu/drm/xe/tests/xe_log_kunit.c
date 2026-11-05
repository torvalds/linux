// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "tests/xe_kunit_helpers.h"
#include "tests/xe_pci_test.h"
#include "xe_device.h"
#include "xe_log.h"

static void nop_dmesg_vprintk(struct pci_dev *pdev, int cper_sev, struct va_format *vaf)
{
}

static void nop_emit_cper(struct pci_dev *pdev, int cper_sev,
			  enum xe_sigid sigid, u32 component, u32 location,
			  const void *data, size_t len, struct va_format *vaf)
{
}

static const char *component_name(u32 component)
{
	switch (component) {
#define make_component_tag_case(_CLASS, _ID, _TAG, _SIG, _NAME) \
	case XE_LOG_COMPONENT_##_TAG: return _NAME;
	DEFINE_XE_LOG_COMPONENTS(make_component_tag_case)
#undef make_component_tag_case
	}
	return component ? "???" : "";
}

static const char *location_type(u32 location)
{
	u32 type = FIELD_GET(XE_LOG_LOCATION_TYPE_MASK, location);

	return type == XE_LOG_LOCATION_TYPE_DEVICE ? "DEVICE" :
	       type == XE_LOG_LOCATION_TYPE_TILE ? "TILE" :
	       type == XE_LOG_LOCATION_TYPE_GT ? "GT" :
	       location ? "?" : "";
}

static void fake_emit_cper(struct pci_dev *pdev, int cper_sev,
			   enum xe_sigid sigid, u32 component, u32 location,
			   const void *data, size_t len, struct va_format *vaf)
{
	char msg[64];
	int n;

	pr_info("\n");
	pr_info("CPER SEV=%u SIGID=%u\n", cper_sev, sigid);
	pr_info("CPER DEVICE=%s\n", dev_name(&pdev->dev));
	if (location)
		pr_info("CPER LOCATION=%#x \t# %s.%u\n",
			location, location_type(location),
			FIELD_GET(XE_LOG_LOCATION_ID_MASK, location));
	if (component)
		pr_info("CPER COMPONENT=%#x \t# %s\n",
			component, component_name(component));
	if (IS_ERR(data))
		pr_info("CPER ERR=%ld \t\t# %pe\n", PTR_ERR(data), data);
	else if (len)
		print_hex_dump(KERN_INFO, "CPER BIN=", DUMP_PREFIX_OFFSET,
			       16, 1, data, len, false);

	n = vscnprintf(msg, sizeof(msg), vaf->fmt, *vaf->va);
	print_hex_dump(KERN_INFO, "CPER MSG=", DUMP_PREFIX_OFFSET, 16, 1, msg, n, true);
	pr_info("CPER END\n");
}

static const u8 blob[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
static const u8 dead[] = { 0xde, 0xad, 0xbe, 0xef };

static struct xe_tile *to_tile_safe(struct xe_device *xe)
{
	return xe ? &xe->tiles[1] : NULL;
}

static struct xe_gt *to_gt_safe(struct xe_device *xe)
{
	return xe ? to_tile_safe(xe)->primary_gt : NULL;
}

static struct pci_dev *to_pdev_safe(struct xe_device *xe)
{
	return xe ? xe_any_to_pdev(xe) : NULL;
}

static void demo(struct xe_device *xe)
{
	struct pci_dev *pdev = xe_any_to_pdev(xe);
	struct xe_tile *tile = to_tile_safe(xe);
	struct xe_gt *gt = to_gt_safe(xe);

	/* SW errno */
	xe_log_emit(pdev, CPER_SEV_FATAL, XE_SIGID_PROBE,
		    XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
		    ERR_PTR(-ENODEV), 0, "testing %s signature\n", "software");

	xe_log_err(tile, PROBE, -ENODEV, "testing %s signature\n", "software");
	xe_log_err_corrected(gt, PROBE, -ENODEV, "testing %s signature\n", "software");
	xe_log_info(gt, PROBE, "testing %s signature\n", "software");

	/* HW data */
	xe_log_emit(pdev, CPER_SEV_FATAL, XE_SIGID_PCIE,
		    XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
		    blob, sizeof(blob), "testing %s signature\n", "HARDWARE");
	xe_log_emit_recoverable(pdev, XE_SIGID_FABRIC,
				XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
				blob, sizeof(blob), "testing %s signature\n", "HARDWARE");
	xe_log_from_corrected(tile, XE_SIGID_DEVICE_MEMORY, XE_LOG_COMPONENT_NONE,
			      blob, sizeof(blob), "testing %s signature\n", "HARDWARE");
	xe_log_from_info(gt, XE_SIGID_CORE_COMPUTE, XE_LOG_COMPONENT_NONE,
			 blob, sizeof(blob), "testing %s signature\n", "HARDWARE");
}

static void demo_dmesg(struct kunit *test)
{
	kunit_activate_static_stub(test, log_emit_cper, nop_emit_cper);
	demo(test->priv);
}

static void demo_cper(struct kunit *test)
{
	kunit_activate_static_stub(test, log_emit_cper, fake_emit_cper);
	kunit_activate_static_stub(test, log_dmesg_vprintk, nop_dmesg_vprintk);
	demo(test->priv);
}

static const char *test_fatal(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_emit_fatal(pdev, XE_SIGID_PROBE,
				  XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
				  NULL, 0, "testing %d\n", 123);
	return "SIGID=101 FATAL testing 123\n";
}

static const char *test_fatal_tile(struct xe_device *xe)
{
	struct xe_tile *tile = to_tile_safe(xe);

	if (tile)
		xe_log_from_fatal(tile, XE_SIGID_PROBE, XE_LOG_COMPONENT_NONE,
				  ERR_PTR(-ENODEV), 0, "testing %d\n", 123);
	return "SIGID=101 FATAL (-ENODEV) Tile1: testing 123\n";
}

static const char *test_fatal_gt(struct xe_device *xe)
{
	struct xe_gt *gt = to_gt_safe(xe);

	if (gt)
		xe_log_from_fatal(gt, XE_SIGID_PROBE, XE_LOG_COMPONENT_NONE,
				  ERR_PTR(-ENODEV), 0, "testing %d\n", 123);
	return "SIGID=101 FATAL (-ENODEV) Tile1: GT1: testing 123\n";
}

static const char *test_fatal_comp(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_err_fatal(pdev, PROBE, -ENODEV, "testing %d\n", 123);
	return "SIGID=101 FATAL (-ENODEV) PROBE: testing 123\n";
}

static const char *test_fatal_comp_tile(struct xe_device *xe)
{
	struct xe_tile *tile = to_tile_safe(xe);

	if (tile)
		xe_log_err_fatal(tile, PROBE, -ENODEV, "testing %d\n", 123);
	return "SIGID=101 FATAL (-ENODEV) Tile1: PROBE: testing 123\n";
}

static const char *test_fatal_comp_gt(struct xe_device *xe)
{
	struct xe_gt *gt = to_gt_safe(xe);

	if (gt)
		xe_log_err_fatal(gt, PROBE, -ENODEV, "testing %d\n", 123);
	return "SIGID=101 FATAL (-ENODEV) Tile1: GT1: PROBE: testing 123\n";
}

static const char *test_fatal_all(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);
	struct xe_gt *gt = to_gt_safe(xe);

	if (pdev)
		xe_log_emit(pdev, CPER_SEV_FATAL, XE_SIGID_PROBE,
			    XE_LOG_COMPONENT_PROBE, MAKE_XE_LOG_LOCATION(GT, gt->info.id),
			    ERR_PTR(-ENODEV), 0, "testing %d\n", 123);
	return "SIGID=101 FATAL (-ENODEV) Tile1: GT1: PROBE: testing 123\n";
}

static const char *test_recoverable(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_emit_recoverable(pdev, XE_SIGID_RUNTIME_FW,
					XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
					ERR_PTR(-EIO), 0, "testing %d\n", 123);
	return "SIGID=104 (-EIO) testing 123\n";
}

static const char *test_recoverable_tile(struct xe_device *xe)
{
	struct xe_tile *tile = to_tile_safe(xe);

	if (tile)
		xe_log_from_recoverable(tile, XE_SIGID_RUNTIME_FW, XE_LOG_COMPONENT_NONE,
					ERR_PTR(-EIO), 0, "testing %d\n", 123);
	return "SIGID=104 (-EIO) Tile1: testing 123\n";
}

static const char *test_recoverable_gt(struct xe_device *xe)
{
	struct xe_gt *gt = to_gt_safe(xe);

	if (gt)
		xe_log_from_recoverable(gt, XE_SIGID_RUNTIME_FW, XE_LOG_COMPONENT_NONE,
					ERR_PTR(-EIO), 0, "testing %d\n", 123);
	return "SIGID=104 (-EIO) Tile1: GT1: testing 123\n";
}

static const char *test_recoverable_comp(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_err(pdev, GUC, -EIO, "testing %d\n", 123);
	return "SIGID=104 (-EIO) GUC: testing 123\n";
}

static const char *test_recoverable_comp_tile(struct xe_device *xe)
{
	struct xe_tile *tile = to_tile_safe(xe);

	if (tile)
		xe_log_err(tile, GUC, -EIO, "testing %d\n", 123);
	return "SIGID=104 (-EIO) Tile1: GUC: testing 123\n";
}

static const char *test_recoverable_comp_gt(struct xe_device *xe)
{
	struct xe_gt *gt = to_gt_safe(xe);

	if (gt)
		xe_log_err(gt, GUC, -EIO, "testing %d\n", 123);
	return "SIGID=104 (-EIO) Tile1: GT1: GUC: testing 123\n";
}

static const char *test_recoverable_all(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);
	struct xe_gt *gt = to_gt_safe(xe);

	if (pdev)
		xe_log_emit(pdev, CPER_SEV_RECOVERABLE, XE_SIGID_RUNTIME_FW,
			    XE_LOG_COMPONENT_GUC, MAKE_XE_LOG_LOCATION(GT, gt->info.id),
			    ERR_PTR(-EIO), 0, "testing %d\n", 123);
	return "SIGID=104 (-EIO) Tile1: GT1: GUC: testing 123\n";
}

static const char *test_info(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_emit(pdev, CPER_SEV_INFORMATIONAL, XE_SIGID_DEVICE_FW,
			    XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
			    NULL, 0, "testing %d\n", 123);
	return "SIGID=105 testing 123\n";
}

static const char *test_info_tile(struct xe_device *xe)
{
	struct xe_tile *tile = to_tile_safe(xe);

	if (tile)
		xe_log_from_info(tile, XE_SIGID_DEVICE_FW, XE_LOG_COMPONENT_NONE,
				 NULL, 0, "testing %d\n", 123);
	return "SIGID=105 Tile1: testing 123\n";
}

static const char *test_info_gt(struct xe_device *xe)
{
	struct xe_gt *gt = to_gt_safe(xe);

	if (gt)
		xe_log_from_info(gt, XE_SIGID_DEVICE_FW, XE_LOG_COMPONENT_NONE,
				 NULL, 0, "testing %d\n", 123);
	return "SIGID=105 Tile1: GT1: testing 123\n";
}

static const char *test_info_err(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_err_info(pdev, PCODE, -EPROTO, "testing %d\n", 123);
	return "SIGID=105 (-EPROTO) PCODE: testing 123\n";
}

static const char *test_info_comp(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_info(pdev, PCODE, "testing %d\n", 123);
	return "SIGID=105 PCODE: testing 123\n";
}

static const char *test_info_comp_tile(struct xe_device *xe)
{
	struct xe_tile *tile = to_tile_safe(xe);

	if (tile)
		xe_log_info(tile, PCODE, "testing %d\n", 123);
	return "SIGID=105 Tile1: PCODE: testing 123\n";
}

static const char *test_info_comp_gt(struct xe_device *xe)
{
	struct xe_gt *gt = to_gt_safe(xe);

	if (gt)
		xe_log_info(gt, PCODE, "testing %d\n", 123);
	return "SIGID=105 Tile1: GT1: PCODE: testing 123\n";
}

static const char *test_info_all(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);
	struct xe_gt *gt = to_gt_safe(xe);

	if (pdev)
		xe_log_emit(pdev, CPER_SEV_INFORMATIONAL, XE_SIGID_DEVICE_FW,
			    XE_LOG_COMPONENT_PCODE, MAKE_XE_LOG_LOCATION(GT, gt->info.id),
			    NULL, 0, "testing %d\n", 123);
	return "SIGID=105 Tile1: GT1: PCODE: testing 123\n";
}

static const char *test_hw_fatal(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_emit_fatal(pdev, XE_SIGID_PCIE,
				  XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
				  dead, sizeof(dead), "testing %d\n", 123);
	return "SIGID=201 FATAL (deadbeef) " HW_ERR "testing 123\n";
}

static const char *test_hw_recoverable(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_emit_recoverable(pdev, XE_SIGID_DEVICE_MEMORY,
					XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
					dead, sizeof(dead), "testing %d\n", 123);
	return "SIGID=202 (deadbeef) " HW_ERR "testing 123\n";
}

static const char *test_hw_corrected(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_emit_corrected(pdev, XE_SIGID_DEVICE_MEMORY,
				      XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
				      dead, sizeof(dead), "testing %d\n", 123);
	return "SIGID=202 CORRECTED (deadbeef) " HW_ERR "testing 123\n";
}

static const char *test_hw_informational(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pdev_safe(xe);

	if (pdev)
		xe_log_emit_info(pdev, XE_SIGID_DEVICE_MEMORY,
				 XE_LOG_COMPONENT_NONE, XE_LOG_LOCATION_NONE,
				 dead, sizeof(dead), "testing %d\n", 123);
	return "SIGID=202 (deadbeef) testing 123\n";
}

static const struct log_test_param {
	const char *(*func)(struct xe_device *xe);
} log_test_params[] = {
	{ .func = test_fatal },
	{ .func = test_fatal_tile },
	{ .func = test_fatal_gt },
	{ .func = test_fatal_comp },
	{ .func = test_fatal_comp_tile },
	{ .func = test_fatal_comp_gt },
	{ .func = test_fatal_all },
	{ .func = test_recoverable },
	{ .func = test_recoverable_tile },
	{ .func = test_recoverable_gt },
	{ .func = test_recoverable_comp },
	{ .func = test_recoverable_comp_tile },
	{ .func = test_recoverable_comp_gt },
	{ .func = test_recoverable_all },
	{ .func = test_info },
	{ .func = test_info_tile },
	{ .func = test_info_gt },
	{ .func = test_info_err },
	{ .func = test_info_comp },
	{ .func = test_info_comp_tile },
	{ .func = test_info_comp_gt },
	{ .func = test_info_all },
	{ .func = test_hw_fatal },
	{ .func = test_hw_recoverable },
	{ .func = test_hw_corrected },
	{ .func = test_hw_informational },
};

static void log_param_get_desc(const struct log_test_param *p, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%ps", p->func);
}

KUNIT_ARRAY_PARAM(log, log_test_params, log_param_get_desc);

static void check_dmesg_vprintk(struct pci_dev *pdev, int cper_sev, struct va_format *vaf)
{
	struct kunit *test = kunit_get_current_test();
	const struct log_test_param *param = test->param_value;
	const char *exp = param->func(NULL);
	char msg[64];
	int n;

	n = vsnprintf(msg, sizeof(msg), vaf->fmt, *vaf->va);
	KUNIT_EXPECT_LT(test, n, sizeof(msg));
	KUNIT_EXPECT_STREQ(test, msg, exp);
}

static void test_dmesg(struct kunit *test)
{
	const struct log_test_param *param = test->param_value;

	kunit_activate_static_stub(test, log_emit_cper, nop_emit_cper);
	kunit_activate_static_stub(test, log_dmesg_vprintk, check_dmesg_vprintk);

	param->func(test->priv);
}

#define INVALID_TILEID	(XE_MAX_TILES_PER_DEVICE + 1)
#define INVALID_GTID	(XE_MAX_TILES_PER_DEVICE * XE_MAX_GT_PER_TILE + 1)

#define PREP_TEST_LOCATION(type, id) \
	(FIELD_PREP_CONST(XE_LOG_LOCATION_TYPE_MASK, (type)) | \
	 FIELD_PREP_CONST(XE_LOG_LOCATION_ID_MASK, (id)))
#define PREP_TEST_COMPONENT(class, type) \
	(FIELD_PREP_CONST(XE_LOG_COMPONENT_CLASS_MASK, (class)) | \
	 FIELD_PREP_CONST(XE_LOG_COMPONENT_TYPE_MASK, (type)))

static const struct {
	u32 comp;
	u32 loc;
	const char *name;
} invalid_params[] = {
	{	.name = "no-component no-location no-warn" },
	{	.loc = PREP_TEST_LOCATION(0, 1),
		.name = "reserved location" },
	{	.loc = PREP_TEST_LOCATION(255, 0),
		.name = "unknown location" },
	{	.loc = PREP_TEST_LOCATION(XE_LOG_LOCATION_TYPE_DEVICE, 1),
		.name = "nonzero-device-id location" },
	{	.loc = PREP_TEST_LOCATION(XE_LOG_LOCATION_TYPE_TILE, INVALID_TILEID),
		.name = "invalid-tile-id location" },
	{	.loc = PREP_TEST_LOCATION(XE_LOG_LOCATION_TYPE_GT, INVALID_GTID),
		.name = "invalid-gt-id location" },
	{	.comp = PREP_TEST_COMPONENT(255, 0),
		.name = "unknown component class" },
	{	.comp = PREP_TEST_COMPONENT(XE_LOG_COMPONENT_CLASS_SYSTEM, 255),
		.name = "unknown system component" },
	{	.comp = PREP_TEST_COMPONENT(XE_LOG_COMPONENT_CLASS_HARDWARE, 255),
		.name = "unknown hardware component" },
	{	.comp = PREP_TEST_COMPONENT(255, 1),
		.loc = PREP_TEST_LOCATION(255, 1),
		.name = "unknown component and location" },
};

KUNIT_ARRAY_PARAM_DESC(invalid_param, invalid_params, name);

static void test_invalid(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	typeof(invalid_params[0]) *param = test->param_value;

	struct pci_dev *pdev = xe_any_to_pdev(xe);

	if (!IS_ENABLED(CONFIG_DRM_XE_DEBUG))
		kunit_skip(test, "requires CONFIG_DRM_XE_DEBUG\n");

	kunit_activate_static_stub(test, log_emit_cper, nop_emit_cper);
	kunit_activate_static_stub(test, log_dmesg_vprintk, nop_dmesg_vprintk);

	kunit_warning_suppress(test) {
		xe_log_emit(pdev, CPER_SEV_FATAL, XE_SIGID_PROBE,
			    param->comp, param->loc,
			    NULL, 0, "testing %s\n", param->name);
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, !!param->comp + !!param->loc);
	}
}

static int xe_log_test_init(struct kunit *test)
{
	struct xe_pci_fake_data fake = {
		.platform = XE_PVC, /* with max_remote_tiles != 0 */
		.subplatform = XE_SUBPLATFORM_NONE,
		.graphics_verx100 = 2001,
		.media_verx100 = 2001,
	};
	struct xe_device *xe;

	test->priv = &fake;
	xe_kunit_helper_xe_device_test_init(test);
	xe = test->priv;

	KUNIT_ASSERT_NOT_NULL(test, to_tile_safe(xe));
	KUNIT_ASSERT_NOT_NULL(test, to_gt_safe(xe));
	KUNIT_EXPECT_EQ(test, 1, xe_any_id(to_tile_safe(xe)));
	KUNIT_EXPECT_EQ(test, 1, xe_any_id(to_gt_safe(xe)));

	return 0;
}

static struct kunit_case xe_log_test_cases[] = {
	KUNIT_CASE(demo_cper),
	KUNIT_CASE(demo_dmesg),
	KUNIT_CASE_PARAM(test_dmesg, log_gen_params),
	KUNIT_CASE_PARAM(test_invalid, invalid_param_gen_params),
	{}
};

static struct kunit_suite xe_log_suite = {
	.name = "xe_log",
	.test_cases = xe_log_test_cases,
	.init = xe_log_test_init,
};

kunit_test_suites(&xe_log_suite);
