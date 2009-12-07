/******************************************************************************
 *
 * Copyright(c) 2003 - 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifdef CONFIG_IWLWIFI_LEDS

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>

#include "iwl-commands.h"
#include "iwl-3945.h"
#include "iwl-core.h"
#include "iwl-dev.h"

#ifdef CONFIG_IWLWIFI_DEBUG
static const char *led_type_str[] = {
	__stringify(IWL_LED_TRG_TX),
	__stringify(IWL_LED_TRG_RX),
	__stringify(IWL_LED_TRG_ASSOC),
	__stringify(IWL_LED_TRG_RADIO),
	NULL
};
#endif /* CONFIG_IWLWIFI_DEBUG */

static const struct {
	u16 brightness;
	u8 on_time;
	u8 off_time;
} blink_tbl[] =
{
	{300, 25, 25},
	{200, 40, 40},
	{100, 55, 55},
	{70, 65, 65},
	{50, 75, 75},
	{20, 85, 85},
	{15, 95, 95 },
	{10, 110, 110},
	{5, 130, 130},
	{0, 167, 167},
	/* SOLID_ON */
	{-1, IWL_LED_SOLID, 0}
};

#define IWL_1MB_RATE (128 * 1024)
#define IWL_LED_THRESHOLD (16)
#define IWL_MAX_BLINK_TBL (ARRAY_SIZE(blink_tbl) - 1) /*Exclude Solid on*/
#define IWL_SOLID_BLINK_IDX (ARRAY_SIZE(blink_tbl) - 1)

static void iwl3945_led_cmd_callback(struct iwl_priv *priv,
				     struct iwl_device_cmd *cmd,
				     struct sk_buff *skb)
{
}

static inline int iwl3945_brightness_to_idx(enum led_brightness brightness)
{
	return fls(0x000000FF & (u32)brightness);
}

/* Send led command */
static int iwl_send_led_cmd(struct iwl_priv *priv,
			    struct iwl_led_cmd *led_cmd)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_LEDS_CMD,
		.len = sizeof(struct iwl_led_cmd),
		.data = led_cmd,
		.flags = CMD_ASYNC,
		.callback = iwl3945_led_cmd_callback,
	};

	return iwl_send_cmd(priv, &cmd);
}



/* Set led on command */
static int iwl3945_led_pattern(struct iwl_priv *priv, int led_id,
			       unsigned int idx)
{
	struct iwl_led_cmd led_cmd = {
		.id = led_id,
		.interval = IWL_DEF_LED_INTRVL
	};

	BUG_ON(idx > IWL_MAX_BLINK_TBL);

	led_cmd.on = blink_tbl[idx].on_time;
	led_cmd.off = blink_tbl[idx].off_time;

	return iwl_send_led_cmd(priv, &led_cmd);
}


/* Set led on command */
static int iwl3945_led_on(struct iwl_priv *priv, int led_id)
{
	struct iwl_led_cmd led_cmd = {
		.id = led_id,
		.on = IWL_LED_SOLID,
		.off = 0,
		.interval = IWL_DEF_LED_INTRVL
	};
	return iwl_send_led_cmd(priv, &led_cmd);
}

/* Set led off command */
static int iwl3945_led_off(struct iwl_priv *priv, int led_id)
{
	struct iwl_led_cmd led_cmd = {
		.id = led_id,
		.on = 0,
		.off = 0,
		.interval = IWL_DEF_LED_INTRVL
	};
	IWL_DEBUG_LED(priv, "led off %d\n", led_id);
	return iwl_send_led_cmd(priv, &led_cmd);
}

/*
 *  Set led on in case of association
 *  */
static int iwl3945_led_associate(struct iwl_priv *priv, int led_id)
{
	IWL_DEBUG_LED(priv, "Associated\n");

	priv->allow_blinking = 1;
	return iwl3945_led_on(priv, led_id);
}
/* Set Led off in case of disassociation */
static int iwl3945_led_disassociate(struct iwl_priv *priv, int led_id)
{
	IWL_DEBUG_LED(priv, "Disassociated\n");

	priv->allow_blinking = 0;

	return 0;
}

/*
 * brightness call back function for Tx/Rx LED
 */
static int iwl3945_led_associated(struct iwl_priv *priv, int led_id)
{
	if (test_bit(STATUS_EXIT_PENDING, &priv->status) ||
	    !test_bit(STATUS_READY, &priv->status))
		return 0;


	/* start counting Tx/Rx bytes */
	if (!priv->last_blink_time && priv->allow_blinking)
		priv->last_blink_time = jiffies;
	return 0;
}

/*
 * brightness call back for association and radio
 */
static void iwl3945_led_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	struct iwl_led *led = container_of(led_cdev,
					   struct iwl_led, led_dev);
	struct iwl_priv *priv = led->priv;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	IWL_DEBUG_LED(priv, "Led type = %s brightness = %d\n",
			led_type_str[led->type], brightness);

	switch (brightness) {
	case LED_FULL:
		if (led->led_on)
			led->led_on(priv, IWL_LED_LINK);
		break;
	case LED_OFF:
		if (led->led_off)
			led->led_off(priv, IWL_LED_LINK);
		break;
	default:
		if (led->led_pattern) {
			int idx = iwl3945_brightness_to_idx(brightness);
			led->led_pattern(priv, IWL_LED_LINK, idx);
		}
		break;
	}
}

/*
 * Register led class with the system
 */
static int iwl3945_led_register_led(struct iwl_priv *priv,
				   struct iwl_led *led,
				   enum led_type type, u8 set_led,
				   char *trigger)
{
	struct device *device = wiphy_dev(priv->hw->wiphy);
	int ret;

	led->led_dev.name = led->name;
	led->led_dev.brightness_set = iwl3945_led_brightness_set;
	led->led_dev.default_trigger = trigger;

	led->priv = priv;
	led->type = type;

	ret = led_classdev_register(device, &led->led_dev);
	if (ret) {
		IWL_ERR(priv, "Error: failed to register led handler.\n");
		return ret;
	}

	led->registered = 1;

	if (set_led && led->led_on)
		led->led_on(priv, IWL_LED_LINK);
	return 0;
}


/*
 * calculate blink rate according to last 2 sec Tx/Rx activities
 */
static inline u8 get_blink_rate(struct iwl_priv *priv)
{
	int index;
	s64 tpt = priv->rxtxpackets;

	if (tpt < 0)
		tpt = -tpt;

	IWL_DEBUG_LED(priv, "tpt %lld \n", (long long)tpt);

	if (!priv->allow_blinking)
		index = IWL_MAX_BLINK_TBL;
	else
		for (index = 0; index < IWL_MAX_BLINK_TBL; index++)
			if (tpt > (blink_tbl[index].brightness * IWL_1MB_RATE))
				break;

	IWL_DEBUG_LED(priv, "LED BLINK IDX=%d\n", index);
	return index;
}

/*
 * this function called from handler. Since setting Led command can
 * happen very frequent we postpone led command to be called from
 * REPLY handler so we know ucode is up
 */
void iwl3945_led_background(struct iwl_priv *priv)
{
	u8 blink_idx;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		priv->last_blink_time = 0;
		return;
	}
	if (iwl_is_rfkill(priv)) {
		priv->last_blink_time = 0;
		return;
	}

	if (!priv->allow_blinking) {
		priv->last_blink_time = 0;
		if (priv->last_blink_rate != IWL_SOLID_BLINK_IDX) {
			priv->last_blink_rate = IWL_SOLID_BLINK_IDX;
			iwl3945_led_pattern(priv, IWL_LED_LINK,
					    IWL_SOLID_BLINK_IDX);
		}
		return;
	}
	if (!priv->last_blink_time ||
	    !time_after(jiffies, priv->last_blink_time +
			msecs_to_jiffies(1000)))
		return;

	blink_idx = get_blink_rate(priv);

	/* call only if blink rate change */
	if (blink_idx != priv->last_blink_rate)
		iwl3945_led_pattern(priv, IWL_LED_LINK, blink_idx);

	priv->last_blink_time = jiffies;
	priv->last_blink_rate = blink_idx;
	priv->rxtxpackets = 0;
}


/* Register all led handler */
int iwl3945_led_register(struct iwl_priv *priv)
{
	char *trigger;
	int ret;

	priv->last_blink_rate = 0;
	priv->rxtxpackets = 0;
	priv->led_tpt = 0;
	priv->last_blink_time = 0;
	priv->allow_blinking = 0;

	trigger = ieee80211_get_radio_led_name(priv->hw);
	snprintf(priv->led[IWL_LED_TRG_RADIO].name,
		 sizeof(priv->led[IWL_LED_TRG_RADIO].name), "iwl-%s::radio",
		 wiphy_name(priv->hw->wiphy));

	priv->led[IWL_LED_TRG_RADIO].led_on = iwl3945_led_on;
	priv->led[IWL_LED_TRG_RADIO].led_off = iwl3945_led_off;
	priv->led[IWL_LED_TRG_RADIO].led_pattern = NULL;

	ret = iwl3945_led_register_led(priv,
				   &priv->led[IWL_LED_TRG_RADIO],
				   IWL_LED_TRG_RADIO, 1, trigger);

	if (ret)
		goto exit_fail;

	trigger = ieee80211_get_assoc_led_name(priv->hw);
	snprintf(priv->led[IWL_LED_TRG_ASSOC].name,
		 sizeof(priv->led[IWL_LED_TRG_ASSOC].name), "iwl-%s::assoc",
		 wiphy_name(priv->hw->wiphy));

	ret = iwl3945_led_register_led(priv,
				   &priv->led[IWL_LED_TRG_ASSOC],
				   IWL_LED_TRG_ASSOC, 0, trigger);

	/* for assoc always turn led on */
	priv->led[IWL_LED_TRG_ASSOC].led_on = iwl3945_led_associate;
	priv->led[IWL_LED_TRG_ASSOC].led_off = iwl3945_led_disassociate;
	priv->led[IWL_LED_TRG_ASSOC].led_pattern = NULL;

	if (ret)
		goto exit_fail;

	trigger = ieee80211_get_rx_led_name(priv->hw);
	snprintf(priv->led[IWL_LED_TRG_RX].name,
		 sizeof(priv->led[IWL_LED_TRG_RX].name), "iwl-%s::RX",
		 wiphy_name(priv->hw->wiphy));

	ret = iwl3945_led_register_led(priv,
				   &priv->led[IWL_LED_TRG_RX],
				   IWL_LED_TRG_RX, 0, trigger);

	priv->led[IWL_LED_TRG_RX].led_on = iwl3945_led_associated;
	priv->led[IWL_LED_TRG_RX].led_off = iwl3945_led_associated;
	priv->led[IWL_LED_TRG_RX].led_pattern = iwl3945_led_pattern;

	if (ret)
		goto exit_fail;

	trigger = ieee80211_get_tx_led_name(priv->hw);
	snprintf(priv->led[IWL_LED_TRG_TX].name,
		 sizeof(priv->led[IWL_LED_TRG_TX].name), "iwl-%s::TX",
		 wiphy_name(priv->hw->wiphy));

	ret = iwl3945_led_register_led(priv,
				   &priv->led[IWL_LED_TRG_TX],
				   IWL_LED_TRG_TX, 0, trigger);

	priv->led[IWL_LED_TRG_TX].led_on = iwl3945_led_associated;
	priv->led[IWL_LED_TRG_TX].led_off = iwl3945_led_associated;
	priv->led[IWL_LED_TRG_TX].led_pattern = iwl3945_led_pattern;

	if (ret)
		goto exit_fail;

	return 0;

exit_fail:
	iwl3945_led_unregister(priv);
	return ret;
}


/* unregister led class */
static void iwl3945_led_unregister_led(struct iwl_led *led, u8 set_led)
{
	if (!led->registered)
		return;

	led_classdev_unregister(&led->led_dev);

	if (set_led)
		led->led_dev.brightness_set(&led->led_dev, LED_OFF);
	led->registered = 0;
}

/* Unregister all led handlers */
void iwl3945_led_unregister(struct iwl_priv *priv)
{
	iwl3945_led_unregister_led(&priv->led[IWL_LED_TRG_ASSOC], 0);
	iwl3945_led_unregister_led(&priv->led[IWL_LED_TRG_RX], 0);
	iwl3945_led_unregister_led(&priv->led[IWL_LED_TRG_TX], 0);
	iwl3945_led_unregister_led(&priv->led[IWL_LED_TRG_RADIO], 1);
}

#endif
