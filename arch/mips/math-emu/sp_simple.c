/* IEEE754 floating point arithmetic
 * single precision
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

#include "ieee754sp.h"

union ieee754sp ieee754sp_neg(union ieee754sp x)
{
	unsigned int oldrm;
	union ieee754sp y;

	oldrm = ieee754_csr.rm;
	ieee754_csr.rm = FPU_CSR_RD;
	y = ieee754sp_sub(ieee754sp_zero(0), x);
	ieee754_csr.rm = oldrm;
	return y;
}

union ieee754sp ieee754sp_abs(union ieee754sp x)
{
	unsigned int oldrm;
	union ieee754sp y;

	oldrm = ieee754_csr.rm;
	ieee754_csr.rm = FPU_CSR_RD;
	if (SPSIGN(x))
		y = ieee754sp_sub(ieee754sp_zero(0), x);
	else
		y = ieee754sp_add(ieee754sp_zero(0), x);
	ieee754_csr.rm = oldrm;
	return y;
}
