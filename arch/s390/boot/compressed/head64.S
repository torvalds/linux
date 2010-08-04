/*
 * Startup glue code to uncompress the kernel
 *
 * Copyright IBM Corp. 2010
 *
 *   Author(s):	Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/init.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include "sizes.h"

__HEAD
	.globl	startup_continue
startup_continue:
	basr	%r13,0			# get base
.LPG1:
	# setup stack
	lg	%r15,.Lstack-.LPG1(%r13)
	aghi	%r15,-160
	brasl	%r14,decompress_kernel
	# setup registers for memory mover & branch to target
	lgr	%r4,%r2
	lg	%r2,.Loffset-.LPG1(%r13)
	la	%r4,0(%r2,%r4)
	lg	%r3,.Lmvsize-.LPG1(%r13)
	lgr	%r5,%r3
	# move the memory mover someplace safe
	la	%r1,0x200
	mvc	0(mover_end-mover,%r1),mover-.LPG1(%r13)
	# decompress image is started at 0x11000
	lgr	%r6,%r2
	br	%r1
mover:
	mvcle	%r2,%r4,0
	jo	mover
	br	%r6
mover_end:

	.align	8
.Lstack:
	.quad	0x8000 + (1<<(PAGE_SHIFT+THREAD_ORDER))
.Loffset:
	.quad	0x11000
.Lmvsize:
	.quad	SZ__bss_start
