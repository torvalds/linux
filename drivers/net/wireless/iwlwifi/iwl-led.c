/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/


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

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"

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
	u16 tpt;
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
#define IWL_MAX_BLINK_TBL (ARRAY_SIZE(blink_tbl) - 1) /* exclude SOLID_ON */
#define IWL_SOLID_BLINK_IDX (ARRAY_SIZE(blink_tbl) - 1)

/*  [0-256] -> [0..8] FIXME: we need [0..10] */
static inline int iwl_brightness_to_idx(enum led_brightness brightness)
{
	return fls(0x000000FF & (u32)brightness);
}

/* Send led command */
static int iwl_send_led_cmd(struct iwl_priv *priv, struct iwl_led_cmd *led_cmd)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_LEDS_CMD,
		.len = sizeof(struct iwl_led_cmd),
		.data = led_cmd,
		.meta.flags = CMD_ASYNC,
		.meta.u.callback = NULL,
	};
	u32 reg;

	reg = iwl_read32(priv, CSR_LED_REG);
	if (reg != (reg & CSR_LED_BSM_CTRL_MSK))
		iwl_write32(priv, CSR_LED_REG, reg & CSR_LED_BSM_CTRL_MSK);

	return iwl_send_cmd(priv, &cmd);
}

/* Set led pattern command */
static int iwl4965_led_pattern(struct iwl_priv *priv, int led_id,
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

/* Set led register off */
static int iwl4965_led_on_reg(struct iwl_priv *priv, int led_id)
{
	IWL_DEBUG_LED("led on %d\n", led_id);
	iwl_write32(priv, CSR_LED_REG, CSR_LED_REG_TRUN_ON);
	return 0;
}

#if 0
/* Set led on command */
static int iwl4965_led_on(struct iwl_priv *priv, int led_id)
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
int iwl4965_led_off(struct iwl_priv *priv, int led_id)
{
	struct iwl_led_cmd led_cmd = {
		.id = led_id,
		.on = 0,
		.off = 0,
		.interval = IWL_DEF_LED_INTRVL
	};
	IWL_DEBUG_LED("led off %d\n", led_id);
	return iwl_send_led_cmd(priv, &led_cmd);
}
#endif


/* Set led register off */
static int iwl4965_led_off_reg(struct iwl_priv *priv, int led_id)
{
	IWL_DEBUG_LED("LED Reg off\n");
	iwl_write32(priv, CSR_LED_REG, CSR_LED_REG_TRUN_OFF);
	return 0;
}

/*
 * Set led register in case of disassociation according to rfkill state
 */
static int iwl_led_associate(struct iwl_priv *priv, int led_id)
{
	IWL_DEBUG_LED("Associated\n");
	priv->allow_blinking = 1;
	return iwl4965_led_on_reg(priv, led_id);
}
static int iwl_led_disassociate(struct iwl_priv *priv, int led_id)
{
	priv->allow_blinking = 0;
	if (iwl_is_rfkill(priv))
		iwl4965_led_off_reg(priv, led_id);
	else
		iwl4965_led_on_reg(priv, led_id);

	return 0;
}

/*
 * brightness call back function for Tx/Rx LED
 */
static int iwl_led_associated(struct iwl_priv *priv, int led_id)
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
static void iwl_led_brightness_set(struct led_classdev *led_cdev,
				       enum led_brightness brightness)
{
	struct iwl_led *led = container_of(led_cdev, struct iwl_led, led_dev);
	struct iwl_priv *priv = led->priv;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;


	IWL_DEBUG_LED("Led type = %s brightness = %d\n",
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
			int idx = iwl_brightness_to_idx(brightness);
			led->led_pattern(priv, IWL_LED_LINK, idx);
		}
		break;
	}
}



/*
 * Register led class with the system
 */
static int iwl_leds_register_led(struct iwl_priv *priv, struct iwl_led *led,
				   enum led_type type, u8 set_led,
				   char *trigger)
{
	struct device *device = wiphy_dev(priv->hw->wiphy);
	int ret;

	led->led_dev.name = led->name;
	led->led_dev.brightness_set = iwl_led_brightness_set;
	led->led_dev.default_trigger = trigger;

	led->priv = priv;
	led->type = type;

	ret = led_classdev_register(device, &led->led_dev);
	if (ret) {
		IWL_ERROR("Error: failed to register led handler.\n");
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
static int iwl_get_blink_rate(struct iwl_priv *priv)
{
	int i;
	u64 current_tpt = priv->tx_stats[2].bytes;
	/* FIXME: + priv->rx_stats[2].bytes; */
	s64 tpt = current_tpt - priv->led_tpt;

	if (tpt < 0) /* wrapparound */
		tpt = -tpt;

	IWL_DEBUG_LED("tpt %lld current_tpt %llu\n",
		(long long)tpt,
		(unsigned long long)current_tpt);
	priv->led_tpt = current_tpt;

	if (!priv->allow_blinking)
		i = IWL_MAX_BLINK_TBL;
	else
		for (i = 0; i < IWL_MAX_BLINK_TBL; i++)
			if (tpt  > (blink_tbl[i].tpt * IWL_1MB_RATE))
				break;

	IWL_DEBUG_LED("LED BLINK IDX=%d", i);
	return i;
}

/*
 * this function called from handler. Since setting Led command can
 * happen very frequent we postpone led command to be called from
 * REPLY handler so we know ucode is up
 */
void iwl_leds_background(struct iwl_priv *priv)
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
			iwl4965_led_pattern(priv, IWL_LED_LINK,
					    IWL_SOLID_BLINK_IDX);
		}
		return;
	}
	if (!priv->last_blink_time ||
	    !time_after(jiffies, priv->last_blink_time +
			msecs_to_jiffies(1000)))
		return;

	blink_idx = iwl_get_blink_rate(priv);

	/* call only if blink rate change */
	if (blink_idx != priv->last_blink_rate)
		iwl4965_led_pattern(priv, IWL_LED_LINK, blink_idx);

	priv->last_blink_time = jiffies;
	priv->last_blink_rate = blink_idx;
}
EXPORT_SYMBOL(iwl_leds_background);

/* Register all led handler */
int iwl_leds_register(struct iwl_priv *priv)
{
	char *trigger;
	int ret;

	priv->last_blink_rate = 0;
	priv->led_tpt = 0;
	priv->last_blink_time = 0;
	priv->allow_blinking = 0;

	trigger = ieee80211_get_radio_led_name(priv->hw);
	snprintf(priv->led[IWL_LED_TRG_RADIO].name,
		 sizeof(priv->led[IWL_LED_TRG_RADIO].name), "iwl-%s:radio",
		 wiphy_name(priv->hw->wiphy));

	priv->led[IWL_LED_TRG_RADIO].led_on = iwl4965_led_on_reg;
	priv->led[IWL_LED_TRG_RADIO].led_off = iwl4965_led_off_reg;
	priv->led[IWL_LED_TRG_RADIO].led_pattern = NULL;

	ret = iwl_leds_register_led(priv, &priv->led[IWL_LED_TRG_RADIO],
				   IWL_LED_TRG_RADIO, 1, trigger);
	if (ret)
		goto exit_fail;

	trigger = ieee80211_get_assoc_led_name(priv->hw);
	snprintf(priv->led[IWL_LED_TRG_ASSOC].name,
		 sizeof(priv->led[IWL_LED_TRG_ASSOC].name), "iwl-%s:assoc",
		 wiphy_name(priv->hw->wiphy));

	ret = iwl_leds_register_led(priv, &priv->led[IWL_LED_TRG_ASSOC],
				   IWL_LED_TRG_ASSOC, 0, trigger);

	/* for assoc always turn led on */
	priv->led[IWL_LED_TRG_ASSOC].led_on = iwl_led_associate;
	priv->led[IWL_LED_TRG_ASSOC].led_off = iwl_led_disassociate;
	priv->led[IWL_LED_TRG_ASSOC].led_pattern = NULL;

	if (ret)
		goto exit_fail;

	trigger = ieee80211_get_rx_led_name(priv->hw);
	snprintf(priv->led[IWL_LED_TRG_RX].name,
		 sizeof(priv->led[IWL_LED_TRG_RX].name), "iwl-%s:RX",
		 wiphy_name(priv->hw->wiphy));

	ret = iwl_leds_register_led(priv, &priv->led[IWL_LED_TRG_RX],
				   IWL_LED_TRG_RX, 0, trigger);

	priv->led[IWL_LED_TRG_RX].led_on = iwl_led_associated;
	priv->led[IWL_LED_TRG_RX].led_off = iwl_led_associated;
	priv->led[IWL_LED_TRG_RX].led_pattern = iwl4965_led_pattern;

	if (ret)
		goto exit_fail;

	trigger = ieee80211_get_tx_led_name(priv->hw);
	snprintf(priv->led[IWL_LED_TRG_TX].name,
		 sizeof(priv->led[IWL_LED_TRG_TX].name), "iwl-%s:TX",
		 wiphy_name(priv->hw->wiphy));

	ret = iwl_leds_register_led(priv, &priv->led[IWL_LED_TRG_TX],
				   IWL_LED_TRG_TX, 0, trigger);

	priv->led[IWL_LED_TRG_TX].led_on = iwl_led_associated;
	priv->led[IWL_LED_TRG_TX].led_off = iwl_led_associated;
	priv->led[IWL_LED_TRG_TX].led_pattern = iwl4965_led_pattern;

	if (ret)
		goto exit_fail;

	return 0;

exit_fail:
	iwl_leds_unregister(priv);
	return ret;
}
EXPORT_SYMBOL(iwl_leds_register);

/* unregister led class */
static void iwl_leds_unregister_led(struct iwl_led *led, u8 set_led)
{
	if (!led->registered)
		return;

	led_classdev_unregister(&led->led_dev);

	if (set_led)
		led->led_dev.brightness_set(&led->led_dev, LED_OFF);
	led->registered = 0;
}

/* Unregister all led handlers */
void iwl_leds_unregister(struct iwl_priv *priv)
{
	iwl_leds_unregister_led(&priv->led[IWL_LED_TRG_ASSOC], 0);
	iwl_leds_unregister_led(&priv->led[IWL_LED_TRG_RX], 0);
	iwl_leds_unregister_led(&priv->led[IWL_LED_TRG_TX], 0);
	iwl_leds_unregister_led(&priv->led[IWL_LED_TRG_RADIO], 1);
}
EXPORT_SYMBOL(iwl_leds_unregister);

