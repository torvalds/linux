/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_M68K_LIBGCC_H
#define __ASM_M68K_LIBGCC_H

#ifndef CONFIG_CPU_HAS_NO_MULDIV64
/*
 * For those 68K CPUs that support 64bit multiply define umul_ppm()
 * for the common muldi3 libgcc helper function (in lib/muldi3.c).
 * CPUs that don't have it (like the original 68000 and ColdFire)
 * will fallback to using the C-coded version of umul_ppmm().
 */
#define umul_ppmm(w1, w0, u, v)				\
	do {						\
		unsigned long __u = (u), __v = (v);	\
		unsigned long __w0, __w1;		\
							\
		__asm__ ("mulu%.l %3,%1:%0"		\
			 : "=d" (__w0),			\
			   "=d" (__w1)			\
			 : "%0" (__u),			\
			   "dmi" (__v));		\
							\
		(w0) = __w0; (w1) = __w1;		\
	} while (0)
#endif /* !CONFIG_CPU_HAS_NO_MULDIV64 */

#endif /* __ASM_M68K_LIBGCC_H */
