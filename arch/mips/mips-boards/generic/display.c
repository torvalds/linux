/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Display routines for display messages in MIPS boards ascii display.
 */

#include <linux/compiler.h>
#include <asm/io.h>
#include <asm/mips-boards/generic.h>

void mips_display_message(const char *str)
{
	static volatile unsigned int *display = NULL;
	int i;

	if (unlikely(display == NULL))
		display = (volatile unsigned int *)ioremap(ASCII_DISPLAY_POS_BASE, 16*sizeof(int));

	for (i = 0; i <= 14; i=i+2) {
	         if (*str)
		         display[i] = *str++;
		 else
		         display[i] = ' ';
	}
}
