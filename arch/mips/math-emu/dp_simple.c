/* IEEE754 floating point arithmetic
 * double precision: common utilities
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
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
 *  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "ieee754dp.h"

union ieee754dp ieee754dp_neg(union ieee754dp x)
{
	unsigned int oldrm;
	union ieee754dp y;

	oldrm = ieee754_csr.rm;
	ieee754_csr.rm = FPU_CSR_RD;
	y = ieee754dp_sub(ieee754dp_zero(0), x);
	ieee754_csr.rm = oldrm;
	return y;
}

union ieee754dp ieee754dp_abs(union ieee754dp x)
{
	unsigned int oldrm;
	union ieee754dp y;

	oldrm = ieee754_csr.rm;
	ieee754_csr.rm = FPU_CSR_RD;
	if (DPSIGN(x))
		y = ieee754dp_sub(ieee754dp_zero(0), x);
	else
		y = ieee754dp_add(ieee754dp_zero(0), x);
	ieee754_csr.rm = oldrm;
	return y;
}
