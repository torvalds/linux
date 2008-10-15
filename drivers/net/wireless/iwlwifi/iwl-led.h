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

#ifndef __iwl_leds_h__
#define __iwl_leds_h__


struct iwl_priv;

#ifdef CONFIG_IWLWIFI_LEDS
#include <linux/leds.h>

#define IWL_LED_SOLID 11
#define IWL_LED_NAME_LEN 31
#define IWL_DEF_LED_INTRVL __constant_cpu_to_le32(1000)

#define IWL_LED_ACTIVITY       (0<<1)
#define IWL_LED_LINK           (1<<1)

enum led_type {
	IWL_LED_TRG_TX,
	IWL_LED_TRG_RX,
	IWL_LED_TRG_ASSOC,
	IWL_LED_TRG_RADIO,
	IWL_LED_TRG_MAX,
};


struct iwl_led {
	struct iwl_priv *priv;
	struct led_classdev led_dev;
	char name[32];

	int (*led_on) (struct iwl_priv *priv, int led_id);
	int (*led_off) (struct iwl_priv *priv, int led_id);
	int (*led_pattern) (struct iwl_priv *priv, int led_id, unsigned int idx);

	enum led_type type;
	unsigned int registered;
};

int iwl_leds_register(struct iwl_priv *priv);
void iwl_leds_unregister(struct iwl_priv *priv);
void iwl_leds_background(struct iwl_priv *priv);

#else
static inline int iwl_leds_register(struct iwl_priv *priv)
{
	return 0;
}
static inline void iwl_leds_unregister(struct iwl_priv *priv)
{
}
static inline void iwl_leds_background(struct iwl_priv *priv)
{
}

#endif /* CONFIG_IWLWIFI_LEDS */
#endif /* __iwl_leds_h__ */
