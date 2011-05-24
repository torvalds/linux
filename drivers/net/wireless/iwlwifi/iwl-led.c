/******************************************************************************
 *
 * Copyright(c) 2003 - 2010 Intel Corporation. All rights reserved.
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

/* default: IWL_LED_BLINK(0) using blinking index table */
static int led_mode;
module_param(led_mode, int, S_IRUGO);
MODULE_PARM_DESC(led_mode, "0=system default, "
		"1=On(RF On)/Off(RF Off), 2=blinking");

static const struct ieee80211_tpt_blink iwl_blink[] = {
	{ .throughput = 0 * 1024 - 1, .blink_time = 334 },
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

/*
 * Adjust led blink rate to compensate on a MAC Clock difference on every HW
 * Led blink rate analysis showed an average deviation of 0% on 3945,
 * 5% on 4965 HW and 20% on 5000 series and up.
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

	IWL_DEBUG_LED(priv, "Led blink time compensation=%u\n",
			priv->cfg->base_params->led_compensation);
	led_cmd.on = iwl_blink_compensation(priv, on,
				priv->cfg->base_params->led_compensation);
	led_cmd.off = iwl_blink_compensation(priv, off,
				priv->cfg->base_params->led_compensation);

	ret = priv->cfg->ops->led->cmd(priv, &led_cmd);
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

	if (brightness > 0)
		on = IWL_LED_SOLID;

	iwl_led_cmd(priv, on, 0);
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
	int mode = led_mode;
	int ret;

	if (mode == IWL_LED_DEFAULT)
		mode = priv->cfg->led_mode;

	priv->led.name = kasprintf(GFP_KERNEL, "%s-led",
				   wiphy_name(priv->hw->wiphy));
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

	ret = led_classdev_register(&priv->pci_dev->dev, &priv->led);
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
