// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 * Copyright (C) 2019, 2025 Intel Corporation
 *****************************************************************************/


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <linux/unaligned.h>
#include "iwl-io.h"
#include "iwl-trans.h"
#include "iwl-modparams.h"
#include "dev.h"
#include "agn.h"

/* Throughput		OFF time(ms)	ON time (ms)
 *	>300			25		25
 *	>200 to 300		40		40
 *	>100 to 200		55		55
 *	>70 to 100		65		65
 *	>50 to 70		75		75
 *	>20 to 50		85		85
 *	>10 to 20		95		95
 *	>5 to 10		110		110
 *	>1 to 5			130		130
 *	>0 to 1			167		167
 *	<=0					SOLID ON
 */
static const struct ieee80211_tpt_blink iwl_blink[] = {
	{ .throughput = 0, .blink_time = 334 },
	{ .throughput = 1 * 1024 - 1, .blink_time = 260 },
	{ .throughput = 5 * 1024 - 1, .blink_time = 220 },
	{ .throughput = 10 * 1024 - 1, .blink_time = 190 },
	{ .throughput = 20 * 1024 - 1, .blink_time = 170 },
	{ .throughput = 50 * 1024 - 1, .blink_time = 150 },
	{ .throughput = 70 * 1024 - 1, .blink_time = 130 },
	{ .throughput = 100 * 1024 - 1, .blink_time = 110 },
	{ .throughput = 200 * 1024 - 1, .blink_time = 80 },
	{ .throughput = 300 * 1024 - 1, .blink_time = 50 },
};

/* Set led register off */
void iwlagn_led_enable(struct iwl_priv *priv)
{
	iwl_write32(priv->trans, CSR_LED_REG, CSR_LED_REG_TURN_ON);
}

/*
 * Adjust led blink rate to compensate on a MAC Clock difference on every HW
 * Led blink rate analysis showed an average deviation of 20% on 5000 series
 * and up.
 * Need to compensate on the led on/off time per HW according to the deviation
 * to achieve the desired led frequency
 * The calculation is: (100-averageDeviation)/100 * blinkTime
 * For code efficiency the calculation will be:
 *     compensation = (100 - averageDeviation) * 64 / 100
 *     NewBlinkTime = (compensation * BlinkTime) / 64
 */
static inline u8 iwl_blink_compensation(struct iwl_priv *priv,
				    u8 time, u16 compensation)
{
	if (!compensation) {
		IWL_ERR(priv, "undefined blink compensation: "
			"use pre-defined blinking time\n");
		return time;
	}

	return (u8)((time * compensation) >> 6);
}

static int iwl_send_led_cmd(struct iwl_priv *priv, struct iwl_led_cmd *led_cmd)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_LEDS_CMD,
		.len = { sizeof(struct iwl_led_cmd), },
		.data = { led_cmd, },
		.flags = CMD_ASYNC,
	};
	u32 reg;

	reg = iwl_read32(priv->trans, CSR_LED_REG);
	if (reg != (reg & CSR_LED_BSM_CTRL_MSK))
		iwl_write32(priv->trans, CSR_LED_REG,
			    reg & CSR_LED_BSM_CTRL_MSK);

	return iwl_dvm_send_cmd(priv, &cmd);
}

/* Set led pattern command */
static int iwl_led_cmd(struct iwl_priv *priv,
		       unsigned long on,
		       unsigned long off)
{
	struct iwl_led_cmd led_cmd = {
		.id = IWL_LED_LINK,
		.interval = IWL_DEF_LED_INTRVL
	};
	int ret;

	if (!test_bit(STATUS_READY, &priv->status))
		return -EBUSY;

	if (priv->blink_on == on && priv->blink_off == off)
		return 0;

	if (off == 0) {
		/* led is SOLID_ON */
		on = IWL_LED_SOLID;
	}

	led_cmd.on = iwl_blink_compensation(priv, on,
				priv->trans->mac_cfg->base->led_compensation);
	led_cmd.off = iwl_blink_compensation(priv, off,
				priv->trans->mac_cfg->base->led_compensation);

	ret = iwl_send_led_cmd(priv, &led_cmd);
	if (!ret) {
		priv->blink_on = on;
		priv->blink_off = off;
	}
	return ret;
}

static void iwl_led_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	struct iwl_priv *priv = container_of(led_cdev, struct iwl_priv, led);
	unsigned long on = 0;
	unsigned long off = 0;

	if (brightness > 0)
		on = IWL_LED_SOLID;
	else
		off = IWL_LED_SOLID;

	iwl_led_cmd(priv, on, off);
}

static int iwl_led_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct iwl_priv *priv = container_of(led_cdev, struct iwl_priv, led);

	return iwl_led_cmd(priv, *delay_on, *delay_off);
}

void iwl_leds_init(struct iwl_priv *priv)
{
	int mode = iwlwifi_mod_params.led_mode;
	int ret;

	if (mode == IWL_LED_DISABLE) {
		IWL_INFO(priv, "Led disabled\n");
		return;
	}
	if (mode == IWL_LED_DEFAULT)
		mode = priv->cfg->led_mode;

	priv->led.name = kasprintf(GFP_KERNEL, "%s-led",
				   wiphy_name(priv->hw->wiphy));
	if (!priv->led.name)
		return;

	priv->led.brightness_set = iwl_led_brightness_set;
	priv->led.blink_set = iwl_led_blink_set;
	priv->led.max_brightness = 1;

	switch (mode) {
	case IWL_LED_DEFAULT:
		WARN_ON(1);
		break;
	case IWL_LED_BLINK:
		priv->led.default_trigger =
			ieee80211_create_tpt_led_trigger(priv->hw,
					IEEE80211_TPT_LEDTRIG_FL_CONNECTED,
					iwl_blink, ARRAY_SIZE(iwl_blink));
		break;
	case IWL_LED_RF_STATE:
		priv->led.default_trigger =
			ieee80211_get_radio_led_name(priv->hw);
		break;
	}

	ret = led_classdev_register(priv->trans->dev, &priv->led);
	if (ret) {
		kfree(priv->led.name);
		return;
	}

	priv->led_registered = true;
}

void iwl_leds_exit(struct iwl_priv *priv)
{
	if (!priv->led_registered)
		return;

	led_classdev_unregister(&priv->led);
	kfree(priv->led.name);
}
