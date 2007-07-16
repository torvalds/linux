/*
 *  linux/arch/h8300/boot/compressed/head.S
 *
 *  Copyright (C) 2006 Yoshinori Sato
 */

	.h8300h
#include <linux/linkage.h>

#define SRAM_START 0xff4000

	.section	.text.startup
	.global	startup
startup:
	mov.l	#SRAM_START+0x8000, sp
	mov.l	#__sbss, er0
	mov.l	#__ebss, er1
	sub.l	er0, er1
	shlr	er1
	shlr	er1
	sub.l	er2, er2
1:
	mov.l	er2, @er0
	adds	#4, er0
	dec.l	#1, er1
	bne	1b
	jsr	@_decompress_kernel
	jmp	@0x400000

	.align	9
fake_headers_as_bzImage:
	.word	0
	.ascii	"HdrS"		; header signature
	.word	0x0202		; header version number (>= 0x0105)
				; or else old loadlin-1.5 will fail)
	.word	0		; default_switch
	.word	0		; SETUPSEG
	.word	0x1000
	.word	0		; pointing to kernel version string
	.byte	0		; = 0, old one (LILO, Loadlin,
				; 0xTV: T=0 for LILO
				;       V = version
	.byte	1		; Load flags bzImage=1
	.word	0x8000		; size to move, when setup is not
	.long	0x100000	; 0x100000 = default for big kernel
	.long	0		; address of loaded ramdisk image
	.long	0		; its size in bytes
