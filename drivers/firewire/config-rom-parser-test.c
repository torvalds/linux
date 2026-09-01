// SPDX-License-Identifier: GPL-2.0-only
//
// config-rom-parser-test.c - An application of Kunit to test configuration ROM parser.
//
// Copyright (c) 2026 Takashi Sakamoto
//
// This file can not be built independently since it is intentionally included in core-device.c.

#include <kunit/test.h>

static struct kunit_case config_rom_parser_test_cases[] = {
	{}
};

static struct kunit_suite config_rom_parser_test_suite = {
	.name = "firewire-config-rom-parser",
	.test_cases = config_rom_parser_test_cases,
};
kunit_test_suite(config_rom_parser_test_suite);
