/*
 * ps4.h: Sony PS4 platform setup code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _ASM_X86_PS4_H
#define _ASM_X86_PS4_H

#include <linux/time.h>

#define PS4_DEFAULT_TSC_FREQ 1594000000

#define EMC_TIMER_BASE 0xd0281000
#define EMC_TIMER_VALUE 0x28

extern unsigned long ps4_calibrate_tsc(void);

#endif
