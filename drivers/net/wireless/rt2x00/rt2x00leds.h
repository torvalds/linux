/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 led datastructures and routines
 */

#ifndef RT2X00LEDS_H
#define RT2X00LEDS_H

/*
* Flags used by driver to indicate which
 * which led types are supported.
 */
#define LED_SUPPORT_RADIO	0x000001
#define LED_SUPPORT_ASSOC	0x000002
#define LED_SUPPORT_ACTIVITY	0x000004
#define LED_SUPPORT_QUALITY	0x000008

enum led_type {
	LED_TYPE_RADIO,
	LED_TYPE_ASSOC,
	LED_TYPE_QUALITY,
};

#ifdef CONFIG_RT2X00_LIB_LEDS

struct rt2x00_led {
	struct rt2x00_dev *rt2x00dev;
	struct led_classdev led_dev;

	enum led_type type;
	unsigned int registered;
};

struct rt2x00_trigger {
	struct led_trigger trigger;

	enum led_type type;
	unsigned int registered;
};

#endif /* CONFIG_RT2X00_LIB_LEDS */

#endif /* RT2X00LEDS_H */
