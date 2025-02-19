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
#include <linux/mutex.h>
#include <linux/regmap.h>

KUNIT_DEFINE_ACTION_WRAPPER(_put_device_wrapper, put_device, struct device *);
KUNIT_DEFINE_ACTION_WRAPPER(_cs_dsp_remove_wrapper, cs_dsp_remove, struct cs_dsp *);

struct cs_dsp_test_local {
	struct cs_dsp_mock_xm_header *xm_header;
	struct cs_dsp_mock_wmfw_builder *wmfw_builder;
	int wmfw_version;
};

struct cs_dsp_ctl_parse_test_param {
	int mem_type;
	int alg_id;
	unsigned int offset;
	unsigned int length;
	u16 ctl_type;
	u16 flags;
};

static const struct cs_dsp_mock_alg_def cs_dsp_ctl_parse_test_algs[] = {
	{
		.id = 0xfafa,
		.ver = 0x100000,
		.xm_size_words = 164,
		.ym_size_words = 164,
		.zm_size_words = 164,
	},
	{
		.id = 0xb,
		.ver = 0x100001,
		.xm_size_words = 8,
		.ym_size_words = 8,
		.zm_size_words = 8,
	},
	{
		.id = 0x9f1234,
		.ver = 0x100500,
		.xm_size_words = 16,
		.ym_size_words = 16,
		.zm_size_words = 16,
	},
	{
		.id = 0xff00ff,
		.ver = 0x300113,
		.xm_size_words = 16,
		.ym_size_words = 16,
		.zm_size_words = 16,
	},
};

static const struct cs_dsp_mock_coeff_def mock_coeff_template = {
	.shortname = "Dummy Coeff",
	.type = WMFW_CTL_TYPE_BYTES,
	.mem_type = WMFW_ADSP2_YM,
	.flags = WMFW_CTL_FLAG_VOLATILE,
	.length_bytes = 4,
};

/* Algorithm info block without controls should load */
static void cs_dsp_ctl_parse_no_coeffs(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct firmware *wmfw;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
}

/*
 * V1 controls do not have names, the name field in the coefficient entry
 * should be ignored.
 */
static void cs_dsp_ctl_parse_v1_name(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	def.fullname = "Dummy";
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, 0);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * V1 controls do not have names, the name field in the coefficient entry
 * should be ignored. Test with a zero-length name string.
 */
static void cs_dsp_ctl_parse_empty_v1_name(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	def.fullname = "\0";
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, 0);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * V1 controls do not have names, the name field in the coefficient entry
 * should be ignored. Test with a maximum length name string.
 */
static void cs_dsp_ctl_parse_max_v1_name(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	char *name;

	name = kunit_kzalloc(test, 256, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, name);
	memset(name, 'A', 255);
	def.fullname = name;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, 0);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/* Short name from coeff descriptor should be used as control name. */
static void cs_dsp_ctl_parse_short_name(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, strlen(def.shortname));
	KUNIT_EXPECT_MEMEQ(test, ctl->subname, def.shortname, ctl->subname_len);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Short name from coeff descriptor should be used as control name.
 * Test with a short name that is a single character.
 */
static void cs_dsp_ctl_parse_min_short_name(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	def.shortname = "Q";
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, 1);
	KUNIT_EXPECT_EQ(test, ctl->subname[0], 'Q');
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Short name from coeff descriptor should be used as control name.
 * Test with a maximum length name.
 */
static void cs_dsp_ctl_parse_max_short_name(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	char *name;
	struct firmware *wmfw;

	name = kunit_kmalloc(test, 255, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, name);
	memset(name, 'A', 255);

	def.shortname = name;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, 255);
	KUNIT_EXPECT_MEMEQ(test, ctl->subname, name, ctl->subname_len);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Full name from coeff descriptor should be ignored. It is a variable
 * length field so affects the position of subsequent fields.
 * Test with a 1-character full name.
 */
static void cs_dsp_ctl_parse_with_min_fullname(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	def.fullname = "Q";
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, strlen(def.shortname));
	KUNIT_EXPECT_MEMEQ(test, ctl->subname, def.shortname, ctl->subname_len);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Full name from coeff descriptor should be ignored. It is a variable
 * length field so affects the position of subsequent fields.
 * Test with a maximum length full name.
 */
static void cs_dsp_ctl_parse_with_max_fullname(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	char *fullname;

	fullname = kunit_kmalloc(test, 255, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fullname);
	memset(fullname, 'A', 255);
	def.fullname = fullname;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, strlen(def.shortname));
	KUNIT_EXPECT_MEMEQ(test, ctl->subname, def.shortname, ctl->subname_len);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Description from coeff descriptor should be ignored. It is a variable
 * length field so affects the position of subsequent fields.
 * Test with a 1-character description
 */
static void cs_dsp_ctl_parse_with_min_description(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	def.description = "Q";
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, strlen(def.shortname));
	KUNIT_EXPECT_MEMEQ(test, ctl->subname, def.shortname, ctl->subname_len);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Description from coeff descriptor should be ignored. It is a variable
 * length field so affects the position of subsequent fields.
 * Test with a maximum length description
 */
static void cs_dsp_ctl_parse_with_max_description(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	char *description;

	description = kunit_kmalloc(test, 65535, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, description);
	memset(description, 'A', 65535);
	def.description = description;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, strlen(def.shortname));
	KUNIT_EXPECT_MEMEQ(test, ctl->subname, def.shortname, ctl->subname_len);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Full name and description from coeff descriptor are variable length
 * fields so affects the position of subsequent fields.
 * Test with a maximum length full name and description
 */
static void cs_dsp_ctl_parse_with_max_fullname_and_description(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	char *fullname, *description;

	fullname = kunit_kmalloc(test, 255, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fullname);
	memset(fullname, 'A', 255);
	def.fullname = fullname;

	description = kunit_kmalloc(test, 65535, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, description);
	memset(description, 'A', 65535);
	def.description = description;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->subname_len, strlen(def.shortname));
	KUNIT_EXPECT_MEMEQ(test, ctl->subname, def.shortname, ctl->subname_len);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

static const char * const cs_dsp_ctl_alignment_test_names[] = {
	"1", "12", "123", "1234", "12345", "123456", "1234567",
	"12345678", "123456789", "123456789A", "123456789AB",
	"123456789ABC", "123456789ABCD", "123456789ABCDE",
	"123456789ABCDEF",
};

/*
 * Variable-length string fields are padded to a multiple of 4-bytes.
 * Test this with various lengths of short name.
 */
static void cs_dsp_ctl_shortname_alignment(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	int i;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_alignment_test_names); i++) {
		def.shortname = cs_dsp_ctl_alignment_test_names[i];
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	}

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_alignment_test_names); i++) {
		mutex_lock(&priv->dsp->pwr_lock);
		ctl = cs_dsp_get_ctl(priv->dsp, cs_dsp_ctl_alignment_test_names[i],
				     def.mem_type, cs_dsp_ctl_parse_test_algs[0].id);
		mutex_unlock(&priv->dsp->pwr_lock);
		KUNIT_ASSERT_NOT_NULL(test, ctl);
		KUNIT_EXPECT_EQ(test, ctl->subname_len, i + 1);
		KUNIT_EXPECT_MEMEQ(test, ctl->subname, cs_dsp_ctl_alignment_test_names[i],
				   ctl->subname_len);
		/* Test fields that are parsed after the variable-length fields */
		KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
		KUNIT_EXPECT_EQ(test, ctl->type, def.type);
		KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
	}
}

/*
 * Variable-length string fields are padded to a multiple of 4-bytes.
 * Test this with various lengths of full name.
 */
static void cs_dsp_ctl_fullname_alignment(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	char ctl_name[4];
	struct firmware *wmfw;
	int i;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_alignment_test_names); i++) {
		/*
		 * Create a unique control name of 3 characters so that
		 * the shortname field is exactly 4 bytes long including
		 * the length byte.
		 */
		snprintf(ctl_name, sizeof(ctl_name), "%03d", i);
		KUNIT_ASSERT_EQ(test, strlen(ctl_name), 3);
		def.shortname = ctl_name;

		def.fullname = cs_dsp_ctl_alignment_test_names[i];
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	}

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_alignment_test_names); i++) {
		snprintf(ctl_name, sizeof(ctl_name), "%03d", i);

		mutex_lock(&priv->dsp->pwr_lock);
		ctl = cs_dsp_get_ctl(priv->dsp, ctl_name, def.mem_type,
				     cs_dsp_ctl_parse_test_algs[0].id);
		mutex_unlock(&priv->dsp->pwr_lock);
		KUNIT_ASSERT_NOT_NULL(test, ctl);
		KUNIT_EXPECT_EQ(test, ctl->subname_len, 3);
		KUNIT_EXPECT_MEMEQ(test, ctl->subname, ctl_name, ctl->subname_len);
		/* Test fields that are parsed after the variable-length fields */
		KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
		KUNIT_EXPECT_EQ(test, ctl->type, def.type);
		KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
	}
}

/*
 * Variable-length string fields are padded to a multiple of 4-bytes.
 * Test this with various lengths of description.
 */
static void cs_dsp_ctl_description_alignment(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	char ctl_name[4];
	struct firmware *wmfw;
	int i;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_alignment_test_names); i++) {
		/*
		 * Create a unique control name of 3 characters so that
		 * the shortname field is exactly 4 bytes long including
		 * the length byte.
		 */
		snprintf(ctl_name, sizeof(ctl_name), "%03d", i);
		KUNIT_ASSERT_EQ(test, strlen(ctl_name), 3);
		def.shortname = ctl_name;

		def.description = cs_dsp_ctl_alignment_test_names[i];
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	}

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_alignment_test_names); i++) {
		snprintf(ctl_name, sizeof(ctl_name), "%03d", i);

		mutex_lock(&priv->dsp->pwr_lock);
		ctl = cs_dsp_get_ctl(priv->dsp, ctl_name, def.mem_type,
				     cs_dsp_ctl_parse_test_algs[0].id);
		mutex_unlock(&priv->dsp->pwr_lock);
		KUNIT_ASSERT_NOT_NULL(test, ctl);
		KUNIT_EXPECT_EQ(test, ctl->subname_len, 3);
		KUNIT_EXPECT_MEMEQ(test, ctl->subname, ctl_name, ctl->subname_len);
		/* Test fields that are parsed after the variable-length fields */
		KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
		KUNIT_EXPECT_EQ(test, ctl->type, def.type);
		KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
	}
}

static const char * const cs_dsp_get_ctl_test_names[] = {
	"Up", "Down", "Switch", "Mute",
	"Left Up", "Left Down", "Right Up", "Right Down",
	"Left Mute", "Right Mute",
	"_trunc_1", "_trunc_2", " trunc",
};

/* Test using cs_dsp_get_ctl() to lookup various controls. */
static void cs_dsp_get_ctl_test(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	int i;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_get_ctl_test_names); i++) {
		def.shortname = cs_dsp_get_ctl_test_names[i];
		def.offset_dsp_words = i;
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	}

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_get_ctl_test_names); i++) {
		mutex_lock(&priv->dsp->pwr_lock);
		ctl = cs_dsp_get_ctl(priv->dsp, cs_dsp_get_ctl_test_names[i],
				     def.mem_type, cs_dsp_ctl_parse_test_algs[0].id);
		mutex_unlock(&priv->dsp->pwr_lock);
		KUNIT_ASSERT_NOT_NULL(test, ctl);
		KUNIT_EXPECT_EQ(test, ctl->subname_len, strlen(cs_dsp_get_ctl_test_names[i]));
		KUNIT_EXPECT_MEMEQ(test, ctl->subname, cs_dsp_get_ctl_test_names[i],
				   ctl->subname_len);
		KUNIT_EXPECT_EQ(test, ctl->offset, i);
	}
}

/*
 * cs_dsp_get_ctl() searches for the control in the currently loaded
 * firmware, so create identical controls in multiple firmware and
 * test that the correct one is found.
 */
static void cs_dsp_get_ctl_test_multiple_wmfw(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct cs_dsp_mock_wmfw_builder *builder2;
	struct firmware *wmfw;

	def.shortname = "_A_CONTROL";

	/* Create a second mock wmfw builder */
	builder2 = cs_dsp_mock_wmfw_init(priv,
					 cs_dsp_mock_wmfw_format_version(local->wmfw_builder));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, builder2);
	cs_dsp_mock_wmfw_add_data_block(builder2,
					WMFW_ADSP2_XM, 0,
					local->xm_header->blob_data,
					local->xm_header->blob_size_bytes);

	/* Load a 'misc' firmware with a control */
	def.offset_dsp_words = 1;
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* Load a 'mbc/vss' firmware with a control of the same name */
	def.offset_dsp_words = 2;
	cs_dsp_mock_wmfw_start_alg_info_block(builder2,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(builder2, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(builder2);
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_fw2", NULL, NULL, "mbc/vss"), 0);

	/* A lookup should return the control for the current firmware */
	mutex_lock(&priv->dsp->pwr_lock);
	ctl = cs_dsp_get_ctl(priv->dsp, def.shortname,
			     def.mem_type, cs_dsp_ctl_parse_test_algs[0].id);
	mutex_unlock(&priv->dsp->pwr_lock);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->offset, 2);

	/* Re-load the 'misc' firmware and a lookup should return its control */
	cs_dsp_power_down(priv->dsp);
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	mutex_lock(&priv->dsp->pwr_lock);
	ctl = cs_dsp_get_ctl(priv->dsp, def.shortname,
			     def.mem_type, cs_dsp_ctl_parse_test_algs[0].id);
	mutex_unlock(&priv->dsp->pwr_lock);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->offset, 1);
}

/* Test that the value of the memory type field is parsed correctly. */
static void cs_dsp_ctl_parse_memory_type(struct kunit *test)
{
	const struct cs_dsp_ctl_parse_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	/* kunit_skip() marks the test skipped forever, so just return */
	if ((param->mem_type == WMFW_ADSP2_ZM) && !cs_dsp_mock_has_zm(priv))
		return;

	def.mem_type = param->mem_type;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->alg_region.type, param->mem_type);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Test that the algorithm id from the parent alg-info block is
 * correctly stored in the cs_dsp_coeff_ctl.
 */
static void cs_dsp_ctl_parse_alg_id(struct kunit *test)
{
	const struct cs_dsp_ctl_parse_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      param->alg_id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->alg_region.alg, param->alg_id);
	KUNIT_EXPECT_EQ(test, ctl->alg_region.type, def.mem_type);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/*
 * Test that the values of (alg id, memory type) tuple is parsed correctly.
 * The alg id is parsed from the alg-info block, but the memory type is
 * parsed from the coefficient info descriptor.
 */
static void cs_dsp_ctl_parse_alg_mem(struct kunit *test)
{
	const struct cs_dsp_ctl_parse_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	/* kunit_skip() marks the test skipped forever, so just return */
	if ((param->mem_type == WMFW_ADSP2_ZM) && !cs_dsp_mock_has_zm(priv))
		return;

	def.mem_type = param->mem_type;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      param->alg_id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->alg_region.alg, param->alg_id);
	KUNIT_EXPECT_EQ(test, ctl->alg_region.type, param->mem_type);
}

/* Test that the value of the offset field is parsed correctly. */
static void cs_dsp_ctl_parse_offset(struct kunit *test)
{
	const struct cs_dsp_ctl_parse_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	def.offset_dsp_words = param->offset;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->offset, param->offset);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/* Test that the value of the length field is parsed correctly. */
static void cs_dsp_ctl_parse_length(struct kunit *test)
{
	const struct cs_dsp_ctl_parse_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	def.length_bytes = param->length;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->offset, def.offset_dsp_words);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->len, param->length);
}

/* Test that the value of the control type field is parsed correctly. */
static void cs_dsp_ctl_parse_ctl_type(struct kunit *test)
{
	const struct cs_dsp_ctl_parse_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;

	def.type = param->ctl_type;
	def.flags = param->flags;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->type, param->ctl_type);
	KUNIT_EXPECT_EQ(test, ctl->flags, def.flags);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/* Test that the value of the flags field is parsed correctly. */
static void cs_dsp_ctl_parse_flags(struct kunit *test)
{
	const struct cs_dsp_ctl_parse_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	u32 reg_val;

	/*
	 * Non volatile controls will be read to initialize the cache
	 * so the regmap cache must contain something to read.
	 */
	reg_val = 0xf11100;
	regmap_raw_write(priv->dsp->regmap,
			 cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM),
			 &reg_val, sizeof(reg_val));

	def.flags = param->flags;
	def.mem_type = WMFW_ADSP2_YM;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	ctl = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	KUNIT_ASSERT_NOT_NULL(test, ctl);
	KUNIT_EXPECT_EQ(test, ctl->type, def.type);
	KUNIT_EXPECT_EQ(test, ctl->flags, param->flags);
	KUNIT_EXPECT_EQ(test, ctl->len, def.length_bytes);
}

/* Test that invalid combinations of (control type, flags) are rejected. */
static void cs_dsp_ctl_illegal_type_flags(struct kunit *test)
{
	const struct cs_dsp_ctl_parse_test_param *param = test->param_value;
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct firmware *wmfw;
	u32 reg_val;

	/*
	 * Non volatile controls will be read to initialize the cache
	 * so the regmap cache must contain something to read.
	 */
	reg_val = 0xf11100;
	regmap_raw_write(priv->dsp->regmap,
			 cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_YM),
			 &reg_val, sizeof(reg_val));

	def.type = param->ctl_type;
	def.flags = param->flags;
	def.mem_type = WMFW_ADSP2_YM;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_LT(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
}

/* Test that the correct firmware name is entered in the cs_dsp_coeff_ctl. */
static void cs_dsp_ctl_parse_fw_name(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *walkctl, *ctl1, *ctl2;
	struct cs_dsp_mock_wmfw_builder *builder2;
	struct firmware *wmfw;

	/* Create a second mock wmfw builder */
	builder2 = cs_dsp_mock_wmfw_init(priv,
					 cs_dsp_mock_wmfw_format_version(local->wmfw_builder));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, builder2);
	cs_dsp_mock_wmfw_add_data_block(builder2,
					WMFW_ADSP2_XM, 0,
					local->xm_header->blob_data,
					local->xm_header->blob_size_bytes);

	/* Load a 'misc' firmware with a control */
	def.offset_dsp_words = 1;
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* Load a 'mbc/vss' firmware with a control */
	def.offset_dsp_words = 2;
	cs_dsp_mock_wmfw_start_alg_info_block(builder2,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(builder2, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(builder2);
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test,
			cs_dsp_power_up(priv->dsp, wmfw, "mock_fw2", NULL, NULL, "mbc/vss"), 0);

	/* Both controls should be in the list (order not guaranteed) */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list), 2);
	ctl1 = NULL;
	ctl2 = NULL;
	list_for_each_entry(walkctl, &priv->dsp->ctl_list, list) {
		if (strcmp(walkctl->fw_name, "misc") == 0)
			ctl1 = walkctl;
		else if (strcmp(walkctl->fw_name, "mbc/vss") == 0)
			ctl2 = walkctl;
	}

	KUNIT_EXPECT_NOT_NULL(test, ctl1);
	KUNIT_EXPECT_NOT_NULL(test, ctl2);
	KUNIT_EXPECT_EQ(test, ctl1->offset, 1);
	KUNIT_EXPECT_EQ(test, ctl2->offset, 2);
}

/* Controls are unique if the algorithm ID is different */
static void cs_dsp_ctl_alg_id_uniqueness(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl1, *ctl2;
	struct firmware *wmfw;

	/* Create an algorithm containing the control */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	/* Create a different algorithm containing an identical control */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[1].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* Both controls should be in the list */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list), 2);
	ctl1 = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	ctl2 = list_next_entry(ctl1, list);
	KUNIT_EXPECT_NOT_NULL(test, ctl1);
	KUNIT_EXPECT_NOT_NULL(test, ctl2);
	KUNIT_EXPECT_NE(test, ctl1->alg_region.alg, ctl2->alg_region.alg);
	KUNIT_EXPECT_EQ(test, ctl1->alg_region.type, ctl2->alg_region.type);
	KUNIT_EXPECT_EQ(test, ctl1->offset, ctl2->offset);
	KUNIT_EXPECT_EQ(test, ctl1->type, ctl2->type);
	KUNIT_EXPECT_EQ(test, ctl1->flags, ctl2->flags);
	KUNIT_EXPECT_EQ(test, ctl1->len, ctl2->len);
	KUNIT_EXPECT_STREQ(test, ctl1->fw_name, ctl2->fw_name);
	KUNIT_EXPECT_EQ(test, ctl1->subname_len, ctl2->subname_len);
	if (ctl1->subname_len)
		KUNIT_EXPECT_MEMEQ(test, ctl1->subname, ctl2->subname, ctl1->subname_len);
}

/* Controls are unique if the memory region is different */
static void cs_dsp_ctl_mem_uniqueness(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl1, *ctl2;
	struct firmware *wmfw;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	/* Create control in XM */
	def.mem_type = WMFW_ADSP2_XM;
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	/* Create control in YM */
	def.mem_type = WMFW_ADSP2_YM;
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* Both controls should be in the list */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list), 2);
	ctl1 = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	ctl2 = list_next_entry(ctl1, list);
	KUNIT_EXPECT_NOT_NULL(test, ctl1);
	KUNIT_EXPECT_NOT_NULL(test, ctl2);
	KUNIT_EXPECT_EQ(test, ctl1->alg_region.alg, ctl2->alg_region.alg);
	KUNIT_EXPECT_NE(test, ctl1->alg_region.type, ctl2->alg_region.type);
	KUNIT_EXPECT_EQ(test, ctl1->offset, ctl2->offset);
	KUNIT_EXPECT_EQ(test, ctl1->type, ctl2->type);
	KUNIT_EXPECT_EQ(test, ctl1->flags, ctl2->flags);
	KUNIT_EXPECT_EQ(test, ctl1->len, ctl2->len);
	KUNIT_EXPECT_STREQ(test, ctl1->fw_name, ctl2->fw_name);
	KUNIT_EXPECT_EQ(test, ctl1->subname_len, ctl2->subname_len);
	if (ctl1->subname_len)
		KUNIT_EXPECT_MEMEQ(test, ctl1->subname, ctl2->subname, ctl1->subname_len);
}

/* Controls are unique if they are in different firmware */
static void cs_dsp_ctl_fw_uniqueness(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl1, *ctl2;
	struct cs_dsp_mock_wmfw_builder *builder2;
	struct firmware *wmfw;

	/* Create a second mock wmfw builder */
	builder2 = cs_dsp_mock_wmfw_init(priv,
					 cs_dsp_mock_wmfw_format_version(local->wmfw_builder));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, builder2);
	cs_dsp_mock_wmfw_add_data_block(builder2,
					WMFW_ADSP2_XM, 0,
					local->xm_header->blob_data,
					local->xm_header->blob_size_bytes);

	/* Load a 'misc' firmware with a control */
	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);
	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* Load a 'mbc/vss' firmware with the same control */
	cs_dsp_mock_wmfw_start_alg_info_block(builder2,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);
	cs_dsp_mock_wmfw_add_coeff_desc(builder2, &def);
	cs_dsp_mock_wmfw_end_alg_info_block(builder2);
	wmfw = cs_dsp_mock_wmfw_get_firmware(builder2);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw2",
			NULL, NULL, "mbc/vss"), 0);
	cs_dsp_power_down(priv->dsp);

	/* Both controls should be in the list */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list), 2);
	ctl1 = list_first_entry_or_null(&priv->dsp->ctl_list, struct cs_dsp_coeff_ctl, list);
	ctl2 = list_next_entry(ctl1, list);
	KUNIT_EXPECT_NOT_NULL(test, ctl1);
	KUNIT_EXPECT_NOT_NULL(test, ctl2);
	KUNIT_EXPECT_EQ(test, ctl1->alg_region.alg, ctl2->alg_region.alg);
	KUNIT_EXPECT_EQ(test, ctl1->alg_region.type, ctl2->alg_region.type);
	KUNIT_EXPECT_EQ(test, ctl1->offset, ctl2->offset);
	KUNIT_EXPECT_EQ(test, ctl1->type, ctl2->type);
	KUNIT_EXPECT_EQ(test, ctl1->flags, ctl2->flags);
	KUNIT_EXPECT_EQ(test, ctl1->len, ctl2->len);
	KUNIT_EXPECT_STRNEQ(test, ctl1->fw_name, ctl2->fw_name);
	KUNIT_EXPECT_EQ(test, ctl1->subname_len, ctl2->subname_len);
	if (ctl1->subname_len)
		KUNIT_EXPECT_MEMEQ(test, ctl1->subname, ctl2->subname, ctl1->subname_len);
}

/*
 * Controls from a wmfw are only added to the list once. If the same
 * wmfw is reloaded the controls are not added again.
 * This creates multiple algorithms with one control each, which will
 * work on both V1 format and >=V2 format controls.
 */
static void cs_dsp_ctl_squash_reloaded_controls(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctls[ARRAY_SIZE(cs_dsp_ctl_parse_test_algs)];
	struct cs_dsp_coeff_ctl *walkctl;
	struct firmware *wmfw;
	int i;

	/* Create some algorithms with a control */
	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_parse_test_algs); i++) {
		cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
						      cs_dsp_ctl_parse_test_algs[i].id,
						      "dummyalg", NULL);
		def.mem_type = WMFW_ADSP2_YM;
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
		cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);
	}

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* All controls should be in the list */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list),
			ARRAY_SIZE(cs_dsp_ctl_parse_test_algs));

	/* Take a copy of the pointers to controls to compare against. */
	i = 0;
	list_for_each_entry(walkctl, &priv->dsp->ctl_list, list) {
		KUNIT_ASSERT_LT(test, i, ARRAY_SIZE(ctls));
		ctls[i++] = walkctl;
	}


	/* Load the wmfw again */
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* The number of controls should be the same */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list),
			ARRAY_SIZE(cs_dsp_ctl_parse_test_algs));

	/* And they should be the same objects */
	i = 0;
	list_for_each_entry(walkctl, &priv->dsp->ctl_list, list) {
		KUNIT_ASSERT_LT(test, i, ARRAY_SIZE(ctls));
		KUNIT_ASSERT_PTR_EQ(test, walkctl, ctls[i++]);
	}
}

/*
 * Controls from a wmfw are only added to the list once. If the same
 * wmfw is reloaded the controls are not added again.
 * This tests >=V2 firmware that can have multiple named controls in
 * the same algorithm.
 */
static void cs_dsp_ctl_v2_squash_reloaded_controls(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctls[ARRAY_SIZE(cs_dsp_get_ctl_test_names)];
	struct cs_dsp_coeff_ctl *walkctl;
	struct firmware *wmfw;
	int i;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);

	/* Create some controls */
	for (i = 0; i < ARRAY_SIZE(cs_dsp_get_ctl_test_names); i++) {
		def.shortname = cs_dsp_get_ctl_test_names[i];
		def.offset_dsp_words = i;
		if (i & BIT(0))
			def.mem_type = WMFW_ADSP2_XM;
		else
			def.mem_type = WMFW_ADSP2_YM;

		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	}

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* All controls should be in the list */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list),
			ARRAY_SIZE(cs_dsp_get_ctl_test_names));

	/* Take a copy of the pointers to controls to compare against. */
	i = 0;
	list_for_each_entry(walkctl, &priv->dsp->ctl_list, list) {
		KUNIT_ASSERT_LT(test, i, ARRAY_SIZE(ctls));
		ctls[i++] = walkctl;
	}


	/* Load the wmfw again */
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);
	cs_dsp_power_down(priv->dsp);

	/* The number of controls should be the same */
	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->dsp->ctl_list),
			ARRAY_SIZE(cs_dsp_get_ctl_test_names));

	/* And they should be the same objects */
	i = 0;
	list_for_each_entry(walkctl, &priv->dsp->ctl_list, list) {
		KUNIT_ASSERT_LT(test, i, ARRAY_SIZE(ctls));
		KUNIT_ASSERT_PTR_EQ(test, walkctl, ctls[i++]);
	}
}

static const char * const cs_dsp_ctl_v2_compare_len_names[] = {
	"LEFT",
	"LEFT_",
	"LEFT_SPK",
	"LEFT_SPK_V",
	"LEFT_SPK_VOL",
	"LEFT_SPK_MUTE",
	"LEFT_SPK_1",
	"LEFT_X",
	"LEFT2",
};

/*
 * When comparing shortnames the full length of both strings is
 * considered, not only the characters in of the shortest string.
 * So that "LEFT" is not the same as "LEFT2".
 * This is specifically to test for the bug that was fixed by commit:
 * 7ac1102b227b ("firmware: cs_dsp: Fix new control name check")
 */
static void cs_dsp_ctl_v2_compare_len(struct kunit *test)
{
	struct cs_dsp_test *priv = test->priv;
	struct cs_dsp_test_local *local = priv->local;
	struct cs_dsp_mock_coeff_def def = mock_coeff_template;
	struct cs_dsp_coeff_ctl *ctl;
	struct firmware *wmfw;
	int i;

	cs_dsp_mock_wmfw_start_alg_info_block(local->wmfw_builder,
					      cs_dsp_ctl_parse_test_algs[0].id,
					      "dummyalg", NULL);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_v2_compare_len_names); i++) {
		def.shortname = cs_dsp_ctl_v2_compare_len_names[i];
		cs_dsp_mock_wmfw_add_coeff_desc(local->wmfw_builder, &def);
	}

	cs_dsp_mock_wmfw_end_alg_info_block(local->wmfw_builder);

	wmfw = cs_dsp_mock_wmfw_get_firmware(priv->local->wmfw_builder);
	KUNIT_ASSERT_EQ(test, cs_dsp_power_up(priv->dsp, wmfw, "mock_fw", NULL, NULL, "misc"), 0);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_ctl_v2_compare_len_names); i++) {
		mutex_lock(&priv->dsp->pwr_lock);
		ctl = cs_dsp_get_ctl(priv->dsp, cs_dsp_ctl_v2_compare_len_names[i],
				     def.mem_type, cs_dsp_ctl_parse_test_algs[0].id);
		mutex_unlock(&priv->dsp->pwr_lock);
		KUNIT_ASSERT_NOT_NULL(test, ctl);
		KUNIT_EXPECT_EQ(test, ctl->subname_len,
				strlen(cs_dsp_ctl_v2_compare_len_names[i]));
		KUNIT_EXPECT_MEMEQ(test, ctl->subname, cs_dsp_ctl_v2_compare_len_names[i],
				   ctl->subname_len);
	}
}

static int cs_dsp_ctl_parse_test_common_init(struct kunit *test, struct cs_dsp *dsp,
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
							cs_dsp_ctl_parse_test_algs,
							ARRAY_SIZE(cs_dsp_ctl_parse_test_algs));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->xm_header);

	local->wmfw_builder = cs_dsp_mock_wmfw_init(priv, priv->local->wmfw_version);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, local->wmfw_builder);

	/* Add dummy XM header blob to wmfw */
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

static int cs_dsp_ctl_parse_test_halo_init(struct kunit *test)
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

	return cs_dsp_ctl_parse_test_common_init(test, dsp, 3);
}

static int cs_dsp_ctl_parse_test_adsp2_32bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_ctl_parse_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_ctl_parse_test_adsp2_32bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_ctl_parse_test_adsp2_32bit_init(test, 1);
}

static int cs_dsp_ctl_parse_test_adsp2_32bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_ctl_parse_test_adsp2_32bit_init(test, 2);
}

static int cs_dsp_ctl_parse_test_adsp2_16bit_init(struct kunit *test, int wmfw_ver)
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

	return cs_dsp_ctl_parse_test_common_init(test, dsp, wmfw_ver);
}

static int cs_dsp_ctl_parse_test_adsp2_16bit_wmfw1_init(struct kunit *test)
{
	return cs_dsp_ctl_parse_test_adsp2_16bit_init(test, 1);
}

static int cs_dsp_ctl_parse_test_adsp2_16bit_wmfw2_init(struct kunit *test)
{
	return cs_dsp_ctl_parse_test_adsp2_16bit_init(test, 2);
}

static const struct cs_dsp_ctl_parse_test_param cs_dsp_ctl_mem_type_param_cases[] = {
	{ .mem_type = WMFW_ADSP2_XM },
	{ .mem_type = WMFW_ADSP2_YM },
	{ .mem_type = WMFW_ADSP2_ZM },
};

static void cs_dsp_ctl_mem_type_desc(const struct cs_dsp_ctl_parse_test_param *param,
				     char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s",
		 cs_dsp_mem_region_name(param->mem_type));
}

KUNIT_ARRAY_PARAM(cs_dsp_ctl_mem_type,
		  cs_dsp_ctl_mem_type_param_cases,
		  cs_dsp_ctl_mem_type_desc);

static const struct cs_dsp_ctl_parse_test_param cs_dsp_ctl_alg_id_param_cases[] = {
	{ .alg_id = 0xb },
	{ .alg_id = 0xfafa },
	{ .alg_id = 0x9f1234 },
	{ .alg_id = 0xff00ff },
};

static void cs_dsp_ctl_alg_id_desc(const struct cs_dsp_ctl_parse_test_param *param,
				   char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "alg_id:%#x", param->alg_id);
}

KUNIT_ARRAY_PARAM(cs_dsp_ctl_alg_id,
		  cs_dsp_ctl_alg_id_param_cases,
		  cs_dsp_ctl_alg_id_desc);

static const struct cs_dsp_ctl_parse_test_param cs_dsp_ctl_offset_param_cases[] = {
	{ .offset = 0x0 },
	{ .offset = 0x1 },
	{ .offset = 0x2 },
	{ .offset = 0x3 },
	{ .offset = 0x4 },
	{ .offset = 0x5 },
	{ .offset = 0x6 },
	{ .offset = 0x7 },
	{ .offset = 0xe0 },
	{ .offset = 0xf1 },
	{ .offset = 0xfffe },
	{ .offset = 0xffff },
};

static void cs_dsp_ctl_offset_desc(const struct cs_dsp_ctl_parse_test_param *param,
				   char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "offset:%#x", param->offset);
}

KUNIT_ARRAY_PARAM(cs_dsp_ctl_offset,
		  cs_dsp_ctl_offset_param_cases,
		  cs_dsp_ctl_offset_desc);

static const struct cs_dsp_ctl_parse_test_param cs_dsp_ctl_length_param_cases[] = {
	{ .length = 0x4 },
	{ .length = 0x8 },
	{ .length = 0x18 },
	{ .length = 0xf000 },
};

static void cs_dsp_ctl_length_desc(const struct cs_dsp_ctl_parse_test_param *param,
				   char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "length:%#x", param->length);
}

KUNIT_ARRAY_PARAM(cs_dsp_ctl_length,
		  cs_dsp_ctl_length_param_cases,
		  cs_dsp_ctl_length_desc);

/* Note: some control types mandate specific flags settings */
static const struct cs_dsp_ctl_parse_test_param cs_dsp_ctl_type_param_cases[] = {
	{ .ctl_type = WMFW_CTL_TYPE_BYTES,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_ACKED,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE |
		   WMFW_CTL_FLAG_SYS },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_SYS },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE |
		   WMFW_CTL_FLAG_SYS },
};

static void cs_dsp_ctl_type_flags_desc(const struct cs_dsp_ctl_parse_test_param *param,
				       char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "ctl_type:%#x flags:%#x",
		 param->ctl_type, param->flags);
}

KUNIT_ARRAY_PARAM(cs_dsp_ctl_type,
		  cs_dsp_ctl_type_param_cases,
		  cs_dsp_ctl_type_flags_desc);

static const struct cs_dsp_ctl_parse_test_param cs_dsp_ctl_flags_param_cases[] = {
	{ .flags = 0 },
	{ .flags = WMFW_CTL_FLAG_READABLE },
	{ .flags = WMFW_CTL_FLAG_WRITEABLE },
	{ .flags = WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE },
	{ .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE },
	{ .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_VOLATILE |
		   WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
};

static void cs_dsp_ctl_flags_desc(const struct cs_dsp_ctl_parse_test_param *param,
				  char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "flags:%#x", param->flags);
}

KUNIT_ARRAY_PARAM(cs_dsp_ctl_flags,
		  cs_dsp_ctl_flags_param_cases,
		  cs_dsp_ctl_flags_desc);

static const struct cs_dsp_ctl_parse_test_param cs_dsp_ctl_illegal_type_flags_param_cases[] = {
	/* ACKED control must be volatile + read + write */
	{ .ctl_type = WMFW_CTL_TYPE_ACKED, .flags = 0 },
	{ .ctl_type = WMFW_CTL_TYPE_ACKED, .flags = WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_ACKED, .flags = WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_ACKED, .flags = WMFW_CTL_FLAG_VOLATILE },
	{ .ctl_type = WMFW_CTL_TYPE_ACKED,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_ACKED,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE },

	/* HOSTEVENT must be system + volatile + read + write */
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT, .flags = 0 },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT, .flags = WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT, .flags = WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT, .flags = WMFW_CTL_FLAG_VOLATILE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT, .flags = WMFW_CTL_FLAG_SYS },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_VOLATILE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOSTEVENT,
	  .flags = WMFW_CTL_FLAG_SYS |  WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE },

	/* FWEVENT rules same as HOSTEVENT */
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT, .flags = 0 },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT, .flags = WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT, .flags = WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT, .flags = WMFW_CTL_FLAG_VOLATILE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT, .flags = WMFW_CTL_FLAG_SYS },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_VOLATILE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_FWEVENT,
	  .flags = WMFW_CTL_FLAG_SYS |  WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE },

	/*
	 * HOSTBUFFER must be system + volatile + readable or
	 * system + volatile + readable + writeable
	 */
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER, .flags = 0 },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER, .flags = WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER, .flags = WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	   .flags = WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE},
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER, .flags = WMFW_CTL_FLAG_VOLATILE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	   .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	   .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	   .flags = WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER, .flags = WMFW_CTL_FLAG_SYS },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	   .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_READABLE | WMFW_CTL_FLAG_WRITEABLE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_VOLATILE },
	{ .ctl_type = WMFW_CTL_TYPE_HOST_BUFFER,
	  .flags = WMFW_CTL_FLAG_SYS | WMFW_CTL_FLAG_VOLATILE | WMFW_CTL_FLAG_WRITEABLE },
};

KUNIT_ARRAY_PARAM(cs_dsp_ctl_illegal_type_flags,
		  cs_dsp_ctl_illegal_type_flags_param_cases,
		  cs_dsp_ctl_type_flags_desc);

static struct kunit_case cs_dsp_ctl_parse_test_cases_v1[] = {
	KUNIT_CASE(cs_dsp_ctl_parse_no_coeffs),
	KUNIT_CASE(cs_dsp_ctl_parse_v1_name),
	KUNIT_CASE(cs_dsp_ctl_parse_empty_v1_name),
	KUNIT_CASE(cs_dsp_ctl_parse_max_v1_name),

	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_memory_type, cs_dsp_ctl_mem_type_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_alg_id, cs_dsp_ctl_alg_id_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_alg_mem, cs_dsp_ctl_mem_type_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_offset, cs_dsp_ctl_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_length, cs_dsp_ctl_length_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_ctl_type, cs_dsp_ctl_type_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_flags, cs_dsp_ctl_flags_gen_params),
	KUNIT_CASE(cs_dsp_ctl_parse_fw_name),

	KUNIT_CASE(cs_dsp_ctl_alg_id_uniqueness),
	KUNIT_CASE(cs_dsp_ctl_mem_uniqueness),
	KUNIT_CASE(cs_dsp_ctl_fw_uniqueness),
	KUNIT_CASE(cs_dsp_ctl_squash_reloaded_controls),

	{ } /* terminator */
};

static struct kunit_case cs_dsp_ctl_parse_test_cases_v2_v3[] = {
	KUNIT_CASE(cs_dsp_ctl_parse_no_coeffs),
	KUNIT_CASE(cs_dsp_ctl_parse_short_name),
	KUNIT_CASE(cs_dsp_ctl_parse_min_short_name),
	KUNIT_CASE(cs_dsp_ctl_parse_max_short_name),
	KUNIT_CASE(cs_dsp_ctl_parse_with_min_fullname),
	KUNIT_CASE(cs_dsp_ctl_parse_with_max_fullname),
	KUNIT_CASE(cs_dsp_ctl_parse_with_min_description),
	KUNIT_CASE(cs_dsp_ctl_parse_with_max_description),
	KUNIT_CASE(cs_dsp_ctl_parse_with_max_fullname_and_description),
	KUNIT_CASE(cs_dsp_ctl_shortname_alignment),
	KUNIT_CASE(cs_dsp_ctl_fullname_alignment),
	KUNIT_CASE(cs_dsp_ctl_description_alignment),
	KUNIT_CASE(cs_dsp_get_ctl_test),
	KUNIT_CASE(cs_dsp_get_ctl_test_multiple_wmfw),

	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_memory_type, cs_dsp_ctl_mem_type_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_alg_id, cs_dsp_ctl_alg_id_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_alg_mem, cs_dsp_ctl_mem_type_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_offset, cs_dsp_ctl_offset_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_length, cs_dsp_ctl_length_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_ctl_type, cs_dsp_ctl_type_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_parse_flags, cs_dsp_ctl_flags_gen_params),
	KUNIT_CASE_PARAM(cs_dsp_ctl_illegal_type_flags,
			 cs_dsp_ctl_illegal_type_flags_gen_params),
	KUNIT_CASE(cs_dsp_ctl_parse_fw_name),

	KUNIT_CASE(cs_dsp_ctl_alg_id_uniqueness),
	KUNIT_CASE(cs_dsp_ctl_mem_uniqueness),
	KUNIT_CASE(cs_dsp_ctl_fw_uniqueness),
	KUNIT_CASE(cs_dsp_ctl_squash_reloaded_controls),
	KUNIT_CASE(cs_dsp_ctl_v2_squash_reloaded_controls),
	KUNIT_CASE(cs_dsp_ctl_v2_compare_len),

	{ } /* terminator */
};

static struct kunit_suite cs_dsp_ctl_parse_test_halo = {
	.name = "cs_dsp_ctl_parse_wmfwV3_halo",
	.init = cs_dsp_ctl_parse_test_halo_init,
	.test_cases = cs_dsp_ctl_parse_test_cases_v2_v3,
};

static struct kunit_suite cs_dsp_ctl_parse_test_adsp2_32bit_wmfw1 = {
	.name = "cs_dsp_ctl_parse_wmfwV1_adsp2_32bit",
	.init = cs_dsp_ctl_parse_test_adsp2_32bit_wmfw1_init,
	.test_cases = cs_dsp_ctl_parse_test_cases_v1,
};

static struct kunit_suite cs_dsp_ctl_parse_test_adsp2_32bit_wmfw2 = {
	.name = "cs_dsp_ctl_parse_wmfwV2_adsp2_32bit",
	.init = cs_dsp_ctl_parse_test_adsp2_32bit_wmfw2_init,
	.test_cases = cs_dsp_ctl_parse_test_cases_v2_v3,
};

static struct kunit_suite cs_dsp_ctl_parse_test_adsp2_16bit_wmfw1 = {
	.name = "cs_dsp_ctl_parse_wmfwV1_adsp2_16bit",
	.init = cs_dsp_ctl_parse_test_adsp2_16bit_wmfw1_init,
	.test_cases = cs_dsp_ctl_parse_test_cases_v1,
};

static struct kunit_suite cs_dsp_ctl_parse_test_adsp2_16bit_wmfw2 = {
	.name = "cs_dsp_ctl_parse_wmfwV2_adsp2_16bit",
	.init = cs_dsp_ctl_parse_test_adsp2_16bit_wmfw2_init,
	.test_cases = cs_dsp_ctl_parse_test_cases_v2_v3,
};

kunit_test_suites(&cs_dsp_ctl_parse_test_halo,
		  &cs_dsp_ctl_parse_test_adsp2_32bit_wmfw1,
		  &cs_dsp_ctl_parse_test_adsp2_32bit_wmfw2,
		  &cs_dsp_ctl_parse_test_adsp2_16bit_wmfw1,
		  &cs_dsp_ctl_parse_test_adsp2_16bit_wmfw2);
