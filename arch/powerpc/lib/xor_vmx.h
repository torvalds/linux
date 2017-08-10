/*
 * Simple interface to link xor_vmx.c and xor_vmx_glue.c
 *
 * Separating these file ensures that no altivec instructions are run
 * outside of the enable/disable altivec block.
 */

void __xor_altivec_2(unsigned long bytes, unsigned long *v1_in,
			     unsigned long *v2_in);

void __xor_altivec_3(unsigned long bytes, unsigned long *v1_in,
			     unsigned long *v2_in, unsigned long *v3_in);

void __xor_altivec_4(unsigned long bytes, unsigned long *v1_in,
			     unsigned long *v2_in, unsigned long *v3_in,
			     unsigned long *v4_in);

void __xor_altivec_5(unsigned long bytes, unsigned long *v1_in,
			     unsigned long *v2_in, unsigned long *v3_in,
			     unsigned long *v4_in, unsigned long *v5_in);
