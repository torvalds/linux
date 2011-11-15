/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
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
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"

/* default: IL_LED_BLINK(0) using blinking idx table */
static int led_mode;
module_param(led_mode, int, S_IRUGO);
MODULE_PARM_DESC(led_mode, "0=system default, "
		"1=On(RF On)/Off(RF Off), 2=blinking");

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
static const struct ieee80211_tpt_blink il_blink[] = {
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

/*
 * Adjust led blink rate to compensate on a MAC Clock difference on every HW
 * Led blink rate analysis showed an average deviation of 0% on 3945,
 * 5% on 4965 HW.
 * Need to compensate on the led on/off time per HW according to the deviation
 * to achieve the desired led frequency
 * The calculation is: (100-averageDeviation)/100 * blinkTime
 * For code efficiency the calculation will be:
 *     compensation = (100 - averageDeviation) * 64 / 100
 *     NewBlinkTime = (compensation * BlinkTime) / 64
 */
static inline u8 il_blink_compensation(struct il_priv *il,
				    u8 time, u16 compensation)
{
	if (!compensation) {
		IL_ERR("undefined blink compensation: "
			"use pre-defined blinking time\n");
		return time;
	}

	return (u8)((time * compensation) >> 6);
}

/* Set led pattern command */
static int il_led_cmd(struct il_priv *il,
		       unsigned long on,
		       unsigned long off)
{
	struct il_led_cmd led_cmd = {
		.id = IL_LED_LINK,
		.interval = IL_DEF_LED_INTRVL
	};
	int ret;

	if (!test_bit(STATUS_READY, &il->status))
		return -EBUSY;

	if (il->blink_on == on && il->blink_off == off)
		return 0;

	if (off == 0) {
		/* led is SOLID_ON */
		on = IL_LED_SOLID;
	}

	D_LED("Led blink time compensation=%u\n",
			il->cfg->base_params->led_compensation);
	led_cmd.on = il_blink_compensation(il, on,
				il->cfg->base_params->led_compensation);
	led_cmd.off = il_blink_compensation(il, off,
				il->cfg->base_params->led_compensation);

	ret = il->cfg->ops->led->cmd(il, &led_cmd);
	if (!ret) {
		il->blink_on = on;
		il->blink_off = off;
	}
	return ret;
}

static void il_led_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	struct il_priv *il = container_of(led_cdev, struct il_priv, led);
	unsigned long on = 0;

	if (brightness > 0)
		on = IL_LED_SOLID;

	il_led_cmd(il, on, 0);
}

static int il_led_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct il_priv *il = container_of(led_cdev, struct il_priv, led);

	return il_led_cmd(il, *delay_on, *delay_off);
}

void il_leds_init(struct il_priv *il)
{
	int mode = led_mode;
	int ret;

	if (mode == IL_LED_DEFAULT)
		mode = il->cfg->led_mode;

	il->led.name = kasprintf(GFP_KERNEL, "%s-led",
				   wiphy_name(il->hw->wiphy));
	il->led.brightness_set = il_led_brightness_set;
	il->led.blink_set = il_led_blink_set;
	il->led.max_brightness = 1;

	switch (mode) {
	case IL_LED_DEFAULT:
		WARN_ON(1);
		break;
	case IL_LED_BLINK:
		il->led.default_trigger =
			ieee80211_create_tpt_led_trigger(il->hw,
					IEEE80211_TPT_LEDTRIG_FL_CONNECTED,
					il_blink, ARRAY_SIZE(il_blink));
		break;
	case IL_LED_RF_STATE:
		il->led.default_trigger =
			ieee80211_get_radio_led_name(il->hw);
		break;
	}

	ret = led_classdev_register(&il->pci_dev->dev, &il->led);
	if (ret) {
		kfree(il->led.name);
		return;
	}

	il->led_registered = true;
}
EXPORT_SYMBOL(il_leds_init);

void il_leds_exit(struct il_priv *il)
{
	if (!il->led_registered)
		return;

	led_classdev_unregister(&il->led);
	kfree(il->led.name);
}
EXPORT_SYMBOL(il_leds_exit);
