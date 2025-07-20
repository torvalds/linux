// SPDX-License-Identifier: GPL-2.0-only
//
// KUnit tests for cs_dsp.
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.
//

#include <kunit/device.h>
#include <kunit/resource.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/build_bug.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/cs_dsp_test_utils.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/random.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#define ADSP2_LOCK_REGION_CTRL               0x7A
#define ADSP2_WDT_TIMEOUT_STS_MASK           0x2000

KUNIT_DEFINE_ACTION_WRAPPER(_put_device_wrapper, put_device, struct device *)
KUNIT_DEFINE_ACTION_WRAPPER(_cs_dsp_remove_wrapper, cs_dsp_remove, struct cs_dsp *)

struct cs_dsp_test_local {
	struct cs_dsp_mock_wmfw_builder *wmfw_builder;

	int num_control_add;
	int num_control_remove;
	int num_pre_run;
	int num_post_run;
	int num_pre_stop;
	int num_post_stop;
	int num_watchdog_expired;

	struct cs_dsp_coeff_ctl *passed_ctl[16];
	struct cs_dsp *passed_dsp;
};

struct cs_dsp_callbacks_test_param {
	const struct cs_dsp_client_ops *ops;
	const char *case_name;
};

static const struct cs_dsp_mock_alg_def cs_dsp_callbacks_test_mock_algs[] = {
	{
		.id = 0xfafa,
		.ver = 0x100000,
		.xm_size_words = 164,
		.ym_size_words = 164,
		.zm_size_words = 164,
	},
};

static const struct cs_dsp_mock_coeff_def mock_coeff_template = {
	.shortname = "Dummy Coeff",
	.type = WMFW_CTL_TYPE_BYTES,
	.mem_type = WMFW_ADSP2_YM,
	.flags = WMFW_CTL_FLAG_VOLATILE,
	.length_bytes = 4,
};

static int cs_dsp_test_control_add_callback(struct cs_dsp_coeff_ctl *ctl)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;

	local->passed_ctl[local->num_control_add] = ctl;
	local->num_control_add++;

	return 0;
}

static void cs_dsp_test_control_remove_callback(struct cs_dsp_coeff_ctl *ctl)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;

	local->passed_ctl[local->num_control_remove] = ctl;
	local->num_control_remove++;
}

static int cs_dsp_test_pre_run_callback(struct cs_dsp *dsp)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;

	local->passed_dsp = dsp;
	local->num_pre_run++;

	return 0;
}

static int cs_dsp_test_post_run_callback(struct cs_dsp *dsp)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;

	local->passed_dsp = dsp;
	local->num_post_run++;

	return 0;
}

static void cs_dsp_test_pre_stop_callback(struct cs_dsp *dsp)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;

	local->passed_dsp = dsp;
	local->num_pre_stop++;
}

static void cs_dsp_test_post_stop_callback(struct cs_dsp *dsp)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;

	local->passed_dsp = dsp;
	local->num_post_stop++;
}

static void cs_dsp_test_watchdog_expired_callback(struct cs_dsp *dsp)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;

	local->passed_dsp = dsp;
	local->num_watchdog_expired++;
}

static const struct cs_dsp_client_ops cs_dsp_callback_test_client_ops = {
	.control_add = cs_dsp_test_control_add_callback,
	.control_remove = cs_dsp_test_control_remove_callback,
	.pre_run = cs_dsp_test_pre_run_callback,
	.post_run = cs_dsp_test_post_run_callback,
	.pre_stop = cs_dsp_test_pre_stop_callback,
	.post_stop = cs_dsp_test_post_stop_callback,
	.watchdog_expired = cs_dsp_test_watchdog_expired_callback,
};

static const struct cs_dsp_client_ops cs_dsp_callback_test_empty_client_ops = {
	/* No entries */
};

static void cs_dsp_test_run_stop_callbacks(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);

	KUNIT_EXPECT_EQ(test, cs_dsp_run(priv->dsp), 0);
	KUNIT_EXPECT_EQ(test, local->num_pre_run, 1);
	KUNIT_EXPECT_EQ(test, local->num_post_run, 1);
	KUNIT_EXPECT_EQ(test, local->num_pre_stop, 0);
	KUNIT_EXPECT_EQ(test, local->num_post_stop, 0);
	KUNIT_EXPECT_PTR_EQ(test, local->passed_dsp, priv->dsp);
	local->passed_dsp = NULL;

	cs_dsp_stop(priv->dsp);
	KUNIT_EXPECT_EQ(test, local->num_pre_run, 1);
	KUNIT_EXPECT_EQ(test, local->num_post_run, 1);
	KUNIT_EXPECT_EQ(test, local->num_pre_stop, 1);
	KUNIT_EXPECT_EQ(test, local->num_post_stop, 1);
	KUNIT_EXPECT_PTR_EQ(test, local->passed_dsp, priv->dsp);
	local->passed_dsp = NULL;

	KUNIT_EXPECT_EQ(test, cs_dsp_run(priv->dsp), 0);
	KUNIT_EXPECT_EQ(test, local->num_pre_run, 2);
	KUNIT_EXPECT_EQ(test, local->num_post_run, 2);
	KUNIT_EXPECT_EQ(test, local->num_pre_stop, 1);
	KUNIT_EXPECT_EQ(test, local->num_post_stop, 1);
	KUNIT_EXPECT_PTR_EQ(test, local->passed_dsp, priv->dsp);
	local->passed_dsp = NULL;

	cs_dsp_stop(priv->dsp);
	KUNIT_EXPECT_EQ(test, local->num_pre_run, 2);
	KUNIT_EXPECT_EQ(test, local->num_post_run, 2);
	KUNIT_EXPECT_EQ(test, local->num_pre_stop, 2);
	KUNIT_EXPECT_EQ(test, local->num_post_stop, 2);
	KUNIT_EXPECT_PTR_EQ(test, local->passed_dsp, priv->dsp);
	local->passed_dsp = NULL;
}

static void cs_dsp_test_ctl_v1_callbacks(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	int i;

	/* Add a control for each memory */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_callbacks_test_mock_algs[0].id,
					      "dummyalg", NULL);
	def.shortname = "zm";
	def.mem_type = WMFW_ADSP2_ZM;
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	def.shortname = "ym";
	def.mem_type = WMFW_ADSP2_YM;
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	def.shortname = "xm";
	def.mem_type = WMFW_ADSP2_XM;
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);

	/* There should have been an add callback for each control */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list), 3);
	KUNIT_EXPECT_EQ(test, local->num_control_add, 3);
	KUNIT_EXPECT_EQ(test, local->num_control_remove, 0);

	i = 0;
	list_for_each_entry_reverse(ctl, &priv->dsp->ctl_list, list)
		KUNIT_EXPECT_PTR_EQ(test, local->passed_ctl[i++], ctl);

	/*
	 * Call cs_dsp_remove() and there should be a remove callback
	 * for each control
	 */
	memset(local->passed_ctl, 0, sizeof(local->passed_ctl));
	cs_dsp_remove(priv->dsp);

	/* Prevent double cleanup */
	kunit_remove_action(priv->test, _cs_dsp_remove_wrapper, priv->dsp);

	KUNIT_EXPECT_EQ(test, local->num_control_add, 3);
	KUNIT_EXPECT_EQ(test, local->num_control_remove, 3);

	i = 0;
	list_for_each_entry_reverse(ctl, &priv->dsp->ctl_list, list)
		KUNIT_EXPECT_PTR_EQ(test, local->passed_ctl[i++], ctl);
}

static void cs_dsp_test_ctl_v2_callbacks(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	char name[2] = { };
	int i;

	/* Add some controls */
	def.shortname = name;
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_callbacks_test_mock_algs[0].id,
					      "dummyalg", NULL);
	for (i = 0; i < ARRAY_SIZE(local->passed_ctl); ++i) {
		name[0] = 'A' + i;
		def.offset_dsp_words = i;
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	}
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);

	/* There should have been an add callback for each control */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list),
			ARRAY_SIZE(local->passed_ctl));
	KUNIT_EXPECT_EQ(test, local->num_control_add, ARRAY_SIZE(local->passed_ctl));
	KUNIT_EXPECT_EQ(test, local->num_control_remove, 0);

	i = 0;
	list_for_each_entry_reverse(ctl, &priv->dsp->ctl_list, list)
		KUNIT_EXPECT_PTR_EQ(test, local->passed_ctl[i++], ctl);

	/*
	 * Call cs_dsp_remove() and there should be a remove callback
	 * for each control
	 */
	memset(local->passed_ctl, 0, sizeof(local->passed_ctl));
	cs_dsp_remove(priv->dsp);

	/* Prevent double cleanup */
	kunit_remove_action(priv->test, _cs_dsp_remove_wrapper, priv->dsp);

	KUNIT_EXPECT_EQ(test, local->num_control_add, ARRAY_SIZE(local->passed_ctl));
	KUNIT_EXPECT_EQ(test, local->num_control_remove, ARRAY_SIZE(local->passed_ctl));

	i = 0;
	list_for_each_entry_reverse(ctl, &priv->dsp->ctl_list, list)
		KUNIT_EXPECT_PTR_EQ(test, local->passed_ctl[i++], ctl);
}

static void cs_dsp_test_no_callbacks(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct firmware *wmfw;

	/* Add a controls */
	def.shortname = "A";
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_callbacks_test_mock_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Run a sequence of ops that would invoke callbacks */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	KUNIT_EXPECT_EQ(test, cs_dsp_run(priv->dsp), 0);
	cs_dsp_stop(priv->dsp);
	cs_dsp_remove(priv->dsp);

	/* Prevent double cleanup */
	kunit_remove_action(priv->test, _cs_dsp_remove_wrapper, priv->dsp);

	/* Something went very wrong if any of our callbacks were called */
	KUNIT_EXPECT_EQ(test, local->num_control_add, 0);
	KUNIT_EXPECT_EQ(test, local->num_control_remove, 0);
	KUNIT_EXPECT_EQ(test, local->num_pre_run, 0);
	KUNIT_EXPECT_EQ(test, local->num_post_run, 0);
	KUNIT_EXPECT_EQ(test, local->num_pre_stop, 0);
	KUNIT_EXPECT_EQ(test, local->num_post_stop, 0);
}

static void cs_dsp_test_adsp2v2_watchdog_callback(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);

	KUNIT_EXPECT_EQ(test, cs_dsp_run(priv->dsp), 0);

	/* Set the watchdog timeout bit */
	regmap_write(priv->dsp->regmap, priv->dsp->base + ADSP2_LOCK_REGION_CTRL,
		     ADSP2_WDT_TIMEOUT_STS_MASK);

	/* Notify an interrupt and the watchdog callback should be called */
	cs_dsp_adsp2_bus_error(priv->dsp);
	KUNIT_EXPECT_EQ(test, local->num_watchdog_expired, 1);
	KUNIT_EXPECT_PTR_EQ(test, local->passed_dsp, priv->dsp);
}

static void cs_dsp_test_adsp2v2_watchdog_no_callbacks(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	KUNIT_EXPECT_EQ(test, cs_dsp_run(priv->dsp), 0);

	/* Set the watchdog timeout bit */
	regmap_write(priv->dsp->regmap, priv->dsp->base + ADSP2_LOCK_REGION_CTRL,
		     ADSP2_WDT_TIMEOUT_STS_MASK);

	/* Notify an interrupt, which will look for a watchdog callback */
	cs_dsp_adsp2_bus_error(priv->dsp);
	KUNIT_EXPECT_EQ(test, local->num_watchdog_expired, 0);
}

static void cs_dsp_test_halo_watchdog_callback(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);

	KUNIT_EXPECT_EQ(test, cs_dsp_run(priv->dsp), 0);

	/* Notify an interrupt and the watchdog callback should be called */
	cs_dsp_halo_wdt_expire(priv->dsp);
	KUNIT_EXPECT_EQ(test, local->num_watchdog_expired, 1);
	KUNIT_EXPECT_PTR_EQ(test, local->passed_dsp, priv->dsp);
}

static void cs_dsp_test_halo_watchdog_no_callbacks(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	KUNIT_EXPECT_EQ(test, cs_dsp_run(priv->dsp), 0);

	/* Notify an interrupt, which will look for a watchdog callback */
	cs_dsp_halo_wdt_expire(priv->dsp);
	KUNIT_EXPECT_EQ(test, local->num_watchdog_expired, 0);
}

static int cs_dsp_callbacks_test_common_init(struct kunit *test, struct cs_dsp *dsp,
					     int wmfw_version)
{
	const struct cs_dsp_callbacks_test_param *param = test->param_value;
	struct cs_dsp_test *priv;
	struct cs_dsp_test_local *local;
	struct device *test_dev;
	struct cs_dsp_mock_xm_header *xm_header;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	local = kunit_kzalloc(test, sizeof(struct cs_dsp_test_local), GFP_KERNEL);
	if (!local)
		return -ENOMEM;

	priv->test = test;
	priv->dsp = dsp;
	test->priv = priv;
	priv->local = local;

	/* Create dummy struct device */
	test_dev = kunit_device_register(test, "cs_dsp_test_drv");
	if (IS_ERR(test_dev))
		return PTR_ERR(test_dev);

	dsp->dev = get_device(test_dev);
	if (!dsp->dev)
		return -ENODEV;

	ret = kunit_add_action_or_reset(test, _put_device_wrapper, dsp->dev);
	if (ret)
		return ret;

	dev_set_drvdata(dsp->dev, priv);

	/* Allocate regmap */
	ret = cs_dsp_mock_regmap_init(priv);
	if (ret)
		return ret;

	/*
	 * There must always be a XM header with at least 1 algorithm,
	 * so create a dummy one and pre-populate XM so the wmfw doesn't
	 * have to contain an XM blob.
	 */
	xm_header = cs_dsp_create_mock_xm_header(priv,
						 cs_dsp_callbacks_test_mock_algs,
						 ARRAY_SIZE(cs_dsp_callbacks_test_mock_algs));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xm_header);
	cs_dsp_mock_xm_header_write_to_regmap(xm_header);

	local->wmfw_builder = cs_dsp_mock_wmfw_init(priv, wmfw_version);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->wmfw_builder);

	/* Add dummy XM header payload to wmfw */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_ADSP2_XM, 0,
					xm_header->blob_data,
					xm_header->blob_size_bytes);

	/* Init cs_dsp */
	dsp->client_ops = param->ops;

	switch (dsp->type) {
	case WMFW_ADSP2:
		ret = cs_dsp_adsp2_init(dsp);
		break;
	case WMFW_HALO:
		ret = cs_dsp_halo_init(dsp);
		break;
	default:
		KUNIT_FAIL(test, "Untested DSP type %d\n", dsp->type);
		return -EINVAL;
	}

	if (ret)
		return ret;

	/* Automatically call cs_dsp_remove() when test case ends */
	return kunit_add_action_or_reset(priv->test, _cs_dsp_remove_wrapper, dsp);
}

static int cs_dsp_callbacks_test_halo_init(struct kunit *test)
{
	struct cs_dsp *dsp;

	/* Fill in cs_dsp and initialize */
	dsp = kunit_kzalloc(test, sizeof(*dsp), GFP_KERNEL);
	if (!dsp)
		return -ENOMEM;

	dsp->num = 1;
	dsp->type = WMFW_HALO;
	dsp->mem = cs_dsp_mock_halo_dsp1_regions;
	dsp->num_mems = cs_dsp_mock_count_regions(cs_dsp_mock_halo_dsp1_region_sizes);
	dsp->base = cs_dsp_mock_halo_core_base;
	dsp->base_sysinfo = cs_dsp_mock_halo_sysinfo_base;

	return cs_dsp_callbacks_test_common_init(test, dsp, 3);
}

static int cs_dsp_callbacks_test_adsp2_32bit_init(struct kunit *test, int rev)
{
	struct cs_dsp *dsp;

	/* Fill in cs_dsp and initialize */
	dsp = kunit_kzalloc(test, sizeof(*dsp), GFP_KERNEL);
	if (!dsp)
		return -ENOMEM;

	dsp->num = 1;
	dsp->type = WMFW_ADSP2;
	dsp->rev = rev;
	dsp->mem = cs_dsp_mock_adsp2_32bit_dsp1_regions;
	dsp->num_mems = cs_dsp_mock_count_regions(cs_dsp_mock_adsp2_32bit_dsp1_region_sizes);
	dsp->base = cs_dsp_mock_adsp2_32bit_sysbase;

	return cs_dsp_callbacks_test_common_init(test, dsp, 2);
}

static int cs_dsp_callbacks_test_adsp2v2_32bit_init(struct kunit *test)
{
	return cs_dsp_callbacks_test_adsp2_32bit_init(test, 2);
}

static int cs_dsp_callbacks_test_adsp2v1_32bit_init(struct kunit *test)
{
	return cs_dsp_callbacks_test_adsp2_32bit_init(test, 1);
}

static int cs_dsp_callbacks_test_adsp2_16bit_init(struct kunit *test)
{
	struct cs_dsp *dsp;

	/* Fill in cs_dsp and initialize */
	dsp = kunit_kzalloc(test, sizeof(*dsp), GFP_KERNEL);
	if (!dsp)
		return -ENOMEM;

	dsp->num = 1;
	dsp->type = WMFW_ADSP2;
	dsp->rev = 0;
	dsp->mem = cs_dsp_mock_adsp2_16bit_dsp1_regions;
	dsp->num_mems = cs_dsp_mock_count_regions(cs_dsp_mock_adsp2_16bit_dsp1_region_sizes);
	dsp->base = cs_dsp_mock_adsp2_16bit_sysbase;

	return cs_dsp_callbacks_test_common_init(test, dsp, 1);
}

static void cs_dsp_callbacks_param_desc(const struct cs_dsp_callbacks_test_param *param,
					char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s", param->case_name);
}

/* Parameterize on different client callback ops tables */
static const struct cs_dsp_callbacks_test_param cs_dsp_callbacks_ops_cases[] = {
	{ .ops =  &cs_dsp_callback_test_client_ops, .case_name = "all ops" },
};

KUNIT_ARRAY_PARAM(cs_dsp_callbacks_ops,
		  cs_dsp_callbacks_ops_cases,
		  cs_dsp_callbacks_param_desc);

static const struct cs_dsp_callbacks_test_param cs_dsp_no_callbacks_cases[] = {
	{ .ops =  &cs_dsp_callback_test_empty_client_ops, .case_name = "empty ops" },
};

KUNIT_ARRAY_PARAM(cs_dsp_no_callbacks,
		  cs_dsp_no_callbacks_cases,
		  cs_dsp_callbacks_param_desc);

static struct kunit_case cs_dsp_callbacks_adsp2_wmfwv1_test_cases[] = {
	KUNIT_CASE_PARAM(cs_dsp_test_run_stop_callbacks, cs_dsp_callbacks_ops_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_test_ctl_v1_callbacks, cs_dsp_callbacks_ops_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_test_no_callbacks, cs_dsp_no_callbacks_gen_params),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_callbacks_adsp2_wmfwv2_test_cases[] = {
	KUNIT_CASE_PARAM(cs_dsp_test_run_stop_callbacks, cs_dsp_callbacks_ops_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_test_ctl_v2_callbacks, cs_dsp_callbacks_ops_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_test_no_callbacks, cs_dsp_no_callbacks_gen_params),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_callbacks_halo_test_cases[] = {
	KUNIT_CASE_PARAM(cs_dsp_test_run_stop_callbacks, cs_dsp_callbacks_ops_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_test_ctl_v2_callbacks, cs_dsp_callbacks_ops_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_test_no_callbacks, cs_dsp_no_callbacks_gen_params),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_watchdog_adsp2v2_test_cases[] = {
	KUNIT_CASE_PARAM(cs_dsp_test_adsp2v2_watchdog_callback, cs_dsp_callbacks_ops_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_test_adsp2v2_watchdog_no_callbacks, cs_dsp_no_callbacks_gen_params),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_watchdog_halo_test_cases[] = {
	KUNIT_CASE_PARAM(cs_dsp_test_halo_watchdog_callback, cs_dsp_callbacks_ops_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_test_halo_watchdog_no_callbacks, cs_dsp_no_callbacks_gen_params),

	{ } /* terminator */
};

static struct kunit_suite cs_dsp_callbacks_test_halo = {
	.name = "cs_dsp_callbacks_halo",
	.init = cs_dsp_callbacks_test_halo_init,
	.test_cases = cs_dsp_callbacks_halo_test_cases,
};

static struct kunit_suite cs_dsp_callbacks_test_adsp2v2_32bit = {
	.name = "cs_dsp_callbacks_adsp2v2_32bit_wmfwv2",
	.init = cs_dsp_callbacks_test_adsp2v2_32bit_init,
	.test_cases = cs_dsp_callbacks_adsp2_wmfwv2_test_cases,
};

static struct kunit_suite cs_dsp_callbacks_test_adsp2v1_32bit = {
	.name = "cs_dsp_callbacks_adsp2v1_32bit_wmfwv2",
	.init = cs_dsp_callbacks_test_adsp2v1_32bit_init,
	.test_cases = cs_dsp_callbacks_adsp2_wmfwv2_test_cases,
};

static struct kunit_suite cs_dsp_callbacks_test_adsp2_16bit = {
	.name = "cs_dsp_callbacks_adsp2_16bit_wmfwv1",
	.init = cs_dsp_callbacks_test_adsp2_16bit_init,
	.test_cases = cs_dsp_callbacks_adsp2_wmfwv1_test_cases,
};

static struct kunit_suite cs_dsp_watchdog_test_adsp2v2_32bit = {
	.name = "cs_dsp_watchdog_adsp2v2_32bit",
	.init = cs_dsp_callbacks_test_adsp2v2_32bit_init,
	.test_cases = cs_dsp_watchdog_adsp2v2_test_cases,
};

static struct kunit_suite cs_dsp_watchdog_test_halo_32bit = {
	.name = "cs_dsp_watchdog_halo",
	.init = cs_dsp_callbacks_test_halo_init,
	.test_cases = cs_dsp_watchdog_halo_test_cases,
};

kunit_test_suites(&cs_dsp_callbacks_test_halo,
		  &cs_dsp_callbacks_test_adsp2v2_32bit,
		  &cs_dsp_callbacks_test_adsp2v1_32bit,
		  &cs_dsp_callbacks_test_adsp2_16bit,
		  &cs_dsp_watchdog_test_adsp2v2_32bit,
		  &cs_dsp_watchdog_test_halo_32bit);
