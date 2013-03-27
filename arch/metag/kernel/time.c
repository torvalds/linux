/*
 * Copyright (C) 2005-2013 Imagination Technologies Ltd.
 *
 * This file contains the Meta-specific time handling details.
 *
 */

#include <linux/init.h>

#include <clocksource/metag_generic.h>

void __init time_init(void)
{
	metag_generic_timer_init();
}
