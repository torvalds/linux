// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024 Intel Corporation
 */
#include <linux/leds.h>
#include <net/mac80211.h>

#include "fw/api/led.h"
#include "mld.h"
#include "led.h"
#include "hcmd.h"

static void iwl_mld_send_led_fw_cmd(struct iwl_mld *mld, bool on)
{
	struct iwl_led_cmd led_cmd = {
		.status = cpu_to_le32(on),
	};
	int err;

	if (WARN_ON(!mld->fw_status.running))
		return;

	err = iwl_mld_send_cmd_with_flags_pdu(mld, WIDE_ID(LONG_GROUP,
							   LEDS_CMD),
					      CMD_ASYNC, &led_cmd);

	if (err)
		IWL_WARN(mld, "LED command failed: %d\n", err);
}

static void iwl_led_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	struct iwl_mld *mld = container_of(led_cdev, struct iwl_mld, led);

	if (!mld->fw_status.running)
		return;

	iwl_mld_send_led_fw_cmd(mld, brightness > 0);
}

int iwl_mld_leds_init(struct iwl_mld *mld)
{
	int mode = iwlwifi_mod_params.led_mode;
	int ret;

	switch (mode) {
	case IWL_LED_BLINK:
		IWL_ERR(mld, "Blink led mode not supported, used default\n");
		fallthrough;
	case IWL_LED_DEFAULT:
	case IWL_LED_RF_STATE:
		mode = IWL_LED_RF_STATE;
		break;
	case IWL_LED_DISABLE:
		IWL_INFO(mld, "Led disabled\n");
		return 0;
	default:
		return -EINVAL;
	}

	mld->led.name = kasprintf(GFP_KERNEL, "%s-led",
				  wiphy_name(mld->hw->wiphy));
	if (!mld->led.name)
		return -ENOMEM;

	mld->led.brightness_set = iwl_led_brightness_set;
	mld->led.max_brightness = 1;

	if (mode == IWL_LED_RF_STATE)
		mld->led.default_trigger =
			ieee80211_get_radio_led_name(mld->hw);

	ret = led_classdev_register(mld->trans->dev, &mld->led);
	if (ret) {
		kfree(mld->led.name);
		mld->led.name = NULL;
		IWL_INFO(mld, "Failed to enable led\n");
	}

	return ret;
}

void iwl_mld_led_config_fw(struct iwl_mld *mld)
{
	if (!mld->led.name)
		return;

	iwl_mld_send_led_fw_cmd(mld, mld->led.brightness > 0);
}

void iwl_mld_leds_exit(struct iwl_mld *mld)
{
	if (!mld->led.name)
		return;

	led_classdev_unregister(&mld->led);
	kfree(mld->led.name);
	mld->led.name = NULL;
}
