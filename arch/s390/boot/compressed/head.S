/* SPDX-License-Identifier: GPL-2.0 */
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
	lg	%r15,.Lstack-.LPG1(%r13)
	aghi	%r15,-160
	brasl	%r14,decompress_kernel
	# Set up registers for memory mover. We move the decompressed image to
	# 0x11000, starting at offset 0x11000 in the decompressed image so
	# that code living at 0x11000 in the image will end up at 0x11000 in
	# memory.
	lgr	%r4,%r2
	lg	%r2,.Loffset-.LPG1(%r13)
	la	%r4,0(%r2,%r4)
	lg	%r3,.Lmvsize-.LPG1(%r13)
	lgr	%r5,%r3
	# Move the memory mover someplace safe so it doesn't overwrite itself.
	la	%r1,0x200
	mvc	0(mover_end-mover,%r1),mover-.LPG1(%r13)
	# When the memory mover is done we pass control to
	# arch/s390/kernel/head64.S:startup_continue which lives at 0x11000 in
	# the decompressed image.
	lgr	%r6,%r2
	br	%r1
mover:
	mvcle	%r2,%r4,0
	jo	mover
	br	%r6
mover_end:

	.align	8
.Lstack:
	.quad	0x8000 + (1<<(PAGE_SHIFT+THREAD_SIZE_ORDER))
.Loffset:
	.quad	0x11000
.Lmvsize:
	.quad	SZ__bss_start
