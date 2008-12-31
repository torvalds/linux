/*
 *  arch/mips/emma2rh/markeins/led.c
 *      This file defines the led display for Mark-eins.
 *
 *  Copyright (C) NEC Electronics Corporation 2004-2006
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <asm/emma/emma2rh.h>

const unsigned long clear = 0x20202020;

#define LED_BASE 0xb1400038

void markeins_led_clear(void)
{
	emma2rh_out32(LED_BASE, clear);
	emma2rh_out32(LED_BASE + 4, clear);
}

void markeins_led(const char *str)
{
	int i;
	int len = strlen(str);

	markeins_led_clear();
	if (len > 8)
		len = 8;

	if (emma2rh_in32(0xb0000800) & (0x1 << 18))
		for (i = 0; i < len; i++)
			emma2rh_out8(LED_BASE + i, str[i]);
	else
		for (i = 0; i < len; i++)
			emma2rh_out8(LED_BASE + (i & 4) + (3 - (i & 3)),
				     str[i]);
}

void markeins_led_hex(u32 val)
{
	char str[10];

	sprintf(str, "%08x", val);
	markeins_led(str);
}
