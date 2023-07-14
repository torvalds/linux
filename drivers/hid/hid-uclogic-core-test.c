// SPDX-License-Identifier: GPL-2.0+

/*
 *  HID driver for UC-Logic devices not fully compliant with HID standard
 *
 *  Copyright (c) 2022 José Expósito <jose.exposito89@gmail.com>
 */

#include <kunit/test.h>
#include "./hid-uclogic-params.h"

#define MAX_EVENT_SIZE 12

struct uclogic_raw_event_hook_test {
	u8 event[MAX_EVENT_SIZE];
	size_t size;
	bool expected;
};

static struct uclogic_raw_event_hook_test hook_events[] = {
	{
		.event = { 0xA1, 0xB2, 0xC3, 0xD4 },
		.size = 4,
	},
	{
		.event = { 0x1F, 0x2E, 0x3D, 0x4C, 0x5B, 0x6A },
		.size = 6,
	},
};

static struct uclogic_raw_event_hook_test test_events[] = {
	{
		.event = { 0xA1, 0xB2, 0xC3, 0xD4 },
		.size = 4,
		.expected = true,
	},
	{
		.event = { 0x1F, 0x2E, 0x3D, 0x4C, 0x5B, 0x6A },
		.size = 6,
		.expected = true,
	},
	{
		.event = { 0xA1, 0xB2, 0xC3 },
		.size = 3,
		.expected = false,
	},
	{
		.event = { 0xA1, 0xB2, 0xC3, 0xD4, 0x00 },
		.size = 5,
		.expected = false,
	},
	{
		.event = { 0x2E, 0x3D, 0x4C, 0x5B, 0x6A, 0x1F },
		.size = 6,
		.expected = false,
	},
};

static void hid_test_uclogic_exec_event_hook_test(struct kunit *test)
{
	struct uclogic_params p = {0, };
	struct uclogic_raw_event_hook *filter;
	bool res;
	int n;

	/* Initialize the list of events to hook */
	p.event_hooks = kunit_kzalloc(test, sizeof(*p.event_hooks), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p.event_hooks);
	INIT_LIST_HEAD(&p.event_hooks->list);

	for (n = 0; n < ARRAY_SIZE(hook_events); n++) {
		filter = kunit_kzalloc(test, sizeof(*filter), GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filter);

		filter->size = hook_events[n].size;
		filter->event = kunit_kzalloc(test, filter->size, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filter->event);
		memcpy(filter->event, &hook_events[n].event[0], filter->size);

		list_add_tail(&filter->list, &p.event_hooks->list);
	}

	/* Test uclogic_exec_event_hook() */
	for (n = 0; n < ARRAY_SIZE(test_events); n++) {
		res = uclogic_exec_event_hook(&p, &test_events[n].event[0],
					      test_events[n].size);
		KUNIT_ASSERT_EQ(test, res, test_events[n].expected);
	}
}

static struct kunit_case hid_uclogic_core_test_cases[] = {
	KUNIT_CASE(hid_test_uclogic_exec_event_hook_test),
	{}
};

static struct kunit_suite hid_uclogic_core_test_suite = {
	.name = "hid_uclogic_core_test",
	.test_cases = hid_uclogic_core_test_cases,
};

kunit_test_suite(hid_uclogic_core_test_suite);

MODULE_DESCRIPTION("KUnit tests for the UC-Logic driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("José Expósito <jose.exposito89@gmail.com>");
