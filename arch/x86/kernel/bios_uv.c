/*
 * BIOS run time interface routines.
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <asm/uv/bios.h>

const char *
x86_bios_strerror(long status)
{
	const char *str;
	switch (status) {
	case  0: str = "Call completed without error";	break;
	case -1: str = "Not implemented";		break;
	case -2: str = "Invalid argument";		break;
	case -3: str = "Call completed with error";	break;
	default: str = "Unknown BIOS status code";	break;
	}
	return str;
}

long
x86_bios_freq_base(unsigned long which, unsigned long *ticks_per_second,
		   unsigned long *drift_info)
{
	struct uv_bios_retval isrv;

	BIOS_CALL(isrv, BIOS_FREQ_BASE, which, 0, 0, 0, 0, 0, 0);
	*ticks_per_second = isrv.v0;
	*drift_info = isrv.v1;
	return isrv.status;
}
EXPORT_SYMBOL_GPL(x86_bios_freq_base);
