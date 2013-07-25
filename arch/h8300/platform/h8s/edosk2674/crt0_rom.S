/*
 *  linux/arch/h8300/platform/h8s/edosk2674/crt0_rom.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend startup
 *  Target Archtecture:	EDOSK-2674
 *  Memory Layout     :	ROM
 */

#define ASSEMBLY

#include <asm/linkage.h>
#include <asm/regs267x.h>
		
	.global __start
	.global __command_line
	.global __platform_gpio_table
	.global __target_name
	
	.h8300s
	.section .text
	.file	"crt0_rom.S"

	/* CPU Reset entry */
__start:
	mov.l	#__ramend,sp
	ldc	#0x80,ccr
	ldc	#0,exr
	
	/* Peripheral Setup */
;BSC/GPIO setup
	mov.l	#init_regs,er0
	mov.w	#0xffff,e2
1:
	mov.w	@er0+,r2
	beq	2f
	mov.w	@er0+,r1
	mov.b	r1l,@er2
	bra	1b

2:
;SDRAM setup
#define SDRAM_SMR 0x400040

	mov.b	#0,r0l
	mov.b	r0l,@DRACCR:16
	mov.w	#0x188,r0
	mov.w	r0,@REFCR:16
	mov.w	#0x85b4,r0
	mov.w	r0,@DRAMCR:16
	mov.b	#0,r1l
	mov.b	r1l,@SDRAM_SMR
	mov.w	#0x84b4,r0
	mov.w	r0,@DRAMCR:16
;special thanks to Arizona Cooperative Power
	
	/* copy .data */
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

	/* copy kernel commandline */
	mov.l	#COMMAND_START,er5
	mov.l	#__command_line,er6
	mov.w	#512,r4
	eepmov.w

	/* linux kernel start */
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

#define INIT_REGS_DATA(REGS,DATA) \
	.word	((REGS) & 0xffff),DATA

init_regs:
INIT_REGS_DATA(ASTCR,0xff)
INIT_REGS_DATA(RDNCR,0x00)
INIT_REGS_DATA(ABWCR,0x80)
INIT_REGS_DATA(WTCRAH,0x27)
INIT_REGS_DATA(WTCRAL,0x77)
INIT_REGS_DATA(WTCRBH,0x71)
INIT_REGS_DATA(WTCRBL,0x22)
INIT_REGS_DATA(CSACRH,0x80)
INIT_REGS_DATA(CSACRL,0x80)
INIT_REGS_DATA(BROMCRH,0xa0)
INIT_REGS_DATA(BROMCRL,0xa0)
INIT_REGS_DATA(P3DDR,0x3a)
INIT_REGS_DATA(P3ODR,0x06)
INIT_REGS_DATA(PADDR,0xff)
INIT_REGS_DATA(PFDDR,0xfe)
INIT_REGS_DATA(PGDDR,0x0f)
INIT_REGS_DATA(PHDDR,0x0f)
INIT_REGS_DATA(PFCR0,0xff)
INIT_REGS_DATA(PFCR2,0x0d)
INIT_REGS_DATA(ITSR, 0x00)
INIT_REGS_DATA(ITSR+1,0x3f)
INIT_REGS_DATA(INTCR,0x20)
		
	.word	0

gpio_table:
	;; P1DDR
	.byte	0x00,0x00
	;; P2DDR
	.byte	0x00,0x00
	;; P3DDR
	.byte	0x00,0x00
	;; dummy
	.byte	0x00,0x00
	;; P5DDR
	.byte	0x00,0x00
	;; P6DDR
	.byte	0x00,0x00
	;; P7DDR
	.byte	0x00,0x00
	;; P8DDR
	.byte	0x00,0x00
	;; dummy
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
	.asciz	"EDOSK-2674"
	
	.section .bss
__command_line:	
	.space	512

	/* interrupt vector */
	.section .vectors,"ax"
	.long	__start
	.long	__start
vector	=	2
	.rept	126
	.long	_interrupt_redirect_table+vector*4
vector	=	vector + 1
	.endr
