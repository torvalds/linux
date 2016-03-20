
#include <linux/sys.h>
#include <linux/init.h>
#include <asm/unistd.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/errno.h>

#if defined(CONFIG_CPU_H8300H)
	.h8300h
#define SYSCR 0xfee012
#define IRAMTOP 0xffff20
#endif
#if defined(CONFIG_CPU_H8S)
	.h8300s
#define INTCR 0xffff31
#define IRAMTOP 0xffc000
#endif

	__HEAD
	.global	_start
_start:
	mov.l	#IRAMTOP,sp
	/* .bss clear */
	mov.l	#_sbss,er5
	mov.l	#_ebss,er4
	sub.l	er5,er4
	shlr	er4
	shlr	er4
	sub.l	er2,er2
1:
	mov.l	er2,@er5
	adds	#4,er5
	dec.l	#1,er4
	bne	1b
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

	.end
