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

/*
 * Test method is:
 *
 * 1) Create a mock regmap in cache-only mode so that all writes will be cached.
 * 2) Create dummy wmfw file.
 * 3) Call cs_dsp_power_up() with the bin file.
 * 4) Readback the cached value of registers that should have been written and
 *    check they have the correct value.
 * 5) All the registers that are expected to have been written are dropped from
 *    the cache. This should leave the cache clean.
 * 6) If the cache is still dirty there have been unexpected writes.
 */

KUNIT_DEFINE_ACTION_WRAPPER(_put_device_wrapper, put_device, struct device *)
KUNIT_DEFINE_ACTION_WRAPPER(_vfree_wrapper, vfree, void *)
KUNIT_DEFINE_ACTION_WRAPPER(_cs_dsp_remove_wrapper, cs_dsp_remove, struct cs_dsp *)

struct cs_dsp_test_local {
	struct cs_dsp_mock_xm_header *xm_header;
	struct cs_dsp_mock_wmfw_builder *wmfw_builder;
	int wmfw_version;
};

struct cs_dsp_wmfw_test_param {
	unsigned int num_blocks;
	int mem_type;
};

static const struct cs_dsp_mock_alg_def cs_dsp_wmfw_test_mock_algs[] = {
	{
		.id = 0xfafa,
		.ver = 0x100000,
		.xm_size_words = 164,
		.ym_size_words = 164,
		.zm_size_words = 164,
	},
};

/*
 * wmfw that writes the XM header.
 * cs_dsp always reads this back from unpacked XM.
 */
static void wmfw_write_xm_header_unpacked(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	unsigned int reg_addr;
	u8 *readback;

	/* XM header payload was added to wmfw by test case init function */

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	/* Read raw so endianness and register width don't matter */
	readback = kunit_kzalloc(test, local->xm_header->blob_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_XM);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					local->xm_header->blob_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, local->xm_header->blob_data,
			   local->xm_header->blob_size_bytes);

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* Write one payload of length param->num_blocks */
static void wmfw_write_one_payload(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	unsigned int mem_offset_dsp_words = 0;
	unsigned int payload_size_bytes;

	payload_size_bytes = param->num_blocks *
			     cs_dsp_mock_reg_block_length_bytes(priv, param->mem_type);

	/* payloads must be a multiple of 4 bytes and a whole number of DSP registers */
	do {
		payload_size_bytes += cs_dsp_mock_reg_block_length_bytes(priv, param->mem_type);
	} while (payload_size_bytes % 4);

	payload_data = kunit_kmalloc(test, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);
	get_random_bytes(payload_data, payload_size_bytes);

	readback = kunit_kzalloc(test, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Tests on XM must be after the XM header */
	if (param->mem_type == WMFW_ADSP2_XM)
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / sizeof(u32);

	/* Add a single payload */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					param->mem_type, mem_offset_dsp_words,
					payload_data, payload_size_bytes);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg_addr += cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv) * mem_offset_dsp_words;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, payload_size_bytes);

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, payload_size_bytes);
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* Write several smallest possible payloads for the given memory type */
static void wmfw_write_multiple_oneblock_payloads(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	unsigned int mem_offset_dsp_words = 0;
	unsigned int payload_size_bytes, payload_size_dsp_words;
	const unsigned int num_payloads = param->num_blocks;
	int i;

	/* payloads must be a multiple of 4 bytes and a whole number of DSP registers */
	payload_size_dsp_words = 0;
	payload_size_bytes = 0;
	do {
		payload_size_dsp_words += cs_dsp_mock_reg_block_length_dsp_words(priv,
										 param->mem_type);
		payload_size_bytes += cs_dsp_mock_reg_block_length_bytes(priv, param->mem_type);
	} while (payload_size_bytes % 4);

	payload_data = kunit_kcalloc(test, num_payloads, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);

	readback = kunit_kcalloc(test, num_payloads, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	get_random_bytes(payload_data, num_payloads * payload_size_bytes);

	/* Tests on XM must be after the XM header */
	if (param->mem_type == WMFW_ADSP2_XM)
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / payload_size_bytes;

	/* Add multiple payloads of one block each */
	for (i = 0; i < num_payloads; ++i) {
		cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
						param->mem_type,
						mem_offset_dsp_words + (i * payload_size_dsp_words),
						&payload_data[i * payload_size_bytes],
						payload_size_bytes);
	}

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg_addr += cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv) * mem_offset_dsp_words;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					num_payloads * payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, num_payloads * payload_size_bytes);

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, num_payloads * payload_size_bytes);
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write several smallest possible payloads of the given memory type
 * in reverse address order
 */
static void wmfw_write_multiple_oneblock_payloads_reverse(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	unsigned int mem_offset_dsp_words = 0;
	unsigned int payload_size_bytes, payload_size_dsp_words;
	const unsigned int num_payloads = param->num_blocks;
	int i;

	/* payloads must be a multiple of 4 bytes and a whole number of DSP registers */
	payload_size_dsp_words = 0;
	payload_size_bytes = 0;
	do {
		payload_size_dsp_words += cs_dsp_mock_reg_block_length_dsp_words(priv,
										 param->mem_type);
		payload_size_bytes += cs_dsp_mock_reg_block_length_bytes(priv, param->mem_type);
	} while (payload_size_bytes % 4);

	payload_data = kunit_kcalloc(test, num_payloads, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);

	readback = kunit_kcalloc(test, num_payloads, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	get_random_bytes(payload_data, num_payloads * payload_size_bytes);

	/* Tests on XM must be after the XM header */
	if (param->mem_type == WMFW_ADSP2_XM)
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / payload_size_bytes;

	/* Add multiple payloads of one block each */
	for (i = num_payloads - 1; i >= 0; --i) {
		cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
						param->mem_type,
						mem_offset_dsp_words + (i * payload_size_dsp_words),
						&payload_data[i * payload_size_bytes],
						payload_size_bytes);
	}

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg_addr += cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv) * mem_offset_dsp_words;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					num_payloads * payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, num_payloads * payload_size_bytes);

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, num_payloads * payload_size_bytes);
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write multiple payloads of length param->num_blocks.
 * The payloads are not in address order and collectively do not patch
 * a contiguous block of memory.
 */
static void wmfw_write_multiple_payloads_sparse_unordered(struct kunit *test)
{
	static const unsigned int random_offsets[] = {
		11, 69, 59, 61, 32, 75, 4, 38, 70, 13, 79, 47, 46, 53, 18, 44,
		54, 35, 51, 21, 26, 45, 27, 41, 66, 2, 17, 56, 40, 9, 8, 20,
		29, 19, 63, 42, 12, 16, 43, 3, 5, 55, 52, 22
	};
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	unsigned int mem_offset_dsp_words = 0;
	unsigned int payload_size_bytes, payload_size_dsp_words;
	const int num_payloads = ARRAY_SIZE(random_offsets);
	int i;

	payload_size_bytes = param->num_blocks *
			     cs_dsp_mock_reg_block_length_bytes(priv, param->mem_type);
	payload_size_dsp_words = param->num_blocks *
				 cs_dsp_mock_reg_block_length_dsp_words(priv, param->mem_type);

	/* payloads must be a multiple of 4 bytes and a whole number of DSP registers */
	do {
		payload_size_dsp_words += cs_dsp_mock_reg_block_length_dsp_words(priv,
										 param->mem_type);
		payload_size_bytes += cs_dsp_mock_reg_block_length_bytes(priv, param->mem_type);
	} while (payload_size_bytes % 4);

	payload_data = kunit_kcalloc(test, num_payloads, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);
	get_random_bytes(payload_data, payload_size_bytes);

	readback = kunit_kcalloc(test, num_payloads, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Tests on XM must be after the XM header */
	if (param->mem_type == WMFW_ADSP2_XM)
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / payload_size_bytes;

	/* Add multiple payloads of one block each at "random" locations */
	for (i = 0; i < num_payloads; ++i) {
		unsigned int offset = random_offsets[i] * payload_size_dsp_words;

		cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
						param->mem_type,
						mem_offset_dsp_words + offset,
						&payload_data[i * payload_size_bytes],
						payload_size_bytes);
	}

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	for (i = 0; i < num_payloads; ++i) {
		unsigned int offset_num_regs = (random_offsets[i] * payload_size_bytes) /
						regmap_get_val_bytes(priv->dsp->regmap);
		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
		reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
		reg_addr += cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv) * mem_offset_dsp_words;
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr,
						&readback[i * payload_size_bytes],
						payload_size_bytes),
				0);

		cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, payload_size_bytes);
	}

	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, payload_size_bytes);

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* Write the whole of PM in a single unpacked payload */
static void wmfw_write_all_unpacked_pm(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	unsigned int payload_size_bytes;

	payload_size_bytes = cs_dsp_mock_size_of_region(priv->dsp, WMFW_ADSP2_PM);
	payload_data = vmalloc(payload_size_bytes);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);
	kunit_add_action_or_reset(priv->test, _vfree_wrapper, payload_data);

	readback = vmalloc(payload_size_bytes);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);
	kunit_add_action_or_reset(priv->test, _vfree_wrapper, readback);
	memset(readback, 0, payload_size_bytes);

	/* Add a single PM payload */
	get_random_bytes(payload_data, payload_size_bytes);
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_ADSP2_PM, 0,
					payload_data, payload_size_bytes);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_PM);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, payload_size_bytes);

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, payload_size_bytes);
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* Write the whole of PM in a single packed payload */
static void wmfw_write_all_packed_pm(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	unsigned int payload_size_bytes;

	payload_size_bytes = cs_dsp_mock_size_of_region(priv->dsp, WMFW_HALO_PM_PACKED);
	payload_data = vmalloc(payload_size_bytes);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);
	kunit_add_action_or_reset(priv->test, _vfree_wrapper, payload_data);

	readback = vmalloc(payload_size_bytes);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);
	kunit_add_action_or_reset(priv->test, _vfree_wrapper, readback);
	memset(readback, 0, payload_size_bytes);

	/* Add a single PM payload */
	get_random_bytes(payload_data, payload_size_bytes);
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_HALO_PM_PACKED, 0,
					payload_data, payload_size_bytes);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_HALO_PM_PACKED);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, payload_size_bytes);

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, payload_size_bytes);
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write a series of payloads to various unpacked memory regions.
 * The payloads are of various lengths and offsets, driven by the
 * payload_defs table. The offset and length are both given as a
 * number of minimum-sized register blocks to keep the maths simpler.
 * (Where a minimum-sized register block is the smallest number of
 * registers that contain a whole number of DSP words.)
 */
static void wmfw_write_multiple_unpacked_mem(struct kunit *test)
{
	static const struct {
		int mem_type;
		unsigned int offset_num_blocks;
		unsigned int num_blocks;
	} payload_defs[] = {
		{ WMFW_ADSP2_PM, 11, 60 },
		{ WMFW_ADSP2_ZM, 69, 8 },
		{ WMFW_ADSP2_YM, 32, 74 },
		{ WMFW_ADSP2_XM, 70, 38 },
		{ WMFW_ADSP2_PM, 84, 48 },
		{ WMFW_ADSP2_XM, 46, 18 },
		{ WMFW_ADSP2_PM, 0,  8 },
		{ WMFW_ADSP2_YM, 0, 30 },
		{ WMFW_ADSP2_PM, 160, 50 },
		{ WMFW_ADSP2_ZM, 21, 26 },
	};
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int payload_size_bytes, offset_num_dsp_words;
	unsigned int reg_addr, offset_bytes, offset_num_regs;
	void **payload_data;
	void *readback;
	int i, ret;

	payload_data = kunit_kcalloc(test, ARRAY_SIZE(payload_defs), sizeof(*payload_data),
				     GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);

	for (i = 0; i < ARRAY_SIZE(payload_defs); ++i) {
		payload_size_bytes = payload_defs[i].num_blocks *
				     cs_dsp_mock_reg_block_length_bytes(priv,
									payload_defs[i].mem_type);

		payload_data[i] = kunit_kmalloc(test, payload_size_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data[i]);
		get_random_bytes(payload_data[i], payload_size_bytes);

		offset_num_dsp_words = payload_defs[i].offset_num_blocks *
				       cs_dsp_mock_reg_block_length_dsp_words(priv,
									 payload_defs[i].mem_type);
		cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
						payload_defs[i].mem_type,
						offset_num_dsp_words,
						payload_data[i],
						payload_size_bytes);
	}

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	for (i = 0; i < ARRAY_SIZE(payload_defs); ++i) {
		payload_size_bytes = payload_defs[i].num_blocks *
				     cs_dsp_mock_reg_block_length_bytes(priv,
									payload_defs[i].mem_type);

		readback = kunit_kzalloc(test, payload_size_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

		offset_bytes = payload_defs[i].offset_num_blocks *
			       cs_dsp_mock_reg_block_length_bytes(priv, payload_defs[i].mem_type);
		offset_num_regs = offset_bytes / regmap_get_val_bytes(priv->dsp->regmap);
		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, payload_defs[i].mem_type);
		reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
		ret = regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes);
		KUNIT_EXPECT_EQ_MSG(test, ret, 0, "%s @%u num:%u\n",
				    cs_dsp_mem_region_name(payload_defs[i].mem_type),
				    payload_defs[i].offset_num_blocks, payload_defs[i].num_blocks);
		KUNIT_EXPECT_MEMEQ_MSG(test, readback, payload_data[i], payload_size_bytes,
				       "%s @%u num:%u\n",
				       cs_dsp_mem_region_name(payload_defs[i].mem_type),
				       payload_defs[i].offset_num_blocks,
				       payload_defs[i].num_blocks);

		kunit_kfree(test, readback);

		cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, payload_size_bytes);
	}

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write a series of payloads to various packed and unpacked memory regions.
 * The payloads are of various lengths and offsets, driven by the
 * payload_defs table. The offset and length are both given as a
 * number of minimum-sized register blocks to keep the maths simpler.
 * (Where a minimum-sized register block is the smallest number of
 * registers that contain a whole number of DSP words.)
 */
static void wmfw_write_multiple_packed_unpacked_mem(struct kunit *test)
{
	static const struct {
		int mem_type;
		unsigned int offset_num_blocks;
		unsigned int num_blocks;
	} payload_defs[] = {
		{ WMFW_HALO_PM_PACKED,	11, 60 },
		{ WMFW_ADSP2_YM,	69, 8 },
		{ WMFW_HALO_YM_PACKED,	32, 74 },
		{ WMFW_HALO_XM_PACKED,	70, 38 },
		{ WMFW_HALO_PM_PACKED,	84, 48 },
		{ WMFW_HALO_XM_PACKED,	46, 18 },
		{ WMFW_HALO_PM_PACKED,	0,  8 },
		{ WMFW_HALO_YM_PACKED,	0, 30 },
		{ WMFW_HALO_PM_PACKED,	160, 50 },
		{ WMFW_ADSP2_XM,	21, 26 },
	};
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int payload_size_bytes, offset_num_dsp_words;
	unsigned int reg_addr, offset_bytes, offset_num_regs;
	void **payload_data;
	void *readback;
	int i, ret;

	payload_data = kunit_kcalloc(test, ARRAY_SIZE(payload_defs), sizeof(*payload_data),
				     GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);

	for (i = 0; i < ARRAY_SIZE(payload_defs); ++i) {
		payload_size_bytes = payload_defs[i].num_blocks *
				     cs_dsp_mock_reg_block_length_bytes(priv,
									payload_defs[i].mem_type);

		payload_data[i] = kunit_kmalloc(test, payload_size_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data[i]);
		get_random_bytes(payload_data[i], payload_size_bytes);

		offset_num_dsp_words = payload_defs[i].offset_num_blocks *
				       cs_dsp_mock_reg_block_length_dsp_words(priv,
									 payload_defs[i].mem_type);
		cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
						payload_defs[i].mem_type,
						offset_num_dsp_words,
						payload_data[i],
						payload_size_bytes);
	}

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	for (i = 0; i < ARRAY_SIZE(payload_defs); ++i) {
		payload_size_bytes = payload_defs[i].num_blocks *
				     cs_dsp_mock_reg_block_length_bytes(priv,
									payload_defs[i].mem_type);

		readback = kunit_kzalloc(test, payload_size_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

		offset_bytes = payload_defs[i].offset_num_blocks *
			       cs_dsp_mock_reg_block_length_bytes(priv, payload_defs[i].mem_type);
		offset_num_regs = offset_bytes / regmap_get_val_bytes(priv->dsp->regmap);
		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, payload_defs[i].mem_type);
		reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
		ret = regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes);
		KUNIT_EXPECT_EQ_MSG(test, ret, 0, "%s @%u num:%u\n",
				    cs_dsp_mem_region_name(payload_defs[i].mem_type),
				    payload_defs[i].offset_num_blocks,
				    payload_defs[i].num_blocks);
		KUNIT_EXPECT_MEMEQ_MSG(test, readback, payload_data[i], payload_size_bytes,
				       "%s @%u num:%u\n",
				       cs_dsp_mem_region_name(payload_defs[i].mem_type),
				       payload_defs[i].offset_num_blocks,
				       payload_defs[i].num_blocks);

		kunit_kfree(test, readback);

		cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, payload_size_bytes);
	}

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is one word longer than a packed block multiple,
 * using one packed payload followed by one unpacked word.
 */
static void wmfw_write_packed_1_unpacked_trailing(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int mem_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[1];
	unsigned int packed_payload_size_bytes, packed_payload_size_dsp_words;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);
	packed_payload_size_dsp_words = param->num_blocks * dsp_words_per_packed_block;

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM) {
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / sizeof(u32);

		/* Round up to multiple of packed block length */
		mem_offset_dsp_words = roundup(mem_offset_dsp_words, dsp_words_per_packed_block);
	}

	/* Add a single packed payload */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type, mem_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);
	/*
	 * Add payload of one unpacked word to DSP memory right after
	 * the packed payload words.
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					mem_offset_dsp_words + packed_payload_size_dsp_words,
					unpacked_payload_data, sizeof(unpacked_payload_data));

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked word was written correctly and drop
	 * it from the regmap cache. The unpacked payload is offset within
	 * unpacked register space by the number of DSP words that were
	 * written in the packed payload.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	offset_num_regs += (packed_payload_size_dsp_words / dsp_words_per_unpacked_block) *
			   cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is two words longer than a packed block multiple,
 * using one packed payload followed by one payload of two unpacked words.
 */
static void wmfw_write_packed_2_unpacked_trailing(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int mem_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[2];
	unsigned int packed_payload_size_bytes, packed_payload_size_dsp_words;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);
	packed_payload_size_dsp_words = param->num_blocks * dsp_words_per_packed_block;

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM) {
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / sizeof(u32);

		/* Round up to multiple of packed block length */
		mem_offset_dsp_words = roundup(mem_offset_dsp_words, dsp_words_per_packed_block);
	}

	/* Add a single packed payload */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type, mem_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);
	/*
	 * Add payload of two unpacked words to DSP memory right after
	 * the packed payload words.
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					mem_offset_dsp_words + packed_payload_size_dsp_words,
					unpacked_payload_data, sizeof(unpacked_payload_data));

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked words were written correctly and drop
	 * them from the regmap cache. The unpacked payload is offset
	 * within unpacked register space by the number of DSP words
	 * that were written in the packed payload.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	offset_num_regs += (packed_payload_size_dsp_words / dsp_words_per_unpacked_block) *
			   cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is three words longer than a packed block multiple,
 * using one packed payload followed by one payload of three unpacked words.
 */
static void wmfw_write_packed_3_unpacked_trailing(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int mem_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[3];
	unsigned int packed_payload_size_bytes, packed_payload_size_dsp_words;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);
	packed_payload_size_dsp_words = param->num_blocks * dsp_words_per_packed_block;

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM) {
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / sizeof(u32);

		/* Round up to multiple of packed block length */
		mem_offset_dsp_words = roundup(mem_offset_dsp_words, dsp_words_per_packed_block);
	}

	/* Add a single packed payload */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type, mem_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);
	/*
	 * Add payload of three unpacked words to DSP memory right after
	 * the packed payload words.
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					mem_offset_dsp_words + packed_payload_size_dsp_words,
					unpacked_payload_data, sizeof(unpacked_payload_data));

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked words were written correctly and drop
	 * them from the regmap cache. The unpacked payload is offset
	 * within unpacked register space by the number of DSP words
	 * that were written in the packed payload.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	offset_num_regs += (packed_payload_size_dsp_words / dsp_words_per_unpacked_block) *
			   cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is two words longer than a packed block multiple,
 * using one packed payload followed by two payloads of one unpacked word each.
 */
static void wmfw_write_packed_2_single_unpacked_trailing(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int mem_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[2];
	unsigned int packed_payload_size_bytes, packed_payload_size_dsp_words;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);
	packed_payload_size_dsp_words = param->num_blocks * dsp_words_per_packed_block;

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM) {
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / sizeof(u32);

		/* Round up to multiple of packed block length */
		mem_offset_dsp_words = roundup(mem_offset_dsp_words, dsp_words_per_packed_block);
	}

	/* Add a single packed payload */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type, mem_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);
	/*
	 * Add two unpacked words to DSP memory right after the packed
	 * payload words. Each unpacked word in its own payload.
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					mem_offset_dsp_words + packed_payload_size_dsp_words,
					&unpacked_payload_data[0],
					sizeof(unpacked_payload_data[0]));
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					mem_offset_dsp_words + packed_payload_size_dsp_words + 1,
					&unpacked_payload_data[1],
					sizeof(unpacked_payload_data[1]));

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);

	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked words were written correctly and drop
	 * them from the regmap cache. The unpacked words are offset
	 * within unpacked register space by the number of DSP words
	 * that were written in the packed payload.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	offset_num_regs += (packed_payload_size_dsp_words / dsp_words_per_unpacked_block) *
			   cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is three words longer than a packed block multiple,
 * using one packed payload followed by three payloads of one unpacked word each.
 */
static void wmfw_write_packed_3_single_unpacked_trailing(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int mem_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[3];
	unsigned int packed_payload_size_bytes, packed_payload_size_dsp_words;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				 cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);
	packed_payload_size_dsp_words = param->num_blocks * dsp_words_per_packed_block;

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM) {
		mem_offset_dsp_words += local->xm_header->blob_size_bytes / sizeof(u32);

		/* Round up to multiple of packed block length */
		mem_offset_dsp_words = roundup(mem_offset_dsp_words, dsp_words_per_packed_block);
	}

	/* Add a single packed payload */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type, mem_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);
	/*
	 * Add three unpacked words to DSP memory right after the packed
	 * payload words. Each unpacked word in its own payload.
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					mem_offset_dsp_words + packed_payload_size_dsp_words,
					&unpacked_payload_data[0],
					sizeof(unpacked_payload_data[0]));
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					mem_offset_dsp_words + packed_payload_size_dsp_words + 1,
					&unpacked_payload_data[1],
					sizeof(unpacked_payload_data[1]));
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					mem_offset_dsp_words + packed_payload_size_dsp_words + 2,
					&unpacked_payload_data[2],
					sizeof(unpacked_payload_data[2]));

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked words were written correctly and drop
	 * them from the regmap cache. The unpacked words are offset
	 * within unpacked register space by the number of DSP words
	 * that were written in the packed payload.
	 */
	offset_num_regs = (mem_offset_dsp_words / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	offset_num_regs += (packed_payload_size_dsp_words / dsp_words_per_unpacked_block) *
			   cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is one word longer than a packed block multiple,
 * and does not start on a packed alignment. Use one unpacked word
 * followed by a packed payload.
 */
static void wmfw_write_packed_1_unpacked_leading(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int packed_payload_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[1];
	unsigned int packed_payload_size_bytes;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM)
		packed_payload_offset_dsp_words += local->xm_header->blob_size_bytes /
						   sizeof(u32);
	/*
	 * Leave space for an unaligned word before the packed block and
	 * round the packed block start to multiple of packed block length.
	 */
	packed_payload_offset_dsp_words += 1;
	packed_payload_offset_dsp_words = roundup(packed_payload_offset_dsp_words,
						  dsp_words_per_packed_block);

	/* Add a single unpacked word right before the first word of packed data */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					packed_payload_offset_dsp_words - 1,
					unpacked_payload_data, sizeof(unpacked_payload_data));

	/* Add payload of packed data to the DSP memory after the unpacked word. */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type,
					packed_payload_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (packed_payload_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked word was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = ((packed_payload_offset_dsp_words - 1) / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is two words longer than a packed block multiple,
 * and does not start on a packed alignment. Use one payload of two unpacked
 * words followed by a packed payload.
 */
static void wmfw_write_packed_2_unpacked_leading(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int packed_payload_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[2];
	unsigned int packed_payload_size_bytes;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM)
		packed_payload_offset_dsp_words += local->xm_header->blob_size_bytes /
						   sizeof(u32);
	/*
	 * Leave space for two unaligned words before the packed block and
	 * round the packed block start to multiple of packed block length.
	 */
	packed_payload_offset_dsp_words += 2;
	packed_payload_offset_dsp_words = roundup(packed_payload_offset_dsp_words,
						  dsp_words_per_packed_block);

	/*
	 * Add two unpacked words as a single payload right before the
	 * first word of packed data
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					packed_payload_offset_dsp_words - 2,
					unpacked_payload_data, sizeof(unpacked_payload_data));

	/* Add payload of packed data to the DSP memory after the unpacked words. */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type,
					packed_payload_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (packed_payload_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked words were written correctly and drop
	 * them from the regmap cache.
	 */
	offset_num_regs = ((packed_payload_offset_dsp_words - 2) / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is three words longer than a packed block multiple,
 * and does not start on a packed alignment. Use one payload of three unpacked
 * words followed by a packed payload.
 */
static void wmfw_write_packed_3_unpacked_leading(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int packed_payload_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[3];
	unsigned int packed_payload_size_bytes;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM)
		packed_payload_offset_dsp_words += local->xm_header->blob_size_bytes /
						   sizeof(u32);
	/*
	 * Leave space for three unaligned words before the packed block and
	 * round the packed block start to multiple of packed block length.
	 */
	packed_payload_offset_dsp_words += 3;
	packed_payload_offset_dsp_words = roundup(packed_payload_offset_dsp_words,
						  dsp_words_per_packed_block);

	/*
	 * Add three unpacked words as a single payload right before the
	 * first word of packed data
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					packed_payload_offset_dsp_words - 3,
					unpacked_payload_data, sizeof(unpacked_payload_data));

	/* Add payload of packed data to the DSP memory after the unpacked words. */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type,
					packed_payload_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (packed_payload_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked words were written correctly and drop
	 * them from the regmap cache.
	 */
	offset_num_regs = ((packed_payload_offset_dsp_words - 3) / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is two words longer than a packed block multiple,
 * and does not start on a packed alignment. Use two payloads of one unpacked
 * word each, followed by a packed payload.
 */
static void wmfw_write_packed_2_single_unpacked_leading(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int packed_payload_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[2];
	unsigned int packed_payload_size_bytes;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM)
		packed_payload_offset_dsp_words += local->xm_header->blob_size_bytes /
						   sizeof(u32);
	/*
	 * Leave space for two unaligned words before the packed block and
	 * round the packed block start to multiple of packed block length.
	 */
	packed_payload_offset_dsp_words += 2;
	packed_payload_offset_dsp_words = roundup(packed_payload_offset_dsp_words,
						  dsp_words_per_packed_block);

	/*
	 * Add two unpacked words as two payloads each containing a single
	 * unpacked word.
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					packed_payload_offset_dsp_words - 2,
					&unpacked_payload_data[0],
					sizeof(unpacked_payload_data[0]));
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					packed_payload_offset_dsp_words - 1,
					&unpacked_payload_data[1],
					sizeof(unpacked_payload_data[1]));

	/* Add payload of packed data to the DSP memory after the unpacked words. */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type,
					packed_payload_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (packed_payload_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked words were written correctly and drop
	 * them from the regmap cache.
	 */
	offset_num_regs = ((packed_payload_offset_dsp_words - 2) / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write XM/YM data that is three words longer than a packed block multiple,
 * and does not start on a packed alignment. Use three payloads of one unpacked
 * word each, followed by a packed payload.
 */
static void wmfw_write_packed_3_single_unpacked_leading(struct kunit *test)
{
	const struct cs_dsp_wmfw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	int packed_mem_type = param->mem_type;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	unsigned int dsp_words_per_packed_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, packed_mem_type);
	unsigned int dsp_words_per_unpacked_block =
		cs_dsp_mock_reg_block_length_dsp_words(priv, unpacked_mem_type);
	unsigned int packed_payload_offset_dsp_words = 0;
	struct firmware *wmfw;
	unsigned int reg_addr;
	void *packed_payload_data, *readback;
	u32 unpacked_payload_data[3];
	unsigned int packed_payload_size_bytes;
	unsigned int offset_num_regs;

	packed_payload_size_bytes = param->num_blocks *
				    cs_dsp_mock_reg_block_length_bytes(priv, packed_mem_type);

	packed_payload_data = kunit_kmalloc(test, packed_payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, packed_payload_data);
	get_random_bytes(packed_payload_data, packed_payload_size_bytes);

	get_random_bytes(unpacked_payload_data, sizeof(unpacked_payload_data));

	readback = kunit_kzalloc(test, packed_payload_size_bytes, GFP_KERNEL);

	/* Tests on XM must be after the XM header */
	if (unpacked_mem_type == WMFW_ADSP2_XM)
		packed_payload_offset_dsp_words += local->xm_header->blob_size_bytes /
						   sizeof(u32);
	/*
	 * Leave space for two unaligned words before the packed block and
	 * round the packed block start to multiple of packed block length.
	 */
	packed_payload_offset_dsp_words += 3;
	packed_payload_offset_dsp_words = roundup(packed_payload_offset_dsp_words,
						  dsp_words_per_packed_block);

	/*
	 * Add three unpacked words as three payloads each containing a single
	 * unpacked word.
	 */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					packed_payload_offset_dsp_words - 3,
					&unpacked_payload_data[0],
					sizeof(unpacked_payload_data[0]));
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					packed_payload_offset_dsp_words - 2,
					&unpacked_payload_data[1],
					sizeof(unpacked_payload_data[1]));
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					unpacked_mem_type,
					packed_payload_offset_dsp_words - 1,
					&unpacked_payload_data[2],
					sizeof(unpacked_payload_data[2]));

	/* Add payload of packed data to the DSP memory after the unpacked words. */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					packed_mem_type,
					packed_payload_offset_dsp_words,
					packed_payload_data, packed_payload_size_bytes);

	/* Download the wmfw */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc"),
			0);
	/*
	 * Check that the packed payload was written correctly and drop
	 * it from the regmap cache.
	 */
	offset_num_regs = (packed_payload_offset_dsp_words / dsp_words_per_packed_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, packed_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, packed_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					packed_payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload_data, packed_payload_size_bytes);

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, packed_payload_size_bytes);

	/*
	 * Check that the unpacked words were written correctly and drop
	 * them from the regmap cache.
	 */
	offset_num_regs = ((packed_payload_offset_dsp_words - 3) / dsp_words_per_unpacked_block) *
			  cs_dsp_mock_reg_block_length_registers(priv, unpacked_mem_type);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type);
	reg_addr += offset_num_regs * regmap_get_reg_stride(priv->dsp->regmap);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(unpacked_payload_data)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, unpacked_payload_data, sizeof(unpacked_payload_data));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* Load a wmfw containing multiple info blocks */
static void wmfw_load_with_info(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;
	unsigned int reg_addr;
	u8 *payload_data, *readback;
	char *infobuf;
	const unsigned int payload_size_bytes = 48;
	int ret;

	payload_data = kunit_kmalloc(test, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload_data);
	get_random_bytes(payload_data, payload_size_bytes);

	readback = kunit_kzalloc(test, payload_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Add a couple of info blocks at the start of the wmfw */
	cs_dsp_mock_wmfw_add_info(local->wmfw_builder, "This is a timestamp");
	cs_dsp_mock_wmfw_add_info(local->wmfw_builder, "This is some more info");

	/* Add a single payload */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_ADSP2_YM, 0,
					payload_data, payload_size_bytes);

	/* Add a bigger info block then another small one*/
	infobuf = kunit_kzalloc(test, 512, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, infobuf);

	for (; strlcat(infobuf, "Waffle{Blah}\n", 512) < 512;)
		;

	cs_dsp_mock_wmfw_add_info(local->wmfw_builder, infobuf);
	cs_dsp_mock_wmfw_add_info(local->wmfw_builder, "Another block of info");

	/* Add another payload */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_ADSP2_YM, 64,
					payload_data, payload_size_bytes);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

	ret = cs_dsp_power_up(priv->dsp, wmfw, "mock_wmfw", NULL, NULL, "misc");
	KUNIT_EXPECT_EQ_MSG(test, ret, 0, "cs_dsp_power_up failed: %d\n", ret);

	/* Check first payload was written */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, payload_size_bytes);

	/* Check second payload was written */
	reg_addr += cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv) * 64;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, payload_size_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, payload_size_bytes);
}

static int cs_dsp_wmfw_test_common_init(struct kunit *test, struct cs_dsp *dsp,
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
							cs_dsp_wmfw_test_mock_algs,
							ARRAY_SIZE(cs_dsp_wmfw_test_mock_algs));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->xm_header);

	local->wmfw_builder = cs_dsp_mock_wmfw_init(priv, priv->local->wmfw_version);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->wmfw_builder);

	/* Add dummy XM header payload to wmfw */
	cs_dsp_mock_wmfw_add_data_block(local->wmfw_builder,
					WMFW_ADSP2_XM, 0,
					local->xm_header->blob_data,
					local->xm_header->blob_size_bytes);

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

static int cs_dsp_wmfw_test_halo_init(struct kunit *test)
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

	return cs_dsp_wmfw_test_common_init(test, dsp, 3);
}

static int cs_dsp_wmfw_test_adsp2_32bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_wmfw_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_wmfw_test_adsp2_32bit_wmfw0_init(struct kunit *test)
{
	return cs_dsp_wmfw_test_adsp2_32bit_init(test, 0);
}

static int cs_dsp_wmfw_test_adsp2_32bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_wmfw_test_adsp2_32bit_init(test, 1);
}

static int cs_dsp_wmfw_test_adsp2_32bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_wmfw_test_adsp2_32bit_init(test, 2);
}

static int cs_dsp_wmfw_test_adsp2_16bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_wmfw_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_wmfw_test_adsp2_16bit_wmfw0_init(struct kunit *test)
{
	return cs_dsp_wmfw_test_adsp2_16bit_init(test, 0);
}

static int cs_dsp_wmfw_test_adsp2_16bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_wmfw_test_adsp2_16bit_init(test, 1);
}

static int cs_dsp_wmfw_test_adsp2_16bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_wmfw_test_adsp2_16bit_init(test, 2);
}

static void cs_dsp_mem_param_desc(const struct cs_dsp_wmfw_test_param *param, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s num_blocks:%u",
		 cs_dsp_mem_region_name(param->mem_type),
		 param->num_blocks);
}

static const struct cs_dsp_wmfw_test_param adsp2_all_num_blocks_param_cases[] = {
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 1 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 2 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 3 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 4 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 5 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 6 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 12 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 13 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 14 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 15 },
	{ .mem_type = WMFW_ADSP2_PM, .num_blocks = 16 },

	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 1 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 2 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 3 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 4 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 5 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 6 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 12 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 13 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 14 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 15 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 16 },

	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 1 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 2 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 3 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 4 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 5 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 6 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 12 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 13 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 14 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 15 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 16 },

	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 1 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 2 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 3 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 4 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 5 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 6 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 12 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 13 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 14 },
	{ .mem_type = WMFW_ADSP2_ZM, .num_blocks = 15 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 16 },
};

KUNIT_ARRAY_PARAM(adsp2_all_num_blocks,
		  adsp2_all_num_blocks_param_cases,
		  cs_dsp_mem_param_desc);

static const struct cs_dsp_wmfw_test_param halo_all_num_blocks_param_cases[] = {
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 1 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 2 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 3 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 4 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 5 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 6 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 12 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 13 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 14 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 15 },
	{ .mem_type = WMFW_HALO_PM_PACKED, .num_blocks = 16 },

	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 1 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 2 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 3 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 4 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 5 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 6 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 12 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 13 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 14 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 15 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 16 },

	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 1 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 2 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 3 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 4 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 5 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 6 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 12 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 13 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 14 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 15 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 16 },

	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 1 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 2 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 3 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 4 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 5 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 6 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 12 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 13 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 14 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 15 },
	{ .mem_type = WMFW_ADSP2_XM, .num_blocks = 16 },

	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 1 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 2 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 3 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 4 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 5 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 6 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 12 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 13 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 14 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 15 },
	{ .mem_type = WMFW_ADSP2_YM, .num_blocks = 16 },
};

KUNIT_ARRAY_PARAM(halo_all_num_blocks,
		  halo_all_num_blocks_param_cases,
		  cs_dsp_mem_param_desc);

static const struct cs_dsp_wmfw_test_param packed_xy_num_blocks_param_cases[] = {
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 1 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 2 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 3 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 4 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 5 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 6 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 12 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 13 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 14 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 15 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .num_blocks = 16 },

	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 1 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 2 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 3 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 4 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 5 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 6 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 12 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 13 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 14 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 15 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .num_blocks = 16 },
};

KUNIT_ARRAY_PARAM(packed_xy_num_blocks,
		  packed_xy_num_blocks_param_cases,
		  cs_dsp_mem_param_desc);

static struct kunit_case cs_dsp_wmfw_test_cases_halo[] = {
	KUNIT_CASE(wmfw_write_xm_header_unpacked),

	KUNIT_CASE_PARAM(wmfw_write_one_payload,
			 halo_all_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_multiple_oneblock_payloads,
			 halo_all_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_multiple_oneblock_payloads_reverse,
			 halo_all_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_multiple_payloads_sparse_unordered,
			 halo_all_num_blocks_gen_params),

	KUNIT_CASE(wmfw_write_all_packed_pm),
	KUNIT_CASE(wmfw_write_multiple_packed_unpacked_mem),

	KUNIT_CASE_PARAM(wmfw_write_packed_1_unpacked_trailing,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_2_unpacked_trailing,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_3_unpacked_trailing,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_2_single_unpacked_trailing,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_3_single_unpacked_trailing,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_1_unpacked_leading,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_2_unpacked_leading,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_3_unpacked_leading,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_2_single_unpacked_leading,
			 packed_xy_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_packed_3_single_unpacked_leading,
			 packed_xy_num_blocks_gen_params),

	KUNIT_CASE(wmfw_load_with_info),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_wmfw_test_cases_adsp2[] = {
	KUNIT_CASE(wmfw_write_xm_header_unpacked),
	KUNIT_CASE_PARAM(wmfw_write_one_payload,
			 adsp2_all_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_multiple_oneblock_payloads,
			 adsp2_all_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_multiple_oneblock_payloads_reverse,
			 adsp2_all_num_blocks_gen_params),
	KUNIT_CASE_PARAM(wmfw_write_multiple_payloads_sparse_unordered,
			 adsp2_all_num_blocks_gen_params),

	KUNIT_CASE(wmfw_write_all_unpacked_pm),
	KUNIT_CASE(wmfw_write_multiple_unpacked_mem),

	KUNIT_CASE(wmfw_load_with_info),

	{ } /* terminator */
};

static struct kunit_suite cs_dsp_wmfw_test_halo = {
	.name = "cs_dsp_wmfwV3_halo",
	.init = cs_dsp_wmfw_test_halo_init,
	.test_cases = cs_dsp_wmfw_test_cases_halo,
};

static struct kunit_suite cs_dsp_wmfw_test_adsp2_32bit_wmfw0 = {
	.name = "cs_dsp_wmfwV0_adsp2_32bit",
	.init = cs_dsp_wmfw_test_adsp2_32bit_wmfw0_init,
	.test_cases = cs_dsp_wmfw_test_cases_adsp2,
};

static struct kunit_suite cs_dsp_wmfw_test_adsp2_32bit_wmfw1 = {
	.name = "cs_dsp_wmfwV1_adsp2_32bit",
	.init = cs_dsp_wmfw_test_adsp2_32bit_wmfw1_init,
	.test_cases = cs_dsp_wmfw_test_cases_adsp2,
};

static struct kunit_suite cs_dsp_wmfw_test_adsp2_32bit_wmfw2 = {
	.name = "cs_dsp_wmfwV2_adsp2_32bit",
	.init = cs_dsp_wmfw_test_adsp2_32bit_wmfw2_init,
	.test_cases = cs_dsp_wmfw_test_cases_adsp2,
};

static struct kunit_suite cs_dsp_wmfw_test_adsp2_16bit_wmfw0 = {
	.name = "cs_dsp_wmfwV0_adsp2_16bit",
	.init = cs_dsp_wmfw_test_adsp2_16bit_wmfw0_init,
	.test_cases = cs_dsp_wmfw_test_cases_adsp2,
};

static struct kunit_suite cs_dsp_wmfw_test_adsp2_16bit_wmfw1 = {
	.name = "cs_dsp_wmfwV1_adsp2_16bit",
	.init = cs_dsp_wmfw_test_adsp2_16bit_wmfw1_init,
	.test_cases = cs_dsp_wmfw_test_cases_adsp2,
};

static struct kunit_suite cs_dsp_wmfw_test_adsp2_16bit_wmfw2 = {
	.name = "cs_dsp_wmfwV2_adsp2_16bit",
	.init = cs_dsp_wmfw_test_adsp2_16bit_wmfw2_init,
	.test_cases = cs_dsp_wmfw_test_cases_adsp2,
};

kunit_test_suites(&cs_dsp_wmfw_test_halo,
		  &cs_dsp_wmfw_test_adsp2_32bit_wmfw0,
		  &cs_dsp_wmfw_test_adsp2_32bit_wmfw1,
		  &cs_dsp_wmfw_test_adsp2_32bit_wmfw2,
		  &cs_dsp_wmfw_test_adsp2_16bit_wmfw0,
		  &cs_dsp_wmfw_test_adsp2_16bit_wmfw1,
		  &cs_dsp_wmfw_test_adsp2_16bit_wmfw2);
