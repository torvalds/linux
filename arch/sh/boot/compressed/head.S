/*
 *  linux/arch/sh/boot/compressed/head.S
 *
 *  Copyright (C) 1999 Stuart Menefy
 *  Copyright (C) 2003 SUGIOKA Toshinobu
 */

.text

#include <linux/config.h>
#include <linux/linkage.h>

	.global	startup
startup:
	/* Load initial status register */
	mov.l   init_sr, r1
	ldc     r1, sr

	/* Move myself to proper location if necessary */
	mova	1f, r0
	mov.l	1f, r2
	cmp/eq	r2, r0
	bt	clear_bss
	sub	r0, r2
	mov.l	bss_start_addr, r0
	mov	#0xe0, r1
	and	r1, r0			! align cache line
	mov.l	text_start_addr, r3
	mov	r0, r1
	sub	r2, r1
3:
	mov.l	@r1, r4
	mov.l	@(4,r1), r5
	mov.l	@(8,r1), r6
	mov.l	@(12,r1), r7
	mov.l	@(16,r1), r8
	mov.l	@(20,r1), r9
	mov.l	@(24,r1), r10
	mov.l	@(28,r1), r11
	mov.l	r4, @r0
	mov.l	r5, @(4,r0)
	mov.l	r6, @(8,r0)
	mov.l	r7, @(12,r0)
	mov.l	r8, @(16,r0)
	mov.l	r9, @(20,r0)
	mov.l	r10, @(24,r0)
	mov.l	r11, @(28,r0)
#ifdef CONFIG_CPU_SH4
	ocbwb	@r0
#endif
	cmp/hi	r3, r0
	add	#-32, r0
	bt/s	3b
	 add	#-32, r1
	mov.l	2f, r0
	jmp	@r0
	 nop

	.align 2
1:	.long	1b
2:	.long	clear_bss
text_start_addr:
	.long	startup

	/* Clear BSS */
clear_bss:
	mov.l	end_addr, r1
	mov.l	bss_start_addr, r2
	mov	#0, r0
l1:
	mov.l	r0, @-r1
	cmp/eq	r1,r2
	bf	l1

	/* Set the initial pointer. */
	mov.l	init_stack_addr, r0
	mov.l	@r0, r15

	/* Decompress the kernel */
	mov.l	decompress_kernel_addr, r0
	jsr	@r0
	nop

	/* Jump to the start of the decompressed kernel */
	mov.l	kernel_start_addr, r0
	jmp	@r0
	nop
	
	.align	2
bss_start_addr:
	.long	__bss_start
end_addr:
	.long	_end
init_sr:
	.long	0x400000F0	/* Privileged mode, Bank=0, Block=0, IMASK=0xF */
init_stack_addr:
	.long	stack_start
decompress_kernel_addr:
	.long	decompress_kernel
kernel_start_addr:
	.long	_text+0x1000

	.align	9
fake_headers_as_bzImage:
	.word	0
	.ascii	"HdrS"		! header signature
	.word	0x0202		! header version number (>= 0x0105)
				! or else old loadlin-1.5 will fail)
	.word	0		! default_switch
	.word	0		! SETUPSEG
	.word	0x1000
	.word	0		! pointing to kernel version string
	.byte	0		! = 0, old one (LILO, Loadlin,
				! 0xTV: T=0 for LILO
				!       V = version
	.byte	1		! Load flags bzImage=1
	.word	0x8000		! size to move, when setup is not
	.long	0x100000	! 0x100000 = default for big kernel
	.long	0		! address of loaded ramdisk image
	.long	0		# its size in bytes
