// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit test for the ACPI-WMI marshalling code.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#include <linux/acpi.h>
#include <linux/align.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include <kunit/resource.h>
#include <kunit/test.h>

#include "../internal.h"

struct wmi_acpi_param {
	const char *name;
	const union acpi_object obj;
	const struct wmi_buffer buffer;
};

struct wmi_string_param {
	const char *name;
	const char *string;
	const struct wmi_buffer buffer;
};

struct wmi_invalid_acpi_param {
	const char *name;
	const union acpi_object obj;
};

struct wmi_invalid_string_param {
	const char *name;
	const struct wmi_buffer buffer;
};

/* 0xdeadbeef */
static u8 expected_single_integer[] = {
	0xef, 0xbe, 0xad, 0xde,
};

/* "TEST" */
static u8 expected_single_string[] = {
	0x0a, 0x00, 0x54, 0x00, 0x45, 0x00, 0x53, 0x00, 0x54, 0x00, 0x00, 0x00,
};

static u8 test_buffer[] = {
	0xab, 0xcd,
};

static u8 expected_single_buffer[] = {
	0xab, 0xcd,
};

static union acpi_object simple_package_elements[] = {
	{
		.buffer = {
			.type = ACPI_TYPE_BUFFER,
			.length = sizeof(test_buffer),
			.pointer = test_buffer,
		},
	},
	{
		.integer = {
			.type = ACPI_TYPE_INTEGER,
			.value = 0x01020304,
		},
	},
};

static u8 expected_simple_package[] = {
	0xab, 0xcd,
	0x00, 0x00,
	0x04, 0x03, 0x02, 0x01,
};

static u8 test_small_buffer[] = {
	0xde,
};

static union acpi_object complex_package_elements[] = {
	{
		.integer = {
			.type = ACPI_TYPE_INTEGER,
			.value = 0xdeadbeef,
		},
	},
	{
		.buffer = {
			.type = ACPI_TYPE_BUFFER,
			.length = sizeof(test_small_buffer),
			.pointer = test_small_buffer,
		},
	},
	{
		.string = {
			.type = ACPI_TYPE_STRING,
			.length = sizeof("TEST") - 1,
			.pointer = "TEST",
		},
	},
	{
		.buffer = {
			.type = ACPI_TYPE_BUFFER,
			.length = sizeof(test_small_buffer),
			.pointer = test_small_buffer,
		},
	},
	{
		.integer = {
			.type = ACPI_TYPE_INTEGER,
			.value = 0x01020304,
		},
	}
};

static u8 expected_complex_package[] = {
	0xef, 0xbe, 0xad, 0xde,
	0xde,
	0x00,
	0x0a, 0x00, 0x54, 0x00, 0x45, 0x00, 0x53, 0x00, 0x54, 0x00, 0x00, 0x00,
	0xde,
	0x00,
	0x04, 0x03, 0x02, 0x01,
};

static const struct wmi_acpi_param wmi_acpi_params_array[] = {
	{
		.name = "single_integer",
		.obj = {
			.integer = {
				.type = ACPI_TYPE_INTEGER,
				.value = 0xdeadbeef,
			},
		},
		.buffer = {
			.data = expected_single_integer,
			.length = sizeof(expected_single_integer),
		},
	},
	{
		.name = "single_string",
		.obj = {
			.string = {
				.type = ACPI_TYPE_STRING,
				.length = sizeof("TEST") - 1,
				.pointer = "TEST",
			},
		},
		.buffer = {
			.data = expected_single_string,
			.length = sizeof(expected_single_string),
		},
	},
	{
		.name = "single_buffer",
		.obj = {
			.buffer = {
				.type = ACPI_TYPE_BUFFER,
				.length = sizeof(test_buffer),
				.pointer = test_buffer,
			},
		},
		.buffer = {
			.data = expected_single_buffer,
			.length = sizeof(expected_single_buffer),
		},
	},
	{
		.name = "simple_package",
		.obj = {
			.package = {
				.type = ACPI_TYPE_PACKAGE,
				.count = ARRAY_SIZE(simple_package_elements),
				.elements = simple_package_elements,
			},
		},
		.buffer = {
			.data = expected_simple_package,
			.length = sizeof(expected_simple_package),
		},
	},
	{
		.name = "complex_package",
		.obj = {
			.package = {
				.type = ACPI_TYPE_PACKAGE,
				.count = ARRAY_SIZE(complex_package_elements),
				.elements = complex_package_elements,
			},
		},
		.buffer = {
			.data = expected_complex_package,
			.length = sizeof(expected_complex_package),
		},
	},
};

static void wmi_acpi_param_get_desc(const struct wmi_acpi_param *param, char *desc)
{
	strscpy(desc, param->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(wmi_unmarshal_acpi_object, wmi_acpi_params_array, wmi_acpi_param_get_desc);

/* "WMI\0" */
static u8 padded_wmi_string[] = {
	0x0a, 0x00,
	0x57, 0x00,
	0x4D, 0x00,
	0x49, 0x00,
	0x00, 0x00,
	0x00, 0x00,
};

static const struct wmi_string_param wmi_string_params_array[] = {
	{
		.name = "test",
		.string = "TEST",
		.buffer = {
			.length = sizeof(expected_single_string),
			.data = expected_single_string,
		},
	},
	{
		.name = "padded",
		.string = "WMI",
		.buffer = {
			.length = sizeof(padded_wmi_string),
			.data = padded_wmi_string,
		},
	},
};

static void wmi_string_param_get_desc(const struct wmi_string_param *param, char *desc)
{
	strscpy(desc, param->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(wmi_marshal_string, wmi_string_params_array, wmi_string_param_get_desc);

static union acpi_object nested_package_elements[] = {
	{
		.package = {
			.type = ACPI_TYPE_PACKAGE,
			.count = ARRAY_SIZE(simple_package_elements),
			.elements = simple_package_elements,
		},
	}
};

static const struct wmi_invalid_acpi_param wmi_invalid_acpi_params_array[] = {
	{
		.name = "nested_package",
		.obj = {
			.package = {
				.type = ACPI_TYPE_PACKAGE,
				.count = ARRAY_SIZE(nested_package_elements),
				.elements = nested_package_elements,
			},
		},
	},
	{
		.name = "reference",
		.obj = {
			.reference = {
				.type = ACPI_TYPE_LOCAL_REFERENCE,
				.actual_type = ACPI_TYPE_ANY,
				.handle = NULL,
			},
		},
	},
	{
		.name = "processor",
		.obj = {
			.processor = {
				.type = ACPI_TYPE_PROCESSOR,
				.proc_id = 0,
				.pblk_address = 0,
				.pblk_length = 0,
			},
		},
	},
	{
		.name = "power_resource",
		.obj = {
			.power_resource = {
				.type = ACPI_TYPE_POWER,
				.system_level = 0,
				.resource_order = 0,
			},
		},
	},
};

static void wmi_invalid_acpi_param_get_desc(const struct wmi_invalid_acpi_param *param, char *desc)
{
	strscpy(desc, param->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(wmi_unmarshal_acpi_object_failure, wmi_invalid_acpi_params_array,
		  wmi_invalid_acpi_param_get_desc);

static u8 oversized_wmi_string[] = {
	0x04, 0x00, 0x00, 0x00,
};

/*
 * The error is that 3 bytes can not hold UTF-16 characters
 * without cutting of the last one.
 */
static u8 undersized_wmi_string[] = {
	0x03, 0x00, 0x00, 0x00, 0x00,
};

static u8 non_ascii_wmi_string[] = {
	0x04, 0x00, 0xC4, 0x00, 0x00, 0x00,
};

static const struct wmi_invalid_string_param wmi_invalid_string_params_array[] = {
	{
		.name = "empty_buffer",
		.buffer = {
			.length = 0,
			.data = ZERO_SIZE_PTR,
		},

	},
	{
		.name = "oversized",
		.buffer = {
			.length = sizeof(oversized_wmi_string),
			.data = oversized_wmi_string,
		},
	},
	{
		.name = "undersized",
		.buffer = {
			.length = sizeof(undersized_wmi_string),
			.data = undersized_wmi_string,
		},
	},
	{
		.name = "non_ascii",
		.buffer = {
			.length = sizeof(non_ascii_wmi_string),
			.data = non_ascii_wmi_string,
		},
	},
};

static void wmi_invalid_string_param_get_desc(const struct wmi_invalid_string_param *param,
					      char *desc)
{
	strscpy(desc, param->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(wmi_marshal_string_failure, wmi_invalid_string_params_array,
		  wmi_invalid_string_param_get_desc);

KUNIT_DEFINE_ACTION_WRAPPER(kfree_wrapper, kfree, const void *);

static void wmi_unmarshal_acpi_object_test(struct kunit *test)
{
	const struct wmi_acpi_param *param = test->param_value;
	struct wmi_buffer result;
	int ret;

	ret = wmi_unmarshal_acpi_object(&param->obj, &result);
	if (ret < 0)
		KUNIT_FAIL_AND_ABORT(test, "Unmarshalling of ACPI object failed\n");

	kunit_add_action(test, kfree_wrapper, result.data);

	KUNIT_EXPECT_TRUE(test, IS_ALIGNED((uintptr_t)result.data, 8));
	KUNIT_EXPECT_EQ(test, result.length, param->buffer.length);
	KUNIT_EXPECT_MEMEQ(test, result.data, param->buffer.data, result.length);
}

static void wmi_unmarshal_acpi_object_failure_test(struct kunit *test)
{
	const struct wmi_invalid_acpi_param *param = test->param_value;
	struct wmi_buffer result;
	int ret;

	ret = wmi_unmarshal_acpi_object(&param->obj, &result);
	if (ret < 0)
		return;

	kfree(result.data);
	KUNIT_FAIL(test, "Invalid ACPI object was not rejected\n");
}

static void wmi_marshal_string_test(struct kunit *test)
{
	const struct wmi_string_param *param = test->param_value;
	struct acpi_buffer result;
	int ret;

	ret = wmi_marshal_string(&param->buffer, &result);
	if (ret < 0)
		KUNIT_FAIL_AND_ABORT(test, "Marshalling of WMI string failed\n");

	kunit_add_action(test, kfree_wrapper, result.pointer);

	KUNIT_EXPECT_EQ(test, result.length, strlen(param->string));
	KUNIT_EXPECT_STREQ(test, result.pointer, param->string);
}

static void wmi_marshal_string_failure_test(struct kunit *test)
{
	const struct wmi_invalid_string_param *param = test->param_value;
	struct acpi_buffer result;
	int ret;

	ret = wmi_marshal_string(&param->buffer, &result);
	if (ret < 0)
		return;

	kfree(result.pointer);
	KUNIT_FAIL(test, "Invalid string was not rejected\n");
}

static struct kunit_case wmi_marshalling_test_cases[] = {
	KUNIT_CASE_PARAM(wmi_unmarshal_acpi_object_test,
			 wmi_unmarshal_acpi_object_gen_params),
	KUNIT_CASE_PARAM(wmi_marshal_string_test,
			 wmi_marshal_string_gen_params),
	KUNIT_CASE_PARAM(wmi_unmarshal_acpi_object_failure_test,
			 wmi_unmarshal_acpi_object_failure_gen_params),
	KUNIT_CASE_PARAM(wmi_marshal_string_failure_test,
			 wmi_marshal_string_failure_gen_params),
	{}
};

static struct kunit_suite wmi_marshalling_test_suite = {
	.name = "wmi_marshalling",
	.test_cases = wmi_marshalling_test_cases,
};

kunit_test_suite(wmi_marshalling_test_suite);

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("KUnit test for the ACPI-WMI marshalling code");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_LICENSE("GPL");
