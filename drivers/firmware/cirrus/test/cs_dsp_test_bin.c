// SPDX-License-Identifier: GPL-2.0-only
//
// KUnit tests for cs_dsp.
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/device.h>
#include <kunit/resource.h>
#include <kunit/test.h>
#include <linux/build_bug.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/cs_dsp_test_utils.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/firmware.h>
#include <linux/math.h>
#include <linux/random.h>
#include <linux/regmap.h>

/*
 * Test method is:
 *
 * 1) Create a mock regmap in cache-only mode so that all writes will be cached.
 * 2) Create a XM header with an algorithm list in the cached regmap.
 * 3) Create dummy wmfw file to satisfy cs_dsp.
 * 4) Create bin file content.
 * 5) Call cs_dsp_power_up() with the bin file.
 * 6) Readback the cached value of registers that should have been written and
 *    check they have the correct value.
 * 7) All the registers that are expected to have been written are dropped from
 *    the cache (including the XM header). This should leave the cache clean.
 * 8) If the cache is still dirty there have been unexpected writes.
 *
 * There are multiple different schemes used for addressing across
 * ADSP2 and Halo Core DSPs:
 *
 *  dsp words:	The addressing scheme used by the DSP, pointers and lengths
 *		in DSP memory use this. A memory region (XM, YM, ZM) is
 *		also required to create a unique DSP memory address.
 *  registers:	Addresses in the register map. Older ADSP2 devices have
 *		16-bit registers with an address stride of 1. Newer ADSP2
 *		devices have 32-bit registers with an address stride of 2.
 *		Halo Core devices have 32-bit registers with a stride of 4.
 *  unpacked:	Registers that have a 1:1 mapping to DSP words
 *  packed:	Registers that pack multiple DSP words more efficiently into
 *		multiple 32-bit registers. Because of this the relationship
 *		between a packed _register_ address and the corresponding
 *		_dsp word_ address is different from unpacked registers.
 *		Packed registers can only be accessed as a group of
 *		multiple registers, therefore can only read/write a group
 *		of multiple DSP words.
 *		Packed registers only exist on Halo Core DSPs.
 *
 * Addresses can also be relative to the start of an algorithm, and this
 * can be expressed in dsp words, register addresses, or bytes.
 */

KUNIT_DEFINE_ACTION_WRAPPER(_put_device_wrapper, put_device, struct device *)
KUNIT_DEFINE_ACTION_WRAPPER(_cs_dsp_remove_wrapper, cs_dsp_remove, struct cs_dsp *)

struct cs_dsp_test_local {
	struct cs_dsp_mock_bin_builder *bin_builder;
	struct cs_dsp_mock_wmfw_builder *wmfw_builder;
	struct firmware *wmfw;
};

struct bin_test_param {
	const char *name;
	int mem_type;
	unsigned int offset_words;
	int alg_idx;
};

static const struct cs_dsp_mock_alg_def bin_test_mock_algs[] = {
	{
		.id = 0xfafa,
		.ver = 0x100000,
		.xm_size_words = 164,
		.ym_size_words = 164,
		.zm_size_words = 164,
	},
	{
		.id = 0xfbfb,
		.ver = 0x100000,
		.xm_size_words = 99,
		.ym_size_words = 99,
		.zm_size_words = 99,
	},
	{
		.id = 0xc321,
		.ver = 0x100000,
		.xm_size_words = 120,
		.ym_size_words = 120,
		.zm_size_words = 120,
	},
	{
		.id = 0xb123,
		.ver = 0x100000,
		.xm_size_words = 96,
		.ym_size_words = 96,
		.zm_size_words = 96,
	},
};

/*
 * Convert number of DSP words to number of packed registers rounded
 * down to the nearest register.
 * There are 3 registers for every 4 packed words.
 */
static unsigned int _num_words_to_num_packed_regs(unsigned int num_dsp_words)
{
	return (num_dsp_words * 3) / 4;
}

/* bin file that patches a single DSP word */
static void bin_patch_one_word(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	u32 reg_val, payload_data;
	unsigned int alg_base_words, reg_addr;
	struct firmware *fw;

	get_random_bytes(&payload_data, sizeof(payload_data));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);

	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  param->offset_words * reg_inc_per_word,
				  &payload_data, sizeof(payload_data));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   ((alg_base_words + param->offset_words) * reg_inc_per_word);
	reg_val = 0;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr,
					&reg_val, sizeof(reg_val)),
			0);
	KUNIT_EXPECT_EQ(test, reg_val, payload_data);

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_regmap_drop_range(priv, reg_addr, reg_addr + reg_inc_per_word - 1);
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);

	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* bin file with a single payload that patches consecutive words */
static void bin_patch_one_multiword(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	u32 payload_data[16], readback[16];
	unsigned int alg_base_words, reg_addr;
	struct firmware *fw;

	static_assert(ARRAY_SIZE(readback) == ARRAY_SIZE(payload_data));

	get_random_bytes(&payload_data, sizeof(payload_data));
	memset(readback, 0, sizeof(readback));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);

	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  param->offset_words * reg_inc_per_word,
				  payload_data, sizeof(payload_data));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   ((alg_base_words + param->offset_words) * reg_inc_per_word);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, sizeof(payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_regmap_drop_range(priv, reg_addr,
				      reg_addr + (reg_inc_per_word * ARRAY_SIZE(payload_data)));
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* bin file with a multiple one-word payloads that patch consecutive words */
static void bin_patch_multi_oneword(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	u32 payload_data[16], readback[16];
	unsigned int alg_base_words, reg_addr;
	struct firmware *fw;
	int i;

	static_assert(ARRAY_SIZE(readback) == ARRAY_SIZE(payload_data));

	get_random_bytes(&payload_data, sizeof(payload_data));
	memset(readback, 0, sizeof(readback));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);

	/* Add one payload per word */
	for (i = 0; i < ARRAY_SIZE(payload_data); ++i) {
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[param->alg_idx].id,
					  bin_test_mock_algs[param->alg_idx].ver,
					  param->mem_type,
					  (param->offset_words + i) * reg_inc_per_word,
					  &payload_data[i], sizeof(payload_data[i]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			 0);

	/* Content of registers should match payload_data */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   ((alg_base_words + param->offset_words) * reg_inc_per_word);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, sizeof(payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_range(priv, reg_addr,
				      reg_addr + (reg_inc_per_word * ARRAY_SIZE(payload_data)));
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file with a multiple one-word payloads that patch a block of consecutive
 * words but the payloads are not in address order.
 */
static void bin_patch_multi_oneword_unordered(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	u32 payload_data[16], readback[16];
	static const u8 word_order[] = { 10, 2, 12, 4, 0, 11, 6, 1, 3, 15, 5, 13, 8, 7, 9, 14 };
	unsigned int alg_base_words, reg_addr;
	struct firmware *fw;
	int i;

	static_assert(ARRAY_SIZE(readback) == ARRAY_SIZE(payload_data));
	static_assert(ARRAY_SIZE(word_order) == ARRAY_SIZE(payload_data));

	get_random_bytes(&payload_data, sizeof(payload_data));
	memset(readback, 0, sizeof(readback));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);

	/* Add one payload per word */
	for (i = 0; i < ARRAY_SIZE(word_order); ++i) {
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[param->alg_idx].id,
					  bin_test_mock_algs[param->alg_idx].ver,
					  param->mem_type,
					  (param->offset_words + word_order[i]) *
					  reg_inc_per_word,
					  &payload_data[word_order[i]], sizeof(payload_data[0]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   ((alg_base_words + param->offset_words) * reg_inc_per_word);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, payload_data, sizeof(payload_data));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_range(priv, reg_addr,
				      reg_addr + (reg_inc_per_word * ARRAY_SIZE(payload_data)));
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file with a multiple one-word payloads. The payloads are not in address
 * order and collectively do not patch a contiguous block of memory.
 */
static void bin_patch_multi_oneword_sparse_unordered(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	static const u8 word_offsets[] = {
		11, 69, 59, 61, 32, 75, 4, 38, 70, 13, 79, 47, 46, 53, 18, 44,
		54, 35, 51, 21, 26, 45, 27, 41, 66, 2, 17, 56, 40, 9, 8, 20,
		29, 19, 63, 42, 12, 16, 43, 3, 5, 55, 52, 22
	};
	u32 payload_data[44];
	unsigned int alg_base_words, reg_addr;
	struct firmware *fw;
	u32 reg_val;
	int i;

	static_assert(ARRAY_SIZE(word_offsets) == ARRAY_SIZE(payload_data));

	get_random_bytes(&payload_data, sizeof(payload_data));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);

	/* Add one payload per word */
	for (i = 0; i < ARRAY_SIZE(word_offsets); ++i) {
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[param->alg_idx].id,
					  bin_test_mock_algs[param->alg_idx].ver,
					  param->mem_type,
					  word_offsets[i] * reg_inc_per_word,
					  &payload_data[i], sizeof(payload_data[i]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	for (i = 0; i < ARRAY_SIZE(word_offsets); ++i) {
		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
			   ((alg_base_words + word_offsets[i]) * reg_inc_per_word);
		reg_val = 0;
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr, &reg_val,
						sizeof(reg_val)),
				0);
		KUNIT_EXPECT_MEMEQ(test, &reg_val, &payload_data[i], sizeof(reg_val));

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_range(priv, reg_addr, reg_addr + reg_inc_per_word - 1);
	}

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file that patches a single DSP word in each of the memory regions
 * of one algorithm.
 */
static void bin_patch_one_word_multiple_mems(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	unsigned int alg_xm_base_words, alg_ym_base_words, alg_zm_base_words;
	unsigned int reg_addr;
	u32 payload_data[3];
	struct firmware *fw;
	u32 reg_val;

	get_random_bytes(&payload_data, sizeof(payload_data));

	alg_xm_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							WMFW_ADSP2_XM);
	alg_ym_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							WMFW_ADSP2_YM);

	if (cs_dsp_mock_has_zm(priv)) {
		alg_zm_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							WMFW_ADSP2_ZM);
	} else {
		alg_zm_base_words = 0;
	}

	/* Add words to XM, YM and ZM */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  WMFW_ADSP2_XM,
				  param->offset_words * reg_inc_per_word,
				  &payload_data[0], sizeof(payload_data[0]));

	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  WMFW_ADSP2_YM,
				  param->offset_words * reg_inc_per_word,
				  &payload_data[1], sizeof(payload_data[1]));

	if (cs_dsp_mock_has_zm(priv)) {
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[param->alg_idx].id,
					  bin_test_mock_algs[param->alg_idx].ver,
					  WMFW_ADSP2_ZM,
					  param->offset_words * reg_inc_per_word,
					  &payload_data[2], sizeof(payload_data[2]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_XM) +
		   ((alg_xm_base_words + param->offset_words) * reg_inc_per_word);
	reg_val = 0;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &reg_val, sizeof(reg_val)),
			0);
	KUNIT_EXPECT_EQ(test, reg_val, payload_data[0]);

	cs_dsp_mock_regmap_drop_range(priv, reg_addr, reg_addr + reg_inc_per_word - 1);

	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM) +
		   ((alg_ym_base_words + param->offset_words) * reg_inc_per_word);
	reg_val = 0;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &reg_val, sizeof(reg_val)),
			0);
	KUNIT_EXPECT_EQ(test, reg_val, payload_data[1]);

	cs_dsp_mock_regmap_drop_range(priv, reg_addr, reg_addr + reg_inc_per_word - 1);

	if (cs_dsp_mock_has_zm(priv)) {
		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_ZM) +
			   ((alg_zm_base_words + param->offset_words) * reg_inc_per_word);
		reg_val = 0;
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr, &reg_val,
						sizeof(reg_val)),
				0);
		KUNIT_EXPECT_EQ(test, reg_val, payload_data[2]);

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_range(priv, reg_addr, reg_addr + reg_inc_per_word - 1);
	}

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file that patches a single DSP word in multiple algorithms.
 */
static void bin_patch_one_word_multiple_algs(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	u32 payload_data[ARRAY_SIZE(bin_test_mock_algs)];
	unsigned int alg_base_words;
	unsigned int reg_inc_per_word, reg_addr;
	struct firmware *fw;
	u32 reg_val;
	int i;

	get_random_bytes(&payload_data, sizeof(payload_data));

	/* Add one payload per algorithm */
	for (i = 0; i < ARRAY_SIZE(bin_test_mock_algs); ++i) {
		reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);

		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[i].id,
					  bin_test_mock_algs[i].ver,
					  param->mem_type,
					  param->offset_words * reg_inc_per_word,
					  &payload_data[i], sizeof(payload_data[i]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	for (i = 0; i < ARRAY_SIZE(bin_test_mock_algs); ++i) {
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[i].id,
								param->mem_type);
		reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
			   ((alg_base_words + param->offset_words) * reg_inc_per_word);
		reg_val = 0;
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr, &reg_val,
						sizeof(reg_val)),
				0);
		KUNIT_EXPECT_EQ(test, reg_val, payload_data[i]);

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_range(priv, reg_addr, reg_addr + reg_inc_per_word - 1);
	}

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file that patches a single DSP word in multiple algorithms.
 * The algorithms are not patched in the same order they appear in the XM header.
 */
static void bin_patch_one_word_multiple_algs_unordered(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	static const u8 alg_order[] = { 3, 0, 2, 1 };
	u32 payload_data[ARRAY_SIZE(bin_test_mock_algs)];
	unsigned int alg_base_words;
	unsigned int reg_inc_per_word, reg_addr;
	struct firmware *fw;
	u32 reg_val;
	int i, alg_idx;

	static_assert(ARRAY_SIZE(alg_order) == ARRAY_SIZE(bin_test_mock_algs));

	get_random_bytes(&payload_data, sizeof(payload_data));

	/* Add one payload per algorithm */
	for (i = 0; i < ARRAY_SIZE(bin_test_mock_algs); ++i) {
		alg_idx = alg_order[i];
		reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);

		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[alg_idx].id,
					  bin_test_mock_algs[alg_idx].ver,
					  param->mem_type,
					  param->offset_words * reg_inc_per_word,
					  &payload_data[i], sizeof(payload_data[i]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	for (i = 0; i < ARRAY_SIZE(bin_test_mock_algs); ++i) {
		alg_idx = alg_order[i];
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[alg_idx].id,
								param->mem_type);
		reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
			   ((alg_base_words + param->offset_words) * reg_inc_per_word);
		reg_val = 0;
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr, &reg_val,
						sizeof(reg_val)),
				0);
		KUNIT_EXPECT_EQ(test, reg_val, payload_data[i]);

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_range(priv, reg_addr, reg_addr + reg_inc_per_word - 1);
	}

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* bin file that patches a single packed block of DSP words */
static void bin_patch_1_packed(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	u32 packed_payload[3], readback[3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round patch start word up to a packed boundary */
	patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  packed_payload, sizeof(packed_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
					sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload, sizeof(packed_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that is one word longer than a packed block using one
 * packed block followed by one unpacked word.
 */
static void bin_patch_1_packed_1_single_trailing(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payload[1], readback[3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payload, sizeof(unpacked_payload));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round patch start word up to a packed boundary */
	patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

	/* Patch packed block */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	/* ... and the unpacked word following that */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((patch_pos_words + 4) - alg_base_words) * 4,
				  unpacked_payload, sizeof(unpacked_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (patch_pos_words + 4) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payload)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payload, sizeof(unpacked_payload));

	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that is two words longer than a packed block using one
 * packed block followed by two blocks of one unpacked word.
 */
static void bin_patch_1_packed_2_single_trailing(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payloads[2], readback[3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payloads));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payloads, sizeof(unpacked_payloads));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round patch start word up to a packed boundary */
	patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

	/* Patch packed block */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	/* ... and the unpacked words following that */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((patch_pos_words + 4) - alg_base_words) * 4,
				  &unpacked_payloads[0], sizeof(unpacked_payloads[0]));

	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((patch_pos_words + 5) - alg_base_words) * 4,
				  &unpacked_payloads[1], sizeof(unpacked_payloads[1]));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payloads */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (patch_pos_words + 4) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payloads)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payloads, sizeof(unpacked_payloads));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payloads));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that is three words longer than a packed block using one
 * packed block followed by three blocks of one unpacked word.
 */
static void bin_patch_1_packed_3_single_trailing(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payloads[3], readback[3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payloads));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payloads, sizeof(unpacked_payloads));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round patch start word up to a packed boundary */
	patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

	/* Patch packed block */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	/* ... and the unpacked words following that */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((patch_pos_words + 4) - alg_base_words) * 4,
				  &unpacked_payloads[0], sizeof(unpacked_payloads[0]));

	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((patch_pos_words + 5) - alg_base_words) * 4,
				  &unpacked_payloads[1], sizeof(unpacked_payloads[1]));

	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((patch_pos_words + 6) - alg_base_words) * 4,
				  &unpacked_payloads[2], sizeof(unpacked_payloads[2]));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payloads */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (patch_pos_words + 4) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payloads)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payloads, sizeof(unpacked_payloads));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payloads));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that is two words longer than a packed block using one
 * packed block followed by a block of two unpacked words.
 */
static void bin_patch_1_packed_2_trailing(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payload[2], readback[3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payload, sizeof(unpacked_payload));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round patch start word up to a packed boundary */
	patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

	/* Patch packed block */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	/* ... and the unpacked words following that */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((patch_pos_words + 4) - alg_base_words) * 4,
				  unpacked_payload, sizeof(unpacked_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (patch_pos_words + 4) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payload)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payload, sizeof(unpacked_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that is three words longer than a packed block using one
 * packed block followed by a block of three unpacked words.
 */
static void bin_patch_1_packed_3_trailing(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payload[3], readback[3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payload, sizeof(unpacked_payload));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round patch start word up to a packed boundary */
	patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

	/* Patch packed block */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	/* ... and the unpacked words following that */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((patch_pos_words + 4) - alg_base_words) * 4,
				  unpacked_payload, sizeof(unpacked_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (patch_pos_words + 4) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payload)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payload, sizeof(unpacked_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that starts one word before a packed boundary using one
 * unpacked word followed by one packed block.
 */
static void bin_patch_1_single_leading_1_packed(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payload[1], readback[3];
	unsigned int alg_base_words, packed_patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payload, sizeof(unpacked_payload));
	memset(readback, 0, sizeof(readback));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round packed start word up to a packed boundary and move to the next boundary */
	packed_patch_pos_words = round_up(alg_base_words + param->offset_words, 4) + 4;

	/* Patch the leading unpacked word */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((packed_patch_pos_words - 1) - alg_base_words) * 4,
				  unpacked_payload, sizeof(unpacked_payload));
	/* ... then the packed block */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(packed_patch_pos_words);
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (packed_patch_pos_words - 1) * 4;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payload)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payload, sizeof(unpacked_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that starts two words before a packed boundary using two
 * unpacked words followed by one packed block.
 */
static void bin_patch_2_single_leading_1_packed(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payload[2], readback[3];
	unsigned int alg_base_words, packed_patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payload, sizeof(unpacked_payload));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round packed start word up to a packed boundary and move to the next boundary */
	packed_patch_pos_words = round_up(alg_base_words + param->offset_words, 4) + 4;

	/* Patch the leading unpacked words */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((packed_patch_pos_words - 2) - alg_base_words) * 4,
				  &unpacked_payload[0], sizeof(unpacked_payload[0]));
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((packed_patch_pos_words - 1) - alg_base_words) * 4,
				  &unpacked_payload[1], sizeof(unpacked_payload[1]));
	/* ... then the packed block */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(packed_patch_pos_words);
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (packed_patch_pos_words - 2) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payload)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payload, sizeof(unpacked_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that starts two words before a packed boundary using one
 * block of two unpacked words followed by one packed block.
 */
static void bin_patch_2_leading_1_packed(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payload[2], readback[3];
	unsigned int alg_base_words, packed_patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payload, sizeof(unpacked_payload));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round packed start word up to a packed boundary and move to the next boundary */
	packed_patch_pos_words = round_up(alg_base_words + param->offset_words, 4) + 4;

	/* Patch the leading unpacked words */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((packed_patch_pos_words - 2) - alg_base_words) * 4,
				  unpacked_payload, sizeof(unpacked_payload));
	/* ... then the packed block */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(packed_patch_pos_words);
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (packed_patch_pos_words - 2) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payload)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payload, sizeof(unpacked_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that starts three words before a packed boundary using three
 * unpacked words followed by one packed block.
 */
static void bin_patch_3_single_leading_1_packed(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payload[3], readback[3];
	unsigned int alg_base_words, packed_patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payload, sizeof(unpacked_payload));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round packed start word up to a packed boundary and move to the next boundary */
	packed_patch_pos_words = round_up(alg_base_words + param->offset_words, 4) + 4;

	/* Patch the leading unpacked words */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((packed_patch_pos_words - 3) - alg_base_words) * 4,
				  &unpacked_payload[0], sizeof(unpacked_payload[0]));
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((packed_patch_pos_words - 2) - alg_base_words) * 4,
				  &unpacked_payload[1], sizeof(unpacked_payload[1]));
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((packed_patch_pos_words - 1) - alg_base_words) * 4,
				  &unpacked_payload[2], sizeof(unpacked_payload[2]));
	/* ... then the packed block */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(packed_patch_pos_words);
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (packed_patch_pos_words - 3) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payload)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payload, sizeof(unpacked_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Patch data that starts three words before a packed boundary using one
 * block of three unpacked words followed by one packed block.
 */
static void bin_patch_3_leading_1_packed(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	unsigned int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	u32 packed_payload[3], unpacked_payload[3], readback[3];
	unsigned int alg_base_words, packed_patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_payload));
	static_assert(sizeof(readback) >= sizeof(unpacked_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));
	get_random_bytes(unpacked_payload, sizeof(unpacked_payload));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round packed start word up to a packed boundary and move to the next boundary */
	packed_patch_pos_words = round_up(alg_base_words + param->offset_words, 4) + 4;

	/* Patch the leading unpacked words */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  unpacked_mem_type,
				  ((packed_patch_pos_words - 3) - alg_base_words) * 4,
				  unpacked_payload, sizeof(unpacked_payload));
	/* ... then the packed block */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(packed_patch_pos_words);
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  param->mem_type,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  &packed_payload, sizeof(packed_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, &packed_payload, sizeof(packed_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload));

	/* Content of unpacked registers should match unpacked_payload */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
		   (packed_patch_pos_words - 3) * 4;
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, &readback,
					sizeof(unpacked_payload)),
			0);
	KUNIT_EXPECT_MEMEQ(test, &readback, unpacked_payload, sizeof(unpacked_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(unpacked_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* bin file with a multiple payloads that each patch one packed block. */
static void bin_patch_multi_onepacked(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	u32 packed_payloads[8][3], readback[8][3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int payload_offset;
	unsigned int reg_addr;
	struct firmware *fw;
	int i;

	static_assert(sizeof(readback) == sizeof(packed_payloads));

	get_random_bytes(packed_payloads, sizeof(packed_payloads));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round patch start word up to a packed boundary */
	patch_pos_words = round_up(alg_base_words + param->offset_words, 4);

	/* Add one payload per packed block */
	for (i = 0; i < ARRAY_SIZE(packed_payloads); ++i) {
		patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words + (i * 4));
		payload_offset = (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4;
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[param->alg_idx].id,
					  bin_test_mock_algs[param->alg_idx].ver,
					  param->mem_type,
					  payload_offset,
					  &packed_payloads[i], sizeof(packed_payloads[i]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payloads */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payloads, sizeof(packed_payloads));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payloads));
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file with a multiple payloads that each patch one packed block.
 * The payloads are not in address order.
 */
static void bin_patch_multi_onepacked_unordered(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	static const u8 payload_order[] = { 4, 3, 6, 1, 0, 7, 5, 2 };
	u32 packed_payloads[8][3], readback[8][3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int payload_offset;
	unsigned int reg_addr;
	struct firmware *fw;
	int i;

	static_assert(ARRAY_SIZE(payload_order) == ARRAY_SIZE(packed_payloads));
	static_assert(sizeof(readback) == sizeof(packed_payloads));

	get_random_bytes(packed_payloads, sizeof(packed_payloads));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Round patch start word up to a packed boundary */
	patch_pos_words = round_up(alg_base_words + param->offset_words, 4);

	/* Add one payload per packed block */
	for (i = 0; i < ARRAY_SIZE(payload_order); ++i) {
		patch_pos_in_packed_regs =
			_num_words_to_num_packed_regs(patch_pos_words + (payload_order[i] * 4));
		payload_offset = (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4;
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[param->alg_idx].id,
					  bin_test_mock_algs[param->alg_idx].ver,
					  param->mem_type,
					  payload_offset,
					  &packed_payloads[payload_order[i]],
					  sizeof(packed_payloads[0]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content in registers should match the order of data in packed_payloads */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_payloads, sizeof(packed_payloads));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payloads));
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file with a multiple payloads that each patch one packed block.
 * The payloads are not in address order. The patched memory is not contiguous.
 */
static void bin_patch_multi_onepacked_sparse_unordered(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	static const u8 word_offsets[] = { 60, 24, 76, 4, 40, 52, 48, 36, 12 };
	u32 packed_payloads[9][3], readback[3];
	unsigned int alg_base_words, alg_base_in_packed_regs;
	unsigned int patch_pos_words, patch_pos_in_packed_regs, payload_offset;
	unsigned int reg_addr;
	struct firmware *fw;
	int i;

	static_assert(ARRAY_SIZE(word_offsets) == ARRAY_SIZE(packed_payloads));
	static_assert(sizeof(readback) == sizeof(packed_payloads[0]));

	get_random_bytes(packed_payloads, sizeof(packed_payloads));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							param->mem_type);
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

	/* Add one payload per packed block */
	for (i = 0; i < ARRAY_SIZE(word_offsets); ++i) {
		/* Round patch start word up to a packed boundary */
		patch_pos_words = round_up(alg_base_words + word_offsets[i], 4);
		patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);
		payload_offset = (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4;
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[param->alg_idx].id,
					  bin_test_mock_algs[param->alg_idx].ver,
					  param->mem_type,
					  payload_offset,
					  &packed_payloads[i],
					  sizeof(packed_payloads[0]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed registers should match packed_payloads */
	for (i = 0; i < ARRAY_SIZE(word_offsets); ++i) {
		patch_pos_words = round_up(alg_base_words + word_offsets[i], 4);
		patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);
		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
			   (patch_pos_in_packed_regs * 4);
		memset(readback, 0, sizeof(readback));
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
						sizeof(readback)),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, packed_payloads[i], sizeof(packed_payloads[i]));

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payloads[i]));
	}

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file that patches a single packed block in each of the memory regions
 * of one algorithm.
 */
static void bin_patch_1_packed_multiple_mems(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	u32 packed_xm_payload[3], packed_ym_payload[3], readback[3];
	unsigned int alg_xm_base_words, alg_ym_base_words;
	unsigned int xm_patch_pos_words, ym_patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr;
	struct firmware *fw;

	static_assert(sizeof(readback) == sizeof(packed_xm_payload));
	static_assert(sizeof(readback) == sizeof(packed_ym_payload));

	get_random_bytes(packed_xm_payload, sizeof(packed_xm_payload));
	get_random_bytes(packed_ym_payload, sizeof(packed_ym_payload));

	alg_xm_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							WMFW_HALO_XM_PACKED);
	alg_ym_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[param->alg_idx].id,
							WMFW_HALO_YM_PACKED);

	/* Round patch start word up to a packed boundary */
	xm_patch_pos_words = round_up(alg_xm_base_words + param->offset_words, 4);
	ym_patch_pos_words = round_up(alg_ym_base_words + param->offset_words, 4);

	/* Add XM and YM patches */
	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_xm_base_words);
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(xm_patch_pos_words);
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  WMFW_HALO_XM_PACKED,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  packed_xm_payload, sizeof(packed_xm_payload));

	alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_ym_base_words);
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(ym_patch_pos_words);
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[param->alg_idx].id,
				  bin_test_mock_algs[param->alg_idx].ver,
				  WMFW_HALO_YM_PACKED,
				  (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4,
				  packed_ym_payload, sizeof(packed_ym_payload));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of packed XM registers should match packed_xm_payload */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(xm_patch_pos_words);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_HALO_XM_PACKED) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_xm_payload, sizeof(packed_xm_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_xm_payload));

	/* Content of packed YM registers should match packed_ym_payload */
	patch_pos_in_packed_regs = _num_words_to_num_packed_regs(ym_patch_pos_words);
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_HALO_YM_PACKED) +
		   (patch_pos_in_packed_regs * 4);
	memset(readback, 0, sizeof(readback));
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr, readback, sizeof(readback)),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, packed_ym_payload, sizeof(packed_ym_payload));

	/* Drop expected writes from the cache */
	cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_ym_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file that patches a single packed block in multiple algorithms.
 */
static void bin_patch_1_packed_multiple_algs(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	u32 packed_payload[ARRAY_SIZE(bin_test_mock_algs)][3];
	u32 readback[ARRAY_SIZE(bin_test_mock_algs)][3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr, payload_offset;
	struct firmware *fw;
	int i;

	static_assert(sizeof(readback) == sizeof(packed_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));

	/* For each algorithm patch one DSP word to a value from packed_payload */
	for (i = 0; i < ARRAY_SIZE(bin_test_mock_algs); ++i) {
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[i].id,
								param->mem_type);
		alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

		/* Round patch start word up to a packed boundary */
		patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
		patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

		payload_offset = (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4;
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[i].id,
					  bin_test_mock_algs[i].ver,
					  param->mem_type,
					  payload_offset,
					  packed_payload[i], sizeof(packed_payload[i]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	memset(readback, 0, sizeof(readback));

	/*
	 * Readback the registers that should have been written. Place
	 * the values into the expected location in readback[] so that
	 * the content of readback[] should match packed_payload[]
	 */
	for (i = 0; i < ARRAY_SIZE(bin_test_mock_algs); ++i) {
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[i].id,
								param->mem_type);
		alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

		patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
		patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
			   (patch_pos_in_packed_regs * 4);
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr,
						readback[i], sizeof(readback[i])),
				0);

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload[i]));
	}

	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload, sizeof(packed_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file that patches a single packed block in multiple algorithms.
 * The algorithms are not patched in the same order they appear in the XM header.
 */
static void bin_patch_1_packed_multiple_algs_unordered(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	static const u8 alg_order[] = { 3, 0, 2, 1 };
	u32 packed_payload[ARRAY_SIZE(bin_test_mock_algs)][3];
	u32 readback[ARRAY_SIZE(bin_test_mock_algs)][3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr, payload_offset;
	struct firmware *fw;
	int i, alg_idx;

	static_assert(ARRAY_SIZE(alg_order) == ARRAY_SIZE(bin_test_mock_algs));
	static_assert(sizeof(readback) == sizeof(packed_payload));

	get_random_bytes(packed_payload, sizeof(packed_payload));

	/*
	 * For each algorithm index in alg_order[] patch one DSP word in
	 * that algorithm to a value from packed_payload.
	 */
	for (i = 0; i < ARRAY_SIZE(alg_order); ++i) {
		alg_idx = alg_order[i];
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[alg_idx].id,
								param->mem_type);
		alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);

		/* Round patch start word up to a packed boundary */
		patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
		patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

		payload_offset = (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4;
		cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
					  bin_test_mock_algs[alg_idx].id,
					  bin_test_mock_algs[alg_idx].ver,
					  param->mem_type,
					  payload_offset,
					  packed_payload[i], sizeof(packed_payload[i]));
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	memset(readback, 0, sizeof(readback));

	/*
	 * Readback the registers that should have been written. Place
	 * the values into the expected location in readback[] so that
	 * the content of readback[] should match packed_payload[]
	 */
	for (i = 0; i < ARRAY_SIZE(alg_order); ++i) {
		alg_idx = alg_order[i];
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[alg_idx].id,
								param->mem_type);

		patch_pos_words = round_up(alg_base_words + param->offset_words, 4);
		patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
			   (patch_pos_in_packed_regs * 4);
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr,
						readback[i], sizeof(readback[i])),
				0);

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(packed_payload[i]));
	}

	KUNIT_EXPECT_MEMEQ(test, readback, packed_payload, sizeof(packed_payload));

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * bin file that contains a mix of packed and unpacked words.
 * payloads are in random offset order. Offsets that are on a packed boundary
 * are written as a packed block. Offsets that are not on a packed boundary
 * are written as a single unpacked word.
 */
static void bin_patch_mixed_packed_unpacked_random(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	const struct bin_test_param *param = test->param_value;
	static const u8 offset_words[] = {
		58, 68, 50, 10, 44, 17, 74, 36,  8,  7, 49, 11, 78, 57, 65, 2,
		48, 38, 22, 70, 77, 21, 61, 56, 75, 34, 27,  3, 31, 20,	43, 63,
		 5, 30, 32, 25, 33, 79, 29,  0,	37, 60, 69, 52, 13, 12, 24, 26,
		 4, 51, 76, 72, 16,  6, 39, 62, 15, 41, 28, 73, 53, 40, 45, 54,
		14, 55, 46, 66, 64, 59, 23,  9, 67, 47, 19, 71, 35, 18, 42,  1,
	};
	struct {
		u32 packed[80][3];
		u32 unpacked[80];
	} *payload;
	u32 readback[3];
	unsigned int alg_base_words, patch_pos_words;
	unsigned int alg_base_in_packed_regs, patch_pos_in_packed_regs;
	unsigned int reg_addr, payload_offset;
	int unpacked_mem_type = cs_dsp_mock_packed_to_unpacked_mem_type(param->mem_type);
	struct firmware *fw;
	int i;

	payload = kunit_kmalloc(test, sizeof(*payload), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, payload);

	get_random_bytes(payload->packed, sizeof(payload->packed));
	get_random_bytes(payload->unpacked, sizeof(payload->unpacked));

	/* Create a patch entry for every offset in offset_words[] */
	for (i = 0; i < ARRAY_SIZE(offset_words); ++i) {
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[0].id,
								param->mem_type);
		/*
		 * If the offset is on a packed boundary use a packed payload else
		 * use an unpacked word
		 */
		patch_pos_words = alg_base_words + offset_words[i];
		if ((patch_pos_words % 4) == 0) {
			alg_base_in_packed_regs = _num_words_to_num_packed_regs(alg_base_words);
			patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);
			payload_offset = (patch_pos_in_packed_regs - alg_base_in_packed_regs) * 4;
			cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
						  bin_test_mock_algs[0].id,
						  bin_test_mock_algs[0].ver,
						  param->mem_type,
						  payload_offset,
						  payload->packed[i],
						  sizeof(payload->packed[i]));
		} else {
			payload_offset = offset_words[i] * 4;
			cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
						  bin_test_mock_algs[0].id,
						  bin_test_mock_algs[0].ver,
						  unpacked_mem_type,
						  payload_offset,
						  &payload->unpacked[i],
						  sizeof(payload->unpacked[i]));
		}
	}

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/*
	 * Readback the packed registers that should have been written.
	 * Place the values into the expected location in readback[] so
	 * that the content of readback[] should match payload->packed[]
	 */
	for (i = 0; i < ARRAY_SIZE(offset_words); ++i) {
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[0].id,
								param->mem_type);
		patch_pos_words = alg_base_words + offset_words[i];

		/* Skip if the offset is not on a packed boundary */
		if ((patch_pos_words % 4) != 0)
			continue;

		patch_pos_in_packed_regs = _num_words_to_num_packed_regs(patch_pos_words);

		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type) +
			   (patch_pos_in_packed_regs * 4);

		memset(readback, 0, sizeof(readback));
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr, readback,
						sizeof(readback)),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, payload->packed[i], sizeof(payload->packed[i]));

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(payload->packed[i]));
	}

	/*
	 * Readback the unpacked registers that should have been written.
	 * Place the values into the expected location in readback[] so
	 * that the content of readback[] should match payload->unpacked[]
	 */
	for (i = 0; i < ARRAY_SIZE(offset_words); ++i) {
		alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
								bin_test_mock_algs[0].id,
								unpacked_mem_type);

		patch_pos_words = alg_base_words + offset_words[i];

		/* Skip if the offset is on a packed boundary */
		if ((patch_pos_words % 4) == 0)
			continue;

		reg_addr = cs_dsp_mock_base_addr_for_mem(priv, unpacked_mem_type) +
			   ((patch_pos_words) * 4);

		readback[0] = 0;
		KUNIT_EXPECT_EQ(test,
				regmap_raw_read(priv->dsp->regmap, reg_addr,
						&readback[0], sizeof(readback[0])),
				0);
		KUNIT_EXPECT_EQ(test, readback[0], payload->unpacked[i]);

		/* Drop expected writes from the cache */
		cs_dsp_mock_regmap_drop_bytes(priv, reg_addr, sizeof(payload->unpacked[i]));
	}

	/* Drop expected writes and the cache should then be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/* Bin file with name and multiple info blocks */
static void bin_patch_name_and_info(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	unsigned int reg_inc_per_word = cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	u32 reg_val, payload_data;
	char *infobuf;
	unsigned int alg_base_words, reg_addr;
	struct firmware *fw;

	get_random_bytes(&payload_data, sizeof(payload_data));

	alg_base_words = cs_dsp_mock_xm_header_get_alg_base_in_words(priv,
							bin_test_mock_algs[0].id,
							WMFW_ADSP2_YM);

	/* Add a name block and info block */
	cs_dsp_mock_bin_add_name(priv->local->bin_builder, "The name");
	cs_dsp_mock_bin_add_info(priv->local->bin_builder, "Some info");

	/* Add a big block of info */
	infobuf = kunit_kzalloc(test, 512, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, infobuf);

	for (; strlcat(infobuf, "Waffle{Blah}\n", 512) < 512; )
		;

	cs_dsp_mock_bin_add_info(priv->local->bin_builder, infobuf);

	/* Add a patch */
	cs_dsp_mock_bin_add_patch(priv->local->bin_builder,
				  bin_test_mock_algs[0].id,
				  bin_test_mock_algs[0].ver,
				  WMFW_ADSP2_YM,
				  0,
				  &payload_data, sizeof(payload_data));

	fw = cs_dsp_mock_bin_get_firmware(priv->local->bin_builder);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, priv->local->wmfw, "mock_wmfw",
					fw, "mock_bin", "misc"),
			0);

	/* Content of registers should match payload_data */
	reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM);
	reg_addr += alg_base_words * reg_inc_per_word;
	reg_val = 0;
	KUNIT_EXPECT_EQ(test,
			regmap_raw_read(priv->dsp->regmap, reg_addr,
					&reg_val, sizeof(reg_val)),
			0);
	KUNIT_EXPECT_EQ(test, reg_val, payload_data);
}

static int cs_dsp_bin_test_common_init(struct kunit *test, struct cs_dsp *dsp)
{
	struct cs_dsp_test *priv;
	struct cs_dsp_mock_xm_header *xm_hdr;
	struct device *test_dev;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->local = kunit_kzalloc(test, sizeof(struct cs_dsp_test_local), GFP_KERNEL);
	if (!priv->local)
		return -ENOMEM;

	priv->test = test;
	priv->dsp = dsp;
	test->priv = priv;

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

	/* Create an XM header */
	xm_hdr = cs_dsp_create_mock_xm_header(priv,
					      bin_test_mock_algs,
					      ARRAY_SIZE(bin_test_mock_algs));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xm_hdr);
	ret = cs_dsp_mock_xm_header_write_to_regmap(xm_hdr);
	KUNIT_ASSERT_EQ(test, ret, 0);

	priv->local->bin_builder =
		cs_dsp_mock_bin_init(priv, 1,
				     cs_dsp_mock_xm_header_get_fw_version_from_regmap(priv));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->local->bin_builder);

	/* We must provide a dummy wmfw to load */
	priv->local->wmfw_builder = cs_dsp_mock_wmfw_init(priv, -1);
	priv->local->wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);

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

static int cs_dsp_bin_test_halo_init(struct kunit *test)
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

	return cs_dsp_bin_test_common_init(test, dsp);
}

static int cs_dsp_bin_test_adsp2_32bit_init(struct kunit *test)
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

	return cs_dsp_bin_test_common_init(test, dsp);
}

static int cs_dsp_bin_test_adsp2_16bit_init(struct kunit *test)
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

	return cs_dsp_bin_test_common_init(test, dsp);
}

/* Parameterize on choice of XM or YM with a range of word offsets */
static const struct bin_test_param x_or_y_and_offset_param_cases[] = {
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 0 },
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 1 },
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 2 },
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 3 },
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 4 },
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 23 },
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 22 },
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 21 },
	{ .mem_type = WMFW_ADSP2_XM, .offset_words = 20 },

	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 0 },
	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 1 },
	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 2 },
	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 3 },
	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 4 },
	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 23 },
	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 22 },
	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 21 },
	{ .mem_type = WMFW_ADSP2_YM, .offset_words = 20 },
};

/* Parameterize on ZM with a range of word offsets */
static const struct bin_test_param z_and_offset_param_cases[] = {
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 0 },
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 1 },
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 2 },
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 3 },
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 4 },
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 23 },
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 22 },
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 21 },
	{ .mem_type = WMFW_ADSP2_ZM, .offset_words = 20 },
};

/* Parameterize on choice of packed XM or YM with a range of word offsets */
static const struct bin_test_param packed_x_or_y_and_offset_param_cases[] = {
	{ .mem_type = WMFW_HALO_XM_PACKED, .offset_words = 0 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .offset_words = 4 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .offset_words = 8 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .offset_words = 12 },

	{ .mem_type = WMFW_HALO_YM_PACKED, .offset_words = 0 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .offset_words = 4 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .offset_words = 8 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .offset_words = 12 },
};

static void x_or_y_or_z_and_offset_param_desc(const struct bin_test_param *param,
					   char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s@%u",
		 cs_dsp_mem_region_name(param->mem_type),
		 param->offset_words);
}

KUNIT_ARRAY_PARAM(x_or_y_and_offset,
		  x_or_y_and_offset_param_cases,
		  x_or_y_or_z_and_offset_param_desc);

KUNIT_ARRAY_PARAM(z_and_offset,
		  z_and_offset_param_cases,
		  x_or_y_or_z_and_offset_param_desc);

KUNIT_ARRAY_PARAM(packed_x_or_y_and_offset,
		  packed_x_or_y_and_offset_param_cases,
		  x_or_y_or_z_and_offset_param_desc);

/* Parameterize on choice of packed XM or YM */
static const struct bin_test_param packed_x_or_y_param_cases[] = {
	{ .mem_type = WMFW_HALO_XM_PACKED, .offset_words = 0 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .offset_words = 0 },
};

static void x_or_y_or_z_param_desc(const struct bin_test_param *param,
					   char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s", cs_dsp_mem_region_name(param->mem_type));
}

KUNIT_ARRAY_PARAM(packed_x_or_y, packed_x_or_y_param_cases, x_or_y_or_z_param_desc);

static const struct bin_test_param offset_param_cases[] = {
	{ .offset_words = 0 },
	{ .offset_words = 1 },
	{ .offset_words = 2 },
	{ .offset_words = 3 },
	{ .offset_words = 4 },
	{ .offset_words = 23 },
	{ .offset_words = 22 },
	{ .offset_words = 21 },
	{ .offset_words = 20 },
};

static void offset_param_desc(const struct bin_test_param *param, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "@%u", param->offset_words);
}

KUNIT_ARRAY_PARAM(offset, offset_param_cases, offset_param_desc);

static const struct bin_test_param alg_param_cases[] = {
	{ .alg_idx = 0 },
	{ .alg_idx = 1 },
	{ .alg_idx = 2 },
	{ .alg_idx = 3 },
};

static void alg_param_desc(const struct bin_test_param *param, char *desc)
{
	WARN_ON(param->alg_idx >= ARRAY_SIZE(bin_test_mock_algs));

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "alg[%u] (%#x)",
		 param->alg_idx, bin_test_mock_algs[param->alg_idx].id);
}

KUNIT_ARRAY_PARAM(alg, alg_param_cases, alg_param_desc);

static const struct bin_test_param x_or_y_and_alg_param_cases[] = {
	{ .mem_type = WMFW_ADSP2_XM, .alg_idx = 0 },
	{ .mem_type = WMFW_ADSP2_XM, .alg_idx = 1 },
	{ .mem_type = WMFW_ADSP2_XM, .alg_idx = 2 },
	{ .mem_type = WMFW_ADSP2_XM, .alg_idx = 3 },

	{ .mem_type = WMFW_ADSP2_YM, .alg_idx = 0 },
	{ .mem_type = WMFW_ADSP2_YM, .alg_idx = 1 },
	{ .mem_type = WMFW_ADSP2_YM, .alg_idx = 2 },
	{ .mem_type = WMFW_ADSP2_YM, .alg_idx = 3 },
};

static void x_or_y_or_z_and_alg_param_desc(const struct bin_test_param *param, char *desc)
{
	WARN_ON(param->alg_idx >= ARRAY_SIZE(bin_test_mock_algs));

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s alg[%u] (%#x)",
		 cs_dsp_mem_region_name(param->mem_type),
		 param->alg_idx, bin_test_mock_algs[param->alg_idx].id);
}

KUNIT_ARRAY_PARAM(x_or_y_and_alg, x_or_y_and_alg_param_cases, x_or_y_or_z_and_alg_param_desc);

static const struct bin_test_param z_and_alg_param_cases[] = {
	{ .mem_type = WMFW_ADSP2_ZM, .alg_idx = 0 },
	{ .mem_type = WMFW_ADSP2_ZM, .alg_idx = 1 },
	{ .mem_type = WMFW_ADSP2_ZM, .alg_idx = 2 },
	{ .mem_type = WMFW_ADSP2_ZM, .alg_idx = 3 },
};

KUNIT_ARRAY_PARAM(z_and_alg, z_and_alg_param_cases, x_or_y_or_z_and_alg_param_desc);

static const struct bin_test_param packed_x_or_y_and_alg_param_cases[] = {
	{ .mem_type = WMFW_HALO_XM_PACKED, .alg_idx = 0 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .alg_idx = 1 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .alg_idx = 2 },
	{ .mem_type = WMFW_HALO_XM_PACKED, .alg_idx = 3 },

	{ .mem_type = WMFW_HALO_YM_PACKED, .alg_idx = 0 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .alg_idx = 1 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .alg_idx = 2 },
	{ .mem_type = WMFW_HALO_YM_PACKED, .alg_idx = 3 },
};

KUNIT_ARRAY_PARAM(packed_x_or_y_and_alg, packed_x_or_y_and_alg_param_cases,
		  x_or_y_or_z_and_alg_param_desc);

static struct kunit_case cs_dsp_bin_test_cases_halo[] = {
	/* Unpacked memory */
	KUNIT_CASE_PARAM(bin_patch_one_word, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_multiword, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword_unordered, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_mems, offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_mems, alg_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword_sparse_unordered, x_or_y_and_alg_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_algs, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_algs_unordered, x_or_y_and_offset_gen_params),

	/* Packed memory tests */
	KUNIT_CASE_PARAM(bin_patch_1_packed,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_1_single_trailing,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_2_single_trailing,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_3_single_trailing,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_2_trailing,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_3_trailing,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_single_leading_1_packed,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_2_single_leading_1_packed,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_2_leading_1_packed,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_3_single_leading_1_packed,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_3_leading_1_packed,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_onepacked,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_onepacked_unordered,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_multiple_mems, offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_multiple_mems, alg_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_onepacked_sparse_unordered,
			 packed_x_or_y_and_alg_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_multiple_algs,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_1_packed_multiple_algs_unordered,
			 packed_x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_mixed_packed_unpacked_random,
			 packed_x_or_y_gen_params),

	KUNIT_CASE(bin_patch_name_and_info),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_bin_test_cases_adsp2[] = {
	/* XM and YM */
	KUNIT_CASE_PARAM(bin_patch_one_word, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_multiword, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword_unordered, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword_sparse_unordered, x_or_y_and_alg_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_algs, x_or_y_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_algs_unordered, x_or_y_and_offset_gen_params),

	/* ZM */
	KUNIT_CASE_PARAM(bin_patch_one_word, z_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_multiword, z_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword, z_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword_unordered, z_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_multi_oneword_sparse_unordered, z_and_alg_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_algs, z_and_offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_algs_unordered, z_and_offset_gen_params),

	/* Other */
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_mems, offset_gen_params),
	KUNIT_CASE_PARAM(bin_patch_one_word_multiple_mems, alg_gen_params),

	KUNIT_CASE(bin_patch_name_and_info),

	{ } /* terminator */
};

static struct kunit_suite cs_dsp_bin_test_halo = {
	.name = "cs_dsp_bin_halo",
	.init = cs_dsp_bin_test_halo_init,
	.test_cases = cs_dsp_bin_test_cases_halo,
};

static struct kunit_suite cs_dsp_bin_test_adsp2_32bit = {
	.name = "cs_dsp_bin_adsp2_32bit",
	.init = cs_dsp_bin_test_adsp2_32bit_init,
	.test_cases = cs_dsp_bin_test_cases_adsp2,
};

static struct kunit_suite cs_dsp_bin_test_adsp2_16bit = {
	.name = "cs_dsp_bin_adsp2_16bit",
	.init = cs_dsp_bin_test_adsp2_16bit_init,
	.test_cases = cs_dsp_bin_test_cases_adsp2,
};

kunit_test_suites(&cs_dsp_bin_test_halo,
		  &cs_dsp_bin_test_adsp2_32bit,
		  &cs_dsp_bin_test_adsp2_16bit);
