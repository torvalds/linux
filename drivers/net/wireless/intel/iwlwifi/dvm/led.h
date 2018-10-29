/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
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
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_leds_h__
#define __iwl_leds_h__


struct iwl_priv;

#define IWL_LED_SOLID 11
#define IWL_DEF_LED_INTRVL cpu_to_le32(1000)

#define IWL_LED_ACTIVITY       (0<<1)
#define IWL_LED_LINK           (1<<1)

#ifdef CONFIG_IWLWIFI_LEDS
void iwlagn_led_enable(struct iwl_priv *priv);
void iwl_leds_init(struct iwl_priv *priv);
void iwl_leds_exit(struct iwl_priv *priv);
#else
static inline void iwlagn_led_enable(struct iwl_priv *priv)
{
}
static inline void iwl_leds_init(struct iwl_priv *priv)
{
}
static inline void iwl_leds_exit(struct iwl_priv *priv)
{
}
#endif

#endif /* __iwl_leds_h__ */
