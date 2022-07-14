/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2005
 *
 * Author(s): Rolf Adelsberger
 *
 */

#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/sigp.h>

/*
 * moves the new kernel to its destination...
 * %r2 = pointer to first kimage_entry_t
 * %r3 = start address - where to jump to after the job is done...
 * %r4 = subcode
 *
 * %r5 will be used as temp. storage
 * %r6 holds the destination address
 * %r7 = PAGE_SIZE
 * %r8 holds the source address
 * %r9 = PAGE_SIZE
 *
 * 0xf000 is a page_mask
 */

	.text
ENTRY(relocate_kernel)
		basr	%r13,0		# base address
	.base:
		lghi	%r7,PAGE_SIZE	# load PAGE_SIZE in r7
		lghi	%r9,PAGE_SIZE	# load PAGE_SIZE in r9
		lg	%r5,0(%r2)	# read another word for indirection page
		aghi	%r2,8		# increment pointer
		tml	%r5,0x1		# is it a destination page?
		je	.indir_check	# NO, goto "indir_check"
		lgr	%r6,%r5		# r6 = r5
		nill	%r6,0xf000	# mask it out and...
		j	.base		# ...next iteration
	.indir_check:
		tml	%r5,0x2		# is it a indirection page?
		je	.done_test	# NO, goto "done_test"
		nill	%r5,0xf000	# YES, mask out,
		lgr	%r2,%r5		# move it into the right register,
		j	.base		# and read next...
	.done_test:
		tml	%r5,0x4		# is it the done indicator?
		je	.source_test	# NO! Well, then it should be the source indicator...
		j	.done		# ok, lets finish it here...
	.source_test:
		tml	%r5,0x8		# it should be a source indicator...
		je	.base		# NO, ignore it...
		lgr	%r8,%r5		# r8 = r5
		nill	%r8,0xf000	# masking
	0:	mvcle	%r6,%r8,0x0	# copy PAGE_SIZE bytes from r8 to r6 - pad with 0
		jo	0b
		j	.base
	.done:
		lgr	%r0,%r4		# subcode
		cghi	%r3,0
		je	.diag
		la	%r4,load_psw-.base(%r13)	# load psw-address into the register
		o	%r3,4(%r4)	# or load address into psw
		st	%r3,4(%r4)
		mvc	0(8,%r0),0(%r4)	# copy psw to absolute address 0
	.diag:
		diag	%r0,%r0,0x308
ENDPROC(relocate_kernel)

		.align	8
	load_psw:
		.long	0x00080000,0x80000000
	relocate_kernel_end:
	.align 8
	.globl	relocate_kernel_len
	relocate_kernel_len:
		.quad	relocate_kernel_end - relocate_kernel
