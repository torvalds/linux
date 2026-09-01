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

// Following to Configuration ROM for AV/C Devices 1.0 (Dec. 2000. 1394 Trading Association,
// Document 1999027).
#define UNIT_SPEC_ID_1394TA		0x0000a02d
#define UNIT_SW_VERSION_AVC		0x00010001
#define UNIT_SW_VERSION_IIDC_0104	0x00000100

static const u32 avc_unit_directory_and_leaf[] = {
	0x00040000,		// Unit directory consists of below 4 quads.
	(CSR_SPECIFIER_ID << 24) | UNIT_SPEC_ID_1394TA,
	(CSR_VERSION << 24) | UNIT_SW_VERSION_AVC,
	(CSR_MODEL << 24) | 0x00260827,	// Today.
	((CSR_LEAF | CSR_DESCRIPTOR) << 24) | 0x00000001, // Point to next quadlet.
	0x00030000,	// Text leaf consists of below 3 quads.
	0x00000000,
	0x00000000,
	0x50756900,	// Pui is the name of a cat that the author takes care of.
};

static const u32 config_rom_with_avc_unit[] = {
	cpu_to_be32(0x0404c1e5), // bus info
	cpu_to_be32(0x31333934), // |
	cpu_to_be32(0xf000b233), // |
	cpu_to_be32(0x01234567), // |
	cpu_to_be32(0x89abcdef), // v
	cpu_to_be32(0x0006a2d2), // root directory
	cpu_to_be32(0x0c0083c0), // |
	cpu_to_be32(0x03001f11), // |
	cpu_to_be32(0x81000004), // |
	cpu_to_be32(0x17023901), // |
	cpu_to_be32(0x81000009), // |
	cpu_to_be32(0xd100000c), // v
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
	cpu_to_be32(0x4a756a75), // v
	cpu_to_be32(0x0004227b), // unit directory (from root)
	cpu_to_be32(0x1200a02d), // |
	cpu_to_be32(0x13010001), // |
	cpu_to_be32(0x17260827), // |
	cpu_to_be32(0x81000001), // v
	cpu_to_be32(0x0003f771), // text descriptor leaf (from unit)
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x50756900), // v
};

// Following to 1394-based Digital Camera Specification Version 1.04 (Aug. 1996. 1394 Trading
// Association)
#define IIDC_COMMAND_REGS_BASE	0x00
#define IIDC_VENDOR_NAME_LEAF	0x01
#define IIDC_MODEL_NAME_LEAF	0x02

static const u32 iidc_unit_directories_and_leafs[] = {
	0x00030000,
	(CSR_SPECIFIER_ID << 24) | UNIT_SPEC_ID_1394TA,
	(CSR_VERSION << 24) | UNIT_SW_VERSION_IIDC_0104,
	((CSR_DIRECTORY | CSR_DEPENDENT_INFO) << 24) | 0x00000001,
	0x00030000,
	((CSR_OFFSET | IIDC_COMMAND_REGS_BASE) << 24) | 0x00012345,
	((CSR_LEAF | IIDC_VENDOR_NAME_LEAF) << 24) | 0x00000002,
	((CSR_LEAF | IIDC_MODEL_NAME_LEAF) << 24) | 0x00000008,
	0x00060000,	// Text leaf consists of below 6 quads.
	0x00000000,
	0x00000000,
	0x4c696e75,	// Use the same name in root directory.
	0x78204669,
	0x72657769,
	0x72650000,
	0x00040000,	// Text leaf consists of below 4 quads.
	0x00000000,
	0x00000000,
	0x43686566,	// Chef Cat is a nick name when inventing IEEE 1394 itself.
	0x20436174,
};

static const u32 config_rom_with_iidc_unit[] = {
	cpu_to_be32(0x04046a3e), // bus info
	cpu_to_be32(0x31333934), // |
	cpu_to_be32(0xf000b243), // |
	cpu_to_be32(0x01234567), // |
	cpu_to_be32(0x89abcdef), // v
	cpu_to_be32(0x0006a2d2), // root directory
	cpu_to_be32(0x0c0083c0), // |
	cpu_to_be32(0x03001f11), // |
	cpu_to_be32(0x81000004), // |
	cpu_to_be32(0x17023901), // |
	cpu_to_be32(0x81000009), // |
	cpu_to_be32(0xd100000c), // v
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
	cpu_to_be32(0x4a756a75), // v
	cpu_to_be32(0x0003d7fe), // unit directory (from root)
	cpu_to_be32(0x1200a02d), // |
	cpu_to_be32(0x13000100), // |
	cpu_to_be32(0xd4000001), // v
	cpu_to_be32(0x0003ea57), // dependent directory (from unit directory)
	cpu_to_be32(0x40012345), // |
	cpu_to_be32(0x81000002), // |
	cpu_to_be32(0x82000008), // v
	cpu_to_be32(0x00064cb7), // text descriptor leaf (from dependent directory)
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x4c696e75), // |
	cpu_to_be32(0x78204669), // |
	cpu_to_be32(0x72657769), // |
	cpu_to_be32(0x72650000), // v
	cpu_to_be32(0x0004a3e9), // text descriptor leaf (from dependent directory)
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x43686566), // |
	cpu_to_be32(0x20436174), // v
};

static const u32 config_rom_with_avc_and_iidc_units[] = {
	cpu_to_be32(0x040439c0), // bus info
	cpu_to_be32(0x31333934), // |
	cpu_to_be32(0xf000b253), // |
	cpu_to_be32(0x01234567), // |
	cpu_to_be32(0x89abcdef), // v
	cpu_to_be32(0x0007073e), // root directory
	cpu_to_be32(0x0c0083c0), // |
	cpu_to_be32(0x03001f11), // |
	cpu_to_be32(0x81000005), // |
	cpu_to_be32(0x17023901), // |
	cpu_to_be32(0x8100000a), // |
	cpu_to_be32(0xd100000d), // |
	cpu_to_be32(0xd1000015), // v
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
	cpu_to_be32(0x4a756a75), // v
	cpu_to_be32(0x0004227b), // unit directory (from root)
	cpu_to_be32(0x1200a02d), // |
	cpu_to_be32(0x13010001), // |
	cpu_to_be32(0x17260827), // |
	cpu_to_be32(0x81000001), // v
	cpu_to_be32(0x0003f771), // text descriptor leaf (from unit)
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x50756900), // v
	cpu_to_be32(0x0003d7fe), // unit directory (from root)
	cpu_to_be32(0x1200a02d), // |
	cpu_to_be32(0x13000100), // |
	cpu_to_be32(0xd4000001), // v
	cpu_to_be32(0x0003ea57), // dependent directory (from unit directory)
	cpu_to_be32(0x40012345), // |
	cpu_to_be32(0x81000002), // |
	cpu_to_be32(0x82000008), // v
	cpu_to_be32(0x00064cb7), // text descriptor leaf (from dependent directory)
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x4c696e75), // |
	cpu_to_be32(0x78204669), // |
	cpu_to_be32(0x72657769), // |
	cpu_to_be32(0x72650000), // v
	cpu_to_be32(0x0004a3e9), // text descriptor leaf (from dependent directory)
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x00000000), // |
	cpu_to_be32(0x43686566), // |
	cpu_to_be32(0x20436174), // v
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
	{
		.name = "with_avc_unit",
		.config_rom_generation = 1,
		.expected = config_rom_with_avc_unit,
		.quadlet_length = ARRAY_SIZE(config_rom_with_avc_unit),
	},
	{
		.name = "with_iidc_unit",
		.config_rom_generation = 2,
		.expected = config_rom_with_iidc_unit,
		.quadlet_length = ARRAY_SIZE(config_rom_with_iidc_unit),
	},
	{
		.name = "with_avc_and_iidc_unit",
		.config_rom_generation = 3,
		.expected = config_rom_with_avc_and_iidc_units,
		.quadlet_length = ARRAY_SIZE(config_rom_with_avc_and_iidc_units),
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
	// Use kernel stack since they should be mutable for doubly linked-list.
	struct fw_descriptor avc_unit_entry = {
		.length = ARRAY_SIZE(avc_unit_directory_and_leaf),
		.key = (CSR_DIRECTORY | CSR_UNIT) << 24,
		.data = avc_unit_directory_and_leaf,
	};
	struct fw_descriptor iidc_unit_entry = {
		.length = ARRAY_SIZE(iidc_unit_directories_and_leafs),
		.key = (CSR_DIRECTORY | CSR_UNIT) << 24,
		.data = iidc_unit_directories_and_leafs,
	};
	const struct generator_test_case *test_case = test->param_value;
	struct state_data *state = test->priv;
	struct fw_card *card = &state->card;
	__be32 *config_rom = state->config_rom;

	card->config_rom_generation = test_case->config_rom_generation;
	card->link_speed = SCODE_800;
	card->max_receive = 11;
	card->guid = 0x0123456789abcdefULL;

	if (test_case->expected == config_rom_with_avc_unit ||
	    test_case->expected == config_rom_with_avc_and_iidc_units)
		KUNIT_EXPECT_EQ(test, fw_core_add_descriptor(&avc_unit_entry), 0);

	if (test_case->expected == config_rom_with_iidc_unit ||
	    test_case->expected == config_rom_with_avc_and_iidc_units)
		KUNIT_EXPECT_EQ(test, fw_core_add_descriptor(&iidc_unit_entry), 0);

	scoped_guard(mutex, &card_mutex) {
		generate_config_rom(card, config_rom);
		KUNIT_EXPECT_EQ(test, config_rom_length, test_case->quadlet_length);
	}

	KUNIT_EXPECT_MEMEQ(test, config_rom, test_case->expected,
			   sizeof(*test_case->expected) * test_case->quadlet_length);

	if (test_case->expected == config_rom_with_iidc_unit ||
	    test_case->expected == config_rom_with_avc_and_iidc_units)
		fw_core_remove_descriptor(&iidc_unit_entry);

	if (test_case->expected == config_rom_with_avc_unit ||
	    test_case->expected == config_rom_with_avc_and_iidc_units)
		fw_core_remove_descriptor(&avc_unit_entry);
}

static void add_descriptor_with_invalid_length(struct kunit *test)
{
	// Use kernel stack since they should be mutable for doubly linked-list.
	struct fw_descriptor entry_with_invalid_length = {
		.length = 257,
	};

	KUNIT_EXPECT_EQ(test, fw_core_add_descriptor(&entry_with_invalid_length), -EINVAL);
}

static void add_descriptor_with_invalid_data(struct kunit *test)
{
	// Use vendor directory defined in Annex A of Configuration ROM for AV/C Devices 1.0 (Dec.
	// 2000, 1394 Trading Association, TA Document 1999027).
	static const u32 invalid_vendor_directory[] = {
		0x00020000,
		(CSR_MODEL << 24) | 0x00009402,
		((CSR_LEAF | CSR_DESCRIPTOR) << 24) | 0x00000001,
		0xffff0000,	// The length should be 6, invalid.
		0x00000000,
		0x00000000,
		0x436f6e63,
		0x6174204e,
		0x6f746174,
		0x696f6e00,
	};
	// Use kernel stack since they should be mutable for doubly linked-list.
	struct fw_descriptor entry_with_invalid_data = {
		.length = ARRAY_SIZE(invalid_vendor_directory),
		.key = (CSR_DIRECTORY | CSR_VENDOR) << 24,
		.data = invalid_vendor_directory,
	};

	KUNIT_EXPECT_EQ(test, fw_core_add_descriptor(&entry_with_invalid_data), -EINVAL);
}

static void add_descriptor_beyond_upper_limit(struct kunit *test)
{
	// Use kernel stack since they should be mutable for doubly linked-list.
	struct fw_descriptor entry_beyond_upper_limit = {
		.length = 255,
		.key = (CSR_DIRECTORY | CSR_UNIT) << 24,
		.data = NULL,
	};
	u32 *data;

	data = kunit_kzalloc(test, entry_beyond_upper_limit.length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, data);

	data[0] = (entry_beyond_upper_limit.length - 1) << 16;
	entry_beyond_upper_limit.data = data;
	KUNIT_EXPECT_EQ(test, fw_core_add_descriptor(&entry_beyond_upper_limit), -EBUSY);

	kunit_kfree(test, data);
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
	KUNIT_CASE(add_descriptor_with_invalid_length),
	KUNIT_CASE(add_descriptor_with_invalid_data),
	KUNIT_CASE(add_descriptor_beyond_upper_limit),
	{}
};

static struct kunit_suite config_rom_generator_test_suite = {
	.name = "firewire-config-rom-generator",
	.init = config_rom_generator_test_init,
	.exit = config_rom_generator_test_exit,
	.test_cases = config_rom_generator_test_cases,
};
kunit_test_suite(config_rom_generator_test_suite);
