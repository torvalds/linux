/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * Floating-point emulation code
 *  Copyright (C) 2001 Hewlett-Packard (Paul Bame) <bame@debian.org>
 */

#ifdef __NO_PA_HDRS
    PA header file -- do not include this header file for non-PA builds.
#endif


/*
 *  These macros are designed to be portable to all machines that have
 *  a wordsize greater than or equal to 32 bits that support the portable
 *  C compiler and the standard C preprocessor.  Wordsize (default 32)
 *  and bitfield assignment (default left-to-right,  unlike VAX, PDP-11)
 *  should be predefined using the constants HOSTWDSZ and BITFRL and
 *  the C compiler "-D" flag (e.g., -DHOSTWDSZ=36 -DBITFLR for the DEC-20).
 *  Note that the macro arguments assume that the integer being referenced
 *  is a 32-bit integer (right-justified on the 20) and that bit 0 is the
 *  most significant bit.
 */

#ifndef HOSTWDSZ
#define	HOSTWDSZ	32
#endif


/*###########################  Macros  ######################################*/

/*-------------------------------------------------------------------------
 * NewDeclareBitField_Reference - Declare a structure similar to the simulator
 * function "DeclBitfR" except its use is restricted to occur within a larger
 * enclosing structure or union definition.  This declaration is an unnamed
 * structure with the argument, name, as the member name and the argument,
 * uname, as the element name. 
 *----------------------------------------------------------------------- */
#define Bitfield_extract(start, length, object) 	\
    ((object) >> (HOSTWDSZ - (start) - (length)) & 	\
    ((unsigned)-1 >> (HOSTWDSZ - (length))))

#define Bitfield_signed_extract(start, length, object) \
    ((int)((object) << start) >> (HOSTWDSZ - (length)))

#define Bitfield_mask(start, len, object)		\
    ((object) & (((unsigned)-1 >> (HOSTWDSZ-len)) << (HOSTWDSZ-start-len)))

#define Bitfield_deposit(value,start,len,object)  object = \
    ((object) & ~(((unsigned)-1 >> (HOSTWDSZ-len)) << (HOSTWDSZ-start-len))) | \
    (((value) & ((unsigned)-1 >> (HOSTWDSZ-len))) << (HOSTWDSZ-start-len))
