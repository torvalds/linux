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

#ifndef IWL3945_LEDS_H
#define IWL3945_LEDS_H

struct iwl_priv;

#ifdef CONFIG_IWLWIFI_LEDS

#include "iwl-led.h"

extern int iwl3945_led_register(struct iwl_priv *priv);
extern void iwl3945_led_unregister(struct iwl_priv *priv);
extern void iwl3945_led_background(struct iwl_priv *priv);

#else
static inline int iwl3945_led_register(struct iwl_priv *priv) { return 0; }
static inline void iwl3945_led_unregister(struct iwl_priv *priv) {}
static inline void iwl3945_led_background(struct iwl_priv *priv) {}

#endif /* IWLWIFI_LEDS*/
#endif /* IWL3945_LEDS_H */
