// SPDX-License-Identifier: GPL-2.0+

/*
 *  HID driver for UC-Logic devices not fully compliant with HID standard
 *
 *  Copyright (c) 2022 José Expósito <jose.exposito89@gmail.com>
 */

#include <kunit/test.h>
#include "./hid-uclogic-params.h"
#include "./hid-uclogic-rdesc.h"

#define MAX_STR_DESC_SIZE 14

struct uclogic_parse_ugee_v2_desc_case {
	const char *name;
	int res;
	const __u8 str_desc[MAX_STR_DESC_SIZE];
	size_t str_desc_size;
	const s32 desc_params[UCLOGIC_RDESC_PH_ID_NUM];
	enum uclogic_params_frame_type frame_type;
};

static struct uclogic_parse_ugee_v2_desc_case uclogic_parse_ugee_v2_desc_cases[] = {
	{
		.name = "invalid_str_desc",
		.res = -EINVAL,
		.str_desc = {},
		.str_desc_size = 0,
		.desc_params = {},
		.frame_type = UCLOGIC_PARAMS_FRAME_BUTTONS,
	},
	{
		.name = "resolution_with_value_0",
		.res = 0,
		.str_desc = {
			0x0E, 0x03,
			0x70, 0xB2,
			0x10, 0x77,
			0x08,
			0x00,
			0xFF, 0x1F,
			0x00, 0x00,
		},
		.str_desc_size = 12,
		.desc_params = {
			[UCLOGIC_RDESC_PEN_PH_ID_X_LM] = 0xB270,
			[UCLOGIC_RDESC_PEN_PH_ID_X_PM] = 0,
			[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] = 0x7710,
			[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] = 0,
			[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM] = 0x1FFF,
			[UCLOGIC_RDESC_FRAME_PH_ID_UM] = 0x08,
		},
		.frame_type = UCLOGIC_PARAMS_FRAME_BUTTONS,
	},
	/* XP-PEN Deco L str_desc: Frame with 8 buttons */
	{
		.name = "frame_type_buttons",
		.res = 0,
		.str_desc = {
			0x0E, 0x03,
			0x70, 0xB2,
			0x10, 0x77,
			0x08,
			0x00,
			0xFF, 0x1F,
			0xD8, 0x13,
		},
		.str_desc_size = 12,
		.desc_params = {
			[UCLOGIC_RDESC_PEN_PH_ID_X_LM] = 0xB270,
			[UCLOGIC_RDESC_PEN_PH_ID_X_PM] = 0x2320,
			[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] = 0x7710,
			[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] = 0x1770,
			[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM] = 0x1FFF,
			[UCLOGIC_RDESC_FRAME_PH_ID_UM] = 0x08,
		},
		.frame_type = UCLOGIC_PARAMS_FRAME_BUTTONS,
	},
	/* PARBLO A610 PRO str_desc: Frame with 9 buttons and dial */
	{
		.name = "frame_type_dial",
		.res = 0,
		.str_desc = {
			0x0E, 0x03,
			0x96, 0xC7,
			0xF9, 0x7C,
			0x09,
			0x01,
			0xFF, 0x1F,
			0xD8, 0x13,
		},
		.str_desc_size = 12,
		.desc_params = {
			[UCLOGIC_RDESC_PEN_PH_ID_X_LM] = 0xC796,
			[UCLOGIC_RDESC_PEN_PH_ID_X_PM] = 0x2749,
			[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] = 0x7CF9,
			[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] = 0x1899,
			[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM] = 0x1FFF,
			[UCLOGIC_RDESC_FRAME_PH_ID_UM] = 0x09,
		},
		.frame_type = UCLOGIC_PARAMS_FRAME_DIAL,
	},
	/* XP-PEN Deco Pro S str_desc: Frame with 8 buttons and mouse */
	{
		.name = "frame_type_mouse",
		.res = 0,
		.str_desc = {
			0x0E, 0x03,
			0xC8, 0xB3,
			0x34, 0x65,
			0x08,
			0x02,
			0xFF, 0x1F,
			0xD8, 0x13,
		},
		.str_desc_size = 12,
		.desc_params = {
			[UCLOGIC_RDESC_PEN_PH_ID_X_LM] = 0xB3C8,
			[UCLOGIC_RDESC_PEN_PH_ID_X_PM] = 0x2363,
			[UCLOGIC_RDESC_PEN_PH_ID_Y_LM] = 0x6534,
			[UCLOGIC_RDESC_PEN_PH_ID_Y_PM] = 0x13EC,
			[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM] = 0x1FFF,
			[UCLOGIC_RDESC_FRAME_PH_ID_UM] = 0x08,
		},
		.frame_type = UCLOGIC_PARAMS_FRAME_MOUSE,
	},
};

static void uclogic_parse_ugee_v2_desc_case_desc(struct uclogic_parse_ugee_v2_desc_case *t,
						 char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(uclogic_parse_ugee_v2_desc, uclogic_parse_ugee_v2_desc_cases,
		  uclogic_parse_ugee_v2_desc_case_desc);

static void uclogic_parse_ugee_v2_desc_test(struct kunit *test)
{
	int res;
	s32 desc_params[UCLOGIC_RDESC_PH_ID_NUM];
	enum uclogic_params_frame_type frame_type;
	const struct uclogic_parse_ugee_v2_desc_case *params = test->param_value;

	res = uclogic_params_parse_ugee_v2_desc(params->str_desc,
						params->str_desc_size,
						desc_params,
						ARRAY_SIZE(desc_params),
						&frame_type);
	KUNIT_ASSERT_EQ(test, res, params->res);

	if (res)
		return;

	KUNIT_EXPECT_EQ(test,
			params->desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_LM],
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_LM]);
	KUNIT_EXPECT_EQ(test,
			params->desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_PM],
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_X_PM]);
	KUNIT_EXPECT_EQ(test,
			params->desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_LM],
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_LM]);
	KUNIT_EXPECT_EQ(test,
			params->desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_PM],
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_Y_PM]);
	KUNIT_EXPECT_EQ(test,
			params->desc_params[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM],
			desc_params[UCLOGIC_RDESC_PEN_PH_ID_PRESSURE_LM]);
	KUNIT_EXPECT_EQ(test,
			params->desc_params[UCLOGIC_RDESC_FRAME_PH_ID_UM],
			desc_params[UCLOGIC_RDESC_FRAME_PH_ID_UM]);
	KUNIT_EXPECT_EQ(test, params->frame_type, frame_type);
}

static struct kunit_case hid_uclogic_params_test_cases[] = {
	KUNIT_CASE_PARAM(uclogic_parse_ugee_v2_desc_test,
			 uclogic_parse_ugee_v2_desc_gen_params),
	{}
};

static struct kunit_suite hid_uclogic_params_test_suite = {
	.name = "hid_uclogic_params_test",
	.test_cases = hid_uclogic_params_test_cases,
};

kunit_test_suite(hid_uclogic_params_test_suite);

MODULE_DESCRIPTION("KUnit tests for the UC-Logic driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("José Expósito <jose.exposito89@gmail.com>");
