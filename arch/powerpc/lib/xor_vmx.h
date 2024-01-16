/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Simple interface to link xor_vmx.c and xor_vmx_glue.c
 *
 * Separating these file ensures that no altivec instructions are run
 * outside of the enable/disable altivec block.
 */

void __xor_altivec_2(unsigned long bytes, unsigned long * __restrict p1,
		     const unsigned long * __restrict p2);
void __xor_altivec_3(unsigned long bytes, unsigned long * __restrict p1,
		     const unsigned long * __restrict p2,
		     const unsigned long * __restrict p3);
void __xor_altivec_4(unsigned long bytes, unsigned long * __restrict p1,
		     const unsigned long * __restrict p2,
		     const unsigned long * __restrict p3,
		     const unsigned long * __restrict p4);
void __xor_altivec_5(unsigned long bytes, unsigned long * __restrict p1,
		     const unsigned long * __restrict p2,
		     const unsigned long * __restrict p3,
		     const unsigned long * __restrict p4,
		     const unsigned long * __restrict p5);
