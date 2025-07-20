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
#include <linux/list.h>
#include <linux/random.h>
#include <linux/regmap.h>

KUNIT_DEFINE_ACTION_WRAPPER(_put_device_wrapper, put_device, struct device *);
KUNIT_DEFINE_ACTION_WRAPPER(_cs_dsp_stop_wrapper, cs_dsp_stop, struct cs_dsp *);
KUNIT_DEFINE_ACTION_WRAPPER(_cs_dsp_remove_wrapper, cs_dsp_remove, struct cs_dsp *);

struct cs_dsp_test_local {
	struct cs_dsp_mock_xm_header *xm_header;
	struct cs_dsp_mock_wmfw_builder *wmfw_builder;
	int wmfw_version;
};

struct cs_dsp_ctl_cache_test_param {
	int mem_type;
	int alg_id;
	unsigned int offs_words;
	unsigned int len_bytes;
	u16 ctl_type;
	u16 flags;
};

static const struct cs_dsp_mock_alg_def cs_dsp_ctl_cache_test_algs[] = {
	{
		.id = 0xfafa,
		.ver = 0x100000,
		.xm_base_words = 60,
		.xm_size_words = 1000,
		.ym_base_words = 0,
		.ym_size_words = 1000,
		.zm_base_words = 0,
		.zm_size_words = 1000,
	},
	{
		.id = 0xb,
		.ver = 0x100001,
		.xm_base_words = 1060,
		.xm_size_words = 1000,
		.ym_base_words = 1000,
		.ym_size_words = 1000,
		.zm_base_words = 1000,
		.zm_size_words = 1000,
	},
	{
		.id = 0x9f1234,
		.ver = 0x100500,
		.xm_base_words = 2060,
		.xm_size_words = 32,
		.ym_base_words = 2000,
		.ym_size_words = 32,
		.zm_base_words = 2000,
		.zm_size_words = 32,
	},
	{
		.id = 0xff00ff,
		.ver = 0x300113,
		.xm_base_words = 2100,
		.xm_size_words = 32,
		.ym_base_words = 2032,
		.ym_size_words = 32,
		.zm_base_words = 2032,
		.zm_size_words = 32,
	},
};

static const struct cs_dsp_mock_coeff_def mock_coeff_template = {
	.shortname = "Dummy Coeff",
	.type = WMFW_CTL_TYPE_BYTES,
	.mem_type = WMFW_ADSP2_YM,
	.flags = WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	.length_bytes = 4,
};

static const char * const cs_dsp_ctl_cache_test_fw_names[] = {
	"misc", "mbc/vss", "haps",
};

static int _find_alg_entry(struct kunit *test, unsigned int alg_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_cache_test_algs); ++i) {
		if (cs_dsp_ctl_cache_test_algs[i].id == alg_id)
			break;
	}

	KUNIT_ASSERT_LT(test, i, ARRAY_SIZE(cs_dsp_ctl_cache_test_algs));

	return i;
}

static int _get_alg_mem_base_words(struct kunit *test, int alg_index, int mem_type)
{
	switch (mem_type) {
	case WMFW_ADSP2_XM:
		return cs_dsp_ctl_cache_test_algs[alg_index].xm_base_words;
	case WMFW_ADSP2_YM:
		return cs_dsp_ctl_cache_test_algs[alg_index].ym_base_words;
	case WMFW_ADSP2_ZM:
		return cs_dsp_ctl_cache_test_algs[alg_index].zm_base_words;
	default:
		KUNIT_FAIL(test, "Bug in test: illegal memory type %d\n", mem_type);
		return 0;
	}
}

static struct cs_dsp_mock_wmfw_builder *_create_dummy_wmfw(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_wmfw_builder *builder;

	builder = cs_dsp_mock_wmfw_init(priv, local->wmfw_version);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, builder);

	/* Init an XM header */
	cs_dsp_mock_wmfw_add_data_block(builder,
					WMFW_ADSP2_XM, 0,
					local->xm_header->blob_data,
					local->xm_header->blob_size_bytes);

	return builder;
}

/*
 * Memory allocated for control cache must be large enough.
 * This creates multiple controls of different sizes so only works on
 * wmfw V2 and later.
 */
static void cs_dsp_ctl_v2_cache_alloc(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	unsigned int reg, alg_base_words, alg_size_bytes;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	char ctl_name[4];
	u32 *reg_vals;
	int num_ctls;

	/* Create some DSP data to initialize the control cache */
	alg_base_words = _get_alg_mem_base_words(test, 0, WMFW_ADSP2_YM);
	alg_size_bytes = cs_dsp_ctl_cache_test_algs[0].ym_size_words *
			 cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	reg_vals = kunit_kzalloc(test, alg_size_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);
	reg = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM);
	reg += alg_base_words *	cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, alg_size_bytes);

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[0].id,
					      "dummyalg", NULL);

	/* Create controls of different sizes */
	def.mem_type = WMFW_ADSP2_YM;
	def.shortname = ctl_name;
	num_ctls = 0;
	for (def.length_bytes = 4; def.length_bytes <= 64; def.length_bytes += 4) {
		snprintf(ctl_name, ARRAY_SIZE(ctl_name), "%x", def.length_bytes);
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
		num_ctls++;
		def.offset_dsp_words += def.length_bytes / sizeof(u32);
	}
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	KUNIT_EXPECT_EQ(test, list_count_nodes(&dsp->ctl_list), num_ctls);

	/* Check that the block allocated for the cache is large enough */
	list_for_each_entry(ctl, &dsp->ctl_list, list)
		KUNIT_EXPECT_GE(test, ksize(ctl->cache), ctl->len);
}

/*
 * Content of registers backing a control should be read into the
 * control cache when the firmware is downloaded.
 */
static void cs_dsp_ctl_cache_init(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/*
	 * The data should have been populated into the control cache
	 * so should be readable through the control.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * For a non-volatile write-only control the cache should be zero-filled
 * when the firmware is downloaded.
 */
static void cs_dsp_ctl_cache_init_write_only(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *readback, *zeros;

	zeros = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, zeros);

	readback = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create a non-volatile write-only control */
	def.flags = param->flags & ~WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/*
	 * The control cache should have been zero-filled so should be
	 * readable through the control.
	 */
	get_random_bytes(readback, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, zeros, param->len_bytes);
}

/*
 * Multiple different firmware with identical controls.
 * This is legal because different firmwares could contain the same
 * algorithm.
 * The control cache should be initialized only with the data from
 * the firmware containing it.
 */
static void cs_dsp_ctl_cache_init_multiple_fw_same_controls(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder[3];
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *walkctl, *ctl[3];
	struct firmware *wmfw;
	u32 *reg_vals[3], *readback;
	int i;

	static_assert(ARRAY_SIZE(ctl) == ARRAY_SIZE(builder));
	static_assert(ARRAY_SIZE(reg_vals) == ARRAY_SIZE(builder));
	static_assert(ARRAY_SIZE(cs_dsp_ctl_cache_test_fw_names) >= ARRAY_SIZE(builder));

	/* Create an identical control in each firmware but with different alg id */
	for (i = 0; i < ARRAY_SIZE(builder); i++) {
		builder[i] = _create_dummy_wmfw(test);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, builder[i]);

		cs_dsp_mock_wmfw_start_alg_info_block(builder[i],
						      cs_dsp_ctl_cache_test_algs[0].id,
						      "dummyalg", NULL);
		cs_dsp_mock_wmfw_add_coeff_desc(builder[i], &def);
		cs_dsp_mock_wmfw_end_alg_info_block(builder[i]);
	}

	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		reg_vals[i] = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals[i]);
	}

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/*
	 * For each firmware create random content in the register backing
	 * the control. Then download, start, stop and power-down.
	 */
	for (i = 0; i < ARRAY_SIZE(builder); i++) {
		alg_base_words = _get_alg_mem_base_words(test, 0, def.mem_type);
		reg = cs_dsp_mock_base_addr_for_mem(priv, def.mem_type);
		reg += (alg_base_words + def.offset_dsp_words) *
			cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);

		get_random_bytes(reg_vals[i], def.length_bytes);
		regmap_raw_write(dsp->regmap, reg, reg_vals[i], def.length_bytes);
		wmfw = cs_dsp_mock_wmfw_get_firmware(builder[i]);
		KUNIT_ASSERT_EQ(test,
				cs_dsp_power_up(dsp, wmfw,
						cs_dsp_ctl_cache_test_fw_names[i],
						NULL, NULL,
						cs_dsp_ctl_cache_test_fw_names[i]),
				0);
		KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
		cs_dsp_stop(dsp);
		cs_dsp_power_down(dsp);
	}

	/* There should now be 3 controls */
	KUNIT_ASSERT_EQ(test, list_count_nodes(&dsp->ctl_list), 3);

	/*
	 * There's no requirement for the control list to be in any
	 * particular order, so don't assume the order.
	 */
	for (i = 0; i < ARRAY_SIZE(ctl); i++)
		ctl[i] = NULL;

	list_for_each_entry(walkctl, &dsp->ctl_list, list) {
		if (strcmp(walkctl->fw_name, cs_dsp_ctl_cache_test_fw_names[0]) == 0)
			ctl[0] = walkctl;
		else if (strcmp(walkctl->fw_name, cs_dsp_ctl_cache_test_fw_names[1]) == 0)
			ctl[1] = walkctl;
		else if (strcmp(walkctl->fw_name, cs_dsp_ctl_cache_test_fw_names[2]) == 0)
			ctl[2] = walkctl;
	}

	KUNIT_ASSERT_NOT_NULL(test, ctl[0]);
	KUNIT_ASSERT_NOT_NULL(test, ctl[1]);
	KUNIT_ASSERT_NOT_NULL(test, ctl[2]);

	/*
	 * The data should have been populated into the control cache
	 * so should be readable through the control.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[0], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[0], def.length_bytes);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[1], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[1], def.length_bytes);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[2], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[2], def.length_bytes);
}

/*
 * Multiple different firmware with controls identical except for alg id.
 * This is legal because the controls are qualified by algorithm id.
 * The control cache should be initialized only with the data from
 * the firmware containing it.
 */
static void cs_dsp_ctl_cache_init_multiple_fwalgid_same_controls(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder[3];
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *walkctl, *ctl[3];
	struct firmware *wmfw;
	u32 *reg_vals[3], *readback;
	int i;

	static_assert(ARRAY_SIZE(ctl) == ARRAY_SIZE(builder));
	static_assert(ARRAY_SIZE(reg_vals) == ARRAY_SIZE(builder));
	static_assert(ARRAY_SIZE(cs_dsp_ctl_cache_test_fw_names) >= ARRAY_SIZE(builder));

	/* Create an identical control in each firmware but with different alg id */
	for (i = 0; i < ARRAY_SIZE(builder); i++) {
		builder[i] = _create_dummy_wmfw(test);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, builder[i]);

		cs_dsp_mock_wmfw_start_alg_info_block(builder[i],
						      cs_dsp_ctl_cache_test_algs[i].id,
						      "dummyalg", NULL);
		cs_dsp_mock_wmfw_add_coeff_desc(builder[i], &def);
		cs_dsp_mock_wmfw_end_alg_info_block(builder[i]);
	}

	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		reg_vals[i] = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals[i]);
	}

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/*
	 * For each firmware create random content in the register backing
	 * the control. Then download, start, stop and power-down.
	 */
	for (i = 0; i < ARRAY_SIZE(builder); i++) {
		alg_base_words = _get_alg_mem_base_words(test, i, def.mem_type);
		reg = cs_dsp_mock_base_addr_for_mem(priv, def.mem_type);
		reg += (alg_base_words + def.offset_dsp_words) *
			cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);

		get_random_bytes(reg_vals[i], def.length_bytes);
		regmap_raw_write(dsp->regmap, reg, reg_vals[i], def.length_bytes);
		wmfw = cs_dsp_mock_wmfw_get_firmware(builder[i]);
		KUNIT_ASSERT_EQ(test,
				cs_dsp_power_up(dsp, wmfw,
						cs_dsp_ctl_cache_test_fw_names[i],
						NULL, NULL,
						cs_dsp_ctl_cache_test_fw_names[i]),
				0);
		KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
		cs_dsp_stop(dsp);
		cs_dsp_power_down(dsp);
	}

	/* There should now be 3 controls */
	KUNIT_ASSERT_EQ(test, list_count_nodes(&dsp->ctl_list), 3);

	/*
	 * There's no requirement for the control list to be in any
	 * particular order, so don't assume the order.
	 */
	for (i = 0; i < ARRAY_SIZE(ctl); i++)
		ctl[i] = NULL;

	list_for_each_entry(walkctl, &dsp->ctl_list, list) {
		if (cs_dsp_ctl_cache_test_algs[0].id == walkctl->alg_region.alg)
			ctl[0] = walkctl;
		else if (cs_dsp_ctl_cache_test_algs[1].id == walkctl->alg_region.alg)
			ctl[1] = walkctl;
		else if (cs_dsp_ctl_cache_test_algs[2].id == walkctl->alg_region.alg)
			ctl[2] = walkctl;
	}

	KUNIT_ASSERT_NOT_NULL(test, ctl[0]);
	KUNIT_ASSERT_NOT_NULL(test, ctl[1]);
	KUNIT_ASSERT_NOT_NULL(test, ctl[2]);

	/*
	 * The data should have been populated into the control cache
	 * so should be readable through the control.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[0], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[0], def.length_bytes);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[1], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[1], def.length_bytes);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[2], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[2], def.length_bytes);
}

/*
 * Firmware with controls at the same position in different memories.
 * The control cache should be initialized with content from the
 * correct memory region.
 */
static void cs_dsp_ctl_cache_init_multiple_mems(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *walkctl, *ctl[3];
	struct firmware *wmfw;
	u32 *reg_vals[3], *readback;
	int i;

	static_assert(ARRAY_SIZE(ctl) ==  ARRAY_SIZE(reg_vals));

	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		reg_vals[i] = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals[i]);
		get_random_bytes(reg_vals[i], def.length_bytes);
	}

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[0].id,
					      "dummyalg", NULL);

	/* Create controls identical except for memory region */
	def.mem_type = WMFW_ADSP2_YM;
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	def.mem_type = WMFW_ADSP2_XM;
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	if (cs_dsp_mock_has_zm(priv)) {
		def.mem_type = WMFW_ADSP2_ZM;
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	}

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Create random content in the registers backing each control */
	alg_base_words = _get_alg_mem_base_words(test, 0, WMFW_ADSP2_YM);
	reg = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM);
	reg += (alg_base_words + def.offset_dsp_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals[0], def.length_bytes);

	alg_base_words = _get_alg_mem_base_words(test, 0, WMFW_ADSP2_XM);
	reg = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_XM);
	reg += (alg_base_words + def.offset_dsp_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals[1], def.length_bytes);

	if (cs_dsp_mock_has_zm(priv)) {
		alg_base_words = _get_alg_mem_base_words(test, 0, WMFW_ADSP2_ZM);
		reg = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_ZM);
		reg += (alg_base_words + def.offset_dsp_words) *
			cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
		regmap_raw_write(dsp->regmap, reg, reg_vals[2], def.length_bytes);
	}

	/* Download, run, stop and power-down the firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);

	/* There should now be 2 or 3 controls */
	KUNIT_ASSERT_EQ(test, list_count_nodes(&dsp->ctl_list),
			cs_dsp_mock_has_zm(priv) ? 3 : 2);

	/*
	 * There's no requirement for the control list to be in any
	 * particular order, so don't assume the order.
	 */
	for (i = 0; i < ARRAY_SIZE(ctl); i++)
		ctl[i] = NULL;

	list_for_each_entry(walkctl, &dsp->ctl_list, list) {
		if (walkctl->alg_region.type == WMFW_ADSP2_YM)
			ctl[0] = walkctl;
		if (walkctl->alg_region.type == WMFW_ADSP2_XM)
			ctl[1] = walkctl;
		if (walkctl->alg_region.type == WMFW_ADSP2_ZM)
			ctl[2] = walkctl;
	}


	/*
	 * The data should have been populated into the control cache
	 * so should be readable through the control.
	 */
	KUNIT_ASSERT_NOT_NULL(test, ctl[0]);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[0], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[0], def.length_bytes);

	KUNIT_ASSERT_NOT_NULL(test, ctl[1]);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[1], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[1], def.length_bytes);

	if (cs_dsp_mock_has_zm(priv)) {
		KUNIT_ASSERT_NOT_NULL(test, ctl[2]);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl[2], 0, readback,
								def.length_bytes),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[2], def.length_bytes);
	}
}

/*
 * Firmware with controls at the same position in different algorithms
 * The control cache should be initialized with content from the
 * memory of the algorithm it points to.
 */
static void cs_dsp_ctl_cache_init_multiple_algs(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *walkctl, *ctl[3];
	struct firmware *wmfw;
	u32 *reg_vals[3], *readback;
	int i;

	static_assert(ARRAY_SIZE(ctl) ==  ARRAY_SIZE(reg_vals));
	static_assert(ARRAY_SIZE(reg_vals) <= ARRAY_SIZE(cs_dsp_ctl_cache_test_algs));

	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		reg_vals[i] = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals[i]);
		get_random_bytes(reg_vals[i], def.length_bytes);
	}

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create controls identical except for algorithm */
	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
						      cs_dsp_ctl_cache_test_algs[i].id,
						      "dummyalg", NULL);
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
		cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);
	}

	/* Create random content in the registers backing each control */
	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		alg_base_words = _get_alg_mem_base_words(test, i, def.mem_type);
		reg = cs_dsp_mock_base_addr_for_mem(priv, def.mem_type);
		reg += (alg_base_words + def.offset_dsp_words) *
			cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
		regmap_raw_write(dsp->regmap, reg, reg_vals[i], def.length_bytes);
	}

	/* Download, run, stop and power-down the firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);

	/* There should now be 3 controls */
	KUNIT_ASSERT_EQ(test, list_count_nodes(&dsp->ctl_list), 3);

	/*
	 * There's no requirement for the control list to be in any
	 * particular order, so don't assume the order.
	 */
	for (i = 0; i < ARRAY_SIZE(ctl); i++)
		ctl[i] = NULL;

	list_for_each_entry(walkctl, &dsp->ctl_list, list) {
		if (walkctl->alg_region.alg == cs_dsp_ctl_cache_test_algs[0].id)
			ctl[0] = walkctl;
		if (walkctl->alg_region.alg == cs_dsp_ctl_cache_test_algs[1].id)
			ctl[1] = walkctl;
		if (walkctl->alg_region.alg == cs_dsp_ctl_cache_test_algs[2].id)
			ctl[2] = walkctl;
	}

	KUNIT_ASSERT_NOT_NULL(test, ctl[0]);
	KUNIT_ASSERT_NOT_NULL(test, ctl[1]);
	KUNIT_ASSERT_NOT_NULL(test, ctl[2]);

	/*
	 * The data should have been populated into the control cache
	 * so should be readable through the control.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[0], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[0], def.length_bytes);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[1], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[1], def.length_bytes);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[2], 0, readback,
							def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[2], def.length_bytes);
}

/*
 * Firmware with controls in the same algorithm and memory but at
 * different offsets.
 * The control cache should be initialized with content from the
 * correct offset.
 * Only for wmfw format V2 and later. V1 only supports one control per
 * memory per algorithm.
 */
static void cs_dsp_ctl_cache_init_multiple_offsets(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	unsigned int reg, alg_base_words, alg_base_reg;
	struct cs_dsp_coeff_ctl *walkctl, *ctl[3];
	struct firmware *wmfw;
	u32 *reg_vals[3], *readback;
	int i;

	static_assert(ARRAY_SIZE(ctl) ==  ARRAY_SIZE(reg_vals));
	static_assert(ARRAY_SIZE(reg_vals) <= ARRAY_SIZE(cs_dsp_ctl_cache_test_algs));

	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		reg_vals[i] = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals[i]);
		get_random_bytes(reg_vals[i], def.length_bytes);
	}

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[0].id,
					      "dummyalg", NULL);

	/* Create controls identical except for offset */
	def.offset_dsp_words = 0;
	def.shortname = "CtlA";
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	def.offset_dsp_words = 5;
	def.shortname = "CtlB";
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	def.offset_dsp_words = 8;
	def.shortname = "CtlC";
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Create random content in the registers backing each control */
	alg_base_words = _get_alg_mem_base_words(test, 0, def.mem_type);
	alg_base_reg = cs_dsp_mock_base_addr_for_mem(priv, def.mem_type);
	alg_base_reg += alg_base_words * cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);

	reg = alg_base_reg;
	regmap_raw_write(dsp->regmap, reg, reg_vals[0], def.length_bytes);
	reg = alg_base_reg + (5 * cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv));
	regmap_raw_write(dsp->regmap, reg, reg_vals[1], def.length_bytes);
	reg = alg_base_reg + (8 * cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv));
	regmap_raw_write(dsp->regmap, reg, reg_vals[2], def.length_bytes);

	/* Download, run, stop and power-down the firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);

	/* There should now be 3 controls */
	KUNIT_ASSERT_EQ(test, list_count_nodes(&dsp->ctl_list), 3);

	/*
	 * There's no requirement for the control list to be in any
	 * particular order, so don't assume the order.
	 */
	for (i = 0; i < ARRAY_SIZE(ctl); i++)
		ctl[i] = NULL;

	list_for_each_entry(walkctl, &dsp->ctl_list, list) {
		if (walkctl->offset == 0)
			ctl[0] = walkctl;
		if (walkctl->offset == 5)
			ctl[1] = walkctl;
		if (walkctl->offset == 8)
			ctl[2] = walkctl;
	}

	KUNIT_ASSERT_NOT_NULL(test, ctl[0]);
	KUNIT_ASSERT_NOT_NULL(test, ctl[1]);
	KUNIT_ASSERT_NOT_NULL(test, ctl[2]);

	/*
	 * The data should have been populated into the control cache
	 * so should be readable through the control.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[0], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[0], def.length_bytes);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[1], 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[1], def.length_bytes);

	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl[2], 0, readback,
							def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals[2], def.length_bytes);
}

/*
 * Read from a cached control before the firmware is started.
 * Should return the data in the cache.
 */
static void cs_dsp_ctl_cache_read_not_started(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP but don't start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the data from the control cache */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Read from a cached control after the firmware has been stopped.
 * Should return the data in the cache.
 */
static void cs_dsp_ctl_cache_read_stopped(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the data from the control cache */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Read from a cached control after the DSP has been powered-up and
 * then powered-down without running.
 * Should return the data in the cache.
 */
static void cs_dsp_ctl_cache_read_powered_down(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP then power-down */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(dsp);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the data from the control cache */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Read from a cached control after the firmware has been run and
 * stopped, then the DSP has been powered-down.
 * Should return the data in the cache.
 */
static void cs_dsp_ctl_cache_read_stopped_powered_down(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware then power-down */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the data from the control cache */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Read from a cached control when a different firmware is currently
 * loaded into the DSP.
 * Should return the data in the cache.
 */
static void cs_dsp_ctl_cache_read_not_current_loaded_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Power-down DSP then power-up with a different firmware */
	cs_dsp_power_down(dsp);
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw2", NULL, NULL, "mbc.vss"), 0);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the data from the control cache */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Read from a cached control when a different firmware is currently
 * running.
 * Should return the data in the cache.
 */
static void cs_dsp_ctl_cache_read_not_current_running_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP then power-down */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(dsp);

	/* Power-up with a different firmware and run it */
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw2", NULL, NULL, "mbc.vss"), 0);
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the data from the control cache */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Read from a cached control with non-zero flags while the firmware is
 * running.
 * Should return the data in the cache, not from the registers.
 */
static void cs_dsp_ctl_cache_read_running(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *init_reg_vals, *new_reg_vals, *readback;

	init_reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, init_reg_vals);

	new_reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create data in the registers backing the control */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(init_reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, init_reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start the firmware running */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/*
	 * Change the values in the registers backing the control then drop
	 * them from the regmap cache. This allows checking that the control
	 * read is returning values from the control cache and not accessing
	 * the registers.
	 */
	KUNIT_ASSERT_EQ(test,
			regmap_raw_write(dsp->regmap, reg, new_reg_vals, param->len_bytes),
			0);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	/* Control should readback the origin data from its cache */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, init_reg_vals, param->len_bytes);

	/* Stop and power-down the DSP */
	kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
	cs_dsp_power_down(dsp);

	/* Control should readback from the cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, init_reg_vals, param->len_bytes);
}

/*
 * Read from a cached control with flags == 0 while the firmware is
 * running.
 * Should behave as volatile and read from the registers.
 * (This is for backwards compatibility with old firmware versions)
 */
static void cs_dsp_ctl_cache_read_running_zero_flags(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *init_reg_vals, *new_reg_vals, *readback;

	init_reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, init_reg_vals);

	new_reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Zero-fill the registers backing the control */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, init_reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = 0;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start the firmware running */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/* Change the values in the registers backing the control */
	get_random_bytes(new_reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, new_reg_vals, param->len_bytes);

	/* Control should readback the new data from the registers */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, new_reg_vals, param->len_bytes);

	/* Stop and power-down the DSP */
	kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
	cs_dsp_power_down(dsp);

	/* Change the values in the registers backing the control */
	regmap_raw_write(dsp->regmap, reg, init_reg_vals, param->len_bytes);

	/* Control should readback from the cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, new_reg_vals, param->len_bytes);
}

/*
 * Write to a cached control while the firmware is running.
 * This should be a writethrough operation, writing to the cache and
 * the registers.
 */
static void cs_dsp_ctl_cache_writethrough(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	memset(reg_vals, 0, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Start the firmware and add an action to stop it during cleanup */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/* Write new data to the control, it should be written to the registers */
	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);
	KUNIT_ASSERT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write unchanged data to a cached control while the firmware is running.
 * The control write should return 0 to indicate that the content
 * didn't change.
 */
static void cs_dsp_ctl_cache_writethrough_unchanged(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Start the firmware and add an action to stop it during cleanup */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/*
	 * If the control is write-only the cache will have been zero-initialized
	 * so the first write will always indicate a change.
	 */
	if (def.flags && !(def.flags & WMFW_CTL_FLAG_READABLE)) {
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals,
								 param->len_bytes),
				1);
	}

	/*
	 * Write the same data to the control, cs_dsp_coeff_lock_and_write_ctrl()
	 * should return 0 to indicate the content didn't change.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);
	KUNIT_ASSERT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write unchanged data to a cached control while the firmware is not started.
 * The control write should return 0 to indicate that the cache content
 * didn't change.
 */
static void cs_dsp_ctl_cache_write_unchanged_not_started(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/*
	 * If the control is write-only the cache will have been zero-initialized
	 * so the first write will always indicate a change.
	 */
	if (def.flags && !(def.flags & WMFW_CTL_FLAG_READABLE)) {
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals,
								 param->len_bytes),
				1);
	}

	/*
	 * Write the same data to the control, cs_dsp_coeff_lock_and_write_ctrl()
	 * should return 0 to indicate the content didn't change.
	 */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);
	KUNIT_ASSERT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control while the firmware is loaded but not
 * started.
 * This should write to the cache only.
 */
static void cs_dsp_ctl_cache_write_not_started(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP but don't start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Write new data to the control, it should not be written to the registers */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	/* Registers should not have been written so regmap cache should still be clean */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control after the firmware has been loaded,
 * started and stopped.
 * This should write to the cache only.
 */
static void cs_dsp_ctl_cache_write_stopped(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Write new data to the control, it should not be written to the registers */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	/* Registers should not have been written so regmap cache should still be clean */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control after the firmware has been loaded,
 * then the DSP powered-down.
 * This should write to the cache only.
 */
static void cs_dsp_ctl_cache_write_powered_down(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP then power-down */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(dsp);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Write new data to the control, it should not be written to the registers */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	/* Registers should not have been written so regmap cache should still be clean */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control after the firmware has been loaded,
 * started, stopped, and then the DSP powered-down.
 * This should write to the cache only.
 */
static void cs_dsp_ctl_cache_write_stopped_powered_down(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware then power-down */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Write new data to the control, it should not be written to the registers */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	/* Registers should not have been written so regmap cache should still be clean */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control that is not in the currently loaded firmware.
 * This should write to the cache only.
 */
static void cs_dsp_ctl_cache_write_not_current_loaded_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Get the control */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Power-down DSP then power-up with a different firmware */
	cs_dsp_power_down(dsp);
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw2", NULL, NULL, "mbc.vss"), 0);

	/* Control from unloaded firmware should be disabled */
	KUNIT_EXPECT_FALSE(test, ctl->enabled);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/*
	 * It should be possible to write new data to the control from
	 * the first firmware. But this should not be written to the
	 * registers.
	 */
	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	/* Registers should not have been written so regmap cache should still be clean */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control that is not in the currently running firmware.
 * This should write to the cache only.
 */
static void cs_dsp_ctl_cache_write_not_current_running_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP then power-down */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(dsp);

	/* Get the control */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Power-up with a different firmware and run it */
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw2", NULL, NULL, "mbc.vss"), 0);
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/* Control from unloaded firmware should be disabled */
	KUNIT_EXPECT_FALSE(test, ctl->enabled);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/*
	 * It should be possible to write new data to the control from
	 * the first firmware. But this should not be written to the
	 * registers.
	 */
	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	/* Registers should not have been written so regmap cache should still be clean */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control before running the firmware.
 * The value written to the cache should be synced out to the registers
 * backing the control when the firmware is run.
 */
static void cs_dsp_ctl_cache_sync_write_before_run(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP but don't start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Write new data to the control, it should not be written to the registers */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMNEQ(test, readback, reg_vals, param->len_bytes);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control while the firmware is running.
 * The value written should be synced out to the registers
 * backing the control when the firmware is next run.
 */
static void cs_dsp_ctl_cache_sync_write_while_running(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *init_vals, *ctl_vals, *readback;

	init_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, init_vals);

	ctl_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctl_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Zero-fill the registers backing the control */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP and start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/* Write new data to the control */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(ctl_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, ctl_vals, param->len_bytes),
			1);

	/* Stop firmware and zero the registers backing the control */
	kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);
	KUNIT_ASSERT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, init_vals, param->len_bytes);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);
}

/*
 * Write to a cached control after stopping the firmware.
 * The value written to the cache should be synced out to the registers
 * backing the control when the firmware is next run.
 */
static void cs_dsp_ctl_cache_sync_write_after_stop(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP but don't start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	/* Write new data to the control, it should not be written to the registers */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMNEQ(test, readback, reg_vals, param->len_bytes);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Write to a cached control that is not in the currently loaded firmware.
 * The value written to the cache should be synced out to the registers
 * backing the control the next time the firmware containing the
 * control is run.
 */
static void cs_dsp_ctl_cache_sync_write_not_current_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP but don't start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Get the control */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Power-down DSP then power-up with a different firmware */
	cs_dsp_power_down(dsp);
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw2", NULL, NULL, "mbc.vss"), 0);

	/* Write new data to the control, it should not be written to the registers */
	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);

	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMNEQ(test, readback, reg_vals, param->len_bytes);

	/* Power-down DSP then power-up with the original firmware */
	cs_dsp_power_down(dsp);
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * The value in the control cache should be synced out to the registers
 * backing the control every time the firmware containing the control
 * is run.
 */
static void cs_dsp_ctl_cache_sync_reapply_every_run(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *init_vals, *readback, *ctl_vals;

	init_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, init_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	ctl_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctl_vals);

	/* Zero-fill the registers backing the control */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP but don't start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Write new data to the control */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(ctl_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, ctl_vals, param->len_bytes),
			1);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);
	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);

	/* Stop the firmware and reset the registers */
	kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);

	/* Start the firmware again and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);
	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);
}

/*
 * The value in the control cache should be retained if the same
 * firmware is downloaded again. It should be synced out to the
 * registers backing the control after the firmware containing the
 * control is downloaded again and run.
 */
static void cs_dsp_ctl_cache_sync_reapply_after_fw_reload(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *init_vals, *readback, *ctl_vals;

	init_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, init_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	ctl_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctl_vals);

	/* Zero-fill the registers backing the control */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP but don't start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Write new data to the control */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(ctl_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, ctl_vals, param->len_bytes),
			1);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);
	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);

	/* Stop the firmware and power-down the DSP */
	kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
	cs_dsp_power_down(dsp);

	/* Reset the registers */
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);

	/* Download the firmware again, the cache content should not change */
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);
	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);
}

/*
 * The value in the control cache should be retained after a different
 * firmware is downloaded.
 * When the firmware containing the control is downloaded and run
 * the value in the control cache should be synced out to the registers
 * backing the control.
 */
static void cs_dsp_ctl_cache_sync_reapply_after_fw_swap(struct kunit *test)
{
	const struct cs_dsp_ctl_cache_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *init_vals, *readback, *ctl_vals;

	init_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, init_vals);

	readback = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	ctl_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctl_vals);

	/* Zero-fill the registers backing the control */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_cache_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Power-up DSP but don't start firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Write new data to the control */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	get_random_bytes(ctl_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, ctl_vals, param->len_bytes),
			1);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);
	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);

	/* Stop the firmware and power-down the DSP */
	kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
	cs_dsp_power_down(dsp);

	/* Reset the registers */
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);

	/* Download and run a different firmware */
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw2", NULL, NULL, "mbc.vss"), 0);
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_power_down(dsp);

	/* Reset the registers */
	regmap_raw_write(dsp->regmap, reg, init_vals, param->len_bytes);

	/* Download the original firmware again */
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	KUNIT_EXPECT_TRUE(test, ctl->set);

	/* Start the firmware and the cached data should be written to registers */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);
	KUNIT_EXPECT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);

	/* Control should readback the new data from the control cache */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, param->len_bytes);
}

static int cs_dsp_ctl_cache_test_common_init(struct kunit *test, struct cs_dsp *dsp,
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
	 * a dummy one that tests can use and extract it to a data blob.
	 */
	local->xm_header = cs_dsp_create_mock_xm_header(priv,
							cs_dsp_ctl_cache_test_algs,
							ARRAY_SIZE(cs_dsp_ctl_cache_test_algs));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->xm_header);

	/* Create wmfw builder */
	local->wmfw_builder = _create_dummy_wmfw(test);

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

static int cs_dsp_ctl_cache_test_halo_init(struct kunit *test)
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

	return cs_dsp_ctl_cache_test_common_init(test, dsp, 3);
}

static int cs_dsp_ctl_cache_test_adsp2_32bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_ctl_cache_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_ctl_cache_test_adsp2_32bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_ctl_cache_test_adsp2_32bit_init(test, 1);
}

static int cs_dsp_ctl_cache_test_adsp2_32bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_ctl_cache_test_adsp2_32bit_init(test, 2);
}

static int cs_dsp_ctl_cache_test_adsp2_16bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_ctl_cache_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_ctl_cache_test_adsp2_16bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_ctl_cache_test_adsp2_16bit_init(test, 1);
}

static int cs_dsp_ctl_cache_test_adsp2_16bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_ctl_cache_test_adsp2_16bit_init(test, 2);
}

static void cs_dsp_ctl_all_param_desc(const struct cs_dsp_ctl_cache_test_param *param,
				      char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "alg:%#x %s@%u len:%u flags:%#x",
		 param->alg_id, cs_dsp_mem_region_name(param->mem_type),
		 param->offs_words, param->len_bytes, param->flags);
}

/* All parameters populated, with various lengths */
static const struct cs_dsp_ctl_cache_test_param all_pop_varying_len_cases[] = {
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 8 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 12 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 16 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 48 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 100 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 512 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 1000 },
};
KUNIT_ARRAY_PARAM(all_pop_varying_len, all_pop_varying_len_cases,
		  cs_dsp_ctl_all_param_desc);

/* All parameters populated, with various offsets */
static const struct cs_dsp_ctl_cache_test_param all_pop_varying_offset_cases[] = {
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 0,   .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1,   .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 2,   .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 3,   .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 8,   .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 10,  .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 128, .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 180, .len_bytes = 4 },
};
KUNIT_ARRAY_PARAM(all_pop_varying_offset, all_pop_varying_offset_cases,
		  cs_dsp_ctl_all_param_desc);

/* All parameters populated, with various X and Y memory regions */
static const struct cs_dsp_ctl_cache_test_param all_pop_varying_xy_cases[] = {
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_XM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
};
KUNIT_ARRAY_PARAM(all_pop_varying_xy, all_pop_varying_xy_cases,
		  cs_dsp_ctl_all_param_desc);

/* All parameters populated, using ZM */
static const struct cs_dsp_ctl_cache_test_param all_pop_z_cases[] = {
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_ZM, .offs_words = 1, .len_bytes = 4 },
};
KUNIT_ARRAY_PARAM(all_pop_z, all_pop_z_cases, cs_dsp_ctl_all_param_desc);

/* All parameters populated, with various algorithm ids */
static const struct cs_dsp_ctl_cache_test_param all_pop_varying_alg_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0xb,      .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0x9f1234, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0xff00ff, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
};
KUNIT_ARRAY_PARAM(all_pop_varying_alg, all_pop_varying_alg_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * non-volatile readable control
 */
static const struct cs_dsp_ctl_cache_test_param all_pop_nonvol_readable_flags_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = 0
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_nonvol_readable_flags,
		  all_pop_nonvol_readable_flags_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * non-volatile readable control, except flags==0
 */
static const struct cs_dsp_ctl_cache_test_param all_pop_nonvol_readable_nonzero_flags_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_nonvol_readable_nonzero_flags,
		  all_pop_nonvol_readable_nonzero_flags_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * non-volatile writeable control
 */
static const struct cs_dsp_ctl_cache_test_param all_pop_nonvol_writeable_flags_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = 0
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_nonvol_writeable_flags,
		  all_pop_nonvol_writeable_flags_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * non-volatile write-only control of varying lengths
 */
static const struct cs_dsp_ctl_cache_test_param all_pop_nonvol_write_only_length_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 512,
	  .flags = WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 512,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_nonvol_write_only_length,
		  all_pop_nonvol_write_only_length_cases,
		  cs_dsp_ctl_all_param_desc);

static struct kunit_case cs_dsp_ctl_cache_test_cases_v1[] = {
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_z_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_nonvol_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init_write_only,
			 all_pop_nonvol_write_only_length_gen_params),

	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_fw_same_controls),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_fwalgid_same_controls),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_mems),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_algs),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_started,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_stopped,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_powered_down,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_stopped_powered_down,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_current_loaded_fw,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_current_running_fw,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_running,
			 all_pop_nonvol_readable_nonzero_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_running_zero_flags,
			 all_pop_varying_len_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_z_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_z_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_unchanged_not_started,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_started,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_stopped,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_powered_down,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_stopped_powered_down,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_current_loaded_fw,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_current_running_fw,
			 all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_before_run,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_while_running,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_after_stop,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_not_current_fw,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_every_run,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_after_fw_reload,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_after_fw_swap,
			 all_pop_nonvol_writeable_flags_gen_params),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_ctl_cache_test_cases_v2[] = {
	KUNIT_CASE(cs_dsp_ctl_v2_cache_alloc),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_z_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_nonvol_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init_write_only,
			 all_pop_nonvol_write_only_length_gen_params),

	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_fw_same_controls),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_fwalgid_same_controls),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_mems),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_algs),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_offsets),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_started,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_stopped,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_powered_down,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_stopped_powered_down,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_current_loaded_fw,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_current_running_fw,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_running,
			 all_pop_nonvol_readable_nonzero_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_running_zero_flags,
			 all_pop_varying_len_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_z_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_z_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_unchanged_not_started,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_started,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_stopped,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_powered_down,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_stopped_powered_down,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_current_loaded_fw,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_current_running_fw,
			 all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_before_run,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_while_running,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_after_stop,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_not_current_fw,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_every_run,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_after_fw_reload,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_after_fw_swap,
			 all_pop_nonvol_writeable_flags_gen_params),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_ctl_cache_test_cases_v3[] = {
	KUNIT_CASE(cs_dsp_ctl_v2_cache_alloc),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init, all_pop_nonvol_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_init_write_only,
			 all_pop_nonvol_write_only_length_gen_params),

	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_fw_same_controls),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_fwalgid_same_controls),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_mems),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_algs),
	KUNIT_CASE(cs_dsp_ctl_cache_init_multiple_offsets),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_started,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_stopped,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_powered_down,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_stopped_powered_down,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_current_loaded_fw,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_not_current_running_fw,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_read_running,
			 all_pop_nonvol_readable_nonzero_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough, all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_writethrough_unchanged,
			 all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_unchanged_not_started,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_started,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_stopped,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_powered_down,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_stopped_powered_down,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_current_loaded_fw,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_write_not_current_running_fw,
			 all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_before_run,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_while_running,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_after_stop,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_write_not_current_fw,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_every_run,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_after_fw_reload,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_cache_sync_reapply_after_fw_swap,
			 all_pop_nonvol_writeable_flags_gen_params),

	{ } /* terminator */
};

static struct kunit_suite cs_dsp_ctl_cache_test_halo = {
	.name = "cs_dsp_ctl_cache_wmfwV3_halo",
	.init = cs_dsp_ctl_cache_test_halo_init,
	.test_cases = cs_dsp_ctl_cache_test_cases_v3,
};

static struct kunit_suite cs_dsp_ctl_cache_test_adsp2_32bit_wmfw1 = {
	.name = "cs_dsp_ctl_cache_wmfwV1_adsp2_32bit",
	.init = cs_dsp_ctl_cache_test_adsp2_32bit_wmfw1_init,
	.test_cases = cs_dsp_ctl_cache_test_cases_v1,
};

static struct kunit_suite cs_dsp_ctl_cache_test_adsp2_32bit_wmfw2 = {
	.name = "cs_dsp_ctl_cache_wmfwV2_adsp2_32bit",
	.init = cs_dsp_ctl_cache_test_adsp2_32bit_wmfw2_init,
	.test_cases = cs_dsp_ctl_cache_test_cases_v2,
};

static struct kunit_suite cs_dsp_ctl_cache_test_adsp2_16bit_wmfw1 = {
	.name = "cs_dsp_ctl_cache_wmfwV1_adsp2_16bit",
	.init = cs_dsp_ctl_cache_test_adsp2_16bit_wmfw1_init,
	.test_cases = cs_dsp_ctl_cache_test_cases_v1,
};

static struct kunit_suite cs_dsp_ctl_cache_test_adsp2_16bit_wmfw2 = {
	.name = "cs_dsp_ctl_cache_wmfwV2_adsp2_16bit",
	.init = cs_dsp_ctl_cache_test_adsp2_16bit_wmfw2_init,
	.test_cases = cs_dsp_ctl_cache_test_cases_v2,
};

kunit_test_suites(&cs_dsp_ctl_cache_test_halo,
		  &cs_dsp_ctl_cache_test_adsp2_32bit_wmfw1,
		  &cs_dsp_ctl_cache_test_adsp2_32bit_wmfw2,
		  &cs_dsp_ctl_cache_test_adsp2_16bit_wmfw1,
		  &cs_dsp_ctl_cache_test_adsp2_16bit_wmfw2);
