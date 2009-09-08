/*
 * Copyright (C) 2003  Richard Curnow, SuperH UK Limited
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

void
panic_handler(unsigned long panicPC, unsigned long panicSSR,
	      unsigned long panicEXPEVT)
{
	/* Never return from the panic handler */
	for (;;) ;
}
