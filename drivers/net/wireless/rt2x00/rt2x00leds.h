/*
	Copyright (C) 2004 - 2009 rt2x00 SourceForge Project
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

enum led_type {
	LED_TYPE_RADIO,
	LED_TYPE_ASSOC,
	LED_TYPE_ACTIVITY,
	LED_TYPE_QUALITY,
};

struct rt2x00_led {
	struct rt2x00_dev *rt2x00dev;
	struct led_classdev led_dev;

	enum led_type type;
	unsigned int flags;
#define LED_INITIALIZED		( 1 << 0 )
#define LED_REGISTERED		( 1 << 1 )
};

#endif /* RT2X00LEDS_H */
