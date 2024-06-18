// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006 PA Semi, Inc
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 */

#include <linux/time.h>

#include <asm/time.h>

#include "pasemi.h"

time64_t __init pas_get_boot_time(void)
{
	/* Let's just return a fake date right now */
	return mktime64(2006, 1, 1, 12, 0, 0);
}
