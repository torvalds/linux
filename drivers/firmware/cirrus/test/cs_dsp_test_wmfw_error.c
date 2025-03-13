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
	struct cs_dsp_mock_xm_header *xm_header;
	struct cs_dsp_mock_wmfw_builder *wmfw_builder;
	int wmfw_version;
};

struct cs_dsp_wmfw_test_param {
	int block_type;
};

static const struct cs_dsp_mock_alg_def cs_dsp_wmfw_err_test_mock_algs[] = {
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

/* Load a wmfw containing unknown blocks. They should be skipped. */
static void wmfw_load_with_unknown_blocks(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	u8 random_data[8];
	const unsigned int payload_size_bytes = 64;

	/* Add dummy XM header payload to wmfw */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_ADSP2_XM, 0,
					local->xm_header->blob_data,
					local->xm_header->blob_size_bytes);

	payload_data = kunit_kmalloc(test, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);
	get_random_bytes(payload_data, payload_size_bytes);

	readback = kunit_kzalloc(test, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Add some unknown blocks at the start of the wmfw */
	get_random_bytes(random_data, sizeof(random_data));
	cs_dsp_mock_wmfw_add_raw_block(local->wmfw_builder, 0xf5, 0,
				       random_data, sizeof(random_data));
	cs_dsp_mock_wmfw_add_raw_block(local->wmfw_builder, 0xc0, 0, random_data,
				       sizeof(random_data));
	cs_dsp_mock_wmfw_add_raw_block(local->wmfw_builder, 0x33, 0, NULL, 0);

	/* Add a single payload to be written to DSP memory */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_ADSP2_YM, 0,
					payload_data, payload_size_bytes);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	/* Check that the payload was written to memory */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, payload_size_bytes);
}

/* Load a wmfw that doesn't have a valid magic marker. */
static void wmfw_err_wrong_magic(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	memcpy((void *)wmfw->data, "WMDR", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	memcpy((void *)wmfw->data, "xMFW", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	memcpy((void *)wmfw->data, "WxFW", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	memcpy((void *)wmfw->data, "WMxW", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	memcpy((void *)wmfw->data, "WMFx", 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	memset((void *)wmfw->data, 0, 4);
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
}

/* Load a wmfw that is too short for a valid header. */
static void wmfw_err_too_short_for_header(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	do {
		wmfw->size--;

		KUNIT_EXPECT_EQ(test,
				cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
				-EOVERFLOW);
	} while (wmfw->size > 0);
}

/* Header length field isn't a valid header length. */
static void wmfw_err_bad_header_length(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	unsigned int real_len, len;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	real_len = le32_to_cpu(header->len);

	for (len = 0; len < real_len; len++) {
		header->len = cpu_to_le32(len);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
				-EOVERFLOW);
	}

	for (len = real_len + 1; len < real_len + 7; len++) {
		header->len = cpu_to_le32(len);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
				-EOVERFLOW);
	}

	header->len = cpu_to_le32(0xffffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	header->len = cpu_to_le32(0x80000000);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	header->len = cpu_to_le32(0x7fffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* Wrong core type in header. */
static void wmfw_err_bad_core_type(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;

	header->core = 0;
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	header->core = 1;
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	header->core = priv->dsp->type + 1;
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	header->core = 0xff;
	KUNIT_EXPECT_LT(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
}

/* File too short to contain a full block header */
static void wmfw_too_short_for_block_header(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int header_length;
	u32 dummy_payload = 0;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	header_length = wmfw->size;
	kunit_kfree(test, wmfw);

	/* Add the block. A block must have at least 4 bytes of payload */
	cs_dsp_mock_wmfw_add_raw_block(local->wmfw_builder, param->block_type, 0,
				       &dummy_payload, sizeof(dummy_payload));

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_ASSERT_GT(test, wmfw->size, header_length);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	for (wmfw->size--; wmfw->size > header_length; wmfw->size--) {
		KUNIT_EXPECT_EQ(test,
				cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
				-EOVERFLOW);
	}
}

/* File too short to contain the block payload */
static void wmfw_too_short_for_block_payload(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	static const u8 payload[256] = { };
	int i;

	cs_dsp_mock_wmfw_add_raw_block(local->wmfw_builder, param->block_type, 0,
				       payload, sizeof(payload));

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	for (i = 0; i < sizeof(payload); i++) {
		wmfw->size--;
		KUNIT_EXPECT_EQ(test,
				cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
				-EOVERFLOW);
	}
}

/* Block payload length is a garbage value */
static void wmfw_block_payload_len_garbage(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	u32 payload = 0;


	cs_dsp_mock_wmfw_add_raw_block(local->wmfw_builder, param->block_type, 0,
				       &payload, sizeof(payload));

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];

	/* Sanity check that we're looking at the correct part of the wmfw */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(region->offset) >> 24, param->block_type);
	KUNIT_ASSERT_EQ(test, le32_to_cpu(region->len), sizeof(payload));

	region->len = cpu_to_le32(0x8000);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	region->len = cpu_to_le32(0xffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	region->len = cpu_to_le32(0x7fffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	region->len = cpu_to_le32(0x80000000);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	region->len = cpu_to_le32(0xffffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* File too short to contain an algorithm header */
static void wmfw_too_short_for_alg_header(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int header_length;

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	header_length = wmfw->size;
	kunit_kfree(test, wmfw);

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      NULL, NULL);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_ASSERT_GT(test, wmfw->size, header_length);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	for (wmfw->size--; wmfw->size > header_length; wmfw->size--) {
		KUNIT_EXPECT_EQ(test,
				cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
				-EOVERFLOW);
	}
}

/* V1 algorithm name does not have NUL terminator */
static void wmfw_v1_alg_name_unterminated(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	struct wmfw_adsp_alg_data *alg_data;
	struct cs_dsp_coeff_ctl *ctl;

	/* Create alg info block with a coefficient */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &mock_coeff_template);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (struct wmfw_adsp_alg_data *)region->data;

	/* Sanity check we're pointing at the alg header */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data->id), cs_dsp_wmfw_err_test_mock_algs[0].id);

	/* Write a string to the alg name that overflows the array */
	memset(alg_data->descr, 0, sizeof(alg_data->descr));
	memset(alg_data->name, 'A', sizeof(alg_data->name));
	memset(alg_data->descr, 'A', sizeof(alg_data->descr) - 1);

	/*
	 * Sanity-check that a strlen would overflow alg_data->name.
	 * FORTIFY_STRING obstructs testing what strlen() would actually
	 * return, so instead verify that a strnlen() returns
	 * sizeof(alg_data->name[]), therefore it doesn't have a NUL.
	 */
	KUNIT_ASSERT_EQ(test, strnlen(alg_data->name, sizeof(alg_data->name)),
			sizeof(alg_data->name));

	/*
	 * The alg name isn't stored, but cs_dsp parses the name field.
	 * It should load the file successfully and create the control.
	 * If FORTIFY_STRING is enabled it will detect a buffer overflow
	 * if cs_dsp string length walks past end of alg name array.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, 0);
}

/* V2+ algorithm name exceeds length of containing block */
static void wmfw_v2_alg_name_exceeds_block(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	__le32 *alg_data;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", NULL);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (__force __le32 *)region->data;

	/*
	 * Sanity check we're pointing at the alg header of
	 *   [     alg_id       ][name_len]abc
	 */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[0]), cs_dsp_wmfw_err_test_mock_algs[0].id);
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[1]), 3 | ('a' << 8) | ('b' << 16) | ('c' << 24));
	KUNIT_ASSERT_EQ(test, *(u8 *)&alg_data[1], 3);

	/* Set name string length longer than available space */
	*(u8 *)&alg_data[1] = 4;
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*(u8 *)&alg_data[1] = 7;
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*(u8 *)&alg_data[1] = 0x80;
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*(u8 *)&alg_data[1] = 0xff;
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* V2+ algorithm description exceeds length of containing block */
static void wmfw_v2_alg_description_exceeds_block(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	__le32 *alg_data;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (__force __le32 *)region->data;

	/*
	 * Sanity check we're pointing at the alg header of
	 *   [     alg_id       ][name_len]abc[desc_len]de
	 */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[0]), cs_dsp_wmfw_err_test_mock_algs[0].id);
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[2]), 2 | ('d' << 16) | ('e' << 24));
	KUNIT_ASSERT_EQ(test, le16_to_cpu(*(__le16 *)&alg_data[2]), 2);

	/* Set name string length longer than available space */
	*(__le16 *)&alg_data[2] = cpu_to_le16(4);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*(__le16 *)&alg_data[2] = cpu_to_le16(7);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*(__le16 *)&alg_data[2] = cpu_to_le16(0x80);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*(__le16 *)&alg_data[2] = cpu_to_le16(0xff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*(__le16 *)&alg_data[2] = cpu_to_le16(0x8000);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*(__le16 *)&alg_data[2] = cpu_to_le16(0xffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* V1 coefficient count exceeds length of containing block */
static void wmfw_v1_coeff_count_exceeds_block(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	struct wmfw_adsp_alg_data *alg_data;

	/* Create alg info block with a coefficient */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &mock_coeff_template);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (struct wmfw_adsp_alg_data *)region->data;

	/* Sanity check we're pointing at the alg header */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data->id), cs_dsp_wmfw_err_test_mock_algs[0].id);

	/* Add one to the coefficient count */
	alg_data->ncoeff = cpu_to_le32(le32_to_cpu(alg_data->ncoeff) + 1);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	/* Make the coefficient count garbage */
	alg_data->ncoeff = cpu_to_le32(0xffffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	alg_data->ncoeff = cpu_to_le32(0x7fffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	alg_data->ncoeff = cpu_to_le32(0x80000000);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* V2+ coefficient count exceeds length of containing block */
static void wmfw_v2_coeff_count_exceeds_block(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	__le32 *alg_data, *ncoeff;

	/* Create alg info block with a coefficient */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &mock_coeff_template);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (__force __le32 *)region->data;

	/* Sanity check we're pointing at the alg header */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[0]), cs_dsp_wmfw_err_test_mock_algs[0].id);

	ncoeff = (__force __le32 *)&alg_data[3];
	KUNIT_ASSERT_EQ(test, le32_to_cpu(*ncoeff), 1);

	/* Add one to the coefficient count */
	*ncoeff = cpu_to_le32(2);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	/* Make the coefficient count garbage */
	*ncoeff = cpu_to_le32(0xffffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*ncoeff = cpu_to_le32(0x7fffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	*ncoeff = cpu_to_le32(0x80000000);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* V2+ coefficient block size exceeds length of containing block */
static void wmfw_v2_coeff_block_size_exceeds_block(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	__le32 *alg_data, *coeff;

	/* Create alg info block with a coefficient */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &mock_coeff_template);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (__force __le32 *)region->data;

	/* Sanity check we're pointing at the alg header */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[0]), cs_dsp_wmfw_err_test_mock_algs[0].id);

	/* Sanity check we're pointing at the coeff block */
	coeff = (__force __le32 *)&alg_data[4];
	KUNIT_ASSERT_EQ(test, le32_to_cpu(coeff[0]), mock_coeff_template.mem_type << 16);

	/* Add one to the block size */
	coeff[1] = cpu_to_le32(le32_to_cpu(coeff[1]) + 1);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	/* Make the block size garbage */
	coeff[1] = cpu_to_le32(0xffffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	coeff[1] = cpu_to_le32(0x7fffffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	coeff[1] = cpu_to_le32(0x80000000);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* V1 coeff name does not have NUL terminator */
static void wmfw_v1_coeff_name_unterminated(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	struct wmfw_adsp_alg_data *alg_data;
	struct wmfw_adsp_coeff_data *coeff;
	struct cs_dsp_coeff_ctl *ctl;

	/* Create alg info block with a coefficient */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &mock_coeff_template);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (struct wmfw_adsp_alg_data *)region->data;

	/* Sanity check we're pointing at the alg header */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data->id), cs_dsp_wmfw_err_test_mock_algs[0].id);
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data->ncoeff), 1);

	coeff = (void *)alg_data->data;

	/* Write a string to the coeff name that overflows the array */
	memset(coeff->descr, 0, sizeof(coeff->descr));
	memset(coeff->name, 'A', sizeof(coeff->name));
	memset(coeff->descr, 'A', sizeof(coeff->descr) - 1);

	/*
	 * Sanity-check that a strlen would overflow coeff->name.
	 * FORTIFY_STRING obstructs testing what strlen() would actually
	 * return, so instead verify that a strnlen() returns
	 * sizeof(coeff->name[]), therefore it doesn't have a NUL.
	 */
	KUNIT_ASSERT_EQ(test, strnlen(coeff->name, sizeof(coeff->name)),
			sizeof(coeff->name));

	/*
	 * V1 controls do not have names, but cs_dsp parses the name
	 * field. It should load the file successfully and create the
	 * control.
	 * If FORTIFY_STRING is enabled it will detect a buffer overflow
	 * if cs_dsp string length walks past end of coeff name array.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, 0);
}

/* V2+ coefficient shortname exceeds length of coeff block */
static void wmfw_v2_coeff_shortname_exceeds_block(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	__le32 *alg_data, *coeff;

	/* Create alg info block with a coefficient */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &mock_coeff_template);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (__force __le32 *)region->data;

	/* Sanity check we're pointing at the alg header */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[0]), cs_dsp_wmfw_err_test_mock_algs[0].id);

	/* Sanity check we're pointing at the coeff block */
	coeff = (__force __le32 *)&alg_data[4];
	KUNIT_ASSERT_EQ(test, le32_to_cpu(coeff[0]), mock_coeff_template.mem_type << 16);

	/* Add one to the shortname length */
	coeff[2] = cpu_to_le32(le32_to_cpu(coeff[2]) + 1);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	/* Maximum shortname length */
	coeff[2] = cpu_to_le32(255);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* V2+ coefficient fullname exceeds length of coeff block */
static void wmfw_v2_coeff_fullname_exceeds_block(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	__le32 *alg_data, *coeff, *fullname;
	size_t shortlen;

	/* Create alg info block with a coefficient */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &mock_coeff_template);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (__force __le32 *)region->data;

	/* Sanity check we're pointing at the alg header */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[0]), cs_dsp_wmfw_err_test_mock_algs[0].id);

	/* Sanity check we're pointing at the coeff block */
	coeff = (__force __le32 *)&alg_data[4];
	KUNIT_ASSERT_EQ(test, le32_to_cpu(coeff[0]), mock_coeff_template.mem_type << 16);

	/* Fullname follows the shortname rounded up to a __le32 boundary */
	shortlen = round_up(le32_to_cpu(coeff[2]) & 0xff, sizeof(__le32));
	fullname = &coeff[2] + (shortlen / sizeof(*coeff));

	/* Fullname increases in blocks of __le32 so increase past the current __le32 */
	fullname[0] = cpu_to_le32(round_up(le32_to_cpu(fullname[0]) + 1, sizeof(__le32)));
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	/* Maximum fullname length */
	fullname[0] = cpu_to_le32(255);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

/* V2+ coefficient description exceeds length of coeff block */
static void wmfw_v2_coeff_description_exceeds_block(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	struct wmfw_header *header;
	struct wmfw_region *region;
	__le32 *alg_data, *coeff, *fullname, *description;
	size_t namelen;

	/* Create alg info block with a coefficient */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_wmfw_err_test_mock_algs[0].id,
					      "abc", "de");
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &mock_coeff_template);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);

	/* Sanity-check that the good wmfw loads ok */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "wmfw", NULL, NULL, "misc"),
			0);
	cs_dsp_power_down(priv->dsp);

	header = (struct wmfw_header *)wmfw->data;
	region = (struct wmfw_region *)&wmfw->data[le32_to_cpu(header->len)];
	alg_data = (__force __le32 *)region->data;

	/* Sanity check we're pointing at the alg header */
	KUNIT_ASSERT_EQ(test, le32_to_cpu(alg_data[0]), cs_dsp_wmfw_err_test_mock_algs[0].id);

	/* Sanity check we're pointing at the coeff block */
	coeff = (__force __le32 *)&alg_data[4];
	KUNIT_ASSERT_EQ(test, le32_to_cpu(coeff[0]), mock_coeff_template.mem_type << 16);

	/* Description follows the shortname and fullname rounded up to __le32 boundaries */
	namelen = round_up(le32_to_cpu(coeff[2]) & 0xff, sizeof(__le32));
	fullname = &coeff[2] + (namelen / sizeof(*coeff));
	namelen = round_up(le32_to_cpu(fullname[0]) & 0xff, sizeof(__le32));
	description = fullname + (namelen / sizeof(*fullname));

	/* Description increases in blocks of __le32 so increase past the current __le32 */
	description[0] = cpu_to_le32(round_up(le32_to_cpu(fullname[0]) + 1, sizeof(__le32)));
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);

	/* Maximum description length */
	fullname[0] = cpu_to_le32(0xffff);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			-EOVERFLOW);
}

static void cs_dsp_wmfw_err_test_exit(struct kunit *test)
{
	/*
	 * Testing error conditions can produce a lot of log output
	 * from cs_dsp error messages, so rate limit the test cases.
	 */
	usleep_range(200, 500);
}

static int cs_dsp_wmfw_err_test_common_init(struct kunit *test, struct cs_dsp *dsp,
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
	local->wmfw_version = wmfw_version;

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
	local->xm_header = cs_dsp_create_mock_xm_header(priv,
							cs_dsp_wmfw_err_test_mock_algs,
							ARRAY_SIZE(cs_dsp_wmfw_err_test_mock_algs));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->xm_header);
	cs_dsp_mock_xm_header_write_to_regmap(local->xm_header);

	local->wmfw_builder = cs_dsp_mock_wmfw_init(priv, local->wmfw_version);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->wmfw_builder);

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

static int cs_dsp_wmfw_err_test_halo_init(struct kunit *test)
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

	return cs_dsp_wmfw_err_test_common_init(test, dsp, 3);
}

static int cs_dsp_wmfw_err_test_adsp2_32bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_wmfw_err_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_wmfw_err_test_adsp2_32bit_wmfw0_init(struct kunit *test)
{
	return cs_dsp_wmfw_err_test_adsp2_32bit_init(test, 0);
}

static int cs_dsp_wmfw_err_test_adsp2_32bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_wmfw_err_test_adsp2_32bit_init(test, 1);
}

static int cs_dsp_wmfw_err_test_adsp2_32bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_wmfw_err_test_adsp2_32bit_init(test, 2);
}

static int cs_dsp_wmfw_err_test_adsp2_16bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_wmfw_err_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_wmfw_err_test_adsp2_16bit_wmfw0_init(struct kunit *test)
{
	return cs_dsp_wmfw_err_test_adsp2_16bit_init(test, 0);
}

static int cs_dsp_wmfw_err_test_adsp2_16bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_wmfw_err_test_adsp2_16bit_init(test, 1);
}

static int cs_dsp_wmfw_err_test_adsp2_16bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_wmfw_err_test_adsp2_16bit_init(test, 2);
}

static void cs_dsp_wmfw_err_block_types_desc(const struct cs_dsp_wmfw_test_param *param,
					     char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "block_type:%#x", param->block_type);
}

static const struct cs_dsp_wmfw_test_param wmfw_valid_block_types_adsp2_cases[] = {
	{ .block_type = WMFW_INFO_TEXT },
	{ .block_type = WMFW_ADSP2_PM },
	{ .block_type = WMFW_ADSP2_YM },
};

KUNIT_ARRAY_PARAM(wmfw_valid_block_types_adsp2,
		  wmfw_valid_block_types_adsp2_cases,
		  cs_dsp_wmfw_err_block_types_desc);

static const struct cs_dsp_wmfw_test_param wmfw_valid_block_types_halo_cases[] = {
	{ .block_type = WMFW_INFO_TEXT },
	{ .block_type = WMFW_HALO_PM_PACKED },
	{ .block_type = WMFW_ADSP2_YM },
};

KUNIT_ARRAY_PARAM(wmfw_valid_block_types_halo,
		  wmfw_valid_block_types_halo_cases,
		  cs_dsp_wmfw_err_block_types_desc);

static const struct cs_dsp_wmfw_test_param wmfw_invalid_block_types_cases[] = {
	{ .block_type = 0x33 },
	{ .block_type = 0xf5 },
	{ .block_type = 0xc0 },
};

KUNIT_ARRAY_PARAM(wmfw_invalid_block_types,
		  wmfw_invalid_block_types_cases,
		  cs_dsp_wmfw_err_block_types_desc);

static struct kunit_case cs_dsp_wmfw_err_test_cases_v0[] = {
	KUNIT_CASE(wmfw_load_with_unknown_blocks),
	KUNIT_CASE(wmfw_err_wrong_magic),
	KUNIT_CASE(wmfw_err_too_short_for_header),
	KUNIT_CASE(wmfw_err_bad_header_length),
	KUNIT_CASE(wmfw_err_bad_core_type),

	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_payload, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),
	KUNIT_CASE_PARAM(wmfw_block_payload_len_garbage, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_wmfw_err_test_cases_v1[] = {
	KUNIT_CASE(wmfw_load_with_unknown_blocks),
	KUNIT_CASE(wmfw_err_wrong_magic),
	KUNIT_CASE(wmfw_err_too_short_for_header),
	KUNIT_CASE(wmfw_err_bad_header_length),
	KUNIT_CASE(wmfw_err_bad_core_type),

	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_payload, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),
	KUNIT_CASE_PARAM(wmfw_block_payload_len_garbage, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),

	KUNIT_CASE(wmfw_too_short_for_alg_header),
	KUNIT_CASE(wmfw_v1_alg_name_unterminated),
	KUNIT_CASE(wmfw_v1_coeff_count_exceeds_block),
	KUNIT_CASE(wmfw_v1_coeff_name_unterminated),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_wmfw_err_test_cases_v2[] = {
	KUNIT_CASE(wmfw_load_with_unknown_blocks),
	KUNIT_CASE(wmfw_err_wrong_magic),
	KUNIT_CASE(wmfw_err_too_short_for_header),
	KUNIT_CASE(wmfw_err_bad_header_length),
	KUNIT_CASE(wmfw_err_bad_core_type),

	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_payload, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),
	KUNIT_CASE_PARAM(wmfw_block_payload_len_garbage, wmfw_valid_block_types_adsp2_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),

	KUNIT_CASE(wmfw_too_short_for_alg_header),
	KUNIT_CASE(wmfw_v2_alg_name_exceeds_block),
	KUNIT_CASE(wmfw_v2_alg_description_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_count_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_block_size_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_shortname_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_fullname_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_description_exceeds_block),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_wmfw_err_test_cases_v3[] = {
	KUNIT_CASE(wmfw_load_with_unknown_blocks),
	KUNIT_CASE(wmfw_err_wrong_magic),
	KUNIT_CASE(wmfw_err_too_short_for_header),
	KUNIT_CASE(wmfw_err_bad_header_length),
	KUNIT_CASE(wmfw_err_bad_core_type),

	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_valid_block_types_halo_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_payload, wmfw_valid_block_types_halo_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),
	KUNIT_CASE_PARAM(wmfw_block_payload_len_garbage, wmfw_valid_block_types_halo_gen_params),
	KUNIT_CASE_PARAM(wmfw_too_short_for_block_header, wmfw_invalid_block_types_gen_params),

	KUNIT_CASE(wmfw_too_short_for_alg_header),
	KUNIT_CASE(wmfw_v2_alg_name_exceeds_block),
	KUNIT_CASE(wmfw_v2_alg_description_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_count_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_block_size_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_shortname_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_fullname_exceeds_block),
	KUNIT_CASE(wmfw_v2_coeff_description_exceeds_block),

	{ } /* terminator */
};

static struct kunit_suite cs_dsp_wmfw_err_test_halo = {
	.name = "cs_dsp_wmfwV3_err_halo",
	.init = cs_dsp_wmfw_err_test_halo_init,
	.exit = cs_dsp_wmfw_err_test_exit,
	.test_cases = cs_dsp_wmfw_err_test_cases_v3,
};

static struct kunit_suite cs_dsp_wmfw_err_test_adsp2_32bit_wmfw0 = {
	.name = "cs_dsp_wmfwV0_err_adsp2_32bit",
	.init = cs_dsp_wmfw_err_test_adsp2_32bit_wmfw0_init,
	.exit = cs_dsp_wmfw_err_test_exit,
	.test_cases = cs_dsp_wmfw_err_test_cases_v0,
};

static struct kunit_suite cs_dsp_wmfw_err_test_adsp2_32bit_wmfw1 = {
	.name = "cs_dsp_wmfwV1_err_adsp2_32bit",
	.init = cs_dsp_wmfw_err_test_adsp2_32bit_wmfw1_init,
	.exit = cs_dsp_wmfw_err_test_exit,
	.test_cases = cs_dsp_wmfw_err_test_cases_v1,
};

static struct kunit_suite cs_dsp_wmfw_err_test_adsp2_32bit_wmfw2 = {
	.name = "cs_dsp_wmfwV2_err_adsp2_32bit",
	.init = cs_dsp_wmfw_err_test_adsp2_32bit_wmfw2_init,
	.exit = cs_dsp_wmfw_err_test_exit,
	.test_cases = cs_dsp_wmfw_err_test_cases_v2,
};

static struct kunit_suite cs_dsp_wmfw_err_test_adsp2_16bit_wmfw0 = {
	.name = "cs_dsp_wmfwV0_err_adsp2_16bit",
	.init = cs_dsp_wmfw_err_test_adsp2_16bit_wmfw0_init,
	.exit = cs_dsp_wmfw_err_test_exit,
	.test_cases = cs_dsp_wmfw_err_test_cases_v0,
};

static struct kunit_suite cs_dsp_wmfw_err_test_adsp2_16bit_wmfw1 = {
	.name = "cs_dsp_wmfwV1_err_adsp2_16bit",
	.init = cs_dsp_wmfw_err_test_adsp2_16bit_wmfw1_init,
	.exit = cs_dsp_wmfw_err_test_exit,
	.test_cases = cs_dsp_wmfw_err_test_cases_v1,
};

static struct kunit_suite cs_dsp_wmfw_err_test_adsp2_16bit_wmfw2 = {
	.name = "cs_dsp_wmfwV2_err_adsp2_16bit",
	.init = cs_dsp_wmfw_err_test_adsp2_16bit_wmfw2_init,
	.exit = cs_dsp_wmfw_err_test_exit,
	.test_cases = cs_dsp_wmfw_err_test_cases_v2,
};

kunit_test_suites(&cs_dsp_wmfw_err_test_halo,
		  &cs_dsp_wmfw_err_test_adsp2_32bit_wmfw0,
		  &cs_dsp_wmfw_err_test_adsp2_32bit_wmfw1,
		  &cs_dsp_wmfw_err_test_adsp2_32bit_wmfw2,
		  &cs_dsp_wmfw_err_test_adsp2_16bit_wmfw0,
		  &cs_dsp_wmfw_err_test_adsp2_16bit_wmfw1,
		  &cs_dsp_wmfw_err_test_adsp2_16bit_wmfw2);
