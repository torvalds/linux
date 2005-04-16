/*
 * arch/v850/kernel/mach.c -- Defaults for some things defined by "mach.h"
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include "mach.h"

/* Called with each timer tick, if non-zero.  */
void (*mach_tick)(void) = 0;
