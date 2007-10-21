/*
 *  linux/boot/head.S
 *
 *  Copyright (C) 1991, 1992, 1993  Linus Torvalds
 */

/*
 *  head.S contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00001000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory. [According to comments etc elsewhere on a compressed
 * kernel it will end up at 0x1000 + 1Mb I hope so as I assume this. - AC]
 *
 * Page 0 is deliberately kept safe, since System Management Mode code in 
 * laptops may need to access the BIOS data stored there.  This is also
 * useful for future device drivers that either access the BIOS via VM86 
 * mode.
 */

/*
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 */
.text

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/boot.h>
#include <asm/asm-offsets.h>

.section ".text.head","ax",@progbits
	.globl startup_32

startup_32:
	/* check to see if KEEP_SEGMENTS flag is meaningful */
	cmpw $0x207, BP_version(%esi)
	jb 1f

	/* test KEEP_SEGMENTS flag to see if the bootloader is asking
	 * us to not reload segments */
	testb $(1<<6), BP_loadflags(%esi)
	jnz 2f

1:	cli
	movl $(__BOOT_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs
	movl %eax,%ss

2:	cld

/* Calculate the delta between where we were compiled to run
 * at and where we were actually loaded at.  This can only be done
 * with a short local call on x86.  Nothing  else will tell us what
 * address we are running at.  The reserved chunk of the real-mode
 * data at 0x1e4 (defined as a scratch field) are used as the stack
 * for this calculation. Only 4 bytes are needed.
 */
	leal (0x1e4+4)(%esi), %esp
	call 1f
1:	popl %ebp
	subl $1b, %ebp

/* %ebp contains the address we are loaded at by the boot loader and %ebx
 * contains the address where we should move the kernel image temporarily
 * for safe in-place decompression.
 */

#ifdef CONFIG_RELOCATABLE
	movl 	%ebp, %ebx
	addl    $(CONFIG_PHYSICAL_ALIGN - 1), %ebx
	andl    $(~(CONFIG_PHYSICAL_ALIGN - 1)), %ebx
#else
	movl $LOAD_PHYSICAL_ADDR, %ebx
#endif

	/* Replace the compressed data size with the uncompressed size */
	subl input_len(%ebp), %ebx
	movl output_len(%ebp), %eax
	addl %eax, %ebx
	/* Add 8 bytes for every 32K input block */
	shrl $12, %eax
	addl %eax, %ebx
	/* Add 32K + 18 bytes of extra slack */
	addl $(32768 + 18), %ebx
	/* Align on a 4K boundary */
	addl $4095, %ebx
	andl $~4095, %ebx

/* Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	pushl %esi
	leal _end(%ebp), %esi
	leal _end(%ebx), %edi
	movl $(_end - startup_32), %ecx
	std
	rep
	movsb
	cld
	popl %esi

/* Compute the kernel start address.
 */
#ifdef CONFIG_RELOCATABLE
	addl    $(CONFIG_PHYSICAL_ALIGN - 1), %ebp
	andl    $(~(CONFIG_PHYSICAL_ALIGN - 1)), %ebp
#else
	movl	$LOAD_PHYSICAL_ADDR, %ebp
#endif

/*
 * Jump to the relocated address.
 */
	leal relocated(%ebx), %eax
	jmp *%eax
.section ".text"
relocated:

/*
 * Clear BSS
 */
	xorl %eax,%eax
	leal _edata(%ebx),%edi
	leal _end(%ebx), %ecx
	subl %edi,%ecx
	cld
	rep
	stosb

/*
 * Setup the stack for the decompressor
 */
	leal stack_end(%ebx), %esp

/*
 * Do the decompression, and jump to the new kernel..
 */
	movl output_len(%ebx), %eax
	pushl %eax
	pushl %ebp	# output address
	movl input_len(%ebx), %eax
	pushl %eax	# input_len
	leal input_data(%ebx), %eax
	pushl %eax	# input_data
	leal _end(%ebx), %eax
	pushl %eax	# end of the image as third argument
	pushl %esi	# real mode pointer as second arg
	call decompress_kernel
	addl $20, %esp
	popl %ecx

#if CONFIG_RELOCATABLE
/* Find the address of the relocations.
 */
	movl %ebp, %edi
	addl %ecx, %edi

/* Calculate the delta between where vmlinux was compiled to run
 * and where it was actually loaded.
 */
	movl %ebp, %ebx
	subl $LOAD_PHYSICAL_ADDR, %ebx
	jz   2f		/* Nothing to be done if loaded at compiled addr. */
/*
 * Process relocations.
 */

1:	subl $4, %edi
	movl 0(%edi), %ecx
	testl %ecx, %ecx
	jz 2f
	addl %ebx, -__PAGE_OFFSET(%ebx, %ecx)
	jmp 1b
2:
#endif

/*
 * Jump to the decompressed kernel.
 */
	xorl %ebx,%ebx
	jmp *%ebp

.bss
.balign 4
stack:
	.fill 4096, 1, 0
stack_end:
