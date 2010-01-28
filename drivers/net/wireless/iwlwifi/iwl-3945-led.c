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
#include "iwl-3945-led.h"


/* Send led command */
static int iwl3945_send_led_cmd(struct iwl_priv *priv,
				struct iwl_led_cmd *led_cmd)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_LEDS_CMD,
		.len = sizeof(struct iwl_led_cmd),
		.data = led_cmd,
		.flags = CMD_ASYNC,
		.callback = NULL,
	};

	return iwl_send_cmd(priv, &cmd);
}

/* Set led on command */
static int iwl3945_led_on(struct iwl_priv *priv)
{
	struct iwl_led_cmd led_cmd = {
		.id = IWL_LED_LINK,
		.on = IWL_LED_SOLID,
		.off = 0,
		.interval = IWL_DEF_LED_INTRVL
	};
	return iwl3945_send_led_cmd(priv, &led_cmd);
}

/* Set led off command */
static int iwl3945_led_off(struct iwl_priv *priv)
{
	struct iwl_led_cmd led_cmd = {
		.id = IWL_LED_LINK,
		.on = 0,
		.off = 0,
		.interval = IWL_DEF_LED_INTRVL
	};
	IWL_DEBUG_LED(priv, "led off\n");
	return iwl3945_send_led_cmd(priv, &led_cmd);
}

const struct iwl_led_ops iwl3945_led_ops = {
	.cmd = iwl3945_send_led_cmd,
	.on = iwl3945_led_on,
	.off = iwl3945_led_off,
};
