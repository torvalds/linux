// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID to Linux Input mapping
 *
 *  Copyright (c) 2022 José Expósito <jose.exposito89@gmail.com>
 */

#include <kunit/test.h>

static void hid_test_input_set_battery_charge_status(struct kunit *test)
{
	struct hid_device *dev;
	bool handled;

	dev = kunit_kzalloc(test, sizeof(*dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	handled = hidinput_set_battery_charge_status(dev, HID_DG_HEIGHT, 0);
	KUNIT_EXPECT_FALSE(test, handled);
	KUNIT_EXPECT_EQ(test, dev->battery_charge_status, POWER_SUPPLY_STATUS_UNKNOWN);

	handled = hidinput_set_battery_charge_status(dev, HID_BAT_CHARGING, 0);
	KUNIT_EXPECT_TRUE(test, handled);
	KUNIT_EXPECT_EQ(test, dev->battery_charge_status, POWER_SUPPLY_STATUS_DISCHARGING);

	handled = hidinput_set_battery_charge_status(dev, HID_BAT_CHARGING, 1);
	KUNIT_EXPECT_TRUE(test, handled);
	KUNIT_EXPECT_EQ(test, dev->battery_charge_status, POWER_SUPPLY_STATUS_CHARGING);
}

static void hid_test_input_get_battery_property(struct kunit *test)
{
	struct power_supply *psy;
	struct hid_device *dev;
	union power_supply_propval val;
	int ret;

	dev = kunit_kzalloc(test, sizeof(*dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	dev->battery_avoid_query = true;

	psy = kunit_kzalloc(test, sizeof(*psy), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, psy);
	psy->drv_data = dev;

	dev->battery_status = HID_BATTERY_UNKNOWN;
	dev->battery_charge_status = POWER_SUPPLY_STATUS_CHARGING;
	ret = hidinput_get_battery_property(psy, POWER_SUPPLY_PROP_STATUS, &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val.intval, POWER_SUPPLY_STATUS_UNKNOWN);

	dev->battery_status = HID_BATTERY_REPORTED;
	dev->battery_charge_status = POWER_SUPPLY_STATUS_CHARGING;
	ret = hidinput_get_battery_property(psy, POWER_SUPPLY_PROP_STATUS, &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val.intval, POWER_SUPPLY_STATUS_CHARGING);

	dev->battery_status = HID_BATTERY_REPORTED;
	dev->battery_charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
	ret = hidinput_get_battery_property(psy, POWER_SUPPLY_PROP_STATUS, &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val.intval, POWER_SUPPLY_STATUS_DISCHARGING);
}

static struct kunit_case hid_input_tests[] = {
	KUNIT_CASE(hid_test_input_set_battery_charge_status),
	KUNIT_CASE(hid_test_input_get_battery_property),
	{ }
};

static struct kunit_suite hid_input_test_suite = {
	.name = "hid_input",
	.test_cases = hid_input_tests,
};

kunit_test_suite(hid_input_test_suite);

MODULE_DESCRIPTION("HID input KUnit tests");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("José Expósito <jose.exposito89@gmail.com>");
