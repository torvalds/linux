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
#include <linux/build_bug.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/cs_dsp_test_utils.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/random.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

KUNIT_DEFINE_ACTION_WRAPPER(_put_device_wrapper, put_device, struct device *);
KUNIT_DEFINE_ACTION_WRAPPER(_cs_dsp_remove_wrapper, cs_dsp_remove, struct cs_dsp *);

struct cs_dsp_test_local {
	struct cs_dsp_mock_bin_builder *bin_builder;
	struct cs_dsp_mock_xm_header *xm_header;
	struct cs_dsp_mock_wmfw_builder *wmfw_builder;
	struct firmware *wmfw;
	int wmfw_version;
};

struct cs_dsp_bin_test_param {
	int block_type;
};

static const struct cs_dsp_mock_alg_def cs_dsp_bin_err_test_mock_algs[] = {
	{
		.id = 0xfafa,
		.ver = 0x100000,
		.xm_size_words = 164,
		.ym_size_words = 164,
		.zm_size_words = 164,
	},
};

/* Load a bin containing unknown blocks. They should be skipped. */
static void bin_load_with_unknown_blocks(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *bin;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	u8 random_data[8];
	const unsigned int payload_size_bytes = 64;

	payload_data = kunit_kmalloc(test, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);
	get_random_bytes(payload_data, payload_size_bytes);

	readback = kunit_kzalloc(test, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Add some unknown blocks at the start of the bin */
	get_random_bytes(random_data, sizeof(random_data));
	cs_dsp_mock_bin_add_raw_block(local->bin_builder,
				      cs_dsp_bin_err_test_mock_algs[0].id,
				      cs_dsp_bin_err_test_mock_algs[0].ver,
				      0xf5, 0,
				      random_data, sizeof(random_data));
	cs_dsp_mock_bin_add_raw_block(local->bin_builder,
				      cs_dsp_bin_err_test_mock_algs[0].id,
				      cs_dsp_bin_err_test_mock_algs[0].ver,
				      0xf500, 0,
				      random_data, sizeof(random_data));
	cs_dsp_mock_bin_add_raw_block(local->bin_builder,
				      cs_dsp_bin_err_test_mock_algs[0].id,
				      cs_dsp_bin_err_test_mock_algs[0].ver,
				      0xc300, 0,
				      random_data, sizeof(random_data));

	/* Add a single payload to be written to DSP memory */
	cs_dsp_mock_bin_add_raw_block(local->bin_builder,
				      cs_dsp_bin_err_test_mock_algs[0].id,
				      cs_dsp_bin_err_test_mock_algs[0].ver,
				      WMFW_ADSP2_YM, 0,
				      payload_data, payload_size_bytes);

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	/* Check that the payload was written to memory */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, payload_size_bytes);
}

/* Load a bin that doesn't have a valid magic marker. */
static void bin_err_wrong_magic(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *bin;

	/* Sanity-check that the wmfw loads ok without the bin */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);

	memcpy((void *)bin->data, "WMFW", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	memcpy((void *)bin->data, "xMDR", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	memcpy((void *)bin->data, "WxDR", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	memcpy((void *)bin->data, "WMxR", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	memcpy((void *)bin->data, "WMDx", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	memset((void *)bin->data, 0, 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);
}

/* Load a bin that is too short for a valid header. */
static void bin_err_too_short_for_header(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *bin;

	/* Sanity-check that the wmfw loads ok without the bin */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);
	do {
		bin->size--;

		KUNIT_EXPECT_LT(test,
				cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
				0);
	} while (bin->size > 0);
}

/* Header length field isn't a valid header length. */
static void bin_err_bad_header_length(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *bin;
	struct wmfw_coeff_hdr *header;
	unsigned int real_len, len;

	/* Sanity-check that the wmfw loads ok without the bin */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);
	header = (struct wmfw_coeff_hdr *)bin->data;
	real_len = le32_to_cpu(header->len);

	for (len = 0; len < real_len; len++) {
		header->len = cpu_to_le32(len);
		KUNIT_EXPECT_LT(test,
				cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
				0);
	}

	for (len = real_len + 1; len < real_len + 7; len++) {
		header->len = cpu_to_le32(len);
		KUNIT_EXPECT_LT(test,
				cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
				0);
	}

	header->len = cpu_to_le32(0xffffffff);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	header->len = cpu_to_le32(0x80000000);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	header->len = cpu_to_le32(0x7fffffff);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);
}

/* Wrong core type in header. */
static void bin_err_bad_core_type(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *bin;
	struct wmfw_coeff_hdr *header;

	/* Sanity-check that the wmfw loads ok without the bin */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);
	header = (struct wmfw_coeff_hdr *)bin->data;

	header->core_ver = cpu_to_le32(0);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	header->core_ver = cpu_to_le32(1);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	header->core_ver = cpu_to_le32(priv->dsp->type + 1);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	header->core_ver = cpu_to_le32(0xff);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);
}

/* File too short to contain a full block header */
static void bin_too_short_for_block_header(struct kunit *test)
{
	const struct cs_dsp_bin_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *bin;
	unsigned int header_length;

	/* Sanity-check that the wmfw loads ok without the bin */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);
	header_length = bin->size;
	kunit_kfree(test, bin);

	cs_dsp_mock_bin_add_raw_block(local->bin_builder,
				      cs_dsp_bin_err_test_mock_algs[0].id,
				      cs_dsp_bin_err_test_mock_algs[0].ver,
				      param->block_type, 0,
				      NULL, 0);

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);
	KUNIT_ASSERT_GT(test, bin->size, header_length);

	for (bin->size--; bin->size > header_length; bin->size--) {
		KUNIT_EXPECT_LT(test,
				cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
				0);
	}
}

/* File too short to contain the block payload */
static void bin_too_short_for_block_payload(struct kunit *test)
{
	const struct cs_dsp_bin_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *bin;
	static const u8 payload[256] = { };
	int i;

	/* Sanity-check that the wmfw loads ok without the bin */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	cs_dsp_mock_bin_add_raw_block(local->bin_builder,
				      cs_dsp_bin_err_test_mock_algs[0].id,
				      cs_dsp_bin_err_test_mock_algs[0].ver,
				      param->block_type, 0,
				      payload, sizeof(payload));

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);
	for (i = 0; i < sizeof(payload); i++) {
		bin->size--;
		KUNIT_EXPECT_LT(test,
				cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
				0);
	}
}

/* Block payload length is a garbage value */
static void bin_block_payload_len_garbage(struct kunit *test)
{
	const struct cs_dsp_bin_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *bin;
	struct wmfw_coeff_hdr *header;
	struct wmfw_coeff_item *block;
	u32 payload = 0;

	/* Sanity-check that the wmfw loads ok without the bin */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	cs_dsp_mock_bin_add_raw_block(local->bin_builder,
				      cs_dsp_bin_err_test_mock_algs[0].id,
				      cs_dsp_bin_err_test_mock_algs[0].ver,
				      param->block_type, 0,
				      &payload, sizeof(payload));

	bin = cs_dsp_mock_bin_get_firmware(local->bin_builder);
	header = (struct wmfw_coeff_hdr *)bin->data;
	block = (struct wmfw_coeff_item *)&bin->data[le32_to_cpu(header->len)];

	/* Sanity check that we're looking at the correct part of the bin */
	KUNIT_ASSERT_EQ(test, le16_to_cpu(block->type), param->block_type);
	KUNIT_ASSERT_EQ(test, le32_to_cpu(block->len), sizeof(payload));

	block->len = cpu_to_le32(0x8000);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	block->len = cpu_to_le32(0xffff);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	block->len = cpu_to_le32(0x7fffffff);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	block->len = cpu_to_le32(0x80000000);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);

	block->len = cpu_to_le32(0xffffffff);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, local->wmfw, "wmfw", bin, "bin", "misc"),
			0);
}

static void cs_dsp_bin_err_test_exit(struct kunit *test)
{
	/*
	 * Testing error conditions can produce a lot of log output
	 * from cs_dsp error messages, so rate limit the test cases.
	 */
	usleep_range(200, 500);
}

static int cs_dsp_bin_err_test_common_init(struct kunit *test, struct cs_dsp *dsp,
					   int wmfw_version)
{
	struct cs_dsp_test *priv;
	struct cs_dsp_test_local *local;
	struct device *test_dev;
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
	priv->local->wmfw_version = wmfw_version;

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
	 * There must always be a XM header with at least 1 algorithm, so create
	 * a dummy one that tests can use and extract it to a data payload.
	 */
	local->xm_header = cs_dsp_create_mock_xm_header(priv,
							cs_dsp_bin_err_test_mock_algs,
							ARRAY_SIZE(cs_dsp_bin_err_test_mock_algs));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->xm_header);

	local->wmfw_builder = cs_dsp_mock_wmfw_init(priv, priv->local->wmfw_version);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->wmfw_builder);

	/* Add dummy XM header payload to wmfw */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_ADSP2_XM, 0,
					local->xm_header->blob_data,
					local->xm_header->blob_size_bytes);

	local->wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	local->bin_builder =
		cs_dsp_mock_bin_init(priv, 1,
				     cs_dsp_mock_xm_header_get_fw_version_from_regmap(priv));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->bin_builder);

	/* Init cs_dsp */
	dsp->client_ops = kunit_kzalloc(test, sizeof(*dsp->client_ops), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dsp->client_ops);

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

static int cs_dsp_bin_err_test_halo_init(struct kunit *test)
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

	return cs_dsp_bin_err_test_common_init(test, dsp, 3);
}

static int cs_dsp_bin_err_test_adsp2_32bit_init(struct kunit *test)
{
	struct cs_dsp *dsp;

	/* Fill in cs_dsp and initialize */
	dsp = kunit_kzalloc(test, sizeof(*dsp), GFP_KERNEL);
	if (!dsp)
		return -ENOMEM;

	dsp->num = 1;
	dsp->type = WMFW_ADSP2;
	dsp->rev = 1;
	dsp->mem = cs_dsp_mock_adsp2_32bit_dsp1_regions;
	dsp->num_mems = cs_dsp_mock_count_regions(cs_dsp_mock_adsp2_32bit_dsp1_region_sizes);
	dsp->base = cs_dsp_mock_adsp2_32bit_sysbase;

	return cs_dsp_bin_err_test_common_init(test, dsp, 2);
}

static int cs_dsp_bin_err_test_adsp2_16bit_init(struct kunit *test)
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

	return cs_dsp_bin_err_test_common_init(test, dsp, 1);
}

static struct kunit_case cs_dsp_bin_err_test_cases_halo[] = {

	{ } /* terminator */
};

static void cs_dsp_bin_err_block_types_desc(const struct cs_dsp_bin_test_param *param,
					    char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "block_type:%#x", param->block_type);
}

/* Some block types to test against, including illegal types */
static const struct cs_dsp_bin_test_param bin_test_block_types_cases[] = {
	{ .block_type = WMFW_INFO_TEXT << 8 },
	{ .block_type = WMFW_METADATA << 8 },
	{ .block_type = WMFW_ADSP2_PM },
	{ .block_type = WMFW_ADSP2_XM },
	{ .block_type = 0x33 },
	{ .block_type = 0xf500 },
	{ .block_type = 0xc000 },
};

KUNIT_ARRAY_PARAM(bin_test_block_types,
		  bin_test_block_types_cases,
		  cs_dsp_bin_err_block_types_desc);

static struct kunit_case cs_dsp_bin_err_test_cases_adsp2[] = {
	KUNIT_CASE(bin_load_with_unknown_blocks),
	KUNIT_CASE(bin_err_wrong_magic),
	KUNIT_CASE(bin_err_too_short_for_header),
	KUNIT_CASE(bin_err_bad_header_length),
	KUNIT_CASE(bin_err_bad_core_type),

	KUNIT_CASE_PARAM(bin_too_short_for_block_header, bin_test_block_types_gen_params),
	KUNIT_CASE_PARAM(bin_too_short_for_block_payload, bin_test_block_types_gen_params),
	KUNIT_CASE_PARAM(bin_block_payload_len_garbage, bin_test_block_types_gen_params),

	{ } /* terminator */
};

static struct kunit_suite cs_dsp_bin_err_test_halo = {
	.name = "cs_dsp_bin_err_halo",
	.init = cs_dsp_bin_err_test_halo_init,
	.exit = cs_dsp_bin_err_test_exit,
	.test_cases = cs_dsp_bin_err_test_cases_halo,
};

static struct kunit_suite cs_dsp_bin_err_test_adsp2_32bit = {
	.name = "cs_dsp_bin_err_adsp2_32bit",
	.init = cs_dsp_bin_err_test_adsp2_32bit_init,
	.exit = cs_dsp_bin_err_test_exit,
	.test_cases = cs_dsp_bin_err_test_cases_adsp2,
};

static struct kunit_suite cs_dsp_bin_err_test_adsp2_16bit = {
	.name = "cs_dsp_bin_err_adsp2_16bit",
	.init = cs_dsp_bin_err_test_adsp2_16bit_init,
	.exit = cs_dsp_bin_err_test_exit,
	.test_cases = cs_dsp_bin_err_test_cases_adsp2,
};

kunit_test_suites(&cs_dsp_bin_err_test_halo,
		  &cs_dsp_bin_err_test_adsp2_32bit,
		  &cs_dsp_bin_err_test_adsp2_16bit);
