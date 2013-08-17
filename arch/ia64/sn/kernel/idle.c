/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2004 Silicon Graphics, Inc.  All rights reserved.
 */

#include <asm/sn/leds.h>

void snidle(int state)
{
	if (state) {
		if (pda->idle_flag == 0) {
			/* 
			 * Turn the activity LED off.
			 */
			set_led_bits(0, LED_CPU_ACTIVITY);
		}

		pda->idle_flag = 1;
	} else {
		/* 
		 * Turn the activity LED on.
		 */
		set_led_bits(LED_CPU_ACTIVITY, LED_CPU_ACTIVITY);

		pda->idle_flag = 0;
	}
}
