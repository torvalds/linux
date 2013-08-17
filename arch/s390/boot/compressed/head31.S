/*
 * Startup glue code to uncompress the kernel
 *
 * Copyright IBM Corp. 2010
 *
 *   Author(s):	Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include "sizes.h"

__HEAD
ENTRY(startup_continue)
	basr	%r13,0			# get base
.LPG1:
	# setup stack
	l	%r15,.Lstack-.LPG1(%r13)
	ahi	%r15,-96
	l	%r1,.Ldecompress-.LPG1(%r13)
	basr	%r14,%r1
	# setup registers for memory mover & branch to target
	lr	%r4,%r2
	l	%r2,.Loffset-.LPG1(%r13)
	la	%r4,0(%r2,%r4)
	l	%r3,.Lmvsize-.LPG1(%r13)
	lr	%r5,%r3
	# move the memory mover someplace safe
	la	%r1,0x200
	mvc	0(mover_end-mover,%r1),mover-.LPG1(%r13)
	# decompress image is started at 0x11000
	lr	%r6,%r2
	br	%r1
mover:
	mvcle	%r2,%r4,0
	jo	mover
	br	%r6
mover_end:

	.align	8
.Lstack:
	.long	0x8000 + (1<<(PAGE_SHIFT+THREAD_ORDER))
.Ldecompress:
	.long	decompress_kernel
.Loffset:
	.long	0x11000
.Lmvsize:
	.long	SZ__bss_start
