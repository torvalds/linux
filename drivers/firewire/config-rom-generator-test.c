// SPDX-License-Identifier: GPL-2.0-only
//
// config-rom-generator-test.c - An application of Kunit to test configuration ROM generator.
//
// Copyright (c) 2026 Takashi Sakamoto
//
// This file can not be built independently since it is intentionally included in core-card.c.

#include <kunit/test.h>

static struct kunit_case config_rom_generator_test_cases[] = {
	{}
};

static struct kunit_suite config_rom_generator_test_suite = {
	.name = "firewire-config-rom-generator",
	.test_cases = config_rom_generator_test_cases,
};
kunit_test_suite(config_rom_generator_test_suite);
