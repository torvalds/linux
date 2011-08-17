/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * env.c: ARCS environment variable routines.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/fw/arc/types.h>
#include <asm/sgialib.h>

PCHAR __init
ArcGetEnvironmentVariable(CHAR *name)
{
	return (CHAR *) ARC_CALL1(get_evar, name);
}

LONG __init
ArcSetEnvironmentVariable(PCHAR name, PCHAR value)
{
	return ARC_CALL2(set_evar, name, value);
}
