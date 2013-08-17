/*
 * floor, trunc, ceil
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */


#include "ieee754.h"

ieee754dp ieee754dp_floor(ieee754dp x)
{
	ieee754dp i;

	if (ieee754dp_lt(ieee754dp_modf(x, &i), ieee754dp_zero(0)))
		return ieee754dp_sub(i, ieee754dp_one(0));
	else
		return i;
}

ieee754dp ieee754dp_ceil(ieee754dp x)
{
	ieee754dp i;

	if (ieee754dp_gt(ieee754dp_modf(x, &i), ieee754dp_zero(0)))
		return ieee754dp_add(i, ieee754dp_one(0));
	else
		return i;
}

ieee754dp ieee754dp_trunc(ieee754dp x)
{
	ieee754dp i;

	(void) ieee754dp_modf(x, &i);
	return i;
}
