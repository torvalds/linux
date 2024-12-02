/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
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
