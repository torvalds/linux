// SPDX-License-Identifier: GPL-2.0-only
//
// device-attribute-test.c - An application of Kunit to test implementation for device attributes.
//
// Copyright (c) 2023 Takashi Sakamoto
//
// This file can not be built independently since it is intentionally included in core-device.c.

#include <kunit/test.h>

// Configuration ROM for AV/C Devices 1.0 (Dec. 12, 2000, 1394 Trading Association)
// Annex C:Configuration ROM example(informative)
// C.1 Simple AV/C device
//
// Copied from the documentation.
static const u32 simple_avc_config_rom[] = {
	0x0404eabf,
	0x31333934,
	0xe0646102,
	0xffffffff,
	0xffffffff,
	0x00063287, // root directory.
	0x03ffffff,
	0x8100000a,
	0x17ffffff,
	0x8100000e,
	0x0c0083c0,
	0xd1000001,
	0x0004442d, // unit 0 directory.
	0x1200a02d,
	0x13010001,
	0x17ffffff,
	0x81000007,
	0x0005c915, // leaf for textual descriptor.
	0x00000000,
	0x00000000,
	0x56656e64,
	0x6f72204e,
	0x616d6500,
	0x00057f16, // leaf for textual descriptor.
	0x00000000,
	0x00000000,
	0x4d6f6465,
	0x6c204e61,
	0x6d650000,
};

// Ibid.
// Annex A:Consideration for configuration ROM reader design (informative)
// A.1 Vendor directory
//
// Written by hand.
static const u32 legacy_avc_config_rom[] = {
	0x04199fe7,
	0x31333934,
	0xe0644000,
	0x00112233,
	0x44556677,
	0x0005dace, // root directory.
	0x03012345,
	0x0c0083c0,
	0x8d000009,
	0xd1000002,
	0xc3000004,
	0x0002e107, // unit 0 directory.
	0x12abcdef,
	0x13543210,
	0x0002cb73, // vendor directory.
	0x17fedcba,
	0x81000004,
	0x00026dc1, // leaf for EUI-64.
	0x00112233,
	0x44556677,
	0x00050e84, // leaf for textual descriptor.
	0x00000000,
	0x00000000,
	0x41424344,
	0x45464748,
	0x494a0000,
};

static void device_attr_simple_avc(struct kunit *test)
{
	static const struct fw_device node = {
		.device = {
			.type = &fw_device_type,
		},
		.config_rom = simple_avc_config_rom,
		.config_rom_length = sizeof(simple_avc_config_rom),
	};
	static const struct fw_unit unit0 = {
		.device = {
			.type = &fw_unit_type,
			.parent = (struct device *)&node.device,
		},
		.directory = &simple_avc_config_rom[12],
	};
	struct device *node_dev = (struct device *)&node.device;
	struct device *unit0_dev = (struct device *)&unit0.device;
	static const int unit0_expected_ids[] = {0x00ffffff, 0x00ffffff, 0x0000a02d, 0x00010001};
	char *buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	int ids[4] = {0, 0, 0, 0};

	// Ensure associations for node and unit devices.

	KUNIT_ASSERT_TRUE(test, is_fw_device(node_dev));
	KUNIT_ASSERT_FALSE(test, is_fw_unit(node_dev));
	KUNIT_ASSERT_PTR_EQ(test, fw_device(node_dev), &node);

	KUNIT_ASSERT_FALSE(test, is_fw_device(unit0_dev));
	KUNIT_ASSERT_TRUE(test, is_fw_unit(unit0_dev));
	KUNIT_ASSERT_PTR_EQ(test, fw_parent_device((&unit0)), &node);
	KUNIT_ASSERT_PTR_EQ(test, fw_unit(unit0_dev), &unit0);

	// For entries in root directory.

	// Vendor immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(node_dev, &config_rom_attributes[0].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0xffffff\n");

	// Model immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(node_dev, &config_rom_attributes[4].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0xffffff\n");

	// Descriptor leaf entry for vendor is found.
	KUNIT_EXPECT_GT(test, show_text_leaf(node_dev, &config_rom_attributes[5].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "Vendor Name\n");

	// Descriptor leaf entry for model is found.
	KUNIT_EXPECT_GT(test, show_text_leaf(node_dev, &config_rom_attributes[6].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "Model Name\n");

	// For entries in unit 0 directory.

	// Vendor immediate entry is not found.
	KUNIT_EXPECT_LT(test, show_immediate(unit0_dev, &config_rom_attributes[0].attr, buf), 0);

	// Model immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(unit0_dev, &config_rom_attributes[4].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0xffffff\n");

	// Descriptor leaf entry for vendor is not found.
	KUNIT_EXPECT_LT(test, show_text_leaf(unit0_dev, &config_rom_attributes[5].attr, buf), 0);

	// Descriptor leaf entry for model is found.
	KUNIT_EXPECT_GT(test, show_text_leaf(unit0_dev, &config_rom_attributes[6].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "Model Name\n");

	// Specifier_ID immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(unit0_dev, &config_rom_attributes[2].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0x00a02d\n");

	// Version immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(unit0_dev, &config_rom_attributes[3].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0x010001\n");

	kunit_kfree(test, buf);

	get_modalias_ids(&unit0, ids);
	KUNIT_EXPECT_MEMEQ(test, ids, unit0_expected_ids, sizeof(ids));
}

static void device_attr_legacy_avc(struct kunit *test)
{
	static const struct fw_device node = {
		.device = {
			.type = &fw_device_type,
		},
		.config_rom = legacy_avc_config_rom,
		.config_rom_length = sizeof(legacy_avc_config_rom),
	};
	static const struct fw_unit unit0 = {
		.device = {
			.type = &fw_unit_type,
			.parent = (struct device *)&node.device,
		},
		.directory = &legacy_avc_config_rom[11],
	};
	struct device *node_dev = (struct device *)&node.device;
	struct device *unit0_dev = (struct device *)&unit0.device;
	static const int unit0_expected_ids[] = {0x00012345, 0x00000000, 0x00abcdef, 0x00543210};
	char *buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	int ids[4] = {0, 0, 0, 0};

	// Ensure associations for node and unit devices.

	KUNIT_ASSERT_TRUE(test, is_fw_device(node_dev));
	KUNIT_ASSERT_FALSE(test, is_fw_unit(node_dev));
	KUNIT_ASSERT_PTR_EQ(test, fw_device((node_dev)), &node);

	KUNIT_ASSERT_FALSE(test, is_fw_device(unit0_dev));
	KUNIT_ASSERT_TRUE(test, is_fw_unit(unit0_dev));
	KUNIT_ASSERT_PTR_EQ(test, fw_parent_device((&unit0)), &node);
	KUNIT_ASSERT_PTR_EQ(test, fw_unit(unit0_dev), &unit0);

	// For entries in root directory.

	// Vendor immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(node_dev, &config_rom_attributes[0].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0x012345\n");

	// Model immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(node_dev, &config_rom_attributes[4].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0xfedcba\n");

	// Descriptor leaf entry for vendor is not found.
	KUNIT_EXPECT_LT(test, show_text_leaf(node_dev, &config_rom_attributes[5].attr, buf), 0);

	// Descriptor leaf entry for model is not found.
	KUNIT_EXPECT_LT(test, show_text_leaf(node_dev, &config_rom_attributes[6].attr, buf), 0);

	// For entries in unit 0 directory.

	// Vendor immediate entry is not found.
	KUNIT_EXPECT_LT(test, show_immediate(unit0_dev, &config_rom_attributes[0].attr, buf), 0);

	// Model immediate entry is not found.
	KUNIT_EXPECT_LT(test, show_immediate(unit0_dev, &config_rom_attributes[4].attr, buf), 0);

	// Descriptor leaf entry for vendor is not found.
	KUNIT_EXPECT_LT(test, show_text_leaf(unit0_dev, &config_rom_attributes[5].attr, buf), 0);

	// Descriptor leaf entry for model is not found.
	KUNIT_EXPECT_LT(test, show_text_leaf(unit0_dev, &config_rom_attributes[6].attr, buf), 0);

	// Specifier_ID immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(unit0_dev, &config_rom_attributes[2].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0xabcdef\n");

	// Version immediate entry is found.
	KUNIT_EXPECT_GT(test, show_immediate(unit0_dev, &config_rom_attributes[3].attr, buf), 0);
	KUNIT_EXPECT_STREQ(test, buf, "0x543210\n");

	kunit_kfree(test, buf);

	get_modalias_ids(&unit0, ids);
	KUNIT_EXPECT_MEMEQ(test, ids, unit0_expected_ids, sizeof(ids));
}

static struct kunit_case device_attr_test_cases[] = {
	KUNIT_CASE(device_attr_simple_avc),
	KUNIT_CASE(device_attr_legacy_avc),
	{}
};

static struct kunit_suite device_attr_test_suite = {
	.name = "firewire-device-attribute",
	.test_cases = device_attr_test_cases,
};
kunit_test_suite(device_attr_test_suite);
