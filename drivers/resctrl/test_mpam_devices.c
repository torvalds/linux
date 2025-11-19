// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Arm Ltd.
/* This file is intended to be included into mpam_devices.c */

#include <kunit/test.h>

/*
 * This test catches fields that aren't being sanitised - but can't tell you
 * which one...
 */
static void test__props_mismatch(struct kunit *test)
{
	struct mpam_props parent = { 0 };
	struct mpam_props child;

	memset(&child, 0xff, sizeof(child));
	__props_mismatch(&parent, &child, false);

	memset(&child, 0, sizeof(child));
	KUNIT_EXPECT_EQ(test, memcmp(&parent, &child, sizeof(child)), 0);

	memset(&child, 0xff, sizeof(child));
	__props_mismatch(&parent, &child, true);

	KUNIT_EXPECT_EQ(test, memcmp(&parent, &child, sizeof(child)), 0);
}

static struct list_head fake_classes_list;
static struct mpam_class fake_class = { 0 };
static struct mpam_component fake_comp1 = { 0 };
static struct mpam_component fake_comp2 = { 0 };
static struct mpam_vmsc fake_vmsc1 = { 0 };
static struct mpam_vmsc fake_vmsc2 = { 0 };
static struct mpam_msc fake_msc1 = { 0 };
static struct mpam_msc fake_msc2 = { 0 };
static struct mpam_msc_ris fake_ris1 = { 0 };
static struct mpam_msc_ris fake_ris2 = { 0 };
static struct platform_device fake_pdev = { 0 };

static inline void reset_fake_hierarchy(void)
{
	INIT_LIST_HEAD(&fake_classes_list);

	memset(&fake_class, 0, sizeof(fake_class));
	fake_class.level = 3;
	fake_class.type = MPAM_CLASS_CACHE;
	INIT_LIST_HEAD_RCU(&fake_class.components);
	INIT_LIST_HEAD(&fake_class.classes_list);

	memset(&fake_comp1, 0, sizeof(fake_comp1));
	memset(&fake_comp2, 0, sizeof(fake_comp2));
	fake_comp1.comp_id = 1;
	fake_comp2.comp_id = 2;
	INIT_LIST_HEAD(&fake_comp1.vmsc);
	INIT_LIST_HEAD(&fake_comp1.class_list);
	INIT_LIST_HEAD(&fake_comp2.vmsc);
	INIT_LIST_HEAD(&fake_comp2.class_list);

	memset(&fake_vmsc1, 0, sizeof(fake_vmsc1));
	memset(&fake_vmsc2, 0, sizeof(fake_vmsc2));
	INIT_LIST_HEAD(&fake_vmsc1.ris);
	INIT_LIST_HEAD(&fake_vmsc1.comp_list);
	fake_vmsc1.msc = &fake_msc1;
	INIT_LIST_HEAD(&fake_vmsc2.ris);
	INIT_LIST_HEAD(&fake_vmsc2.comp_list);
	fake_vmsc2.msc = &fake_msc2;

	memset(&fake_ris1, 0, sizeof(fake_ris1));
	memset(&fake_ris2, 0, sizeof(fake_ris2));
	fake_ris1.ris_idx = 1;
	INIT_LIST_HEAD(&fake_ris1.msc_list);
	fake_ris2.ris_idx = 2;
	INIT_LIST_HEAD(&fake_ris2.msc_list);

	fake_msc1.pdev = &fake_pdev;
	fake_msc2.pdev = &fake_pdev;

	list_add(&fake_class.classes_list, &fake_classes_list);
}

static void test_mpam_enable_merge_features(struct kunit *test)
{
	reset_fake_hierarchy();

	mutex_lock(&mpam_list_lock);

	/* One Class+Comp, two RIS in one vMSC with common features */
	fake_comp1.class = &fake_class;
	list_add(&fake_comp1.class_list, &fake_class.components);
	fake_comp2.class = NULL;
	fake_vmsc1.comp = &fake_comp1;
	list_add(&fake_vmsc1.comp_list, &fake_comp1.vmsc);
	fake_vmsc2.comp = NULL;
	fake_ris1.vmsc = &fake_vmsc1;
	list_add(&fake_ris1.vmsc_list, &fake_vmsc1.ris);
	fake_ris2.vmsc = &fake_vmsc1;
	list_add(&fake_ris2.vmsc_list, &fake_vmsc1.ris);

	mpam_set_feature(mpam_feat_cpor_part, &fake_ris1.props);
	mpam_set_feature(mpam_feat_cpor_part, &fake_ris2.props);
	fake_ris1.props.cpbm_wd = 4;
	fake_ris2.props.cpbm_wd = 4;

	mpam_enable_merge_features(&fake_classes_list);

	KUNIT_EXPECT_TRUE(test, mpam_has_feature(mpam_feat_cpor_part, &fake_class.props));
	KUNIT_EXPECT_EQ(test, fake_class.props.cpbm_wd, 4);

	reset_fake_hierarchy();

	/* One Class+Comp, two RIS in one vMSC with non-overlapping features */
	fake_comp1.class = &fake_class;
	list_add(&fake_comp1.class_list, &fake_class.components);
	fake_comp2.class = NULL;
	fake_vmsc1.comp = &fake_comp1;
	list_add(&fake_vmsc1.comp_list, &fake_comp1.vmsc);
	fake_vmsc2.comp = NULL;
	fake_ris1.vmsc = &fake_vmsc1;
	list_add(&fake_ris1.vmsc_list, &fake_vmsc1.ris);
	fake_ris2.vmsc = &fake_vmsc1;
	list_add(&fake_ris2.vmsc_list, &fake_vmsc1.ris);

	mpam_set_feature(mpam_feat_cpor_part, &fake_ris1.props);
	mpam_set_feature(mpam_feat_cmax_cmin, &fake_ris2.props);
	fake_ris1.props.cpbm_wd = 4;
	fake_ris2.props.cmax_wd = 4;

	mpam_enable_merge_features(&fake_classes_list);

	/* Multiple RIS within one MSC controlling the same resource can be mismatched */
	KUNIT_EXPECT_TRUE(test, mpam_has_feature(mpam_feat_cpor_part, &fake_class.props));
	KUNIT_EXPECT_TRUE(test, mpam_has_feature(mpam_feat_cmax_cmin, &fake_class.props));
	KUNIT_EXPECT_TRUE(test, mpam_has_feature(mpam_feat_cmax_cmin, &fake_vmsc1.props));
	KUNIT_EXPECT_EQ(test, fake_class.props.cpbm_wd, 4);
	KUNIT_EXPECT_EQ(test, fake_vmsc1.props.cmax_wd, 4);
	KUNIT_EXPECT_EQ(test, fake_class.props.cmax_wd, 4);

	reset_fake_hierarchy();

	/* One Class+Comp, two MSC with overlapping features */
	fake_comp1.class = &fake_class;
	list_add(&fake_comp1.class_list, &fake_class.components);
	fake_comp2.class = NULL;
	fake_vmsc1.comp = &fake_comp1;
	list_add(&fake_vmsc1.comp_list, &fake_comp1.vmsc);
	fake_vmsc2.comp = &fake_comp1;
	list_add(&fake_vmsc2.comp_list, &fake_comp1.vmsc);
	fake_ris1.vmsc = &fake_vmsc1;
	list_add(&fake_ris1.vmsc_list, &fake_vmsc1.ris);
	fake_ris2.vmsc = &fake_vmsc2;
	list_add(&fake_ris2.vmsc_list, &fake_vmsc2.ris);

	mpam_set_feature(mpam_feat_cpor_part, &fake_ris1.props);
	mpam_set_feature(mpam_feat_cpor_part, &fake_ris2.props);
	fake_ris1.props.cpbm_wd = 4;
	fake_ris2.props.cpbm_wd = 4;

	mpam_enable_merge_features(&fake_classes_list);

	KUNIT_EXPECT_TRUE(test, mpam_has_feature(mpam_feat_cpor_part, &fake_class.props));
	KUNIT_EXPECT_EQ(test, fake_class.props.cpbm_wd, 4);

	reset_fake_hierarchy();

	/* One Class+Comp, two MSC with non-overlapping features */
	fake_comp1.class = &fake_class;
	list_add(&fake_comp1.class_list, &fake_class.components);
	fake_comp2.class = NULL;
	fake_vmsc1.comp = &fake_comp1;
	list_add(&fake_vmsc1.comp_list, &fake_comp1.vmsc);
	fake_vmsc2.comp = &fake_comp1;
	list_add(&fake_vmsc2.comp_list, &fake_comp1.vmsc);
	fake_ris1.vmsc = &fake_vmsc1;
	list_add(&fake_ris1.vmsc_list, &fake_vmsc1.ris);
	fake_ris2.vmsc = &fake_vmsc2;
	list_add(&fake_ris2.vmsc_list, &fake_vmsc2.ris);

	mpam_set_feature(mpam_feat_cpor_part, &fake_ris1.props);
	mpam_set_feature(mpam_feat_cmax_cmin, &fake_ris2.props);
	fake_ris1.props.cpbm_wd = 4;
	fake_ris2.props.cmax_wd = 4;

	mpam_enable_merge_features(&fake_classes_list);

	/*
	 * Multiple RIS in different MSC can't control the same resource,
	 * mismatched features can not be supported.
	 */
	KUNIT_EXPECT_FALSE(test, mpam_has_feature(mpam_feat_cpor_part, &fake_class.props));
	KUNIT_EXPECT_FALSE(test, mpam_has_feature(mpam_feat_cmax_cmin, &fake_class.props));
	KUNIT_EXPECT_EQ(test, fake_class.props.cpbm_wd, 0);
	KUNIT_EXPECT_EQ(test, fake_class.props.cmax_wd, 0);

	reset_fake_hierarchy();

	/* One Class+Comp, two MSC with incompatible overlapping features */
	fake_comp1.class = &fake_class;
	list_add(&fake_comp1.class_list, &fake_class.components);
	fake_comp2.class = NULL;
	fake_vmsc1.comp = &fake_comp1;
	list_add(&fake_vmsc1.comp_list, &fake_comp1.vmsc);
	fake_vmsc2.comp = &fake_comp1;
	list_add(&fake_vmsc2.comp_list, &fake_comp1.vmsc);
	fake_ris1.vmsc = &fake_vmsc1;
	list_add(&fake_ris1.vmsc_list, &fake_vmsc1.ris);
	fake_ris2.vmsc = &fake_vmsc2;
	list_add(&fake_ris2.vmsc_list, &fake_vmsc2.ris);

	mpam_set_feature(mpam_feat_cpor_part, &fake_ris1.props);
	mpam_set_feature(mpam_feat_cpor_part, &fake_ris2.props);
	mpam_set_feature(mpam_feat_mbw_part, &fake_ris1.props);
	mpam_set_feature(mpam_feat_mbw_part, &fake_ris2.props);
	fake_ris1.props.cpbm_wd = 5;
	fake_ris2.props.cpbm_wd = 3;
	fake_ris1.props.mbw_pbm_bits = 5;
	fake_ris2.props.mbw_pbm_bits = 3;

	mpam_enable_merge_features(&fake_classes_list);

	/*
	 * Multiple RIS in different MSC can't control the same resource,
	 * mismatched features can not be supported.
	 */
	KUNIT_EXPECT_FALSE(test, mpam_has_feature(mpam_feat_cpor_part, &fake_class.props));
	KUNIT_EXPECT_FALSE(test, mpam_has_feature(mpam_feat_mbw_part, &fake_class.props));
	KUNIT_EXPECT_EQ(test, fake_class.props.cpbm_wd, 0);
	KUNIT_EXPECT_EQ(test, fake_class.props.mbw_pbm_bits, 0);

	reset_fake_hierarchy();

	/* One Class+Comp, two MSC with overlapping features that need tweaking */
	fake_comp1.class = &fake_class;
	list_add(&fake_comp1.class_list, &fake_class.components);
	fake_comp2.class = NULL;
	fake_vmsc1.comp = &fake_comp1;
	list_add(&fake_vmsc1.comp_list, &fake_comp1.vmsc);
	fake_vmsc2.comp = &fake_comp1;
	list_add(&fake_vmsc2.comp_list, &fake_comp1.vmsc);
	fake_ris1.vmsc = &fake_vmsc1;
	list_add(&fake_ris1.vmsc_list, &fake_vmsc1.ris);
	fake_ris2.vmsc = &fake_vmsc2;
	list_add(&fake_ris2.vmsc_list, &fake_vmsc2.ris);

	mpam_set_feature(mpam_feat_mbw_min, &fake_ris1.props);
	mpam_set_feature(mpam_feat_mbw_min, &fake_ris2.props);
	mpam_set_feature(mpam_feat_cmax_cmax, &fake_ris1.props);
	mpam_set_feature(mpam_feat_cmax_cmax, &fake_ris2.props);
	fake_ris1.props.bwa_wd = 5;
	fake_ris2.props.bwa_wd = 3;
	fake_ris1.props.cmax_wd = 5;
	fake_ris2.props.cmax_wd = 3;

	mpam_enable_merge_features(&fake_classes_list);

	/*
	 * RIS with different control properties need to be sanitised so the
	 * class has the common set of properties.
	 */
	KUNIT_EXPECT_TRUE(test, mpam_has_feature(mpam_feat_mbw_min, &fake_class.props));
	KUNIT_EXPECT_TRUE(test, mpam_has_feature(mpam_feat_cmax_cmax, &fake_class.props));
	KUNIT_EXPECT_EQ(test, fake_class.props.bwa_wd, 3);
	KUNIT_EXPECT_EQ(test, fake_class.props.cmax_wd, 3);

	reset_fake_hierarchy();

	/* One Class Two Comp with overlapping features */
	fake_comp1.class = &fake_class;
	list_add(&fake_comp1.class_list, &fake_class.components);
	fake_comp2.class = &fake_class;
	list_add(&fake_comp2.class_list, &fake_class.components);
	fake_vmsc1.comp = &fake_comp1;
	list_add(&fake_vmsc1.comp_list, &fake_comp1.vmsc);
	fake_vmsc2.comp = &fake_comp2;
	list_add(&fake_vmsc2.comp_list, &fake_comp2.vmsc);
	fake_ris1.vmsc = &fake_vmsc1;
	list_add(&fake_ris1.vmsc_list, &fake_vmsc1.ris);
	fake_ris2.vmsc = &fake_vmsc2;
	list_add(&fake_ris2.vmsc_list, &fake_vmsc2.ris);

	mpam_set_feature(mpam_feat_cpor_part, &fake_ris1.props);
	mpam_set_feature(mpam_feat_cpor_part, &fake_ris2.props);
	fake_ris1.props.cpbm_wd = 4;
	fake_ris2.props.cpbm_wd = 4;

	mpam_enable_merge_features(&fake_classes_list);

	KUNIT_EXPECT_TRUE(test, mpam_has_feature(mpam_feat_cpor_part, &fake_class.props));
	KUNIT_EXPECT_EQ(test, fake_class.props.cpbm_wd, 4);

	reset_fake_hierarchy();

	/* One Class Two Comp with non-overlapping features */
	fake_comp1.class = &fake_class;
	list_add(&fake_comp1.class_list, &fake_class.components);
	fake_comp2.class = &fake_class;
	list_add(&fake_comp2.class_list, &fake_class.components);
	fake_vmsc1.comp = &fake_comp1;
	list_add(&fake_vmsc1.comp_list, &fake_comp1.vmsc);
	fake_vmsc2.comp = &fake_comp2;
	list_add(&fake_vmsc2.comp_list, &fake_comp2.vmsc);
	fake_ris1.vmsc = &fake_vmsc1;
	list_add(&fake_ris1.vmsc_list, &fake_vmsc1.ris);
	fake_ris2.vmsc = &fake_vmsc2;
	list_add(&fake_ris2.vmsc_list, &fake_vmsc2.ris);

	mpam_set_feature(mpam_feat_cpor_part, &fake_ris1.props);
	mpam_set_feature(mpam_feat_cmax_cmin, &fake_ris2.props);
	fake_ris1.props.cpbm_wd = 4;
	fake_ris2.props.cmax_wd = 4;

	mpam_enable_merge_features(&fake_classes_list);

	/*
	 * Multiple components can't control the same resource, mismatched features can
	 * not be supported.
	 */
	KUNIT_EXPECT_FALSE(test, mpam_has_feature(mpam_feat_cpor_part, &fake_class.props));
	KUNIT_EXPECT_FALSE(test, mpam_has_feature(mpam_feat_cmax_cmin, &fake_class.props));
	KUNIT_EXPECT_EQ(test, fake_class.props.cpbm_wd, 0);
	KUNIT_EXPECT_EQ(test, fake_class.props.cmax_wd, 0);

	mutex_unlock(&mpam_list_lock);
}

static void test_mpam_reset_msc_bitmap(struct kunit *test)
{
	char __iomem *buf = kunit_kzalloc(test, SZ_16K, GFP_KERNEL);
	struct mpam_msc fake_msc = {};
	u32 *test_result;

	if (!buf)
		return;

	fake_msc.mapped_hwpage = buf;
	fake_msc.mapped_hwpage_sz = SZ_16K;
	cpumask_copy(&fake_msc.accessibility, cpu_possible_mask);

	/* Satisfy lockdep checks */
	mutex_init(&fake_msc.part_sel_lock);
	mutex_lock(&fake_msc.part_sel_lock);

	test_result = (u32 *)(buf + MPAMCFG_CPBM);

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 0);
	KUNIT_EXPECT_EQ(test, test_result[0], 0);
	KUNIT_EXPECT_EQ(test, test_result[1], 0);
	test_result[0] = 0;
	test_result[1] = 0;

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 1);
	KUNIT_EXPECT_EQ(test, test_result[0], 1);
	KUNIT_EXPECT_EQ(test, test_result[1], 0);
	test_result[0] = 0;
	test_result[1] = 0;

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 16);
	KUNIT_EXPECT_EQ(test, test_result[0], 0xffff);
	KUNIT_EXPECT_EQ(test, test_result[1], 0);
	test_result[0] = 0;
	test_result[1] = 0;

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 32);
	KUNIT_EXPECT_EQ(test, test_result[0], 0xffffffff);
	KUNIT_EXPECT_EQ(test, test_result[1], 0);
	test_result[0] = 0;
	test_result[1] = 0;

	mpam_reset_msc_bitmap(&fake_msc, MPAMCFG_CPBM, 33);
	KUNIT_EXPECT_EQ(test, test_result[0], 0xffffffff);
	KUNIT_EXPECT_EQ(test, test_result[1], 1);
	test_result[0] = 0;
	test_result[1] = 0;

	mutex_unlock(&fake_msc.part_sel_lock);
}

static struct kunit_case mpam_devices_test_cases[] = {
	KUNIT_CASE(test_mpam_reset_msc_bitmap),
	KUNIT_CASE(test_mpam_enable_merge_features),
	KUNIT_CASE(test__props_mismatch),
	{}
};

static struct kunit_suite mpam_devices_test_suite = {
	.name = "mpam_devices_test_suite",
	.test_cases = mpam_devices_test_cases,
};

kunit_test_suites(&mpam_devices_test_suite);
