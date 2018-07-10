/* vi: set sw=4 ts=4: */
/*
 * Various system configuration helpers.
 *
 * Copyright (C) 2014 Bartosz Golaszewski <bartekgola@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

#if !defined(bb_arg_max)
unsigned FAST_FUNC bb_arg_max(void)
{
	long r = sysconf(_SC_ARG_MAX);

	/* I've seen a version of uclibc which returned -1.
	 * Guard about it, and also avoid insanely large values
	 */
	if ((unsigned long)r > 64*1024*1024)
		r = 64*1024*1024;

	return r;
}
#endif

/* Return the number of clock ticks per second. */
unsigned FAST_FUNC bb_clk_tck(void)
{
	return sysconf(_SC_CLK_TCK);
}
