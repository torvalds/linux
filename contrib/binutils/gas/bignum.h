/* bignum.h-arbitrary precision integers
   Copyright 1987, 1992, 2003, 2005 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/***********************************************************************\
 *									*
 *	Arbitrary-precision integer arithmetic.				*
 *	For speed, we work in groups of bits, even though this		*
 *	complicates algorithms.						*
 *	Each group of bits is called a 'littlenum'.			*
 *	A bunch of littlenums representing a (possibly large)		*
 *	integer is called a 'bignum'.					*
 *	Bignums are >= 0.						*
 *									*
 \***********************************************************************/

#define	LITTLENUM_NUMBER_OF_BITS	(16)
#define	LITTLENUM_RADIX			(1 << LITTLENUM_NUMBER_OF_BITS)
#define	LITTLENUM_MASK			(0xFFFF)
#define LITTLENUM_SHIFT			(1)
#define CHARS_PER_LITTLENUM		(1 << LITTLENUM_SHIFT)
#ifndef BITS_PER_CHAR
#define BITS_PER_CHAR			(8)
#endif

typedef unsigned short LITTLENUM_TYPE;
