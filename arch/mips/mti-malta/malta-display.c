/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Display routines for display messages in MIPS boards ascii display.
 *
 * Copyright (C) 1999,2000,2012  MIPS Technologies, Inc.
 * All rights reserved.
 * Authors: Carsten Langgaard <carstenl@mips.com>
 *          Steven J. Hill <sjhill@mips.com>
 */
#include <linux/compiler.h>
#include <linux/timer.h>
#include <linux/io.h>

#include <asm/mips-boards/generic.h>

extern const char display_string[];
static unsigned int display_count;
static unsigned int max_display_count;

void mips_display_message(const char *str)
{
	static unsigned int __iomem *display = NULL;
	int i;

	if (unlikely(display == NULL))
		display = ioremap(ASCII_DISPLAY_POS_BASE, 16*sizeof(int));

	for (i = 0; i <= 14; i += 2) {
		if (*str)
			__raw_writel(*str++, display + i);
		else
			__raw_writel(' ', display + i);
	}
}

static void scroll_display_message(struct timer_list *unused);
static DEFINE_TIMER(mips_scroll_timer, scroll_display_message);

static void scroll_display_message(struct timer_list *unused)
{
	mips_display_message(&display_string[display_count++]);
	if (display_count == max_display_count)
		display_count = 0;

	mod_timer(&mips_scroll_timer, jiffies + HZ);
}

void mips_scroll_message(void)
{
	del_timer_sync(&mips_scroll_timer);
	max_display_count = strlen(display_string) + 1 - 8;
	mod_timer(&mips_scroll_timer, jiffies + 1);
}
