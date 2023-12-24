// SPDX-License-Identifier: GPL-2.0-only
//
// device-attribute-test.c - An application of Kunit to test implementation for device attributes.
//
// Copyright (c) 2023 Takashi Sakamoto
//
// This file can not be built independently since it is intentionally included in core-device.c.

#include <kunit/test.h>

static struct kunit_case device_attr_test_cases[] = {
	{}
};

static struct kunit_suite device_attr_test_suite = {
	.name = "firewire-device-attribute",
	.test_cases = device_attr_test_cases,
};
kunit_test_suite(device_attr_test_suite);
