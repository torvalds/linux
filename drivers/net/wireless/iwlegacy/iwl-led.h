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

#ifndef __il_leds_h__
#define __il_leds_h__


struct il_priv;

#define IL_LED_SOLID 11
#define IL_DEF_LED_INTRVL cpu_to_le32(1000)

#define IL_LED_ACTIVITY       (0<<1)
#define IL_LED_LINK           (1<<1)

/*
 * LED mode
 *    IL_LED_DEFAULT:  use device default
 *    IL_LED_RF_STATE: turn LED on/off based on RF state
 *			LED ON  = RF ON
 *			LED OFF = RF OFF
 *    IL_LED_BLINK:    adjust led blink rate based on blink table
 */
enum il_led_mode {
	IL_LED_DEFAULT,
	IL_LED_RF_STATE,
	IL_LED_BLINK,
};

void il_leds_init(struct il_priv *il);
void il_leds_exit(struct il_priv *il);

#endif /* __il_leds_h__ */
