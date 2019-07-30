/*
 * Startup glue code to uncompress the kernel
 *
 *   (C) 2017 Helge Deller <deller@gmx.de>
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/psw.h>
#include <asm/pdc.h>
#include <asm/assembly.h>
#include "sizes.h"

#define BOOTADDR(x)	(x)

#ifndef CONFIG_64BIT
	.import	$global$		/* forward declaration */
#endif /*!CONFIG_64BIT*/

	__HEAD

ENTRY(startup)
	 .level PA_ASM_LEVEL

#define PSW_W_SM	0x200
#define PSW_W_BIT       36

	;! nuke the W bit, saving original value
	.level 2.0
	rsm	PSW_W_SM, %r1

	.level 1.1
	extrw,u	%r1, PSW_W_BIT-32, 1, %r1
	copy	%r1, %arg0

	/* Make sure sr4-sr7 are set to zero for the kernel address space */
	mtsp    %r0,%sr4
	mtsp    %r0,%sr5
	mtsp    %r0,%sr6
	mtsp    %r0,%sr7

	/* Clear BSS */

	.import _bss,data
	.import _ebss,data

	load32	BOOTADDR(_bss),%r3
	load32	BOOTADDR(_ebss),%r4
	ldo	FRAME_SIZE(%r4),%sp	/* stack at end of bss */
$bss_loop:
	cmpb,<<,n %r3,%r4,$bss_loop
	stw,ma	%r0,4(%r3)

	/* Initialize the global data pointer */
	loadgp

	/* arg0..arg4 were set by palo. */
	copy	%arg1, %r6		/* command line */
	copy	%arg2, %r7		/* rd-start */
	copy	%arg3, %r8		/* rd-end */
	load32	BOOTADDR(decompress_kernel),%r3

#ifdef CONFIG_64BIT
	.level PA_ASM_LEVEL
	ssm	PSW_W_SM, %r0		/* set W-bit */
	depdi	0, 31, 32, %r3
#endif
	load32	BOOTADDR(startup_continue), %r2
	bv,n	0(%r3)

startup_continue:
#ifdef CONFIG_64BIT
	.level PA_ASM_LEVEL
	rsm	PSW_W_SM, %r0		/* clear W-bit */
#endif

	load32	KERNEL_BINARY_TEXT_START, %arg0 /* free mem */
	copy	%r6, %arg1		/* command line */
	copy	%r7, %arg2		/* rd-start */
	copy	%r8, %arg3		/* rd-end */

	bv,n	0(%ret0)
END(startup)
