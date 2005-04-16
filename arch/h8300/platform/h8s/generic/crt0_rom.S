/*
 *  linux/arch/h8300/platform/h8s/generic/crt0_rom.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend startup 
 *  Target Archtecture:	generic
 *  Memory Layout     :	ROM
 */

#define ASSEMBLY

#include <linux/config.h>
#include <asm/linkage.h>
#include <asm/regs267x.h>
	
	.global SYMBOL_NAME(_start)
	.global SYMBOL_NAME(_command_line)
	.global SYMBOL_NAME(_platform_gpio_table)
	.global SYMBOL_NAME(_target_name)
	
	.h8300s
	.section .text
	.file	"crt0_rom.S"

	/* CPU Reset entry */
SYMBOL_NAME_LABEL(_start)
	mov.l	#__ramend,sp
	ldc	#0x80,ccr
	ldc	#0,exr
	bclr	#4,@INTCR:8
	bset	#5,@INTCR:8	/* Interrupt mode 2 */
	
	/* Peripheral Setup */
	
	/* copy .data */
#if !defined(CONFIG_H8S_SIM)
	mov.l	#__begin_data,er5
	mov.l	#__sdata,er6
	mov.l	#__edata,er4
	sub.l	er6,er4
	shlr.l	#2,er4
1:	
	mov.l	@er5+,er0
	mov.l	er0,@er6
	adds	#4,er6
	dec.l	#1,er4
	bne	1b	
#endif

	/* .bss clear */
	mov.l	#__sbss,er5
	mov.l	#__ebss,er4
	sub.l	er5,er4
	shlr.l	#2,er4		
	sub.l	er0,er0
1:
	mov.l	er0,@er5
	adds	#4,er5
	dec.l	#1,er4
	bne	1b

	/* linux kernel start */
	ldc	#0x90,ccr	/* running kernel */
	mov.l	#SYMBOL_NAME(init_thread_union),sp
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
	.byte	0x00,0x00
	;; P2DDR
	.byte	0x00,0x00
	;; P3DDR
	.byte	0x00,0x00
	;; P4DDR
	.byte	0x00,0x00
	;; P5DDR
	.byte	0x00,0x00
	;; P6DDR
	.byte	0x00,0x00
	;; dummy
	.byte	0x00,0x00
	;; P8DDR
	.byte	0x00,0x00
	;; PADDR
	.byte	0x00,0x00
	;; PBDDR
	.byte	0x00,0x00
	;; PCDDR
	.byte	0x00,0x00
	;; PDDDR
	.byte	0x00,0x00
	;; PEDDR
	.byte	0x00,0x00
	;; PFDDR
	.byte	0x00,0x00
	;; PGDDR
	.byte	0x00,0x00
	;; PHDDR
	.byte	0x00,0x00

	.section .rodata
__target_name:	
	.asciz	"generic"
	
	.section .bss
__command_line:	
	.space	512

	/* interrupt vector */
	.section .vectors,"ax"
	.long	__start
	.long	__start
vector	=	2
	.rept	126-1
	.long	_interrupt_redirect_table+vector*4
vector	=	vector + 1
	.endr
