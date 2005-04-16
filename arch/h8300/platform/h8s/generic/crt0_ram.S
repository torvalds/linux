/*
 *  linux/arch/h8300/platform/h8s/edosk2674/crt0_ram.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend startup
 *  Target Archtecture:	generic
 *  Memory Layout     :	RAM
 */

#define ASSEMBLY

#include <linux/config.h>
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
	
	.global SYMBOL_NAME(_start)
	.global SYMBOL_NAME(_command_line)
	.global SYMBOL_NAME(_platform_gpio_table)
	.global SYMBOL_NAME(_target_name)
	
	.h8300s

	.section .text
	.file	"crt0_ram.S"

	/* CPU Reset entry */
SYMBOL_NAME_LABEL(_start)
	mov.l	#RAMEND,sp
	ldc	#0x80,ccr
	ldc	#0x00,exr

	/* Peripheral Setup */
	bclr	#4,@INTCR:8	/* interrupt mode 2 */
	bset	#5,@INTCR:8

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
	mov.l	#SYMBOL_NAME(command_line),er6
	mov.w	#512,r4
	eepmov.w

	/* uClinux kernel start */
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
	;;      used,ddr
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

__target_name:	
	.asciz	"generic"
	
	.section .bootvec,"ax"
	jmp	@SYMBOL_NAME(_start)
