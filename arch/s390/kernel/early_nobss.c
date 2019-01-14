// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007, 2018
 */

/*
 * Early setup functions which may not rely on an initialized bss
 * section. The last thing that is supposed to happen here is
 * initialization of the bss section.
 */

#include <linux/processor.h>
#include <linux/string.h>
#include <asm/sections.h>
#include <asm/lowcore.h>
#include <asm/timex.h>
#include <asm/kasan.h>
#include "entry.h"

static void __init reset_tod_clock(void)
{
	u64 time;

	if (store_tod_clock(&time) == 0)
		return;
	/* TOD clock not running. Set the clock to Unix Epoch. */
	if (set_tod_clock(TOD_UNIX_EPOCH) != 0 || store_tod_clock(&time) != 0)
		disabled_wait(0);

	memset(tod_clock_base, 0, 16);
	*(__u64 *) &tod_clock_base[1] = TOD_UNIX_EPOCH;
	S390_lowcore.last_update_clock = TOD_UNIX_EPOCH;
}

static void __init clear_bss_section(void)
{
	memset(__bss_start, 0, __bss_stop - __bss_start);
}

void __init startup_init_nobss(void)
{
	reset_tod_clock();
	clear_bss_section();
	kasan_early_init();
}
