/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_XOR_ALTIVEC_H
#define _ASM_POWERPC_XOR_ALTIVEC_H

#ifdef CONFIG_ALTIVEC
void xor_altivec_2(unsigned long bytes, unsigned long * __restrict p1,
		   const unsigned long * __restrict p2);
void xor_altivec_3(unsigned long bytes, unsigned long * __restrict p1,
		   const unsigned long * __restrict p2,
		   const unsigned long * __restrict p3);
void xor_altivec_4(unsigned long bytes, unsigned long * __restrict p1,
		   const unsigned long * __restrict p2,
		   const unsigned long * __restrict p3,
		   const unsigned long * __restrict p4);
void xor_altivec_5(unsigned long bytes, unsigned long * __restrict p1,
		   const unsigned long * __restrict p2,
		   const unsigned long * __restrict p3,
		   const unsigned long * __restrict p4,
		   const unsigned long * __restrict p5);

#endif
#endif /* _ASM_POWERPC_XOR_ALTIVEC_H */
