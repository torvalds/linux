/* SPDX-License-Identifier: GPL-2.0 */
/*	
 * unsigned long __xdiv64_32(unsigned long long n, unsigned long d); 
 */

#include <linux/linkage.h>

.text
ENTRY(__xdiv64_32)
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	mov	r4, r0
	mov	r5, r1
#else
	mov	r4, r1
	mov	r5, r0
#endif
	cmp/hs	r6, r1
	bf.s	1f
	 mov	#0, r2

	mov	r1, r2
	mov	#0, r3
	div0u
	.rept	32
	rotcl	r2
	div1	r6, r3
	.endr
	rotcl	r2
	mul.l	r6, r2
	sts	macl, r3
	sub	r3, r1
1:
	div0u
	.rept	32
	rotcl	r0
	div1	r6, r1
	.endr
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	mov	r2, r1
	rts
	 rotcl	r0
#else
	rotcl	r0
	mov	r0, r1
	rts
	 mov	r2, r0
#endif
