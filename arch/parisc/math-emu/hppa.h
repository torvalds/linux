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


/* amount is assumed to be a constant between 0 and 32 (non-inclusive) */
#define Shiftdouble(left,right,amount,dest)			\
    /* int left, right, amount, dest; */			\
    dest = ((left) << (32-(amount))) | ((unsigned int)(right) >> (amount))

/* amount must be less than 32 */
#define Variableshiftdouble(left,right,amount,dest)		\
    /* unsigned int left, right;  int amount, dest; */		\
    if (amount == 0) dest = right;				\
    else dest = ((((unsigned) left)&0x7fffffff) << (32-(amount))) |	\
          ((unsigned) right >> (amount))

/* amount must be between 0 and 32 (non-inclusive) */
#define Variable_shift_double(left,right,amount,dest)		\
    /* unsigned int left, right;  int amount, dest; */		\
    dest = (left << (32-(amount))) | ((unsigned) right >> (amount))
