/* Bootloader to resume MIO A701
 *
 * 2007-1-12 Robert Jarzmik
 *
 * This code is licenced under the GPLv2.
*/

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Note: Yes, part of the following code is located into the .data section.
 *       This is to allow jumpaddr to be accessed with a relative load
 *       while we can't rely on any MMU translation.  We could have put
 *       sleep_save_sp in the .text section as well, but some setups might
 *       insist on it to be truly read-only.
 */
	.data
ENTRY(mioa701_bootstrap)
0:
	b	1f
ENTRY(mioa701_jumpaddr)
	.word	0x40f00008		@ PSPR in no-MMU mode
1:
	mov	r0,     #0xa0000000	@ Don't suppose memory access works
	orr	r0, r0, #0x00200000	@ even if it's supposed to
	mov	r1, #0
	str	r1, [r0]		@ Early disable resume for next boot
	ldr	r0, mioa701_jumpaddr	@ (Murphy's Law)
	ldr	r0, [r0]
	mov	pc, r0
2:

ENTRY(mioa701_bootstrap_lg)
	.data
	.word	2b-0b
