/*
 *  linux/arch/h8300/platform/h8300h/aki3068net/crt0_ram.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend startup
 *  Target Archtecture:	AE-3068 (aka. aki3068net)
 *  Memory Layout     :	RAM
 */

#define ASSEMBLY

#include <asm/linkage.h>
	
#if !defined(CONFIG_BLKDEV_RESERVE)
#if defined(CONFIG_GDB_DEBUG)
#define RAMEND (__ramend - 0xc000)
#else
#define RAMEND __ramend
#endif
#else
#define RAMEND CONFIG_BLKDEV_RESERVE_ADDRESS
#endif
	
	.global __start
	.global _command_line
	.global __platform_gpio_table
	.global __target_name
	
	.h8300h

	.section .text
	.file	"crt0_ram.S"

	/* CPU Reset entry */
__start:
	mov.l	#RAMEND,sp
	ldc	#0x80,ccr

	/* Peripheral Setup */
	
#if defined(CONFIG_MTD_UCLINUX)
	/* move romfs image */
	jsr	@__move_romfs	
#endif
	
	/* .bss clear */
	mov.l	#__sbss,er5
	mov.l	#__ebss,er4
	sub.l	er5,er4
	shlr	er4
	shlr	er4
	sub.l	er0,er0
1:	
	mov.l	er0,@er5
	adds	#4,er5
	dec.l	#1,er4
	bne	1b

	/* copy kernel commandline */
	mov.l	#COMMAND_START,er5
	mov.l	#_command_line,er6
	mov.w	#512,r4
	eepmov.w

	/* uClinux kernel start */
	ldc	#0x90,ccr	/* running kernel */
	mov.l	#_init_thread_union,sp
	add.l	#0x2000,sp
	jsr	@_start_kernel
_exit:

	jmp	_exit

	rts

	/* I/O port assign information */
__platform_gpio_table:	
	mov.l	#gpio_table,er0
	rts

gpio_table:
	;; P1DDR
	.byte	0xff,0xff
	;; P2DDR
	.byte	0xff,0xff
	;; P3DDR
	.byte	0xff,0x00
	;; P4DDR
	.byte	0x00,0x00
	;; P5DDR
	.byte	0x01,0x01
	;; P6DDR
	.byte	0x00,0x00
	;; dummy
	.byte	0x00,0x00
	;; P8DDR
	.byte	0x0c,0x0c
	;; P9DDR
	.byte	0x00,0x00
	;; PADDR
	.byte	0x00,0x00
	;; PBDDR
	.byte	0x30,0x30

__target_name:	
	.asciz	"AE-3068"
	
	.section .bootvec,"ax"
	jmp	@__start
