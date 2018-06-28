/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_XOR_ALTIVEC_H
#define _ASM_POWERPC_XOR_ALTIVEC_H

#ifdef CONFIG_ALTIVEC

void xor_altivec_2(unsigned long bytes, unsigned long *v1_in,
		   unsigned long *v2_in);
void xor_altivec_3(unsigned long bytes, unsigned long *v1_in,
		   unsigned long *v2_in, unsigned long *v3_in);
void xor_altivec_4(unsigned long bytes, unsigned long *v1_in,
		   unsigned long *v2_in, unsigned long *v3_in,
		   unsigned long *v4_in);
void xor_altivec_5(unsigned long bytes, unsigned long *v1_in,
		   unsigned long *v2_in, unsigned long *v3_in,
		   unsigned long *v4_in, unsigned long *v5_in);

#endif
#endif /* _ASM_POWERPC_XOR_ALTIVEC_H */
