#include <linux/init.h>
#include <asm/thread_info.h>

#if defined(CONFIG_CPU_H8300H)
	.h8300h
#define SYSCR 0xfee012
#define IRAMTOP 0xffff20
#define NR_INT 64
#endif
#if defined(CONFIG_CPU_H8S)
	.h8300s
#define INTCR 0xffff31
#define IRAMTOP 0xffc000
#define NR_INT 128
#endif

	__HEAD
	.global	_start
_start:
	mov.l	#IRAMTOP,sp
#if !defined(CONFIG_H8300H_SIM) && \
    !defined(CONFIG_H8S_SIM)
	jsr	@lowlevel_init

	/* copy .data */
	mov.l	#_begin_data,er5
	mov.l	#_sdata,er6
	mov.l	#_edata,er4
	sub.l	er6,er4
	shlr.l	er4
	shlr.l	er4
1:
	mov.l	@er5+,er0
	mov.l	er0,@er6
	adds	#4,er6
	dec.l	#1,er4
	bne	1b
	/* .bss clear */
	mov.l	#_sbss,er5
	mov.l	#_ebss,er4
	sub.l	er5,er4
	shlr	er4
	shlr	er4
	sub.l	er0,er0
1:
	mov.l	er0,@er5
	adds	#4,er5
	dec.l	#1,er4
	bne	1b
#else
	/* get cmdline from gdb */
	jsr	@0xcc
	;; er0 - argc
	;; er1 - argv
	mov.l	#command_line,er3
	adds	#4,er1
	dec.l	#1,er0
	beq	4f
1:
	mov.l	@er1+,er2
2:
	mov.b	@er2+,r4l
	beq	3f
	mov.b	r4l,@er3
	adds	#1,er3
	bra	2b
3:
	mov.b	#' ',r4l
	mov.b	r4l,@er3
	adds	#1,er3
	dec.l	#1,er0
	bne	1b
	subs	#1,er3
	mov.b	#0,r4l
	mov.b	r4l,@er3
4:
#endif
	sub.l	er0,er0
	jsr	@h8300_fdt_init
	/* linux kernel start */
#if defined(CONFIG_CPU_H8300H)
	ldc	#0xd0,ccr	/* running kernel */
	mov.l	#SYSCR,er0
	bclr	#3,@er0
#endif
#if defined(CONFIG_CPU_H8S)
	ldc	#0x07,exr
	bclr	#4,@INTCR:8
	bset	#5,@INTCR:8	/* Interrupt mode 2 */
	ldc	#0x90,ccr	/* running kernel */
#endif
	mov.l	#init_thread_union,sp
	add.l	#0x2000,sp
	jsr	@start_kernel

1:
	bra	1b

#if defined(CONFIG_ROMKERNEL)
	/* interrupt vector */
	.section .vectors,"ax"
	.long	_start
	.long	_start
vector	=	2
	.rept	NR_INT - 2
	.long	_interrupt_redirect_table+vector*4
vector	=	vector + 1
	.endr
#endif
	.end
