/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Extracting time information from ARCS prom.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 */
#include <linux/init.h>

#include <asm/fw/arc/types.h>
#include <asm/sgialib.h>

struct linux_tinfo * __init
ArcGetTime(VOID)
{
	return (struct linux_tinfo *) ARC_CALL0(get_tinfo);
}

ULONG __init
ArcGetRelativeTime(VOID)
{
	return ARC_CALL0(get_rtime);
}
