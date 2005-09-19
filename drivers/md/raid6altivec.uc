/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002-2004 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Bostom MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6altivec$#.c
 *
 * $#-way unrolled portable integer math RAID-6 instruction set
 *
 * This file is postprocessed using unroll.pl
 *
 * <benh> hpa: in process,
 * you can just "steal" the vec unit with enable_kernel_altivec() (but
 * bracked this with preempt_disable/enable or in a lock)
 */

#include "raid6.h"

#ifdef CONFIG_ALTIVEC

#include <altivec.h>
#ifdef __KERNEL__
# include <asm/system.h>
# include <asm/cputable.h>
#endif

/*
 * This is the C data type to use.  We use a vector of
 * signed char so vec_cmpgt() will generate the right
 * instruction.
 */

typedef vector signed char unative_t;

#define NBYTES(x) ((vector signed char) {x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x})
#define NSIZE	sizeof(unative_t)

/*
 * The SHLBYTE() operation shifts each byte left by 1, *not*
 * rolling over into the next byte
 */
static inline __attribute_const__ unative_t SHLBYTE(unative_t v)
{
	return vec_add(v,v);
}

/*
 * The MASK() operation returns 0xFF in any byte for which the high
 * bit is 1, 0x00 for any byte for which the high bit is 0.
 */
static inline __attribute_const__ unative_t MASK(unative_t v)
{
	unative_t zv = NBYTES(0);

	/* vec_cmpgt returns a vector bool char; thus the need for the cast */
	return (unative_t)vec_cmpgt(zv, v);
}


/* This is noinline to make damned sure that gcc doesn't move any of the
   Altivec code around the enable/disable code */
static void noinline
raid6_altivec$#_gen_syndrome_real(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;

	unative_t wd$$, wq$$, wp$$, w1$$, w2$$;
	unative_t x1d = NBYTES(0x1d);

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	for ( d = 0 ; d < bytes ; d += NSIZE*$# ) {
		wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE];
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			wp$$ = vec_xor(wp$$, wd$$);
			w2$$ = MASK(wq$$);
			w1$$ = SHLBYTE(wq$$);
			w2$$ = vec_and(w2$$, x1d);
			w1$$ = vec_xor(w1$$, w2$$);
			wq$$ = vec_xor(w1$$, wd$$);
		}
		*(unative_t *)&p[d+NSIZE*$$] = wp$$;
		*(unative_t *)&q[d+NSIZE*$$] = wq$$;
	}
}

static void raid6_altivec$#_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	preempt_disable();
	enable_kernel_altivec();

	raid6_altivec$#_gen_syndrome_real(disks, bytes, ptrs);

	preempt_enable();
}

int raid6_have_altivec(void);
#if $# == 1
int raid6_have_altivec(void)
{
	/* This assumes either all CPUs have Altivec or none does */
# ifdef __KERNEL__
	return cpu_has_feature(CPU_FTR_ALTIVEC);
# else
	return 1;
# endif
}
#endif

const struct raid6_calls raid6_altivec$# = {
	raid6_altivec$#_gen_syndrome,
	raid6_have_altivec,
	"altivecx$#",
	0
};

#endif /* CONFIG_ALTIVEC */
