/*
 *  linux/arch/h8300/platform/h8s/edosk2674/crt0_ram.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend startup
 *  Target Archtecture:	EDOSK-2674
 *  Memory Layout     :	RAM
 */

#define ASSEMBLY

#include <asm/linkage.h>
#include <asm/regs267x.h>
			
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
	.global __command_line
	.global __platform_gpio_table
	.global __target_name
	
	.h8300s

	.section .text
	.file	"crt0_ram.S"

	/* CPU Reset entry */
__start:
	mov.l	#RAMEND,sp
	ldc	#0x80,ccr
	ldc	#0x00,exr

	/* Peripheral Setup */
	bclr	#4,@INTCR:8	/* interrupt mode 2 */
	bset	#5,@INTCR:8
	bclr	#0,@IER+1:16
	bset	#1,@ISCRL+1:16	/* IRQ0 Positive Edge */
	bclr	#0,@ISCRL+1:16

#if defined(CONFIG_MTD_UCLINUX)
	/* move romfs image */
	jsr	@__move_romfs	
#endif
	
	/* .bss clear */
	mov.l	#__sbss,er5
	mov.l	er5,er6
	mov.l	#__ebss,er4
	sub.l	er5,er4
	shlr	#2,er4
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
	;;      used,ddr
	.byte	0x00,0x00
	;; P2DDR
	.byte	0x00,0x00
	;; P3DDR
	.byte	0x3f,0x3a
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
	.byte	0xff,0xff
	;; PBDDR
	.byte	0xff,0x00
	;; PCDDR
	.byte	0xff,0x00
	;; PDDDR
	.byte	0xff,0x00
	;; PEDDR
	.byte	0xff,0x00
	;; PFDDR
	.byte	0xff,0xff
	;; PGDDR
	.byte	0x0f,0x0f
	;; PHDDR
	.byte	0x0f,0x0f

__target_name:	
	.asciz	"EDOSK-2674"
	
	.section .bootvec,"ax"
	jmp	@__start
