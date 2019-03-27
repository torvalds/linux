/* gnu_stab.h  Definitions for GNU extensions to STABS

   Copyright 2001 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */
#ifndef __GNU_STAB__

/* Indicate the GNU stab.h is in use.  */

#define __GNU_STAB__

#define __define_stab(NAME, CODE, STRING) NAME=CODE,
#define __define_stab_duplicate(NAME, CODE, STRING) NAME=CODE,

enum __stab_debug_code
{
#include "aout/stab.def"
LAST_UNUSED_STAB_CODE
};

#undef __define_stab

/* Definitions of "desc" field for N_SO stabs in Solaris2.  */

#define	N_SO_AS		1
#define	N_SO_C		2
#define	N_SO_ANSI_C	3
#define	N_SO_CC		4	/* C++ */
#define	N_SO_FORTRAN	5
#define	N_SO_PASCAL	6

/* Solaris2: Floating point type values in basic types.  */

#define	NF_NONE		0
#define	NF_SINGLE	1	/* IEEE 32-bit */
#define	NF_DOUBLE	2	/* IEEE 64-bit */
#define	NF_COMPLEX	3	/* Fortran complex */
#define	NF_COMPLEX16	4	/* Fortran double complex */
#define	NF_COMPLEX32	5	/* Fortran complex*16 */
#define	NF_LDOUBLE	6	/* Long double (whatever that is) */

#endif /* __GNU_STAB_ */
