// SPDX-License-Identifier: GPL-2.0-only
//
// config-rom-parser-test.c - An application of Kunit to test configuration ROM parser.
//
// Copyright (c) 2026 Takashi Sakamoto
//
// This file can not be built independently since it is intentionally included in core-device.c.

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <kunit/device.h>

static const u32 sony_dcr_trv310k_config_rom[] = {
	0x0404e552,
	0x31333934,
	0xe0648100,
	0x00008500,
	0x005eb597,
	0x0007cdd0,
	0x03000085,
	0x8100000d,
	0x17000002,
	0x81000010,
	0x0c0083c0,
	0xd8000002,
	0xd1000003,
	0x0001ce96,
	0xd1000001,
	0x0004bbee,
	0x1200a02d,
	0x13010001,
	0x17000002,
	0x81000006,
	0x00046dc8,
	0x00000000,
	0x00000000,
	0x43616e6f,
	0x6e000000,
	0x000621ee,
	0x00000000,
	0x00000000,
	0x4d563569,
	0x204d4300,
	0x00000000,
	0x00000000,
};

static const u32 canon_mv5i_mc_config_rom[] = {
	0x0404e552,
	0x31333934,
	0xe0648100,
	0x00008500,
	0x005eb597,
	0x0007cdd0,
	0x03000085,
	0x8100000d,
	0x17000002,
	0x81000010,
	0x0c0083c0,
	0xd8000002,
	0xd1000003,
	0x0001ce96,
	0xd1000001,
	0x0004bbee,
	0x1200a02d,
	0x13010001,
	0x17000002,
	0x81000006,
	0x00046dc8,
	0x00000000,
	0x00000000,
	0x43616e6f,
	0x6e000000,
	0x000621ee,
	0x00000000,
	0x00000000,
	0x4d563569,
	0x204d4300,
	0x00000000,
	0x00000000,
};

static const u32 motu_audioexpress_config_rom[] = {
	0x0410a756,
	0x31333934,
	0x20ff7000,
	0x0001f200,
	0x000a8a7b,
	0x0004ef04,
	0x030001f2,
	0x0c0083c0,
	0xd1000002,
	0x8d000005,
	0x00031680,
	0x120001f2,
	0x13000033,
	0x17104800,
	0x00025ef3,
	0x0001f200,
	0x000a8a7b,
};

static const u32 tascam_fw1884_config_rom[] = {
	0x040f23c0,
	0x31333934,
	0x20ff7002,
	0x00022eff,
	0xfe800000,
	0x0004bccc,
	0x0300022e,
	0x0c0083c0,
	0x8d000006,
	0xd1000001,
	0x000347f5,
	0x1200022e,
	0x13800000,
	0xd4000004,
	0x000289aa,
	0x00022eff,
	0xfe800000,
	0x0002ae47,
	0x81000002,
	0x82000006,
	0x0004a79e,
	0x00000000,
	0x00000000,
	0x54415343,
	0x414d0000,
	0x00045443,
	0x00000000,
	0x00000000,
	0x46572d31,
	0x38383400,
};

static const struct parser_test_case {
	const char *name;
	const u32 *quadlets;
	size_t quadlet_length;
	int phy_speed_in_self_id;
	int expected_speed;
	int expected_quirk;
	unsigned int expected_max_rec;
	bool expected_cmc;
	bool expected_irmc;
} parser_test_cases[] = {
	{
		.name = "detect_irm_is_1394_1995_only_quirk",
		.quadlets = sony_dcr_trv310k_config_rom,
		.quadlet_length = ARRAY_SIZE(sony_dcr_trv310k_config_rom),
		.phy_speed_in_self_id = SCODE_100,
		.expected_speed = SCODE_100,
		.expected_quirk = FW_DEVICE_QUIRK_IRM_IS_1394_1995_ONLY,
		.expected_max_rec = 8,
		.expected_cmc = true,
		.expected_irmc = true,
	},
	{
		.name = "detect_irm_ignores_bus_manager_quirk",
		.quadlets = canon_mv5i_mc_config_rom,
		.quadlet_length = ARRAY_SIZE(canon_mv5i_mc_config_rom),
		.phy_speed_in_self_id = SCODE_100,
		.expected_speed = SCODE_100,
		.expected_quirk = FW_DEVICE_QUIRK_IRM_IGNORES_BUS_MANAGER,
		.expected_max_rec = 8,
		.expected_cmc = true,
		.expected_irmc = true,
	},
	{
		.name = "detect_ack_packet_with_invalid_pending_code_quirk",
		.quadlets = motu_audioexpress_config_rom,
		.quadlet_length = ARRAY_SIZE(motu_audioexpress_config_rom),
		.phy_speed_in_self_id = SCODE_400,
		.expected_speed = SCODE_400,
		.expected_quirk = FW_DEVICE_QUIRK_ACK_PACKET_WITH_INVALID_PENDING_CODE,
		.expected_max_rec = 7,
		.expected_cmc = false,
		.expected_irmc = false,
	},
	{
		.name = "detect_unstable_at_s400_quirk",
		.quadlets = tascam_fw1884_config_rom,
		.quadlet_length = ARRAY_SIZE(tascam_fw1884_config_rom),
		.phy_speed_in_self_id = SCODE_400,
		.expected_speed = SCODE_200,
		.expected_quirk = FW_DEVICE_QUIRK_UNSTABLE_AT_S400,
		.expected_max_rec = 7,
		.expected_cmc = false,
		.expected_irmc = false,
	},
};

// Define parser_test_gen_params.
KUNIT_ARRAY_PARAM_DESC(parser_test, parser_test_cases, name);

static int stub_run_transaction_regular(struct fw_card *card, int tcode, int destination_id,
					int generation, int speed, unsigned long long offset,
					void *payload, size_t length)
{
	struct kunit *test = kunit_get_current_test();
	const struct parser_test_case *param = test->param_value;

	KUNIT_ASSERT_GE(test, offset, CSR_REGISTER_BASE | CSR_CONFIG_ROM);
	KUNIT_ASSERT_LT(test, offset, CSR_REGISTER_BASE | CSR_CONFIG_ROM_END);
	KUNIT_ASSERT_NOT_NULL(test, payload);
	KUNIT_ASSERT_EQ(test, length, 4);

	unsigned int index = (offset - (CSR_REGISTER_BASE | CSR_CONFIG_ROM)) / sizeof(u32);
	u32 *quadlet = payload;

	KUNIT_EXPECT_LE(test, speed, param->expected_speed);
	KUNIT_EXPECT_LT(test, index, param->quadlet_length);

	*quadlet = cpu_to_be32(param->quadlets[index]);

	return RCODE_COMPLETE;
}

static void test_parser_with_regular_cases(struct kunit *test)
{
	struct fw_device *device = test->priv;
	const struct parser_test_case *param = test->param_value;

	kunit_activate_static_stub(test, fw_run_transaction, stub_run_transaction_regular);

	device->card->link_speed = SCODE_BETA;
	device->node->max_speed = param->phy_speed_in_self_id;

	KUNIT_EXPECT_EQ(test, read_config_rom(device, 0), RCODE_COMPLETE);

	KUNIT_EXPECT_EQ(test, device->config_rom_length, param->quadlet_length);
	KUNIT_EXPECT_MEMEQ(test, device->config_rom, param->quadlets, param->quadlet_length);

	KUNIT_EXPECT_TRUE(test, device->quirks & param->expected_quirk);
	KUNIT_EXPECT_EQ(test, device->max_speed, param->expected_speed);
	KUNIT_EXPECT_EQ(test, (unsigned int)device->max_rec, param->expected_max_rec);
	KUNIT_EXPECT_EQ(test, (bool)device->cmc, param->expected_cmc);
	KUNIT_EXPECT_EQ(test, (bool)device->irmc, param->expected_irmc);

	kunit_deactivate_static_stub(test, fw_run_transaction);
}

static int stub_run_transaction_malformed(struct fw_card *card, int tcode, int destination_id,
					  int generation, int speed, unsigned long long offset,
					  void *payload, size_t length)
{
	static const u32 config_rom_first_part[] = {
		0x04000000,
		0x31333934,
		0x00008002,
		0x00000000,
		0x00000000,
		0x00030000,
		(CSR_VENDOR << 24) | 0x00123456,	// Regular entry.
		((CSR_LEAF | CSR_DESCRIPTOR) << 24) | 0x000000f9,	// Beyond the upper limit.
		((CSR_DIRECTORY | CSR_UNIT) << 24) | 0x00000001,
		0xffff0000,	// Over the upper limit.
	};
	struct kunit *test = kunit_get_current_test();

	KUNIT_ASSERT_GE(test, offset, CSR_REGISTER_BASE | CSR_CONFIG_ROM);
	KUNIT_ASSERT_LT(test, offset, CSR_REGISTER_BASE | CSR_CONFIG_ROM_END);
	KUNIT_ASSERT_NOT_NULL(test, payload);
	KUNIT_ASSERT_EQ(test, length, 4);

	unsigned int index = (offset - (CSR_REGISTER_BASE | CSR_CONFIG_ROM)) / sizeof(u32);
	u32 *quadlet = payload;

	if (index < ARRAY_SIZE(config_rom_first_part))
		*quadlet = cpu_to_be32(config_rom_first_part[index]);
	else
		*quadlet = 0;

	return RCODE_COMPLETE;
}

static void test_parser_with_overflowed_case(struct kunit *test)
{
	static const u32 corrected_config_rom[] = {
		0x04000000,
		0x31333934,
		0x00008002,
		0x00000000,
		0x00000000,
		0x00030000,
		0x03123456,
		0x00000000,	// Sanitized.
		0xd1000001,
		0x00000000,	// Sanitized.
	};
	struct fw_device *device = test->priv;

	kunit_activate_static_stub(test, fw_run_transaction, stub_run_transaction_malformed);

	device->card->link_speed = SCODE_BETA;

	KUNIT_EXPECT_EQ(test, read_config_rom(device, 0), RCODE_COMPLETE);

	KUNIT_EXPECT_EQ(test, device->config_rom_length, ARRAY_SIZE(corrected_config_rom));
	KUNIT_EXPECT_MEMEQ(test, device->config_rom, corrected_config_rom,
			   sizeof(corrected_config_rom));

	kunit_deactivate_static_stub(test, fw_run_transaction);
}

static const struct fw_card_driver dummy_card_driver;

static int config_rom_parser_test_init(struct kunit *test)
{
	struct fw_device *device;
	struct device *dev;

	device = kunit_kzalloc(test, sizeof(*device), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, device);

	device->node = kunit_kzalloc(test, sizeof(*device->node), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, device->node);
	kref_init(&device->node->kref);

	device->card = kunit_kzalloc(test, sizeof(*device->card), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, device->card);

	dev = kunit_device_register(test, "dummy-device");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	fw_card_initialize(device->card, &dummy_card_driver, dev);

	test->priv = device;

	return 0;
}

static void config_rom_parser_test_exit(struct kunit *test)
{
	struct fw_device *device = test->priv;

	kunit_device_unregister(test, device->card->device);
	kunit_kfree(test, device->card);
	kunit_kfree(test, device->node);
	kunit_kfree(test, device);
}

static struct kunit_case config_rom_parser_test_cases[] = {
	KUNIT_CASE_PARAM(test_parser_with_regular_cases, parser_test_gen_params),
	KUNIT_CASE(test_parser_with_overflowed_case),
	{}
};

static struct kunit_suite config_rom_parser_test_suite = {
	.name = "firewire-config-rom-parser",
	.init = config_rom_parser_test_init,
	.exit = config_rom_parser_test_exit,
	.test_cases = config_rom_parser_test_cases,
};
kunit_test_suite(config_rom_parser_test_suite);
