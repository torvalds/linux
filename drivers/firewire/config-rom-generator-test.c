// SPDX-License-Identifier: GPL-2.0-only
//
// config-rom-generator-test.c - An application of Kunit to test configuration ROM generator.
//
// Copyright (c) 2026 Takashi Sakamoto
//
// This file can not be built independently since it is intentionally included in core-card.c.

#include <kunit/test.h>
#include <kunit/device.h>

static const u32 config_rom_bare[] = {
	cpu_to_be32(0x0404921b), // bus info
	cpu_to_be32(0x31333934), // |
	cpu_to_be32(0xf000b223), // |
	cpu_to_be32(0x01234567), // |
	cpu_to_be32(0x89abcdef), // v
	cpu_to_be32(0x00051b70), // root directory
	cpu_to_be32(0x0c0083c0), // |
	cpu_to_be32(0x03001f11), // |
	cpu_to_be32(0x81000003), // |
	cpu_to_be32(0x17023901), // |
	cpu_to_be32(0x81000008), // v
	cpu_to_be32(0x00064cb7), // text descriptor leaf (from root)
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x4c696e75), // |
	cpu_to_be32(0x78204669), // |
	cpu_to_be32(0x72657769), // |
	cpu_to_be32(0x72650000), // v
	cpu_to_be32(0x0003ff1c), // text descriptor leaf (from root)
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x4a756a75), // v Juju is a code name at the development time of this stack.
};

static const struct generator_test_case {
	const char *const name;
	int config_rom_generation;
	const __be32 *const expected;
	unsigned int quadlet_length;
} generator_test_cases[] = {
	{
		.name = "bare",
		.config_rom_generation = 0,
		.expected = config_rom_bare,
		.quadlet_length = ARRAY_SIZE(config_rom_bare),
	},
};

// Define generator_test_gen_params.
KUNIT_ARRAY_PARAM_DESC(generator_test, generator_test_cases, name);

struct state_data {
	struct fw_card card;
	__be32 config_rom[(CSR_CONFIG_ROM_END - CSR_CONFIG_ROM) / sizeof(__be32)];
};

static void test_config_rom_generator(struct kunit *test)
{
	const struct generator_test_case *test_case = test->param_value;
	struct state_data *state = test->priv;
	struct fw_card *card = &state->card;
	__be32 *config_rom = state->config_rom;

	card->config_rom_generation = test_case->config_rom_generation;
	card->link_speed = SCODE_800;
	card->max_receive = 11;
	card->guid = 0x0123456789abcdefULL;

	scoped_guard(mutex, &card_mutex) {
		generate_config_rom(card, config_rom);
		KUNIT_EXPECT_EQ(test, config_rom_length, test_case->quadlet_length);
	}

	KUNIT_EXPECT_MEMEQ(test, config_rom, test_case->expected,
			   sizeof(*test_case->expected) * test_case->quadlet_length);
}

static const struct fw_card_driver dummy_card_driver;

static int config_rom_generator_test_init(struct kunit *test)
{
	struct state_data *state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, state);

	struct device *dev = kunit_device_register(test, "dummy-device");

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	fw_card_initialize(&state->card, &dummy_card_driver, dev);

	test->priv = state;

	return 0;
}

static void config_rom_generator_test_exit(struct kunit *test)
{
	struct state_data *state = test->priv;

	kunit_device_unregister(test, state->card.device);
	kunit_kfree(test, state);
}


static struct kunit_case config_rom_generator_test_cases[] = {
	KUNIT_CASE_PARAM(test_config_rom_generator, generator_test_gen_params),
	{}
};

static struct kunit_suite config_rom_generator_test_suite = {
	.name = "firewire-config-rom-generator",
	.init = config_rom_generator_test_init,
	.exit = config_rom_generator_test_exit,
	.test_cases = config_rom_generator_test_cases,
};
kunit_test_suite(config_rom_generator_test_suite);
