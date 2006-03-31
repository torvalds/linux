/*
 * Find memory based upon settings in the CPC700 bridge
 *
 * Author: Dan Cox
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <asm/types.h>
#include <asm/io.h>
#include "cpc700.h"

unsigned long
cpc700_get_mem_size(void)
{
	int i;
	unsigned long len, amt;

	/* Start at MB1EA, since MB0EA will most likely be the ending address
	   for ROM space. */
	for(len = 0, i = CPC700_MB1EA; i <= CPC700_MB4EA; i+=4) {
		amt = cpc700_read_memreg(i);
		if (amt == 0)
			break;
		len = amt;
	}

	return len;
}


