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

struct cs_dsp_ctl_rw_test_param {
	int mem_type;
	int alg_id;
	unsigned int offs_words;
	unsigned int len_bytes;
	u16 ctl_type;
	u16 flags;
};

static const struct cs_dsp_mock_alg_def cs_dsp_ctl_rw_test_algs[] = {
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

static int _find_alg_entry(struct kunit *test, unsigned int alg_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_rw_test_algs); ++i) {
		if (cs_dsp_ctl_rw_test_algs[i].id == alg_id)
			break;
	}

	KUNIT_ASSERT_LT(test, i, ARRAY_SIZE(cs_dsp_ctl_rw_test_algs));

	return i;
}

static int _get_alg_mem_base_words(struct kunit *test, int alg_index, int mem_type)
{
	switch (mem_type) {
	case WMFW_ADSP2_XM:
		return cs_dsp_ctl_rw_test_algs[alg_index].xm_base_words;
	case WMFW_ADSP2_YM:
		return cs_dsp_ctl_rw_test_algs[alg_index].ym_base_words;
	case WMFW_ADSP2_ZM:
		return cs_dsp_ctl_rw_test_algs[alg_index].zm_base_words;
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
 * Write to a control while the firmware is running.
 * This should write to the underlying registers.
 */
static void cs_dsp_ctl_write_running(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
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

	/* Create some initial register content */
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
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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
	 * Write new data to the control, it should be written to the registers
	 * and cs_dsp_coeff_lock_and_write_ctrl() should return 1 to indicate
	 * that the control content changed.
	 */
	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			1);
	KUNIT_ASSERT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, param->len_bytes), 0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Read from a volatile control while the firmware is running.
 * This should return the current state of the underlying registers.
 */
static void cs_dsp_ctl_read_volatile_running(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
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

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	memset(reg_vals, 0, param->len_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Read the control, it should return the current register content */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);

	/*
	 * Change the register content and read the control, it should return
	 * the new register content
	 */
	get_random_bytes(reg_vals, param->len_bytes);
	KUNIT_ASSERT_EQ(test, regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes), 0);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, param->len_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, param->len_bytes);
}

/*
 * Read from a volatile control before the firmware is started.
 * This should return an error.
 */
static void cs_dsp_ctl_read_volatile_not_started(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Read the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);
}

/*
 * Read from a volatile control after the firmware has stopped.
 * This should return an error.
 */
static void cs_dsp_ctl_read_volatile_stopped(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	/* Read the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);
}

/*
 * Read from a volatile control after the DSP has been powered down.
 * This should return an error.
 */
static void cs_dsp_ctl_read_volatile_stopped_powered_down(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware then power down */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);

	/* Read the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);
}

/*
 * Read from a volatile control when a different firmware is currently
 * loaded into the DSP.
 * Should return an error.
 */
static void cs_dsp_ctl_read_volatile_not_current_loaded_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Read the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);
}

/*
 * Read from a volatile control when a different firmware is currently
 * running.
 * Should return an error.
 */
static void cs_dsp_ctl_read_volatile_not_current_running_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Start the firmware and add an action to stop it during cleanup */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/* Read the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);
}

/*
 * Write to a volatile control before the firmware is started.
 * This should return an error.
 */
static void cs_dsp_ctl_write_volatile_not_started(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	/* Write the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);

	/* Should not have been any writes to registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write to a volatile control after the firmware has stopped.
 * This should return an error.
 */
static void cs_dsp_ctl_write_volatile_stopped(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	/* Write the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);

	/* Should not have been any writes to registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write to a volatile control after the DSP has been powered down.
 * This should return an error.
 */
static void cs_dsp_ctl_write_volatile_stopped_powered_down(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kzalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	/* Start and stop the firmware then power down */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	/* Write the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);

	/* Should not have been any writes to registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write to a volatile control when a different firmware is currently
 * loaded into the DSP.
 * Should return an error.
 */
static void cs_dsp_ctl_write_volatile_not_current_loaded_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Write the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);

	/* Should not have been any writes to registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write to a volatile control when a different firmware is currently
 * running.
 * Should return an error.
 */
static void cs_dsp_ctl_write_volatile_not_current_running_fw(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_mock_wmfw_builder *builder2 = _create_dummy_wmfw(test);
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	reg_vals = kunit_kmalloc(test, param->len_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some DSP data to be read into the control cache */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, param->len_bytes);

	/* Create control pointing to this data */
	def.flags = param->flags | WMFW_CTL_FLAG_VOLATILE;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Start the firmware and add an action to stop it during cleanup */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, _cs_dsp_stop_wrapper, dsp), 0);

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	/* Write the control, it should return an error */
	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, param->len_bytes),
			0);

	/* Should not have been any writes to registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Read from an offset into the control data. Should return only the
 * portion of data from the offset position.
 */
static void cs_dsp_ctl_read_with_seek(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;
	unsigned int seek_words;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = 48;

	reg_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, def.length_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	for (seek_words = 1; seek_words < (def.length_bytes / sizeof(u32)); seek_words++) {
		unsigned int len_bytes = def.length_bytes - (seek_words * sizeof(u32));

		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, seek_words,
								readback, len_bytes),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, &reg_vals[seek_words], len_bytes);
	}
}

/*
 * Read from an offset into the control cache. Should return only the
 * portion of data from the offset position.
 * Same as cs_dsp_ctl_read_with_seek() except the control is cached
 * and the firmware is not running.
 */
static void cs_dsp_ctl_read_cache_with_seek(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;
	unsigned int seek_words;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = 48;

	reg_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, def.length_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Start and stop the firmware so the read will come from the cache */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	for (seek_words = 1; seek_words < (def.length_bytes / sizeof(u32)); seek_words++) {
		unsigned int len_bytes = def.length_bytes - (seek_words * sizeof(u32));

		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, seek_words,
								readback, len_bytes),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, &reg_vals[seek_words], len_bytes);
	}
}

/*
 * Read less than the full length of data from a control. Should return
 * only the requested number of bytes.
 */
static void cs_dsp_ctl_read_truncated(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;
	unsigned int len_bytes;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = 48;

	reg_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, def.length_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Reads are only allowed to be a multiple of the DSP word length */
	for (len_bytes = sizeof(u32); len_bytes < def.length_bytes; len_bytes += sizeof(u32)) {
		memset(readback, 0, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, len_bytes),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, len_bytes);
		KUNIT_EXPECT_MEMNEQ(test,
				    (u8 *)readback + len_bytes,
				    (u8 *)reg_vals + len_bytes,
				    def.length_bytes - len_bytes);
	}
}

/*
 * Read less than the full length of data from a cached control.
 * Should return only the requested number of bytes.
 * Same as cs_dsp_ctl_read_truncated() except the control is cached
 * and the firmware is not running.
 */
static void cs_dsp_ctl_read_cache_truncated(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback;
	unsigned int len_bytes;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = 48;

	reg_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, def.length_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Start and stop the firmware so the read will come from the cache */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	/* Reads are only allowed to be a multiple of the DSP word length */
	for (len_bytes = sizeof(u32); len_bytes < def.length_bytes; len_bytes += sizeof(u32)) {
		memset(readback, 0, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, len_bytes),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, len_bytes);
		KUNIT_EXPECT_MEMNEQ(test,
				    (u8 *)readback + len_bytes,
				    (u8 *)reg_vals + len_bytes,
				    def.length_bytes - len_bytes);
	}
}

/*
 * Write to an offset into the control data. Should only change the
 * portion of data from the offset position.
 */
static void cs_dsp_ctl_write_with_seek(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback, *new_data;
	unsigned int seek_words;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = 48;

	reg_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	new_data = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_data);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, def.length_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	for (seek_words = 1; seek_words < (def.length_bytes / sizeof(u32)); seek_words++) {
		unsigned int len_bytes = def.length_bytes - (seek_words * sizeof(u32));

		/* Reset the register values to the test data */
		regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

		get_random_bytes(new_data, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, seek_words,
								 new_data, len_bytes),
				1);
		KUNIT_ASSERT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, def.length_bytes),
				0);
		/* Initial portion of readback should be unchanged */
		KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, seek_words * sizeof(u32));
		KUNIT_EXPECT_MEMEQ(test, &readback[seek_words], new_data, len_bytes);
	}
}

/*
 * Write to an offset into the control cache. Should only change the
 * portion of data from the offset position.
 * Same as cs_dsp_ctl_write_with_seek() except the control is cached
 * and the firmware is not running.
 */
static void cs_dsp_ctl_write_cache_with_seek(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback, *new_data;
	unsigned int seek_words;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = 48;

	reg_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	new_data = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_data);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, def.length_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Start and stop the firmware so the read will come from the cache */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	for (seek_words = 1; seek_words < (def.length_bytes / sizeof(u32)); seek_words++) {
		unsigned int len_bytes = def.length_bytes - (seek_words * sizeof(u32));

		/* Reset the cache to the test data */
		KUNIT_EXPECT_GE(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals,
								 def.length_bytes),
				0);

		get_random_bytes(new_data, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, seek_words,
								 new_data, len_bytes),
				1);

		memset(readback, 0, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback,
								def.length_bytes),
				0);
		/* Initial portion of readback should be unchanged */
		KUNIT_EXPECT_MEMEQ(test, readback, reg_vals, seek_words * sizeof(u32));
		KUNIT_EXPECT_MEMEQ(test, &readback[seek_words], new_data, len_bytes);
	}
}

/*
 * Write less than the full length of data to a control. Should only
 * change the requested number of bytes.
 */
static void cs_dsp_ctl_write_truncated(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback, *new_data;
	unsigned int len_bytes;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = 48;

	reg_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	new_data = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_data);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, def.length_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Writes are only allowed to be a multiple of the DSP word length */
	for (len_bytes = sizeof(u32); len_bytes < def.length_bytes; len_bytes += sizeof(u32)) {
		/* Reset the register values to the test data */
		regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

		get_random_bytes(new_data, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, new_data, len_bytes),
				1);

		memset(readback, 0, def.length_bytes);
		KUNIT_ASSERT_EQ(test, regmap_raw_read(dsp->regmap, reg, readback, def.length_bytes),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, new_data, len_bytes);
		KUNIT_EXPECT_MEMEQ(test,
				   (u8 *)readback + len_bytes,
				   (u8 *)reg_vals + len_bytes,
				   def.length_bytes - len_bytes);
	}
}

/*
 * Write less than the full length of data to a cached control.
 * Should only change the requested number of bytes.
 * Same as cs_dsp_ctl_write_truncated() except the control is cached
 * and the firmware is not running.
 */
static void cs_dsp_ctl_write_cache_truncated(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals, *readback, *new_data;
	unsigned int len_bytes;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = 48;

	reg_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	readback = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	new_data = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_data);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	get_random_bytes(reg_vals, def.length_bytes);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);

	/* Start and stop the firmware so the read will come from the cache */
	KUNIT_ASSERT_EQ(test, cs_dsp_run(dsp), 0);
	cs_dsp_stop(dsp);

	/* Writes are only allowed to be a multiple of the DSP word length */
	for (len_bytes = sizeof(u32); len_bytes < def.length_bytes; len_bytes += sizeof(u32)) {
		/* Reset the cache to the test data */
		KUNIT_EXPECT_GE(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals,
								 def.length_bytes),
				0);

		get_random_bytes(new_data, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, new_data, len_bytes),
				1);

		memset(readback, 0, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback,
								def.length_bytes),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, new_data, len_bytes);
		KUNIT_EXPECT_MEMEQ(test,
				   (u8 *)readback + len_bytes,
				   (u8 *)reg_vals + len_bytes,
				   def.length_bytes - len_bytes);
	}
}

/*
 * Read from an offset that is beyond the end of the control data.
 * Should return an error.
 */
static void cs_dsp_ctl_read_with_seek_oob(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;
	unsigned int seek_words;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	reg_vals = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	seek_words = def.length_bytes / sizeof(u32);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, seek_words,
							reg_vals, def.length_bytes),
			0);

	if (!(def.flags & WMFW_CTL_FLAG_VOLATILE)) {
		/* Stop firmware and repeat the read from the cache */
		kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
		KUNIT_ASSERT_FALSE(test, dsp->running);

		KUNIT_EXPECT_LT(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, seek_words,
								reg_vals, def.length_bytes),
				0);
	}
}

/*
 * Read more data than the length of the control data.
 * Should return an error.
 */
static void cs_dsp_ctl_read_with_length_overflow(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	reg_vals = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, reg_vals, def.length_bytes + 1),
			0);

	if (!(def.flags & WMFW_CTL_FLAG_VOLATILE)) {
		/* Stop firmware and repeat the read from the cache */
		kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
		KUNIT_ASSERT_FALSE(test, dsp->running);

		KUNIT_EXPECT_LT(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, reg_vals,
								def.length_bytes + 1),
				0);
	}
}

/*
 * Read with a seek and length that ends beyond the end of control data.
 * Should return an error.
 */
static void cs_dsp_ctl_read_with_seek_and_length_oob(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	reg_vals = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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
	 * Read full control length but at a start offset of 1 so that
	 * offset + length exceeds the length of the control.
	 */
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 1, reg_vals, def.length_bytes),
			0);

	if (!(def.flags & WMFW_CTL_FLAG_VOLATILE)) {
		/* Stop firmware and repeat the read from the cache */
		kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
		KUNIT_ASSERT_FALSE(test, dsp->running);

		KUNIT_EXPECT_LT(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, 1, reg_vals,
								def.length_bytes),
				0);
	}
}

/*
 * Write to an offset that is beyond the end of the control data.
 * Should return an error without touching any registers.
 */
static void cs_dsp_ctl_write_with_seek_oob(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;
	unsigned int seek_words;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	reg_vals = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	get_random_bytes(reg_vals, def.length_bytes);
	seek_words = def.length_bytes / sizeof(u32);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, seek_words,
							 reg_vals, def.length_bytes),
			0);

	if (!(def.flags & WMFW_CTL_FLAG_VOLATILE)) {
		/* Stop firmware and repeat the write to the cache */
		kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
		KUNIT_ASSERT_FALSE(test, dsp->running);

		get_random_bytes(reg_vals, def.length_bytes);
		KUNIT_EXPECT_LT(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, seek_words,
								 reg_vals, def.length_bytes),
				0);
	}

	/* Check that it didn't write any registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write more data than the length of the control data.
 * Should return an error.
 */
static void cs_dsp_ctl_write_with_length_overflow(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	reg_vals = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	get_random_bytes(reg_vals, def.length_bytes);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, def.length_bytes + 1),
			0);

	if (!(def.flags & WMFW_CTL_FLAG_VOLATILE)) {
		/* Stop firmware and repeat the write to the cache */
		kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
		KUNIT_ASSERT_FALSE(test, dsp->running);

		get_random_bytes(reg_vals, def.length_bytes);
		KUNIT_EXPECT_LT(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals,
								 def.length_bytes + 1),
				0);
	}

	/* Check that it didn't write any registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Write with a seek and length that ends beyond the end of control data.
 * Should return an error.
 */
static void cs_dsp_ctl_write_with_seek_and_length_oob(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	reg_vals = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	/*
	 * Write full control length but at a start offset of 1 so that
	 * offset + length exceeeds the length of the control.
	 */
	get_random_bytes(reg_vals, def.length_bytes);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 1, reg_vals, def.length_bytes),
			0);

	if (!(def.flags & WMFW_CTL_FLAG_VOLATILE)) {
		/* Stop firmware and repeat the write to the cache */
		kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
		KUNIT_ASSERT_FALSE(test, dsp->running);

		get_random_bytes(reg_vals, def.length_bytes);
		KUNIT_EXPECT_LT(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 1, reg_vals,
								 def.length_bytes),
				0);
	}

	/* Check that it didn't write any registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

/*
 * Read from a write-only control. This is legal because controls can
 * always be read. Write-only only indicates that it is not useful to
 * populate the cache from the DSP memory.
 */
static void cs_dsp_ctl_read_from_writeonly(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *ctl_vals, *readback;

	/* Sanity check parameters */
	KUNIT_ASSERT_TRUE(test, param->flags & WMFW_CTL_FLAG_WRITEABLE);
	KUNIT_ASSERT_FALSE(test, param->flags & WMFW_CTL_FLAG_READABLE);

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	ctl_vals = kunit_kmalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctl_vals);

	readback = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, readback);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Write some test data to the control */
	get_random_bytes(ctl_vals, def.length_bytes);
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, ctl_vals, def.length_bytes),
			1);

	/* Read back the data */
	KUNIT_EXPECT_EQ(test,
			cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback, def.length_bytes),
			0);
	KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, def.length_bytes);

	if (!(def.flags & WMFW_CTL_FLAG_VOLATILE)) {
		/* Stop firmware and repeat the read from the cache */
		kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
		KUNIT_ASSERT_FALSE(test, dsp->running);

		memset(readback, 0, def.length_bytes);
		KUNIT_EXPECT_EQ(test,
				cs_dsp_coeff_lock_and_read_ctrl(ctl, 0, readback,
								def.length_bytes),
				0);
		KUNIT_EXPECT_MEMEQ(test, readback, ctl_vals, def.length_bytes);
	}
}

/*
 * Write to a read-only control.
 * This should return an error without writing registers.
 */
static void cs_dsp_ctl_write_to_readonly(struct kunit *test)
{
	const struct cs_dsp_ctl_rw_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp *dsp = priv->dsp;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	int alg_idx = _find_alg_entry(test, param->alg_id);
	unsigned int reg, alg_base_words;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 *reg_vals;

	/* Sanity check parameters */
	KUNIT_ASSERT_FALSE(test, param->flags & WMFW_CTL_FLAG_WRITEABLE);
	KUNIT_ASSERT_TRUE(test, param->flags & WMFW_CTL_FLAG_READABLE);

	def.flags = param->flags;
	def.mem_type = param->mem_type;
	def.offset_dsp_words = param->offs_words;
	def.length_bytes = param->len_bytes;

	reg_vals = kunit_kzalloc(test, def.length_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reg_vals);

	/* Create some initial register content */
	alg_base_words = _get_alg_mem_base_words(test, alg_idx, param->mem_type);
	reg = cs_dsp_mock_base_addr_for_mem(priv, param->mem_type);
	reg += (alg_base_words + param->offs_words) *
		cs_dsp_mock_reg_addr_inc_per_unpacked_word(priv);
	regmap_raw_write(dsp->regmap, reg, reg_vals, def.length_bytes);

	/* Create control pointing to this data */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_rw_test_algs[alg_idx].id,
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

	/* Drop expected writes and the regmap cache should be clean */
	cs_dsp_mock_xm_header_drop_from_regmap_cache(priv);
	cs_dsp_mock_regmap_drop_bytes(priv, reg, param->len_bytes);

	get_random_bytes(reg_vals, def.length_bytes);
	KUNIT_EXPECT_LT(test,
			cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals, def.length_bytes),
			0);

	if (!(def.flags & WMFW_CTL_FLAG_VOLATILE)) {
		/* Stop firmware and repeat the write to the cache */
		kunit_release_action(test, _cs_dsp_stop_wrapper, dsp);
		KUNIT_ASSERT_FALSE(test, dsp->running);

		get_random_bytes(reg_vals, def.length_bytes);
		KUNIT_EXPECT_LT(test,
				cs_dsp_coeff_lock_and_write_ctrl(ctl, 0, reg_vals,
								 def.length_bytes),
				0);
	}

	/* Check that it didn't write any registers */
	KUNIT_EXPECT_FALSE(test, cs_dsp_mock_regmap_is_dirty(priv, true));
}

static int cs_dsp_ctl_rw_test_common_init(struct kunit *test, struct cs_dsp *dsp,
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
							cs_dsp_ctl_rw_test_algs,
							ARRAY_SIZE(cs_dsp_ctl_rw_test_algs));
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

static int cs_dsp_ctl_rw_test_halo_init(struct kunit *test)
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

	return cs_dsp_ctl_rw_test_common_init(test, dsp, 3);
}

static int cs_dsp_ctl_rw_test_adsp2_32bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_ctl_rw_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_ctl_rw_test_adsp2_32bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_ctl_rw_test_adsp2_32bit_init(test, 1);
}

static int cs_dsp_ctl_rw_test_adsp2_32bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_ctl_rw_test_adsp2_32bit_init(test, 2);
}

static int cs_dsp_ctl_rw_test_adsp2_16bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_ctl_rw_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_ctl_rw_test_adsp2_16bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_ctl_rw_test_adsp2_16bit_init(test, 1);
}

static int cs_dsp_ctl_rw_test_adsp2_16bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_ctl_rw_test_adsp2_16bit_init(test, 2);
}

static void cs_dsp_ctl_all_param_desc(const struct cs_dsp_ctl_rw_test_param *param,
				      char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "alg:%#x %s@%u len:%u flags:%#x",
		 param->alg_id, cs_dsp_mem_region_name(param->mem_type),
		 param->offs_words, param->len_bytes, param->flags);
}

/* All parameters populated, with various lengths */
static const struct cs_dsp_ctl_rw_test_param all_pop_varying_len_cases[] = {
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
static const struct cs_dsp_ctl_rw_test_param all_pop_varying_offset_cases[] = {
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
static const struct cs_dsp_ctl_rw_test_param all_pop_varying_xy_cases[] = {
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_XM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
};
KUNIT_ARRAY_PARAM(all_pop_varying_xy, all_pop_varying_xy_cases,
		  cs_dsp_ctl_all_param_desc);

/* All parameters populated, using ZM */
static const struct cs_dsp_ctl_rw_test_param all_pop_z_cases[] = {
	{ .alg_id = 0xfafa, .mem_type = WMFW_ADSP2_ZM, .offs_words = 1, .len_bytes = 4 },
};
KUNIT_ARRAY_PARAM(all_pop_z, all_pop_z_cases, cs_dsp_ctl_all_param_desc);

/* All parameters populated, with various algorithm ids */
static const struct cs_dsp_ctl_rw_test_param all_pop_varying_alg_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0xb,      .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0x9f1234, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
	{ .alg_id = 0xff00ff, .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4 },
};
KUNIT_ARRAY_PARAM(all_pop_varying_alg, all_pop_varying_alg_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * readable control.
 */
static const struct cs_dsp_ctl_rw_test_param all_pop_readable_flags_cases[] = {
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
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS |
		   WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_readable_flags,
		  all_pop_readable_flags_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * read-only control
 */
static const struct cs_dsp_ctl_rw_test_param all_pop_readonly_flags_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_readonly_flags,
		  all_pop_readonly_flags_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * non-volatile readable control
 */
static const struct cs_dsp_ctl_rw_test_param all_pop_nonvol_readable_flags_cases[] = {
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
 * writeable control
 */
static const struct cs_dsp_ctl_rw_test_param all_pop_writeable_flags_cases[] = {
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
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS |
		   WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_writeable_flags,
		  all_pop_writeable_flags_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * write-only control
 */
static const struct cs_dsp_ctl_rw_test_param all_pop_writeonly_flags_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_writeonly_flags,
		  all_pop_writeonly_flags_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * non-volatile writeable control
 */
static const struct cs_dsp_ctl_rw_test_param all_pop_nonvol_writeable_flags_cases[] = {
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
 * volatile readable control.
 */
static const struct cs_dsp_ctl_rw_test_param all_pop_volatile_readable_flags_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = 0 /* flags == 0 is volatile while firmware is running */
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS |
		   WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_volatile_readable_flags,
		  all_pop_volatile_readable_flags_cases,
		  cs_dsp_ctl_all_param_desc);

/*
 * All parameters populated, with all combinations of flags for a
 * volatile readable control.
 */
static const struct cs_dsp_ctl_rw_test_param all_pop_volatile_writeable_flags_cases[] = {
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = 0 /* flags == 0 is volatile while firmware is running */
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE,
	},
	{ .alg_id = 0xfafa,   .mem_type = WMFW_ADSP2_YM, .offs_words = 1, .len_bytes = 4,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_SYS |
		   WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE,
	},
};
KUNIT_ARRAY_PARAM(all_pop_volatile_writeable_flags,
		  all_pop_volatile_writeable_flags_cases,
		  cs_dsp_ctl_all_param_desc);

static struct kunit_case cs_dsp_ctl_rw_test_cases_adsp[] = {
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_z_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running, all_pop_z_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running,
			 all_pop_volatile_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_not_started,
			 all_pop_volatile_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_stopped,
			 all_pop_volatile_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_stopped_powered_down,
			 all_pop_volatile_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_not_current_loaded_fw,
			 all_pop_volatile_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_not_current_running_fw,
			 all_pop_volatile_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_not_started,
			 all_pop_volatile_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_stopped,
			 all_pop_volatile_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_stopped_powered_down,
			 all_pop_volatile_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_not_current_loaded_fw,
			 all_pop_volatile_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_not_current_running_fw,
			 all_pop_volatile_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_with_seek,
			 all_pop_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_cache_with_seek,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_truncated,
			 all_pop_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_cache_truncated,
			 all_pop_nonvol_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_write_with_seek,
			 all_pop_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_cache_with_seek,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_truncated,
			 all_pop_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_cache_truncated,
			 all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_with_seek_oob,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_with_length_overflow,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_with_seek_and_length_oob,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_with_seek_oob,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_with_length_overflow,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_with_seek_and_length_oob,
			 all_pop_varying_len_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_from_writeonly,
			 all_pop_writeonly_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_to_readonly,
			 all_pop_readonly_flags_gen_params),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_ctl_rw_test_cases_halo[] = {
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_varying_alg_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_running, all_pop_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running, all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running, all_pop_varying_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running, all_pop_varying_xy_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_running,
			 all_pop_volatile_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_not_started,
			 all_pop_volatile_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_stopped,
			 all_pop_volatile_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_stopped_powered_down,
			 all_pop_volatile_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_not_current_loaded_fw,
			 all_pop_volatile_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_volatile_not_current_running_fw,
			 all_pop_volatile_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_not_started,
			 all_pop_volatile_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_stopped,
			 all_pop_volatile_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_stopped_powered_down,
			 all_pop_volatile_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_not_current_loaded_fw,
			 all_pop_volatile_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_volatile_not_current_running_fw,
			 all_pop_volatile_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_with_seek,
			 all_pop_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_cache_with_seek,
			 all_pop_nonvol_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_truncated,
			 all_pop_readable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_cache_truncated,
			 all_pop_nonvol_readable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_write_with_seek,
			 all_pop_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_cache_with_seek,
			 all_pop_nonvol_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_truncated,
			 all_pop_writeable_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_cache_truncated,
			 all_pop_nonvol_writeable_flags_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_with_seek_oob,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_with_length_overflow,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_read_with_seek_and_length_oob,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_with_seek_oob,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_with_length_overflow,
			 all_pop_varying_len_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_with_seek_and_length_oob,
			 all_pop_varying_len_gen_params),

	KUNIT_CASE_PARAM(cs_dsp_ctl_read_from_writeonly,
			 all_pop_writeonly_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_write_to_readonly,
			 all_pop_readonly_flags_gen_params),

	{ } /* terminator */
};

static struct kunit_suite cs_dsp_ctl_rw_test_halo = {
	.name = "cs_dsp_ctl_rw_wmfwV3_halo",
	.init = cs_dsp_ctl_rw_test_halo_init,
	.test_cases = cs_dsp_ctl_rw_test_cases_halo,
};

static struct kunit_suite cs_dsp_ctl_rw_test_adsp2_32bit_wmfw1 = {
	.name = "cs_dsp_ctl_rw_wmfwV1_adsp2_32bit",
	.init = cs_dsp_ctl_rw_test_adsp2_32bit_wmfw1_init,
	.test_cases = cs_dsp_ctl_rw_test_cases_adsp,
};

static struct kunit_suite cs_dsp_ctl_rw_test_adsp2_32bit_wmfw2 = {
	.name = "cs_dsp_ctl_rw_wmfwV2_adsp2_32bit",
	.init = cs_dsp_ctl_rw_test_adsp2_32bit_wmfw2_init,
	.test_cases = cs_dsp_ctl_rw_test_cases_adsp,
};

static struct kunit_suite cs_dsp_ctl_rw_test_adsp2_16bit_wmfw1 = {
	.name = "cs_dsp_ctl_rw_wmfwV1_adsp2_16bit",
	.init = cs_dsp_ctl_rw_test_adsp2_16bit_wmfw1_init,
	.test_cases = cs_dsp_ctl_rw_test_cases_adsp,
};

static struct kunit_suite cs_dsp_ctl_rw_test_adsp2_16bit_wmfw2 = {
	.name = "cs_dsp_ctl_rw_wmfwV2_adsp2_16bit",
	.init = cs_dsp_ctl_rw_test_adsp2_16bit_wmfw2_init,
	.test_cases = cs_dsp_ctl_rw_test_cases_adsp,
};

kunit_test_suites(&cs_dsp_ctl_rw_test_halo,
		  &cs_dsp_ctl_rw_test_adsp2_32bit_wmfw1,
		  &cs_dsp_ctl_rw_test_adsp2_32bit_wmfw2,
		  &cs_dsp_ctl_rw_test_adsp2_16bit_wmfw1,
		  &cs_dsp_ctl_rw_test_adsp2_16bit_wmfw2);
