/*
 * arch/s390/kernel/relocate_kernel64.S
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Author(s): Rolf Adelsberger <adelsberger@de.ibm.com>
 *
 */

/*
 * moves the new kernel to its destination...
 * %r2 = pointer to first kimage_entry_t
 * %r3 = start address - where to jump to after the job is done...
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
	.globl		relocate_kernel
	relocate_kernel:
		basr	%r13,0		#base address
	.base:
		spx	zero64-.base(%r13)	#absolute addressing mode
		stnsm	sys_msk-.base(%r13),0xf8	#disable DAT and IRQ (external)
	.top:
		lghi	%r7,4096	#load PAGE_SIZE in r7
		lghi	%r9,4096	#load PAGE_SIZE in r9
		lg	%r5,0(%r2)	#read another word for indirection page
		aghi	%r2,8		#increment pointer
		tml	%r5,0x1		#is it a destination page?
		je	.indir_check	#NO, goto "indir_check"
		lgr	%r6,%r5		#r6 = r5
		nill	%r6,0xf000	#mask it out and...
		j	.top		#...next iteration
	.indir_check:
		tml     %r5,0x2		#is it a indirection page?
		je      .done_test	#NO, goto "done_test"
		nill    %r5,0xf000	#YES, mask out,
		lgr     %r2,%r5		#move it into the right register,
		j       .top		#and read next...
	.done_test:
		tml     %r5,0x4		#is it the done indicator?
		je      .source_test	#NO! Well, then it should be the source indicator...
		j       .done		#ok, lets finish it here...
	.source_test:
		tml     %r5,0x8		#it should be a source indicator...
		je      .top		#NO, ignore it...
		lgr     %r8,%r5		#r8 = r5
		nill    %r8,0xf000	#masking
	0:	mvcle   %r6,%r8,0x0	#copy PAGE_SIZE bytes from r8 to r6 - pad with 0
		jo	0b
		j       .top
	.done:
		sgr     %r0,%r0		#clear register r0
		la      %r4,load_psw-.base(%r13)	#load psw-address into the register
		o	%r3,4(%r4)	#or load address into psw
		st	%r3,4(%r4)
		mvc     0(8,%r0),0(%r4)	#copy psw to absolute address 0
		sam31			#31 bit mode
		sr      %r1,%r1		#erase register r1
		sr      %r2,%r2		#erase register r2
		sigp    %r1,%r2,0x12	#set cpuid to zero
		lpsw    0		#hopefully start new kernel...

	        .align	8
	zero64:
		.quad	0
	load_psw:
		.long	0x00080000,0x80000000
	sys_msk:
		.quad	0
	relocate_kernel_end:
	.globl	relocate_kernel_len
	relocate_kernel_len:
		.quad	relocate_kernel_end - relocate_kernel

